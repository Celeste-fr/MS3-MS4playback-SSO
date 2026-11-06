//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  RenderAhead: playback's audio computed ahead of the audio callback (branch render-ahead, 2026-10-31; the owner
//  approved). Seq's header (seq.h › Render ahead) has the design; this is its engine, Seq-free so a test can drive it.
//
//    producer    one thread (realtime priority: MMCSS "Pro Audio" on Windows, SCHED_FIFO where allowed, as the helper
//                processes' audio threads) renders blocks with the producer function (Seq::processDirect: every synth
//                and plug-in is processed on this thread only) into a ring of frames, each block with a record
//                (Block: may it be heard later than rendered, was what came before it stale, its play position, a
//                message for the GUI to send when it is heard)
//    lead        while the blocks say lead (playing), the producer keeps up to leadMs of audio queued; the callback
//                only copies. A block not to be heard late (stopped: clicked notes, MIDI input) is rendered when the
//                callback asks for it and waits for it (sync): no lead, as direct
//    prefill     the first lead block (Play's start, a seek while playing) is heard once leadMs of audio is queued
//                (or the lead ended): Play's start waits up to the time it takes to render leadMs
//    flush       a block with flush drops every frame before it unheard (a seek, Stop: the GUI messages of the dropped
//                blocks are still sent, in order)
//    underrun    the callback finds less than it needs while leading: the rest silence (never a repeat), counted
//                (underruns()), and prefill again, so that the lead builds up once more
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __RENDERAHEAD_H__
#define __RENDERAHEAD_H__

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace Ms {

//---------------------------------------------------------
//   AudioCapture
//    what the callback outputs while playback is heard playing (MS_PLAY_CAPTURE, a test tool: playcapture.h),
//    into memory taken beforehand; written out by the GUI thread once playback stopped
//---------------------------------------------------------

struct AudioCapture {
      std::vector<float> data;                  // interleaved stereo
      std::atomic<size_t> frames { 0 };
      std::atomic<bool> overflow { false };
      void append(const float* p, unsigned n)
            {
            const size_t f = frames.load(std::memory_order_relaxed);
            const size_t room = data.size() / 2 - f;
            const size_t k = n < room ? n : room;
            for (size_t i = 0; i < 2 * k; ++i)
                  data[2 * f + i] = p[i];
            if (k < n)
                  overflow = true;
            frames.store(f + k, std::memory_order_release);
            }
      void appendSilence(unsigned n)
            {
            const size_t f = frames.load(std::memory_order_relaxed);
            const size_t room = data.size() / 2 - f;
            const size_t k = n < room ? n : room;
            for (size_t i = 0; i < 2 * k; ++i)
                  data[2 * f + i] = 0.f;
            if (k < n)
                  overflow = true;
            frames.store(f + k, std::memory_order_release);
            }
      };

class RenderAhead {
   public:
      // what the producer function says about the block it rendered
      struct Block {
            bool lead { false };          // may be heard later than rendered (playing, nothing leaving MuseScore by MIDI)
            bool flush { false };         // the frames rendered before it are stale: dropped unheard
            bool hold { false };          // nothing rendered (waiting for the GUI: a loop's restart): not queued
            bool playing { false };       // heard as playback (the cursor follows frame; captured)
            bool countIn { false };
            int frame { 0 };              // its first frame's play position (Seq's playFrame)
            int guiMsg { 0 };             // a message for the GUI, sent when the block begins to be heard (0: none)
            int guiArg { 0 };
            };
      using Produce = std::function<Block(unsigned frames, float* out)>;
      using Attention = std::function<bool()>;  // something waits for the producer (a seek, a stop, a swap): render now
      using Deliver = std::function<void(int msg, int arg)>;  // a block's guiMsg (the callback's thread)

      RenderAhead();
      ~RenderAhead();
      RenderAhead(const RenderAhead&) = delete;
      RenderAhead& operator=(const RenderAhead&) = delete;

      // the producer thread started (leadMs > 0): returns false (nothing started) for leadMs <= 0
      bool start(int leadMs, int sampleRate, Produce produce, Attention attention, Deliver deliver);
      void stop();                              // joins the thread; the ring emptied
      bool running() const                      { return _running.load(std::memory_order_acquire); }
      int leadMs() const                        { return _leadMs; }

      // the audio callback: frames of finished audio (or silence); the capture, when given, gets what is heard
      // playing. Returns true when this callback had an underrun while playback was heard
      bool process(unsigned frames, float* out, AudioCapture* capture = nullptr);
      void poke();                              // (any thread) wake the producer

      // (any thread) what the callback has output: the play position heard, playing, in the count-in
      int heardFrame() const                    { return _heardFrame.load(std::memory_order_acquire); }
      bool heardPlaying() const                 { return _heardPlaying.load(std::memory_order_acquire); }
      bool heardCountIn() const                 { return _heardCountIn.load(std::memory_order_acquire); }
      int queuedFrames() const;                 // rendered, not yet heard
      unsigned underruns() const                { return _underruns.load(std::memory_order_relaxed); }
      // heard play positions that don't follow the one before (a seek, a loop's restart, a play's start): the GUI
      // keeps a seek's marks until it is heard
      unsigned heardJumps() const               { return _jumps.load(std::memory_order_acquire); }

      // a test's stall (MS_RENDER_AHEAD_STALL "<s>:<ms>"): the producer sleeps ms once, at the first lead block
      // whose play position reaches s seconds
      void setStall(double atSeconds, int ms)   { _stallAt = atSeconds; _stallMs = ms; _stalled = false; }

   private:
      struct Record {
            uint64_t start { 0 };         // ring index of its first frame
            unsigned frames { 0 };
            Block block;
            };
      void producerLoop();
      void setRealtimePriority();
      void deliverUpTo(uint64_t rec);
      unsigned blockFrames() const;

      std::vector<float> _ring;                 // interleaved stereo, _capacity frames
      uint64_t _capacity { 0 };
      std::vector<Record> _records;
      static constexpr uint64_t RECORDS = 8192;
      std::vector<float> _scratch;

      // indexes: monotonic, ring position = index % capacity
      std::atomic<uint64_t> _write { 0 };       // frames written (producer)
      std::atomic<uint64_t> _read { 0 };        // frames read or skipped (callback)
      std::atomic<uint64_t> _recWrite { 0 };    // records written (producer)
      std::atomic<uint64_t> _recRead { 0 };     // records fully read (callback)
      uint64_t _recDelivered { 0 };             // (callback) records whose message was sent
      uint64_t _flushStart { 0 };               // (producer) the last flush block's start

      std::atomic<uint32_t> _reqSeq { 0 };      // the callback asks for a block (sync)
      std::atomic<uint32_t> _doneSeq { 0 };
      std::atomic<bool> _producerLead { false }; // the last block written was a lead block
      std::atomic<unsigned> _block { 256 };     // the callback's frames: the producer's block

      std::mutex _mutex;
      std::condition_variable _wakeProducer;
      std::condition_variable _wakeCallback;
      std::atomic<bool> _quit { false };
      std::atomic<bool> _running { false };
      std::thread _thread;

      Produce _produce;
      Attention _attention;
      Deliver _deliver;
      int _leadMs { 0 };
      int _sampleRate { 48000 };
      uint64_t _leadFrames { 0 };

      // callback state
      bool _inLead { false };
      bool _prefill { false };
      bool _capturing { false };
      std::atomic<int> _heardFrame { 0 };
      std::atomic<bool> _heardPlaying { false };
      std::atomic<bool> _heardCountIn { false };
      std::atomic<unsigned> _underruns { 0 };
      std::atomic<unsigned> _jumps { 0 };
      bool _prevPlaying { false };
      int _expectFrame { 0 };

      double _stallAt { -1 };
      int _stallMs { 0 };
      bool _stalled { false };
      };

} // namespace Ms
#endif
