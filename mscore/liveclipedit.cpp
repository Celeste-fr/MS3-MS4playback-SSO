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
#include "elidedlabel.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QDir>
#include <QtConcurrent>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>

#include "libmscore/automation.h"
#include "libmscore/liveclips.h"
#include "libmscore/part.h"
#include "libmscore/score.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/undo.h"
#include "liveclips.h"
#include "livehelpers.h"
#include "liveintegration.h"
#include "musescore.h"
#include "seq.h"

namespace Ms {
namespace LiveIntegration {

using namespace LiveClipEdit;

static const char* const SETTING = "liveIntegration/editClips";
static const char* const PLAY_SETTING = "liveIntegration/clipTabsPlayLive";
static constexpr int DEBOUNCE_MS  = 300;
static constexpr int CONFIRM_MS   = 3800;         // (measured: liveclips.cpp CONFIRM_MS)
static constexpr int AUDIBLE_BEAT_MS = 1000;      // (the device restores after 7 s without one: AUDIBLE_STALE_MS)
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
      return true;                      // always on since 2026-10-07 (the owner: no reason to turn it off)
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
      updateAudible();
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
      _audibleBeat = new QTimer(this);
      _audibleBeat->setInterval(AUDIBLE_BEAT_MS);
      connect(_audibleBeat, &QTimer::timeout, this, &LiveClipEditor::audibleBeat);
      if (qApp)                                 // (quitting while playing: Live's mute and solo as they were)
            connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() { setAudible(0); });
      _setWatch = new QFileSystemWatcher(this);
      _setSettle = new QTimer(this);
      _setSettle->setSingleShot(true);
      _setSettle->setInterval(1500);            // (Live writes the file in steps: as LiveIntegration::Watcher)
      connect(_setWatch, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
            if (!_setsChanged.contains(path))
                  _setsChanged << path;
            _setSettle->start();
            });
      connect(_setSettle, &QTimer::timeout, this, &LiveClipEditor::setsSaved);
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

static std::function<void(const QByteArray&)> sendHook;

void LiveClipEditor::setSendHook(std::function<void(const QByteArray&)> hook)
      {
      sendHook = hook;
      }

void LiveClipEditor::send(const QByteArray& p)
      {
      if (sendHook)
            sendHook(p);
      LiveClipsLink::instance()->sendDatagram(p);
      }

//---------------------------------------------------------
//   the clip's track audible while MuseScore plays through it (the owner, 2026-10-03, option A: "as long as it
//   returns to the previous state after MuseScore stops playing"): /ms/cliptab/audible 1 <track> at Play and each
//   second while playing (the device's heartbeat: silent 4 s, it restores by itself), 0 <track> at Stop, when the
//   tab or its routing changes, at quit. The device un-mutes (and solos) and puts back (MuseScoreLink.js › audible).
//---------------------------------------------------------

void LiveClipEditor::setAudible(int track)
      {
      if (track == _audible)
            return;
      if (_audible) {
            log(QString("playback: Live track %1 as it was").arg(_audible));
            send(LiveClips::osc("/ms/cliptab/audible", { 0, _audible }));
            }
      _audible = track;
      if (_audible) {
            log(QString("playback: Live track %1 audible").arg(_audible));
            send(LiveClips::osc("/ms/cliptab/audible", { 1, _audible }));
            _audibleBeat->start();
            }
      else
            _audibleBeat->stop();
      }

void LiveClipEditor::audibleBeat()
      {
      if (_audible)
            send(LiveClips::osc("/ms/cliptab/audible", { 1, _audible }));
      }

void LiveClipEditor::updateAudible()
      {
      if (seq && !_seqConnected) {
            _seqConnected = true;
            connect(seq, &Seq::started, this, [this]() { _playing = true; updateAudible(); });
            connect(seq, &Seq::stopped, this, [this]() { _playing = false; updateAudible(); });
            }
      setAudible(_playing && _routed > 0 ? _routed : 0);
      }

void LiveClipEditor::sendEnv(const QByteArray& p)
      {
      LiveClipsLink::instance()->sendDatagramTo(p, ENV_PORT);
      }

//---------------------------------------------------------
//   automation lanes: the clip's envelopes
//---------------------------------------------------------

const std::vector<LiveParam>* LiveClipEditor::liveParams(const Score* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      if (!s || s->env != EnvState::READY)
            return nullptr;
      return TrackParams::instance()->params(s->clip.key);
      }

QString LiveClipEditor::clipKey(const Score* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      return s ? s->clip.key : QString();
      }

LiveClipEditor::EnvState LiveClipEditor::envState(const MasterScore* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      return s ? s->env : EnvState::NONE;
      }

void LiveClipEditor::paramsChanged(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end())
            return;
      log(QString("clip %1: the track's parameters (%2)").arg(key)
          .arg(TrackParams::instance()->params(key) ? int(TrackParams::instance()->params(key)->size()) : 0));
      if (it->second.score)
            it->second.score->update();
      updateStatus();
      }

void LiveClipEditor::readEnvelopes(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end())
            return;
      Session& s = it->second;
      s.env = EnvState::READING;
      s.envIncoming.clear();
      s.envChunks.clear();
      s.envExpected = -1;
      s.envInFlight = false;
      s.envSentAt = QDateTime::currentMSecsSinceEpoch();
      sendEnv(LiveClips::osc("/ms/env/read", { key, s.envTrack, s.envSlot }));
      }

// the envelopes read: the score's lanes on Live parameters are Live's now (no undo step: as the notes were imported)
void LiveClipEditor::envelopesRead(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !it->second.score)
            return;
      Session& s = it->second;
      MasterScore* score = s.score;
      if (score->parts().empty())
            return;
      const Part* part = score->parts().front();
      std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
      Automation::PartLanes keep;
      for (const Automation::Lane& l : all[part])
            if (!parseLiveTarget(l.target, nullptr, nullptr))
                  keep.push_back(l);
      s.envSent.clear();
      for (const auto& in : s.envIncoming) {
            std::vector<std::pair<int, double>> ev;
            for (const auto& c : in.second)
                  ev.insert(ev.end(), c.second.begin(), c.second.end());
            Automation::Lane l;
            l.target = liveTarget(in.first.first, in.first.second);
            l.points = lanePoints(ev);
            if (l.points.empty())
                  continue;
            const LiveParam* p = TrackParams::instance()->param(key, l.target);
            if (p)
                  l.extra["name"] = p->name;
            s.envSent[l.target] = Automation::pointsHash(l.points);
            keep.push_back(l);
            }
      all[part] = keep;
      const QString tag = Automation::write(score, all);
      if (tag != score->metaTag(Automation::metaTag)) {
            if (seq)
                  seq->waitForRendering();
            score->setMetaTag(Automation::metaTag, tag);
            score->setPlaylistDirty();
            }
      s.envIncoming.clear();
      s.envChunks.clear();
      s.env = EnvState::READY;
      log(QString("clip %1: %2 envelope(s) read").arg(key).arg(s.envSent.size()));
      score->update();
      updateStatus();
      }

