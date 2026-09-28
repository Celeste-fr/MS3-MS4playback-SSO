//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

//---------------------------------------------------------
//   Playing through Ableton Live (LIVE.md): MIDI sync out (libmscore/midisync.h)
//---------------------------------------------------------

#include <cmath>
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <zlib.h>

#include "audio/midi/event.h"
#include "libmscore/automation.h"
#include "libmscore/instrument.h"
#include "libmscore/liveset.h"
#include "libmscore/midisync.h"
#include "libmscore/part.h"
#include "libmscore/rendermidi.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/tempo.h"
#include "mtest/testutils.h"

#define DIR QString("libmscore/liveintegration/")
#define DIR_ROOT (MTest::rootPath() + "/" + DIR)

using namespace Ms;

class TestLiveIntegration : public QObject, public MTest
      {
      Q_OBJECT

   private slots:
      void initTestCase() { initMTest(); }
      void clockFromStart();
      void clockFromMiddle();
      void clockFollowsTempo();
      void clockLocate();
      void liveSetRead();
      void liveSetCurve();
      void liveSetLanes();
      void liveLanesPlayback();
      };

//---------------------------------------------------------
//   gzip: the fixture's XML as Live writes a set (gzip)
//---------------------------------------------------------

static QByteArray gzip(const QByteArray& in)
      {
      z_stream zs;
      memset(&zs, 0, sizeof(zs));
      deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
      QByteArray out(int(deflateBound(&zs, uLong(in.size()))) + 64, 0);
      zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
      zs.avail_in = uInt(in.size());
      zs.next_out = reinterpret_cast<Bytef*>(out.data());
      zs.avail_out = uInt(out.size());
      deflate(&zs, Z_FINISH);
      out.resize(int(zs.total_out));
      deflateEnd(&zs);
      return out;
      }

// the fixture as an .als in dir
static QString writeSet(const QTemporaryDir& dir)
      {
      QFile x(QString(DIR_ROOT) + "liveset.xml");
      if (!x.open(QIODevice::ReadOnly))
            return QString();
      const QString path = dir.path() + "/Test Project/Test.als";
      QDir().mkpath(dir.path() + "/Test Project");
      QFile f(path);
      f.open(QIODevice::WriteOnly);
      f.write(gzip(x.readAll()));
      return path;
      }

static std::shared_ptr<SoundLib::Library> loadMap(const QString& xml)
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

static const char* const MAP =
   "<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/>"
   "<Controller id='vibrato' name='Vibrato' param='Vibrato'/>"
   "<Controller id='tone' name='Tone' param='Tone'/>"
   "<Controller id='expr2' name='Expression 2' cc='21' default='64'/>"
   "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
   "<Instrument name='Flute' ids='flute'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
   "</SoundLibrary>";

static QStringList text(const std::vector<MidiSync::Message>& m)
      {
      QStringList l;
      for (const MidiSync::Message& x : m)
            l << MidiSync::toString(x);
      return l;
      }

//---------------------------------------------------------
//   clockFromStart
//    at 120 bpm from the start: Start, then a clock every 1/48 s (20 ticks), in order and once
//    each whatever the period; Stop at the end
//---------------------------------------------------------

void TestLiveIntegration::clockFromStart()
      {
      auto time = [](int utick) { return utick / 960.0; };     // 120 bpm
      for (double period : { 0.001, 0.0107, 0.093 }) {
            const std::vector<MidiSync::Message> m = MidiSync::schedule(time, 0, 2.0, period);
            QVERIFY(m.size() > 3);
            QCOMPARE(m.front().status, int(MidiSync::START));
            QCOMPARE(m.back().status, int(MidiSync::STOP));
            int clocks = 0;
            for (size_t i = 1; i + 1 < m.size(); ++i) {
                  QCOMPARE(m[i].status, int(MidiSync::CLOCK));
                  QCOMPARE(m[i].value, clocks * 20);
                  QVERIFY(std::fabs(m[i].seconds - clocks / 48.0) < 1e-9);
                  ++clocks;
                  }
            QCOMPARE(clocks, 96);         // 2 s at 48 clocks a second
            }
      }

