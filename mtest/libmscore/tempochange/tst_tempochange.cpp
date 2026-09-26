//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include <QtTest/QtTest>
#include "mtest/testutils.h"
#include "libmscore/score.h"
#include "libmscore/measure.h"
#include "libmscore/tempo.h"
#include "libmscore/tempochange.h"
#include "libmscore/textline.h"
#include "libmscore/undo.h"

#define DIR QString("libmscore/tempochange/")

using namespace Ms;

//---------------------------------------------------------
//   TestTempoChange
//    rit. / accel. lines in the tempo map (libmscore/tempochange.h)
//---------------------------------------------------------

class TestTempoChange : public QObject, public MTest
      {
      Q_OBJECT

      static TextLine* firstTextLine(Score* score)
            {
            for (const auto& i : score->spannerMap().map())
                  if (i.second->isTextLine())
                        return toTextLine(i.second);
            return nullptr;
            }

   private slots:
      void initTestCase() { initMTest(); }
      void terms();
      void ritardando();
      };

//---------------------------------------------------------
//   terms
//---------------------------------------------------------

void TestTempoChange::terms()
      {
      QCOMPARE(TempoChange::defaultFactor("rit."), 75.0);
      QCOMPARE(TempoChange::defaultFactor("<i>rall.</i>"), 75.0);
      QCOMPARE(TempoChange::defaultFactor("accel."), 133.0);
      QCOMPARE(TempoChange::defaultFactor("Staff"), 0.0);
      QCOMPARE(TempoChange::curve(ChangeMethod::NORMAL, 0.25), 0.25);
      QVERIFY(qAbs(TempoChange::curve(ChangeMethod::EASE_IN_OUT, 0.5) - 0.5) < 1e-12);
      }

//---------------------------------------------------------
//   ritardando
//    a rit. line over measures 2 and 3 (120 BPM): 75 % at its end, linear, the end tempo stays;
//    the factor and method from the Inspector, undo, and their round trip through the file
//---------------------------------------------------------

void TestTempoChange::ritardando()
      {
      MasterScore* score = readScore(DIR + "tempochange.mscx");
      QVERIFY(score);
      Measure* m2 = score->firstMeasure()->nextMeasure();
      Measure* m3 = m2->nextMeasure();
      Measure* m4 = m3->nextMeasure();

      TextLine* line = new TextLine(score, true);
      line->setBeginText("rit.");
      line->setTrack(0);
      line->setTrack2(0);
      line->setTick(m2->tick());
      line->setTick2(m4->tick());
      score->startCmd();
      score->undoAddElement(line);
      score->endCmd();
      score->doLayout();

      const TempoMap* tm = score->tempomap();
      QCOMPARE(tm->tempo(m2->tick().ticks() - 1), 2.0);
      QCOMPARE(tm->tempo(m2->tick().ticks()), 2.0);
      QVERIFY(qAbs(tm->tempo(m3->tick().ticks()) - 1.75) < 1e-9);            // half way
      QVERIFY(qAbs(tm->tempo(m4->tick().ticks()) - 1.5) < 1e-9);             // 75 %
      QVERIFY(qAbs(tm->tempo(m4->endTick().ticks() - 1) - 1.5) < 1e-9);      // and it stays

      // the Inspector's factor and method
      score->startCmd();
      line->undoChangeProperty(Pid::TEMPO_CHANGE_FACTOR, 50.0);
      line->undoChangeProperty(Pid::TEMPO_CHANGE_METHOD, int(ChangeMethod::EASE_IN));
      score->endCmd();
      score->doLayout();
      QVERIFY(qAbs(tm->tempo(m4->tick().ticks()) - 1.0) < 1e-9);
      const double easeIn = 2.0 - 1.0 * (1.0 - std::cos(M_PI / 4.0));
      QVERIFY(qAbs(tm->tempo(m3->tick().ticks()) - easeIn) < 1e-9);

      // saved in the metaTag, read back onto the line
      QVERIFY(saveScore(score, "tempochange-saved.mscx"));
      MasterScore* again = readCreatedScore("tempochange-saved.mscx");
      QVERIFY(again);
      TextLine* read = firstTextLine(again);
      QVERIFY(read);
      QCOMPARE(read->tempoChangeFactor(), 50.0);
      QCOMPARE(read->tempoChangeMethod(), ChangeMethod::EASE_IN);
      QVERIFY(again->metaTag(TempoChange::metaTag).isEmpty());          // on the line, not in the tags
      again->doLayout();
      QVERIFY(qAbs(again->tempomap()->tempo(m4->tick().ticks()) - 1.0) < 1e-9);
      delete again;

      // undo: back to the default
      score->undoRedo(true, 0);
      score->doLayout();
      QCOMPARE(line->tempoChangeFactor(), 0.0);
      QVERIFY(qAbs(tm->tempo(m4->tick().ticks()) - 1.5) < 1e-9);
      delete score;
      }

QTEST_MAIN(TestTempoChange)
#include "tst_tempochange.moc"
