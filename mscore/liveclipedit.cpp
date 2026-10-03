//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveclipedit.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

#include "libmscore/liveclips.h"
#include "libmscore/score.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/undo.h"
#include "liveclips.h"
#include "musescore.h"
#include "seq.h"

namespace Ms {
namespace LiveIntegration {

using namespace LiveClipEdit;

static const char* const SETTING = "liveIntegration/editClips";
static const char* const PLAY_SETTING = "liveIntegration/clipTabsPlayLive";
static constexpr int DEBOUNCE_MS  = 300;
static constexpr int CONFIRM_MS   = 3000;
static constexpr int MAX_TRIES    = 3;

static void log(const QString& s)
      {
      static const bool on = qEnvironmentVariableIsSet("MS_LIVE_LOG");
      if (on)
            qInfo("%.3f LiveClipEdit: %s", double(QDateTime::currentMSecsSinceEpoch() % 100000) / 1000.0, qPrintable(s));
      }

LiveClipEditor* LiveClipEditor::instance()
      {
      static LiveClipEditor* g = nullptr;
      if (!g)
            g = new LiveClipEditor;
      return g;
      }

bool LiveClipEditor::enabledSetting()
      {
      return QSettings().value(SETTING, true).toBool();
      }

void LiveClipEditor::setEnabledSetting(bool on)
      {
      QSettings().setValue(SETTING, on);
      LiveClipsLink::instance()->updateSocket();
      }

bool LiveClipEditor::playLiveSetting()
      {
      return QSettings().value(PLAY_SETTING, true).toBool();
      }

void LiveClipEditor::setPlayLiveSetting(bool on)
      {
      QSettings().setValue(PLAY_SETTING, on);
      instance()->updateRouting();
      instance()->updateStatus();
      }

//---------------------------------------------------------
//   playback through the clip's Live track
//---------------------------------------------------------

int LiveClipEditor::liveTrack(const MasterScore* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      if (!s || !s->trackId || !s->copy || !playLiveSetting())
            return 0;
      const LiveClipsLink* link = LiveClipsLink::instance();
      if (!link->deviceAnswers() || link->deviceProtocol() < MIDI_PROTOCOL)
            return 0;
      return s->trackId;
      }

void LiveClipEditor::updateRouting()
      {
      const int track = liveTrack(_current);
      if (seq)
            seq->setLiveTrack(track ? _current.data() : nullptr, track);
      if (track != _routed) {
            log(QString("playback: %1").arg(track ? QString("through Live track %1").arg(track) : QString("MuseScore's own sounds")));
            _routed = track;
            updateStatus();
            }
      }

void LiveClipEditor::linkChanged()
      {
      if (LiveClipsLink::instance()->deviceAnswers()) {
            for (auto& e : _sessions)           // (unanswered while the link was down: written again now)
                  if (e.second.state == State::NO_ANSWER && e.second.score) {
                        e.second.state = State::SYNC;
                        e.second.inFlight = false;
                        _dirty.insert(e.first);
                        _debounce->start();
                        }
            }
      updateRouting();
      if (sessionOf(_current))
            updateStatus();
      }

void LiveClipEditor::countTabs(int* tabs, int* throughLive) const
      {
      *tabs = 0;
      *throughLive = 0;
      for (const auto& s : _sessions) {
            if (!s.second.score)
                  continue;
            ++*tabs;
            if (s.second.trackId && s.second.copy && playLiveSetting())
                  ++*throughLive;
            }
      }

// a new hub (the copy that was it went): it gets each clip edited here, and what was edited meanwhile
void LiveClipEditor::newDevice()
      {
      for (auto& e : _sessions) {
            Session& s = e.second;
            if (!s.score)
                  continue;
            send(LiveClips::osc("/ms/clip/adopt", { e.first, s.liveHash }));
            log(QString("clip %1 handed to the new hub").arg(e.first));
            if (s.state == State::SENDING || s.state == State::NO_ANSWER || s.state == State::GONE) {
                  s.inFlight = false;
                  s.state = State::SYNC;
                  s.pending = Diff();
                  }
            if (s.state == State::SYNC) {
                  _dirty.insert(e.first);
                  _debounce->start();
                  }
            }
      updateStatus();
      }

LiveClipEditor::LiveClipEditor()
      {
      _debounce = new QTimer(this);
      _debounce->setSingleShot(true);
      _debounce->setInterval(DEBOUNCE_MS);
      connect(_debounce, &QTimer::timeout, this, &LiveClipEditor::flush);
      _poll = new QTimer(this);
      _poll->setInterval(500);
      connect(_poll, &QTimer::timeout, this, &LiveClipEditor::poll);
      _poll->start();
      }

LiveClipEditor::Session* LiveClipEditor::sessionOf(const Score* score)
      {
      if (!score)
            return nullptr;
      for (auto& s : _sessions)
            if (s.second.score && s.second.score == score->masterScore())
                  return &s.second;
      return nullptr;
      }

bool LiveClipEditor::isClipScore(const Score* score) const
      {
      return const_cast<LiveClipEditor*>(this)->sessionOf(score) != nullptr;
      }

bool LiveClipEditor::unsavedClipScore(const MasterScore* score) const
      {
      return _replaced.count(score) || (isClipScore(score) && score->created());
      }

void LiveClipEditor::send(const QByteArray& p)
      {
      LiveClipsLink::instance()->sendDatagram(p);
      }

//---------------------------------------------------------
//   the device's messages
//---------------------------------------------------------

void LiveClipEditor::received(const QString& address, const QVariantList& args)
      {
      const QString key = args.value(0).toString();
      if (address == "/live/clip/begin") {
            Clip c;
            c.key = key;
            c.gen = args.value(1).toInt();
            c.track = args.value(2).toString();
            c.name = args.value(3).toString();
            c.drums = args.value(4).toInt() != 0;
            c.bpm = args.value(5).toDouble();
            c.num = args.value(6).toInt();
            c.den = args.value(7).toInt();
            c.end = args.value(8).toDouble();
            c.loopStart = args.value(9).toDouble();
            c.loopEnd = args.value(10).toDouble();
            c.looping = args.value(11).toInt() != 0;
            c.count = args.value(12).toInt();
            c.chunks = args.value(13).toInt();
            c.hash = args.value(14).toInt();
            log(QString("clip %1 (%2 › %3): %4 notes coming").arg(key, c.track, c.name).arg(c.count));
            if (c.complete())
                  opened(c);
            else
                  _incoming[key] = c;
            }
      else if (address == "/live/clip/notes") {
            auto it = _incoming.find(key);
            if (it == _incoming.end() || it->second.gen != args.value(1).toInt())
                  return;
            Clip& c = it->second;
            for (int i = 3; i + 8 < args.size(); i += 9) {
                  LiveNote n;
                  n.id = args[i].toInt();
                  n.pitch = args[i + 1].toInt();
                  n.start = args[i + 2].toDouble();
                  n.duration = args[i + 3].toDouble();
                  n.velocity = args[i + 4].toDouble();
                  n.mute = args[i + 5].toInt() != 0;
                  n.probability = args[i + 6].toDouble();
                  n.velocityDeviation = args[i + 7].toDouble();
                  n.releaseVelocity = args[i + 8].toDouble();
                  c.notes.push_back(n);
                  }
            ++c.got;
            if (c.complete()) {
                  const Clip done = c;
                  _incoming.erase(it);
                  opened(done);
                  }
            }
      else if (address == "/live/clip/written") {
            std::vector<int> ids;
            for (int i = 4; i < args.size(); ++i)
                  ids.push_back(args[i].toInt());
            written(key, args.value(1).toInt(), args.value(2).toString(), args.value(3).toInt(), ids);
            }
      else if (address == "/live/clip/conflict") {
            auto it = _sessions.find(key);
            if (it != _sessions.end() && it->second.state != State::RELOADING) {
                  it->second.state = State::CONFLICT;
                  it->second.inFlight = false;
                  log(QString("clip %1 changed in Live: no more writes until it is reloaded").arg(key));
                  updateStatus();
                  }
            }
      else if (address == "/live/clip/gone") {
            auto it = _sessions.find(key);
            if (it != _sessions.end()) {
                  if (it->second.state != State::GONE)
                        LiveClipsLink::instance()->notice(tr("The Live clip %1 › %2 is gone (deleted in Live): its tab is no "
                                                             "longer linked; Save As keeps it as a score.")
                                                          .arg(it->second.clip.track, it->second.clip.name));
                  it->second.state = State::GONE;
                  it->second.inFlight = false;
                  it->second.copy = false;
                  updateRouting();
                  updateStatus();
                  }
            }
      else if (address == "/live/clip/track") {
            auto it = _sessions.find(key);
            if (it == _sessions.end())
                  return;
            Session& s = it->second;
            const int trackId = args.value(1).toInt();
            const bool copy = args.value(2).toInt() != 0;
            if (!args.value(3).toString().isEmpty())
                  s.clip.track = args.value(3).toString();
            if (s.copyWas && !copy && playLiveSetting())
                  LiveClipsLink::instance()->notice(tr("MuseScore Link was removed from the Live track %1: the clip tab %2 plays "
                                                       "MuseScore's own sounds until the device is on that track again.")
                                                    .arg(s.clip.track, s.clip.name));
            else if (!s.copyWas && copy && s.trackId == trackId && s.trackId && playLiveSetting())
                  LiveClipsLink::instance()->notice(tr("The clip tab %1 now plays through the Live track %2.")
                                                    .arg(s.clip.name, s.clip.track), true);
            s.trackId = trackId;
            s.copy = copy;
            s.copyWas = copy;
            log(QString("clip %1: track %2, %3").arg(key).arg(trackId).arg(copy ? "MuseScore Link on it" : "no MuseScore Link on it"));
            updateRouting();
            updateStatus();
            }
      }

// a clip read from Live: a new tab, the tab it has (unchanged), or the tab replaced (read again)
void LiveClipEditor::opened(Clip clip)
      {
      if (!mscore)
            return;
      auto it = _sessions.find(clip.key);
      MasterScore* old = it != _sessions.end() ? it->second.score.data() : nullptr;
      if (old && it->second.state != State::RELOADING && it->second.liveHash == clip.hash
          && it->second.state != State::CONFLICT) {
            const int idx = mscore->scores().indexOf(old);
            if (idx >= 0)
                  mscore->setCurrentScoreView(idx);
            return;
            }
      QString error;
      MasterScore* score = importClip(clip, &error);
      if (!score) {
            mscore->showMessage(tr("Live clip %1 could not be opened: %2").arg(clip.name, error), 5000);
            send(LiveClips::osc("/ms/clip/close", { clip.key }));
            return;
            }
      if (synti)
            score->setSynthesizerState(synti->state());
      score->updateExpressive(MuseScore::synthesizer("Fluid"));
      Session s;
      s.clip = clip;
      s.score = score;
      s.base = match(clip, score);
      applyMutes(s.base, score);             // (Live's muted notes don't play here either)
      s.liveHash = clip.hash;
      if (it != _sessions.end()) {
            s.write = it->second.write;          // (write numbers go on: the device may remember the last)
            s.trackId = it->second.trackId;      // (the device says it again after the notes)
            s.copy = it->second.copy;
            s.copyWas = it->second.copyWas;
            }
      _sessions[clip.key] = s;
      connect(score, &Score::playlistChanged, this, [this, score]() { scoreChanged(score); });
      log(QString("clip %1 opened: %2 notation notes, %3 Live notes not shown, %4 outside the clip")
          .arg(clip.key).arg(s.base.entries.size()).arg(s.base.unmatched).arg(s.base.outside));
      const int idx = mscore->appendScore(score);
      mscore->setCurrentScoreView(idx);
      if (old) {                                  // read again: the old tab goes (no question: it is replaced)
            const int oldIdx = mscore->scores().indexOf(old);
            if (oldIdx >= 0) {
                  _replaced.insert(old);
                  QMetaObject::invokeMethod(mscore, "removeTab", Qt::DirectConnection, Q_ARG(int, oldIdx));
                  _replaced.erase(old);
                  }
            }
      updateStatus();
      }

//---------------------------------------------------------
//   edits
//---------------------------------------------------------

void LiveClipEditor::scoreChanged(MasterScore* score)
      {
      for (auto& s : _sessions)
            if (s.second.score == score) {
                  _dirty.insert(s.first);
                  _debounce->start();
                  }
      }

void LiveClipEditor::flush()
      {
      const std::set<QString> keys = _dirty;
      _dirty.clear();
      for (const QString& key : keys)
            write(key);
      }

void LiveClipEditor::write(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !it->second.score)
            return;
      Session& s = it->second;
      if (s.state != State::SYNC && s.state != State::SENDING)
            return;                             // (conflict, gone, reloading: nothing is written)
      if (s.inFlight) {
            s.changedMeanwhile = true;
            return;
            }
      if (s.score->undoStack()->active()) {     // in the middle of a command
            _dirty.insert(key);
            _debounce->start();
            return;
            }
      Diff d = diff(s.base, signatures(s.score));
      if (d.empty()) {
            s.state = State::SYNC;
            updateStatus();
            return;
            }
      s.pending = d;
      ++s.write;
      s.packets = writePackets(key, s.write, d.ops);
      s.inFlight = true;
      s.tries = 1;
      s.sentAt = QDateTime::currentMSecsSinceEpoch();
      s.state = State::SENDING;
      log(QString("clip %1 write %2: %3 modified, %4 removed, %5 added")
          .arg(key).arg(s.write).arg(d.modified).arg(d.removed).arg(d.added.size()));
      for (const QByteArray& p : s.packets)
            send(p);
      updateStatus();
      }

void LiveClipEditor::written(const QString& key, int writeNo, const QString& status, qint32 hash, const std::vector<int>& ids)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end())
            return;
      Session& s = it->second;
      if (!s.inFlight || writeNo != s.write)
            return;
      s.inFlight = false;
      log(QString("clip %1 write %2: %3").arg(key).arg(writeNo).arg(status));
      if (status == "ok") {
            if (!setAddedIds(s.pending, ids)) {
                  s.state = State::FAILED;
                  s.error = tr("Live gave %1 id(s) for %2 added note(s): reload the clip").arg(ids.size()).arg(s.pending.added.size());
                  updateStatus();
                  return;
                  }
            s.base = s.pending.next;
            s.liveHash = hash;
            ++s.writes;
            s.notesChanged += int(s.pending.ops.size());
            s.state = State::SYNC;
            s.pending = Diff();
            if (s.changedMeanwhile) {
                  s.changedMeanwhile = false;
                  write(key);
                  }
            }
      else if (status == "conflict")
            s.state = State::CONFLICT;
      else if (status == "gone")
            s.state = State::GONE;
      else {
            s.state = State::FAILED;
            s.error = status;
            }
      updateStatus();
      }