//---------------------------------------------------------
//   clockFromMiddle
//    from a tick between 16ths: SPP of the next 16th, Continue, then the first clock at that
//    16th's time (a slave starts on the first clock after Continue, at the SPP's position)
//---------------------------------------------------------

void TestLiveIntegration::clockFromMiddle()
      {
      auto time = [](int utick) { return utick / 960.0; };
      const std::vector<MidiSync::Message> m = MidiSync::schedule(time, 500, 1.0);
      QCOMPARE(text(m).mid(0, 3), QStringList({ "0.5208 SPP 5", "0.5208 continue", "0.6250 clock 600" }));
      // on a 16th: that 16th
      const std::vector<MidiSync::Message> n = MidiSync::schedule(time, 960, 1.1);
      QCOMPARE(text(n).mid(0, 3), QStringList({ "1.0000 SPP 8", "1.0000 continue", "1.0000 clock 960" }));
      // past the 14-bit SPP: its last value (1024 bars of 4/4)
      QCOMPARE(MidiSync::Clock::sppOf(16383 * 120 + 600), 16383);
      QCOMPARE(MidiSync::Clock::sppOf(1), 1);
      QCOMPARE(MidiSync::Clock::sppOf(0), 0);
      }

//---------------------------------------------------------
//   clockFollowsTempo
//    the clocks follow the tempo map and the relative tempo (the Play Panel): each clock at its
//    tick's time
//---------------------------------------------------------

void TestLiveIntegration::clockFollowsTempo()
      {
      TempoMap tm;
      tm.setTempo(0, 2.0);                // 120 bpm
      tm.setTempo(960, 1.0);              // 60 bpm from beat 3
      tm.setRelTempo(1.5);                // played at 150 %
      auto time = [&tm](int utick) { return tm.tick2time(utick); };
      const double end = tm.tick2time(1920);
      const std::vector<MidiSync::Message> m = MidiSync::schedule(time, 0, end, 0.0107);
      std::vector<MidiSync::Message> clocks;
      for (const MidiSync::Message& x : m)
            if (x.status == MidiSync::CLOCK)
                  clocks.push_back(x);
      QCOMPARE(int(clocks.size()), 96);   // 4 beats (the clock at 1920 is at the end)
      for (const MidiSync::Message& c : clocks)
            QVERIFY(std::fabs(c.seconds - tm.tick2time(c.value)) < 1e-9);
      const double fast = clocks[1].seconds - clocks[0].seconds;
      const double slow = clocks[60].seconds - clocks[59].seconds;
      QVERIFY2(std::fabs(fast - 1.0 / (24 * 2.0 * 1.5)) < 1e-9, qPrintable(QString::number(fast)));
      QVERIFY2(std::fabs(slow - 1.0 / (24 * 1.0 * 1.5)) < 1e-9, qPrintable(QString::number(slow)));
      }

//---------------------------------------------------------
//   clockLocate
//    a jump while running: Stop, SPP, Continue, then clocks from there; while stopped: SPP only
//---------------------------------------------------------

void TestLiveIntegration::clockLocate()
      {
      std::vector<MidiSync::Message> m;
      auto out = [&m](const MidiSync::Message& x) { m.push_back(x); };
      auto time = [](int utick) { return utick / 960.0; };
      MidiSync::Clock c;
      c.locate(1000, 0, out);             // stopped: SPP only
      QCOMPARE(text(m), QStringList({ "0.0000 SPP 9" }));
      m.clear();
      c.start(0, 0, out);
      c.run(0.05, time, out);
      QCOMPARE(text(m), QStringList({ "0.0000 start", "0.0000 clock 0", "0.0208 clock 20", "0.0417 clock 40" }));
      m.clear();
      c.locate(1920, 0.05, out);          // a loop back or a seek
      c.run(2.01, time, out);
      QCOMPARE(text(m), QStringList({ "0.0500 stop", "0.0500 SPP 16", "0.0500 continue", "2.0000 clock 1920" }));
      m.clear();
      c.stop(3, out);
      c.stop(3, out);                     // once
      QCOMPARE(text(m), QStringList({ "3.0000 stop" }));
      QVERIFY(!c.running());
      }


//---------------------------------------------------------
//   liveSetRead
//    the hand-built set (liveset.xml, gzipped as Live saves): tracks, MIDI input, plug-in
//    parameters, envelopes, a looped clip's CC envelope placed in the arrangement
//---------------------------------------------------------

