//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

//---------------------------------------------------------
//   tst_playbackaudit: the whole-score playback audit (libmscore/playbackaudit.h) finds each class of fault in
//   fixtures made to have it, and (auditScore, MS_AUDIT_SCORE) audits any score: VERIFY.md › Playback audit
//---------------------------------------------------------

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <cmath>
#include <set>

#include "audio/midi/event.h"
#include "libmscore/playbackaudit.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/rendermidi.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "mtest/testutils.h"

#define DIR QString("libmscore/playbackaudit/")

using namespace Ms;

class TestPlaybackAudit : public QObject, public MTest
      {
      Q_OBJECT

      std::shared_ptr<SoundLib::Library> loadMap(const QString& xml);
      static void recommended(std::map<QString, QString> extra = {});

   private slots:
      void initTestCase() { initMTest(); }
      void cleanup()
            {
            SoundLib::setCurrent(nullptr);
            SoundLib::setOutput(SoundLib::Output::MIDI);
            SoundLib::setDynamicsCalibration(nullptr);
            Playback::setIniValuesForTest({});
            }
      void overlapDetected();
      void swapFindings();
      void swapByPitch();
      void cappedArrival();
      void droppedFindings();
      void leapFindings();
      void transitionArrival();
      void auditScore();
      };

std::shared_ptr<SoundLib::Library> TestPlaybackAudit::loadMap(const QString& xml)
      {
      QTemporaryFile f;
      f.open();
      f.write(xml.toUtf8());
      f.close();
      QString error;
      std::shared_ptr<SoundLib::Library> lib = SoundLib::Library::load(f.fileName(), &error);
      if (!lib)
            qWarning() << error;
      return lib;
      }

// the Recommended preset's values (Playback::presets()), plus extra
void TestPlaybackAudit::recommended(std::map<QString, QString> extra)
      {
      for (const Playback::Preset& p : Playback::presets())
            if (QString(p.id) == "recommended")
                  for (const auto& v : p.values)
                        extra.insert({ QString(v.first), QString::number(v.second) });
      Playback::setIniValuesForTest(extra);
      }

static int moveNoteOff(EventMap& events, int pitch, int nth, int to)
      {
      int seen = 0;
      for (auto it = events.begin(); it != events.end(); ++it) {
            const NPlayEvent& e = it->second;
            if (!e.isExternal() || e.librarySwitch() || e.pitch() != pitch)
                  continue;
            if ((e.type() == ME_NOTEON && e.velo() == 0) || e.type() == ME_NOTEOFF) {
                  if (seen++ == nth) {
                        const NPlayEvent off = e;
                        const int from = it->first;
                        events.erase(it);
                        events.insert({ to, off });
                        return from;
                        }
                  }
            }
      return -1;
      }

static std::vector<std::pair<int, int>> noteOns(const EventMap& events)
      {
      std::vector<std::pair<int, int>> ons;           // utick, pitch
      for (const auto& te : events)
            if (te.second.isExternal() && !te.second.librarySwitch() && te.second.type() == ME_NOTEON && te.second.velo() > 0)
                  ons.push_back({ te.first, te.second.pitch() });
      return ons;
      }

//---------------------------------------------------------
//   overlapDetected
//    the notes started early by an onset longer than the note before (onset-longer.musicxml, Whence bar 7's
//    violas, tst_soundlibrary::onsetLongerThanNote): as rendered since cc1c9dcd2b no OVERLAP; a note-off moved
//    past the next note's start (more than [legato] keepMs), or past its key's next strike, is found. (With
//    cc1c9dcd2b's change to finishLibraryEvents reverted, the rendered events themselves fail: checked by hand
//    2026-10-09)
//---------------------------------------------------------

