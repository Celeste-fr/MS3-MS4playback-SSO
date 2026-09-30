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
#include "libmscore/liveclips.h"
#include "libmscore/liveset.h"
#include "libmscore/midisync.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/undo.h"
#include "libmscore/part.h"
#include "libmscore/repeatlist.h"
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
      void clipsTimeline();
      void clipsControllers();
      void clipsScore();
      void clipsChanges();
      void clipsOsc();
      void clipsImport();
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

//---------------------------------------------------------
//   Live plays the score (liveclips.h)
//---------------------------------------------------------

// the score rendered as LiveClipsLink renders it (a chunk at a time, no metronome)
static EventMap renderClips(MasterScore* score)
      {
      EventMap events;
      SynthesizerState ss;
      MidiRenderer r(score);
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      ctx.renderHarmony = true;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk c = r.getChunkAt(utick);
            if (!c)
                  break;
            r.renderChunk(c, &events, ctx);
            utick = c.utick2();
            }
      return events;
      }

static QString describe(const std::vector<LiveClips::Note>& notes)
      {
      QStringList l;
      for (const LiveClips::Note& n : notes)
            l << QString("%1@%2+%3v%4").arg(n.pitch).arg(n.start).arg(n.length).arg(n.velocity);
      return l.join(" ");
      }

static std::vector<LiveClips::Note> realNotes(const std::vector<LiveClips::Note>& notes)
      {
      std::vector<LiveClips::Note> o;
      for (const LiveClips::Note& n : notes)
            if (n.pitch < LiveClips::CARRIER_LOW)
                  o.push_back(n);
      return o;
      }

static std::vector<LiveClips::Note> carriers(const std::vector<LiveClips::Note>& notes, int pitch)
      {
      std::vector<LiveClips::Note> o;
      for (const LiveClips::Note& n : notes)
            if (n.pitch == pitch)
                  o.push_back(n);
      return o;
      }

static constexpr int U = LiveClips::UNITS_PER_BEAT;

//---------------------------------------------------------
//   clipsTimeline
//    Live at the score's first tempo, the notes at their real times: clips.musicxml is 60 bpm, then
//    120 from bar 3, bars 2-3 repeated (played 1 2 3 2 3 4: 4 + 4 + 2 + 4 + 2 + 2 s); back from
//    Live's beat to the played tick; a score of one tempo: beat = tick / 480 exactly
//---------------------------------------------------------

void TestLiveIntegration::clipsTimeline()
      {
      MasterScore* score = readScore(DIR + "clips.musicxml");
      QVERIFY(score);
      score->setExpandRepeats(true);
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      QCOMPARE(tl.bpm, 60.0);
      QCOMPARE(LiveClips::playedTicks(score), 6 * 1920);
      const double starts[] = { 0, 4, 8, 10, 14, 16, 18 };
      for (int bar = 0; bar <= 6; ++bar) {
            QVERIFY2(std::fabs(tl.beats(bar * 1920) - starts[bar]) < 1e-9, qPrintable(QString::number(tl.beats(bar * 1920))));
            QCOMPARE(tl.units(bar * 1920), int(starts[bar]) * U);
            }
      QCOMPARE(tl.beats(3840 + 480), 8.5);                  // a beat at 120 is half a second
      QCOMPARE(tl.beats(5760 + 480), 11.0);                 // bar 2 again: 60
      QCOMPARE(tl.utick(8.5), 3840 + 480);
      QCOMPARE(tl.utick(10.0), 5760);
      QCOMPARE(tl.utick(10.5), 5760 + 240);
      QCOMPARE(tl.utick(2.0), 960);                         // 2 s at 60
      // the Play Panel's tempo is left out: Live plays the written tempo
      score->tempomap()->setRelTempo(0.5);
      QCOMPARE(LiveClips::timeline(score).beats(1920), 4.0);
      QCOMPARE(LiveClips::timeline(score).utick(4.0), 1920);
      score->tempomap()->setRelTempo(1.0);
      delete score;

      MasterScore* one = readScore(DIR + "violin-flute.musicxml");        // no tempo: 120
      QVERIFY(one);
      one->setExpandRepeats(true);
      const LiveClips::Timeline t1 = LiveClips::timeline(one);
      QCOMPARE(t1.bpm, 120.0);
      for (int utick = 0; utick <= 10 * 1920; utick += 120)
            QCOMPARE(t1.units(utick), utick * U / 480);
      delete one;
      }

