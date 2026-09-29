//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PlaybackVerify: the analysis core of --verify-playback (see playbackverify.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "playbackverify.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iterator>
#include <map>
#include <cstdio>
#include <numeric>
#include <set>

namespace Ms {
namespace PlaybackVerify {

static const double PI = 3.14159265358979323846;

const char* kindName(Finding::Kind kind)
      {
      switch (kind) {
            case Finding::Kind::MissingAttack: return "missing-attack";
            case Finding::Kind::CutShort:      return "cut-short";
            case Finding::Kind::Silence:       return "silence";
            case Finding::Kind::Clipping:      return "clipping";
            case Finding::Kind::Drift:         return "drift";
            case Finding::Kind::MissingNote:   return "missing-note";
            }
      return "?";
      }

std::string pitchName(int pitch)
      {
      static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
      const int octave = pitch / 12 - 1;
      return std::string(names[((pitch % 12) + 12) % 12]) + std::to_string(octave);
      }

static double median(std::vector<double> v)
      {
      if (v.empty())
            return 0;
      const size_t m = v.size() / 2;
      std::nth_element(v.begin(), v.begin() + long(m), v.end());
      return v[m];
      }

static float medianF(std::vector<float> v)
      {
      if (v.empty())
            return 0;
      const size_t m = v.size() / 2;
      std::nth_element(v.begin(), v.begin() + long(m), v.end());
      return v[m];
      }

//---------------------------------------------------------
//   fft
//    in place, radix 2 (n a power of two)
//---------------------------------------------------------

static void fft(std::vector<std::complex<float>>& a)
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
            const std::complex<float> wl(float(std::cos(ang)), float(std::sin(ang)));
            for (size_t i = 0; i < n; i += len) {
                  std::complex<float> w(1, 0);
                  for (size_t k = 0; k < len / 2; ++k) {
                        const std::complex<float> u = a[i + k];
                        const std::complex<float> v = a[i + k + len / 2] * w;
                        a[i + k] = u + v;
                        a[i + k + len / 2] = u - v;
                        w *= wl;
                        }
                  }
            }
      }

//---------------------------------------------------------
//   Spectrogram
//---------------------------------------------------------

Spectrogram::Spectrogram(const std::vector<float>& mono, double rate)
      : _rate(rate)
      {
      _size = rate > 50000 ? 2048 : 1024;
      _hop = std::max(1, int(std::lround(rate * 0.01)));
      _bins = _size / 2 + 1;
      for (float x : mono)
            _peak = std::max(_peak, double(std::fabs(x)));
      const double norm = _peak > 0 ? 1.0 / _peak : 1.0;
      _pad = _size;
      _audio.resize(mono.size());
      for (size_t i = 0; i < mono.size(); ++i)
            _audio[i] = float(mono[i] * norm);
      std::vector<float> padded(size_t(_pad), 0.f);
      padded.insert(padded.end(), mono.begin(), mono.end());
      // G for a 1024-point frame: the same |X| of a tone at double the size is twice as large
      const float gain = float(GAIN * 1024.0 / _size * norm);
      const int n = padded.size() >= size_t(_size) ? int((padded.size() - size_t(_size)) / size_t(_hop)) + 1 : 0;
      _log.assign(size_t(n) * size_t(_bins), 0.f);
      _flux.assign(size_t(n), 0.f);
      _rms.assign(size_t(n), -200.f);
      std::vector<float> window(static_cast<size_t>(_size));
      for (int i = 0; i < _size; ++i)
            window[size_t(i)] = float(0.5 - 0.5 * std::cos(2 * PI * i / (_size - 1)));
      std::vector<std::complex<float>> buf(static_cast<size_t>(_size));
      for (int f = 0; f < n; ++f) {
            const float* x = padded.data() + size_t(f) * size_t(_hop);
            double sum = 0;
            for (int i = 0; i < _size; ++i) {
                  buf[size_t(i)] = std::complex<float>(x[i] * window[size_t(i)], 0.f);
                  sum += double(x[i]) * x[i];
                  }
            const double rms = std::sqrt(sum / _size) * norm;
            _rms[size_t(f)] = float(rms > 1e-10 ? 20 * std::log10(rms) : -200.0);
            fft(buf);
            float* out = _log.data() + size_t(f) * size_t(_bins);
            for (int k = 0; k < _bins; ++k)
                  out[k] = std::log1p(gain * std::abs(buf[size_t(k)]));
            if (f > 0) {
                  const float* prev = out - _bins;
                  float d = 0;
                  for (int k = 0; k < _bins; ++k)
                        d += std::max(0.f, out[k] - prev[k]);
                  _flux[size_t(f)] = d;
                  }
            }
      }

