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

#include "libmscore/soundlibrary.h"

class QTableWidget;
class QLabel;
class QDoubleSpinBox;
class QSpinBox;

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
            };
      std::array<Slot, 64> _slots;
      // instances set aside by syncSome: a patch the score being loaded doesn't play in their slot
#ifdef USE_VST3
      struct Spare {
            std::unique_ptr<Vst3Plugin> plugin;
            Slot slot;
            };
      std::vector<Spare> _spares;
#endif
      QTimer _idle;
      QTimer _preloadTimer;
      QPointer<MasterScore> _preloadScore;
      int _loads { 0 };                   // instances loaded so far
      int _preloadFrom { 0 };             // _loads when the preload started
      bool _preloadLogged { false };      // its list of what to load is in load times.log
      QElapsedTimer _lastInput;           // since the user's last key, click or wheel
      static constexpr int INPUT_PAUSE_MS = 0;     // a background load waits for this long a pause (the owner, 2026-09-27: 0, no wait)
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

      // setups made by MuseScore
      static bool makesSetups(const SoundLib::Library& library);        // the map has <Files>
      static QString libraryFolder(const SoundLib::Library& library);   // where its .nki are; empty: not found
      static void setLibraryFolder(const SoundLib::Library& library, const QString& folder);
      static QString nkiPath(const SoundLib::Library& library, const QString& patch);
      static const SoundLib::LibInstrument* findPatch(const SoundLib::Library& library, const QString& name);
      static QByteArray setupId(const SoundLib::Library& library, const QString& patch);  // changes with its setup
      static QByteArray setupState(const SoundLib::Library& library, const QString& patch, const QString& pluginPath,
                                   QString* error = nullptr);            // made first when needed
      static bool loadSetup(Vst3Plugin* p, const SoundLib::Library& library, const QString& patch,
                            const QString& pluginPath, QString* error = nullptr);

      Vst3Synth* synth() const;
      bool sync(Score* score, QString* error = nullptr);    // the score's routes' instances
      void preloadSoon(Score* score);     // load them all when the score is opened or shown
      void release();                     // no instances
      bool loaded(int slot) const;
      qint64 memory(int slot) const       { return slot >= 0 && slot < int(_slots.size()) ? _slots[size_t(slot)].memory : -1; }
      static qint64 processMemory();      // the process's own memory (Task Manager's "Memory"), bytes; -1: unknown
      bool showEditor(int slot, QString* error = nullptr);
      static void routesMayChange();

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
      ~SoundLibraryExport() { finish(); }
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
      QLabel* _info;
      QWidget* _lanesRow { nullptr };     // the copies for other tunings: tolerance, ring, at most
      QDoubleSpinBox* _tolerance { nullptr };
      QDoubleSpinBox* _tail { nullptr };
      QSpinBox* _maxLanes { nullptr };
      QDoubleSpinBox* _balance { nullptr };   // the calibration's short notes against held ones (dB)
      QTableWidget* _table;
      void setLaneSettings(bool libraryDefaults);

      void rebuild();

   public:
      SoundLibraryDialog(std::shared_ptr<const SoundLib::Library> library, SoundLib::Output output, QWidget* parent = nullptr);
      };

} // namespace Ms
#endif
