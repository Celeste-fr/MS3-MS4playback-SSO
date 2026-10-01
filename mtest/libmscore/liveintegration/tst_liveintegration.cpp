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
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <zlib.h>

#include "audio/midi/event.h"
#include "libmscore/automation.h"
#include "libmscore/instrument.h"
#include "libmscore/liveclips.h"
#include "libmscore/liveset.h"
#include "libmscore/livesetwriter.h"
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
#include "libmscore/measure.h"
#include "libmscore/rest.h"
#include "libmscore/tie.h"
#include "libmscore/staff.h"
#include "mscore/liveclipmodel.h"
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
      void clipsOnsetEarly();
      void clipsChanges();
      void clipsOsc();
      void clipsImport();
      void clipEditImport();
      void clipEditPitch();
      void clipEditVelocityAndMute();
      void clipEditDeleteAndAdd();
      void clipEditLengthAndMove();
      void clipEditTieChain();
      void clipEditChord();
      void clipEditUndoAndIds();
      void clipEditDrums();
      void clipEditInstrument();
      void clipEditOutside();
      void clipEditPackets();
      void liveSetWrite();
      void liveSetMissing();
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
      QVERIFY(vib.playedByLive());
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
      QVERIFY(read.at(back->parts()[0])[0].playedByLive());
      QVERIFY(!read.at(back->parts()[1])[0].playedByLive());       // the score's own
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
      QCOMPARE(dyn[0].velocity, 64);                          // (the value: carrierVelocity)
      QCOMPARE(dyn[0].length, b - 2 * E);                     // until the next value
      QCOMPARE(dyn[1].start, b - E);                          // after the keyswitch (switches first), before the note
      QCOMPARE(dyn[1].velocity, 50);                          // the last of 40, 50
      QCOMPARE(dyn[1].start + dyn[1].length, 4 * U);
      const std::vector<LiveClips::Note> pedal = carriers(n, 124);
      QCOMPARE(int(pedal.size()), 1);
      QCOMPARE(pedal[0].start, b + E);                        // after the note, as rendered
      QCOMPARE(pedal[0].velocity, 127);                       // 127 exact (UACC alone is value + 1)

      const std::vector<LiveClips::Note> notes = realNotes(n);
      QVERIFY2(notes.size() == 4, qPrintable(describe(n)));
      QCOMPARE(notes[0].pitch, 60);
      QCOMPARE(notes[0].start, 2 * E);                        // the note waits for its two controllers
      QCOMPARE(notes[0].start + notes[0].length, b);
      QCOMPARE(notes[0].velocity, 90);
      QCOMPARE(notes[1].pitch, 24);                           // the keyswitch, first before the note
      QCOMPARE(notes[1].start, b - 2 * E);
      QCOMPARE(notes[1].start + notes[1].length, 500 * U / 480);
      QCOMPARE(notes[2].pitch, 62);
      QCOMPARE(notes[2].start, b);
      QCOMPARE(notes[2].start + notes[2].length, 1000 * U / 480);    // first on, first off
      QCOMPARE(notes[3].pitch, 62);
      QCOMPARE(notes[3].start, 2 * U);
      QCOMPARE(notes[3].start + notes[3].length, 1200 * U / 480);
      QCOMPARE(rn.highNotes, 1);
      QCOMPARE(rn.dropped, 1);                                // CC7
      // the pitch bend (the centre, 8192): its two halves on 115 (upper, 64) and then 114 (lower 7 bits, 0 as 1);
      // no note at that tick: the last of them at the tick itself
      QCOMPARE(rn.bends, 1);
      const std::vector<LiveClips::Note> lsb = carriers(n, LiveClips::BEND_LSB);
      const std::vector<LiveClips::Note> msb = carriers(n, LiveClips::BEND_MSB);
      QCOMPARE(int(lsb.size()), 1);
      QCOMPARE(int(msb.size()), 1);
      QCOMPARE(lsb[0].velocity, 1);
      QCOMPARE(msb[0].velocity, 64);
      QCOMPARE(msb[0].start, 1200 * U / 480 - E);
      QCOMPARE(lsb[0].start, 1200 * U / 480);
      QCOMPARE(msb[0].start + msb[0].length, 4 * U);           // (until the next value: chased anywhere)
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
//   clipsOnsetEarly
//    held notes early by their measured onset (<Articulation onset>, <Onset early>; the sound library's
//    renderer) reach the clips as MuseScore plays them: 100 ms (a tenth of a beat at 60) early, but the
//    clip's first note and the first of a repeat's pass (where a pass starts, a note can't move earlier)
//---------------------------------------------------------

