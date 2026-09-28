//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3
//=============================================================================

// The fork's built-in tuning (libmscore/tuning.h) against the two MuseScore 3.6 plugins it
// replaces: the Microtonal Tuner's own rule fixture (cases.mscx and expected.json, copied from
// the plugin's test folder, gen_cases.py there) and billhails' Tuning plugin's temperaments.

#include <cmath>
#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "libmscore/chord.h"
#include "libmscore/key.h"
#include "libmscore/keysig.h"
#include "libmscore/page.h"
#include "libmscore/system.h"
#include "libmscore/staff.h"
#include "libmscore/sym.h"
#include "libmscore/undo.h"
#include "libmscore/symbol.h"
#include "libmscore/accidental.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/tuning.h"
#include "mtest/testutils.h"

#define DIR QString("libmscore/tuning/")

using namespace Ms;

class TestTuning : public QObject, public MTest
      {
      Q_OBJECT

   private slots:
      void initTestCase() { initMTest(); }
      void microtonal();
      void pluginParity();
      void microtonalTempered();
      void presets();
      void spelling();
      void justSpelling();
      void heji();
      void stacked();
      void temperedAndEnharmonic();
      void families();
      void diatonicCustomKey();
      void customKeyForClef();
      void customKeyDrop();
      void customKeyPasteAndAdapt();
      void json();
      };

//---------------------------------------------------------
//   notes: every note of the score by the fixture's key (staff:tick:track:pitch:grace)
//---------------------------------------------------------

static QMap<QString, const Note*> notes(Score* score)
      {
      QMap<QString, const Note*> m;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int t = 0; t < score->ntracks(); ++t) {
                  Element* e = s->element(t);
                  if (!e || !e->isChord())
                        continue;
                  Chord* c = toChord(e);
                  auto add = [&](const Chord* ch, int grace) {
                        for (const Note* n : ch->notes())
                              m.insert(QString("%1:%2:%3:%4:%5").arg(t / VOICES).arg(s->tick().ticks()).arg(t).arg(n->pitch()).arg(grace), n);
                        };
                  for (const Chord* g : c->graceNotes())
                        add(g, 1);
                  add(c, 0);
                  }
            }
      return m;
      }

static QJsonArray expected()
      {
      QFile f(QString(TESTROOT "/mtest/") + DIR + "expected.json");
      f.open(QIODevice::ReadOnly);
      return QJsonDocument::fromJson(f.readAll()).array();
      }

//---------------------------------------------------------
//   pluginRounded
//    the plugin rounded some accidentals the fork now plays exactly (Sagittal ratios to 0.1 cent,
//    Wyschnegradsky's twelfths of a tone to whole cents): how far off the plugin's value may be for
//    this note (its accidental, or the one before it on its line in the bar)
//---------------------------------------------------------

// the accidental in force on the note's line (its own, else the last one before it in the bar)
static const Accidental* accidentalInForce(const Note* n)
      {
      const Accidental* found = nullptr;
      const Measure* m = n->chord()->measure();
      for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            if (s->tick() > n->chord()->tick())
                  break;
            for (int t = n->staffIdx() * VOICES; t < (n->staffIdx() + 1) * VOICES; ++t) {
                  Element* e = s->element(t);
                  if (!e || !e->isChord())
                        continue;
                  QList<const Chord*> chords;
                  for (const Chord* g : toChord(e)->graceNotes())
                        chords.append(g);
                  chords.append(toChord(e));
                  for (const Chord* c : chords)
                        for (const Note* o : c->notes())
                              if (o->line() == n->line() && o->accidental() && (s->tick() < n->chord()->tick() || o == n))
                                    found = o->accidental();
                  }
            }
      return found;
      }

// Arel-Ezgi-Uzdilek accidentals, which MuseScore 3.6.2 (and so the plugin) gave no value: the fork
// plays them, in Holdrian commas (1/53 octave)
static bool aeuCents(const Note* n, double* cents)
      {
      const Accidental* a = accidentalInForce(n);
      SymId sym = a ? a->symbol() : SymId::noSym;
      if (!a) {                                            // a custom key signature's symbol on the line
            const KeySigEvent ke = n->staff()->keySigEvent(n->tick());
            if (ke.custom())
                  for (const KeySym& ks : ke.keySymbols())
                        if (((int(std::lround(ks.spos.y() * 2.0)) - n->line()) % 7 + 7) % 7 == 0)
                              sym = ks.sym;
            }
      const double k = 1200.0 / 53.0;
      switch (sym) {
            case SymId::accidentalBuyukMucennebFlat:  *cents = -8 * k; return true;
            case SymId::accidentalBakiyeFlat:         *cents = -4 * k; return true;
            case SymId::accidentalKucukMucennebSharp: *cents =  5 * k; return true;
            case SymId::accidentalBuyukMucennebSharp: *cents =  8 * k; return true;
            default:                                  return false;
            }
      }

static double pluginRounded(const Note* n)
      {
      const Measure* m = n->chord()->measure();
      for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            if (s->tick() > n->chord()->tick())
                  break;
            for (int t = n->staffIdx() * VOICES; t < (n->staffIdx() + 1) * VOICES; ++t) {
                  Element* e = s->element(t);
                  if (!e || !e->isChord())
                        continue;
                  QList<const Chord*> chords;
                  for (const Chord* g : toChord(e)->graceNotes())
                        chords.append(g);
                  chords.append(toChord(e));
                  for (const Chord* c : chords)
                        for (const Note* o : c->notes())
                              if (o->line() == n->line() && o->accidental()) {
                                    const QString name = Sym::id2name(o->accidental()->symbol());
                                    if (name.startsWith("accSagittal") || name.startsWith("accidentalWyschnegradsky"))
                                          return 0.7;
                                    }
                  }
            }
      return 0.001;
      }

//---------------------------------------------------------
//   microtonal
//    every note as the plugin tunes it. One deliberate difference: a tuning the user typed in
//    (not one the plugin wrote) is added on top here, where the plugin overwrites it.
//---------------------------------------------------------

