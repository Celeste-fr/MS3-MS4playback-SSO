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
#include "libmscore/articulation.h"
#include "libmscore/automation.h"
#include "libmscore/chord.h"
#include "libmscore/excerpt.h"
#include "libmscore/glissando.h"
#include "libmscore/instrtemplate.h"
#include "libmscore/instrument.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/notelane.h"
#include "libmscore/part.h"
#include "libmscore/playbackaudit.h"
#include "libmscore/performancetechnique.h"
#include "libmscore/playbacksettings.h"
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
      MasterScore* score(bool performance = true, const QString& file = "performancetechnique.musicxml")
            {
            MasterScore* s = readScore(DIR + file);
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
            else        // the All techniques patch: [slurs] performance (on by default since 2026-10-10) off
                  s->setMetaTag(Playback::metaTag, "slurs/performance=0");
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
      static void set(std::vector<Note*> list, Pid pid, const QVariant& value)
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
      // duplicate.musicxml (a Violin, 4/4): C5 [E5 G5] A5(half) | C5 [E5 G5 B5] A5~A5 | D5 D5 D5 D5 | D5 D5 D5~D5 | rest
      MasterScore* duplicateScore() { return readScore(DIR + "duplicate.musicxml"); }
      // the note of the chord at tick on track 0, idx from the bottom
      static Note* at(Score* score, int tick, int idx = 0)
            {
            Segment* s = score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
            Element* e = s && s->tick().ticks() == tick ? s->element(0) : nullptr;
            if (!e || !e->isChord() || idx >= int(toChord(e)->notes().size()))
                  return nullptr;
            return toChord(e)->notes()[size_t(idx)];
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
      void ownValues();
      void ownVelocityElsewhere();
      void ownSaved();
      void trace();
      void lanes();
      void avoidBadSamples();
      void onPerformancePatch();
      void ownTechnique();
      void duplicateNoteValues();
      void duplicateAutomation();
      void slurTransitions();
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
//    C5 ending 54 ms before it; E5 and F5 fingered (69; [slurs] transitions 0: bowed, 93), the note before ending 24 ms
//    after them; G5's accent accented
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
      QCOMPARE(k[76].velocity, 69);           // (slurs/transitions: fingered under a slur; 0: bowed, 93)
      QCOMPARE(k[77].velocity, 69);
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
//   ownValues
//    the Velocity and Join lanes' values: D5 joined -20 ms (a gap up to legatoGap 39: a transition, bowed 93, C5
//    ending 20 ms before it); E5 joined -60 ms (longer: a re-attack 106, D5 ending 60 ms before it); F5 at velocity 50,
//    joined +40 (E5 ending 40 ms after it); C5's join unused (no note before: its attack, spiccato 112)
//---------------------------------------------------------

void TestPerformanceTechnique::ownValues()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      std::vector<Note*> n = notes(s);
      QCOMPARE(n[0]->propertyDefault(Pid::LIBRARY_JOIN).toInt(), PerformanceTechnique::JOIN_AUTO);
      set({ n[0] }, Pid::LIBRARY_JOIN, 30);
      set({ n[1] }, Pid::LIBRARY_JOIN, -20);
      set({ n[2] }, Pid::LIBRARY_JOIN, -60);
      set({ n[3] }, Pid::LIBRARY_JOIN, 40);
      set({ n[3] }, Pid::LIBRARY_VELOCITY, 50);
      std::map<int, Key> k = render(s, lib);
      QCOMPARE(k[72].velocity, 112);
      QCOMPARE(k[74].velocity, 93);
      QCOMPARE(k[76].velocity, 106);
      QCOMPARE(k[77].velocity, 50);
      auto within = [](double a, double b) { return std::abs(a - b) < 2.5; };
      QVERIFY2(within(ms(k[74].on - k[72].off), 20), qPrintable(QString::number(ms(k[74].on - k[72].off))));
      QVERIFY2(within(ms(k[76].on - k[74].off), 60), qPrintable(QString::number(ms(k[76].on - k[74].off))));
      QVERIFY2(within(ms(k[76].off - k[77].on), 40), qPrintable(QString::number(ms(k[76].off - k[77].on))));
      // the edge: -39 still a transition, -40 a re-attack
      set({ n[1] }, Pid::LIBRARY_JOIN, -39);
      QCOMPARE(render(s, lib)[74].velocity, 93);
      set({ n[1] }, Pid::LIBRARY_JOIN, -40);
      QCOMPARE(render(s, lib)[74].velocity, 106);
      // its own velocity after a technique: the velocity wins; a technique chosen after it: back to Auto (smooth: 5),
      // undone together
      set({ n[4] }, Pid::PERFORMANCE_ATTACK, int(Attack::SMOOTH));
      set({ n[4] }, Pid::LIBRARY_VELOCITY, 30);
      QCOMPARE(render(s, lib)[79].velocity, 30);
      set({ n[4] }, Pid::PERFORMANCE_ATTACK, int(Attack::ACCENTED));
      set({ n[4] }, Pid::PERFORMANCE_ATTACK, int(Attack::SMOOTH));
      QCOMPARE(n[4]->libraryVelocity(), 0);
      QCOMPARE(render(s, lib)[79].velocity, 5);
      set({ n[1] }, Pid::LIBRARY_JOIN, -20);
      set({ n[1] }, Pid::PERFORMANCE_TRANSITION, int(Transition::FINGERED));
      QCOMPARE(n[1]->libraryJoin(), PerformanceTechnique::JOIN_AUTO);
      s->undoRedo(true, 0);
      QCOMPARE(n[1]->libraryJoin(), -20);
      QCOMPARE(n[1]->performanceTransition(), Transition::AUTO);
      delete s;
      }

//---------------------------------------------------------
//   ownVelocityElsewhere
//    without "performance" ([slurs] performance off: the All techniques patch) a note's own velocity is sent too; its
//    join changes nothing
//---------------------------------------------------------

void TestPerformanceTechnique::ownVelocityElsewhere()
      {
      auto lib = sso();
      MasterScore* s = score(false);
      QVERIFY(lib && s);
      std::vector<Note*> n = notes(s);
      std::map<int, Key> before = render(s, lib);
      set({ n[0] }, Pid::LIBRARY_VELOCITY, 77);
      set({ n[1], n[2] }, Pid::LIBRARY_JOIN, -100);
      std::map<int, Key> after = render(s, lib);
      QCOMPARE(after[72].velocity, 77);
      for (int p : { 74, 76, 77 }) {
            QCOMPARE(after[p].velocity, before[p].velocity);
            QCOMPARE(after[p].on, before[p].on);
            QCOMPARE(after[p].off, before[p].off);
            }
      QCOMPARE(after[72].on, before[72].on);
      QCOMPARE(after[72].off, before[72].off);
      delete s;
      }

//---------------------------------------------------------
//   ownSaved
//    velocity and join in the metaTag, read back; a note with only them is written
//---------------------------------------------------------

void TestPerformanceTechnique::ownSaved()
      {
      MasterScore* s = score();
      QVERIFY(s);
      std::vector<Note*> n = notes(s);
      set({ n[1] }, Pid::LIBRARY_VELOCITY, 88);
      set({ n[2] }, Pid::LIBRARY_JOIN, -45);
      QVERIFY(saveScore(s, "performancetechnique-own.mscx"));
      const QString text = fileText("performancetechnique-own.mscx");
      QVERIFY2(text.contains("&quot;velocity&quot;:88") && text.contains("&quot;join&quot;:-45"),
               qPrintable(text.section("<metaTag name=\"performanceTechniques\">", 1).left(300)));
      QVERIFY(!text.contains("libraryVelocity") && !text.contains("libraryJoin"));
      MasterScore* again = readCreatedScore("performancetechnique-own.mscx");
      QVERIFY(again);
      std::vector<Note*> m = notes(again);
      QCOMPARE(m[1]->libraryVelocity(), 88);
      QCOMPARE(m[1]->libraryJoin(), PerformanceTechnique::JOIN_AUTO);
      QCOMPARE(m[2]->libraryJoin(), -45);
      QCOMPARE(m[2]->libraryVelocity(), 0);
      QCOMPARE(m[0]->libraryJoin(), PerformanceTechnique::JOIN_AUTO);
      delete again;
      delete s;
      }

//---------------------------------------------------------
//   trace
//    what the lanes read (MidiRenderer::LibTrace): each note's velocity as sent, attack or transition, its join; a
//    technique on velocity (the All techniques patch's Spiccato, staccato notes) its velocity at ppp … fff, rising
//---------------------------------------------------------

void TestPerformanceTechnique::trace()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      EventMap events;
      std::vector<MidiRenderer::LibTrace> t;
      PlaybackAudit::render(s, &events, &t);
      std::map<int, MidiRenderer::LibTrace> byPitch;
      for (const MidiRenderer::LibTrace& e : t)
            byPitch.emplace(e.note->pitch(), e);
      QCOMPARE(byPitch[72].velocity, 112);
      QCOMPARE(byPitch[72].performance, MidiRenderer::LibPerformance::ATTACK);
      QCOMPARE(byPitch[72].joinMs, PerformanceTechnique::JOIN_AUTO);
      QCOMPARE(byPitch[74].performance, MidiRenderer::LibPerformance::ATTACK);
      QCOMPARE(byPitch[74].joinMs, -54);
      QCOMPARE(byPitch[76].velocity, 69);       // (fingered: [slurs] transitions)
      QCOMPARE(byPitch[76].performance, MidiRenderer::LibPerformance::TRANSITION);
      QCOMPARE(byPitch[76].joinMs, 24);
      QVERIFY(byPitch[76].dynamicVelocities.empty());           // (dynamics on the controller)
      delete s;

      // without "performance", staccato: the Spiccato (on velocity)
      MasterScore* st = score(false);
      QVERIFY(st);
      for (Note* n : notes(st)) {
            Articulation* a = new Articulation(st);
            a->setSymId(SymId::articStaccatoAbove);
            a->setTrack(n->track());
            a->setParent(n->chord());
            st->startCmd();
            st->undoAddElement(a);
            st->endCmd();
            }
      t.clear();
      events.clear();
      PlaybackAudit::render(st, &events, &t);
      SoundLib::setCurrent(nullptr);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      QVERIFY(!t.empty());
      const MidiRenderer::LibTrace& e = t.front();
      QCOMPARE(e.performance, MidiRenderer::LibPerformance::NONE);
      QVERIFY2(e.dynamicVelocities.size() == 8, qPrintable(QString("%1 %2").arg(e.patch->name).arg(e.dynamicVelocities.size())));
      for (size_t i = 1; i < 8; ++i)
            QVERIFY(e.dynamicVelocities[i] >= e.dynamicVelocities[i - 1]);
      QVERIFY(e.dynamicVelocities.front() < e.dynamicVelocities.back());
      delete st;
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
      QCOMPARE(k[77].velocity, 69);           // (Auto under a slur: fingered, [slurs] transitions)
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
//    without staff text "performance" ([slurs] performance off) the techniques change nothing: the same events with
//    and without them
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

//---------------------------------------------------------
//   lanes
//    what the Velocity and Join lanes show (NoteLane): the notes by tick; a Performance note's bands, the attacks' or,
//    joined by transition, the transitions'; a join down to -39 ms (legatoGap) still a transition; an own join
//    rendered; the Spiccato's dynamics bands, ppp … fff, contiguous over 1-127
//---------------------------------------------------------

void TestPerformanceTechnique::lanes()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      auto names = [](const std::vector<NoteLane::Band>& bands) {
            QStringList l;
            for (const NoteLane::Band& b : bands)
                  l << QString("%1:%2-%3").arg(b.name).arg(b.low).arg(b.high);
            return l.join(" ");
            };
      auto marks = NoteLane::marks(s);
      QCOMPARE(int(marks.size()), 1);
      std::vector<NoteLane::Mark> m = marks.begin()->second;
      QCOMPARE(int(m.size()), 7);
      for (size_t i = 1; i < m.size(); ++i)
            QVERIFY(m[i].tick > m[i - 1].tick);
      // C5: an attack, nothing before it
      QCOMPARE(m[0].note->pitch(), 72);
      QVERIFY(!m[0].joinable());
      QCOMPARE(names(NoteLane::velocityBands(m[0], m[0].joinMs)), QString("smooth:1-9 spiccato:10-116 accented:117-127"));
      QCOMPARE(NoteLane::bandAt(NoteLane::velocityBands(m[0], m[0].joinMs), m[0].velocity)->name, QString("spiccato"));
      // D5: re-attacked 54 ms after C5 ends; joined at -39 ms, a transition, at -40 not
      QCOMPARE(m[1].joinMs, -54);
      QCOMPARE(NoteLane::legatoGap(m[1]), 39);
      QCOMPARE(NoteLane::performanceAt(m[1], -54), MidiRenderer::LibPerformance::ATTACK);
      QCOMPARE(NoteLane::performanceAt(m[1], -39), MidiRenderer::LibPerformance::TRANSITION);
      QCOMPARE(NoteLane::performanceAt(m[1], -40), MidiRenderer::LibPerformance::ATTACK);
      QCOMPARE(names(NoteLane::velocityBands(m[1], -39)), QString("portamento:1-19 fingered:20-84 bowed:85-127"));
      // E5: a transition, F5's 24 ms overlap
      QCOMPARE(m[2].performance, MidiRenderer::LibPerformance::TRANSITION);
      QCOMPARE(m[2].joinMs, 24);
      QCOMPARE(NoteLane::performanceAt(m[2], -60), MidiRenderer::LibPerformance::ATTACK);
      // D5's own join: rendered as a transition, marked own
      std::vector<Note*> n = notes(s);
      set({ n[1] }, Pid::LIBRARY_JOIN, -20);
      m = NoteLane::marks(s).begin()->second;
      QVERIFY(m[1].ownJoin);
      QCOMPARE(m[1].joinMs, -20);
      QCOMPARE(m[1].performance, MidiRenderer::LibPerformance::TRANSITION);
      QVERIFY(!m[2].ownJoin);
      delete s;

      // without "performance", staccato: the Spiccato on velocity, the dynamics' bands
      MasterScore* st = score(false);
      QVERIFY(st);
      for (Note* x : notes(st)) {
            Articulation* a = new Articulation(st);
            a->setSymId(SymId::articStaccatoAbove);
            a->setTrack(x->track());
            a->setParent(x->chord());
            st->startCmd();
            st->undoAddElement(a);
            st->endCmd();
            }
      m = NoteLane::marks(st).begin()->second;
      SoundLib::setCurrent(nullptr);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      QVERIFY(!m.empty());
      QVERIFY(!m[1].joinable());
      const std::vector<NoteLane::Band> bands = NoteLane::velocityBands(m[0], m[0].joinMs);
      QVERIFY2(bands.size() >= 2, qPrintable(names(bands)));
      QCOMPARE(bands.front().low, 1);
      QCOMPARE(bands.back().high, 127);
      QCOMPARE(bands.back().name, QString("fff"));
      for (size_t i = 1; i < bands.size(); ++i)
            QCOMPARE(bands[i].low, bands[i - 1].high + 1);
      const std::vector<int>& d = m[0].dynamicVelocities;
      if (d[2] < d[3])
            QCOMPARE(NoteLane::bandAt(bands, d[2])->name, QString::fromUtf8("p–mp"));
      QCOMPARE(NoteLane::bandAt(bands, m[0].velocity) != nullptr, true);
      delete st;
      }