// the frame whose window is centred nearest the time
int Spectrogram::frameAt(double seconds) const
      {
      return int(std::lround((seconds * _rate + _pad - _size / 2.0) / _hop));
      }

double Spectrogram::timeOf(int frame) const
      {
      return (double(frame) * _hop + _size / 2.0 - _pad) / _rate;
      }

std::vector<int> Spectrogram::partialBins(const std::vector<int>& pitches, int harmonics, int spread) const
      {
      std::set<int> bins;
      for (int p : pitches) {
            const double f0 = 440.0 * std::pow(2.0, (p - 69) / 12.0);
            for (int k = 1; k <= harmonics; ++k) {
                  const double f = k * f0;
                  if (f > _rate * 0.45)
                        break;
                  const int b = int(std::lround(f * _size / _rate));
                  for (int d = -spread; d <= spread; ++d)
                        if (b + d > 0 && b + d < _bins)
                              bins.insert(b + d);
                  }
            }
      return std::vector<int>(bins.begin(), bins.end());
      }

std::vector<float> Spectrogram::pitchFlux(const std::vector<int>& bins, int from, int to) const
      {
      from = std::max(1, from);
      to = std::min(frames(), to);
      std::vector<float> r;
      if (to <= from)
            return r;
      r.reserve(size_t(to - from));
      for (int f = from; f < to; ++f) {
            const float* cur = frame(f);
            const float* prev = frame(f - 1);
            float d = 0;
            for (int b : bins)
                  d += std::max(0.f, cur[b] - prev[b]);
            r.push_back(d);
            }
      return r;
      }

// the partials' power (|X|^2, back from the log), mean or maximum over the frames from … to-1
double Spectrogram::power(const std::vector<int>& bins, int from, int to, bool maximum) const
      {
      from = std::max(0, from);
      to = std::min(frames(), to);
      if (to <= from || bins.empty())
            return 0;
      double best = 0, sum = 0;
      for (int f = from; f < to; ++f) {
            const float* cur = frame(f);
            double e = 0;
            for (int b : bins) {
                  const double m = std::expm1(double(cur[b]));
                  e += m * m;
                  }
            best = std::max(best, e);
            sum += e;
            }
      return maximum ? best : sum / (to - from);
      }

std::vector<int> Spectrogram::finePartialBins(const std::vector<int>& pitches, int harmonics, int spread) const
      {
      const int n = fineSize();
      std::set<int> bins;
      for (int p : pitches) {
            const double f0 = 440.0 * std::pow(2.0, (p - 69) / 12.0);
            for (int k = 1; k <= harmonics; ++k) {
                  const double f = k * f0;
                  if (f > _rate * 0.45)
                        break;
                  const int b = int(std::lround(f * n / _rate));
                  for (int d = -spread; d <= spread; ++d)
                        if (b + d > 0 && b + d < n / 2)
                              bins.insert(b + d);
                  }
            }
      return std::vector<int>(bins.begin(), bins.end());
      }