void TestLiveIntegration::clipsOnsetEarly()
      {
      MasterScore* score = readScore(DIR + "clips.musicxml");
      QVERIFY(score);
      auto lib = loadMap("<SoundLibrary name='t'><Switch type='cc' number='32'/><Dynamics cc='1'/><Onset early='100'/>"
                         "<Instrument name='Violin' ids='violin'><Articulation name='Long' value='1' techniques='long legato' onset='100'/></Instrument>"
                         "<Instrument name='Flute' ids='flute'><Articulation name='Long' value='1' techniques='long legato'/></Instrument>"
                         "</SoundLibrary>");
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      score->rebuildMidiMapping();
      score->setExpandRepeats(true);
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> tracks = LiveClips::tracks(score, *lib, renderClips(score), { "MuseScore A" }, tl);
      QCOMPARE(int(tracks.size()), 2);
      const std::vector<LiveClips::Note> notes = realNotes(tracks[0].notes);
      QVERIFY2(notes.size() == 6, qPrintable(describe(tracks[0].notes)));
      const int starts[] = { 0, 4, 8, 10, 14, 16 };          // seconds = beats at 60
      const bool early[] = { false, true, true, false, true, true };
      for (int i = 0; i < 6; ++i) {
            const int expected = starts[i] * U - (early[i] ? U / 10 : 0);
            QVERIFY2(qAbs(notes[size_t(i)].start - expected) <= 8 * LiveClips::EPSILON,
                     qPrintable(QString("note %1 at %2, expected %3: %4").arg(i).arg(notes[size_t(i)].start).arg(expected)
                                .arg(describe(notes))));
            }
      // (the flute has no onset: as written)
      const std::vector<LiveClips::Note> fn = realNotes(tracks[1].notes);
      QCOMPARE(int(fn.size()), 2);
      QCOMPARE(fn[1].start, 2 * U);
      const std::vector<LiveClips::Note> uacc = carriers(tracks[0].notes, 127);
      QVERIFY(!uacc.empty() && uacc.front().start <= notes.front().start - LiveClips::EPSILON);
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
      QCOMPARE(LiveClips::carrierPitch(68), 116);
      QCOMPARE(LiveClips::CARRIER_LOW, LiveClips::BEND_LSB);      // (below the controllers: the pitch bend's two)
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

//---------------------------------------------------------
//   Editing Live clips in MuseScore (mscore/liveclipmodel.h)
//---------------------------------------------------------

using namespace Ms::LiveClipEdit;

static LiveNote ln(int id, int pitch, double start, double duration, double velocity)
      {
      LiveNote n;
      n.id = id;
      n.pitch = pitch;
      n.start = start;
      n.duration = duration;
      n.velocity = velocity;
      n.probability = 0.75;               // (fields MuseScore never shows: kept by Live)
      n.velocityDeviation = 3.5;
      n.releaseVelocity = 40;
      return n;
      }

// a humanized melody: every note a little off the grid, velocities with fractions
static Clip melody(const QString& track = "Violin")
      {
      Clip c;
      c.key = "c7";
      c.track = track;
      c.name = "Idea";
      c.bpm = 96;
      c.num = 4;
      c.den = 4;
      c.end = 12;                         // (a third bar, empty)
      c.notes = { ln(101, 67, 0.013, 0.95, 87.3), ln(102, 69, 1.02, 0.97, 80.6), ln(103, 71, 1.991, 1.03, 91.2),
                  ln(104, 72, 3.004, 0.49, 70.0), ln(105, 74, 3.51, 0.48, 75.9), ln(106, 76, 4.0, 3.96, 99.4) };
      return c;
      }

static const LiveNote* liveOf(const Baseline& b, int id)
      {
      for (const Entry& e : b.entries)
            for (const LiveNote& n : e.live)
                  if (n.id == id)
                        return &n;
      return nullptr;
      }

static Note* noteAt(Score* score, int tick, int pitch)
      {
      std::vector<Note*> notes;
      const std::vector<Sig> sigs = signatures(score, &notes);
      for (size_t i = 0; i < sigs.size(); ++i)
            if (sigs[i].tick == tick && sigs[i].pitch == pitch)
                  return notes[i];
      return nullptr;
      }

void TestLiveIntegration::clipEditImport()
      {
      const Clip clip = melody();
      QString error;
      MasterScore* score = importClip(clip, &error);
      QVERIFY2(score, qPrintable(error));
      // Continuous View from the start, one part named after the track, a violin, the clip's time
      QCOMPARE(int(score->layoutMode()), int(LayoutMode::LINE));
      QCOMPARE(score->pages().size(), 1);                 // laid out as one line: one page, one system
      QCOMPARE(score->systems().size(), 1);
      QCOMPARE(score->parts().size(), 1);
      QCOMPARE(score->parts()[0]->partName(), QString("Violin"));
      QCOMPARE(score->parts()[0]->instrument()->getId(), QString("violin"));
      QCOMPARE(score->nstaves(), 1);
      QCOMPARE(score->fileInfo()->completeBaseName(), QString("Violin › Idea"));
      QCOMPARE(score->lastMeasure()->endTick().ticks(), 12 * 480);     // up to the clip's end
      QCOMPARE(score->firstMeasure()->timesig(), Fraction(4, 4));
      QCOMPARE(qRound(score->tempomap()->tempo(0) * 60), 96);
      // quantized in the notation, each note found with its Live note
      const std::vector<Sig> sigs = signatures(score);
      QCOMPARE(int(sigs.size()), 6);
      QCOMPARE(sigs[0].tick, 0);
      QCOMPARE(sigs[2].tick, 960);
      QCOMPARE(sigs[5].tick, 1920);
      QCOMPARE(sigs[5].ticks, 1920);
      const Baseline b = match(clip, score);
      QCOMPARE(b.unmatched, 0);
      QCOMPARE(b.outside, 0);
      for (const Entry& e : b.entries)
            QCOMPARE(int(e.live.size()), 1);
      QCOMPARE(liveOf(b, 103)->start, 1.991);
      // nothing edited: nothing to send
      QVERIFY(diff(b, signatures(score)).empty());
      delete score;
      }

void TestLiveIntegration::clipEditPitch()
      {
      const Clip clip = melody();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      Note* n = noteAt(score, 960, 71);
      QVERIFY(n);
      score->startCmd();
      score->undoChangePitch(n, 70, n->tpc1() - 7, n->tpc2() - 7);
      score->endCmd();
      const Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(int(d.ops[0].kind), int(Op::MODIFY));
      QCOMPARE(d.ops[0].id, 103);
      QCOMPARE(d.ops[0].mask, int(PITCH));
      QCOMPARE(d.ops[0].pitch, 70);
      // Live's copy: the new pitch, its own humanized start, length, velocity and the rest unchanged
      const LiveNote* x = liveOf(d.next, 103);
      QCOMPARE(x->pitch, 70);
      QCOMPARE(x->start, 1.991);
      QCOMPARE(x->duration, 1.03);
      QCOMPARE(x->velocity, 91.2);
      QCOMPARE(x->probability, 0.75);
      // the untouched notes keep Live's data exactly
      QCOMPARE(liveOf(d.next, 101)->start, 0.013);
      QCOMPARE(liveOf(d.next, 102)->velocity, 80.6);
      delete score;
      }

void TestLiveIntegration::clipEditVelocityAndMute()
      {
      const Clip clip = melody();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      Note* n = noteAt(score, 0, 67);
      Note* m = noteAt(score, 480, 69);
      QVERIFY(n && m);
      QCOMPARE(n->veloOffset(), 87);
      score->startCmd();
      n->undoChangeProperty(Pid::VELO_OFFSET, 110);
      m->undoChangeProperty(Pid::PLAY, false);
      score->endCmd();
      Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 2);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& a, const Op& c) { return a.id < c.id; });
      QCOMPARE(d.ops[0].id, 101);
      QCOMPARE(d.ops[0].mask, int(VELOCITY));
      QCOMPARE(d.ops[0].velocity, 110);
      QCOMPARE(d.ops[1].id, 102);
      QCOMPARE(d.ops[1].mask, int(MUTE));
      QVERIFY(d.ops[1].mute);
      QCOMPARE(liveOf(d.next, 101)->start, 0.013);
      delete score;
      }