//---------------------------------------------------------
//   avoidBadSamples
//    the map's avoid (sso_bad_samples.json): SSO's Celli - Performance C3 attacks in the faulty round robin's band
//    play at the nearest clean velocity; on the test score's Violins 1 - Performance a band put at D5 (its re-attack,
//    106) and E5 / F5 (bowed transitions, 93): D5 moves, the transitions don't, C5 (another key) doesn't; a note's own
//    velocity stays; [notes] avoidBadSamples 0: as chosen
//---------------------------------------------------------

void TestPerformanceTechnique::avoidBadSamples()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      SoundLib::Articulation* violins = nullptr;
      const SoundLib::Articulation* celli = nullptr;
      for (auto* list : { &lib->instruments, &lib->otherPatches })
            for (auto& i : *list)
                  for (auto& a : i.articulations) {
                        if (a.attacks.empty())
                              QVERIFY2(a.avoid.empty(), qPrintable(i.name));
                        else if (i.name == "Violins 1 - Performance")
                              violins = &a;
                        else if (i.name == "Celli - Performance")
                              celli = &a;
                        }
      QVERIFY(violins && celli);
      // the measured band (Windows VM, kthost, 2026-10-10): C3 at the re-attack and spiccato velocities moves, below
      // the band and other keys don't
      QVERIFY(!celli->avoid.empty());
      const int clean = celli->avoidVelocity(48, 106);
      QVERIFY(clean != 106);
      QCOMPARE(celli->avoidVelocity(48, 112), clean);
      QCOMPARE(celli->avoidVelocity(48, clean), clean);
      QCOMPARE(celli->avoidVelocity(48, 80), 80);
      QCOMPARE(celli->avoidVelocity(60, 106), 106);
      QCOMPARE(violins->avoidVelocity(74, 106), 106);
      violins->avoid = { { 74, 74, 100, 116, 90 }, { 76, 77, 85, 127, 50 } };
      std::map<int, Key> k = render(s, lib);
      QCOMPARE(k[72].velocity, 112);
      QCOMPARE(k[74].velocity, 90);
      QCOMPARE(k[76].velocity, 69);           // (transitions, fingered: not steered)
      QCOMPARE(k[77].velocity, 69);
      std::vector<Note*> n = notes(s);
      set({ n[1] }, Pid::LIBRARY_VELOCITY, 110);
      QCOMPARE(render(s, lib)[74].velocity, 110);
      set({ n[1] }, Pid::LIBRARY_VELOCITY, 0);
      s->setMetaTag(Playback::metaTag, "notes/avoidBadSamples=0");
      QCOMPARE(render(s, lib)[74].velocity, 106);
      delete s;
      }

