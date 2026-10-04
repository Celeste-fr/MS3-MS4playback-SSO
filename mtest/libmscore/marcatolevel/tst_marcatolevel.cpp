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
#include "libmscore/chord.h"
#include "libmscore/excerpt.h"
#include "libmscore/measure.h"
#include "libmscore/undo.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/rendermidi.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/synthesizerstate.h"

#define DIR QString("libmscore/marcatolevel/")

using namespace Ms;

//---------------------------------------------------------
//   TestMarcatoLevel
//    marcatolevel.musicxml: Violins (MusicXML: "strings", SSO's Strings Ensemble: Marcato Attack, on the dynamics CC), Trumpet (SSO: Marcato, on
//    velocity), Tuba; marcato, marcato-staccato and marcato-tenuto among plain notes, mf then f
//---------------------------------------------------------

class TestMarcatoLevel : public QObject, public MTest
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
      MasterScore* score()
            {
            MasterScore* s = readScore(DIR + "marcatolevel.musicxml");
            if (s)
                  s->rebuildMidiMapping();
            return s;
            }
      // every event, a line each ("tick type channel a b port extChannel switch patch"), sorted (MIDI export's
      // same-tick order isn't stable); library: the SSO map plays every part, else the built-in synthesizer
      static QStringList events(MasterScore* score, std::shared_ptr<SoundLib::Library> library)
            {
            SoundLib::setCurrent(library);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
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

      // the chords of a part's first voice, in order
      static std::vector<Chord*> chords(Score* score, int part)
            {
            std::vector<Chord*> list;
            const int track = score->parts().at(part)->startTrack();
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
                  if (s->element(track) && s->element(track)->isChord())
                        list.push_back(toChord(s->element(track)));
            return list;
            }
      static Articulation* marcato(Chord* chord)
            {
            for (Articulation* a : chord->articulations())
                  if (a->isMarcato())
                        return a;
            return nullptr;
            }
      static Articulation* marcato(Score* score, int part, int chord)
            {
            const std::vector<Chord*> c = chords(score, part);
            return chord < int(c.size()) ? marcato(c[size_t(chord)]) : nullptr;
            }
      static void setLevel(Articulation* a, double db)
            {
            a->score()->startCmd();
            a->undoChangeProperty(Pid::MARCATO_LEVEL, db);
            a->score()->endCmd();
            }
      static QString fileText(const QString& name)
            {
            QFile f(name);
            if (!f.open(QIODevice::ReadOnly))
                  return QString();
            return QString::fromUtf8(f.readAll());
            }
      struct Ev { int tick; NPlayEvent e; };
      // the rendered events of one MIDI-out route (library) or channel (built-in), in order
      static std::vector<Ev> render(MasterScore* score, std::shared_ptr<SoundLib::Library> library, int port, int channel)
            {
            SoundLib::setCurrent(library);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            EventMap events;
            score->renderMidi(&events, false, true, SynthesizerState());
            SoundLib::setCurrent(nullptr);
            SoundLib::setOutput(SoundLib::Output::MIDI);
            std::vector<Ev> list;
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (library ? (e.isExternal() && e.extPort() == port && e.extChannel() == channel)
                              : (!e.isExternal() && e.channel() == channel))
                        list.push_back({ te.first, e });
                  }
            return list;
            }
      static int noteOn(const std::vector<Ev>& ev, int pitch, int* tick = nullptr, size_t* index = nullptr)
            {
            for (size_t i = 0; i < ev.size(); ++i)
                  if (ev[i].e.type() == ME_NOTEON && ev[i].e.velo() > 0 && !ev[i].e.librarySwitch() && ev[i].e.pitch() == pitch) {
                        if (tick)
                              *tick = ev[i].tick;
                        if (index)
                              *index = i;
                        return ev[i].e.velo();
                        }
            return -1;
            }
      // the value of controller cc in force right before event index (-1: none)
      static int controllerBefore(const std::vector<Ev>& ev, size_t index, int cc)
            {
            for (size_t i = index; i-- > 0;)
                  if (ev[i].e.type() == ME_CONTROLLER && !ev[i].e.librarySwitch() && ev[i].e.controller() == cc)
                        return ev[i].e.value();
            return -1;
            }

   private slots:
      void initTestCase() { initMTest(); }
      void cleanup() { SoundLib::setCurrent(nullptr); SoundLib::setOutput(SoundLib::Output::MIDI);
                       SoundLib::setDynamicsCalibration(nullptr); }
      void defaultUnchanged();
      void law();
      void saveLoad();
      void undoRedo();
      void copyPaste();
      void parts();
      void builtIn();
      void velocityPath();
      void controllerPath();
      };

