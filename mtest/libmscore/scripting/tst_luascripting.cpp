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
#include "libmscore/segment.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/articulation.h"
#include "libmscore/stafftext.h"
#include "libmscore/undo.h"
#include "libmscore/script/scriptengine.h"

#define DIR QString("libmscore/scripting/")

using namespace Ms;

//---------------------------------------------------------
//   TestScripting
//    the Lua score scripts' host (libmscore/script/scriptengine.h) on scripting.musicxml:
//    Violin (G minor, 4/4):  1: G4 staccato p | A4 ( Bb4  C5 )     2: D4+F4 half | D5 half, tied on
//                            3: D5 quarter (the tie's end) | grace Eb5, F5 | rest | G5 f
//    Clarinet in B flat:     1: written C5, a whole note (sounds B flat 4)   2-3: rests
//    The query library itself is tested without MuseScore by libmscore/script/test/run.lua.
//---------------------------------------------------------

class TestScripting : public QObject, public MTest
      {
      Q_OBJECT

      MasterScore* load()
            {
            MasterScore* score = readScore(DIR + "scripting.musicxml");
            if (score) {
                  score->rebuildMidiMapping();
                  score->doLayout();
                  }
            return score;
            }
      static Ms::Chord* chordAt(Score* score, int measure, int tickInMeasure, int track = 0)
            {
            Measure* m = score->firstMeasure();
            for (int i = 1; i < measure && m; ++i)
                  m = m->nextMeasure();
            if (!m)
                  return nullptr;
            Segment* s = m->findSegment(SegmentType::ChordRest, m->tick() + Fraction::fromTicks(tickInMeasure));
            Element* e = s ? s->element(track) : nullptr;
            return e && e->isChord() ? toChord(e) : nullptr;
            }
      static int marks(const Ms::Chord* c, SymId above, SymId below)
            {
            int n = 0;
            for (const Articulation* a : c->articulations())
                  if (a->symId() == above || a->symId() == below)
                        ++n;
            return n;
            }
      static QString printed(const ScriptResult& r)
            {
            QStringList l;
            for (const ScriptLog& e : r.log)
                  if (e.kind == "print")
                        l << e.text;
            return l.join("|");
            }
      static int staffTexts(Score* score, const QString& text)
            {
            int n = 0;
            for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure())
                  for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest))
                        for (Element* e : s->annotations())
                              if (e->isStaffText() && toStaffText(e)->plainText() == text)
                                    ++n;
            return n;
            }

   private slots:
      void initTestCase() { initMTest(); }
      void engine();
      void snapshot();
      void queries();
      void addMarks();
      void chordsAndTies();
      void staffText();
      void failedScriptRollsBack();
      void preview();
      void limits();
      void console();
      void editorHelpers();
      };

//---------------------------------------------------------
//   engine
//    the embedded library loads; the sandbox holds without a score
//---------------------------------------------------------

void TestScripting::engine()
      {
      ScriptEngine e;
      QVERIFY2(e.valid(), qPrintable(e.initError()));
      QVERIFY(ScriptEngine::librarySource().startsWith("-- Score Script"));
      ScriptResult r = e.run("print(1 + 1, ('ab'):rep(2))");
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(printed(r), QString("2  abab"));
      for (const char* name : { "os", "io", "require", "debug", "load", "dofile", "package", "coroutine" }) {
            r = e.run(QString("return %1").arg(name));
            QVERIFY2(r.error.contains("isn't defined"), name);
            }
      }

//---------------------------------------------------------
//   snapshot
//    libmscore -> the notes a script sees
//---------------------------------------------------------

