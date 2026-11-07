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
#include "audio/midi/event.h"
#include "mtest/testutils.h"
#include "libmscore/chord.h"
#include "libmscore/excerpt.h"
#include "libmscore/instrument.h"
#include "libmscore/measure.h"
#include "libmscore/ms4playback.h"
#include "libmscore/articulation.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/rendermidi.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/tempo.h"
#include "libmscore/tempotext.h"
#include "libmscore/textline.h"
#include "libmscore/slur.h"
#include "libmscore/undo.h"

#define DIR QString("libmscore/phrasemark/")

using namespace Ms;

//---------------------------------------------------------
//   TestPhraseMark
//    slurs marked as phrase marks (libmscore/slur.h): no legato in playback, the metaTag
//    "phraseMarks" through saving and loading, undo, copy / paste, parts.
//    phrasemark.musicxml: violin, C5 D5 E5 F5 | G5 F5 E5 D5 | rest | rest, a slur over the eight
//    notes (the phrase mark) and an ordinary one inside it over E5 F5 G5; 4/4 at 120 BPM.
//    Shape and Ending (slur.h › Phrase shape and ending): levels and tempo, their metaTag, undo, copy / paste, parts
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
      static void setShape(Slur* slur, int shape, int ending)
            {
            slur->score()->startCmd();
            slur->undoChangeProperty(Pid::PHRASE_SHAPE, shape);
            slur->undoChangeProperty(Pid::PHRASE_ENDING, ending);
            slur->score()->endCmd();
            }
      // MuseScore 3.6's dynamics method: the MS3 playback mode (tst_midi's ms3State)
      static SynthesizerState ms3State()
            {
            return SynthesizerState({ SynthesizerGroup("master", { { 4, "1" }, { 5, "1" } }) });
            }
      // every rendered event, a line each ("tick type channel a b port extChannel switch patch"), sorted
      static QStringList events(MasterScore* score, std::shared_ptr<SoundLib::Library> library, const SynthesizerState& state = SynthesizerState())
            {
            SoundLib::setCurrent(library);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            EventMap events;
            score->renderMidi(&events, false, true, state);
            SoundLib::setCurrent(nullptr);
            SoundLib::setOutput(SoundLib::Output::MIDI);
            QStringList lines;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  lines << QString("%1 %2 %3 %4 %5 %6 %7 %8 %9").arg(te.first, 8, 10, QChar('0')).arg(e.type()).arg(e.channel())
                           .arg(e.dataA()).arg(e.dataB()).arg(e.extPort()).arg(e.extChannel()).arg(e.librarySwitch())
                           .arg(e.libraryPatch());
                  }
            lines.sort();
            return lines;
            }
      struct Ev { int tick; NPlayEvent e; };
      static std::vector<Ev> render(MasterScore* score, std::shared_ptr<SoundLib::Library> library, const SynthesizerState& state = SynthesizerState())
            {
            SoundLib::setCurrent(library);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            EventMap events;
            score->renderMidi(&events, false, true, state);
            SoundLib::setCurrent(nullptr);
            SoundLib::setOutput(SoundLib::Output::MIDI);
            std::vector<Ev> list;
            for (const auto& te : events)
                  if (library ? te.second.isExternal() : !te.second.isExternal())
                        list.push_back({ te.first, te.second });
            return list;
            }
      // the note-ons (not switches) in order: velocity, and the value of controller cc in force right before (-1: none)
      struct On { int pitch; int velocity; int cc; };
      static std::vector<On> noteOns(const std::vector<Ev>& ev, int cc = -1)
            {
            std::vector<On> list;
            for (size_t i = 0; i < ev.size(); ++i) {
                  const NPlayEvent& e = ev[i].e;
                  if (e.type() != ME_NOTEON || e.velo() == 0 || e.librarySwitch())
                        continue;
                  int value = -1;
                  for (size_t k = i; cc >= 0 && k-- > 0;)
                        if (ev[k].e.type() == ME_CONTROLLER && !ev[k].e.librarySwitch() && ev[k].e.controller() == cc
                            && ev[k].e.channel() == e.channel() && ev[k].e.extPort() == e.extPort() && ev[k].e.extChannel() == e.extChannel()) {
                              value = ev[k].e.value();
                              break;
                              }
                  list.push_back({ e.pitch(), e.velo(), value });
                  }
            return list;
            }
      std::shared_ptr<SoundLib::Library> sso()
            {
            QString error;
            auto lib = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
            if (!lib)
                  qWarning("%s", qPrintable(error));
            return lib;
            }
      static QString fileText(const QString& name)
            {
            QFile f(name);
            if (!f.open(QIODevice::ReadOnly))
                  return QString();
            return QString::fromUtf8(f.readAll());
            }

   private slots:
      // the tests' expectations were written for these [phrasing] values: pinned, so the defaults (piano data) can move
      void initTestCase() {
            initMTest();
            Playback::setIniValuesForTest({ { "phrasing/shapeAmount", "3" }, { "phrasing/endingSlight", "90" },
                                            { "phrasing/endingBroad", "80" }, { "phrasing/endingLength", "2" } });
            }
      void playback();
      void gateTimeMs3();
      void saveLoad();
      void undoRedo();
      void copyPaste();
      void parts();
      void shapeCurves();
      void defaultsUnchanged();
      void shapeBuiltIn();
      void shapeLibrary();
      void ending();
      void endingWrittenWins();
      void shapeEndingSaveLoad();
      void shapeEndingUndo();
      void shapeEndingCopyPaste();
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); }
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

