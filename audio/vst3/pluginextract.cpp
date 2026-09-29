//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  PluginExtract: what a hosted plug-in does with MIDI controllers and parameters (see
//  pluginextract.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "pluginextract.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <QJsonArray>
#include <QRegularExpression>

#include "vst3plugin.h"
#include "audio/midi/event.h"

namespace Ms {

static constexpr int AFTERTOUCH = 128;          // as Vst3Plugin::controllerParameter() numbers them
static constexpr int PITCHBEND = 129;
static constexpr int READ_ONLY = 1 << 1;        // ParameterInfo::kIsReadOnly
static constexpr int PROGRAM_CHANGE = 1 << 15;  // ParameterInfo::kIsProgramChange
static constexpr int BYPASS = 1 << 16;          // ParameterInfo::kIsBypass

//---------------------------------------------------------
//   pictures
//---------------------------------------------------------

static bool pixelDiffers(QRgb p, QRgb q)
      {
      return std::abs(qRed(p) - qRed(q)) + std::abs(qGreen(p) - qGreen(q)) + std::abs(qBlue(p) - qBlue(q)) > 60;
      }

int PluginExtract::differingPixels(const QImage& a, const QImage& b)
      {
      if (a.isNull() || b.isNull() || a.size() != b.size())
            return a.isNull() && b.isNull() ? 0 : std::numeric_limits<int>::max();
      const QImage x = a.convertToFormat(QImage::Format_RGB32);
      const QImage y = b.convertToFormat(QImage::Format_RGB32);
      int n = 0;
      for (int row = 0; row < x.height(); ++row) {
            const QRgb* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
            const QRgb* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
            for (int col = 0; col < x.width(); ++col)
                  n += pixelDiffers(p[col], q[col]);
            }
      return n;
      }

// where a and b differ, with a margin (a null rect: nowhere)
QRect PluginExtract::changedRect(const QImage& a, const QImage& b)
      {
      if (a.isNull() || b.isNull() || a.size() != b.size())
            return QRect();
      const QImage x = a.convertToFormat(QImage::Format_RGB32);
      const QImage y = b.convertToFormat(QImage::Format_RGB32);
      QRect r;
      for (int row = 0; row < x.height(); ++row) {
            const QRgb* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
            const QRgb* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
            int first = -1, last = -1;
            for (int col = 0; col < x.width(); ++col)
                  if (pixelDiffers(p[col], q[col])) {
                        if (first < 0)
                              first = col;
                        last = col;
                        }
            if (first >= 0)
                  r |= QRect(first, row, last - first + 1, 1);
            }
      return r.isNull() ? r : r.adjusted(-16, -16, 16, 16) & x.rect();
      }

// the cells (CELL x CELL pixels, numbered by row: row * columns + column) where a and b differ: where a change is,
// pixel by pixel, not one box around all of it (Kontakt's CPU and voice meters at the window's top move with every
// note, and a box around them and a slider takes in most of the window: the owner's links run of 2026-09-28)
QJsonArray PluginExtract::changedCells(const QImage& a, const QImage& b)
      {
      QJsonArray cells;
      if (a.isNull() || b.isNull() || a.size() != b.size())
            return cells;
      const QImage x = a.convertToFormat(QImage::Format_RGB32);
      const QImage y = b.convertToFormat(QImage::Format_RGB32);
      const int columns = (x.width() + CELL - 1) / CELL;
      std::vector<char> changed(size_t(columns * ((x.height() + CELL - 1) / CELL)), 0);
      for (int row = 0; row < x.height(); ++row) {
            const QRgb* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
            const QRgb* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
            for (int col = 0; col < x.width(); ++col)
                  if (pixelDiffers(p[col], q[col]))
                        changed[size_t((row / CELL) * columns + col / CELL)] = 1;
            }
      for (size_t i = 0; i < changed.size(); ++i)
            if (changed[i])
                  cells.append(int(i));
      return cells;
      }

//---------------------------------------------------------
//   controlsMoved
//    which named control each controller moves: the window cells a controller's 0 -> 127 changed against those each
//    parameter's 0 -> 1 changed (PluginExtract::changedCells), without the cells that change by themselves (the
//    baselines' noiseCells, and any cell more than 40 % of the tries changed: Kontakt's CPU and voice meters move with
//    every note), the most overlap (intersection over union) over 0.3; or a parameter the controller's own try changed.
//    (It compared one box around each change until 2026-09-29: the meters made every box most of the window, and the
//    owner's links run matched nonsense, CC 1 -> Mic 5 level)
//---------------------------------------------------------

QJsonArray PluginExtract::controlsMoved(const QJsonObject& controllers, const QJsonObject& parameters)
      {
      auto cellsOf = [](const QJsonObject& e) {
            std::set<int> c;
            for (const QJsonValue& v : e.value("cells").toArray())
                  c.insert(v.toInt());
            return c;
            };
      const QJsonArray ce = controllers.value("effects").toArray();
      const QJsonArray pe = parameters.value("effects").toArray();
      std::set<int> noise;
      for (const QJsonObject* o : { &controllers, &parameters })
            for (const QJsonValue& v : o->value("noiseCells").toArray())
                  noise.insert(v.toInt());
      std::map<int, int> seen;
      int tries = 0;
      for (const QJsonArray* list : { &ce, &pe })
            for (const QJsonValue& v : *list) {
                  const std::set<int> c = cellsOf(v.toObject());
                  tries += !c.empty();
                  for (int x : c)
                        ++seen[x];
                  }
      if (tries >= 5)
            for (const auto& s : seen)
                  if (s.second * 10 > tries * 4)
                        noise.insert(s.first);
      auto clean = [&](const QJsonObject& e) {
            std::set<int> c;
            for (int x : cellsOf(e))
                  if (!noise.count(x))
                        c.insert(x);
            return c;
            };
      QJsonArray out;
      for (const QJsonValue& cv : ce) {
            const QJsonObject c = cv.toObject();
            const std::set<int> cc = clean(c);
            QJsonObject m;
            m["cc"] = c.value("cc");
            double best = 0;
            for (const QJsonValue& pv : pe) {
                  const QJsonObject p = pv.toObject();
                  // (a parameter that the controller's own try changed: that is the answer)
                  for (const char* key : { "parametersLowToHigh", "parametersBeforeToLow" })
                        for (const QJsonValue& x : c.value(key).toArray())
                              if (x.toObject().value("id") == p.value("id")) {
                                    best = 2;
                                    m["control"] = p.value("title");
                                    m["id"] = p.value("id");
                                    m["by"] = "parameter";
                                    }
                  const std::set<int> pc = clean(p);
                  if (cc.empty() || pc.empty())
                        continue;
                  int inter = 0;
                  for (int x : cc)
                        inter += pc.count(x);
                  const double iou = double(inter) / double(cc.size() + pc.size() - inter);
                  if (iou > 0.3 && iou > best) {
                        best = iou;
                        m["control"] = p.value("title");
                        m["id"] = p.value("id");
                        m["by"] = QString("window cells %1").arg(std::round(iou * 100) / 100);
                        }
                  }
            if (!m.contains("control"))
                  m["control"] = QJsonValue();
            m["cells"] = int(cc.size());
            out.append(m);
            }
      return out;
      }

//---------------------------------------------------------
//   level
//---------------------------------------------------------

PluginExtract::Level PluginExtract::level(const std::vector<float>& buffer)
      {
      Level l;
      const size_t frames = buffer.size() / 2;
      if (frames < 2)
            return l;
      double power = 0, diffPower = 0, left = 0, right = 0;
      double prev = 0.5 * (double(buffer[0]) + double(buffer[1]));
      for (size_t i = 0; i < frames; ++i) {
            const double x = 0.5 * (double(buffer[2 * i]) + double(buffer[2 * i + 1]));
            left += double(buffer[2 * i]) * double(buffer[2 * i]);
            right += double(buffer[2 * i + 1]) * double(buffer[2 * i + 1]);
            power += x * x;
            if (i > 0)
                  diffPower += (x - prev) * (x - prev);
            prev = x;
            }
      power /= double(frames);
      diffPower /= double(frames - 1);
      l.db = power > 1e-20 ? 10 * std::log10(power) : -200;
      l.brightness = power > 1e-20 && diffPower > 1e-20 ? 10 * std::log10(diffPower / power) : 0;
      // (one side silent: ±60 dB)
      l.balance = left + right > 1e-20 ? 10 * std::log10(std::max(left, 1e-6 * right) / std::max(right, 1e-6 * left)) : 0;
      return l;
      }

//---------------------------------------------------------
//   centsShift
//---------------------------------------------------------

// the magnitude spectrum on a log-frequency grid (5 cents a bin from 40 Hz), of the mono mix
// decimated by 4 (enough for the harmonics that carry the pitch), Hann-windowed; log-compressed
// and made zero-mean, unit-length for a correlation
static std::vector<double> logSpectrum(const std::vector<float>& stereo, double sampleRate, double step, int bins)
      {
      const int decimate = 4;
      std::vector<double> x;
      const size_t frames = stereo.size() / 2;
      for (size_t i = 0; i + decimate <= frames; i += decimate) {
            double s = 0;
            for (int k = 0; k < decimate; ++k)
                  s += 0.5 * (double(stereo[2 * (i + k)]) + double(stereo[2 * (i + k) + 1]));
            x.push_back(s / decimate);
            }
      const double rate = sampleRate / decimate;
      const size_t n = x.size();
      std::vector<double> spec(size_t(bins), 0.0);
      if (n < 64)
            return spec;
      for (size_t i = 0; i < n; ++i)
            x[i] *= 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n - 1));
      for (int b = 0; b < bins; ++b) {
            const double f = 40.0 * std::pow(2.0, b * step / 1200.0);
            if (f >= 0.45 * rate)
                  break;
            // Goertzel
            const double w = 2 * M_PI * f / rate;
            const double c = 2 * std::cos(w);
            double s1 = 0, s2 = 0;
            for (size_t i = 0; i < n; ++i) {
                  const double s0 = x[i] + c * s1 - s2;
                  s2 = s1;
                  s1 = s0;
                  }
            const double power = std::max(0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
            spec[size_t(b)] = std::log1p(1e4 * std::sqrt(power) / double(n));
            }
      double mean = 0;
      for (double v : spec)
            mean += v;
      mean /= bins;
      double norm = 0;
      for (double& v : spec) {
            v -= mean;
            norm += v * v;
            }
      norm = std::sqrt(norm);
      if (norm > 0)
            for (double& v : spec)
                  v /= norm;
      return spec;
      }

