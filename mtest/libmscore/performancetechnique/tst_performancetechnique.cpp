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
#include "libmscore/glissando.h"
#include "libmscore/instrtemplate.h"
#include "libmscore/instrument.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/performancetechnique.h"
#include "libmscore/rendermidi.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/stafftext.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/undo.h"

#define DIR QString("libmscore/performancetechnique/")

using namespace Ms;
using PerformanceTechnique::Attack;
using PerformanceTechnique::Transition;

//---------------------------------------------------------
//   TestPerformanceTechnique
//    performancetechnique.musicxml: a Violin made Violins (SSO's Violins 1; under staff text "performance" its Performance
//    patch, whose map lists the techniques by velocity), 120 BPM: C5 | D5 E5 F5 slurred || G5 accented | A5 B5
//    slurred, a glissando between them | rest
//---------------------------------------------------------

class TestPerformanceTechnique : public QObject, public MTest
      {
      Q_OBJECT

      std::shared_ptr<SoundLib::Library> sso()
            {
            QString error;
            auto lib = SoundLib::Library::load(root + "/../share/soundlibraries/Spitfire Symphony Orchestra.xml", &error);
            if (!lib)
                  qWarning("%s", qPrintable(error));
            return lib;
            }
      MasterScore* score(bool performance = true)
            {
            MasterScore* s = readScore(DIR + "performancetechnique.musicxml");
            if (!s)
                  return nullptr;
            // the section (MusicXML's strings.group is ambiguous; "Violin" is SSO's Solo Violin)
            s->parts().front()->setInstrument(Instrument::fromTemplate(searchTemplate("violins")));
            s->rebuildMidiMapping();
            // a portamento line: the patch glides (a chromatic one plays its steps as written notes)
            for (Note* n : notes(s))
                  for (Spanner* sp : n->spannerFor())
                        if (sp->isGlissando())
                              toGlissando(sp)->setGlissandoStyle(GlissandoStyle::PORTAMENTO);
            if (performance) {
                  StaffText* t = new StaffText(s);
                  t->setXmlText("performance");
                  t->setTrack(0);
                  t->setParent(s->firstMeasure()->first(SegmentType::ChordRest));
                  s->startCmd();
                  s->undoAddElement(t);
                  s->endCmd();
                  }
            return s;
            }
      static std::vector<Note*> notes(Score* score)
            {
            std::vector<Note*> list;
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->element(0) && s->element(0)->isChord())
                        list.push_back(toChord(s->element(0))->upNote());
            return list;
            }
      // every selected note at once, one undo step (as the Inspector does)
      static void set(std::vector<Note*> list, Pid pid, int value)
            {
            Score* s = list.front()->score();
            s->startCmd();
            for (Note* n : list)
                  n->undoChangeProperty(pid, value);
            s->endCmd();
            }
      struct Key { int on = -1; int off = -1; int velocity = -1; };
      // each key's first note: note-on tick, velocity and note-off tick (the library's events, not its switches)
      static std::map<int, Key> render(MasterScore* score, std::shared_ptr<SoundLib::Library> library, QStringList* all = nullptr)
            {
            SoundLib::setCurrent(library);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            SoundLib::setCurrent(nullptr);
            SoundLib::setOutput(SoundLib::Output::MIDI);
            std::map<int, Key> keys;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (all)
                        *all << QString("%1 %2 %3 %4 %5 %6").arg(te.first).arg(e.type()).arg(e.dataA()).arg(e.dataB())
                                .arg(e.librarySwitch()).arg(e.libraryPatch());
                  if (!e.isExternal() || e.librarySwitch() || (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF))
                        continue;
                  Key& k = keys[e.pitch()];
                  if (e.type() == ME_NOTEON && e.velo() > 0) {
                        if (k.on < 0) {
                              k.on = te.first;
                              k.velocity = e.velo();
                              }
                        }
                  else if (k.on >= 0 && k.off < 0)
                        k.off = te.first;
                  }
            return keys;
            }
      // ms at 120 BPM (480 ticks a quarter)
      static double ms(int ticks) { return ticks * 500.0 / 480.0; }
      static QString fileText(const QString& name)
            {
            QFile f(name);
            if (!f.open(QIODevice::ReadOnly))
                  return QString();
            return QString::fromUtf8(f.readAll());
            }

   private slots:
      void initTestCase() { initMTest(); }
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI); }
      void map();
      void autoTechniques();
      void overrides();
      void withoutPerformance();
      void saveLoad();
      void undoRedo();
      void copyPaste();
      void parts();
      };

