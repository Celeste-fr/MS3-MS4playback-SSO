//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include <set>
#include <QtTest/QtTest>
#include "audio/midi/event.h"
#include "mtest/testutils.h"
#include "libmscore/articulation.h"
#include "libmscore/automation.h"
#include "libmscore/chord.h"
#include "libmscore/evenlevels.h"
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
      void initTestCase() { initMTest(); Playback::setIniValuesForTest({}); }        // (the test ini leaves slurs/plainPerformance off: setIniValuesForTest)
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
      void noteLaneCopyPaste();
      void duplicateAutomation();
      void slurTransitions();
      void evenLevels();
      void evenLevelsPlain();
      void evenLevelsEdges();
      void evenLevelsRelease();
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
      QCOMPARE(violins->transition("portamento")->velocity, 5);        // (1-9: FING plays from 10, sso_performance_layers.json)
      // the portamento where PORT plays alone (sso_performance_layers.json: FING from 10, the Basses' from 20); the speed
      // controller ([slurs] speedFromWritten) on the five sections' Performance patches only
      int speed = 0;
      for (const auto* list : { &lib->instruments, &lib->otherPatches })
            for (const auto& i : *list) {
                  const bool section = QStringList({ "Violins 1", "Violins 2", "Violas", "Celli", "Basses" }).contains(i.name.section(" - ", 0, 0))
                                       && i.name.endsWith(" - Performance") && !i.name.contains("Sul ");
                  QCOMPARE(i.speedCC, section ? 119 : 0);
                  QCOMPARE(i.speedScript, section ? QString("$vfm12") : QString());
                  speed += section ? 1 : 0;
                  for (const auto& a : i.articulations)
                        if (section && !a.transitions.empty())
                              QCOMPARE(a.transition("portamento")->velocity, i.name.startsWith("Basses") ? 10 : 5);
                  }
      QCOMPARE(speed, 5);
      QCOMPARE(violins->transition("fingered")->velocity, 69);
      QCOMPARE(violins->transition("bowed")->velocity, 93);
      QVERIFY(!violins->attack("bowed"));
      QCOMPARE(violins->reattackVelocity, 106);
      QCOMPARE(violins->reattackGapMs, 54.0);
      QCOMPARE(violins->overlapMs, 24.0);
      // the smooth attack's own onset (sso_smooth_onset.json: the median, each pitch's mean); the others none
      QCOMPARE(violins->attack("smooth")->onsetMs, 100.2);
      QCOMPARE(violins->attack("smooth")->onsetAt(57), 86.1);
      QVERIFY(!violins->attack("spiccato")->hasOnset());
      QVERIFY(!violins->attack("accented")->hasOnset());
      }

//---------------------------------------------------------
//   autoTechniques
//    Auto plays the notation: C5 after silence smooth (5; [slurs] smoothStart 0: spiccato, 112); D5 starts the slur right after C5: a re-attack (106),
//    C5 ending 54 ms before it; E5 and F5 fingered (69; [slurs] transitions 0: bowed, 93), the note before ending 24 ms
//    after them; G5's accent accented
//    (122), F5 ending 54 ms before it; A5 right after it a re-attack (106); B5 after the glissando portamento (5),
//    A5 ending 24 ms after it
//---------------------------------------------------------

void TestPerformanceTechnique::autoTechniques()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      std::map<int, Key> k = render(s, lib);
      QVERIFY(k.count(72) && k.count(83));
      QCOMPARE(k[72].velocity, 5);          // (smooth after silence: [slurs] smoothStart)
      QCOMPARE(k[74].velocity, 106);
      QCOMPARE(k[76].velocity, 69);           // (slurs/transitions: fingered under a slur; 0: bowed, 93)
      QCOMPARE(k[77].velocity, 69);
      QCOMPARE(k[79].velocity, 122);
      QCOMPARE(k[81].velocity, 106);          // A5 right after G5: a re-attack
      QCOMPARE(k[83].velocity, 5);            // (the map's portamento: 1-9, PORT without FING)
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
      QCOMPARE(k[72].velocity, 5);          // (smooth after silence: [slurs] smoothStart)
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
      QCOMPARE(byPitch[72].velocity, 5);          // (smooth after silence: [slurs] smoothStart)
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
//    portamento (5)
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
      QCOMPARE(k[74].velocity, 5);
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
      QCOMPARE(NoteLane::bandAt(NoteLane::velocityBands(m[0], m[0].joinMs), m[0].velocity)->name, QString("smooth"));
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
      QCOMPARE(k[72].velocity, 5);          // (smooth after silence: [slurs] smoothStart)
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

      // selected notes only (the Velocity and Join lanes' Ctrl+D on stems, NoteLanes::duplicateSelected): bar 1's
      // first note and the lower note at 480 onto bar 2's, the others there as they were; the notes found returned
      s->startCmd();
      at(s, 0)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 101);
      at(s, 960)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 102);
      at(s, 480, 1)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 103);
      at(s, 480, 0)->undoChangeProperty(Pid::LIBRARY_JOIN, 31);
      at(s, 1920)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 1);
      at(s, 2880)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 2);
      at(s, 2400, 1)->undoChangeProperty(Pid::LIBRARY_VELOCITY, 3);
      s->endCmd();
      const std::set<const Note*> only { at(s, 0), at(s, 480, 0) };
      std::vector<Note*> to;
      s->startCmd();
      PerformanceTechnique::duplicateNoteValues(s, part, 0, 1920, s->endTick().ticks(), &only, &to);
      s->endCmd();
      QCOMPARE(int(to.size()), 2);
      QVERIFY(std::find(to.begin(), to.end(), at(s, 1920)) != to.end());
      QVERIFY(std::find(to.begin(), to.end(), at(s, 2400, 0)) != to.end());
      QCOMPARE(at(s, 1920)->libraryVelocity(), 101);
      QCOMPARE(at(s, 2400, 0)->libraryJoin(), 31);
      QCOMPARE(at(s, 2880)->libraryVelocity(), 2);                              // (not selected)
      QCOMPARE(at(s, 2400, 1)->libraryVelocity(), 3);
      s->undoRedo(true, 0);                                                      // (one undo step)
      QCOMPARE(at(s, 1920)->libraryVelocity(), 1);
      delete s;
      }

//---------------------------------------------------------
//   noteLaneCopyPaste
//    Ctrl+C / Ctrl+V in the Velocity and Join lanes (NoteLane::copyClip / pasteClip; the lanes' keys: mscore/notelanes.h):
//    the selected stems' own values in note order (Auto as Auto, relative ticks), pasted in order onto the lane's
//    stems from the target on, the rest dropped when the notes run out, Velocity and Join each into their own lane
//    only, one undo step, and the score's own copy / paste untouched
//---------------------------------------------------------