void LiveClipEditor::writeEnvelopes(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !it->second.score)
            return;
      Session& s = it->second;
      if (s.env != EnvState::READY || s.state == State::CONFLICT || s.state == State::GONE || s.state == State::RELOADING)
            return;
      if (s.envInFlight) {
            s.envChangedMeanwhile = true;
            return;
            }
      if (s.score->undoStack()->active())
            return;                             // (write() asks again after the command)
      MasterScore* score = s.score;
      if (score->parts().empty())
            return;
      const std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
      auto pl = all.find(score->parts().front());
      std::map<QString, const Automation::Lane*> now;
      if (pl != all.end())
            for (const Automation::Lane& l : pl->second)
                  if (!l.points.empty() && parseLiveTarget(l.target, nullptr, nullptr))
                        now[l.target] = &l;
      std::vector<EnvLane> lanes;
      std::map<QString, QString> next;
      for (const auto& n : now) {
            const QString h = Automation::pointsHash(n.second->points);
            next[n.first] = h;
            auto was = s.envSent.find(n.first);
            if (was != s.envSent.end() && was->second == h)
                  continue;
            EnvLane e;
            parseLiveTarget(n.first, &e.d, &e.p);
            e.events = envelopeEvents(n.second->points);
            lanes.push_back(e);
            }
      for (const auto& was : s.envSent)
            if (!now.count(was.first)) {
                  EnvLane e;                    // (cleared)
                  parseLiveTarget(was.first, &e.d, &e.p);
                  lanes.push_back(e);
                  }
      if (lanes.empty())
            return;
      s.envPending = next;
      ++s.envWrite;
      s.envPackets = envWritePackets(key, s.envWrite, s.envTrack, s.envSlot, s.envHash, lanes);
      s.envInFlight = true;
      s.envTries = 1;
      s.envSentAt = QDateTime::currentMSecsSinceEpoch();
      log(QString("clip %1 envelope write %2: %3 lane(s)").arg(key).arg(s.envWrite).arg(lanes.size()));
      for (const QByteArray& p : s.envPackets)
            sendEnv(p);
      updateStatus();
      }

void LiveClipEditor::envWritten(const QString& key, int writeNo, const QString& status, qint32 hash)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end())
            return;
      Session& s = it->second;
      if (!s.envInFlight || writeNo != s.envWrite)
            return;
      s.envInFlight = false;
      log(QString("clip %1 envelope write %2: %3").arg(key).arg(writeNo).arg(status));
      if (status == "ok") {
            s.envSent = s.envPending;
            s.envHash = hash;
            ++s.envWrites;
            if (s.envChangedMeanwhile) {
                  s.envChangedMeanwhile = false;
                  writeEnvelopes(key);
                  }
            }
      else if (status == "conflict") {
            s.envHash = hash;
            s.state = State::CONFLICT;
            s.inFlight = false;
            }
      else if (status == "gone")
            s.state = State::GONE;
      else {
            s.env = EnvState::FAILED;
            s.envError = status;
            }
      updateStatus();
      }

QString LiveClipEditor::envText(const MasterScore* score, QString* details) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      if (!s)
            return QString();
      QString more;
      QString text;
      switch (s->env) {
            case EnvState::NONE:
                  if (LiveClipsLink::instance()->deviceProtocol() < PARAMS_PROTOCOL) {
                        text = tr("no lanes (update MuseScore Link)");
                        more = tr("Automation lanes need a newer MuseScore Link device in Live: update it.");
                        }
                  break;
            case EnvState::READING:
                  text = tr("reading envelopes…");
                  break;
            case EnvState::READY:
                  if (!TrackParams::instance()->params(s->clip.key))
                        text = tr("waiting for the track's parameters");
                  else if (s->envInFlight)
                        text = tr("writing envelopes…");
                  else if (s->envWrites)
                        text = tr("envelopes in sync");
                  break;
            case EnvState::ARRANGEMENT:
                  text = tr("no lanes (arrangement clip)");
                  more = tr("No automation lanes: an arrangement clip has no envelopes Live's API can reach (a session "
                            "clip has).");
                  break;
            case EnvState::NO_SCRIPT:
                  text = tr("no lanes (MuseScore Envelopes not set up)");
                  more = tr("No automation lanes: the MuseScore Envelopes control surface doesn't answer. Install it in "
                            "Live and choose it in Settings › Tempo & MIDI › Control Surface (LIVE.md).");
                  break;
            case EnvState::FAILED:
                  text = tr("envelopes failed");
                  more = tr("Envelopes: %1").arg(s->envError);
                  break;
            }
      if (details)
            *details = more;
      return text;
      }

//---------------------------------------------------------
//   the Velocity lane (liveclipmodel.h): its record in the MuseScore Link device
//---------------------------------------------------------

// a record's atoms as text: two records alike are the same text (doubles as the device keeps them, float32: 6 digits)
static QString velRecordKey(const QVariantList& atoms)
      {
      QStringList l;
      for (const QVariant& a : atoms)
            l << (int(a.type()) == QMetaType::Double || int(a.type()) == QMetaType::Float ? QString::number(a.toDouble(), 'g', 6)
                                                                                          : a.toString());
      return l.join(' ');
      }

void LiveClipEditor::askVelocity(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end())
            return;
      Session& s = it->second;
      s.velAsked = true;
      s.velRead = false;
      s.velIncoming.clear();
      s.velSentAt = QDateTime::currentMSecsSinceEpoch();
      s.velTries = 1;
      send(LiveClips::osc("/ms/vel/ask", { key }));
      }

