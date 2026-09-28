//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveintegration.h"

#include <QCheckBox>
#include <QMessageBox>
#include <QSettings>

#include "libmscore/soundlibrary.h"
#include "musescore.h"
#include "preferences.h"
#include "seq.h"
#include "soundlibraryhost.h"

namespace Ms {

extern void updateExternalValuesFromPreferences();

namespace LiveIntegration {

//---------------------------------------------------------
//   playingThroughMidi / setPlayThroughMidi
//---------------------------------------------------------

bool playingThroughMidi()
      {
      return SoundLib::output() == SoundLib::Output::MIDI;
      }

bool setPlayThroughMidi(bool midi, QWidget* parent)
      {
      if (midi == playingThroughMidi())
            return true;
      if (!midi && !SoundLibraryHost::available())
            return false;
      static const char* const DONT_ASK = "liveIntegration/dontAskSwitch";
      QSettings settings;
      if (!settings.value(DONT_ASK, false).toBool()) {
            QMessageBox box(QMessageBox::Question, QObject::tr("Play through Live"),
                            midi ? QObject::tr("The sound library's parts will play through MIDI output (A-D) to Ableton Live. "
                                               "The patches loaded in MuseScore are released (their memory freed); switching "
                                               "back loads them again.")
                                 : QObject::tr("The sound library's parts will play through the library's plug-in in MuseScore "
                                               "again. Its patches are loaded again, which can take a while."),
                            QMessageBox::Ok | QMessageBox::Cancel, parent);
            QCheckBox* dontAsk = new QCheckBox(QObject::tr("Don't ask again"), &box);
            box.setCheckBox(dontAsk);
            if (box.exec() != QMessageBox::Ok)
                  return false;
            if (dontAsk->isChecked())
                  settings.setValue(DONT_ASK, true);
            }
      if (seq && seq->isPlaying())
            seq->stopWait();
      preferences.setPreference(PREF_IO_SOUNDLIBRARY_OUTPUT, midi ? "midi" : "plugin");
      updateExternalValuesFromPreferences();          // (releases the instances for MIDI, marks playlists dirty)
      if (mscore) {
            mscore->updatePlaybackMode();
            if (!midi && mscore->currentScore())
                  SoundLibraryHost::instance()->preloadSoon(mscore->currentScore());
            QString message = midi ? QObject::tr("Sound library: through MIDI output (Ableton Live)")
                                   : QObject::tr("Sound library: through the library's plug-in");
            if (midi && seq && seq->driver() && !seq->driver()->canOutputMidi())
                  message += QObject::tr(" — no MIDI output is open: set MIDI output A in Preferences › I/O");
            mscore->showMessage(message, 6000);
            }
      return true;
      }

}     // namespace LiveIntegration
}     // namespace Ms
