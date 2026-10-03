//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveclips.h"
#include "soundlibraryhost.h"

#include <cmath>

#include <QCoreApplication>
#include <QDateTime>
#include <QEvent>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QStatusBar>
#include <QToolButton>
#include <QSettings>
#include <QTimer>
#include <QUdpSocket>

#include "libmscore/part.h"
#include "libmscore/partplayback.h"
#include "libmscore/rendermidi.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/undo.h"
#include "liveclipedit.h"
#include "liveintegration.h"
#include "livemidiout.h"
#include "musescore.h"
#include "preferences.h"
#include "seq.h"

namespace Ms {

extern bool (*midiInputSilenced)();     // libmscore/mscore.cpp (Score::processMidiInput asks it)

namespace LiveIntegration {

static const char* const SETTING = "liveIntegration/clips";
static constexpr int DEBOUNCE_MS      = 300;
static constexpr int STEP_BUDGET_MS   = 10;
static constexpr int PACE_MS          = 15;
static constexpr int PACKETS_PER_PACE = 8;          // ~ 0.7 MB/s at most
// HELLO_TIMEOUT_MS and CONFIRM_MS measured (numbers-measured, 2026-10-03, Live 12.4.6 on the Windows VM, a listener in
// MuseScore's place logging the MuseScore Link hub's datagrams, docs/PLAYBACK_SETTINGS.md › Measured by sweeps): while
// Live froze a Kontakt + SSO track with a 160-note clip (3x), duplicated such a track (2x) and loaded a set, the hub's
// hellos (every 2 s from its 1 s Task) were at most 6.0 s apart (the owner's rule 7B: the timeout is the longest silence
// plus one hello period: 8 s; with extra load, 4 heavy probes a second, they reached 8.6 s). Probe answers (the path of
// MuseScore's own messages) came at most 3.79 s after outside freezes (median 46 ms): CONFIRM_MS, rounded up to 0.1 s
// (during a freeze Live answers nothing until it ends). MAX_TRIES, DEBOUNCE_MS: not measured (no UDP loss was seen,
// 0 of 6898 probes on localhost)
static constexpr int CONFIRM_MS       = 3800;
static constexpr int MAX_TRIES        = 3;
static constexpr int HELLO_TIMEOUT_MS = 8000;       // the device says hello every 2 s
static constexpr double FOLLOW_TOLERANCE = 0.15;    // seconds MuseScore may be off Live's position before it moves

// MS_LIVE_LOG=1: what the link does, on stderr (tools/live/test/fake_live_server.js)
static void log(const QString& s)
      {
      static const bool on = qEnvironmentVariableIsSet("MS_LIVE_LOG");
      if (on)
            qInfo("%.3f LiveClips: %s", double(QDateTime::currentMSecsSinceEpoch() % 100000) / 1000.0, qPrintable(s));
      }

LiveClipsLink* LiveClipsLink::instance()
      {
      static LiveClipsLink* g = nullptr;
      if (!g) {
            g = new LiveClipsLink;
            if (enabledSetting())
                  g->setOn(true);
            else
                  g->updateSocket();      // (editing Live clips: listening for the device all the same)
            }
      return g;
      }

bool LiveClipsLink::enabledSetting()
      {
      return QSettings().value(SETTING, false).toBool();
      }

LiveClipsLink::LiveClipsLink()
      {
      midiInputSilenced = &LiveClipsLink::silencesMidiInput;
      _debounce = new QTimer(this);
      _debounce->setSingleShot(true);
      _debounce->setInterval(DEBOUNCE_MS);
      connect(_debounce, &QTimer::timeout, this, &LiveClipsLink::startRender);
      _step = new QTimer(this);
      _step->setSingleShot(true);
      _step->setInterval(0);
      connect(_step, &QTimer::timeout, this, &LiveClipsLink::renderStep);
      _pace = new QTimer(this);
      _pace->setInterval(PACE_MS);
      connect(_pace, &QTimer::timeout, this, &LiveClipsLink::sendSome);
      _poll = new QTimer(this);
      _poll->setInterval(1000);
      connect(_poll, &QTimer::timeout, this, &LiveClipsLink::poll);
      _watchTimer = new QTimer(this);
      _watchTimer->setInterval(1000);
      connect(_watchTimer, &QTimer::timeout, this, &LiveClipsLink::watch);
      }

LinkWatch::Uses LiveClipsLink::uses() const
      {
      LinkWatch::Uses u;
      LiveClipEditor::instance()->countTabs(&u.clipTabs, &u.clipTabsThroughLive);
      u.livePlaysScore = _on;
      u.playThroughLive = playingThroughMidi();
      u.port = _port;
      return u;
      }

void LiveClipsLink::watch()
      {
      const LinkWatch::Uses u = uses();
      const LinkWatch::Change c = _watch.update(deviceAnswers(), u.any());
      if (c == LinkWatch::LOST) {
            log("the device doesn't answer: connection lost");
            notice(LinkWatch::lostText(u));
            }
      else if (c == LinkWatch::BACK) {
            log("the device answers again");
            notice(LinkWatch::backText(u), true);
            }
      LiveClipEditor::instance()->linkChanged();
      }

void LiveClipsLink::notice(const QString& text, bool good)
      {
      _lastNotice = text;
      emit statusChanged();
      if (!mscore || MScore::noGui)
            return;
      // a bar over the top of the score area, the window's width (laid over it, not in a layout: the status bar
      // and the docks stay as they are), until dismissed; a good one goes after 10 s
      QWidget* host = mscore->centralWidget();
      if (!host)
            return;
      // (a child of the main window, not of the central widget: a splitter would take it into its layout)
      struct Follow : public QObject {          // (keeps the bar over the top of the central widget, its width)
            QFrame* bar;
            QWidget* host;
            Follow(QFrame* b, QWidget* h) : QObject(b), bar(b), host(h) {}
            void place() {
                  const QRect r = host->geometry();
                  const int hw = bar->layout()->heightForWidth(r.width());
                  bar->setGeometry(r.x(), r.y(), r.width(), hw > 0 ? hw : bar->sizeHint().height());
                  bar->raise();
                  }
            bool eventFilter(QObject*, QEvent* e) override {
                  if (e->type() == QEvent::Resize || e->type() == QEvent::Move)
                        place();
                  return false;
                  }
            };
      static QPointer<QFrame> bar;
      static QLabel* label = nullptr;
      static QTimer* hide = nullptr;
      static Follow* follow = nullptr;
      if (!bar) {
            bar = new QFrame(mscore);
            bar->setObjectName("liveNotice");
            QHBoxLayout* h = new QHBoxLayout(bar);
            h->setContentsMargins(10, 4, 4, 4);
            label = new QLabel(bar);
            label->setWordWrap(true);
            QToolButton* close = new QToolButton(bar);
            close->setText("×");
            close->setAutoRaise(true);
            close->setToolTip(tr("Hide this notice"));
            h->addWidget(label, 1);
            h->addWidget(close, 0, Qt::AlignTop);
            hide = new QTimer(bar);
            hide->setSingleShot(true);
            connect(hide, &QTimer::timeout, bar.data(), &QWidget::hide);
            connect(close, &QToolButton::clicked, bar.data(), &QWidget::hide);
            follow = new Follow(bar, host);
            host->installEventFilter(follow);
            }
      bar->setStyleSheet(good ? "QFrame#liveNotice { background: #c8ecc8; border-bottom: 1px solid #6a9f6a; } QLabel { color: #103010; }"
                              : "QFrame#liveNotice { background: #ffd970; border-bottom: 1px solid #b08a20; } QLabel { color: #302000; }");
      label->setText(text);
      label->setToolTip(text);
      bar->show();
      follow->place();
      if (good)
            hide->start(10000);
      else
            hide->stop();
      }

LiveClipsLink::~LiveClipsLink()
      {
      }

void LiveClipsLink::updateSocket()
      {
      const bool want = _on || LiveClipEditor::enabledSetting();
      if (want && !_socket)
            bindSocket();
      else if (!want && _socket) {
            _socket->close();
            delete _socket;
            _socket = nullptr;
            _session.clear();
            _lastHello = 0;
            _watchTimer->stop();
            _watch = LinkWatch();         // (turned off here: not a lost connection)
            }
      }

bool LiveClipsLink::deviceAnswers() const
      {
      return _socket && !_session.isEmpty() && QDateTime::currentMSecsSinceEpoch() - _lastHello < HELLO_TIMEOUT_MS;
      }

bool LiveClipsLink::liveSoundsMidiInputSetting()
      {
      return QSettings().value("liveIntegration/liveSoundsMidiInput", true).toBool();
      }

void LiveClipsLink::setLiveSoundsMidiInputSetting(bool on)
      {
      QSettings().setValue("liveIntegration/liveSoundsMidiInput", on);
      }

bool LiveClipsLink::silencesMidiInput()
      {
      return liveSoundsMidiInputSetting() && instance()->deviceAnswers();
      }

bool LiveClipsLink::active() const
      {
      return _on && playingThroughMidi();
      }

void LiveClipsLink::setOn(bool on)
      {
      QSettings().setValue(SETTING, on);
      if (on == _on)
            return;
      if (seq && seq->isPlaying())
            seq->stopWait();
      _on = on;
      if (on) {
            if (!_socket)
                  bindSocket();
            _poll->start();
            _sent.clear();
            _song = LiveClips::Song();
            _debounce->start();
            }
      else {
            sendMode();                   // (the tracks back to MuseScore's stream: Monitor In)
            sendSome();
            _debounce->stop();
            _step->stop();
            _renderer.reset();
            _events.clear();
            _nextUtick = -1;
            _pace->stop();
            _poll->stop();
            _queue.clear();
            _sent.clear();
                  _session.clear();
            _lastHello = 0;
            _following = false;
            updateSocket();               // (kept for editing Live clips)
            }
      applySeqMode();
      emit statusChanged();
      }

void LiveClipsLink::outputChanged()
      {
      applySeqMode();
      if (_on) {
            sendMode();
            scoreChanged();
            }
      emit statusChanged();
      }

void LiveClipsLink::applySeqMode()
      {
      if (seq)
            seq->setLiveClips(active());
      }

void LiveClipsLink::bindSocket()
      {
      _port = preferences.getInt(PREF_IO_LIVE_CLIPSPORT);
      if (_port <= 0 || _port >= 65535)
            _port = LiveClips::DEFAULT_PORT;
      delete _socket;
      _socket = new QUdpSocket(this);
      if (!_socket->bind(QHostAddress::LocalHost, quint16(_port + 1)))
            qWarning("Live clips: cannot listen on UDP port %d: %s", _port + 1, qPrintable(_socket->errorString()));
      connect(_socket, &QUdpSocket::readyRead, this, &LiveClipsLink::read);
      LiveMidiOut::instance()->setPort(_port);
      _watchTimer->start();
      }

void LiveClipsLink::setScore(MasterScore* score)
      {
      if (score && LiveClipEditor::instance()->isClipScore(score))
            score = nullptr;              // (a Live clip edited here: not the score Live plays)
      if (score == _score)
            return;
      if (_score)
            disconnect(_score, &Score::playlistChanged, this, nullptr);
      _score = score;
      if (_score)
            connect(_score, &Score::playlistChanged, this, &LiveClipsLink::scoreChanged);
      _following = false;
      scoreChanged();
      }

bool LiveClipsLink::connected() const
      {
      // (a hello first: until then the device's session, and so what it holds, is unknown)
      return _on && !_session.isEmpty() && QDateTime::currentMSecsSinceEpoch() - _lastHello < HELLO_TIMEOUT_MS;
      }

//---------------------------------------------------------
//   the device's messages
//---------------------------------------------------------

void LiveClipsLink::read()
      {
      while (_socket && _socket->hasPendingDatagrams()) {
            QByteArray d;
            d.resize(int(_socket->pendingDatagramSize()));
            _socket->readDatagram(d.data(), d.size());
            QString address;
            QVariantList args;
            if (LiveClips::parseOsc(d, &address, &args))
                  received(address, args);
            }
      }

void LiveClipsLink::received(const QString& address, const QVariantList& args)
      {
      const qint64 now = QDateTime::currentMSecsSinceEpoch();
      if (address.startsWith("/live/clip/")) {     // editing a Live clip (liveclipedit.h)
            _lastHello = now;
            LiveClipEditor::instance()->received(address, args);
            return;
            }
      if (address.startsWith("/live/env/")) {      // the MuseScore Envelopes script (a clip tab's lanes)
            LiveClipEditor::instance()->received(address, args);
            return;
            }
      if (address == "/live/params") {             // a track's parameters (automation lanes of any track)
            _lastHello = now;
            if (LiveClipEdit::TrackParams::instance()->accept(args))
                  LiveClipEditor::instance()->paramsChanged(args.value(0).toString());
            return;
            }
      if (address == "/live/hello") {
            const QString session = args.value(0).toString();
            _deviceProtocol = args.value(1).toInt();
            _lastHello = now;
            if (session != _session) {          // the device (re)loaded: it knows nothing yet
                  _session = session;
                  if (_deviceProtocol >= LiveClipEdit::PARAMS_PROTOCOL)       // (and this MuseScore knows none of its tracks)
                        send(LiveClips::osc("/ms/params/ask", {}));
                  if (_on)
                        resync();
                  LiveClipEditor::instance()->newDevice();      // (the clips edited here: to the new hub)
                  }
            watch();
            }
      else if (address == "/live/bye") {        // the hub copy goes (deleted, its set closed): lost at once
            if (args.value(0).toString() == _session) {
                  _session.clear();
                  _lastHello = 0;
                  watch();
                  }
            }
      else if (address == "/live/resync") {
            _lastHello = now;
            if (_on)
                  resync();
            }
      else if (address == "/live/applied") {
            _lastHello = now;
            const QString key = args.value(0).toString();
            const qint32 hash = args.value(1).toInt();
            if (key == "song") {
                  if (qint32(_song.hash) == hash) {
                        _songConfirmed = true;
                        _songStatus = args.value(2).toString();
                        }
                  }
            else {
                  auto s = _sent.find(key);
                  if (s != _sent.end() && qint32(s->second.track.hash) == hash) {
                        s->second.confirmed = true;
                        s->second.status = args.value(2).toString();
                        s->second.liveTrack = args.value(3).toString();
                        _lastSync = now;
                        }
                  }
            emit statusChanged();
            }
      else if (address == "/live/papplied") {
            _lastHello = now;
            auto s = _sent.find(args.value(0).toString());
            if (s != _sent.end() && qint32(s->second.track.paramsHash) == args.value(1).toInt()) {
                  s->second.paramsConfirmed = true;
                  s->second.paramsStatus = args.value(2).toString();
                  }
            emit statusChanged();
            }
      else if (address == "/live/transport") {
            _lastHello = now;
            transport(args.value(0).toInt() != 0, args.value(1).toDouble(), args.value(2).toDouble());
            }
      if (_wasConnected != connected()) {
            _wasConnected = connected();
            emit statusChanged();
            }
      }

void LiveClipsLink::resync()
      {
      _sent.clear();
      _song = LiveClips::Song();
      _songConfirmed = false;
      _queue.clear();
      _sentMode.clear();
      sendMode();
      scoreChanged();
      _debounce->start(0);
      }

void LiveClipsLink::sendMode()
      {
      if (!_socket)
            return;
      const QString mode = active() ? "clips" : "stream";
      if (mode == _sentMode)
            return;
      _sentMode = mode;
      send(LiveClips::osc("/ms/mode", { mode }));
      }

//---------------------------------------------------------
//   following Live
//---------------------------------------------------------

void LiveClipsLink::transport(bool playing, double beat, double bpm)
      {
      const bool changed = playing != _livePlaying || std::fabs(beat - _liveBeat) > 1e-6;
      _livePlaying = playing;
      _liveBeat = beat;
      if (std::fabs(bpm - _liveBpm) > 1e-6) {
            _liveBpm = bpm;
            emit statusChanged();
            }
      if (!active() || !seq || !_score || seq->score() != _score || !seq->isRunning())
            return;
      const LiveClips::Timeline tl = LiveClips::timeline(_score);
      const int utick = std::max(0, tl.utick(beat));
      if (playing) {
            if (!seq->isPlaying() && _following && QDateTime::currentMSecsSinceEpoch() - _startedAt < 1000)
                  return;                 // (started: the audio thread takes it up at its next period)
            if (!seq->isPlaying()) {
                  log(QString("Live plays: MuseScore starts at tick %1 (beat %2)").arg(utick).arg(beat));
                  _startingFromLive = true;
                  seq->seek(utick);
                  seq->start();
                  seq->seek(utick);       // (start plays from the score's play position: a repeat's first pass)
                  _startingFromLive = false;
                  _following = true;
                  _startedAt = QDateTime::currentMSecsSinceEpoch();
                  return;
                  }
            if (QDateTime::currentMSecsSinceEpoch() - _startedAt < 1000)
                  return;                 // (just started or moved: the sequencer's position follows at its next period)
            const double here = tl.beats(seq->getCurTick());
            if (std::fabs(here - beat) * 60.0 / tl.bpm > FOLLOW_TOLERANCE) {
                  _startedAt = QDateTime::currentMSecsSinceEpoch();
                  log(QString("off by %1 s: MuseScore moves to tick %2").arg((here - beat) * 60.0 / tl.bpm).arg(utick));
                  seq->seek(utick);
                  }
            _following = true;
            }
      else if (seq->isPlaying()) {
            if (_following) {
                  log(QString("Live stopped at beat %1: MuseScore stops").arg(beat));
                  _following = false;
                  seq->stop();
                  }
            }
      else if (changed) {
            log(QString("Live's position: beat %1, the cursor to tick %2").arg(beat).arg(utick));
            seq->seek(utick);             // the cursor follows Live's
            }
      }

bool LiveClipsLink::startRequested(int utick)
      {
      if (!active() || !connected() || _startingFromLive || !_score)
            return false;
      const LiveClips::Timeline tl = LiveClips::timeline(_score);
      send(LiveClips::osc("/ms/play", { tl.beats(utick) }));
      log(QString("Play: Live asked to start at beat %1").arg(tl.beats(utick)));
      if (mscore)
            mscore->showMessage(tr("Live plays the score: starting Live…"), 3000);
      return true;
      }

void LiveClipsLink::stopRequested()
      {
      if (!active() || !connected())
            return;
      if (_following && !_livePlaying)
            return;                       // (Live stopped: MuseScore follows)
      _following = false;
      if (_livePlaying)
            send(LiveClips::osc("/ms/stop", {}));
      }

//---------------------------------------------------------
//   rendering, a few measures at a time
//---------------------------------------------------------

void LiveClipsLink::scoreChanged()
      {
      if (!_on)
            return;
      _step->stop();                // a render under way is stale
      _renderer.reset();
      _events.clear();
      _nextUtick = -1;
      _debounce->start();
      }

void LiveClipsLink::startRender()
      {
      if (!active() || !_score || !connected())
            return;                 // (a hello from the device starts it)
      if (_score->undoStack()->active()) {      // in the middle of a command
            _debounce->start();
            return;
            }
      _renderer.reset(new MidiRenderer(_score));
      _renderer->setForLiveClips(true);   // (the automation lanes of plug-in parameters: for the device)
      _events.clear();
      _nextUtick = 0;
      _renderStarted = QDateTime::currentMSecsSinceEpoch();
      _routesGeneration = SoundLib::routesGeneration();
      _portNames = outputPortNames();
      _step->start();
      }

void LiveClipsLink::renderStep()
      {
      if (!_renderer || !_score || _nextUtick < 0)
            return;
      if (_score->undoStack()->active()) {
            scoreChanged();
            return;
            }
      QElapsedTimer t;
      t.start();
      const SynthesizerState ss = mscore ? mscore->synthesizerState() : SynthesizerState();
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      ctx.renderHarmony = true;
      while (t.elapsed() < STEP_BUDGET_MS) {
            const MidiRenderer::Chunk chunk = _renderer->getChunkAt(_nextUtick);
            if (!chunk) {
                  finish();
                  return;
                  }
            _renderer->renderChunk(chunk, &_events, ctx);
            _nextUtick = chunk.utick2();
            }
      _step->start();
      }

// the parameters' plug-in ids as last seen on a loaded instance (Live names Kontakt's slots by number), in the hash
void LiveClipsLink::resolveParameterIds(std::vector<LiveClips::Track>* tracks, const SoundLib::Library& library)
      {
      for (LiveClips::Track& t : *tracks) {
            if (t.params.empty())
                  continue;
            for (LiveClips::Track::ParamLane& pl : t.params)
                  pl.id = SoundLibraryHost::knownParameterId(library, t.patch, pl.title);
            t.paramsHash ^= quint32(qHash(t.patch));
            for (const LiveClips::Track::ParamLane& pl : t.params)
                  t.paramsHash = t.paramsHash * 31u + quint32(pl.id + 1);
            }
      }

// the score's clips as finish() makes them, rendered at once (Create Live Set: the lanes the device keeps in the set)
std::vector<LiveClips::Track> LiveClipsLink::renderTracks(MasterScore* score, const SoundLib::Library& library,
                                                          const QStringList& portNames)
      {
      EventMap events;
      MidiRenderer r(score);
      r.setForLiveClips(true);
      const SynthesizerState ss = mscore ? mscore->synthesizerState() : SynthesizerState();
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      ctx.renderHarmony = true;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &events, ctx);
            utick = c.utick2();
            }
      std::vector<LiveClips::Track> tracks = LiveClips::tracks(score, library, events, portNames, LiveClips::timeline(score));
      resolveParameterIds(&tracks, library);
      return tracks;
      }

