//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  SoundLibraryHost: the sound library's plug-in (Kontakt …) hosted in MuseScore, when the
//  library plays through its plug-in (SoundLib::Output::PLUGIN). For the current score's
//  routes (SoundLib::routes: one per library part) it keeps an instance in Vst3Synth's slot
//  of the route, with the instrument's setup loaded:
//
//    setup     what the plug-in's editor was left with for a library instrument (Kontakt with
//              the "Violins 1" patch, UACC switching …): its state, saved once per instrument
//              in <data>/soundlibraries/<library>/<instrument>.vst3state and loaded into every
//              instance that plays that instrument, in any score
//    editor    the plug-in's window for a part (Vst3EditorWindow); closing it saves the setup
//              of an instrument that has none yet
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
#include <memory>

#include <QDialog>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "libmscore/soundlibrary.h"

class QTableWidget;
class QLabel;

namespace Ms {

class MasterSynthesizer;
class NPlayEvent;
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
            };
      std::array<Slot, 64> _slots;
      QTimer _idle;

      SoundLibraryHost();

   public:
      static SoundLibraryHost* instance();

      static bool available();            // built with plug-in hosting
      static QStringList pluginFolders(); // the system's VST 3 folders
      static QString pluginPath(const SoundLib::Library& library, QString* error = nullptr);
      static QString setupFile(const SoundLib::Library& library, const QString& instrument);
      static bool hasSetup(const SoundLib::Library& library, const QString& instrument);

      Vst3Synth* synth() const;
      bool sync(Score* score, QString* error = nullptr);    // the score's routes' instances
      void release();                     // no instances
      bool loaded(int slot) const;
      bool saveSetup(int slot, QString* error = nullptr);
      bool showEditor(int slot, QString* error = nullptr);
      static void routesMayChange();
      void setupChanged(const QString& instrument);   // saved elsewhere: reload it

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
//    channel, or the hosted plug-in with its setup (show / save)
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