double Spectrogram::finePower(const std::vector<int>& bins, double seconds) const
      {
      const int n = fineSize();
      const long start = long(std::lround(seconds * _rate)) - n / 2;
      std::vector<std::complex<float>> buf(static_cast<size_t>(n));
      bool any = false;
      for (int i = 0; i < n; ++i) {
            const long k = start + i;
            const float x = k >= 0 && k < long(_audio.size()) ? _audio[size_t(k)] : 0.f;
            any = any || x != 0.f;
            buf[size_t(i)] = std::complex<float>(x * float(0.5 - 0.5 * std::cos(2 * PI * i / (n - 1))), 0.f);
            }
      if (!any)
            return 0;
      fft(buf);
      double e = 0;
      for (int b : bins)
            e += std::norm(buf[size_t(b)]);
      return e;
      }

//---------------------------------------------------------
//   strikes
//---------------------------------------------------------

std::vector<Strike> strikes(const std::vector<Note>& notes, double tolerance)
      {
      std::vector<int> order(notes.size());
      std::iota(order.begin(), order.end(), 0);
      std::stable_sort(order.begin(), order.end(), [&notes](int a, int b) { return notes[size_t(a)].on < notes[size_t(b)].on; });
      std::vector<Strike> result;
      for (int i : order) {
            const Note& n = notes[size_t(i)];
            if (result.empty() || n.on - result.back().time > tolerance) {
                  Strike s;
                  s.time = n.on;
                  result.push_back(s);
                  }
            result.back().notes.push_back(i);
            if (std::find(result.back().pitches.begin(), result.back().pitches.end(), n.pitch) == result.back().pitches.end())
                  result.back().pitches.push_back(n.pitch);
            }
      return result;
      }

//---------------------------------------------------------
//   bestOffset
//---------------------------------------------------------

static double onsetFlux(const Spectrogram& s, const std::vector<Strike>& strikes, double offset, size_t from = 0, size_t to = size_t(-1))
      {
      double sum = 0;
      const std::vector<float>& flux = s.flux();
      for (size_t i = from; i < std::min(to, strikes.size()); ++i) {
            const int f = s.frameAt(strikes[i].time + offset);
            if (f >= 0 && f < s.frames())
                  sum += flux[size_t(f)];
            }
      return sum;
      }

double bestOffset(const Spectrogram& s, const std::vector<Strike>& strikes, double from, double to, double step)
      {
      double best = from, bestSum = -1;
      for (double o = from; o <= to + 1e-9; o += step) {
            const double sum = onsetFlux(s, strikes, o);
            if (sum > bestSum) {
                  bestSum = sum;
                  best = o;
                  }
            }
      return best;
      }

//---------------------------------------------------------
//   measure
//    a strike's attack features at the offset
//---------------------------------------------------------

static void measure(const Spectrogram& s, Strike& k, double offset, const Settings& settings)
      {
      const double hop = s.hop();
      const int c = s.frameAt(k.time + offset);
      const int a = c - int(std::lround(settings.attackBefore / hop));
      const int b = c + int(std::lround(settings.attackAfter / hop));
      const int around = int(std::lround(1.5 / hop));
      const std::vector<float>& flux = s.flux();
      float peak = 0;
      for (int f = std::max(1, a); f <= b && f < s.frames(); ++f)
            peak = std::max(peak, flux[size_t(f)]);
      std::vector<float> ctx(flux.begin() + std::max(0, std::min(s.frames(), c - around)),
                             flux.begin() + std::max(0, std::min(s.frames(), c + around)));
      const float bg = medianF(ctx);
      k.broad = peak;
      k.local = bg > 0 ? peak / bg : 0;
      const std::vector<int> bins = s.partialBins(k.pitches);
      const int from = std::max(1, c - around);
      const std::vector<float> pf = s.pitchFlux(bins, from, c + around);
      float pp = 0;
      for (int f = std::max(1, a); f <= b; ++f)
            if (f - from >= 0 && f - from < int(pf.size()))
                  pp = std::max(pp, pf[size_t(f - from)]);
      const float pbg = std::max(medianF(pf), 0.05f * float(bins.size()));
      k.pitchLocal = pp / pbg;
      }