void LiveClipsLink::finish()
      {
      QElapsedTimer took;
      took.start();
      _renderer.reset();
      _nextUtick = -1;
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      _timeline = LiveClips::timeline(_score);
      std::vector<LiveClips::Track> tracks;
      if (library && _score)
            tracks = LiveClips::tracks(_score, *library, _events, _portNames, _timeline);
      if (library)
            resolveParameterIds(&tracks, *library);
      _events.clear();

      sendMode();
      // the tempo and the bar locators first (the clips' lengths are in the same beats)
      const LiveClips::Song song = LiveClips::song(_score, _timeline);
      if (song.hash != _song.hash) {
            _song = song;
            _songConfirmed = false;
            _songStatus.clear();
            for (const QByteArray& p : LiveClips::packets(_song, ++_generation))
                  _queue.push_back(p);
            }
      std::map<QString, Sent> keep;
      std::vector<LiveClips::Track> changed, paramsChanged;
      for (const LiveClips::Track& t : tracks) {
            auto s = _sent.find(t.key);
            if (s != _sent.end() && s->second.track.hash == t.hash) {
                  keep[t.key] = s->second;
                  if (s->second.track.paramsHash != t.paramsHash) {
                        keep[t.key].track = t;
                        paramsChanged.push_back(t);
                        }
                  continue;
                  }
            keep[t.key].track = t;
            changed.push_back(t);
            }
      for (const auto& s : _sent)             // routes gone: their clips go
            if (!keep.count(s.first))
                  _queue.push_back(LiveClips::clearPacket(s.second.track, ++_generation));
      _sent = keep;
      for (const LiveClips::Track& t : changed) {
            enqueue(t);
            enqueueParams(t);
            }
      for (const LiveClips::Track& t : paramsChanged)
            enqueueParams(t);
      if (!_queue.empty() && !_pace->isActive())
            _pace->start();
      log(QString("rendered in %1 ms (after the change: +%2 ms), clips made in %3 ms: %4 route(s), %5 changed, %6 datagram(s)")
          .arg(QDateTime::currentMSecsSinceEpoch() - _renderStarted).arg(DEBOUNCE_MS).arg(took.elapsed())
          .arg(tracks.size()).arg(changed.size()).arg(_queue.size()));
      emit statusChanged();
      }