//---------------------------------------------------------
//   clipsControllers
//    the controllers as carrier notes: a controller before the tick's note goes EPSILON before it
//    (in order), one after it EPSILON after; a keyswitch like a controller, as a note; each carrier
//    lasts until its controller's next value (chased from there), the last to the clip's end;
//    velocity = value + 1 (127 as 126); at the clip's start the note waits instead; set twice at a
//    tick: the last value; a key struck again; what a clip can't hold is counted
//---------------------------------------------------------

void TestLiveIntegration::clipsControllers()
      {
      auto ev = [](int type, int a, int b, bool sw = false) {
            NPlayEvent e(uchar(type), 0, uchar(a), uchar(b));
            e.setExternal(0, 2);
            e.setLibrarySwitch(sw);
            return e;
            };
      EventMap m;
      // tick 0: UACC 1 and CC1 64, then a note: nothing can come before 0
      m.insert({ 0, ev(ME_CONTROLLER, 32, 1, true) });
      m.insert({ 0, ev(ME_CONTROLLER, 1, 64) });
      m.insert({ 0, ev(ME_NOTEON, 60, 90) });
      // tick 480: the note off, CC1 set twice (40 then 50), a keyswitch, the note, the pedal after it
      m.insert({ 480, ev(ME_NOTEON, 60, 0) });
      m.insert({ 480, ev(ME_CONTROLLER, 1, 40) });
      m.insert({ 480, ev(ME_CONTROLLER, 1, 50) });
      m.insert({ 480, ev(ME_NOTEON, 24, 100, true) });
      m.insert({ 480, ev(ME_NOTEON, 62, 80) });
      m.insert({ 480, ev(ME_CONTROLLER, 64, 127) });
      m.insert({ 500, ev(ME_NOTEON, 24, 0, true) });
      // tick 960: the same key struck again before its first note ends; things a clip can't hold
      m.insert({ 960, ev(ME_NOTEON, 62, 70) });
      m.insert({ 1000, ev(ME_NOTEON, 62, 0) });
      m.insert({ 1200, ev(ME_NOTEON, 62, 0) });
      m.insert({ 1200, ev(ME_PITCHBEND, 0, 64) });
      m.insert({ 1200, ev(ME_CONTROLLER, 7, 100) });
      NPlayEvent par(ME_PARAMETER, 0, 0, 0);
      par.setExternal(0, 2);
      m.insert({ 1200, par });
      m.insert({ 1300, ev(ME_NOTEON, 120, 60) });             // a key the carriers use
      m.insert({ 1400, ev(ME_NOTEON, 120, 0) });
      // another route: not mixed in
      NPlayEvent other(ME_NOTEON, 0, 50, 50);
      other.setExternal(1, 0);
      m.insert({ 0, other });

      LiveClips::Timeline tl;             // (no score: beat = tick / 480)
      const int end = 1920;
      const std::map<int, LiveClips::RouteNotes> r = LiveClips::clipNotes(m, tl, end);
      QCOMPARE(int(r.size()), 2);
      const LiveClips::RouteNotes& rn = r.at(2);
      const std::vector<LiveClips::Note>& n = rn.notes;
      const int E = LiveClips::EPSILON;
      const int b = U;                                        // tick 480

      const std::vector<LiveClips::Note> uacc = carriers(n, 127);
      QCOMPARE(int(uacc.size()), 1);
      QCOMPARE(uacc[0].start, 0);
      QCOMPARE(uacc[0].velocity, 2);                          // UACC 1
      QCOMPARE(uacc[0].length, 4 * U);                        // to the end: chased anywhere
      const std::vector<LiveClips::Note> dyn = carriers(n, 126);
      QCOMPARE(int(dyn.size()), 2);
      QCOMPARE(dyn[0].start, E);                              // after the switch, in order
      QCOMPARE(dyn[0].velocity, 65);
      QCOMPARE(dyn[0].length, b - 3 * E);                     // until the next value
      QCOMPARE(dyn[1].start, b - 2 * E);                      // before the keyswitch and the note
      QCOMPARE(dyn[1].velocity, 51);                          // the last of 40, 50
      QCOMPARE(dyn[1].start + dyn[1].length, 4 * U);
      const std::vector<LiveClips::Note> pedal = carriers(n, 124);
      QCOMPARE(int(pedal.size()), 1);
      QCOMPARE(pedal[0].start, b + E);                        // after the note, as rendered
      QCOMPARE(pedal[0].velocity, 127);                       // 127 as 126 + 1

      const std::vector<LiveClips::Note> notes = realNotes(n);
      QVERIFY2(notes.size() == 4, qPrintable(describe(n)));
      QCOMPARE(notes[0].pitch, 60);
      QCOMPARE(notes[0].start, 2 * E);                        // the note waits for its two controllers
      QCOMPARE(notes[0].start + notes[0].length, b);
      QCOMPARE(notes[0].velocity, 90);
      QCOMPARE(notes[1].pitch, 24);                           // the keyswitch, before the note
      QCOMPARE(notes[1].start, b - E);
      QCOMPARE(notes[1].start + notes[1].length, 500 * U / 480);
      QCOMPARE(notes[2].pitch, 62);
      QCOMPARE(notes[2].start, b);
      QCOMPARE(notes[2].start + notes[2].length, 1000 * U / 480);    // first on, first off
      QCOMPARE(notes[3].pitch, 62);
      QCOMPARE(notes[3].start, 2 * U);
      QCOMPARE(notes[3].start + notes[3].length, 1200 * U / 480);
      QCOMPARE(rn.highNotes, 1);
      QCOMPARE(rn.dropped, 2);                                // pitch bend, CC7
      QCOMPARE(rn.parameters, 1);
      for (size_t i = 1; i < n.size(); ++i)
            QVERIFY(n[i - 1].start <= n[i].start);
      }