//---------------------------------------------------------
//   map
//    the five string sections' Performance Legato list the techniques; nothing else does
//---------------------------------------------------------

void TestPerformanceTechnique::map()
      {
      auto lib = sso();
      QVERIFY(lib);
      int with = 0;
      const SoundLib::Articulation* violins = nullptr;
      for (const auto* list : { &lib->instruments, &lib->otherPatches })
            for (const auto& i : *list)
                  for (const auto& a : i.articulations)
                        if (!a.attacks.empty()) {
                              ++with;
                              QVERIFY2(i.name.endsWith(" - Performance") && a.name == "Legato", qPrintable(i.name));
                              if (i.name == "Violins 1 - Performance")
                                    violins = &a;
                              }
      QCOMPARE(with, 5);
      QVERIFY(violins);
      QCOMPARE(violins->attack("smooth")->velocity, 5);
      QCOMPARE(violins->attack("spiccato")->low, 10);
      QCOMPARE(violins->attack("spiccato")->high, 116);
      QCOMPARE(violins->attack("accented")->velocity, 122);
      QCOMPARE(violins->transition("portamento")->velocity, 10);
      QCOMPARE(violins->transition("fingered")->velocity, 69);
      QCOMPARE(violins->transition("bowed")->velocity, 93);
      QVERIFY(!violins->attack("bowed"));
      QCOMPARE(violins->reattackVelocity, 106);
      QCOMPARE(violins->reattackGapMs, 54.0);
      QCOMPARE(violins->overlapMs, 24.0);
      }

//---------------------------------------------------------
//   autoTechniques
//    Auto plays the notation: C5 after silence spiccato (112); D5 starts the slur right after C5: a re-attack (106),
//    C5 ending 54 ms before it; E5 and F5 bowed (93), the note before ending 24 ms after them; G5's accent accented
//    (122), F5 ending 54 ms before it; A5 right after it a re-attack (106); B5 after the glissando portamento (10),
//    A5 ending 24 ms after it
//---------------------------------------------------------

void TestPerformanceTechnique::autoTechniques()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      std::map<int, Key> k = render(s, lib);
      QVERIFY(k.count(72) && k.count(83));
      QCOMPARE(k[72].velocity, 112);
      QCOMPARE(k[74].velocity, 106);
      QCOMPARE(k[76].velocity, 93);
      QCOMPARE(k[77].velocity, 93);
      QCOMPARE(k[79].velocity, 122);
      QCOMPARE(k[81].velocity, 106);          // A5 right after G5: a re-attack
      QCOMPARE(k[83].velocity, 10);
      auto within = [](double a, double b) { return std::abs(a - b) < 2.5; };   // a tick each way, rounding
      QVERIFY2(within(ms(k[74].on - k[72].off), 54), qPrintable(QString::number(ms(k[74].on - k[72].off))));
      QVERIFY2(within(ms(k[74].off - k[76].on), 24), qPrintable(QString::number(ms(k[74].off - k[76].on))));
      QVERIFY2(within(ms(k[76].off - k[77].on), 24), qPrintable(QString::number(ms(k[76].off - k[77].on))));
      QVERIFY2(within(ms(k[79].on - k[77].off), 54), qPrintable(QString::number(ms(k[79].on - k[77].off))));
      QVERIFY2(within(ms(k[81].on - k[79].off), 54), qPrintable(QString::number(ms(k[81].on - k[79].off))));
      QVERIFY2(within(ms(k[81].off - k[83].on), 24), qPrintable(QString::number(ms(k[81].off - k[83].on))));
      delete s;
      }

//---------------------------------------------------------
//   overrides
//    the Inspector's choice on several notes at once: C5 and G5 smooth (5); E5 fingered (69); G5's transition
//    fingered: it joins F5 (no slur) by a fingered transition, F5 ending 24 ms after it, its attack unused; D5's
//    portamento (10)
//---------------------------------------------------------