//---------------------------------------------------------
//   defaultUnchanged
//    marcatos without a level play as before the setting existed: the events (built-in and SSO) of
//    main 922a4849dc (SSO's velocity marcatos regenerated 2026-10-02 without MS4's accent boost: mf 103 -> 80, f 123 -> 96; regenerated
//    2026-10-04 for [legato] phraseGapMs: five SSO note-offs before a fresh attack on the legato patch end 44-58 ticks earlier,
//    the built-in events unchanged), kept in marcatolevel-events.txt. MS_MARCATO_DUMP_OUT=<file> writes this build's
//    events there instead (to make the reference again after an intended playback change)
//---------------------------------------------------------

void TestMarcatoLevel::defaultUnchanged()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* s = score();
      QVERIFY(s);
      QStringList all;
      all << "# built-in" << events(s, nullptr) << "# SSO" << events(s, lib);
      const QString text = all.join('\n') + '\n';
      const QString out = qEnvironmentVariable("MS_MARCATO_DUMP_OUT");
      if (!out.isEmpty()) {
            QFile f(out);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(text.toUtf8());
            delete s;
            return;
            }
      QFile f(root + "/" + DIR + "marcatolevel-events.txt");
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QStringList expected = QString::fromUtf8(f.readAll()).split('\n');
      const QStringList got = text.split('\n');
      for (int i = 0; i < qMin(expected.size(), got.size()); ++i)
            if (expected[i] != got[i])
                  QFAIL(qPrintable(QString("line %1: %2, expected %3").arg(i + 1).arg(got[i], expected[i])));
      QCOMPARE(got.size(), expected.size());
      delete s;
      }

//---------------------------------------------------------
//   law
//    SoundFont 2's default velocity curve (FluidSynth): 40 log10(v / 127) dB
//---------------------------------------------------------

void TestMarcatoLevel::law()
      {
      QCOMPARE(MarcatoLevel::velocity(80, 0.0), 80);
      QCOMPARE(MarcatoLevel::velocity(103, -6.0), 73);            // 103 * 10^(-6/40) = 72.9
      QCOMPARE(MarcatoLevel::velocity(80, 3.0), 95);              // 80 * 10^(3/40) = 95.1
      QCOMPARE(MarcatoLevel::velocity(120, 12.0), 127);
      QCOMPARE(MarcatoLevel::velocity(2, -24.0), 1);
      for (int v : { 20, 64, 100 }) {
            const int w = MarcatoLevel::velocity(v, -6.0);
            QVERIFY(std::abs(40 * std::log10(double(w) / v) + 6.0) < 0.4);
            }
      }

//---------------------------------------------------------
//   saveLoad
//    the level is in the metaTag, not in the articulation's XML; read back onto the articulation; saved
//    again the same; a score without levels has no metaTag; one that doesn't match (moved elsewhere) is dropped
//---------------------------------------------------------