void TestPlaybackAudit::overlapDetected()
      {
      Playback::setIniValuesForTest({ { "heldNotes/byPitch", "1" } });
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long' onset='200'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      MasterScore* score = readScore("libmscore/soundlibrary/onset-longer.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      PlaybackAudit::render(score, &events, &trace);
      QCOMPARE(int(trace.size()), 8);
      PlaybackAudit::Report r = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
      QCOMPARE(r.notes, 8);
      QVERIFY2(r.count(PlaybackAudit::Kind::OVERLAP) == 0, qPrintable(r.text()));

      // the first A sounding 1/8 beat (68 ms at 110 bpm) into the B-flat after it
      const std::vector<std::pair<int, int>> ons = noteOns(events);
      QCOMPARE(ons[1].second, 70);
      EventMap moved = events;
      QVERIFY(moveNoteOff(moved, 69, 0, ons[1].first + DIVISION / 8) >= 0);
      r = PlaybackAudit::audit(score, moved, trace, PlaybackAudit::Measured());
      QVERIFY2(r.count(PlaybackAudit::Kind::OVERLAP, PlaybackAudit::Severity::FAIL) >= 1, qPrintable(r.text()));
      bool into = false;
      for (const PlaybackAudit::Finding& f : r.findings)
            if (f.kind == PlaybackAudit::Kind::OVERLAP && f.text.contains("into the next note")) {
                  into = true;
                  QCOMPARE(f.part, QString("Violin"));
                  QCOMPARE(f.bar, 1);
                  QCOMPARE(f.pitch, 69);
                  QCOMPARE(f.beat, 2.0);
                  QVERIFY(f.value > r.keepMs);
                  }
      QVERIFY2(into, qPrintable(r.text()));

      // ... and past its own key's next strike (the A after the B-flat): the bug cc1c9dcd2b fixed
      moved = events;
      QCOMPARE(ons[2].second, 69);
      QVERIFY(moveNoteOff(moved, 69, 0, ons[2].first + 1) >= 0);
      r = PlaybackAudit::audit(score, moved, trace, PlaybackAudit::Measured());
      bool again = false;
      for (const PlaybackAudit::Finding& f : r.findings)
            again |= f.kind == PlaybackAudit::Kind::OVERLAP && f.text.contains("struck again");
      QVERIFY2(again, qPrintable(r.text()));
      delete score;
      }

//---------------------------------------------------------
//   swapByPitch
//    slurred notes [slurs] quick swaps follow [heldNotes] byPitch (the owner, 2026-10-09): 0 every one early by the
//    swapped technique's median onset (an even run), 1 each by its pitch's; the audit predicts the same starts
//---------------------------------------------------------

void TestPlaybackAudit::swapByPitch()
      {
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violas' ids='viola'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='30' peak='800'/>"
         "<Articulation name='Long (Rachm.)' value='16' techniques='long legato' modifiers='espressivo' peak='800'"
         " onset='57:20 59:40 60:60 62:80 64:100'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "swap-violas.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      // how early each swapped note is sent (written tick less sent tick), by pitch
      auto early = [score](const char* byPitch) {
            recommended({ { "levels/calibrated", "0" }, { "heldNotes/byPitch", byPitch } });
            EventMap events;
            std::vector<MidiRenderer::LibTrace> trace;
            PlaybackAudit::render(score, &events, &trace);
            std::map<int, int> e;
            for (const MidiRenderer::LibTrace& t : trace) {
                  if (!t.choice.swapped)
                        continue;
                  const int pitch = t.note->ppitch();
                  for (const auto& te : events)
                        if (te.second.type() == ME_NOTEON && te.second.velo() > 0 && te.second.pitch() == pitch) {
                              e[pitch] = t.utick - te.first;
                              break;
                              }
                  }
            PlaybackAudit::Report r = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
            if (r.count(PlaybackAudit::Kind::CAPPED) || r.count(PlaybackAudit::Kind::ARRIVAL))
                  qWarning() << r.text();
            return e;
            };
      const std::map<int, int> even = early("0");
      const std::map<int, int> each = early("1");
      QCOMPARE(even.size(), size_t(4));
      QCOMPARE(each.size(), size_t(4));
      // 0: the median, 60 ms, for every pitch
      for (const auto& p : even)
            QVERIFY2(qAbs(p.second - even.at(60)) <= 1, qPrintable(QString("%1: %2").arg(p.first).arg(p.second)));
      QVERIFY(even.at(60) > 0);
      // 1: C4's own 60 ms as before, the others 20, 40 and 80 ms: in order of pitch, A3's a third of C4's
      QVERIFY(qAbs(each.at(60) - even.at(60)) <= 1);
      QVERIFY(each.at(57) < each.at(59) && each.at(59) < each.at(60) && each.at(60) < each.at(62));
      QVERIFY(qAbs(3 * each.at(57) - each.at(60)) <= 3);
      Playback::setIniValuesForTest({});
      delete score;
      }

