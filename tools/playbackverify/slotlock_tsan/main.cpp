// slot-lock (2026-10-31): Vst3Synth's slots' lock under ThreadSanitizer (slotlock_tsan.sh). An audio thread plays
// events and blocks cut as Seq cuts them; a GUI-like thread hammers setMix / setLowCut / setLink / setParameterIds /
// plugin / instance / hostOf / idle / takePlugin + setPlugin with the values in force, and puts a new instance into an
// unused slot now and then (the old one destroyed; slot changes and idle() are one thread's, the GUI's); a third thread
// sets the Mixer and the low cut (any thread's). Output with and without them compared sample
// for sample; skippedBlocks() 0; TSan reports any race.
#include "audio/vst3/vst3synth.h"
#include <cstdio>
#include <thread>
#include <atomic>
#include <vector>
#include <cstdlib>
using namespace Ms;

static std::vector<float> run(bool hammer, int blocks, unsigned* waits, unsigned* skipped)
      {
      const int BLOCK = 512;
      Vst3Synth vst;
      vst.init(48000);
      for (int k = 0; k < 4; ++k)
            vst.setPlugin(k, Vst3Plugin::load("", 48000, BLOCK));
      vst.setPlugin(20, Vst3Plugin::load("", 48000, BLOCK));
      vst.setLink(4, 0, 1, false);
      const std::vector<long> ids { 2, -1, 3 };
      for (int k = 0; k < 4; ++k) {
            vst.setMix(k, 90 + 5 * k, 40 + 10 * k, false, 0.9f);
            vst.setLowCut(k, 60.f + 20.f * k);
            vst.setParameterIds(k, ids);
            }
      std::atomic<bool> stop { false };
      std::thread gui, loader;
      if (hammer) {
            gui = std::thread([&]() {
                  for (long long n = 0; !stop.load(); ++n) {
                        const int k = int(n % 4);
                        vst.setMix(k, 90 + 5 * k, 40 + 10 * k, false, 0.9f);
                        vst.setLowCut(k, 60.f + 20.f * k);
                        vst.setLink(4, 0, 1, false);
                        vst.setParameterIds(k, ids);
                        (void)vst.instance(4);
                        (void)vst.hostOf(4);
                        if (n % 16 == 0)
                              vst.idle();
                        if (n % 16 == 8) {
                              std::unique_ptr<Vst3Plugin> spare = vst.takePlugin(20);
                              vst.setPlugin(20, std::move(spare));
                              }
                        if (n % 64 == 32)       // (a new instance in, the old destroyed: on this thread, as idle(): the GUI's)
                              vst.setPlugin(30, Vst3Plugin::load("", 48000, BLOCK));
                        }
                  vst.setPlugin(30, nullptr);
                  });
            // a third thread: the Mixer, the low cut and the varispeed flags are any thread's (atomics)
            loader = std::thread([&]() {
                  for (long long n = 0; !stop.load(); ++n) {
                        const int k = int(n % 4);
                        vst.setMix(k, 90 + 5 * k, 40 + 10 * k, false, 0.9f);
                        vst.setExportMix(k, 100, 64, false);
                        vst.setLowCut(k, 60.f + 20.f * k);
                        (void)vst.lowCutAt(k);
                        }
                  });
            }
      unsigned seed = 12345;
      auto next = [&seed](unsigned m) { seed = seed * 1103515245u + 12345u; return (seed >> 8) % m; };
      std::vector<float> out(size_t(2) * BLOCK * size_t(blocks), 0.f);
      bool on[5][128] {};
      for (int b = 0; b < blocks; ++b) {
            float* o = out.data() + size_t(2) * BLOCK * size_t(b);
            const unsigned cut = next(BLOCK);
            if (cut)
                  vst.process(cut, o, nullptr, nullptr);
            const int slot = int(next(5));
            const unsigned what = next(8);
            if (what < 4) {
                  const int key = 48 + int(next(24));
                  PlayEvent e(ME_NOTEON, slot, key, on[slot][key] ? 0 : 60 + int(next(60)));
                  vst.play(e);
                  on[slot][key] = !on[slot][key];
                  }
            else if (what < 6)
                  vst.play(PlayEvent(ME_CONTROLLER, slot, 1, int(next(128))));
            else {
                  PlayEvent e(ME_PARAMETER, slot, what == 6 ? 0 : 2, 0);
                  e.setTuning(float(next(100)) / 100.f);
                  vst.play(e);
                  }
            vst.process(unsigned(BLOCK) - cut, o + 2 * cut, nullptr, nullptr);
            if (b % 997 == 500)
                  vst.allNotesOff(-1);
            if (b % 1999 == 1000)
                  vst.restartTransport();
            }
      stop = true;
      if (gui.joinable())
            gui.join();
      if (loader.joinable())
            loader.join();
#ifdef SLOTLOCK_OLD
      *waits = 1;             // (the code before slot-lock, slotlock_tsan.sh --old: no such count)
#else
      *waits = vst.lockWaits();
#endif
      *skipped = vst.skippedBlocks();
      return out;
      }

int main(int argc, char** argv)
      {
      const int blocks = argc > 1 ? std::atoi(argv[1]) : 4000;
      unsigned w0, s0, w1, s1;
      const std::vector<float> a = run(false, blocks, &w0, &s0);
      const std::vector<float> b = run(true, blocks, &w1, &s1);
      size_t diff = 0;
      for (size_t i = 0; i < a.size(); ++i)
            diff += a[i] != b[i];
      std::printf("slotlock_tsan: %d blocks (%lld frames); waits %u / %u, skipped %u / %u, %zu samples differ\n",
                  blocks, (long long)blocks * 512, w0, w1, s0, s1, diff);
      return (diff == 0 && s0 == 0 && s1 == 0 && w1 > 0) ? 0 : 1;
      }
