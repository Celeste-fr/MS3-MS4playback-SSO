//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  RenderAhead (renderahead.h has the design)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "renderahead.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace Ms {

static constexpr unsigned MIN_BLOCK = 16;
static constexpr unsigned MAX_BLOCK = 8192;

RenderAhead::RenderAhead()
      {
      }

RenderAhead::~RenderAhead()
      {
      stop();
      }

//---------------------------------------------------------
//   start / stop
//---------------------------------------------------------

bool RenderAhead::start(int leadMs, int sampleRate, Produce produce, Attention attention, Deliver deliver)
      {
      stop();
      if (leadMs <= 0 || sampleRate <= 0 || !produce)
            return false;
      _leadMs = leadMs;
      _sampleRate = sampleRate;
      _leadFrames = uint64_t(leadMs) * uint64_t(sampleRate) / 1000;
      // (the lead, room for a block written after a flush while the callback hasn't skipped yet, and a callback's worth)
      _capacity = _leadFrames + 4 * MAX_BLOCK;
      _ring.assign(size_t(_capacity) * 2, 0.f);
      _records.assign(size_t(RECORDS), Record());
      _scratch.assign(size_t(MAX_BLOCK) * 2, 0.f);
      _write = 0;
      _read = 0;
      _recWrite = 0;
      _recRead = 0;
      _recDelivered = 0;
      _flushStart = 0;
      _reqSeq = 0;
      _doneSeq = 0;
      _producerLead = false;
      _inLead = false;
      _prefill = false;
      _capturing = false;
      _heardPlaying = false;
      _heardCountIn = false;
      _underruns = 0;
      _prevPlaying = false;
      _produce = std::move(produce);
      _attention = std::move(attention);
      _deliver = std::move(deliver);
      _quit = false;
      _thread = std::thread([this]() { producerLoop(); });
      _running.store(true, std::memory_order_release);
      return true;
      }

void RenderAhead::stop()
      {
      if (!_thread.joinable())
            return;
      _running.store(false, std::memory_order_release);
      {
            std::lock_guard<std::mutex> lock(_mutex);
            _quit = true;
      }
      _wakeProducer.notify_all();
      _wakeCallback.notify_all();
      _thread.join();
      _ring.clear();
      _records.clear();
      _write = 0;
      _read = 0;
      _recWrite = 0;
      _recRead = 0;
      _recDelivered = 0;
      _heardPlaying = false;
      }

void RenderAhead::poke()
      {
      _wakeProducer.notify_one();
      }

int RenderAhead::queuedFrames() const
      {
      return int(_write.load(std::memory_order_acquire) - _read.load(std::memory_order_acquire));
      }

unsigned RenderAhead::blockFrames() const
      {
      return std::min(MAX_BLOCK, std::max(MIN_BLOCK, _block.load(std::memory_order_relaxed)));
      }

//---------------------------------------------------------
//   setRealtimePriority
//    as the helper processes' audio thread (vst3remote.cpp › Child::audioLoop) and PortAudio's own callback
//---------------------------------------------------------

void RenderAhead::setRealtimePriority()
      {
#ifdef _WIN32
      if (HMODULE avrt = LoadLibraryW(L"avrt.dll")) {
            using Fn = HANDLE (WINAPI*)(LPCWSTR, LPDWORD);
            if (auto f = reinterpret_cast<Fn>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"))) {
                  DWORD index = 0;
                  f(L"Pro Audio", &index);
                  }
            }
#else
      // (realtime where the system allows it; else as it is)
      sched_param sp {};
      sp.sched_priority = 10;
      pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
#endif
      }

//---------------------------------------------------------
//   producerLoop
//    renders a block when the callback asks for one (sync), or, while the blocks lead, until leadMs are queued, and
//    at once when something waits for it (a seek, a stop): never more than the ring holds
//---------------------------------------------------------