void TestPerformanceTechnique::noteLaneCopyPaste()
      {
      auto lib = sso();
      MasterScore* s = score();
      QVERIFY(lib && s);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      std::vector<Note*> n = notes(s);
      QCOMPARE(int(n.size()), 7);
      auto marks = [s]() { return NoteLane::marks(s).begin()->second; };
      std::vector<NoteLane::Mark> m = marks();
      QVERIFY(!m[0].joinable());
      for (size_t i = 1; i < m.size(); ++i)
            QVERIFY(m[i].joinable());

      // velocities: notes 0, 1, 3 own values, note 2 Auto
      set({ n[0] }, Pid::LIBRARY_VELOCITY, 90);
      set({ n[1] }, Pid::LIBRARY_VELOCITY, 70);
      set({ n[3] }, Pid::LIBRARY_VELOCITY, 55);
      const std::set<const Note*> sel { n[0], n[1], n[2], n[3] };
      NoteLane::Clip c = NoteLane::copyClip(marks(), sel, false);
      QVERIFY(!c.join);
      QCOMPARE(int(c.items.size()), 4);
      QCOMPARE(c.items[0].value, 90);
      QCOMPARE(c.items[1].value, 70);
      QCOMPARE(c.items[2].value, 0);                       // Auto stays Auto
      QCOMPARE(c.items[3].value, 55);
      QCOMPARE(c.items[0].dTick, 0);
      QCOMPARE(c.items[3].dTick, m[3].tick - m[0].tick);

      // pasted onto the notes from note 3 on (3 left: 3, 4, 5 .. there are 4 left, 4 values): all four
      s->startCmd();
      std::vector<Note*> to = NoteLane::pasteClip(marks(), c, m[3].tick);
      s->endCmd();
      QCOMPARE(int(to.size()), 4);
      QCOMPARE(to[0], n[3]);
      QCOMPARE(n[3]->libraryVelocity(), 90);
      QCOMPARE(n[4]->libraryVelocity(), 70);
      QCOMPARE(n[5]->libraryVelocity(), 0);
      QCOMPARE(n[6]->libraryVelocity(), 55);
      QCOMPARE(n[2]->libraryVelocity(), 0);                // (before the target: as it was)
      QCOMPARE(n[0]->libraryVelocity(), 90);
      s->undoRedo(true, 0);                                 // one step
      QCOMPARE(n[3]->libraryVelocity(), 55);
      QCOMPARE(n[4]->libraryVelocity(), 0);
      QCOMPARE(n[6]->libraryVelocity(), 0);

      // fewer notes left than values: the rest dropped
      s->startCmd();
      to = NoteLane::pasteClip(marks(), c, m[5].tick);
      s->endCmd();
      QCOMPARE(int(to.size()), 2);
      QCOMPARE(n[5]->libraryVelocity(), 90);
      QCOMPARE(n[6]->libraryVelocity(), 70);
      s->undoRedo(true, 0);
      // a target past the last note: nothing, no undo step made
      s->startCmd();
      to = NoteLane::pasteClip(marks(), c, m[6].tick + 1);
      s->endCmd();
      QVERIFY(to.empty());
      QCOMPARE(n[6]->libraryVelocity(), 0);

      // Join: only joinable notes (the first has no note before it); Auto as Auto; the clip's lane is its own
      set({ n[1] }, Pid::LIBRARY_JOIN, -20);
      set({ n[3] }, Pid::LIBRARY_JOIN, 30);
      const std::set<const Note*> js { n[0], n[1], n[2], n[3] };
      NoteLane::Clip j = NoteLane::copyClip(marks(), js, true);
      QVERIFY(j.join);
      QCOMPARE(int(j.items.size()), 3);                    // (n[0] has no stem in the Join lane)
      QCOMPARE(j.items[0].value, -20);
      QCOMPARE(j.items[1].value, PerformanceTechnique::JOIN_AUTO);
      QCOMPARE(j.items[2].value, 30);
      s->startCmd();
      to = NoteLane::pasteClip(marks(), j, marks()[4].tick);
      s->endCmd();
      QCOMPARE(int(to.size()), 3);
      QCOMPARE(n[4]->libraryJoin(), -20);
      QCOMPARE(n[5]->libraryJoin(), PerformanceTechnique::JOIN_AUTO);
      QCOMPARE(n[6]->libraryJoin(), 30);
      QCOMPARE(n[4]->libraryVelocity(), 0);                // (the velocities untouched)
      s->undoRedo(true, 0);
      QCOMPARE(n[4]->libraryJoin(), PerformanceTechnique::JOIN_AUTO);
      QCOMPARE(n[6]->libraryJoin(), PerformanceTechnique::JOIN_AUTO);

      // a Join clip never writes velocities and a Velocity clip never joins (the Pid follows the clip)
      s->startCmd();
      NoteLane::pasteClip(marks(), j, 0);
      s->endCmd();
      QCOMPARE(n[1]->libraryVelocity(), 70);
      s->undoRedo(true, 0);

      // the metaTag carries the pasted values
      s->startCmd();
      NoteLane::pasteClip(marks(), c, m[3].tick);
      s->endCmd();
      QVERIFY(saveScore(s, "performancetechnique-lanepaste.mscx"));
      QVERIFY(fileText("performancetechnique-lanepaste.mscx").contains("<metaTag name=\"performanceTechniques\">"));
      MasterScore* again = readCreatedScore("performancetechnique-lanepaste.mscx");
      QVERIFY(again);
      QCOMPARE(notes(again)[3]->libraryVelocity(), 90);
      QCOMPARE(notes(again)[6]->libraryVelocity(), 55);
      delete again;

      // the score's own copy / paste: the first note onto the last, untouched by the lanes' clip
      s->select(n[0]);
      QMimeData mime;
      mime.setData(s->selection().mimeType(), s->selection().mimeData());
      s->select(n[6]);
      const int before = n[6]->pitch();
      QVERIFY(before != n[0]->pitch());
      s->startCmd();
      s->cmdPaste(&mime, nullptr);
      s->endCmd();
      QCOMPARE(notes(s).back()->pitch(), n[0]->pitch());
      QCOMPARE(int(c.items.size()), 4);
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
//    [C5 D5 E5 F5] [G5 A5 B5 C6] [D6> E6]. C5 after silence smooth (5, [slurs] smoothStart); the notes under a slur fingered (69), the
//    note before ending 24 ms after them; G5, a slur's first note right after the slur before, a bowed transition (93)
//    from F5 (F5 ending 24 ms after it, not cut); D6's accent keeps the accented attack (122), C6 ending 54 ms before
//    it; E6 goes on from it by a transition (until 2026-10-12 a re-attack: the accent made D6's choice other than
//    "legato"). 0: under a slur bowed (93), G5 a re-attack (106) with F5 ending 54 ms before it. With [slurs]
//    plainPerformance: by [slurs] plainAuto G5 bowed (93) and C5, after silence, smooth (5), early by its onset less the
//    patch's plainShift; without the slurs every touching note bowed; plainAuto 0 all 64; plainAuto 2 as 1, each bowed
//    note sent its patch's bowedEarly earlier (branch bowed-early), F5 still ending 24 ms after G5's moved note-on
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
            QCOMPARE(k[72].velocity, 5);          // (smooth after silence: [slurs] smoothStart)
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
      // [slurs] plainPerformance (every plain note at 64, SSO's fingered transition) with [slurs] plainAuto (branch
      // celli-bowed, 2026-10-26; the owner: "this is not supposed to be fingered. it's supposed to be bowed", "slurs should
      // have smooth start when there's nothing preceding it"): 1 G5, the slur starting where the slur before ends, keeps the
      // bowed transition (93) from F5, F5 ending 24 ms after it, and C5, the first note after silence, the smooth
      // attack (5), early by Violins 1 - Performance's smooth onset less its plainShift (the start lead moves the rest
      // later); 0 (as plainPerformance alone) both 64 at their written start. The notes under a slur 64 either way
      const SoundLib::LibInstrument* v1 = nullptr;
      const SoundLib::Articulation* legato = nullptr;
      for (const auto* list : { &lib->instruments, &lib->otherPatches })
            for (const auto& i : *list)
                  for (const auto& a : i.articulations)
                        if (i.name == "Violins 1 - Performance" && !a.attacks.empty()) {
                              v1 = &i;
                              legato = &a;
                              }
      QVERIFY(v1 && std::isfinite(v1->plainShiftMs));
      QVERIFY(legato && legato->attack("smooth"));
      const double smoothEarly = legato->startOnset(legato->attack("smooth"), 72, false) - v1->plainShiftMs;
      QVERIFY2(smoothEarly > 20, qPrintable(QString::number(smoothEarly)));   // (34.4 ms: 100.2 - 65.8)
      // ([slurs] plainAuto 2: a new bow stroke its patch's bowedEarly earlier; Violins 1 - Performance 125.2 ms)
      QVERIFY2(std::isfinite(v1->bowedEarlyMs) && v1->bowedEarlyMs > 100, qPrintable(QString::number(v1->bowedEarlyMs)));
      std::map<int, double> bowedGap;     // plainAuto -> F5's note-on to G5's
      for (int plainAuto : { 2, 1, 0 }) {
            MasterScore* ps = score(false, "slurs.musicxml");
            QVERIFY(ps);
            ps->setMetaTag(Playback::metaTag, QString("slurs/plainPerformance=1;slurs/plainAuto=%1").arg(plainAuto));
            std::map<int, Key> k = render(ps, lib);
            QVERIFY(k.count(72) && k.count(74) && k.count(79));
            bowedGap[plainAuto] = ms(k[79].on - k[77].on);
            QCOMPARE(k[79].velocity, plainAuto ? 93 : MidiRenderer::PLAIN_VELOCITY);
            QVERIFY2(within(ms(k[77].off - k[79].on), 24), qPrintable(QString::number(ms(k[77].off - k[79].on))));
            QCOMPARE(k[72].velocity, plainAuto ? 5 : MidiRenderer::PLAIN_VELOCITY);
            const double gap = ms(k[74].on - k[72].on);
            QVERIFY2(within(gap, 500 + (plainAuto ? smoothEarly : 0.0)), qPrintable(QString::number(gap)));
            for (int p : { 74, 76, 77, 81, 83, 84 })
                  QVERIFY2(k[p].velocity == MidiRenderer::PLAIN_VELOCITY, qPrintable(QString("key %1: %2").arg(p).arg(k[p].velocity)));
            delete ps;
            }
      QVERIFY2(within(bowedGap[1], 500) && within(bowedGap[0], 500), qPrintable(QString("%1 %2").arg(bowedGap[1]).arg(bowedGap[0])));
      QVERIFY2(within(bowedGap[2], 500 - v1->bowedEarlyMs), qPrintable(QString::number(bowedGap[2])));
      // without the slurs (détaché): C5 after silence smooth and early as above, every other note touching the one before a
      // new bow stroke, bowed (93); plainAuto 0 all 64; 2 every bowed note bowedEarly earlier (D5 after the smooth C5,
      // the notes after it as before: each one moved as much)
      for (int plainAuto : { 2, 1, 0 }) {
            MasterScore* ps = score(false, "slurs.musicxml");
            QVERIFY(ps);
            std::vector<Spanner*> slurs;
            for (auto i : ps->spanner())
                  if (i.second->isSlur())
                        slurs.push_back(i.second);
            ps->startCmd();
            for (Spanner* sp : slurs)
                  ps->undoRemoveElement(sp);
            ps->endCmd();
            ps->setMetaTag(Playback::metaTag, QString("slurs/plainPerformance=1;slurs/plainAuto=%1").arg(plainAuto));
            std::map<int, Key> k = render(ps, lib);
            QVERIFY(k.count(72) && k.count(74));
            QCOMPARE(k[72].velocity, plainAuto ? 5 : MidiRenderer::PLAIN_VELOCITY);
            const double gap = ms(k[74].on - k[72].on);
            QVERIFY2(within(gap, 500 + (plainAuto ? smoothEarly : 0.0) - (plainAuto == 2 ? v1->bowedEarlyMs : 0.0)), qPrintable(QString::number(gap)));
            QVERIFY2(within(ms(k[76].on - k[74].on), 500), qPrintable(QString::number(ms(k[76].on - k[74].on))));
            const int bowed = legato->legatoVelocity > 0 ? legato->legatoVelocity : legato->transition("bowed")->velocity;
            for (int p : { 74, 76, 77, 79, 81, 83, 84, 88 })
                  QVERIFY2(k[p].velocity == (plainAuto ? bowed : MidiRenderer::PLAIN_VELOCITY), qPrintable(QString("key %1: %2").arg(p).arg(k[p].velocity)));
            QCOMPARE(k[86].velocity, MidiRenderer::PLAIN_VELOCITY);       // (D6's accent: plainAccents', 0 here)
            delete ps;
            }
      // slur-repeat.musicxml: [C5 C5 D5 E5], the same key struck again under a slur: not after silence, so spiccato (112),
      // not smooth (until 2026-10-12 smooth: smoothStart took "no transition" for silence)
      MasterScore* s = score(true, "slur-repeat.musicxml");
      QVERIFY(s);
      QStringList all;
      render(s, lib, &all);
      QList<int> c5;
      for (const QString& l : all) {
            const QStringList f = l.split(' ');
            if (f[1].toInt() == ME_NOTEON && f[2].toInt() == 72 && f[3].toInt() > 0 && f[4] == "0")
                  c5 << f[3].toInt();
            }
      QVERIFY2(c5.size() == 2, qPrintable(all.join('\n')));
      QCOMPARE(c5[0], 5);
      QCOMPARE(c5[1], 112);       // (spiccato)
      delete s;
      }