//---------------------------------------------------------
//   swapFindings
//    slurred sixteenths swapped to a quicker technique (Choice::swapped, [slurs] quick 2) whose onset (150 ms) is
//    longer than the note (136 ms), then a half note on the held one (onset 30 ms) under the same slur
//    (swap-violas.musicxml, as Whence bars 9-12's violas): UNMEASURED per instrument while the fit files don't list
//    it; every early start applied in full, so no CAPPED and no ARRIVAL step (the sent times differ by 120 ms, the
//    predicted arrivals don't); a LEVEL STEP value (quickLevel)
//---------------------------------------------------------

void TestPlaybackAudit::swapFindings()
      {
      recommended({ { "levels/calibrated", "0" } });         // (no dynamics calibration here)
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violas' ids='viola'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='30' peak='800'/>"
         "<Articulation name='Long (Rachm.)' value='16' techniques='long legato' modifiers='espressivo' onset='150' peak='800'"
         " quickLevel='50/136:3 70/136:3'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "swap-violas.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      PlaybackAudit::render(score, &events, &trace);
      int swapped = 0;
      for (const MidiRenderer::LibTrace& t : trace)
            swapped += t.choice.swapped;
      QCOMPARE(swapped, 4);

      PlaybackAudit::Report r = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
      const QString text = r.text();
      QCOMPARE(r.count(PlaybackAudit::Kind::OVERLAP), 0);
      // unmeasured: the swapped technique (onset and level) and the held one (onset), per instrument
      QCOMPARE(r.count(PlaybackAudit::Kind::UNMEASURED), 2);
      for (const PlaybackAudit::Finding& f : r.findings)
            if (f.kind == PlaybackAudit::Kind::UNMEASURED) {
                  QCOMPARE(f.part, QString("Violas"));
                  if (f.technique == "Long (Rachm.)") {
                        QCOMPARE(f.value, 4.0);
                        QVERIFY2(f.text.contains("swapped") && f.text.contains("onset") && f.text.contains("level"), qPrintable(f.text));
                        }
                  else
                        QCOMPARE(f.technique, QString("Long"));
                  }
      QVERIFY2(r.count(PlaybackAudit::Kind::CAPPED) == 0, qPrintable(text));
      QVERIFY2(r.count(PlaybackAudit::Kind::ARRIVAL) == 0, qPrintable(text));
      bool step = false;
      for (const PlaybackAudit::Finding& f : r.findings)
            if (f.kind == PlaybackAudit::Kind::LEVEL_STEP && f.pitch == 64) {
                  step = true;
                  QCOMPARE(f.severity, PlaybackAudit::Severity::INFO);
                  QVERIFY2(f.value < -1, qPrintable(f.text));          // the 3 dB of quickLevel gone
                  }
      QVERIFY2(step, qPrintable(text));

      // measured: nothing unmeasured
      PlaybackAudit::Measured m;
      m.onsetFits["Long"].insert("Violas");
      m.onsetFits["Long (Rachm.)"].insert("Violas");
      m.levelFits["Long (Rachm.)"].insert("Violas");
      r = PlaybackAudit::audit(score, events, trace, m);
      QVERIFY2(r.count(PlaybackAudit::Kind::UNMEASURED) == 0, qPrintable(r.text()));
      delete score;
      }