//---------------------------------------------------------
//   shapeCurves
//    0 … 1 of the amount along each Shape; an Arch peaks at the highest note (G5, the 5th of 8 chords: x = 4/7)
//---------------------------------------------------------

void TestPhraseMark::shapeCurves()
      {
      using namespace PhraseMark;
      QCOMPARE(shapeAt(SHAPE_NONE, 0.5, 0.5), 0.0);
      QCOMPARE(shapeAt(SHAPE_TOWARD_END, 0.25, 0.5), 0.25);
      QCOMPARE(shapeAt(SHAPE_TOWARD_START, 0.25, 0.5), 0.75);
      QCOMPARE(shapeAt(SHAPE_ARCH, 0.0, 0.5), 0.0);
      QCOMPARE(shapeAt(SHAPE_ARCH, 0.5, 0.5), 1.0);
      QCOMPARE(shapeAt(SHAPE_ARCH, 0.75, 0.5), 0.5);
      QCOMPARE(shapeAt(SHAPE_ARCH, 1.0, 0.5), 0.0);
      QCOMPARE(shapeAt(SHAPE_ARCH, 0.0, 0.0), 1.0);       // (a peak on the first chord: falling from it)
      QCOMPARE(shapeAt(SHAPE_ARCH, 1.0, 1.0), 1.0);

      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      QVERIFY(qAbs(archPeak(o) - 4.0 / 7.0) < 1e-12);
      const std::vector<Chord*> c = chords(score);
      QCOMPARE(int(c.size()), 8);
      // no phrase mark, or no Shape: nothing
      setShape(o, SHAPE_ARCH, ENDING_NONE);
      QCOMPARE(shapeDb(c[4], 3.0), 0.0);                  // (not a phrase mark: a slur's Shape doesn't play)
      mark(o, true);
      QVERIFY(qAbs(shapeDb(c[4], 3.0) - 3.0) < 1e-12);
      QVERIFY(qAbs(shapeDb(c[2], 3.0) - 1.5) < 1e-12);    // half way up
      QCOMPARE(shapeDb(c[0], 3.0), 0.0);
      QCOMPARE(shapeDb(c[7], 3.0), 0.0);
      setShape(o, SHAPE_TOWARD_END, ENDING_NONE);
      QVERIFY(qAbs(shapeDb(c[7], 3.0) - 3.0) < 1e-12);
      QVERIFY(qAbs(shapeDb(c[1], 3.0) - 3.0 / 7.0) < 1e-12);
      setShape(o, SHAPE_TOWARD_START, ENDING_NONE);
      QVERIFY(qAbs(shapeDb(c[0], 3.0) - 3.0) < 1e-12);
      QCOMPARE(shapeDb(c[7], 3.0), 0.0);
      // two highest notes: the middle (x = 0.5)
      c[1]->upNote()->setPitch(79);
      QCOMPARE(archPeak(o), 0.5);
      delete score;
      }