//---------------------------------------------------------
//   onPerformancePatch
//    which notes play on a Performance patch, as rendered (NoteLane::onPerformancePatch: the Inspector greys Performance
//    attack / transition otherwise): at the defaults ([slurs] performance on) the slurred D5 E5 F5 and A5 B5, not the
//    unslurred C5 and G5 (All techniques); a part's note as its master's; [slurs] performance off or no library: none
//---------------------------------------------------------

void TestPerformanceTechnique::onPerformancePatch()
      {
      auto lib = sso();
      MasterScore* s = score(false);
      QVERIFY(lib && s);
      s->setMetaTag(Playback::metaTag, QString());       // the defaults
      std::vector<Note*> n = notes(s);
      QCOMPARE(int(n.size()), 7);
      const std::vector<const Note*> all(n.begin(), n.end());
      auto pitches = [](const std::set<const Note*>& on) {
            QStringList p;
            for (const Note* x : on)
                  p << QString::number(x->pitch());
            p.sort();
            return p.join(" ");
            };
      QCOMPARE(pitches(NoteLane::onPerformancePatch(all)), QString());        // no library
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      QCOMPARE(pitches(NoteLane::onPerformancePatch(all)), QString("74 76 77 81 83"));
      QVERIFY(NoteLane::onPerformancePatch({ n[0] }).empty());
      QVERIFY(NoteLane::onPerformancePatch({ n[4] }).empty());
      QCOMPARE(int(NoteLane::onPerformancePatch({ n[0], n[1] }).size()), 1);
      // a value set on an inert note stays and changes nothing there
      set({ n[0] }, Pid::PERFORMANCE_ATTACK, int(Attack::ACCENTED));
      QVERIFY(NoteLane::onPerformancePatch({ n[0] }).empty());
      QCOMPARE(n[0]->performanceAttack(), Attack::ACCENTED);

      // a part's notes: its master's
      QList<Part*> parts;
      parts.append(s->parts().at(0));
      Score* nscore = new Score(s);
      Excerpt* ex = new Excerpt(s);
      ex->setPartScore(nscore);
      ex->setParts(parts);
      ex->setTitle(parts.front()->partName());
      Excerpt::createExcerpt(ex);
      s->excerpts().append(ex);
      std::vector<Note*> pn = notes(nscore);
      QCOMPARE(int(NoteLane::onPerformancePatch({ pn[0], pn[2] }).size()), 1);
      QVERIFY(NoteLane::onPerformancePatch({ pn[2] }).count(pn[2]));

      s->setMetaTag(Playback::metaTag, "slurs/performance=0");
      QVERIFY(NoteLane::onPerformancePatch(all).empty());
      delete s;
      }

