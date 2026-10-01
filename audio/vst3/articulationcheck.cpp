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
#include <deque>
#include <future>
#include <map>
#include <thread>

#include "pluginextract.h"
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
//   kWeighted
//    a stereo interleaved clip as mono, K-weighted (BS.1770's two stages, for this sample rate): a
//    high shelf (+4 dB over ~1.7 kHz, the head), a high-pass (~38 Hz)
//---------------------------------------------------------

static std::vector<double> kWeighted(const std::vector<float>& clip, double sampleRate)
      {
      const size_t frames = clip.size() / 2;
      std::vector<double> x(frames);
      for (size_t i = 0; i < frames; ++i)
            x[i] = 0.5 * (double(clip[2 * i]) + double(clip[2 * i + 1]));
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
      return x;
      }

//---------------------------------------------------------
//   ErbFilter
//    auditory filters one ERB apart from 50 Hz to 15 kHz, each a rounded exponential (Glasberg & Moore
//    1990): smooth and overlapping, so a tone counts the same wherever it falls (hard band edges
//    made a tone split over two bands several dB louder). Weights on an N-point FFT's bins
//---------------------------------------------------------

namespace {
struct ErbFilter {
      double fc { 0 };
      size_t from { 0 };
      std::vector<double> w;
      };
}

static double erbRate(double f) { return 21.4 * std::log10(4.37 * f / 1000 + 1); }
static double erbFreq(double e) { return (std::pow(10.0, e / 21.4) - 1) * 1000 / 4.37; }

static ErbFilter erbFilter(double fc, size_t N, double sampleRate)
      {
      const double binHz = sampleRate / double(N);
      const double p = 4 * fc / (24.7 * (4.37 * fc / 1000 + 1));
      ErbFilter flt;
      flt.fc = fc;
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
      return flt;
      }

// the centre frequencies (one ERB apart)
static std::vector<double> erbCentres(double sampleRate)
      {
      std::vector<double> fcs;
      for (double e = erbRate(50); e <= erbRate(std::min(15000.0, sampleRate * 0.45)); e += 1.0)
            fcs.push_back(erbFreq(e));
      return fcs;
      }

// a Hann window of n points
static std::vector<double> hann(size_t n)
      {
      std::vector<double> window(n);
      for (size_t i = 0; i < n; ++i)
            window[i] = 0.5 - 0.5 * std::cos(2 * PI * double(i) / double(n - 1));
      return window;
      }

// (a threshold: a filter barely excited adds next to nothing, as in hearing; A ~60 dB under
// a -20 dBFS tone at a 2048-point FFT)
static const double LOUDNESS_A = 1e-3;

static double specificLoudness(double e)
      {
      return std::pow(e + LOUDNESS_A, 0.3) - std::pow(LOUDNESS_A, 0.3);
      }

//---------------------------------------------------------
//   perceivedLoudnessDb
//---------------------------------------------------------

std::vector<double> ArticulationCheck::perceivedEnvelope(const std::vector<float>& clip, double sampleRate, double* firstMs)
      {
      std::vector<double> out;
      const size_t frames = clip.size() / 2;
      if (firstMs)
            *firstMs = sampleRate > 0 ? 1024 * 1000.0 / sampleRate : 0;
      if (frames == 0 || sampleRate <= 0)
            return out;
      const std::vector<double> x = kWeighted(clip, sampleRate);
      // (2048 at 48 kHz: 23 Hz bins; 43 ms, short enough for a short note)
      const size_t N = 2048;
      const size_t hop = std::max<size_t>(1, size_t(sampleRate * 0.005));
      const double dt = double(hop) / sampleRate;
      const double attack = 1 - std::exp(-dt / 0.022), release = 1 - std::exp(-dt / 0.050);
      const std::vector<double> window = hann(N);
      std::vector<ErbFilter> filters;
      for (double fc : erbCentres(sampleRate)) {
            ErbFilter flt = erbFilter(fc, N, sampleRate);
            if (!flt.w.empty())
                  filters.push_back(std::move(flt));
            }
      std::vector<std::complex<double>> spec(N);
      std::vector<double> power(N / 2);
      double stl = 0;
      for (size_t start = 0; start + N <= frames + N / 2; start += hop) {
            for (size_t i = 0; i < N; ++i) {
                  const size_t j = start + i;
                  spec[i] = j < frames ? x[j] * window[i] : 0.0;
                  }
            fft(spec);
            for (size_t k = 0; k < N / 2; ++k)
                  power[k] = std::norm(spec[k]);
            double loud = 0;
            for (const ErbFilter& flt : filters) {
                  double e = 0;
                  for (size_t i = 0; i < flt.w.size(); ++i)
                        e += flt.w[i] * power[flt.from + i];
                  loud += specificLoudness(e);
                  }
            stl += (loud > stl ? attack : release) * (loud - stl);
            out.push_back(stl > 0 ? 33.2 * std::log10(stl) : -200);
            }
      return out;
      }

double ArticulationCheck::perceivedLoudnessDb(const std::vector<float>& clip, double sampleRate)
      {
      double peak = -200;
      for (double x : perceivedEnvelope(clip, sampleRate))
            peak = std::max(peak, x);
      return peak;
      }