void LiveClipsLink::enqueue(const LiveClips::Track& t)
      {
      Sent& s = _sent[t.key];
      s.sentAt = QDateTime::currentMSecsSinceEpoch();
      s.tries++;
      s.confirmed = false;
      for (const QByteArray& p : LiveClips::packets(t, ++_generation))
            _queue.push_back(p);
      if (!_pace->isActive())
            _pace->start();
      }

void LiveClipsLink::enqueueParams(const LiveClips::Track& t)
      {
      Sent& s = _sent[t.key];
      s.paramsConfirmed = false;
      s.paramsStatus.clear();
      if (_deviceProtocol < 3 && t.params.empty())
            return;           // (an older device: nothing to say; with lanes it hears them once a newer one loads)
      s.paramsSentAt = QDateTime::currentMSecsSinceEpoch();
      s.paramsTries++;
      for (const QByteArray& p : LiveClips::paramPackets(t, ++_generation))
            _queue.push_back(p);
      if (!_pace->isActive())
            _pace->start();
      }

void LiveClipsLink::send(const QByteArray& packet)
      {
      if (_socket)
            _socket->writeDatagram(packet, QHostAddress::LocalHost, quint16(_port));
      }

void LiveClipsLink::sendSome()
      {
      if (!_socket || _queue.empty()) {
            _pace->stop();
            return;
            }
      for (int i = 0; i < PACKETS_PER_PACE && !_queue.empty(); ++i) {
            send(_queue.front());
            _queue.pop_front();
            }
      }

