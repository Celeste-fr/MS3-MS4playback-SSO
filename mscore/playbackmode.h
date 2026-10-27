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
//
//   Engines (the owner, 2026-10-26: "natively integrate Vial Instrumental as another playback engine, like we have now
//   for ms3, ms4 and sso"): the boxes list MS3, MS4 and each sound library by its name ("Spitfire Symphony Orchestra",
//   "Vial Instrumental", "Ample Guitar M"), as a part's own box does (the Mixer: metaTag partPlayback "library:<name>",
//   libmscore/partplayback.h); SoundLib's engines (soundlibrary.h) play several in one score. An engine is a map of
//   share/soundlibraries (or the one chosen elsewhere); a map named "<engine> (<variant>)" is a variant of it, which
//   plays in its place where Preferences › I/O › Sound library picks it (preference io/soundLibraryVariants: Vial
//   Instrumental's "Fuse", Vial Fuse.vst3's mic mixers). An engine whose plug-in isn't found (hosted), or any but the
//   global library over MIDI out, is grayed, its tooltip saying why
//---------------------------------------------------------

#include <map>
#include <vector>

#include <QComboBox>
#include <QPointer>
#include <QStringList>

namespace Ms {

enum class PlaybackMode : char { MS3, MS4, LIBRARY };

PlaybackMode playbackMode();
void setPlaybackMode(PlaybackMode mode);
// LIBRARY with the engine (empty: the library of the preference, else the last one used)
void setPlaybackMode(PlaybackMode mode, const QString& engine);
QString playbackModeName(PlaybackMode mode);
QString soundLibraryPath();         // the library the LIBRARY mode plays (the last one used …)

// the sound library engines: the maps that come with MuseScore and the preference's elsewhere, by engine
struct SoundEngine {
      QString name;                 // "Vial Instrumental"
      QStringList maps;             // its maps, its own first, then its variants' ("Vial Instrumental (Fuse)")
      QString path;                 // the map it plays: its variant chosen (soundEngineVariant)
      };
std::vector<SoundEngine> soundEngines();
QString soundEngineOf(const QString& mapPath);          // the engine of a map ("": not one of them)
QString soundEngineVariant(const QString& engine);      // the map the engine plays
QString soundEngineVariantName(const QString& mapPath); // a map's variant as shown ("Fuse"; its own map: the engine's name)
// the variant each engine plays (engine -> map); a change sets SoundLib's engines and the global library if it is that engine
void setSoundEngineVariants(const std::map<QString, QString>& variants);
// the engine can play here: hosted with its plug-in found, or the global library (why: else, as the tooltip says)
bool soundEngineAvailable(const QString& engine, QString* why = nullptr);
QString globalSoundEngine();        // the global mode's engine (LIBRARY), else the last one used
// SoundLib's engines and their availability from the maps and preferences (at start and whenever those change); true:
// the engines or a variant changed (the scores play again)
bool updateSoundEngines();

// box's items for every engine (soundEngines), each with data and its engine's name as Qt::UserRole + 1, grayed with a
// tooltip where it can't play (soundEngineAvailable); *selected: the item of selectedEngine, when there
void addEngineItems(QComboBox* box, int data, const QString& selectedEngine, int* selected);

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