void TestScripting::snapshot()
      {
      MasterScore* score = load();
      QVERIFY(score);
      const ScriptSnapshot s = ScriptEngine::snapshot(score);
      QCOMPARE(s.parts.size(), 2);
      QCOMPARE(s.parts[0].name, QString("Violin"));
      QCOMPARE(s.parts[0].instrument, QString("violin"));
      QCOMPARE(s.parts[0].family, QString("orchestral-strings"));
      QCOMPARE(s.parts[0].group, QString("strings"));
      QCOMPARE(s.parts[1].family, QString("clarinets"));
      QCOMPARE(s.parts[1].group, QString("woodwinds"));
      // 10 violin notes (the tie's continuation is no note of its own; the grace note is one) + 1 clarinet note
      QCOMPARE(s.notes.size(), 11);
      auto find = [&s](const QString& name, int part) -> const ScriptNote* {
            for (const ScriptNote& n : s.notes)
                  if (n.name == name && n.part == part)
                        return &n;
            return nullptr;
            };
      const ScriptNote* g4 = find("G4", 1);
      QVERIFY(g4);
      QCOMPARE(g4->measure, 1);
      QCOMPARE(g4->index, 1);
      QCOMPARE(g4->beat, 1.0);
      QCOMPARE(g4->duration, 1.0);
      QCOMPARE(g4->pitch, 67);
      QCOMPARE(g4->degree, 1);                  // G minor
      QCOMPARE(g4->dynamic, QString("p"));
      QCOMPARE(g4->marks, QStringList { "staccato" });
      QVERIFY(g4->slurs.isEmpty());
      const ScriptNote* bb = find("Bb4", 1);
      QVERIFY(bb);
      QCOMPARE(bb->beat, 3.0);
      QCOMPARE(bb->degree, 3);
      QCOMPARE(bb->slurs.size(), 1);
      QCOMPARE(bb->slurs[0], qMakePair(2, 3));
      const ScriptNote* d5 = find("D5", 1);
      QVERIFY(d5);
      QVERIFY(d5->tied);
      QCOMPARE(d5->measure, 2);
      QCOMPARE(d5->duration, 3.0);              // a half tied to a quarter
      const ScriptNote* f4 = find("F4", 1);
      const ScriptNote* d4 = find("D4", 1);
      QVERIFY(f4 && d4);
      QVERIFY(f4->chord && f4->top && !f4->bottom && d4->bottom && !d4->top);
      QCOMPARE(f4->chordId, d4->chordId);
      QCOMPARE(f4->beat, 1.0);
      QCOMPARE(d5->beat, 3.0);
      const ScriptNote* grace = find("Eb5", 1);
      QVERIFY(grace);
      QVERIFY(grace->grace);
      QCOMPARE(grace->measure, 3);
      const ScriptNote* g5 = find("G5", 1);
      QVERIFY(g5);
      QCOMPARE(g5->dynamic, QString("f"));
      QCOMPARE(g5->beat, 4.0);
      const ScriptNote* cl = find("C5", 2);     // written C5, sounding B flat 4, no dynamic
      QVERIFY(cl);
      QCOMPARE(cl->pitch, 72);
      QCOMPARE(cl->sounding, 70);
      QCOMPARE(cl->duration, 4.0);
      QVERIFY(cl->dynamic.isEmpty());
      // the same score again: the same signature; after an edit: another
      QCOMPARE(ScriptEngine::snapshot(score).signature, s.signature);
      delete score;
      }

//---------------------------------------------------------
//   queries
//---------------------------------------------------------