void LiveClipEditor::poll()
      {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      for (auto& e : _sessions) {
            Session& s = e.second;
            if (!s.inFlight || now - s.sentAt < CONFIRM_MS)
                  continue;
            if (s.tries >= MAX_TRIES) {
                  s.inFlight = false;
                  s.state = State::NO_ANSWER;
                  updateStatus();
                  continue;
                  }
            ++s.tries;
            s.sentAt = now;
            for (const QByteArray& p : s.packets)       // (the same write number: the device applies it once)
                  send(p);
            }
      }

void LiveClipEditor::reload(MasterScore* score)
      {
      Session* s = sessionOf(score);
      if (!s)
            return;
      s->state = State::RELOADING;
      s->inFlight = false;
      send(LiveClips::osc("/ms/clip/reload", { s->clip.key }));
      updateStatus();
      }

void LiveClipEditor::scoreClosed(MasterScore* score)
      {
      for (auto it = _sessions.begin(); it != _sessions.end(); ++it) {
            if (it->second.score != score)
                  continue;
            log(QString("clip %1: its tab closed, editing ends").arg(it->first));
            send(LiveClips::osc("/ms/clip/close", { it->first }));
            _dirty.erase(it->first);
            _sessions.erase(it);
            break;
            }
      if (_current == score)
            _current = nullptr;
      updateRouting();
      updateStatus();
      }