//---------------------------------------------------------
//   evenLevelsPlain
//    [slurs] plainPerformance: every note of slurs.musicxml at velocity 64, touching (joined), so every transition is
//    a fingered one; the even levels' slurs still start at each written slur (G5, D6: the level rule's phrases), not
//    only at the passage's first note (without, the glide flattened the three slurs to one median)
//---------------------------------------------------------

void TestPerformanceTechnique::evenLevelsPlain()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* s = score(false, "slurs.musicxml");
      QVERIFY(s);
      s->setMetaTag(Playback::metaTag, "slurs/plainPerformance=1");
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MidiRenderer midi(s);
      midi.setMinChunkSize(10);
      std::vector<EvenLevels::NoteInfo> in;
      midi.setEvenLevelsInfo(&in);
      EventMap events;
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      midi.renderScore(&events, ctx);
      SoundLib::setCurrent(nullptr);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      std::sort(in.begin(), in.end(), [](const EvenLevels::NoteInfo& a, const EvenLevels::NoteInfo& b) { return a.on < b.on; });
      QCOMPARE(int(in.size()), 10);
      for (size_t k = 0; k < in.size(); ++k) {
            QCOMPARE(in[k].velocity, MidiRenderer::PLAIN_VELOCITY);
            QVERIFY(in[k].context.startsWith("Violins 1 - Performance|"));
            QCOMPARE(in[k].joined, k != 0);
            QCOMPARE(in[k].newSlur, k == 0 || k == 4 || k == 8);
            }
      delete s;
      }

//---------------------------------------------------------
//   evenLevels
//    [levels] evenLevels (evenlevels.h): slurs.musicxml under "performance" (SSO's Violins 1 - Performance): the
//    renderer reports every note's context (joined, the slur starts: a bowed transition, the windows: the map's legato
//    delay by interval); values set with their contexts are current; an edit nearby (a note's own velocity) makes that
//    note and its joined neighbours stale, the others stay current, undo makes them current again; the metaTag
//    "evenLevels" round trip (left out when none: the file otherwise unchanged); the headroom (tops); the fit's maths
//---------------------------------------------------------

