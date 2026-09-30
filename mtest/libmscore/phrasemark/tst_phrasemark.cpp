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
#include "libmscore/chord.h"
#include "libmscore/excerpt.h"
#include "libmscore/instrument.h"
#include "libmscore/measure.h"
#include "libmscore/ms4playback.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/slur.h"
#include "libmscore/undo.h"

#define DIR QString("libmscore/phrasemark/")

using namespace Ms;

//---------------------------------------------------------
//   TestPhraseMark
//    slurs marked as phrase marks (libmscore/slur.h): no legato in playback, the metaTag
//    "phraseMarks" through saving and loading, undo, copy / paste, parts.
//    phrasemark.musicxml: violin, C5 D5 E5 F5 | G5 F5 E5 D5 | rest | rest, a slur over the eight
//    notes (the phrase mark) and an ordinary one inside it over E5 F5 G5
//---------------------------------------------------------

class TestPhraseMark : public QObject, public MTest
      {
      Q_OBJECT

      // the score's slurs that start at tick, by length (longest first)
      static std::vector<Slur*> slursAt(Score* score, int tick)
            {
            std::vector<Slur*> list;
            for (const auto& i : score->spannerMap().map())
                  if (i.second->isSlur() && i.second->tick().ticks() == tick)
                        list.push_back(toSlur(i.second));
            std::sort(list.begin(), list.end(), [](Slur* a, Slur* b) { return a->ticks() > b->ticks(); });
            return list;
            }
      static Slur* outer(Score* score)
            {
            std::vector<Slur*> l = slursAt(score, 0);
            return l.empty() ? nullptr : l.front();
            }
      static Slur* inner(Score* score)
            {
            std::vector<Slur*> l = slursAt(score, 2 * DIVISION);
            return l.empty() ? nullptr : l.front();
            }
      static std::vector<Chord*> chords(Score* score)
            {
            std::vector<Chord*> list;
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->element(0) && s->element(0)->isChord())
                        list.push_back(toChord(s->element(0)));
            return list;
            }
      // which of the chords MuseScore 4's model plays legato (the sound library follows it)
      static QString legato(Score* score)
            {
            Ms4::Dynamics dynamics;
            dynamics.build(score, score->parts().front());
            QString s;
            for (Chord* c : chords(score)) {
                  bool l = false;
                  for (const Ms4::ArtRef& a : Ms4::chordArticulations(c, dynamics))
                        l |= a.art == Ms4::Art::Legato;
                  s += l ? 'L' : '-';
                  }
            return s;
            }
      static void mark(Slur* slur, bool on)
            {
            slur->score()->startCmd();
            slur->undoChangeProperty(Pid::PHRASE_MARK, on);
            slur->score()->endCmd();
            }
      static QString fileText(const QString& name)
            {
            QFile f(name);
            if (!f.open(QIODevice::ReadOnly))
                  return QString();
            return QString::fromUtf8(f.readAll());
            }

   private slots:
      void initTestCase() { initMTest(); }
      void playback();
      void gateTimeMs3();
      void saveLoad();
      void undoRedo();
      void copyPaste();
      void parts();
      };

//---------------------------------------------------------
//   playback
//    the outer slur plays legato until the inner one starts (MS4 cuts it off there); as a phrase
//    mark, only the inner slur's notes are legato
//---------------------------------------------------------

void TestPhraseMark::playback()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      Slur* i = inner(score);
      QVERIFY(o && i && o != i);
      QCOMPARE(o->ticks().ticks(), 7 * DIVISION);
      QVERIFY(!o->phraseMark());
      QCOMPARE(legato(score), QString("LLLLL---"));

      mark(o, true);
      QVERIFY(o->phraseMark());
      QCOMPARE(legato(score), QString("--LLL---"));

      // the phrase mark alone: nothing legato
      score->startCmd();
      score->undoRemoveElement(i);
      score->endCmd();
      QCOMPARE(legato(score), QString("--------"));
      delete score;
      }

//---------------------------------------------------------
//   gateTimeMs3
//    MuseScore 3's note model plays a slurred note at 100 % of its length, others at the
//    instrument's gate time (here 95 %): a phrase mark is not a slur there either
//---------------------------------------------------------

void TestPhraseMark::gateTimeMs3()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Instrument* in = score->parts().front()->instrument();
      in->setArticulation({ MidiArticulation("", "", 100, 95) });
      auto lengths = [score]() {
            score->createPlayEvents();
            QString s;
            for (Chord* c : chords(score))
                  s += c->notes().front()->playEvents().front().len() == 1000 ? 'S' : '-';
            return s;
            };
      QCOMPARE(lengths(), QString("SSSSSSS-"));       // a slur's last note is not slurred here
      mark(outer(score), true);
      QCOMPARE(lengths(), QString("--SS----"));
      delete score;
      }

//---------------------------------------------------------
//   saveLoad
//    the mark is in the metaTag, not in the slur's XML; read back onto the slur; saved again the
//    same; a score without phrase marks has no metaTag
//---------------------------------------------------------