void TestScripting::queries()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      QVERIFY2(e.valid(), qPrintable(e.initError()));
      auto count = [&](const QString& conditions) -> QString {
            const ScriptResult r = e.runOnScore(score, QString("print(count(%1))").arg(conditions), ScriptEngine::Preview);
            return r.ok() ? printed(r) : r.error;
            };
      QCOMPARE(count("strings"), QString("9"));                    // grace notes left out
      QCOMPARE(count("violin, grace"), QString("1"));
      QCOMPARE(count("violin1"), QString("9"));
      QCOMPARE(count("violins"), QString("9"));
      QCOMPARE(count("winds"), QString("1"));
      QCOMPARE(count("clarinets, pitch == 72, sounding == 70"), QString("1"));
      QCOMPARE(count("part(\"Clarinet in B♭\")"), QString("1"));
      QCOMPARE(count("violin, 1 <= measure <= 2"), QString("7"));
      QCOMPARE(count("violin, beat == 3"), QString("2"));          // Bb4, D5
      QCOMPARE(count("violin, degree == 1"), QString("2"));        // G4, G5
      QCOMPARE(count("slur"), QString("3"));
      QCOMPARE(count("slur.first"), QString("1"));
      QCOMPARE(count("slur, slur.index == slur.length"), QString("1"));
      QCOMPARE(count("has(staccato)"), QString("1"));
      QCOMPARE(count("tied, duration == 3"), QString("1"));
      QCOMPARE(count("chord, top"), QString("1"));
      QCOMPARE(count("dynamic == p"), QString("8"));
      QCOMPARE(count("dynamic, dynamic >= f"), QString("1"));
      QCOMPARE(count("not dynamic"), QString("1"));                // the clarinet
      QCOMPARE(count("name == \"Bb4\""), QString("1"));
      QCOMPARE(count("duration == half"), QString("2"));
      QCOMPARE(count("mesure == 1"), QString("line 1: argument 1 of count: 'mesure' isn't a note property: did you mean measure?"));
      const ScriptResult r = e.runOnScore(score, "findall(violin, measure == 1)", ScriptEngine::Preview);
      QCOMPARE(r.matched.size(), 4);
      QCOMPARE(e.notes(r.matched).size(), 4);
      QCOMPARE(e.notes(r.matched)[0]->pitch(), 67);
      delete score;
      }

//---------------------------------------------------------
//   addMarks
//    one undo step per run; idempotent; undo takes all of it back
//---------------------------------------------------------

void TestScripting::addMarks()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      ScriptResult r = e.runOnScore(score, "findall(violin, measure == 1).add(marcato)");
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(r.changes, 4);
      for (int beat = 0; beat < 4; ++beat) {
            Ms::Chord* c = chordAt(score, 1, beat * 480);
            QVERIFY(c);
            QCOMPARE(marks(c, SymId::articMarcatoAbove, SymId::articMarcatoBelow), 1);
            }
      // again: nothing to do, and never toggled off
      r = e.runOnScore(score, "findall(violin, measure == 1).add(marcato)");
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(r.changes, 0);
      QCOMPARE(marks(chordAt(score, 1, 0), SymId::articMarcatoAbove, SymId::articMarcatoBelow), 1);
      // the staccato the score has already: three new ones
      r = e.runOnScore(score, "findall(violin, measure == 1):add(staccato)");
      QCOMPARE(r.changes, 3);
      QCOMPARE(marks(chordAt(score, 1, 0), SymId::articStaccatoAbove, SymId::articStaccatoBelow), 1);
      // remove, then undo each run: one step each
      r = e.runOnScore(score, "findall(violin, measure == 1).remove(marcato, staccato)");
      QCOMPARE(r.changes, 8);
      QCOMPARE(chordAt(score, 1, 0)->articulations().size(), 0);
      score->undoRedo(true, nullptr);           // the removal
      QCOMPARE(chordAt(score, 1, 0)->articulations().size(), 2);
      score->undoRedo(true, nullptr);           // the staccatos
      score->undoRedo(true, nullptr);           // the marcatos (the run that changed nothing made no step)
      QCOMPARE(marks(chordAt(score, 1, 480), SymId::articMarcatoAbove, SymId::articMarcatoBelow), 0);
      QCOMPARE(chordAt(score, 1, 0)->articulations().size(), 1);        // the score's own staccato
      delete score;
      }

//---------------------------------------------------------
//   chordsAndTies
//    a mark once per chord; a tied note is one note: its continuation gets nothing
//---------------------------------------------------------