void TestTuning::microtonal()
      {
      MasterScore* score = readScore(DIR + "cases.mscx");
      QVERIFY(score);
      const QMap<QString, const Note*> byKey = notes(score);
      const QJsonArray exp = expected();
      QCOMPARE(byKey.size(), exp.size());
      ScoreTuning tuning(score);
      int added = 0;
      for (const QJsonValue& v : exp) {
            const QJsonObject o = v.toObject();
            const QString key = QString("%1:%2:%3:%4:%5").arg(o["staff"].toInt()).arg(o["tick"].toInt())
                                .arg(o["track"].toInt()).arg(o["pitch"].toInt()).arg(o["grace"].toInt());
            const Note* n = byKey.value(key);
            QVERIFY2(n, qPrintable(key));
            const double stored = n->tuning();
            double want;
            if (o["tuning"].isString())                         // "keep": the plugin leaves the note alone
                  want = stored;
            else {
                  want = o["tuning"].toDouble();
                  if (qAbs(stored - want) > 0.001 && qAbs(stored) > 0.001 && !ScoreTuning::looksLikeTunerValue(stored)) {
                        want += stored;
                        ++added;
                        }
                  }
            double aeu;
            if (aeuCents(n, &aeu))                              // the plugin left it alone; the fork plays it
                  want = stored + aeu;
            QVERIFY2(qAbs(tuning.cents(n) - want) < pluginRounded(n),
                     qPrintable(QString("%1: want %2, got %3").arg(key).arg(want).arg(tuning.cents(n))));
            }
      qDebug("%d notes, %d with the user's own tuning added", exp.size(), added);
      delete score;
      }

//---------------------------------------------------------
//   pluginParity
//    cases-tempered.mscx (the fixture with quarter-comma meantone, the plugin's gen_tempered.py)
//    tuned by the Microtonal Tuner plugin in MuseScore 3.6.2 (plugin-tempered.json: each note's
//    tuning before and after): the fork plays each note as the plugin tunes it, but for a
//    tuning the user typed in, which the plugin overwrites and the fork adds
//---------------------------------------------------------

void TestTuning::pluginParity()
      {
      MasterScore* score = readScore(DIR + "cases-tempered.mscx");
      QVERIFY(score);
      QVERIFY(!Temperament::ofScore(score).isEqual());
      QFile f(QString(TESTROOT "/mtest/") + DIR + "plugin-tempered.json");
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QJsonObject plugin = QJsonDocument::fromJson(f.readAll()).object().value("notes").toObject();
      const QMap<QString, const Note*> byKey = notes(score);
      QCOMPARE(byKey.size(), plugin.size());
      ScoreTuning tuning(score);
      int same = 0, added = 0;
      for (auto it = byKey.begin(); it != byKey.end(); ++it) {
            const QJsonObject p = plugin.value(it.key()).toObject();
            QVERIFY2(!p.isEmpty(), qPrintable(it.key()));
            const double before = p.value("before").toDouble(), after = p.value("after").toDouble();
            double want = after;
            if (qAbs(before - after) > 0.001 && qAbs(before) > 0.001 && !ScoreTuning::looksLikeTunerValue(before)) {
                  want += before;
                  ++added;
                  }
            else
                  ++same;
            double aeu;
            if (aeuCents(it.value(), &aeu))                     // the plugin left it alone; the fork plays it
                  want = before + aeu + Temperament::ofScore(score).cents(it.value()->tpc1(), it.value()->pitch());
            QVERIFY2(qAbs(tuning.cents(it.value()) - want) < pluginRounded(it.value()),
                     qPrintable(QString("%1: plugin %2 (before %3), fork %4").arg(it.key()).arg(after).arg(before)
                                .arg(tuning.cents(it.value()))));
            }
      qDebug("%d notes as the plugin, %d with the user's own tuning added", same, added);
      delete score;
      }

//---------------------------------------------------------
//   microtonalTempered
//    with a temperament: each note's accidental part stays the plugin's, the temperament adds
//    the offset of its spelling, the parts add up
//---------------------------------------------------------

void TestTuning::microtonalTempered()
      {
      MasterScore* score = readScore(DIR + "cases.mscx");
      QVERIFY(score);
      const Temperament t = Temperament::preset("werkmeister");
      ScoreTuning plain(score);
      score->setMetaTag(Temperament::metaTag, t.toJson());
      ScoreTuning tempered(score);
      QVERIFY(!tempered.temperament().isEqual());
      QVERIFY(plain.temperament().isEqual());
      for (const Note* n : notes(score)) {
            const NoteTuning a = plain.tuning(n);
            const NoteTuning b = tempered.tuning(n);
            QCOMPARE(b.accidental, a.accidental);
            QVERIFY(qAbs(b.total() - (b.temperament + b.accidental + b.manual)) < 1e-9);
            if (!b.tied && !b.unvalued)
                  QCOMPARE(b.temperament, t.cents(n->tpc1(), n->pitch()));
            }
      delete score;
      }

//---------------------------------------------------------
//   presets
//    final values as the Tuning plugin shows them (lookUp, toFixed(1)), by pitch class C..B
//---------------------------------------------------------

void TestTuning::presets()
      {
      QCOMPARE(Temperament::presetNames().size(), 17);
      const Temperament w = Temperament::preset("werkmeister");
      const double werckmeister[12] = { 0, -10, -8, -6, -10, -2, -12, -4, -8, -12, -4, -8 };
      for (int i = 0; i < 12; ++i)
            QCOMPARE(w.offsets[i], werckmeister[i]);
      QVERIFY(!w.isChain());
      // Pythagorean: root Eb, pure tone A (0), each fifth +2
      const Temperament p = Temperament::preset("pythagorean");
      QCOMPARE(p.offsets[9], 0.0);
      QCOMPARE(p.offsets[4], 2.0);          // E
      QCOMPARE(p.offsets[2], -2.0);         // D
      QCOMPARE(p.offsets[8], 10.0);         // G#: the end of the chain from Eb
      QCOMPARE(p.offsets[3], -12.0);        // Eb: its start
      double step = 0;
      QVERIFY(p.isChain(&step));
      QCOMPARE(step, 2.0);
      // root G (1) and pure tone G: the plugin's rotation
      const Temperament v = Temperament::preset("vallotti", 1, 1, 0.0);
      QCOMPARE(v.offsets[7], 0.0);          // G
      QCOMPARE(v.offsets[2], -2.0);         // D, one fifth up
      const Temperament tw = Temperament::preset("equal", 0, 0, 3.0);
      QCOMPARE(tw.offsets[5], 3.0);
      QVERIFY(tw.isChain());
      }

