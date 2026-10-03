//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "tuning.h"
#include "accidental.h"
#include "chord.h"
#include "key.h"
#include "measure.h"
#include "note.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "sym.h"
#include "symbol.h"
#include "tuningtables.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ms {

const char* Temperament::metaTag = "temperament";

static int mod12(int n) { return ((n % 12) + 12) % 12; }
static int mod7(int n)  { return ((n % 7) + 7) % 7; }

// pitch class of a position on the line of fifths (0 C, 1 G, -1 F …)
static int pitchOfFifths(int f) { return mod12(f * 7); }

// the spelling a circle-of-fifths position (0..11) has in the plugin's table: C G D A E B F# C# G#,
// then Eb Bb F
static int naturalFifths(int pos) { return pos <= 8 ? pos : pos - 12; }

static const TuningTables::Preset* findPreset(const QString& name)
      {
      for (const TuningTables::Preset& p : TuningTables::temperaments)
            if (name == p.name)
                  return &p;
      return nullptr;
      }

//---------------------------------------------------------
//   Temperament
//---------------------------------------------------------

bool Temperament::isEqual() const
      {
      for (double o : offsets)
            if (qAbs(o) > 0.0005)
                  return false;
      return true;
      }

//---------------------------------------------------------
//   isChain
//    whether the 12 values are a chain of equal fifths from the root (Pythagorean, meantones,
//    equal, a tweak alone): each fifth up the chain adds the same step, to 0.15 cents (the plugin
//    rounds its values to 0.1). Such a tuning goes on past 12 notes, which is what tells C#
//    from Db.
//---------------------------------------------------------

bool Temperament::isChain(double* step) const
      {
      const int fr = naturalFifths(mod12(root));
      double v[12];
      for (int k = 0; k < 12; ++k)
            v[k] = offsets[pitchOfFifths(fr + k)];
      const double d = (v[11] - v[0]) / 11.0;
      for (int k = 0; k < 11; ++k)
            if (qAbs(v[k + 1] - v[k] - d) > 0.15)
                  return false;
      if (step)
            *step = d;
      return true;
      }

//---------------------------------------------------------
//   johnstonCents
//    Just intonation by spelling, as Ben Johnston's notation defines it, relative to the root
//    (rootTpc as 1/1), in cents from equal temperament. A spelling k fifths from the root is
//    k pure fifths (3/2), less a syntonic comma (81/80) for the 6th, 3rd and 7th of the root's
//    major scale (5/3, 5/4, 15/8), and each sharp is 25/24 (a Pythagorean sharp less two
//    commas), each flat 24/25.
//---------------------------------------------------------

static const double FIFTH = 1200.0 * std::log2(3.0 / 2.0) - 700.0;      // a pure fifth: +1.955
static const double COMMA = 1200.0 * std::log2(81.0 / 80.0);             // the syntonic comma: 21.506

double Temperament::johnstonCents(int tpc, int rootTpc)
      {
      const int k = tpc - rootTpc;                           // fifths from the root
      const int a = (k + 1 >= 0) ? (k + 1) / 7 : -((6 - (k + 1)) / 7);      // sharps (flats < 0) over the major scale
      const int letter = k - 7 * a;                          // -1 (4th) … 5 (7th)
      const int commas = (letter >= 3 ? 1 : 0) + 2 * a;
      return k * FIFTH - commas * COMMA;
      }

// Helmholtz-Ellis' unmarked notes: pure fifths from the root
double Temperament::pythagoreanCents(int tpc, int rootTpc)
      {
      return (tpc - rootTpc) * FIFTH;
      }

bool Temperament::isJustBySpelling() const
      {
      return just != Just::KEYS && name == "just";
      }

//---------------------------------------------------------
//   cents
//    the temperament's offset for a note of this spelling (tpc, concert) and pitch
//---------------------------------------------------------

double Temperament::cents(int tpc, int pitch) const
      {
      if (isJustBySpelling() && tpcIsValid(tpc)) {
            const int rootTpc = Tpc::TPC_C + naturalFifths(mod12(root));
            const int pureTpc = Tpc::TPC_C + naturalFifths(mod12(pure));
            if (just == Just::HEJI)
                  return pythagoreanCents(tpc, rootTpc) - pythagoreanCents(pureTpc, rootTpc) + tweak;
            return johnstonCents(tpc, rootTpc) - johnstonCents(pureTpc, rootTpc) + tweak;
            }
      double step;
      if (spelled && tpcIsValid(tpc) && isChain(&step)) {
            const int fr = naturalFifths(mod12(root));
            const int i = (tpc - Tpc::TPC_C) - fr;          // fifths up the chain from its first note
            const int q = i >= 0 ? i / 12 : -((11 - i) / 12);
            const int r = i - 12 * q;
            return offsets[pitchOfFifths(fr + r)] + 12.0 * step * q;
            }
      return offsets[mod12(pitch)];
      }

//---------------------------------------------------------
//   preset
//    the final values as the Tuning plugin computes them (lookUp, then toFixed(1))
//---------------------------------------------------------

