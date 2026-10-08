//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYBACKSETTINGS_H__
#define __PLAYBACKSETTINGS_H__

//---------------------------------------------------------
//   Playback adjustments, configurable (the owner, 2026-10-01: "make all the playback adjustments we made
//   fully configurable … and editable in an ini file"). docs/PLAYBACK_SETTINGS.md lists every adjustment.
//
//   Three layers, each over the one before:
//     1. built-in defaults (definitions(); for a few, the sound library map's value:
//        <Onset early>, <Tuning tolerance tail maxLanes>)
//     2. the global ini file, playback.ini in MuseScore's data folder (Windows:
//        %LOCALAPPDATA%\MuseScore\MuseScore3Evo\playback.ini), written with every key, its default and a
//        comment when missing; never overwritten. Read at start and by reload() (Edit › Reload Playback
//        Settings, Mixer › Advanced Options…): a reload renders again
//     3. the score: metaTag "playbackSettings" ("legato/keepMs=60;shorts/staccato=40"), and the two
//        older per-score metaTags (soundLibraryOnsetEarly, soundLibraryLanes; soundLibraryLegatoEarly ignored since 2026-10-07),
//        which stay where they were. A score without overrides has no such metaTag (unchanged files)
//   Per-patch tables (map data: legato delays, onsets, shorts' from=) can be overridden in the ini only,
//   by patch name: [legato.delay], [heldNotes.onset], [shorts.from] (see the generated file).
//---------------------------------------------------------

#include <QString>
#include <QStringList>
#include <map>
#include <utility>
#include <vector>

namespace Ms {

class Score;

namespace Playback {

struct Definition {
      const char* id;               // "section/key"
      double value;                 // built-in default (NaN: the sound library map's)
      double min;
      double max;
      const char* unit;             // "ms", "%", "ticks", "on/off" …
      const char* comment;          // for the ini and the UI
      bool perScore;                // false: global only (hosting: one plug-in instance for every score)
      bool mapDefault() const;
      };

const std::vector<Definition>& definitions();
const Definition* definition(const QString& id);

enum class Source : signed char { DEFAULT, MAP, INI, SCORE };
QString sourceName(Source s);

// the effective value: the score's, else the ini's, else mapValue when the default is the map's (and the map
// gives one: >= 0), else the built-in default
double value(const char* id, const Score* score = nullptr, double mapValue = -1);
bool on(const char* id, const Score* score = nullptr);
Source source(const char* id, const Score* score = nullptr, double mapValue = -1);

// the ini
void setIniPath(const QString& path);           // reads it (writes it with the defaults if missing)
QString iniPath();
void reload();                                  // reads it again
int generation();                               // changes at each read (the renderer renders again)
QStringList warnings();                         // unknown keys, bad values (also logged)
QString iniTemplate();                          // the file as written when missing
bool iniHas(const char* id);
double iniValue(const char* id);

// presets: a few settings set together in playback.ini (the owner, 2026-10-07: "library default, and recommended")
struct Preset {
      const char* id;                                   // "recommended", "library"
      const char* name;                                 // for menus
      std::vector<std::pair<const char*, double>> values;   // setting id -> value; the others are left as they are
      };
const std::vector<Preset>& presets();
// the preset's keys written into the ini file's text (comments and other lines kept; a missing key or section is added)
QString applyPresetToText(const QString& text, const Preset& preset);
// the file edited (written from iniTemplate() first when missing); false when it can't be written. Does not reload.
bool applyPreset(const QString& id, const QString& path = QString());
// the preset the ini's values match ("" : custom); a key not in the ini counts as the map's (Recommended: SSO's 100)
QString detectPreset(const std::map<QString, double>& iniValues);
QString currentPreset();                                // for the ini now read
// the preset's keys the score overrides (own value or the older metaTag)
QStringList presetKeysOverriddenBy(const Score* score);

// per-patch tables (ini only): text by patch ("Violins 2 - Performance") or "patch|articulation"; an
// offset ("+25", "-30": ms added) or a whole table ("-12:240 -7:280 …" / one number)
QString patchEntry(const char* table, const QString& patch, const QString& articulation = QString());
double adjust(const char* table, const QString& patch, const QString& articulation, int key, double mapValue);

// the score layer
extern const char* metaTag;                     // "playbackSettings"
std::map<QString, double> scoreValues(const Score* score);
QString writeScoreValues(const std::map<QString, double>& values);   // "" when none
// the older per-score metaTags that are this layer for their settings (legato/early, heldNotes/early,
// tuning/tolerance, tuning/tail, tuning/maxLanes)
bool hasOwnMetaTag(const char* id);

// test hook: the ini's values set directly (no file)
void setIniValuesForTest(const std::map<QString, QString>& values);

} // namespace Playback
} // namespace Ms
#endif
