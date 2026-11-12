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
//
//   A score's mode (the owner, 2026-10-31: "it should be loaded with ms3 or ms4 depending on which ver it's from"; before,
//   the global mode stayed whatever was chosen last): opening a score file sets the global mode by where it comes from,
//   and each score keeps its mode while it is open, so switching tabs plays each score in its own:
//   - the score's metaTag "playbackMode" ("ms3", "library:<engine>"; MS4, the default, left out), written by this fork:
//     the global mode the score last had in it (recordScorePlaybackMode at every switch, so a save writes it; 3.6 keeps
//     metaTags through a round trip). An engine not found here leaves the global mode as it is;
//   - else a file of MuseScore 4 (version >= 400, opened as too new): MS4;
//   - else a file this fork saved before the metaTag (programVersion 3.6.3 or later: 3.6.2 was MuseScore 3's last; or a
//     metaTag only the fork writes, partPlayback / playbackSettings / automation / trackDelays …): MS4, the fork's own;
//   - else (MuseScore 3.6 or older, 1.x / 2.x too): MS3.
//   Noted at open (notePlaybackModeAtOpen, MuseScore::openScore: .mscz / .mscx only), put in the metaTag when the score
//   is first shown; a new score or an import takes the global mode it is first shown in. Showing a score
//   (MuseScore::setCurrentScoreView) switches the global mode only when it differs from the score's
//   (applyScorePlaybackMode): the same mode loads nothing again. A part's own mode (partplayback.h) and its Engine lane
//   (enginelane.h) play over it as before. Command-line conversions show no score: their files keep the metaTag as read
//   (the round trip through 3.6). An MS4 score of the fork without any fork metaTag, saved by 3.6.2 (programVersion
//   3.6.2), opens as MuseScore 3's
//---------------------------------------------------------

#include <functional>
#include <map>
#include <vector>

#include <QComboBox>
#include <QPointer>
#include <QStringList>

namespace Ms {

class MasterScore;

enum class PlaybackMode : char { MS3, MS4, LIBRARY };

PlaybackMode playbackMode();
void setPlaybackMode(PlaybackMode mode);
// LIBRARY with the engine (empty: the library of the preference, else the last one used)
void setPlaybackMode(PlaybackMode mode, const QString& engine);
QString playbackModeName(PlaybackMode mode);
QString soundLibraryPath();         // the library the LIBRARY mode plays (the last one used …)

// a score's mode (above): the metaTag, its text for the global mode ("ms3", "ms4", "library:<engine>"), the mode a
// score file opens with, as text (empty: a new score or an import, none)
extern const char* const SCORE_PLAYBACK_MODE_TAG;
QString playbackModeText();
QString openingPlaybackMode(const MasterScore* score);
// at open (score files): the mode the score opens with, noted
void notePlaybackModeAtOpen(MasterScore* score);
// the score shown: the global mode switched to the score's where it differs (true: switched); set: the switch (tests)
bool applyScorePlaybackMode(MasterScore* score,
                            const std::function<void(PlaybackMode, const QString&)>& set = std::function<void(PlaybackMode, const QString&)>());
// the global mode into the score's metaTag (once its mode is known: shown or opened)
void recordScorePlaybackMode(MasterScore* score);

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