void TestPhraseMark::saveLoad()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      QVERIFY(saveScore(score, "phrasemark-none.mscx"));
      QVERIFY(!fileText("phrasemark-none.mscx").contains(PhraseMark::metaTag));
      QVERIFY(!fileText("phrasemark-none.mscx").contains("phraseMark"));

      mark(outer(score), true);
      QVERIFY(saveScore(score, "phrasemark-saved.mscx"));
      const QString text = fileText("phrasemark-saved.mscx");
      QVERIFY(text.contains("<metaTag name=\"phraseMarks\">[{&quot;tick&quot;:0,&quot;tick2&quot;:3360,&quot;track&quot;:0,&quot;track2&quot;:0}]</metaTag>"));
      QCOMPARE(text.count("phraseMark"), 1);        // the metaTag only: nothing in the slur
      // apart from the metaTag, the file is the one without the mark (MuseScore 3.6 reads it unchanged)
      QString without = text;
      without.remove(QRegularExpression("\\s*<metaTag name=\"phraseMarks\">[^<]*</metaTag>"));
      QCOMPARE(without, fileText("phrasemark-none.mscx"));

      MasterScore* again = readCreatedScore("phrasemark-saved.mscx");
      QVERIFY(again);
      QVERIFY(outer(again)->phraseMark());
      QVERIFY(!inner(again)->phraseMark());
      QVERIFY(again->metaTag(PhraseMark::metaTag).isEmpty());          // on the slur, not in the tags
      QCOMPARE(legato(again), QString("--LLL---"));
      QVERIFY(saveScore(again, "phrasemark-saved2.mscx"));
      QCOMPARE(fileText("phrasemark-saved2.mscx"), text);
      delete again;

      // a slur the list doesn't match (moved or deleted elsewhere) is not marked; nor another one
      // at the same start
      QString edited = text;
      edited.replace("&quot;tick2&quot;:3360", "&quot;tick2&quot;:2880");
      QFile f("phrasemark-edited.mscx");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(edited.toUtf8());
      f.close();
      MasterScore* other = readCreatedScore("phrasemark-edited.mscx");
      QVERIFY(other);
      QVERIFY(!outer(other)->phraseMark());
      QVERIFY(!inner(other)->phraseMark());
      delete other;
      delete score;
      }

//---------------------------------------------------------
//   undoRedo
//---------------------------------------------------------

void TestPhraseMark::undoRedo()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      // through a segment, as the Inspector sets it
      score->doLayout();
      QVERIFY(!o->spannerSegments().empty());
      score->startCmd();
      o->frontSegment()->undoChangeProperty(Pid::PHRASE_MARK, true, PropertyFlags::NOSTYLE);
      score->endCmd();
      QVERIFY(o->phraseMark());
      QCOMPARE(o->frontSegment()->getProperty(Pid::PHRASE_MARK).toBool(), true);
      score->undoRedo(true, 0);
      QVERIFY(!o->phraseMark());
      QCOMPARE(legato(score), QString("LLLLL---"));
      score->undoRedo(false, 0);
      QVERIFY(o->phraseMark());
      QCOMPARE(legato(score), QString("--LLL---"));
      delete score;
      }

//---------------------------------------------------------
//   copyPaste
//    measures 1-2 pasted over 3-4: the pasted outer slur is a phrase mark, the inner one not
//---------------------------------------------------------

void TestPhraseMark::copyPaste()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      mark(outer(score), true);
      Measure* m1 = score->firstMeasure();
      Measure* m2 = m1->nextMeasure();
      Measure* m3 = m2->nextMeasure();
      score->select(m1);
      score->select(m2, SelectType::RANGE, 0);
      QVERIFY(score->selection().canCopy());
      QMimeData* mimeData = new QMimeData;
      mimeData->setData(score->selection().mimeType(), score->selection().mimeData());
      score->select(m3->first(SegmentType::ChordRest)->element(0));
      score->startCmd();
      score->cmdPaste(mimeData, 0);
      score->endCmd();
      delete mimeData;

      std::vector<Slur*> pasted = slursAt(score, m3->tick().ticks());
      QCOMPARE(int(pasted.size()), 1);
      QVERIFY(pasted[0]->phraseMark());
      std::vector<Slur*> pastedInner = slursAt(score, m3->tick().ticks() + 2 * DIVISION);
      QCOMPARE(int(pastedInner.size()), 1);
      QVERIFY(!pastedInner[0]->phraseMark());
      QCOMPARE(legato(score), QString("--LLL-----LLL---"));
      delete score;
      }

//---------------------------------------------------------
//   parts
//    a part's copy of the slur follows the score's (linked), and the part keeps it through a
//    save and load
//---------------------------------------------------------

void TestPhraseMark::parts()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      QList<Part*> parts;
      parts.append(score->parts().at(0));
      Score* nscore = new Score(score);
      Excerpt* ex = new Excerpt(score);
      ex->setPartScore(nscore);
      ex->setParts(parts);
      ex->setTitle(parts.front()->partName());
      Excerpt::createExcerpt(ex);
      score->excerpts().append(ex);

      Slur* partSlur = outer(nscore);
      QVERIFY(partSlur);
      QVERIFY(partSlur != outer(score));
      mark(outer(score), true);
      QVERIFY(partSlur->phraseMark());
      QVERIFY(!inner(nscore)->phraseMark());
      score->undoRedo(true, 0);
      QVERIFY(!partSlur->phraseMark());
      score->undoRedo(false, 0);
      QVERIFY(partSlur->phraseMark());

      QVERIFY(saveScore(score, "phrasemark-parts.mscx"));
      QCOMPARE(fileText("phrasemark-parts.mscx").count("<metaTag name=\"phraseMarks\">"), 2);   // score and part
      MasterScore* again = readCreatedScore("phrasemark-parts.mscx");
      QVERIFY(again);
      QCOMPARE(int(again->excerpts().size()), 1);
      Score* part = again->excerpts().front()->partScore();
      QVERIFY(outer(again)->phraseMark());
      QVERIFY(outer(part)->phraseMark());
      QVERIFY(!inner(part)->phraseMark());
      QVERIFY(part->metaTag(PhraseMark::metaTag).isEmpty());
      delete again;
      delete score;
      }

QTEST_MAIN(TestPhraseMark)
#include "tst_phrasemark.moc"