//---------------------------------------------------------
//   ownTechnique
//    a note's own technique (Note::libraryTechnique, the Inspector's Technique): the list (sounding articulations, the
//    Performance patch's with its name), the rendered choice replaced (the unslurred C5 on Pizzicato, then on the
//    Performance Legato: an attack there, so its Performance fields apply), every selected note in one undo step, the
//    metaTag through a save and load, a technique the part doesn't have plays Auto, a tied note as its chain's first
//---------------------------------------------------------

void TestPerformanceTechnique::ownTechnique()
      {
      auto lib = sso();
      MasterScore* s = score(false);
      QVERIFY(lib && s);
      s->setMetaTag(Playback::metaTag, QString());       // the defaults
      std::vector<Note*> n = notes(s);
      QVERIFY(SoundLib::ownTechniques(n[0]).empty());    // (no library)
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      auto traced = [&](MasterScore* sc) {
            EventMap events;
            std::vector<MidiRenderer::LibTrace> t;
            PlaybackAudit::render(sc, &events, &t);
            std::map<int, QString> byPitch;
            for (const MidiRenderer::LibTrace& e : t)
                  byPitch.emplace(e.note->pitch(), e.patch->name + "|" + e.choice.articulation->name
                                  + (e.performance == MidiRenderer::LibPerformance::NONE ? "" : " perf"));
            return byPitch;
            };
      const std::vector<std::pair<QString, QString>> list = SoundLib::ownTechniques(n[0]);
      auto has = [&](const QString& key) {
            for (const auto& t : list)
                  if (t.first == key)
                        return true;
            return false;
            };
      QVERIFY(has("Violins 1|Long") && has("Violins 1|Pizzicato") && has("Violins 1 - Performance|Legato"));
      QVERIFY(!has("Violins 1|Long Sul G"));             // (expect="silent")
      for (const auto& t : list)
            if (t.first == "Violins 1 - Performance|Legato")
                  QCOMPARE(t.second, QString("Legato (Violins 1 - Performance)"));

      QCOMPARE(traced(s)[72], QString("Violins 1|Long"));
      const QString autoD5 = traced(s)[74];
      set({ n[0], n[4] }, Pid::LIBRARY_TECHNIQUE, QString("Violins 1|Pizzicato"));
      QCOMPARE(traced(s)[72], QString("Violins 1|Pizzicato"));
      QCOMPARE(traced(s)[79], QString("Violins 1|Pizzicato"));
      QCOMPARE(traced(s)[74], autoD5);
      s->undoRedo(true, 0);                                // one step: both back
      QVERIFY(n[0]->libraryTechnique().isEmpty() && n[4]->libraryTechnique().isEmpty());
      QCOMPARE(traced(s)[72], QString("Violins 1|Long"));
      s->undoRedo(false, 0);
      QCOMPARE(n[4]->libraryTechnique(), QString("Violins 1|Pizzicato"));

      set({ n[0] }, Pid::LIBRARY_TECHNIQUE, QString("Violins 1 - Performance|Legato"));
      QCOMPARE(traced(s)[72], QString("Violins 1 - Performance|Legato perf"));
      QVERIFY(NoteLane::onPerformancePatch({ n[0] }).count(n[0]));
      set({ n[0] }, Pid::PERFORMANCE_ATTACK, int(Attack::SMOOTH));
      const int smooth = render(s, lib)[72].velocity;
      set({ n[0] }, Pid::PERFORMANCE_ATTACK, int(Attack::ACCENTED));
      QVERIFY2(render(s, lib)[72].velocity > smooth, qPrintable(QString::number(smooth)));    // (its attack applies)
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);

      // a technique the part's patches don't have: Auto
      set({ n[1] }, Pid::LIBRARY_TECHNIQUE, QString("Celli|Pizzicato"));
      QCOMPARE(traced(s)[74], autoD5);

      // the metaTag
      QVERIFY(saveScore(s, "performancetechnique-tech.mscx"));
      const QString text = fileText("performancetechnique-tech.mscx");
      QVERIFY2(text.contains("&quot;technique&quot;:&quot;Violins 1 - Performance|Legato&quot;"),
               qPrintable(text.section("<metaTag name=\"performanceTechniques\">", 1).left(400)));
      QVERIFY(!text.contains("<libraryTechnique>"));
      MasterScore* again = readCreatedScore("performancetechnique-tech.mscx");
      QVERIFY(again);
      std::vector<Note*> m = notes(again);
      QCOMPARE(m[0]->libraryTechnique(), QString("Violins 1 - Performance|Legato"));
      QCOMPARE(m[0]->performanceAttack(), Attack::ACCENTED);
      QCOMPARE(m[4]->libraryTechnique(), QString("Violins 1|Pizzicato"));
      QVERIFY(m[2]->libraryTechnique().isEmpty());
      again->rebuildMidiMapping();
      QCOMPARE(traced(again)[79], QString("Violins 1|Pizzicato"));
      QVERIFY(saveScore(again, "performancetechnique-tech2.mscx"));
      QCOMPARE(fileText("performancetechnique-tech2.mscx"), text);
      delete again;
      delete s;
      SoundLib::setCurrent(nullptr);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      }