//---------------------------------------------------------
//   spelling
//    chain tunings tell enharmonics apart; keyboard tunings don't
//---------------------------------------------------------

void TestTuning::spelling()
      {
      const int CS = Tpc::TPC_C_S, DB = Tpc::TPC_D_B;
      const Temperament p = Temperament::preset("pythagorean");
      QCOMPARE(p.cents(CS, 61), p.offsets[1]);               // C#: inside the chain Eb..G#
      QCOMPARE(p.cents(DB, 61) - p.cents(CS, 61), -24.0);    // Db: 12 fifths down, a Pythagorean comma lower
      const Temperament a = Temperament::preset("aaron");    // quarter-comma meantone
      QCOMPARE(a.cents(DB, 61) - a.cents(CS, 61), 42.0);     // Db 42 cents above C# (12 × 3.5)
      QCOMPARE(a.cents(Tpc::TPC_G_S, 68) - a.cents(Tpc::TPC_A_B, 68), -42.0);
      Temperament off = a;
      off.spelled = false;
      QCOMPARE(off.cents(DB, 61), off.cents(CS, 61));
      const Temperament w = Temperament::preset("werkmeister");
      QCOMPARE(w.cents(DB, 61), w.cents(CS, 61));
      const Temperament e;
      QCOMPARE(e.cents(DB, 61), 0.0);
      }

//---------------------------------------------------------
//   justSpelling
//    Just intonation by spelling (Ben Johnston's notation): every spelling against its ratio
//    (Johnston: the major scale 1/1 9/8 5/4 4/3 3/2 5/3 15/8, sharp / flat 25/24); off, the
//    Tuning plugin's 12 keys
//---------------------------------------------------------

void TestTuning::justSpelling()
      {
      auto fromEqual = [](double num, double den, int semitones) {
            return 1200.0 * std::log2(num / den) - 100.0 * semitones;
            };
      Temperament j = Temperament::preset("just");
      QVERIFY(j.just == Temperament::Just::KEYS);                                     // the plugin's keys by default
      QCOMPARE(j.cents(Tpc::TPC_D_B, 61), j.cents(Tpc::TPC_C_S, 61));
      j.just = Temperament::Just::JOHNSTON;
      struct { int tpc; double num, den; int semitones; } ratios[] = {
            { Tpc::TPC_C, 1, 1, 0 }, { Tpc::TPC_D, 9, 8, 2 }, { Tpc::TPC_E, 5, 4, 4 }, { Tpc::TPC_F, 4, 3, 5 },
            { Tpc::TPC_G, 3, 2, 7 }, { Tpc::TPC_A, 5, 3, 9 }, { Tpc::TPC_B, 15, 8, 11 },
            { Tpc::TPC_C_S, 25, 24, 1 }, { Tpc::TPC_D_B, 27, 25, 1 },  // C# and Db: 62.6 cents apart
            { Tpc::TPC_G_S, 25, 16, 8 }, { Tpc::TPC_A_B, 8, 5, 8 },    // G# and Ab: the diesis 128/125
            { Tpc::TPC_F_S, 25, 18, 6 }, { Tpc::TPC_B_B, 9, 5, 10 }, { Tpc::TPC_E_B, 6, 5, 3 },
            { Tpc::TPC_D_S, 75, 64, 3 }, { Tpc::TPC_C_SS, 625, 576, 2 }, { Tpc::TPC_E_BB, 144, 125, 2 },
            };
      for (const auto& r : ratios)
            QVERIFY2(qAbs(j.cents(r.tpc, 60 + r.semitones) - fromEqual(r.num, r.den, r.semitones)) < 1e-9,
                     qPrintable(QString("tpc %1: %2").arg(r.tpc).arg(j.cents(r.tpc, 60 + r.semitones))));
      QVERIFY(qAbs(j.cents(Tpc::TPC_A_B, 68) - j.cents(Tpc::TPC_G_S, 68) - 1200.0 * std::log2(128.0 / 125.0)) < 1e-9);
      // root G (1), pure tone G: G's major scale, B its pure third
      Temperament g = Temperament::preset("just", 1, 1, 0.0);
      g.just = Temperament::Just::JOHNSTON;
      QCOMPARE(g.cents(Tpc::TPC_G, 67), 0.0);
      QVERIFY(qAbs(g.cents(Tpc::TPC_B, 71) - g.cents(Tpc::TPC_G, 67) - fromEqual(5, 4, 4)) < 1e-9);
      QVERIFY(qAbs(g.cents(Tpc::TPC_F_S, 66) - fromEqual(15, 8, 11)) < 1e-9);
      // the 12 keys agree with it on 11 notes, to the plugin's rounding (within 1 cent): its F# is
      // 45/32, Johnston's 25/18 (its black keys are C# Eb F# G# Bb)
      const Temperament keys = Temperament::preset("just");
      for (const auto& r : ratios)
            if (r.semitones < 12 && r.tpc != Tpc::TPC_F_S && r.tpc != Tpc::TPC_D_B && r.tpc != Tpc::TPC_D_S
                && r.tpc != Tpc::TPC_A_B && r.tpc != Tpc::TPC_C_SS && r.tpc != Tpc::TPC_E_BB)
                  QVERIFY2(qAbs(keys.cents(r.tpc, 60 + r.semitones) - j.cents(r.tpc, 60 + r.semitones)) < 1.0,
                           qPrintable(QString("tpc %1").arg(r.tpc)));
      // a JSON round trip keeps the choice, and leaves it out when off
      QVERIFY(Temperament::fromJson(j.toJson()).just == Temperament::Just::JOHNSTON);
      QVERIFY(j.toJson().contains("\"just\":\"spelled\""));
      QVERIFY(!keys.toJson().contains("\"just\":"));
      QVERIFY(Temperament::fromJson(j.toJson()) == j);
      }