// once a second: what the device hasn't confirmed, sent again; the routes, ports or mode changed
// without an edit of the score (Preferences, the Mixer, a part's playback mode); the connection
void LiveClipsLink::poll()
      {
      if (!_on)
            return;
      if (_wasConnected != connected()) {
            _wasConnected = connected();
            emit statusChanged();
            }
      if (seq && seq->liveClips() != active()) {
            applySeqMode();
            sendMode();
            scoreChanged();
            }
      if (!connected())
            return;
      if (_nextUtick < 0 && !_debounce->isActive()
          && (_routesGeneration != SoundLib::routesGeneration() || _portNames != outputPortNames()))
            scoreChanged();
      if (!_queue.empty())
            return;
      const qint64 t = QDateTime::currentMSecsSinceEpoch();
      for (auto& s : _sent) {
            if (!s.second.confirmed && s.second.tries < MAX_TRIES && t - s.second.sentAt >= CONFIRM_MS) {
                  const LiveClips::Track track = s.second.track;
                  enqueue(track);
                  enqueueParams(track);
                  continue;
                  }
            if (_deviceProtocol >= 3 && s.second.paramsSentAt && !s.second.paramsConfirmed && s.second.paramsTries < MAX_TRIES
                && t - s.second.paramsSentAt >= CONFIRM_MS) {
                  const LiveClips::Track track = s.second.track;
                  enqueueParams(track);
                  }
            }
      }

