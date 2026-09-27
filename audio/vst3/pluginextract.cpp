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

//---------------------------------------------------------
//   level
//---------------------------------------------------------

PluginExtract::Level PluginExtract::level(const std::vector<float>& buffer)
      {
      Level l;
      const size_t frames = buffer.size() / 2;
      if (frames < 2)
            return l;
      double power = 0, diffPower = 0;
      double prev = 0.5 * (double(buffer[0]) + double(buffer[1]));
      for (size_t i = 0; i < frames; ++i) {
            const double x = 0.5 * (double(buffer[2 * i]) + double(buffer[2 * i + 1]));
            power += x * x;
            if (i > 0)
                  diffPower += (x - prev) * (x - prev);
            prev = x;
            }
      power /= double(frames);
      diffPower /= double(frames - 1);
      l.db = power > 1e-20 ? 10 * std::log10(power) : -200;
      l.brightness = power > 1e-20 && diffPower > 1e-20 ? 10 * std::log10(diffPower / power) : 0;
      return l;
      }

static double round1(double x)
      {
      return std::round(x * 10) / 10;
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
                  if (skip.count(v.first))
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
                  if (!skip.count(r.first))
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

double levelDistance(const PluginExtract::Level& a, const PluginExtract::Level& b)
      {
      return std::fabs(a.db - b.db) + (a.db > -90 && b.db > -90 ? std::fabs(a.brightness - b.brightness) : 0.0);
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

      // what changes by itself: the window (meters …) and the sound (vibrato, round robins)
      if (!restartNote(p, s, run))
            return stop();
      Level a, b;
      if (!run(s.listen, &a))
            return stop();
      const QImage g1 = grab();
      if (!run(s.grabWait, nullptr) || !run(s.listen, &b))
            return stop();
      const QImage g2 = grab();
      const int pixelNoise = g1.isNull() ? 0 : differingPixels(g1, g2);
      const int pixelThreshold = std::max(30, 3 * pixelNoise);
      const double soundNoise = levelDistance(a, b);
      const double soundThreshold = std::max(1.5, 3 * soundNoise);
      out["windowNoisePixels"] = pixelNoise;
      out["soundNoiseDb"] = round1(soundNoise);
      out["noteLevelDb"] = round1(a.db);
      if (a.db < -90)
            out["warning"] = "the held note made no sound: the sound column means nothing";

      std::vector<int> ccs;
      for (int cc = 0; cc < 120; ++cc)
            if (cc != s.switchCC)
                  ccs.push_back(cc);
      ccs.push_back(AFTERTOUCH);
      ccs.push_back(PITCHBEND);

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
            const QString name = controllerName(cc);
            if (status)
                  status(QString("controller %1%2 (%3 of %4)").arg(cc < 128 ? QString("CC %1").arg(cc) : name)
                         .arg(cc < 128 && !name.isEmpty() ? " " + name : QString()).arg(done).arg(ccs.size()));
            std::set<unsigned> skip { unsigned(proxy) };
            if (!restartNote(p, s, run))
                  return stop();
            c.reported(skip);
            const Snapshot before = c.snapshot();
            Level l0, lLow, lHigh;
            const QImage g0 = grab();
            if (!run(s.listen, &l0))
                  return stop();

            auto tryValue = [&](int value, Level* l, QImage* g) {
                  sendController(p, s.channel, cc, value);
                  if (!run(s.grabWait, nullptr) || !run(s.listen, l))
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
            if (!window && !sound && !paramsChanged && reportedLow.isEmpty() && reportedHigh.isEmpty()) {
                  none.append(cc);
                  sendController(p, s.channel, cc, cc == PITCHBEND ? 64 : 0);
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
            e["levelDb"] = QJsonArray { round1(l0.db), round1(lLow.db), round1(lHigh.db) };
            e["brightnessDb"] = QJsonArray { round1(l0.brightness), round1(lLow.brightness), round1(lHigh.brightness) };
            if (!params.isEmpty())
                  e["parametersLowToHigh"] = params;
            if (!fromBefore.isEmpty())
                  e["parametersBeforeToLow"] = fromBefore;
            if (!reportedLow.isEmpty() || !reportedHigh.isEmpty())
                  e["reportedByPlugin"] = QJsonArray { reportedLow, reportedHigh };

            // back to the patch's own value: the one that looks (else sounds) most like before
            int best = -1;
            double bestDistance = 0;
            if (cc == PITCHBEND)
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
                        if (!tryValue(value, &l, window ? &g : nullptr))
                              return false;
                        *d = window ? double(differingPixels(g, g0)) : levelDistance(l, l0);
                        tried[value] = *d;
                        return true;
                        };
                  tried[0] = window ? double(differingPixels(gLow, g0)) : levelDistance(lLow, l0);
                  tried[127] = window ? double(differingPixels(gHigh, g0)) : levelDistance(lHigh, l0);
                  for (int v = 16; v < 127; v += 16) {
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
                  // parameters only: the value its parameter had (what was last sent)
                  best = int(std::lround(before.values.count(unsigned(proxy)) ? before.values.at(unsigned(proxy)) * 127 : 0));
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
      p->midi(ME_NOTEON, s.channel, s.pitch, 0);
      run(300, nullptr);
      out["effects"] = effects;
      out["noEffect"] = none;
      out["notMapped"] = notMapped;
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

      Level a, b;
      if (!restartNote(p, s, run) || !run(s.listen, &a))
            return stop();
      const QImage g1 = grab();
      if (!run(s.grabWait, nullptr) || !run(s.listen, &b))
            return stop();
      const int pixelThreshold = std::max(30, 3 * (g1.isNull() ? 0 : differingPixels(g1, grab())));
      const double soundThreshold = std::max(1.5, 3 * levelDistance(a, b));

      QJsonArray effects;
      QJsonArray none;
      int done = 0;
      for (const Vst3Plugin::Parameter& par : tried) {
            ++done;
            if (status)
                  status(QString("parameter %1 \"%2\" (%3 of %4)").arg(par.id).arg(par.title).arg(done).arg(tried.size()));
            std::set<unsigned> skip { par.id };
            if (!restartNote(p, s, run))
                  return stop();
            c.reported(skip);
            const double original = p->parameter(par.id);
            auto tryValue = [&](double v, Level* l, QImage* g) {
                  p->setParameter(par.id, v);
                  if (!run(s.grabWait, nullptr) || !run(s.listen, l))
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
            e["levelDb"] = QJsonArray { round1(lLow.db), round1(lHigh.db) };
            e["brightnessDb"] = QJsonArray { round1(lLow.brightness), round1(lHigh.brightness) };
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
      c.reported(skip);
      Snapshot last = c.snapshot();
      QJsonObject changes;
      int any = 0;
      for (int value : s.switchValues) {
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
      p->midi(ME_CONTROLLER, s.channel, s.switchCC, s.switchValues.front());
      run(300, nullptr);
      return out;
      }

} // namespace Ms
