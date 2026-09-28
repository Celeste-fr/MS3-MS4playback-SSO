//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYABILITY_H__
#define __PLAYABILITY_H__

//---------------------------------------------------------
//   The playability checker, built in (PLAYABILITY.md): successor of the owner's Playability
//   Checker plugin for MuseScore 3.6. A pass over the score's bowed-string staves judges every
//   chord (playabilityrules.h) and keeps, beside the score, a mark per note and a row per problem.
//   Nothing is written to the score: Note::draw colours a marked notehead the way MuseScore colours
//   notes out of an instrument's range, never when printing, so files stay as MuseScore 3.6 reads
//   and writes them. The pass runs after each layout while the checker is on (Score::doLayoutRange).
//---------------------------------------------------------

#include "fraction.h"

#include <vector>
#include <QColor>
#include <QHash>
#include <QString>
#include <QStringList>

namespace Ms {

class Note;
class Part;
class Score;

enum class PlayMark : char {
      NONE,
      OPEN,             // played on an open string
      OUT_OF_REACH,     // a stretch, or risky: dark yellow, as MuseScore's "out of amateur range"
      IMPOSSIBLE        // red, as MuseScore's "out of professional range"
      };

struct PlayabilityRow {
      int bar { 0 };                // 1-based measure index (not the displayed number), as the plugin
      Fraction tick;
      Fraction tickEnd;
      int track { -1 };
      int grace { -1 };             // -1 the main chord, else its index in graceNotes()
      QString staff;                // the part's long name
      QString staffShort;           // its short name, else an abbreviation of the long name
      QString kind;                 // "stop", "harmonic"
      QString verdict;              // "impossible", "outOfReach", "risky"
      QString reason;
      QString notes;
      bool error() const { return verdict == "impossible"; }
      };

struct PlayabilityResult {
      QHash<const Note*, PlayMark> marks;
      std::vector<PlayabilityRow> rows;
      int open { 0 };
      int playable { 0 };
      int outOfReach { 0 };
      int impossible { 0 };
      int div { 0 };
      int harmonics { 0 };
      };

class Chord;

//---------------------------------------------------------
//   ChordInfo: one chord for the panel, by the same rules as the pass: the Selected line and the
//   fingerboard (a playable stop, or a natural harmonic's strings and nodes)
//---------------------------------------------------------

struct FingerNote {
      double pitch;
      QString name;
      int string;
      double offset;                // semitones above its open string
      };

struct HarmonicNodeInfo {
      int pitch;
      QString name;
      int num, den;                 // the node's place from the nut
      bool solo;
      };

struct HarmonicOptionInfo {
      int string;
      int partial;
      int sounds;
      QString soundsName;
      bool solo;
      std::vector<HarmonicNodeInfo> nodes;
      };

struct HarmonicNoteInfo {
      int pitch;
      QString name;
      std::vector<HarmonicOptionInfo> options;
      };

struct ChordInfo {
      enum class Kind : char { NONE, STOP, HARMONIC };
      QString text;                 // the Selected line, without "Selected: "
      bool bowedString { false };   // the chord is on a bowed string instrument
      Kind kind { Kind::NONE };     // what the fingerboard shows
      QString instrument;
      std::vector<int> strings;
      QStringList stringNames;
      QString tuning;               // a scordatura in force (strings low to high), else empty
      std::vector<FingerNote> notes;                // STOP
      int stopped { 0 };
      double worst { 0 };
      double position { 0 };
      double reach { 0 };
      std::vector<HarmonicNoteInfo> harmonics;      // HARMONIC
      bool atNode { false };
      };

namespace Playability {

// the checker's switches, set from the preferences (mscore/musescore.cpp), like MScore::warnPitchRange
extern bool enabled;                  // the checker runs and marks notes
extern bool openStringMarks;          // open strings are marked (the owner: switchable)
extern QColor openStringColor;        // the owner's choice, 2026-09-27: slate grey #7d8791

const QColor IMPOSSIBLE_COLOR = QColor(Qt::red);
const QColor OUT_OF_REACH_COLOR = QColor(Qt::darkYellow);

// the whole pass; always runs (tests call it directly), whatever `enabled` says
PlayabilityResult analyse(Score* score);
// the panel's view of one chord (a grace chord too)
ChordInfo inspect(Chord* chord);
// the chord a selection is about: the first selected note or part of a chord, walking up
Chord* selectedChord(Score* score);
// the staff column's name: the part's short name, else an abbreviation of its long name
QString shortStaffName(const Part* part, const QString& longName, const Fraction& tick);
// the colour Note::draw gives a note, or an invalid QColor
QColor markColor(const Note* note, bool selected);

}     // namespace Playability
}     // namespace Ms
#endif
