// slot-lock (2026-10-31): a fake Vst3Plugin for slotlock_tsan.sh, so that Vst3Synth (audio/vst3/vst3synth.cpp, compiled
// unchanged) runs under -fsanitize=thread without the VST 3 SDK. Each instance: a deterministic sine per sounding key,
// the parameter queue and MIDI mapping touched as the real one does (the mapping swapped under Vst3Synth's lock in idle).
#include "audio/vst3/vst3plugin.h"
#include "libmscore/followcut.h"
#include <atomic>
#include <cmath>

namespace Ms {

class Vst3PluginPrivate {
   public:
      double rate { 48000 };
      double pitch { 0 };
      double phase[128] {};
      float vel[128] {};
      double level { 1.0 };
      std::vector<unsigned> ccParam = std::vector<unsigned>(16 * 130, 7u);    // (the mapping, read by midi())
      std::mutex handler;                 // (the editor's edits, as Vst3Plugin's handler.mutex)
      std::vector<std::pair<unsigned, double>> edits;
      std::atomic<bool> restart { false };
      int remap { 0 };
      };

Vst3Plugin::Vst3Plugin() : d(new Vst3PluginPrivate) {}
Vst3Plugin::~Vst3Plugin() {}

std::unique_ptr<Vst3Plugin> Vst3Plugin::load(const QString&, double sampleRate, int, QString*, const QString&)
      {
      std::unique_ptr<Vst3Plugin> p(new Vst3Plugin);
      p->d->rate = sampleRate;
      return p;
      }

void Vst3Plugin::midi(int type, int channel, int a, int b, float tuning)
      {
      const unsigned id = d->ccParam[size_t(channel * 130 + (a & 127))];
      (void)id;
      if (type == 0x90 && b > 0)
            d->vel[a & 127] = float(b) / 127.f * (1.f + tuning / 1000.f);
      else if (type == 0x90 || type == 0x80)
            d->vel[a & 127] = 0.f;
      else if (type == 0xb0 && a == 1)
            d->level = b / 127.0;
      }

void Vst3Plugin::processDirect(int frames, float* buf)
      {
      {
            std::lock_guard<std::mutex> lock(d->handler);
            for (const auto& e : d->edits)
                  d->level = e.second;
            d->edits.clear();
      }
      d->restart.exchange(false);
      for (int k = 0; k < 128; ++k) {
            if (d->vel[k] == 0.f)
                  continue;
            const double w = 2 * 3.14159265358979 * 440.0 * std::pow(2.0, (k - 69 + d->pitch / 100.0) / 12.0) / d->rate;
            for (int i = 0; i < frames; ++i) {
                  const float x = float(0.05 * d->level * d->vel[k] * std::sin(d->phase[k]));
                  d->phase[k] += w;
                  buf[2 * i] += x;
                  buf[2 * i + 1] += x;
                  }
            }
      }

void Vst3Plugin::process(int frames, float* buf) { processDirect(frames, buf); }
void Vst3Plugin::beginProcess(int) {}
void Vst3Plugin::setPitch(double cents, double) { d->pitch = cents; }
void Vst3Plugin::allNotesOff() { for (float& v : d->vel) v = 0.f; }
void Vst3Plugin::restartTransport() { d->restart = true; }
bool Vst3Plugin::setOffline(bool) { return true; }
bool Vst3Plugin::setSampleRate(double r) { d->rate = r; return true; }
void Vst3Plugin::queueParameter(unsigned, double v) { d->level = v; }
void Vst3Plugin::setParameter(unsigned id, double v, int)
      {
      std::lock_guard<std::mutex> lock(d->handler);
      d->edits.emplace_back(id, v);
      }

// as the real idle: the mapping made outside the lock, swapped in under it (every 4th call: a "remap")
void Vst3Plugin::idle(std::mutex* processing)
      {
      if (++d->remap % 4 != 0)
            return;
      std::vector<unsigned> map(16 * 130, 7u);
      std::unique_lock<std::mutex> lock;
      if (processing)
            lock = std::unique_lock<std::mutex>(*processing);
      d->ccParam.swap(map);
      }

namespace FollowCut {
double stepFactor(double rate) { return std::exp(-double(STEP_FRAMES) / (SMOOTH_S * (rate > 0 ? rate : 44100.0))); }
}
} // namespace Ms
