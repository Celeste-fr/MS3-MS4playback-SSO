//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __MIDISYNC_H__
#define __MIDISYNC_H__

//---------------------------------------------------------
//   MIDI sync out: MuseScore as the master of a DAW (the owner, 2026-09-28: play through Ableton
//   Live 12, which hosts the library and draws the automation; see LIVE.md).
//
//   Ableton Link can't set a song position, so it is MIDI clock: 24 clocks per quarter note that
//   follow the actual playback tempo (the tempo map with the Play Panel's relative tempo: time
//   comes from the same utick -> seconds as the notes), Song Position Pointer (SPP, in 16ths) and
//   Start / Continue / Stop. Live follows both when Sync is on for that MIDI input port, jumps
//   included.
//
//   - start(utick): playback starts there. At 0: Start. Else SPP of the next 16th at or after it,
//     then Continue: a slave starts on the first clock after Continue, at the SPP's position, so
//     the first clock is sent when playback reaches that 16th (at most a 16th after the start; the
//     notes, played by MuseScore, are not late for it: only the DAW's automation starts then).
//   - run(until): the clocks due before `until` (seconds of playback), one every 20 ticks.
//   - locate(utick): a jump (seek, loop) while running: Stop, SPP, Continue (the MIDI spec: SPP
//     only while stopped); while stopped, SPP alone (the DAW's cursor follows).
//   - stop(): Stop.
//   Every message carries the playback time it is due at; the sequencer (Seq::process) turns it
//   into a frame of its period and PortMidi's timestamp, as it does for the notes.
//
//   Header only (templates over the output and the tempo function): used in the audio thread,
//   no allocation there. schedule() is the pure function the tests use.
//---------------------------------------------------------

#include <functional>
#include <vector>
#include <QString>

namespace Ms {
namespace MidiSync {

constexpr int DIVISION_TICKS    = 480;          // (Constant::division)
constexpr int CLOCKS_PER_BEAT   = 24;           // MIDI clock: per quarter note
constexpr int TICKS_PER_CLOCK   = DIVISION_TICKS / CLOCKS_PER_BEAT;    // 20
constexpr int TICKS_PER_SPP     = DIVISION_TICKS / 4;                  // a 16th: 120
constexpr int MAX_SPP           = 16383;        // 14 bits

enum Status : int {
      SONGPOS  = 0xF2,
      CLOCK    = 0xF8,
      START    = 0xFA,
      CONTINUE = 0xFB,
      STOP     = 0xFC,
      };

struct Message {
      double seconds { 0 };         // playback time it is due at
      int status { CLOCK };
      int value { 0 };              // SONGPOS: 16ths from the start; CLOCK: the clock's utick
      bool operator==(const Message& o) const { return seconds == o.seconds && status == o.status && value == o.value; }
      };

QString toString(const Message& m);

//---------------------------------------------------------
//   Clock
//---------------------------------------------------------

class Clock {
      int _next { -1 };             // utick of the next clock; -1: not running

   public:
      bool running() const { return _next >= 0; }
      int nextClock() const { return _next; }

      static int sppOf(int utick)
            {
            const int spp = (utick + TICKS_PER_SPP - 1) / TICKS_PER_SPP;
            return spp < 0 ? 0 : (spp > MAX_SPP ? MAX_SPP : spp);
            }

      template <class Out> void start(int utick, double now, Out out)
            {
            if (utick <= 0) {
                  out(Message { now, START, 0 });
                  _next = 0;
                  return;
                  }
            const int spp = sppOf(utick);
            out(Message { now, SONGPOS, spp });
            out(Message { now, CONTINUE, 0 });
            _next = spp * TICKS_PER_SPP;
            }

      template <class Out> void stop(double now, Out out)
            {
            if (!running())
                  return;
            out(Message { now, STOP, 0 });
            _next = -1;
            }

      template <class Out> void locate(int utick, double now, Out out)
            {
            if (!running()) {
                  out(Message { now, SONGPOS, sppOf(utick) });
                  return;
                  }
            stop(now, out);
            start(utick, now, out);
            }

      // clocks due before `until`; utick2seconds: the playback time of an unrolled tick
      template <class Time, class Out> void run(double until, const Time& utick2seconds, Out out)
            {
            while (_next >= 0) {
                  const double t = utick2seconds(_next);
                  if (!(t < until))
                        break;
                  out(Message { t, CLOCK, _next });
                  _next += TICKS_PER_CLOCK;
                  }
            }
      };

//---------------------------------------------------------
//   schedule
//    what a play from startUtick sends until endSeconds, run in periods of periodSeconds as the
//    sequencer does (tests)
//---------------------------------------------------------

std::vector<Message> schedule(const std::function<double(int)>& utick2seconds, int startUtick, double endSeconds,
                              double periodSeconds = 0.01);

}     // namespace MidiSync
}     // namespace Ms
#endif
