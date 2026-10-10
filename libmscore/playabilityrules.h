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
#include "playabilitywindsdyn.h"

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
// W2/B1 levels (SPEC-w2b1): soft = the pp tier (pp or softer), p/mp = the p tier, fff = velocity 126
// or more, MuseScore's own fff velocity (owner 2026-10-10)
bool windLevelMatches(WindLevel level, double velocity);

// S10 slurred staccato, SECTIONS only (Wagner p. 35: four to six notes soft, three at f-ff;
// Forsyth p. 344, Sevsay p. 13); soloists: no count limit.
constexpr int GROUP_STACCATO_SOFT = 6;
constexpr int GROUP_STACCATO_LOUD = 3;
// S10 jeté, SECTIONS only: more than 6 notes on one stroke is a warning on every bowed string
// (Sevsay p. 18, Wagner p. 40; the owner's choice, 2026-09-14). A single player's jeté is timed.
constexpr int JETE_MAX = 6;
// S12 fast passages, double bass SECTIONS only: notes under 0.1 s each, a run over 1.5 s (placed
// between the composers' marked examples: Adler p. 84, Prout p. 27, Jadassohn p. 318 / 175,
// Forsyth p. 454, Kennan p. 26; no source states them)
constexpr double FAST_NOTE_SECONDS = 0.1;
constexpr double FAST_RUN_SECONDS = 1.5;

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

//---------------------------------------------------------
//   H1 harp pedals, H2 harp hands, P1 timpani, K1 keyboard span: the owner's approved spec
//   (diagrams-spec-htk.md, 2026-10-08). Severity: what a book calls impossible is red, what it
//   calls difficult / rare / to avoid is a warning.
//---------------------------------------------------------

// letters: 0 C, 1 D, 2 E, 3 F, 4 G, 5 A, 6 B
struct HarpNote {
      int letter { 0 };
      int alter { 0 };              // -2 … +2
      int octave { 0 };             // the octave of the letter: Cb4 is on the C string of octave 4
      };
HarpNote harpNote(int tpc, int pitch);
// 47 strings, C1 to G7 (Adler p. 90, Ex 4-1): with the pedals Cb1 to G#7
bool harpHasString(const HarpNote& n);
int harpStringIndex(const HarpNote& n);              // 0 = the C1 string; one per letter and octave
// the C and D of octave 1 have no pedal: retuned by hand (Adler p. 90)
bool harpHandTuned(const HarpNote& n);
// the bottom seven strings, C1 to B1 (the string layout, Adler p. 90 Ex 4-1; Blatter p. 256)
bool harpLowestOctave(const HarpNote& n);
// pedal order, left to right: D C B | E F G A; the left foot works D C B (Adler p. 91)
extern const int HARP_PEDAL_ORDER[7];                // letters
bool harpLeftFoot(int letter);
// the key signature's alteration of a letter (fifths: + sharps, - flats)
int keyAlter(int fifths, int letter);
QString letterName(int letter, int alter);           // "Cb", "F#", "D"
// the same pitch on a neighbouring letter, a single accidental at most (Cb = B, F = E#, F# = Gb);
// empty for D, G and A, which have none (Kennan & Grantham p. 278), and for double accidentals
QString harpEnharmonic(int letter, int alter);
// H2: more notes in one hand at once is red (Blatter p. 256, K&G p. 281); a chord wider than a
// 10th in one hand a warning: ten strings, both ends counted (Blatter p. 256, K&G p. 281)
constexpr int HARP_HAND_NOTES = 4;
constexpr int HARP_HAND_STRINGS = 10;

//---------------------------------------------------------
//   P1 timpani: drum ranges (Adler p. 445 Ex 12-24, K&G p. 227 Ex 13.1, Blatter p. 211 Ex 5.4),
//   four drums, a fifth (the 20″ piccolo) only when the part names one; 15 s to retune a drum
//   (Blatter p. 210)
//---------------------------------------------------------