void TestMarcatoLevel::saveLoad()
      {
      MasterScore* s = score();
      QVERIFY(s);
      QVERIFY(saveScore(s, "marcatolevel-none.mscx"));
      QVERIFY(!fileText("marcatolevel-none.mscx").contains("marcatoLevel"));

      Articulation* a = marcato(s, 1, 0);              // the trumpet's first note
      QVERIFY(a);
      setLevel(a, -6.5);
      Articulation* b = marcato(s, 0, 2);              // the violins' B4
      QVERIFY(b);
      setLevel(b, 3);
      QVERIFY(saveScore(s, "marcatolevel-saved.mscx"));
      const QString text = fileText("marcatolevel-saved.mscx");
      QVERIFY2(text.contains("<metaTag name=\"marcatoLevels\">[{&quot;db&quot;:-6.5,&quot;sym&quot;:&quot;articMarcatoAbove&quot;,"
                             "&quot;tick&quot;:0,&quot;track&quot;:4},{&quot;db&quot;:3,&quot;sym&quot;:&quot;articMarcatoAbove&quot;,"
                             "&quot;tick&quot;:960,&quot;track&quot;:0}]</metaTag>"),
               qPrintable(text.section("<metaTag name=\"marcatoLevels\">", 1).left(300)));
      QCOMPARE(text.count("marcatoLevel"), 1);          // the metaTag only
      // apart from the metaTag, the file is the one without levels (MuseScore 3.6 reads it unchanged)
      QString without = text;
      without.remove(QRegularExpression("\\s*<metaTag name=\"marcatoLevels\">[^<]*</metaTag>"));
      QCOMPARE(without, fileText("marcatolevel-none.mscx"));

      MasterScore* again = readCreatedScore("marcatolevel-saved.mscx");
      QVERIFY(again);
      QCOMPARE(marcato(again, 1, 0)->marcatoLevel(), -6.5);
      QCOMPARE(marcato(again, 0, 2)->marcatoLevel(), 3.0);
      QCOMPARE(marcato(again, 0, 0)->marcatoLevel(), 0.0);
      QVERIFY(again->metaTag(MarcatoLevel::metaTag).isEmpty());          // on the articulations, not in the tags
      QVERIFY(saveScore(again, "marcatolevel-saved2.mscx"));
      QCOMPARE(fileText("marcatolevel-saved2.mscx"), text);
      delete again;

      // a marcato the list doesn't find (its note moved or deleted in MuseScore 3.6): no level; nor another one
      QString edited = text;
      edited.replace("&quot;tick&quot;:960", "&quot;tick&quot;:1440");
      QFile f("marcatolevel-edited.mscx");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(edited.toUtf8());
      f.close();
      MasterScore* other = readCreatedScore("marcatolevel-edited.mscx");
      QVERIFY(other);
      QCOMPARE(marcato(other, 0, 2)->marcatoLevel(), 0.0);
      QCOMPARE(marcato(other, 1, 0)->marcatoLevel(), -6.5);
      delete other;
      delete s;
      }

//---------------------------------------------------------
//   undoRedo
//    as the Inspector sets it (undoChangeProperty), and reset (0: the library's)
//---------------------------------------------------------

void TestMarcatoLevel::undoRedo()
      {
      MasterScore* s = score();
      QVERIFY(s);
      Articulation* a = marcato(s, 1, 0);
      QVERIFY(a);
      QCOMPARE(a->getProperty(Pid::MARCATO_LEVEL).toDouble(), 0.0);
      QCOMPARE(a->propertyDefault(Pid::MARCATO_LEVEL).toDouble(), 0.0);
      setLevel(a, -4);
      QCOMPARE(a->marcatoLevel(), -4.0);
      s->undoRedo(true, 0);
      QCOMPARE(a->marcatoLevel(), 0.0);
      s->undoRedo(false, 0);
      QCOMPARE(a->marcatoLevel(), -4.0);
      setLevel(a, 99);                                  // clamped
      QCOMPARE(a->marcatoLevel(), MarcatoLevel::MAX_DB);
      s->startCmd();
      a->undoResetProperty(Pid::MARCATO_LEVEL);
      s->endCmd();
      QCOMPARE(a->marcatoLevel(), 0.0);
      s->undoRedo(true, 0);
      QCOMPARE(a->marcatoLevel(), MarcatoLevel::MAX_DB);
      delete s;
      }

//---------------------------------------------------------
//   copyPaste
//    measure 1 of the trumpet pasted over measure 2: the pasted marcato keeps its level
//---------------------------------------------------------

void TestMarcatoLevel::copyPaste()
      {
      MasterScore* s = score();
      QVERIFY(s);
      setLevel(marcato(s, 1, 0), -3);
      Measure* m1 = s->firstMeasure();
      Measure* m2 = m1->nextMeasure();
      s->select(m1, SelectType::RANGE, 1);
      QVERIFY(s->selection().canCopy());
      QMimeData* mimeData = new QMimeData;
      mimeData->setData(s->selection().mimeType(), s->selection().mimeData());
      s->select(m2->first(SegmentType::ChordRest)->element(4));
      s->startCmd();
      s->cmdPaste(mimeData, 0);
      s->endCmd();
      delete mimeData;
      const std::vector<Chord*> c = chords(s, 1);
      QCOMPARE(int(c.size()), 8);
      QCOMPARE(marcato(c[4])->marcatoLevel(), -3.0);
      QCOMPARE(marcato(c[6])->marcatoLevel(), 0.0);
      delete s;
      }