//---------------------------------------------------------
//   heji
//    Just intonation by spelling as Helmholtz-Ellis notates it: unmarked notes Pythagorean from
//    the root, each arrow a syntonic comma; HEJI's accidentals (which MuseScore 3 plays as
//    naturals) sound as written, carried through the bar like any accidental
//---------------------------------------------------------

void TestTuning::heji()
      {
      auto fromEqual = [](double num, double den, int semitones) {
            return 1200.0 * std::log2(num / den) - 100.0 * semitones;
            };
      Temperament h = Temperament::preset("just");
      h.just = Temperament::Just::HEJI;
      QVERIFY(qAbs(h.cents(Tpc::TPC_E, 64) - fromEqual(81, 64, 4)) < 1e-9);
      QVERIFY(qAbs(h.cents(Tpc::TPC_C_S, 61) - fromEqual(2187, 2048, 1)) < 1e-9);
      QVERIFY(qAbs(h.cents(Tpc::TPC_D_B, 61) - fromEqual(256, 243, 1)) < 1e-9);
      QVERIFY(Temperament::fromJson(h.toJson()).just == Temperament::Just::HEJI);
      QVERIFY(h.toJson().contains("\"just\":\"heji\""));

      // the accidentals: sharps and commas, whatever the tuning
      bool valued;
      int spelled;
      QVERIFY(qAbs(ScoreTuning::accidentalCents(AccidentalType::SHARP_ONE_ARROW_DOWN, &valued, &spelled) - (100.0 - 21.5063)) < 1e-3);
      QVERIFY(valued);
      QCOMPARE(spelled, 1);
      QVERIFY(qAbs(ScoreTuning::accidentalCents(AccidentalType::DOUBLE_FLAT_THREE_ARROWS_UP, &valued, &spelled) - (-200.0 + 3 * 21.5063)) < 1e-3);
      QCOMPARE(spelled, -2);

      MasterScore* score = readScore(DIR + "heji.mscx");
      QVERIFY(score);
      score->setMetaTag(Temperament::metaTag, h.toJson());
      ScoreTuning tuning(score);
      QList<const Note*> ns;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  ns.append(toChord(s->element(0))->upNote());
      QCOMPARE(ns.size(), 8);
      // cents from the written note's equal-tempered pitch (MuseScore plays HEJI's accidentals as naturals)
      const double want[8] = {
            0.0,                                       // C
            fromEqual(81, 64, 4),                      // E: Pythagorean
            fromEqual(5, 4, 4),                        // E, one arrow down: 5/4
            fromEqual(45, 32, 6) + 100.0,              // F sharp, one arrow down: 45/32, above the written F
            fromEqual(4, 3, 5),                        // F (a new bar)
            fromEqual(16, 9, 10) + 1200.0 * std::log2(81.0 / 80.0) * 2 - 100.0,   // B flat, two arrows up
            fromEqual(9, 8, 2),                        // D
            fromEqual(16, 9, 10) + 1200.0 * std::log2(81.0 / 80.0) * 2 - 100.0,   // B: the bar's B flat, two up
            };
      for (int i = 0; i < 8; ++i)
            QVERIFY2(qAbs(tuning.cents(ns[i]) - want[i]) < 0.002,
                     qPrintable(QString("note %1: want %2, got %3").arg(i).arg(want[i]).arg(tuning.cents(ns[i]))));
      delete score;
      }

//---------------------------------------------------------
//   stacked
//    stacked accidentals: a Helmholtz-Ellis prime modifier beside a note's accidental (a Symbol
//    on the note, which MuseScore 3.6 keeps): tuned with it and carried through the bar; drawn
//    and spaced by the accidental; the editing rules; saved and read back
//---------------------------------------------------------

//---------------------------------------------------------
//   temperedAndEnharmonic
//    Helmholtz-Ellis's tempered accidentals sound at their 12-edo pitch whatever the tuning; the
//    tilde beside an arrow accidental moves it one schisma to its Pythagorean respelling; "="
//    adds nothing (HEJI 2020 legend, reviewed by the owner 2026-09-27)
//---------------------------------------------------------

void TestTuning::temperedAndEnharmonic()
      {
      auto c = [](double num, double den) { return 1200.0 * std::log2(num / den); };
      MasterScore* score = readScore(DIR + "enharmonic.mscx");
      QVERIFY(score);
      Temperament h = Temperament::preset("just");
      h.just = Temperament::Just::HEJI;
      score->setMetaTag(Temperament::metaTag, h.toJson());
      score->doLayout();
      QList<Note*> ns;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  ns.append(toChord(s->element(0))->upNote());
      QCOMPARE(ns.size(), 4);
      ScoreTuning tuning(score);
      auto near = [](double a, double b) { return qAbs(a - b) < 0.002; };
      // C, tempered flat: 100 cents under the note MuseScore plays (C), no just intonation
      QVERIFY2(near(tuning.cents(ns[0]), -100.0), qPrintable(QString::number(tuning.cents(ns[0]))));
      // E, tempered natural: 0, where HEJI's plain E (81/64) would be +7.82
      QVERIFY2(near(tuning.cents(ns[1]), 0.0), qPrintable(QString::number(tuning.cents(ns[1]))));
      // G sharp, comma down, tilde: exactly the Pythagorean A flat (4 fifths down), from G
      const double aFlat = 3 * 1200.0 - 4 * c(3, 2);
      QVERIFY2(near(tuning.cents(ns[2]), aFlat - 700.0), qPrintable(QString::number(tuning.cents(ns[2]))));
      QVERIFY(near(tuning.cents(ns[2]) - (100.0 - c(81, 80) + (8 * c(3, 2) - 4 * 1200.0 - 800.0)), -c(32805, 32768)));
      // B flat, comma up, "=": B flat raised by a comma, nothing more, from B
      QVERIFY2(near(tuning.cents(ns[3]), (2 * 1200.0 - 2 * c(3, 2)) + c(81, 80) - 1100.0),
               qPrintable(QString::number(tuning.cents(ns[3]))));
      // the signs stack beside the accidental, outermost
      QVERIFY(Accidental::isStackModifier(AccidentalType::TILDE));
      QCOMPARE(ns[2]->accidental()->stackedModifiers(), QList<SymId>({ SymId::accidentalEnharmonicTilde }));
      // alone: no alteration, and valued (not left out of the tuning)
      bool valued = false;
      QCOMPARE(ScoreTuning::accidentalCents(AccidentalType::EQUALS, &valued), 0.0);
      QVERIFY(valued);
      delete score;
      }

