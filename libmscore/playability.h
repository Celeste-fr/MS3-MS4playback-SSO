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

namespace Ms {

class Note;
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

namespace Playability {

// the checker's switches, set from the preferences (mscore/musescore.cpp), like MScore::warnPitchRange
extern bool enabled;                  // the checker runs and marks notes
extern bool openStringMarks;          // open strings are marked (the owner: switchable)
extern QColor openStringColor;        // the owner's choice, 2026-09-27: slate grey #7d8791

const QColor IMPOSSIBLE_COLOR = QColor(Qt::red);
const QColor OUT_OF_REACH_COLOR = QColor(Qt::darkYellow);

// the whole pass; always runs (tests call it directly), whatever `enabled` says
PlayabilityResult analyse(Score* score);
// the colour Note::draw gives a note, or an invalid QColor
QColor markColor(const Note* note, bool selected);

}     // namespace Playability
}     // namespace Ms
#endif