//---------------------------------------------------------
//   duplicateNoteValues
//    Ctrl+D over a time selection of bars (Automation::duplicateTimeSelection): each note's technique, attack,
//    transition, velocity, join and Live timing override onto the note at the same place in the bars after (same track, offset, index in its chord from
//    the bottom; Auto too); a note without one under it, or tied from the one before, left; one undo step each; the
//    score's end
//---------------------------------------------------------

void TestPerformanceTechnique::duplicateNoteValues()
      {
      MasterScore* s = duplicateScore();
      QVERIFY(s);
      Part* part = s->parts().front();
      QCOMPARE(s->endTick().ticks(), 9600);
      QVERIFY(at(s, 3360) && at(s, 3360)->tieBack());
      QVERIFY(at(s, 7200) && at(s, 7200)->tieBack());
      // bar 1's values, and bar 2's own (replaced, or left: B5 has no note under it in bar 1, nor has the tied A5)
      s->startCmd();
      at(s, 0)->undoChangeProperty(Pid::LIBRARY_TECHNIQUE, QString("Violins 1 - Performance|Legato"));
      at(s, 0)->undoChangeProperty(Pid::PERFORMANCE_ATTACK, int(Attack::SPICCATO));
      at(s, 0)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 90);
      at(s, 480, 0)->undoChangeProperty(Pid::LIBRARY_JOIN, 25);
      at(s, 480, 0)->undoChangeProperty(Pid::LIVE_START, 12.5);
      at(s, 480, 0)->undoChangeProperty(Pid::LIVE_END, -30.0);
      at(s, 480, 1)->undoChangeProperty(Pid::PERFORMANCE_TRANSITION, int(Transition::BOWED));
      at(s, 960)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 70);
      at(s, 1920)->undoChangeProperty(Pid::PERFORMANCE_TRANSITION, int(Transition::FINGERED));
      at(s, 2400, 1)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 20);
      at(s, 2400, 2)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 50);
      at(s, 3360)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 33);
      at(s, 5280)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 44);
      s->endCmd();

      QVERIFY(Automation::duplicateTimeSelection(s, part, 0, 1920) == std::make_pair(1920, 3840));
      QCOMPARE(at(s, 1920)->libraryTechnique(), QString("Violins 1 - Performance|Legato"));
      QCOMPARE(at(s, 1920)->performanceAttack(), Attack::SPICCATO);           // (after the technique: kept)
      QCOMPARE(at(s, 1920)->performanceTransition(), Transition::AUTO);       // (Auto in bar 1)
      QCOMPARE(at(s, 2400, 0)->liveStart(), 12.5);                             // (Live's timing override too)
      QCOMPARE(at(s, 2400, 0)->liveEnd(), -30.0);
      QVERIFY(at(s, 2400, 1)->libraryTechnique().isEmpty());
      QCOMPARE(at(s, 1920)->libraryVelocity(), 90);
      QCOMPARE(at(s, 2400, 0)->libraryJoin(), 25);
      QCOMPARE(at(s, 2400, 1)->performanceTransition(), Transition::BOWED);
      QCOMPARE(at(s, 2400, 1)->libraryVelocity(), 0);                          // (Auto in bar 1)
      QCOMPARE(at(s, 2400, 2)->libraryVelocity(), 50);                         // (no third note in bar 1's chord)
      QCOMPARE(at(s, 2880)->libraryVelocity(), 70);
      QCOMPARE(at(s, 3360)->libraryVelocity(), 33);                            // (no note at its place in bar 1)
      QCOMPARE(at(s, 1920)->libraryJoin(), PerformanceTechnique::JOIN_AUTO);

      // again, bar 2 onto bar 3: a note tied from the one before isn't copied (its chain plays the first note's)
      QVERIFY(Automation::duplicateTimeSelection(s, part, 1920, 3840) == std::make_pair(3840, 5760));
      QCOMPARE(at(s, 3840)->performanceAttack(), Attack::SPICCATO);
      QCOMPARE(at(s, 3840)->libraryVelocity(), 90);
      QCOMPARE(at(s, 4320)->libraryJoin(), 25);                                // (the lowest note's: E5's)
      QCOMPARE(at(s, 4800)->libraryVelocity(), 70);
      QCOMPARE(at(s, 5280)->libraryVelocity(), 44);
      // bar 3 onto bar 4: nor onto one
      QVERIFY(Automation::duplicateTimeSelection(s, part, 3840, 5760) == std::make_pair(5760, 7680));
      QCOMPARE(at(s, 6720)->libraryVelocity(), 70);
      QCOMPARE(at(s, 7200)->libraryVelocity(), 0);

      // each one undo step
      s->undoRedo(true, 0);
      QCOMPARE(at(s, 6720)->libraryVelocity(), 0);
      QCOMPARE(at(s, 4800)->libraryVelocity(), 70);
      s->undoRedo(true, 0);
      QCOMPARE(at(s, 3840)->performanceAttack(), Attack::AUTO);
      QCOMPARE(at(s, 3840)->libraryVelocity(), 0);
      QCOMPARE(at(s, 1920)->libraryVelocity(), 90);
      s->undoRedo(true, 0);
      QCOMPARE(at(s, 1920)->performanceTransition(), Transition::FINGERED);
      QCOMPARE(at(s, 1920)->performanceAttack(), Attack::AUTO);
      QCOMPARE(at(s, 1920)->libraryVelocity(), 0);
      QCOMPARE(at(s, 2400, 1)->libraryVelocity(), 20);
      QCOMPARE(at(s, 2400, 1)->performanceTransition(), Transition::AUTO);
      s->undoRedo(false, 0);
      QCOMPARE(at(s, 1920)->libraryVelocity(), 90);

      // the score's end: two bars from bar 3 have room for one (no measures added); from bar 5 none
      const int measures = s->nmeasures();
      QVERIFY(Automation::duplicateTimeSelection(s, part, 3840, 7680) == std::make_pair(7680, 9600));
      QVERIFY(Automation::duplicateTimeSelection(s, part, 7680, 9600) == std::make_pair(-1, -1));
      QCOMPARE(s->nmeasures(), measures);
      delete s;
      }