// the record read back: the lane in the score as the device keeps it (no undo step: as the notes were imported), and in
// "write" each note still at the velocity written gets its original
void LiveClipEditor::velocityRead(const QString& key, bool found, const QVariantList& atoms)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !it->second.score)
            return;
      Session& s = it->second;
      s.velRead = true;
      VelocityLane v;
      std::vector<Original> orig;
      if (!found || !parseVelRecord(atoms, &v, &orig)) {
            log(QString("clip %1: no velocity curve kept").arg(key));
            updateStatus();
            return;
            }
      MasterScore* score = s.score;
      if (score->parts().empty())
            return;
      const Part* part = score->parts().front();
      std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
      Automation::PartLanes keep;
      for (const Automation::Lane& l : all[part])
            if (l.target != VELOCITY_TARGET)
                  keep.push_back(l);
      if (v.present)
            keep.push_back(v.lane);
      all[part] = keep;
      const QString tag = Automation::write(score, all);
      if (tag != score->metaTag(Automation::metaTag)) {
            if (seq)
                  seq->waitForRendering();
            score->setMetaTag(Automation::metaTag, tag);
            score->setPlaylistDirty();
            }
      const int n = applyOriginals(s.base, score, orig);
      s.velSent = velRecordKey(velRecord(v, orig));
      log(QString("clip %1: velocity curve read (%2 point(s), %3 original(s), %4 note(s) back to theirs)")
          .arg(key).arg(v.lane.points.size()).arg(orig.size()).arg(n));
      score->update();
      updateStatus();
      }

// the record to the device when it changed (a lane edit, its mode, the originals after a write)
void LiveClipEditor::sendVelocity(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !it->second.score)
            return;
      Session& s = it->second;
      if (LiveClipsLink::instance()->deviceProtocol() < VEL_PROTOCOL || !s.velRead
          || s.state == State::CONFLICT || s.state == State::GONE || s.state == State::RELOADING)
            return;
      if (s.velInFlight || s.inFlight || s.score->undoStack()->active()) {
            s.velAfterWrite = true;             // (once that is confirmed: the originals as Live has the notes then)
            return;
            }
      s.velAfterWrite = false;
      const VelocityLane v = velocityLane(s.score);
      const std::vector<Original> orig = originals(s.base, s.score);
      if (!v.present && orig.empty() && s.velSent.isEmpty())
            return;                             // (never had one)
      const QVariantList atoms = velRecord(v, orig);
      const QString k = velRecordKey(atoms);
      if (k == s.velSent)
            return;
      s.velPending = k;
      ++s.velSerial;
      s.velPackets = velSetPackets(key, s.velSerial, atoms);
      s.velInFlight = true;
      s.velTries = 1;
      s.velSentAt = QDateTime::currentMSecsSinceEpoch();
      log(QString("clip %1: velocity curve %2 sent (%3 point(s), %4 original(s))").arg(key).arg(s.velSerial)
          .arg(v.present ? int(v.lane.points.size()) : 0).arg(orig.size()));
      for (const QByteArray& p : s.velPackets)
            send(p);
      updateStatus();
      }

bool LiveClipEditor::confirmVelocityWrite(QWidget* parent)
      {
      static const char* const NO_ASK = "liveIntegration/velocityWriteNoAsk";
      QSettings st;
      if (st.value(NO_ASK, false).toBool())
            return true;
      QMessageBox box(QMessageBox::Question, tr("Write velocities into the Live clip"),
                      tr("The Velocity lane's curve will be written into the velocities of the clip's notes in Live.\n\n"
                         "Their velocities now are kept (in MuseScore Link, with the Live set), so the curve can be changed "
                         "or set back to \"Shape while playing\" later: the notes then get them back."),
                      QMessageBox::Cancel, parent);
      QPushButton* write = box.addButton(tr("Write"), QMessageBox::AcceptRole);
      box.setDefaultButton(write);
      QCheckBox* again = new QCheckBox(tr("Don't ask again"));
      box.setCheckBox(again);
      box.exec();
      if (box.clickedButton() != write)
            return false;
      if (again->isChecked())
            st.setValue(NO_ASK, true);
      return true;
      }