void TestLiveIntegration::liveSetRead()
      {
      QTemporaryDir dir;
      const QString path = writeSet(dir);
      QVERIFY(!path.isEmpty());
      // gunzip gives the XML back
      QFile x(DIR_ROOT + "liveset.xml");
      QVERIFY(x.open(QIODevice::ReadOnly));
      const QByteArray xml = x.readAll();
      QString error;
      QCOMPARE(LiveSet::gunzip(gzip(xml), &error), xml);
      QVERIFY(LiveSet::gunzip("not gzip", &error).isEmpty() && !error.isEmpty());

      const LiveSet::Set set = LiveSet::read(path);
      QVERIFY2(set.error.isEmpty(), qPrintable(set.error));
      QCOMPARE(set.creator, QString("Ableton Live 12.2.6"));
      QCOMPARE(set.tempo, 120.0);
      QCOMPARE(int(set.tracks.size()), 2);
      const LiveSet::Track& t = set.tracks[0];
      QCOMPARE(t.name, QString("Strings 1"));
      QCOMPARE(t.inputDevice, QString("MuseScore A"));
      QCOMPARE(t.inputChannel, 1);
      QCOMPARE(t.devices, QStringList({ "Kontakt 8" }));
      QCOMPARE(int(t.envelopes.size()), 4);
      const LiveSet::Envelope& vib = t.envelopes[0];
      QCOMPARE(int(vib.kind), int(LiveSet::Envelope::Kind::PARAMETER));
      QCOMPARE(vib.device, QString("Kontakt 8"));
      QCOMPARE(vib.parameter, QString("Vibrato"));
      QCOMPARE(vib.parameterId, 1);
      QCOMPARE(vib.initial, 0.25);                  // Live's value before everything (-63072000)
      // 0, 4, the curve's 16 pieces to 8, 26, 34
      QCOMPARE(int(vib.points.size()), 2 + 16 + 2);
      QCOMPARE(vib.points[1].beat, 4.0);
      QCOMPARE(vib.points[17].beat, 8.0);
      QCOMPARE(vib.points[17].value, 0.5);
      QCOMPARE(t.envelopes[1].parameter, QString("Unknown Knob"));
      QCOMPARE(int(t.envelopes[2].kind), int(LiveSet::Envelope::Kind::OTHER));
      QCOMPARE(t.envelopes[2].parameter, QString("Mixer Volume"));
      // the clip: beats 8-16, its 4-beat loop twice, CC 21 from 0 to 127 each time
      const LiveSet::Envelope& clip = t.envelopes[3];
      QCOMPARE(int(clip.kind), int(LiveSet::Envelope::Kind::CLIP_CC));
      QCOMPARE(clip.cc, 21);
      QStringList pts;
      for (const LiveSet::Point& p : clip.points)
            pts << QString("%1:%2").arg(p.beat).arg(p.value);
      QCOMPARE(pts, QStringList({ "8:0", "12:127", "12:0", "16:127" }));

      const LiveSet::Track& f = set.tracks[1];
      QCOMPARE(f.name, QString("Flute"));
      QCOMPARE(f.inputDevice, QString());           // all inputs
      QCOMPARE(f.inputChannel, -1);
      QCOMPARE(f.envelopes[0].parameter, QString("#002 Tone"));

      QVERIFY(!LiveSet::parse("<LiveSet/>").error.isEmpty());      // not a set
      }

//---------------------------------------------------------
//   liveSetCurve
//    a curved segment as straight pieces: from a to b, inside their box; the diagonal is a line
//---------------------------------------------------------

