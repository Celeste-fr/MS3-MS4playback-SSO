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
#include "libmscore/measurenumber.h"
#include "libmscore/rest.h"
#include "libmscore/tie.h"
#include "libmscore/staff.h"
#include "mscore/cliptempo.h"
#include "libmscore/system.h"
#include "libmscore/page.h"
#include <QPainter>
#include <QImage>
#include "libmscore/clef.h"
#include "mscore/liveclipedit.h"
#include "libmscore/tempotext.h"
#include "libmscore/systemtext.h"
#include "mscore/liveclips.h"
#include "mscore/liveclipmodel.h"
#include "mscore/livehelpers.h"
#include "audio/midi/event.h"
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
      void clipEditDottedLengths();
      void clipEditHumanizedLengths();
      void clipEditAddTie();
      void clipEditRemoveTie();
      void clipEditChord();
      void clipEditUndoAndIds();
      void clipEditDrums();
      void clipEditInstrument();
      void clipEditBands();
      void clipEditOutside();
      void clipEditPackets();
      void clipTitleUnnamed();
      void clipTabClean();
      void clipTabAudible();
      void clipVelocityLane();
      void clipVelocityWrite();
      void clipVelocityReopen();
      void clipVelocityRecord();
      void clipEnvelopeMapping();
      void laneTimeAxis();
      void liveParamLanes();
      void clipTabMidi();
      void linkWatch();
      void midiInputSilent();
      void liveSetWrite();
      void liveSetMissing();
      void liveHelpersLibrary();
      void liveHelpersInstall();
      void clipTempoSetRead();
      void clipTempoMapping();
      void clipTempoScore();
      void clipTempoFollowLive();
      void clipTempoArrangement();
      void clipTempoLiveLists();
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
      // 0, 4, the curve's pieces (within one MIDI step of it: LiveSet::curve) to 8, 26, 34
      const size_t n = vib.points.size();
      QVERIFY2(n >= 2 + 2 + 2 && n < 2 + 64 + 2, qPrintable(QString::number(n)));
      QCOMPARE(vib.points[1].beat, 4.0);
      QCOMPARE(vib.points[n - 3].beat, 8.0);
      QCOMPARE(vib.points[n - 3].value, 0.5);
      QCOMPARE(vib.points[n - 2].beat, 26.0);
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
//    a curved segment as straight pieces: from a to b, inside their box, each within one MIDI step of the curve
//    (Automation::flattenCurve: as few as that allows); the diagonal is one line
//---------------------------------------------------------

void TestLiveIntegration::liveSetCurve()
      {
      const LiveSet::Point a { 4, 1.0 }, b { 8, 0.5 };
      const double tol = Automation::CC_RESOLUTION;
      const std::vector<LiveSet::Point> c = LiveSet::curve(a, b, 0.2, 0.8, 0.5, 1.0, tol);
      QVERIFY2(c.size() > 2 && c.size() < 64, qPrintable(QString::number(c.size())));
      QCOMPARE(c.back().beat, 8.0);
      QCOMPARE(c.back().value, 0.5);
      double last = a.beat;
      for (const LiveSet::Point& p : c) {
            QVERIFY(p.beat >= last && p.beat <= 8.0);
            QVERIFY(p.value >= 0.5 - 1e-9 && p.value <= 1.0 + 1e-9);
            last = p.beat;
            }
      // within one MIDI step of the curve at every 1/480 beat, and bowed towards the end value early (1Y 0.8 at 1X
      // 0.2): half way the value is past half way
      LiveSet::Point from = a;
      for (const LiveSet::Point& p : c) {
            for (double x = from.beat; x <= p.beat; x += 1.0 / 480) {
                  const double line = from.value + (p.value - from.value) * (p.beat > from.beat ? (x - from.beat) / (p.beat - from.beat) : 0);
                  const double curve = 1.0 - 0.5 * Automation::curveAt(0.2, 0.8, 0.5, 1.0, (x - 4) / 4);
                  QVERIFY2(std::fabs(line - curve) <= tol + 1e-9, qPrintable(QString("beat %1: %2 against %3").arg(x).arg(line).arg(curve)));
                  if (std::fabs(x - 6) < 1e-9)
                        QVERIFY2(line < 0.75, qPrintable(QString::number(line)));
                  }
            from = p;
            }
      const std::vector<LiveSet::Point> line = LiveSet::curve(a, b, 1.0 / 3, 1.0 / 3, 2.0 / 3, 2.0 / 3, tol);
      QCOMPARE(int(line.size()), 1);
      QCOMPARE(line[0].beat, 8.0);
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

// a picture of the score's one page as MuseScore draws it (Score::print), for a person to look at
static bool renderPng(MasterScore* score, const QString& path)
      {
      score->doLayout();
      Page* page = score->pages().front();
      QRectF r;
      for (const Element* e : page->items(page->abbox()))
            if (e->visible())
                  r |= e->pageBoundingRect();
      r.adjust(-20, -20, 20, 20);
      const qreal scale = 0.5;
      QImage img(int(r.width() * scale) + 1, int(r.height() * scale) + 1, QImage::Format_ARGB32);
      img.fill(Qt::white);
      QPainter p(&img);
      p.setRenderHint(QPainter::Antialiasing);
      p.scale(scale, scale);
      p.translate(-r.topLeft());
      score->print(&p, 0);
      p.end();
      return img.save(path);
      }

// the band staves shown in the clip tab (0 treble 15ma, 1 treble, 2 bass, 3 bass 15mb)
static std::vector<int> shownStaves(Score* score)
      {
      score->doLayout();
      std::vector<int> out;
      System* sys = score->systems().front();
      for (int i = 0; i < score->nstaves(); ++i)
            if (sys->staff(i)->show())
                  out.push_back(i);
      return out;
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
      QCOMPARE(score->nstaves(), BANDS);                  // the band staves, treble shown
      QVERIFY(shownStaves(score) == std::vector<int>({ 1 }));
      QCOMPARE(score->fileInfo()->completeBaseName(), QString("Violin › Idea"));
      QCOMPARE(score->lastMeasure()->endTick().ticks(), 12 * 480);     // up to the clip's end
      QCOMPARE(score->firstMeasure()->timesig(), Fraction(4, 4));
      QCOMPARE(qRound(score->tempomap()->tempo(0) * 60), 96);
      // every bar numbered, the first too
      QVERIFY(score->styleB(Sid::showMeasureNumber));
      QVERIFY(score->styleB(Sid::showMeasureNumberOne));
      QCOMPARE(score->styleI(Sid::measureNumberInterval), 1);
      QVERIFY(!score->styleB(Sid::measureNumberSystem));
      // (on the first staff shown: treble)
      for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure())
            QVERIFY2(m->noText(1) && m->noText(1)->visible(), qPrintable(QString("bar %1").arg(m->no() + 1)));
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

      // a note muted in Live doesn't play here either (it plays through the Live track now), and nothing is written
      // for it; played again in MuseScore: unmuted in Live
      Clip muted = melody();
      muted.notes[1].mute = true;
      score = importClip(muted, nullptr);
      QVERIFY(score);
      Baseline mb = match(muted, score);
      QCOMPARE(applyMutes(mb, score), 1);
      Note* x = noteAt(score, 480, 69);
      QVERIFY(x && !x->play());
      QVERIFY(noteAt(score, 0, 67)->play());
      QVERIFY(diff(mb, signatures(score)).empty());
      // played as Live has them (MuseScore 3's rendering: each note's own velocity; the muted one not at all)
      {
      score->setPlaylistDirty();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      QList<int> played;
      for (const auto& te : events)
            if (te.second.type() == ME_NOTEON && te.second.velo() > 0)
                  played << te.second.pitch() << te.second.velo();
      QCOMPARE(played, QList<int>({ 67, 87, 71, 91, 72, 70, 74, 76, 76, 99 }));
      }
      score->startCmd();
      x->undoChangeProperty(Pid::PLAY, true);
      score->endCmd();
      d = diff(mb, signatures(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(d.ops[0].id, 102);
      QCOMPARE(d.ops[0].mask, int(MUTE));
      QVERIFY(!d.ops[0].mute);
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

// what the first voice of staff 0 shows: (tick, ticks, note pitch or -1 for a rest), up to tick `end`
static QList<QList<int>> voice0(Score* score, int end)
      {
      QList<QList<int>> out;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            if (s->tick().ticks() >= end)
                  break;
            Element* e = s->element(0);
            if (!e)
                  continue;
            ChordRest* cr = toChordRest(e);
            out << QList<int>({ s->tick().ticks(), cr->actualTicks().ticks(),
                                cr->isChord() ? toChord(cr)->upNote()->pitch() : -1 });
            }
      return out;
      }

static Clip simpleClip(const std::vector<LiveNote>& notes, double end = 4)
      {
      Clip c;
      c.key = "c11";
      c.track = "Violin";
      c.name = "Lengths";
      c.end = end;
      c.notes = notes;
      return c;
      }

void TestLiveIntegration::clipEditDottedLengths()
      {
      // the owner, 2026-10-02: three G's 0.75 beat long, each followed by a 0.25-beat gap: dotted eighths and
      // sixteenth rests (the import's "simplify durations" made them quarters)
      Clip clip = simpleClip({ ln(1, 67, 0, 0.75, 90), ln(2, 67, 1, 0.75, 90), ln(3, 67, 2, 0.75, 90),
                               ln(4, 72, 3, 0.5, 90) });
      QCOMPARE(importGrid(clip), 120);
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      QCOMPARE(voice0(score, 1920), QList<QList<int>>({ { 0, 360, 67 }, { 360, 120, -1 }, { 480, 360, 67 }, { 840, 120, -1 },
                                                          { 960, 360, 67 }, { 1320, 120, -1 },
                                                          { 1440, 240, 72 }, { 1680, 240, -1 } }));
      QVERIFY(!noteAt(score, 1440, 72)->chord()->articulations().size());   // (not a staccato quarter)
      const Baseline b = match(clip, score);
      QCOMPARE(b.unmatched, 0);
      for (const Entry& e : b.entries)
            QCOMPARE(int(e.live.size()), 1);
      QCOMPARE(b.entries[0].sig.ticks, 360);
      QVERIFY(diff(b, signatures(score)).empty());        // untouched: nothing written
      delete score;

      // on 32nds: a 32nd grid (a dotted sixteenth and a 32nd rest)
      clip = simpleClip({ ln(1, 67, 0, 0.375, 90), ln(2, 67, 0.5, 0.375, 90), ln(3, 69, 1, 1, 90) });
      QCOMPARE(importGrid(clip), 60);
      score = importClip(clip, nullptr);
      QVERIFY(score);
      QCOMPARE(voice0(score, 960), QList<QList<int>>({ { 0, 180, 67 }, { 180, 60, -1 }, { 240, 180, 67 }, { 420, 60, -1 },
                                                         { 480, 480, 69 } }));
      QVERIFY(diff(match(clip, score), signatures(score)).empty());
      delete score;
      }

void TestLiveIntegration::clipEditHumanizedLengths()
      {
      // humanized (off the grid by up to 0.05 beat): a sixteenth grid, each end at its nearest sixteenth, no tiny rests
      Clip clip = simpleClip({ ln(1, 67, 0.013, 0.73, 90), ln(2, 67, 1.02, 0.71, 90), ln(3, 67, 1.98, 0.79, 90),
                               ln(4, 72, 3.03, 0.93, 90) });
      QCOMPARE(importGrid(clip), 120);
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      QCOMPARE(voice0(score, 1920), QList<QList<int>>({ { 0, 360, 67 }, { 360, 120, -1 }, { 480, 360, 67 }, { 840, 120, -1 },
                                                          { 960, 360, 67 }, { 1320, 120, -1 }, { 1440, 480, 72 } }));
      Baseline b = match(clip, score);
      QCOMPARE(b.unmatched, 0);
      QVERIFY(diff(b, signatures(score)).empty());
      delete score;

      // the humanized melody: quarters, eighths and a whole note as before (no rests between them)
      clip = melody();
      QCOMPARE(importGrid(clip), 120);
      score = importClip(clip, nullptr);
      QVERIFY(score);
      QList<int> lengths;
      for (const Sig& g : signatures(score))
            lengths << g.ticks;
      QCOMPARE(lengths, QList<int>({ 480, 480, 480, 240, 240, 1920 }));
      for (const QList<int>& cr : voice0(score, 1920))
            QVERIFY(cr[2] >= 0);
      QVERIFY(diff(match(clip, score), signatures(score)).empty());
      delete score;
      }

void TestLiveIntegration::clipEditAddTie()
      {
      // the owner, 2026-10-02: a G half note tied to a G dotted quarter (then an eighth C) stayed two notes in Live.
      // What MuseScore does: the tie fires playlistChanged (the clip tab writes on it) and the write is the first
      // G as long as the chain plus the second G removed by id
      // (in the second bar: not the one with the tempo marking, whose layout marks the playlist dirty anyway)
      Clip clip = simpleClip({ ln(1, 67, 4, 2, 90), ln(2, 67, 6, 1.5, 90), ln(3, 72, 7.5, 0.5, 90) }, 8);
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      QCOMPARE(int(b.entries.size()), 3);
      QCOMPARE(b.entries[1].sig.ticks, 720);
      score->setPlaylistClean();                         // (as after the clip tab's earlier edits)
      int changed = 0;
      QObject::connect(score, &Score::playlistChanged, [&changed]() { ++changed; });
      Note* g = noteAt(score, 1920, 67);
      QVERIFY(g && !g->tieFor());
      score->select(g);
      score->cmdToggleTie();                              // (T: its own command)
      QVERIFY(g->tieFor());
      QVERIFY(changed > 0);                               // the clip tab hears of it
      Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 2);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& x, const Op& y) { return x.id < y.id; });
      QCOMPARE(int(d.ops[0].kind), int(Op::MODIFY));     // the first G: the chain's length
      QCOMPARE(d.ops[0].id, 1);
      QCOMPARE(d.ops[0].mask, int(DURATION));
      QCOMPARE(d.ops[0].duration, 1680);
      QCOMPARE(int(d.ops[1].kind), int(Op::REMOVE));     // the second G: gone
      QCOMPARE(d.ops[1].id, 2);
      QCOMPARE(liveOf(d.next, 1)->duration, 3.5);
      QCOMPARE(liveOf(d.next, 1)->start, 4.0);
      QVERIFY(!liveOf(d.next, 2));
      // undone: the first G back to 2 beats, the second added again
      changed = 0;
      score->undoRedo(true, nullptr);
      QVERIFY(!g->tieFor());
      QVERIFY(changed > 0);
      d = diff(d.next, signatures(score));
      QCOMPARE(int(d.ops.size()), 2);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& x, const Op& y) { return int(x.kind) < int(y.kind); });
      QCOMPARE(d.ops[0].id, 1);
      QCOMPARE(d.ops[0].mask, int(DURATION));
      QCOMPARE(d.ops[0].duration, 960);
      QCOMPARE(int(d.ops[1].kind), int(Op::ADD));
      QCOMPARE(d.ops[1].pitch, 67);
      QCOMPARE(d.ops[1].start, 2880);
      QCOMPARE(d.ops[1].duration, 720);
      delete score;

      // over the bar line: G 2 … 4 tied to G 4 … 6
      clip = simpleClip({ ln(1, 67, 2, 2, 90), ln(2, 67, 4, 2, 90) }, 8);
      score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b2 = match(clip, score);
      g = noteAt(score, 960, 67);
      QVERIFY(g && !g->tieFor());
      score->select(g);
      score->cmdToggleTie();
      QVERIFY(g->tieFor() && g->tieFor()->endNote()->tick().ticks() == 1920);
      d = diff(b2, signatures(score));
      QCOMPARE(int(d.ops.size()), 2);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& x, const Op& y) { return x.id < y.id; });
      QCOMPARE(d.ops[0].id, 1);
      QCOMPARE(d.ops[0].mask, int(DURATION));
      QCOMPARE(d.ops[0].duration, 1920);
      QCOMPARE(int(d.ops[1].kind), int(Op::REMOVE));
      QCOMPARE(d.ops[1].id, 2);
      delete score;
      }