void TestTuning::stacked()
      {
      auto c = [](double num, double den) { return 1200.0 * std::log2(num / den); };
      MasterScore* score = readScore(DIR + "stacked.mscx");
      QVERIFY(score);
      Temperament h = Temperament::preset("just");
      h.just = Temperament::Just::HEJI;
      score->setMetaTag(Temperament::metaTag, h.toJson());
      score->doLayout();
      auto notes = [](Score* sc) {
            QList<Note*> ns;
            for (Segment* s = sc->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->element(0) && s->element(0)->isChord())
                        ns.append(toChord(s->element(0))->upNote());
            return ns;
            };
      QList<Note*> ns = notes(score);
      QCOMPARE(ns.size(), 4);
      {
      ScoreTuning tuning(score);
      const double f = c(45, 32) - c(64, 63) - 500.0;          // F sharp, arrow down, septimal comma down
      QVERIFY2(qAbs(tuning.cents(ns[0]) - f) < 0.002, qPrintable(QString::number(tuning.cents(ns[0]))));
      QVERIFY(qAbs(tuning.cents(ns[1]) - f) < 0.002);          // carried through the bar
      QVERIFY(qAbs(tuning.cents(ns[2]) - (c(81, 64) + c(33, 32) - 400.0)) < 0.002);   // E, undecimal up
      QVERIFY(qAbs(tuning.cents(ns[3])) < 0.002);
      }

      // drawn by the accidental, to its left, and spaced with it; the symbol is saved there
      const Accidental* a = ns[0]->accidental();
      QVERIFY(a);
      QCOMPARE(a->stackedModifiers().size(), 1);
      Symbol* sym = nullptr;
      for (Element* e : ns[0]->el())
            if (e->isSymbol())
                  sym = toSymbol(e);
      QVERIFY(sym && sym->isStackedAccidental());
      QVERIFY(sym->bbox().isEmpty());
      QVERIFY(a->stackedModifierPos(sym->sym()).x() < 0.001);                    // leftmost
      QVERIFY(a->width() > a->symWidth(SymId::accidentalSharpOneArrowDown) * 1.5);

      // editing
      score->startCmd();
      score->changeAccidental(ns[0], AccidentalType::RAISE_ONE_SEPTIMAL_COMMA);    // the other 7: replaces
      score->endCmd();
      QCOMPARE(ns[0]->accidentalType(), AccidentalType::SHARP_ONE_ARROW_DOWN);
      QCOMPARE(ns[0]->accidental()->stackedModifiers(), QList<SymId>({ SymId::accidentalRaiseOneSeptimalComma }));
      score->startCmd();
      score->changeAccidental(ns[0], AccidentalType::RAISE_ONE_UNDECIMAL_QUARTERTONE);   // another prime: added
      score->endCmd();
      QCOMPARE(ns[0]->accidental()->stackedModifiers(),
               QList<SymId>({ SymId::accidentalRaiseOneUndecimalQuartertone, SymId::accidentalRaiseOneSeptimalComma }));
      score->startCmd();
      score->changeAccidental(ns[0], AccidentalType::RAISE_ONE_SEPTIMAL_COMMA);    // the same again: taken away
      score->endCmd();
      QCOMPARE(ns[0]->accidental()->stackedModifiers(), QList<SymId>({ SymId::accidentalRaiseOneUndecimalQuartertone }));
      score->undoRedo(true, nullptr);                                              // undo brings it back
      QCOMPARE(ns[0]->accidental()->stackedModifiers().size(), 2);
      score->startCmd();
      score->changeAccidental(ns[3], AccidentalType::LOWER_ONE_SEPTIMAL_COMMA);    // no accidental: it is the accidental
      score->endCmd();
      QCOMPARE(ns[3]->accidentalType(), AccidentalType::LOWER_ONE_SEPTIMAL_COMMA);
      QCOMPARE(ns[3]->pitch(), 60);

      // saved and read back: the accidental, the symbols (where the accidental draws them, for
      // MuseScore 3.6), the tuning
      QVERIFY(saveScore(score, "stacked-saved.mscx"));
      QVERIFY(saveScore(score, "stacked-saved.mscx"));          // (twice: no drift)
      for (Element* e : ns[0]->el()) {
            if (!e->isSymbol())
                  continue;
            const Accidental* acc = ns[0]->accidental();
            const QPointF want = acc->pos() + acc->stackedModifierPos(toSymbol(e)->sym());
            QVERIFY((e->pos() - want).manhattanLength() < 0.01);                   // (pos: with the offset)
            QVERIFY(e->pos().x() < acc->pos().x() + acc->bbox().width() - acc->symWidth(SymId::accidentalSharpOneArrowDown));
            }
      MasterScore* again = readCreatedScore("stacked-saved.mscx");
      QVERIFY(again);
      again->doLayout();
      QList<Note*> ns2 = notes(again);
      QCOMPARE(ns2[0]->accidentalType(), AccidentalType::SHARP_ONE_ARROW_DOWN);
      QCOMPARE(ns2[0]->accidental()->stackedModifiers().size(), 2);
      {
      ScoreTuning t1(score), t2(again);
      for (int i = 0; i < 4; ++i)
            QVERIFY(qAbs(t1.cents(ns[i]) - t2.cents(ns2[i])) < 1e-6);
      QVERIFY(qAbs(t2.cents(ns2[0]) - (c(45, 32) + c(64, 63) + c(33, 32) - 500.0)) < 0.002);
      QVERIFY(qAbs(t2.cents(ns2[3]) + c(64, 63)) < 0.002);
      }

      // the accidental taken away takes its modifiers with it
      score->startCmd();
      score->changeAccidental(ns[0], AccidentalType::NONE);
      score->endCmd();
      QVERIFY(!ns[0]->accidental());
      int left = 0;
      for (Element* e : ns[0]->el())
            left += e->isSymbol();
      QCOMPARE(left, 0);
      delete again;
      delete score;
      }