QString LiveClipEditor::velocityText(const MasterScore* score, QString* details) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      if (!s || !s->score)
            return QString();
      const VelocityLane v = velocityLane(s->score);
      if (!v.present && s->velSent.isEmpty())
            return QString();
      QString text, more;
      const bool old = LiveClipsLink::instance()->deviceProtocol() < VEL_PROTOCOL;
      if (v.output == VelOutput::WRITE) {
            text = tr("velocity written");
            if (old)
                  more = tr("Velocity: written into the notes; the notes' velocities before the curve are not kept (the "
                            "MuseScore Link device in Live is older: update it).");
            else if (!s->velKept)
                  more = tr("Velocity: written into the notes; the notes' velocities before the curve are kept only while "
                            "Live runs (set up the MuseScore Envelopes control surface to keep them with the set).");
            }
      else {
            if (old) {
                  text = tr("velocity: MuseScore only (update MuseScore Link)");
                  more = tr("Velocity: only MuseScore's own playback follows the curve: the MuseScore Link device in Live is "
                            "older (update it).");
                  }
            else if (!s->velStatus.isEmpty() && s->velStatus != "ok") {
                  text = tr("velocity not shaped in Live");
                  more = tr("Velocity: %1").arg(s->velStatus);
                  }
            else {
                  text = tr("velocity shaped in Live");
                  if (!s->velKept && !s->velSent.isEmpty())
                        more = tr("Velocity: shaped while Live runs, not kept with the set (set up the MuseScore Envelopes "
                                  "control surface).");
                  }
            }
      if (s->velInFlight)
            text = tr("sending velocity…");
      if (details)
            *details = more;
      return text;
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
            // identified by number: a packet Live's link delivered twice has its notes in already. No timeout: a
            // clip that never completes is replaced by the next /live/clip/begin for its key (Edit in MuseScore again)
            const int chunk = args.value(2).toInt();
            if (c.got.count(chunk))
                  return;
            c.got.insert(chunk);
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
      else if (address == "/live/clip/where") {
            auto it = _sessions.find(key);
            if (it == _sessions.end())
                  return;
            Session& s = it->second;
            const int track = args.value(1).toInt();
            const int slot = args.value(2).toInt();
            const bool moved = s.envTrack != track || s.envSlot != slot;
            s.envTrack = track;
            s.envSlot = slot;
            s.place = slot < 0 || track < 0 ? ARRANGEMENT : slot;
            retitle(s);                         // (an unnamed clip: named by its place)
            log(QString("clip %1: track %2, %3").arg(key).arg(track).arg(slot < 0 ? QString("arrangement")
                                                                                  : QString("session slot %1").arg(slot)));
            // the song's tempo: an arrangement clip's from its set, once the device says where it is in the song
            if (slot < 0 || track < 0) {
                  if (LiveClipsLink::instance()->deviceProtocol() < SPAN_PROTOCOL)
                        s.tempoFrom = TempoFrom::OLD_DEVICE;
                  else if (s.tempoFrom == TempoFrom::LIVE || s.tempoFrom == TempoFrom::OLD_DEVICE)
                        s.tempoFrom = TempoFrom::SEARCHING;
                  }
            else if (s.tempoFrom != TempoFrom::LIVE) {    // (now a session clip: Live's tempo)
                  s.tempoFrom = TempoFrom::LIVE;
                  s.hasSpan = false;
                  s.setPath.clear();
                  s.tempoPending = true;
                  watchSets();
                  }
            if (slot < 0 || track < 0) {
                  s.env = EnvState::ARRANGEMENT;
                  updateStatus();
                  }
            else if (s.env == EnvState::NONE || s.env == EnvState::ARRANGEMENT || s.env == EnvState::NO_SCRIPT)
                  readEnvelopes(key);
            else if (moved && s.env == EnvState::READY)
                  log(QString("clip %1 moved in Live: its envelopes are written there").arg(key));
            }
      else if (address == "/live/clip/span") {      // an arrangement clip's place in the song (protocol 6)
            auto it = _sessions.find(key);
            if (it == _sessions.end())
                  return;
            Session& s = it->second;
            LiveClipTempo::Span sp;
            sp.start = args.value(1).toDouble();
            sp.end = args.value(2).toDouble();
            sp.startMarker = args.value(3).toDouble();
            sp.endMarker = args.value(4).toDouble();
            sp.loopStart = args.value(5).toDouble();
            sp.loopEnd = args.value(6).toDouble();
            sp.looping = args.value(7).toInt() != 0;
            const bool changed = !s.hasSpan || sp != s.span;
            s.span = sp;
            s.hasSpan = true;
            log(QString("clip %1: in the song from beat %2 to %3").arg(key).arg(sp.start).arg(sp.end));
            if (s.tempoFrom == TempoFrom::LIVE || s.tempoFrom == TempoFrom::OLD_DEVICE)
                  s.tempoFrom = TempoFrom::SEARCHING;
            if (changed)
                  findSet(key, s.setPath);
            }
      else if (address == "/live/env/begin") {
            auto it = _sessions.find(key);
            if (it == _sessions.end() || it->second.env != EnvState::READING)
                  return;
            Session& s = it->second;
            const QString status = args.value(1).toString();
            if (status == "ok") {
                  s.envHash = args.value(2).toInt();
                  s.envExpected = args.value(3).toInt();
                  s.envIncoming.clear();
                  s.envChunks.clear();
                  if (s.envExpected == 0)
                        envelopesRead(key);
                  }
            else if (status == "arrangement")
                  s.env = EnvState::ARRANGEMENT;
            else if (status == "gone")
                  s.env = EnvState::FAILED, s.envError = tr("the clip isn't in that slot any more");
            else
                  s.env = EnvState::FAILED, s.envError = status;
            updateStatus();
            }
      else if (address == "/live/env/lane") {
            auto it = _sessions.find(key);
            if (it == _sessions.end() || it->second.env != EnvState::READING || it->second.envExpected < 0)
                  return;
            Session& s = it->second;
            const std::pair<int, int> dp { args.value(1).toInt(), args.value(2).toInt() };
            std::vector<std::pair<int, double>>& part = s.envIncoming[dp][args.value(3).toInt()];
            part.clear();
            for (int i = 5; i + 1 < args.size(); i += 2)
                  part.push_back({ args[i].toInt(), args[i + 1].toDouble() });
            s.envChunks[dp] = args.value(4).toInt();
            int complete = 0;
            for (const auto& in : s.envIncoming)
                  if (int(in.second.size()) >= s.envChunks[in.first])
                        ++complete;
            if (complete >= s.envExpected)
                  envelopesRead(key);
            }
      else if (address == "/live/env/written")
            envWritten(key, args.value(1).toInt(), args.value(2).toString(), args.value(3).toInt());
      else if (address == "/live/vel/curve") {
            auto it = _sessions.find(key);
            if (it == _sessions.end() || it->second.velRead)
                  return;
            Session& s = it->second;
            s.velKept = args.value(2).toInt() != 0;
            s.velIncoming[args.value(3).toInt()] = args.mid(5);
            if (int(s.velIncoming.size()) < args.value(4).toInt())
                  return;
            QVariantList all;
            for (const auto& c : s.velIncoming)
                  all.append(c.second);
            s.velIncoming.clear();
            velocityRead(key, args.value(1).toInt() != 0, all);
            }
      else if (address == "/live/vel/set") {
            auto it = _sessions.find(key);
            if (it == _sessions.end())
                  return;
            Session& s = it->second;
            if (!s.velInFlight || args.value(1).toInt() != s.velSerial)
                  return;
            s.velInFlight = false;
            s.velSent = s.velPending;
            s.velStatus = args.value(2).toString();
            s.velKept = args.value(3).toInt() != 0;
            log(QString("clip %1: velocity curve %2: %3%4").arg(key).arg(s.velSerial).arg(s.velStatus)
                .arg(s.velKept ? ", kept in the set" : ", not kept in the set"));
            if (s.velAfterWrite) {
                  s.velAfterWrite = false;
                  sendVelocity(key);
                  }
            updateStatus();
            }
      else if (address == "/live/env/conflict") {
            auto it = _sessions.find(key);
            if (it != _sessions.end() && it->second.env == EnvState::READY && it->second.state != State::RELOADING) {
                  it->second.state = State::CONFLICT;
                  it->second.envInFlight = false;
                  log(QString("clip %1: its envelopes changed in Live: no more writes until it is reloaded").arg(key));
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

int LiveClipEditor::incomingNotes(const QString& key) const
      {
      const auto it = _incoming.find(key);
      return it == _incoming.end() ? -1 : int(it->second.notes.size());
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
      edit(clip, score);
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

void LiveClipEditor::edit(const Clip& clip, MasterScore* score)
      {
      auto it = _sessions.find(clip.key);
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
            s.place = it->second.place;
            s.tempoFrom = it->second.tempoFrom;  // (the song's tempo as found for it)
            s.span = it->second.span;
            s.hasSpan = it->second.hasSpan;
            s.setPath = it->second.setPath;
            s.setAutomated = it->second.setAutomated;
            s.setTempo = it->second.setTempo;
            s.asked = it->second.asked;
            }
      // the song's tempo: the import's marking (the clip's tempo when it was read) is this one's to change
      s.tempoOwned = LiveClipTempo::tempoTexts(score);
      _sessions[clip.key] = s;
      applyTempo(clip.key);                    // (at once: exact, as Live has it)
      connect(score, &Score::playlistChanged, this, [this, score]() { scoreChanged(score); });
      // the clip's Velocity lane as the device keeps it (an older device: none)
      if (LiveClipsLink::instance()->deviceProtocol() >= VEL_PROTOCOL)
            askVelocity(clip.key);
      else
            _sessions[clip.key].velRead = true;
      log(QString("clip %1 opened: %2 notation notes, %3 Live notes not shown, %4 outside the clip")
          .arg(clip.key).arg(s.base.entries.size()).arg(s.base.unmatched).arg(s.base.outside));
      if (s.place != NO_PLACE)
            retitle(_sessions[clip.key]);
      updateClean(clip.key);                  // (as read from Live: nothing to save)
      }

//---------------------------------------------------------
//   in sync: no '*'
//---------------------------------------------------------

bool LiveClipEditor::inSync(const QString& key, const Session& s) const
      {
      return s.score && s.state == State::SYNC && !s.inFlight && !s.changedMeanwhile && !_dirty.count(key) && key != _flushing
             && !s.envInFlight && !s.envChangedMeanwhile && s.env != EnvState::FAILED && !s.velInFlight && !s.velAfterWrite
             && !s.score->undoStack()->active();
      }

bool LiveClipEditor::inSync(const MasterScore* score) const
      {
      for (const auto& s : _sessions)
            if (s.second.score && s.second.score == score)
                  return inSync(s.first, s.second);
      return false;
      }

void LiveClipEditor::updateClean(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !inSync(key, it->second) || !it->second.score->dirty())
            return;
      MasterScore* score = it->second.score;
      score->undoStack()->setClean();         // (undo and redo stay: an undo is an edit, written like any)
      if (!mscore || MScore::noGui || mscore->scores().indexOf(score) < 0)
            return;                           // (not in a tab yet: its tab is made clean)
      mscore->dirtyChanged(score);            // (the tab's '*')
      Score* cs = mscore->currentScore();
      mscore->setWindowModified(cs ? cs->dirty() : false);   // (dirtyChanged set it from this score)
      }

// an unnamed clip's tab and window title once its place is known (a tab saved meanwhile keeps its file's name)
void LiveClipEditor::retitle(Session& s)
      {
      if (!s.score || !s.score->created() || !s.clip.name.trimmed().isEmpty())
            return;
      const QString name = clipTitle(s.clip, s.place) + ".mscz";
      if (s.score->fileInfo()->fileName() == name)
            return;
      s.score->fileInfo()->setFile(name);
      if (!mscore || MScore::noGui)
            return;
      if (mscore->scores().indexOf(s.score) >= 0)
            mscore->dirtyChanged(s.score);      // (sets the tab's text)
      Score* cs = mscore->currentScore();
      mscore->setWindowModified(cs ? cs->dirty() : false);
      if (cs && cs->masterScore() == s.score)
            mscore->updateWindowTitle(cs);
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
      for (const QString& key : keys) {
            _flushing = key;                  // (not in sync before both are sent)
            write(key);
            writeEnvelopes(key);
            sendVelocity(key);
            _flushing.clear();
            updateClean(key);                 // (nothing to send: a change Live doesn't have, e.g. a text)
            }
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
      Diff d = diff(s.base, signaturesForLive(s.score));
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
            sendVelocity(key);                  // ("write": the originals as Live has the notes now)
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
      applyPendingTempos();
      for (auto& e : _sessions) {             // the envelopes: a read or a write unanswered
            Session& s = e.second;
            if (s.env == EnvState::READING && now - s.envSentAt >= CONFIRM_MS) {
                  s.env = EnvState::NO_SCRIPT;
                  s.envSentAt = now;
                  log(QString("clip %1: the MuseScore Envelopes script doesn't answer").arg(e.first));
                  updateStatus();
                  // how to set it up, once (livehelpers.h): the device answers (it sent the clip), the script doesn't
                  if (mscore && !MScore::noGui && s.state != State::NO_ANSWER && s.state != State::GONE)
                        LiveHelpers::controlSurfaceHint(mscore);
                  }
            else if (s.env == EnvState::NO_SCRIPT && now - s.envSentAt >= 5000 && s.score)
                  readEnvelopes(e.first);       // (asked again: installed meanwhile)
            else if (s.envInFlight && now - s.envSentAt >= CONFIRM_MS) {
                  if (s.envTries >= MAX_TRIES) {
                        s.envInFlight = false;
                        s.env = EnvState::FAILED;
                        s.envError = tr("the MuseScore Envelopes script stopped answering (reload the clip)");
                        updateStatus();
                        }
                  else {
                        ++s.envTries;
                        s.envSentAt = now;
                        for (const QByteArray& p : s.envPackets)    // (the same write number: applied once)
                              sendEnv(p);
                        }
                  }
            }
      for (auto& e : _sessions) {             // the Velocity lane: the ask or the record unanswered
            Session& s = e.second;
            if (s.velAsked && !s.velRead && now - s.velSentAt >= CONFIRM_MS) {
                  if (s.velTries >= MAX_TRIES) {
                        s.velRead = true;       // (none read: the lane as it is here)
                        s.velStatus = tr("MuseScore Link didn't answer");
                        updateStatus();
                        }
                  else {
                        ++s.velTries;
                        s.velSentAt = now;
                        send(LiveClips::osc("/ms/vel/ask", { e.first }));
                        }
                  }
            else if (s.velInFlight && now - s.velSentAt >= CONFIRM_MS) {
                  if (s.velTries >= MAX_TRIES) {
                        s.velInFlight = false;
                        s.velStatus = tr("MuseScore Link didn't answer");
                        updateStatus();
                        }
                  else {
                        ++s.velTries;
                        s.velSentAt = now;
                        for (const QByteArray& p : s.velPackets)    // (the same serial: the device keeps the last)
                              send(p);
                        }
                  }
            }
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
//   the song's tempo (cliptempo.h)
//---------------------------------------------------------

static const char* const SETS_SETTING = "liveIntegration/tempoSets";
static QStringList livePrefsBasesOverride;
static bool livePrefsBasesSet = false;
static bool searchInline = false;

void LiveClipEditor::setLivePrefsBases(const QStringList& bases)
      {
      livePrefsBasesOverride = bases;
      livePrefsBasesSet = true;
      }

void LiveClipEditor::setSearchInline(bool on)
      {
      searchInline = on;
      }

static QStringList livePrefsBases()
      {
      return livePrefsBasesSet ? livePrefsBasesOverride : LiveHelpers::prefsBases();
      }

// Live's lists (Log.txt, Preferences.cfg) as they are now: the latest of their files' times
static qint64 livePrefsStamp()
      {
      qint64 t = 0;
      for (const QString& base : livePrefsBases())
            for (const QString& v : QDir(base).entryList({ "Live *" }, QDir::Dirs))
                  for (const char* f : { "/Preferences/Log.txt", "/Preferences/Preferences.cfg" }) {
                        const QFileInfo fi(base + "/" + v + f);
                        if (fi.exists())
                              t = std::max(t, fi.lastModified().toMSecsSinceEpoch());
                        }
      return t;
      }

QStringList LiveClipEditor::rememberedSets()
      {
      return QSettings().value(SETS_SETTING).toStringList();
      }

static void rememberSet(const QString& path)
      {
      QStringList l = LiveClipEditor::rememberedSets();
      l.removeAll(path);
      l.prepend(path);
      for (int i = l.size() - 1; i >= 0; --i)         // (sets deleted or moved since: dropped)
            if (!QFileInfo(l[i]).isFile())
                  l.removeAt(i);
      QSettings().setValue(SETS_SETTING, l);
      }

namespace {
struct SearchResult {
      QString path;
      std::map<QString, LiveClipEditor::ReadSet> read;      // the sets read for it (kept for the next search)
      };
}

// the candidates in order, read (or taken from what was read before, unchanged) until one has the clip
static SearchResult searchSets(QStringList candidates, QStringList prefsBases, std::map<QString, LiveClipEditor::ReadSet> known,
                               QString track, int trackIndex, LiveClipTempo::Span span)
      {
      SearchResult r;
      for (const QString& p : LiveClipTempo::setCandidates(prefsBases))
            if (!candidates.contains(p))
                  candidates << p;
      for (const QString& path : candidates) {
            const QFileInfo fi(path);
            if (!fi.isFile())
                  continue;
            std::shared_ptr<const LiveSet::Set> set;
            auto k = known.find(path);
            if (k != known.end() && k->second.modified == fi.lastModified() && k->second.set)
                  set = k->second.set;
            else {
                  set = std::make_shared<const LiveSet::Set>(LiveSet::read(path));
                  r.read[path] = { fi.lastModified(), set };
                  }
            if (set->error.isEmpty() && LiveClipTempo::setHasClip(*set, track, trackIndex, span)) {
                  r.path = path;
                  r.read[path] = { fi.lastModified(), set };
                  return r;
                  }
            }
      return r;
      }

void LiveClipEditor::findSet(const QString& key, const QString& prefer)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !it->second.hasSpan)
            return;
      Session& s = it->second;
      // the candidates: the one asked for, the sets of other clip tabs, the sets linked to open scores, the sets chosen
      // before; then (in the search) Live's own lists
      QStringList candidates;
      auto add = [&candidates](const QString& p) {
            if (!p.isEmpty() && !candidates.contains(p))
                  candidates << p;
            };
      add(prefer);
      for (const auto& o : _sessions)
            add(o.second.setPath);
      if (mscore)
            for (MasterScore* m : mscore->scores())
                  add(linkedSet(m));
      for (const QString& p : rememberedSets())
            add(p);
      const int serial = ++s.searchSerial;
      s.searching = true;
      s.searchedPrefs = livePrefsStamp();
      log(QString("clip %1: looking for its Live Set (%2 candidates before Live's lists)").arg(key).arg(candidates.size()));
      const QStringList bases = livePrefsBases();
      if (searchInline || MScore::noGui) {
            const SearchResult r = searchSets(candidates, bases, _sets, s.clip.track, s.envTrack, s.span);
            setFound(key, serial, r.path, r.read);
            return;
            }
      QFutureWatcher<SearchResult>* w = new QFutureWatcher<SearchResult>(this);
      connect(w, &QFutureWatcher<SearchResult>::finished, this, [this, w, key, serial]() {
            const SearchResult r = w->result();
            w->deleteLater();
            setFound(key, serial, r.path, r.read);
            });
      const std::map<QString, ReadSet> known = _sets;
      const QString track = s.clip.track;
      const int trackIndex = s.envTrack;
      const LiveClipTempo::Span span = s.span;
      w->setFuture(QtConcurrent::run([candidates, bases, known, track, trackIndex, span]() {
            return searchSets(candidates, bases, known, track, trackIndex, span);
            }));
      updateStatus();
      }