//---------------------------------------------------------
//   analyse
//---------------------------------------------------------

static std::string fmt(const char* f, double a, double b = 0, double c = 0, double d = 0)
      {
      char buf[256];
      snprintf(buf, sizeof(buf), f, a, b, c, d);
      return buf;
      }

Result analyse(const Spectrogram& s, const std::vector<Note>& notes, const Settings& settings,
               const Result* reference, const Spectrogram* ref)
      {
      Result r;
      r.peakDb = s.peak() > 0 ? 20 * std::log10(s.peak()) : -200;
      r.strikeList = strikes(notes, settings.chordTolerance);
      std::vector<Strike>& ks = r.strikeList;
      r.strikes = int(ks.size());
      if (ks.empty() || s.frames() < 2)
            return r;
      r.offset = bestOffset(s, ks, settings.offsetFrom, settings.offsetTo);

      // attacks
      std::vector<double> peaks;
      for (Strike& k : ks) {
            measure(s, k, r.offset, settings);
            peaks.push_back(k.broad);
            }
      const double med = median(peaks);
      std::vector<bool> flagged(ks.size(), false);
      std::vector<size_t> candidates;           // weak, where the reference is not
      for (size_t i = 0; i < ks.size(); ++i) {
            Strike& k = ks[i];
            k.broad = med > 0 ? k.broad / med : 0;
            k.weak = k.broad < settings.missingBroad && k.pitchLocal < settings.missingPitch;
            if (!k.weak)
                  continue;
            ++r.weakStrikes;
            const Strike* rk = reference && i < reference->strikeList.size() ? &reference->strikeList[i] : nullptr;
            if (rk && rk->weak) {
                  ++r.unclear;
                  continue;
                  }
            candidates.push_back(i);
            }
      // a weak strike is missing (with a reference: when a note of it is under its level too; a soft
      // lone note with no attack noise sounds, its notes at their level: MS Test Synth's bass notes)
      auto flagStrikes = [&]() {
      for (size_t i : candidates) {
            Strike& k = ks[i];
            const Strike* rk = reference && i < reference->strikeList.size() ? &reference->strikeList[i] : nullptr;
            if (!r.noteChecks.empty()) {
                  double least = 0;
                  for (int n : k.notes)
                        least = std::min(least, r.noteChecks[size_t(n)].deficit);
                  if (least > -settings.strikeDeficitDb) {
                        ++r.sounding;
                        continue;
                        }
                  }
            flagged[i] = true;
            Finding fd;
            fd.kind = Finding::Kind::MissingAttack;
            fd.time = k.time;
            fd.audioTime = k.time + r.offset;
            fd.notes = k.notes;
            fd.value = k.broad;
            fd.second = k.pitchLocal;
            fd.expected = rk ? rk->broad : -1;
            fd.text = fmt("no attack: flux %.2f of the median strike's, its partials %.1f x their flux around", k.broad, k.pitchLocal)
                      + (rk ? fmt(" (built-in synth: %.2f, %.1f x)", rk->broad, rk->pitchLocal) : std::string());
            r.findings.push_back(fd);
            }
            };
      if (!(reference && ref))
            flagStrikes();

      // each note on its own partials (needs the reference: what this note should give)
      if (reference && ref) {
            std::vector<bool> inFlagged(notes.size(), false);
            // the note's partials (fine bins) that no other note sounding from `from` to `to` has: its
            // harmonics 1-4, each with a bin either side, none within 2 bins of another's harmonic 1-8
            auto ownBins = [&](const Spectrogram& sp, size_t i, double from, double to) {
                  std::vector<int> others;
                  for (size_t j = 0; j < notes.size(); ++j)
                        if (j != i && notes[j].on <= to && notes[j].off + 0.3 >= from && notes[j].pitch != notes[i].pitch)
                              others.push_back(notes[j].pitch);
                  const std::vector<int> shared = sp.finePartialBins(others, 8, 2);
                  std::vector<int> own;
                  for (int b : sp.finePartialBins({ notes[i].pitch }, 4, 0)) {
                        bool clear = true;
                        for (int d = -1; d <= 1; ++d)
                              clear = clear && !std::binary_search(shared.begin(), shared.end(), b + d);
                        if (clear)
                              for (int d = -1; d <= 1; ++d)
                                    own.push_back(b + d);
                        }
                  std::sort(own.begin(), own.end());
                  own.erase(std::unique(own.begin(), own.end()), own.end());
                  return own;
                  };
            // the attack: the stronger of the windows centred 50 and 100 ms after the onset
            auto attack = [&](const Spectrogram& sp, const Note& n, const std::vector<int>& bins, double offset) {
                  return std::max(sp.finePower(bins, n.on + offset + 0.05), sp.finePower(bins, n.on + offset + 0.1));
                  };
            auto late = [&](const Spectrogram& sp, const Note& n, const std::vector<int>& bins, double offset) {
                  const double at = std::min(n.on + 0.7 * (n.off - n.on), n.off - 0.05);
                  return sp.finePower(bins, at + offset);
                  };
            auto db = [](double p) { return 10 * std::log10(std::max(p, 1e-20)); };

            // each note's fundamental and 2nd harmonic (shared or not: a chord's octaves share them)
            // from before its onset to after it, and its level after it
            r.noteChecks.assign(notes.size(), NoteCheck());
            std::vector<double> diff(notes.size(), 0);
            std::vector<int> kind(notes.size(), 0);       // which harmonics: 3 both, 1 or 2 one, 0 both shared
            for (size_t i = 0; i < notes.size(); ++i) {
                  const Note& n = notes[i];
                  // its fundamental and 2nd harmonic, those another note sounding then has (within 2
                  // bins of its harmonics 1-8) left out, unless both are (an octave above another note)
                  std::vector<int> others;
                  for (size_t j = 0; j < notes.size(); ++j)
                        if (j != i && notes[j].on <= n.on + 0.15 && notes[j].off + 0.3 >= n.on - 0.1 && notes[j].pitch != n.pitch)
                              others.push_back(notes[j].pitch);
                  const std::vector<int> shared = s.finePartialBins(others, 8, 2);
                  std::vector<int> bins;
                  for (int h = 1; h <= 2; ++h) {
                        const std::vector<int> hb = s.finePartialBins({ n.pitch }, h, 1);
                        const std::vector<int> prev = h > 1 ? s.finePartialBins({ n.pitch }, h - 1, 1) : std::vector<int>();
                        std::vector<int> only;
                        std::set_difference(hb.begin(), hb.end(), prev.begin(), prev.end(), std::back_inserter(only));
                        bool clear = true;
                        for (int b : only)
                              clear = clear && !std::binary_search(shared.begin(), shared.end(), b);
                        if (clear) {
                              bins.insert(bins.end(), only.begin(), only.end());
                              kind[i] |= h;
                              }
                        }
                  if (bins.empty())
                        bins = s.finePartialBins({ n.pitch }, 2, 1);
                  double la = 0, ra = 0;
                  auto rise = [&](const Spectrogram& sp, double offset, double* level) {
                        const double before = sp.finePower(bins, n.on + offset - 0.06);
                        const double after = std::max(sp.finePower(bins, n.on + offset + 0.05), sp.finePower(bins, n.on + offset + 0.1));
                        *level = db(after);
                        return db(after) - db(before);
                        };
                  NoteCheck& c = r.noteChecks[i];
                  c.rise = std::max(-99.0, std::min(99.0, rise(s, r.offset, &la)));
                  c.refRise = std::max(-99.0, std::min(99.0, rise(*ref, reference->offset, &ra)));
                  c.after = std::max(-120.0, la);       // (-120: nothing at all)
                  c.refAfter = std::max(-120.0, ra);
                  diff[i] = c.after - c.refAfter;
                  }
            // the level against the reference's, against that of the notes within 3 semitones (the
            // library's and the built-in synth's registers differ: SSO's top octave is quieter)
            // (measured on the same harmonics: the two instruments' balance of fundamental and 2nd
            // harmonic differs too; at least 12 notes: the window widens where the register has fewer)
            std::map<int, std::map<int, std::vector<double>>> byPitch;    // harmonics -> pitch -> diffs
            for (size_t i = 0; i < notes.size(); ++i)
                  byPitch[kind[i]][notes[i].pitch].push_back(diff[i]);
            std::map<int, std::map<int, double>> base;
            for (auto& bk : byPitch) {
                  for (const auto& bp : bk.second) {
                        std::vector<double> nearby;
                        for (int w = 3; w <= 128; w *= 2) {
                              nearby.clear();
                              for (int q = bp.first - w; q <= bp.first + w; ++q)
                                    if (bk.second.count(q))
                                          nearby.insert(nearby.end(), bk.second[q].begin(), bk.second[q].end());
                              if (nearby.size() >= 12)
                                    break;
                              }
                        base[bk.first][bp.first] = median(nearby);
                        }
                  }
            for (size_t i = 0; i < notes.size(); ++i) {
                  NoteCheck& c = r.noteChecks[i];
                  c.deficit = diff[i] - base[kind[i]][notes[i].pitch];
                  // (nothing at all where the reference sounds: missing, whatever its register does,
                  // e.g. when most notes like it are missing too)
                  if (c.after <= -100 && c.refAfter > -60)
                        c.deficit = -99;
                  }
            flagStrikes();
            for (size_t i = 0; i < ks.size(); ++i)
                  if (flagged[i])
                        for (int n : ks[i].notes)
                              inFlagged[size_t(n)] = true;
            for (size_t i = 0; i < notes.size(); ++i) {
                  NoteCheck& c = r.noteChecks[i];
                  if (inFlagged[i])
                        continue;
                  // missing: far under the level its register has against the reference, not rising at
                  // its onset (or much less than the reference), where the reference does rise
                  const bool noRise = c.rise < settings.riseMin || c.rise < c.refRise - settings.riseMargin;
                  // (so far under that nothing of it is there: however the reference rises, e.g. the same
                  // note ringing on under its pedal)
                  const bool absent = c.deficit <= -settings.absentDb;
                  if ((c.deficit <= -settings.missingNoteDb && noRise && c.refRise >= settings.refRiseMin) || absent) {
                        inFlagged[i] = true;
                        Finding fd;
                        fd.kind = Finding::Kind::MissingNote;
                        fd.time = notes[i].on;
                        fd.audioTime = notes[i].on + r.offset;
                        fd.notes = { int(i) };
                        fd.value = c.deficit;
                        fd.second = c.rise;
                        fd.expected = c.refRise;
                        fd.text = fmt("note missing: %.0f dB under its register's level (against the built-in synth), rising %.0f dB at "
                                      "its onset (built-in synth: %.0f dB)", std::min(-c.deficit, 99.0), c.rise, c.refRise);
                        r.findings.push_back(fd);
                        }
                  }

            // cut short: a held note's partials at 70 % of it. (A candidate whose attack on its own
            // partials is far under the reference's, against the other long notes', never sounded:
            // a missing note, not a cut one)
            struct Cut { size_t note; double d, rd, attDiff; };
            std::vector<Cut> cuts;
            std::vector<double> attDiffs;
            for (size_t i = 0; i < notes.size(); ++i) {
                  const Note& n = notes[i];
                  if (n.off - n.on < settings.minLength || inFlagged[i] || !n.sustained)
                        continue;
                  const double at = std::min(n.on + 0.7 * (n.off - n.on), n.off - 0.05);
                  const std::vector<int> bins = ownBins(s, i, n.on - 0.05, at + 0.05);
                  if (bins.size() < 3)
                        continue;         // (all its partials are another note's too)
                  ++r.longNotes;
                  const double att = attack(s, n, bins, r.offset), refAtt = attack(*ref, n, bins, reference->offset);
                  if (refAtt <= 0)
                        continue;
                  const double d = db(late(s, n, bins, r.offset)) - db(att);
                  const double rd = db(late(*ref, n, bins, reference->offset)) - db(refAtt);
                  r.noteChecks[i].cutBins = int(bins.size());
                  r.noteChecks[i].drop = d;
                  r.noteChecks[i].refDrop = rd;
                  attDiffs.push_back(db(att) - db(refAtt));
                  if (d < settings.cutDrop && d < rd - settings.cutMargin)
                        cuts.push_back({ i, d, rd, db(att) - db(refAtt) });
                  }
            const double attMed = median(attDiffs);
            for (const Cut& c : cuts) {
                  const Note& n = notes[c.note];
                  Finding fd;
                  fd.time = n.on;
                  fd.audioTime = n.on + r.offset;
                  fd.notes = { int(c.note) };
                  if (c.attDiff < attMed - settings.missingNoteDb) {
                        fd.kind = Finding::Kind::MissingNote;
                        fd.value = c.attDiff - attMed;
                        fd.text = fmt("note missing: its own partials %.0f dB under the reference's (against the other long notes), "
                                      "and nothing of it at 70 %% of its %.2f s", attMed - c.attDiff, n.off - n.on);
                        }
                  else {
                        fd.kind = Finding::Kind::CutShort;
                        fd.length = n.off - n.on;
                        fd.value = c.d;
                        fd.second = c.rd;
                        fd.text = fmt("cut short: at 70 %% of its %.2f s its partials are %.0f dB under its attack (built-in synth: %.0f dB)",
                                      n.off - n.on, std::max(c.d, -120.0), std::max(c.rd, -120.0));
                        }
                  r.findings.push_back(fd);
                  }
            }

      // silence where notes sound (the first second of each note, after its attack)
      {
            std::vector<char> sounding(size_t(s.frames()), 0);
            for (const Note& n : notes) {
                  if (!n.sustained)
                        continue;
                  const int a = s.frameAt(n.on + r.offset + 0.05);
                  const int b = s.frameAt(std::min(n.off, n.on + 1.0) + r.offset);
                  for (int f = std::max(0, a); f <= b && f < s.frames(); ++f)
                        sounding[size_t(f)] = 1;
                  }
            const int minFrames = int(std::lround(settings.silenceMin / s.hop()));
            int start = -1;
            for (int f = 0; f <= s.frames(); ++f) {
                  const bool silent = f < s.frames() && sounding[size_t(f)] && s.rms()[size_t(f)] < settings.silenceDb;
                  if (silent && start < 0)
                        start = f;
                  else if (!silent && start >= 0) {
                        if (f - start >= minFrames) {
                              // the reference sounds there (most of it)
                              bool heard = true;
                              if (ref && reference) {
                                    int loud = 0;
                                    for (int g = start; g < f; ++g) {
                                          const int rf = ref->frameAt(s.timeOf(g) - r.offset + reference->offset);
                                          if (rf >= 0 && rf < ref->frames() && ref->rms()[size_t(rf)] > settings.silenceDb + 20)
                                                ++loud;
                                          }
                                    heard = loud * 2 > (f - start);
                                    }
                              if (heard) {
                                    Finding fd;
                                    fd.kind = Finding::Kind::Silence;
                                    fd.audioTime = s.timeOf(start);
                                    fd.time = fd.audioTime - r.offset;
                                    fd.length = (f - start) * s.hop();
                                    for (size_t i = 0; i < notes.size(); ++i)
                                          if (notes[i].on < fd.time + fd.length && notes[i].off > fd.time)
                                                fd.notes.push_back(int(i));
                                    fd.text = fmt("silence: %.2f s under %.0f dB (against the peak) while notes sound", fd.length, settings.silenceDb);
                                    r.findings.push_back(fd);
                                    }
                              }
                        start = -1;
                        }
                  }
      }

      // drift: the best offset per window (half overlapping), around the whole one
      {
            const double end = ks.back().time;
            for (double w = 0; w < end; w += settings.driftWindow / 2) {
                  size_t a = 0, b = 0;
                  while (a < ks.size() && ks[a].time < w)
                        ++a;
                  b = a;
                  while (b < ks.size() && ks[b].time < w + settings.driftWindow)
                        ++b;
                  if (b - a < 6)
                        continue;
                  double best = r.offset, bestSum = -1;
                  for (double o = r.offset - 0.1; o <= r.offset + 0.1 + 1e-9; o += 0.002) {
                        const double sum = onsetFlux(s, ks, o, a, b);
                        if (sum > bestSum) {
                              bestSum = sum;
                              best = o;
                              }
                        }
                  r.windows.push_back({ w + settings.driftWindow / 2, best });
                  if (w + settings.driftWindow >= end)
                        break;
                  }
            if (r.windows.size() >= 2) {
                  auto mm = std::minmax_element(r.windows.begin(), r.windows.end(),
                                                [](const std::pair<double, double>& x, const std::pair<double, double>& y) { return x.second < y.second; });
                  const double spread = mm.second->second - mm.first->second;
                  if (spread > settings.driftMax) {
                        Finding fd;
                        fd.kind = Finding::Kind::Drift;
                        fd.time = std::min(mm.first->first, mm.second->first);
                        fd.audioTime = fd.time + r.offset;
                        fd.length = std::fabs(mm.second->first - mm.first->first);
                        fd.value = spread;
                        fd.text = fmt("timing drift: the audio's offset against the notes goes from %.0f ms (at %.0f s) to %.0f ms (at %.0f s)",
                                      mm.first->second * 1000, mm.first->first, mm.second->second * 1000, mm.second->first);
                        r.findings.push_back(fd);
                        }
                  }
      }
      std::stable_sort(r.findings.begin(), r.findings.end(), [](const Finding& a, const Finding& b) { return a.time < b.time; });
      return r;
      }

