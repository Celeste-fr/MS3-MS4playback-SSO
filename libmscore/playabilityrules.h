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
#include <QStringList>

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
      QStringList stringNames;      // as a scordatura text spells them; empty: from the pitch
      QString tuning;               // a scordatura in force: its strings, low to high ("F D A E"); else empty
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
// The owner (2026-10-04): to depend on the note's frequency, from the pitch difference limen of Wier, Jesteadt &
// Green 1977 (JASA 61), once the paper (or Moore, An Introduction to the Psychology of Hearing) is here; 5 until then
// (the only figure found, Rossing 2010 p. 379, spans 2.6-58 cents over the range)
constexpr double MICRO_MIN_CENTS = 5.0;
double soundingPitch(int pitch, double cents);
QString centsSuffix(double cents);
QString tpcName(int tpc, int pitch);
QString plainName(int pitch);                        // no spelling context: C C# D Eb E F F# G Ab A Bb B
QString stringName(int openPitch);                   // "G", never "G3": every open string is a natural
QString stringName(const StringInstrument& in, int index);    // under scordatura as its text spells it
extern const char* const ROMAN[7];
QString roman(int index);

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

// Every way to play one natural harmonic, highest string first. atNode: the written pitch is the
// node (diamond), else the sounding pitch (circle).
struct HarmonicNode {
      int pitch;
      int num;                      // the node's place on the string from the nut: num / den
      int den;
      bool solo;                    // the 2/5 node
      };
struct HarmonicOption {
      int string;
      int partial;
      int sounds;
      bool solo;                    // every node for it is solo only
      std::vector<HarmonicNode> nodes;
      };
std::vector<HarmonicOption> naturalOptions(const StringInstrument& in, int pitch, bool atNode);
// the Selected line for a natural harmonic chord: every string, node and sounding pitch of each
// note; empty when the chord is not a readable natural harmonic. atNode is set.
QString inspectNatural(const StringInstrument& in, const std::vector<HarmonicNote>& list, const Spelling& sp, bool* atNode = nullptr);

//---------------------------------------------------------
//   Bowing (S10–S13)
//---------------------------------------------------------

// S11: the longest slur, in seconds, on one bow, per dynamic tier (violin; viola as violin, cello
// and bass × 0.6). Sevsay p. 10's scale 12 / 6 / 3 / 1 / 0.5 s for pp … ff; warn / red values set
// inside the range the other sources give: pp 12 / 15 (Widor p. 163, Forsyth p. 343 ≈ 10–13 s;
// Askenfelt 1986 p. 1011, Flesch p. 64 ≈ 15 s), p 6 / 12 (Wagner p. 30), mf 3 / 6 (Russo), f 2.5 /
// 4.5 (Forsyth p. 390, Schoonderwaldt 2009 p. 2715, Wagner p. 30), ff 1.5 / 3 (Sevsay, Russo).
// Cello and bass × 0.6 derived from Forsyth p. 445 (bass bow changed every 3–4 s at p). Without any
// dynamic a passage counts as mf (velocity 80).
struct BowLimit {
      bool valid { false };
      double warn { 0 };
      double red { 0 };
      };
BowLimit bowLimit(const StringInstrument& in, const QString& tier);
constexpr int DEFAULT_VELOCITY = 80;

// Dynamics as MuseScore's velocities (verified on 3.6.2): pp 33, p 49, mf 80, f 96, ff 112 …; a
// dynamic that changes after its attack settles at velocity + change (fp 49, sfp 65). Accents (sf,
// sfz, fz, rf, the lone letters) are not a level: -1, as is a text that is no dynamic.
// elVelocity / elChange: a Dynamic's own values, or -1 / 0 for other texts.
int dynamicVelocity(const QString& text, int elVelocity, int elChange);
// f or louder: settled velocity 89 and up (Wagner p. 35: mf counts with the softer dynamics)
bool isLoud(double velocity);
// S11 tiers, split halfway between MuseScore's default velocities; mp goes with p
QString dynamicTier(double velocity);
int tierOrder(const QString& tier);