void TestLiveIntegration::clipEditRemoveTie()
      {
      // one Live note over the bar line (3 … 5): a tie chain; its tie removed: two notes, the Live note shortened
      // to the first and the second added
      Clip clip = simpleClip({ ln(1, 67, 3, 2, 90) }, 8);
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      QCOMPARE(int(b.entries.size()), 1);
      QCOMPARE(b.entries[0].sig.ticks, 960);
      Note* g = noteAt(score, 1440, 67);
      QVERIFY(g && g->tieFor());
      score->setPlaylistClean();                         // (as after the clip tab's earlier edits)
      int changed = 0;
      QObject::connect(score, &Score::playlistChanged, [&changed]() { ++changed; });
      score->select(g);
      score->cmdToggleTie();
      QVERIFY(!g->tieFor());
      QVERIFY(changed > 0);
      Diff d = diff(b, signatures(score));
      QCOMPARE(int(d.ops.size()), 2);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& x, const Op& y) { return int(x.kind) < int(y.kind); });
      QCOMPARE(int(d.ops[0].kind), int(Op::MODIFY));
      QCOMPARE(d.ops[0].id, 1);
      QCOMPARE(d.ops[0].mask, int(DURATION));
      QCOMPARE(d.ops[0].duration, 480);
      QCOMPARE(int(d.ops[1].kind), int(Op::ADD));
      QCOMPARE(d.ops[1].pitch, 67);
      QCOMPARE(d.ops[1].start, 1920);
      QCOMPARE(d.ops[1].duration, 480);
      QCOMPARE(int(d.added.size()), 1);
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
      QVERIFY(shownStaves(score) == std::vector<int>({ 1 }));    // the range fits treble
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
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      QCOMPARE(score->parts()[0]->instrument()->getId(), QString("piano"));
      // C6 needs one ledger line on treble 15ma, two on treble; C2 one on bass 15mb, two on bass: all four bands
      QVERIFY(shownStaves(score) == std::vector<int>({ 0, 1, 2, 3 }));
      const Baseline b = match(clip, score);
      QCOMPARE(b.unmatched, 0);
      QVERIFY(diff(b, signatures(score)).empty());
      delete score;
      }

//---------------------------------------------------------
//   clipEditBands: the four band staves acting as one (makeBandStaves, assignBands)
//---------------------------------------------------------

static Clip bandClip(const QString& track, const std::vector<int>& pitches)
      {
      Clip clip;
      clip.key = "b";
      clip.track = track;
      clip.name = "Bands";
      clip.end = 8;
      int id = 1;
      double t = 0;
      for (int p : pitches) {
            clip.notes.push_back(ln(id++, p, t, 0.5, 90));
            t += 0.5;
            }
      return clip;
      }

// the clefs never change: each staff's clef at every bar is its band's, no clef element anywhere
static bool clefsFixed(Score* score)
      {
      for (int i = 0; i < score->nstaves(); ++i)
            for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure())
                  if (score->staff(i)->clef(m->tick()) != BAND_CLEFS[i])
                        return false;
      for (Segment* seg = score->firstSegment(SegmentType::All); seg; seg = seg->next1())
            for (Element* e : seg->elist())
                  if (e && e->isClef() && !e->generated())
                        return false;
      return true;
      }

static int outOfRange(Score* score)
      {
      int n = 0;
      std::vector<Note*> notes;
      signatures(score, &notes);
      for (Note* note : notes) {
            const Instrument* in = note->part()->instrument(note->tick());
            if (note->ppitch() < in->minPitchP() || note->ppitch() > in->maxPitchP())
                  ++n;
            }
      return n;
      }

// every note drawn on the band where it needs the fewest ledger lines
static bool onBands(Score* score)
      {
      std::vector<Note*> notes;
      signatures(score, &notes);
      for (Note* n : notes) {
            const std::vector<int> b = bandsOf(n->ppitch());
            if (std::find(b.begin(), b.end(), n->chord()->vStaffIdx()) == b.end())
                  return false;
            }
      return true;
      }

