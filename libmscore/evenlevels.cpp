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
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
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
      return fitGlide(p, env, level, std::vector<bool>(), fitRms, envRms);
      }

std::vector<double> fitGlide(const Passage& p, const std::vector<double>& env, const std::vector<double>& level,
                             const std::vector<bool>& fixed, double* fitRms, double* envRms)
      {
      const size_t n = p.on.size();
      auto held = [&fixed](size_t j) { return j < fixed.size() && fixed[j]; };
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
      // a held note's value stays: its row the identity, no coupling, its change 0
      for (size_t a = 0; a < n; ++a)
            if (held(a)) {
                  for (size_t c = 0; c < n; ++c)
                        A[a][c] = A[c][a] = 0.0;
                  A[a][a] = 1.0;
                  b[a] = 0.0;
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

//---------------------------------------------------------
//   windows
//---------------------------------------------------------

std::vector<Window> windows(int n, const std::vector<bool>& needs, int runIn, int runOut, int margin)
      {
      std::vector<Window> out;
      for (int j = 0; j < n; ++j) {
            if (j >= int(needs.size()) || !needs[size_t(j)])
                  continue;
            Window w;
            w.measureFirst = std::max(0, j - margin);
            w.measureLast = std::min(n - 1, j + margin);
            w.renderFirst = std::max(0, w.measureFirst - runIn);
            w.renderLast = std::min(n - 1, w.measureLast + runOut);
            if (!out.empty() && w.renderFirst <= out.back().renderLast) {
                  Window& b = out.back();
                  b.measureLast = std::max(b.measureLast, w.measureLast);
                  b.renderLast = std::max(b.renderLast, w.renderLast);
                  }
            else
                  out.push_back(w);
            }
      for (Window& w : out) {
            if (w.renderFirst == 0 && w.renderLast == n - 1) {
                  w.measureFirst = 0;
                  w.measureLast = n - 1;
                  }
            }
      return out;
      }

int runInFor(const SoundLib::Library& library, const SoundLib::LibInstrument* patch)
      {
      if (!patch)
            return RUN_IN;
      const SoundLib::LibInstrument* main = patch;
      if (!patch->with.isEmpty()) {
            for (const SoundLib::LibInstrument& li : library.instruments)
                  if (li.name == patch->with)
                        main = &li;
            }
      for (const QString& id : main->ids)
            if (id == "contrabass" || id == "contrabasses" || id == "double-bass")
                  return RUN_IN_BASSES;
      return RUN_IN;
      }

//---------------------------------------------------------
//   gapFor / earlyStopFor
//    the patches measured by tools/soundlibraries/even_gap_check.py (sso_even_gap_check.json; Windows test VM, kthost +
//    Kontakt 8 + SSO, the windows the background measurer renders of Whence): the smallest gap whose levels and fitted
//    glides matched the 3 s gap's, and the early stop's threshold (the 95th percentile of |pass 1 - pass 2| over the
//    measured notes). Any other patch: the job's 3 s, always PASSES passes
//---------------------------------------------------------

static const struct Measured { const char* patch; double gap; double earlyStopDb; } MEASURED[] = {
      { "Violins 1 - Performance", 1.0, 0.67 },
      { "Violas - Performance", 1.0, 0.74 },
      { "Celli - Performance", 1.0, 1.26 },
      { "Basses - Performance", 1.5, 1.49 },  // (Whence's Celli bars 1-8 an octave lower: Whence has no slurred Basses)
      };

static const Measured* measured(const SoundLib::LibInstrument* patch)
      {
      if (!patch)
            return nullptr;
      for (const Measured& m : MEASURED)
            if (patch->name == QLatin1String(m.patch))
                  return &m;
      return nullptr;
      }

double gapFor(const SoundLib::Library&, const SoundLib::LibInstrument* patch)
      {
      const Measured* m = measured(patch);
      return m ? m->gap : GAP_S;
      }

double earlyStopFor(const SoundLib::Library&, const SoundLib::LibInstrument* patch)
      {
      const Measured* m = measured(patch);
      return m ? m->earlyStopDb : 0.0;
      }

QString cacheKey(const std::vector<QString>& contexts, size_t j)
      {
      QStringList parts;
      for (long k = long(j) - MARGIN; k <= long(j) + MARGIN; ++k)
            parts << (k >= 0 && k < long(contexts.size()) ? contexts[size_t(k)] : QString("-"));
      return QString::fromLatin1(QCryptographicHash::hash(parts.join('/').toUtf8(), QCryptographicHash::Sha1).toHex().left(24));
      }

bool mustWait(const std::vector<double>& pendingOns, double from, double to)
      {
      for (double t : pendingOns)
            if (t >= from - 1e-9 && t < to)
                  return true;
      return false;
      }

//---------------------------------------------------------
//   Cache
//---------------------------------------------------------

void Cache::load(const QString& file)
      {
      _file = file;
      _entries.clear();
      _clock = 0;
      _changed = false;
      if (file.isEmpty())
            return;
      QFile f(file);
      if (!f.open(QIODevice::ReadOnly))
            return;
      const QJsonObject top = QJsonDocument::fromJson(f.readAll()).object();
      const QJsonObject e = top.value("entries").toObject();
      for (auto i = e.begin(); i != e.end(); ++i) {
            const QJsonArray a = i.value().toArray();
            if (a.size() < 2)
                  continue;
            const qint64 used = qint64(a[1].toDouble());
            _entries[i.key()] = { clampDb(a[0].toDouble()), used };
            _clock = std::max(_clock, used);
            }
      }

bool Cache::save()
      {
      if (_file.isEmpty() || !_changed)
            return true;
      QJsonObject e;
      for (const auto& x : _entries)
            e[x.first] = QJsonArray { x.second.db, double(x.second.used) };
      QJsonObject top;
      top["about"] = QString("MuseScore's even-levels values by context (libmscore/evenlevels.h, Cache): measured in the "
                             "background near edits; reused when a context comes back (undo, redo, the same passage again)");
      top["entries"] = e;
      QDir().mkpath(QFileInfo(_file).absolutePath());
      QSaveFile f(_file);
      if (!f.open(QIODevice::WriteOnly))
            return false;
      f.write(QJsonDocument(top).toJson(QJsonDocument::Compact));
      if (!f.commit())
            return false;
      _changed = false;
      return true;
      }

bool Cache::get(const QString& key, double* db)
      {
      auto i = _entries.find(key);
      if (i == _entries.end())
            return false;
      i->second.used = ++_clock;
      if (db)
            *db = i->second.db;
      return true;
      }

void Cache::put(const QString& key, double db)
      {
      Entry& e = _entries[key];
      if (std::fabs(e.db - db) > 1e-9 || e.used == 0)
            _changed = true;
      e.db = db;
      e.used = ++_clock;
      if (int(_entries.size()) > MAX_ENTRIES) {
            // the least recently used tenth goes
            std::vector<qint64> uses;
            uses.reserve(_entries.size());
            for (const auto& x : _entries)
                  uses.push_back(x.second.used);
            std::nth_element(uses.begin(), uses.begin() + long(uses.size() / 10), uses.end());
            const qint64 cut = uses[uses.size() / 10];
            for (auto i = _entries.begin(); i != _entries.end();)
                  i = i->second.used <= cut ? _entries.erase(i) : std::next(i);
            _changed = true;
            }
      }

}     // namespace EvenLevels
}     // namespace Ms
