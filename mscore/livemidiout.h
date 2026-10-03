//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __MSCORE_LIVEMIDIOUT_H__
#define __MSCORE_LIVEMIDIOUT_H__

//---------------------------------------------------------
//   A clip tab plays through its Live track (LIVE.md › Editing Live clips in MuseScore › Playback; the protocol:
//   liveclipmodel.h). The sequencer (audio thread) posts each MIDI message with the moment it is due: the start
//   of the period being computed plus the event's frame in it, so the messages keep their spacing within a
//   period (else they would leave together, up to a period apart: 5-23 ms). A thread of its own sends each one
//   as /ms/midi to the MuseScore Link device (UDP on 127.0.0.1, the link's port) at that moment. The audio thread
//   never allocates or touches a socket here: a fixed ring, a short lock.
//   MuseScore's cursor is drawn from the same play position, so the notes leave in step with it; Live plays them
//   after its own output latency plus the hop through Max (a few ms).
//---------------------------------------------------------

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "liveclipmodel.h"

namespace Ms {
namespace LiveIntegration {

class LiveMidiOut {
   public:
      using Clock = std::chrono::steady_clock;

   private:
      struct Item {
            Clock::time_point due;
            int track;
            LiveClipEdit::MidiMsg msg;
            };
      static constexpr int RING = 4096;
      Item _ring[RING];
      int _head { 0 };                    // the next to send
      int _count { 0 };
      std::mutex _mutex;
      std::condition_variable _wake;
      std::thread _thread;
      std::atomic<int> _port { 9001 };
      std::atomic<int> _sent { 0 };
      std::atomic<int> _dropped { 0 };
      std::atomic<int> _maxLateUs { 0 };  // the latest a message left after it was due

      LiveMidiOut();
      void run();

   public:
      static LiveMidiOut* instance();     // (never deleted: its thread waits until the process ends)
      void setPort(int port) { _port = port; }
      // any thread (the audio thread): send m to the device for track at due
      void post(int track, const LiveClipEdit::MidiMsg& m, Clock::time_point due);
      int sent() const { return _sent; }
      int dropped() const { return _dropped; }
      double maxLateMs() const { return _maxLateUs / 1000.0; }
      };

}     // namespace LiveIntegration
}     // namespace Ms
#endif