//---------------------------------------------------------
//   attackSalience
//    see articulationcheck.h. Loudness as the ear resolves it in time: the same auditory filters (one
//    ERB apart, 50 Hz to 15 kHz) as 4th-order gammatone filters in the time domain (Patterson et al. 1992;
//    complex one-pole cascade, Hohmann 2002), whose ringing is the ear's own time resolution (~5 ms at
//    1 kHz), rather than FFT frames (a short window smears a steady tone over more filters and made it
//    3 dB louder). Each filter's power, averaged per 1 ms, on perceivedLoudnessDb's power scale (a tone at
//    a filter's centre gives the rounded exponential's output there), compressed the same way, summed:
//    Glasberg & Moore (2002)'s instantaneous loudness, smoothed only by the ear's temporal window (an
//    equivalent rectangular duration of ~8 ms: Plack & Moore 1990; here a one-pole attack of 5 ms,
//    release 50 ms) instead of short-term loudness's 22 ms attack, which smooths a bow's or a tongue's
//    transient away. Salience: each filter's specific loudness weighted by Zwicker's sharpness weighting
//    g(z) (DIN 45692: 1 up to 15.8 Bark, then 0.15 e^(0.42 (z - 15.8)) + 0.85), since a brighter onset
//    stands out more (Huang & Elhilali 2017: salient events are sudden rises in loudness and in
//    brightness, the spectral centroid)
//---------------------------------------------------------

