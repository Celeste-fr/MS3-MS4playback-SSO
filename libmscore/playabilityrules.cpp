//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playabilityrules.h"
#include "playabilitybrass.h"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <QRegularExpression>
#include <QStringList>

namespace Ms {
namespace Playability {

const char* const ROMAN[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };

QString roman(int index)
      {
      return index >= 0 && index < 7 ? QString(ROMAN[index]) : QString::number(index + 1);
      }

static const char* const NAMES[12]       = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
static const char* const SHARP_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* const FLAT_NAMES[12]  = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };

static const QString DASH = QString(QChar(0x2014));
static const QString ARROW = QString(QChar(0x2192));

// JavaScript's Math.round: halves go up
static double jsRound(double x) { return std::floor(x + 0.5); }
static int floorDiv(int a, int b) { return int(std::floor(double(a) / b)); }
static int pc(int pitch) { return ((pitch % 12) + 12) % 12; }

//---------------------------------------------------------
//   instruments
//---------------------------------------------------------

static StringInstrument make(const char* name, std::vector<int> strings, double span0, double crossMax, bool requireOpen = false)
      {
      StringInstrument in;
      in.name = name;
      in.strings = strings;
      in.span0 = span0;
      in.crossMax = crossMax;
      in.requireOpenString = requireOpen;
      return in;
      }

static StringInstrument baseInstrument(const QString& id, const QString& longName)
      {
      static const std::vector<std::pair<QString, StringInstrument>> INSTRUMENTS = {
            { "strings.violin",     make("Violin",      { 76, 69, 62, 55 }, 6, 6) },
            { "strings.viola",      make("Viola",       { 69, 62, 55, 48 }, 5, 5) },
            { "strings.cello",      make("Cello",       { 57, 50, 43, 36 }, 4, 3) },
            { "strings.contrabass", make("Double bass", { 43, 38, 33, 28 }, 2, 2, true) },
            };
      // fallback when the id is missing or unusual: the long name
      static const std::vector<std::pair<QRegularExpression, QString>> NAME_HINTS = {
            { QRegularExpression("violoncell|cello|\\bvc\\b"),                     "strings.cello" },
            { QRegularExpression("contrabass|double\\s*bass|kontrabass|\\bcb\\b"), "strings.contrabass" },
            { QRegularExpression("viola|bratsche|\\bvla\\b"),                      "strings.viola" },
            { QRegularExpression("violin|violine|geige|\\bvln\\b"),                "strings.violin" },
            };
      for (const auto& p : INSTRUMENTS)
            if (id == p.first)
                  return p.second;
      if (!id.isEmpty())
            for (const auto& p : INSTRUMENTS)               // e.g. "strings.violin-section"
                  if (id.startsWith(p.first))
                        return p.second;
      QString n = longName.toLower();
      for (const auto& h : NAME_HINTS)
            if (h.first.match(n).hasMatch())
                  for (const auto& p : INSTRUMENTS)
                        if (p.first == h.second)
                              return p.second;
      return StringInstrument();
      }

//---------------------------------------------------------
//   isSection
//    Section or single player (the owner's rule, 2026-09-14): a plural name (Violins, Violini,
//    Celli, Basses …), MuseScore's section id "strings.group", or a section sound on the arco
//    channel (GM 48–51: String Ensemble 1/2, Synth Strings 1/2; the single instruments use 40–43).
//---------------------------------------------------------

bool isSection(const QString& instrumentId, const QString& longName, int program)
      {
      static const QRegularExpression SECTION_NAME(QString::fromUtf8(
            "(^|[^a-z])(violins|violini|violas|viole|violoncellos|violoncelli|cellos|celli|contrabasses|contrabassi|"
            "double[\\s-]*basses|basses|kontrabässe|bratschen|violinen)([^a-z]|$)"));
      if (instrumentId == "strings.group")
            return true;
      if (SECTION_NAME.match(longName.toLower()).hasMatch())
            return true;
      return program >= 48 && program <= 51;
      }

StringInstrument lookup(const QString& instrumentId, const QString& longName, int program)
      {
      StringInstrument in = baseInstrument(instrumentId, longName);
      if (in.valid())
            in.section = isSection(instrumentId, longName, program);
      return in;
      }

//---------------------------------------------------------
//   names
//---------------------------------------------------------

QString fmtReach(double x)
      {
      return QString::number(jsRound(x * 10) / 10, 'g', 12);
      }

double soundingPitch(int pitch, double cents)
      {
      return std::fabs(cents) >= MICRO_MIN_CENTS ? pitch + cents / 100 : pitch;
      }

QString centsSuffix(double cents)
      {
      int c = int(jsRound(cents));
      if (std::abs(c) < MICRO_MIN_CENTS)
            return QString();
      return (c > 0 ? QString("+%1").arg(c) : QString(QChar(0x2212)) + QString::number(-c)) + QChar(0x00a2);
      }

QString tpcName(int tpc, int pitch)
      {
      QChar letter = QString("FCGDAEB").at((((tpc + 1) % 7) + 7) % 7);
      int alter = floorDiv(tpc + 1, 7) - 2;                     // -2 .. +2
      QString acc;
      for (int i = 0; i < std::abs(alter); ++i)
            acc += alter > 0 ? "#" : "b";
      return letter + acc + QString::number(floorDiv(pitch - alter, 12) - 1);   // B#3 is pitch 60
      }

QString plainName(int pitch)
      {
      return QString(NAMES[pc(pitch)]) + QString::number(floorDiv(pitch, 12) - 1);
      }

QString stringName(int openPitch)
      {
      return NAMES[pc(openPitch)];
      }

QString stringName(const StringInstrument& in, int index)
      {
      if (index >= 0 && index < in.stringNames.size() && !in.stringNames[index].isEmpty())
            return in.stringNames[index];
      return index >= 0 && index < int(in.strings.size()) ? stringName(in.strings[index]) : QString("?");
      }

//---------------------------------------------------------
//   scordatura
//---------------------------------------------------------

bool scordaturaText(const QString& text, ScordaturaText* out)
      {
      static const QRegularExpression RESET(QString::fromUtf8("(normal|standard|usual|ordinary|regular)\\s+tuning|\\baccord(\\.|atura)?(?![a-z])|\\bord(\\.|inario)?\\s+tuning|\\bscord(\\.|atura)?\\s+(off|ends?)\\b"),
                                        QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression SCORD("\\bscord(\\.|atura)?", QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression NOTE(QString::fromUtf8("(?<![A-Za-z])([A-G])(bb|𝄫|♭♭|b|♭|##|x|𝄪|♯♯|#|♯)?(-?\\d)?(?![A-Za-z])"));
      ScordaturaText t;
      if (RESET.match(text).hasMatch()) {
            t.reset = true;
            *out = t;
            return true;
            }
      QRegularExpressionMatch sm = SCORD.match(text);
      if (!sm.hasMatch())
            return false;
      static const int LETTER_PC[7] = { 9, 11, 0, 2, 4, 5, 7 };     // A B C D E F G
      auto it = NOTE.globalMatch(text, sm.capturedEnd());
      while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            QString acc = m.captured(2);
            int alter = 0;
            if (acc == "b" || acc == QString::fromUtf8("♭"))
                  alter = -1;
            else if (acc == "bb" || acc == QString::fromUtf8("𝄫") || acc == QString::fromUtf8("♭♭"))
                  alter = -2;
            else if (acc == "#" || acc == QString::fromUtf8("♯"))
                  alter = 1;
            else if (!acc.isEmpty())
                  alter = 2;
            int letter = m.captured(1).at(0).unicode() - 'A';
            int octave = m.captured(3).isEmpty() ? -100 : m.captured(3).toInt();
            QString name = m.captured(1) + (alter < 0 ? QString(-alter, 'b') : QString(alter, '#'));
            t.strings.push_back({ ((LETTER_PC[letter] + alter) % 12 + 12) % 12, octave, name });
            }
      if (t.strings.empty())
            return false;
      *out = t;
      return true;
      }

StringInstrument withStrings(const StringInstrument& in, const std::vector<int>& lowToHigh)
      {
      StringInstrument out = in;
      if (lowToHigh.empty())
            return out;
      for (int p : lowToHigh)
            if (p < 12 || p > 115)
                  return out;           // not a string's pitch
      out.strings.assign(lowToHigh.rbegin(), lowToHigh.rend());
      out.stringNames.clear();
      if (out.strings != in.strings) {              // a tuning of its own: named low to high
            QStringList names;
            for (int p : lowToHigh)
                  names << stringName(p);
            out.tuning = names.join(" ");
            }
      return out;
      }

StringInstrument retune(const StringInstrument& in, const ScordaturaText& t)
      {
      if (t.reset || t.strings.size() != in.strings.size())
            return in;
      StringInstrument out = in;
      size_t n = in.strings.size();
      out.stringNames.clear();
      QStringList written;
      for (size_t k = 0; k < n; ++k) {
            const ScordaturaText::Str& s = t.strings[k];
            int old = in.strings[n - 1 - k];            // the text runs low to high, strings high to low
            int p;
            if (s.octave != -100) {
                  // the octave goes with the letter: B#3 is pitch 60, Cb4 is 59
                  p = (s.octave + 1) * 12 + s.pc;
                  if (s.name.startsWith('B') && s.name.contains('#'))
                        p -= 12;
                  if (s.name.startsWith('C') && s.name.contains('b'))
                        p += 12;
                  }
            else {
                  p = old - pc(old) + s.pc;               // the same pitch class nearest the old string
                  if (p - old > 6)
                        p -= 12;
                  else if (old - p > 6)
                        p += 12;
                  }
            out.strings[n - 1 - k] = p;
            written << s.name;
            }
      for (size_t i = 0; i < n; ++i)
            out.stringNames << t.strings[n - 1 - i].name;
      out.tuning = written.join(" ");
      return out;
      }

Spelling::Spelling(const std::vector<SpelledNote>& notes, int fifths)
      : _fifths(fifths), _set(true)
      {
      for (const SpelledNote& n : notes) {
            if (n.tpc < -1 || n.tpc > 33)
                  continue;
            _bySound[soundingPitch(n.pitch, n.cents)] = { n.tpc, n.pitch, n.cents };
            if (n.cents != 0.0)
                  continue;                 // a plain note of that pitch names derived pitches
            _byPitch[n.pitch] = n.tpc;
            _byPc[pc(n.pitch)] = n.tpc;
            }
      }

QString Spelling::name(double pitch) const
      {
      auto s = _bySound.find(pitch);
      if (s != _bySound.end())
            return tpcName(s->second.tpc, s->second.pitch) + centsSuffix(s->second.cents);
      if (pitch != std::floor(pitch)) {           // between the semitones with no note behind it
            double base = jsRound(pitch);
            return name(base) + centsSuffix((pitch - base) * 100);
            }
      int p = int(pitch);
      if (!_set)
            return plainName(p);
      auto bp = _byPitch.find(p);
      if (bp != _byPitch.end())
            return tpcName(bp->second, p);
      auto bc = _byPc.find(pc(p));
      if (bc != _byPc.end())
            return tpcName(bc->second, p);
      const char* const* table = _fifths > 0 ? SHARP_NAMES : _fifths < 0 ? FLAT_NAMES : NAMES;
      return QString(table[pc(p)]) + QString::number(floorDiv(p, 12) - 1);
      }

//---------------------------------------------------------
//   S1, S2
//---------------------------------------------------------

int openStringIndex(const StringInstrument& in, double pitch)
      {
      for (size_t i = 0; i < in.strings.size(); ++i)
            if (in.strings[i] == pitch)
                  return int(i);
      return -1;
      }

//---------------------------------------------------------
//   spanAt
//    Stopping points follow x(n) = L(1 − 2^(−n/12)), so one physical hand span covers more
//    semitones the higher the hand sits: Forsyth's limits "apply only to the lower positions"
//    (p. 357), Galamian's frame "gets smaller the higher up the fingerboard you play" (Simon
//    Fischer, The Strad, July 2011). The string length cancels. At pos 0 this is `limit`.
//---------------------------------------------------------

double spanAt(double limit, double pos)
      {
      if (!(pos > 0))
            return limit;
      double r = std::pow(2.0, -pos / 12) - 1 + std::pow(2.0, -limit / 12);
      if (r <= 0.03)
            return 24;                  // the hand is far up: unrestricted
      return -12 * std::log2(r) - pos;
      }

double reachAt(const StringInstrument& in, double pos)
      {
      return spanAt(in.crossMax, pos);
      }

struct Stretch {
      int stopped { 0 };
      double worst { 0 };
      double position { 0 };
      };

static Stretch stretchOf(const StringInstrument& in, const std::vector<double>& pitches, const std::vector<int>& assign)
      {
      Stretch s;
      double lowest = -1;
      for (size_t i = 0; i < pitches.size(); ++i) {
            double off = pitches[i] - in.strings[assign[i]];
            if (off > 0) {
                  s.stopped++;
                  if (lowest < 0 || off < lowest)
                        lowest = off;           // where the hand sits
                  }
            }
      for (size_t j = 0; j + 1 < pitches.size(); ++j) {
            double a = pitches[j] - in.strings[assign[j]];
            double b = pitches[j + 1] - in.strings[assign[j + 1]];
            if (a == 0 || b == 0)
                  continue;                     // open strings cost nothing
            s.worst = std::max(s.worst, std::fabs(a - b));
            }
      s.position = lowest < 0 ? 0 : lowest;
      return s;
      }

// Which string do this note and a neighbour both need?
static QString sharedString(const StringInstrument& in, const std::vector<double>& pitches, size_t i)
      {
      for (int s = int(in.strings.size()) - 1; s >= 0; --s)
            if (pitches[i] >= in.strings[s])
                  return stringName(in, s);
      return "?";
      }

// No window fits: strings go out greedily from the lowest note up, so the notes that have a
// home keep it, and the clash is named.
static StopResult orphanAssign(const StringInstrument& in, const std::vector<double>& pitches)
      {
      StopResult r;
      r.verdict = Verdict::IMPOSSIBLE;
      int nStrings = int(in.strings.size());
      std::vector<bool> used(nStrings, false);
      r.assign.assign(pitches.size(), -1);
      for (int i = int(pitches.size()) - 1; i >= 0; --i) {          // lowest first
            int got = -1;
            for (int s = nStrings - 1; s >= 0; --s)
                  if (!used[s] && pitches[i] >= in.strings[s]) {
                        got = s;
                        used[s] = true;
                        break;
                        }
            r.assign[i] = got;
            if (got < 0 && r.reason.isEmpty())
                  r.reason = pitches[i] < in.strings[nStrings - 1]
                             ? QString("below the lowest string")
                             : QString("same string (%1)").arg(sharedString(in, pitches, i));
            }
      if (r.reason.isEmpty())
            r.reason = "strings not adjacent";
      return r;
      }

//---------------------------------------------------------
//   analyseStop
//    One note per string, on adjacent strings, the highest note on the highest of them
//    (Adler p. 11): an assignment is a window of consecutive strings. The best window stops the
//    fewest notes; a window over the reach at its hand position is "out of reach" (a stretch).
//---------------------------------------------------------

StopResult analyseStop(const StringInstrument& in, const std::vector<double>& pitches)
      {
      size_t n = pitches.size(), nStrings = in.strings.size();
      if (n > nStrings) {
            StopResult r = orphanAssign(in, pitches);
            r.reason = "more notes than strings";
            return r;
            }

      std::vector<int> best, reach;
      Stretch bestCost, reachCost;
      double reachAllow = 0;
      for (size_t top = 0; top + n <= nStrings; ++top) {
            std::vector<int> assign;
            bool ok = true;
            for (size_t k = 0; k < n; ++k) {
                  if (pitches[k] < in.strings[top + k]) {
                        ok = false;
                        break;
                        }
                  assign.push_back(int(top + k));
                  }
            if (!ok)
                  continue;
            Stretch cost = stretchOf(in, pitches, assign);
            double allow = reachAt(in, cost.position);
            if (cost.worst <= allow) {
                  if (best.empty() || cost.stopped < bestCost.stopped) {
                        best = assign;
                        bestCost = cost;
                        }
                  }
            else if (reach.empty() || cost.worst - allow < reachCost.worst - reachAllow) {
                  reach = assign;                 // least over the limit
                  reachCost = cost;
                  reachAllow = allow;
                  }
            }

      StopResult r;
      if (!best.empty()) {
            r.assign = best;
            r.worst = bestCost.worst;
            if (in.requireOpenString && bestCost.stopped == int(n)) {
                  r.verdict = Verdict::OUT_OF_REACH;
                  r.reason = "no open string";
                  }
            return r;
            }
      if (!reach.empty()) {
            r.verdict = Verdict::OUT_OF_REACH;
            r.assign = reach;
            r.worst = reachCost.worst;
            r.reason = QString("stretch %1 st, max %2 here").arg(fmtReach(reachCost.worst), fmtReach(reachAllow));
            return r;
            }
      return orphanAssign(in, pitches);
      }

QString describe(const StringInstrument& in, const std::vector<double>& pitches, const StopResult& res, const Spelling& sp)
      {
      Q_UNUSED(in);
      QStringList parts;
      for (int i = int(pitches.size()) - 1; i >= 0; --i) {
            int s = i < int(res.assign.size()) ? res.assign[i] : -1;
            parts << sp.name(pitches[i]) + (s >= 0 ? QString(" (%1)").arg(roman(s)) : " (" + DASH + ")");
            }
      return parts.join(" + ");
      }

//---------------------------------------------------------
//   harmonics (Adler: partials and nodes p. 42–44; artificial p. 46, highest p. 47; the bass
//   natural only p. 46, 50, 86; touch 5th / M3 / m3 seldom used p. 57; cello touch-4th p. 80)
//---------------------------------------------------------

// touch interval above the open string (semitones) -> partial
static const std::map<int, int> NODES = { { 3, 6 }, { 4, 5 }, { 5, 4 }, { 7, 3 }, { 9, 5 }, { 12, 2 },
                                          { 16, 5 }, { 19, 3 }, { 24, 4 }, { 28, 5 } };
// what each partial sounds, semitones above the open string
static const std::map<int, int> SOUNDS = { { 2, 12 }, { 3, 19 }, { 4, 24 }, { 5, 28 }, { 6, 31 } };
// the 2/5 node (touch M6) is solo / chamber only; Adler lists 1/5, 3/5, 4/5 as orchestral
static const int SOLO_ONLY = 9;
static const std::map<int, const char*> TOUCH_NAME = { { 3, "m3" }, { 4, "M3" }, { 5, "P4" }, { 7, "5th" } };
static bool riskyTouch(int iv) { return iv == 3 || iv == 4 || iv == 7; }
// highest practical STOPPED note for an artificial harmonic (p. 47, Ex 2-76)
static int highestStop(const QString& name)
      {
      if (name == "Violin") return 84;
      if (name == "Viola") return 74;
      if (name == "Cello") return 65;
      return -1;
      }

struct HarmInfo {
      bool valid { false };
      int string { -1 };
      int partial { 0 };
      int sounds { 0 };
      };

struct HarmOne {
      HarmonicVerdict verdict { HarmonicVerdict::OK };
      HarmInfo info;
      QString reason;
      };

static int lookupMap(const std::map<int, int>& m, int key)
      {
      auto i = m.find(key);
      return i == m.end() ? 0 : i->second;
      }

// a diamond written at the node
static HarmOne naturalAtNode(const StringInstrument& in, int touched)
      {
      HarmInfo found, solo;
      for (size_t i = 0; i < in.strings.size(); ++i) {
            int off = touched - in.strings[i];
            int partial = lookupMap(NODES, off);
            if (!partial)
                  continue;
            HarmInfo cand { true, int(i), partial, in.strings[i] + lookupMap(SOUNDS, partial) };
            if (off == SOLO_ONLY) {
                  if (!solo.valid)
                        solo = cand;
                  }
            else if (!found.valid)
                  found = cand;
            }
      if (found.valid)
            return { HarmonicVerdict::OK, found, QString() };
      if (solo.valid)
            return { HarmonicVerdict::RISKY, solo, QString("2/5 node ") + DASH + " solo and chamber only" };
      return { HarmonicVerdict::IMPOSSIBLE, HarmInfo(), "no node on any string" };
      }

// a circle written over the sounding note
static HarmOne naturalAtSounding(const StringInstrument& in, int sounding)
      {
      for (size_t i = 0; i < in.strings.size(); ++i)
            for (const auto& p : SOUNDS)
                  if (in.strings[i] + p.second == sounding)
                        return { HarmonicVerdict::OK, HarmInfo { true, int(i), p.first, sounding }, QString() };
      return { HarmonicVerdict::IMPOSSIBLE, HarmInfo(), "no natural harmonic sounds this pitch" };
      }

// stop a note, touch an interval above it on the same string
static HarmOne artificial(const StringInstrument& in, int stopped, int touched, const Spelling& sp)
      {
      int iv = touched - stopped;
      int partial = lookupMap(NODES, iv);
      auto tn = TOUCH_NAME.find(iv);
      if (tn == TOUCH_NAME.end() || !partial)
            return { HarmonicVerdict::IMPOSSIBLE, HarmInfo(), QString("touch %1 st is not a harmonic").arg(iv) };
      HarmInfo info { true, -1, partial, stopped + lookupMap(SOUNDS, partial) };
      if (in.name == "Double bass")
            return { HarmonicVerdict::RISKY, info, "artificial harmonic on the bass " + DASH + " use natural only" };
      if (riskyTouch(iv))
            return { HarmonicVerdict::RISKY, info, QString("touch %1 ").arg(tn->second) + DASH + " seldom used, risky" };
      int top = highestStop(in.name);
      if (top >= 0 && stopped > top)
            return { HarmonicVerdict::RISKY, info, "stopped above " + sp.name(top) + " " + DASH + " insecure, may not speak" };
      return { HarmonicVerdict::OK, info, QString() };
      }

static QString describeNatural(const HarmOne& r, int pitch, const Spelling& sp)
      {
      if (!r.info.valid)
            return sp.name(pitch) + " (" + DASH + ")";
      return sp.name(pitch) + QString(" (%1) partial %2 ").arg(roman(r.info.string)).arg(r.info.partial)
             + ARROW + " sounds " + sp.name(r.info.sounds);
      }

static QString describeArtificial(const HarmOne& r, int stopped, int touched, const Spelling& sp)
      {
      QString s = "stop " + sp.name(stopped) + " + touch " + sp.name(touched);
      if (r.info.valid)
            s += " " + ARROW + " sounds " + sp.name(r.info.sounds);
      return s;
      }

HarmonicResult classifyHarmonic(const StringInstrument& in, const std::vector<HarmonicNote>& list, const Spelling& sp)
      {
      std::vector<HarmonicNote> diamonds, plain;
      int circles = 0;
      for (const HarmonicNote& n : list) {
            if (n.diamond)
                  diamonds.push_back(n);
            else
                  plain.push_back(n);
            if (n.circle)
                  circles++;
            }
      HarmonicResult res;
      if (diamonds.empty() && !circles)
            return res;                                     // an ordinary chord
      res.harmonic = true;

      // a stopped note and a diamond a touch interval above: an artificial harmonic
      if (diamonds.size() == 1 && plain.size() == 1) {
            int st = plain[0].pitch, to = diamonds[0].pitch;
            if (to > st) {
                  HarmOne a = artificial(in, st, to, sp);
                  res.verdict = a.verdict;
                  res.reason = a.reason;
                  res.detail = describeArtificial(a, st, to, sp);
                  return res;
                  }
            }

      // diamonds only: one natural harmonic per note at its node, the worst reported (the strings
      // they need are not checked against each other: naturalAtNode takes the first that fits)
      // circles only: each note at its sounding pitch
      if (plain.empty() || diamonds.empty()) {
            bool atNode = !diamonds.empty();
            const std::vector<HarmonicNote>& notes = atNode ? diamonds : list;
            QStringList parts;
            for (const HarmonicNote& n : notes) {
                  HarmOne r = atNode ? naturalAtNode(in, n.pitch) : naturalAtSounding(in, n.pitch);
                  parts << describeNatural(r, n.pitch, sp);
                  if (r.verdict == HarmonicVerdict::IMPOSSIBLE
                     || (r.verdict == HarmonicVerdict::RISKY && res.verdict == HarmonicVerdict::OK)) {
                        res.verdict = r.verdict;
                        res.reason = r.reason;
                        }
                  }
            res.detail = parts.join(" + ");
            return res;
            }

      // anything else (several diamonds with stopped notes, diamonds mixed with circles): not
      // read, and not called impossible either
      res.verdict = HarmonicVerdict::RISKY;
      res.reason = "unrecognised harmonic notation";
      res.detail = QString("%1 notes").arg(list.size());
      return res;
      }

std::vector<HarmonicOption> naturalOptions(const StringInstrument& in, int pitch, bool atNode)
      {
      std::vector<HarmonicOption> out;
      auto place = [](int partial, int off) { return int(jsRound(partial * (1 - std::pow(2.0, -off / 12.0)))); };
      for (size_t i = 0; i < in.strings.size(); ++i) {
            int open = in.strings[i];
            if (atNode) {
                  int p = lookupMap(NODES, pitch - open);
                  if (p) {
                        bool solo = pitch - open == SOLO_ONLY;
                        out.push_back({ int(i), p, open + lookupMap(SOUNDS, p), solo, { { pitch, place(p, pitch - open), p, solo } } });
                        }
                  continue;
                  }
            for (const auto& q : SOUNDS) {
                  if (open + q.second != pitch)
                        continue;
                  HarmonicOption o { int(i), q.first, pitch, true, {} };
                  for (const auto& n : NODES)
                        if (n.second == q.first) {
                              o.nodes.push_back({ open + n.first, place(q.first, n.first), q.first, n.first == SOLO_ONLY });
                              if (n.first != SOLO_ONLY)
                                    o.solo = false;
                              }
                  std::sort(o.nodes.begin(), o.nodes.end(), [](const HarmonicNode& a, const HarmonicNode& b) { return a.pitch < b.pitch; });
                  out.push_back(o);
                  }
            }
      return out;
      }

// one line per string: "A string (II): node A5, sounds A6"
static QString describeOptions(const StringInstrument& in, const std::vector<HarmonicOption>& opts, const Spelling& sp)
      {
      QStringList parts;
      for (const HarmonicOption& o : opts) {
            QStringList names;
            for (const HarmonicNode& n : o.nodes)
                  names << sp.name(n.pitch) + (n.pitch - in.strings[o.string] == SOLO_ONLY && o.nodes.size() > 1 ? " (solo only)" : "");
            parts << stringName(in, o.string) + QString(" string (%1): node ").arg(roman(o.string)) + names.join(" or ")
                     + ", sounds " + sp.name(o.sounds) + (o.solo ? " (solo only)" : "");
            }
      return parts.join("\n");
      }

QString inspectNatural(const StringInstrument& in, const std::vector<HarmonicNote>& list, const Spelling& sp, bool* atNodeOut)
      {
      size_t diamonds = 0, circles = 0;
      for (const HarmonicNote& n : list) {
            diamonds += n.diamond;
            circles += n.circle;
            }
      bool atNode;
      if (diamonds && diamonds == list.size())
            atNode = true;                  // node notation
      else if (!diamonds && circles)
            atNode = false;                 // sounding notation
      else
            return QString();
      QStringList parts;
      for (const HarmonicNote& n : list) {
            std::vector<HarmonicOption> opts = naturalOptions(in, n.pitch, atNode);
            if (opts.empty())
                  return QString();
            parts << (list.size() > 1 ? sp.name(n.pitch) + ":\n" : QString()) + describeOptions(in, opts, sp);
            }
      if (atNodeOut)
            *atNodeOut = atNode;
      return parts.join("\n");
      }

//---------------------------------------------------------
//   bowing
//---------------------------------------------------------

BowLimit bowLimit(const StringInstrument& in, const QString& tier)
      {
      static const std::map<QString, std::pair<double, double>> BOW_SECONDS = {
            { "pp", { 12, 15 } }, { "p", { 6, 12 } }, { "mf", { 3, 6 } }, { "f", { 2.5, 4.5 } }, { "ff", { 1.5, 3 } } };
      BowLimit b;
      auto t = BOW_SECONDS.find(tier);
      if (!in.valid() || t == BOW_SECONDS.end())
            return b;
      double f = (in.name == "Cello" || in.name == "Double bass") ? 0.6 : 1.0;
      b.valid = true;
      b.warn = t->second.first * f;
      b.red = t->second.second * f;
      return b;
      }

int dynamicVelocity(const QString& text, int elVelocity, int elChange)
      {
      static const QRegularExpression WS("\\s+");
      static const QRegularExpression LETTERS("^[pmfrszn]+$");
      static const QRegularExpression ACCENT("^(s|r)|^fz$|^[mrsz]$");
      static const std::map<QString, int> DYN_VELOCITY = {
            { "pppppp", 1 }, { "ppppp", 5 }, { "pppp", 10 }, { "ppp", 16 }, { "pp", 33 }, { "p", 49 }, { "mp", 64 },
            { "mf", 80 }, { "f", 96 }, { "ff", 112 }, { "fff", 126 }, { "ffff", 127 }, { "fffff", 127 }, { "ffffff", 127 },
            { "fp", 49 }, { "pf", 96 }, { "sfp", 65 }, { "sfpp", 33 } };
      QString t = QString(text).remove(WS).toLower();
      bool letters = LETTERS.match(t).hasMatch();
      if (letters && !t.endsWith('p') && ACCENT.match(t).hasMatch())
            return -1;
      if (elVelocity > 0)
            return elVelocity + elChange;
      if (letters) {
            auto i = DYN_VELOCITY.find(t);
            if (i != DYN_VELOCITY.end())
                  return i->second;
            }
      return -1;
      }

bool isLoud(double velocity)
      {
      return velocity >= 89;
      }

QString dynamicTier(double v)
      {
      if (v <= 40) return "pp";
      if (v <= 72) return "p";
      if (v <= 88) return "mf";
      if (v <= 104) return "f";
      return "ff";
      }

bool windLevelMatches(WindLevel level, double v)
      {
      switch (level) {
            case WindLevel::ANY:  return true;
            case WindLevel::SOFT: return dynamicTier(v) == "pp";
            case WindLevel::P_MP: return dynamicTier(v) == "p";
            case WindLevel::FFF:  return v > (112 + 126) / 2.0;
            }
      return false;
      }

int tierOrder(const QString& tier)
      {
      static const QStringList ORDER = { "pp", "p", "mf", "f", "ff" };
      return ORDER.indexOf(tier);
      }

TremoloResult fingeredTremolo(const StringInstrument& in, int a, int b)
      {
      int low = std::min(a, b), high = std::max(a, b), n = int(in.strings.size());
      TremoloResult res;
      res.interval = high - low;
      if (high == low) {
            res.fine = true;
            return res;
            }
      int acrossLow = -1, acrossHigh = -1;
      for (int s = 0; s < n; ++s) {
            int offLow = low - in.strings[s];
            if (offLow < 0)
                  continue;                   // the lower note is not on string s
            int offHigh = high - in.strings[s];
            if (offLow == 0 || offHigh - offLow <= spanAt(in.span0, offLow) + 1e-9) {
                  res.fine = true;            // both on string s
                  res.lower = res.upper = s;
                  return res;
                  }
            if (s > 0 && high >= in.strings[s - 1]) {     // the upper note on the next higher string
                  int offUp = high - in.strings[s - 1];
                  if (offUp == 0) {
                        res.fine = true;
                        res.lower = s;
                        res.upper = s - 1;
                        return res;
                        }
                  if (acrossLow < 0 && std::abs(offUp - offLow) <= spanAt(in.crossMax, std::min(offLow, offUp)) + 1e-9) {
                        acrossLow = s;
                        acrossHigh = s - 1;
                        }
                  }
            }
      if (acrossLow >= 0) {
            res.verdict = Verdict::OUT_OF_REACH;
            res.lower = acrossLow;
            res.upper = acrossHigh;
            }
      return res;
      }

QString intervalName(int semitones)
      {
      static const char* const NAMES[13] = { "unison", "minor 2nd", "major 2nd", "minor 3rd", "major 3rd", "perfect 4th",
                                             "tritone", "perfect 5th", "minor 6th", "major 6th", "minor 7th", "major 7th", "octave" };
      return semitones <= 12 ? QString(NAMES[semitones]) : QString("%1 semitones").arg(semitones);
      }

QString fmtSeconds(double x)
      {
      return QString::number(std::floor(x * 10 + 1e-6 + 0.5) / 10, 'g', 12) + " s";
      }

//---------------------------------------------------------
//   texts
//---------------------------------------------------------

int jeteState(const QString& text)
      {
      static const QRegularExpression ON(QString::fromUtf8("(^|[^a-z])(jet[ée]|gettato|ricochet)(?![a-z])"),
                                         QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression OFF(QString::fromUtf8("(^|[^a-z])(ord(\\.|in)|nat(\\.|ural)|norm(\\.|al)|modo\\s+ordinario|"
                                          "d[ée]tach|legato|spicc|sautill|martel|pizz|col\\s+legno|arco)"),
                                          QRegularExpression::CaseInsensitiveOption);
      if (ON.match(text).hasMatch())
            return 1;
      if (OFF.match(text).hasMatch())
            return 0;
      return -1;
      }

int pizzState(const QString& text)
      {
      static const QRegularExpression ON("(^|[^a-z])pizz", QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression OFF("(^|[^a-z])arco(?![a-z])", QRegularExpression::CaseInsensitiveOption);
      if (ON.match(text).hasMatch())
            return 1;
      if (OFF.match(text).hasMatch())
            return 0;
      return -1;
      }

//---------------------------------------------------------
//   texts
//---------------------------------------------------------

int divState(const QString& text)
      {
      static const QRegularExpression DIV_ON(QString::fromUtf8("\\bdiv(\\.|isi|is[ée]s)?\\b|geteilt|\\bdiv\\s*a\\s*\\d"),
                                             QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression DIV_OFF("\\bunis(\\.|on[oi]?)?\\b|\\bnon\\s*div", QRegularExpression::CaseInsensitiveOption);
      if (DIV_OFF.match(text).hasMatch())
            return 0;
      if (DIV_ON.match(text).hasMatch())
            return 1;
      return -1;
      }

QString plainText(const QString& xmlText)
      {
      static const QRegularExpression SYM("<sym>dynamic(\\w+)</sym>");
      static const QRegularExpression TAG("<[^>]*>");
      static const std::map<QString, QString> LETTER = { { "Piano", "p" }, { "Mezzo", "m" }, { "Forte", "f" },
            { "Rinforzando", "r" }, { "Sforzando", "s" }, { "Z", "z" }, { "Niente", "n" } };
      QString out;
      int at = 0;
      auto it = SYM.globalMatch(xmlText);
      while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            out += xmlText.mid(at, m.capturedStart() - at);
            auto l = LETTER.find(m.captured(1));
            if (l != LETTER.end())
                  out += l->second;
            at = m.capturedEnd();
            }
      out += xmlText.mid(at);
      return out.remove(TAG);
      }

//---------------------------------------------------------
//   H1 / H2 harp
//---------------------------------------------------------

// natural pitch classes of C D E F G A B
static const int LETTER_PC[7] = { 0, 2, 4, 5, 7, 9, 11 };

HarpNote harpNote(int tpc, int pitch)
      {
      HarpNote n;
      // tpc: F C G D A E B, then the same with sharps…; tpc 14 is C (tpcName's layout)
      static const int FIFTHS_LETTER[7] = { 3, 0, 4, 1, 5, 2, 6 };      // F C G D A E B
      n.letter = FIFTHS_LETTER[(((tpc + 1) % 7) + 7) % 7];
      n.alter = floorDiv(tpc + 1, 7) - 2;
      n.octave = floorDiv(pitch - n.alter, 12) - 1;
      return n;
      }

int harpStringIndex(const HarpNote& n)
      {
      return (n.octave - 1) * 7 + n.letter;
      }

bool harpHasString(const HarpNote& n)
      {
      // C1 (index 0) to G7 (6 × 7 + 4 = 46): 47 strings
      int i = harpStringIndex(n);
      return i >= 0 && i <= 46;
      }

bool harpHandTuned(const HarpNote& n)
      {
      return n.octave == 1 && (n.letter == 0 || n.letter == 1);
      }

bool harpLowestOctave(const HarpNote& n)
      {
      return n.octave == 1;
      }

const int HARP_PEDAL_ORDER[7] = { 1, 0, 6, 2, 3, 4, 5 };    // D C B | E F G A

bool harpLeftFoot(int letter)
      {
      return letter == 1 || letter == 0 || letter == 6;
      }

int keyAlter(int fifths, int letter)
      {
      static const int SHARPS[7] = { 3, 0, 4, 1, 5, 2, 6 };     // F C G D A E B
      static const int FLATS[7] = { 6, 2, 5, 1, 4, 0, 3 };      // B E A D G C F
      for (int i = 0; i < std::min(7, std::abs(fifths)); ++i)
            if ((fifths > 0 ? SHARPS[i] : FLATS[i]) == letter)
                  return fifths > 0 ? 1 : -1;
      return 0;
      }

QString letterName(int letter, int alter)
      {
      QString s = QString("CDEFGAB").at(letter);
      for (int i = 0; i < std::abs(alter); ++i)
            s += alter > 0 ? "#" : "b";
      return s;
      }

// the neighbouring letter that reaches the same pitch class with a single accidental at most
QString harpEnharmonic(int letter, int alter)
      {
      if (std::abs(alter) > 1)
            return QString();
      int target = pc(LETTER_PC[letter] + alter);
      for (int d : { -1, 1 }) {
            int l = (letter + d + 7) % 7;
            for (int a = -1; a <= 1; ++a)
                  if (pc(LETTER_PC[l] + a) == target)
                        return letterName(l, a);
            }
      return QString();
      }

//---------------------------------------------------------
//   P1 timpani
//---------------------------------------------------------

std::vector<TimpaniDrum> timpaniDrums(bool fifth)
      {
      // 32″ D2–A2, 29″ F2–C3, 26″ Bb2–F3, 23″ D3–A3, the 20″ piccolo F3–C4
      std::vector<TimpaniDrum> d = {
            { "32″", 38, 45 }, { "29″", 41, 48 }, { "26″", 46, 53 }, { "23″", 50, 57 } };
      if (fifth)
            d.push_back({ "20″", 53, 60 });
      return d;
      }

bool timpaniFifthDrum(const QString& text)
      {
      static const QRegularExpression FIFTH("\\b(5|five)\\s*(drums|timp)|piccol[oa]\\s+timp|timpan[oi]\\s+piccol",
                                            QRegularExpression::CaseInsensitiveOption);
      return FIFTH.match(text).hasMatch();
      }

TimpaniPlan planTimpani(const std::vector<TimpaniMoment>& moments, bool fifth)
      {
      const std::vector<TimpaniDrum> drums = timpaniDrums(fifth);
      const int nd = int(drums.size());
      std::vector<int> tuned(nd, -1);
      std::vector<double> lastEnd(nd, -1e9);
      TimpaniPlan plan;
      typedef TimpaniNotePlan::Problem P;
      for (size_t m = 0; m < moments.size(); ++m) {
            const TimpaniMoment& mo = moments[m];
            std::vector<TimpaniNotePlan> out;
            for (int p : mo.pitches) {
                  TimpaniNotePlan np;
                  np.pitch = p;
                  out.push_back(np);
                  }
            const double eps = 1e-9;
            if (int(mo.pitches.size()) > nd) {
                  for (auto& np : out)
                        np.problem = P::TOO_MANY;
                  }
            else {
                  std::vector<bool> used(nd, false);
                  auto holds = [&](int d, int p) { return drums[d].lo <= p && p <= drums[d].hi; };
                  auto rangeCount = [&](int p) { int c = 0; for (int d = 0; d < nd; ++d) c += holds(d, p); return c; };
                  // a drum already at the pitch
                  for (auto& np : out) {
                        if (!rangeCount(np.pitch)) {
                              np.problem = P::RANGE;
                              continue;
                              }
                        for (int d = 0; d < nd; ++d)
                              if (!used[d] && tuned[d] == np.pitch) {
                                    np.drum = d;
                                    used[d] = true;
                                    break;
                                    }
                        }
                  // the rest, the pitches with fewer drums first
                  std::vector<int> order;
                  for (int i = 0; i < int(out.size()); ++i)
                        if (out[i].drum < 0 && out[i].problem == P::NONE)
                              order.push_back(i);
                  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return rangeCount(out[a].pitch) < rangeCount(out[b].pitch); });
                  for (int i : order) {
                        TimpaniNotePlan& np = out[i];
                        int best = -1;
                        auto key = [&](int d) {
                              double secs = tuned[d] < 0 ? 1e9 : mo.start - lastEnd[d];
                              return std::make_tuple(secs + eps >= TIMPANI_RETUNE_SECONDS ? 0 : 1, tuned[d] < 0 ? 0 : 1,
                                                     std::fabs(np.pitch - drums[d].middle()), d);
                              };
                        for (int d = 0; d < nd; ++d) {
                              if (used[d] || !holds(d, np.pitch) || (tuned[d] >= 0 && lastEnd[d] > mo.start + eps))
                                    continue;
                              if (best < 0 || key(d) < key(best))
                                    best = d;
                              }
                        if (best < 0) {
                              np.problem = P::NO_DRUM;
                              continue;
                              }
                        np.drum = best;
                        used[best] = true;
                        if (tuned[best] >= 0) {
                              np.from = tuned[best];
                              np.seconds = mo.start - lastEnd[best];
                              if (np.seconds + eps < TIMPANI_RETUNE_SECONDS)
                                    np.problem = P::RETUNE;
                              }
                        }
                  for (size_t k = 0; k < out.size(); ++k)
                        if (out[k].drum >= 0) {
                              int d = out[k].drum;
                              if (tuned[d] != out[k].pitch)
                                    lastEnd[d] = -1e9;
                              tuned[d] = out[k].pitch;
                              lastEnd[d] = std::max(lastEnd[d], mo.ends[k]);
                              }
                  }
            plan.moments.push_back(out);
            plan.tuning.push_back(tuned);
            }
      return plan;
      }

//---------------------------------------------------------
//   instrument families
//---------------------------------------------------------

bool isKeyboard(const QString& id, const QString& name)
      {
      static const QRegularExpression ID("^keyboard\\.(piano|harpsichord|celesta|clavichord|organ|harmonium|virginal)");
      static const QRegularExpression NAME("piano|harpsichord|cembalo|clavecin|celest|clavichord|organ|orgel|harmonium|virginal",
                                           QRegularExpression::CaseInsensitiveOption);
      if (!id.isEmpty())
            return ID.match(id).hasMatch();
      return NAME.match(name).hasMatch();
      }

bool isHarp(const QString& id, const QString& name)
      {
      // the pedal harp: not the Celtic (lever) harp, which has no pedals
      static const QRegularExpression NAME("^\\s*(harp|harpe|arpa|harfe)", QRegularExpression::CaseInsensitiveOption);
      if (!id.isEmpty())
            return id == "pluck.harp";
      return NAME.match(name).hasMatch() && !name.contains("celtic", Qt::CaseInsensitive);
      }

bool isTimpani(const QString& id, const QString& name)
      {
      static const QRegularExpression NAME("timpan|\\btimp\\b|pauken", QRegularExpression::CaseInsensitiveOption);
      if (!id.isEmpty())
            return id == "drum.timpani";
      return NAME.match(name).hasMatch();
      }

//---------------------------------------------------------
//   B4-B6 brass (diagrams-spec-brass.md)
//---------------------------------------------------------

static const QString SHARP = QString(QChar(0x266F));

bool isTrombone(Brass b)
      {
      return b == Brass::TENOR_TROMBONE || b == Brass::BASS_TROMBONE || b == Brass::ALTO_TROMBONE
             || b == Brass::CONTRABASS_TROMBONE;
      }

QString brassName(Brass b)
      {
      switch (b) {
            case Brass::TENOR_TROMBONE:      return "tenor trombone";
            case Brass::BASS_TROMBONE:       return "bass trombone";
            case Brass::ALTO_TROMBONE:       return "alto trombone";
            case Brass::CONTRABASS_TROMBONE: return "contrabass trombone";
            case Brass::HORN:                return "double horn";
            case Brass::TRUMPET:             return "trumpet";
            case Brass::EUPHONIUM:           return "euphonium";
            case Brass::BARITONE:            return "baritone";
            case Brass::F_TUBA:              return "F tuba";
            case Brass::EB_TUBA:             return QString("E") + QChar(0x266D) + " tuba";
            case Brass::CC_TUBA:             return "CC tuba";
            case Brass::BBB_TUBA:            return QString("BB") + QChar(0x266D) + " tuba";
            default:                         return QString();
            }
      }

// a tuba's key from its name: a word E♭ / Eb / E-flat, F, C / CC, B♭ / BB♭ / Bb; none: BB♭
static Brass tubaKey(const QString& name)
      {
      static const QString L = "(?:^|[\\s(])(?:in\\s+)?";
      static const QString R = "(?=$|[\\s),])";
      static const QRegularExpression EB(L + "e(?:♭|b|-flat|\\s+flat)" + R, QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression F(L + "f" + R, QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression C(L + "cc?" + R, QRegularExpression::CaseInsensitiveOption);
      if (EB.match(name).hasMatch())
            return Brass::EB_TUBA;
      if (F.match(name).hasMatch())
            return Brass::F_TUBA;
      if (C.match(name).hasMatch())
            return Brass::CC_TUBA;
      return Brass::BBB_TUBA;
      }

static Brass tromboneByName(const QString& name, Brass dflt)
      {
      QString n = name.toLower();
      if (n.contains("soprano") || n.contains("sackbut"))
            return Brass::NONE;
      if (n.contains("contrabass") || n.contains("kontrabass"))
            return Brass::CONTRABASS_TROMBONE;
      if (n.contains("bass"))
            return Brass::BASS_TROMBONE;
      if (n.contains("alt"))
            return Brass::ALTO_TROMBONE;
      return dflt;
      }

Brass brassType(const QString& id, const QString& name)
      {
      QString n = name.toLower();
      if (!id.isEmpty()) {
            if (id == "brass.trombone" || id == "brass.trombone.tenor")
                  return tromboneByName(name, Brass::TENOR_TROMBONE);
            if (id == "brass.trombone.bass")
                  return Brass::BASS_TROMBONE;
            if (id == "brass.trombone.alto")
                  return Brass::ALTO_TROMBONE;
            if (id == "brass.trombone.contrabass")
                  return Brass::CONTRABASS_TROMBONE;
            if (id == "brass.french-horn")
                  return n.contains("alto") ? Brass::NONE : Brass::HORN;
            if ((id.startsWith("brass.trumpet") && !id.startsWith("brass.trumpet.baroque") && id != "brass.trumpet.slide")
                || id.startsWith("brass.cornet.") || id == "brass.cornet" || id == "brass.flugelhorn")
                  return Brass::TRUMPET;
            if (id == "brass.euphonium" || id == "brass.bugle.euphonium-bugle")
                  return Brass::EUPHONIUM;
            if (id == "brass.baritone-horn")
                  return Brass::BARITONE;
            if (id == "brass.bugle.contrabass")
                  return Brass::BBB_TUBA;
            if (id == "brass.tuba" || id == "brass.tuba.bass" || id == "brass.sousaphone" || id == "brass.helicon")
                  return tubaKey(name);
            return Brass::NONE;
            }
      static const QRegularExpression TROMBONE("trombon|posaune", QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression HORN("^\\s*(french\\s+|double\\s+)?horns?\\b|^\\s*corn[oi]\\b|^\\s*cors?\\b|^\\s*waldhorn",
                                           QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression TRUMPET("trumpet|tromba|trompet|cornet\\b|fl[uü]gelhorn", QRegularExpression::CaseInsensitiveOption);
      static const QRegularExpression TUBA("\\btuba\\b|sousaphone|helicon", QRegularExpression::CaseInsensitiveOption);
      if (TROMBONE.match(name).hasMatch())
            return tromboneByName(name, Brass::TENOR_TROMBONE);
      if (n.contains("euphonium"))
            return Brass::EUPHONIUM;
      if (n.contains("baritone horn"))
            return Brass::BARITONE;
      if (TUBA.match(name).hasMatch() && !n.contains("wagner"))
            return tubaKey(name);
      if (TRUMPET.match(name).hasMatch() && !n.contains("baroque") && !n.contains("natural") && !n.contains("slide"))
            return Brass::TRUMPET;
      if (HORN.match(name).hasMatch() && !n.contains("alto") && !n.contains("natural") && !n.contains("anglais")
          && !n.contains("ingl"))
            return Brass::HORN;
      return Brass::NONE;
      }

bool outOfTunePartial(int n)
      {
      return n == 7 || n == 11 || n == 13 || n == 14;
      }

int partialOf(int d)
      {
      for (int n = 1; n <= 32; ++n)
            if (std::fabs(12.0 * std::log2(double(n)) - d) <= 0.5)
                  return n;
      return 0;
      }

int raisedPartialOf(int d)
      {
      for (int n = 1; n <= 32; ++n) {
            double x = d - 12.0 * std::log2(double(n));
            if (x >= 0 && x < 1)
                  return n;
            }
      return 0;
      }

int brassAttachments(const QString& text)
      {
      static const QRegularExpression RE("\\b([FE])[\\s-]*(attachment|trigger|valve)\\b|\\bwith\\s+(?:an?\\s+)?([FE])\\b",
                                         QRegularExpression::CaseInsensitiveOption);
      int out = 0;
      auto it = RE.globalMatch(text);
      while (it.hasNext()) {
            auto m = it.next();
            QString l = (m.captured(1).isEmpty() ? m.captured(3) : m.captured(1)).toUpper();
            out |= l == "F" ? ATTACH_F : ATTACH_E;
            }
      return out;
      }

int brassValveText(const QString& text)
      {
      static const QRegularExpression RE("\\b([345]|three|four|five)[\\s-]*valves?\\b", QRegularExpression::CaseInsensitiveOption);
      auto m = RE.match(text);
      if (!m.hasMatch())
            return 0;
      QString w = m.captured(1).toLower();
      return w == "three" ? 3 : w == "four" ? 4 : w == "five" ? 5 : w.toInt();
      }

int brassValves(Brass b)
      {
      switch (b) {
            case Brass::EUPHONIUM:
            case Brass::F_TUBA:
            case Brass::EB_TUBA:
            case Brass::CC_TUBA:
            case Brass::BBB_TUBA:
                  return 4;
            case Brass::NONE:
                  return 0;
            default:
                  return isTrombone(b) ? 0 : 3;
            }
      }

bool brassSpecialists(Brass b)
      {
      switch (b) {
            case Brass::HORN:
            case Brass::F_TUBA:
            case Brass::EB_TUBA:
            case Brass::CC_TUBA:
            case Brass::BBB_TUBA:
                  return false;
            default:
                  return b != Brass::NONE;
            }
      }

int brassChartPitch(Brass b, int sounding, int transposeChromatic)
      {
      if (b == Brass::TRUMPET)
            return sounding - transposeChromatic;
      if (b == Brass::HORN)
            return sounding + 7;                  // written for horn in F, a perfect 5th above
      return sounding;
      }

static int valveSteps(int mask)
      {
      int s = 0;
      for (int v = 0; v < 4; ++v)
            if (mask & (1 << v))
                  s += VALVE_STEP[v];
      return s;
      }

static int popcount(int m)
      {
      int c = 0;
      for (; m; m >>= 1)
            c += m & 1;
      return c;
      }

QString fingeringName(int mask)
      {
      QStringList v;
      for (int i = 0; i < 5; ++i)
            if (mask & (1 << i))
                  v << QString::number(i + 1);
      QString s = v.isEmpty() ? QString("0") : v.join("+");
      return (mask & HORN_THUMB) ? "T" + s : s;
      }

// D3: the F attachment's six positions as I II III IV VI VII (Adler p. 344, TB12)
static const int F_SLOT[6] = { 0, 1, 2, 3, 5, 6 };

QString slidePositionName(int side, int position, bool raised)
      {
      QString sh = raised ? SHARP : QString();
      if (side == 1)
            return "F " + sh + roman(F_SLOT[std::max(0, std::min(position, 6) - 1)]);
      if (side == 2)
            return "E " + sh + QString::number(position);
      return sh + roman(position - 1);
      }

double BrassEntry::slot() const
      {
      double x = side == 1 ? F_SLOT[std::max(0, std::min(position, 6) - 1)] : position - 1;
      return raised ? x - 0.4 : x;           // drawn a little toward I (cosmetic)
      }

static void labelEntry(BrassEntry& e, int valves, bool specialists)
      {
      if (e.partial > 0)
            e.labels << QString("partial %1").arg(e.partial);
      if (e.pedal || e.partial == 1)
            e.labels << "pedal, difficult";
      if (outOfTunePartial(e.partial) && !e.raised)
            e.labels << "out of tune";
      if (specialists && e.partial >= BRASS_SPECIALIST)
            e.labels << "specialists";
      if ((e.mask & 8) && popcount(e.mask & 7) >= 2)
            e.labels << "may be sharp";                   // Blatter p. 459 (BR7)
      if (e.mask & 0x10)
            e.labels << "needs 5th valve";
      else if ((e.mask & 8) && valves < 4 && valves > 0)
            e.labels << "needs 4th valve";
      if (e.side == 1)
            e.labels << "F attachment";
      else if (e.side == 2)
            e.labels << "E attachment";
      if (e.derived)
            e.labels << "derived";
      }

static SlideChart slideChart(Brass b)
      {
      return b == Brass::ALTO_TROMBONE ? SlideChart::ALTO : b == Brass::CONTRABASS_TROMBONE ? SlideChart::CONTRABASS : SlideChart::TENOR;
      }

static int slidePartial(SlideChart c, int side, int position, bool raised, int pitch)
      {
      int d = pitch - (slideFundamental(c, side) - (position - 1));
      return raised ? raisedPartialOf(d) : partialOf(d);
      }

std::vector<BrassEntry> slideEntries(Brass b, int attachments, int pitch)
      {
      std::vector<BrassEntry> out;
      if (!isTrombone(b))
            return out;
      SlideChart c = slideChart(b);
      // the sides: no attachment always; F on the bass trombone (TB9) or when named; E when named.
      // The tenor shows F as an extra when not named (D4)
      bool side[3] = { true, b == Brass::BASS_TROMBONE || (attachments & ATTACH_F), bool(attachments & ATTACH_E) };
      bool extra[3] = { false, false, false };
      if (b == Brass::TENOR_TROMBONE && !side[1]) {
            side[1] = true;
            extra[1] = true;
            }
      const std::vector<SlideRow>& rows = slideRows(c);
      const SlideRow* row = nullptr;
      for (const SlideRow& r : rows)
            if (r.pitch == pitch)
                  row = &r;
      bool inRange = pitch >= rows.front().pitch && pitch <= rows.back().pitch;
      for (int s = 0; s < 3; ++s) {
            if (!side[s])
                  continue;
            std::vector<BrassEntry> plain, raised;
            if (row) {
                  for (int code : row->side[s]) {
                        BrassEntry e;
                        e.side = s;
                        e.position = code % 10;
                        e.raised = code > 10;
                        e.partial = slidePartial(c, s, e.position, e.raised, pitch);
                        e.pedal = row->pedal;
                        plain.push_back(e);
                        }
                  }
            else if (!inRange) {
                  for (int n = 1; n <= SLIDE_SIDE_POSITIONS[s]; ++n) {
                        int d = pitch - (slideFundamental(c, s) - (n - 1));
                        int p = partialOf(d);
                        if (p >= BRASS_PARTIAL_LO && p <= BRASS_PARTIAL_HI && !outOfTunePartial(p)) {
                              BrassEntry e;
                              e.side = s;
                              e.position = n;
                              e.partial = p;
                              e.derived = true;
                              plain.push_back(e);
                              }
                        // the out-of-tune partials in a raised position, II to VII (Blatter p. 460, TB20)
                        int rp = raisedPartialOf(d);
                        if (n > 1 && rp >= BRASS_PARTIAL_LO && rp <= BRASS_PARTIAL_HI && outOfTunePartial(rp)) {
                              BrassEntry e;
                              e.side = s;
                              e.position = n;
                              e.raised = true;
                              e.partial = rp;
                              e.derived = true;
                              raised.push_back(e);
                              }
                        }
                  }
            plain.insert(plain.end(), raised.begin(), raised.end());
            for (BrassEntry& e : plain) {
                  e.extra = extra[s];
                  e.name = slidePositionName(s, e.position, e.raised);
                  labelEntry(e, 0, true);
                  out.push_back(e);
                  }
            }
      return out;
      }

static ValveColumn valveColumn(Brass b, int* fund)
      {
      ValveColumn c = ValveColumn::TREBLE;
      switch (b) {
            case Brass::EUPHONIUM:
            case Brass::BARITONE: c = ValveColumn::EUPHONIUM; break;
            case Brass::F_TUBA:   c = ValveColumn::F_TUBA; break;
            case Brass::CC_TUBA:  c = ValveColumn::CC_TUBA; break;
            case Brass::EB_TUBA:
            case Brass::BBB_TUBA: c = ValveColumn::BBB_TUBA; break;
            default: break;
            }
      // E♭ tuba: no chart column; its open fundamental is the E♭ named by the instrument, E♭1, between the
      // CC and F tubas: the BB♭ fundamental (B♭0) plus 5 semitones
      *fund = b == Brass::EB_TUBA ? valveFundamental(ValveColumn::BBB_TUBA) + 5 : valveFundamental(c);
      return c;
      }

// derived fingerings at an interval d above the open fundamental: the instrument's valves (up to 4),
// partials 2-16, not the 7th (Blatter p. 459, BR8); ordered as the chart's row an octave away when
// there is one (the neighbouring notes' pattern), then fewest valves, the 3-valve set first, the
// smallest step
static std::vector<BrassEntry> deriveValves(int rel, int valves, int thumb, int fundShift, const std::vector<int>& pattern)
      {
      std::vector<BrassEntry> out;
      int n = std::min(valves, 4);
      for (int m = 0; m < (1 << n); ++m) {
            int p = partialOf(rel + fundShift + valveSteps(m));
            if (p < BRASS_PARTIAL_LO || p > BRASS_PARTIAL_HI || p == 7)
                  continue;
            BrassEntry e;
            e.mask = m | thumb;
            e.partial = p;
            e.derived = true;
            out.push_back(e);
            }
      auto rank = [&](const BrassEntry& e) {
            auto it = std::find(pattern.begin(), pattern.end(), e.mask);
            int pi = it == pattern.end() ? 1000 : int(it - pattern.begin());
            int m = e.mask & 0x1f;
            return std::make_tuple(pi, popcount(m), (m & 8) ? 1 : 0, valveSteps(m));
            };
      std::stable_sort(out.begin(), out.end(), [&](const BrassEntry& a, const BrassEntry& b) { return rank(a) < rank(b); });
      return out;
      }

std::vector<BrassEntry> valveEntries(Brass b, int valves, int pitch)
      {
      std::vector<BrassEntry> out;
      if (b == Brass::NONE || isTrombone(b))
            return out;
      if (valves <= 0)
            valves = brassValves(b);
      auto finish = [&](BrassEntry& e) {
            int hi = 0;
            for (int v = 0; v < 5; ++v)
                  if (e.mask & (1 << v))
                        hi = v + 1;
            e.playable = hi <= valves;
            e.name = fingeringName(e.mask);
            labelEntry(e, valves, brassSpecialists(b));
            out.push_back(e);
            };
      if (b == Brass::HORN) {
            const std::vector<HornRow>& rows = hornRows();
            int lo = rows.front().written, hi = rows.back().written;
            for (int side = 0; side < 2; ++side) {
                  int fund = side ? HORN_FUNDAMENTAL_BB : HORN_FUNDAMENTAL_F;
                  int thumb = side ? HORN_THUMB : 0;
                  if (pitch >= lo && pitch <= hi) {
                        for (int m : side ? rows[pitch - lo].bb : rows[pitch - lo].f) {
                              BrassEntry e;
                              e.mask = m;
                              e.partial = partialOf(pitch - (fund - valveSteps(m & 0xf)));
                              finish(e);
                              }
                        }
                  else {
                        int ref = pitch < lo ? pitch + 12 : pitch - 12;
                        std::vector<int> pattern;
                        if (ref >= lo && ref <= hi)
                              pattern = side ? rows[ref - lo].bb : rows[ref - lo].f;
                        for (BrassEntry e : deriveValves(pitch - fund, 3, thumb, 0, pattern))
                              finish(e);
                        }
                  }
            return out;
            }
      int fund = 0;
      valveColumn(b, &fund);
      const std::vector<ValveRow>& rows = valveRows();
      int rel = pitch - fund;
      int lo = rows.front().rel, hi = rows.back().rel;
      if (b != Brass::EB_TUBA && rel >= lo && rel <= hi) {
            const ValveRow& r = rows[rel - lo];
            for (const ValveCell& c : r.fingerings) {
                  BrassEntry e;
                  e.mask = c.mask;
                  e.chartRow = c.row;
                  e.partial = (c.mask & 0x10) ? 0 : partialOf(rel + valveSteps(c.mask));
                  e.pedal = r.pedal;
                  finish(e);
                  }
            return out;
            }
      int ref = rel >= lo && rel <= hi ? rel : rel < lo ? rel + 12 : rel - 12;
      std::vector<int> pattern;
      if (ref >= lo && ref <= hi)
            for (const ValveCell& c : rows[ref - lo].fingerings)
                  pattern.push_back(c.mask);
      for (BrassEntry e : deriveValves(rel, valves, 0, 0, pattern))
            finish(e);
      return out;
      }

std::vector<BrassEntry> brassEntries(Brass b, int attachments, int valves, int pitch)
      {
      return isTrombone(b) ? slideEntries(b, attachments, pitch) : valveEntries(b, valves, pitch);
      }

int slideGlissando(Brass b, int attachments, int from, int to, QString* reason)
      {
      int w = std::abs(to - from);
      if (w > GLISSANDO_MAX) {
            *reason = QString("a slide glissando of %1 semitones, wider than a tritone").arg(w);
            return 2;
            }
      std::vector<BrassEntry> a = slideEntries(b, attachments, from), z = slideEntries(b, attachments, to);
      bool pedalOnly = false;
      for (const BrassEntry& x : a)
            for (const BrassEntry& y : z)
                  if (!x.extra && !y.extra && x.side == y.side && x.partial > 0 && x.partial == y.partial) {
                        if (x.partial > 1) {
                              reason->clear();
                              return 0;
                              }
                        pedalOnly = true;
                        }
      if (pedalOnly) {
            *reason = "a slide glissando on the pedal partial";
            return 1;
            }
      *reason = "no partial holds both notes of the slide glissando";
      return 2;
      }

}     // namespace Playability
}     // namespace Ms