void TestPerformanceTechnique::evenLevels()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* s = score(true, "slurs.musicxml");
      QVERIFY(s);
      auto infos = [&]() {
            SoundLib::setCurrent(lib);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            MidiRenderer midi(s);
            midi.setMinChunkSize(10);
            std::vector<EvenLevels::NoteInfo> in;
            midi.setEvenLevelsInfo(&in);
            EventMap events;
            SynthesizerState ss;
            MidiRenderer::Context ctx(ss);
            ctx.metronome = false;
            midi.renderScore(&events, ctx);
            SoundLib::setCurrent(nullptr);
            SoundLib::setOutput(SoundLib::Output::MIDI);
            std::sort(in.begin(), in.end(), [](const EvenLevels::NoteInfo& a, const EvenLevels::NoteInfo& b) { return a.on < b.on; });
            return in;
            };
      std::vector<EvenLevels::NoteInfo> in = infos();
      QCOMPARE(int(in.size()), 10);
      const std::vector<int> pitches { 72, 74, 76, 77, 79, 81, 83, 84, 86, 88 };
      const SoundLib::Articulation* legato = nullptr;
      for (const SoundLib::LibInstrument& li : lib->otherPatches)
            if (li.name == "Violins 1 - Performance")
                  legato = EvenLevels::legatoOf(&li);
      for (const SoundLib::LibInstrument& li : lib->instruments)
            if (li.name == "Violins 1 - Performance")
                  legato = EvenLevels::legatoOf(&li);
      QVERIFY(legato);
      for (size_t k = 0; k < in.size(); ++k) {
            QCOMPARE(in[k].pitch, pitches[k]);
            QCOMPARE(in[k].joined, k != 0 && k != 8);                   // (D6: accented, a re-attack)
            QCOMPARE(in[k].newSlur, k == 0 || k == 4 || k == 8);        // (G5: the slur touching, bowed)
            QVERIFY(in[k].context.startsWith("Violins 1 - Performance|"));
            if (in[k].joined)
                  QCOMPARE(in[k].windowMs, legato->legatoDelayAt(pitches[k] - pitches[k - 1], pitches[k - 1]));
            }
      // contexts: the same each render, distinct from note to note
      const std::vector<EvenLevels::NoteInfo> again = infos();
      std::set<QString> distinct;
      for (size_t k = 0; k < in.size(); ++k) {
            QCOMPARE(again[k].context, in[k].context);
            distinct.insert(in[k].context);
            }
      QCOMPARE(int(distinct.size()), 10);
      // values with their contexts (one undo step)
      std::vector<Note*> n = notes(s);
      QCOMPARE(int(n.size()), 10);
      s->startCmd();
      for (size_t k = 0; k < 8; ++k) {
            n[k]->undoChangeProperty(Pid::EVEN_LEVEL, k % 2 ? 1.25 : -0.5);
            n[k]->undoChangeProperty(Pid::EVEN_LEVEL_CONTEXT, in[k].context);
            }
      s->endCmd();
      QCOMPARE(EvenLevels::count(s), 8);
      infos();
      for (size_t k = 0; k < 10; ++k)
            QCOMPARE(EvenLevels::state(n[k]), k < 8 ? EvenLevels::State::CURRENT : EvenLevels::State::NONE);
      // headroom: the patch's largest value
      const auto tops = EvenLevels::tops(s);
      QCOMPARE(int(tops.size()), 1);
      QCOMPARE(tops.begin()->second.at("Violins 1 - Performance"), 1.25);
      // E5's own velocity: it and its joined neighbours stale, the rest current
      set({ n[2] }, Pid::LIBRARY_VELOCITY, 40);
      infos();
      for (size_t k = 0; k < 8; ++k)
            QVERIFY2(EvenLevels::state(n[k]) == (k >= 1 && k <= 3 ? EvenLevels::State::STALE : EvenLevels::State::CURRENT),
                     qPrintable(QString("note %1").arg(k)));
      s->undoRedo(true, 0);
      infos();
      for (size_t k = 0; k < 8; ++k)
            QCOMPARE(EvenLevels::state(n[k]), EvenLevels::State::CURRENT);
      // the file: the metaTag, nothing else; read back, written again the same
      QVERIFY(saveScore(s, "evenlevels-saved.mscx"));
      const QString text = fileText("evenlevels-saved.mscx");
      QVERIFY(text.contains("<metaTag name=\"evenLevels\">[{&quot;context&quot;:&quot;Violins 1 - Performance|"));
      QVERIFY(!text.contains("<evenLevel>"));
      s->startCmd();
      for (size_t k = 0; k < 8; ++k) {
            n[k]->undoChangeProperty(Pid::EVEN_LEVEL, EvenLevels::NONE);
            n[k]->undoChangeProperty(Pid::EVEN_LEVEL_CONTEXT, QString());
            }
      s->endCmd();
      QCOMPARE(EvenLevels::count(s), 0);
      QVERIFY(saveScore(s, "evenlevels-none.mscx"));
      QString without = text;
      without.remove(QRegularExpression("\\s*<metaTag name=\"evenLevels\">[^<]*</metaTag>"));
      QCOMPARE(without, fileText("evenlevels-none.mscx"));
      MasterScore* read = readCreatedScore("evenlevels-saved.mscx");
      QVERIFY(read);
      QVERIFY(read->metaTag(EvenLevels::metaTag).isEmpty());
      std::vector<Note*> m = notes(read);
      for (size_t k = 0; k < 10; ++k) {
            QCOMPARE(m[k]->evenLevel(), k < 8 ? (k % 2 ? 1.25 : -0.5) : EvenLevels::NONE);
            QCOMPARE(m[k]->evenContext(), k < 8 ? in[k].context : QString());
            }
      QVERIFY(saveScore(read, "evenlevels-saved2.mscx"));
      QCOMPARE(fileText("evenlevels-saved2.mscx"), text);
      delete read;
      delete s;

      // the maths: the glide (linear, the ends held), the slur statistics, a fit that flattens a swell
      QCOMPARE(EvenLevels::glideAt({ 1.0, 2.0 }, { -1.0, 3.0 }, 0.5), -1.0);
      QCOMPARE(EvenLevels::glideAt({ 1.0, 2.0 }, { -1.0, 3.0 }, 1.25), 0.0);
      QCOMPARE(EvenLevels::glideAt({ 1.0, 2.0 }, { -1.0, 3.0 }, 9.0), 3.0);
      const EvenLevels::Stats st = EvenLevels::slurStats({ 0, 0, 0, 0, 1, 1, 1, 1 }, { 9, 1, 4, 2, 5, 8, 6, 9 });
      QCOMPARE(st.median.at(0), 2.0);                   // (1 4 2: the first note left out)
      QCOMPARE(st.median.at(1), 6.0);                   // (5 8 6: the last left out)
      QCOMPARE(int(st.ranges.size()), 2);
      QCOMPARE(st.ranges[0], 3.0);
      QCOMPARE(st.ranges[1], 3.0);
      QCOMPARE(st.residual, 2.0);
      EvenLevels::Passage p;
      for (int k = 0; k < 8; ++k) {
            p.on.push_back(0.136 * k);
            p.off.push_back(0.136 * k + 0.16);
            p.window.push_back(0.136 * k + 0.22);
            p.slur.push_back(0);
            }
      // the nodes: halfway from each note-on to the next (the last: to its note-off; a window's last: to Passage::lastNext)
      QCOMPARE(EvenLevels::nodeTime(1.0, 1.5, 1.2), 1.1);
      QCOMPARE(EvenLevels::nodeTime(1.0, 1.5, -1.0), 1.25);
      QCOMPARE(EvenLevels::nodeTime(1.0, -1.0, -1.0), 1.0);
      std::vector<double> c = p.centres();
      QVERIFY(std::fabs(c[2] - (0.272 + 0.068)) < 1e-9);
      QVERIFY(std::fabs(c[7] - (0.952 + 0.08)) < 1e-9);
      p.lastNext = 0.952 + 0.2;
      QVERIFY(std::fabs(p.centres()[7] - (0.952 + 0.1)) < 1e-9);
      p.lastNext = -1.0;
      // an envelope with notes 3 and 4 3 dB up (as Whence's Celli): the fit's rms off target well under the swell's,
      // the two notes' values lower
      std::vector<double> env(size_t(p.gridSize()), 0.0);
      for (int i = 0; i < p.gridSize(); ++i) {
            const double t = p.gridStart() + i * EvenLevels::STEP_MS / 1000.0;
            env[size_t(i)] = (t >= p.on[3] && t < p.on[5]) ? 3.0 : 0.0;
            }
      double fitRms = 0, envRms = 0;
      const std::vector<double> v = EvenLevels::fitGlide(p, env, &fitRms, &envRms);
      QVERIFY2(envRms > 1.0 && fitRms < 0.6 * envRms, qPrintable(QString("%1 -> %2").arg(envRms).arg(fitRms)));
      QVERIFY2(v[3] < -1.0 && v[4] < -1.0 && std::fabs(v[1]) < 1.0,
               qPrintable(QString("%1 %2 %3").arg(v[1]).arg(v[3]).arg(v[4])));
      // (the rounds' measure starts at the first note's heard window or note 2's note-on, whichever is earlier: here
      // note 2's, as the fit's target)
      QVERIFY(std::fabs(EvenLevels::envelopeRms(p, env) - envRms) < 1e-9);
      // the measured gain: no change measured, none told (1); the envelope as flat as the fit says: closer
      QCOMPARE(EvenLevels::envelopeGain(p, env, env, v), 1.0);
      std::vector<double> flat(env.size(), 0.0);
      QVERIFY(EvenLevels::envelopeRms(p, flat) < envRms);
      const double g = EvenLevels::envelopeGain(p, env, flat, v);
      QVERIFY2(g >= 0.5 && g < 3.0, qPrintable(QString::number(g)));
      }