ArticulationCheck::Attack ArticulationCheck::attackSalience(const std::vector<float>& clip, double sampleRate)
      {
      Attack out;
      const size_t frames = clip.size() / 2;
      if (frames == 0 || sampleRate <= 0)
            return out;
      const std::vector<double> x = kWeighted(clip, sampleRate);
      // perceivedLoudnessDb's scale: a sine of amplitude a at a filter's centre gives it N S a^2 / 4
      // (Parseval over the positive bins; S: the Hann window's sum of squares), its mean power a^2 / 2
      const size_t N0 = 2048;
      double s0 = 0;
      for (double w : hann(N0))
            s0 += w * w;
      const double scale = double(N0) * s0 / 2;
      struct Gammatone {
            std::complex<double> a;       // the pole
            double gain;                  // per stage: 1 at the centre
            std::complex<double> y[4];
            double g;                     // the sharpness weighting
            double power { 0 };           // this 1 ms
            };
      std::vector<Gammatone> bank;
      for (double fc : erbCentres(sampleRate)) {
            const double erb = 24.7 * (4.37 * fc / 1000 + 1);
            const double b = 1.019 * erb;                   // (order 4: ERB = 0.982 b)
            const double lambda = std::exp(-2 * PI * b / sampleRate);
            Gammatone gt;
            gt.a = std::polar(lambda, 2 * PI * fc / sampleRate);
            gt.gain = 1 - lambda;
            for (auto& y : gt.y)
                  y = 0;
            const double z = 13 * std::atan(0.00076 * fc) + 3.5 * std::atan((fc / 7500) * (fc / 7500));
            gt.g = z <= 15.8 ? 1.0 : 0.15 * std::exp(0.42 * (z - 15.8)) + 0.85;
            bank.push_back(gt);
            }
      const size_t hop = std::max<size_t>(1, size_t(sampleRate * 0.001));
      const double dt = double(hop) / sampleRate;
      const double attack = 1 - std::exp(-dt / 0.005), release = 1 - std::exp(-dt / 0.050);
      double fast = 0, salient = 0, fastPeak = 0, salientPeak = 0;
      std::vector<double> envelope;       // the fast loudness, every hop
      const size_t total = frames + size_t(sampleRate * 0.02);      // (and the filters' ringing)
      for (size_t start = 0; start < total; start += hop) {
            const size_t end = std::min(total, start + hop);
            for (Gammatone& gt : bank) {
                  double p = 0;
                  for (size_t i = start; i < end; ++i) {
                        std::complex<double> v = i < frames ? x[i] : 0.0;
                        for (auto& y : gt.y) {
                              y = gt.gain * v + gt.a * y;
                              v = y;
                              }
                        p += std::norm(v);
                        }
                  // (the complex filter passes the positive frequencies only: |y|^2 is half the power)
                  gt.power = 2 * p / double(end - start);
                  }
            double loud = 0, weighted = 0;
            for (const Gammatone& gt : bank) {
                  const double n = specificLoudness(scale * gt.power);
                  loud += n;
                  weighted += gt.g * n;
                  }
            fast += (loud > fast ? attack : release) * (loud - fast);
            salient += (weighted > salient ? attack : release) * (weighted - salient);
            fastPeak = std::max(fastPeak, fast);
            salientPeak = std::max(salientPeak, salient);
            envelope.push_back(fast);
            }
      if (fastPeak <= 0)
            return out;
      out.fastDb = 33.2 * std::log10(fastPeak);
      out.salienceDb = 33.2 * std::log10(salientPeak);
      // the rise: the fast loudness from 10 % to 90 % of its peak (the attack time of timbre studies,
      // McAdams et al. 1995, Peeters et al. 2011, on loudness rather than energy)
      size_t i10 = envelope.size(), i90 = envelope.size();
      for (size_t i = 0; i < envelope.size(); ++i) {
            if (i10 == envelope.size() && envelope[i] >= 0.1 * fastPeak)
                  i10 = i;
            if (envelope[i] >= 0.9 * fastPeak) {
                  i90 = i;
                  break;
                  }
            }
      if (i10 <= i90 && i90 < envelope.size())
            out.riseMs = double(i90 - i10) * dt * 1000;
      return out;
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
//   drumIcons
//    the drum icons of a Kickstart patch's window (SSO's percussion, in MuseScore's editor window,
//    1377 x 679 at 100 %): right-aligned on a grid of 100 pixels, each with its name under it (the
//    owner's pictures of the six percussion ensembles, 2026-09-27 20:15 and 2026-09-28 04:41: 4, 5, 6, 8, 8
//    and 9 icons, as many drums as each patch's file has). Their centres, right to left, while a name is there
//---------------------------------------------------------

std::vector<QPoint> ArticulationCheck::drumIcons(const QImage& window)
      {
      std::vector<QPoint> icons;
      // (Kontakt's whole window only: while it opens it is drawn at another size first, 1010 x 647 on the
      // owner's run of 2026-09-28 04:41, where the grid is elsewhere)
      const double aspect = window.height() > 0 ? double(window.width()) / window.height() : 0;
      if (window.width() < 400 || window.height() < 300 || aspect < 1.9 || aspect > 2.15)
            return icons;
      const QImage img = window.convertToFormat(QImage::Format_RGB32);
      const double sx = img.width() / 1377.0;
      const double sy = img.height() / 679.0;
      for (int k = 0; k < 12; ++k) {
            const int cx = int(std::lround((1303 - 100 * k) * sx));
            if (cx - 45 * sx < 365 * sx)
                  break;                              // (the panel's left edge)
            int label = 0;                            // light grey pixels in the name's row
            for (int y = int(std::lround(438 * sy)); y < int(std::lround(462 * sy)); ++y) {
                  const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
                  for (int x = int(std::lround(cx - 45 * sx)); x < int(std::lround(cx + 45 * sx)); ++x) {
                        const QRgb c = line[x];
                        const int lo = std::min({ qRed(c), qGreen(c), qBlue(c) });
                        const int hi = std::max({ qRed(c), qGreen(c), qBlue(c) });
                        label += lo > 150 && hi - lo < 30;
                        }
                  }
            if (label < 12)
                  break;
            icons.push_back(QPoint(cx, int(std::lround(400 * sy))));
            }
      return icons;
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
      ArticulationCheck::Attack attack;   // ArticulationCheck::attackSalience (measureAttack only)
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
      bool measureAttack { false };       // the dynamics measurement: the attack's salience too

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

      // the switch to value (after prior, when there is one), the dynamics and expression CCs, 0.1 s
      void arm(int prior, int value)
            {
            if (prior >= 0 && s.switchCC >= 0 && !s.switchIsKey) {
                  p->midi(ME_CONTROLLER, s.channel, s.switchCC, prior);
                  render(frames(0.1));
                  }
            if (s.switchIsKey && value >= 0 && value <= 127) {
                  p->midi(ME_NOTEON, s.channel, value, 100);
                  render(frames(0.05));
                  p->midi(ME_NOTEON, s.channel, value, 0);
                  render(frames(0.05));
                  }
            else if (s.switchCC >= 0 && value >= 0)       // (-1: a patch without switching, or no value)
                  p->midi(ME_CONTROLLER, s.channel, s.switchCC, value);
            if (s.dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, s.channel, s.dynamicsCC, s.dynamicsValue);
            if (s.expressionCC >= 0 && s.expressionCC != s.dynamicsCC)
                  p->midi(ME_CONTROLLER, s.channel, s.expressionCC, qBound(0, s.expressionValue, 127));
            render(frames(0.1));
            }

      Clip play(int prior, int value, int pitch)
            {
            settle();
            arm(prior, value);
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
            if (measureAttack)
                  c.attack = ArticulationCheck::attackSalience(clip, s.sampleRate);
            c.features = ArticulationCheck::features(clip, frames(s.note), s.sampleRate);
            return c;
            }

      // a note at velocity = dynamics CC = level, held seconds, then its tail: until it is 50 dB under its loudest
      // (and under the level before the release), at most tailMax seconds. offFrame: where the release is;
      // residualDb: the level just before the note-on (what is left of the last note's tail)
      std::vector<float> held(int value, int pitch, int level, double seconds, double tailMax, size_t* offFrame,
                              double* residualDb = nullptr);
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
      player.measureAttack = true;
      int done = 0;
      const int total = 8 * n;                  // (about: 3 to classify, 2 to 7 more; 7 for a held note's volume)
      double pdb = -200;                  // the last note's perceived loudness
      ArticulationCheck::Attack patk;     // and its attack
      auto at = [&](int value, int pitch, int velocity, int cc, double* db) {
            player.s.velocity = qBound(1, velocity, 127);
            player.s.dynamicsValue = qBound(0, cc, 127);
            const Clip c = player.play(-1, value, pitch);
            *db = c.loudDb;
            pdb = c.perceivedDb;
            patk = c.attack;
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
            ArticulationCheck::Attack a32, acc127;
            for (int shift : { 0, 12, -12, 7, -5, 24 }) {
                  const int p = pitches[i] + shift;
                  if (shift && (p < settings.minPitch || p > settings.maxPitch))
                        continue;
                  if (!at(r.value, p, 32, 32, &d32))
                        return out;
                  p32 = pdb;
                  a32 = patk;
                  if (!at(r.value, p, 32, 127, &cc127))
                        return out;
                  pcc127 = pdb;
                  acc127 = patk;
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
                        ArticulationCheck::Attack ak = a32;
                        if (x != 32) {
                              if (!at(r.value, r.pitch, x, x, &db))
                                    return out;
                              pd = pdb;
                              ak = patk;
                              }
                        r.curve.push_back({ x, db });
                        r.perceived.push_back({ x, pd });
                        r.attack.push_back({ x, ak.salienceDb });
                        r.riseMs.push_back({ x, ak.riseMs });
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
                  const ArticulationCheck::Attack a80 = patk;
                  if (!at(r.value, r.pitch, 112, 112, &d112))
                        return out;
                  p112 = pdb;
                  const ArticulationCheck::Attack a112 = patk;
                  r.curve = { { 32, d32 }, { 80, d80 }, { 112, d112 }, { 127, cc127 } };
                  r.perceived = { { 32, p32 }, { 80, p80 }, { 112, p112 }, { 127, pcc127 } };
                  r.attack = { { 32, a32.salienceDb }, { 80, a80.salienceDb }, { 112, a112.salienceDb }, { 127, acc127.salienceDb } };
                  r.riseMs = { { 32, a32.riseMs }, { 80, a80.riseMs }, { 112, a112.riseMs }, { 127, acc127.riseMs } };
                  }
            out.push_back(r);
            }
      player.settle();
      return out;
      }

//---------------------------------------------------------
//   timing
//---------------------------------------------------------

constexpr double ArticulationCheck::HOLD_SECONDS;
constexpr double ArticulationCheck::TAIL_SECONDS;
constexpr int ArticulationCheck::LEGATO_OVERLAP_MS;
constexpr int ArticulationCheck::LEGATO_VELOCITIES[3];
constexpr int ArticulationCheck::LEGATO_INTERVALS[2];

static constexpr double WINDOW_MS = 5.0;

std::vector<double> ArticulationCheck::envelope(const std::vector<float>& clip, double sampleRate)
      {
      std::vector<double> env;
      const size_t win = size_t(sampleRate * WINDOW_MS / 1000.0);
      if (win == 0)
            return env;
      for (size_t from = 0; 2 * (from + win) <= clip.size(); from += win) {
            double sum = 0;
            for (size_t i = 2 * from; i < 2 * (from + win); ++i)
                  sum += double(clip[i]) * clip[i];
            env.push_back(db(sum / double(2 * win)));
            }
      return env;
      }

std::vector<float> Player::held(int value, int pitch, int level, double seconds, double tailMax, size_t* offFrame, double* residualDb)
      {
      const double sr = s.sampleRate;
      settle();
      s.dynamicsValue = level;
      arm(-1, value);
      if (residualDb) {             // (arm's last 0.1 s)
            double peak = 0;
            for (float x : buffer)
                  peak = std::max(peak, double(std::fabs(x)));
            *residualDb = db(peak * peak);
            }
      lastPitch = pitch;
      p->midi(ME_NOTEON, s.channel, pitch, qBound(1, level, 127));
      std::vector<float> clip = render(frames(seconds));
      p->midi(ME_NOTEON, s.channel, pitch, 0);
      if (offFrame)
            *offFrame = clip.size() / 2;
      const std::vector<double> heldEnv = ArticulationCheck::envelope(clip, sr);
      double loudest = -200;
      for (double e : heldEnv)
            loudest = std::max(loudest, e);
      const size_t k = heldEnv.size() > 20 ? heldEnv.size() - 20 : 0;       // (the 100 ms before the release)
      double before = -200;
      for (size_t i = k; i < heldEnv.size(); ++i)
            before = std::max(before, heldEnv[i]);
      const double floorDb = std::max(-95.0, std::min(loudest - 50, before - 40));
      const int block = frames(0.1);
      for (double t = 0; t < tailMax; t += 0.1) {
            const std::vector<float>& b = render(block);
            clip.insert(clip.end(), b.begin(), b.end());
            double peak = 0;
            for (float x : b)
                  peak = std::max(peak, double(std::fabs(x)));
            if (db(peak * peak) < floorDb)
                  break;
            }
      lastLoudDb = loudest;
      return clip;
      }

//---------------------------------------------------------
//   legatoPair
//    two notes slurred as MuseScore plays them (the second starts, the first ends LEGATO_OVERLAP_MS later)
//    at velocity, a then b: when the pitch leaves the first, arrives at the second, the level's dip
//    (ArticulationCheck::timing, ::rest)
//---------------------------------------------------------

// the pitch spectrum of a note of a at velocity held 1.2 s, 0.6 to 1.1 s of it (legatoPair's reference)
static std::vector<double> referenceSpectrum(Player& player, int value, int a, int velocity)
      {
      const double sr = player.s.sampleRate;
      player.settle();
      player.s.dynamicsValue = 80;
      player.arm(-1, value);
      player.p->midi(ME_NOTEON, player.s.channel, a, velocity);
      const std::vector<float> note = player.render(player.frames(1.2));
      player.p->midi(ME_NOTEON, player.s.channel, a, 0);
      player.lastPitch = a;
      const std::vector<float> reference(note.begin() + 2 * player.frames(0.6), note.begin() + 2 * player.frames(1.1));
      return PluginExtract::pitchSpectrum(reference, sr);
      }

// firstSeconds: how long the first note is held before the second note-on (shorter than 1.1 s: reference, the
// first note's spectrum from referenceSpectrum)
static ArticulationCheck::TimingResult::Legato legatoPair(Player& player, int value, int a, int b, int velocity,
                                                          double firstSeconds = 1.2, const std::vector<double>* reference = nullptr)
      {
      const double sr = player.s.sampleRate;
      const size_t hop = size_t(sr * 0.010);
      const size_t frame = size_t(sr * 0.080);
      ArticulationCheck::TimingResult::Legato l;
      l.velocity = velocity;
      l.interval = b - a;
      l.firstMs = std::round(firstSeconds * 1000);
      const int interval = b - a;
      player.settle();
      player.s.dynamicsValue = 80;
      player.arm(-1, value);
      player.p->midi(ME_NOTEON, player.s.channel, a, velocity);
      const std::vector<float> first = player.render(player.frames(firstSeconds));
      player.p->midi(ME_NOTEON, player.s.channel, b, velocity);
      std::vector<float> second = player.render(player.frames(ArticulationCheck::LEGATO_OVERLAP_MS / 1000.0));
      player.p->midi(ME_NOTEON, player.s.channel, a, 0);
      const std::vector<float>& rest = player.render(player.frames(1.4));
      second.insert(second.end(), rest.begin(), rest.end());
      player.p->midi(ME_NOTEON, player.s.channel, b, 0);
      player.lastPitch = b;
      // the first note's pitch: 0.6 to 1.1 s of it
      std::vector<double> referenceSpectrum;
      if (reference)
            referenceSpectrum = *reference;
      else if (firstSeconds >= 1.1) {
            const std::vector<float> ref(first.begin() + 2 * player.frames(0.6), first.begin() + 2 * player.frames(1.1));
            referenceSpectrum = PluginExtract::pitchSpectrum(ref, sr);
            }
      if (referenceSpectrum.empty())
            return l;
      // frames centred 0 … 800 ms after the second note-on (the first 40 ms from before it)
      std::vector<float> joined(first.end() - 2 * std::ptrdiff_t(frame / 2), first.end());
      joined.insert(joined.end(), second.begin(), second.end());
      int arrivedRun = 0;
      for (size_t c = 0; 2 * (c * hop + frame) <= joined.size() && c * 10 <= 800; ++c) {
            const std::vector<float> f(joined.begin() + 2 * std::ptrdiff_t(c * hop),
                                       joined.begin() + 2 * std::ptrdiff_t(c * hop + frame));
            double confidence = 0;
            const double cents = PluginExtract::centsShift(referenceSpectrum, f, sr, 1300, &confidence);
            if (confidence < 0.3)
                  continue;
            const int t = int(c * 10);
            l.cents.push_back({ t, std::round(cents) });
            if (l.leaveMs < 0 && std::fabs(cents) > 35)
                  l.leaveMs = t;
            if (l.arriveMs < 0) {
                  if (std::fabs(cents - 100.0 * interval) < 35) {
                        if (++arrivedRun == 3)
                              l.arriveMs = t - 20;
                        }
                  else
                        arrivedRun = 0;
                  }
            }
      // the dip: the quietest 5 ms in the first 600 ms against the second note after 800 ms
      const std::vector<double> e = ArticulationCheck::envelope(second, sr);
      double dip = 200, own = -200;
      for (size_t k = 0; k < e.size(); ++k) {
            const double t = k * WINDOW_MS;
            if (t < 600)
                  dip = std::min(dip, e[k]);
            else if (t >= 800 && t < 1200)
                  own = std::max(own, e[k]);
            }
      if (own > SILENT_DB)
            l.dipDb = std::min(0.0, std::round((dip - own) * 10) / 10);
      return l;
      }

namespace {

// the loudest window, and the windows from the note-on to 30 dB, 6 dB under it and to it
struct Onset {
      double peakDb { -200 };
      int peak { -1 }, start { -1 }, full { -1 };
      };

Onset onset(const std::vector<double>& env, size_t until)
      {
      Onset o;
      until = std::min(until, env.size());
      for (size_t i = 0; i < until; ++i)
            if (env[i] > o.peakDb) {
                  o.peakDb = env[i];
                  o.peak = int(i);
                  }
      for (size_t i = 0; o.peak >= 0 && i <= size_t(o.peak); ++i) {
            if (o.start < 0 && env[i] >= o.peakDb - 30)
                  o.start = int(i);
            if (o.full < 0 && env[i] >= o.peakDb - 6)
                  o.full = int(i);
            }
      return o;
      }

// the last window from..to at level or louder (-1: none)
int lastAbove(const std::vector<double>& env, size_t from, size_t to, double level)
      {
      int last = -1;
      for (size_t i = from; i < std::min(to, env.size()); ++i)
            if (env[i] >= level)
                  last = int(i);
      return last;
      }

} // namespace

std::vector<ArticulationCheck::TimingResult> ArticulationCheck::timing(Vst3Plugin* plugin, const std::vector<int>& values,
   const std::vector<int>& pitches, const std::vector<bool>& legato, const Settings& settings, Progress progress)
      {
      std::vector<TimingResult> out;
      const int n = int(values.size());
      if (!plugin || n == 0 || int(pitches.size()) != n || int(legato.size()) != n)
            return out;
      Player player { plugin, settings, {} };
      player.relativeSettle = true;
      const double sr = settings.sampleRate;
      auto ms = [](int windows) { return windows < 0 ? -1.0 : windows * WINDOW_MS; };
      int done = 0;
      int total = 0;
      for (int i = 0; i < n; ++i)
            total += 4 + (legato[size_t(i)] ? 6 : 0);
      auto step = [&]() {
            ++done;
            return !progress || progress(done, std::max(total, done));
            };
      auto note = [&](int value, int pitch, int level, double seconds, double tailMax, size_t* offFrame) {
            return player.held(value, pitch, level, seconds, tailMax, offFrame);
            };

      for (int i = 0; i < n; ++i) {
            TimingResult r;
            r.value = values[size_t(i)];
            // mf, held: its start, length and release; at another pitch where the test pitch plays nothing
            std::vector<double> env;
            size_t off = 0;
            for (int shift : { 0, 12, -12, 7, -5, 24 }) {
                  const int p = pitches[size_t(i)] + shift;
                  if (shift && (p < settings.minPitch || p > settings.maxPitch))
                        continue;
                  const std::vector<float> clip = note(r.value, p, 80, HOLD_SECONDS, TAIL_SECONDS, &off);
                  if (!step())
                        return out;
                  env = envelope(clip, sr);
                  const Onset o = onset(env, env.size());
                  if (o.peakDb > SILENT_DB) {
                        r.pitch = p;
                        break;
                        }
                  }
            if (r.pitch < 0) {
                  out.push_back(r);
                  continue;
                  }
            const size_t offWindow = size_t(double(off) / (sr * WINDOW_MS / 1000.0));
            const Onset mf = onset(env, env.size());
            r.startMs[1] = ms(mf.start);
            r.fullMs[1] = ms(mf.full);
            r.peakMs[1] = ms(mf.peak);
            r.peakDb[1] = mf.peakDb;
            // (a short rings on in the room 40 dB down for seconds, the owner's first run of 2026-09-29: Spiccato 2 s;
            // within 20 dB is the note itself, and a note still that loud at its release sustains)
            double before = -200;
            for (size_t k = offWindow > 20 ? offWindow - 20 : 0; k < offWindow && k < env.size(); ++k)
                  before = std::max(before, env[k]);
            r.sustains = before >= mf.peakDb - 20;
            r.bodyMs = ms(lastAbove(env, 0, env.size(), mf.peakDb - 20) + 1);
            r.lengthMs = ms(lastAbove(env, 0, env.size(), mf.peakDb - 40) + 1);
            if (r.sustains) {
                  const int rel = lastAbove(env, offWindow, env.size(), before - 30);
                  // (still within 30 dB at the tail's end: longer than the tail)
                  if (rel + 1 < int(env.size()))
                        r.releaseMs = ms(std::max(0, rel + 1 - int(offWindow)));
                  }
            // pp and ff: the start only (1 s, a short tail)
            for (int k : { 0, 2 }) {
                  const std::vector<float> clip = note(r.value, r.pitch, k == 0 ? 32 : 112, 1.0, 0.3, nullptr);
                  if (!step())
                        return out;
                  const std::vector<double> e = envelope(clip, sr);
                  const Onset o = onset(e, e.size());
                  if (o.peakDb > SILENT_DB) {
                        r.startMs[k] = ms(o.start);
                        r.fullMs[k] = ms(o.full);
                        r.peakMs[k] = ms(o.peak);
                        r.peakDb[k] = o.peakDb;
                        }
                  }
            // a 0.1 s note
            {
                  const std::vector<float> clip = note(r.value, r.pitch, 80, 0.1, 3.0, nullptr);
                  if (!step())
                        return out;
                  const std::vector<double> e = envelope(clip, sr);
                  const Onset o = onset(e, e.size());
                  if (o.peakDb > SILENT_DB) {
                        r.shortNoteBodyMs = ms(lastAbove(e, 0, e.size(), o.peakDb - 20) + 1);
                        r.shortNoteMs = ms(lastAbove(e, 0, e.size(), o.peakDb - 40) + 1);
                        }
            }
            // legato: two slurred notes
            if (legato[size_t(i)]) {
                  for (int velocity : LEGATO_VELOCITIES) {
                        for (int interval : LEGATO_INTERVALS) {
                              const int a = r.pitch, b = r.pitch + interval;
                              if (b < settings.minPitch || b > settings.maxPitch || b < 0 || b > 127) {
                                    if (!step())
                                          return out;
                                    continue;
                                    }
                              const TimingResult::Legato l = legatoPair(player, r.value, a, b, velocity);
                              if (!step())
                                    return out;
                              r.legato.push_back(l);
                              }
                        }
                  }
            out.push_back(r);
            }
      player.settle();
      return out;
      }

//---------------------------------------------------------
//   rest
//---------------------------------------------------------

constexpr double ArticulationCheck::MF_SECONDS;
constexpr double ArticulationCheck::PP_FF_SECONDS;
constexpr int ArticulationCheck::REST_LEGATO_VELOCITIES[9];
constexpr int ArticulationCheck::REST_LEGATO_INTERVALS[14];
constexpr double ArticulationCheck::ONSET_SECONDS;
constexpr double ArticulationCheck::ONSET_DROPS[4];
constexpr double ArticulationCheck::SHORT_SECONDS[6];
constexpr double ArticulationCheck::DECAY_DROPS[4];
constexpr double ArticulationCheck::LEGATO_FIRST_SECONDS[5];
constexpr int ArticulationCheck::LEGATO_LENGTH_INTERVALS[6];

namespace {

// the peak of env (values every 5 ms from firstMs) up to untilMs, its time and the first times within drops dB of it
void onsetTimes(const std::vector<double>& env, double firstMs, double untilMs, const double (&drops)[4], double* peakMs,
                double (&times)[4])
      {
      double peak = -200;
      int at = -1;
      for (size_t i = 0; i < env.size() && firstMs + 5.0 * i <= untilMs; ++i)
            if (env[i] > peak) {
                  peak = env[i];
                  at = int(i);
                  }
      if (at < 0 || peak <= -199)
            return;
      if (peakMs)
            *peakMs = firstMs + 5.0 * at;
      for (int k = 0; k < 4; ++k)
            for (int i = 0; i <= at; ++i)
                  if (env[size_t(i)] >= peak - drops[k]) {
                        times[k] = firstMs + 5.0 * i;
                        break;
                        }
      }

// the peak of env and the last times within drops dB of it (the window after: until then it sounded)
double decayTimes(const std::vector<double>& env, double firstMs, const double (&drops)[4], double* peakMs, double (&times)[4])
      {
      double peak = -200;
      int at = -1;
      for (size_t i = 0; i < env.size(); ++i)
            if (env[i] > peak) {
                  peak = env[i];
                  at = int(i);
                  }
      if (at < 0 || peak <= -199)
            return peak;
      if (peakMs)
            *peakMs = firstMs + 5.0 * at;
      for (int k = 0; k < 4; ++k)
            for (int i = int(env.size()) - 1; i >= at; --i)
                  if (env[size_t(i)] >= peak - drops[k]) {
                        times[k] = firstMs + 5.0 * (i + 1);
                        break;
                        }
      return peak;
      }

} // namespace

ArticulationCheck::RestResult ArticulationCheck::rest(Vst3Plugin* plugin, int value, int pitch, bool legato, int controls,
                                                      SetControl setControl, const Settings& settings, const RestSettings& rs,
                                                      Progress progress)
      {
      RestResult r;
      r.value = value;
      if (!plugin)
            return r;
      Player player { plugin, settings, {} };
      player.relativeSettle = true;
      const double sr = settings.sampleRate;
      auto ms = [](int windows) { return windows < 0 ? -1.0 : windows * WINDOW_MS; };
      int done = 0;
      auto step = [&]() {
            ++done;
            return !progress || progress(done, 0);
            };
      // the notes (a deque: they stay where they are), their perceived loudness and attack worked out on other threads
      // while the plug-in plays the next ones (half the time of a note; the owner's VM has 4 processors)
      std::deque<NoteStats> notes;
      std::deque<std::future<void>> pending;
      const size_t workers = std::max(1u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
      auto finish = [&]() {
            for (std::future<void>& f : pending)
                  f.wait();
            pending.clear();
            };
      // one note: held seconds, at mf with its tail (how long it sounds, its release), else a short one
      auto measure = [&](int p, int level, bool mf, NoteStats** out, double seconds = -1) {
            notes.emplace_back();
            NoteStats* n = &notes.back();
            *out = n;
            n->pitch = p;
            n->level = level;
            size_t off = 0;
            double residual = -200;
            std::vector<float> clip = player.held(value, p, level, seconds > 0 ? seconds : mf ? MF_SECONDS : PP_FF_SECONDS,
                                                  mf ? TAIL_SECONDS : 0.3, &off, &residual);
            const std::vector<double> env = envelope(clip, sr);
            const Onset o = onset(env, env.size());
            n->sounds = o.peakDb > SILENT_DB && o.peakDb > residual + 10;
            if (n->sounds) {
                  const size_t win = size_t(2 * player.frames(0.05));
                  double loudest = 0;
                  for (size_t from = 0; win > 0 && from + win <= clip.size(); from += win / 2) {
                        double sum = 0;
                        for (size_t i = from; i < from + win; ++i)
                              sum += double(clip[i]) * clip[i];
                        loudest = std::max(loudest, sum / win);
                        }
                  n->loudDb = db(loudest);
                  n->startMs = ms(o.start);
                  n->fullMs = ms(o.full);
                  n->peakMs = ms(o.peak);
                  onsetTimes(env, 0, ONSET_SECONDS * 1000, ONSET_DROPS, nullptr, n->energyOnsetMs);
                  if (mf) {
                        const size_t offWindow = size_t(double(off) / (sr * WINDOW_MS / 1000.0));
                        double before = -200;
                        for (size_t k = offWindow > 20 ? offWindow - 20 : 0; k < offWindow && k < env.size(); ++k)
                              before = std::max(before, env[k]);
                        n->sustains = before >= o.peakDb - 20;
                        n->bodyMs = ms(lastAbove(env, 0, env.size(), o.peakDb - 20) + 1);
                        if (n->sustains) {
                              const int rel = lastAbove(env, offWindow, env.size(), before - 30);
                              if (rel + 1 < int(env.size()))
                                    n->releaseMs = ms(std::max(0, rel + 1 - int(offWindow)));
                              }
                        }
                  // (perceived loudness and attack on the note and 0.3 s, as the dynamics' clips: the tail adds nothing)
                  clip.resize(std::min(clip.size(), 2 * off + 2 * size_t(player.frames(0.3))));
                  if (pending.size() >= workers) {
                        pending.front().wait();
                        pending.pop_front();
                        }
                  pending.push_back(std::async(std::launch::async, [n, sr](std::vector<float> head) {
                        double first = 0;
                        const std::vector<double> pe = perceivedEnvelope(head, sr, &first);
                        for (double x : pe)
                              n->perceivedDb = std::max(n->perceivedDb, x);
                        onsetTimes(pe, first, ONSET_SECONDS * 1000, ONSET_DROPS, &n->perceivedPeakMs, n->onsetMs);
                        const Attack a = attackSalience(head, sr);
                        n->salienceDb = a.salienceDb;
                        n->riseMs = a.riseMs;
                        }, std::move(clip)));
                  }
            return step();
            };
      auto avoided = [&](int p) {
            return p < rs.low || p > rs.high || p < 0 || p > 127 || std::find(rs.avoid.begin(), rs.avoid.end(), p) != rs.avoid.end();
            };
      // (the notes' numbers, once every analysis is done; and on a stop)
      std::vector<NoteStats*> range, repeats, onsetNotes;
      std::deque<ShortNote> shortNotes;
      std::vector<std::pair<std::pair<int, double>, NoteStats*>> controlPoints;
      auto result = [&]() {
            finish();
            for (NoteStats* n : range)
                  r.range.push_back(*n);
            for (NoteStats* n : repeats)
                  r.repeats.push_back(*n);
            for (NoteStats* n : onsetNotes)
                  r.onset.push_back(*n);
            for (const ShortNote& n : shortNotes)
                  r.shorts.push_back(n);
            for (const auto& c : controlPoints) {
                  RestResult::ControlPoint pt;
                  pt.control = c.first.first;
                  pt.value = c.first.second;
                  pt.note = *c.second;
                  r.controls.push_back(pt);
                  }
            return r;
            };

      // the test pitch: where it sounds at mf (as timing: another pitch where the given one is silent)
      NoteStats* home = nullptr;
      for (int shift : { 0, 12, -12, 7, -5, 24 }) {
            const int p = pitch + shift;
            if (avoided(p) || (shift && (p < settings.minPitch || p > settings.maxPitch)))
                  continue;
            NoteStats* n;
            if (!measure(p, 80, true, &n))
                  return result();
            if (n->sounds) {
                  r.pitch = p;
                  home = n;
                  break;
                  }
            }
      if (r.pitch < 0)
            return result();

      // every semitone from the test pitch down and up until silenceRun in a row are silent, pp / mf / ff; seconds > 0:
      // each held that long (onset), else as range
      auto walk = [&](std::vector<NoteStats*>& into, double seconds) {
            std::map<int, std::array<NoteStats*, 3>> byPitch;
            for (int direction : { -1, 1 }) {
                  int silentRun = 0;
                  for (int p = direction < 0 ? r.pitch : r.pitch + 1; !avoided(p) && silentRun < rs.silenceRun; p += direction) {
                        std::array<NoteStats*, 3> n { nullptr, nullptr, nullptr };
                        if (p == r.pitch && seconds <= 0)
                              n[1] = home;
                        else if (!measure(p, 80, true, &n[1], seconds))
                              return false;
                        byPitch[p] = n;
                        if (!n[1]->sounds) {
                              ++silentRun;
                              continue;
                              }
                        silentRun = 0;
                        if (!measure(p, 32, false, &n[0], seconds) || !measure(p, 112, false, &n[2], seconds))
                              return false;
                        byPitch[p] = n;
                        }
                  }
            // (by pitch; pp / mf / ff, a silent pitch: mf only)
            for (const auto& b : byPitch)
                  for (NoteStats* n : b.second)
                        if (n)
                              into.push_back(n);
            return true;
            };
      if (rs.range && !walk(range, -1))
            return result();
      if (rs.onset && !walk(onsetNotes, ONSET_SECONDS))
            return result();
      if (rs.shorts) {
            // the test pitch, an octave (else a fifth) under and over it, where it sounds
            auto shortNote = [&](int p, double seconds, bool* sounds) {
                  size_t off = 0;
                  double residual = -200;
                  std::vector<float> clip = player.held(value, p, 80, seconds, TAIL_SECONDS, &off, &residual);
                  const std::vector<double> env = envelope(clip, sr);
                  const Onset o = onset(env, env.size());
                  *sounds = o.peakDb > SILENT_DB && o.peakDb > residual + 10;
                  if (!*sounds)
                        return step();
                  shortNotes.emplace_back();
                  ShortNote* n = &shortNotes.back();
                  n->pitch = p;
                  n->seconds = seconds;
                  const size_t win = size_t(2 * player.frames(0.05));
                  double loudest = 0;
                  for (size_t from = 0; win > 0 && from + win <= clip.size(); from += win / 2) {
                        double sum = 0;
                        for (size_t i = from; i < from + win; ++i)
                              sum += double(clip[i]) * clip[i];
                        loudest = std::max(loudest, sum / win);
                        }
                  n->loudDb = db(loudest);
                  double unused = -1;
                  decayTimes(env, 0, DECAY_DROPS, &unused, n->energyLastMs);
                  if (pending.size() >= workers) {
                        pending.front().wait();
                        pending.pop_front();
                        }
                  pending.push_back(std::async(std::launch::async, [n, sr](std::vector<float> c) {
                        double first = 0;
                        const std::vector<double> pe = perceivedEnvelope(c, sr, &first);
                        n->perceivedPeakDb = decayTimes(pe, first, DECAY_DROPS, &n->perceivedPeakMs, n->perceivedLastMs);
                        }, std::move(clip)));
                  return step();
                  };
            std::vector<int> pitches { r.pitch };
            for (const std::vector<int>& tries : { std::vector<int>{ -12, -7 }, std::vector<int>{ 12, 7 } }) {
                  for (int shift : tries) {
                        const int p = r.pitch + shift;
                        if (avoided(p) || p < settings.minPitch || p > settings.maxPitch)
                              continue;
                        bool sounds = false;
                        if (!shortNote(p, SHORT_SECONDS[2], &sounds))
                              return result();
                        if (sounds) {
                              pitches.push_back(p);
                              break;
                              }
                        }
                  }
            // (the probe notes above are 0.25 s ones of their pitch: kept, the rest added)
            for (int p : pitches) {
                  for (double seconds : SHORT_SECONDS) {
                        if (p != r.pitch && seconds == SHORT_SECONDS[2])
                              continue;
                        bool sounds = false;
                        if (!shortNote(p, seconds, &sounds))
                              return result();
                        }
                  }
            }
      if (rs.legatoLengths && legato) {
            const std::vector<double> reference = referenceSpectrum(player, value, r.pitch, LEGATO_LENGTH_VELOCITY);
            for (int interval : LEGATO_LENGTH_INTERVALS) {
                  const int b = r.pitch + interval;
                  if (avoided(b))
                        continue;
                  for (double first : LEGATO_FIRST_SECONDS) {
                        r.legatoLengths.push_back(legatoPair(player, value, r.pitch, b, LEGATO_LENGTH_VELOCITY, first, &reference));
                        if (!step())
                              return result();
                        }
                  }
            }
      if (rs.repeats) {
            for (int k = 0; k < rs.repeatCount; ++k) {
                  NoteStats* n;
                  if (!measure(r.pitch, 80, true, &n))
                        return result();
                  repeats.push_back(n);
                  }
            }
      if (rs.legato && legato) {
            for (int velocity : REST_LEGATO_VELOCITIES) {
                  for (int interval : REST_LEGATO_INTERVALS) {
                        const int b = r.pitch + interval;
                        if (avoided(b))
                              continue;
                        r.legato.push_back(legatoPair(player, value, r.pitch, b, velocity));
                        if (!step())
                              return result();
                        }
                  }
            }
      if (rs.controls && setControl) {
            for (int c = 0; c < controls; ++c) {
                  for (double v : rs.controlValues) {
                        player.settle();
                        if (!setControl(c, v))
                              return result();
                        player.render(player.frames(0.3));      // (the patch's script takes it)
                        NoteStats* n;
                        const bool go = measure(r.pitch, 80, true, &n);
                        controlPoints.push_back({ { c, v }, n });
                        if (!go) {
                              setControl(c, -1);
                              return result();
                              }
                        }
                  player.settle();
                  if (!setControl(c, -1))
                        return result();
                  player.render(player.frames(0.3));
                  }
            }
      player.settle();
      return result();
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