void TestLiveIntegration::clipEditBands()
      {
      // ledger lines as MuseScore places the notes
      QCOMPARE(ledgerLines(64, ClefType::G), 0);
      QCOMPARE(ledgerLines(60, ClefType::G), 1);
      QCOMPARE(ledgerLines(57, ClefType::G), 2);
      QCOMPARE(ledgerLines(84, ClefType::G), 2);
      QCOMPARE(ledgerLines(36, ClefType::F), 2);
      QCOMPARE(ledgerLines(12, ClefType::F15_MB), 2);
      QCOMPARE(ledgerLines(88, ClefType::G15_MA), 0);
      // the bands: the fewest ledger lines; middle C on treble or bass alike, B5 treble or treble 15ma, D2 bass or 15mb
      QVERIFY(bandsOf(60) == std::vector<int>({ 1, 2 }));
      QVERIFY(bandsOf(83) == std::vector<int>({ 0, 1 }));
      QVERIFY(bandsOf(38) == std::vector<int>({ 2, 3 }));
      QVERIFY(bandsOf(59) == std::vector<int>({ 2 }));
      QVERIFY(bandsOf(62) == std::vector<int>({ 1 }));
      QVERIFY(bandsOf(35) == std::vector<int>({ 3 }));

      struct Case {
            const char* what;
            QString track;
            std::vector<int> pitches;
            std::vector<int> shown;
            };
      const std::vector<Case> cases {
            // the owner's bass synth (2026-10-03): its notes on bass 15mb, the higher ones on bass
            { "low", "35-BuzzWave", { 12, 19, 24, 28, 31, 35, 43, 50 }, { 2, 3 } },
            { "wide", "Pad", { 48, 55, 60, 64, 72, 79, 43, 76 }, { 1, 2 } },
            { "very wide", "Pad", { 12, 24, 43, 50, 67, 76, 96, 100 }, { 0, 1, 2, 3 } },
            { "high", "Lead", { 88, 91, 96, 100, 103, 108, 96, 88 }, { 0 } },
            };
      for (const Case& c : cases) {
            const Clip clip = bandClip(c.track, c.pitches);
            MasterScore* score = importClip(clip, nullptr);
            QVERIFY2(score, c.what);
            QCOMPARE(score->nstaves(), BANDS);
            QVERIFY2(shownStaves(score) == c.shown, c.what);
            QVERIFY2(clefsFixed(score), c.what);
            QVERIFY2(onBands(score), c.what);
            QCOMPARE(outOfRange(score), 0);
            const Baseline b = match(clip, score);
            QCOMPARE(b.unmatched, 0);
            QVERIFY2(diff(b, signatures(score)).empty(), c.what);
            delete score;
            }

      // a pitch edit over a band border: the note drawn on the other staff, one modification sent; undo
      {
            const Clip clip = bandClip("Pad", { 60, 64, 67, 72, 76, 79, 72, 67 });
            MasterScore* score = importClip(clip, nullptr);
            QVERIFY(score);
            QVERIFY(shownStaves(score) == std::vector<int>({ 1 }));
            const Baseline b = match(clip, score);
            Note* n = noteAt(score, 240, 64);
            QVERIFY(n);
            score->startCmd();
            score->undoChangePitch(n, 48, n->tpc1(), n->tpc2());
            score->endCmd();
            QCOMPARE(n->chord()->vStaffIdx(), 2);
            QVERIFY(shownStaves(score) == std::vector<int>({ 1, 2 }));
            QVERIFY(onBands(score));
            const Diff d = diff(b, signatures(score));
            QCOMPARE(int(d.ops.size()), 1);
            QCOMPARE(d.ops[0].id, 2);
            QCOMPARE(d.ops[0].pitch, 48);
            score->undoRedo(true, nullptr);
            QCOMPARE(n->chord()->vStaffIdx(), 1);
            QVERIFY(shownStaves(score) == std::vector<int>({ 1 }));
            QVERIFY(diff(b, signatures(score)).empty());
            score->undoRedo(false, nullptr);
            QCOMPARE(n->chord()->vStaffIdx(), 2);
            delete score;
      }

      // a chord over two bands: split by band into two voices, each drawn on its staff; nothing sent; a pitch
      // edit of one of its notes sends only that note
      {
            Clip clip = bandClip("Pad", {});
            clip.notes = { ln(1, 43, 0, 1, 80), ln(2, 48, 0, 1, 81), ln(3, 72, 0, 1, 82), ln(4, 76, 0, 1, 83),
                           ln(5, 79, 1, 1, 84) };
            MasterScore* score = importClip(clip, nullptr);
            QVERIFY(score);
            QVERIFY(onBands(score));
            QVERIFY(shownStaves(score) == std::vector<int>({ 1, 2 }));
            Note* lo = noteAt(score, 0, 43);
            Note* hi = noteAt(score, 0, 72);
            QVERIFY(lo && hi);
            QVERIFY(lo->chord() != hi->chord());
            QCOMPARE(lo->chord()->vStaffIdx(), 2);
            QCOMPARE(hi->chord()->vStaffIdx(), 1);
            const Baseline b = match(clip, score);
            QCOMPARE(b.unmatched, 0);
            QVERIFY(diff(b, signatures(score)).empty());
            // velocities kept by the split
            QCOMPARE(signatures(score).size(), size_t(5));
            score->startCmd();
            score->undoChangePitch(lo, 41, lo->tpc1(), lo->tpc2());
            score->endCmd();
            const Diff d = diff(b, signatures(score));
            QCOMPARE(int(d.ops.size()), 1);
            QCOMPARE(d.ops[0].id, 1);
            delete score;
      }

      // a picture (MS_CLIPBANDS_PNG=<folder>): a clip over all four bands, then a note moved from treble to bass
      // and a chord over two bands
      if (qEnvironmentVariableIsSet("MS_CLIPBANDS_PNG")) {
            const QString dir = qEnvironmentVariable("MS_CLIPBANDS_PNG");
            Clip clip = bandClip("35-BuzzWave", { 12, 19, 24, 31, 36, 43, 48, 55, 60, 64, 67, 72, 79, 88, 96, 100 });
            clip.notes.push_back(ln(50, 48, 8.0 - 1.0, 1.0, 80));
            clip.notes.push_back(ln(51, 72, 8.0 - 1.0, 1.0, 80));
            clip.end = 8;
            MasterScore* score = importClip(clip, nullptr);
            QVERIFY(score);
            QVERIFY(renderPng(score, dir + "/clip-bands-1-imported.png"));
            {
                  QFileInfo fi(dir + "/clip-bands-1-imported.mscz");
                  QVERIFY(score->saveCompressedFile(fi, false, false));
            }
            Note* n = noteAt(score, 9 * 240, 64);
            QVERIFY(n);
            score->startCmd();
            score->undoChangePitch(n, 50, n->tpc1(), n->tpc2());
            score->endCmd();
            QVERIFY(renderPng(score, dir + "/clip-bands-2-E4-to-D3.png"));
            {
                  QFileInfo fi(dir + "/clip-bands-2-E4-to-D3.mscz");
                  QVERIFY(score->saveCompressedFile(fi, false, false));
            }
            delete score;
            }

      // a track named after an instrument keeps its range
      {
            const Clip clip = bandClip("Cello", { 48, 50, 52, 53, 55, 57, 59, 60 });
            MasterScore* score = importClip(clip, nullptr);
            QVERIFY(score);
            QCOMPARE(score->parts()[0]->instrument()->getId(), QString("violoncello"));
            QVERIFY(score->parts()[0]->instrument()->minPitchP() > 0);
            QVERIFY(shownStaves(score) == std::vector<int>({ 2 }));
            delete score;
      }
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
//   clipTitleUnnamed
//    an unnamed clip's tab and window title (the owner, 2026-10-03: "35-BuzzWave ›" with nothing after it): "(clip)"
//    until the device says where it is, then its session slot (shown from 1) or "arrangement clip"
//---------------------------------------------------------

void TestLiveIntegration::clipTitleUnnamed()
      {
      Clip clip = melody("35-BuzzWave");
      QCOMPARE(clipTitle(clip), QString("35-BuzzWave › Idea"));
      QCOMPARE(clipTitle(clip, 2), QString("35-BuzzWave › Idea"));      // (a name wins)
      clip.name = QString();
      QCOMPARE(clipLabel(clip), QString("(clip)"));
      QCOMPARE(clipTitle(clip), QString("35-BuzzWave › (clip)"));
      QCOMPARE(clipTitle(clip, 2), QString("35-BuzzWave › session slot 3"));
      QCOMPARE(clipTitle(clip, ARRANGEMENT), QString("35-BuzzWave › arrangement clip"));
      clip.name = "  ";
      QCOMPARE(clipLabel(clip, 0), QString("session slot 1"));
      clip.name = "a/b: c?";
      QCOMPARE(clipTitle(clip), QString("35-BuzzWave › a_b_ c_"));
      clip.name = QString();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      QCOMPARE(score->fileInfo()->completeBaseName(), QString("35-BuzzWave › (clip)"));
      QCOMPARE(score->title(), QString("35-BuzzWave › (clip)"));
      delete score;
      }

//---------------------------------------------------------
//   clipTabClean
//    a clip tab shows no '*' while it is in sync with Live (liveclipedit.h; the owner, 2026-10-03): clean when
//    opened, dirty from an edit until Live confirms the write, clean again then; undo still works (dirty, written,
//    clean); a conflict leaves it dirty
//---------------------------------------------------------

void TestLiveIntegration::clipTabClean()
      {
      using namespace Ms::LiveIntegration;
      LiveClipEditor* ed = LiveClipEditor::instance();
      Clip clip = melody();
      clip.key = "c901";
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      Note* first = noteAt(score, 0, 67);
      QVERIFY(first);
      score->startCmd();                                // (an undo step before the editing starts)
      score->undoChangePitch(first, 66, first->tpc1() - 7, first->tpc2() - 7);
      score->endCmd();
      QVERIFY(score->dirty());
      clip.notes[0].pitch = 66;                         // (Live has it so)
      ed->edit(clip, score);
      QVERIFY(ed->isClipScore(score));
      QVERIFY(ed->inSync(score));
      QVERIFY(!score->dirty());                         // as read from Live: no '*'
      QVERIFY(score->undoStack()->canUndo());           // (the undo steps stay)

      // an edit: dirty until the write is confirmed
      Note* n = noteAt(score, 480, 69);
      QVERIFY(n);
      score->startCmd();
      score->undoChangePitch(n, 68, n->tpc1() - 7, n->tpc2() - 7);
      score->endCmd();
      QVERIFY(score->dirty());
      QVERIFY(!ed->inSync(score));                      // (waiting for the debounce)
      QTest::qWait(500);
      QCOMPARE(int(ed->state(score)), int(LiveClipEditor::State::SENDING));
      QVERIFY(score->dirty());
      QVERIFY(!ed->inSync(score));
      ed->received("/live/clip/written", { clip.key, 1, "ok", 1234 });
      QCOMPARE(int(ed->state(score)), int(LiveClipEditor::State::SYNC));
      QVERIFY(ed->inSync(score));
      QVERIFY(!score->dirty());
      QVERIFY2(ed->statusText(score).startsWith("Live clip Violin › Idea: in sync"), qPrintable(ed->statusText(score)));

      // undo: dirty, written, clean; the undone pitch is in Live
      QVERIFY(score->undoStack()->canUndo());
      score->undoRedo(true, nullptr);
      score->update();                                  // (the next update says the playlist changed)
      QVERIFY(score->dirty());
      QVERIFY(noteAt(score, 480, 69));
      QTest::qWait(500);
      QCOMPARE(int(ed->state(score)), int(LiveClipEditor::State::SENDING));
      QVERIFY(score->dirty());
      ed->received("/live/clip/written", { clip.key, 2, "ok", 1235 });
      QVERIFY(!score->dirty());
      QVERIFY(score->undoStack()->canRedo());

      // a conflict: stays dirty
      n = noteAt(score, 480, 69);
      QVERIFY(n);
      score->startCmd();
      score->undoChangePitch(n, 70, n->tpc1() + 7, n->tpc2() + 7);
      score->endCmd();
      QTest::qWait(500);
      ed->received("/live/clip/written", { clip.key, 3, "conflict", 999 });
      QCOMPARE(int(ed->state(score)), int(LiveClipEditor::State::CONFLICT));
      QVERIFY(!ed->inSync(score));
      QVERIFY(score->dirty());
      QVERIFY(ed->statusText(score).contains("conflict"));
      QVERIFY(ed->statusDetails(score).contains("Reload from Live"));

      ed->scoreClosed(score);
      QVERIFY(!ed->isClipScore(score));
      delete score;
      }

//---------------------------------------------------------
//   clipTabAudible
//    a clip tab playing through its Live track asks the device to make the track audible (liveclipedit.h; the
//    owner, 2026-10-03, option A): /ms/cliptab/audible 1 <track> at Play and each second while playing (the device's
//    heartbeat), 0 <track> at Stop or when it plays through another track; tools/live/test/test_cliptab.js has the
//    device's side
//---------------------------------------------------------

void TestLiveIntegration::clipTabAudible()
      {
      using namespace Ms::LiveIntegration;
      LiveClipEditor* ed = LiveClipEditor::instance();
      QStringList sent;
      LiveClipEditor::setSendHook([&sent](const QByteArray& p) {
            QString address;
            QVariantList args;
            if (LiveClips::parseOsc(p, &address, &args) && address == "/ms/cliptab/audible")
                  sent << QString("%1 %2").arg(args.value(0).toInt()).arg(args.value(1).toInt());
            });
      ed->setAudible(42);
      QCOMPARE(sent, QStringList({ "1 42" }));
      QCOMPARE(ed->audible(), 42);
      ed->setAudible(42);                               // (unchanged: nothing more than the heartbeat)
      QCOMPARE(sent.size(), 1);
      ed->audibleBeat();                                // the heartbeat (each second while audible)
      QCOMPARE(sent, QStringList({ "1 42", "1 42" }));
      sent.clear();
      ed->setAudible(43);                               // another track: the first as it was, then the new one
      QCOMPARE(sent, QStringList({ "0 42", "1 43" }));
      sent.clear();
      ed->setAudible(0);                                // Stop
      QCOMPARE(sent, QStringList({ "0 43" }));
      ed->audibleBeat();
      QCOMPARE(sent.size(), 1);                         // (no heartbeat after)
      QCOMPARE(ed->audible(), 0);
      LiveClipEditor::setSendHook(nullptr);
      }

//---------------------------------------------------------
//   clipVelocityLane
//    a clip tab's Velocity lane (liveclipmodel.h): scale (2u: 0-200 %) and absolute (127 u) within 1-127, nothing
//    before the first point; in "shape" the notes Live has stay (nothing to write) and MuseScore's own playback plays
//    them shaped, as the MuseScore Link device does in Live; the device's record and back
//---------------------------------------------------------

// the clip score's Velocity lane set as one undo step (the lane editor's commit)
static void setVelocityLane(MasterScore* score, const std::vector<std::pair<int, double>>& points, LiveClipEdit::VelMode m,
                            LiveClipEdit::VelOutput o)
      {
      Automation::Lane l;
      l.target = LiveClipEdit::VELOCITY_TARGET;
      for (const auto& p : points)
            l.points.push_back(Automation::Point(p.first, p.second, Automation::Curve::STEP));
      LiveClipEdit::setVelMode(l, m);
      LiveClipEdit::setVelOutput(l, o);
      std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
      Automation::PartLanes keep;
      for (const Automation::Lane& x : all[score->parts().front()])
            if (x.target != l.target)
                  keep.push_back(x);
      keep.push_back(l);
      all[score->parts().front()] = keep;
      QVERIFY(Automation::undoWrite(score, all));
      }

// MuseScore 3's playback of the clip score: (pitch, velocity) of each note-on
static QList<int> playedVelocities(MasterScore* score)
      {
      score->setPlaylistDirty();
      EventMap events;
      SynthesizerState ss;
      score->renderMidi(&events, false, true, ss);
      QList<int> played;
      for (const auto& te : events)
            if (te.second.type() == ME_NOTEON && te.second.velo() > 0)
                  played << te.second.pitch() << te.second.velo();
      return played;
      }

void TestLiveIntegration::clipVelocityLane()
      {
      using LiveClipEdit::VelMode;
      using LiveClipEdit::VelOutput;
      QCOMPARE(shapeVelocity(100, -1, VelMode::SCALE), 100);           // before the first point: its own
      QCOMPARE(shapeVelocity(100, 0.5, VelMode::SCALE), 100);          // the lane's middle: 100 %
      QCOMPARE(shapeVelocity(87, 0.25, VelMode::SCALE), 44);           // 50 %: 43.5, rounded
      QCOMPARE(shapeVelocity(90, 1.0, VelMode::SCALE), 127);           // 200 %: 180, at most 127
      QCOMPARE(shapeVelocity(100, 0.0, VelMode::SCALE), 1);            // 0 %: 1 (0 is a note-off)
      QCOMPARE(shapeVelocity(37, 0.5, VelMode::SET), 64);         // absolute: 63.5, its own left out
      QCOMPARE(shapeVelocity(37, 0.0, VelMode::SET), 1);
      QCOMPARE(shapeVelocity(37, 1.0, VelMode::SET), 127);
      QCOMPARE(LiveClipEdit::velocityText(0.5, VelMode::SCALE), QString("100 %"));
      QCOMPARE(LiveClipEdit::velocityText(0.5, VelMode::SET), QString("64"));
      QCOMPARE(LiveClipEdit::velocityFromShown(150, VelMode::SCALE), 0.75);
      QCOMPARE(LiveClipEdit::velocityShown(0.75, VelMode::SCALE), 150.0);

      const Clip clip = melody();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const Baseline b = match(clip, score);
      QCOMPARE(playedVelocities(score), QList<int>({ 67, 87, 69, 81, 71, 91, 72, 70, 74, 76, 76, 99 }));
      // 50 % from beat 0, 150 % from beat 2; shaped while playing: nothing to write, MuseScore plays it shaped
      setVelocityLane(score, { { 0, 0.25 }, { 960, 0.75 } }, VelMode::SCALE, VelOutput::SHAPE);
      const LiveClipEdit::VelocityLane v = LiveClipEdit::velocityLane(score);
      QVERIFY(v.present);
      QCOMPARE(int(v.output), int(VelOutput::SHAPE));
      QVERIFY(diff(b, signaturesForLive(score)).empty());
      QCOMPARE(playedVelocities(score), QList<int>({ 67, 44, 69, 41, 71, 127, 72, 105, 74, 114, 76, 127 }));
      // the same curve, absolute: 32 and 95 whatever the notes had
      setVelocityLane(score, { { 0, 0.25 }, { 960, 0.75 } }, VelMode::SET, VelOutput::SHAPE);
      QCOMPARE(playedVelocities(score), QList<int>({ 67, 32, 69, 32, 71, 95, 72, 95, 74, 95, 76, 95 }));
      // the lane's settings stay without points (the lane editor keeps them), and nothing is shaped then
      setVelocityLane(score, {}, VelMode::SET, VelOutput::WRITE);
      const LiveClipEdit::VelocityLane e = LiveClipEdit::velocityLane(score);
      QVERIFY(!e.present);
      QCOMPARE(int(e.mode), int(VelMode::SET));
      QCOMPARE(int(e.output), int(VelOutput::WRITE));
      QCOMPARE(playedVelocities(score), QList<int>({ 67, 87, 69, 81, 71, 91, 72, 70, 74, 76, 76, 99 }));

      // the device's record: mode, output, the points (with a curve), the originals; and back
      LiveClipEdit::VelocityLane r;
      r.present = true;
      r.mode = VelMode::SET;
      r.output = VelOutput::WRITE;
      r.lane.target = LiveClipEdit::VELOCITY_TARGET;
      Automation::Point p0(0, 0.2, Automation::Curve::LINEAR);
      Automation::setCurvature(p0, 0.5);
      r.lane.points = { p0, Automation::Point(1920, 0.9, Automation::Curve::STEP) };
      const std::vector<LiveClipEdit::Original> orig = { { 101, 67, 50, 87, 44 }, { 102, 69, 3917, 81, 41 } };
      const QVariantList atoms = velRecord(r, orig);
      QCOMPARE(atoms.size(), 3 + 2 * 7 + 1 + 2 * 5);
      QCOMPARE(atoms.value(0).toInt(), 1);
      QCOMPARE(atoms.value(1).toInt(), 1);
      LiveClipEdit::VelocityLane back;
      std::vector<LiveClipEdit::Original> ob;
      QVERIFY(parseVelRecord(atoms, &back, &ob));
      QCOMPARE(int(back.mode), int(VelMode::SET));
      QCOMPARE(int(back.output), int(VelOutput::WRITE));
      QCOMPARE(int(back.lane.points.size()), 2);
      QVERIFY(back.lane.points[0].curved());
      QCOMPARE(back.lane.points[0].c1y, p0.c1y);
      QCOMPARE(back.lane.points[1].tick, 1920);
      QCOMPARE(int(ob.size()), 2);
      QCOMPARE(ob[1].start, 3917);
      QCOMPARE(ob[1].written, 41);
      QVERIFY(!parseVelRecord(QVariantList({ 0, 0, 5 }), nullptr, nullptr));       // (cut short)
      // the packets: chunked, each with the key and serial
      QVariantList many;
      for (int i = 0; i < 1000; ++i)
            many << i;
      const std::vector<QByteArray> pk = velSetPackets("c7", 3, many);
      QVERIFY(pk.size() > 1);
      int total = 0;
      for (size_t c = 0; c < pk.size(); ++c) {
            QString address;
            QVariantList args;
            QVERIFY(LiveClips::parseOsc(pk[c], &address, &args));
            QCOMPARE(address, QString("/ms/vel/set"));
            QCOMPARE(args.value(0).toString(), QString("c7"));
            QCOMPARE(args.value(1).toInt(), 3);
            QCOMPARE(args.value(2).toInt(), int(c));
            QCOMPARE(args.value(3).toInt(), int(pk.size()));
            total += args.size() - 4;
            }
      QCOMPARE(total, 1000);
      delete score;
      }

//---------------------------------------------------------
//   clipVelocityWrite
//    "Write into the notes": the curve written as velocity-only modifications of the notes it changes, always from
//    the notes' velocities before it (the originals, kept in the notation), so a curve edited or re-applied never
//    scales scaled values; undo writes back; back to "shape": the originals written back
//---------------------------------------------------------

void TestLiveIntegration::clipVelocityWrite()
      {
      using LiveClipEdit::VelMode;
      using LiveClipEdit::VelOutput;
      const Clip clip = melody();
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      Baseline b = match(clip, score);
      setVelocityLane(score, { { 0, 0.25 }, { 960, 0.75 } }, VelMode::SCALE, VelOutput::WRITE);
      Diff d = diff(b, signaturesForLive(score));
      QCOMPARE(int(d.ops.size()), 6);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& a, const Op& c) { return a.id < c.id; });
      QList<int> vel;
      for (const Op& o : d.ops) {
            QCOMPARE(int(o.kind), int(Op::MODIFY));
            QCOMPARE(o.mask, int(VELOCITY));              // velocity only: Live's timing, probability … kept
            vel << o.velocity;
            }
      QCOMPARE(vel, QList<int>({ 44, 41, 127, 105, 114, 127 }));
      QCOMPARE(liveOf(d.next, 101)->start, 0.013);
      QCOMPARE(liveOf(d.next, 101)->probability, 0.75);
      b = d.next;                                       // (Live confirmed the write)
      // the notation keeps the originals; the device gets them with what was written
      QCOMPARE(noteAt(score, 0, 67)->veloOffset(), 87);
      std::vector<LiveClipEdit::Original> orig = originals(b, score);
      QCOMPARE(int(orig.size()), 6);
      std::sort(orig.begin(), orig.end(), [](const LiveClipEdit::Original& a, const LiveClipEdit::Original& c) { return a.id < c.id; });
      QCOMPARE(orig[0].id, 101);
      QCOMPARE(orig[0].pitch, 67);
      QCOMPARE(orig[0].start, 50);                      // 0.013 beats in UNITS (3840 a beat)
      QCOMPARE(orig[0].velocity, 87);
      QCOMPARE(orig[0].written, 44);
      QCOMPARE(orig[2].written, 127);
      QVERIFY(diff(b, signaturesForLive(score)).empty());

      // the curve edited (50 % -> 80 % in the first half): from the originals, not from what was written (87 -> 70,
      // not 44 -> 35)
      setVelocityLane(score, { { 0, 0.4 }, { 960, 0.75 } }, VelMode::SCALE, VelOutput::WRITE);
      d = diff(b, signaturesForLive(score));
      QCOMPARE(int(d.ops.size()), 2);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& a, const Op& c) { return a.id < c.id; });
      QCOMPARE(d.ops[0].id, 101);
      QCOMPARE(d.ops[0].velocity, 70);
      QCOMPARE(d.ops[1].id, 102);
      QCOMPARE(d.ops[1].velocity, 65);
      b = d.next;
      // the same curve applied again: nothing (no compounding)
      setVelocityLane(score, { { 0, 0.4 }, { 960, 0.75 }, { 5000, 0.75 } }, VelMode::SCALE, VelOutput::WRITE);
      QVERIFY(diff(b, signaturesForLive(score)).empty());
      score->undoRedo(true, nullptr);                   // (that last, no-op edit undone)
      // undo of the curve edit: written back as it was (44, 41)
      score->undoRedo(true, nullptr);
      d = diff(b, signaturesForLive(score));
      QCOMPARE(int(d.ops.size()), 2);
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& a, const Op& c) { return a.id < c.id; });
      QCOMPARE(d.ops[0].velocity, 44);
      QCOMPARE(d.ops[1].velocity, 41);
      b = d.next;
      // a velocity edited here: the new original, written through the curve (100 at 50 %: 50)
      Note* n = noteAt(score, 0, 67);
      score->startCmd();
      n->undoChangeProperty(Pid::VELO_OFFSET, 100);
      score->endCmd();
      d = diff(b, signaturesForLive(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(d.ops[0].velocity, 50);
      b = d.next;
      // absolute: the same curve, 32 / 95, whatever the originals
      setVelocityLane(score, { { 0, 0.25 }, { 960, 0.75 } }, VelMode::SET, VelOutput::WRITE);
      d = diff(b, signaturesForLive(score));
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& a, const Op& c) { return a.id < c.id; });
      vel.clear();
      for (const Op& o : d.ops)
            vel << o.velocity;
      QCOMPARE(vel, QList<int>({ 32, 32, 95, 95, 95, 95 }));
      b = d.next;
      // back to "shape while playing": one write, the originals back into the notes
      setVelocityLane(score, { { 0, 0.25 }, { 960, 0.75 } }, VelMode::SET, VelOutput::SHAPE);
      d = diff(b, signaturesForLive(score));
      std::sort(d.ops.begin(), d.ops.end(), [](const Op& a, const Op& c) { return a.id < c.id; });
      vel.clear();
      for (const Op& o : d.ops) {
            QCOMPARE(o.mask, int(VELOCITY));
            vel << o.velocity;
            }
      QCOMPARE(vel, QList<int>({ 100, 81, 91, 70, 76, 99 }));
      b = d.next;
      QVERIFY(originals(b, score).empty());             // ("shape": the notes are the originals)
      delete score;
      }