Temperament Temperament::preset(const QString& name, int root, int pure, double tweak)
      {
      Temperament t;
      const TuningTables::Preset* p = findPreset(name);
      if (!p)
            return t;
      t.name = name;
      t.root = mod12(root);
      t.pure = mod12(pure);
      t.tweak = tweak;
      const double pureAdjustment = p->offsets[mod12(t.pure - t.root)];
      for (int pitch = 0; pitch < 12; ++pitch) {
            const double v = p->offsets[mod12(pitch * 7 - t.root)] - pureAdjustment + tweak;
            t.offsets[pitch] = std::round(v * 10.0) / 10.0 + 0.0;    // (+ 0.0: no -0)
            }
      return t;
      }

Temperament Temperament::preset(const QString& name)
      {
      return preset(name, presetRoot(name), presetPure(name), 0.0);
      }

QStringList Temperament::presetNames()
      {
      QStringList l;
      for (const TuningTables::Preset& p : TuningTables::temperaments)
            l.append(p.name);
      return l;
      }

int Temperament::presetRoot(const QString& name)
      {
      const TuningTables::Preset* p = findPreset(name);
      return p ? p->root : 0;
      }

int Temperament::presetPure(const QString& name)
      {
      const TuningTables::Preset* p = findPreset(name);
      return p ? p->pure : 0;
      }

//---------------------------------------------------------
//   fromJson / toJson
//    the Tuning plugin's save file: {"offsets": [12, C to B], "temperament", "root", "pure",
//    "tweak"}, plus "spelled": false when enharmonic spellings are to sound alike
//---------------------------------------------------------

Temperament Temperament::fromJson(const QString& json, bool* ok)
      {
      Temperament t;
      QJsonParseError error;
      const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &error);
      if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            if (ok)
                  *ok = false;
            return t;
            }
      const QJsonObject o = doc.object();
      t.name  = o.value("temperament").toString("custom");
      t.root  = mod12(o.value("root").toInt(0));
      t.pure  = mod12(o.value("pure").toInt(0));
      t.tweak = o.value("tweak").toDouble(0.0);
      t.spelled = o.value("spelled").toBool(true);
      const QString just = o.value("just").toString();
      t.just = just == "spelled" ? Just::JOHNSTON : just == "heji" ? Just::HEJI : Just::KEYS;
      const QString quarter = o.value("quarterTones").toString();
      t.quarter = quarter == "half" ? Quarter::HALF : quarter == "33/32" ? Quarter::JUST : Quarter::FIXED;
      const QString persian = o.value("persian").toString();
      t.persian = persian == "practice" || persian == "musescore36" ? Persian::MS36 : Persian::VAZIRI;   // ("practice": see Persian)
      const QJsonArray a = o.value("offsets").toArray();
      if (a.size() == 12) {
            for (int i = 0; i < 12; ++i)
                  t.offsets[i] = a.at(i).toDouble(0.0);
            }
      else if (findPreset(t.name)) {
            const Temperament p = preset(t.name, t.root, t.pure, t.tweak);
            std::copy(std::begin(p.offsets), std::end(p.offsets), std::begin(t.offsets));
            }
      if (ok)
            *ok = true;
      return t;
      }

QString Temperament::toJson() const
      {
      QJsonObject o;
      QJsonArray a;
      for (double v : offsets)
            a.append(v);
      o["offsets"] = a;
      o["temperament"] = name;
      o["root"] = root;
      o["pure"] = pure;
      o["tweak"] = tweak;
      if (!spelled)
            o["spelled"] = false;
      if (just != Just::KEYS)
            o["just"] = just == Just::HEJI ? "heji" : "spelled";
      if (quarter != Quarter::FIXED)
            o["quarterTones"] = quarter == Quarter::HALF ? "half" : "33/32";
      if (persian != Persian::VAZIRI)
            o["persian"] = "musescore36";
      return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
      }

bool Temperament::operator==(const Temperament& t) const
      {
      if (name != t.name || root != t.root || pure != t.pure || qAbs(tweak - t.tweak) > 1e-9 || spelled != t.spelled
          || just != t.just || quarter != t.quarter || persian != t.persian)
            return false;
      for (int i = 0; i < 12; ++i)
            if (qAbs(offsets[i] - t.offsets[i]) > 1e-9)
                  return false;
      return true;
      }

Temperament Temperament::ofScore(const Score* score)
      {
      const QString json = score->masterScore()->metaTag(metaTag);
      if (json.trimmed().isEmpty())
            return Temperament();
      return fromJson(json);
      }

//---------------------------------------------------------
//   ScoreTuning
//---------------------------------------------------------

ScoreTuning::ScoreTuning(const Score* score)
   : _score(score), _temperament(Temperament::ofScore(score))
      {
      _equal = _temperament.isEqual();
      }

//---------------------------------------------------------
//   hejiAccidental
//    the Helmholtz-Ellis accidentals with syntonic-comma arrows (one to three, up or down, on a
//    double flat … double sharp): MuseScore 3.6 gives them no pitch (they play as naturals and
//    have no value in its table). Their sharps and arrows, as HEJI defines them
//---------------------------------------------------------