void RenderAhead::producerLoop()
      {
      setRealtimePriority();
      using Clock = std::chrono::steady_clock;
      Clock::time_point holdUntil = Clock::time_point::min();
      while (!_quit.load(std::memory_order_acquire)) {
            const uint64_t w = _write.load(std::memory_order_relaxed);
            const uint64_t r = _read.load(std::memory_order_acquire);
            const uint64_t rw = _recWrite.load(std::memory_order_relaxed);
            const uint64_t rr = _recRead.load(std::memory_order_acquire);
            const unsigned block = blockFrames();
            const uint32_t req = _reqSeq.load(std::memory_order_acquire);
            const bool sync = req != _doneSeq.load(std::memory_order_relaxed);
            const bool lead = _producerLead.load(std::memory_order_relaxed);
            const uint64_t queued = w - std::max(r, _flushStart);
            bool need = sync;
            if (!need && lead) {
                  if (queued < _leadFrames && Clock::now() >= holdUntil)
                        need = true;
                  else if (_attention && _attention())
                        need = true;
                  }
            const bool room = w + block - r <= _capacity && rw + 1 - rr <= RECORDS;
            if (need && room) {
                  Block b = _produce(block, _scratch.data());
                  if (b.hold) {
                        holdUntil = Clock::now() + std::chrono::milliseconds(20);
                        if (sync) {
                              {
                                    std::lock_guard<std::mutex> lock(_mutex);
                                    _doneSeq.store(req, std::memory_order_release);
                              }
                              _wakeCallback.notify_all();
                              }
                        std::unique_lock<std::mutex> lock(_mutex);
                        _wakeProducer.wait_for(lock, std::chrono::milliseconds(1));
                        continue;
                        }
                  holdUntil = Clock::time_point::min();
                  if (b.lead && _stallAt >= 0 && !_stalled && b.frame >= _stallAt * _sampleRate) {
                        _stalled = true;
                        std::this_thread::sleep_for(std::chrono::milliseconds(_stallMs));
                        }
                  if (b.flush)
                        _flushStart = w;
                  for (unsigned i = 0; i < block; ++i) {
                        const size_t at = size_t((w + i) % _capacity) * 2;
                        _ring[at]     = _scratch[2 * i];
                        _ring[at + 1] = _scratch[2 * i + 1];
                        }
                  Record& rec = _records[size_t(rw % RECORDS)];
                  rec.start = w;
                  rec.frames = block;
                  rec.block = b;
                  _write.store(w + block, std::memory_order_release);
                  _recWrite.store(rw + 1, std::memory_order_release);
                  _producerLead.store(b.lead, std::memory_order_release);
                  if (sync) {
                        {
                              std::lock_guard<std::mutex> lock(_mutex);
                              _doneSeq.store(req, std::memory_order_release);
                        }
                        _wakeCallback.notify_all();
                        }
                  continue;
                  }
            std::unique_lock<std::mutex> lock(_mutex);
            if (_quit.load(std::memory_order_relaxed))
                  break;
            if (_reqSeq.load(std::memory_order_acquire) != _doneSeq.load(std::memory_order_relaxed) && room)
                  continue;
            _wakeProducer.wait_for(lock, std::chrono::milliseconds(2));
            }
      }

//---------------------------------------------------------
//   deliverUpTo
//    (callback) the messages of the records before rec, each once, in order
//---------------------------------------------------------

void RenderAhead::deliverUpTo(uint64_t rec)
      {
      for (; _recDelivered < rec; ++_recDelivered) {
            const Block& b = _records[size_t(_recDelivered % RECORDS)].block;
            if (b.guiMsg && _deliver)
                  _deliver(b.guiMsg, b.guiArg);
            }
      }

//---------------------------------------------------------
//   process
//    the audio callback
//---------------------------------------------------------