void LiveClipEditor::setFound(const QString& key, int serial, const QString& path, const std::map<QString, ReadSet>& read)
      {
      for (const auto& r : read)
            _sets[r.first] = r.second;
      auto it = _sessions.find(key);
      if (it == _sessions.end() || it->second.searchSerial != serial)
            return;                           // (closed, or a later search)
      Session& s = it->second;
      s.searching = false;
      if (s.tempoFrom == TempoFrom::LIVE)       // (a session clip meanwhile)
            return;
      if (path.isEmpty()) {
            log(QString("clip %1: its Live Set wasn't found").arg(key));
            s.tempoFrom = TempoFrom::NO_SET;
            s.setPath.clear();
            s.setTempo.clear();
            s.setAutomated = false;
            s.tempoPending = true;
            if (!s.asked && mscore && !MScore::noGui) {
                  s.asked = true;
                  QPointer<MasterScore> score = s.score;
                  LiveClipsLink::instance()->notice(tr("The Live clip %1 › %2 plays at Live's current tempo: its Live Set wasn't "
                                                       "found, so the song's tempo automation can't be read. Save the set in "
                                                       "Live to use its tempo automation, or choose its file.")
                                                    .arg(s.clip.track, clipLabel(s.clip, s.place)), false,
                                                    tr("Choose Live Set…"), [this, score]() { if (score) chooseSet(score); });
                  }
            }
      else {
            const ReadSet& rs = _sets[path];
            s.setPath = path;
            s.setAutomated = rs.set && !rs.set->tempoEvents.empty();
            s.setTempo = rs.set ? LiveClipTempo::songTempo(*rs.set) : std::vector<LiveClipTempo::Point>();
            s.tempoFrom = TempoFrom::SET;
            s.tempoPending = true;
            rememberSet(path);
            log(QString("clip %1: its Live Set is %2 (%3)").arg(key, path, s.setAutomated ? QString("tempo automation")
                                                                                         : QString("no tempo automation")));
            }
      watchSets();
      applyPendingTempos();
      updateStatus();
      }