//---------------------------------------------------------
//   clipVelocityReopen
//    a clip in "write" opened again (another session, the set reopened): the record's originals give each note its
//    velocity before the curve when Live still has the velocity written; a note changed in Live since takes Live's as
//    its original, one without a record keeps its own; then the notation and Live agree (nothing written)
//---------------------------------------------------------

void TestLiveIntegration::clipVelocityReopen()
      {
      using LiveClipEdit::VelMode;
      using LiveClipEdit::VelOutput;
      Clip clip = melody();
      // as written by the curve (50 % then 150 %); note 104 changed in Live since (60 for 105); the note ids new (the
      // set reopened): found by pitch and start
      const double written[] = { 44, 41, 127, 60, 114, 127 };
      for (int i = 0; i < 6; ++i) {
            clip.notes[size_t(i)].velocity = written[i];
            clip.notes[size_t(i)].id = 201 + i;
            }
      const std::vector<LiveClipEdit::Original> orig = {
            { 101, 67, int(std::lround(0.013 * 3840)), 87, 44 }, { 102, 69, int(std::lround(1.02 * 3840)), 81, 41 },
            { 103, 71, int(std::lround(1.991 * 3840)), 91, 127 }, { 104, 72, int(std::lround(3.004 * 3840)), 70, 105 },
            { 105, 74, int(std::lround(3.51 * 3840)), 76, 114 } };            // (106: none, added later)
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      Baseline b = match(clip, score);
      setVelocityLane(score, { { 0, 0.25 }, { 960, 0.75 } }, VelMode::SCALE, VelOutput::WRITE);
      QCOMPARE(applyOriginals(b, score, orig), 4);
      QCOMPARE(noteAt(score, 0, 67)->veloOffset(), 87);
      QCOMPARE(noteAt(score, 480, 69)->veloOffset(), 81);
      QCOMPARE(noteAt(score, 960, 71)->veloOffset(), 91);
      QCOMPARE(noteAt(score, 1440, 72)->veloOffset(), 60);        // changed in Live: Live's is its original now
      QCOMPARE(noteAt(score, 1680, 74)->veloOffset(), 76);
      QCOMPARE(noteAt(score, 1920, 76)->veloOffset(), 127);       // no record: its own
      // the curve over the originals is what Live has, but for the note changed in Live: 60 at 150 % -> 90
      Diff d = diff(b, signaturesForLive(score));
      QCOMPARE(int(d.ops.size()), 1);
      QCOMPARE(d.ops[0].id, 204);
      QCOMPARE(d.ops[0].velocity, 90);
      QCOMPARE(d.ops[0].mask, int(VELOCITY));
      // applied twice: nothing more
      QCOMPARE(applyOriginals(b, score, orig), 0);
      delete score;
      }

//---------------------------------------------------------
//   clipVelocityRecord
//    the clip tab and the MuseScore Link device (protocol 6): the record asked for when the tab opens and read back
//    into the lane; a lane edit sends it (/ms/vel/set) once confirmed in sync; in "write" the notes' write goes first
//    and the record (with the originals) after Live confirmed it; the status line
//---------------------------------------------------------

void TestLiveIntegration::clipVelocityRecord()
      {
      using namespace Ms::LiveIntegration;
      using LiveClipEdit::VelMode;
      using LiveClipEdit::VelOutput;
      LiveClipsLink::instance()->setDeviceProtocol(LiveClipEdit::VEL_PROTOCOL);
      LiveClipEditor* ed = LiveClipEditor::instance();
      QStringList sent;
      std::map<QString, QVariantList> last;
      LiveClipEditor::setSendHook([&sent, &last](const QByteArray& p) {
            QString address;
            QVariantList args;
            if (LiveClips::parseOsc(p, &address, &args)) {
                  sent << address;
                  last[address] = args;
                  }
            });
      Clip clip = melody();
      clip.key = "c902";
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      ed->edit(clip, score);
      QVERIFY(sent.contains("/ms/vel/ask"));
      QCOMPARE(last["/ms/vel/ask"], QVariantList({ "c902" }));
      // the device keeps a curve for it: read into the lane (no undo step), nothing sent back
      LiveClipEdit::VelocityLane kept;
      kept.present = true;
      kept.lane.target = LiveClipEdit::VELOCITY_TARGET;
      kept.lane.points = { Automation::Point(0, 0.25, Automation::Curve::STEP) };
      QVariantList curve { "c902", 1, 1, 0, 1 };
      curve.append(velRecord(kept, {}));
      sent.clear();
      ed->received("/live/vel/curve", curve);
      const LiveClipEdit::VelocityLane got = LiveClipEdit::velocityLane(score);
      QVERIFY(got.present);
      QCOMPARE(got.lane.points.front().value, 0.25);
      QVERIFY(!score->undoStack()->canUndo());
      QTest::qWait(500);
      QVERIFY(ed->inSync(score));
      QVERIFY(!sent.contains("/ms/vel/set"));
      QVERIFY2(ed->statusText(score).contains("velocity shaped in Live"), qPrintable(ed->statusText(score)));

      // an edit of the lane: the record sent, in flight until the device answers
      setVelocityLane(score, { { 0, 0.4 } }, VelMode::SCALE, VelOutput::SHAPE);
      score->update();
      QTest::qWait(500);
      QVERIFY(sent.contains("/ms/vel/set"));
      QVERIFY(!sent.contains("/ms/clip/write"));        // ("shape": the notes stay)
      QVariantList a = last["/ms/vel/set"];
      QCOMPARE(a.value(0).toString(), QString("c902"));
      const int serial = a.value(1).toInt();
      LiveClipEdit::VelocityLane v;
      std::vector<LiveClipEdit::Original> o;
      QVERIFY(parseVelRecord(a.mid(4), &v, &o));
      QVERIFY(std::fabs(v.lane.points.front().value - 0.4) < 1e-6);     // (float32 in OSC)
      QVERIFY(o.empty());
      QVERIFY(!ed->inSync(score));
      ed->received("/live/vel/set", { "c902", serial, "ok", 1 });
      QVERIFY(ed->inSync(score));

      // "write": the notes first, the record with the originals once Live confirmed them
      sent.clear();
      setVelocityLane(score, { { 0, 0.4 } }, VelMode::SCALE, VelOutput::WRITE);
      score->update();
      QTest::qWait(500);
      QVERIFY(sent.contains("/ms/clip/write"));
      QVERIFY(!sent.contains("/ms/vel/set"));
      const int write = last["/ms/clip/write"].value(1).toInt();
      ed->received("/live/clip/written", { "c902", write, "ok", 4321 });
      QVERIFY(sent.contains("/ms/vel/set"));
      a = last["/ms/vel/set"];
      QVERIFY(parseVelRecord(a.mid(4), &v, &o));
      QCOMPARE(int(v.output), int(VelOutput::WRITE));
      QCOMPARE(int(o.size()), 6);
      ed->received("/live/vel/set", { "c902", a.value(1).toInt(), "ok", 1 });
      QVERIFY(ed->inSync(score));
      QVERIFY2(ed->statusText(score).contains("velocity written"), qPrintable(ed->statusText(score)));

      // not kept (the script not set up): said in the details
      setVelocityLane(score, { { 0, 0.3 } }, VelMode::SCALE, VelOutput::SHAPE);
      score->update();
      QTest::qWait(500);
      const int w2 = last["/ms/clip/write"].value(1).toInt();
      ed->received("/live/clip/written", { "c902", w2, "ok", 4322 });          // (the originals written back)
      a = last["/ms/vel/set"];
      ed->received("/live/vel/set", { "c902", a.value(1).toInt(), "ok", 0 });
      QVERIFY2(ed->statusDetails(score).contains("not kept with the set"), qPrintable(ed->statusDetails(score)));

      ed->scoreClosed(score);
      LiveClipEditor::setSendHook(nullptr);
      LiveClipsLink::instance()->setDeviceProtocol(0);
      delete score;
      }

//---------------------------------------------------------
//   clipEnvelopeMapping
//    a clip tab's lanes as the clip's envelopes (liveclipmodel.h): a lane's points as Live's breakpoints (a step's
//    value again before the next point, a jump as two at one time, a curve as straight pieces along MuseScore's
//    Bézier), read back as the same points; the targets; the /ms/env packets; the device's parameter lists
//---------------------------------------------------------