//---------------------------------------------------------
//   families
//    each accidental family by its own definition, and the two the score chooses: quarter tones
//    (fixed 50, half the tuning's sharp, 33/32) and koron / sori (Vaziri, practice, MuseScore 3.6)
//---------------------------------------------------------

void TestTuning::families()
      {
      auto c = [](double r) { return 1200.0 * std::log2(r); };
      const double k = 1200.0 / 53.0;
      bool v;
      auto acc = [&](AccidentalType t) { return ScoreTuning::accidentalCents(t, &v); };
      QVERIFY(qAbs(acc(AccidentalType::SAGITTAL_SHARP) - c(2187.0 / 2048.0)) < 1e-9);         // the apotome, exact
      QVERIFY(qAbs(acc(AccidentalType::SAGITTAL_5CU) - c(81.0 / 80.0)) < 1e-9);
      QCOMPARE(acc(AccidentalType::FLAT2_ARROW_UP), -150.0);      // three quarter tones flat (3.6.2's table: -250)
      QCOMPARE(acc(AccidentalType::FLAT2_ARROW_DOWN), -250.0);    // five quarter tones flat (3.6.2's table: -150)
      QVERIFY(qAbs(acc(AccidentalType::FLAT_SLASH2) + 8 * k) < 1e-9);     // büyük mücenneb flat: 8 Holdrian commas
      QVERIFY(qAbs(acc(AccidentalType::SHARP_SLASH3) - 5 * k) < 1e-9);    // küçük mücenneb sharp: 5
      QVERIFY(qAbs(acc(AccidentalType::THREE_COMMA_FLAT) + 3 * k) < 1e-9);        // Turkish folk: 3 commas
      QVERIFY(qAbs(acc(AccidentalType::FIVE_TWELFTH_SHARP) - 500.0 / 6.0) < 1e-9); // Wyschnegradsky: 5/12 tone
      QVERIFY(qAbs(acc(AccidentalType::RAISE_ONE_TRIDECIMAL_QUARTERTONE) - c(27.0 / 26.0)) < 1e-9);

      MasterScore* score = readScore(DIR + "quarter.mscx");
      QVERIFY(score);
      QList<const Note*> ns;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            ns.append(toChord(s->element(0))->upNote());
      QCOMPARE(ns.size(), 4);                   // C half-sharp, E sesqui-sharp, A koron, F sori
      auto play = [&](const Temperament& t) {
            score->setMetaTag(Temperament::metaTag, t.toJson());
            ScoreTuning tuning(score);
            QList<double> l;
            for (const Note* n : ns)
                  l.append(tuning.cents(n));
            return l;
            };
      // equal temperament, the defaults: 24-EDO and Vaziri
      Temperament t;
      QVERIFY(t.accidentalsDefault());
      QList<double> l = play(t);
      QVERIFY(qAbs(l[0] - 50) < 1e-3 && qAbs(l[1] - 150) < 1e-3 && qAbs(l[2] + 50) < 1e-3 && qAbs(l[3] - 50) < 1e-3);
      t.quarter = Temperament::Quarter::HALF;                     // equal temperament: the same
      l = play(t);
      QVERIFY(qAbs(l[0] - 50) < 1e-3 && qAbs(l[1] - 150) < 1e-3);

      // quarter-comma meantone
      Temperament m = Temperament::preset("aaron");
      l = play(m);                                                 // fixed: 50 above the tuned C
      QVERIFY(qAbs(l[0] - (m.cents(Tpc::TPC_C, 60) + 50)) < 1e-3);
      m.quarter = Temperament::Quarter::HALF;                      // half: midway between C and C#
      l = play(m);
      QVERIFY(qAbs(l[0] - (m.cents(Tpc::TPC_C, 60) + m.cents(Tpc::TPC_C_S, 61) + 100) / 2) < 1e-3);
      QVERIFY(qAbs(l[1] - (m.cents(Tpc::TPC_E_S, 65) + 100 + m.cents(Tpc::TPC_E_SS, 66) + 200) / 2) < 1e-3);  // midway E# - E##
      m.quarter = Temperament::Quarter::JUST;                      // 33/32 above the tuned C
      l = play(m);
      QVERIFY(qAbs(l[0] - (m.cents(Tpc::TPC_C, 60) + c(33.0 / 32.0))) < 1e-3);

      // koron and sori
      Temperament p;
      p.persian = Temperament::Persian::PRACTICE;
      l = play(p);
      QVERIFY(qAbs(l[2] + 60) < 1e-3 && qAbs(l[3] - 40) < 1e-3);
      p.persian = Temperament::Persian::MS36;
      l = play(p);
      QVERIFY(qAbs(l[2] + 67) < 1e-3 && qAbs(l[3] - 33) < 1e-3);

      // the choices go through the metaTag, left out when default
      const Temperament r = Temperament::fromJson(p.toJson());
      QVERIFY(r.persian == Temperament::Persian::MS36 && r.quarter == Temperament::Quarter::FIXED);
      QVERIFY(!Temperament().toJson().contains("persian") && !Temperament().toJson().contains("quarterTones"));
      m.quarter = Temperament::Quarter::HALF;
      QVERIFY(Temperament::fromJson(m.toJson()).quarter == Temperament::Quarter::HALF);
      delete score;
      }

//---------------------------------------------------------
//   json: the Tuning plugin's save file
//---------------------------------------------------------

void TestTuning::json()
      {
      bool ok = false;
      const Temperament t = Temperament::fromJson(
            "{\"offsets\":[0,-10,-8,-6,-10,-2,-12,-4,-8,-12,-4,-8],\"temperament\":\"werkmeister\",\"root\":0,\"pure\":0,\"tweak\":0}", &ok);
      QVERIFY(ok);
      QVERIFY(t == Temperament::preset("werkmeister"));
      QVERIFY(Temperament::fromJson(t.toJson()) == t);
      Temperament s = Temperament::preset("aaron");
      s.spelled = false;
      QVERIFY(Temperament::fromJson(s.toJson()) == s);
      Temperament::fromJson("not json", &ok);
      QVERIFY(!ok);
      }