void TestLiveIntegration::clipEditDeleteAndAdd()
      {
      const Clip clip = melody();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      Note* n = noteAt(score, 480, 69);
      QVERIFY(n);
      score->startCmd();
      score->select(n);
      score->cmdDeleteSelection();
      score->endCmd();
      Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(int(d.ops[0].kind), int(Op::REMOVE));
      QCOMPARE(d.ops[0].id, 102);
      // a note written where the deleted one was (its rest)
      Segment* seg = score->tick2segment(Fraction(480, 480 * 4), true, SegmentType::ChordRest);
      QVERIFY(seg && seg->element(0) && seg->element(0)->isRest());
      score->startCmd();
      score->setNoteRest(seg, 0, NoteVal(62), Fraction(1, 4));
      score->endCmd();
      d = diff(b, signatures(score));
      // pitch 69 -> 62 at the same place and length: one modification of the same Live note
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(int(d.ops[0].kind), int(Op::MODIFY));
      QCOMPARE(d.ops[0].id, 102);
      QCOMPARE(d.ops[0].mask & PITCH, int(PITCH));
      delete score;

      // a new note in the empty last bar: an addition, velocity 100 (none set)

      MasterScore* s = importClip(clip, nullptr);
      const Baseline b2 = match(clip, s);
      Measure* last = s->lastMeasure();
      Segment* rs = last->first(SegmentType::ChordRest);
      QVERIFY(rs->element(0)->isRest());
      s->startCmd();
      s->setNoteRest(rs, 0, NoteVal(60), Fraction(1, 4));
      s->endCmd();
      d = diff(b2, signatures(s));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(int(d.ops[0].kind), int(Op::ADD));
      QCOMPARE(d.ops[0].pitch, 60);
      QCOMPARE(d.ops[0].start, last->tick().ticks());
      QCOMPARE(d.ops[0].duration, 480);
      QCOMPARE(d.ops[0].velocity, 100);
      QCOMPARE(int(d.added.size()), 1);
      delete s;
      }

void TestLiveIntegration::clipEditLengthAndMove()
      {
      const Clip clip = melody();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      // shorter: the eighth C (3.004) half as long
      Note* n = noteAt(score, 1440, 72);
      QVERIFY(n);
      score->startCmd();
      score->select(n);
      score->changeCRlen(n->chord(), TDuration(TDuration::DurationType::V_16TH));
      score->endCmd();
      const Diff d = diff(b, signatures(score));
      QVERIFY(!d.empty());
      bool found = false;
      for (const Op& o : d.ops) {
            if (o.id == 104) {
                  QCOMPARE(int(o.kind), int(Op::MODIFY));
                  QCOMPARE(o.mask, int(DURATION));
                  QCOMPARE(o.duration, 120);
                  found = true;
                  }
            else                    // (the others untouched)
                  QVERIFY2(false, qPrintable(QString("op on %1").arg(o.id)));
            }
      QVERIFY(found);
      QCOMPARE(liveOf(d.next, 104)->start, 3.004);
      delete score;
      }