void TestLiveIntegration::clipEnvelopeMapping()
      {
      using Automation::Point;
      using Automation::Curve;
      int d = 0, p = 0;
      QCOMPARE(liveTarget(-1, 0), QString("live:-1/0"));
      QVERIFY(parseLiveTarget("live:3/12", &d, &p));
      QCOMPARE(d, 3);
      QCOMPARE(p, 12);
      QVERIFY(parseLiveTarget("live:-1/1", &d, &p) && d == -1 && p == 1);
      QVERIFY(!parseLiveTarget("vibrato", &d, &p));
      QVERIFY(!parseLiveTarget("live:a/1", &d, &p));

      // steps (0.25 from beat 0, 0.75 from beat 2), a straight ramp to 0.5 at beat 4 … 1.0 at beat 6, a jump to 0 there
      std::vector<Point> pts { Point(0, 0.25, Curve::STEP), Point(960, 0.75, Curve::STEP), Point(1920, 0.5, Curve::LINEAR),
                               Point(2880, 1.0, Curve::LINEAR), Point(2880, 0.0, Curve::STEP) };
      std::vector<std::pair<int, double>> ev = envelopeEvents(pts);
      const std::vector<std::pair<int, double>> want { { 0, 0.25 }, { 960, 0.25 }, { 960, 0.75 }, { 1920, 0.75 }, { 1920, 0.5 },
                                                       { 2880, 1.0 }, { 2880, 0.0 } };
      QCOMPARE(ev, want);
      // read back: the same lane (each point; the steps steps again)
      const std::vector<Point> back = lanePoints(ev);
      QCOMPARE(int(back.size()), int(pts.size()));
      for (size_t i = 0; i < pts.size(); ++i) {
            QCOMPARE(back[i].tick, pts[i].tick);
            QCOMPARE(back[i].value, pts[i].value);
            if (i + 1 < pts.size() && pts[i + 1].tick != pts[i].tick)
                  QCOMPARE(int(back[i].curve), int(pts[i].curve));
            }
      // and written again from what was read: the same breakpoints (nothing grows on a round trip)
      QCOMPARE(envelopeEvents(back), want);
      // a step to the same value: no extra breakpoint; one point: one breakpoint
      QCOMPARE(int(envelopeEvents({ Point(0, 0.5, Curve::STEP), Point(480, 0.5, Curve::STEP) }).size()), 2);
      QCOMPARE(int(envelopeEvents({ Point(240, 0.3, Curve::LINEAR) }).size()), 1);

      // a curved ramp (curvature 0.8, rising early): straight pieces nowhere further than one MIDI step (1/127 of the
      // range) from MuseScore's curve, at every tick (Automation::flattenCurve; the breakpoints on ticks: + a tick's rise)
      Point c(0, 0.0, Curve::LINEAR);
      Automation::setCurvature(c, 0.8);
      QVERIFY(c.curved());
      ev = envelopeEvents({ c, Point(1920, 1.0, Curve::STEP) });
      QVERIFY2(ev.size() > 3 && ev.size() < 64, qPrintable(QString::number(ev.size())));
      QCOMPARE(ev.front(), std::make_pair(0, 0.0));
      QCOMPARE(ev.back(), std::make_pair(1920, 1.0));
      for (size_t i = 1; i < ev.size(); ++i) {
            QVERIFY(ev[i].first > ev[i - 1].first);
            for (int t = ev[i - 1].first; t <= ev[i].first; ++t) {
                  const double line = ev[i - 1].second + (ev[i].second - ev[i - 1].second) * (t - ev[i - 1].first)
                                                         / (ev[i].first - ev[i - 1].first);
                  const double curve = Automation::curveAt(c.c1x, c.c1y, c.c2x, c.c2y, t / 1920.0);
                  QVERIFY2(std::fabs(line - curve) <= Automation::CC_RESOLUTION + 0.003,
                           qPrintable(QString("tick %1: %2 against %3").arg(t).arg(line).arg(curve)));
                  }
            }
      for (size_t i = 1; i < ev.size(); ++i)                // (above the straight line half way: it rises early)
            if (ev[i - 1].first <= 960 && ev[i].first >= 960)
                  QVERIFY(ev[i - 1].second + (ev[i].second - ev[i - 1].second) * (960 - ev[i - 1].first)
                          / std::max(1, ev[i].first - ev[i - 1].first) > 0.6);

      // the packets: /ms/env/write, then each lane's /ms/env/lane in chunks of ENV_PAIRS
      EnvLane a { 1, 2, {} };
      for (int i = 0; i < 150; ++i)
            a.events.push_back({ i * 10, i / 150.0 });
      EnvLane cleared { -1, 0, {} };
      const std::vector<QByteArray> pk = envWritePackets("c9", 4, 2, 1, -77, { a, cleared });
      QCOMPARE(int(pk.size()), 4);
      QString address;
      QVariantList args;
      QVERIFY(LiveClips::parseOsc(pk[0], &address, &args));
      QCOMPARE(address, QString("/ms/env/write"));
      QCOMPARE(args, QVariantList({ "c9", 4, 2, 1, -77, 2 }));
      QVERIFY(LiveClips::parseOsc(pk[2], &address, &args));
      QCOMPARE(address, QString("/ms/env/lane"));
      QCOMPARE(args.mid(0, 6), QVariantList({ "c9", 4, 1, 2, 1, 2 }));
      QCOMPARE(args.size(), 6 + 2 * 50);
      QCOMPARE(args[6].toInt(), 1000);
      QVERIFY(LiveClips::parseOsc(pk[3], &address, &args));
      QCOMPARE(args, QVariantList({ "c9", 4, -1, 0, 0, 1 }));           // (no pairs: cleared)

      // the device's parameter lists: in chunks, complete once every chunk is in; the same list again changes nothing
      TrackParams* tp = TrackParams::instance();
      tp->clear();
      const QVariantList c0 { "c9", 55, 0, 2, -1, 0, "Mixer › Volume", 0.0, 1.0, 0 };
      const QVariantList c1 { "c9", 55, 1, 2, 1, 4, "Operator › Algorithm", 0.0, 10.0, 1 };
      QVERIFY(!tp->accept(c0));
      QVERIFY(!tp->params("c9"));
      QVERIFY(tp->accept(c1));
      QCOMPARE(int(tp->params("c9")->size()), 2);
      const LiveParam* algo = tp->param("c9", "live:1/4");
      QVERIFY(algo && algo->quantized && algo->max == 10.0 && algo->name == QString("Operator › Algorithm"));
      QVERIFY(!tp->param("c9", "live:1/5"));
      const int gen = tp->generation();
      QVERIFY(!tp->accept(c0));
      QVERIFY(!tp->accept(c1));                                         // (the same list)
      QCOMPARE(tp->generation(), gen);
      QVERIFY(tp->accept({ "c9", 56, 0, 1, -1, 1, "Mixer › Pan", -1.0, 1.0, 0 }));   // a device removed in Live
      QCOMPARE(int(tp->params("c9")->size()), 1);
      tp->clear();
      }

//---------------------------------------------------------
//   laneTimeAxis
//    the automation lanes' time axis in Continuous View (libmscore/automation.h timeAxis, drawn by mscore/automationlanes.h; the owner's screenshot, 2026-10-02:
//    the lanes' bar lines ~10 px right of the staff's, a point left of its note): a tick at a note is the middle of
//    its note heads; the grid's bar line is the staff's bar line; between them the time runs on to the bar line
//---------------------------------------------------------

void TestLiveIntegration::laneTimeAxis()
      {
      MasterScore* score = readScore(DIR + "violin-flute.musicxml");
      QVERIFY(score);
      score->setLayoutMode(LayoutMode::LINE);
      score->doLayout();
      const double sp = score->spatium();
      const std::vector<std::pair<int, double>> anchors = Automation::timeAxis(score);
      QVERIFY(!anchors.empty());
      int notes = 0;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            Element* e = s->element(0);
            if (!e || !e->isChord())
                  continue;
            for (Note* n : toChord(e)->notes()) {
                  const double head = n->canvasBoundingRect().center().x();
                  const double x = Automation::xAtTick(anchors, s->tick().ticks());
                  QVERIFY2(std::fabs(x - head) < 0.1 * sp, qPrintable(QString("tick %1: lane %2, note %3")
                                                                     .arg(s->tick().ticks()).arg(x).arg(head)));
                  ++notes;
                  }
            }
      QVERIFY(notes > 3);
      for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            const double bar = Automation::barLineX(m);
            const double edge = m->canvasPos().x() + m->width();
            // (the bar line drawn: the end bar line's middle, or before a start repeat that one's: within its width)
            QVERIFY2(std::fabs(bar - edge) < 1.0 * sp, qPrintable(QString("bar %1, measure's end %2").arg(bar).arg(edge)));
            // just before the bar: almost at the bar line; at the next bar's tick: its first note, right of it
            QVERIFY(std::fabs(Automation::xAtTick(anchors, m->endTick().ticks() - 1) - bar) < 0.2 * sp);
            if (m->nextMeasure())
                  QVERIFY(Automation::xAtTick(anchors, m->endTick().ticks()) > bar + 0.5 * sp);
            }
      delete score;
      }

//---------------------------------------------------------
//   liveParamLanes
//    a part Live plays: a lane on a parameter of its Live track ("live:<d>/<p>") goes to the device as a parameter
//    lane titled by its target (the device finds it on the track), next to the library's own; MuseScore's own
//    playback (the hosted plug-in) has nothing for it; an empty one nothing at all
//---------------------------------------------------------

void TestLiveIntegration::liveParamLanes()
      {
      MasterScore* score = readScore(DIR + "violin-flute.musicxml");
      QVERIFY(score);
      auto lib = loadMap(MAP);
      SoundLib::setCurrent(lib);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      score->rebuildMidiMapping();
      std::map<const Part*, Automation::PartLanes> all;
      Automation::Lane vib;
      vib.target = "vibrato";
      vib.points = { Automation::Point(0, 0.5, Automation::Curve::STEP) };
      Automation::Lane tone;
      tone.target = "live:1/2";
      tone.extra["name"] = "Operator › Tone";
      tone.points = { Automation::Point(0, 0.2, Automation::Curve::STEP), Automation::Point(960, 0.9, Automation::Curve::STEP) };
      Automation::Lane vol;
      vol.target = "live:-1/0";
      vol.points = { Automation::Point(480, 0.7, Automation::Curve::STEP) };
      Automation::Lane empty;
      empty.target = "live:2/0";
      all[score->parts()[0]] = { vib, tone, vol, empty };
      score->setMetaTag(Automation::metaTag, Automation::write(score, all));
      QCOMPARE(LiveClips::liveLanes(Automation::read(score).at(score->parts()[0])), QStringList({ "live:1/2", "live:-1/0" }));

      EventMap events;
      MidiRenderer r(score);
      r.setForLiveClips(true);
      SynthesizerState ss;
      MidiRenderer::Context ctx(ss);
      ctx.metronome = false;
      for (int utick = 0;;) {
            const MidiRenderer::Chunk ch = r.getChunkAt(utick);
            if (!ch)
                  break;
            r.renderChunk(ch, &events, ctx);
            utick = ch.utick2();
            }
      const std::vector<LiveClips::Track> tracks = LiveClips::tracks(score, *lib, events, { "MuseScore A" },
                                                                     LiveClips::timeline(score));
      const LiveClips::Track* violin = nullptr;
      for (const LiveClips::Track& t : tracks)
            if (t.part == "Violin" && t.main)
                  violin = &t;
      QVERIFY(violin);
      QStringList titles;
      for (const LiveClips::Track::ParamLane& pl : violin->params)
            titles << pl.title;
      titles.sort();
      QCOMPARE(titles, QStringList({ "Vibrato", "live:-1/0", "live:1/2" }));
      for (const LiveClips::Track::ParamLane& pl : violin->params) {
            if (pl.title != "live:1/2")
                  continue;
            QCOMPARE(int(pl.events.size()), 2);
            QCOMPARE(pl.events[0].first, 0);
            QVERIFY(std::fabs(pl.events[0].second - 0.2f) < 1e-6);
            QCOMPARE(pl.events[1].first, 2 * U);
            QVERIFY(std::fabs(pl.events[1].second - 0.9f) < 1e-6);
            }
      // the flute: none
      for (const LiveClips::Track& t : tracks)
            if (t.part == "Flute")
                  QVERIFY(t.params.empty());

      // MuseScore's own playback (the hosted plug-in): Vibrato only
      SoundLib::setOutput(SoundLib::Output::PLUGIN);
      score->setPlaylistDirty();
      EventMap hosted;
      score->renderMidi(&hosted, false, true, ss);
      int vibrato = 0;
      for (const auto& te : hosted) {
            if (te.second.type() != ME_PARAMETER)
                  continue;
            QVERIFY(te.second.dataA() != LiveClips::LIVE_PARAM);
            ++vibrato;
            }
      QVERIFY(vibrato > 0);
      SoundLib::setOutput(SoundLib::Output::MIDI);
      SoundLib::setCurrent(nullptr);
      delete score;
      }

//---------------------------------------------------------
//   clipTabMidi
//    a clip tab playing through its Live track (liveclipmodel.h LiveMidi): notes with their velocity on
//    channel 1, the pedals and the bend once per change, the Mixer's controllers and programs left out, a
//    stop (all notes off on every channel) releases the keys down once; the /ms/midi packet
//---------------------------------------------------------

