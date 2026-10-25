//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  SoundLibraryHost: the sound library's plug-in (Kontakt …) hosted in MuseScore, when the
//  library plays through its plug-in (SoundLib::Output::PLUGIN). For the current score's
//  routes (SoundLib::routes: one per library part) it keeps an instance in Vst3Synth's slot
//  of the route, with the instrument's setup loaded:
//
//    setup     the plug-in's state with a library patch loaded (Kontakt with "Violins 1 - All
//              techniques", UACC switching …), loaded into every instance that plays the patch.
//              MuseScore makes it (the owner, 2026-09-27: no manual set-up): for a library whose
//              map has <Files> (a Kontakt library), from the patch's .nki in the library's folder
//              and Kontakt's own state with nothing loaded (a fresh instance's, kept), with the
//              map's script values (KontaktSetup, audio/vst3/kontaktsetup.h); made again when the
//              .nki, the values, the plug-in or the maker change ("made setups.json"). Kept in
//              <data>/soundlibraries/<library>/<patch>.vst3state; setups made by hand earlier
//              are moved to "old setups (not used)" there. A library without <Files> uses such a
//              file if one is there (the tests' plug-ins)
//    editor    the plug-in's window for a part (Vst3EditorWindow), to look at it; what is changed
//              there lasts until the patch loads again
//    export    SoundLibraryExport lends the instances to an audio export
//
//  Loading never holds the window (the owner, 2026-10-24: "is it possible for SSO to load without locking up the rest
//  of MuseScore? just have the playback button grayed out during it"): a new instance is made and its setup read on
//  the GUI thread (Kontakt's component: VST 3 wants it there; made on a worker thread it hung Kontakt, branch
//  load-small), its setState (Kontakt's instrument load: seconds) on a worker thread, one at a time (or
//  io/soundLibraryLoadThreads at once), then settled, resaved and put in its slot on the GUI thread: the same calls
//  in the same order on the same bytes as before, only setState on another thread, while the event loop runs as
//  usual (user input included). While the score's patches load (loading()), Play is grayed (MuseScore::
//  updatePlayAction) and the status bar says "Loading sounds: n of m"; Play pressed when the score needs a patch
//  not loaded (readyToPlay) starts its load instead of playing; an edit that needs new patches loads them in the
//  background (scoreEdited, a second after it); a change that stopped playback (GoOnPlaying) plays on once they are
//  loaded (whenLoaded). Only sync() still waits (an audio export, the tests): user input held back meanwhile
//
//  GUI thread only.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __SOUNDLIBRARYHOST_H__
#define __SOUNDLIBRARYHOST_H__

#include "config.h"

#include <algorithm>
#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <vector>

#include <QDialog>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "libmscore/partcontrollers.h"
#include "libmscore/soundlibrary.h"

class QTableWidget;
class QTreeWidget;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QComboBox;

namespace Ms {

class MasterScore;
class MasterSynthesizer;
class NPlayEvent;
class Vst3Plugin;
class Part;
class Score;
class Vst3EditorWindow;
class Vst3Plugin;
class Vst3Synth;

class SoundLibraryHost : public QObject {
      Q_OBJECT