//---------------------------------------------------------
//   status
//---------------------------------------------------------

LiveClipEditor::State LiveClipEditor::state(const MasterScore* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      return s ? s->state : State::SYNC;
      }

QString LiveClipEditor::statusText(const MasterScore* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      if (!s)
            return QString();
      QString what = tr("Editing Live clip %1 › %2").arg(s->clip.track, s->clip.name);
      QString state;
      switch (s->state) {
            case State::SYNC:
                  state = s->notesChanged ? tr("in sync (%n note change(s) sent)", "", s->notesChanged) : tr("in sync");
                  break;
            case State::SENDING:
                  state = tr("%n change(s) being sent", "", int(s->pending.ops.size()));
                  break;
            case State::CONFLICT:
                  state = tr("conflict: the clip changed in Live; nothing more is written until you reload it");
                  break;
            case State::RELOADING:
                  state = tr("reading the clip from Live…");
                  break;
            case State::GONE:
                  state = tr("the clip is gone from Live");
                  break;
            case State::NO_ANSWER:
                  state = tr("Live doesn't answer (is the MuseScore Link device loaded?)");
                  break;
            case State::FAILED:
                  state = s->error;
                  break;
            }
      QString text = what + ": " + state;
      if (s->base.unmatched || s->base.outside)
            text += " " + tr("(%n Live note(s) not shown here are left as they are)", "", s->base.unmatched + s->base.outside);
      // playback
      const LiveClipsLink* link = LiveClipsLink::instance();
      if (!playLiveSetting())
            return text;                        // (MuseScore's own sounds, as asked)
      if (!link->deviceAnswers())
            text += " · " + tr("the connection to Live is lost: MuseScore's own sounds (reconnects by itself)");
      else if (liveTrack(score))
            text += " · " + tr("plays through Live's track %1").arg(s->clip.track);
      else if (link->deviceProtocol() < MIDI_PROTOCOL || !s->trackId)
            text += " · " + tr("MuseScore's own sounds: the MuseScore Link device in Live is older (update it to play "
                               "through the track)");
      else if (!s->copy)
            text += " · " + tr("add MuseScore Link to the Live track %1 to hear it there (MuseScore's own sounds meanwhile)")
                    .arg(s->clip.track);
      return text;
      }

void LiveClipEditor::setCurrentScore(MasterScore* score)
      {
      _current = score;
      updateRouting();
      updateStatus();
      }

void LiveClipEditor::updateStatus()
      {
      emit statusChanged();
      if (!mscore || MScore::noGui)
            return;
      if (!_status) {
            _status = new QWidget;
            QHBoxLayout* h = new QHBoxLayout(_status);
            h->setContentsMargins(4, 0, 4, 0);
            _statusLabel = new QLabel(_status);
            _reload = new QPushButton(tr("Reload from Live"), _status);
            _reload->setToolTip(tr("Read the clip from Live again, as it is there now. Edits made here and not yet in "
                                   "Live are lost."));
            h->addWidget(_statusLabel);
            h->addWidget(_reload);
            connect(_reload, &QPushButton::clicked, this, [this]() { reload(_current); });
            mscore->statusBar()->addPermanentWidget(_status);
            }
      const Session* s = sessionOf(_current);
      _status->setVisible(s != nullptr);
      if (!s)
            return;
      _statusLabel->setText(statusText(_current));
      _reload->setVisible(s->state == State::CONFLICT || s->state == State::FAILED || s->state == State::NO_ANSWER);
      }

}     // namespace LiveIntegration
}     // namespace Ms