//---------------------------------------------------------
//   parts
//    a part's copy follows the score's (linked), and the part keeps it through a save and load
//---------------------------------------------------------

void TestMarcatoLevel::parts()
      {
      MasterScore* s = score();
      QVERIFY(s);
      QList<Part*> parts;
      parts.append(s->parts().at(1));
      Score* nscore = new Score(s);
      Excerpt* ex = new Excerpt(s);
      ex->setPartScore(nscore);
      ex->setParts(parts);
      ex->setTitle(parts.front()->partName());
      Excerpt::createExcerpt(ex);
      s->excerpts().append(ex);

      Articulation* partMarcato = marcato(nscore, 0, 0);
      QVERIFY(partMarcato);
      QVERIFY(partMarcato != marcato(s, 1, 0));
      setLevel(marcato(s, 1, 0), -2.5);
      QCOMPARE(partMarcato->marcatoLevel(), -2.5);
      s->undoRedo(true, 0);
      QCOMPARE(partMarcato->marcatoLevel(), 0.0);
      s->undoRedo(false, 0);
      QCOMPARE(partMarcato->marcatoLevel(), -2.5);

      QVERIFY(saveScore(s, "marcatolevel-parts.mscx"));
      QCOMPARE(fileText("marcatolevel-parts.mscx").count("<metaTag name=\"marcatoLevels\">"), 2);   // score and part
      MasterScore* again = readCreatedScore("marcatolevel-parts.mscx");
      QVERIFY(again);
      QCOMPARE(int(again->excerpts().size()), 1);
      Score* part = again->excerpts().front()->partScore();
      QCOMPARE(marcato(again, 1, 0)->marcatoLevel(), -2.5);
      QCOMPARE(marcato(part, 0, 0)->marcatoLevel(), -2.5);
      QCOMPARE(marcato(part, 0, 2)->marcatoLevel(), 0.0);
      QVERIFY(part->metaTag(MarcatoLevel::metaTag).isEmpty());
      delete again;
      delete s;
      }

//---------------------------------------------------------
//   builtIn
//    the built-in synthesizer (MS4 note model): the marcato's velocity by SoundFont 2's law; the other notes as before
//---------------------------------------------------------

void TestMarcatoLevel::builtIn()
      {
      MasterScore* s = score();
      QVERIFY(s);
      const int channel = s->parts().at(1)->instrument()->channel(0)->channel();
      const std::vector<Ev> before = render(s, nullptr, 0, channel);
      const int v = noteOn(before, 72);
      QVERIFY(v > 0);
      setLevel(marcato(s, 1, 0), -6);
      const std::vector<Ev> after = render(s, nullptr, 0, channel);
      QCOMPARE(noteOn(after, 72), MarcatoLevel::velocity(v, -6));
      QCOMPARE(int(after.size()), int(before.size()));
      int changed = 0;
      for (size_t i = 0; i < before.size(); ++i)
            changed += before[i].tick != after[i].tick || !(before[i].e == after[i].e);
      QCOMPARE(changed, 1);
      delete s;
      }

//---------------------------------------------------------
//   velocityPath
//    SSO's Trumpet Solo: a short marcato plays "Marcato" (on velocity): the velocity by the law without a
//    measured curve, by the curve with one (dynamics.json); nothing else changes
//---------------------------------------------------------