double PluginExtract::centsShift(const std::vector<float>& reference, const std::vector<float>& shifted,
                                 double sampleRate, double maxCents, double* confidence)
      {
      const double step = 5.0;                  // cents a bin
      const int bins = int(std::log2(5000.0 / 40.0) * 1200.0 / step);
      const std::vector<double> a = logSpectrum(reference, sampleRate, step, bins);
      const std::vector<double> b = logSpectrum(shifted, sampleRate, step, bins);
      const int maxShift = int(maxCents / step);
      std::vector<double> score(size_t(2 * maxShift + 1), -1.0);
      int best = 0;
      for (int s = -maxShift; s <= maxShift; ++s) {
            double sum = 0;
            for (int i = 0; i < bins; ++i) {
                  const int j = i + s;
                  if (j >= 0 && j < bins)
                        sum += a[size_t(i)] * b[size_t(j)];
                  }
            score[size_t(s + maxShift)] = sum;
            if (sum > score[size_t(best + maxShift)])
                  best = s;
            }
      double refined = best;
      if (best > -maxShift && best < maxShift) {    // (a parabola through the peak and its neighbours)
            const double l = score[size_t(best - 1 + maxShift)], m = score[size_t(best + maxShift)], r = score[size_t(best + 1 + maxShift)];
            const double d = l - 2 * m + r;
            if (d < 0)
                  refined = best + 0.5 * (l - r) / d;
            }
      if (confidence)
            *confidence = std::max(0.0, score[size_t(best + maxShift)]);
      return refined * step;
      }

