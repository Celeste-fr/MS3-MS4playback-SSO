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

std::vector<float> render(Vst3Synth* vst, const EvenAuto::Job& job, int rate, std::vector<double>* starts,
                          std::function<bool()> goOn)
      {
      std::vector<float> mono;
      if (!vst || job.events.empty() || job.events.size() != job.times.size())
            return mono;
      vst->setExportMix(0, job.volume, job.pan, false, job.gain);
      auto isNote = [](const NPlayEvent& e) {
            return (e.type() == ME_NOTEON || e.type() == ME_NOTEOFF) && !e.librarySwitch();
            };
      static const unsigned FRAMES = 512;
      float buffer[FRAMES * 2];
      const qint64 first = qint64(std::llround(job.from * rate));
      const qint64 last = qint64(std::llround((job.to + job.gap) * rate));
      mono.reserve(size_t(std::max<qint64>(0, last - first)) * size_t(EvenLevels::PASSES) + FRAMES);
      for (int pass = 0; pass < EvenLevels::PASSES; ++pass) {
            size_t i = 0;
            for (; i < job.events.size() && job.times[i] < job.from; ++i)
                  if (!isNote(job.events[i]))
                        vst->play(job.events[i]);
            if (starts)
                  starts->push_back(double(mono.size()) / rate);
            qint64 playTime = first;
            while (playTime < last) {
                  if (goOn && !goOn()) {
                        vst->allNotesOff(-1);
                        mono.clear();
                        if (starts)
                              starts->clear();
                        return mono;
                        }
                  unsigned frames = FRAMES;
                  std::memset(buffer, 0, sizeof(buffer));
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
                  for (unsigned k = 0; k < FRAMES; ++k)
                        mono.push_back(0.5f * (buffer[2 * k] + buffer[2 * k + 1]));
                  playTime = endTime;
                  }
            vst->allNotesOff(-1);
            }
      return mono;
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
      const std::vector<float> mono = render(vst, job, rate, &starts, goOn);
      r.renderSeconds = clock.elapsed() / 1000.0;
      r.audioSeconds = double(mono.size()) / rate;
      if (mono.empty() || starts.size() != size_t(EvenLevels::PASSES)) {
            r.error = mono.empty() && goOn ? QString("cancelled") : QString("nothing rendered");
            return r;
            }
      measure(mono, rate, starts, job.from, job.fit, &r.levels, &r.env);
      r.ok = true;
      return r;
      }

}     // namespace EvenMeasure
}     // namespace Ms