//---------------------------------------------------------
//   diatonicCustomKey
//    Alt+Shift+Up / Down (pitch-up/down-diatonic) under a custom key signature (a flat on B, a
//    quarter-tone flat on E): the next step takes the signature's accidental (A up: B flat, not
//    B natural with a natural sign); a step with a microtonal accidental is its natural (the
//    signature's symbol is its tuning); no accidental is added
//---------------------------------------------------------

void TestTuning::diatonicCustomKey()
      {
      MasterScore* score = readScore(DIR + "keysig-updown.mscx");
      QVERIFY(score);
      QVERIFY(score->staff(0)->keySigEvent(Fraction(0, 1)).custom());
      std::vector<Note*> ns;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            if (s->element(0) && s->element(0)->isChord())
                  ns.push_back(toChord(s->element(0))->upNote());
      QCOMPARE(int(ns.size()), 4);
      struct Case { int index; bool up; int pitch; int tpc; };
      const Case cases[] = {
            { 0, true,  70, 12 },         // A4 up: B flat 4
            { 1, true,  76, 18 },         // D5 up: E5 (the signature's quarter-tone flat)
            { 2, false, 70, 12 },         // C5 down: B flat 4
            { 3, false, 74, 16 },         // E5 down: D5
            };
      for (const Case& c : cases) {
            Note* n = ns[c.index];
            score->select(n, SelectType::SINGLE, 0);
            score->startCmd();
            score->upDown(c.up, UpDownMode::DIATONIC);
            score->endCmd();
            QCOMPARE(n->pitch(), c.pitch);
            QCOMPARE(n->tpc(), c.tpc);
            QVERIFY2(!n->accidental(), qPrintable(QString("note %1 got an accidental").arg(c.index)));
            }
      // and back
      score->select(ns[0], SelectType::SINGLE, 0);
      score->startCmd();
      score->upDown(false, UpDownMode::DIATONIC);
      score->endCmd();
      QCOMPARE(ns[0]->pitch(), 69);
      QCOMPARE(ns[0]->tpc(), 17);
      delete score;
      }

//---------------------------------------------------------
//   customKeyForClef
//    a custom key signature made on a treble staff (the palette's), dropped on staves with other
//    clefs: each gets it for its clef, so it alters the same notes (the owner: the bass staff had
//    the treble's positions, a wrong key signature); a standard-looking one lands where that
//    clef's standard key signature puts it
//---------------------------------------------------------

void TestTuning::customKeyForClef()
      {
      KeySigEvent e;
      e.setCustom(true);
      auto add = [&e](SymId sym, double y) { KeySym k; k.sym = sym; k.spos = QPointF(0.0, y); e.keySymbols().append(k); };
      add(SymId::accidentalSharp, 0.0);                       // F5
      add(SymId::accidentalFlat, 2.0);                        // B4
      add(SymId::accidentalQuarterToneFlatStein, 0.5);        // E5, a quarter-tone flat
      QCOMPARE(e.forClef(ClefType::G, ClefType::G), e);
      const KeySigEvent bass = e.forClef(ClefType::G, ClefType::F);
      QCOMPARE(bass.keySymbols()[0].spos.y(), 1.0);            // F3: bass F sharp's line
      QCOMPARE(bass.keySymbols()[1].spos.y(), 3.0);            // B2: bass B flat's line
      QCOMPARE(bass.keySymbols()[2].spos.y(), 1.5);            // E3: bass E flat's line
      AccidentalState treble;
      treble.init(e, ClefType::G);
      for (ClefType c : { ClefType::F, ClefType::C3, ClefType::C4, ClefType::G8_VB }) {
            AccidentalState other;
            other.init(e.forClef(ClefType::G, c), c);
            for (int step = 7; step < MAX_ACC_STATE - 7; ++step)
                  QVERIFY2(treble.accidentalVal(step) == other.accidentalVal(step),
                           qPrintable(QString("clef %1, step %2").arg(int(c)).arg(step)));
            }
      }

//---------------------------------------------------------
//   customKeyDrop
//    a custom key signature (the palette's, made on a treble staff) dropped on a score of a
//    viola (alto clef) and a cello (treble clef 15mb): each staff's signature for its clef, and
//    the same notes take the same accidentals
//---------------------------------------------------------