void TestMarcatoLevel::velocityPath()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* s = score();
      QVERIFY(s);
      QCOMPARE(s->parts().at(1)->instrument()->getId(), QString("trumpet"));
      const std::vector<Ev> before = render(s, lib, 0, 1);
      QCOMPARE(noteOn(before, 72), 80);                  // mf: the plain level, no MS4 accent boost
      QCOMPARE(noteOn(render(s, lib, 0, 3), 48), 80);    // the tuba likewise (was 103)
      QCOMPARE(noteOn(before, 79), 96);                  // marcato-tenuto at f: the plain f (was 123)
      setLevel(marcato(s, 1, 0), -6);
      const std::vector<Ev> after = render(s, lib, 0, 1);
      QCOMPARE(noteOn(after, 72), 57);                   // the law: 80 * 10^(-6/40)
      QCOMPARE(int(after.size()), int(before.size()));
      int changed = 0;
      for (size_t i = 0; i < before.size(); ++i)
            changed += before[i].tick != after[i].tick || !(before[i].e == after[i].e);
      QCOMPARE(changed, 1);

      // measured (tools/soundlibraries/sso_sound_dynamics.json, Trumpet Solo's Marcato): -33.1 dB at 80,
      // -6 dB from there (-39.1) at velocity 56
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      SoundLib::DynamicsCurve c;
      c.drivenBy = "velocity";
      c.points = { { 16, -60.2 }, { 32, -46 }, { 48, -40.8 }, { 64, -37.2 }, { 80, -33.1 }, { 96, -27.6 }, { 112, -25.2 }, { 127, -21.3 } };
      cal->setCurve("Trumpet Solo", 52, c);
      SoundLib::setDynamicsCalibration(cal);
      QCOMPARE(noteOn(render(s, lib, 0, 1), 72), 56);
      setLevel(marcato(s, 1, 0), 6);
      QCOMPARE(noteOn(render(s, lib, 0, 1), 72), 99);      // -27.1 dB
      delete s;
      }

//---------------------------------------------------------
//   controllerPath
//    SSO's Strings Ensemble (the strings part): a marcato plays "Marcato Attack" (on the dynamics CC): softer, the expression CC (CC11) on
//    the route right before its note-on, back to 127 right before the next note-on; louder, the dynamics CC (CC1)
//    likewise, a dynamic in between mapped too. The note-ons themselves don't change
//---------------------------------------------------------

void TestMarcatoLevel::controllerPath()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* s = score();
      QVERIFY(s);
      QCOMPARE(s->parts().at(0)->instrument()->getId(), QString("strings"));        // (SSO: Strings Ensemble)
      const std::vector<Ev> before = render(s, lib, 0, 0);

      setLevel(marcato(s, 0, 0), -6);                   // G4, beat 1
      std::vector<Ev> after = render(s, lib, 0, 0);
      size_t g = 0, a = 0;
      QCOMPARE(noteOn(after, 67, nullptr, &g), noteOn(before, 67));
      QCOMPARE(controllerBefore(after, g, 11), 90);      // 127 * 10^(-6/40)
      int aTick = 0;
      noteOn(after, 69, &aTick, &a);
      QCOMPARE(controllerBefore(after, a, 11), 127);
      QCOMPARE(after[a - 1].e.controller(), 11);         // right before it
      QCOMPARE(after[a - 1].tick, aTick);
      QCOMPARE(int(after.size()), int(before.size()) + 2);

      // with the held note's measured expression curve: CC11 at which it is 6 dB under 127's level
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      SoundLib::DynamicsCurve held;
      held.drivenBy = "controller";
      held.points = { { 32, -41.4 }, { 80, -32.6 }, { 112, -32 }, { 127, -29.8 } };
      held.expression = { { 1, -80 }, { 64, -12 }, { 96, -5 }, { 127, 0 } };
      cal->setCurve("Strings Ensemble", 1, held);
      SoundLib::setDynamicsCalibration(cal);
      after = render(s, lib, 0, 0);
      noteOn(after, 67, nullptr, &g);
      QCOMPARE(controllerBefore(after, g, 11), 91);      // -6 dB: 64 + (6/7) * 32 = 91.4
      SoundLib::setDynamicsCalibration(nullptr);

      // louder: the dynamics CC, from mf's 80; f's 96 at the next marcato's tick (C5, m2) mapped, then 96 again
      setLevel(marcato(s, 0, 0), 0);
      setLevel(marcato(s, 0, 2), 3);                    // B4, beats 3-4
      after = render(s, lib, 0, 0);
      size_t b = 0, c = 0;
      QCOMPARE(noteOn(after, 71, nullptr, &b), noteOn(before, 71));
      QCOMPARE(controllerBefore(after, b, 1), 95);       // 80 * 10^(3/40)
      int cTick = 0;
      noteOn(after, 72, &cTick, &c);
      QCOMPARE(controllerBefore(after, c, 1), 96);       // f, as before
      QCOMPARE(controllerBefore(after, c, 11), 127);
      delete s;
      }

QTEST_MAIN(TestMarcatoLevel)
#include "tst_marcatolevel.moc"