void LiveClipsLink::sendDatagramTo(const QByteArray& packet, int port)
      {
      if (_socket)
            _socket->writeDatagram(packet, QHostAddress::LocalHost, quint16(port));
      }

QString LiveClipsLink::routeKey(const Part* part) const
      {
      const std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
      if (!part || !lib || !_on || !active() || !_score || part->masterScore() != _score.data())
            return QString();
      const Part* master = PartPlaybackModes::masterPart(part);
      for (const SoundLib::Route& r : SoundLib::routes(_score.data(), *lib))
            if (r.part == master && r.patch == 0 && r.lane == 0)
                  return QString("%1:%2").arg(r.port).arg(r.channel + 1);
      return QString();
      }

QStringList LiveClipsLink::keysWithoutTrack(const MasterScore* score, bool* known) const
      {
      QStringList keys;
      bool all = _on && active() && connected() && score && _score == score && !_sent.empty();
      for (const auto& s : _sent) {
            if (!s.second.confirmed)
                  all = false;
            else if (s.second.status.startsWith("no track"))
                  keys << s.first;
            }
      if (known)
            *known = all;
      return all ? keys : QStringList();
      }

QString LiveClipsLink::statusText() const
      {
      if (!_on)
            return tr("Off: MuseScore plays through Live (Mixer › Play through Live) and is the clock.");
      if (!playingThroughMidi())
            return tr("Waiting: turn on Mixer › Play through Live (Live hosts the library).");
      if (!connected())
            return tr("Waiting for the MuseScore Link device in Live (UDP port %1).").arg(_port);
      int ok = 0, waiting = 0, dropped = 0, high = 0;
      QStringList problems;
      for (const auto& s : _sent) {
            dropped += s.second.track.dropped;
            high += s.second.track.highNotes;
            if (!s.second.confirmed)
                  ++waiting;
            else if (s.second.status == "ok")
                  ++ok;
            else
                  problems << QString("%1: %2").arg(s.second.track.clip.mid(QString("MuseScore: ").size()), s.second.status);
            if (!s.second.track.params.empty()) {
                  if (_deviceProtocol < 3)
                        problems << tr("%1: automation of plug-in parameters needs the MuseScore Link device of this MuseScore")
                                    .arg(s.second.track.clip.mid(QString("MuseScore: ").size()));
                  else if (s.second.paramsConfirmed && s.second.paramsStatus != "ok")
                        problems << QString("%1: %2").arg(s.second.track.clip.mid(QString("MuseScore: ").size()), s.second.paramsStatus);
                  }
            }
      QString text = tr("Connected to Live. %n clip(s) up to date", "", ok);
      if (waiting)
            text += tr(", %n being sent", "", waiting);
      text += ".";
      if (_lastSync)
            text += " " + tr("Last update: %1.").arg(QDateTime::fromMSecsSinceEpoch(_lastSync).toString("HH:mm:ss"));
      if (_timeline.score && _liveBpm > 0 && std::fabs(_liveBpm - _timeline.bpm) > 0.01)
            text += "\n" + tr("Live's tempo is %1, the clips are at %2 bpm: set Live's tempo back (and leave its tempo "
                              "automation empty).").arg(_liveBpm, 0, 'f', 2).arg(_timeline.bpm, 0, 'f', 2);
      if (_songConfirmed && _songStatus != "ok")
            text += "\n" + tr("Tempo and locators: %1").arg(_songStatus);
      if (!problems.isEmpty())
            text += "\n" + tr("Not in Live: %1").arg(problems.join("; "));
      if (dropped)
            text += "\n" + tr("%n event(s) a clip can't hold were left out (other controllers).",
                              "", dropped);
      if (high)
            text += "\n" + tr("%n note(s) at F#8 or above were left out (those keys carry the controllers and the pitch bend).", "", high);
      return text;
      }

}     // namespace LiveIntegration
}     // namespace Ms