void TestPerformanceTechnique::overrides()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      std::vector<Note*> n = notes(s);
      QCOMPARE(int(n.size()), 7);
      set({ n[0], n[4] }, Pid::PERFORMANCE_ATTACK, int(Attack::SMOOTH));
      set({ n[2], n[4] }, Pid::PERFORMANCE_TRANSITION, int(Transition::FINGERED));
      set({ n[1] }, Pid::PERFORMANCE_TRANSITION, int(Transition::PORTAMENTO));
      std::map<int, Key> k = render(s, lib);
      QCOMPARE(k[72].velocity, 5);
      QCOMPARE(k[74].velocity, 10);
      QCOMPARE(k[76].velocity, 69);
      QCOMPARE(k[77].velocity, 93);
      QCOMPARE(k[79].velocity, 69);
      auto within = [](double a, double b) { return std::abs(a - b) < 2.5; };
      QVERIFY2(within(ms(k[72].off - k[74].on), 24), qPrintable(QString::number(ms(k[72].off - k[74].on))));
      QVERIFY2(within(ms(k[77].off - k[79].on), 24), qPrintable(QString::number(ms(k[77].off - k[79].on))));
      // a transition with no note before on the patch (after the rest, the score's first note): its attack
      set({ n[0] }, Pid::PERFORMANCE_TRANSITION, int(Transition::BOWED));
      QCOMPARE(render(s, lib)[72].velocity, 5);
      delete s;
      }

//---------------------------------------------------------
//   withoutPerformance
//    without staff text "performance" the techniques change nothing: the same events with and without them
//---------------------------------------------------------

void TestPerformanceTechnique::withoutPerformance()
      {
      auto lib = sso();
      MasterScore* s = score(false);
      QVERIFY(lib && s);
      QStringList before;
      render(s, lib, &before);
      std::vector<Note*> n = notes(s);
      set(n, Pid::PERFORMANCE_ATTACK, int(Attack::ACCENTED));
      set(n, Pid::PERFORMANCE_TRANSITION, int(Transition::PORTAMENTO));
      QStringList after;
      render(s, lib, &after);
      QCOMPARE(after, before);
      delete s;
      }

//---------------------------------------------------------
//   saveLoad
//    the choices are in the metaTag, not in the note's XML; read back onto the notes; saved again the same;
//    without choices no metaTag
//---------------------------------------------------------

void TestPerformanceTechnique::saveLoad()
      {
      MasterScore* s = score();
      QVERIFY(s);
      QVERIFY(saveScore(s, "performancetechnique-none.mscx"));
      QVERIFY(!fileText("performancetechnique-none.mscx").contains("performanceTechniques"));
      std::vector<Note*> n = notes(s);
      set({ n[0] }, Pid::PERFORMANCE_ATTACK, int(Attack::ACCENTED));
      set({ n[2] }, Pid::PERFORMANCE_TRANSITION, int(Transition::FINGERED));
      QVERIFY(saveScore(s, "performancetechnique-saved.mscx"));
      const QString text = fileText("performancetechnique-saved.mscx");
      QVERIFY2(text.contains("<metaTag name=\"performanceTechniques\">[{&quot;attack&quot;:&quot;accented&quot;,&quot;pitch&quot;:72,"
                             "&quot;tick&quot;:0,&quot;track&quot;:0},{&quot;pitch&quot;:76,&quot;tick&quot;:960,"
                             "&quot;track&quot;:0,&quot;transition&quot;:&quot;fingered&quot;}]</metaTag>"),
               qPrintable(text.section("<metaTag name=\"performanceTechniques\">", 1).left(300)));
      QVERIFY(!text.contains("performanceAttack") && !text.contains("performanceTransition"));
      QString without = text;
      without.remove(QRegularExpression("\\s*<metaTag name=\"performanceTechniques\">[^<]*</metaTag>"));
      QCOMPARE(without, fileText("performancetechnique-none.mscx"));

      MasterScore* again = readCreatedScore("performancetechnique-saved.mscx");
      QVERIFY(again);
      std::vector<Note*> m = notes(again);
      QCOMPARE(m[0]->performanceAttack(), Attack::ACCENTED);
      QCOMPARE(m[0]->performanceTransition(), Transition::AUTO);
      QCOMPARE(m[2]->performanceTransition(), Transition::FINGERED);
      QCOMPARE(m[1]->performanceAttack(), Attack::AUTO);
      QVERIFY(again->metaTag(PerformanceTechnique::metaTag).isEmpty());
      QVERIFY(saveScore(again, "performancetechnique-saved2.mscx"));
      QCOMPARE(fileText("performancetechnique-saved2.mscx"), text);
      delete again;
      delete s;
      }

//---------------------------------------------------------
//   undoRedo
//    several notes in one step, undone and redone together; reset to Auto
//---------------------------------------------------------