//---------------------------------------------------------
//   clipsScore
//    a rendered score's clips: each route its notes in Live's timeline, the repeat written out,
//    the switch and dynamics carriers before each note; the tempo and a locator a played bar
//---------------------------------------------------------

void TestLiveIntegration::clipsScore()
      {
      MasterScore* score = readScore(DIR + "clips.musicxml");
      QVERIFY(score);
      auto lib = loadMap(MAP);
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      score->rebuildMidiMapping();
      score->setExpandRepeats(true);
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const EventMap events = renderClips(score);
      const std::vector<LiveClips::Track> tracks = LiveClips::tracks(score, *lib, events, { "MuseScore A" }, tl);
      QCOMPARE(int(tracks.size()), 2);
      const LiveClips::Track& violin = tracks[0];
      QCOMPARE(violin.key, QString("0:1"));
      QCOMPARE(violin.portName, QString("MuseScore A"));
      QCOMPARE(violin.channel, 1);
      QCOMPARE(violin.part, QString("Violin"));
      QCOMPARE(violin.clip, QString("MuseScore: Violin"));
      QVERIFY(violin.main);
      QCOMPARE(violin.length, 18 * U);
      const std::vector<LiveClips::Note> notes = realNotes(violin.notes);
      QVERIFY2(notes.size() == 6, qPrintable(describe(violin.notes)));
      const int pitches[] = { 72, 74, 76, 74, 76, 77 };      // C5 D5 E5, D5 E5 again, F5
      const int starts[] = { 0, 4, 8, 10, 14, 16 };          // seconds = beats at 60
      const int lengths[] = { 4, 4, 2, 4, 2, 2 };
      for (int i = 0; i < 6; ++i) {
            const LiveClips::Note& x = notes[size_t(i)];
            QVERIFY2(x.pitch == pitches[i], qPrintable(describe(notes)));
            // (the first waits for its carriers at the clip's start)
            QVERIFY2(x.start >= starts[i] * U && x.start <= starts[i] * U + 8 * LiveClips::EPSILON, qPrintable(describe(notes)));
            QVERIFY2(x.length > lengths[i] * U / 2 && x.start + x.length <= (starts[i] + lengths[i]) * U, qPrintable(describe(notes)));
            QVERIFY(x.velocity > 0 && x.velocity < 128);
            QVERIFY(!x.muted);
            }
      // the articulation switch (UACC CC32, "Long" = 1) and the dynamics (CC1) in force at each note
      const std::vector<LiveClips::Note> uacc = carriers(violin.notes, 127);
      const std::vector<LiveClips::Note> dyn = carriers(violin.notes, 126);
      QVERIFY2(!uacc.empty() && !dyn.empty(), qPrintable(describe(violin.notes)));
      QVERIFY(uacc.front().start <= notes.front().start - LiveClips::EPSILON);
      QCOMPARE(uacc.front().velocity, 2);
      for (const std::vector<LiveClips::Note>* c : { &uacc, &dyn }) {
            QCOMPARE(c->front().start < notes.front().start, true);
            for (size_t i = 1; i < c->size(); ++i)
                  QCOMPARE((*c)[i - 1].start + (*c)[i - 1].length, (*c)[i].start);   // no gap: chased anywhere
            QCOMPARE(c->back().start + c->back().length, violin.length);
            }
      QCOMPARE(violin.highNotes, 0);
      const LiveClips::Track& flute = tracks[1];
      QCOMPARE(flute.channel, 2);
      QCOMPARE(flute.clip, QString("MuseScore: Flute"));
      const std::vector<LiveClips::Note> fn = realNotes(flute.notes);
      QCOMPARE(int(fn.size()), 2);
      QCOMPARE(fn[0].pitch, 67);
      QCOMPARE(fn[1].pitch, 69);
      QCOMPARE(fn[1].start, 2 * U);

      const LiveClips::Song song = LiveClips::song(score, tl);
      QCOMPARE(song.bpm, 60.0);
      QCOMPARE(song.length, 18 * U);
      QStringList cues;
      for (const LiveClips::Cue& c : song.cues)
            cues << QString("%1@%2").arg(c.name).arg(c.time / U);
      QCOMPARE(cues.join(" "), QString("MS 1@0 MS 2@4 MS 3@8 MS 2@10 MS 3@14 MS 4@16"));

      // repeats off (the Play Panel's "Play repeats"): as written
      score->setExpandRepeats(false);
      score->setPlaylistDirty();
      const std::vector<LiveClips::Track> once = LiveClips::tracks(score, *lib, renderClips(score), { "MuseScore A" },
                                                                   LiveClips::timeline(score));
      QCOMPARE(int(realNotes(once[0].notes).size()), 4);
      QCOMPARE(once[0].length, 12 * U);
      QVERIFY(once[0].hash != violin.hash);
      score->setExpandRepeats(true);
      SoundLib::setCurrent(nullptr);
      delete score;
      }