//---------------------------------------------------------
//   clipping
//---------------------------------------------------------

std::vector<Finding> clipping(const float* x, size_t frames, int channels, double rate, double* peakDb)
      {
      std::vector<Finding> r;
      double peak = 0;
      const size_t gap = size_t(rate * 0.01);         // runs closer than 10 ms are one
      bool in = false;
      size_t start = 0, last = 0;
      double runPeak = 0;
      auto close = [&]() {
            Finding fd;
            fd.kind = Finding::Kind::Clipping;
            fd.time = fd.audioTime = start / rate;
            fd.length = (last + 1 - start) / rate;
            fd.value = 20 * std::log10(runPeak);
            fd.text = fmt("over full scale: %.1f ms, up to %+.1f dBFS", fd.length * 1000, fd.value);
            r.push_back(fd);
            in = false;
            };
      for (size_t i = 0; i < frames; ++i) {
            double m = 0;
            for (int c = 0; c < channels; ++c)
                  m = std::max(m, double(std::fabs(x[i * size_t(channels) + size_t(c)])));
            peak = std::max(peak, m);
            if (m <= 1.0)
                  continue;
            if (in && i - last > gap)
                  close();
            if (!in) {
                  in = true;
                  start = i;
                  runPeak = 0;
                  }
            last = i;
            runPeak = std::max(runPeak, m);
            }
      if (in)
            close();
      if (peakDb)
            *peakDb = peak > 0 ? 20 * std::log10(peak) : -200;
      return r;
      }

std::vector<float> mono(const float* x, size_t frames, int channels)
      {
      std::vector<float> m(frames);
      for (size_t i = 0; i < frames; ++i) {
            float s = 0;
            for (int c = 0; c < channels; ++c)
                  s += x[i * size_t(channels) + size_t(c)];
            m[i] = s / float(channels);
            }
      return m;
      }

}     // namespace PlaybackVerify
}     // namespace Ms