// S10 slurred staccato, SECTIONS only (Wagner p. 35: four to six notes soft, three at f-ff;
// Forsyth p. 344, Sevsay p. 13); soloists: no count limit.
constexpr int GROUP_STACCATO_SOFT = 6;
constexpr int GROUP_STACCATO_LOUD = 3;
// S10 jeté, SECTIONS only: more than 6 notes on one stroke is a warning on every bowed string
// (Sevsay p. 18, Wagner p. 40; the owner's choice, 2026-09-14). A single player's jeté is timed.
constexpr int JETE_MAX = 6;
// S12 fast passages, double bass SECTIONS only: notes of 94 ms or less each, a run over 1.25 s. The owner's choice
// (2026-10-04, candidate A of the review page "Checker Speed Sources"): both are the books' own examples, not a
// threshold placed between them. 94 ms: the one criticized run's notes (Adler p. 84, Ex 3-69, Beethoven 4 finale
// sixteenths at half = 80, "muddied"); 1.25 s: the longest run accepted (Kennan p. 26, Ex 2.22a, Beethoven 5 trio,
// 12 eighths at dotted half = 96). Readings of Jadassohn, Prout and Forsyth on that page are from the earlier research
// table, not re-read against the books
constexpr double FAST_NOTE_SECONDS = 0.094;
constexpr double FAST_RUN_SECONDS = 1.25;

// S13 fingered tremolo ("between notes"): fine on one string within span0 at that position or with
// an open string; out of reach when both notes are stopped on two adjacent strings (Forsyth
// p. 355-357: "somewhat shabby and ineffective"); impossible when no fingering reaches.
struct TremoloResult {
      Verdict verdict { Verdict::IMPOSSIBLE };
      bool fine { false };
      int lower { -1 };             // string indices
      int upper { -1 };
      int interval { 0 };
      };
TremoloResult fingeredTremolo(const StringInstrument& in, int a, int b);
QString intervalName(int semitones);
// "3.8 s": one decimal, as the plugin's fmtSeconds
QString fmtSeconds(double x);

//---------------------------------------------------------
//   Text states: 1 on, 0 off, -1 no keyword
//   div. (Adler p. 12); jeté on (jeté, gettato, ricochet), off by Adler's cancel words (p. 33) or
//   another stroke; pizz. / arco
//---------------------------------------------------------

int jeteState(const QString& text);

//---------------------------------------------------------
//   Scordatura: the strings' tuning. A part's String Data (Staff/Part Properties › Edit String
//   Data, saved in the file) is its tuning; a staff text naming the strings, low to high, after
//   "scord." / "scordatura" ("scord. G D A Eb", "Scordatura: F–D–A–E", octaves optional:
//   "G3 D4 A4 Eb5") retunes from there, and "normal tuning" / "standard tuning" / "accord." /
//   "accordatura" goes back to the String Data (the owner's choice, 2026-09-28). Notes are read at
//   sounding pitch.
//---------------------------------------------------------

struct ScordaturaText {
      bool reset { false };         // back to the String Data
      struct Str { int pc; int octave; QString name; };   // octave -100: not given
      std::vector<Str> strings;     // low to high, as written
      };

// false: the text is not about the tuning
bool scordaturaText(const QString& text, ScordaturaText* out);
// the instrument retuned: a text's strings, each without an octave placed nearest the string it
// replaces; left as it is when the count differs from the instrument's strings
StringInstrument retune(const StringInstrument& in, const ScordaturaText& t);
// from String Data pitches (low to high); the standard tuning when there are none
StringInstrument withStrings(const StringInstrument& in, const std::vector<int>& lowToHigh);
int pizzState(const QString& text);

//---------------------------------------------------------
//   div. state (Adler p. 12): 1 div. on, 0 off (unis.), -1 no keyword
//---------------------------------------------------------

int divState(const QString& text);
// a text as a player reads it: a dynamic's SMuFL symbols become their letters, other tags go
QString plainText(const QString& xmlText);

}     // namespace Playability
}     // namespace Ms
#endif