//---------------------------------------------------------
//   evenLevelsEdges
//    even-edges (2026-11-01): the fit's target reaches the last note's end (targetEnd), a note the target barely sees
//    held at 0 (unseenNotes: the first; a note too short for the smoothing), the round kept over every note and not one
//    that takes a note > ROUND_REJECT_DB further from its slur's median (chooseRound). Envelopes synthetic: steps at
//    each note's heard window start (note-on + window), as SSO's lag their note-ons; the K. 550 shape from the job's
//    measured levels (K. 550 i, Violins 2 bars 1-3, real SSO on the Windows VM; the score is not in the repo)
//---------------------------------------------------------

// a passage: note-ons, lengths (s), window starts (ms after the note-on), slurs
static EvenLevels::Passage edgePassage(const std::vector<double>& on, const std::vector<double>& off,
                                       const std::vector<double>& windowMs, const std::vector<int>& slur)
      {
      EvenLevels::Passage p;
      p.on = on;
      p.off = off;
      for (size_t k = 0; k < on.size(); ++k)
            p.window.push_back(on[k] + windowMs[k] / 1000.0);
      p.slur = slur;
      return p;
      }

// the envelope on p's grid: silence (floor) before the first heard window start, then each note's level from its
// heard window start on, the last level to the last note-off + 0.15 s, silence after; smoothed by the one pole
static std::vector<double> edgeEnvelope(const EvenLevels::Passage& p, const std::vector<double>& level, double floor = 10.0)
      {
      std::vector<double> env(size_t(p.gridSize()), floor);
      const double end = p.off.back() + 0.15;
      for (int i = 0; i < p.gridSize(); ++i) {
            const double t = p.gridStart() + i * EvenLevels::STEP_MS / 1000.0;
            if (t >= end)
                  continue;
            for (size_t k = 0; k < p.window.size(); ++k)
                  if (t >= p.window[k])
                        env[size_t(i)] = level[k];
            }
      // (smoothed as the perceived envelope is: the one pole, SMOOTH_MS)
      const double a = 1.0 - std::exp(-EvenLevels::STEP_MS / EvenLevels::SMOOTH_MS);
      double y = env.empty() ? 0.0 : env[0];
      for (double& x : env) {
            y += a * (x - y);
            x = y;
            }
      return env;
      }

static QString dbText(const std::vector<double>& v)
      {
      QStringList out;
      for (double x : v)
            out << QString::number(x, 'f', 2);
      return out.join(' ');
      }