struct TimpaniDrum {
      const char* size;
      int lo, hi;                   // sounding MIDI
      double middle() const { return (lo + hi) / 2.0; }
      };
std::vector<TimpaniDrum> timpaniDrums(bool fifth);   // largest first
constexpr double TIMPANI_RETUNE_SECONDS = 15.0;
// a text naming a fifth drum: "5 drums", "five timpani", "piccolo timpano", "timpano piccolo"
bool timpaniFifthDrum(const QString& text);

struct TimpaniMoment {
      double start { 0 };           // seconds
      std::vector<int> pitches;     // distinct, ascending
      std::vector<double> ends;     // per pitch: the end of its longest note, seconds
      };
struct TimpaniNotePlan {
      enum class Problem : char { NONE, RANGE, TOO_MANY, NO_DRUM, RETUNE };
      int pitch { 0 };
      int drum { -1 };
      Problem problem { Problem::NONE };
      int from { -1 };              // the drum's pitch before a retune, else -1
      double seconds { 0 };         // the time the retune has
      };
struct TimpaniPlan {
      std::vector<std::vector<TimpaniNotePlan>> moments;
      std::vector<std::vector<int>> tuning;          // per moment, per drum after it: pitch or -1
      };
// The planner (K&G p. 227, Blatter p. 211): a pitch goes to a free drum already tuned to it; else
// to a free drum whose range holds it, choosing (1) one that leaves TIMPANI_RETUNE_SECONDS from its
// last note to this one (a drum not yet used always does), (2) a drum not yet used, so a tuned
// drum keeps its tuning, (3) the drum whose middle is nearest the pitch, (4) the larger drum.
// Pitches with fewer drums to choose from are placed first. A drum is free when it has no note
// at this moment and its last note has ended.
TimpaniPlan planTimpani(const std::vector<TimpaniMoment>& moments, bool fifth);

//---------------------------------------------------------
//   K1 keyboard hand span (Blatter p. 244): more than a 9th a warning, more than a 10th red.
//   Intervals bands are inclusive: a 9th is up to the major 9th (14 semitones), a 10th up to
//   the major 10th (16). Measured in semitones: keys are as wide whatever their spelling.
//---------------------------------------------------------

constexpr int KEYBOARD_NINTH = 14;
constexpr int KEYBOARD_TENTH = 16;
constexpr int KEYBOARD_OCTAVE = 12;
// a keyboard with a grand staff: piano, harpsichord, celesta, clavichord, organ, harmonium,
// virginal (not accordions); harp, timpani by their MusicXML id, else the name
bool isKeyboard(const QString& instrumentId, const QString& name);
bool isHarp(const QString& instrumentId, const QString& name);
bool isTimpani(const QString& instrumentId, const QString& name);

//---------------------------------------------------------
//   B4-B6 trombone slide, valve brass fingerings: the owner's approved spec (diagrams-spec-brass.md,
//   2026-10-08). The lists come from Blatter's charts (playabilitybrass.h, generated): trombone
//   pp. 469-470, valved brass pp. 463-464, horn pp. 461-462; where a chart covers a note its entries
//   are the list, in printed order, the first the standard one. Outside a chart's range the list is
//   derived: partials 2-16 (Adler p. 299, BR1) of each valve combination's or position's fundamental,
//   valve steps 2 / 1 / 3 semitones (Adler p. 303, Blatter p. 459), the 4th valve a perfect 4th (the chart's +4 row
//   bears it out); no 7th partial on valve brass (Blatter p. 459, BR8).
//---------------------------------------------------------

enum class Brass : char { NONE, TENOR_TROMBONE, BASS_TROMBONE, ALTO_TROMBONE, CONTRABASS_TROMBONE,
      HORN, TRUMPET, EUPHONIUM, BARITONE, F_TUBA, EB_TUBA, CC_TUBA, BBB_TUBA };