void TestLiveIntegration::clipEditTieChain()
      {
      // a note over the bar line (2.5 … 5.5): one tie chain, one Live note
      Clip clip;
      clip.key = "c9";
      clip.track = "Flute";
      clip.name = "Tied";
      clip.end = 8;
      clip.notes = { ln(1, 72, 0, 2.5, 90), ln(2, 74, 2.5, 3.0, 90) };
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const std::vector<Sig> sigs = signatures(score);
      QCOMPARE(int(sigs.size()), 2);
      QCOMPARE(sigs[1].tick, 1200);
      QVERIFY(sigs[1].ticks >= 1440);                     // (the import may round its end to the beat)
      QVERIFY(noteAt(score, 1200, 74)->tieFor());        // over the bar line: a tie chain
      const Baseline b = match(clip, score);
      QCOMPARE(int(b.entries[1].live.size()), 1);
      QCOMPARE(b.entries[1].live[0].id, 2);
      // a pitch change of the tie chain (its first note; the tie follows): one modification
      Note* n = noteAt(score, 1200, 74);
      score->startCmd();
      score->undoChangePitch(n, 76, n->tpc1() + 2, n->tpc2() + 2);
      if (n->tieFor()) {
            Note* e = n->tieFor()->endNote();
            score->undoChangePitch(e, 76, e->tpc1() + 2, e->tpc2() + 2);
            }
      score->endCmd();
      const Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(d.ops[0].id, 2);
      QCOMPARE(d.ops[0].mask, int(PITCH));
      delete score;
      }

void TestLiveIntegration::clipEditChord()
      {
      Clip clip;
      clip.key = "c3";
      clip.track = "Organ Pad";           // (no such instrument: a piano)
      clip.name = "Chords";
      clip.end = 4;
      clip.notes = { ln(11, 60, 0.01, 1.98, 70), ln(12, 64, 0.0, 2.02, 71), ln(13, 67, 0.02, 1.99, 72),
                     ln(14, 62, 2.0, 2.0, 60), ln(15, 65, 2.0, 2.0, 60) };
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      QCOMPARE(score->parts()[0]->instrument()->getId(), QString("piano"));
      QCOMPARE(score->nstaves(), 1);          // the range fits one clef
      const Baseline b = match(clip, score);
      QCOMPARE(b.unmatched, 0);
      QCOMPARE(int(b.entries.size()), 5);
      // the chord's middle note up: only its own Live note
      Note* n = noteAt(score, 0, 64);
      QVERIFY(n);
      score->startCmd();
      score->undoChangePitch(n, 63, n->tpc1() - 7, n->tpc2() - 7);
      score->endCmd();
      const Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(d.ops[0].id, 12);
      QCOMPARE(liveOf(d.next, 12)->start, 0.0);
      QCOMPARE(liveOf(d.next, 12)->duration, 2.02);
      QCOMPARE(liveOf(d.next, 11)->start, 0.01);
      delete score;
      }

void TestLiveIntegration::clipEditUndoAndIds()
      {
      const Clip clip = melody();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      Baseline b = match(clip, score);
      Measure* last = score->lastMeasure();
      Segment* rs = last->first(SegmentType::ChordRest);
      score->startCmd();
      score->setNoteRest(rs, 0, NoteVal(60), Fraction(1, 4));
      score->endCmd();
      Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.added.size()), 1);
      QVERIFY(!setAddedIds(d, { 1, 2 }));
      QVERIFY(setAddedIds(d, { 555 }));
      b = d.next;                                     // (Live confirmed)
      QVERIFY(liveOf(b, 555));
      QVERIFY(diff(b, signatures(score)).empty());    // in sync
      // undo: the added note goes, by its id
      score->undoRedo(true, nullptr);
      d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(int(d.ops[0].kind), int(Op::REMOVE));
      QCOMPARE(d.ops[0].id, 555);
      b = d.next;
      // a pitch edit, then undone: back to Live's pitch, the note's timing untouched all along
      Note* n = noteAt(score, 480, 69);
      score->startCmd();
      score->undoChangePitch(n, 68, n->tpc1() - 7, n->tpc2() - 7);
      score->endCmd();
      d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      b = d.next;
      score->undoRedo(true, nullptr);
      d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(d.ops[0].id, 102);
      QCOMPARE(d.ops[0].pitch, 69);
      QCOMPARE(d.ops[0].mask, int(PITCH));
      QCOMPARE(liveOf(d.next, 102)->start, 1.02);
      delete score;
      }

void TestLiveIntegration::clipEditDrums()
      {
      Clip clip;
      clip.key = "c5";
      clip.track = "808 Kit";
      clip.name = "Beat";
      clip.drums = true;
      clip.end = 4;
      for (int i = 0; i < 4; ++i) {
            clip.notes.push_back(ln(200 + i, 36, i + 0.004 * i, 0.25, 110 - i));       // kick on the beats
            clip.notes.push_back(ln(210 + i, 42, i + 0.5, 0.25, 64));                  // closed hat off the beats
            }
      clip.notes.push_back(ln(220, 38, 1.0, 0.25, 100));                                // snare on 2
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      QVERIFY(score->staff(0)->isDrumStaff(Fraction(0, 1)));
      QVERIFY(score->parts()[0]->instrument()->drumset());
      const Baseline b = match(clip, score);
      QCOMPARE(b.unmatched, 0);
      QCOMPARE(int(b.entries.size()), 9);
      QVERIFY(diff(b, signatures(score)).empty());
      // the snare up to a hand clap (39)
      Note* n = noteAt(score, 480, 38);
      QVERIFY(n);
      score->startCmd();
      score->undoChangePitch(n, 39, n->tpc1(), n->tpc2());
      score->endCmd();
      const Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(d.ops[0].id, 220);
      QCOMPARE(d.ops[0].pitch, 39);
      delete score;
      }