      struct Slot {
            QString instrument;           // the library instrument it plays
            QString part;
            bool hasSetup { false };      // its setup was loaded
            bool setupFailed { false };   // its setup could not be loaded: not tried again at each play
            QPointer<Vst3EditorWindow> editor;
            // the patch's own value (its setup's) of each plug-in parameter a score has set: put
            // back for a score that doesn't set it, and in the setup saved (a score's values
            // stay in the score; the setup is the library's default). Cleared when a setup loads
            std::map<unsigned, double> patchValues;
            qint64 memory { -1 };         // bytes the process grew by as it loaded (-1: not measured)
            QString tuning;               // its slots' tunings (tuningKey: a patch's copies in one instance, or a copy
                                          // by its Tune); empty: plain, tuned by varispeed
            std::vector<int> routes;      // the slots it plays (SoundLib::Instance::routes; empty: its own)
            QString values;               // the script values its score's playback settings put in its state
                                          // (scriptValuesFor, as text); another score's: loaded again
            };
      std::array<Slot, 64> _slots;
      std::set<int> _lostSlots;           // slots whose instance's host process ended, said (checkHostProcesses)
      void checkHostProcesses();
      // the part each slot plays (its patch, an extra, a copy for another tuning) and the score, from
      // the last sync: the Mixer's values go to the slots (applyMixer). Only compared with the score's
      // parts before use (a part deleted since isn't read)
      std::array<const Part*, 64> _slotParts {};
      std::array<QString, 64> _slotPatches;         // (and its patch: a track level's headroom, TrackDelays::patchGain)
      const MasterScore* _slotScore { nullptr };
      // instances set aside by syncSome: a patch the score being loaded doesn't play in their slot
#ifdef USE_VST3
      struct Spare {
            std::unique_ptr<Vst3Plugin> plugin;
            Slot slot;
            };
      std::vector<Spare> _spares;
      // loads whose setup is being set on a worker thread (loadThreads() > 0), finished (resaved,
      // put in their slot) on the GUI thread
      struct Pending;
      std::vector<std::unique_ptr<Pending>> _pending;
      std::unique_ptr<Pending> beginLoad(int slot, const QString& name, bool setup, const std::vector<double>& cents,
                                         const std::vector<int>& routes, const SoundLib::Library& library,
                                         const QString& path, QString* error,
                                         const std::map<QString, QByteArray>& values = std::map<QString, QByteArray>());
      // the instances' slots linked to their instance (Vst3Synth::setLink), from the slots loaded
      void applyLinks();
      bool _resync { false };             // a patch's tuned instance fell back to plain: its copies need instances of their own
      void finishLoad(std::unique_ptr<Pending> pl, const SoundLib::Library& library);
      void startSetState(Pending* pl, const QByteArray& state, double backgroundLimit);   // on a worker thread
      bool harvest(const SoundLib::Library& library, int keep);   // finished pending loads (keep >= 0: waits till no more are left)
      bool pendingOn(int slot) const;
      // a batch of loads (at play, at score open) for load times.log: what each step took, then how
      // long until the process's memory stops growing (Kontakt goes on loading samples after setState)
      struct Batch;
      std::unique_ptr<Batch> _batch;
      QTimer _settleTimer;
      void batchStart(const QString& kind);
      void batchEnd(const SoundLib::Library& library);
      void settleStep();
#endif
      QTimer _idle;
      QTimer _preloadTimer;
      QPointer<MasterScore> _preloadScore;
      int _loads { 0 };                   // instances loaded so far
      int _preloadFrom { 0 };             // _loads when the preload started
      bool _preloadLogged { false };      // its list of what to load is in load times.log
      bool _syncing { false };            // in syncSome (the event loop runs while it waits for a worker thread)
      bool _preloadKeep { false };        // the preload after an edit: an instance the score no longer needs stays (an
                                          // undo may need it again), released at the next play
      bool _keepUnused { false };         // (syncSome: nothing released, no parameters set: the score isn't about to play)
      // the loading's progress for the status bar and Play (loading()): the score's instances, how many are loaded
      QPointer<MasterScore> _progressScore;
      int _progressTotal { 0 };
      int _progressLoaded { 0 };
      bool _wasLoading { false };         // (loadingChanged: when loading() turns)
      int _stepping { 0 };                // in preloadStep / readyToPlay: loading() is true meanwhile (no flicker between a
                                          // load harvested and the next begun)
      bool _releasing { false };          // in release(): nothing starts loading
      bool _whenLoadedPosted { false };
      QTimer _editCheck;                  // an edit's check for patches it needs (scoreEdited)
      QPointer<MasterScore> _editScore;
      // played once the score's patches are loaded (whenLoaded), for this score
      std::function<void()> _whenLoaded;
      QPointer<MasterScore> _whenLoadedScore;
      QString _preloadKind { "At score open" };   // load times.log's batch: "At score open", "At play", "At an edit"
      void noteLoading();                 // loadingChanged when loading() turned, the status bar's progress
      void editCheck();
      static const MasterScore* shownScore(const MasterScore* fallback);
      QElapsedTimer _lastInput;           // since the user's last key, click or wheel
      static constexpr int INPUT_PAUSE_MS = 0;     // a background load waits for this long a pause (the owner, 2026-09-27: 0, no wait)
      // the pause between two loads at score open, for the window to repaint and take input: 0 (a
      // zero timer runs once the events waiting are done; it was 100 ms, 2.5 s of a full orchestra's 11)
      static constexpr int PRELOAD_GAP_MS = 0;
      void preloadStep();
      void preloadStepInner();
      bool readyToPlayInner(Score* score, QString* error);
      bool syncSome(Score* score, QString* error, int maxLoads, int* remaining);