//---------------------------------------------------------
//   defaultsUnchanged
//    a phrase mark with Shape and Ending None, or a Shape / Ending on a slur that isn't a phrase mark, renders every
//    event and the tempo map as the phrase mark / the slur without them (MuseScore 3 and 4 modes, SSO)
//---------------------------------------------------------

void TestPhraseMark::defaultsUnchanged()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Slur* o = outer(score);
      auto all = [&]() {
            score->doLayout();
            QStringList l;
            l << events(score, nullptr) << "#" << events(score, nullptr, ms3State()) << "#" << events(score, lib);
            for (const auto& t : *score->tempomap())
                  l << QString("tempo %1 %2").arg(t.first).arg(t.second.tempo);
            return l;
            };
      const QStringList slur = all();
      setShape(o, PhraseMark::SHAPE_ARCH, PhraseMark::ENDING_BROAD);     // (a slur: nothing)
      QCOMPARE(all(), slur);
      setShape(o, PhraseMark::SHAPE_NONE, PhraseMark::ENDING_NONE);
      mark(o, true);
      const QStringList plain = all();
      setShape(o, PhraseMark::SHAPE_TOWARD_END, PhraseMark::ENDING_SLIGHT);
      QVERIFY(all() != plain);
      setShape(o, PhraseMark::SHAPE_NONE, PhraseMark::ENDING_NONE);
      QCOMPARE(all(), plain);
      // the settings at 0 dB / 100 %: nothing either
      setShape(o, PhraseMark::SHAPE_ARCH, PhraseMark::ENDING_BROAD);
      score->setMetaTag(Playback::metaTag, "phrasing/shapeAmount=0;phrasing/endingBroad=100");
      QCOMPARE(all(), plain);
      score->setMetaTag(Playback::metaTag, QString());
      delete score;
      }

//---------------------------------------------------------
//   shapeBuiltIn
//    MuseScore 4 and 3 modes: each note's velocity by SoundFont 2's law, the Shape's dB at its place (Toward end: 3 dB x
//    i / 7 on the i-th of the 8 chords); the amount from phrasing/shapeAmount
//---------------------------------------------------------

void TestPhraseMark::shapeBuiltIn()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      mark(outer(score), true);
      for (const SynthesizerState& state : { SynthesizerState(), ms3State() }) {
            score->setMetaTag(Playback::metaTag, QString());
            setShape(outer(score), PhraseMark::SHAPE_NONE, PhraseMark::ENDING_NONE);
            const std::vector<On> before = noteOns(render(score, nullptr, state));
            QCOMPARE(int(before.size()), 8);
            setShape(outer(score), PhraseMark::SHAPE_TOWARD_END, PhraseMark::ENDING_NONE);
            std::vector<On> after = noteOns(render(score, nullptr, state));
            QCOMPARE(int(after.size()), 8);
            for (size_t i = 0; i < 8; ++i)
                  QCOMPARE(after[i].velocity, MarcatoLevel::velocity(before[i].velocity, 3.0 * double(i) / 7.0));
            QVERIFY(after[7].velocity > before[7].velocity);
            score->setMetaTag(Playback::metaTag, "phrasing/shapeAmount=6");
            after = noteOns(render(score, nullptr, state));
            for (size_t i = 0; i < 8; ++i)
                  QCOMPARE(after[i].velocity, MarcatoLevel::velocity(before[i].velocity, 6.0 * double(i) / 7.0));
            }
      score->setMetaTag(Playback::metaTag, QString());
      delete score;
      }

//---------------------------------------------------------
//   shapeLibrary
//    SSO (the violin's Long, on CC1): the Shape plays as a note level, louder by the dynamics CC (or velocity), the first
//    note (x = 0) unchanged, each later one louder than without; the level settings after these tests off, the inner slur
//    removed (the phrase's notes on one technique)
//---------------------------------------------------------

