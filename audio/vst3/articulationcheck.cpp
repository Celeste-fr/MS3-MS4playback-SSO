//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  ArticulationCheck: which articulation values switch a hosted plug-in (see
//  articulationcheck.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "articulationcheck.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "vst3plugin.h"
#include "audio/midi/event.h"

namespace Ms {

static constexpr double PI = 3.14159265358979323846;
static constexpr int BANDS = 24;
static constexpr int PARTS = 5;           // 4 parts of the note, then the tail
static constexpr int FFT = 2048;
static constexpr double FLOOR_DB = -100.0;
static constexpr double SILENT_DB = -80.0;

const char* ArticulationCheck::name(Verdict v)
      {
      switch (v) {
            case Verdict::SWITCHES:   return "switches";
            case Verdict::IGNORED:    return "ignored";
            case Verdict::UNCLEAR:    return "unclear";
            case Verdict::SILENT:     return "silent";
            case Verdict::UNTESTABLE: return "untestable";
            }
      return "";
      }

//---------------------------------------------------------
//   fft
//    in place, radix 2
//---------------------------------------------------------

static void fft(std::vector<std::complex<double>>& a)
      {
      const size_t n = a.size();
      for (size_t i = 1, j = 0; i < n; ++i) {
            size_t bit = n >> 1;
            for (; j & bit; bit >>= 1)
                  j ^= bit;
            j ^= bit;
            if (i < j)
                  std::swap(a[i], a[j]);
            }
      for (size_t len = 2; len <= n; len <<= 1) {
            const double ang = -2 * PI / double(len);
            const std::complex<double> wl(std::cos(ang), std::sin(ang));
            for (size_t i = 0; i < n; i += len) {
                  std::complex<double> w(1);
                  for (size_t j = 0; j < len / 2; ++j) {
                        const std::complex<double> u = a[i + j];
                        const std::complex<double> v = a[i + j + len / 2] * w;
                        a[i + j] = u + v;
                        a[i + j + len / 2] = u - v;
                        w *= wl;
                        }
                  }
            }
      }

static double db(double power)
      {
      return power > 0 ? std::max(FLOOR_DB, 10 * std::log10(power)) : FLOOR_DB;
      }

//---------------------------------------------------------
//   perceivedLoudnessDb
//---------------------------------------------------------

double ArticulationCheck::perceivedLoudnessDb(const std::vector<float>& clip, double sampleRate)
      {
      const size_t frames = clip.size() / 2;
      if (frames == 0 || sampleRate <= 0)
            return -200;
      std::vector<double> x(frames);
      for (size_t i = 0; i < frames; ++i)
            x[i] = 0.5 * (double(clip[2 * i]) + double(clip[2 * i + 1]));
      // K-weighting (BS.1770's two stages, for this sample rate): a high shelf (+4 dB over ~1.7 kHz,
      // the head), a high-pass (~38 Hz)
      auto biquad = [&x](double b0, double b1, double b2, double a1, double a2) {
            double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
            for (double& v : x) {
                  const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
                  x2 = x1; x1 = v; y2 = y1; y1 = y;
                  v = y;
                  }
            };
      {
            const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            const double K = std::tan(PI * f0 / sampleRate), Vh = std::pow(10.0, G / 20.0), Vb = std::pow(Vh, 0.4996667741545416);
            const double a0 = 1 + K / Q + K * K;
            biquad((Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0,
                   2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0);
      }
      {
            const double f0 = 38.13547087602444, Q = 0.5003270373238773;
            const double K = std::tan(PI * f0 / sampleRate);
            const double a0 = 1 + K / Q + K * K;
            biquad(1.0, -2.0, 1.0, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0);
      }
      // auditory filters one ERB apart from 50 Hz to 15 kHz, each a rounded exponential (Glasberg & Moore
      // 1990): smooth and overlapping, so a tone counts the same wherever it falls (hard band edges
      // made a tone split over two bands several dB louder)
      // (2048 at 48 kHz: 23 Hz bins; 43 ms, short enough for a short note)
      const size_t N = 2048;
      const size_t hop = std::max<size_t>(1, size_t(sampleRate * 0.005));
      const double dt = double(hop) / sampleRate;
      const double attack = 1 - std::exp(-dt / 0.022), release = 1 - std::exp(-dt / 0.050);
      std::vector<double> window(N);
      for (size_t i = 0; i < N; ++i)
            window[i] = 0.5 - 0.5 * std::cos(2 * PI * double(i) / double(N - 1));
      auto erbRate = [](double f) { return 21.4 * std::log10(4.37 * f / 1000 + 1); };
      auto erbFreq = [](double e) { return (std::pow(10.0, e / 21.4) - 1) * 1000 / 4.37; };
      struct Filter { size_t from; std::vector<double> w; };
      std::vector<Filter> filters;
      const double binHz = sampleRate / double(N);
      for (double e = erbRate(50); e <= erbRate(std::min(15000.0, sampleRate * 0.45)); e += 1.0) {
            const double fc = erbFreq(e);
            const double p = 4 * fc / (24.7 * (4.37 * fc / 1000 + 1));
            Filter flt;
            flt.from = 0;
            bool started = false;
            for (size_t k = 1; k < N / 2; ++k) {
                  const double g = std::fabs(double(k) * binHz - fc) / fc;
                  const double w = (1 + p * g) * std::exp(-p * g);
                  if (w < 1e-4) {
                        if (started)
                              break;
                        continue;
                        }
                  if (!started)
                        flt.from = k, started = true;
                  flt.w.push_back(w);
                  }
            if (started)
                  filters.push_back(std::move(flt));
            }
      std::vector<std::complex<double>> spec(N);
      std::vector<double> power(N / 2);
      double stl = 0, peak = 0;
      for (size_t start = 0; start + N <= frames + N / 2; start += hop) {
            for (size_t i = 0; i < N; ++i) {
                  const size_t j = start + i;
                  spec[i] = j < frames ? x[j] * window[i] : 0.0;
                  }
            fft(spec);
            for (size_t k = 0; k < N / 2; ++k)
                  power[k] = std::norm(spec[k]);
            // (a threshold: a filter barely excited adds next to nothing, as in hearing; A ~60 dB under
            // a -20 dBFS tone at this FFT size)
            static const double A = 1e-3;
            double loud = 0;
            for (const Filter& flt : filters) {
                  double e = 0;
                  for (size_t i = 0; i < flt.w.size(); ++i)
                        e += flt.w[i] * power[flt.from + i];
                  loud += std::pow(e + A, 0.3) - std::pow(A, 0.3);
                  }
            stl += (loud > stl ? attack : release) * (loud - stl);
            peak = std::max(peak, stl);
            }
      return peak > 0 ? 33.2 * std::log10(peak) : -200;
      }

//---------------------------------------------------------
//   features
//    [0] the number of spectrum values; the spectrum (dB per band, per part); the loudness
//    envelope (dB per 20 ms)
//---------------------------------------------------------

std::vector<double> ArticulationCheck::features(const std::vector<float>& clip, int noteFrames, double sampleRate)
      {
      const int frames = int(clip.size() / 2);
      std::vector<double> mono(frames);
      for (int i = 0; i < frames; ++i)
            mono[i] = 0.5 * (double(clip[2 * i]) + double(clip[2 * i + 1]));
      // at the same loudness (of the note): round robins differ most in level
      double power = 0;
      const int held = std::min(noteFrames, frames);
      for (int i = 0; i < held; ++i)
            power += mono[i] * mono[i];
      if (held > 0 && power > 0) {
            const double gain = 0.1 / std::sqrt(power / held);
            for (double& x : mono)
                  x *= gain;
            }

      // band edges (FFT bins), log spaced from 80 Hz
      const double top = std::min(16000.0, sampleRate / 2);
      std::vector<int> edge(BANDS + 1);
      for (int b = 0; b <= BANDS; ++b) {
            const double f = 80.0 * std::pow(top / 80.0, double(b) / BANDS);
            edge[b] = std::max(1, int(f * FFT / sampleRate));
            }
      for (int b = 1; b <= BANDS; ++b)
            edge[b] = std::max(edge[b], edge[b - 1] + 1);

      std::vector<double> spectrum(PARTS * BANDS, 0.0);
      std::vector<int> count(PARTS, 0);
      std::vector<std::complex<double>> buf(FFT);
      for (int at = 0; at + FFT <= frames; at += FFT / 2) {
            const int centre = at + FFT / 2;
            const int part = centre < noteFrames ? std::min(3, centre * 4 / std::max(1, noteFrames)) : 4;
            for (int i = 0; i < FFT; ++i)
                  buf[i] = mono[at + i] * (0.5 - 0.5 * std::cos(2 * PI * i / (FFT - 1)));
            fft(buf);
            for (int b = 0; b < BANDS; ++b) {
                  double p = 0;
                  for (int k = edge[b]; k < edge[b + 1] && k < FFT / 2; ++k)
                        p += std::norm(buf[k]);
                  spectrum[part * BANDS + b] += p / (FFT * FFT);
                  }
            ++count[part];
            }
      std::vector<double> f;
      f.push_back(PARTS * BANDS);
      for (int part = 0; part < PARTS; ++part)
            for (int b = 0; b < BANDS; ++b)
                  f.push_back(db(count[part] ? spectrum[part * BANDS + b] / count[part] : 0));

      const int hop = std::max(1, int(sampleRate * 0.02));
      for (int at = 0; at + hop <= frames; at += hop) {
            double p = 0;
            for (int i = at; i < at + hop; ++i)
                  p += mono[i] * mono[i];
            f.push_back(db(p / hop));
            }
      return f;
      }

//---------------------------------------------------------
//   distance
//    the RMS difference of the spectra plus that of the envelopes, dB
//---------------------------------------------------------

double ArticulationCheck::distance(const std::vector<double>& a, const std::vector<double>& b)
      {
      if (a.empty() || b.empty())
            return 0;
      const size_t spec = size_t(a[0]);
      auto rms = [&](size_t from, size_t to) {
            if (to <= from)
                  return 0.0;
            double s = 0;
            for (size_t i = from; i < to; ++i)
                  s += (a[i] - b[i]) * (a[i] - b[i]);
            return std::sqrt(s / double(to - from));
            };
      const size_t n = std::min(a.size(), b.size());
      return rms(1, 1 + spec) + rms(1 + spec, n);
      }

//---------------------------------------------------------
//   scanPictures
//---------------------------------------------------------

std::vector<bool> ArticulationCheck::scanPictures(const QImage& base, const std::vector<QImage>& sameState, const std::vector<QImage>& shots,
                                                  const QRect& area, const std::vector<int>& candidates, int* noneIndex,
                                                  const std::vector<std::pair<QImage, QImage>>& samePairs)
      {
      const int n = int(shots.size());
      std::vector<bool> articulation(n, false);
      if (n == 0 || base.isNull())
            return articulation;
      const QRect a = area.isNull() ? base.rect() : (area & base.rect());
      auto prepared = [&](const QImage& img) {
            return (img.size() == base.size() ? img : img.scaled(base.size())).copy(a).convertToFormat(QImage::Format_RGB32);
            };
      const QImage b = prepared(base);
      const int w = b.width();
      const int h = b.height();
      auto differs = [](QRgb p1, QRgb p2) {
            return std::abs(qRed(p1) - qRed(p2)) > 40 || std::abs(qGreen(p1) - qGreen(p2)) > 40
                   || std::abs(qBlue(p1) - qBlue(p2)) > 40;
            };
      // what changes by itself, in cells of 8 x 8 pixels, with a margin of 2 cells
      const int C = 8;
      const int cw = (w + C - 1) / C;
      const int ch = (h + C - 1) / C;
      std::vector<char> noisy(cw * ch, 0);
      auto noise = [&](const QImage& one, const QImage& other) {
            for (int y = 0; y < h; ++y) {
                  const QRgb* p1 = reinterpret_cast<const QRgb*>(one.constScanLine(y));
                  const QRgb* p2 = reinterpret_cast<const QRgb*>(other.constScanLine(y));
                  for (int x = 0; x < w; ++x)
                        if (differs(p1[x], p2[x]))
                              noisy[(y / C) * cw + x / C] = 1;
                  }
            };
      for (const QImage& s : sameState)
            noise(b, prepared(s));
      for (const auto& pair : samePairs)
            noise(prepared(pair.first), prepared(pair.second));
      std::vector<char> masked(cw * ch, 0);
      for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x)
                  if (noisy[y * cw + x])
                        for (int dy = -2; dy <= 2; ++dy)
                              for (int dx = -2; dx <= 2; ++dx)
                                    if (y + dy >= 0 && y + dy < ch && x + dx >= 0 && x + dx < cw)
                                          masked[(y + dy) * cw + x + dx] = 1;
      std::vector<QImage> crops;
      for (const QImage& sh : shots)
            crops.push_back(prepared(sh));
      auto differing = [&](const QImage& p, const QImage& q) {
            int count = 0;
            for (int y = 0; y < h; ++y) {
                  const QRgb* p1 = reinterpret_cast<const QRgb*>(p.constScanLine(y));
                  const QRgb* p2 = reinterpret_cast<const QRgb*>(q.constScanLine(y));
                  const char* m = &masked[(y / C) * cw];
                  for (int x = 0; x < w; ++x)
                        if (!m[x / C] && differs(p1[x], p2[x]))
                              ++count;
                  }
            return count;
            };
      // a word changed is hundreds of pixels; a few may differ in a redrawn edge
      const int same = 12;
      int none = -1;
      int most = -1;
      for (int c : candidates) {
            if (c < 0 || c >= n)
                  continue;
            int count = 0;
            for (const QImage& cr : crops)
                  count += differing(crops[c], cr) <= same;
            if (count > most) {
                  most = count;
                  none = c;
                  }
            }
      if (none < 0)
            none = 0;
      for (int i = 0; i < n; ++i)
            articulation[i] = differing(crops[none], crops[i]) > same;
      // "no articulation" can look two ways within a scan: SSO leaves its RELEASE slider where the
      // last short articulation put it, so the "None" pictures before the first short differ from
      // those after (the owner's scan of 2026-09-27 21:29: 44 values of Celli - Core techniques
      // taken for articulations). An articulation shows its own name: a picture shared by several
      // values is another "no articulation"
      const int shared = 4;
      std::vector<int> reps;                    // one picture of each kind, and how many share it
      std::vector<std::vector<int>> members;
      for (int i = 0; i < n; ++i) {
            if (!articulation[i])
                  continue;
            size_t k = 0;
            while (k < reps.size() && differing(crops[reps[k]], crops[i]) > same)
                  ++k;
            if (k == reps.size()) {
                  reps.push_back(i);
                  members.push_back({});
                  }
            members[k].push_back(i);
            }
      for (const std::vector<int>& m : members)
            if (int(m.size()) >= shared)
                  for (int i : m)
                        articulation[i] = false;
      if (noneIndex)
            *noneIndex = none;
      return articulation;
      }

//---------------------------------------------------------
//   Player
//    one instance, rendered offline block by block
//---------------------------------------------------------

namespace {

struct Clip {
      std::vector<double> features;
      double peakDb { -200 };
      double loudDb { -200 };       // the loudest 50 ms (RMS): a level round robins and clicks move less
      double perceivedDb { -200 };  // ArticulationCheck::perceivedLoudnessDb
      };

struct Player {
      Vst3Plugin* p;
      ArticulationCheck::Settings s;
      std::vector<float> buffer;

