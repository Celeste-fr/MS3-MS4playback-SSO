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
//  GUI thread only.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#ifndef __SOUNDLIBRARYHOST_H__
#define __SOUNDLIBRARYHOST_H__

#include "config.h"

#include <array>
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
            quint64 used { 0 };           // when it last played or was set aside (_spareClock: least recently used goes first)
            bool pooled { false };        // kept from an earlier sync (the instance pool): loaded again before it plays
            };
      // the instance pool (the owner, 2026-10-18, option 1 of private/sso-load-profile.md): an instance no score plays
      // now stays loaded, so when a score needs its patch again (reopened, reloaded, another score) a new instance loads
      // while the kept one still holds the patch's samples: Kontakt finds them in memory (VM, ktprof: two instances of a
      // patch in one process read its samples once; a Performance patch's background loading is 15-18 s), and the kept
      // one goes once the new one has its setup. The new one is a new instance (plays as one: setState alone left the
      // test synth's round robins as they were). Instances go when the computer's memory runs short (trimPool), at exit
      // and when the library stops playing (release)
      std::vector<Spare> _spares;
      quint64 _spareClock { 0 };
      qint64 _largestInstance { -1 };     // the most memory an instance took so far (Slot::memory), bytes
      Spare* sameSpare(const QString& path, const QString& name);   // a spare of this patch (pooled first), else nullptr
      bool memoryShort() const;           // trimPool's test
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
      QElapsedTimer _lastInput;           // since the user's last key, click or wheel
      static constexpr int INPUT_PAUSE_MS = 0;     // a background load waits for this long a pause (the owner, 2026-09-27: 0, no wait)
      // the pause between two loads at score open, for the window to repaint and take input: 0 (a
      // zero timer runs once the events waiting are done; it was 100 ms, 2.5 s of a full orchestra's 11)
      static constexpr int PRELOAD_GAP_MS = 0;
      void preloadStep();
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
      static QByteArray setupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                   QString* error = nullptr, const Score* score = nullptr);   // made first when needed
      // (the same with those script values given: scriptValuesFor)
      static QByteArray setupStateWith(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                       const std::map<QString, QByteArray>& values, QString* error);
      static std::map<QString, QByteArray> scriptValuesFor(const SoundLib::Library& library, const QString& patch, const Score* score);
      // (the setup with the voice limit: setupState before the script values)
      static QByteArray maxVoicesState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                       QString* error);
      // (the setup as made or resaved, before setupState sets the voice limit)
      static QByteArray savedSetupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                        QString* error);
      static bool loadSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch,
                            const QString& pluginPath, QString* error = nullptr, const Score* score = nullptr);
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
      // an instance's setup into p as MuseScore plays it: plain (cents empty) as loadSetup, else tuned (tunedState); a
      // made setup not yet resaved goes in plain first (resaved: Kontakt's own state; a multi of a made one would
      // load each slot as slowly), then tuned. *tuned: its tuning is in its state (false: plain, varispeed tunes it)
      static bool loadInstanceSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch,
                                    const std::vector<double>& cents, const QString& pluginPath, QString* error, bool* tuned,
                                    const Score* score = nullptr);
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
      // worker threads that set the setups (Vst3Plugin::setState) while MuseScore goes on: 0 (default)
      // on the GUI thread, one at a time, as VST 3 asks; n: up to n at once. Advanced preference
      // io/soundLibraryLoadThreads, or MS_SOUNDLIBRARY_LOAD_THREADS in the environment. Untried with
      // Kontakt: the load times measurement (--measure-load-times) tries it
      static int loadThreads();
      static void workerThread(bool start);       // on a worker thread before and after (COM on Windows)
      static void logLoadTime(const SoundLib::Library& library, const QString& line);   // load times.log

      Vst3Synth* synth() const;
      bool sync(Score* score, QString* error = nullptr);    // the score's routes' instances
      void preloadSoon(Score* score);     // load them all when the score is opened or shown
      // that loading still going on (more instances to load, or setups being set on worker threads): the even levels'
      // own instances wait for it (mscore/evenautomeasurer.h), so the playback's load first
      bool preloading() const;
      void release();                     // no instances
      // the instance pool (Spare): an instance of the patch kept loaded (whoever takes it keeps it until a new instance
      // of the patch has its setup, then lets it go: the samples stay in memory), else nullptr; and an instance handed back to it (SoundLibraryExport's own instances when they go). Off with
      // MS_INSTANCE_POOL=0 in the environment (to compare: tests only, it changes no sound)
      static bool poolOn();
      std::unique_ptr<Vst3Plugin> takePooled(const QString& path, const QString& name, double sampleRate);
      void pool(std::unique_ptr<Vst3Plugin> plugin, const QString& name);
      // pooled instances released, least recently used first, while the computer's available memory is below the most
      // an instance took (measured: Slot::memory; at least POOL_RESERVE_BYTES)
      void trimPool();
      // 1017 MB: the most one SSO patch took alone (Windows VM, Kontakt 8, 2026-10-15, --measure-load-times on Whence,
      // "each patch alone 425-1017 MB", HANDOFF.md › Kontakt slots for tuning copies)
      static constexpr qint64 POOL_RESERVE_BYTES = 1017LL * 1024 * 1024;
      int pooledCount() const;
      void releasePool();                 // the spares released (before the modules unload at exit)
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