//---------------------------------------------------------
//   clipsChanges
//    an edit changes the hash of its part's route only (only that clip is sent); the same score
//    renders the same hashes; the Mixer's mute marks the notes; undo gives the first hash back
//---------------------------------------------------------

void TestLiveIntegration::clipsChanges()
      {
      MasterScore* score = readScore(DIR + "clips.musicxml");
      QVERIFY(score);
      auto lib = loadMap(MAP);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      score->rebuildMidiMapping();
      score->setExpandRepeats(true);
      const QStringList ports { "MuseScore A" };
      auto clips = [&]() { return LiveClips::tracks(score, *lib, renderClips(score), ports, LiveClips::timeline(score)); };
      const std::vector<LiveClips::Track> a = clips();
      const std::vector<LiveClips::Track> again = clips();
      QCOMPARE(again[0].hash, a[0].hash);
      QCOMPARE(again[1].hash, a[1].hash);

      // the flute's A4 up a tone
      Note* note = nullptr;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s && !note; s = s->next1(SegmentType::ChordRest)) {
            Element* e = s->element(score->parts()[1]->startTrack());
            if (e && e->isChord() && toChord(e)->upNote()->pitch() == 69)
                  note = toChord(e)->upNote();
            }
      QVERIFY(note);
      score->startCmd();
      note->undoChangeProperty(Pid::PITCH, 71);
      note->undoChangeProperty(Pid::TPC1, note->tpc1() + 2);
      note->undoChangeProperty(Pid::TPC2, note->tpc2() + 2);
      score->endCmd();
      const std::vector<LiveClips::Track> b = clips();
      QCOMPARE(b[0].hash, a[0].hash);                       // the violin: not sent again
      QVERIFY(b[1].hash != a[1].hash);
      QCOMPARE(realNotes(b[1].notes)[1].pitch, 71);
      score->undoRedo(true, nullptr);
      QCOMPARE(clips()[1].hash, a[1].hash);

      // the Mixer's mute: the notes marked muted (drawn deactivated in Live)
      Channel* flutePlays = score->playbackChannel(score->parts()[1]->instrument()->channel(0));
      flutePlays->setMute(true);
      const std::vector<LiveClips::Track> m = clips();
      flutePlays->setMute(false);
      const std::vector<LiveClips::Note> mn = realNotes(m[1].notes);
      QVERIFY(!mn.empty());
      for (const LiveClips::Note& n : mn)
            QVERIFY(n.muted);
      QVERIFY(m[1].hash != a[1].hash);
      SoundLib::setCurrent(nullptr);
      delete score;
      }

