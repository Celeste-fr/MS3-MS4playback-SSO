//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __TEMPOCHANGE_H__
#define __TEMPOCHANGE_H__

//---------------------------------------------------------
//   Gradual tempo changes, as MuseScore 4 plays them, on MuseScore 3's text lines: a text line
//   whose begin text is rit., rall., accel. … (or that has a tempo change set in the Inspector)
//   changes the tempo over its length, like a hairpin the dynamics:
//
//   - from the tempo in force at its start to that tempo × the factor (Inspector: "Tempo
//     change", in %; 0 / "Auto": MuseScore 4's default for the term, rit. 75 %, accel. 133 % …),
//   - on the curve of the Inspector's method (linear, ease in / out / in and out, exponential),
//   - the end tempo stays until the next tempo marking; a tempo marking at the line's end takes
//     over there (as a dynamic at a hairpin's end).
//
//   Playback only adds points to the tempo map (rebuildTempoAndTimeSigMaps, a 32nd apart), the
//   way the TempoChanges plugin's hidden tempo markings did, without writing anything in the
//   score. The Inspector's values are kept in the score's metaTag "tempoChanges" (MuseScore 3.6
//   keeps metaTags through a round trip): JSON [{"tick", "tick2", "track", "factor", "method"}],
//   written on save (the lines' current positions), read after loading.
//---------------------------------------------------------

#include <QString>

namespace Ms {

class Fraction;
class MasterScore;
class Score;
class TextLine;
enum class ChangeMethod : signed char;

namespace TempoChange {

extern const char* const metaTag;

// the default end tempo (% of the start) of the term a text begins with; 0: none
double defaultFactor(const QString& text);
// the line changes the tempo; its end tempo in % of the start
bool isTempoChange(const TextLine* line);
double factor(const TextLine* line);
// 0 … 1 of the way from the start tempo to the end one, at x (0 … 1) of the line
double curve(ChangeMethod method, double x);

// the lines' points of the tempo map in [from, to) (the master score's measure being rebuilt)
void addToTempoMap(Score* score, const Fraction& from, const Fraction& to);

// the metaTag: to the lines after loading (and out of the tags), from them on saving
void read(MasterScore* score);
QString write(const Score* score);

}     // namespace TempoChange
}     // namespace Ms
#endif
