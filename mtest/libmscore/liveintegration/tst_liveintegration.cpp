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

#include "libmscore/midisync.h"
#include "libmscore/tempo.h"
#include "mtest/testutils.h"

#define DIR QString("libmscore/liveintegration/")

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
      };


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


QTEST_MAIN(TestLiveIntegration)
#include "tst_liveintegration.moc"
