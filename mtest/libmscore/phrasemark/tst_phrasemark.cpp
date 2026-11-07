//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include <memory>
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
//    Amount and Peak (slur.h › Phrase swell): the swell (MuseScore 3 / 4, SSO), a new mark's amount, their metaTag,
//    undo, copy / paste, parts. swell.musicxml: violin, mf, a phrase mark over two whole notes G4 | E4, rest
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
      struct Ev { int tick; NPlayEvent e; };
      static void setSwell(Slur* slur, double amount, int peak = PhraseMark::PEAK_AUTO)
            {
            slur->score()->startCmd();
            slur->undoChangeProperty(Pid::PHRASE_AMOUNT, amount);
            slur->undoChangeProperty(Pid::PHRASE_PEAK, peak);
            slur->score()->endCmd();
            }
      // a phrase mark without a swell (a mark made a phrase mark starts at the default amount)
      static void markPlain(Slur* slur)
            {
            mark(slur, true);
            setSwell(slur, 0.0);
            }
      // the values of controller cc in time order on the channel / route of the first note-on (built-in: channel; a
      // library: its port and channel), each tick's last value
      static std::vector<std::pair<int, int>> curve(const std::vector<Ev>& ev, int cc)
            {
            const NPlayEvent* first = nullptr;
            for (const Ev& e : ev)
                  if (!first && e.e.type() == ME_NOTEON && e.e.velo() > 0 && !e.e.librarySwitch())
                        first = &e.e;
            std::vector<std::pair<int, int>> list;
            if (!first)
                  return list;
            for (const Ev& e : ev) {
                  if (e.e.type() != ME_CONTROLLER || e.e.librarySwitch() || e.e.controller() != cc || e.e.channel() != first->channel()
                      || e.e.extPort() != first->extPort() || e.e.extChannel() != first->extChannel())
                        continue;
                  if (!list.empty() && list.back().first == e.tick)
                        list.back().second = e.e.value();
                  else
                        list.push_back({ e.tick, e.e.value() });
                  }
            return list;
            }
      // the controller's value in force at tick
      static int valueAt(const std::vector<std::pair<int, int>>& c, int tick)
            {
            int v = -1;
            for (const auto& p : c)
                  if (p.first <= tick)
                        v = p.second;
            return v;
            }
      // a swell in [t1, t2) peaking at peak: values rising (never falling) up to the peak, falling (never rising) after
      // it, the largest (from its first tick to where it ends: whole CC steps) within slack ticks of it and louder than the
      // value from t2 (the phrase's end) on
      static QString checkSwell(const std::vector<std::pair<int, int>>& c, int t1, int t2, int peak, int slack = 60)
            {
            const int base = valueAt(c, t2);
            int maxTick = -1;
            int maxEnd = -1;
            int maxValue = -1;
            int prev = -1;
            for (const auto& p : c) {
                  if (p.first < t1 || p.first >= t2)
                        continue;
                  if (prev >= 0 && p.first <= peak && p.second < prev)
                        return QString("falls before the peak at %1: %2 -> %3").arg(p.first).arg(prev).arg(p.second);
                  if (prev >= 0 && p.first > peak + slack && p.second > prev)
                        return QString("rises after the peak at %1: %2 -> %3").arg(p.first).arg(prev).arg(p.second);
                  if (p.second > maxValue) {
                        maxValue = p.second;
                        maxTick = p.first;
                        maxEnd = -1;
                        }
                  else if (p.second < maxValue && maxEnd < 0)
                        maxEnd = p.first;
                  prev = p.second;
                  }
            if (maxEnd < 0)
                  maxEnd = t2;
            if (maxValue <= base)
                  return QString("no swell: after the phrase %1, largest %2").arg(base).arg(maxValue);
            if (peak < maxTick - slack || peak > maxEnd + slack)
                  return QString("largest %1 from %2 to %3, the peak at %4").arg(maxValue).arg(maxTick).arg(maxEnd).arg(peak);
            return QString();
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
      void initTestCase() { initMTest(); }
      void playback();
      void gateTimeMs3();
      void saveLoad();
      void undoRedo();
      void copyPaste();
      void parts();
      void swellCurve();
      void autoPeakRule();
      void defaultsUnchanged();
      void swellMs3();
      void swellMs4();
      void swellPeakMoves();
      void swellLibrary();
      void newMarkAmount();
      void amountPeakSaveLoad();
      void amountPeakUndo();
      void amountPeakCopyPaste();
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
      // (a new phrase mark: Amount 3.3, Peak Auto)
      QVERIFY(text.contains("<metaTag name=\"phraseMarks\">[{&quot;amount&quot;:3.3,&quot;tick&quot;:0,&quot;tick2&quot;:3360,&quot;track&quot;:0,&quot;track2&quot;:0}]</metaTag>"));
      QCOMPARE(text.count("phraseMark"), 1);        // the metaTag only: nothing in the slur
      // apart from the metaTag, the file is the one without the mark (MuseScore 3.6 reads it unchanged)
      QString without = text;
      without.remove(QRegularExpression("\\s*<metaTag name=\"phraseMarks\">[^<]*</metaTag>"));
      QCOMPARE(without, fileText("phrasemark-none.mscx"));

      MasterScore* again = readCreatedScore("phrasemark-saved.mscx");
      QVERIFY(again);
      QVERIFY(outer(again)->phraseMark());
      QCOMPARE(outer(again)->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);
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
      QCOMPARE(o->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);          // (the same undo step)
      score->undoRedo(true, 0);
      QVERIFY(!o->phraseMark());
      QCOMPARE(o->phraseAmount(), 0.0);
      QCOMPARE(legato(score), QString("LLLLL---"));
      score->undoRedo(false, 0);
      QVERIFY(o->phraseMark());
      QCOMPARE(o->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);
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
//   swellCurve
//    the swell's 0 … 1 over the phrase; swellDb (MuseScore 3's mode) at each chord's start, the phrase from tick 0 to its
//    last note's end (3840), Peak set and Auto, by the mark's own Amount; nothing on a slur that isn't a phrase mark nor
//    at Amount 0
//---------------------------------------------------------

void TestPhraseMark::swellCurve()
      {
      using namespace PhraseMark;
      QCOMPARE(swellAt(0.0, 0.5), 0.0);
      QCOMPARE(swellAt(0.5, 0.5), 1.0);
      QCOMPARE(swellAt(0.75, 0.5), 0.5);
      QCOMPARE(swellAt(1.0, 0.5), 0.0);
      QCOMPARE(swellAt(0.2, 0.4), 0.5);
      QCOMPARE(swellAt(0.7, 0.4), 0.5);
      QCOMPARE(swellAt(0.0, 0.0), 1.0);       // (a peak at the start: falling from it)
      QCOMPARE(swellAt(0.5, 0.0), 0.5);
      QCOMPARE(swellAt(1.0, 1.0), 1.0);       // (at the end: rising to it)

      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      const std::vector<Chord*> c = chords(score);
      QCOMPARE(int(c.size()), 8);
      setSwell(o, 3.0, 25);
      QCOMPARE(swellDb(c[2]), 0.0);                       // (not a phrase mark: a slur's Amount doesn't play)
      mark(o, true);
      QCOMPARE(o->phraseAmount(), 3.0);                   // (it had an amount: kept)
      QVERIFY(qAbs(peakOf(o) - 0.25) < 1e-12);
      QVERIFY(qAbs(swellDb(c[2]) - 3.0) < 1e-12);         // tick 960: 25 % of 3840
      QVERIFY(qAbs(swellDb(c[1]) - 1.5) < 1e-12);         // half way up
      QVERIFY(qAbs(swellDb(c[4]) - 2.0) < 1e-12);         // tick 1920: (1 - 0.5) / 0.75
      QCOMPARE(swellDb(c[0]), 0.0);
      QVERIFY(qAbs(swellDb(c[7]) - 3.0 * (480.0 / 3840.0) / 0.75) < 1e-12);    // (the last note's start: not the end)
      setSwell(o, 7.5, 25);                               // the mark's own Amount sets the size
      QVERIFY(qAbs(swellDb(c[2]) - 7.5) < 1e-12);
      QVERIFY(qAbs(swellDb(c[1]) - 3.75) < 1e-12);
      setSwell(o, 3.0, 0);
      QVERIFY(qAbs(swellDb(c[0]) - 3.0) < 1e-12);
      setSwell(o, 0.0, 0);
      QCOMPARE(swellDb(c[0]), 0.0);
      QVERIFY(!hasSwell(o));
      delete score;
      }

//---------------------------------------------------------
//   autoPeakRule
//    Auto: the highest note's chord when only one chord reaches it and it starts within 25 … 75 % of the phrase (by
//    ticks, to the last note's end), else the middle
//---------------------------------------------------------

void TestPhraseMark::autoPeakRule()
      {
      using namespace PhraseMark;
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      mark(o, true);
      const std::vector<Chord*> c = chords(score);
      QCOMPARE(o->phrasePeak(), int(PEAK_AUTO));
      QCOMPARE(autoPeak(o), 0.5);                         // G5 at 1920 / 3840
      c[3]->upNote()->setPitch(80);                       // G#5 at 1440: 37.5 %
      QCOMPARE(autoPeak(o), 0.375);
      QCOMPARE(peakOf(o), 0.375);
      c[2]->upNote()->setPitch(81);                       // A5 at 960: 25 %, still inside
      QCOMPARE(autoPeak(o), 0.25);
      c[1]->upNote()->setPitch(82);                       // A#5 at 480: 12.5 %, outside: the middle
      QCOMPARE(autoPeak(o), 0.5);
      c[1]->upNote()->setPitch(74);
      c[6]->upNote()->setPitch(83);                       // B5 at 2880: 75 %, inside
      QCOMPARE(autoPeak(o), 0.75);
      c[7]->upNote()->setPitch(84);                       // C6 at 3360: 87.5 %, outside
      QCOMPARE(autoPeak(o), 0.5);
      c[7]->upNote()->setPitch(83);                       // two chords reach B5: the middle
      QCOMPARE(autoPeak(o), 0.5);
      setSwell(o, 3.0, 40);                               // a Peak set: Auto doesn't matter
      QCOMPARE(peakOf(o), 0.4);
      delete score;
      }

//---------------------------------------------------------
//   defaultsUnchanged
//    a phrase mark at Amount 0, or an Amount on a slur that isn't a phrase mark, renders every event and the tempo map
//    as the phrase mark / the slur without it (MuseScore 3 and 4 modes, SSO); the removed [phrasing] settings in a score
//    change nothing
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
      setSwell(o, 6.0, 30);                               // (a slur: nothing)
      QCOMPARE(all(), slur);
      markPlain(o);
      const QStringList plain = all();
      setSwell(o, PhraseMark::DEFAULT_AMOUNT);
      const QStringList swelled = all();
      QVERIFY(swelled != plain);
      // the removed settings: ignored
      score->setMetaTag(Playback::metaTag, "phrasing/shapeAmount=0;phrasing/endingBroad=50;phrasing/endingLength=4");
      QCOMPARE(all(), swelled);
      score->setMetaTag(Playback::metaTag, QString());
      setSwell(o, 0.0);
      QCOMPARE(all(), plain);
      delete score;
      }

