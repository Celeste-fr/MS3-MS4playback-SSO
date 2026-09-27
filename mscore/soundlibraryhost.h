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

#include <QDialog>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "libmscore/soundlibrary.h"

class QTableWidget;
class QLabel;

namespace Ms {

class MasterScore;
class MasterSynthesizer;
class NPlayEvent;
class Vst3Plugin;
class Score;
class Vst3EditorWindow;
class Vst3Synth;

class SoundLibraryHost : public QObject {
      Q_OBJECT

      struct Slot {
            QString instrument;           // the library instrument it plays
            QString part;
            bool hasSetup { false };      // its setup was loaded
            QPointer<Vst3EditorWindow> editor;
            // the patch's own value (its setup's) of each plug-in parameter a score has set: put
            // back for a score that doesn't set it, and in the setup saved (a score's values
            // stay in the score; the setup is the library's default). Cleared when a setup loads
            std::map<unsigned, double> patchValues;
            };
      std::array<Slot, 64> _slots;
      QTimer _idle;
      QTimer _preloadTimer;
      QPointer<MasterScore> _preloadScore;
      void preloadStep();
      bool syncSome(Score* score, QString* error, int maxLoads, int* remaining);

      SoundLibraryHost();

   public:
      static SoundLibraryHost* instance();

      static bool available();            // built with plug-in hosting
      static QStringList pluginFolders(); // the system's VST 3 folders
      static QString pluginPath(const SoundLib::Library& library, QString* error = nullptr);
      static QString setupFile(const SoundLib::Library& library, const QString& instrument);
      static bool hasSetup(const SoundLib::Library& library, const QString& instrument);  // it has one or can make it

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
      void preloadSoon(Score* score);     // load them ahead of the first play, in the background
      void release();                     // no instances
      bool loaded(int slot) const;
      bool showEditor(int slot, QString* error = nullptr);
      static void routesMayChange();

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
      QTableWidget* _table;

      void rebuild();

   public:
      SoundLibraryDialog(std::shared_ptr<const SoundLib::Library> library, SoundLib::Output output, QWidget* parent = nullptr);
      };

} // namespace Ms
#endif