void TestLiveIntegration::clipTabMidi()
      {
      LiveMidi m;
      MidiMsg out[LiveMidi::MAX_OUT];
      auto bytes = [&](int n) {
            QList<int> l;
            for (int i = 0; i < n; ++i)
                  l << out[i].b[0] << out[i].b[1] << out[i].b[2];
            return l;
            };
      QCOMPARE(bytes(m.accept(ME_NOTEON, 60, 87, out)), QList<int>({ 0x90, 60, 87 }));
      QVERIFY(m.sounding());
      QCOMPARE(bytes(m.accept(ME_NOTEON, 60, 0, out)), QList<int>({ 0x80, 60, 0 }));         // (note-on 0: off)
      QVERIFY(!m.sounding());
      QCOMPARE(m.accept(ME_NOTEOFF, 60, 0, out), 0);                                          // not down: nothing
      // two notes on one key (two voices, channels): released with the last
      m.accept(ME_NOTEON, 64, 90, out);
      QCOMPARE(bytes(m.accept(ME_NOTEON, 64, 70, out)), QList<int>({ 0x90, 64, 70 }));
      QCOMPARE(m.accept(ME_NOTEOFF, 64, 0, out), 0);
      QCOMPARE(bytes(m.accept(ME_NOTEOFF, 64, 0, out)), QList<int>({ 0x80, 64, 0 }));
      // the Mixer's and the instrument's controllers stay MuseScore's
      for (int cc : std::initializer_list<int>{ CTRL_VOLUME, CTRL_PANPOT, CTRL_REVERB_SEND, CTRL_CHORUS_SEND, CTRL_EXPRESSION, CTRL_MODULATION, 0, 32 })
            QCOMPARE(m.accept(ME_CONTROLLER, cc, 100, out), 0);
      QCOMPARE(m.accept(ME_PROGRAM, 5, 0, out), 0);
      // the pedals, once per change
      QCOMPARE(m.accept(ME_CONTROLLER, CTRL_SUSTAIN, 0, out), 0);                             // (up already)
      QCOMPARE(bytes(m.accept(ME_CONTROLLER, CTRL_SUSTAIN, 127, out)), QList<int>({ 0xb0, 64, 127 }));
      QCOMPARE(m.accept(ME_CONTROLLER, CTRL_SUSTAIN, 127, out), 0);
      QCOMPARE(bytes(m.accept(ME_CONTROLLER, 67, 100, out)), QList<int>({ 0xb0, 67, 100 }));
      // the bend: lower, upper 7 bits; the centre left out until it changed
      QCOMPARE(m.accept(ME_PITCHBEND, 0, 64, out), 0);
      QCOMPARE(bytes(m.accept(ME_PITCHBEND, 57, 96, out)), QList<int>({ 0xe0, 57, 96 }));
      // MuseScore's stop: sustain off, 128 note-offs and all notes off on each channel -> once what is down
      m.accept(ME_NOTEON, 48, 80, out);
      m.accept(ME_NOTEON, 52, 80, out);
      int total = 0;
      QList<int> stop;
      for (int ch = 0; ch < 3; ++ch) {
            int n = m.accept(ME_CONTROLLER, CTRL_SUSTAIN, 0, out);
            stop << bytes(n);
            total += n;
            for (int p = 0; p < 128; ++p) {
                  n = m.accept(ME_NOTEOFF, p, 0, out);
                  stop << bytes(n);
                  total += n;
                  }
            n = m.accept(ME_CONTROLLER, CTRL_ALL_NOTES_OFF, 0, out);
            stop << bytes(n);
            total += n;
            n = m.accept(ME_PITCHBEND, 0, 64, out);
            stop << bytes(n);
            total += n;
            }
      QCOMPARE(stop, QList<int>({ 0xb0, 64, 0, 0x80, 48, 0, 0x80, 52, 0, 0xb0, 67, 0, 0xe0, 0, 64 }));
      QCOMPARE(total, 5);
      QVERIFY(!m.sounding());
      // the packet: /ms/midi trackId status data1 data2
      QString address;
      QVariantList args;
      MidiMsg on { { 0x90, 72, 101 } };
      QVERIFY(LiveClips::parseOsc(midiPacket(12345, on), &address, &args));
      QCOMPARE(address, QString("/ms/midi"));
      QCOMPARE(args, QVariantList({ 12345, 0x90, 72, 101 }));
      }

//---------------------------------------------------------
//   midiInputSilent: while Live is linked, notes from the MIDI input device are entered but not sounded
//---------------------------------------------------------

namespace Ms { extern bool (*midiInputSilenced)(); }
static bool silentYes() { return true; }
static bool silentNo() { return false; }

void TestLiveIntegration::midiInputSilent()
      {
      QVERIFY(!LiveIntegration::LiveClipsLink::silencesMidiInput());          // no device answering: MuseScore sounds as before
      MasterScore* score = readScore(DIR + "violin-flute.musicxml");
      score->rebuildMidiMapping();
      for (int round = 0; round < 2; ++round) {
            const bool silent = round == 0;
            Ms::midiInputSilenced = silent ? &silentYes : &silentNo;
            score->inputState().setTrack(0);
            score->inputState().setSegment(score->tick2segment(Fraction(0, 1), false, SegmentType::ChordRest));
            score->inputState().setDuration(TDuration::DurationType::V_QUARTER);
            score->inputState().setNoteEntryMode(true);
            score->setPlayNote(false);
            score->setPlayChord(false);
            score->enqueueMidiEvent({ 60 + round, false, 80 });
            QVERIFY(score->processMidiInput());
            Ms::Chord* c = score->firstMeasure()->findChord(Fraction(0, 1), 0);
            QVERIFY(c);
            QCOMPARE(c->notes().front()->pitch(), 60 + round);    // note input works either way
            QCOMPARE(score->playNote(), !silent);                 // the entered note sounds only when not silenced
            score->inputState().setNoteEntryMode(false);
            score->undoRedo(true, nullptr);
            }
      Ms::midiInputSilenced = nullptr;
      delete score;
      }

//---------------------------------------------------------
//   linkWatch
//    the connection to Live lost and back (mscore/liveclips.h LinkWatch): one notice per loss, none
//    while nothing used the link, one when it answers again; what the texts say
//---------------------------------------------------------

void TestLiveIntegration::linkWatch()
      {
      using LiveIntegration::LinkWatch;
      LinkWatch w;
      QCOMPARE(w.update(false, true), LinkWatch::NONE);           // never connected: nothing to lose
      QCOMPARE(w.update(true, true), LinkWatch::NONE);            // the first hello: no notice
      QCOMPARE(w.update(true, true), LinkWatch::NONE);
      QCOMPARE(w.update(false, true), LinkWatch::LOST);           // lost: one notice
      QCOMPARE(w.update(false, true), LinkWatch::NONE);           // (no repeats)
      QCOMPARE(w.update(false, true), LinkWatch::NONE);
      QCOMPARE(w.update(true, true), LinkWatch::BACK);            // back: one notice
      QCOMPARE(w.update(true, true), LinkWatch::NONE);
      QCOMPARE(w.update(false, false), LinkWatch::NONE);          // lost while nothing used it: quiet
      QCOMPARE(w.update(true, false), LinkWatch::NONE);           // (and so no "back" either)
      QCOMPARE(w.update(false, true), LinkWatch::LOST);
      QCOMPARE(w.update(true, false), LinkWatch::BACK);           // (a loss announced is announced back)

      LinkWatch::Uses u;
      QVERIFY(!u.any());
      u.clipTabs = 2;
      u.clipTabsThroughLive = 1;
      u.livePlaysScore = true;
      u.playThroughLive = true;
      u.port = 9101;
      QVERIFY(u.any());
      const QString lost = LinkWatch::lostText(u);
      QVERIFY(lost.startsWith("Lost the connection to Live"));
      QVERIFY(lost.contains("9101"));
      QVERIFY(lost.contains("2 Live clip tab"));
      QVERIFY(lost.contains("own sounds meanwhile"));
      QVERIFY(lost.contains("Live plays the score"));
      QVERIFY(lost.contains("Play through Live"));
      QVERIFY(lost.contains("reconnects by itself"));
      u.clipTabsThroughLive = 0;
      u.livePlaysScore = false;
      QVERIFY(!LinkWatch::lostText(u).contains("own sounds meanwhile"));
      QVERIFY(!LinkWatch::lostText(u).contains("Live plays the score"));
      QVERIFY(LinkWatch::backText(u).contains("clip tabs are linked again"));
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

//---------------------------------------------------------
//   liveHelpersLibrary
//    Live's Library.cfg (12.4.6's, from the test VM, the user's name replaced): ProjectPath + ProjectName; the newest
//    Live version whose library exists; Documents/Ableton/User Library without one
//---------------------------------------------------------

void TestLiveIntegration::liveHelpersLibrary()
      {
      using namespace LiveIntegration::LiveHelpers;
      QFile f(QString(DIR_ROOT) + "Library.cfg");
      QVERIFY(f.open(QIODevice::ReadOnly));
      const QByteArray cfg = f.readAll();
      QCOMPARE(userLibraryFromCfg(cfg), QString("C:/Users/someone/Documents/Ableton/User Library"));
      QCOMPARE(userLibraryFromCfg("<Ableton><ContentLibrary><UserLibrary><LibraryProject>"
                                  "<ProjectPath Value=\"D:\\OneDrive\\Docs\\Ableton\\User Library\"/>"
                                  "</LibraryProject></UserLibrary></ContentLibrary></Ableton>"),
               QString("D:/OneDrive/Docs/Ableton/User Library"));
      QCOMPARE(userLibraryFromCfg("<Ableton><ContentLibrary/></Ableton>"), QString());

      QVERIFY(compareLiveVersions("Live 12.4.6", "Live 12.2") > 0);
      QVERIFY(compareLiveVersions("Live 12.10", "Live 12.4.6") > 0);           // (not as text)
      QVERIFY(compareLiveVersions("Live 11.3.13", "Live 12") < 0);
      QVERIFY(compareLiveVersions("Live 12.2", "Live 12.2.0") < 0);
      QCOMPARE(compareLiveVersions("Live 12.4", "Live 12.4"), 0);

      QTemporaryDir tmp;
      const QString base = tmp.path() + "/AppData/Ableton";
      const QString docs = tmp.path() + "/Documents";
      auto prefs = [&](const QString& version, const QString& lib) {
            const QString dir = base + "/" + version + "/Preferences";
            QDir().mkpath(dir);
            QFile c(dir + "/Library.cfg");
            QVERIFY(c.open(QIODevice::WriteOnly));
            QByteArray x = cfg;
            const int slash = lib.lastIndexOf('/');
            x.replace("C:/Users/someone/Documents/Ableton", lib.left(slash).toUtf8());
            x.replace("\"User Library\"", "\"" + lib.mid(slash + 1).toUtf8() + "\"");
            c.write(x);
            };
      // nothing: no library
      QCOMPARE(findUserLibrary({ base }, docs), QString());
      // the Documents fallback
      QDir().mkpath(docs + "/Ableton/User Library");
      QCOMPARE(findUserLibrary({ base }, docs), docs + "/Ableton/User Library");
      // Live's preferences win; the newest version whose library exists
      QDir().mkpath(tmp.path() + "/Lib122/User Library");
      QDir().mkpath(tmp.path() + "/Lib1246/My Library");
      QDir().mkpath(base + "/Live Reports");
      prefs("Live 12.2", tmp.path() + "/Lib122/User Library");
      QCOMPARE(findUserLibrary({ base }, docs), tmp.path() + "/Lib122/User Library");
      prefs("Live 12.4.6", tmp.path() + "/Lib1246/My Library");
      QCOMPARE(findUserLibrary({ base }, docs), tmp.path() + "/Lib1246/My Library");
      prefs("Live 12.10", tmp.path() + "/Gone/User Library");                   // (its folder doesn't exist)
      QCOMPARE(findUserLibrary({ base }, docs), tmp.path() + "/Lib1246/My Library");

      QVERIFY(underOneDrive("D:/OneDrive/Artemisia - personnel/Documents/Ableton/User Library"));
      QVERIFY(underOneDrive("C:\\Users\\x\\OneDrive - Company\\Documents"));
      QVERIFY(!underOneDrive("C:/Users/user/Documents/Ableton/User Library"));
      }

//---------------------------------------------------------
//   liveHelpersInstall
//    the check (missing, different, up to date) and the copy, in a temp dir: only our files, other files untouched
//---------------------------------------------------------

void TestLiveIntegration::liveHelpersInstall()
      {
      using namespace LiveIntegration::LiveHelpers;
      QTemporaryDir tmp;
      const QString bin = tmp.path() + "/bin";
      const QString lib = tmp.path() + "/User Library";
      auto write = [](const QString& path, const QByteArray& data) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(data);
            };
      auto read = [](const QString& path) {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray("(none)");
            };
      QDir().mkpath(lib);
      QCOMPARE(files(bin, lib).size(), 0);                                      // (a build without them: nothing)
      write(bin + "/MuseScore Link.amxd", "device 1");
      write(bin + "/MuseScoreEnvelopes/__init__.py", "init 1");
      write(bin + "/MuseScoreEnvelopes/core.py", "core 1");
      write(bin + "/MuseScoreEnvelopes/surface.py", "surface 1");
      write(lib + "/Presets/MIDI Effects/Max MIDI Effect/Other.amxd", "someone else's");
      write(lib + "/Remote Scripts/MuseScoreEnvelopes/MuseScoreEnvelopes.log", "log");

      const QVector<File> list = files(bin, lib);
      QCOMPARE(list.size(), 4);
      QCOMPARE(list[0].target, lib + "/Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd");
      QCOMPARE(list[2].target, lib + "/Remote Scripts/MuseScoreEnvelopes/core.py");
      QVector<Item> items = check(list);
      QCOMPARE(items.size(), 4);
      for (const Item& it : items)
            QCOMPARE(int(it.state), int(State::MISSING));
      QVERIFY(!upToDate(items));
      const QString v1 = shippedVersion(list);

      InstallResult r = install(items, false);
      QCOMPARE(r.copied.size(), 4);
      QVERIFY(r.failed.isEmpty());
      QVERIFY(!r.pinned);
      items = check(list);
      QVERIFY(upToDate(items));
      QCOMPARE(read(lib + "/Remote Scripts/MuseScoreEnvelopes/surface.py"), QByteArray("surface 1"));
      QCOMPARE(read(lib + "/Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd"), QByteArray("device 1"));

      // a MuseScore with another script: that file only, Update; the shipped version changes (asked again)
      write(bin + "/MuseScoreEnvelopes/core.py", "core 2");
      items = check(list);
      QCOMPARE(int(items[0].state), int(State::UP_TO_DATE));
      QCOMPARE(int(items[2].state), int(State::DIFFERENT));
      QVERIFY(shippedVersion(list) != v1);
      r = install(items, false);
      QCOMPARE(r.copied, QStringList({ lib + "/Remote Scripts/MuseScoreEnvelopes/core.py" }));
      QCOMPARE(read(lib + "/Remote Scripts/MuseScoreEnvelopes/core.py"), QByteArray("core 2"));
      QVERIFY(upToDate(check(list)));
      // edited in the library: different again
      write(lib + "/Presets/MIDI Effects/Max MIDI Effect/MuseScore Link.amxd", "device edited");
      QCOMPARE(int(check(list)[0].state), int(State::DIFFERENT));

      // the other files untouched
      QCOMPARE(read(lib + "/Presets/MIDI Effects/Max MIDI Effect/Other.amxd"), QByteArray("someone else's"));
      QCOMPARE(read(lib + "/Remote Scripts/MuseScoreEnvelopes/MuseScoreEnvelopes.log"), QByteArray("log"));
      QCOMPARE(QDir(lib + "/Remote Scripts/MuseScoreEnvelopes").entryList(QDir::Files).size(), 4);
      }