void LiveClipEditor::useSet(MasterScore* score, const QString& path)
      {
      Session* s = sessionOf(score);
      if (!s || path.isEmpty())
            return;
      _sets.erase(path);                        // (read anew)
      const QString key = s->clip.key;
      s->asked = true;
      findSet(key, path);
      }

void LiveClipEditor::chooseSet(MasterScore* score)
      {
      Session* s = sessionOf(score);
      if (!s || !mscore)
            return;
      const QStringList remembered = rememberedSets();
      const QString start = !s->setPath.isEmpty() ? s->setPath : (remembered.isEmpty() ? QString() : QFileInfo(remembered.first()).path());
      const QString path = QFileDialog::getOpenFileName(mscore, tr("The Live Set of the clip %1").arg(clipLabel(s->clip, s->place)),
                                                        start, tr("Ableton Live Set") + " (*.als)");
      if (path.isEmpty())
            return;
      const QString key = s->clip.key;
      useSet(score, path);
      auto it = _sessions.find(key);
      if (it != _sessions.end() && !it->second.searching && it->second.setPath != path)
            LiveClipsLink::instance()->notice(tr("%1 has no clip of the track %2 at its place in the song: is the set saved "
                                                 "in Live?").arg(QFileInfo(path).fileName(), it->second.clip.track));
      }