bool ScoreTuning::hejiAccidental(AccidentalType type, int* sharps, int* arrows)
      {
      struct H { AccidentalType type; int sharps, arrows; };
      static const H heji[] = {
            { AccidentalType::DOUBLE_FLAT_ONE_ARROW_DOWN, -2, -1 }, { AccidentalType::FLAT_ONE_ARROW_DOWN, -1, -1 },
            { AccidentalType::NATURAL_ONE_ARROW_DOWN, 0, -1 }, { AccidentalType::SHARP_ONE_ARROW_DOWN, 1, -1 },
            { AccidentalType::DOUBLE_SHARP_ONE_ARROW_DOWN, 2, -1 },
            { AccidentalType::DOUBLE_FLAT_ONE_ARROW_UP, -2, 1 }, { AccidentalType::FLAT_ONE_ARROW_UP, -1, 1 },
            { AccidentalType::NATURAL_ONE_ARROW_UP, 0, 1 }, { AccidentalType::SHARP_ONE_ARROW_UP, 1, 1 },
            { AccidentalType::DOUBLE_SHARP_ONE_ARROW_UP, 2, 1 },
            { AccidentalType::DOUBLE_FLAT_TWO_ARROWS_DOWN, -2, -2 }, { AccidentalType::FLAT_TWO_ARROWS_DOWN, -1, -2 },
            { AccidentalType::NATURAL_TWO_ARROWS_DOWN, 0, -2 }, { AccidentalType::SHARP_TWO_ARROWS_DOWN, 1, -2 },
            { AccidentalType::DOUBLE_SHARP_TWO_ARROWS_DOWN, 2, -2 },
            { AccidentalType::DOUBLE_FLAT_TWO_ARROWS_UP, -2, 2 }, { AccidentalType::FLAT_TWO_ARROWS_UP, -1, 2 },
            { AccidentalType::NATURAL_TWO_ARROWS_UP, 0, 2 }, { AccidentalType::SHARP_TWO_ARROWS_UP, 1, 2 },
            { AccidentalType::DOUBLE_SHARP_TWO_ARROWS_UP, 2, 2 },
            { AccidentalType::DOUBLE_FLAT_THREE_ARROWS_DOWN, -2, -3 }, { AccidentalType::FLAT_THREE_ARROWS_DOWN, -1, -3 },
            { AccidentalType::NATURAL_THREE_ARROWS_DOWN, 0, -3 }, { AccidentalType::SHARP_THREE_ARROWS_DOWN, 1, -3 },
            { AccidentalType::DOUBLE_SHARP_THREE_ARROWS_DOWN, 2, -3 },
            { AccidentalType::DOUBLE_FLAT_THREE_ARROWS_UP, -2, 3 }, { AccidentalType::FLAT_THREE_ARROWS_UP, -1, 3 },
            { AccidentalType::NATURAL_THREE_ARROWS_UP, 0, 3 }, { AccidentalType::SHARP_THREE_ARROWS_UP, 1, 3 },
            { AccidentalType::DOUBLE_SHARP_THREE_ARROWS_UP, 2, 3 },
            };
      for (const H& h : heji) {
            if (h.type == type) {
                  *sharps = h.sharps;
                  *arrows = h.arrows;
                  return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   accidental families, each by its own definition
//---------------------------------------------------------

static const double HOLDRIAN = 1200.0 / 53.0;       // Turkish koma: 22.642 cents

// quarter-tone accidentals (Stein-Zimmermann, Gould's arrows): whole sharps and a quarter up or down
static bool quarterTone(SymId s, int* sharps, int* quarter)
      {
      struct Q { SymId sym; int sharps, quarter; };
      static const Q qs[] = {
            { SymId::accidentalQuarterToneFlatStein, 0, -1 },            { SymId::accidentalThreeQuarterTonesFlatZimmermann, -1, -1 },
            { SymId::accidentalQuarterToneSharpStein, 0, 1 },            { SymId::accidentalThreeQuarterTonesSharpStein, 1, 1 },
            { SymId::accidentalQuarterToneFlatArrowUp, -1, 1 },          { SymId::accidentalThreeQuarterTonesFlatArrowDown, -1, -1 },
            { SymId::accidentalQuarterToneSharpNaturalArrowUp, 0, 1 },   { SymId::accidentalQuarterToneFlatNaturalArrowDown, 0, -1 },
            { SymId::accidentalThreeQuarterTonesSharpArrowUp, 1, 1 },    { SymId::accidentalQuarterToneSharpArrowDown, 1, -1 },
            { SymId::accidentalFiveQuarterTonesSharpArrowUp, 2, 1 },     { SymId::accidentalThreeQuarterTonesSharpArrowDown, 2, -1 },
            { SymId::accidentalThreeQuarterTonesFlatArrowUp, -2, 1 },    { SymId::accidentalFiveQuarterTonesFlatArrowDown, -2, -1 },
            { SymId::accidentalArrowUp, 0, 1 },                          { SymId::accidentalArrowDown, 0, -1 },
            };
      for (const Q& q : qs) {
            if (q.sym == s) {
                  *sharps = q.sharps;
                  *quarter = q.quarter;
                  return true;
                  }
            }
      return false;
      }

// Helmholtz-Ellis's tempered accidentals (the HEJI 2020 legend, "Tempered notes": "indicate the
// respective 12-edo semitone … tempered quartertones may be written similarly"): cents from the
// natural, played as they are whatever the score's tuning (doNote leaves its temperament out)
static bool temperedCents(SymId s, double* cents)
      {
      switch (s) {
            case SymId::accidentalDoubleFlatEqualTempered:   *cents = -200.0; return true;
            case SymId::accidentalFlatEqualTempered:         *cents = -100.0; return true;
            case SymId::accidentalNaturalEqualTempered:      *cents =    0.0; return true;
            case SymId::accidentalSharpEqualTempered:        *cents =  100.0; return true;
            case SymId::accidentalDoubleSharpEqualTempered:  *cents =  200.0; return true;
            case SymId::accidentalQuarterFlatEqualTempered:  *cents =  -50.0; return true;
            case SymId::accidentalQuarterSharpEqualTempered: *cents =   50.0; return true;
            default:                                         return false;
            }
      }

// Helmholtz-Ellis's enharmonic signs (Accidental::stackPrime): the tilde alters by one schisma
// (32805/32768) toward the note's enharmonic respelling (HEJI 2020 legend: "~♯↓ = ♭", "~♭↑ = ♯"),
// so down with the accidental's comma arrows down, up with them up, nothing without arrows; "="
// and "≈" only mark a respelling (≈ is not in the legend)
static bool isEnharmonicSign(SymId s)
      {
      return s == SymId::accidentalEnharmonicTilde || s == SymId::accidentalEnharmonicEquals
             || s == SymId::accidentalEnharmonicAlmostEqualTo;
      }

// families with one definition: cents from the natural note
static bool conventionCents(SymId s, double* cents)
      {
      if (temperedCents(s, cents))
            return true;
      if (isEnharmonicSign(s)) {                                // alone on a note: no alteration
            *cents = 0.0;
            return true;
            }
      auto c = [](double r) { return 1200.0 * std::log2(r); };
      const double apotome = c(2187.0 / 2048.0);
      int sharps, quarter;
      if (quarterTone(s, &sharps, &quarter)) {                  // fixed: 24-EDO
            *cents = 100.0 * sharps + 50.0 * quarter;
            return true;
            }
      switch (s) {
            // Arel-Ezgi-Uzdilek: bakiye 4, küçük mücenneb 5, büyük mücenneb 8 Holdrian commas
            case SymId::accidentalBuyukMucennebFlat:   *cents = -8 * HOLDRIAN; return true;
            case SymId::accidentalBakiyeFlat:          *cents = -4 * HOLDRIAN; return true;
            case SymId::accidentalKucukMucennebSharp:  *cents =  5 * HOLDRIAN; return true;
            case SymId::accidentalBuyukMucennebSharp:  *cents =  8 * HOLDRIAN; return true;
            // Turkish folk music: n Holdrian commas
            case SymId::accidental1CommaFlat:          *cents = -1 * HOLDRIAN; return true;
            case SymId::accidental1CommaSharp:         *cents =  1 * HOLDRIAN; return true;
            case SymId::accidental2CommaFlat:          *cents = -2 * HOLDRIAN; return true;
            case SymId::accidental2CommaSharp:         *cents =  2 * HOLDRIAN; return true;
            case SymId::accidental3CommaFlat:          *cents = -3 * HOLDRIAN; return true;
            case SymId::accidental3CommaSharp:         *cents =  3 * HOLDRIAN; return true;
            case SymId::accidental4CommaFlat:          *cents = -4 * HOLDRIAN; return true;
            case SymId::accidental5CommaSharp:         *cents =  5 * HOLDRIAN; return true;
            // Sagittal: exact ratios over the Pythagorean note (the apotome is its sharp)
            case SymId::accSagittal5v7KleismaDown:     *cents = -c(5120.0 / 5103.0); return true;
            case SymId::accSagittal5v7KleismaUp:       *cents =  c(5120.0 / 5103.0); return true;
            case SymId::accSagittal5CommaDown:         *cents = -c(81.0 / 80.0); return true;
            case SymId::accSagittal5CommaUp:           *cents =  c(81.0 / 80.0); return true;
            case SymId::accSagittal7CommaDown:         *cents = -c(64.0 / 63.0); return true;
            case SymId::accSagittal7CommaUp:           *cents =  c(64.0 / 63.0); return true;
            case SymId::accSagittal25SmallDiesisDown:  *cents = -c(6561.0 / 6400.0); return true;
            case SymId::accSagittal25SmallDiesisUp:    *cents =  c(6561.0 / 6400.0); return true;
            case SymId::accSagittal35MediumDiesisDown: *cents = -c(36.0 / 35.0); return true;
            case SymId::accSagittal35MediumDiesisUp:   *cents =  c(36.0 / 35.0); return true;
            case SymId::accSagittal11MediumDiesisDown: *cents = -c(33.0 / 32.0); return true;
            case SymId::accSagittal11MediumDiesisUp:   *cents =  c(33.0 / 32.0); return true;
            case SymId::accSagittal11LargeDiesisDown:  *cents = -c(729.0 / 704.0); return true;
            case SymId::accSagittal11LargeDiesisUp:    *cents =  c(729.0 / 704.0); return true;
            case SymId::accSagittal35LargeDiesisDown:  *cents = -c(8505.0 / 8192.0); return true;
            case SymId::accSagittal35LargeDiesisUp:    *cents =  c(8505.0 / 8192.0); return true;
            case SymId::accSagittalFlat25SUp:          *cents = -apotome + c(6561.0 / 6400.0); return true;
            case SymId::accSagittalSharp25SDown:       *cents =  apotome - c(6561.0 / 6400.0); return true;
            case SymId::accSagittalFlat7CUp:           *cents = -apotome + c(64.0 / 63.0); return true;
            case SymId::accSagittalSharp7CDown:        *cents =  apotome - c(64.0 / 63.0); return true;
            case SymId::accSagittalFlat5CUp:           *cents = -apotome + c(81.0 / 80.0); return true;
            case SymId::accSagittalSharp5CDown:        *cents =  apotome - c(81.0 / 80.0); return true;
            case SymId::accSagittalFlat5v7kUp:         *cents = -apotome + c(5120.0 / 5103.0); return true;
            case SymId::accSagittalSharp5v7kDown:      *cents =  apotome - c(5120.0 / 5103.0); return true;
            case SymId::accSagittalFlat:               *cents = -apotome; return true;
            case SymId::accSagittalSharp:              *cents =  apotome; return true;
            default:
                  break;
            }
      // Wyschnegradsky: n twelfths of a tone, 72-EDO steps
      static const SymId wyschSharp[] = { SymId::accidentalWyschnegradsky1TwelfthsSharp, SymId::accidentalWyschnegradsky2TwelfthsSharp,
            SymId::accidentalWyschnegradsky3TwelfthsSharp, SymId::accidentalWyschnegradsky4TwelfthsSharp, SymId::accidentalWyschnegradsky5TwelfthsSharp,
            SymId::accidentalWyschnegradsky6TwelfthsSharp, SymId::accidentalWyschnegradsky7TwelfthsSharp, SymId::accidentalWyschnegradsky8TwelfthsSharp,
            SymId::accidentalWyschnegradsky9TwelfthsSharp, SymId::accidentalWyschnegradsky10TwelfthsSharp, SymId::accidentalWyschnegradsky11TwelfthsSharp };
      static const SymId wyschFlat[] = { SymId::accidentalWyschnegradsky1TwelfthsFlat, SymId::accidentalWyschnegradsky2TwelfthsFlat,
            SymId::accidentalWyschnegradsky3TwelfthsFlat, SymId::accidentalWyschnegradsky4TwelfthsFlat, SymId::accidentalWyschnegradsky5TwelfthsFlat,
            SymId::accidentalWyschnegradsky6TwelfthsFlat, SymId::accidentalWyschnegradsky7TwelfthsFlat, SymId::accidentalWyschnegradsky8TwelfthsFlat,
            SymId::accidentalWyschnegradsky9TwelfthsFlat, SymId::accidentalWyschnegradsky10TwelfthsFlat, SymId::accidentalWyschnegradsky11TwelfthsFlat };
      for (int i = 0; i < 11; ++i) {
            if (s == wyschSharp[i]) {
                  *cents = (i + 1) * 200.0 / 12.0;
                  return true;
                  }
            if (s == wyschFlat[i]) {
                  *cents = -(i + 1) * 200.0 / 12.0;
                  return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   modifierCents
//    a Helmholtz-Ellis prime modifier (Accidental::stackPrime): 7 64/63, 11 33/32, 13 27/26 (HEJI's
//    definitions; MuseScore 3.6 gives them no value), 17 … 53 as MuseScore 3.6.2 has them (HEJI's
//    combining schismas and commas)
//---------------------------------------------------------

static double modifierCents(SymId s, bool* valued)
      {
      auto c = [](double r) { return 1200.0 * std::log2(r); };
      *valued = true;
      switch (s) {
            case SymId::accidentalLowerOneSeptimalComma:          return -c(64.0 / 63.0);
            case SymId::accidentalRaiseOneSeptimalComma:          return  c(64.0 / 63.0);
            case SymId::accidentalLowerTwoSeptimalCommas:         return -2 * c(64.0 / 63.0);
            case SymId::accidentalRaiseTwoSeptimalCommas:         return  2 * c(64.0 / 63.0);
            case SymId::accidentalLowerOneUndecimalQuartertone:   return -c(33.0 / 32.0);
            case SymId::accidentalRaiseOneUndecimalQuartertone:   return  c(33.0 / 32.0);
            case SymId::accidentalLowerOneTridecimalQuartertone:  return -c(27.0 / 26.0);
            case SymId::accidentalRaiseOneTridecimalQuartertone:  return  c(27.0 / 26.0);
            default:
                  break;
            }
      *valued = false;
      return 0.0;
      }

// the stacked modifiers beside a note's accidental (their cents), valued: all have a value
static double stackedCents(const Note* n, bool* valued)
      {
      double sum = 0.0;
      *valued = true;
      for (const Element* e : n->el()) {
            if (!e->isSymbol() || !Accidental::stackPrime(toSymbol(e)->sym()))
                  continue;
            if (isEnharmonicSign(toSymbol(e)->sym())) {
                  int sharps, arrows;
                  if (toSymbol(e)->sym() == SymId::accidentalEnharmonicTilde
                      && ScoreTuning::hejiAccidental(n->accidentalType(), &sharps, &arrows) && arrows)
                        sum += (arrows < 0 ? -1.0 : 1.0) * 1200.0 * std::log2(32805.0 / 32768.0);
                  continue;
                  }
            bool v;
            double c = modifierCents(toSymbol(e)->sym(), &v);
            if (!v)
                  c = ScoreTuning::symbolCents(toSymbol(e)->sym(), &v);
            *valued = *valued && v;
            sum += c;
            }
      return sum;
      }

static bool hasStacked(const Note* n)
      {
      for (const Element* e : n->el())
            if (e->isSymbol() && Accidental::stackPrime(toSymbol(e)->sym()))
                  return true;
      return false;
      }

//---------------------------------------------------------
//   accidentalCents, symbolCents
//    cents from the natural note: MuseScore 3.6.2's value, else a Helmholtz-Ellis accidental's
//    (its sharps and commas; spelled: its sharps, which the note's spelling lacks)
//---------------------------------------------------------

double ScoreTuning::accidentalCents(AccidentalType type, bool* valued, int* spelled)
      {
      static const QHash<int, double> table = [] {
            QHash<int, double> h;
            for (const TuningTables::AccCents& a : TuningTables::accidentals)
                  h.insert(int(a.type), a.cents);
            return h;
            }();
      if (spelled)
            *spelled = 0;
      double conv;
      if (conventionCents(Accidental::subtype2symbol(type), &conv)) {
            *valued = true;
            return conv;
            }
      auto it = table.find(int(type));
      *valued = it != table.end();
      if (*valued)
            return *it;
      const double m = modifierCents(Accidental::subtype2symbol(type), valued);
      if (*valued)
            return m;
      int sharps, arrows;
      if (!hejiAccidental(type, &sharps, &arrows))
            return 0.0;
      *valued = true;
      if (spelled)
            *spelled = sharps;
      return 100.0 * sharps + COMMA * arrows;
      }

double ScoreTuning::symbolCents(SymId sym, bool* valued, int* spelled)
      {
      static const QHash<int, double> table = [] {
            QHash<int, double> h;
            for (const TuningTables::AccCents& a : TuningTables::accidentals)
                  h.insert(int(a.sym), a.cents);
            return h;
            }();
      static const QHash<int, AccidentalType> hejiSyms = [] {
            QHash<int, AccidentalType> h;
            for (int i = 0; i < int(AccidentalType::END); ++i) {
                  int s, a;
                  if (hejiAccidental(AccidentalType(i), &s, &a))
                        h.insert(int(Accidental::subtype2symbol(AccidentalType(i))), AccidentalType(i));
                  }
            return h;
            }();
      if (spelled)
            *spelled = 0;
      double conv;
      if (conventionCents(sym, &conv)) {
            *valued = true;
            return conv;
            }
      auto it = table.find(int(sym));
      *valued = it != table.end();
      if (*valued)
            return *it;
      const double m = modifierCents(sym, valued);
      if (*valued)
            return m;
      auto h = hejiSyms.find(int(sym));
      if (h == hejiSyms.end())
            return 0.0;
      return accidentalCents(*h, valued, spelled);
      }

//---------------------------------------------------------
//   looksLikeTunerValue
//    a tuning the Microtonal Tuner plugin can leave behind (a residue of its table's values,
//    mod 100): as its looksManaged(), such a value is the plugin's, not the user's
//---------------------------------------------------------

bool ScoreTuning::looksLikeTunerValue(double t)
      {
      static const QVector<double> residues = [] {
            QVector<double> r { 0.0 };
            for (const TuningTables::AccCents& a : TuningTables::accidentals) {
                  const double x = std::fmod(std::fmod(a.cents, 100.0) + 100.0, 100.0);
                  if (!r.contains(x))
                        r.append(x);
                  }
            return r;
            }();
      if (qAbs(t) < 0.001)
            return false;
      const double x = std::fmod(std::fmod(t, 100.0) + 100.0, 100.0);
      for (double r : residues) {
            const double d = qAbs(x - r);
            if (d < 0.01 || d > 99.99)
                  return true;
            }
      return false;
      }

//---------------------------------------------------------
//   smallestAccidentalGap
//    every value an accidental can give a note (cents from the natural), as this file plays them: each
//    accidental type and symbol accidentalCents / symbolCents value (MuseScore 3.6.2's table, the families
//    with one definition, HEJI's arrows and prime modifiers), the score choices' sizes (quarter tones of just
//    intonation 33/32 on a double flat … double sharp; koron and sori both ways) and the enharmonic tilde's
//    schisma (stacked on a HEJI arrow). The half tuning-dependent quarter tones (Quarter::HALF) are left out:
//    they follow the temperament, not a table. The smallest difference between two distinct values: half of it
//    is the lanes' tolerance (SoundLib::defaultLaneTolerance: merging only rounding, never two accidentals;
//    MuseScore 3.6's table is given to 0.1 cents, so its own rounding is at most 0.05 a value)
//---------------------------------------------------------

double ScoreTuning::smallestAccidentalGap()
      {
      static const double gap = [] {
            std::vector<double> v;
            bool valued;
            for (int i = 0; i < int(AccidentalType::END); ++i) {
                  const double c = accidentalCents(AccidentalType(i), &valued);
                  if (valued)
                        v.push_back(c);
                  }
            for (int i = 0; i < int(SymId::lastSym); ++i) {
                  const double c = symbolCents(SymId(i), &valued);
                  if (valued)
                        v.push_back(c);
                  }
            const double just = 1200.0 * std::log2(33.0 / 32.0);
            for (int sharps = -2; sharps <= 2; ++sharps)
                  for (int q : { -1, 1 })
                        v.push_back(100.0 * sharps + q * just);
            for (double c : { 50.0, 33.0, -50.0, -67.0 })        // sori / koron (Temperament::Persian)
                  v.push_back(c);
            const double tilde = 1200.0 * std::log2(32805.0 / 32768.0);
            v.push_back(tilde);
            v.push_back(-tilde);
            std::sort(v.begin(), v.end());
            double g = 1e9;
            for (size_t k = 1; k < v.size(); ++k)
                  if (v[k] - v[k - 1] > 1e-9)                    // (distinct: not the same value twice)
                        g = std::min(g, v[k] - v[k - 1]);
            return g;
            }();
      return gap;
      }

//---------------------------------------------------------
//   tuning
//---------------------------------------------------------

NoteTuning ScoreTuning::tuning(const Note* note)
      {
      auto it = _notes.find(note);
      if (it != _notes.end())
            return *it;
      const Measure* m = note->findMeasure();
      if (m && note->staffIdx() < _score->nstaves())
            computeMeasure(m, note->staffIdx());
      it = _notes.find(note);
      if (it != _notes.end())
            return *it;
      // a note the pass doesn't reach: its own tuning only
      NoteTuning t;
      t.manual = note->tuning();
      return t;
      }

//---------------------------------------------------------
//   computeMeasure
//    the Microtonal Tuner's pass (tuner.js, retune) over one measure of one staff: segments
//    in order, voices 1-4 at each, grace notes before their chord
//---------------------------------------------------------

void ScoreTuning::computeMeasure(const Measure* m, int staffIdx)
      {
      const QPair<const Measure*, int> key(m, staffIdx);
      if (_done.contains(key))
            return;
      _done.insert(key);

      const Staff* staff = _score->staff(staffIdx);
      if (!staff)
            return;
      const Fraction tick = m->tick();
      const bool drum = staff->isDrumStaff(tick);
      const bool pitchLines = staff->isPitchedStaff(tick);      // tablature has no staff lines of pitch

      // an accidental in force: its cents from the natural (stacked modifiers apart), sharps the
      // note's spelling lacks, and its symbol (families whose size the score chooses)
      struct Target { bool valued; double cents; int spelled; SymId sym; double stack; };
      QHash<int, Target> keyLines;                               // line mod 7 -> the custom key signature's symbol
      if (pitchLines) {
            const KeySigEvent ke = staff->keySigEventForClef(tick);
            if (ke.custom()) {
                  for (const KeySym& ks : ke.keySymbols()) {
                        bool valued;
                        int spelled;
                        const double c = symbolCents(ks.sym, &valued, &spelled);
                        if (valued)                              // no value in MuseScore: left alone
                              keyLines.insert(mod7(int(std::lround(ks.spos.y() * 2.0))), { true, c, spelled, ks.sym, 0.0 });
                        }
                  }
            }
      QHash<int, Target> bar;                                    // staff line -> the last accidental on it, this bar

      std::function<void(const Note*)> doNote = [&](const Note* n) {
            NoteTuning t;
            const double stored = n->tuning();
            if (drum) {
                  t.manual = stored;
                  _notes.insert(n, t);
                  return;
                  }
            if (n->tieBack() && n->firstTiedNote() && n->firstTiedNote() != n) {
                  t = tuning(n->firstTiedNote());                // sounds as the tie's start
                  t.tied = true;
                  _notes.insert(n, t);
                  return;
                  }
            if (!pitchLines) {                                    // tablature: frets are equal-tempered
                  t.manual = stored;
                  _notes.insert(n, t);
                  return;
                  }
            const int line = n->line();
            const int plain = 100 * (((n->tpc() + 1) / 7) - 2);  // the note's spelling as MuseScore plays it
            Target target { true, double(plain), 0, SymId::noSym, 0.0 };
            const AccidentalType acc = n->accidentalType();
            if (acc != AccidentalType::NONE || hasStacked(n)) {
                  bool valued = true;
                  int spelled = 0;
                  const double c = acc != AccidentalType::NONE ? accidentalCents(acc, &valued, &spelled) : double(plain);
                  bool stackValued;
                  const double stack = stackedCents(n, &stackValued);   // (stacked modifiers, Accidental::isStackModifier)
                  target = { valued && stackValued, c, spelled,
                             acc != AccidentalType::NONE ? Accidental::subtype2symbol(acc) : SymId::noSym, stack };
                  bar.insert(line, target);
                  }
            else if (bar.contains(line))
                  target = bar.value(line);
            else if (keyLines.contains(mod7(line)))
                  target = keyLines.value(mod7(line));
            if (!target.valued) {                                 // a symbol with no value: the note is left
                  t.unvalued = true;                              // alone, as the plugin does (its own tuning only)
                  t.temperament = 0.0;
                  t.manual = stored;
                  _notes.insert(n, t);
                  return;
                  }
            // the families whose size the score chooses (Temperament::quarter, ::persian)
            int sharps, quarter;
            if (quarterTone(target.sym, &sharps, &quarter) && _temperament.quarter != Temperament::Quarter::FIXED) {
                  target.spelled = sharps;
                  if (_temperament.quarter == Temperament::Quarter::JUST)
                        target.cents = 100.0 * sharps + quarter * 1200.0 * std::log2(33.0 / 32.0);
                  else {
                        // half the tuning's own chromatic step, up or down from the spelled note
                        auto temp = [this](int tpc, int pitch) { return _equal ? 0.0 : _temperament.cents(tpc, pitch); };
                        const int tb = n->tpc1() + 7 * sharps;
                        const int pb = n->pitch() + sharps;
                        const double step = quarter > 0 ? temp(tb + 7, pb + 1) + 100.0 - temp(tb, pb)
                                                        : temp(tb, pb) - temp(tb - 7, pb - 1) + 100.0;
                        target.cents = 100.0 * sharps + quarter * step / 2.0;
                        }
                  }
            else if (target.sym == SymId::accidentalSori || target.sym == SymId::accidentalKoron) {
                  static const double sori[] = { 50.0, 33.0 };               // Vaziri, MuseScore 3.6 (Temperament::Persian)
                  static const double koron[] = { -50.0, -67.0 };
                  const int p = int(_temperament.persian);
                  target.cents = target.sym == SymId::accidentalSori ? sori[p] : koron[p];
                  }
            target.cents += target.stack;
            // (a Helmholtz-Ellis sharp, which MuseScore plays as a natural: the temperament of the
            // sharp's spelling)
            double tempered;
            t.temperament = _equal || temperedCents(target.sym, &tempered) ? 0.0
                            : _temperament.cents(n->tpc1() + 7 * target.spelled, n->pitch() + target.spelled);
            t.accidental = std::round((target.cents - plain) * 1000.0) / 1000.0 + 0.0;
            const double computed = t.accidental + t.temperament;
            // the note's own tuning counts, unless the Microtonal Tuner plugin wrote it in
            // MuseScore 3.6 (the same total, or a value of its own left behind)
            if (qAbs(stored - computed) < 0.001 || looksLikeTunerValue(stored))
                  t.manual = 0.0;
            else
                  t.manual = stored;
            _notes.insert(n, t);
            };

      std::function<void(const Chord*)> doChord = [&](const Chord* c) {
            for (const Chord* g : c->graceNotes())
                  doChord(g);
            for (const Note* n : c->notes())
                  doNote(n);
            };

      for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (int v = 0; v < VOICES; ++v) {
                  const Element* e = s->element(staffIdx * VOICES + v);
                  if (e && e->isChord())
                        doChord(toChord(e));
                  }
            }
      }

//---------------------------------------------------------
//   playbackTuning
//    a note's tuning in cents from equal temperament, for playback. A rendering pass keeps
//    one ScoreTuning (ScoreTuningScope); a single note (clicked, entered) gets its own.
//---------------------------------------------------------

static thread_local ScoreTuning* currentTuning = nullptr;
static thread_local const Score* currentScore = nullptr;

ScoreTuningScope::ScoreTuningScope(const Score* score)
      : _tuning(score), _previous(currentTuning), _previousScore(currentScore)
      {
      currentTuning = &_tuning;
      currentScore = score;
      }

ScoreTuningScope::~ScoreTuningScope()
      {
      currentTuning = _previous;
      currentScore = _previousScore;
      }

double playbackTuning(const Note* note)
      {
      if (currentTuning && currentScore == note->score())
            return currentTuning->cents(note);
      ScoreTuning t(note->score());
      return t.cents(note);
      }

}     // namespace Ms