      SoundLibraryHost();
      ~SoundLibraryHost();

   public:
      static SoundLibraryHost* instance();

      static bool available();            // built with plug-in hosting
      static QStringList pluginFolders(); // the system's VST 3 folders
      static QString pluginPath(const SoundLib::Library& library, QString* error = nullptr);
      static QString setupFile(const SoundLib::Library& library, const QString& instrument);
      static bool hasSetup(const SoundLib::Library& library, const QString& instrument);  // it has one or can make it
      // where the setups (and made setups.json, load times.log, checks.json …) go: <dataPath>/soundlibraries,
      // or this folder (the background extract, --extract-library: its own copy, so the MuseScore the
      // owner works in is never touched)
      static void setDataFolder(const QString& folder);
      static QString setupsFolder(const SoundLib::Library& library);
      // the dynamics calibration (Check articulations › Dynamics): <setups folder>/dynamics.json,
      // read into SoundLib::dynamicsCalibration for the current library
      static QString calibrationFile(const SoundLib::Library& library);
      static void loadCalibration();
      // the render preflight (libmscore/renderpreflight.h: calibration files and each patch's source, setups folder,
      // playback.ini) appended to "render preflight.log" in the setups folder in use and on stderr; job: who renders
      static QStringList logPreflight(const QString& job);