//---------------------------------------------------------
//   swellMs3
//    MuseScore 3's mode (velocity hairpins): each chord's velocity by SoundFont 2's law, the swell's dB at its start
//    (Peak 25 %: Amount x swellAt on the 8 chords at 0, 480 … 3360 of 0 … 3840), by the mark's own Amount
//---------------------------------------------------------

void TestPhraseMark::swellMs3()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      markPlain(outer(score));
      const std::vector<On> before = noteOns(render(score, nullptr, ms3State()));
      QCOMPARE(int(before.size()), 8);
      for (double amount : { 3.0, 6.0 }) {
            setSwell(outer(score), amount, 25);
            const std::vector<On> after = noteOns(render(score, nullptr, ms3State()));
            QCOMPARE(int(after.size()), 8);
            for (size_t i = 0; i < 8; ++i)
                  QCOMPARE(after[i].velocity, MarcatoLevel::velocity(before[i].velocity, amount * PhraseMark::swellAt(i * 480.0 / 3840.0, 0.25)));
            QVERIFY(after[2].velocity > before[2].velocity);
            }
      delete score;
      }

//---------------------------------------------------------
//   swellMs4
//    MuseScore 4's mode, a violin (single-note dynamics): the swell as a hairpin, on CC11, inside the held notes:
//    swell.musicxml, mf, a phrase of two whole notes (G4, E4: 0 … 3840), Auto (the highest note at 0 %: the middle,
//    1920); the note-ons' velocities as without; CC11 back at mf's after the phrase; 3 dB at the peak by SoundFont 2's
//    law (mf's CC11 x 10^(3 / 40))
//---------------------------------------------------------