//---------------------------------------------------------
//   clipsOsc
//    OSC 1.0 encoding (4-byte alignment, big-endian, type tags) and back; a clip as datagrams of at
//    most 48 notes, each well under a UDP datagram's limits, whose notes read back as sent; the
//    song's locators likewise
//---------------------------------------------------------

void TestLiveIntegration::clipsOsc()
      {
      const QByteArray m = LiveClips::osc("/ms/x", { 1, QString("ab"), 0.5, -2 });
      QCOMPARE(m.size(), 8 + 8 + 4 + 4 + 4 + 4);
      QCOMPARE(m.left(8), QByteArray("/ms/x\0\0\0", 8));
      QCOMPARE(m.mid(8, 8), QByteArray(",isfi\0\0\0", 8));
      QCOMPARE(m.mid(16, 4), QByteArray("\0\0\0\1", 4));
      QCOMPARE(m.mid(20, 4), QByteArray("ab\0\0", 4));
      QCOMPARE(m.mid(24, 4), QByteArray("\x3f\0\0\0", 4));
      QCOMPARE(m.mid(28, 4), QByteArray("\xff\xff\xff\xfe", 4));
      QString address;
      QVariantList args;
      QVERIFY(LiveClips::parseOsc(m, &address, &args));
      QCOMPARE(address, QString("/ms/x"));
      QCOMPARE(args.size(), 4);
      QCOMPARE(args[0].toInt(), 1);
      QCOMPARE(args[1].toString(), QString("ab"));
      QCOMPARE(args[2].toDouble(), 0.5);
      QCOMPARE(args[3].toInt(), -2);
      QVERIFY(!LiveClips::parseOsc(QByteArray("/ms/x\0\0\0,i\0\0", 12), &address, &args));      // an int missing
      QVERIFY(LiveClips::parseOsc(LiveClips::osc("/live/resync", {}), &address, &args));
      QCOMPARE(address, QString("/live/resync"));
      QVERIFY(args.isEmpty());
      QCOMPARE(LiveClips::osc("/abc", {}).size(), 4 + 4 + 4);         // a 4-byte string gets 4 zeros
      // a double, as Max may send one
      QByteArray d("/live/transport\0,ifd\0\0\0\0", 24);
      d += QByteArray("\0\0\0\1", 4) + QByteArray("\x41\x20\0\0", 4) + QByteArray("\x40\x5e\0\0\0\0\0\0", 8);
      QVERIFY(LiveClips::parseOsc(d, &address, &args));
      QCOMPARE(args[1].toDouble(), 10.0);
      QCOMPARE(args[2].toDouble(), 120.0);

      LiveClips::Track t;
      t.key = "1:16";
      t.port = 1;
      t.channel = 16;
      t.portName = "MuseScore B";
      t.part = "Violins 1 é";
      t.clip = LiveClips::clipName(t.part, "Violins 1 - Long", true, 0);
      t.length = 100000;
      for (int i = 0; i < 100; ++i)
            t.notes.push_back({ 40 + i % 50, i * 240, 200, 1 + i, i % 7 == 0 });
      t.hash = LiveClips::hashOf(t);
      const std::vector<QByteArray> p = LiveClips::packets(t, 7);
      QCOMPARE(int(p.size()), 1 + 3);                       // 48 + 48 + 4
      for (const QByteArray& x : p)
            QVERIFY2(x.size() < 1400, qPrintable(QString::number(x.size())));
      QVERIFY(LiveClips::parseOsc(p[0], &address, &args));
      QCOMPARE(address, QString("/ms/track"));
      QCOMPARE(args.size(), 11);
      QCOMPARE(args[0].toInt(), 7);
      QCOMPARE(args[1].toString(), QString("1:16"));
      QCOMPARE(args[2].toString(), QString("MuseScore B"));
      QCOMPARE(args[3].toInt(), 16);
      QCOMPARE(args[4].toString(), QString("Violins 1 é"));
      QCOMPARE(args[5].toString(), QString("MuseScore: Violins 1 é"));
      QCOMPARE(args[6].toInt(), 1);
      QCOMPARE(args[7].toInt(), 100000);
      QCOMPARE(args[8].toInt(), 100);
      QCOMPARE(args[9].toInt(), 3);
      QCOMPARE(qint32(args[10].toInt()), qint32(t.hash));
      std::vector<LiveClips::Note> back;
      for (size_t i = 1; i < p.size(); ++i) {
            QVERIFY(LiveClips::parseOsc(p[i], &address, &args));
            QCOMPARE(address, QString("/ms/notes"));
            QCOMPARE(args[0].toInt(), 7);
            QCOMPARE(args[1].toString(), QString("1:16"));
            QCOMPARE(args[2].toInt(), int(i) - 1);
            QCOMPARE((args.size() - 3) % 5, 0);
            for (int k = 3; k < args.size(); k += 5)
                  back.push_back({ args[k].toInt(), args[k + 1].toInt(), args[k + 2].toInt(), args[k + 3].toInt(), args[k + 4].toInt() != 0 });
            }
      QVERIFY(back == t.notes);
      QVERIFY(LiveClips::parseOsc(LiveClips::clearPacket(t, 8), &address, &args));
      QCOMPARE(address, QString("/ms/clear"));
      QCOMPARE(args[5].toString(), t.clip);
      QCOMPARE(LiveClips::clipName("Violins 1", "Violins 1 - Legato", false, 0), QString("MuseScore: Violins 1 – Violins 1 - Legato"));
      QCOMPARE(LiveClips::clipName("Violins 1", "Violins 1", true, 1), QString("MuseScore: Violins 1 (2)"));
      LiveClips::Track e = t;
      e.notes.clear();
      QCOMPARE(int(LiveClips::packets(e, 9).size()), 1);

      LiveClips::Song s;
      s.bpm = 72.5;
      s.length = 5000;
      for (int i = 0; i < 90; ++i)
            s.cues.push_back({ i * 100, QString("MS %1").arg(i + 1) });
      const std::vector<QByteArray> sp = LiveClips::packets(s, 3);
      QCOMPARE(int(sp.size()), 1 + 3);                      // 40 + 40 + 10
      QVERIFY(LiveClips::parseOsc(sp[0], &address, &args));
      QCOMPARE(address, QString("/ms/song"));
      QCOMPARE(args[1].toDouble(), 72.5);
      QCOMPARE(args[3].toInt(), 90);
      QCOMPARE(args[4].toInt(), 3);
      QVERIFY(LiveClips::parseOsc(sp[3], &address, &args));
      QCOMPARE(address, QString("/ms/cues"));
      QCOMPARE(args.size(), 2 + 2 * 10);
      QCOMPARE(args[2].toInt(), 8000);
      QCOMPARE(args[3].toString(), QString("MS 81"));
      // the carrier table: one key a controller, from the top
      QCOMPARE(LiveClips::carrierPitch(32), 127);
      QCOMPARE(LiveClips::carrierPitch(1), 126);
      QCOMPARE(LiveClips::carrierPitch(64), 124);
      QCOMPARE(LiveClips::carrierPitch(68), LiveClips::CARRIER_LOW);
      QCOMPARE(LiveClips::carrierPitch(7), -1);
      }