      // setups made by MuseScore
      static bool makesSetups(const SoundLib::Library& library);        // the map has <Files>
      static QString libraryFolder(const SoundLib::Library& library);   // where its .nki are; empty: not found
      static void setLibraryFolder(const SoundLib::Library& library, const QString& folder);
      static QString nkiPath(const SoundLib::Library& library, const QString& patch);
      static const SoundLib::LibInstrument* findPatch(const SoundLib::Library& library, const QString& name);
      static QByteArray setupId(const SoundLib::Library& library, const QString& patch);  // changes with its setup
      // the voice limit MuseScore gives every Kontakt patch it sets up (setupState): 512, or
      // MS_KONTAKT_MAX_VOICES (0: the patch's own)
      static constexpr int KONTAKT_MAX_VOICES = 512;
      static int kontaktMaxVoices();
      // score: whose playback settings set the patch's script values on top of the setup (SoundLib::playbackScriptValues:
      // [slurs] speedFromWritten); nullptr: playback.ini's
      // part: whose state settings (SoundLib::Controller::state, a non-Kontakt plug-in's: Ample's sample set) go into the
      // state (stateValuesFor); nullptr: none
      static QByteArray setupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                   QString* error = nullptr, const Score* score = nullptr,
                                   const Part* part = nullptr);   // made first when needed
      // (the same with those script values given: scriptValuesFor)
      static QByteArray setupStateWith(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                       const std::map<QString, QByteArray>& values, QString* error);
      static std::map<QString, QByteArray> scriptValuesFor(const SoundLib::Library& library, const QString& patch, const Score* score);
      // a non-Kontakt library's state settings the part sets for the patch (PartControllers::stateValues, by its lanes);
      // empty for Kontakt (makesSetups) or without a part. setupStateWith writes them into the state (pluginstate.h)
      static std::map<QString, QByteArray> stateValuesFor(const SoundLib::Library& library, const QString& patch, const Part* part);
      // the library's <PluginSettings> found bad (Ample's global midiGuitarMode on, SSO's shared settings overriding the
      // articulation mode …): the warnings, each logged and shown once (checkPluginSettings in soundlibraryhost.cpp);
      // checked: a line per file read ("<library>: <path> read" / "not there"); the file under the user's Documents
      // folder, folder="local": the local application data folder (MS_PLUGIN_SETTINGS_DIR / MS_PLUGIN_SETTINGS_LOCAL_DIR
      // instead, for tests). Also in every render's preflight (loadCalibration: RenderPreflight::setPluginSettings)
      static QStringList checkPluginSettings(const SoundLib::Library& library, QStringList* checked = nullptr);
      static QString pluginSettingsFile(const QString& file, const QString& folder = QString());
      // a non-Kontakt plug-in's own state as a new instance has it (a patch with state settings and no setup file starts
      // from it); kept for the process, by plug-in path. Empty with *error when the plug-in doesn't load
      static QByteArray defaultState(const QString& pluginPath, QString* error = nullptr);
      // (the setup with the voice limit: setupState before the script values)
      static QByteArray maxVoicesState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                       QString* error);
      // (the setup as made or resaved, before setupState sets the voice limit)
      static QByteArray savedSetupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                        QString* error);
      static bool loadSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch,
                            const QString& pluginPath, QString* error = nullptr, const Score* score = nullptr,
                            const Part* part = nullptr);
      // hosted tuning by slots (SoundLib::Instance, [tuning] hostedTuning): a setup state (setupState's form) tuned by
      // Kontakt's Tune, one slot (cents of one: KontaktSetup::withTune) or a multi of the patch, a slot per copy on its
      // own MIDI channel (KontaktSetup::withSlots, as the plain Live set's); empty with *error when the state isn't
      // Kontakt's (no program in its first slot): that patch then plays plain, a copy per instance, by varispeed
      // library: made once and kept (the very bytes: soundlibraryhost.cpp › prepared), else made now
      static QByteArray tunedState(const QByteArray& state, const std::vector<double>& cents, QString* error = nullptr,
                                   const SoundLib::Library* library = nullptr);
      static QString tuningKey(const std::vector<double>& cents);     // what Slot::tuning compares ("" plain)
      // a made setup is Kontakt's own state already (resaved; true for a library whose setups aren't made)
      static bool setupResaved(const SoundLib::Library& library, const QString& patch);
      // the prepared states (setupState's voice limit, tunedState's tunings: made once, the very bytes, kept in memory
      // and in "<setups folder>/prepared") forgotten in memory (the files stay); made: how many were made so far
      static void forgetPrepared(int* made = nullptr);
      // an instance's setup into p as MuseScore plays it (part: whose state settings, as setupState): plain (cents empty)
      // as loadSetup, else tuned (tunedState); a
      // made setup not yet resaved goes in plain first (resaved: Kontakt's own state; a multi of a made one would
      // load each slot as slowly), then tuned. *tuned: its tuning is in its state (false: plain, varispeed tunes it)
      static bool loadInstanceSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch,
                                    const std::vector<double>& cents, const QString& pluginPath, QString* error, bool* tuned,
                                    const Score* score = nullptr, const Part* part = nullptr);
      // tests: called with each state loaded into an instance (loadSetup, loadInstanceSetup: the patch and the very
      // bytes), on the loading thread; empty: none
      static std::function<void(const QString& patch, const QByteArray& state)>& loadedStateObserver();
      // patches whose setup can't be tuned (not Kontakt's): their copies play plain, an instance each
      static std::set<QString>& untunable();
      // a part's plug-in parameter settings (its flat automation lanes: partcontrollers.h) as MuseScore's playback sets
      // them on a route's instance
      struct AppliedParameter {
            QString controller;           // the map's controller id ("mic1")
            QString title;                // the plug-in's parameter ("Mic 1 level", as the plug-in names it)
            long id { -1 };
            double value { 0 };           // 0-127, the part's setting (or the map's default)
            double readBack { -1 };       // normalized, what the plug-in holds after it took it
            };
      // the parameters the part sets on this route's patch (none: the setup plays as it is)
      static std::vector<QString> routeParameterControllers(const SoundLib::Route& r,
                                                            const std::map<const Part*, Automation::PartLanes>& lanes);
      // the route's setup (setupState) as MuseScore plays it: loaded into a new instance of the plug-in, its script
      // settled (loadSetup), the part's parameters set exactly as playback sets them
      // (LibraryControllers::applyParameters), a moment of audio for the plug-in to take them, then its state (the
      // form of a setup: Vst3Plugin::state). Create Live Set embeds it (LIVE.md › Create Live Set). Empty and *error
      // when it failed
      // the plug-in's parameter ids by title per patch, as last found on a loaded instance (hosted playback, Create
      // Live Set): kept in the setups folder ("parameter ids.json") for when no instance is loaded ("Live plays the
      // score": Live names Kontakt's automation slots "#001" … and the MuseScore Link device finds them by id)
      static void rememberParameterIds(const SoundLib::Library& library, const QString& patch, const std::map<QString, long>& ids);
      static long knownParameterId(const SoundLib::Library& library, const QString& patch, const QString& title);
      // (after a setup loaded and settled: its parameters' values for setupParameterValue; one that differs from the
      // map's patchDefault by more than PATCH_DEFAULT_TOLERANCE, the map's rounding, is logged in "load times.log")
      static void recordSetupValues(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch);
      static constexpr double PATCH_DEFAULT_TOLERANCE = 1e-4;
      // a Kontakt's state from a Live set (its component: ProcessorState) with plug-in parameters set (by title, 0-1):
      // loaded into a new instance, its script settled, the values set, a moment for the plug-in to take them, then its
      // component state (Update Live Set: a lane that never changes is the parameter's plain value, in Live's panel and
      // in the state, livesetupdate.h). *ids: each title's parameter id on it (-1: not in it). Empty and *error: failed
      static QByteArray stateWithParameters(const QByteArray& component, const QString& pluginPath,
                                            const std::vector<std::pair<QString, double>>& values,
                                            std::map<QString, long>* ids, QString* error = nullptr);
      // the parameters with these titles on the patch as MuseScore loads it (setup, script settled): id (-1: not in it)
      // and the patch's own value (readBack); controller empty
      static bool parametersOf(const SoundLib::Library& library, const SoundLib::Route& r, const QStringList& titles,
                               const QString& pluginPath, std::vector<AppliedParameter>* found, QString* error = nullptr);
      static QByteArray stateWithControllers(const SoundLib::Library& library, const SoundLib::Route& r,
                                             const std::map<const Part*, Automation::PartLanes>& lanes,
                                             const QString& pluginPath, std::vector<AppliedParameter>* applied,
                                             QString* error = nullptr);
      // after a setup was set on p (loadSetup does both): load times.log, and a setup made from the
      // .nki replaced by Kontakt's own state (resave). setupMs: reading or making the setup
      static void setupLoaded(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch, const QByteArray& state,
                              double setupMs);
      // how many setups are set (Vst3Plugin::setState) on worker threads at once while MuseScore goes on: 0 (default)
      // and 1 one at a time (2026-10-24: always on a worker thread, the window free; before, 0 set it on the GUI thread,
      // holding the window for each instance's seconds); n: up to n at once (measured 2026-10-15 with Kontakt: no
      // faster, HANDOFF.md › Kontakt slots). Advanced preference io/soundLibraryLoadThreads, or
      // MS_SOUNDLIBRARY_LOAD_THREADS in the environment
      static int loadThreads();
      static int parallelLoads()          { return std::max(1, loadThreads()); }
      static void workerThread(bool start);       // on a worker thread before and after (COM on Windows)
      static void logLoadTime(const SoundLib::Library& library, const QString& line);   // load times.log

      Vst3Synth* synth() const;
      // the score's routes' instances, loaded before it returns (user input held back while it waits): an audio export,
      // the tests. Playback doesn't wait: readyToPlay
      bool sync(Score* score, QString* error = nullptr);
      // Play: true when every instance the score needs is loaded (then set up for play as sync does it: the parts'
      // parameters, the instances it no longer needs released); else false at once, its loading started in the
      // background (preloadSoon), Play grayed until it is done. True also when the library doesn't play through its
      // plug-in, or on an error (*error: shown; the library's parts play nothing, as before)
      bool readyToPlay(Score* score, QString* error = nullptr);
      // load them all when the score is opened or shown, in the background (also: Play, a clicked note, an edit);
      // keep: an instance the score no longer needs stays (scoreEdited)
      void preloadSoon(Score* score, bool keep = false);
      // an edit (MuseScore::endCmd): a second after the last one, while stopped, the score's needs worked out (the
      // routes: what Play works out anyway) and new patches loaded in the background, so that Play is grayed only
      // while they load
      void scoreEdited(Score* score);
      // instances still loading (or to load) for the score shown (null: any): Play is grayed meanwhile
      bool loading(const Score* score = nullptr) const;
      int loadingTotal() const            { return _progressTotal; }
      int loadingLoaded() const           { return _progressLoaded; }
      // fn once the score's loading is done (now, from the event loop, when nothing loads); one at a time: another
      // replaces it; dropped when another score's patches load or the instances are released
      void whenLoaded(Score* score, std::function<void()> fn);
      // the Play action as MuseScore::changeState enables it (its shortcut's states), grayed while the shown score's
      // patches load and playback is stopped (a playing Play stops). The toolbar, the Play Panel, the shortcut and a MIDI
      // remote all trigger that action
      static void updatePlayAction();
      // that loading still going on (more instances to load, or setups being set on worker threads): the even levels'
      // own instances wait for it (mscore/evenautomeasurer.h), so the playback's load first
      bool preloading() const;
      // no instances (a load on a worker thread is waited for: it can't be stopped; the event loop runs meanwhile, user
      // input held back, for a plug-in that needs the GUI thread while it loads); nothing loads after it
      void release();
      bool loaded(int slot) const;
      qint64 memory(int slot) const       { return slot >= 0 && slot < int(_slots.size()) ? _slots[size_t(slot)].memory : -1; }
      static qint64 processMemory();      // the process's own memory (Task Manager's "Memory"), bytes; -1: unknown
      static void systemMemory(qint64* total, qint64* available);   // the computer's, bytes; -1: unknown
      bool showEditor(int slot, QString* error = nullptr);
      static void routesMayChange();
      // the Mixer (volume, pan, mute, solo) of the score's library parts on their slots
      // (Vst3Synth::setMix): after each sync, at once from the Mixer and Seq::setController, and every
      // 50 ms (so mute and solo from anywhere reach them: the Mixer, the old part editor, OSC)
      void applyMixer(const Score* score = nullptr);
      std::vector<int> slotsOf(const Part* part) const;   // the part's slots (last sync)
      // live (an automation lane edited, also during playback: mscore/automationlanes.h): the part's plug-in parameter
      // settings (its flat lanes; without one the patch's own value again) on all its loaded slots at once, leaving the
      // parameters its other lanes play to them; the slots set
      std::vector<int> applyPartSettings(MasterScore* ms, const Part* part);
      // the setup's own value (0-1) of a plug-in parameter of a patch, as recorded when the setup last loaded on this
      // computer ("parameter values.json" in the setups folder; -1: not known): the lanes' dashed line
      static double setupParameterValue(const SoundLib::Library& library, const QString& patch, const QString& title);

   protected:
      bool eventFilter(QObject* o, QEvent* e) override;

   signals:
      void changed();
      void loadingChanged();              // loading() turned, or its progress moved
      };