//---------------------------------------------------------
//   duplicateAutomation
//    the same over the part's automation lanes (Automation::Edit::duplicateSpan): the envelope copied as drawn (a ramp
//    across the bar line split, the value before a lane's first point), the target bars' points replaced, the envelope
//    after them as before; Ctrl+D again goes on; one undo step; cut at the score's end
//---------------------------------------------------------

void TestPerformanceTechnique::duplicateAutomation()
      {
      using Automation::Curve;
      using Automation::Lane;
      using Automation::Point;
      MasterScore* s = duplicateScore();
      QVERIFY(s);
      Part* part = s->parts().front();
      Lane a, b, c;
      a.target = "cc11";          // 0.2, a ramp 0.4 -> 1.0 from beat 3 of bar 1 to beat 3 of bar 2
      a.points = { Point(0, 0.2, Curve::STEP), Point(960, 0.4, Curve::LINEAR), Point(2880, 1.0, Curve::STEP) };
      b.target = "cc1";           // steps, one in bar 2 (replaced) and one in bar 3
      b.points = { Point(0, 0.5, Curve::STEP), Point(480, 0.3, Curve::STEP), Point(2400, 0.9, Curve::STEP), Point(4000, 0.1, Curve::STEP) };
      c.target = "cc2";           // a point in bar 2 only: bar 1 has its value (the lane's dashed line)
      c.points = { Point(2000, 0.6, Curve::STEP) };
      std::map<const Part*, Automation::PartLanes> all;
      all[part] = { a, b, c };
      QVERIFY(Automation::undoWrite(s, all));
      auto laneOf = [s, part](const QString& t) {
            for (const Lane& l : Automation::lanes(part, Automation::read(s)))
                  if (l.target == t)
                        return l;
            return Lane();
            };
      auto same = [](double x, double y) { return std::fabs(x - y) < 1e-6; };

      QVERIFY(Automation::duplicateTimeSelection(s, part, 0, 1920) == std::make_pair(1920, 3840));
      Lane la = laneOf("cc11");
      Lane lb = laneOf("cc1");
      Lane lc = laneOf("cc2");
      for (int t : { 0, 300, 960, 1200, 1500, 1800, 1919 }) {
            QVERIFY2(same(la.valueAt(t + 1920), la.valueAt(t)), qPrintable(QString::number(t)));
            QVERIFY2(same(lb.valueAt(t + 1920), lb.valueAt(t)), qPrintable(QString::number(t)));
            }
      QVERIFY(same(la.valueAt(1440 + 1920), 0.55));       // (the ramp's first half: 0.4 -> 0.7)
      QVERIFY(same(la.valueAt(3840), 1.0));                // (after: as before)
      QVERIFY(same(lb.valueAt(3840), 0.9));                // (bar 2's 0.9 at 2400 replaced, still in force from bar 3)
      QVERIFY(same(lb.valueAt(4000), 0.1));
      QCOMPARE(lb.points.size(), size_t(6));
      QVERIFY(same(lc.valueAt(1920), 0.6));
      QVERIFY(lc.extra.contains("edited"));

      // Ctrl+D again: bar 2 onto bar 3
      QVERIFY(Automation::duplicateTimeSelection(s, part, 1920, 3840) == std::make_pair(3840, 5760));
      la = laneOf("cc11");
      lb = laneOf("cc1");
      for (int t : { 0, 300, 960, 1200, 1500, 1800, 1919 }) {
            QVERIFY2(same(la.valueAt(t + 3840), la.valueAt(t)), qPrintable(QString::number(t)));
            QVERIFY2(same(lb.valueAt(t + 3840), lb.valueAt(t)), qPrintable(QString::number(t)));
            }
      QVERIFY(same(lb.valueAt(5760), 0.1));                // (the point at 4000 replaced; its value after the copy)
      QVERIFY(same(la.valueAt(6000), 1.0));

      // one undo step each: lanes (and notes) together
      s->undoRedo(true, 0);
      QCOMPARE(laneOf("cc1").points.size(), size_t(6));
      s->undoRedo(true, 0);
      QVERIFY(same(laneOf("cc1").valueAt(2400), 0.9));
      QVERIFY(same(laneOf("cc2").valueAt(1920), -1));

      // the same value through both spans: unchanged
      Lane d;
      d.points = { Point(0, 0.3, Curve::STEP) };
      QVERIFY(!Automation::Edit::duplicateSpan(d, 0, 1920, 9600));
      QCOMPARE(d.points.size(), size_t(1));
      // cut at the limit: two bars onto one, the ramp's first half, then back to the value it had
      Lane e;
      e.points = { Point(0, 0.0, Curve::LINEAR), Point(3840, 1.0, Curve::STEP) };
      QVERIFY(Automation::Edit::duplicateSpan(e, 0, 3840, 5760));
      QVERIFY(same(e.valueAt(3840), 0.0));
      QVERIFY(same(e.valueAt(4800), 0.25));
      QVERIFY(same(e.valueAt(5700), 0.5 * 1860 / 1920));
      QVERIFY(same(e.valueAt(5760), 1.0));
      // no room
      QVERIFY(!Automation::Edit::duplicateSpan(e, 3840, 5760, 5760));
      delete s;
      }