void TestPerformanceTechnique::evenLevelsEdges()
      {
      // K. 550 i, Violins 2 bars 1-3 as played (Recommended): Eb D | D | Eb D | D | Eb D | D Bb (the leap, the passage's
      // last), the smooth attack's, transitions' and attacks' window starts, round 0's measured levels (result.json)
      const std::vector<double> on { 0.0, 0.1088, 0.3362, 0.6470, 0.7306, 0.9579, 1.2688, 1.3524, 1.5797, 1.8187 };
      const std::vector<double> off { 0.1323, 0.3362, 0.6470, 0.7543, 0.9579, 1.2688, 1.3763, 1.5797, 1.8423, 2.2013 };
      const std::vector<double> win { 105.2, 185, 175, 165, 185, 175, 165, 185, 175, 241 };
      const std::vector<int> slur { 0, 0, 1, 2, 2, 3, 4, 4, 5, 5 };
      const std::vector<double> lv0 { 44.76, 47.75, 35.76, 50.28, 50.14, 36.43, 49.99, 50.42, 39.15, 45.78 };
      const EvenLevels::Passage k550 = edgePassage(on, off, win, slur);
      const size_t n = on.size();

      // the last note's part of the target: its heard window, never past its note-off (even-release); a window's:
      // its note-on to Passage::lastNext
      QVERIFY(std::fabs(EvenLevels::lastFrom(k550) - (on[9] + 0.241)) < 1e-9);
      QVERIFY(std::fabs(EvenLevels::targetEnd(k550) - std::min(off[9], on[9] + (241 + EvenLevels::WINDOW_MS) / 1000.0)) < 1e-9);
      {
            EvenLevels::Passage w = k550;
            w.lastNext = 2.05;
            QCOMPARE(EvenLevels::targetEnd(w), 2.05);
            QCOMPARE(EvenLevels::lastFrom(w), on[9]);
      }

      // first and last notes: the first unseen (its own span outside the target), the last seen in full now, every inner
      // note well above OBSERVED_MIN (the bowed Eb's 83 ms span the least)
      const std::vector<double> env0 = edgeEnvelope(k550, lv0);
      const std::vector<double> own = EvenLevels::ownWeights(k550, env0);
      const std::vector<bool> unseen = EvenLevels::unseenNotes(k550, env0);
      qDebug("K. 550 own weights: %s", qPrintable(dbText(own)));
      QVERIFY(own[0] < EvenLevels::OBSERVED_MIN && unseen[0]);
      for (size_t k = 1; k < n; ++k)
            QVERIFY2(!unseen[k] && own[k] > 4 * EvenLevels::OBSERVED_MIN, qPrintable(QString("%1: %2").arg(k).arg(own[k])));
      QVERIFY(own[9] > 15.0);

      // the regression on K. 550's shape: the first note held at 0, the leap note's value small (the job gave it
      // +6.87 in round 1 and +14.01 in round 2; the replica on the real render, round 1: +5.18 before, +0.24 with the
      // target to the note's end), the fit closer than the envelope
      double fitRms = 0, envRms = 0;
      const std::vector<double> v = EvenLevels::fitGlide(k550, env0, &fitRms, &envRms);
      qDebug("K. 550 values: %s (rms %.2f -> %.2f)", qPrintable(dbText(v)), envRms, fitRms);
      QCOMPARE(v[0], 0.0);
      QVERIFY2(std::fabs(v[9] / EvenLevels::CC11_FACTOR) < 3.0, qPrintable(dbText(v)));
      QVERIFY(fitRms < envRms);

      // several slurs: a step between slurs stays (each slur flattened to its own median): three slurs, flat at 40, 46,
      // 43 dB as heard, need nothing; a +3 dB note inside the middle one gets a negative value, its neighbours ~0 (heard
      // 20 ms after the note-on: with spans from note-on to note-on a lag puts the slur before's level into a slur's
      // first samples, docs/PLAYBACK_SETTINGS.md › Even levels › Open)
      {
            std::vector<double> o, f, wm, lv;
            std::vector<int> sl;
            for (int k = 0; k < 12; ++k) {
                  o.push_back(0.2 * k);
                  f.push_back(0.2 * k + 0.22);
                  wm.push_back(20.0);              // (heard at once: the spans' lag is an open item)
                  sl.push_back(k / 4);
                  lv.push_back(k < 4 ? 40.0 : k < 8 ? 46.0 : 43.0);
                  }
            const EvenLevels::Passage p = edgePassage(o, f, wm, sl);
            const std::vector<double> flat = EvenLevels::fitGlide(p, edgeEnvelope(p, lv));
            qDebug("three flat slurs: %s", qPrintable(dbText(flat)));
            // (the one pole's ramp at each slur's start leaves ~1 dB at the boundaries; levelling the 6 dB step would be ±3)
            double mean[3] { 0, 0, 0 };
            for (size_t k = 0; k < flat.size(); ++k) {
                  QVERIFY2(std::fabs(flat[k]) < 1.5, qPrintable(dbText(flat)));
                  mean[k / 4] += flat[k] / 4.0;
                  }
            QVERIFY2(std::fabs(mean[1] - mean[0]) < 1.0 && std::fabs(mean[2] - mean[1]) < 1.0, qPrintable(dbText(flat)));
            lv[5] += 3.0;
            const std::vector<double> bump = EvenLevels::fitGlide(p, edgeEnvelope(p, lv));
            qDebug("a +3 dB note in the middle slur: %s", qPrintable(dbText(bump)));
            QVERIFY2(bump[5] < -1.0 && std::fabs(bump[1]) < 1.0 && std::fabs(bump[10]) < 1.0, qPrintable(dbText(bump)));
      }

      // an envelope that lags its note-ons by 150-240 ms: the last note +4 dB as heard is now seen and lowered (before,
      // its sound lay after the target: no change asked, or a free one); the first note, +4 dB, held
      {
            std::vector<double> lv(n, 46.0);
            lv[9] += 4.0;
            const std::vector<double> lastLoud = EvenLevels::fitGlide(k550, edgeEnvelope(k550, lv));
            qDebug("lagging, the last note +4 dB: %s", qPrintable(dbText(lastLoud)));
            QVERIFY2(lastLoud[9] < -1.5 && std::fabs(lastLoud[2]) < 1.0, qPrintable(dbText(lastLoud)));
            lv[9] = 46.0;
            lv[0] += 4.0;
            const std::vector<double> firstLoud = EvenLevels::fitGlide(k550, edgeEnvelope(k550, lv));
            QCOMPARE(firstLoud[0], 0.0);
            for (size_t k = 1; k < n; ++k)
                  QVERIFY2(std::fabs(firstLoud[k]) < 1.5, qPrintable(dbText(firstLoud)));
      }

      // a window inside a longer passage ends at the next joined note-on; a note too short for the smoothing is unseen
      {
            std::vector<double> o { 0.0, 0.2, 0.4, 0.405, 0.6, 0.8 };
            std::vector<double> f { 0.22, 0.42, 0.42, 0.62, 0.82, 1.02 };
            const EvenLevels::Passage p = edgePassage(o, f, { 100, 150, 150, 150, 150, 150 }, { 0, 0, 0, 0, 0, 0 });
            const std::vector<bool> u = EvenLevels::unseenNotes(p, edgeEnvelope(p, std::vector<double>(6, 40.0)));
            QVERIFY(u[0] && u[2] && !u[1] && !u[3] && !u[4] && !u[5]);      // (note 2: 5 ms to the next note-on)
            std::vector<bool> fixed(6, false);
            fixed[5] = true;
            std::vector<double> lv(6, 40.0);
            lv[2] = 46.0;
            const std::vector<double> vv = EvenLevels::fitGlide(p, edgeEnvelope(p, lv), fixed);
            QVERIFY(vv[0] == 0.0 && vv[2] == 0.0 && vv[5] == 0.0);
      }

      // the rounds' measure covers every note: a last or first note off its slur's median as heard counts (no silence
      // around the passage here: the attack's rise from silence is in every round's rms alike)
      {
            std::vector<double> lv(n, 46.0);
            const double flatRms = EvenLevels::envelopeRms(k550, edgeEnvelope(k550, lv, 46.0));
            lv[9] += 6.0;
            const double lastRms = EvenLevels::envelopeRms(k550, edgeEnvelope(k550, lv, 46.0));
            lv[9] = 46.0;
            lv[0] += 6.0;
            const double firstRms = EvenLevels::envelopeRms(k550, edgeEnvelope(k550, lv, 46.0));
            QVERIFY2(flatRms < 1e-9 && lastRms > 1.0 && firstRms > 0.3,
                     qPrintable(QString("%1 %2 %3").arg(flatRms).arg(lastRms).arg(firstRms)));
      }

      // the round choice: the job's measured levels of K. 550's Violins 2 bars 1-3 (rms 5.90 -> 2.28 -> 2.12: round 2
      // was kept): the Bb 45.78 -> 50.51 -> 56.23 against its slur's D 39.15 -> 38.25 -> 39.70, 6.6 -> 12.3 -> 16.5 dB
      // off: both rounds rejected, round 0 kept. Bars 24-26 (the C5 49.97 -> 56.65 -> 62.32) the same
      {
            const std::vector<double> levels[3] {
                  lv0,
                  { 42.3, 42.06, 36.3, 43.1, 42.92, 37.38, 44.29, 44.66, 38.25, 50.51 },
                  { 39.74, 38.71, 33.69, 40.09, 40.06, 35.57, 41.16, 42.13, 39.7, 56.23 } };
            const double rms[3] { 5.90, 2.28, 2.12 };
            const bool measured[3] { true, true, true };
            int note = -1;
            QVERIFY(EvenLevels::roundMovesNote(slur, levels[0], levels[2], &note));
            QCOMPARE(note, 9);
            int rejected = 0;
            QCOMPARE(EvenLevels::chooseRound(slur, levels, rms, measured, &rejected), 0);
            QCOMPARE(rejected, 6);
            const std::vector<double> l24[3] {
                  { 45.04, 44.17, 35.01, 48.18, 45.2, 33.1, 48.44, 45.81, 36.04, 49.97 },
                  { 42.71, 40.16, 35.71, 41.61, 40.09, 34.69, 43.21, 42.24, 37.18, 56.65 },
                  { 40.06, 36.24, 32.14, 37.47, 37.09, 31.79, 39.8, 40.95, 38.73, 62.32 } };
            const double rms24[3] { 6.16, 2.73, 2.48 };
            QCOMPARE(EvenLevels::chooseRound(slur, l24, rms24, measured), 0);
            // a round that brings every note closer is kept by its rms; one only 2.9 dB further still kept, 3.1 not
            std::vector<double> better = lv0;
            for (size_t k = 0; k < n; ++k)
                  better[k] = 0.5 * (lv0[k] + 46.0);
            std::vector<double> further = better;
            const std::vector<double> d0 = EvenLevels::slurDeviations(slur, lv0);
            const std::vector<double> levelsB[3] { lv0, better, further };
            QCOMPARE(EvenLevels::chooseRound(slur, levelsB, rms, measured), 2);
            further[2] = lv0[2];                       // (a slur of one note: never further from itself)
            const double med5 = EvenLevels::slurStats(slur, further).median.at(5);
            further[9] = med5 + d0[9] + 2.9;
            const std::vector<double> levelsC[3] { lv0, better, further };
            QCOMPARE(EvenLevels::chooseRound(slur, levelsC, rms, measured), 2);
            further[9] = med5 + d0[9] + 3.1;
            const std::vector<double> levelsD[3] { lv0, better, further };
            QCOMPARE(EvenLevels::chooseRound(slur, levelsD, rms, measured), 1);
            const bool notMeasured[3] { true, true, false };
            QCOMPARE(EvenLevels::chooseRound(slur, levelsD, rms, notMeasured), 1);
      }
      }