//---------------------------------------------------------
//   SoundLibraryExport
//    the library parts of an audio export on the hosted plug-ins (a no-op when the library
//    doesn't play through its plug-in)
//---------------------------------------------------------

class SoundLibraryExport {
      MasterSynthesizer* _synth { nullptr };
      Vst3Synth* _vst { nullptr };
      std::shared_ptr<Vst3Synth> _own;          // without a sequencer

   public:
      SoundLibraryExport(Score* score, MasterSynthesizer* synth, float sampleRate);
      // with instances loaded before (loadInstances): the playback verification renders a score
      // several times (mixed, each part alone) and loads the library's patches once. The score's mix
      // as the other form's (setExportMix; mute false: a muted part still plays)
      SoundLibraryExport(MasterSynthesizer* synth, float sampleRate, std::shared_ptr<Vst3Synth> own, Score* score, bool mute = true);
      // each slot's export mix: every slot at 100 / 64, each route of the score at its part's Mixer
      // values (volume, pan, mute if mute) times its patch's volume (TrackDelays::patchGain: a track
      // level's headroom, [levels] context's G - S). Without it the slots play at 0 dB: the verification's
      // renders had SSO's Performance patches 9-15 dB under the others (2026-10-11)
      static void setExportMix(Vst3Synth* vst, Score* score, bool mute);
      ~SoundLibraryExport() { finish(); }
      // instances of its own for the score's library parts (no sequencer: a conversion from the
      // command line); patches: slot -> the patch loaded there; parts: only these parts' routes (null: every part's; the
      // even-levels job loads one part's at a time: every part's SSO strings at once needed more memory than the
      // Windows VM's 16 GB, 2026-10-16)
      static std::shared_ptr<Vst3Synth> loadInstances(Score* score, float sampleRate, QString* error,
                                                      std::map<int, QString>* patches = nullptr,
                                                      const std::set<const Part*>* parts = nullptr);
      // one route's patch, as loadInstances loads it, on slot 0 of an instance of its own (the even levels measured in
      // the background: mscore/evenautomeasurer.h)
      static std::shared_ptr<Vst3Synth> loadInstance(Score* score, const SoundLib::Route& route, float sampleRate, QString* error);
      // loadInstance in steps, its plug-in's setState (Kontakt's instrument load: seconds) on a worker thread meanwhile
      // (the edit lag, 2026-10-17: the measurer's loads were 1.8-3.2 s stalls of the window). The same calls in the same
      // order on the same bytes as loadInstance; only setState runs on another thread, as the host's own
      // io/soundLibraryLoadThreads does (Kontakt takes it: HISTORY › load times). begin on the main thread; ready
      // polls; finish (main thread) waits if needed and completes it: the instance, or nullptr and error
      struct InstanceLoad;
      static std::unique_ptr<InstanceLoad, void(*)(InstanceLoad*)> beginInstance(Score* score, const SoundLib::Route& route,
                                                                                 float sampleRate, QString* error);
      static bool instanceReady(const InstanceLoad* load);
      static std::shared_ptr<Vst3Synth> finishInstance(InstanceLoad* load, Score* score, QString* error);
      void finish();                            // before the MasterSynthesizer goes
      bool play(const NPlayEvent& event);       // true: a library part's event, played here
      };