void TestScripting::chordsAndTies()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      ScriptResult r = e.runOnScore(score, "findall(violin, measure == 2, chord).add(accent)");
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(r.matched.size(), 2);
      QCOMPARE(r.changes, 1);
      QCOMPARE(marks(chordAt(score, 2, 0), SymId::articAccentAbove, SymId::articAccentBelow), 1);
      r = e.runOnScore(score, "findall(violin).add(tenuto)");
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(marks(chordAt(score, 2, 960), SymId::articTenutoAbove, SymId::articTenutoBelow), 1);   // D5, the tie's start
      QCOMPARE(marks(chordAt(score, 3, 0), SymId::articTenutoAbove, SymId::articTenutoBelow), 0);     // its continuation
      Ms::Chord* f5 = chordAt(score, 3, 480);
      QVERIFY(f5);
      QCOMPARE(marks(f5, SymId::articTenutoAbove, SymId::articTenutoBelow), 1);
      QCOMPARE(f5->graceNotes().size(), 1);
      QCOMPARE(marks(f5->graceNotes()[0], SymId::articTenutoAbove, SymId::articTenutoBelow), 0);      // not asked for
      r = e.runOnScore(score, "findall(violin, grace).add(accent)");
      QCOMPARE(r.changes, 1);
      QCOMPARE(marks(f5->graceNotes()[0], SymId::articAccentAbove, SymId::articAccentBelow), 1);
      r = e.runOnScore(score, "last(violin).add(fermata)\nprint(count(has(fermata)))");
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(printed(r), QString("1"));
      r = e.runOnScore(score, "last(violin).add(fermata)");
      QCOMPARE(r.changes, 0);
      delete score;
      }

//---------------------------------------------------------
//   staffText
//---------------------------------------------------------

void TestScripting::staffText()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      ScriptResult r = e.runOnScore(score, "findall(violin, measure == 2).text(\"pizz.\")");
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(r.changes, 2);                   // D5 and the dyad: once per chord
      QCOMPARE(staffTexts(score, "pizz."), 2);
      e.runOnScore(score, "findall(violin, measure == 2).text(\"pizz.\")");
      QCOMPARE(staffTexts(score, "pizz."), 2);  // the same text isn't stacked
      score->undoRedo(true, nullptr);
      QCOMPARE(staffTexts(score, "pizz."), 0);
      delete score;
      }

//---------------------------------------------------------
//   failedScriptRollsBack
//---------------------------------------------------------

void TestScripting::failedScriptRollsBack()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      const int steps = score->undoStack()->getCurIdx();
      ScriptResult r = e.runOnScore(score, "findall(violin, measure == 1).add(tenuto)\nerror('stop here')");
      QVERIFY(!r.ok());
      QCOMPARE(r.errorLine, 2);
      QVERIFY2(r.error.contains("stop here"), qPrintable(r.error));
      QCOMPARE(marks(chordAt(score, 1, 0), SymId::articTenutoAbove, SymId::articTenutoBelow), 0);
      QCOMPARE(score->undoStack()->getCurIdx(), steps);
      // and the next run starts from the score as it is
      r = e.runOnScore(score, "print(count(has(tenuto)))");
      QCOMPARE(printed(r), QString("0"));
      delete score;
      }

//---------------------------------------------------------
//   preview
//    nothing reaches the score or the undo stack; the result says what a run would do
//---------------------------------------------------------

void TestScripting::preview()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      const int steps = score->undoStack()->getCurIdx();
      ScriptResult r = e.runOnScore(score, "findall(violin, measure == 1).add(marcato)\nprint(count(has(marcato)))", ScriptEngine::Preview);
      QVERIFY2(r.ok(), qPrintable(r.error));
      QVERIFY(r.preview);
      QCOMPARE(r.changes, 4);
      QCOMPARE(r.matched.size(), 4);
      QCOMPARE(printed(r), QString("4"));
      QCOMPARE(chordAt(score, 1, 0)->articulations().size(), 1);
      QCOMPARE(score->undoStack()->getCurIdx(), steps);
      // a preview needs no score: a second engine on the snapshot alone (the console's worker thread)
      ScriptEngine worker;
      QVERIFY(worker.setSnapshot(ScriptEngine::snapshot(score)));
      r = worker.run("findall(violin, measure == 1)", ScriptEngine::Preview);
      QCOMPARE(r.matched.size(), 4);
      delete score;
      }

//---------------------------------------------------------
//   limits
//---------------------------------------------------------