//---------------------------------------------------------
//   evenLevelsRelease
//    even-release (2026-11-01): a passage's last note can't be pushed by its own release or decay. The target covers
//    the last note's heard window only, never past its note-off (lastFrom .. targetEnd); a last note too short for it
//    held at 0; a round whose largest value change exceeds round 0's largest deviation from its slur's median +
//    ROUND_REJECT_DB not kept (roundTooBig). The K. 550 rows: i bars 18-20 (nine repeated D's, each its own slur, the
//    last 622 ms; SSO's Performance legato doesn't re-attack a key struck again at velocity 64, so the passage's sound
//    decays from the third note on), the five string parts' times, windows, measured levels and values of the VM job on
//    real SSO with build 522 (even-edges: rounds' values up to +24 dB, the master trim 18 dB deeper); the envelope a
//    stand-in through the measured levels (the score is not in the repo)
//---------------------------------------------------------

// an envelope through each note's measured level at its heard window's middle (linear in dB), the last slope going on
// to the note-off and then a release of 120 dB/s; silence (floor) before the first heard window; smoothed by the one pole
static std::vector<double> decayEnvelope(const EvenLevels::Passage& p, const std::vector<double>& level, double floor)
      {
      const size_t n = p.on.size();
      std::vector<double> c;
      for (size_t k = 0; k < n; ++k)
            c.push_back(p.window[k] + 0.5 * EvenLevels::WINDOW_MS / 1000.0);
      const double slope = n > 1 ? (level[n - 1] - level[n - 2]) / (c[n - 1] - c[n - 2]) : 0.0;
      std::vector<double> env(size_t(p.gridSize()), floor);
      for (int i = 0; i < p.gridSize(); ++i) {
            const double t = p.gridStart() + i * EvenLevels::STEP_MS / 1000.0;
            double v;
            if (t < p.window[0])
                  v = floor;
            else if (t <= c[0])
                  v = level[0];
            else if (t >= c[n - 1]) {
                  v = level[n - 1] + slope * (std::min(t, p.off[n - 1]) - c[n - 1]);
                  if (t > p.off[n - 1])
                        v -= 120.0 * (t - p.off[n - 1]);
                  }
            else {
                  const size_t k = size_t(std::upper_bound(c.begin(), c.end(), t) - c.begin());
                  v = level[k - 1] + (level[k] - level[k - 1]) * (t - c[k - 1]) / (c[k] - c[k - 1]);
                  }
            env[size_t(i)] = std::max(floor, v);
            }
      const double a = 1.0 - std::exp(-EvenLevels::STEP_MS / EvenLevels::SMOOTH_MS);
      double y = env.empty() ? 0.0 : env[0];
      for (double& x : env) {
            y += a * (x - y);
            x = y;
            }
      return env;
      }