//---------------------------------------------------------
//   slurTransitions
//    [slurs] transitions (on by default, Recommended; the owner, 2026-10-12): slurs.musicxml, three slurs touching:
//    [C5 D5 E5 F5] [G5 A5 B5 C6] [D6> E6]. C5 after silence spiccato (112); the notes under a slur fingered (69), the
//    note before ending 24 ms after them; G5, a slur's first note right after the slur before, a bowed transition (93)
//    from F5 (F5 ending 24 ms after it, not cut); D6's accent keeps the accented attack (122), C6 ending 54 ms before
//    it; E6 goes on from it by a transition (until 2026-10-12 a re-attack: the accent made D6's choice other than
//    "legato"). 0: under a slur bowed (93), G5 a re-attack (106) with F5 ending 54 ms before it
//---------------------------------------------------------

void TestPerformanceTechnique::slurTransitions()
      {
      auto lib = sso();
      QVERIFY(lib);
      auto within = [](double a, double b) { return std::abs(a - b) < 2.5; };   // a tick each way, rounding
      for (int on : { 1, 0 }) {
            MasterScore* s = score(true, "slurs.musicxml");
            QVERIFY(s);
            s->setMetaTag(Playback::metaTag, on ? QString() : QString("slurs/transitions=0"));
            std::map<int, Key> k = render(s, lib);
            QVERIFY(k.count(72) && k.count(88));
            QCOMPARE(k[72].velocity, 112);
            for (int p : { 74, 76, 77, 81, 83, 84, 88 })
                  QVERIFY2(k[p].velocity == (on ? 69 : 93), qPrintable(QString("key %1: %2").arg(p).arg(k[p].velocity)));
            QCOMPARE(k[86].velocity, 122);
            QVERIFY2(within(ms(k[76].off - k[77].on), 24), qPrintable(QString::number(ms(k[76].off - k[77].on))));
            if (on) {
                  QCOMPARE(k[79].velocity, 93);
                  QVERIFY2(within(ms(k[77].off - k[79].on), 24), qPrintable(QString::number(ms(k[77].off - k[79].on))));
                  }
            else {
                  QCOMPARE(k[79].velocity, 106);
                  QVERIFY2(within(ms(k[79].on - k[77].off), 54), qPrintable(QString::number(ms(k[79].on - k[77].off))));
                  }
            QVERIFY2(within(ms(k[86].on - k[84].off), 54), qPrintable(QString::number(ms(k[86].on - k[84].off))));
            delete s;
            }
      }

QTEST_MAIN(TestPerformanceTechnique)
#include "tst_performancetechnique.moc"
