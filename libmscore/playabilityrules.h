//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYABILITYRULES_H__
#define __PLAYABILITYRULES_H__

//---------------------------------------------------------
//   The playability checker's rules for bowed strings: multiple stops (S1 open strings, S2
//   stops), natural and artificial harmonics (S7, S8), div./unis. texts.
//
//   Ported from the owner's Playability Checker plugin for MuseScore 3.6 (strings/strings.js,
//   strings/harmonics.js, frozen since 2026-09-27); every limit and its source is as there, and
//   the reasons read word for word as the plugin's (its test results are the parity tests,
//   mtest/libmscore/playability). All pitches are SOUNDING MIDI numbers, moved by a note's
//   microtonal part (soundingPitch): a quarter-sharp G3 is 55.5.
//---------------------------------------------------------

#include <map>
#include <vector>
#include <QString>

namespace Ms {
namespace Playability {

//---------------------------------------------------------
//   StringInstrument
//
//   strings   sounding MIDI, string I (highest) first; Adler, The Study of Orchestration,
//             3rd ed., p. 9 (Ex 2-1…2-5), p. 44
//   span0     the 1st–4th finger stretch on ONE string, at the nut, in semitones (Forsyth,
//             Orchestration 1914: vn p. 357 fn aug 4th, va p. 391 P4, vc p. 425 M3, cb p. 441
//             whole tone). Used by the fingered tremolo check (S13).
//   crossMax  how much further up the neck the higher string may be stopped than its
//             neighbour, at the nut: Forsyth's interval with the 1st finger on the lower string
//             and the 4th on the next, less the tuning's fifth (vn minor 9th 13 − 7 = 6, va
//             octave 12 − 7 = 5, vc minor 7th 10 − 7 = 3); the bass reuses its whole tone
//             (derived, conservative).
//   requireOpenString  the double bass needs an open string in a stop (Adler p. 11, p. 86)
//   section   a section, not a single player (isSection)
//---------------------------------------------------------

struct StringInstrument {
      QString name;                 // "Violin", "Viola", "Cello", "Double bass"
      std::vector<int> strings;
      double span0 { 0 };
      double crossMax { 0 };
      bool requireOpenString { false };
      bool section { false };
      bool valid() const { return !strings.empty(); }
      };

// instrumentId: the MusicXML sound id (Instrument::instrumentId(), "strings.violin"); longName:
// the part's or instrument's long name (fallback); program: the arco channel's MIDI program, or -1.
StringInstrument lookup(const QString& instrumentId, const QString& longName, int program);
bool isSection(const QString& instrumentId, const QString& longName, int program);

//---------------------------------------------------------
//   Spelling: names follow the score. A note in the chord is spelled as written (tpc1, the concert
//   spelling); a pitch worked out here (a harmonic's node or sound) takes the spelling of the same
//   pitch class in the chord, else follows the key signature (sharps in sharp keys, flats in flat
//   keys, the plugin's old default in C). A microtonal note is named with its offset in cents.
//---------------------------------------------------------

struct SpelledNote {
      int pitch;
      int tpc;
      double cents;
      };

class Spelling {
      struct Sound { int tpc; int pitch; double cents; };
      std::map<double, Sound> _bySound;
      std::map<int, int> _byPitch;
      std::map<int, int> _byPc;
      int _fifths { 0 };
      bool _set { false };

   public:
      Spelling() {}
      Spelling(const std::vector<SpelledNote>& notes, int fifths);
      QString name(double pitch) const;
      };

// Microtones: within MICRO_MIN_CENTS of the equal-tempered pitch counts as that pitch (a HEJI
// schisma is not a different finger); the temperament never counts (a meantone G# is a G#).
constexpr double MICRO_MIN_CENTS = 5.0;
double soundingPitch(int pitch, double cents);
QString centsSuffix(double cents);
QString tpcName(int tpc, int pitch);
QString plainName(int pitch);                        // no spelling context: C C# D Eb E F F# G Ab A Bb B
QString stringName(int openPitch);                   // "G", never "G3": every open string is a natural
extern const char* const ROMAN[5];

// one decimal, no trailing ".0" (the plugin's fmtReach)
QString fmtReach(double x);

//---------------------------------------------------------
//   S2 multiple stops
//---------------------------------------------------------

enum class Verdict : char { PLAYABLE, OUT_OF_REACH, IMPOSSIBLE };

struct StopResult {
      Verdict verdict { Verdict::PLAYABLE };
      std::vector<int> assign;      // string index per note, -1 for a note with no string
      QString reason;
      double worst { 0 };
      };

// how far the hand reaches with its nearest stopped note `pos` semitones above the open string
double spanAt(double limit, double pos);
double reachAt(const StringInstrument& in, double pos);
int openStringIndex(const StringInstrument& in, double pitch);
// pitches descending
StopResult analyseStop(const StringInstrument& in, const std::vector<double>& pitches);
// "G3 (IV) + A3 (—)": bottom note first
QString describe(const StringInstrument& in, const std::vector<double>& pitches, const StopResult& res, const Spelling& sp);

//---------------------------------------------------------
//   S7 / S8 harmonics
//---------------------------------------------------------

enum class HarmonicVerdict : char { OK, RISKY, IMPOSSIBLE };

struct HarmonicNote {
      int pitch;
      bool diamond;                 // a diamond notehead: written at the node
      bool circle;                  // a harmonic circle: written at the sounding pitch
      };

struct HarmonicResult {
      bool harmonic { false };      // false: an ordinary chord
      HarmonicVerdict verdict { HarmonicVerdict::OK };
      QString reason;
      QString detail;
      };

HarmonicResult classifyHarmonic(const StringInstrument& in, const std::vector<HarmonicNote>& list, const Spelling& sp);

//---------------------------------------------------------
//   div. state (Adler p. 12): 1 div. on, 0 off (unis.), -1 no keyword
//---------------------------------------------------------

int divState(const QString& text);
// a text as a player reads it: a dynamic's SMuFL symbols become their letters, other tags go
QString plainText(const QString& xmlText);

}     // namespace Playability
}     // namespace Ms
#endif