void TestPerformanceTechnique::undoRedo()
      {
      MasterScore* s = score();
      QVERIFY(s);
      std::vector<Note*> n = notes(s);
      QCOMPARE(n[0]->propertyDefault(Pid::PERFORMANCE_ATTACK).toInt(), 0);
      set({ n[0], n[1], n[2] }, Pid::PERFORMANCE_ATTACK, int(Attack::SPICCATO));
      for (int i : { 0, 1, 2 })
            QCOMPARE(n[size_t(i)]->performanceAttack(), Attack::SPICCATO);
      s->undoRedo(true, 0);
      for (int i : { 0, 1, 2 })
            QCOMPARE(n[size_t(i)]->performanceAttack(), Attack::AUTO);
      s->undoRedo(false, 0);
      QCOMPARE(n[2]->performanceAttack(), Attack::SPICCATO);
      set({ n[3] }, Pid::PERFORMANCE_TRANSITION, 99);          // out of range: the last
      QCOMPARE(n[3]->performanceTransition(), Transition::BOWED);
      s->startCmd();
      n[2]->undoResetProperty(Pid::PERFORMANCE_ATTACK);
      s->endCmd();
      QCOMPARE(n[2]->performanceAttack(), Attack::AUTO);
      delete s;
      }

//---------------------------------------------------------
//   copyPaste
//    measure 1 pasted over measure 2: the pasted notes keep their choices
//---------------------------------------------------------

void TestPerformanceTechnique::copyPaste()
      {
      MasterScore* s = score();
      QVERIFY(s);
      std::vector<Note*> n = notes(s);
      set({ n[1] }, Pid::PERFORMANCE_ATTACK, int(Attack::SMOOTH));
      set({ n[2] }, Pid::PERFORMANCE_TRANSITION, int(Transition::PORTAMENTO));
      Measure* m1 = s->firstMeasure();
      Measure* m2 = m1->nextMeasure();
      s->select(m1, SelectType::RANGE, 0);
      QVERIFY(s->selection().canCopy());
      QMimeData* mimeData = new QMimeData;
      mimeData->setData(s->selection().mimeType(), s->selection().mimeData());
      s->select(m2->first(SegmentType::ChordRest)->element(0));
      s->startCmd();
      s->cmdPaste(mimeData, 0);
      s->endCmd();
      delete mimeData;
      std::vector<Note*> p = notes(s);
      QCOMPARE(int(p.size()), 8);
      QCOMPARE(p[5]->performanceAttack(), Attack::SMOOTH);
      QCOMPARE(p[6]->performanceTransition(), Transition::PORTAMENTO);
      QCOMPARE(p[4]->performanceAttack(), Attack::AUTO);
      delete s;
      }

//---------------------------------------------------------
//   parts
//    a part's notes follow the score's (linked), and the part keeps them through a save and load
//---------------------------------------------------------

void TestPerformanceTechnique::parts()
      {
      MasterScore* s = score();
      QVERIFY(s);
      QList<Part*> parts;
      parts.append(s->parts().at(0));
      Score* nscore = new Score(s);
      Excerpt* ex = new Excerpt(s);
      ex->setPartScore(nscore);
      ex->setParts(parts);
      ex->setTitle(parts.front()->partName());
      Excerpt::createExcerpt(ex);
      s->excerpts().append(ex);

      Note* partNote = notes(nscore)[4];
      set({ notes(s)[4] }, Pid::PERFORMANCE_ATTACK, int(Attack::ACCENTED));
      QCOMPARE(partNote->performanceAttack(), Attack::ACCENTED);
      s->undoRedo(true, 0);
      QCOMPARE(partNote->performanceAttack(), Attack::AUTO);
      s->undoRedo(false, 0);

      QVERIFY(saveScore(s, "performancetechnique-parts.mscx"));
      QCOMPARE(fileText("performancetechnique-parts.mscx").count("<metaTag name=\"performanceTechniques\">"), 2);
      MasterScore* again = readCreatedScore("performancetechnique-parts.mscx");
      QVERIFY(again);
      QCOMPARE(int(again->excerpts().size()), 1);
      Score* part = again->excerpts().front()->partScore();
      QCOMPARE(notes(again)[4]->performanceAttack(), Attack::ACCENTED);
      QCOMPARE(notes(part)[4]->performanceAttack(), Attack::ACCENTED);
      QCOMPARE(notes(part)[3]->performanceAttack(), Attack::AUTO);
      delete again;
      delete s;
      }

QTEST_MAIN(TestPerformanceTechnique)
#include "tst_performancetechnique.moc"