      std::vector<float> render(int frames)
            {
            buffer.assign(size_t(2 * frames), 0.f);
            if (frames > 0)
                  p->process(frames, buffer.data());
            return buffer;
            }
      int frames(double seconds) const { return int(seconds * s.sampleRate); }

      int lastPitch { -1 };
      // settle to 50 dB under the last note, not to -70 dBFS (the dynamics measurement: its notes go
      // from soft to loud, and a tail 50 dB under the loudest 50 ms of the next note moves it by
      // less than 0.1 dB; the owner, 2026-09-28: make the test faster without losing data)
      bool relativeSettle { false };
      double lastLoudDb { -200 };

      // until the last note has died away to -70 dBFS (or 2 s): what is left of a reverb tail
      // then is 50 dB and more under the next note, too little to change its features
      void settle()
            {
            if (lastPitch >= 0)
                  p->midi(ME_NOTEON, s.channel, lastPitch, 0);
            p->allNotesOff();
            const int block = frames(0.1);
            for (int i = 0; i < 20; ++i) {
                  const std::vector<float>& b = render(block);
                  double peak = 0;
                  for (float x : b)
                        peak = std::max(peak, double(std::fabs(x)));
                  const double floorDb = relativeSettle ? std::max(-70.0, lastLoudDb - 50) : -70.0;
                  if (db(peak * peak) < floorDb && i >= 2)
                        break;
                  }
            }