void LiveClipEditor::watchSets()
      {
      QStringList want;
      for (const auto& s : _sessions)
            if (!s.second.setPath.isEmpty() && !want.contains(s.second.setPath))
                  want << s.second.setPath;
      const QStringList have = _setWatch->files();
      for (const QString& p : have)
            if (!want.contains(p))
                  _setWatch->removePath(p);
      for (const QString& p : want)
            if (!have.contains(p) && QFileInfo(p).isFile())
                  _setWatch->addPath(p);
      }

// Live saved a set (settled): the clips in it read it again
void LiveClipEditor::setsSaved()
      {
      const QStringList paths = _setsChanged;
      _setsChanged.clear();
      for (const QString& path : paths) {
            if (!QFileInfo(path).isFile()) {      // (replaced by a rename: back in a moment)
                  QTimer::singleShot(1000, this, [this]() { watchSets(); });
                  continue;
                  }
            _sets.erase(path);
            std::vector<QString> keys;
            for (const auto& s : _sessions)
                  if (s.second.setPath == path)
                        keys.push_back(s.first);
            for (const QString& k : keys)
                  findSet(k, path);
            }
      watchSets();                              // (a file replaced by rename leaves the watcher)
      }

// for the tests and the watcher alike: a set file saved (path), as the watcher reports it
void LiveClipEditor::setSaved(const QString& path)
      {
      if (!_setsChanged.contains(path))
            _setsChanged << path;
      setsSaved();
      }

void LiveClipEditor::songTempo(double bpm)
      {
      if (bpm <= 0 || bpm == _songBpm)
            return;
      _songBpm = bpm;
      for (auto& s : _sessions)
            if (s.second.tempoFrom != TempoFrom::SET || !s.second.setAutomated)
                  s.second.tempoPending = true;
      }

void LiveClipEditor::applyPendingTempos()
      {
      std::vector<QString> keys;
      for (const auto& s : _sessions)
            keys.push_back(s.first);
      for (const QString& k : keys) {
            auto it = _sessions.find(k);
            if (it == _sessions.end())
                  continue;
            Session& s = it->second;
            // not found: looked for again when Live's lists change (a set saved, opened)
            if (s.tempoFrom == TempoFrom::NO_SET && !s.searching && livePrefsStamp() != s.searchedPrefs)
                  findSet(k);
            it = _sessions.find(k);
            if (it != _sessions.end() && it->second.tempoPending)
                  applyTempo(k);
            }
      }

void LiveClipEditor::applyTempo(const QString& key)
      {
      auto it = _sessions.find(key);
      if (it == _sessions.end() || !it->second.score)
            return;
      Session& s = it->second;
      if (seq && seq->isPlaying()) {            // (after MuseScore stops)
            s.tempoPending = true;
            return;
            }
      s.tempoPending = false;
      MasterScore* score = s.score;
      std::vector<LiveClipTempo::Point> pts;
      if (s.tempoFrom == TempoFrom::SET && s.setAutomated && s.hasSpan)
            pts = LiveClipTempo::clipTempo(s.setTempo, LiveClipTempo::firstPasses(s.span, s.clip.end));
      else {
            const double bpm = _songBpm > 0 ? _songBpm : s.clip.bpm;
            pts.push_back({ 0, bpm, false });
            s.appliedBpm = bpm;
            }
      const std::vector<LiveClipTempo::Mark> want = LiveClipTempo::marks(pts, score->endTick().ticks());
      if (seq)
            seq->waitForRendering();
      const int r = LiveClipTempo::apply(score, want, &s.tempoOwned, score->undoStack()->canUndo() || score->undoStack()->canRedo());
      if (r) {
            log(QString("clip %1: the song's tempo put in the score (%2 marking(s) and line(s)%3)").arg(key).arg(want.size())
                .arg(r == 2 ? QString(", replaced") : QString(", in place")));
            score->update();
            updateClean(key);
            }
      }

LiveClipEditor::TempoFrom LiveClipEditor::tempoFrom(const MasterScore* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      return s ? s->tempoFrom : TempoFrom::LIVE;
      }

QString LiveClipEditor::tempoSet(const MasterScore* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      return s ? s->setPath : QString();
      }

QString LiveClipEditor::tempoText(const MasterScore* score, QString* details) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      if (!s)
            return QString();
      QString bpm = QString::number(s->appliedBpm > 0 ? s->appliedBpm : s->clip.bpm, 'f', 2);
      while (bpm.contains('.') && (bpm.endsWith('0') || bpm.endsWith('.')))
            bpm.chop(1);
      QString text, more;
      switch (s->tempoFrom) {
            case TempoFrom::LIVE:
                  text = tr("tempo: Live's (%1)").arg(bpm);
                  more = tr("Tempo: Live's song tempo, followed as it changes.");
                  break;
            case TempoFrom::SEARCHING:
                  text = tr("tempo: looking for the Live Set…");
                  break;
            case TempoFrom::SET:
                  if (s->setAutomated) {
                        text = tr("tempo: the song's automation");
                        more = tr("Tempo: the song's tempo automation under the clip, from %1 (read again when Live saves it). "
                                  "A looping clip shows each beat at its tempo in its first pass.").arg(QDir::toNativeSeparators(s->setPath));
                        }
                  else {
                        text = tr("tempo: Live's (%1)").arg(bpm);
                        more = tr("Tempo: Live's song tempo, followed as it changes (%1 has no tempo automation).")
                               .arg(QFileInfo(s->setPath).fileName());
                        }
                  break;
            case TempoFrom::NO_SET:
                  text = tr("tempo: Live's (%1), set not found").arg(bpm);
                  more = tr("Tempo: Live's current song tempo. The clip's Live Set wasn't found: save the set in Live to use its "
                            "tempo automation, or choose its file (Choose Live Set…).");
                  break;
            case TempoFrom::OLD_DEVICE:
                  text = tr("tempo: Live's (%1)").arg(bpm);
                  more = tr("Tempo: Live's current song tempo. Update the MuseScore Link device in Live to follow the song's "
                            "tempo automation under an arrangement clip.");
                  break;
            }
      if (details)
            *details = more;
      return text;
      }