static double round1(double x)
      {
      return std::round(x * 10) / 10;
      }

//---------------------------------------------------------
//   pitchBend
//---------------------------------------------------------

QJsonObject PluginExtract::pitchBend(Vst3Plugin* p, const Settings& s, Capture capture,
                                     std::function<void()> prepare, std::function<void(const QString&)> status)
      {
      QJsonObject out;
      out["pitch"] = s.pitch;
      auto play = [&](int bend, std::vector<float>* c) {
            if (status)
                  status(QString("pitch bend %1…").arg(bend));
            p->midi(ME_PITCHBEND, s.channel, bend & 0x7f, bend >> 7);
            if (!capture(150, nullptr))
                  return false;
            if (prepare)
                  prepare();
            p->midi(ME_NOTEON, s.channel, s.pitch, s.velocity);
            if (!capture(300, nullptr))                        // (the attack left out)
                  return false;
            if (!capture(1200, c))
                  return false;
            p->midi(ME_NOTEON, s.channel, s.pitch, 0);
            return capture(900, nullptr);
            };
      std::vector<float> reference;
      bool ok = play(8192, &reference);
      QJsonArray bends;
      for (int bend : { 0, 4096, 6144, 7168, 8192, 9216, 10240, 12288, 16383 }) {
            std::vector<float> c;
            if (!ok || !(ok = play(bend, &c)))
                  break;
            double confidence = 0;
            const double cents = centsShift(reference, c, s.sampleRate, 2600, &confidence);
            QJsonObject o;
            o["bend"] = bend;
            o["cents"] = round1(cents);
            o["confidence"] = std::round(confidence * 100) / 100;
            o["db"] = round1(level(c).db);
            bends.append(o);
            if (bend == 16383 && confidence >= 0.5)
                  out["rangeUp"] = round1(cents);
            }
      p->midi(ME_PITCHBEND, s.channel, 0, 64);             // (the centre)
      capture(200, nullptr);
      out["bends"] = bends;
      return out;
      }


//---------------------------------------------------------
//   helpers
//---------------------------------------------------------

namespace {

struct Snapshot {
      std::map<unsigned, double> values;
      };

struct Context {
      Vst3Plugin* p;
      const PluginExtract::Settings& s;
      PluginExtract::Run& run;
      PluginExtract::Grab& grab;
      std::map<unsigned, QString> titles;
      std::set<unsigned> controllerParams;      // the MIDI controllers' parameters (any channel)
      std::set<unsigned> selfChanging;          // what changes by itself (meters …): left out

      // what changes (or is reported) while nothing is touched, over what() (a few notes)
      bool learnSelfChanging(const std::function<bool()>& what)
            {
            p->takeReported();
            const Snapshot a = snapshot();
            if (!what())
                  return false;
            const Snapshot b = snapshot();
            for (const auto& v : a.values)
                  if (b.values.count(v.first) && std::fabs(b.values.at(v.first) - v.second) >= 1e-6)
                        selfChanging.insert(v.first);
            for (const auto& r : p->takeReported())
                  selfChanging.insert(r.first);
            return true;
            }

      QJsonArray selfChangingList() const
            {
            QJsonArray list;
            for (unsigned id : selfChanging)
                  list.append(QString("%1 %2").arg(id).arg(titles.count(id) ? titles.at(id) : QString("(not listed)")));
            return list;
            }

