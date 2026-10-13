//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  EvenMeasure (evenmeasure.h)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "evenmeasure.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <QElapsedTimer>

#include "articulationcheck.h"
#include "vst3synth.h"

namespace Ms {
namespace EvenMeasure {

static double median(std::vector<double> v)
      {
      if (v.empty())
            return 0.0;
      std::sort(v.begin(), v.end());
      const size_t n = v.size();
      return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
      }

//---------------------------------------------------------
//   measure
//---------------------------------------------------------

Envelope envelope(const std::vector<float>& mono, int rate)
      {
      Envelope out;
      std::vector<float> stereo(mono.size() * 2);         // (perceivedEnvelope takes interleaved stereo)
      for (size_t k = 0; k < mono.size(); ++k)
            stereo[2 * k] = stereo[2 * k + 1] = mono[k];
      out.e = ArticulationCheck::perceivedEnvelope(stereo, rate, &out.firstMs);
      out.hopMs = int(rate * 0.005) * 1000.0 / rate;
      return out;
      }

double Envelope::at(double s) const
      {
      const double x = (s * 1000.0 - firstMs) / hopMs;
      if (e.empty())
            return 0.0;
      if (x <= 0)
            return e.front();
      const size_t i = size_t(x);
      if (i + 1 >= e.size())
            return e.back();
      return e[i] + (e[i + 1] - e[i]) * (x - double(i));
      }

void measure(const std::vector<float>& mono, int rate, const std::vector<double>& starts, double from,
             const EvenLevels::Passage& p, std::vector<double>* levels, std::vector<double>* env)
      {
      measure(envelope(mono, rate), starts, from, p, levels, env);
      }

void measure(const Envelope& en, const std::vector<double>& starts, double from, const EvenLevels::Passage& p,
             std::vector<double>* levels, std::vector<double>* env)
      {
      const std::vector<double>& e = en.e;
      const double firstMs = en.firstMs;
      const double hopMs = en.hopMs;
      auto envAt = [&en](double s) { return en.at(s); };
      const size_t n = p.on.size();
      std::vector<std::vector<double>> per(n);
      for (double start : starts) {
            for (size_t j = 0; j < n; ++j) {
                  const double a = start + (p.window[j] - from);
                  const double b = a + EvenLevels::WINDOW_MS / 1000.0;
                  double sum = 0;
                  int cnt = 0;
                  const long i0 = std::max(0L, long(std::ceil((a * 1000.0 - firstMs) / hopMs)));
                  for (long i = i0; i < long(e.size()); ++i) {
                        const double ti = (firstMs + i * hopMs) / 1000.0;
                        if (ti >= b)
                              break;
                        if (ti >= a) {
                              sum += e[size_t(i)];
                              ++cnt;
                              }
                        }
                  per[j].push_back(cnt ? sum / cnt : envAt(a));
                  }
            }
      if (levels) {
            levels->assign(n, 0.0);
            for (size_t j = 0; j < n; ++j)
                  (*levels)[j] = median(per[j]);
            }
      if (env) {
            const int m = p.gridSize();
            env->assign(size_t(std::max(0, m)), 0.0);
            for (int i = 0; i < m; ++i) {
                  const double tg = p.gridStart() + i * EvenLevels::STEP_MS / 1000.0;
                  std::vector<double> v;
                  for (double start : starts)
                        v.push_back(envAt(start + (tg - from)));
                  (*env)[size_t(i)] = median(v);
                  }
            }
      }

//---------------------------------------------------------
//   render
//---------------------------------------------------------

// one pass appended to mono (its start in *starts); false: cancelled (goOn)
static bool renderPass(Vst3Synth* vst, const EvenAuto::Job& job, int rate, std::vector<float>* mono,
                       std::vector<double>* starts, const std::function<bool()>& goOn)
      {
      auto isNote = [](const NPlayEvent& e) {
            return (e.type() == ME_NOTEON || e.type() == ME_NOTEOFF) && !e.librarySwitch();
            };
      // (blocks of 4096, the plug-in's largest, events still at their sample: kthost on the VM rendered 12-16 % faster than
      // in 512s, the levels the same; sso_even_gap_check.json › blockSize)
      static const unsigned FRAMES = 4096;
      std::vector<float> buf(FRAMES * 2);
      float* buffer = buf.data();
      const qint64 first = qint64(std::llround(job.from * rate));
      const qint64 last = qint64(std::llround((job.to + job.gap) * rate));
      // each pass is a playback (renderPasses: the plug-in sees the transport start again)
      vst->restartTransport();
      size_t i = 0;
      for (; i < job.events.size() && job.times[i] < job.from; ++i)
            if (!isNote(job.events[i]))
                  vst->play(job.events[i]);
      if (starts)
            starts->push_back(double(mono->size()) / rate);
      qint64 playTime = first;
      while (playTime < last) {
            if (goOn && !goOn()) {
                  vst->allNotesOff(-1);
                  return false;
                  }
            const unsigned block = unsigned(std::min<qint64>(FRAMES, last - playTime));   // (the last one to `last`)
            unsigned frames = block;
            std::fill(buf.begin(), buf.end(), 0.f);
            const qint64 endTime = playTime + frames;
            float* p = buffer;
            for (; i < job.events.size(); ++i) {
                  const double t = job.times[i];
                  const qint64 f = qint64(t * rate);
                  if (f >= endTime)
                        break;
                  const qint64 n = f - playTime;
                  if (n > 0) {
                        vst->process(unsigned(n), p, nullptr, nullptr);
                        p += 2 * n;
                        playTime += n;
                        frames -= unsigned(n);
                        }
                  const NPlayEvent& e = job.events[i];
                  if (e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch() && t >= job.to)
                        continue;                   // (after the span: not this pass's)
                  vst->play(e);
                  }
            if (frames) {
                  vst->process(frames, p, nullptr, nullptr);
                  playTime += frames;
                  }
            for (unsigned k = 0; k < block; ++k)
                  mono->push_back(0.5f * (buffer[2 * k] + buffer[2 * k + 1]));
            playTime = endTime;
            }
      vst->allNotesOff(-1);
      return true;
      }

static bool ready(Vst3Synth* vst, const EvenAuto::Job& job)
      {
      if (!vst || job.events.empty() || job.events.size() != job.times.size())
            return false;
      vst->setExportMix(0, job.volume, job.pan, false, job.gain);
      return true;
      }

std::vector<float> render(Vst3Synth* vst, const EvenAuto::Job& job, int rate, std::vector<double>* starts,
                          std::function<bool()> goOn)
      {
      std::vector<float> mono;
      if (!ready(vst, job))
            return mono;
      mono.reserve(size_t(std::max(0.0, (job.to + job.gap - job.from) * rate)) * size_t(EvenLevels::PASSES) + 512);
      for (int pass = 0; pass < EvenLevels::PASSES; ++pass) {
            if (!renderPass(vst, job, rate, &mono, starts, goOn)) {
                  mono.clear();
                  if (starts)
                        starts->clear();
                  break;
                  }
            }
      return mono;
      }

//---------------------------------------------------------
//   passesAlike
//    early stop: passes 1 and 2 within job.earlyStopDb on every measured note (sso_even_gap_check.json: then their
//    mean fits as the median of 3 does)
//---------------------------------------------------------

bool passesAlike(const std::vector<double>& a, const std::vector<double>& b, const std::vector<bool>& fixed, double db)
      {
      if (db <= 0 || a.size() != b.size())
            return false;
      for (size_t j = 0; j < a.size(); ++j)
            if (!(j < fixed.size() && fixed[j]) && std::fabs(a[j] - b[j]) > db)
                  return false;
      return true;
      }

//---------------------------------------------------------
//   run
//---------------------------------------------------------

EvenAuto::Result run(Vst3Synth* vst, const EvenAuto::Job& job, int rate, std::function<bool()> goOn)
      {
      EvenAuto::Result r;
      r.id = job.id;
      r.round = job.round;
      QElapsedTimer clock;
      clock.start();
      std::vector<double> starts;
      std::vector<float> mono;
      bool ok = ready(vst, job);
      for (int pass = 0; ok && pass < EvenLevels::PASSES; ++pass) {
            ok = renderPass(vst, job, rate, &mono, &starts, goOn);
            if (ok && pass == 1 && job.earlyStopDb > 0) {
                  const Envelope env = envelope(mono, rate);
                  std::vector<double> a, b;
                  measure(env, { starts[0] }, job.from, job.fit, &a, nullptr);
                  measure(env, { starts[1] }, job.from, job.fit, &b, nullptr);
                  if (passesAlike(a, b, job.fixed, job.earlyStopDb))
                        break;
                  }
            }
      r.renderSeconds = clock.elapsed() / 1000.0;
      r.audioSeconds = double(mono.size()) / rate;
      r.passes = int(starts.size());
      if (!ok || mono.empty() || starts.size() < 2) {
            r.error = !ok && goOn ? QString("cancelled") : QString("nothing rendered");
            return r;
            }
      measure(mono, rate, starts, job.from, job.fit, &r.levels, &r.env);
      r.ok = true;
      return r;
      }

}     // namespace EvenMeasure
}     // namespace Ms
