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
#include <QTemporaryFile>
#include <cmath>

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
//   auditScore
//    any score: MS_AUDIT_SCORE (.mscz/.mscx), MS_AUDIT_MAP (default: the shipped SSO map, with its shipped
//    dynamics calibration), MS_AUDIT_SETTINGS (a preset id, default "recommended", or a playbackSettings metaTag
//    "id=value;..."), MS_AUDIT_OUT (the report; else printed). Fails on any OVERLAP
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
                                     PlaybackAudit::Kind::ARRIVAL, PlaybackAudit::Kind::LEVEL_STEP })
            qInfo("%s: %d fail, %d warn, %d info", PlaybackAudit::kindName(k), r.count(k, PlaybackAudit::Severity::FAIL),
                  r.count(k, PlaybackAudit::Severity::WARN), r.count(k, PlaybackAudit::Severity::INFO));
      const int overlaps = r.count(PlaybackAudit::Kind::OVERLAP);
      delete score;
      QVERIFY2(overlaps == 0, qPrintable(QString("%1 OVERLAP").arg(overlaps)));
      }

QTEST_MAIN(TestPlaybackAudit)
#include "tst_playbackaudit.moc"