bool RenderAhead::process(unsigned frames, float* out, AudioCapture* capture)
      {
      _block.store(frames, std::memory_order_relaxed);
      bool underrun = false;
      // a flush: every record before the last flush block dropped (their messages sent)
      {
            const uint64_t rw = _recWrite.load(std::memory_order_acquire);
            uint64_t rr = _recRead.load(std::memory_order_relaxed);
            uint64_t last = rw;
            for (uint64_t k = rr; k < rw; ++k)
                  if (_records[size_t(k % RECORDS)].block.flush)
                        last = k;
            if (last < rw) {
                  const Record& f = _records[size_t(last % RECORDS)];
                  if (last > rr || _read.load(std::memory_order_relaxed) < f.start) {
                        deliverUpTo(last);
                        _read.store(f.start, std::memory_order_release);
                        _recRead.store(last, std::memory_order_release);
                        rr = last;
                        if (f.block.lead) {
                              _inLead = true;
                              _prefill = true;
                              }
                        }
                  }
      }
      unsigned pos = 0;
      bool asked = false;
      auto silence = [&](unsigned n) {
            std::memset(out + 2 * pos, 0, sizeof(float) * 2 * n);
            if (_capturing && capture)
                  capture->appendSilence(n);
            pos += n;
            };
      while (pos < frames) {
            const uint64_t rw = _recWrite.load(std::memory_order_acquire);
            const uint64_t rr = _recRead.load(std::memory_order_relaxed);
            if (rr == rw) {
                  if (_inLead && _producerLead.load(std::memory_order_acquire)) {
                        // (the producer too slow for longer than the lead: silence, never a repeat; the lead builds again)
                        if (_capturing)
                              underrun = true;
                        _prefill = true;
                        silence(frames - pos);
                        break;
                        }
                  if (!asked) {
                        // stopped (or the lead ended): this block is rendered now, as the callback would have
                        asked = true;
                        const uint32_t req = _reqSeq.fetch_add(1, std::memory_order_acq_rel) + 1;
                        _wakeProducer.notify_one();
                        const auto budget = std::chrono::microseconds(int64_t(900000.0 * frames / std::max(1, _sampleRate)));
                        std::unique_lock<std::mutex> lock(_mutex);
                        _wakeCallback.wait_for(lock, budget, [&]() {
                              return _quit.load(std::memory_order_relaxed)
                                     || int32_t(_doneSeq.load(std::memory_order_acquire) - req) >= 0;
                              });
                        continue;
                        }
                  if (_capturing)
                        underrun = true;
                  silence(frames - pos);
                  break;
                  }
            const Record& rec = _records[size_t(rr % RECORDS)];
            if (rec.block.lead && !_inLead) {
                  _inLead = true;
                  _prefill = true;
                  }
            else if (!rec.block.lead) {
                  _inLead = false;
                  _prefill = false;
                  }
            if (_inLead && _prefill) {
                  const uint64_t w = _write.load(std::memory_order_acquire);
                  const uint64_t q = w - _read.load(std::memory_order_relaxed);
                  bool leadEnds = false;
                  for (uint64_t k = rr; k < rw && !leadEnds; ++k)
                        leadEnds = !_records[size_t(k % RECORDS)].block.lead;
                  if (q >= _leadFrames || leadEnds || q + 2 * MAX_BLOCK >= _capacity)
                        _prefill = false;
                  else {
                        silence(frames - pos);
                        break;
                        }
                  }
            uint64_t rd = _read.load(std::memory_order_relaxed);
            if (rd == rec.start)
                  deliverUpTo(rr + 1);
            const unsigned n = unsigned(std::min<uint64_t>(frames - pos, rec.start + rec.frames - rd));
            for (unsigned i = 0; i < n; ++i) {
                  const size_t at = size_t((rd + i) % _capacity) * 2;
                  out[2 * (pos + i)]     = _ring[at];
                  out[2 * (pos + i) + 1] = _ring[at + 1];
                  }
            if (rd == rec.start) {
                  const bool follows = rec.block.playing && !rec.block.countIn;
                  if (follows && (!_prevPlaying || rec.block.frame != _expectFrame))
                        _jumps.fetch_add(1, std::memory_order_acq_rel);
                  _prevPlaying = follows;
                  _expectFrame = rec.block.frame + int(rec.frames);
                  }
            if (rec.block.playing) {
                  _capturing = true;
                  _heardFrame.store(rec.block.frame + int(rd + n - rec.start), std::memory_order_release);
                  _heardCountIn.store(rec.block.countIn, std::memory_order_release);
                  _heardPlaying.store(true, std::memory_order_release);
                  }
            else if (_capturing || _heardPlaying.load(std::memory_order_relaxed)) {
                  _capturing = false;
                  _heardPlaying.store(false, std::memory_order_release);
                  }
            if (_capturing && capture)
                  capture->append(out + 2 * pos, n);
            pos += n;
            rd += n;
            _read.store(rd, std::memory_order_release);
            if (rd == rec.start + rec.frames)
                  _recRead.store(rr + 1, std::memory_order_release);
            }
      if (underrun)
            _underruns.fetch_add(1, std::memory_order_relaxed);
      _wakeProducer.notify_one();
      return underrun;
      }

} // namespace Ms