void TestPerformanceTechnique::evenLevelsRelease()
      {
      // a passage ending before a rest: eight notes 0.2 s apart in two slurs, flat (46 dB) as heard, the last note
      // 0.6 s long, its sound decaying after its heard window (30 dB/s, a long note's bow fading) to its note-off, then
      // released (120 dB/s) into the rest. Nothing to correct: every value ~0, the first and the last held or ~0; the
      // target's last part ends at the heard window's end, inside the note
      {
            std::vector<double> o, f, wm;
            std::vector<int> sl;
            for (int k = 0; k < 8; ++k) {
                  o.push_back(0.2 * k);
                  f.push_back(k < 7 ? 0.2 * (k + 1) : 0.2 * k + 0.6);
                  wm.push_back(k == 0 ? 100.0 : 150.0);
                  sl.push_back(k / 4);
                  }
            const EvenLevels::Passage p = edgePassage(o, f, wm, sl);
            QVERIFY(std::fabs(EvenLevels::lastFrom(p) - (o[7] + 0.150)) < 1e-9);
            QVERIFY(std::fabs(EvenLevels::targetEnd(p) - (o[7] + 0.150 + EvenLevels::WINDOW_MS / 1000.0)) < 1e-9);
            QVERIFY(EvenLevels::targetEnd(p) < f[7]);
            std::vector<double> env(size_t(p.gridSize()), 0.0);
            const double heardEnd = p.window[7] + EvenLevels::WINDOW_MS / 1000.0;
            for (int i = 0; i < p.gridSize(); ++i) {
                  const double t = p.gridStart() + i * EvenLevels::STEP_MS / 1000.0;
                  double v = t < p.window[0] ? 0.0 : 46.0;
                  if (t > heardEnd)
                        v -= 30.0 * (std::min(t, f[7]) - heardEnd);
                  if (t > f[7])
                        v -= 120.0 * (t - f[7]);
                  env[size_t(i)] = std::max(0.0, v);
                  }
            double fitRms = 0, envRms = 0;
            const std::vector<double> v = EvenLevels::fitGlide(p, env, &fitRms, &envRms);
            qDebug("a passage ending before a rest, its release in the envelope: %s (rms %.2f -> %.2f), envelopeRms %.2f",
                   qPrintable(dbText(v)), envRms, fitRms, EvenLevels::envelopeRms(p, env));
            QCOMPARE(v[0], 0.0);
            for (double x : v)
                  QVERIFY2(std::fabs(x) < 0.5, qPrintable(dbText(v)));
            QVERIFY(EvenLevels::envelopeRms(p, env) < 0.5);

            // the same passage with its last note 4 dB louder as heard: lowered, from its heard window alone
            for (int i = 0; i < p.gridSize(); ++i) {
                  const double t = p.gridStart() + i * EvenLevels::STEP_MS / 1000.0;
                  if (t >= p.window[7])
                        env[size_t(i)] += 4.0;
                  }
            const std::vector<double> loud = EvenLevels::fitGlide(p, env);
            qDebug("its last note +4 dB: %s", qPrintable(dbText(loud)));
            QVERIFY2(loud[7] < -1.5 && loud[7] > -6.0 && std::fabs(loud[2]) < 1.0, qPrintable(dbText(loud)));

            // a last note ending before its heard window, or 5 ms into it: unseen, held at 0
            EvenLevels::Passage shortLast = p;
            shortLast.off[7] = p.window[7] - 0.01;
            QVERIFY(EvenLevels::targetEnd(shortLast) <= EvenLevels::lastFrom(shortLast) + 1e-12);
            QVERIFY(EvenLevels::unseenNotes(shortLast, env)[7]);
            QCOMPARE(EvenLevels::fitGlide(shortLast, env)[7], 0.0);
            shortLast.off[7] = p.window[7] + 0.005;
            QVERIFY(EvenLevels::unseenNotes(shortLast, env)[7]);
            QCOMPARE(EvenLevels::fitGlide(shortLast, env)[7], 0.0);
      }

      // K. 550 i bars 18-20, the five string parts: the job's round-0 levels as a decaying envelope. Before (even-edges)
      // the job's values on the last two notes: +12.9 .. +24 / -1.9 .. -16.6. Now: the first note 0, every value within
      // the passage's own spread, the last two small
      struct Row {
            const char* part;
            std::vector<double> on, win, level[3], values1, values2;
            int chosen522;
            };
      const std::vector<Row> rows {
            { "Violins 1", { 0.0000, 0.1898, 0.3453, 0.6561, 0.8116, 0.9670, 1.2779, 1.4333, 1.5888 },
              { 109, 220, 220, 220, 220, 220, 220, 220, 220 },
              { { 58.6, 54.62, 45.72, 39.97, 32.89, 26.05, 17.4, -0.13, 0.3 },
                { 37.2, 33.74, 25.99, 13.02, 7.02, -2.65, 6.15, -26, -49.33 },
                { 34.34, 31.68, 24.94, 8.62, 4.11, -3.91, 8.25, -23.79, -49.34 } },
              { 0, -4.56, 1.48, 1.16, -3.61, 1.55, -2.43, 12.9, -11.29 },
              { 0, -7.39, 2.55, 1.34, -5.4, 1.77, -0.9, 15.78, -9.57 }, 0 },
            { "Violins 2", { 0.0000, 0.1807, 0.3362, 0.6470, 0.8025, 0.9579, 1.2688, 1.4242, 1.5797 },
              { 102.9, 175, 175, 175, 175, 175, 175, 175, 175 },
              { { 54.62, 52.32, 45.61, 40.31, 31.79, 22.5, 14.56, 1.34, -15.01 },
                { 32.58, 31.05, 25.49, 12.42, 4.09, -6.29, 0.93, -21.22, -61.03 },
                { 29.52, 29.12, 25.92, 6.64, 1.72, -4.92, 8.55, -9.54, -47.68 } },
              { 0, -2.66, 1.03, 0.94, -4.34, 2.6, -3.56, 14.05, -16.61 },
              { 0, -4.45, 2.04, 1.07, -7.49, 4.7, -3.78, 21.56, -14.41 }, 2 },
            { "Violas", { 0.0000, 0.2805, 0.4359, 0.7468, 0.9022, 1.0577, 1.3686, 1.5240, 1.6794 },
              { 254.1, 200, 200, 200, 200, 200, 200, 200, 200 },
              { { 49.25, 50.89, 44.29, 32.45, 25.07, 20.79, 10.03, -7.66, -17.13 },
                { 27.03, 30.95, 23.35, 1.67, -8.73, -13.2, -14.27, -45.62, -83.09 },
                { 26.32, 30.77, 23.44, -1.59, -11.76, -14.83, -13.92, -44.55, -81.2 } },
              { 0, -0.82, 1.56, 0.52, -2.55, 0.03, -2.19, 8.01, -14.76 },
              { 0, -1.2, 2.76, 0.59, -3.78, 0.04, -2.01, 9.23, -13.04 }, 0 },
            { "Celli", { 0.0000, 0.2474, 0.4028, 0.7137, 0.8691, 1.0246, 1.3355, 1.4909, 1.6463 },
              { 174.5, 225, 225, 225, 225, 225, 225, 225, 225 },
              { { 56.9, 56.39, 50.94, 40.24, 34.77, 30.99, 22.4, 15.54, 13.22 },
                { 36.37, 36.74, 31.36, 14.31, 10.11, 5.87, 5.24, -14.23, -29.33 },
                { 34.77, 36.95, 31.98, 11.79, 9.9, 4.61, 12.34, -8.57, -30.19 } },
              { 0, -1.1, 1.1, 1.22, -2.14, 1.61, -2.52, 8.72, -13.59 },
              { 0, -1.79, 2.38, 1.9, -3.15, 2.66, -4.14, 15.18, -12.68 }, 2 },
            { "Basses", { 0.0000, 0.1995, 0.3549, 0.6658, 0.8213, 0.9767, 1.2876, 1.4430, 1.5985 },
              { 104.1, 110, 110, 110, 110, 110, 110, 110, 110 },
              { { 43.76, 50.66, 49.33, 38.51, 32.2, 29.55, 28.01, 10.38, 6.9 },
                { 25.06, 33.22, 32.04, 19.83, 12.59, 10.3, 16.12, 16.48, 7.01 },
                { 23.73, 32.07, 30.9, 18.6, 11.24, 8.64, 13.7, 14.7, 1.37 } },
              { 0, -0.86, 1.07, 0.48, 0.18, 0.85, -8.53, 24, -1.92 },
              { 0, -1.85, 2.29, 0.8, 1.64, -1.45, -10.97, 24, -5.06 }, 1 } };
      for (const Row& r : rows) {
            const size_t n = r.on.size();
            std::vector<double> off;
            for (size_t k = 0; k + 1 < n; ++k)
                  off.push_back(r.on[k + 1]);
            off.push_back(r.on[n - 1] + 0.6218);          // (the last D's 622 ms, as rendered)
            const std::vector<int> slur { 0, 1, 2, 3, 4, 5, 6, 7, 8 };
            const EvenLevels::Passage p = edgePassage(r.on, off, r.win, slur);
            QVERIFY(EvenLevels::targetEnd(p) < off[n - 1]);
            const std::vector<double> env = decayEnvelope(p, r.level[0], -60.0);
            double fitRms = 0, envRms = 0;
            std::vector<double> v = EvenLevels::fitGlide(p, env, &fitRms, &envRms);
            for (double& x : v)
                  x = EvenLevels::clampDb(x / EvenLevels::CC11_FACTOR);
            qDebug("K. 550 bars 18-20 %s round 1: %s (envelopeRms %.2f; own %s)", r.part, qPrintable(dbText(v)),
                   EvenLevels::envelopeRms(p, env), qPrintable(dbText(EvenLevels::ownWeights(p, env))));
            QCOMPARE(v[0], 0.0);
            QVERIFY2(std::fabs(v[n - 2]) < 4.0 && std::fabs(v[n - 1]) < 4.0, qPrintable(dbText(v)));
            for (double x : v)
                  QVERIFY2(std::fabs(x) < 6.0, qPrintable(dbText(v)));

            // the guard on the job's own rounds: one-note slurs (round 0's deviations 0), so any value over 3 dB is
            // bigger than the problem; both build-522 rounds rejected, round 0 kept
            const std::vector<double> values[3] { {}, r.values1, r.values2 };
            const double rms[3] { 3.0, 2.0, 1.0 };      // (rounds 1 and 2 "closer": only the guard rejects them)
            const bool measured[3] { true, true, true };
            int rejected = 0, tooBig = 0;
            QCOMPARE(EvenLevels::chooseRound(slur, r.level, rms, measured, &rejected, values, &tooBig), 0);
            QCOMPARE(tooBig, 6);
            QCOMPARE(EvenLevels::chooseRound(slur, r.level, rms, measured), 2);   // (without values: no guard)
            }

      // the guard's threshold: round 0's largest |level - slur median| + ROUND_REJECT_DB, on the change from round 0's
      // values (the background measurer's round 0 plays the values a note has)
      {
            const std::vector<int> slur { 0, 0, 0, 0 };
            const std::vector<double> lv0 { 40.0, 44.0, 40.0, 40.0 };      // (first / last left out: median 42, max dev 2)
            double largest = 0, allowed = 0;
            QVERIFY(!EvenLevels::roundTooBig(slur, lv0, {}, { 0, -4.9, 0, 0 }, &largest, &allowed));
            QVERIFY(std::fabs(allowed - (2.0 + EvenLevels::ROUND_REJECT_DB)) < 1e-9 && std::fabs(largest - 4.9) < 1e-9);
            QVERIFY(EvenLevels::roundTooBig(slur, lv0, {}, { 0, -5.1, 0, 0 }));
            QVERIFY(!EvenLevels::roundTooBig(slur, lv0, { 0, 3.0, 0, 0 }, { 0, 7.5, 0, 0 }));
            QVERIFY(EvenLevels::roundTooBig(slur, lv0, { 0, 3.0, 0, 0 }, { 0, 8.5, 0, 0 }));
      }
      }

QTEST_MAIN(TestPerformanceTechnique)
#include "tst_performancetechnique.moc"