//---------------------------------------------------------
//   clip tabs at the song's tempo (mscore/cliptempo.h)
//---------------------------------------------------------

static const char* const TEMPO_SETS = "liveIntegration/tempoSets";

// tempo.xml (a Live 12.4.6 set's main track, tempo automation added) as an .als at path
static bool writeTempoSet(const QString& path, const QByteArray& from = QByteArray(), const QByteArray& to = QByteArray())
      {
      QFile x(QString(DIR_ROOT) + "tempo.xml");
      if (!x.open(QIODevice::ReadOnly))
            return false;
      QByteArray xml = x.readAll();
      if (!from.isEmpty())
            xml.replace(from, to);
      QDir().mkpath(QFileInfo(path).path());
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly))
            return false;
      f.write(gzip(xml));
      return true;
      }

static double bpmAt(const Score* score, int tick)
      {
      return score->tempomap()->tempo(tick) * 60.0;
      }

static bool nearly(double a, double b)
      {
      return std::fabs(a - b) <= 1e-9 * std::max(1.0, std::fabs(b));
      }

// the score's tempo markings: visible, invisible (a ramp's steps), and the words (system text)
static void countTempo(const Score* score, int* shown, int* hidden, int* words)
      {
      *shown = *hidden = *words = 0;
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest))
            for (Element* e : s->annotations()) {
                  if (e->isTempoText())
                        ++*(e->visible() ? shown : hidden);
                  else if (e->isSystemText())
                        ++*words;
                  }
      }

//---------------------------------------------------------
//   clipTempoSetRead
//    the main track's tempo automation and the tracks' arrangement clips; which set has the clip
//---------------------------------------------------------

void TestLiveIntegration::clipTempoSetRead()
      {
      QTemporaryDir dir;
      const QString path = dir.path() + "/Song Project/Song.als";
      QVERIFY(writeTempoSet(path));
      const LiveSet::Set set = LiveSet::read(path);
      QVERIFY2(set.error.isEmpty(), qPrintable(set.error));
      QCOMPARE(set.creator, QString("Ableton Live 12.4.6"));
      QCOMPARE(set.tempo, 120.0);
      QCOMPARE(set.tempoInitial, 120.0);
      QCOMPARE(int(set.tempoEvents.size()), 7);
      QCOMPARE(set.tempoEvents[2].time, 16.0);
      QCOMPARE(set.tempoEvents[2].value, 150.0);
      QVERIFY(!set.tempoEvents[2].curved);
      QVERIFY(set.tempoEvents[3].curved);
      QCOMPARE(set.tempoEvents[3].c1x, 0.5);
      QCOMPARE(set.tempoEvents[3].c2y, 0.5);
      QCOMPARE(set.tempoEvents[5].time, 32.0);
      QCOMPARE(set.tempoEvents[6].time, 32.0);
      QCOMPARE(set.tempoEvents[6].value, 100.0);
      // the time signature's envelope (an EnumEvent on another target) is not the tempo
      QCOMPARE(int(set.tracks.size()), 3);
      QCOMPARE(set.tracks[0].kind, QString("MidiTrack"));
      QCOMPARE(set.tracks[2].kind, QString("ReturnTrack"));
      QCOMPARE(int(set.tracks[0].clips.size()), 2);
      const LiveSet::ArrangementClip& c = set.tracks[0].clips[0];
      QCOMPARE(c.start, 8.0);
      QCOMPARE(c.end, 24.0);
      QVERIFY(c.loopOn);
      QCOMPARE(c.startRelative, 1.0);
      QCOMPARE(c.name, QString("Riff"));

      using namespace LiveClipTempo;
      Span s;
      s.start = 8;
      s.end = 24;
      QVERIFY(setHasClip(set, "Violin", 0, s));
      QVERIFY(!setHasClip(set, "Violin", 1, s));         // (another track there)
      QVERIFY(setHasClip(set, "Violin", -1, s));
      QVERIFY(!setHasClip(set, "Bass", 1, s));
      s.start = 0;
      s.end = 16;
      QVERIFY(setHasClip(set, "Bass", 1, s));
      // the device's float32: 24 × 2^-23 ≈ 2.9e-6 apart at most
      s.start = 8;
      s.end = float(24.000001);
      QVERIFY(setHasClip(set, "Violin", 0, s));
      s.end = 24.0001;
      QVERIFY(!setHasClip(set, "Violin", 0, s));
      // a set without tempo automation (only Live's default event): none
      const LiveSet::Set plain = LiveSet::read(writeSet(dir));
      QVERIFY(plain.tempoEvents.empty());
      QCOMPARE(int(songTempo(plain).size()), 1);
      QCOMPARE(songTempo(plain)[0].bpm, 120.0);
      }

//---------------------------------------------------------
//   clipTempoMapping
//    song beats -> clip beats: the start marker, a loop (each beat at its first pass), the end; the tempo on them
//---------------------------------------------------------

void TestLiveIntegration::clipTempoMapping()
      {
      using namespace LiveClipTempo;
      QTemporaryDir dir;
      const QString path = dir.path() + "/Song.als";
      QVERIFY(writeTempoSet(path));
      const std::vector<Point> song = songTempo(LiveSet::read(path));
      // the default 120, the points; the curve's pieces between its ends; the jump at 32
      const std::vector<LiveSet::Point> pieces = LiveSet::curve({ 20, 150 }, { 28, 90 }, 0.5, 0, 1, 0.5, CURVE_TOLERANCE_BPM);
      QVERIFY(pieces.size() > 1);
      QCOMPARE(int(song.size()), 1 + 7 + int(pieces.size()) - 1);
      QCOMPARE(tempoAt(song, 12), 135.0);
      QCOMPARE(tempoAt(song, 32, true), 90.0);
      QCOMPARE(tempoAt(song, 32), 100.0);
      QCOMPARE(tempoAt(song, 50), 100.0);
      for (const LiveSet::Point& q : pieces)
            QVERIFY(nearly(tempoAt(song, q.beat), q.value));

      // a looping clip: from beat 8 of the song, start marker 1, loop 0-4, to beat 24
      Span s;
      s.start = 8;
      s.end = 24;
      s.startMarker = 1;
      s.endMarker = 4;
      s.loopStart = 0;
      s.loopEnd = 4;
      s.looping = true;
      std::vector<Piece> p = firstPasses(s, 4);
      QCOMPARE(int(p.size()), 2);
      QCOMPARE(p[0].clipFrom, 0.0);           // (first played in the second pass: song beat 11)
      QCOMPARE(p[0].clipTo, 1.0);
      QCOMPARE(p[0].song, 11.0);
      QCOMPARE(p[1].clipFrom, 1.0);
      QCOMPARE(p[1].clipTo, 4.0);
      QCOMPARE(p[1].song, 8.0);
      std::vector<Point> clip = clipTempo(song, p);
      QCOMPARE(int(clip.size()), 4);
      QCOMPARE(clip[0].beat, 0.0);
      QCOMPARE(clip[0].bpm, 131.25);          // song 11: 120 + 30 × 3/8
      QCOMPARE(clip[1].beat, 1.0);
      QCOMPARE(clip[1].bpm, 135.0);           // song 12
      QCOMPARE(clip[2].beat, 1.0);
      QCOMPARE(clip[2].bpm, 120.0);           // song 8 (a jump in the clip's time)
      QCOMPARE(clip[3].bpm, 131.25);          // song 11

      // cut short: ends at song 10 (only beats 1-3 of the clip played)
      s.end = 10;
      p = firstPasses(s, 4);
      QCOMPARE(int(p.size()), 1);
      QCOMPARE(p[0].clipFrom, 1.0);
      QCOMPARE(p[0].clipTo, 3.0);
      clip = clipTempo(song, p);
      QCOMPARE(clip.front().beat, 0.0);       // (before: held)
      QCOMPARE(clip.front().bpm, 120.0);
      QCOMPARE(clip.back().beat, 3.0);
      QCOMPARE(clip.back().bpm, 127.5);

      // not looping, the start marker at 2: beats 2-6 at song 0-4; 0-2 never played (held), 6-8 neither
      Span n;
      n.start = 12;
      n.end = 16;
      n.startMarker = 2;
      n.endMarker = 8;
      p = firstPasses(n, 8);
      QCOMPARE(int(p.size()), 1);
      QCOMPARE(p[0].clipFrom, 2.0);
      QCOMPARE(p[0].clipTo, 6.0);
      QCOMPARE(p[0].song, 12.0);
      clip = clipTempo(song, p);
      QCOMPARE(clip.front().bpm, 135.0);
      QCOMPARE(tempoAt(clip, 1), 135.0);
      QCOMPARE(tempoAt(clip, 4), 142.5);
      QCOMPARE(tempoAt(clip, 7), 150.0);

      // the marks: a marking, "accel.", its steps every 32nd; at the jump a marking, "accel." again, its steps
      s.end = 24;
      const std::vector<Mark> m = marks(clipTempo(song, firstPasses(s, 4)), 1920);
      int texts = 0, steps = 0, words = 0;
      for (const Mark& k : m)
            ++(k.kind == Mark::TEXT ? texts : (k.kind == Mark::STEP ? steps : words));
      QCOMPARE(texts, 2);
      QCOMPARE(words, 2);
      QCOMPARE(steps, 7 + 23);
      QCOMPARE(int(m[0].kind), int(Mark::TEXT));
      QCOMPARE(m[0].tempo, 131.25 / 60);
      QCOMPARE(int(m[1].kind), int(Mark::WORD));
      QCOMPARE(m[1].text, QString("accel."));
      QCOMPARE(m[5].tick, 240);
      QCOMPARE(m[5].tempo, 133.125 / 60);
      QCOMPARE(m[9].tick, 480);
      QCOMPARE(int(m[9].kind), int(Mark::TEXT));
      QCOMPARE(m[9].tempo, 2.0);
      QCOMPARE(int(m[10].kind), int(Mark::WORD));
      QCOMPARE(m.back().tick, 1860);
      QCOMPARE(tempoText(97.5), QString("<sym>metNoteQuarterUp</sym> = 97.5"));
      QCOMPARE(tempoText(120), QString("<sym>metNoteQuarterUp</sym> = 120"));
      QCOMPARE(tempoText(133.333333), QString("<sym>metNoteQuarterUp</sym> = 133.33"));
      }

//---------------------------------------------------------
//   clipTempoScore
//    the marks in a clip score: the tempo map as Live's (ramps, a curve, a jump), the notes untouched (nothing to
//    send), values changed in place without an undo step, a new shape as one undo step once the score has edits
//---------------------------------------------------------

static Clip tempoClip(const QString& key, double end)
      {
      Clip c;
      c.key = key;
      c.track = "Violin";
      c.name = "Riff";
      c.bpm = 120;
      c.end = end;
      int id = 900;
      for (int b = 0; b < int(end); ++b)
            c.notes.push_back(ln(++id, 60 + b % 12, b, 1, 90));
      return c;
      }

void TestLiveIntegration::clipTempoScore()
      {
      using namespace LiveClipTempo;
      QTemporaryDir dir;
      const QString path = dir.path() + "/Song.als";
      QVERIFY(writeTempoSet(path));
      const std::vector<Point> song = songTempo(LiveSet::read(path));
      const Clip clip = tempoClip("c940", 16);
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const std::vector<Sig> before = signatures(score);
      std::vector<Element*> owned = tempoTexts(score);
      QCOMPARE(int(owned.size()), 1);                   // (the import's)

      // song beats 16-32 (the curve from 20 to 28) on a clip of 16 beats
      Span s;
      s.start = 16;
      s.end = 32;
      s.endMarker = 16;
      const std::vector<Point> pts = clipTempo(song, firstPasses(s, 16));
      QCOMPARE(apply(score, marks(pts, score->endTick().ticks()), &owned, false), 2);
      QVERIFY(!score->undoStack()->canUndo());
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 150.0);
      QCOMPARE(bpmAt(score, 4 * 480 - 1), 150.0);
      // each of the curve's pieces ends on its value (the tick it is rounded to)
      const std::vector<LiveSet::Point> curve = LiveSet::curve({ 20, 150 }, { 28, 90 }, 0.5, 0, 1, 0.5, CURVE_TOLERANCE_BPM);
      std::map<int, double> atTick;                     // (pieces less than a tick apart: the later one)
      for (const LiveSet::Point& c : curve)
            atTick[int(std::lround((c.beat - 16) * 480))] = c.value;
      for (const auto& c : atTick)
            QVERIFY2(nearly(bpmAt(score, c.first), c.second), qPrintable(QString("tick %1: %2, Live %3").arg(c.first).arg(bpmAt(score, c.first)).arg(c.second)));
      // between them a step every 32nd at most, each on Live's curve (its pieces, within CURVE_TOLERANCE_BPM of it),
      // held to the next (as MuseScore's own rit. / accel. lines)
      int last = -1;
      for (const Mark& k : marks(pts, score->endTick().ticks())) {
            if (k.kind == Mark::WORD)
                  continue;
            QVERIFY(nearly(bpmAt(score, k.tick), k.tempo * 60));
            // (a piece's start rounded to its tick: Live's tempo within half a tick of it)
            const double lo = tempoAt(song, 16 + (k.tick - 0.5) / 480.0), hi = tempoAt(song, 16 + (k.tick + 0.5) / 480.0);
            QVERIFY(k.tempo * 60 >= std::min(lo, hi) - 1e-9 && k.tempo * 60 <= std::max(lo, hi) + 1e-9);
            if (last >= 1920 && k.tick <= 5760)
                  QVERIFY2(k.tick - last <= STEP_TICKS, qPrintable(QString("%1 -> %2").arg(last).arg(k.tick)));
            last = k.tick;
            }
      QCOMPARE(bpmAt(score, 15 * 480), 90.0);
      int shown = 0, hidden = 0, words = 0;
      countTempo(score, &shown, &hidden, &words);
      QCOMPARE(shown, 2);                               // 150 at the start, 90 where the curve ends
      QCOMPARE(words, 1);                               // the curve read as one rit.
      int wantSteps = 0;
      for (const Mark& k : marks(pts, score->endTick().ticks()))
            wantSteps += k.kind == Mark::STEP;
      QCOMPARE(hidden, wantSteps);                      // its steps: every 32nd from each piece's start
      QVERIFY(hidden >= (5760 - 1920) / STEP_TICKS);
      // the notes are where they were: nothing to send
      QVERIFY(signatures(score) == before);
      QVERIFY(diff(match(clip, score), signatures(score)).empty());

      // the same shape, other values: in place, no undo step
      std::vector<Point> faster = pts;
      for (Point& p : faster)
            p.bpm *= 1.5;
      QCOMPARE(apply(score, marks(faster, score->endTick().ticks()), &owned, false), 1);
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 225.0);
      QCOMPARE(bpmAt(score, 15 * 480), 135.0);
      QVERIFY(!score->undoStack()->canUndo());
      QCOMPARE(apply(score, marks(faster, score->endTick().ticks()), &owned, false), 0);   // (unchanged: nothing)

      // another shape once the score has an edit: one undo step, undone to the marks before
      Note* n = noteAt(score, 0, 60);
      QVERIFY(n);
      score->startCmd();
      score->undoChangePitch(n, 61, n->tpc1() + 7, n->tpc2() + 7);
      score->endCmd();
      const std::vector<Point> flat = { { 0, 100, false } };
      QCOMPARE(apply(score, marks(flat, score->endTick().ticks()), &owned, true), 2);
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 100.0);
      QCOMPARE(bpmAt(score, 15 * 480), 100.0);
      countTempo(score, &shown, &hidden, &words);
      QCOMPARE(shown + hidden + words, 1);
      QCOMPARE(int(owned.size()), 1);
      score->undoRedo(true, nullptr);                   // the tempo's step
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 225.0);
      countTempo(score, &shown, &hidden, &words);
      QCOMPARE(hidden, wantSteps);
      QVERIFY(noteAt(score, 0, 61));                    // (the edit's step is the one before)
      delete score;
      }

