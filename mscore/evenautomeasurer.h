//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  EvenAutoMeasurer: the even levels measured in the background near edits (libmscore/evenauto.h has the design and
//  the rounds; the owner, 2026-10-13: a note plays its measured correction or playback waits for it).
//
//    when        [levels] evenLevels plays for the score and the library plays through its plug-in (EvenAuto::active);
//                else idle (nothing loaded). After an edit (MuseScore::endCmd: the undo stack's state changed), once the
//                edits pause (DEBOUNCE_MS), and when a score is shown (OPEN_DELAY_MS, after the host's preload starts):
//                EvenAuto::Session::detect on the main thread (one render of the score), the cache's values written at
//                once, the rest measured
//    where       plug-in instances of its own, one per part and patch with something to measure, loaded on the main
//                thread when first needed (SoundLibraryExport::loadInstance: the same setup, controllers and parameter
//                ids as playback's; its setState on a worker thread meanwhile, beginInstance / finishInstance, the
//                window free: the edit lag, 2026-10-17), each with a worker thread: played a test note in real time until it sounds
//                (PlaybackVerifier::waitUntilSounding: Kontakt goes on loading samples), then offline (beginExport)
//                rendering the jobs given to it (EvenMeasure::run). Parts measure in parallel (an instance each). The
//                main playback's instances are never touched (note previews while editing play as before). Released
//                when another score is shown, the setting goes off, or MuseScore quits
//    playback    while MuseScore plays, the workers pause between blocks (Kontakt's real-time playback keeps the CPU);
//                an edit made while playing is measured after it stops. Running playback is never stopped or held
//    Play        Seq::start asks prepare() (an edit not yet looked at, or the score just opened: detect now) and then
//                holdPlay(): when a note being measured (queued or rendering; at opening every passage is queued) has its
//                note-on in what is about to play, from the transport's start (PlayStart: the pre-roll of early notes
//                included) to the loop's end (else the score's), playback waits (the owner, 2026-10-13: "Wait for the
//                measurement", also at opening): a countdown in the status bar ("playing in ~n s": the rendering still
//                to do at the speed measured so far, or the VM's until one is; "loading" while a plug-in it needs
//                loads), then plays (resume: Seq::start again, now with the values). Stop (or Esc) cancels the wait.
//                Otherwise it plays at once. Running playback is never stopped
//    order       each instance renders its queue by EvenAuto::priority from the focus: the play position (at each
//                detect), or the start of what Play waits for: the windows there first, from it forward, then the
//                rest of the score in the background; instances load in the same order
//    values      written without an undo step (evenauto.h): the score is marked modified (MasterScore::
//                setEvenLevelsWritten); the per-user cache (<setups folder>/even cache.json, EvenLevels::Cache) keeps every
//                value by its context, so an undo or redo restores values without rendering
//
//  Untried with Kontakt and SSO (2026-10-13: built and tested on Linux with the test synth only).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __EVENAUTOMEASURER_H__
#define __EVENAUTOMEASURER_H__

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "libmscore/evenauto.h"
#include "libmscore/evenlevels.h"
#include "soundlibraryhost.h"

namespace Ms {

class MasterScore;
class Score;

class EvenAutoMeasurer : public QObject {
      Q_OBJECT

      static constexpr int RATE = 44100;              // the job's
      static constexpr int DEBOUNCE_MS = 600;         // (UI timing: after the last edit)
      static constexpr int OPEN_DELAY_MS = 1500;      // (UI timing: after a score is shown)
      static constexpr int LOAD_POLL_MS = 20;         // (UI timing: a load's setState on its worker looked at)

      struct Instance;
      QPointer<MasterScore> _score;
      std::unique_ptr<EvenAuto::Session> _session;
      EvenLevels::Cache _cache;
      QString _cacheLibrary;
      std::map<QString, std::unique_ptr<Instance>> _instances;
      std::set<QString> _failed;                      // instances that could not load (this score)
      std::vector<EvenAuto::Job> _toLoad;             // waiting for their instance
      // the instance loading in steps (its setState on a worker: SoundLibraryExport::beginInstance) and the job it is for
      std::unique_ptr<SoundLibraryExport::InstanceLoad, void(*)(SoundLibraryExport::InstanceLoad*)> _pendingLoad { nullptr, nullptr };
      EvenAuto::Job _pendingJob;
      QElapsedTimer _pendingClock;
      QTimer _debounce;
      QTimer _loadTimer;
      QTimer _countdown;
      QTimer _saveTimer;
      int _undoState { -1 };
      bool _detectAfterPlay { false };
      std::atomic<bool> _paused { false };
      std::atomic<bool> _probePaused { false };       // (the lag probe's)
      std::mutex _liveMutex;
      std::set<int> _live;                            // the Works' ids still wanted (the workers skip the rest)
      std::atomic<double> _focus { 0 };               // seconds: the jobs from here on first (EvenAuto::priority)
      double _speed { 0 };                            // audio seconds per second of rendering, as measured (0: none yet)

      bool _holding { false };
      double _holdFrom { 0 }, _holdTo { 0 };
      std::function<void()> _resume;
      QElapsedTimer _holdClock;

      EvenAutoMeasurer();
      bool ready(Score* cs);                          // the session for cs (made when needed); false: idle
      void detect();
      void dispatch(const EvenAuto::Job& job);
      void loadStep();
      void loaded(const EvenAuto::Job& job, std::shared_ptr<Vst3Synth> vst, const QString& error);
      void onResult(const EvenAuto::Result& r);
      void updateLive();
      void checkHold();
      void countdownStep();
      void releaseInstances();
      void worker(Instance* in);
      double playSeconds() const;                     // the score's play position (seconds)
      bool loading(const std::vector<QString>& instances) const;     // one of them not loaded or not sounding yet

   public:
      ~EvenAutoMeasurer();
      static EvenAutoMeasurer* instance();
      void setScore(Score* cs);           // the score shown (MuseScore::setCurrentScoreView)
      void edited(Score* cs);             // after a command (MuseScore::endCmd)
      void playing(bool on);              // Seq started / stopped
      void prepare(Score* cs);            // Play pressed: an edit not looked at yet, now
      // Play pressed, the transport about to start at from seconds (to: the loop's or score's end): true when it waits
      // (resume called once the notes in it are measured)
      bool holdPlay(Score* cs, double from, double to, std::function<void()> resume);
      void cancelHold();                  // Stop
      bool holding() const { return _holding; }
      // the lag probe only (MS_LAG_PROBE, lagprobesession.h): work queued, rendering or an instance to load; paused:
      // edits not looked at, Play doesn't wait, the workers idle (the probe's "measurement paused" phases); refresh: the
      // session made or dropped for the setting as it is now
      bool probeBusy() const;
      void setProbePaused(bool on);
      void probeRefresh(Score* cs);
      void shutdown();                    // MuseScore quits: workers stopped, instances released, cache saved
      };

} // namespace Ms
#endif
