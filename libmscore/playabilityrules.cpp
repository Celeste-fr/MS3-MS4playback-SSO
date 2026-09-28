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

#include <cmath>
#include <QRegularExpression>
#include <QStringList>

namespace Ms {
namespace Playability {

const char* const ROMAN[5] = { "I", "II", "III", "IV", "V" };

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
                  return stringName(in.strings[s]);
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
            parts << sp.name(pitches[i]) + (s >= 0 ? QString(" (%1)").arg(ROMAN[s]) : " (" + DASH + ")");
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
      return sp.name(pitch) + QString(" (%1) partial %2 ").arg(ROMAN[r.info.string]).arg(r.info.partial)
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

}     // namespace Playability
}     // namespace Ms