void TestPhraseMark::swellMs4()
      {
      MasterScore* score = readScore(DIR + "swell.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Slur* o = outer(score);
      QVERIFY(o);
      markPlain(o);
      const std::vector<Ev> plain = render(score, nullptr);
      const std::vector<std::pair<int, int>> before = curve(plain, CTRL_EXPRESSION);
      QVERIFY(!before.empty());
      const int mf = valueAt(before, 0);
      QVERIFY(mf > 0);
      setSwell(o, 3.0);
      const std::vector<Ev> swelled = render(score, nullptr);
      const std::vector<std::pair<int, int>> after = curve(swelled, CTRL_EXPRESSION);
      QVERIFY(after.size() > before.size() + 20);
      const QString e = checkSwell(after, 0, 3840, 1920);
      QVERIFY2(e.isEmpty(), qPrintable(e));
      QCOMPARE(valueAt(after, 0), mf);
      QCOMPARE(valueAt(after, 1920), std::min(127, int(std::lround(mf * std::pow(10.0, 3.0 / 40.0)))));
      QCOMPARE(valueAt(after, 3840), mf);
      // rising inside the first note, falling inside the second
      QVERIFY(valueAt(after, 480) < valueAt(after, 1440));
      QVERIFY(valueAt(after, 2400) > valueAt(after, 3360));
      // the note-ons: the first (at the swell's 0 dB) as without, the second (at its peak) as a note starting there in a
      // hairpin: MuseScore 4's velocity at the level in force
      const std::vector<On> on0 = noteOns(plain);
      const std::vector<On> on1 = noteOns(swelled);
      QCOMPARE(int(on1.size()), 2);
      QCOMPARE(int(on0.size()), 2);
      QCOMPARE(on1[0].velocity, on0[0].velocity);
      QVERIFY(on1[1].velocity >= on0[1].velocity);
      // the mark's own Amount: 6 dB at the peak
      setSwell(o, 6.0);
      QCOMPARE(valueAt(curve(render(score, nullptr), CTRL_EXPRESSION), 1920), std::min(127, int(std::lround(mf * std::pow(10.0, 6.0 / 40.0)))));
      // Amount 0: as without
      setSwell(o, 0.0);
      QCOMPARE(curve(render(score, nullptr), CTRL_EXPRESSION), before);
      delete score;
      }

//---------------------------------------------------------
//   swellPeakMoves
//    Peak 0 / 40 / 100 % moves the largest CC11 to 0 / 1536 (inside the first whole note: rising, then falling in it) /
//    just before 3840; a piano (no single-note dynamics) plays it by velocity at each note's start, as a hairpin
//---------------------------------------------------------

void TestPhraseMark::swellPeakMoves()
      {
      MasterScore* score = readScore(DIR + "swell.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Slur* o = outer(score);
      markPlain(o);
      const int mf = valueAt(curve(render(score, nullptr), CTRL_EXPRESSION), 0);
      for (int peak : { 0, 40, 100 }) {
            setSwell(o, 3.0, peak);
            const std::vector<std::pair<int, int>> c = curve(render(score, nullptr), CTRL_EXPRESSION);
            const int peakTick = 3840 * peak / 100;
            const QString e = checkSwell(c, 0, 3840, peakTick);
            QVERIFY2(e.isEmpty(), qPrintable(QString("peak %1: %2").arg(peak).arg(e)));
            int maxValue = 0;
            for (const auto& p : c)
                  if (p.first < 3840)
                        maxValue = std::max(maxValue, p.second);
            QCOMPARE(maxValue, std::min(127, int(std::lround(mf * std::pow(10.0, 3.0 / 40.0)))));
            QCOMPARE(valueAt(c, 3840), mf);
            if (peak == 40) {
                  QVERIFY(valueAt(c, 600) < valueAt(c, 1500));          // inside the first note: rising
                  QVERIFY(valueAt(c, 1600) > valueAt(c, 1900));         // and falling
                  }
            if (peak == 0)
                  QVERIFY(valueAt(c, 0) > mf);
            if (peak == 100)
                  QVERIFY(valueAt(c, 3839) > valueAt(c, 1920));
            }
      delete score;
      }

//---------------------------------------------------------
//   swellLibrary
//    SSO (the violin's Long, on CC1): the swell on the dynamics CC, as a hairpin's controller events, inside the held
//    notes (Auto: 1920; Peak 40 %: 1536), with the level settings after these tests off and with [levels] realRange 2 (by
//    the real players' levels); back to the dynamic's CC1 after the phrase; a larger Amount swells more; Amount 0 as
//    without
//---------------------------------------------------------

void TestPhraseMark::swellLibrary()
      {
      auto lib = sso();
      QVERIFY(lib);
      MasterScore* score = readScore(DIR + "swell.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      Slur* o = outer(score);
      mark(o, true);
      const QString off = "levels/sectionBalance=0;levels/micCompensation=0;levels/context=0;levels/evenLevels=0;levels/calibrated=0;"
                          "levels/attackLevels=0;slurs/plainPerformance=0;heldNotes/early=0;slurs/smoothStart=0;";
      for (const QString& range : { QString("levels/realRange=0"), QString("levels/realRange=2") }) {
            score->setMetaTag(Playback::metaTag, off + range);
            setSwell(o, 0.0);
            const std::vector<std::pair<int, int>> before = curve(render(score, lib), 1);
            QVERIFY2(!before.empty(), qPrintable(range));
            const int mf = valueAt(before, 0);
            for (int peak : { int(PhraseMark::PEAK_AUTO), 40 }) {
                  setSwell(o, 3.0, peak);
                  const std::vector<std::pair<int, int>> after = curve(render(score, lib), 1);
                  // (no early starts, set above: the library's events at the written ticks, no start lead)
                  const int peakTick = peak < 0 ? 1920 : 1536;
                  const QString e = checkSwell(after, 0, 3840, peakTick);
                  if (!e.isEmpty() && qEnvironmentVariableIsSet("PHRASE_DUMP"))
                        for (const auto& p : after)
                              qDebug("CC1 %d %d", p.first, p.second);
                  QVERIFY2(e.isEmpty(), qPrintable(QString("%1, peak %2: %3").arg(range).arg(peak).arg(e)));
                  QVERIFY(valueAt(after, peakTick) > mf);
                  QCOMPARE(valueAt(after, 3840), mf);
                  }
            setSwell(o, 3.0);
            const int at3 = valueAt(curve(render(score, lib), 1), 1920);
            setSwell(o, 9.0);
            QVERIFY2(valueAt(curve(render(score, lib), 1), 1920) > at3, qPrintable(range));
            setSwell(o, 0.0);
            QCOMPARE(curve(render(score, lib), 1), before);
            }
      score->setMetaTag(Playback::metaTag, QString());
      delete score;
      }

//---------------------------------------------------------
//   newMarkAmount
//    a slur made a phrase mark starts at DEFAULT_AMOUNT and Auto; one that has an amount keeps it; Amount and Peak
//    bounded; the Inspector's reset value
//---------------------------------------------------------

void TestPhraseMark::newMarkAmount()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      QCOMPARE(o->phraseAmount(), 0.0);
      mark(o, true);
      QCOMPARE(o->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);
      QCOMPARE(o->phrasePeak(), int(PhraseMark::PEAK_AUTO));
      QVERIFY(PhraseMark::hasSwell(o));
      setSwell(o, 6.0, 30);
      mark(o, false);
      QVERIFY(!PhraseMark::hasSwell(o));                  // (kept, not played)
      mark(o, true);
      QCOMPARE(o->phraseAmount(), 6.0);
      QCOMPARE(o->phrasePeak(), 30);
      setSwell(o, 0.0);
      mark(o, false);
      mark(o, true);
      QCOMPARE(o->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);
      setSwell(o, 20.0);
      QCOMPARE(o->phraseAmount(), PhraseMark::MAX_AMOUNT);
      setSwell(o, -2.0, 150);
      QCOMPARE(o->phraseAmount(), 0.0);
      QCOMPARE(o->phrasePeak(), 100);
      QCOMPARE(o->propertyDefault(Pid::PHRASE_AMOUNT).toDouble(), PhraseMark::DEFAULT_AMOUNT);
      QCOMPARE(o->propertyDefault(Pid::PHRASE_PEAK).toInt(), int(PhraseMark::PEAK_AUTO));
      // (a copy, as Alt+S's template is cloned: the amount with it)
      Slur templ(score);
      templ.setPhraseMark(true);
      templ.setPhraseAmount(PhraseMark::DEFAULT_AMOUNT);
      std::unique_ptr<Slur> made(templ.clone());
      QCOMPARE(made->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);
      delete score;
      }

//---------------------------------------------------------
//   amountPeakSaveLoad
//    "amount" / "peak" in the phrase mark's metaTag entry only when set; read back; saved again the same; the rest of
//    the file as without them; an entry without "amount" (saved before amounts) loads at 0; an unmerged branch's
//    "shape" / "ending" are ignored and not written again
//---------------------------------------------------------

void TestPhraseMark::amountPeakSaveLoad()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      Slur* o = outer(score);
      markPlain(o);
      QVERIFY(saveScore(score, "phraseswell-none.mscx"));
      const QString noneText = fileText("phraseswell-none.mscx");
      QVERIFY(noneText.contains("[{&quot;tick&quot;:0,&quot;tick2&quot;:3360,&quot;track&quot;:0,&quot;track2&quot;:0}]"));
      setSwell(o, 4.5);
      QVERIFY(saveScore(score, "phraseswell-auto.mscx"));       // (Auto: no "peak")
      QVERIFY(fileText("phraseswell-auto.mscx").contains("[{&quot;amount&quot;:4.5,&quot;tick&quot;:0,&quot;tick2&quot;:3360,&quot;track&quot;:0,&quot;track2&quot;:0}]"));
      setSwell(o, 4.5, 40);
      QVERIFY(saveScore(score, "phraseswell-saved.mscx"));
      const QString text = fileText("phraseswell-saved.mscx");
      QVERIFY(text.contains("<metaTag name=\"phraseMarks\">[{&quot;amount&quot;:4.5,&quot;peak&quot;:40,&quot;tick&quot;:0,&quot;tick2&quot;:3360,&quot;track&quot;:0,&quot;track2&quot;:0}]</metaTag>"));
      QVERIFY(!text.contains("phraseAmount"));
      QVERIFY(!text.contains("phrasePeak"));
      QString without = text;
      without.remove(QRegularExpression("\\s*<metaTag name=\"phraseMarks\">[^<]*</metaTag>"));
      QString none = noneText;
      none.remove(QRegularExpression("\\s*<metaTag name=\"phraseMarks\">[^<]*</metaTag>"));
      QCOMPARE(without, none);

      MasterScore* again = readCreatedScore("phraseswell-saved.mscx");
      QVERIFY(again);
      QVERIFY(outer(again)->phraseMark());
      QCOMPARE(outer(again)->phraseAmount(), 4.5);
      QCOMPARE(outer(again)->phrasePeak(), 40);
      QCOMPARE(inner(again)->phrasePeak(), int(PhraseMark::PEAK_AUTO));
      QCOMPARE(inner(again)->phraseAmount(), 0.0);
      QVERIFY(saveScore(again, "phraseswell-saved2.mscx"));
      QCOMPARE(fileText("phraseswell-saved2.mscx"), text);
      delete again;

      // without "amount" (a phrase mark saved before amounts): no swell
      MasterScore* old = readCreatedScore("phraseswell-none.mscx");
      QVERIFY(old);
      QVERIFY(outer(old)->phraseMark());
      QCOMPARE(outer(old)->phraseAmount(), 0.0);
      QVERIFY(!PhraseMark::hasSwell(outer(old)));
      delete old;

      // "shape" / "ending" (the branch before): ignored, not written again
      QString legacy = noneText;
      legacy.replace("[{&quot;tick&quot;:0,", "[{&quot;ending&quot;:2,&quot;shape&quot;:1,&quot;tick&quot;:0,");
      QVERIFY(legacy != noneText);
      QFile f("phraseswell-legacy.mscx");
      QVERIFY(f.open(QIODevice::WriteOnly));
      f.write(legacy.toUtf8());
      f.close();
      MasterScore* legacyScore = readCreatedScore("phraseswell-legacy.mscx");
      QVERIFY(legacyScore);
      QVERIFY(outer(legacyScore)->phraseMark());
      QCOMPARE(outer(legacyScore)->phraseAmount(), 0.0);
      QVERIFY(saveScore(legacyScore, "phraseswell-legacy2.mscx"));
      QCOMPARE(fileText("phraseswell-legacy2.mscx"), noneText);
      delete legacyScore;
      delete score;
      }

//---------------------------------------------------------
//   amountPeakUndo
//    through a segment as the Inspector sets them; undo / redo; a part's linked copy follows
//---------------------------------------------------------

void TestPhraseMark::amountPeakUndo()
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
      QCOMPARE(partSlur->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);   // (the new mark's amount reaches the part)
      score->doLayout();
      QVERIFY(!o->spannerSegments().empty());
      score->startCmd();
      o->frontSegment()->undoChangeProperty(Pid::PHRASE_AMOUNT, 5.5, PropertyFlags::NOSTYLE);
      o->frontSegment()->undoChangeProperty(Pid::PHRASE_PEAK, 60, PropertyFlags::NOSTYLE);
      score->endCmd();
      QCOMPARE(o->phraseAmount(), 5.5);
      QCOMPARE(o->frontSegment()->getProperty(Pid::PHRASE_AMOUNT).toDouble(), 5.5);
      QCOMPARE(o->frontSegment()->getProperty(Pid::PHRASE_PEAK).toInt(), 60);
      QCOMPARE(partSlur->phraseAmount(), 5.5);
      QCOMPARE(partSlur->phrasePeak(), 60);
      score->undoRedo(true, 0);
      QCOMPARE(o->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);
      QCOMPARE(partSlur->phraseAmount(), PhraseMark::DEFAULT_AMOUNT);
      QCOMPARE(o->phrasePeak(), int(PhraseMark::PEAK_AUTO));
      QCOMPARE(partSlur->phrasePeak(), int(PhraseMark::PEAK_AUTO));
      QVERIFY(o->phraseMark());
      score->undoRedo(false, 0);
      QCOMPARE(o->phraseAmount(), 5.5);
      QCOMPARE(partSlur->phraseAmount(), 5.5);
      QCOMPARE(partSlur->phrasePeak(), 60);
      delete score;
      }