void TestLiveIntegration::liveSetCurve()
      {
      const LiveSet::Point a { 4, 1.0 }, b { 8, 0.5 };
      const std::vector<LiveSet::Point> c = LiveSet::curve(a, b, 0.2, 0.8, 0.5, 1.0);
      QCOMPARE(int(c.size()), 16);
      QCOMPARE(c.back().beat, 8.0);
      QCOMPARE(c.back().value, 0.5);
      double last = a.beat;
      for (const LiveSet::Point& p : c) {
            QVERIFY(p.beat >= last && p.beat <= 8.0);
            QVERIFY(p.value >= 0.5 - 1e-9 && p.value <= 1.0 + 1e-9);
            last = p.beat;
            }
      // bowed towards the end value early (1Y 0.8 at 1X 0.2): half way the value is past half way
      const double mid = c[7].value;
      QVERIFY2(mid < 0.75, qPrintable(QString::number(mid)));
      const std::vector<LiveSet::Point> line = LiveSet::curve(a, b, 1.0 / 3, 1.0 / 3, 2.0 / 3, 2.0 / 3, 4);
      for (const LiveSet::Point& p : line)
            QVERIFY(std::fabs(p.value - (1.0 - 0.5 * (p.beat - 4) / 4)) < 1e-9);
      }

//---------------------------------------------------------
//   liveSetLanes
//    the set's envelopes as the score's lanes: parts by MIDI input (port and channel), else by
//    name; controllers by parameter title (a Kontakt slot number ignored) or CC; beats to ticks,
//    a repeat's second pass left out; marked read-only, kept in the metaTag through a save
//---------------------------------------------------------

void TestLiveIntegration::liveSetLanes()
      {
      MasterScore* score = readScore(DIR + "violin-flute.musicxml");
      QVERIFY(score);
      auto lib = loadMap(MAP);
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      score->rebuildMidiMapping();
      QTemporaryDir dir;
      const QString path = writeSet(dir);
      const LiveSet::Set set = LiveSet::read(path);
      const std::vector<LiveSet::PartInfo> parts = LiveSet::partInfos(score, { "MuseScore A", "MuseScore B", "", "" });
      QCOMPARE(int(parts.size()), 2);
      QCOMPARE(parts[0].part, score->parts()[0]);
      QCOMPARE(parts[0].portName, QString("MuseScore A"));
      QCOMPARE(parts[0].channel, 1);
      QCOMPARE(LiveSet::portDisplayName("MMSystem,MuseScore A"), QString("MuseScore A"));

      const QDateTime modified = QFileInfo(path).lastModified();
      LiveSet::Report report;
      const std::map<const Part*, Automation::PartLanes> lanes = LiveSet::lanes(score, set, parts, path, modified, &report);
      QCOMPARE(report.lanes, 3);
      QCOMPARE(report.repeatedPoints, 1);
      QCOMPARE(int(report.unmatched.size()), 2);           // the unknown knob, the mixer's volume
      QVERIFY(report.unmatched.join("\n").contains("Unknown Knob"));
      QVERIFY(report.unmatched.join("\n").contains("Mixer Volume"));
      QVERIFY(report.matched.join("\n").contains("MIDI input"));
      QVERIFY(report.matched.join("\n").contains("name"));

      const Automation::PartLanes& violin = lanes.at(score->parts()[0]);
      QCOMPARE(int(violin.size()), 2);
      const Automation::Lane& vib = violin[0];
      QCOMPARE(vib.target, QString("vibrato"));
      QVERIFY(vib.readOnly());
      QCOMPARE(vib.source(), QString("live"));
      QCOMPARE(vib.extra.value("track").toString(), QString("Strings 1"));
      QCOMPARE(vib.extra.value("param").toString(), QString("Vibrato"));
      QCOMPARE(vib.extra.value("paramId").toInt(), 1);
      QCOMPARE(vib.extra.value("set").toString(), path);
      QCOMPARE(vib.points.front().tick, 0);
      QCOMPARE(vib.points.front().value, 0.25);
      QCOMPARE(vib.points[1].tick, 1920);                 // beat 4
      QCOMPARE(vib.points[1].value, 1.0);
      QCOMPARE(vib.valueAt(3840), 0.5);                   // beat 8
      QCOMPARE(vib.points.back().tick, 26 * 480);         // beat 34, played after the repeat: bar 7's
      QCOMPARE(vib.points.back().value, 0.1);
      for (const Automation::Point& p : vib.points)
            QVERIFY(p.tick < 16 * 480 || p.tick >= 24 * 480);     // (beat 26: the repeat's second pass, left out)
      const Automation::Lane& cc = violin[1];
      QCOMPARE(cc.target, QString("expr2"));              // CC 21 is the map's "expr2"
      QCOMPARE(cc.valueAt(3840), 0.0);
      QCOMPARE(cc.valueAt(5760 - 1) > 0.99, true);
      QCOMPARE(cc.valueAt(5760), 0.0);                    // the loop again
      const Automation::PartLanes& flute = lanes.at(score->parts()[1]);
      QCOMPARE(int(flute.size()), 1);
      QCOMPARE(flute[0].target, QString("tone"));         // "#002 Tone"
      QCOMPARE(flute[0].valueAt(1919), 0.0);
      QCOMPARE(flute[0].valueAt(1920), 1.0);

      // kept with the score's own lanes: an import replaces its own only
      Automation::Lane own;
      own.target = "cc7";
      own.points = { { 0, 0.5, Automation::Curve::STEP } };
      std::map<const Part*, Automation::PartLanes> all { { score->parts()[1], { own } } };
      all = Automation::replaceSource(all, Automation::SOURCE_LIVE, lanes);
      QCOMPARE(int(all.at(score->parts()[1]).size()), 2);
      score->setMetaTag(Automation::metaTag, Automation::write(score, all));
      all = Automation::replaceSource(all, Automation::SOURCE_LIVE, lanes);          // again: the same
      QCOMPARE(Automation::write(score, all), score->metaTag(Automation::metaTag));

      // through a save (the metaTag is what MuseScore 3.6 keeps)
      QVERIFY(saveScore(score, "liveintegration-lanes.mscx"));
      MasterScore* back = readCreatedScore("liveintegration-lanes.mscx");
      QVERIFY(back);
      QCOMPARE(back->metaTag(Automation::metaTag), score->metaTag(Automation::metaTag));
      const std::map<const Part*, Automation::PartLanes> read = Automation::read(back);
      QCOMPARE(int(read.at(back->parts()[0]).size()), 2);
      QVERIFY(read.at(back->parts()[0])[0].readOnly());
      QVERIFY(!read.at(back->parts()[1])[0].readOnly());           // the score's own
      QCOMPARE(read.at(back->parts()[0])[0].extra.value("param").toString(), QString("Vibrato"));
      delete back;
      delete score;
      SoundLib::setCurrent(nullptr);
      }