      Clip play(int prior, int value, int pitch)
            {
            settle();
            if (prior >= 0 && s.switchCC >= 0) {
                  p->midi(ME_CONTROLLER, s.channel, s.switchCC, prior);
                  render(frames(0.1));
                  }
            if (s.switchCC >= 0)          // (-1: a patch without switching)
                  p->midi(ME_CONTROLLER, s.channel, s.switchCC, value);
            if (s.dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, s.channel, s.dynamicsCC, s.dynamicsValue);
            if (s.expressionCC >= 0 && s.expressionCC != s.dynamicsCC)
                  p->midi(ME_CONTROLLER, s.channel, s.expressionCC, qBound(0, s.expressionValue, 127));
            render(frames(0.1));
            lastPitch = pitch;
            p->midi(ME_NOTEON, s.channel, pitch, s.velocity);
            std::vector<float> clip = render(frames(s.note));
            p->midi(ME_NOTEON, s.channel, pitch, 0);
            const std::vector<float>& tail = render(frames(s.tail));
            clip.insert(clip.end(), tail.begin(), tail.end());
            Clip c;
            double peak = 0;
            for (float x : clip)
                  peak = std::max(peak, double(std::fabs(x)));
            c.peakDb = peak > 0 ? 20 * std::log10(peak) : -200;
            const size_t win = size_t(2 * frames(0.05));
            double loudest = 0;
            for (size_t from = 0; win > 0 && from + win <= clip.size(); from += win / 2) {
                  double sum = 0;
                  for (size_t i = from; i < from + win; ++i)
                        sum += double(clip[i]) * clip[i];
                  loudest = std::max(loudest, sum / win);
                  }
            c.loudDb = db(loudest);
            lastLoudDb = c.loudDb;
            c.perceivedDb = ArticulationCheck::perceivedLoudnessDb(clip, s.sampleRate);
            c.features = ArticulationCheck::features(clip, frames(s.note), s.sampleRate);
            return c;
            }
      };

} // namespace

//---------------------------------------------------------
//   dynamics
//---------------------------------------------------------

constexpr int ArticulationCheck::CURVE_POINTS[8];
constexpr int ArticulationCheck::EXPRESSION_POINTS[7];

const char* ArticulationCheck::DynamicsResult::drivenBy() const
      {
      const double v = velocityDb[1] - velocityDb[0], c = ccDb[1] - ccDb[0];
      return v >= 3 && c >= 3 ? "both" : v >= 3 ? "velocity" : c >= 3 ? "controller" : "neither";
      }

std::vector<ArticulationCheck::DynamicsResult> ArticulationCheck::dynamics(Vst3Plugin* plugin, const std::vector<int>& values,
   const std::vector<int>& pitches, const std::vector<bool>& full, const Settings& settings, Progress progress)
      {
      std::vector<DynamicsResult> out;
      const int n = int(values.size());
      if (!plugin || n == 0 || int(pitches.size()) != n || int(full.size()) != n)
            return out;
      Player player { plugin, settings, {} };
      player.relativeSettle = true;
      int done = 0;
      const int total = 8 * n;                  // (about: 3 to classify, 2 to 7 more; 7 for a held note's volume)
      double pdb = -200;                  // the last note's perceived loudness
      auto at = [&](int value, int pitch, int velocity, int cc, double* db) {
            player.s.velocity = qBound(1, velocity, 127);
            player.s.dynamicsValue = qBound(0, cc, 127);
            const Clip c = player.play(-1, value, pitch);
            *db = c.loudDb;
            pdb = c.perceivedDb;
            ++done;
            return !progress || progress(done, std::max(total, done));
            };
      for (int i = 0; i < n; ++i) {
            DynamicsResult r;
            r.value = values[i];
            player.s.note = settings.note;
            player.s.tail = settings.tail;
            // what drives it: velocity 32 / CC 32, then the CC alone up, then the velocity alone up;
            // at another pitch of the instrument where the test pitch plays nothing (harmonics …)
            double d32 = -200, cc127 = -200, v127 = -200;
            double p32 = -200, pcc127 = -200;
            for (int shift : { 0, 12, -12, 7, -5, 24 }) {
                  const int p = pitches[i] + shift;
                  if (shift && (p < settings.minPitch || p > settings.maxPitch))
                        continue;
                  if (!at(r.value, p, 32, 32, &d32))
                        return out;
                  p32 = pdb;
                  if (!at(r.value, p, 32, 127, &cc127))
                        return out;
                  pcc127 = pdb;
                  if (!at(r.value, p, 127, 32, &v127))
                        return out;
                  if (std::max(d32, std::max(cc127, v127)) > SILENT_DB) {
                        r.pitch = p;
                        break;
                        }
                  }
            if (r.pitch < 0) {                  // silent at every pitch tried: no curve
                  out.push_back(r);
                  continue;
                  }
            r.velocityDb[0] = d32;
            r.velocityDb[1] = v127;
            r.ccDb[0] = d32;
            r.ccDb[1] = cc127;
            const QString by = r.drivenBy();
            if (by == "velocity" || by == "both" || full[size_t(i)]) {
                  // the whole curve, soft to loud; a short on velocity: its loudest 50 ms is its start,
                  // a shorter note and tail do (0.5 s, 0.2 s)
                  if (by == "velocity") {
                        player.s.note = std::min(settings.note, 0.5);
                        player.s.tail = std::min(settings.tail, 0.2);
                        }
                  for (int x : CURVE_POINTS) {
                        double db = d32;
                        double pd = p32;
                        if (x != 32) {
                              if (!at(r.value, r.pitch, x, x, &db))
                                    return out;
                              pd = pdb;
                              }
                        r.curve.push_back({ x, db });
                        r.perceived.push_back({ x, pd });
                        }
                  // the held note on the controller: the expression CC's volume (even dynamic steps,
                  // SoundLib::evenStep), at mf; 127 is the curve's 80
                  if (full[size_t(i)] && by != "velocity" && settings.expressionCC >= 0) {
                        for (int x : EXPRESSION_POINTS) {
                              double db;
                              player.s.expressionValue = x;
                              const bool go = at(r.value, r.pitch, 80, 80, &db);
                              player.s.expressionValue = 127;
                              if (!go)
                                    return out;
                              r.expression.push_back({ x, db });
                              r.expressionPerceived.push_back({ x, pdb });
                              }
                        for (size_t k = 0; k < r.curve.size(); ++k) {
                              if (r.curve[k].first == 80) {
                                    r.expression.push_back({ 127, r.curve[k].second });
                                    r.expressionPerceived.push_back({ 127, r.perceived[k].second });
                                    }
                              }
                        }
                  }
            else {
                  // on the controller (a long, tremolo …): the dynamics the report shows, and 127
                  // (velocity doesn't move it: velocity 32 with CC 127 is CC 127)
                  double d80, d112, p80, p112;
                  if (!at(r.value, r.pitch, 80, 80, &d80))
                        return out;
                  p80 = pdb;
                  if (!at(r.value, r.pitch, 112, 112, &d112))
                        return out;
                  p112 = pdb;
                  r.curve = { { 32, d32 }, { 80, d80 }, { 112, d112 }, { 127, cc127 } };
                  r.perceived = { { 32, p32 }, { 80, p80 }, { 112, p112 }, { 127, pcc127 } };
                  }
            out.push_back(r);
            }
      player.settle();
      return out;
      }

//---------------------------------------------------------
//   run
//---------------------------------------------------------

ArticulationCheck::Report ArticulationCheck::run(Vst3Plugin* plugin, const std::vector<int>& values, const Settings& settings, Progress progress)
      {
      Report report;
      const int n = int(values.size());
      if (!plugin || n == 0) {
            report.message = "nothing to check";
            return report;
            }
      Player player { plugin, settings, {} };
      int done = 0;
      int total = 3 * n;
      auto step = [&]() {
            ++done;
            if (progress && !progress(done, total))
                  report.cancelled = true;
            return !report.cancelled;
            };

      // every value after the first
      std::vector<Clip> first(n);
      for (int i = 0; i < n; ++i) {
            first[i] = player.play(values[0], values[i], settings.pitch);
            if (!step())
                  return report;
            }
      report.results.resize(n);
      for (int i = 0; i < n; ++i) {
            report.results[i].value = values[i];
            report.results[i].peakDb = first[i].peakDb;
            report.results[i].firstDistance = distance(first[i].features, first[0].features);
            report.results[i].pitch = settings.pitch;
            }
      auto silent = [](const Clip& c) { return c.peakDb < SILENT_DB; };

      // no sound at the pitch: far under the patch's loudest (its features would be noise
      // brought up to the loudness of the others)
      double loudest = -200;
      for (const Clip& c : first)
            loudest = std::max(loudest, c.peakDb);
      // (50 dB: SSO's Long Super Sul Tasto plays 42 dB under the patch's loudest, harmonics
      // with no sample at the pitch 57 dB and more)
      auto quiet = [&](const Clip& c) { return silent(c) || c.peakDb < loudest - 50; };

      // the references: A most unlike the first, B most unlike A among the others unlike the first
      int a = -1;
      for (int i = 1; i < n; ++i) {
            if (!quiet(first[i]) && (a < 0 || report.results[i].firstDistance > report.results[a].firstDistance))
                  a = i;
            }
      // round robins of one articulation stay well under this (dB)
      const double minDistance = 3.0;
      if (silent(first[0]) || a < 0 || report.results[a].firstDistance < minDistance) {
            for (Result& r : report.results)
                  r.verdict = silent(first[&r - &report.results[0]]) ? Verdict::SILENT : Verdict::UNTESTABLE;
            report.message = silent(first[0])
               ? QString("The first value plays nothing: is the patch loaded, on MIDI channel %1?").arg(settings.channel + 1)
               : QString("Every value sounds the same: is the patch set to switch articulations with CC%1 (Spitfire: UACC), on MIDI channel %2?")
                    .arg(settings.switchCC).arg(settings.channel + 1);
            return report;
            }
      report.switching = true;
      const double aDistance = report.results[a].firstDistance;
      auto unlike = [&](int from, int except1, int except2) {
            int best = -1;
            double bestD = -1;
            for (int i = 0; i < n; ++i) {
                  if (i == except1 || i == except2 || quiet(first[i]))
                        continue;
                  if (i != 0 && report.results[i].firstDistance < 0.5 * aDistance)
                        continue;         // might sound like the first because it is ignored
                  double d = distance(first[i].features, first[from].features);
                  if (except2 >= 0)
                        d = std::min(d, distance(first[i].features, first[except2].features));
                  if (d > bestD) {
                        bestD = d;
                        best = i;
                        }
                  }
            return best;
            };
      const int b = unlike(a, a, -1);
      const int c = n > 2 ? unlike(a, a, b) : -1;    // for A and B themselves
      report.refA = values[a];
      report.refB = values[b];
      report.refDistance = distance(first[a].features, first[b].features);

      // other pitches, for a value with no sound at the test pitch
      std::vector<int> others;
      for (int d : { 12, -12, 7, -7, 19, -19, 24, 5, -5 }) {
            const int q = settings.pitch + d;
            if (q >= std::max(0, settings.minPitch) && q <= std::min(127, settings.maxPitch))
                  others.push_back(q);
            }

      // every value after A and after B
      for (int i = 0; i < n; ++i) {
            Result& r = report.results[i];
            int p1 = a, p2 = b;
            if (i == a)
                  p1 = c;
            else if (i == b)
                  p2 = c;
            if (p1 < 0 || p2 < 0 || p1 == p2) {
                  // two values only: A and B are known to switch
                  r.verdict = silent(first[i]) ? Verdict::SILENT : Verdict::SWITCHES;
                  done += 2;
                  continue;
                  }
            int pitch = settings.pitch;
            double ref = distance(first[p1].features, first[p2].features);
            if (quiet(first[i])) {
                  // where does it sound?
                  pitch = -1;
                  for (int q : others) {
                        const Clip t = player.play(values[0], values[i], q);
                        if (!quiet(t)) {
                              pitch = q;
                              break;
                              }
                        }
                  if (pitch < 0) {
                        r.verdict = Verdict::SILENT;
                        ++done;
                        if (!step())
                              return report;
                        continue;
                        }
                  r.pitch = pitch;
                  // the references at that pitch
                  ref = distance(player.play(values[p1], values[p1], pitch).features,
                                 player.play(values[p2], values[p2], pitch).features);
                  }
            double ratio = 0;
            int tries = 0;
            for (;;) {
                  const Clip x = player.play(values[p1], values[i], pitch);
                  if (!step())
                        return report;
                  const Clip y = player.play(values[p2], values[i], pitch);
                  if (!step())
                        return report;
                  r.peakDb = std::max(x.peakDb, y.peakDb);
                  if (quiet(x) && quiet(y)) {
                        r.verdict = Verdict::SILENT;
                        break;
                        }
                  const double d = distance(x.features, y.features);
                  r.spread = tries ? std::min(r.spread, d) : d;
                  ratio += d / std::max(ref, 1e-9);
                  ++tries;
                  r.ratio = ratio / tries;
                  r.verdict = r.ratio < 0.5 ? Verdict::SWITCHES : r.ratio > 0.8 ? Verdict::IGNORED : Verdict::UNCLEAR;
                  if (r.verdict != Verdict::UNCLEAR || tries > 1)
                        break;
                  total += 2;       // once more
                  }
            }

      std::vector<double> spreads;
      for (const Result& r : report.results)
            if (r.verdict == Verdict::SWITCHES && r.spread >= 0)
                  spreads.push_back(r.spread);
      if (!spreads.empty()) {
            std::nth_element(spreads.begin(), spreads.begin() + spreads.size() / 2, spreads.end());
            report.sameDistance = std::max(0.5, 2 * spreads[spreads.size() / 2]);
            }
      else
            report.sameDistance = 0.5;

      // values that sound just like another that switches (an ignored one sounds like the
      // value played before it)
      for (int i = 0; i < n; ++i) {
            Result& r = report.results[i];
            if (r.verdict != Verdict::SWITCHES || r.pitch != settings.pitch)
                  continue;
            double best = report.sameDistance;
            for (int j = 0; j < n; ++j) {
                  if (j == i || report.results[j].verdict != Verdict::SWITCHES || report.results[j].pitch != settings.pitch)
                        continue;
                  const double d = distance(first[i].features, first[j].features);
                  if (d < best) {
                        best = d;
                        r.sameAs = values[j];
                        }
                  }
            }
      return report;
      }

} // namespace Ms