//---------------------------------------------------------
//   cappedArrival
//    swap-violas.musicxml with the held technique's onset (300 ms) longer than the sixteenth before it plus the
//    swapped one's (10 ms): the half note's early start is cut where the note before on its patch keeps [legato]
//    keepMs, so it arrives late by the cut: CAPPED with the cut, an ARRIVAL fail of that much against the sixteenth.
//    Without an onset for the held technique: UNMEASURED ("no onset"), no ARRIVAL number
//---------------------------------------------------------

void TestPlaybackAudit::cappedArrival()
      {
      recommended({ { "levels/calibrated", "0" } });
      auto lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violas' ids='viola'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='300' peak='800'/>"
         "<Articulation name='Long (Rachm.)' value='16' techniques='long legato' modifiers='espressivo' onset='10' peak='800'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      MasterScore* score = readScore(DIR + "swap-violas.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      PlaybackAudit::render(score, &events, &trace);
      PlaybackAudit::Report r = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
      QString text = r.text();
      double cut = -1;
      for (const PlaybackAudit::Finding& f : r.findings)
            if (f.kind == PlaybackAudit::Kind::CAPPED && f.bar == 2 && f.pitch == 64) {
                  cut = f.value;
                  QVERIFY2(f.text.contains("keeps [legato] keepMs"), qPrintable(f.text));
                  }
      // 300 meant; the sixteenth before (136 ms as written) is sent 10 ms early and keeps keepMs from there: the half
      // may start keepMs after it, 136 + 10 - keepMs early
      const double expectCut = 300 - (60000.0 / 110 / 4 + 10 - r.keepMs);
      QVERIFY2(std::abs(cut - expectCut) < 2, qPrintable(text));
      bool late = false;
      for (const PlaybackAudit::Finding& f : r.findings)
            if (f.kind == PlaybackAudit::Kind::ARRIVAL && f.bar == 2 && f.pitch == 64) {
                  late = true;
                  QCOMPARE(f.severity, PlaybackAudit::Severity::FAIL);
                  QVERIFY2(std::abs(f.value - expectCut) < 2, qPrintable(f.text));
                  }
      QVERIFY2(late, qPrintable(text));

      // no onset for the held technique: its arrival unknown
      lib = loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violas' ids='viola'>"
         "<Articulation name='Long' value='1' techniques='long legato' peak='800'/>"
         "<Articulation name='Long (Rachm.)' value='16' techniques='long legato' modifiers='espressivo' onset='10' peak='800'/>"
         "</Instrument></SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      events.clear();
      trace.clear();
      PlaybackAudit::render(score, &events, &trace);
      r = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
      text = r.text();
      bool none = false;
      for (const PlaybackAudit::Finding& f : r.findings) {
            none |= f.kind == PlaybackAudit::Kind::UNMEASURED && f.technique == "Long" && f.text.contains("no onset");
            QVERIFY2(!(f.kind == PlaybackAudit::Kind::ARRIVAL && f.bar == 2), qPrintable(text));
            }
      QVERIFY2(none, qPrintable(text));
      delete score;
      }

//---------------------------------------------------------
//   droppedFindings
//    SSO's legato engine plays no legato note started less than 15 ms after the previous one's note-on at velocity
//    64 or lower (fast-legato.musicxml: slurred 64th notes at 300 bpm, 12.5 ms apart, then slurred quarters): at
//    the map's legatoVelocity 60 the three fast transitions are DROPPED fails; at 65, or at a slow tempo, none
//---------------------------------------------------------