void TestPhraseMark::shapeLibrary()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      score->startCmd();
      score->undoRemoveElement(inner(score));
      score->endCmd();
      mark(outer(score), true);
      score->setMetaTag(Playback::metaTag, "levels/sectionBalance=0;levels/realRange=0;levels/micCompensation=0;levels/context=0;"
                                           "levels/evenLevels=0;levels/calibrated=0;levels/attackLevels=0;slurs/plainPerformance=0");
      const std::vector<On> before = noteOns(render(score, lib), 1);
      QCOMPARE(int(before.size()), 8);
      setShape(outer(score), PhraseMark::SHAPE_TOWARD_END, PhraseMark::ENDING_NONE);
      const std::vector<On> after = noteOns(render(score, lib), 1);
      QCOMPARE(int(after.size()), 8);
      QCOMPARE(after[0].velocity, before[0].velocity);
      QCOMPARE(after[0].cc, before[0].cc);
      for (size_t i = 1; i < 8; ++i) {
            QCOMPARE(after[i].pitch, before[i].pitch);
            QVERIFY2(after[i].velocity > before[i].velocity || after[i].cc > before[i].cc,
                     qPrintable(QString("note %1: velocity %2 -> %3, CC1 %4 -> %5").arg(i).arg(before[i].velocity).arg(after[i].velocity)
                                .arg(before[i].cc).arg(after[i].cc)));
            }
      score->setMetaTag(Playback::metaTag, QString());
      delete score;
      }

//---------------------------------------------------------
//   ending
//    the phrase's last note (D5, tick 3360) ends at 3840: Slight from 2880 (2 beats before) at 120 BPM (2.0) to 90 %
//    (1.8) at 3840, linear, 120 BPM again from 3840; Broad to 80 %; the length and curve from the settings; a phrase
//    shorter than the length from its start
//---------------------------------------------------------

void TestPhraseMark::ending()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      mark(o, true);
      QCOMPARE(PhraseMark::endTick(o), 8 * DIVISION);
      const TempoMap* tm = score->tempomap();
      score->doLayout();
      QCOMPARE(tm->tempo(3360), 2.0);
      setShape(o, PhraseMark::SHAPE_NONE, PhraseMark::ENDING_SLIGHT);
      score->doLayout();
      QCOMPARE(tm->tempo(2879), 2.0);
      QCOMPARE(tm->tempo(2880), 2.0);
      QVERIFY(qAbs(tm->tempo(3360) - 1.9) < 1e-9);                     // half way
      QVERIFY(qAbs(tm->tempo(3839) - (2.0 - 0.2 * (3840 - 60 - 2880) / 960.0)) < 1e-9);   // (the last 32nd's point)
      QCOMPARE(tm->tempo(3840), 2.0);                                     // back after the phrase
      QCOMPARE(tm->tempo(5000), 2.0);
      // the phrase takes longer: 2 beats at 120 BPM are 1 s, now more
      QVERIFY(tm->tick2time(3840) - tm->tick2time(2880) > 1.04);

      setShape(o, PhraseMark::SHAPE_NONE, PhraseMark::ENDING_BROAD);
      score->doLayout();
      QVERIFY(qAbs(tm->tempo(3360) - 1.8) < 1e-9);
      score->setMetaTag(Playback::metaTag, "phrasing/endingBroad=50;phrasing/endingLength=4;phrasing/endingCurve=2");
      score->doLayout();
      QCOMPARE(tm->tempo(1919), 2.0);
      QCOMPARE(tm->tempo(1920), 2.0);
      QVERIFY(qAbs(tm->tempo(2880) - (2.0 - 1.0 * (1.0 - std::cos(M_PI / 4.0)))) < 1e-9);   // ease-in, half way
      QCOMPARE(tm->tempo(3840), 2.0);
      score->setMetaTag(Playback::metaTag, "phrasing/endingLength=16");                  // longer than the phrase
      score->doLayout();
      QCOMPARE(tm->tempo(0), 2.0);
      QVERIFY(qAbs(tm->tempo(1920) - 1.8) < 1e-9);                     // half of the phrase's 8 beats
      score->setMetaTag(Playback::metaTag, QString());
      delete score;
      }

//---------------------------------------------------------
//   endingWrittenWins
//    a rit. line overlapping the Ending, or a tempo marking inside it: no Ending; written changes elsewhere leave it
//---------------------------------------------------------

