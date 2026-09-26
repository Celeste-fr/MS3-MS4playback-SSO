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
            QVERIFY2(qAbs(tuning.cents(n) - want) < 0.001,
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
            QVERIFY2(qAbs(tuning.cents(it.value()) - want) < 0.001,
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

QTEST_MAIN(TestTuning)
#include "tst_tuning.moc"
