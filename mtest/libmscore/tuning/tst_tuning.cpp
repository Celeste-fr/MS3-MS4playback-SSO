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

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "libmscore/chord.h"
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