      Snapshot snapshot() const
            {
            Snapshot sn;
            for (const auto& t : titles)
                  sn.values[t.first] = p->parameter(t.first);
            return sn;
            }

      // the parameters that differ between a and b, but those in skip
      QJsonArray changed(const Snapshot& a, const Snapshot& b, const std::set<unsigned>& skip) const
            {
            QJsonArray list;
            for (const auto& v : a.values) {
                  if (skip.count(v.first) || selfChanging.count(v.first))
                        continue;
                  auto it = b.values.find(v.first);
                  if (it == b.values.end() || std::fabs(it->second - v.second) < 1e-6)
                        continue;
                  QJsonObject o;
                  o["id"] = double(v.first);
                  o["title"] = titles.count(v.first) ? titles.at(v.first) : QString();
                  o["from"] = v.second;
                  o["to"] = it->second;
                  o["fromText"] = p->parameterText(v.first, v.second);
                  o["toText"] = p->parameterText(v.first, it->second);
                  list.append(o);
                  }
            return list;
            }

      QJsonArray reported(const std::set<unsigned>& skip)
            {
            std::map<unsigned, double> last;
            for (const auto& r : p->takeReported())
                  if (!skip.count(r.first) && !selfChanging.count(r.first))
                        last[r.first] = r.second;
            QJsonArray list;
            for (const auto& r : last)
                  list.append(QString("%1 %2 = %3 (%4)").arg(r.first).arg(titles.count(r.first) ? titles.at(r.first) : QString())
                              .arg(r.second).arg(p->parameterText(r.first, r.second)));
            return list;
            }
      };

Context makeContext(Vst3Plugin* p, const PluginExtract::Settings& s, PluginExtract::Run& run, PluginExtract::Grab& grab)
      {
      Context c { p, s, run, grab, {}, {} };
      for (const Vst3Plugin::Parameter& par : p->parameters())
            c.titles[par.id] = par.title;
      for (int ch = 0; ch < 16; ++ch)
            for (int cc = 0; cc < 130; ++cc) {
                  const long id = p->controllerParameter(ch, cc);
                  if (id >= 0)
                        c.controllerParams.insert(unsigned(id));
                  }
      return c;
      }

void sendController(Vst3Plugin* p, int channel, int cc, int value)
      {
      value = qBound(0, value, 127);
      if (cc == PITCHBEND) {
            const int v14 = value == 127 ? 16383 : value * 128;
            p->midi(ME_PITCHBEND, channel, v14 & 0x7f, v14 >> 7);
            }
      else if (cc == AFTERTOUCH)
            p->midi(ME_AFTERTOUCH, channel, value, 0);
      else
            p->midi(ME_CONTROLLER, channel, cc, value);
      }

QString controllerName(int cc)
      {
      switch (cc) {
            case AFTERTOUCH: return "channel pressure";
            case PITCHBEND:  return "pitch bend";
            case 0:   return "bank select";
            case 1:   return "modulation";
            case 2:   return "breath";
            case 7:   return "volume";
            case 10:  return "pan";
            case 11:  return "expression";
            case 64:  return "sustain pedal";
            case 65:  return "portamento";
            case 66:  return "sostenuto";
            case 67:  return "soft pedal";
            case 68:  return "legato footswitch";
            default:  return QString();
            }
      }

// a new note for each try: a short articulation, or a controller that ends it, leaves none
bool restartNote(Vst3Plugin* p, const PluginExtract::Settings& s, PluginExtract::Run& run)
      {
      p->midi(ME_NOTEON, s.channel, s.pitch, 0);
      if (!run(150, nullptr))
            return false;
      p->midi(ME_NOTEON, s.channel, s.pitch, s.velocity);
      return run(400, nullptr);
      }

// a background run's step: its key noted (a crash names it), or false when it is to be left out
bool stepAllowed(const PluginExtract::Settings& s, const QString& key, QJsonArray* skipped)
      {
      if (s.skip && s.skip(key)) {
            skipped->append(key);
            return false;
            }
      if (s.step)
            s.step(key);
      return true;
      }

double levelDistance(const PluginExtract::Level& a, const PluginExtract::Level& b)
      {
      const bool both = a.db > -90 && b.db > -90;
      return std::fabs(a.db - b.db) + (both ? std::fabs(a.brightness - b.brightness) + std::fabs(a.balance - b.balance) : 0.0);
      }

} // namespace

//---------------------------------------------------------
//   controllers
//---------------------------------------------------------