// by the MusicXML id (brass.trombone…, brass.french-horn, brass.trumpet…, brass.cornet…,
// brass.flugelhorn, brass.euphonium, brass.baritone-horn, brass.tuba…), else the name; a tuba's key
// from its name (generic: BB-flat, MuseScore's tuba range starting on that fundamental)
Brass brassType(const QString& instrumentId, const QString& name);
bool isTrombone(Brass b);
QString brassName(Brass b);

constexpr int VALVE_STEP[4] = { 2, 1, 3, 5 };       // valves 1-4, semitones; the 5th's is not known
constexpr int BRASS_PARTIAL_LO = 2;                 // derived lists (BR1)
constexpr int BRASS_PARTIAL_HI = 16;
constexpr int BRASS_SPECIALIST = 9;                 // partials 9 and up (Blatter p. 458, BR4)
bool outOfTunePartial(int n);                       // 7, 11, 13, 14 (Adler p. 299, BR2)
// the harmonic n whose 12 log2 n lies within half a semitone of the interval; 0 none
int partialOf(int semitones);
// a raised (♯) slide position: the partial just under the note, by less than a semitone
int raisedPartialOf(int semitones);

// a text naming a trombone attachment ("F attachment", "F trigger", "with F", "E attachment"):
// bit 1 F, bit 2 E
int brassAttachments(const QString& text);
constexpr int ATTACH_F = 1;
constexpr int ATTACH_E = 2;
// a text naming the valves ("4 valves", "five-valve"): their count, else 0
int brassValveText(const QString& text);
// the valves assumed without a text: trumpet group 3 (Adler p. 335; Sevsay p. 93), euphonium 4
// (Adler p. 354; Sevsay p. 109), baritone 3 (Sevsay p. 109), tubas 4 (Sevsay p. 109; Kennan &
// Grantham, tuba section), horn 3 and the thumb
int brassValves(Brass b);
// whether partials 9-16 are labelled "specialists": not on horn and tubas, whose players reach the
// 16th partial (Blatter p. 458, BR4); unnamed there (trombones, euphonium, baritone): the usual 1-8
bool brassSpecialists(Brass b);

struct BrassEntry {
      // a slide position
      int side { 0 };               // 0 no attachment, 1 F, 2 E
      int position { 0 };           // as Blatter numbers it, 1-7
      bool raised { false };
      // a fingering
      int mask { 0 };               // bit 0 valve 1 … bit 4 valve 5; 0x20 the horn's thumb
      int chartRow { -1 };          // 0 the 3-valve row, 1 +4, 2 +5; -1 derived or the horn
      // both
      int partial { 0 };            // 0 not known (5th valve)
      bool pedal { false };         // the chart's pedal bracket, or partial 1
      bool extra { false };         // an attachment the part doesn't name (tenor's F)
      bool playable { true };       // with the instrument's valves
      bool derived { false };
      QString name;                 // "II", "♯IV", "F VI", "E 3"; "0", "1+3", "T2+3"
      QStringList labels;
      double slot() const;          // on the slide: 0 = I … 6 = VII, a raised position a little toward I
      };

// B6 lists: sounding pitch for trombones, tubas, euphonium and baritone; written for the trumpet
// group (any key) and the horn (as in F)
std::vector<BrassEntry> slideEntries(Brass b, int attachments, int pitch);
std::vector<BrassEntry> valveEntries(Brass b, int valves, int pitch);
std::vector<BrassEntry> brassEntries(Brass b, int attachments, int valves, int pitch);
// the pitch the lists are read at
int brassChartPitch(Brass b, int sounding, int transposeChromatic);
QString slidePositionName(int side, int position, bool raised);
QString fingeringName(int mask);
// B5: a glissando between two sounding pitches: 0 fine, 1 only on the pedal partial (warning), 2 red;
// why in *reason
int slideGlissando(Brass b, int attachments, int from, int to, QString* reason);
constexpr int GLISSANDO_MAX = 6;                    // a tritone (Adler p. 347, TB17)

}     // namespace Playability
}     // namespace Ms
#endif