void TestPhraseMark::endingWrittenWins()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      mark(o, true);
      setShape(o, PhraseMark::SHAPE_NONE, PhraseMark::ENDING_SLIGHT);
      const TempoMap* tm = score->tempomap();
      Measure* m2 = score->firstMeasure()->nextMeasure();
      Measure* m3 = m2->nextMeasure();

      TextLine* line = new TextLine(score, true);
      line->setBeginText("rit.");
      line->setTrack(0);
      line->setTrack2(0);
      line->setTick(m2->tick() + Fraction(1, 2));             // beats 3-4 of measure 2: the Ending's time
      line->setTick2(m3->tick());
      score->startCmd();
      score->undoAddElement(line);
      score->endCmd();
      score->doLayout();
      QVERIFY(qAbs(tm->tempo(3840) - 1.5) < 1e-9);           // the rit.'s 75 %, staying
      QVERIFY(qAbs(tm->tempo(3360) - 1.75) < 1e-9);          // the rit. alone
      score->undoRedo(true, 0);
      score->doLayout();
      QVERIFY(qAbs(tm->tempo(3360) - 1.9) < 1e-9);           // the Ending again

      // a tempo marking on beat 4 of measure 2 (inside the Ending)
      Segment* seg = m2->findSegment(SegmentType::ChordRest, m2->tick() + Fraction(3, 4));
      QVERIFY(seg);
      TempoText* tt = new TempoText(score);
      tt->setTempo(1.0);
      tt->setXmlText("<sym>metNoteQuarterUp</sym> = 60");
      tt->setTrack(0);
      tt->setParent(seg);
      score->startCmd();
      score->undoAddElement(tt);
      score->endCmd();
      score->doLayout();
      QCOMPARE(tm->tempo(3000), 2.0);
      QCOMPARE(tm->tempo(3360), 1.0);
      QCOMPARE(tm->tempo(3840), 1.0);
      delete score;
      }

//---------------------------------------------------------
//   shapeEndingSaveLoad
//    "shape" / "ending" in the phrase mark's metaTag entry only when set; read back; saved again the same; the rest of
//    the file as without them
//---------------------------------------------------------

void TestPhraseMark::shapeEndingSaveLoad()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      mark(o, true);
      QVERIFY(saveScore(score, "phraseshape-none.mscx"));
      QVERIFY(fileText("phraseshape-none.mscx").contains("[{&quot;tick&quot;:0,&quot;tick2&quot;:3360,&quot;track&quot;:0,&quot;track2&quot;:0}]"));
      setShape(o, PhraseMark::SHAPE_ARCH, PhraseMark::ENDING_BROAD);
      QVERIFY(saveScore(score, "phraseshape-saved.mscx"));
      const QString text = fileText("phraseshape-saved.mscx");
      QVERIFY(text.contains("<metaTag name=\"phraseMarks\">[{&quot;ending&quot;:2,&quot;shape&quot;:1,&quot;tick&quot;:0,&quot;tick2&quot;:3360,&quot;track&quot;:0,&quot;track2&quot;:0}]</metaTag>"));
      QVERIFY(!text.contains("phraseShape"));
      QVERIFY(!text.contains("phraseEnding"));
      QString without = text;
      without.remove(QRegularExpression("\\s*<metaTag name=\"phraseMarks\">[^<]*</metaTag>"));
      QString none = fileText("phraseshape-none.mscx");
      none.remove(QRegularExpression("\\s*<metaTag name=\"phraseMarks\">[^<]*</metaTag>"));
      QCOMPARE(without, none);

      MasterScore* again = readCreatedScore("phraseshape-saved.mscx");
      QVERIFY(again);
      QVERIFY(outer(again)->phraseMark());
      QCOMPARE(outer(again)->phraseShape(), int(PhraseMark::SHAPE_ARCH));
      QCOMPARE(outer(again)->phraseEnding(), int(PhraseMark::ENDING_BROAD));
      QCOMPARE(inner(again)->phraseShape(), 0);
      again->doLayout();
      QVERIFY(qAbs(again->tempomap()->tempo(3360) - 1.8) < 1e-9);
      QVERIFY(saveScore(again, "phraseshape-saved2.mscx"));
      QCOMPARE(fileText("phraseshape-saved2.mscx"), text);
      delete again;
      delete score;
      }