//---------------------------------------------------------
//   liveLanesPlayback
//    through MIDI output (to Live) a lane from Live sends nothing, and holds its controller's place
//    (no part value either); with the hosted plug-in it plays like any lane
//---------------------------------------------------------

void TestLiveIntegration::liveLanesPlayback()
      {
      MasterScore* score = readScore(DIR + "violin-flute.musicxml");
      QVERIFY(score);
      auto lib = loadMap(MAP);
      SoundLib::setCurrent(lib);
      score->rebuildMidiMapping();
      QTemporaryDir dir;
      const QString path = writeSet(dir);
      LiveSet::Report report;
      const auto lanes = LiveSet::lanes(score, LiveSet::read(path), LiveSet::partInfos(score, { "MuseScore A" }), path,
                                        QDateTime::currentDateTime(), &report);
      score->setMetaTag(Automation::metaTag, Automation::write(score, lanes));
      const int ch = score->parts()[0]->instrument()->channel(0)->channel();
      auto render = [&](SoundLib::Output output, std::vector<int>* cc21, int* params) {
            SoundLib::setOutput(output);
            score->setPlaylistDirty();
            EventMap events;
            SynthesizerState ss;
            score->renderMidi(&events, false, true, ss);
            for (const auto& te : events) {
                  const NPlayEvent& e = te.second;
                  if (e.channel() != ch)
                        continue;
                  if (e.type() == ME_CONTROLLER && e.dataA() == 21)
                        cc21->push_back(te.first);
                  if (e.type() == ME_PARAMETER)
                        ++*params;
                  }
            };
      std::vector<int> cc21;
      int params = 0;
      render(SoundLib::Output::MIDI, &cc21, &params);
      QVERIFY2(cc21.empty(), qPrintable(QString::number(cc21.size())));    // not even the map's default 64
      QCOMPARE(params, 0);
      render(SoundLib::Output::PLUGIN, &cc21, &params);
      QVERIFY(!cc21.empty());
      QVERIFY(params > 0);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      SoundLib::setCurrent(nullptr);
      delete score;
      }

QTEST_MAIN(TestLiveIntegration)
#include "tst_liveintegration.moc"