//---------------------------------------------------------
//   amountPeakCopyPaste
//    measures 1-2 pasted over 3-4: the pasted phrase mark keeps its Amount and Peak; the inner slur none
//---------------------------------------------------------

void TestPhraseMark::amountPeakCopyPaste()
      {
      MasterScore* score = readScore(DIR + "phrasemark.musicxml");
      QVERIFY(score);
      mark(outer(score), true);
      setSwell(outer(score), 4.5, 35);
      Measure* m1 = score->firstMeasure();
      Measure* m2 = m1->nextMeasure();
      Measure* m3 = m2->nextMeasure();
      score->select(m1);
      score->select(m2, SelectType::RANGE, 0);
      QMimeData* mimeData = new QMimeData;
      mimeData->setData(score->selection().mimeType(), score->selection().mimeData());
      QVERIFY(QString::fromUtf8(score->selection().mimeData()).contains("<phraseAmount>4.5</phraseAmount>"));
      QVERIFY(QString::fromUtf8(score->selection().mimeData()).contains("<phrasePeak>35</phrasePeak>"));
      score->select(m3->first(SegmentType::ChordRest)->element(0));
      score->startCmd();
      score->cmdPaste(mimeData, 0);
      score->endCmd();
      delete mimeData;
      std::vector<Slur*> pasted = slursAt(score, m3->tick().ticks());
      QCOMPARE(int(pasted.size()), 1);
      QVERIFY(pasted[0]->phraseMark());
      QCOMPARE(pasted[0]->phraseAmount(), 4.5);
      QCOMPARE(pasted[0]->phrasePeak(), 35);
      std::vector<Slur*> pastedInner = slursAt(score, m3->tick().ticks() + 2 * DIVISION);
      QCOMPARE(int(pastedInner.size()), 1);
      QCOMPARE(pastedInner[0]->phraseAmount(), 0.0);
      delete score;
      }

QTEST_MAIN(TestPhraseMark)
#include "tst_phrasemark.moc"