//---------------------------------------------------------
//   clipTempoFollowLive
//    a session clip follows Live's tempo (/live/transport): in place, no undo step, nothing sent to Live
//---------------------------------------------------------

void TestLiveIntegration::clipTempoFollowLive()
      {
      using namespace Ms::LiveIntegration;
      LiveClipEditor* ed = LiveClipEditor::instance();
      QStringList sent;
      LiveClipEditor::setSendHook([&sent](const QByteArray& p) {
            QString address;
            QVariantList args;
            if (LiveClips::parseOsc(p, &address, &args))
                  sent << address;
            });
      Clip clip = tempoClip("c950", 8);
      clip.bpm = 96;
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      ed->songTempo(96);
      ed->edit(clip, score);
      ed->received("/live/clip/where", { clip.key, 0, 2 });     // a session clip
      QCOMPARE(int(ed->tempoFrom(score)), int(LiveClipEditor::TempoFrom::LIVE));
      ed->songTempo(97.5);
      ed->applyPendingTempos();
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 97.5);
      QCOMPARE(bpmAt(score, 7 * 480), 97.5);
      const std::vector<Element*> texts = LiveClipTempo::tempoTexts(score);
      QCOMPARE(int(texts.size()), 1);
      QVERIFY(toTempoText(texts[0])->xmlText().endsWith("= 97.5"));
      QVERIFY(!score->undoStack()->canUndo());
      QVERIFY(ed->tempoText(score).contains("97.5"));
      QTest::qWait(500);                                // (the edit debounce: nothing to write)
      QVERIFY2(!sent.contains("/ms/clip/write"), qPrintable(sent.join(" ")));
      QVERIFY(ed->inSync(score));
      ed->scoreClosed(score);
      LiveClipEditor::setSendHook(nullptr);
      delete score;
      }

//---------------------------------------------------------
//   clipTempoArrangement
//    an arrangement clip: its set found through Live's Log.txt (a set without the clip passed over), the song's
//    tempo automation under it, read again when the set is saved; not found: Live's tempo and the reason
//---------------------------------------------------------

void TestLiveIntegration::clipTempoArrangement()
      {
      using namespace Ms::LiveIntegration;
      LiveClipEditor* ed = LiveClipEditor::instance();
      QSettings().remove(TEMPO_SETS);
      QTemporaryDir dir;
      const QString path = dir.path() + "/Song Project/Song.als";
      QVERIFY(writeTempoSet(path));
      const QString other = writeSet(dir);              // (another set, opened later in Live: without the clip)
      const QString prefs = dir.path() + "/Ableton/Live 12.4.6/Preferences";
      QDir().mkpath(prefs);
      {
      QFile log(prefs + "/Log.txt");
      QVERIFY(log.open(QIODevice::WriteOnly));
      log.write(QString("2026-10-03T11:35:09.696048: info: Loading document \"%1\"\n"
                        "2026-10-03T11:35:12.660148: info: Loaded document was created by Ableton Live 12.4.6\n"
                        "2026-10-03T11:36:01.000000: info: Loading document \"C:\\ProgramData\\Ableton\\Live 12 Trial\\Resources\\Core Library\\Defaults\\Creating Tracks/MIDI Track\\Default MIDI Track.als\"\n"
                        "2026-10-03T11:37:54.611976: info: Loading document \"%2\"\n")
                .arg(QDir::toNativeSeparators(path), QDir::toNativeSeparators(other)).toUtf8());
      }
      LiveClipEditor::setLivePrefsBases({ dir.path() + "/Ableton" });
      LiveClipEditor::setSearchInline(true);
      QStringList sent;
      LiveClipEditor::setSendHook([&sent](const QByteArray& p) {
            QString address;
            QVariantList args;
            if (LiveClips::parseOsc(p, &address, &args))
                  sent << address;
            });

      const Clip clip = tempoClip("c960", 4);
      MasterScore* score = importClip(clip, nullptr);
      QVERIFY(score);
      const std::vector<Sig> before = signatures(score);
      ed->edit(clip, score);
      ed->received("/live/clip/where", { clip.key, 0, -1 });
      ed->received("/live/clip/span", { clip.key, 8.0, 24.0, 1.0, 4.0, 0.0, 4.0, 1 });
      QCOMPARE(int(ed->tempoFrom(score)), int(LiveClipEditor::TempoFrom::SET));
      QCOMPARE(ed->tempoSet(score), path);
      QCOMPARE(LiveClipEditor::rememberedSets(), QStringList({ path }));
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 131.25);                // clip beat 0: song beat 11 (the loop's second pass)
      QCOMPARE(bpmAt(score, 240), 133.125);
      QCOMPARE(bpmAt(score, 480), 120.0);               // clip beat 1: song beat 8
      QCOMPARE(bpmAt(score, 1200), 125.625);
      QVERIFY(signatures(score) == before);
      QVERIFY(!score->undoStack()->canUndo());
      QVERIFY(ed->tempoText(score).contains("automation"));
      // drawn on the band staves (the top one, track 0's, is hidden where no note falls on it): the markings and
      // words are on the page; MS_CLIP_TEMPO_PNG=<file>: a picture to look at
      if (!qEnvironmentVariableIsEmpty("MS_CLIP_TEMPO_PNG"))
            QVERIFY(renderPng(score, qEnvironmentVariable("MS_CLIP_TEMPO_PNG")));
      {
      score->doLayout();
      Page* page = score->pages().front();
      int drawnTexts = 0, drawnWords = 0;
      for (const Element* e : page->items(page->abbox())) {
            if (e->isTempoText() && e->visible() && !e->bbox().isEmpty())
                  ++drawnTexts;
            if (e->isSystemText() && !e->bbox().isEmpty())
                  ++drawnWords;
            }
      QCOMPARE(drawnTexts, 2);
      QCOMPARE(drawnWords, 2);
      }
      // Live's tempo now doesn't change it (the song's automation does)
      ed->songTempo(70);
      ed->applyPendingTempos();
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 131.25);

      // Live saves the set: the ramp now goes to 180 at beat 16
      QVERIFY(writeTempoSet(path, "Time=\"16\" Value=\"150\"", "Time=\"16\" Value=\"180\""));
      ed->setSaved(path);
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 142.5);                 // 120 + 60 × 3/8
      QCOMPARE(bpmAt(score, 480), 120.0);
      QVERIFY(!score->undoStack()->canUndo());          // (values only: in place)
      QTest::qWait(500);
      QVERIFY2(!sent.contains("/ms/clip/write"), qPrintable(sent.join(" ")));

      // the clip moved in Live to where no saved set has it: Live's tempo, the reason said
      ed->received("/live/clip/span", { clip.key, 100.0, 116.0, 1.0, 4.0, 0.0, 4.0, 1 });
      QCOMPARE(int(ed->tempoFrom(score)), int(LiveClipEditor::TempoFrom::NO_SET));
      ed->applyPendingTempos();
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 70.0);
      QCOMPARE(bpmAt(score, 1200), 70.0);
      QVERIFY(ed->tempoText(score, nullptr).contains("not found"));
      QString details;
      ed->tempoText(score, &details);
      QVERIFY(details.contains("save the set in Live"));
      // chosen by hand (the set saved meanwhile with the clip there): found
      QVERIFY(writeTempoSet(path, "<CurrentStart Value=\"8\" />\n\t\t\t\t\t\t\t<CurrentEnd Value=\"24\" />",
                            "<CurrentStart Value=\"100\" />\n\t\t\t\t\t\t\t<CurrentEnd Value=\"116\" />"));
      ed->useSet(score, path);
      QCOMPARE(int(ed->tempoFrom(score)), int(LiveClipEditor::TempoFrom::SET));
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 100.0);                 // song beat 103: after the jump at 32
      // moved into a session slot: Live's tempo
      ed->received("/live/clip/where", { clip.key, 0, 3 });
      QCOMPARE(int(ed->tempoFrom(score)), int(LiveClipEditor::TempoFrom::LIVE));
      ed->applyPendingTempos();
      score->doLayout();
      QCOMPARE(bpmAt(score, 0), 70.0);

      ed->scoreClosed(score);
      LiveClipEditor::setSendHook(nullptr);
      LiveClipEditor::setSearchInline(false);
      LiveClipEditor::setLivePrefsBases(QStringList());
      QSettings().remove(TEMPO_SETS);
      delete score;
      }

//---------------------------------------------------------
//   clipTempoLiveLists
//    the sets Live lists: Log.txt's loaded documents (the latest first, Live's own left out), Preferences.cfg's
//    RecentDocsList (UTF-16, as Live 12.4.6 writes it), each Live version's folder, newest first
//---------------------------------------------------------

static QByteArray utf16Entry(const QString& s)
      {
      QByteArray b;
      const quint32 n = quint32(s.size());
      b.append(char(n & 0xff)).append(char((n >> 8) & 0xff)).append(char(0)).append(char(0));
      for (const QChar c : s)
            b.append(char(c.unicode() & 0xff)).append(char(c.unicode() >> 8));
      b.append(char(1)).append(QByteArray(31, 0));
      return b;
      }

void TestLiveIntegration::clipTempoLiveLists()
      {
      using namespace LiveClipTempo;
      const QByteArray log =
            "2026-10-03T11:35:09: info: Loading document \"C:\\claude\\a\\One.als\"\n"
            "2026-10-03T11:36:01: info: Loading document \"C:\\ProgramData\\Ableton\\Live 12 Trial\\Resources\\Core Library\\Defaults\\Creating Tracks/MIDI Track\\Default MIDI Track.als\"\n"
            "2026-10-03T11:37:54: info: Loading document \"C:\\claude\\b\\Two.als\"\n"
            "2026-10-03T11:38:00: info: Loading document \"C:\\claude\\a\\One.als\"\n";
      QCOMPARE(documentsFromLog(log), QStringList({ "C:/claude/a/One.als", "C:/claude/b/Two.als" }));
      // (the layout seen in Live 12.4.6's Preferences.cfg: the key, a count, "FileRef", each path's length and UTF-16)
      QByteArray cfg = QByteArray::fromHex("000e") + QByteArray("RecentDocsList") + QByteArray::fromHex("08000000170000000007")
                       + QByteArray("FileRef") + QByteArray(12, 0) + utf16Entry("C:/claude/b/Two.als")
                       + QByteArray::fromHex("07") + QByteArray("FileRef") + QByteArray(12, 0)
                       + utf16Entry(QString("C:/Users/me/Música/Três.als"));
      QCOMPARE(documentsFromPreferences(cfg), QStringList({ "C:/claude/b/Two.als", QString("C:/Users/me/Música/Três.als") }));

      QTemporaryDir dir;
      const QString base = dir.path() + "/Ableton";
      const QString a = dir.path() + "/sets/A.als";
      const QString b = dir.path() + "/sets/B.als";
      QDir().mkpath(dir.path() + "/sets");
      for (const QString& p : { a, b }) {
            QFile f(p);
            QVERIFY(f.open(QIODevice::WriteOnly));
            }
      auto write = [](const QString& p, const QByteArray& data) {
            QDir().mkpath(QFileInfo(p).path());
            QFile f(p);
            f.open(QIODevice::WriteOnly);
            f.write(data);
            };
      write(base + "/Live 12.2/Preferences/Log.txt", QString("info: Loading document \"%1\"\n").arg(a).toUtf8());
      write(base + "/Live 12.4.6/Preferences/Log.txt", QString("info: Loading document \"%1\"\n"
                                                               "info: Loading document \"%2/sets/Gone.als\"\n").arg(b, dir.path()).toUtf8());
      write(base + "/Live 12.4.6/Preferences/Preferences.cfg", utf16Entry(a));
      QDir().mkpath(base + "/Live Reports");
      // 12.4.6 first (its log, then its recent list), then 12.2; files that don't exist left out
      QCOMPARE(setCandidates({ base }), QStringList({ b, a }));
      }

QTEST_MAIN(TestLiveIntegration)
#include "tst_liveintegration.moc"