//---------------------------------------------------------
//   SoundLibraryDialog
//    the parts of the current score and where they play: the patch, and the MIDI output and
//    channel, or the hosted plug-in (its setup, its window)
//---------------------------------------------------------

class SoundLibraryDialog : public QDialog {
      Q_OBJECT

      std::shared_ptr<const SoundLib::Library> _library;
      SoundLib::Output _output;
      QPointer<Score> _score;             // (null: the current score)
      QLabel* _info;
      QLabel* _folder { nullptr };        // the library's folder (the box above the parts: every score)
      QTreeWidget* _tree;                 // a part, its patches (extras, copies for other tunings) under it

      void rebuild();

   public:
      // score: the one shown (null: the current one, followed)
      SoundLibraryDialog(std::shared_ptr<const SoundLib::Library> library, SoundLib::Output output, QWidget* parent = nullptr,
                         Score* score = nullptr);
      };

//---------------------------------------------------------
//   SoundLibraryOptions
//    Mixer › Advanced Options…: the score's sound library settings (the copies for other tunings,
//    held notes early by their onset); the owner, 2026-09-28: "put the useful configs in there … make
//    all these configs per score". (The library's folder: View › Sound Library…, since 2026-10-15)
//---------------------------------------------------------

class SoundLibraryOptions : public QDialog {
      Q_OBJECT

      std::shared_ptr<const SoundLib::Library> _library;
      QPointer<MasterScore> _score;
      QLabel* _liveSet { nullptr };
      QCheckBox* _liveAuto { nullptr };
      QPushButton* _liveUnlink { nullptr };
      QCheckBox* _liveClips { nullptr };
      QLabel* _liveClipsStatus { nullptr };

      void load();
      void setMetaTag(const char* tag, const QString& value);

   public:
      SoundLibraryOptions(MasterScore* score, QWidget* parent = nullptr);
      static void showFor(QWidget* parent);     // for the current score
      };

} // namespace Ms
#endif
