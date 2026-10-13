//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  EvenAutoMeasurer (evenautomeasurer.h)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "evenautomeasurer.h"

#include <chrono>
#include <condition_variable>
#include <thread>

#include <cmath>

#include "libmscore/repeatlist.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/undo.h"
#include "musescore.h"
#include "soundlibrarycheck.h"
#include "soundlibraryhost.h"

#ifdef USE_VST3
#include "audio/vst3/evenmeasure.h"
#include "audio/vst3/vst3synth.h"
#endif

namespace Ms {

//---------------------------------------------------------
//   Instance
//---------------------------------------------------------

struct EvenAutoMeasurer::Instance {
      QString key;
      QString patch;
#ifdef USE_VST3
      std::shared_ptr<Vst3Synth> vst;
#endif
      std::thread thread;
      std::mutex mutex;
      std::condition_variable cv;
      std::vector<EvenAuto::Job> queue;           // (taken by EvenAuto::next: the focus first)
      std::atomic<bool> sounding { false };       // past its test note: rendering jobs
      bool stop { false };
      };

EvenAutoMeasurer::EvenAutoMeasurer()
      {
      _debounce.setSingleShot(true);
      connect(&_debounce, &QTimer::timeout, this, [this]() { detect(); });
      _loadTimer.setSingleShot(true);
      _loadTimer.setInterval(0);
      connect(&_loadTimer, &QTimer::timeout, this, [this]() { loadStep(); });
      _countdown.setInterval(250);
      connect(&_countdown, &QTimer::timeout, this, [this]() { countdownStep(); });
      _saveTimer.setSingleShot(true);
      _saveTimer.setInterval(5000);
      connect(&_saveTimer, &QTimer::timeout, this, [this]() { _cache.save(); });
      }

EvenAutoMeasurer::~EvenAutoMeasurer()
      {
      shutdown();
      }

EvenAutoMeasurer* EvenAutoMeasurer::instance()
      {
      static EvenAutoMeasurer* m = new EvenAutoMeasurer;
      return m;
      }

static void logLine(const QString& line)
      {
      qDebug("%s", qPrintable(line));
      }

//---------------------------------------------------------
//   ready
//---------------------------------------------------------

bool EvenAutoMeasurer::ready(Score* cs)
      {
#ifdef USE_VST3
      MasterScore* ms = cs ? cs->masterScore() : nullptr;
      if (!ms || !EvenAuto::active(ms)) {
            if (_session && (!ms || _session->score() == ms)) {
                  cancelHold();
                  _session.reset();
                  releaseInstances();
                  }
            return false;
            }
      if (_session && _session->score() == ms)
            return true;
      cancelHold();
      _session.reset();
      releaseInstances();
      _failed.clear();
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (library->name != _cacheLibrary) {
            _cache.save();
            _cacheLibrary = library->name;
            _cache.load(SoundLibraryHost::setupsFolder(*library) + "/even cache.json");
            }
      _score = ms;
      _session.reset(new EvenAuto::Session(ms, &_cache));
      _session->setLog(logLine);
      if (mscore)
            _session->setSynthesizerState(mscore->synthesizerState());
      _undoState = ms->undoStack()->state();
      return true;
#else
      Q_UNUSED(cs);
      return false;
#endif
      }

//---------------------------------------------------------
//   setScore / edited / playing / prepare
//---------------------------------------------------------

void EvenAutoMeasurer::setScore(Score* cs)
      {
      MasterScore* ms = cs ? cs->masterScore() : nullptr;
      if (_session && _session->score() == ms)
            return;
      if (!ready(cs))
            return;
      _session->edited();
      _debounce.start(OPEN_DELAY_MS);
      }

void EvenAutoMeasurer::edited(Score* cs)
      {
      if (!cs)
            return;
      MasterScore* ms = cs->masterScore();
      if (_session && _session->score() == ms && ms->undoStack()->state() == _undoState)
            return;                       // (a command that changed nothing: a selection)
      if (!ready(cs))
            return;
      _undoState = ms->undoStack()->state();
      _session->edited();
      _debounce.start(DEBOUNCE_MS);
      }

void EvenAutoMeasurer::playing(bool on)
      {
      _paused = on;
      if (!on && _detectAfterPlay) {
            _detectAfterPlay = false;
            _debounce.start(DEBOUNCE_MS);
            }
      }

void EvenAutoMeasurer::prepare(Score* cs)
      {
      if (!ready(cs))
            return;
      if (_session->needsDetect() || cs->masterScore()->playlistDirty()) {
            _session->edited();
            _debounce.stop();
            detect();
            }
      }

//---------------------------------------------------------
//   detect
//---------------------------------------------------------

void EvenAutoMeasurer::detect()
      {
      if (!_session || !_score)
            return;
      if (_paused) {
            _detectAfterPlay = true;      // (measured once playback stops)
            return;
            }
      if (!EvenAuto::active(_score)) {
            ready(_score);
            return;
            }
      if (mscore)
            _session->setSynthesizerState(mscore->synthesizerState());
      if (!_holding)
            _focus = playSeconds();       // (the windows from the play position on first)
      const bool wasWritten = _score->evenLevelsWritten();
      const std::vector<EvenAuto::Job> jobs = _session->detect();
      updateLive();
      for (const EvenAuto::Job& j : jobs)
            dispatch(j);
      if (!wasWritten && _score->evenLevelsWritten() && mscore)
            mscore->dirtyChanged(_score);
      if (_session->fromCache() || !jobs.empty())
            _saveTimer.start();
      checkHold();
      }

void EvenAutoMeasurer::updateLive()
      {
      std::set<int> live;
      if (_session)
            for (int id : _session->workIds())
                  live.insert(id);
      std::lock_guard<std::mutex> lock(_liveMutex);
      _live.swap(live);
      }

//---------------------------------------------------------
//   dispatch / loadStep
//    a job to its instance's worker; an instance not loaded yet is loaded on the main thread, one per event-loop turn
//---------------------------------------------------------

void EvenAutoMeasurer::dispatch(const EvenAuto::Job& job)
      {
      if (_failed.count(job.instance)) {
            EvenAuto::Result r;
            r.id = job.id;
            r.round = job.round;
            r.error = "the plug-in instance could not be loaded";
            QTimer::singleShot(0, this, [this, r]() { onResult(r); });
            return;
            }
      auto i = _instances.find(job.instance);
      if (i == _instances.end()) {
            _toLoad.push_back(job);
            _loadTimer.start();
            return;
            }
      Instance* in = i->second.get();
      {
            std::lock_guard<std::mutex> lock(in->mutex);
            in->queue.push_back(job);
      }
      in->cv.notify_one();
      }

void EvenAutoMeasurer::loadStep()
      {
#ifdef USE_VST3
      if (_toLoad.empty() || !_score)
            return;
      const int k = EvenAuto::next(_toLoad, _focus);      // (the instance the focus needs first)
      const EvenAuto::Job job = _toLoad[size_t(k)];
      _toLoad.erase(_toLoad.begin() + k);
      if (!_instances.count(job.instance) && !_failed.count(job.instance)) {
            std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
            const SoundLib::Route* route = nullptr;
            std::vector<SoundLib::Route> routes;
            if (library)
                  routes = SoundLib::routes(_score, *library);
            for (const SoundLib::Route& r : routes)
                  if (r.port * 16 + r.channel == job.route)
                        route = &r;
            QString error;
            std::shared_ptr<Vst3Synth> vst = route ? SoundLibraryExport::loadInstance(_score, *route, float(RATE), &error) : nullptr;
            if (!vst) {
                  _failed.insert(job.instance);
                  logLine(QString("even levels: could not load %1: %2").arg(job.patch, error));
                  if (mscore)
                        mscore->showMessage(tr("Even levels: could not load %1 for measuring: %2").arg(job.patch, error), 10000);
                  }
            else {
                  std::unique_ptr<Instance> in(new Instance);
                  in->key = job.instance;
                  in->patch = job.patch;
                  in->vst = vst;
                  Instance* p = in.get();
                  _instances[job.instance] = std::move(in);
                  p->thread = std::thread([this, p]() { worker(p); });
                  }
            }
      dispatch(job);
      if (!_toLoad.empty())
            _loadTimer.start();
#endif
      }

//---------------------------------------------------------
//   worker
//---------------------------------------------------------

void EvenAutoMeasurer::worker(Instance* in)
      {
#ifdef USE_VST3
      SoundLibraryHost::workerThread(true);
      // until it sounds: a test note in real time, as PlaybackVerifier::waitUntilSounding plays it (Kontakt goes on
      // loading samples after its state is set), here given up when the instance is released
      std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
      const SoundLib::LibInstrument* li = lib ? SoundLibraryHost::findPatch(*lib, in->patch) : nullptr;
      if (li && !li->kit) {
            const int waitLimit = qEnvironmentVariableIsSet("MS_VERIFY_LOAD_WAIT") ? qEnvironmentVariableIntValue("MS_VERIFY_LOAD_WAIT") : 300;
            const int pitch = ArticulationCheckDialog::testPitch(*li);
            const unsigned block = 512;
            std::vector<float> buf(2 * block);
            QElapsedTimer waited;
            waited.start();
            bool sounded = false;
            while (!sounded && !in->stop && waited.elapsed() < waitLimit * 1000) {
                  if (li->switchType == SoundLib::SwitchType::CC && !li->articulations.empty())
                        in->vst->play(PlayEvent(ME_CONTROLLER, 0, li->switchNumber, li->articulations.front().value));
                  if (lib->dynamicsCC >= 0)
                        in->vst->play(PlayEvent(ME_CONTROLLER, 0, lib->dynamicsCC, 100));
                  in->vst->play(PlayEvent(ME_NOTEON, 0, pitch, 100));
                  for (int b = 0; b < int(1.6 * RATE / block) && !sounded && !in->stop; ++b) {
                        if (b == int(1.2 * RATE / block))
                              in->vst->play(PlayEvent(ME_NOTEON, 0, pitch, 0));
                        std::fill(buf.begin(), buf.end(), 0.f);
                        in->vst->process(block, buf.data(), nullptr, nullptr);
                        for (float x : buf)
                              sounded = sounded || std::fabs(x) > 1e-4f;
                        std::this_thread::sleep_for(std::chrono::microseconds(long(1e6 * block / RATE)));
                        }
                  in->vst->play(PlayEvent(ME_NOTEON, 0, pitch, 0));
                  }
            in->vst->allNotesOff(-1);
            for (int b = 0; b < int(2 * RATE / block) && !in->stop; ++b)
                  in->vst->process(block, buf.data(), nullptr, nullptr);       // (what rings of the test notes: gone)
            logLine(QString("even levels: %1 %2 after %3 s").arg(in->patch, sounded ? "sounds" : "never sounded")
                    .arg(waited.elapsed() / 1000.0, 0, 'f', 1));
            }
      in->vst->beginExport(float(RATE));
      in->sounding = true;
      for (;;) {
            EvenAuto::Job job;
            {
                  std::unique_lock<std::mutex> lock(in->mutex);
                  in->cv.wait(lock, [in]() { return in->stop || !in->queue.empty(); });
                  if (in->stop)
                        break;
                  const int k = EvenAuto::next(in->queue, _focus);     // the focus first (the wait as short as can be)
                  job = in->queue[size_t(k)];
                  in->queue.erase(in->queue.begin() + k);
            }
            auto wanted = [this, &job]() {
                  std::lock_guard<std::mutex> lock(_liveMutex);
                  return _live.count(job.id) > 0;
                  };
            if (!wanted())
                  continue;
            in->vst->idle();              // (the plug-in's parameter changes and MIDI mapping, between renders)
            const EvenAuto::Result r = EvenMeasure::run(in->vst.get(), job, RATE, [this, in, &wanted]() {
                  while (_paused && !in->stop)
                        std::this_thread::sleep_for(std::chrono::milliseconds(20));
                  return !in->stop && wanted();
                  });
            if (in->stop)
                  break;
            QMetaObject::invokeMethod(this, [this, r]() { onResult(r); }, Qt::QueuedConnection);
            }
      in->vst->endExport();
      SoundLibraryHost::workerThread(false);
#else
      Q_UNUSED(in);
#endif
      }

//---------------------------------------------------------
//   onResult
//---------------------------------------------------------

void EvenAutoMeasurer::onResult(const EvenAuto::Result& r)
      {
      if (!_session || !_score)
            return;
      const bool wasWritten = _score->evenLevelsWritten();
      EvenAuto::Job next;
      if (_session->result(r, &next))
            dispatch(next);
      updateLive();
      if (r.ok && r.renderSeconds > 0.05) {
            const double s = r.audioSeconds / r.renderSeconds;
            _speed = _speed > 0 ? 0.7 * _speed + 0.3 * s : s;
            }
      if (!wasWritten && _score->evenLevelsWritten() && mscore)
            mscore->dirtyChanged(_score);
      if (!_session->busy()) {
            _saveTimer.start();
            if (mscore && !_holding)
                  mscore->showMessage(tr("Even levels: measured"), 3000);
            }
      checkHold();
      }

//---------------------------------------------------------
//   holdPlay / checkHold / countdown / cancelHold
//---------------------------------------------------------

bool EvenAutoMeasurer::holdPlay(Score* cs, double from, double to, std::function<void()> resume)
      {
      if (!_session || !cs || cs->masterScore() != _session->score())
            return false;
      if (!_session->mustWait(from, to))
            return false;
      _holding = true;
      _holdFrom = from;
      _holdTo = to;
      _focus = from;                // (what Play waits for first, from its start forward)
      _resume = resume;
      _holdClock.start();
      logLine(QString("even levels: Play waits for %1").arg(_session->pendingText(from, to).join("; ")));
      _paused = false;
      countdownStep();
      _countdown.start();
      return true;
      }

void EvenAutoMeasurer::checkHold()
      {
      if (!_holding)
            return;
      if (_session && _session->mustWait(_holdFrom, _holdTo))
            return;
      _holding = false;
      _countdown.stop();
      if (mscore)
            mscore->showMessage(QString(), 1);
      logLine(QString("even levels: Play waited %1 s").arg(_holdClock.elapsed() / 1000.0, 0, 'f', 1));
      std::function<void()> f = _resume;
      _resume = nullptr;
      if (f)
            QTimer::singleShot(0, this, f);
      }

void EvenAutoMeasurer::countdownStep()
      {
      if (!_holding || !_session || !mscore)
            return;
      const QStringList what = _session->pendingText(_holdFrom, _holdTo);
      const double audio = _session->pendingAudioSeconds(_holdFrom, _holdTo);
      // (until a render here is timed: the VM's speed, EvenLevels::RENDER_SPEED)
      const double speed = _speed > 0 ? _speed : EvenLevels::RENDER_SPEED;
      QString when = tr("playing in ~%1 s").arg(std::max(1, int(std::ceil(audio / speed))));
      if (loading(_session->pendingInstances(_holdFrom, _holdTo)))
            when = tr("loading the plug-in, then ~%1 s").arg(std::max(1, int(std::ceil(audio / speed))));
      mscore->showMessage(tr("Even levels: measuring %1 … %2 (Stop cancels)").arg(what.join("; "), when), 0);
      }

void EvenAutoMeasurer::cancelHold()
      {
      if (!_holding)
            return;
      _holding = false;
      _resume = nullptr;
      _countdown.stop();
      if (mscore)
            mscore->showMessage(QString(), 1);
      }

//---------------------------------------------------------
//   playSeconds / loading
//---------------------------------------------------------

double EvenAutoMeasurer::playSeconds() const
      {
      if (!_score)
            return 0;
      MasterScore* ms = _score.data();
      return ms->utick2utime(ms->repeatList().tick2utick(ms->playPos().ticks()));
      }

bool EvenAutoMeasurer::loading(const std::vector<QString>& instances) const
      {
      for (const QString& key : instances) {
            auto i = _instances.find(key);
            if (i == _instances.end()) {
                  if (!_failed.count(key))
                        return true;
                  continue;
                  }
            if (!i->second->sounding)
                  return true;
            }
      return false;
      }

//---------------------------------------------------------
//   releaseInstances / shutdown
//---------------------------------------------------------

void EvenAutoMeasurer::releaseInstances()
      {
      _toLoad.clear();
      for (auto& i : _instances) {
            Instance* in = i.second.get();
            {
                  std::lock_guard<std::mutex> lock(in->mutex);
                  in->stop = true;
                  in->queue.clear();
            }
            in->cv.notify_all();
            }
      for (auto& i : _instances)
            if (i.second->thread.joinable())
                  i.second->thread.join();
      _instances.clear();               // (the plug-ins go on the main thread)
      updateLive();
      }

void EvenAutoMeasurer::shutdown()
      {
      cancelHold();
      _debounce.stop();
      _session.reset();
      releaseInstances();
      _cache.save();
      }

} // namespace Ms