void TestLiveIntegration::clipEditInstrument()
      {
      QCOMPARE(instrumentForTrack("Violin"), QString("violin"));
      QCOMPARE(instrumentForTrack("2-Violin"), QString("violin"));
      QCOMPARE(instrumentForTrack("flute"), QString("flute"));
      QCOMPARE(instrumentForTrack("Cello 2"), QString("violoncello"));
      QCOMPARE(instrumentForTrack("1-MIDI"), QString());
      QCOMPARE(instrumentForTrack("Lead Synth Thing"), QString());
      // a piano over a wide range: a grand staff
      Clip clip;
      clip.key = "c4";
      clip.track = "1-MIDI";
      clip.name = "Wide";
      clip.end = 4;
      clip.notes = { ln(1, 36, 0, 1, 80), ln(2, 84, 0, 1, 80), ln(3, 43, 1, 1, 80), ln(4, 79, 1, 1, 80) };
      QVERIFY(needsGrandStaff(clip));
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      QCOMPARE(score->parts()[0]->instrument()->getId(), QString("piano"));
      QCOMPARE(score->nstaves(), 2);
      const Baseline b = match(clip, score);
      QCOMPARE(b.unmatched, 0);
      QVERIFY(diff(b, signatures(score)).empty());
      delete score;
      }

void TestLiveIntegration::clipEditOutside()
      {
      // notes before the clip's start or after its end: not shown, never touched
      Clip clip = melody();
      clip.notes.push_back(ln(900, 60, -1.0, 0.5, 80));
      clip.notes.push_back(ln(901, 60, 13.0, 0.5, 80));
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      QCOMPARE(b.outside, 2);
      QVERIFY(!liveOf(b, 900) && !liveOf(b, 901));
      // everything deleted in the notation: only the six notes it shows are removed
      score->startCmd();
      score->cmdSelectAll();
      score->cmdDeleteSelection();
      score->endCmd();
      const Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 6);
      for (const Op& o : d.ops) {
            QCOMPARE(int(o.kind), int(Op::REMOVE));
            QVERIFY(o.id != 900 && o.id != 901);
            }
      // the MIDI file leaves them out too
      const QByteArray mid = midiFile(clip);
      QVERIFY(mid.startsWith("MThd"));
      delete score;
      }

void TestLiveIntegration::clipEditPackets()
      {
      std::vector<Op> ops;
      for (int i = 0; i < 40; ++i) {
            Op o;
            o.kind = i % 3 == 0 ? Op::REMOVE : Op::MODIFY;
            o.id = 1000 + i;
            o.mask = PITCH;
            o.pitch = 60 + i % 12;
            ops.push_back(o);
            }
      const std::vector<QByteArray> p = writePackets("c7", 3, ops);
      QCOMPARE(int(p.size()), 3);
      QString address;
      QVariantList args;
      QVERIFY(LiveClips::parseOsc(p[0], &address, &args));
      QCOMPARE(address, QString("/ms/clip/write"));
      QCOMPARE(args, QVariantList({ "c7", 3, 40, 2 }));
      QVERIFY(LiveClips::parseOsc(p[2], &address, &args));
      QCOMPARE(address, QString("/ms/clip/ops"));
      QCOMPARE(args.size(), 3 + 8 * 8);
      QCOMPARE(args[3 + 1].toInt(), 1032);
      }

//---------------------------------------------------------
//   liveSetWrite
//    Create Live Set (livesetwriter.h): the score's routes as tracks (names, MIDI From, colours), the
//    song's tempo and time signature, the devices in order with the state bytes exactly, Live's
//    bookkeeping (pointee ids, clip slots per scene), gzip, and the reader reads it back
//---------------------------------------------------------

namespace {
struct Written {
      QStringList tracks;
      std::vector<QStringList> devices;                   // the Devices' children, per track
      std::vector<QByteArray> processor, controller;      // per PluginDevice
      std::vector<QByteArray> blobs;
      std::map<QString, QString> values;                  // the first Value of some elements, by path end
      QStringList fileRef;                                // the MxPatchRef's FileRef values
      QStringList uid;
      QString inputTarget;
      };
}

static Written readWritten(const QByteArray& xml)
      {
      Written w;
      QXmlStreamReader r(xml);
      QStringList path;
      QByteArray text;
      while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                  const QString tag = r.name().toString();
                  const QString parent = path.isEmpty() ? QString() : path.back();
                  const QString value = r.attributes().value("Value").toString();
                  if (parent == "Devices")
                        w.devices.back() << tag;
                  if (tag == "Devices" && path.contains("MidiTrack"))
                        w.devices.push_back(QStringList());
                  if (tag == "EffectiveName" && path.size() >= 2 && path[path.size() - 2] == "MidiTrack")
                        w.tracks << value;
                  if (parent == "FileRef" && path.contains("MxPatchRef"))
                        w.fileRef << tag + "=" + value;
                  if (parent == "Uid" && path.contains("Vst3Preset"))
                        w.uid << value;
                  if (tag == "Target" && parent == "MidiInputRouting" && path.contains("MidiTrack") && w.inputTarget.isEmpty())
                        w.inputTarget = value;
                  for (const char* k : { "Tempo/Manual", "TimeSignature/Manual", "Scene/Tempo", "Scene/TimeSignatureId" })
                        if (QString(k) == parent + "/" + tag && !w.values.count(k))
                              w.values[k] = value;
                  if (tag == "EnumEvent" || tag == "FloatEvent")
                        w.values[tag] = r.attributes().value("Value").toString() + "@" + r.attributes().value("Time").toString();
                  text.clear();
                  path << tag;
                  }
            else if (r.isCharacters())
                  text += r.text().toUtf8();
            else if (r.isEndElement()) {
                  const QString tag = path.takeLast();
                  const QByteArray bytes = QByteArray::fromHex(text.simplified().replace(' ', ""));
                  if (tag == "ProcessorState")
                        w.processor.push_back(bytes);
                  else if (tag == "ControllerState")
                        w.controller.push_back(bytes);
                  else if (tag == "Blob")
                        w.blobs.push_back(bytes);
                  text.clear();
                  }
            }
      return w;
      }