void TestPlaybackAudit::droppedFindings()
      {
      recommended({ { "levels/calibrated", "0" } });
      MasterScore* score = readScore(DIR + "fast-legato.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      auto audit = [&](int legatoVelocity) {
            auto lib = loadMap(QString(
               "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
               "<Instrument name='Violin' ids='violin'>"
               "<Articulation name='Long' value='1' techniques='long'/>"
               "<Articulation name='Legato' value='20' techniques='legato long' legatoVelocity='%1'/>"
               "</Instrument></SoundLibrary>").arg(legatoVelocity));
            SoundLib::setCurrent(lib);
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            EventMap events;
            std::vector<MidiRenderer::LibTrace> trace;
            PlaybackAudit::render(score, &events, &trace);
            return PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
            };
      PlaybackAudit::Report r = audit(60);
      QVERIFY2(r.count(PlaybackAudit::Kind::DROPPED, PlaybackAudit::Severity::FAIL) == 3, qPrintable(r.text()));
      QCOMPARE(r.count(PlaybackAudit::Kind::LEAP), 0);
      std::set<int> pitches;
      for (const PlaybackAudit::Finding& f : r.findings)
            if (f.kind == PlaybackAudit::Kind::DROPPED) {
                  pitches.insert(f.pitch);
                  QCOMPARE(f.part, QString("Violin"));
                  QCOMPARE(f.bar, 1);
                  QVERIFY2(std::abs(f.value - 12.5) < 1.0, qPrintable(f.text));
                  QVERIFY2(f.text.contains("velocity 60"), qPrintable(f.text));
                  }
      QCOMPARE(pitches, (std::set<int> { 62, 64, 65 }));
      QVERIFY2(r.text().contains("DROPPED"), qPrintable(r.text()));
      r = audit(65);
      QVERIFY2(r.count(PlaybackAudit::Kind::DROPPED) == 0, qPrintable(r.text()));
      delete score;
      }

//---------------------------------------------------------
//   leapFindings
//    a transition beyond an octave gets no transition in SSO (a fresh attack): leap.musicxml's slurred C4 D4 E5 E4 F5
//    G5 steps 2, 14, -12 (exactly an octave: fine), 13 and 2 semitones: LEAP warns at E5 and F5 only, on a plain
//    legato articulation and on a Performance patch (a transition: perf.from), and nothing is DROPPED
//---------------------------------------------------------