void TestScripting::limits()
      {
      ScriptEngine e;
      e.setInstructionLimit(200000);
      ScriptResult r = e.run("while true do end");
      QVERIFY2(r.error.contains("ran too long"), qPrintable(r.error));
      r = e.run("while true do pcall(function() while true do end end) end");
      QVERIFY2(r.error.contains("ran too long"), qPrintable(r.error));
      r = e.run("print(1)");                    // and the engine goes on
      QCOMPARE(printed(r), QString("1"));
      e.setInstructionLimit(0);
      e.cancel();                               // (run() clears an old cancel)
      r = e.run("print(2)");
      QCOMPARE(printed(r), QString("2"));
      e.setInstructionLimit(2000000000);
      e.setMemoryLimit(size_t(16) * 1024 * 1024);
      r = e.run("local t = {}\nfor i = 1, 100000000 do t[i] = tostring(i) end");
      QVERIFY2(r.error.contains("too much memory"), qPrintable(r.error));
      r = e.run("print(3)");
      QCOMPARE(printed(r), QString("3"));
      }

//---------------------------------------------------------
//   console
//    variables stay between lines; a selection is good until the notes change
//---------------------------------------------------------

void TestScripting::console()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      const int flags = ScriptEngine::Persist | ScriptEngine::Echo;
      ScriptResult r = e.runOnScore(score, "s = findall(violin, measure == 1)", flags);
      QVERIFY2(r.ok(), qPrintable(r.error));
      r = e.runOnScore(score, "s.add(accent)", flags);
      QVERIFY2(r.ok(), qPrintable(r.error));
      QCOMPARE(r.changes, 4);
      r = e.runOnScore(score, "#s", flags);
      QCOMPARE(r.log.size(), 1);
      QCOMPARE(r.log[0].kind, QString("value"));
      QCOMPARE(r.log[0].text, QString("4"));
      // delete the last violin note: the selection is stale
      Ms::Chord* last = chordAt(score, 3, 1440);
      QVERIFY(last);
      score->startCmd();
      score->deleteItem(last->notes()[0]);
      score->endCmd();
      r = e.runOnScore(score, "s.add(tenuto)", flags);
      QCOMPARE(r.error, QString("line 1: the score changed since this selection was made: run the query again"));
      e.resetConsole();
      r = e.runOnScore(score, "s", flags);
      QVERIFY(r.error.contains("isn't defined"));
      delete score;
      }

//---------------------------------------------------------
//   editorHelpers
//---------------------------------------------------------

void TestScripting::editorHelpers()
      {
      MasterScore* score = load();
      QVERIFY(score);
      ScriptEngine e;
      QVERIFY(e.setSnapshot(ScriptEngine::snapshot(score)));
      int line = -1;
      QCOMPARE(e.check("findall(violin, measure == 1).add(marcato)", &line), QString());
      QCOMPARE(e.check("\nfindall(violin, measure = 1)", &line), QString("line 2: argument 2 of findall: use == to compare (a single = assigns)"));
      QCOMPARE(line, 2);
      ScriptCompletion c = e.complete("findall(cla", 11);
      QCOMPARE(c.from, 8);
      QVERIFY(!c.items.isEmpty());
      QCOMPARE(c.items[0].kind, QString("part"));
      QVERIFY(c.items[0].word.startsWith("cla"));
      c = e.complete(QString::fromUtf8("-- B♭\nfindall(violin).a"), 23);     // characters, not bytes
      QCOMPARE(c.from, 22);
      QCOMPARE(c.items.size(), 1);
      QCOMPARE(c.items[0].word, QString("add"));
      QString kind;
      QVERIFY(e.help("measure", &kind).contains("bar number"));
      QCOMPARE(kind, QString("property"));
      QCOMPARE(e.help("violins"), QString("Violin"));
      bool query = false, part = false;
      for (const auto& w : e.words()) {
            query = query || (w.first == "findall" && w.second == "query");
            part = part || (w.first == "clarinets" && w.second == "part");
            }
      QVERIFY(query && part);
      delete score;
      }

QTEST_MAIN(TestScripting)
#include "tst_luascripting.moc"