void TestLiveIntegration::liveSetWrite()
      {
      using namespace LiveSetWriter;
      MasterScore* score = readScore(DIR + "clips.musicxml");
      QVERIFY(score);
      auto lib = loadMap(MAP);
      QVERIFY(lib);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      score->rebuildMidiMapping();

      Spec spec;
      setSong(score, &spec);
      QCOMPARE(spec.tempo, 60.0);                        // the first tempo (bar 3 has 120)
      QCOMPARE(spec.numerator, 4);
      QCOMPARE(spec.denominator, 4);
      spec.tracks = tracks(score, *lib, { "MuseScore A" });
      QCOMPARE(int(spec.tracks.size()), 2);
      QCOMPARE(spec.tracks[0].name, QString("Violin"));
      QCOMPARE(spec.tracks[1].name, QString("Flute"));
      QCOMPARE(spec.tracks[0].routeKey, QString("0:1"));
      QCOMPARE(spec.tracks[1].channel, 2);
      QCOMPARE(spec.tracks[1].portName, QString("MuseScore A"));
      QVERIFY(spec.tracks[0].mainPatch && spec.tracks[0].instrument && spec.tracks[0].instrument->name == "Violin");
      QVERIFY(spec.tracks[0].color != spec.tracks[1].color);

      // the states: every byte value, and a controller state on one
      QByteArray a, b;
      for (int i = 0; i < 5000; ++i)
            a += char((i * 7) & 0xff);
      for (int i = 0; i < 81; ++i)                       // (not a whole line of 40)
            b += char(255 - i);
      const quint32 kontakt[4] = { 0x5653544E, 0x694B386B, 0x6F6E7461, 0x6B742038 };
      for (Track& t : spec.tracks) {
            t.hasPlugin = true;
            t.plugin.name = "Kontakt 8";
            std::copy(kontakt, kontakt + 4, t.plugin.uid);
            t.plugin.audioOutputs = 16;
            }
      spec.tracks[0].plugin.component = a;
      spec.tracks[1].plugin.component = b;
      spec.tracks[1].plugin.controller = "controller";
      spec.link.path = "C:/Users/me/Documents/Ableton/User Library/Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd";
      spec.link.userLibraryPath = "Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd";
      spec.link.size = 65572;
      spec.link.crc = 4321;
      spec.link.modified = 1790000000;
      spec.link.port = 9005;

      int next = 0;
      const QByteArray x = xml(spec, &next);
      QCOMPARE(validate(x), QString());
      QVERIFY(next > 800);                               // (two tracks' targets, 128 plug-in slots each …)
      QVERIFY(x.startsWith("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n<Ableton MajorVersion=\"5\" MinorVersion=\"12.0_12203\""));
      QVERIFY(x.contains("\r\n\t\t<NextPointeeId Value=\"" + QByteArray::number(next) + "\" />\r\n"));
      QVERIFY(x.endsWith("</Ableton>\r\n"));

      const Written w = readWritten(x);
      QCOMPARE(w.tracks, QStringList({ "Violin", "Flute" }));
      QCOMPARE(int(w.devices.size()), 2);                // (the MIDI tracks.)
      QCOMPARE(w.devices[0], QStringList({ "MxDeviceMidiEffect", "PluginDevice" }));   // the device before Kontakt
      QCOMPARE(w.devices[1], QStringList({ "MxDeviceMidiEffect", "PluginDevice" }));
      QCOMPARE(int(w.processor.size()), 2);
      QCOMPARE(w.processor[0], a);                       // the states, byte for byte
      QCOMPARE(w.processor[1], b);
      QCOMPARE(w.controller[0], QByteArray());
      QCOMPARE(w.controller[1], QByteArray("controller"));
      QCOMPARE(w.blobs[0], QByteArray("{\r\n\t\"Port\" : [ 9005 ]\r\n}\r\n") + QByteArray(1, '\0'));
      QCOMPARE(w.uid.mid(0, 4), QStringList({ "1448301646", "1766537323", "1869509729", "1802772536" }));
      QCOMPARE(w.fileRef, QStringList({ "RelativePathType=6", "RelativePath=" + spec.link.userLibraryPath, "Path=" + spec.link.path,
                                        "Type=1", "LivePackName=", "LivePackId=", "OriginalFileSize=65572", "OriginalCrc=4321",
                                        "RelativePathType=6", "RelativePath=" + spec.link.userLibraryPath, "Path=" + spec.link.path,
                                        "Type=1", "LivePackName=", "LivePackId=", "OriginalFileSize=65572", "OriginalCrc=4321" }));
      QCOMPARE(w.inputTarget, QString("MidiIn/External.Dev:MuseScore A/0"));
      QCOMPARE(w.values.at("Tempo/Manual"), QString("60"));
      QCOMPARE(w.values.at("TimeSignature/Manual"), QString("201"));
      QCOMPARE(w.values.at("EnumEvent"), QString("201@-63072000"));
      QCOMPARE(w.values.at("FloatEvent"), QString("60@-63072000"));
      QCOMPARE(w.values.at("Scene/TimeSignatureId"), QString("201"));

      // written gzipped, read back by the automation import's reader
      QString error;
      QCOMPARE(LiveSet::gunzip(LiveSetWriter::gzip(x), &error), x);
      QTemporaryDir dir;
      const QString path = dir.path() + "/Score.als";
      QVERIFY2(write(path, spec, &error), qPrintable(error));
      // (MS_LIVESET_OUT: a copy to compare with a set Live saved: tools/live/test/compare_als_skeleton.py)
      if (!qEnvironmentVariable("MS_LIVESET_OUT").isEmpty())
            QVERIFY(write(qEnvironmentVariable("MS_LIVESET_OUT"), spec, &error));
      QFile f(path);
      QVERIFY(f.open(QIODevice::ReadOnly));
      QVERIFY(f.read(2) == QByteArray("\x1f\x8b"));
      const LiveSet::Set set = LiveSet::read(path);
      QVERIFY2(set.error.isEmpty(), qPrintable(set.error));
      QCOMPARE(set.creator, QString("Ableton Live 12.2"));
      QCOMPARE(set.tempo, 60.0);
      QCOMPARE(int(set.tracks.size()), 2);
      QCOMPARE(set.tracks[0].name, QString("Violin"));
      QCOMPARE(set.tracks[0].inputDevice, QString("MuseScore A"));
      QCOMPARE(set.tracks[0].inputChannel, 1);
      QCOMPARE(set.tracks[1].inputChannel, 2);
      QCOMPARE(set.tracks[1].devices, QStringList({ "Kontakt 8" }));
      QVERIFY(set.tracks[0].envelopes.empty());
      QVERIFY(!set.museScoreClips);
      // the automation import would match each track to its part by MIDI From (its rule: port and channel)
      const std::vector<LiveSet::PartInfo> parts = LiveSet::partInfos(score, { "MuseScore A" });
      QCOMPARE(int(parts.size()), 2);
      for (size_t i = 0; i < 2; ++i) {
            QCOMPARE(LiveSet::portDisplayName(parts[i].portName), set.tracks[i].inputDevice);
            QCOMPARE(parts[i].channel, set.tracks[i].inputChannel);
            }

      // no MIDI output set: All Ins; no plug-in, no device: an empty device chain
      spec.tracks[1].portName.clear();
      spec.tracks[1].hasPlugin = false;
      spec.tracks[1].link = false;
      const QByteArray y = xml(spec);
      QCOMPARE(validate(y), QString());
      const LiveSet::Set set2 = LiveSet::parse(y);
      QCOMPARE(set2.tracks[1].inputDevice, QString());
      QCOMPARE(set2.tracks[1].inputChannel, -1);
      QVERIFY(set2.tracks[1].devices.isEmpty());
      QCOMPARE(readWritten(y).devices[1], QStringList());
      // no device found: no device on any track
      spec.link = LinkDevice();
      QCOMPARE(readWritten(xml(spec)).devices[0], QStringList({ "PluginDevice" }));

      // the checks catch what Live would refuse
      QByteArray bad = x;
      bad.replace("<NextPointeeId Value=\"" + QByteArray::number(next) + "\"", "<NextPointeeId Value=\"5\"");
      QVERIFY(validate(bad).contains("NextPointeeId"));
      bad = x;
      const int at = bad.indexOf("<Pointee Id=\"");
      const int at2 = bad.indexOf("<Pointee Id=\"", at + 1);
      bad.replace(at2, bad.indexOf('"', at2 + 13) - at2, bad.mid(at, bad.indexOf('"', at + 13) - at));
      QVERIFY(validate(bad).contains("twice"));
      bad = x;
      bad.replace("<ClipSlot Id=\"7\">", "<Clip Id=\"7\">");
      QVERIFY(!validate(bad).isEmpty());
      QVERIFY(!validate(x.left(x.size() / 2)).isEmpty());

      // Live's numbers
      QCOMPARE(timeSignatureId(4, 4), 201);
      QCOMPARE(timeSignatureId(3, 4), 200);
      QCOMPARE(timeSignatureId(6, 8), 302);
      QCOMPARE(timeSignatureId(7, 16), 402);
      QCOMPARE(timeSignatureId(99, 16), 494);            // Live's range (TimeSignature's 0-494)
      QCOMPARE(timeSignatureId(5, 3), -1);
      QCOMPARE(timeSignatureId(100, 4), -1);
      QCOMPARE(int(fileCrc("123456789")), 0xFEE8);       // CRC-16/UMTS's check value
      QByteArray big(20000, 'x');
      QCOMPARE(fileCrc(big), fileCrc(big.left(16384)));  // (the first 16 KiB)
      QVERIFY(fileCrc(big) != fileCrc(big.left(16383)));
      QCOMPARE(linkBlob(9001).toHex().toUpper(), QByteArray("7B0D0A0922506F727422203A205B2039303031205D0D0A7D0D0A00"));
      // the track's lanes in the device's stores (MuseScoreLink.js decodes the same: test_params.js "the blob as
      // Create Live Set writes it")
      {
      Track t;
      t.routeKey = "0:1";
      t.linkHash = 4243;
      t.linkLength = 8;
      t.linkLanes.push_back({ "Vib \"x\"", 1, { { 0, 0.5f }, { 3840, 1.0f } } });
      bool kept = false;
      const QByteArray blob = linkBlob(9001, &t, &kept);
      QVERIFY(kept);
      const QRegularExpression re("^\\{\r\n\t\"Port\" : \\[ 9001 \\],\r\n\t\"Lanes\" : \\[ \"msl-lanes\", 2, \\d+, 0, 1, 8, 1, "
                                  "\"0:1\", 4243, 1, \"Vib \\\\\"x\\\\\"\", 1, 4, 0, 0.5, 3840, 1 \\]\r\n\\}\r\n$");
      QVERIFY(blob.endsWith(QByteArray("}\r\n", 3) + QByteArray(1, '\0')));
      QVERIFY2(re.match(QString::fromLatin1(blob.chopped(1))).hasMatch(), blob.constData());
      // a straight ramp's steps: one run (MuseScoreLink.js packLane, test_params.js the same atoms)
      {
      std::vector<std::pair<int, float>> ramp;
      for (int i = 0; i <= 32; ++i)
            ramp.push_back({ i * 240, float(0.9 - 0.7 * i / 32) });
      ramp.push_back({ 7680 + 3840, 0.5f });
      const std::vector<double> p = packLane(ramp);
      QCOMPARE(int(p.size()), 8);
      QCOMPARE(p[0], -33.0);
      QCOMPARE(p[1], 0.0);
      QCOMPARE(p[3], 7680.0);
      QCOMPARE(float(p[4]), ramp[32].second);
      QCOMPARE(p[6], 11520.0);
      }
      // too many points for the 4 stores (values nothing packs): no lanes kept, the port as before
      quint32 seed = 1;
      auto rnd = [&seed]() { seed = quint32((quint64(seed) * 16807) % 2147483647); return float(seed % 1000) / 1000; };
      for (int i = 0; i < 70000; ++i)
            t.linkLanes[0].events.push_back({ 7680 + 7 * i + int(seed % 3), rnd() });
      const QByteArray huge = linkBlob(9001, &t, &kept);
      QCOMPARE(huge, linkBlob(9001));
      QVERIFY(!kept);
      // over two stores: "Lanes" and "Lanes2", each at most LINK_STORE_ATOMS atoms
      t.linkLanes[0].events.resize(20000);
      const QByteArray two = linkBlob(9001, &t, &kept);
      QVERIFY(kept && two.contains("\"Lanes2\" : [ \"msl-lanes\", 2, ") && !two.contains("\"Lanes3\""));
      }
      // track names = the clips' without "MuseScore: " (the device's rule)
      for (bool main : { true, false })
            for (int lane : { 0, 1, 2 }) {
                  QCOMPARE(trackName("Violin", "Solo Violin - Performance", main, lane),
                           LiveClips::clipName("Violin", "Solo Violin - Performance", main, lane).mid(11));
                  }
      QCOMPARE(trackName("Violin", "Solo Violin - Performance", false, 0), QString("Violin – Solo Violin - Performance"));
      QCOMPARE(trackName("Violin", "Violin", true, 1), QString("Violin (2)"));
      delete score;
      }