void TestPlaybackAudit::leapFindings()
      {
      recommended({ { "levels/calibrated", "0" } });
      MasterScore* score = readScore(DIR + "leap.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      const char* plain =
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' legatoVelocity='70'/>"
         "</Instrument></SoundLibrary>";
      const char* performance =
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39'/>"
         "</Instrument></SoundLibrary>";
      for (const char* xml : { plain, performance }) {
            SoundLib::setCurrent(loadMap(xml));
            SoundLib::setOutput(SoundLib::Output::PLUGIN);
            EventMap events;
            std::vector<MidiRenderer::LibTrace> trace;
            PlaybackAudit::render(score, &events, &trace);
            int transitions = 0;
            for (const MidiRenderer::LibTrace& t : trace)
                  transitions += t.from != nullptr;
            QVERIFY2(transitions >= 4, qPrintable(QString::number(transitions)));
            const PlaybackAudit::Report r = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
            const QString text = r.text();
            QCOMPARE(r.count(PlaybackAudit::Kind::DROPPED), 0);
            QVERIFY2(r.count(PlaybackAudit::Kind::LEAP, PlaybackAudit::Severity::WARN) == 2, qPrintable(text));
            QCOMPARE(r.count(PlaybackAudit::Kind::LEAP), 2);
            bool e5 = false, f5 = false;
            for (const PlaybackAudit::Finding& f : r.findings)
                  if (f.kind == PlaybackAudit::Kind::LEAP) {
                        QCOMPARE(f.part, QString("Violin"));
                        if (f.pitch == 76) {
                              e5 = true;
                              QCOMPARE(f.bar, 1);
                              QCOMPARE(f.beat, 3.0);
                              QCOMPARE(f.value, 1400.0);
                              }
                        if (f.pitch == 77) {
                              f5 = true;
                              QCOMPARE(f.bar, 2);
                              QCOMPARE(f.value, 1300.0);
                              }
                        }
            QVERIFY2(e5 && f5, qPrintable(text));
            }
      delete score;
      }

//---------------------------------------------------------
//   transitionArrival
//    a legato transition arrives when it reaches the new pitch, as the renderer models it (LibTrace::glideArriveMs: here
//    the map's legatoDelay, 120 ms at every interval, after notes long enough for the whole of it), not by the attack's
//    onset: leap.musicxml's slur on a Performance legato patch (onset 60) starts on time (sent 60 ms early), each
//    transition (sent on the beat) 120 ms late: one ARRIVAL, the D4 against the C4, 120 ms; the transitions after it
//    arrive alike
//---------------------------------------------------------

void TestPlaybackAudit::transitionArrival()
      {
      // (the transitions on the beat first: [slurs] transitionsEarly 0, Recommended's 1 since 2026-10-15)
      recommended({ { "levels/calibrated", "0" }, { "slurs/transitionsEarly", "0" } });
      MasterScore* score = readScore(DIR + "leap.musicxml");
      QVERIFY(score);
      score->rebuildMidiMapping();
      SoundLib::setCurrent(loadMap(
         "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
         "<Instrument name='Violin' ids='violin'>"
         "<Articulation name='Long' value='1' techniques='long legato' onset='60'/>"
         "</Instrument>"
         "<Instrument name='Violin - Performance' with='Violin'>"
         "<Switch type='none'/>"
         "<Articulation name='Legato' value='20' techniques='legato long' modifiers='performance' prefer='long' onset='60'"
         " legatoDelay='-12:120 +14:120'"
         " attacks='smooth:1-9:5 spiccato:10-116:112 accented:117-127:122' transitions='portamento:1-19:10 fingered:20-84:69 bowed:85-127:93'"
         " reattack='106' reattackGap='54' overlap='24' legatoGap='39'/>"
         "</Instrument></SoundLibrary>"));
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      PlaybackAudit::render(score, &events, &trace);
      int transitions = 0;
      for (const MidiRenderer::LibTrace& t : trace)
            if (t.from) {
                  ++transitions;
                  QVERIFY2(std::fabs(t.glideArriveMs - 120) < 0.5, qPrintable(QString::number(t.glideArriveMs)));
                  }
      QCOMPARE(transitions, 5);
      const PlaybackAudit::Report r = PlaybackAudit::audit(score, events, trace, PlaybackAudit::Measured());
      const QString text = r.text();
      QVERIFY2(r.count(PlaybackAudit::Kind::ARRIVAL) == 1, qPrintable(text));
      for (const PlaybackAudit::Finding& f : r.findings)
            if (f.kind == PlaybackAudit::Kind::ARRIVAL) {
                  QCOMPARE(f.pitch, 62);
                  QCOMPARE(f.severity, PlaybackAudit::Severity::FAIL);
                  QVERIFY2(std::fabs(f.value - 120) < 1.5, qPrintable(text));
                  QVERIFY2(f.text.contains("transition arrives 120.0 after its note-on"), qPrintable(f.text));
                  }
      QVERIFY(r.transitionShiftsMs.empty());
      // the heard check's rows (Report::rows, writeHeardInputs, 2026-10-16): one per note-on, the transitions with their
      // modelled arrival (sent + 120 ms), each joined and slurred to the chord before; the files written
      QCOMPARE(int(r.rows.size()), r.notes);
      int rowTransitions = 0;
      for (const PlaybackAudit::NoteRow& row : r.rows) {
            QCOMPARE(row.patch.isEmpty(), false);
            if (row.transition) {
                  ++rowTransitions;
                  QVERIFY(row.hasArrival && row.fromPitch > 0 && row.joinedBefore && row.slurredBefore && row.slur >= 0);
                  QVERIFY2(std::fabs(row.arrivalMs - (row.sentMs + 120)) < 0.01, qPrintable(QString::number(row.arrivalMs)));
                  }
            }
      QCOMPARE(rowTransitions, 5);
      QTemporaryDir dir;
      QVERIFY(PlaybackAudit::writeHeardInputs(score, events, r, dir.path()));
      for (const char* f : { "predicted.tsv", "routemap.txt", "lead.txt", "bars.txt" })
            QVERIFY2(QFile::exists(dir.path() + "/" + f), f);
      QFile pf(dir.path() + "/predicted.tsv");
      QVERIFY(pf.open(QIODevice::ReadOnly | QIODevice::Text));
      QCOMPARE(QString(pf.readAll()).trimmed().split('\n').size(), r.notes + 1);
      QVERIFY(!QDir(dir.path()).entryList({ "* museScore.txt" }).isEmpty());
      // [slurs] transitionsEarly (2026-10-14): each transition sent its arrival (120 ms) early arrives on its beat: no
      // ARRIVAL, none capped (500 ms apart), none DROPPED, none CAPPED by the onset
      score->setMetaTag(Playback::metaTag, "slurs/transitionsEarly=1");
      EventMap early;
      std::vector<MidiRenderer::LibTrace> earlyTrace;
      PlaybackAudit::render(score, &early, &earlyTrace);
      const PlaybackAudit::Report e = PlaybackAudit::audit(score, early, earlyTrace, PlaybackAudit::Measured());
      const QString earlyText = e.text();
      QVERIFY2(e.count(PlaybackAudit::Kind::ARRIVAL) == 0, qPrintable(earlyText));
      QVERIFY2(e.count(PlaybackAudit::Kind::DROPPED) == 0, qPrintable(earlyText));
      QVERIFY2(e.count(PlaybackAudit::Kind::OVERLAP) == 0, qPrintable(earlyText));
      QCOMPARE(int(e.transitionShiftsMs.size()), 5);
      QCOMPARE(e.transitionsCapped, 0);
      for (double shift : e.transitionShiftsMs)
            QVERIFY2(std::fabs(shift - 120) < 1.5, qPrintable(QString::number(shift)));
      QVERIFY2(earlyText.contains("[slurs] transitionsEarly 1: 5 transitions started early"), qPrintable(earlyText));
      QVERIFY(e.transitionsFallback.empty());
      // mode 2 (heard on the beat) on a patch without a measured glide (no bendGlide): mode 1's arrival, the patch listed
      score->setMetaTag(Playback::metaTag, "slurs/transitionsEarly=2");
      EventMap two;
      std::vector<MidiRenderer::LibTrace> twoTrace;
      PlaybackAudit::render(score, &two, &twoTrace);
      const PlaybackAudit::Report r2 = PlaybackAudit::audit(score, two, twoTrace, PlaybackAudit::Measured());
      QCOMPARE(int(r2.transitionShiftsMs.size()), 5);
      for (double shift : r2.transitionShiftsMs)
            QVERIFY2(std::fabs(shift - 120) < 1.5, qPrintable(QString::number(shift)));
      QVERIFY(r2.transitionsFallback.count("Violin - Performance"));
      QVERIFY2(r2.text().contains("fell back to the arrival): Violin - Performance"), qPrintable(r2.text()));
      score->setMetaTag(Playback::metaTag, "");
      delete score;
      }

//---------------------------------------------------------
//   auditScore
//    any score: MS_AUDIT_SCORE (.mscz/.mscx), MS_AUDIT_MAP (default: the shipped SSO map, with its shipped
//    dynamics calibration), MS_AUDIT_SETTINGS (a preset id, default "recommended", or a playbackSettings metaTag
//    "id=value;..."), MS_AUDIT_OUT (the report; else printed), MS_AUDIT_DUMP (a folder: the heard check's inputs,
//    PlaybackAudit::writeHeardInputs). Fails on any OVERLAP
//---------------------------------------------------------

void TestPlaybackAudit::auditScore()
      {
      if (!qEnvironmentVariableIsSet("MS_AUDIT_SCORE"))
            QSKIP("MS_AUDIT_SCORE not set");
      const QString share = root + "/../share/soundlibraries/";
      const QString mapFile = qEnvironmentVariableIsSet("MS_AUDIT_MAP") ? qEnvironmentVariable("MS_AUDIT_MAP")
                                                                         : share + "Spitfire Symphony Orchestra.xml";
      QString error;
      std::shared_ptr<SoundLib::Library> lib = SoundLib::Library::load(mapFile, &error);
      QVERIFY2(lib, qPrintable(error));
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      const QString calFile = QFileInfo(mapFile).absolutePath() + "/" + QFileInfo(mapFile).completeBaseName() + ".dynamics.json";
      auto cal = std::make_shared<SoundLib::DynamicsCalibration>();
      if (cal->read(calFile))
            SoundLib::setDynamicsCalibration(cal);
      const QString settings = qEnvironmentVariableIsSet("MS_AUDIT_SETTINGS") ? qEnvironmentVariable("MS_AUDIT_SETTINGS")
                                                                               : QString("recommended");
      MasterScore* score = readCreatedScore(qEnvironmentVariable("MS_AUDIT_SCORE"));
      QVERIFY(score);
      score->rebuildMidiMapping();
      if (settings.contains('='))
            score->setMetaTag(Playback::metaTag, settings);
      else {
            bool found = false;
            std::map<QString, QString> values;
            for (const Playback::Preset& p : Playback::presets())
                  if (settings == p.id) {
                        found = true;
                        for (const auto& v : p.values)
                              values[v.first] = QString::number(v.second);
                        }
            QVERIFY2(found, qPrintable("no preset " + settings));
            Playback::setIniValuesForTest(values);
            }
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      PlaybackAudit::render(score, &events, &trace);
      const PlaybackAudit::Measured measured = PlaybackAudit::measuredFrom(root + "/../tools/soundlibraries");
      const PlaybackAudit::Report r = PlaybackAudit::audit(score, events, trace, measured);
      // MS_AUDIT_DUMP=<folder>: the heard check's inputs (VERIFY.md › Heard check)
      if (qEnvironmentVariableIsSet("MS_AUDIT_DUMP"))
            QVERIFY(PlaybackAudit::writeHeardInputs(score, events, r, qEnvironmentVariable("MS_AUDIT_DUMP")));
      const QString text = r.text(QString("Playback audit of %1 (settings: %2; map: %3; calibration: %4; fits: %5)")
                                  .arg(QFileInfo(qEnvironmentVariable("MS_AUDIT_SCORE")).fileName(), settings,
                                       QFileInfo(mapFile).fileName(), SoundLib::dynamicsCalibration() ? "shipped" : "none",
                                       measured.files.join(", ")));
      if (qEnvironmentVariableIsSet("MS_AUDIT_OUT")) {
            QFile f(qEnvironmentVariable("MS_AUDIT_OUT"));
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(text.toUtf8());
            }
      else
            qInfo("%s", qPrintable(text));
      for (PlaybackAudit::Kind k : { PlaybackAudit::Kind::OVERLAP, PlaybackAudit::Kind::UNMEASURED, PlaybackAudit::Kind::CAPPED,
                                     PlaybackAudit::Kind::ARRIVAL, PlaybackAudit::Kind::LEVEL_STEP,
                                     PlaybackAudit::Kind::DROPPED, PlaybackAudit::Kind::LEAP })
            qInfo("%s: %d fail, %d warn, %d info", PlaybackAudit::kindName(k), r.count(k, PlaybackAudit::Severity::FAIL),
                  r.count(k, PlaybackAudit::Severity::WARN), r.count(k, PlaybackAudit::Severity::INFO));
      const int overlaps = r.count(PlaybackAudit::Kind::OVERLAP);
      delete score;
      QVERIFY2(overlaps == 0, qPrintable(QString("%1 OVERLAP").arg(overlaps)));
      }

QTEST_MAIN(TestPlaybackAudit)
#include "tst_playbackaudit.moc"