void TestTuning::customKeyDrop()
      {
      MasterScore* score = readScore(DIR + "keysig-clefs.musicxml");
      QVERIFY(score);
      QCOMPARE(score->staff(0)->clef(Fraction(0, 1)), ClefType::C3);
      KeySigEvent e;
      e.setCustom(true);
      KeySym k;
      k.sym = SymId::accidentalFlat;
      k.spos = QPointF(0.0, 0.5);                                  // E5 on a treble staff
      e.keySymbols().append(k);
      KeySig* ks = new KeySig(score);
      ks->setKeySigEvent(e);
      score->doLayout();
      Measure* m = score->firstMeasure();
      EditData ed;
      ed.dropElement = ks;
      ed.pos = m->staffabbox(1).center() + m->system()->page()->pos();          // as the palette drops it
      score->startCmd();
      score->firstMeasure()->drop(ed);
      score->endCmd();
      // E flat in every octave, whatever the clef: as the staff's key list holds it and as read
      // for the clef in force at tick
      auto eFlat = [score](int staffIdx, const Fraction& tick, const char* what) {
            const ClefType clef = score->staff(staffIdx)->clef(tick);
            const KeySigEvent se = score->staff(staffIdx)->keySigEventForClef(tick);
            QVERIFY(se.custom());
            AccidentalState as;
            as.init(se, clef);
            for (int step = 7; step < MAX_ACC_STATE - 7; ++step)
                  QVERIFY2(as.accidentalVal(step) == (step % 7 == 2 ? AccidentalVal::FLAT : AccidentalVal::NATURAL),
                           qPrintable(QString("%1: staff %2 clef %3 step %4 line %5").arg(what).arg(staffIdx).arg(int(clef)).arg(step).arg(se.keySymbols()[0].spos.y())));
            };
      const Fraction bar2 = score->firstMeasure()->nextMeasure()->tick();
      for (int staffIdx : { 0, 1 })
            eFlat(staffIdx, Fraction(0, 1), "dropped");

      // the viola's clef changed at the signature (treble): the signature follows it
      score->startCmd();
      score->undoChangeClef(score->staff(0), score->firstMeasure(), ClefType::G);
      score->endCmd();
      QCOMPARE(score->staff(0)->clef(Fraction(0, 1)), ClefType::G);
      QCOMPARE(score->staff(0)->keySigEvent(Fraction(0, 1)).keySymbols()[0].spos.y(), 0.5);
      eFlat(0, Fraction(0, 1), "clef changed at the signature");

      // the cello changes to bass clef in bar 2: the signature, drawn for the treble clef, is
      // read (and drawn at a system start) for the bass clef there
      score->startCmd();
      score->undoChangeClef(score->staff(1), score->firstMeasure()->nextMeasure(), ClefType::F);
      score->endCmd();
      QCOMPARE(score->staff(1)->clef(bar2), ClefType::F);
      QCOMPARE(score->staff(1)->keySigEvent(bar2).keySymbols()[0].spos.y(), 0.5);
      eFlat(1, bar2, "later clef change");

      // a staff added with the Instruments dialog takes the first staff's keys, for its own clef
      KeyList km = *score->staff(1)->keyList();
      std::map<int, ClefType> kmClefs;
      for (const auto& k : km)
            kmClefs[k.first] = score->staff(1)->clef(Fraction::fromTicks(k.first));
      MasterScore* other = readScore(DIR + "keysig-clefs.musicxml");
      QVERIFY(other);
      other->adjustKeySigs(0, 1, km, kmClefs);                   // the viola, alto clef
      auto eFlatOther = [other]() {
            AccidentalState as;
            as.init(other->staff(0)->keySigEventForClef(Fraction(0, 1)), ClefType::C3);
            for (int step = 7; step < MAX_ACC_STATE - 7; ++step)
                  QVERIFY2(as.accidentalVal(step) == (step % 7 == 2 ? AccidentalVal::FLAT : AccidentalVal::NATURAL),
                           qPrintable(QString("added staff: step %1").arg(step)));
            };
      eFlatOther();
      delete other;
      delete score;
      }

//---------------------------------------------------------
//   customKeyPasteAndAdapt
//    a score saved before the signature followed the clef: the viola (alto clef) has the
//    treble-15mb cello's E flat as placed. Adapt key signatures to clefs places it for the alto
//    clef, once. Then the viola's signature copied (as Ctrl+C writes it) and pasted on the cello
//    (as Ctrl+V drops it): E flat on both.
//---------------------------------------------------------

void TestTuning::customKeyPasteAndAdapt()
      {
      MasterScore* score = readScore(DIR + "keysig-clefs.musicxml");
      QVERIFY(score);
      KeySigEvent e;
      e.setCustom(true);
      KeySym k;
      k.sym = SymId::accidentalFlat;
      k.spos = QPointF(0.0, 0.5);                                  // E5 on a treble staff
      e.keySymbols().append(k);
      score->startCmd();
      for (int staffIdx : { 0, 1 })
            score->undoChangeKeySig(score->staff(staffIdx), Fraction(0, 1), e);     // as placed
      score->endCmd();
      auto eFlat = [score](int staffIdx, const char* what) {
            const ClefType clef = score->staff(staffIdx)->clef(Fraction(0, 1));
            AccidentalState as;
            as.init(score->staff(staffIdx)->keySigEventForClef(Fraction(0, 1)), clef);
            for (int step = 7; step < MAX_ACC_STATE - 7; ++step)
                  QVERIFY2(as.accidentalVal(step) == (step % 7 == 2 ? AccidentalVal::FLAT : AccidentalVal::NATURAL),
                           qPrintable(QString("%1: staff %2 step %3").arg(what).arg(staffIdx).arg(step)));
            };
      eFlat(1, "cello as placed");
      QVERIFY(score->staff(0)->keySigEvent(Fraction(0, 1)).keySymbols()[0].spos.y() == 0.5);   // F on the alto staff

      score->startCmd();
      QCOMPARE(score->cmdAdaptKeySigsToClefs(), 1);
      score->endCmd();
      eFlat(0, "adapted");
      eFlat(1, "adapted");
      score->startCmd();
      QCOMPARE(score->cmdAdaptKeySigsToClefs(), 0);                 // adapted ones are left alone
      score->endCmd();
      eFlat(0, "adapted twice");

      // copy the viola's signature, paste it on the cello (after clearing the cello's)
      score->doLayout();
      Segment* seg = score->firstMeasure()->findSegment(SegmentType::KeySig, Fraction(0, 1));
      QVERIFY(seg);
      KeySig* violaKey = toKeySig(seg->element(0));
      QVERIFY(violaKey);
      const QByteArray data = violaKey->mimeData(QPointF());
      score->startCmd();
      score->undoChangeKeySig(score->staff(1), Fraction(0, 1), KeySigEvent());
      score->endCmd();
      score->doLayout();
      QPointF dragOffset;
      Fraction duration(1, 4);
      std::unique_ptr<Element> pasted(Element::readMimeData(score, data, &dragOffset, &duration));
      QVERIFY(pasted && pasted->isKeySig());
      Element* target = score->firstMeasure()->findSegment(SegmentType::ChordRest, Fraction(0, 1))->element(4);
      QVERIFY(target);
      if (target->isChord())
            target = toChord(target)->upNote();
      EditData ed;
      ed.dropElement = pasted.get();
      QVERIFY(target->acceptDrop(ed));
      ed.dropElement = pasted->clone();
      score->startCmd();
      target->drop(ed);
      score->endCmd();
      eFlat(0, "pasted");
      eFlat(1, "pasted");
      delete score;
      }

QTEST_MAIN(TestTuning)
#include "tst_tuning.moc"