//---------------------------------------------------------
//   shapeEndingUndo
//    through a segment as the Inspector sets them; undo / redo; a part's linked copy follows
//---------------------------------------------------------

void TestPhraseMark::shapeEndingUndo()
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
      Slur* o = outer(score);
      Slur* partSlur = outer(nscore);
      mark(o, true);
      score->doLayout();
      QVERIFY(!o->spannerSegments().empty());
      score->startCmd();
      o->frontSegment()->undoChangeProperty(Pid::PHRASE_SHAPE, int(PhraseMark::SHAPE_TOWARD_START), PropertyFlags::NOSTYLE);
      o->frontSegment()->undoChangeProperty(Pid::PHRASE_ENDING, int(PhraseMark::ENDING_SLIGHT), PropertyFlags::NOSTYLE);
      score->endCmd();
      QCOMPARE(o->phraseShape(), int(PhraseMark::SHAPE_TOWARD_START));
      QCOMPARE(o->frontSegment()->getProperty(Pid::PHRASE_ENDING).toInt(), int(PhraseMark::ENDING_SLIGHT));
      QCOMPARE(partSlur->phraseShape(), int(PhraseMark::SHAPE_TOWARD_START));
      QCOMPARE(partSlur->phraseEnding(), int(PhraseMark::ENDING_SLIGHT));
      score->doLayout();
      QVERIFY(qAbs(score->tempomap()->tempo(3360) - 1.9) < 1e-9);
      score->undoRedo(true, 0);
      QCOMPARE(o->phraseShape(), 0);
      QCOMPARE(o->phraseEnding(), 0);
      QCOMPARE(partSlur->phraseEnding(), 0);
      QVERIFY(o->phraseMark());
      score->doLayout();
      QCOMPARE(score->tempomap()->tempo(3360), 2.0);
      score->undoRedo(false, 0);
      QCOMPARE(o->phraseShape(), int(PhraseMark::SHAPE_TOWARD_START));
      QCOMPARE(partSlur->phraseShape(), int(PhraseMark::SHAPE_TOWARD_START));
      score->doLayout();
      QVERIFY(qAbs(score->tempomap()->tempo(3360) - 1.9) < 1e-9);
      QCOMPARE(o->propertyDefault(Pid::PHRASE_SHAPE).toInt(), 0);
      QCOMPARE(o->propertyDefault(Pid::PHRASE_ENDING).toInt(), 0);
      delete score;
      }

//---------------------------------------------------------
//   shapeEndingCopyPaste
//    measures 1-2 pasted over 3-4: the pasted phrase mark keeps its Shape and Ending
//---------------------------------------------------------

void TestPhraseMark::shapeEndingCopyPaste()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      mark(outer(score), true);
      setShape(outer(score), PhraseMark::SHAPE_ARCH, PhraseMark::ENDING_BROAD);
      Measure* m1 = score->firstMeasure();
      Measure* m2 = m1->nextMeasure();
      Measure* m3 = m2->nextMeasure();
      score->select(m1);
      score->select(m2, SelectType::RANGE, 0);
      QMimeData* mimeData = new QMimeData;
      mimeData->setData(score->selection().mimeType(), score->selection().mimeData());
      QVERIFY(QString::fromUtf8(score->selection().mimeData()).contains("<phraseShape>1</phraseShape>"));
      score->select(m3->first(SegmentType::ChordRest)->element(0));
      score->startCmd();
      score->cmdPaste(mimeData, 0);
      score->endCmd();
      delete mimeData;
      std::vector<Slur*> pasted = slursAt(score, m3->tick().ticks());
      QCOMPARE(int(pasted.size()), 1);
      QVERIFY(pasted[0]->phraseMark());
      QCOMPARE(pasted[0]->phraseShape(), int(PhraseMark::SHAPE_ARCH));
      QCOMPARE(pasted[0]->phraseEnding(), int(PhraseMark::ENDING_BROAD));
      std::vector<Slur*> pastedInner = slursAt(score, m3->tick().ticks() + 2 * DIVISION);
      QCOMPARE(int(pastedInner.size()), 1);
      QCOMPARE(pastedInner[0]->phraseShape(), 0);
      delete score;
      }

QTEST_MAIN(TestPhraseMark)
#include "tst_phrasemark.moc"