//---------------------------------------------------------
//   clipsImport
//    a Live Set where Live played the score as clips (a clip named "MuseScore: …"): its beats are
//    seconds at the set's tempo, mapped back through the score's tempo map
//---------------------------------------------------------

void TestLiveIntegration::clipsImport()
      {
      QFile x(QString(DIR_ROOT) + "liveset.xml");
      QVERIFY(x.open(QIODevice::ReadOnly));
      QByteArray xml = x.readAll();
      QVERIFY(xml.contains("<Name Value=\"\" />"));
      const LiveSet::Set plain = LiveSet::parse(xml);
      QVERIFY(!plain.museScoreClips);
      xml.replace("<Name Value=\"\" />", "<Name Value=\"MuseScore: Violin\" />");
      const LiveSet::Set set = LiveSet::parse(xml);
      QVERIFY(set.museScoreClips);
      QCOMPARE(set.tempo, 120.0);

      MasterScore* score = readScore(DIR + "clips.musicxml");            // 60 bpm, then 120 from bar 3
      QVERIFY(score);
      auto lib = loadMap(MAP);
      SoundLib::setCurrent(lib);
      score->rebuildMidiMapping();
      score->setExpandRepeats(true);
      const std::vector<LiveSet::PartInfo> parts = LiveSet::partInfos(score, { "MuseScore A" });
      LiveSet::Report report;
      const auto lanes = LiveSet::lanes(score, set, parts, "t.als", QDateTime::currentDateTime(), &report);
      QVERIFY(report.matched.join("\n").contains("clips"));
      const Automation::Lane& vib = lanes.at(score->parts()[0])[0];
      QCOMPARE(vib.target, QString("vibrato"));
      // beat 4 at 120 bpm = 2 s = beat 2 of bar 1 (60 bpm); beat 8 = 4 s = bar 2
      int at4 = -1, at8 = -1;
      for (const Automation::Point& p : vib.points) {
            if (p.value == 1.0 && at4 < 0)
                  at4 = p.tick;
            if (p.value == 0.5 && at8 < 0)
                  at8 = p.tick;
            }
      QCOMPARE(at4, 960);
      QCOMPARE(at8, 1920);
      // the same set read as a quarter-note timeline (no MuseScore clip): beat 8 = tick 3840
      const auto quarter = LiveSet::lanes(score, plain, parts, "t.als", QDateTime::currentDateTime(), &report);
      int q8 = -1;
      for (const Automation::Point& p : quarter.at(score->parts()[0])[0].points)
            if (p.value == 0.5 && q8 < 0)
                  q8 = p.tick;
      QCOMPARE(q8, 3840);
      SoundLib::setCurrent(nullptr);
      delete score;
      }

QTEST_MAIN(TestLiveIntegration)
#include "tst_liveintegration.moc"