QJsonObject PluginExtract::controllers(Vst3Plugin* p, const Settings& s, Run run, Grab grab, Status status, std::vector<Found>* found, bool* cancelled)
      {
      QJsonObject out;
      Context c = makeContext(p, s, run, grab);
      auto stop = [&]() { if (cancelled) *cancelled = true; out["cancelled"] = true; return out; };
      QJsonArray skippedAfterCrash;
      if (s.step)
            s.step("controllers: baseline");

      // what changes by itself: the window (meters …) and the sound (vibrato, round robins)
      Level a, b;
      if (!restartNote(p, s, run) || !run(std::max(0, s.grabWait - 550), nullptr) || !run(s.listen, &a))
            return stop();
      const QImage g1 = grab();
      if (!restartNote(p, s, run) || !run(std::max(0, s.grabWait - 550), nullptr) || !run(s.listen, &b))
            return stop();
      const QImage g2 = grab();
      const int pixelNoise = g1.isNull() ? 0 : differingPixels(g1, g2);
      const int pixelThreshold = std::max(30, 3 * pixelNoise);
      if (!g1.isNull()) {
            out["windowSize"] = QJsonArray { g1.width(), g1.height() };
            out["cellSize"] = CELL;
            out["noiseCells"] = changedCells(g1, g2);
            }
      const double soundNoise = levelDistance(a, b);
      const double soundThreshold = std::max(1.5, 3 * soundNoise);
      // a sound averaged over some notes (round robins differ in level): the patch as it is (a
      // controller put back sounds like it again), and each try of a search by sound
      auto listen = [&](int notes, Level* l) {
            double db = 0, brightness = 0, balance = 0;
            for (int i = 0; i < notes; ++i) {
                  Level x;
                  if (!restartNote(p, s, run) || !run(std::max(0, s.grabWait - 550), nullptr) || !run(s.listen, &x))
                        return false;
                  db += x.db;
                  brightness += x.brightness;
                  balance += x.balance;
                  }
            l->db = db / notes;
            l->brightness = brightness / notes;
            l->balance = balance / notes;
            return true;
            };
      Level baseline;
      if (!c.learnSelfChanging([&]() { return listen(6, &baseline); }))
            return stop();
      out["selfChangingParameters"] = c.selfChangingList();
      out["baselineDb"] = QJsonArray { round1(baseline.db), round1(baseline.brightness), round1(baseline.balance) };
      out["windowNoisePixels"] = pixelNoise;
      out["soundNoiseDb"] = round1(soundNoise);
      out["noteLevelDb"] = round1(a.db);
      if (a.db < -90)
            out["warning"] = "the held note made no sound: the sound column means nothing";

      std::vector<int> ccs;
      if (!s.onlyControllers.empty()) {
            for (int cc : s.onlyControllers)
                  if (cc != s.switchCC)
                        ccs.push_back(cc);
            out["onlyControllers"] = int(ccs.size());
            }
      else {
            for (int cc = 0; cc < 120; ++cc)
                  if (cc != s.switchCC)
                        ccs.push_back(cc);
            ccs.push_back(AFTERTOUCH);
            ccs.push_back(PITCHBEND);
            }

      QJsonArray effects;
      QJsonArray none;
      QJsonArray notMapped;
      int done = 0;
      for (int cc : ccs) {
            ++done;
            const long proxy = p->controllerParameter(s.channel, cc);
            if (proxy < 0) {
                  notMapped.append(cc);
                  continue;
                  }
            if (!stepAllowed(s, QString("cc %1").arg(cc), &skippedAfterCrash))
                  continue;
            const QString name = controllerName(cc);
            if (status)
                  status(QString("controller %1%2 (%3 of %4)").arg(cc < 128 ? QString("CC %1").arg(cc) : name)
                         .arg(cc < 128 && !name.isEmpty() ? " " + name : QString()).arg(done).arg(ccs.size()));
            std::set<unsigned> skip { unsigned(proxy) };
            if (!restartNote(p, s, run) || !run(std::max(0, s.grabWait - 550), nullptr))
                  return stop();
            c.reported(skip);
            const Snapshot before = c.snapshot();
            Level l0, lLow, lHigh;
            if (!run(s.listen, &l0))
                  return stop();
            // (its own value known from an earlier run: no search, so no picture of it as it was)
            const auto known = s.patchValues.find(cc);
            const QImage g0 = known == s.patchValues.end() ? grab() : QImage();

            // each value on a new note, measured as long after its start as l0 (a decaying
            // sample would otherwise sound softer at every try)
            auto tryValue = [&](int value, Level* l, QImage* g) {
                  sendController(p, s.channel, cc, value);
                  if (!restartNote(p, s, run) || !run(std::max(0, s.grabWait - 550), nullptr) || !run(s.listen, l))
                        return false;
                  if (g)
                        *g = grab();
                  return true;
                  };
            QImage gLow, gHigh;
            if (!tryValue(0, &lLow, &gLow))
                  return stop();
            const Snapshot low = c.snapshot();
            const QJsonArray reportedLow = c.reported(skip);
            if (!tryValue(127, &lHigh, &gHigh))
                  return stop();
            const Snapshot high = c.snapshot();
            const QJsonArray reportedHigh = c.reported(skip);

            const int pixels = gLow.isNull() ? 0 : differingPixels(gLow, gHigh);
            const bool window = pixels > pixelThreshold;
            const bool sound = levelDistance(lLow, lHigh) > soundThreshold;
            QJsonArray params = c.changed(low, high, skip);
            const QJsonArray fromBefore = c.changed(before, low, skip);
            const bool paramsChanged = !params.isEmpty() || !fromBefore.isEmpty();
            // (the value its parameter had: the plug-in's default, nothing was sent before)
            const int previous = int(std::lround(before.values.count(unsigned(proxy)) ? before.values.at(unsigned(proxy)) * 127 : 0));
            if (!window && !sound && !paramsChanged && reportedLow.isEmpty() && reportedHigh.isEmpty()) {
                  none.append(cc);
                  sendController(p, s.channel, cc, cc == PITCHBEND ? 64 : previous);
                  continue;
                  }

            QJsonObject e;
            e["cc"] = cc;
            if (!name.isEmpty())
                  e["name"] = name;
            e["parameter"] = double(proxy);
            QJsonArray what;
            if (window)
                  what.append("window");
            if (sound)
                  what.append("sound");
            if (paramsChanged)
                  what.append("parameters");
            if (!reportedLow.isEmpty() || !reportedHigh.isEmpty())
                  what.append("reported");
            e["changes"] = what;
            e["windowPixels"] = pixels;
            const QRect r = window ? changedRect(gLow, gHigh) : QRect();
            if (!r.isNull())
                  e["region"] = QJsonArray { r.x(), r.y(), r.width(), r.height() };
            if (window)
                  e["cells"] = changedCells(gLow, gHigh);
            e["levelDb"] = QJsonArray { round1(l0.db), round1(lLow.db), round1(lHigh.db) };
            e["brightnessDb"] = QJsonArray { round1(l0.brightness), round1(lLow.brightness), round1(lHigh.brightness) };
            e["balanceDb"] = QJsonArray { round1(l0.balance), round1(lLow.balance), round1(lHigh.balance) };
            if (!params.isEmpty())
                  e["parametersLowToHigh"] = params;
            if (!fromBefore.isEmpty())
                  e["parametersBeforeToLow"] = fromBefore;
            if (!reportedLow.isEmpty() || !reportedHigh.isEmpty())
                  e["reportedByPlugin"] = QJsonArray { reportedLow, reportedHigh };

            // Quick: the patch reloaded, as it was before (its own value of every controller); the
            // controller's own value is not known then ("patchValue" left out)
            if (s.restore && cc != PITCHBEND) {
                  if (!s.restore())
                        return stop();
                  e["restored"] = true;
                  if (!run(s.grabWait, nullptr))
                        return stop();
                  c.reported(skip);
                  effects.append(e);
                  if (found && window && !r.isNull()) {
                        QStringList bits;
                        for (const QJsonValue& w : what)
                              bits << w.toString();
                        found->push_back({ QString("%1%2: %3 · level %4 → %5 dB")
                                           .arg(cc < 128 ? QString("CC %1").arg(cc) : name)
                                           .arg(cc < 128 && !name.isEmpty() ? " (" + name + ")" : QString())
                                           .arg(bits.join(", ")).arg(round1(lLow.db)).arg(round1(lHigh.db)),
                                           gLow.copy(r), gHigh.copy(r) });
                        }
                  continue;
                  }

            // back to the patch's own value: the one that looks (else sounds) most like before
            int best = -1;
            double bestDistance = 0;
            if (known != s.patchValues.end()) {
                  best = known->second;
                  e["patchValueMatch"] = "an earlier run's";
                  }
            else if (cc == PITCHBEND)
                  best = 64;
            else if (window || sound) {
                  std::map<int, double> tried;
                  auto distance = [&](int value, double* d) {
                        auto it = tried.find(value);
                        if (it != tried.end()) {
                              *d = it->second;
                              return true;
                              }
                        Level l;
                        QImage g;
                        if (window) {
                              if (!tryValue(value, &l, &g))
                                    return false;
                              *d = double(differingPixels(g, g0));
                              }
                        else {
                              sendController(p, s.channel, cc, value);
                              if (!listen(3, &l))
                                    return false;
                              *d = levelDistance(l, baseline);
                              }
                        tried[value] = *d;
                        return true;
                        };
                  if (window) {
                        tried[0] = double(differingPixels(gLow, g0));
                        tried[127] = double(differingPixels(gHigh, g0));
                        }
                  for (int v = 0; v <= 127; v = v == 112 ? 127 : v + 16) {
                        double d;
                        if (!distance(v, &d))
                              return stop();
                        }
                  for (const auto& t : tried)
                        if (best < 0 || t.second < bestDistance) {
                              best = t.first;
                              bestDistance = t.second;
                              }
                  for (int step : { 8, 4, 2, 1 }) {
                        const int centre = best;
                        for (int v : { centre - step, centre + step }) {
                              if (v < 0 || v > 127)
                                    continue;
                              double d;
                              if (!distance(v, &d))
                                    return stop();
                              if (d < bestDistance) {
                                    best = v;
                                    bestDistance = d;
                                    }
                              }
                        }
                  e["patchValueMatch"] = window ? QString("%1 pixels differ").arg(int(bestDistance))
                                                : QString("%1 dB").arg(round1(bestDistance));
                  }
            else {
                  // parameters only: the value its parameter had
                  best = previous;
                  }
            e["patchValue"] = best;
            sendController(p, s.channel, cc, best);
            if (!run(s.grabWait, nullptr))
                  return stop();
            c.reported(skip);
            effects.append(e);
            if (found && window && !r.isNull()) {
                  QStringList bits;
                  for (const QJsonValue& w : what)
                        bits << w.toString();
                  found->push_back({ QString("%1%2: %3 · level %4 → %5 dB · patch value %6")
                                     .arg(cc < 128 ? QString("CC %1").arg(cc) : name)
                                     .arg(cc < 128 && !name.isEmpty() ? " (" + name + ")" : QString())
                                     .arg(bits.join(", ")).arg(round1(lLow.db)).arg(round1(lHigh.db)).arg(best),
                                     gLow.copy(r), gHigh.copy(r) });
                  }
            }
      // the patch as it was at the start? (each controller went back to the value that sounds or looks
      // like before; the value its parameter had can be one Kontakt never received: CC 7 at 0 is silence,
      // the owner's background run of 2026-09-28 08:54 measured every later controller on a silent patch)
      if (s.step)
            s.step("controllers: end");
      Level end;
      if (!listen(6, &end))
            return stop();
      out["endDb"] = QJsonArray { round1(end.db), round1(end.brightness), round1(end.balance) };
      out["endDistanceDb"] = round1(levelDistance(end, baseline));
      p->midi(ME_NOTEON, s.channel, s.pitch, 0);
      run(300, nullptr);
      out["effects"] = effects;
      out["noEffect"] = none;
      out["notMapped"] = notMapped;
      if (!skippedAfterCrash.isEmpty())
            out["skippedAfterCrash"] = skippedAfterCrash;
      return out;
      }

