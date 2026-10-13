//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "evenlevels.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>

#include "chord.h"
#include "note.h"
#include "part.h"
#include "score.h"
#include "segment.h"
#include "soundlibrary.h"

namespace Ms {
namespace EvenLevels {

const char* const metaTag = "evenLevels";

double clampDb(double db)
      {
      if (!std::isfinite(db) || !isSet(db))
            return NONE;
      return std::round(std::max(-MAX_DB, std::min(MAX_DB, db)) * 100.0) / 100.0;
      }

namespace {
// every chord of the score with its segment's tick and track, grace chords with their index (-1: the chord itself)
template <typename F> void eachChord(const Score* score, F f)
      {
      for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (int track = 0; track < score->ntracks(); ++track) {
                  Element* e = s->element(track);
                  if (!e || !e->isChord())
                        continue;
                  Chord* chord = toChord(e);
                  for (int g = 0; g < chord->graceNotes().size(); ++g)
                        f(s->tick().ticks(), track, g, chord->graceNotes()[g]);
                  f(s->tick().ticks(), track, -1, chord);
                  }
            }
      }
}

//---------------------------------------------------------
//   read
//    a note found by its chord's tick, track (and grace index) and pitch, as performancetechnique.cpp's
//---------------------------------------------------------

void read(Score* score)
      {
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return;
      score->metaTags().remove(metaTag);        // written again from the notes on saving
      for (const QJsonValue& v : QJsonDocument::fromJson(tag.toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            const int tick = o.value("tick").toInt(-1);
            const int track = o.value("track").toInt(-1);
            const int grace = o.value("grace").toInt(-1);
            const int pitch = o.value("pitch").toInt(-1);
            const double db = o.contains("db") ? clampDb(o.value("db").toDouble()) : NONE;
            const QString context = o.value("context").toString();
            if (!isSet(db))
                  continue;
            Note* note = find(score, tick, track, grace, pitch);
            if (!note)
                  continue;
            for (ScoreElement* l : note->linkList()) {
                  if (!l->isNote())
                        continue;
                  toNote(l)->setEvenLevel(db);
                  toNote(l)->setEvenContext(context);
                  }
            }
      }

Note* find(Score* score, int tick, int track, int grace, int pitch)
      {
      if (!score || tick < 0 || track < 0 || track >= score->ntracks() || pitch < 0)
            return nullptr;
      Segment* seg = score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
      Element* e = seg ? seg->element(track) : nullptr;
      if (!e || !e->isChord())
            return nullptr;
      Chord* chord = toChord(e);
      if (grace >= 0)
            chord = grace < chord->graceNotes().size() ? chord->graceNotes()[grace] : nullptr;
      if (!chord)
            return nullptr;
      for (Note* note : chord->notes())
            if (note->pitch() == pitch)
                  return note;
      return nullptr;
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

QString write(const Score* score)
      {
      QJsonArray list;
      eachChord(score, [&](int tick, int track, int grace, const Chord* c) {
            for (const Note* n : c->notes()) {
                  if (!isSet(n->evenLevel()))
                        continue;
                  QJsonObject o;
                  o["tick"] = tick;
                  o["track"] = track;
                  if (grace >= 0)
                        o["grace"] = grace;
                  o["pitch"] = n->pitch();
                  o["db"] = std::round(n->evenLevel() * 100.0) / 100.0;
                  if (!n->evenContext().isEmpty())
                        o["context"] = n->evenContext();
                  list.append(o);
                  }
            });
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

int count(const Score* score)
      {
      int n = 0;
      eachChord(score, [&](int, int, int, const Chord* c) {
            for (const Note* note : c->notes())
                  n += isSet(note->evenLevel()) ? 1 : 0;
            });
      return n;
      }

QString contextPatch(const QString& context)
      {
      const int bar = context.lastIndexOf('|');
      return bar < 0 ? QString() : context.left(bar);
      }

std::map<const Part*, std::map<QString, double>> tops(const MasterScore* score)
      {
      std::map<const Part*, std::map<QString, double>> out;
      if (!score)
            return out;
      eachChord(score, [&](int, int, int, const Chord* c) {
            for (const Note* n : c->notes()) {
                  if (!isSet(n->evenLevel()) || n->evenLevel() <= 0.0)
                        continue;
                  const QString patch = contextPatch(n->evenContext());
                  if (patch.isEmpty())
                        continue;
                  double& t = out[n->part()][patch];
                  t = std::max(t, n->evenLevel());
                  }
            });
      return out;
      }

//---------------------------------------------------------
//   windowStartMs
//---------------------------------------------------------

const SoundLib::Articulation* legatoOf(const SoundLib::LibInstrument* patch)
      {
      if (!patch)
            return nullptr;
      for (const SoundLib::Articulation& a : patch->articulations)
            if (!a.transitions.empty())
                  return &a;
      return nullptr;
      }

double windowStartMs(const SoundLib::Articulation& a, int pitch, int velocity, bool joined, int prevPitch)
      {
      if (joined && prevPitch >= 0) {
            const double d = a.legatoDelayAt(pitch - prevPitch, prevPitch);
            if (d >= 0)
                  return d;
            }
      for (const SoundLib::Articulation::Technique& t : a.attacks)
            if (velocity >= t.low && velocity <= t.high && t.hasOnset())
                  return t.onsetAt(pitch);
      const double o = a.onsetAt(pitch);
      return o >= 0 ? o : 0.0;
      }

//---------------------------------------------------------
//   contextOf
//---------------------------------------------------------

QString contextOf(const ContextParts& c)
      {
      auto ms = [](double x) { return QString::number(std::lround(x)); };
      QStringList parts;
      parts << QString::number(c.pitch) << QString::number(std::lround(c.cents * 10.0)) << QString::number(c.velocity)
            << (c.joined ? "j" : "a") << ms(c.lengthMs) << QString::number(c.dynamics);
      if (c.prevPitch >= 0)
            parts << "p" << QString::number(c.prevPitch) << QString::number(c.prevVelocity) << ms(c.prevOnMs) << ms(c.prevLengthMs);
      if (c.nextPitch >= 0)
            parts << "n" << QString::number(c.nextPitch) << QString::number(c.nextVelocity) << ms(c.nextOnMs);
      const QByteArray h = QCryptographicHash::hash((c.patch + "|" + parts.join(',')).toUtf8(), QCryptographicHash::Sha1).toHex();
      return c.patch + "|" + QString::fromLatin1(h.left(16));
      }

//---------------------------------------------------------
//   the renders' contexts
//---------------------------------------------------------

static QMutex renderedMutex;
static std::map<const Note*, QString> renderedContexts;

void rendered(const Note* note, const QString& context)
      {
      QMutexLocker lock(&renderedMutex);
      if (renderedContexts.size() > 200000)
            renderedContexts.clear();
      renderedContexts[note] = context;
      }

State state(const Note* note)
      {
      if (!note || !isSet(note->evenLevel()))
            return State::NONE;
      QMutexLocker lock(&renderedMutex);
      auto r = renderedContexts.find(note);
      if (r == renderedContexts.end())
            return State::UNKNOWN;
      return r->second == note->evenContext() ? State::CURRENT : State::STALE;
      }

//---------------------------------------------------------
//   slurStats
//---------------------------------------------------------

static double median(std::vector<double> v)
      {
      if (v.empty())
            return 0.0;
      std::sort(v.begin(), v.end());
      const size_t n = v.size();
      return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
      }

Stats slurStats(const std::vector<int>& slur, const std::vector<double>& level)
      {
      Stats s;
      const size_t n = std::min(slur.size(), level.size());
      std::map<int, std::vector<double>> by;
      for (size_t k = 1; k + 1 < n; ++k)
            by[slur[k]].push_back(level[k]);
      for (const auto& b : by) {
            s.median[b.first] = median(b.second);
            if (b.second.size() > 1)
                  s.ranges.push_back(*std::max_element(b.second.begin(), b.second.end())
                                     - *std::min_element(b.second.begin(), b.second.end()));
            }
      s.dev.assign(n, 0.0);
      for (size_t k = 1; k + 1 < n; ++k) {
            s.dev[k] = level[k] - s.median[slur[k]];
            s.residual = std::max(s.residual, std::fabs(s.dev[k]));
            }
      s.rangeMedian = median(s.ranges);
      return s;
      }

//---------------------------------------------------------
//   the glide and its fit
//---------------------------------------------------------

double glideAt(const std::vector<double>& centres, const std::vector<double>& db, double t)
      {
      const size_t n = std::min(centres.size(), db.size());
      if (n == 0)
            return 0.0;
      if (t <= centres[0])
            return db[0];
      if (t >= centres[n - 1])
            return db[n - 1];
      const size_t k = size_t(std::upper_bound(centres.begin(), centres.begin() + long(n), t) - centres.begin());
      const double a = centres[k - 1], b = centres[k];
      return b > a ? db[k - 1] + (db[k] - db[k - 1]) * (t - a) / (b - a) : db[k];
      }

double Passage::gridStart() const
      {
      return on.empty() ? 0.0 : on.front() - 0.2;
      }

int Passage::gridSize() const
      {
      if (off.empty())
            return 0;
      const double last = *std::max_element(off.begin(), off.end());
      return int(std::floor((last + 1.0 - gridStart()) / (STEP_MS / 1000.0))) + 1;
      }

std::vector<double> Passage::centres() const
      {
      std::vector<double> c;
      for (double w : window)
            c.push_back(w + WINDOW_MS / 2000.0);
      return c;
      }

// a curve on the grid smoothed by the one pole (from its first value)
static std::vector<double> smooth(const std::vector<double>& g)
      {
      const double a = 1.0 - std::exp(-STEP_MS / SMOOTH_MS);
      std::vector<double> out(g.size());
      double s = g.empty() ? 0.0 : g[0];
      for (size_t i = 0; i < g.size(); ++i) {
            s += a * (g[i] - s);
            out[i] = s;
            }
      return out;
      }

// the smoothed glide of each note's unit value: columns of the grid x notes matrix
static std::vector<std::vector<double>> unitResponses(const Passage& p)
      {
      const size_t n = p.on.size();
      const int m = p.gridSize();
      const double t0 = p.gridStart();
      const std::vector<double> c = p.centres();
      std::vector<std::vector<double>> cols(n);
      for (size_t j = 0; j < n; ++j) {
            std::vector<double> e(n, 0.0);
            e[j] = 1.0;
            std::vector<double> g(size_t(std::max(0, m)));
            for (int i = 0; i < m; ++i)
                  g[size_t(i)] = glideAt(c, e, t0 + i * STEP_MS / 1000.0);
            cols[j] = smooth(g);
            }
      return cols;
      }

// solves A x = b (A n x n, symmetric positive definite here) by Gaussian elimination with partial pivoting
static std::vector<double> solve(std::vector<std::vector<double>> A, std::vector<double> b)
      {
      const size_t n = b.size();
      for (size_t c = 0; c < n; ++c) {
            size_t p = c;
            for (size_t r = c + 1; r < n; ++r)
                  if (std::fabs(A[r][c]) > std::fabs(A[p][c]))
                        p = r;
            std::swap(A[c], A[p]);
            std::swap(b[c], b[p]);
            if (std::fabs(A[c][c]) < 1e-12)
                  continue;
            for (size_t r = c + 1; r < n; ++r) {
                  const double f = A[r][c] / A[c][c];
                  if (f == 0.0)
                        continue;
                  for (size_t k = c; k < n; ++k)
                        A[r][k] -= f * A[c][k];
                  b[r] -= f * b[c];
                  }
            }
      std::vector<double> x(n, 0.0);
      for (size_t c = n; c-- > 0;) {
            double s = b[c];
            for (size_t k = c + 1; k < n; ++k)
                  s -= A[c][k] * x[k];
            x[c] = std::fabs(A[c][c]) < 1e-12 ? 0.0 : s / A[c][c];
            }
      return x;
      }

std::vector<double> fitGlide(const Passage& p, const std::vector<double>& env, const std::vector<double>& level,
                             double* fitRms, double* envRms)
      {
      const size_t n = p.on.size();
      const int m = std::min(p.gridSize(), int(env.size()));
      std::vector<double> du(n, 0.0);
      if (n < 3 || m <= 0)
            return du;
      const Stats s = slurStats(p.slur, level);
      const double t0 = p.gridStart();
      const double lastOff = p.off.back();
      std::vector<double> w(size_t(m), 0.0), r(size_t(m), 0.0);
      for (int i = 0; i < m; ++i) {
            const double t = t0 + i * STEP_MS / 1000.0;
            if (t < p.window[1] || t >= lastOff)
                  continue;
            // the note whose window started last before t: its slur's median is the target
            size_t k = size_t(std::upper_bound(p.window.begin(), p.window.end(), t) - p.window.begin());
            k = k == 0 ? 0 : k - 1;
            auto med = s.median.find(p.slur[k]);
            if (med == s.median.end() || !std::isfinite(env[size_t(i)]))
                  continue;
            w[size_t(i)] = 1.0;
            r[size_t(i)] = med->second - env[size_t(i)];
            }
      const std::vector<std::vector<double>> M = unitResponses(p);
      std::vector<std::vector<double>> A(n, std::vector<double>(n, 0.0));
      std::vector<double> b(n, 0.0);
      for (size_t a = 0; a < n; ++a) {
            for (size_t c = a; c < n; ++c) {
                  double x = 0;
                  for (int i = 0; i < m; ++i)
                        x += w[size_t(i)] * M[a][size_t(i)] * M[c][size_t(i)];
                  A[a][c] = A[c][a] = x;
                  }
            A[a][a] += RIDGE;
            double y = 0;
            for (int i = 0; i < m; ++i)
                  y += w[size_t(i)] * M[a][size_t(i)] * r[size_t(i)];
            b[a] = y;
            }
      du = solve(A, b);
      double sw = 0, fit = 0, flat = 0;
      for (int i = 0; i < m; ++i) {
            if (w[size_t(i)] <= 0)
                  continue;
            double pred = 0;
            for (size_t j = 0; j < n; ++j)
                  pred += M[j][size_t(i)] * du[j];
            sw += 1;
            fit += (pred - r[size_t(i)]) * (pred - r[size_t(i)]);
            flat += r[size_t(i)] * r[size_t(i)];
            }
      if (fitRms)
            *fitRms = sw > 0 ? std::sqrt(fit / sw) : 0.0;
      if (envRms)
            *envRms = sw > 0 ? std::sqrt(flat / sw) : 0.0;
      return du;
      }

std::vector<double> windowChanges(const Passage& p, const std::vector<double>& v)
      {
      const size_t n = p.on.size();
      const int m = p.gridSize();
      const double t0 = p.gridStart();
      const std::vector<double> c = p.centres();
      std::vector<double> g(size_t(std::max(0, m)));
      for (int i = 0; i < m; ++i)
            g[size_t(i)] = glideAt(c, v, t0 + i * STEP_MS / 1000.0);
      g = smooth(g);
      std::vector<double> out(n, 0.0);
      for (size_t k = 0; k < n; ++k) {
            double sum = 0;
            int cnt = 0;
            for (int i = 0; i < m; ++i) {
                  const double t = t0 + i * STEP_MS / 1000.0;
                  if (t >= p.window[k] && t < p.window[k] + WINDOW_MS / 1000.0) {
                        sum += g[size_t(i)];
                        ++cnt;
                        }
                  }
            out[k] = cnt ? sum / cnt : 0.0;
            }
      return out;
      }

}     // namespace EvenLevels
}     // namespace Ms
