//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYBACKMODE_H__
#define __PLAYBACKMODE_H__

//---------------------------------------------------------
//   Playback mode: one switch (the Mixer's and the Play Panel's "Playback" box; actions
//   playback-ms3 / -ms4 / -library for shortcuts) between
//
//   MS3      MuseScore 3.6's playback: its default dynamics method (SND and changes at the start
//            of a segment, CC2), the Zita reverb and MuseScore_General (when MuseScore 3's
//            SoundFont can be found: the SoundFonts folders or a MuseScore 3 install)
//   MS4      this fork's MuseScore 4 playback (dynamics method 3, MuseReverb, MS Basic.sf3)
//   LIBRARY  MS4 plus the sound library (the last one used, else the first in share/soundlibraries)
//
//   The mode is not stored on its own: it is read back from the synthesizer's dynamics method
//   and the sound library preference, and a switch saves the synthesizer settings as the
//   default (synthesizer.xml), so it survives a restart. A SoundFont the user chose (neither
//   MS Basic nor MuseScore_General) is left alone.
//---------------------------------------------------------

#include <QComboBox>
#include <QPointer>

namespace Ms {

enum class PlaybackMode : char { MS3, MS4, LIBRARY };

PlaybackMode playbackMode();
void setPlaybackMode(PlaybackMode mode);
QString playbackModeName(PlaybackMode mode);
QString soundLibraryPath();         // the library the LIBRARY mode plays (the last one used …)

//---------------------------------------------------------
//   PlaybackModeBox
//    the mode as a drop-down (the Mixer, the Play Panel); every box follows every switch
//---------------------------------------------------------

class PlaybackModeBox : public QComboBox {
      Q_OBJECT
      static QList<QPointer<PlaybackModeBox>> _boxes;
      void refresh();

   public:
      explicit PlaybackModeBox(QWidget* parent = nullptr);
      ~PlaybackModeBox();
      static QWidget* row(QWidget* parent);     // "Playback:" and a box
      static void updateAll();
      };

}     // namespace Ms
#endif