//---------------------------------------------------------
//   status
//---------------------------------------------------------

LiveClipEditor::State LiveClipEditor::state(const MasterScore* score) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      return s ? s->state : State::SYNC;
      }

// the status line's parts: short ones (the status bar) and the long explanations (its tooltip)
void LiveClipEditor::statusParts(const MasterScore* score, QStringList* parts, QStringList* details) const
      {
      const Session* s = const_cast<LiveClipEditor*>(this)->sessionOf(score);
      if (!s)
            return;
      const QString what = tr("Live clip %1 › %2").arg(s->clip.track, clipLabel(s->clip, s->place));
      QString state;
      switch (s->state) {
            case State::SYNC:
                  state = tr("in sync");
                  if (s->notesChanged)
                        state += " · " + tr("%n change(s) sent", "", s->notesChanged);
                  break;
            case State::SENDING:
                  state = tr("sending %n change(s)", "", int(s->pending.ops.size()));
                  break;
            case State::CONFLICT:
                  state = tr("conflict: changed in Live");
                  *details << tr("Conflict: the clip changed in Live; nothing more is written until you reload it "
                                 "(Reload from Live).");
                  break;
            case State::RELOADING:
                  state = tr("reading from Live…");
                  break;
            case State::GONE:
                  state = tr("gone from Live");
                  *details << tr("The clip is gone from Live (deleted there): edits here are no longer written.");
                  break;
            case State::NO_ANSWER:
                  state = tr("Live doesn't answer");
                  *details << tr("Live doesn't answer: is the MuseScore Link device loaded?");
                  break;
            case State::FAILED:
                  state = tr("failed");
                  *details << s->error;
                  break;
            }
      *parts << what + ": " + state;
      QString envMore;
      const QString env = envText(s->score, &envMore);
      if (!env.isEmpty())
            *parts << env;
      if (!envMore.isEmpty())
            *details << envMore;
      QString velMore;
      const QString vel = velocityText(s->score, &velMore);
      if (!vel.isEmpty())
            *parts << vel;
      if (!velMore.isEmpty())
            *details << velMore;
      QString tempoMore;
      const QString tempo = tempoText(s->score, &tempoMore);
      if (!tempo.isEmpty())
            *parts << tempo;
      if (!tempoMore.isEmpty())
            *details << tempoMore;
      if (s->base.unmatched || s->base.outside) {
            const int n = s->base.unmatched + s->base.outside;
            *parts << tr("%n Live note(s) not shown", "", n);
            *details << tr("%n Live note(s) not shown here are left as they are.", "", n);
            }
      // playback
      const LiveClipsLink* link = LiveClipsLink::instance();
      if (!playLiveSetting())
            return;                             // (MuseScore's own sounds, as asked)
      if (!link->deviceAnswers()) {
            *parts << tr("Live link lost: own sounds");
            *details << tr("The connection to Live is lost: MuseScore's own sounds play (it reconnects by itself).");
            }
      else if (liveTrack(score)) {
            *parts << tr("plays through Live");
            *details << tr("Plays through Live's track %1.").arg(s->clip.track);
            }
      else if (link->deviceProtocol() < MIDI_PROTOCOL || !s->trackId) {
            *parts << tr("own sounds (update MuseScore Link)");
            *details << tr("MuseScore's own sounds: the MuseScore Link device in Live is older (update it to play "
                           "through the track).");
            }
      else if (!s->copy) {
            *parts << tr("own sounds (no MuseScore Link on the track)");
            *details << tr("Add MuseScore Link to the Live track %1 to hear it there (MuseScore's own sounds meanwhile).")
                        .arg(s->clip.track);
            }
      }

QString LiveClipEditor::statusText(const MasterScore* score) const
      {
      QStringList parts, details;
      statusParts(score, &parts, &details);
      return parts.join(" · ");
      }

QString LiveClipEditor::statusDetails(const MasterScore* score) const
      {
      QStringList parts, details;
      statusParts(score, &parts, &details);
      return details.join("\n");
      }

void LiveClipEditor::setCurrentScore(MasterScore* score)
      {
      _current = score;
      updateRouting();
      updateStatus();
      }

void LiveClipEditor::updateStatus()
      {
      TrackParams::instance()->touch();         // (the lanes a clip tab offers may have changed)
      for (const auto& e : _sessions)           // (a write confirmed, the envelopes read: no '*')
            updateClean(e.first);
      emit statusChanged();
      if (!mscore || MScore::noGui)
            return;
      if (!_status) {
            _status = new QWidget;
            QHBoxLayout* h = new QHBoxLayout(_status);
            h->setContentsMargins(4, 0, 4, 0);
            _statusLabel = new ElidedLabel(_status);    // (cut to its room: never widens the window; the owner, 2026-10-03)
            _reload = new QPushButton(tr("Reload from Live"), _status);
            _reload->setToolTip(tr("Read the clip from Live again, as it is there now. Edits made here and not yet in "
                                   "Live are lost."));
            _chooseSet = new QPushButton(tr("Choose Live Set…"), _status);
            _chooseSet->setToolTip(tr("The Live Set this arrangement clip is in, for the song's tempo automation (Live doesn't "
                                      "say which file it is; save the set in Live first)."));
            h->addWidget(_statusLabel);
            h->addWidget(_reload);
            h->addWidget(_chooseSet);
            connect(_reload, &QPushButton::clicked, this, [this]() { reload(_current); });
            connect(_chooseSet, &QPushButton::clicked, this, [this]() { chooseSet(_current); });
            mscore->statusBar()->addPermanentWidget(_status);
            }
      const Session* s = sessionOf(_current);
      _status->setVisible(s != nullptr);
      if (!s)
            return;
      const QString text = statusText(_current);
      const QString details = statusDetails(_current);
      _statusLabel->setText(text);
      _statusLabel->setToolTip(details.isEmpty() ? text : text + "\n\n" + details);
      _reload->setVisible(s->state == State::CONFLICT || s->state == State::FAILED || s->state == State::NO_ANSWER
                          || s->env == EnvState::FAILED);
      _chooseSet->setVisible(s->tempoFrom == TempoFrom::NO_SET);
      }

}     // namespace LiveIntegration
}     // namespace Ms