//---------------------------------------------------------
//   parameters
//---------------------------------------------------------

QJsonObject PluginExtract::parameters(Vst3Plugin* p, const Settings& s, Run run, Grab grab, Status status, std::vector<Found>* found, bool* cancelled)
      {
      QJsonObject out;
      Context c = makeContext(p, s, run, grab);
      auto stop = [&]() { if (cancelled) *cancelled = true; out["cancelled"] = true; return out; };
      const std::vector<Vst3Plugin::Parameter> all = p->parameters();

      // placeholders: many parameters titled alike but for their numbers
      std::map<QString, int> families;
      auto family = [](const QString& title) {
            QString f = title;
            f.replace(QRegularExpression("\\d+"), "#");
            return f.trimmed();
            };
      for (const Vst3Plugin::Parameter& par : all)
            if (!c.controllerParams.count(par.id))
                  ++families[family(par.title)];
      QJsonObject placeholders;
      for (const auto& f : families)
            if (f.second > 8)
                  placeholders[f.first] = f.second;
      out["placeholders"] = placeholders;
      out["controllerParameters"] = int(c.controllerParams.size());

      std::vector<Vst3Plugin::Parameter> tried;
      QJsonArray skipped;
      for (const Vst3Plugin::Parameter& par : all) {
            if (c.controllerParams.count(par.id) || families[family(par.title)] > 8)
                  continue;
            if (!s.onlyParameters.isEmpty() && !s.onlyParameters.contains(par.title.trimmed(), Qt::CaseInsensitive))
                  continue;
            if (par.flags & (READ_ONLY | PROGRAM_CHANGE | BYPASS)) {
                  skipped.append(QString("%1 %2 (%3)").arg(par.id).arg(par.title)
                                 .arg(par.flags & READ_ONLY ? "read only" : par.flags & PROGRAM_CHANGE ? "program change" : "bypass"));
                  continue;
                  }
            tried.push_back(par);
            }
      out["notTried"] = skipped;
      if (tried.size() > 200) {
            out["triedOnlyFirst"] = 200;
            tried.resize(200);
            }

      QJsonArray skippedAfterCrash;
      if (s.step)
            s.step("parameters: baseline");
      Level a, b;
      QImage g1;
      if (!c.learnSelfChanging([&]() {
                  if (!restartNote(p, s, run) || !run(std::max(0, s.grabWait - 550), nullptr) || !run(s.listen, &a))
                        return false;
                  g1 = grab();
                  return restartNote(p, s, run) && run(std::max(0, s.grabWait - 550), nullptr) && run(s.listen, &b);
                  }))
            return stop();
      out["selfChangingParameters"] = c.selfChangingList();
      const QImage g2 = g1.isNull() ? QImage() : grab();
      const int pixelThreshold = std::max(30, 3 * (g1.isNull() ? 0 : differingPixels(g1, g2)));
      if (!g1.isNull())
            out["noiseCells"] = changedCells(g1, g2);
      const double soundThreshold = std::max(1.5, 3 * levelDistance(a, b));

      QJsonArray effects;
      QJsonArray none;
      int done = 0;
      for (const Vst3Plugin::Parameter& par : tried) {
            ++done;
            if (!stepAllowed(s, QString("parameter %1").arg(par.id), &skippedAfterCrash))
                  continue;
            if (status)
                  status(QString("parameter %1 \"%2\" (%3 of %4)").arg(par.id).arg(par.title).arg(done).arg(tried.size()));
            std::set<unsigned> skip { par.id };
            c.reported(skip);
            const double original = p->parameter(par.id);
            auto tryValue = [&](double v, Level* l, QImage* g) {
                  p->setParameter(par.id, v);
                  if (!restartNote(p, s, run) || !run(std::max(0, s.grabWait - 550), nullptr) || !run(s.listen, l))
                        return false;
                  *g = grab();
                  return true;
                  };
            Level lLow, lHigh;
            QImage gLow, gHigh;
            if (!tryValue(0.0, &lLow, &gLow))
                  return stop();
            const Snapshot low = c.snapshot();
            const QJsonArray reportedLow = c.reported(skip);
            if (!tryValue(1.0, &lHigh, &gHigh))
                  return stop();
            const Snapshot high = c.snapshot();
            const QJsonArray reportedHigh = c.reported(skip);
            p->setParameter(par.id, original);
            if (!run(s.grabWait, nullptr))
                  return stop();
            c.reported(skip);

            const int pixels = gLow.isNull() ? 0 : differingPixels(gLow, gHigh);
            const bool window = pixels > pixelThreshold;
            const bool sound = levelDistance(lLow, lHigh) > soundThreshold;
            const QJsonArray params = c.changed(low, high, skip);
            if (!window && !sound && params.isEmpty() && reportedLow.isEmpty() && reportedHigh.isEmpty()) {
                  none.append(QString("%1 %2").arg(par.id).arg(par.title));
                  continue;
                  }
            QJsonObject e;
            e["id"] = double(par.id);
            e["title"] = par.title;
            e["value"] = original;
            e["valueText"] = p->parameterText(par.id, original);
            QJsonArray what;
            if (window)
                  what.append("window");
            if (sound)
                  what.append("sound");
            if (!params.isEmpty())
                  what.append("parameters");
            if (!reportedLow.isEmpty() || !reportedHigh.isEmpty())
                  what.append("reported");
            e["changes"] = what;
            e["windowPixels"] = pixels;
            const QRect r = window ? changedRect(gLow, gHigh) : QRect();
            if (!r.isNull())
                  e["region"] = QJsonArray { r.x(), r.y(), r.width(), r.height() };
            if (window)
                  e["cells"] = changedCells(gLow, gHigh);
            e["levelDb"] = QJsonArray { round1(lLow.db), round1(lHigh.db) };
            e["brightnessDb"] = QJsonArray { round1(lLow.brightness), round1(lHigh.brightness) };
            e["balanceDb"] = QJsonArray { round1(lLow.balance), round1(lHigh.balance) };
            if (!params.isEmpty())
                  e["parametersLowToHigh"] = params;
            if (!reportedLow.isEmpty() || !reportedHigh.isEmpty())
                  e["reportedByPlugin"] = QJsonArray { reportedLow, reportedHigh };
            effects.append(e);
            if (found && !r.isNull())
                  found->push_back({ QString("parameter %1 \"%2\": level %3 → %4 dB").arg(par.id).arg(par.title)
                                     .arg(round1(lLow.db)).arg(round1(lHigh.db)), gLow.copy(r), gHigh.copy(r) });
            }
      p->midi(ME_NOTEON, s.channel, s.pitch, 0);
      run(300, nullptr);
      out["effects"] = effects;
      out["noEffect"] = none;
      if (!skippedAfterCrash.isEmpty())
            out["skippedAfterCrash"] = skippedAfterCrash;
      return out;
      }