//---------------------------------------------------------
//   liveSetMissing
//    Add missing tracks: a route has a track in a set when the device would find one: MIDI From = its
//    port and channel, else by name (the part's for its main patch; "<part> – <patch>", else the patch's
//    alone on one track only), names compared loosely
//---------------------------------------------------------

void TestLiveIntegration::liveSetMissing()
      {
      using namespace LiveSetWriter;
      auto setOf = [](const std::vector<std::pair<QString, QString>>& named) {   // name, MIDI From port ("": All Ins)
            Spec s;
            int ch = 0;
            for (const auto& n : named) {
                  Track t;
                  t.name = n.first;
                  t.portName = n.second;
                  t.channel = ++ch;
                  t.link = false;
                  s.tracks.push_back(t);
                  }
            return LiveSet::parse(xml(s));
            };
      auto route = [](const QString& part, const QString& patch, bool main, int channel) {
            Track t;
            t.part = part;
            t.patch = patch;
            t.mainPatch = main;
            t.name = trackName(part, patch, main, 0);
            t.portName = "MuseScore A";
            t.channel = channel;
            return t;
            };
      const Track violin = route("Violin", "Violin", true, 1);
      const Track legato = route("Violin", "Solo Violin - Performance", false, 2);
      const Track flute = route("Flute", "Flute", true, 3);

      LiveSet::Set set = setOf({ { "violin", "" }, { "Something else", "" } });
      QVERIFY(hasTrack(set, violin));                     // by name, loosely
      QVERIFY(!hasTrack(set, legato));
      QVERIFY(!hasTrack(set, flute));
      set = setOf({ { "Violin – Solo Violin - Performance", "" } });
      QVERIFY(hasTrack(set, legato));
      QVERIFY(!hasTrack(set, violin));
      set = setOf({ { "Violin - Solo Violin - Performance", "" } });   // any dash
      QVERIFY(hasTrack(set, legato));
      set = setOf({ { "Solo Violin - Performance", "" } });            // the patch alone, on one track
      QVERIFY(hasTrack(set, legato));
      set = setOf({ { "Solo Violin - Performance", "" }, { "Solo Violin - Performance", "" } });
      QVERIFY(!hasTrack(set, legato));
      // by MIDI From: the third track listens to MuseScore A, channel 3 (setOf numbers them 1, 2, 3)
      set = setOf({ { "a", "" }, { "b", "" }, { "Winds", "MuseScore A" } });
      QVERIFY(hasTrack(set, flute));
      QVERIFY(!hasTrack(set, violin));
      set = setOf({ { "a", "" }, { "b", "" }, { "Winds", "MuseScore B" } });
      QVERIFY(!hasTrack(set, flute));
      }

QTEST_MAIN(TestLiveIntegration)
#include "tst_liveintegration.moc"