//---------------------------------------------------------
//   switches
//---------------------------------------------------------

QJsonObject PluginExtract::switches(Vst3Plugin* p, const Settings& s, Run run, Status status, bool* cancelled)
      {
      QJsonObject out;
      if (s.switchCC < 0 || s.switchValues.empty())
            return out;
      Grab noGrab = []() { return QImage(); };
      Context c = makeContext(p, s, run, noGrab);
      std::set<unsigned> skip;
      const long proxy = p->controllerParameter(s.channel, s.switchCC);
      if (proxy >= 0)
            skip.insert(unsigned(proxy));
      out["switchCC"] = s.switchCC;
      out["switchParameter"] = double(proxy);
      if (!c.learnSelfChanging([&]() { return restartNote(p, s, run) && run(600, nullptr); })) {
            if (cancelled)
                  *cancelled = true;
            out["cancelled"] = true;
            return out;
            }
      p->midi(ME_NOTEON, s.channel, s.pitch, 0);
      run(300, nullptr);
      out["selfChangingParameters"] = c.selfChangingList();
      c.reported(skip);
      Snapshot last = c.snapshot();
      QJsonObject changes;
      QJsonArray skippedAfterCrash;
      int any = 0;
      for (int value : s.switchValues) {
            if (!stepAllowed(s, QString("switch %1").arg(value), &skippedAfterCrash))
                  continue;
            if (status)
                  status(QString("switch value %1").arg(value));
            p->midi(ME_CONTROLLER, s.channel, s.switchCC, value);
            if (!run(300, nullptr)) {
                  if (cancelled)
                        *cancelled = true;
                  out["cancelled"] = true;
                  return out;
                  }
            const Snapshot now = c.snapshot();
            QJsonObject e;
            const QJsonArray changed = c.changed(last, now, skip);
            const QJsonArray reported = c.reported(skip);
            if (!changed.isEmpty())
                  e["parameters"] = changed;
            if (!reported.isEmpty())
                  e["reportedByPlugin"] = reported;
            any += !e.isEmpty();
            changes[QString::number(value)] = e;
            last = now;
            }
      out["values"] = changes;
      out["valuesChangingParameters"] = any;
      if (!skippedAfterCrash.isEmpty())
            out["skippedAfterCrash"] = skippedAfterCrash;
      p->midi(ME_CONTROLLER, s.channel, s.switchCC, s.switchValues.front());
      run(300, nullptr);
      return out;
      }

} // namespace Ms
