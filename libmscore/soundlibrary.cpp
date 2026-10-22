//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  SoundLib: playback through an external sample library (see soundlibrary.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "soundlibrary.h"
#include "calibrationfile.h"
#include "partplayback.h"
#include "playbacksettings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>
#include <set>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include "accidental.h"
#include "articulation.h"
#include "chord.h"
#include "instrument.h"
#include "note.h"
#include "part.h"
#include "pitchspelling.h"
#include "score.h"
#include "segment.h"
#include "staff.h"
#include "stafftext.h"
#include "sym.h"
#include "tremolo.h"
#include "tempo.h"
#include "trill.h"
#include "tuning.h"
#include "automation.h"
#include "partcontrollers.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>                // (GlobalMemoryStatusEx: freeMemoryBytes)
#endif

namespace Ms {
namespace SoundLib {

//---------------------------------------------------------
//   Library::load
//---------------------------------------------------------

static QStringList words(const QString& s)
      {
      QStringList l = s.split(QRegularExpression("[\\s,]+"));
      l.removeAll(QString());
      return l;
      }

static bool readSwitch(const QXmlStreamAttributes& a, SwitchType& type, int& number)
      {
      const QString t = a.value("type").toString().toLower();
      if (t == "cc" || t.isEmpty())
            type = SwitchType::CC;
      else if (t == "keyswitch")
            type = SwitchType::KEYSWITCH;
      else if (t == "program")
            type = SwitchType::PROGRAM;
      else if (t == "none")
            type = SwitchType::NONE;
      else
            return false;
      if (a.hasAttribute("number"))
            number = a.value("number").toInt();
      return number >= 0 && number < 128;
      }

// <Articulation name="Long" value="1" [techniques="…"] [modifiers="…"] [expect="silent|ignored|unclear"]
//               [prefer="…"] [length="0.5" [from="0.43"]] [release="885"] [legatoDelay="210" | legatoDelay="-12:210 -7:230 … +12:360"]
//               [legatoVelocity="100"] [attacks="smooth:1-9:7 …" transitions="portamento:1-19:10 …" reattack="106"
//               reattackGap="54" overlap="24" [attackOnset="smooth:100.2;57:86.1,66:127.3 …"]] [avoid="48:100-116:90 …"] [onset="40" | onset="55:60 67:40 …"] [octaveUp="36:180 37:140 …"] [octaveDown="48:150 …"]
//               [bendGlide="136:-8:100 300:70:230 …"] [heldOffset="-3.4" | heldOffset="32:-7 112:-9"]
//               [accentedLevel="72/100:3.4 72/150:4.2 …"]/>;
// no techniques: listed for reference and checked, never chosen by notation

// legatoDelay / onset: one number (ms, for every interval / pitch) or "key:ms" pairs (an interval in signed
// semitones, a MIDI pitch), sorted into table; value: one number, or the pairs' median; false: malformed
static bool readKeyedMs(const QString& text, double& value, std::vector<std::pair<int, double>>& table)
      {
      table.clear();
      if (!text.contains(':')) {
            bool ok = false;
            value = text.toDouble(&ok);
            return ok;
            }
      std::vector<double> values;
      for (const QString& pair : text.split(' ', QString::SkipEmptyParts)) {
            const QStringList iv = pair.split(':');
            bool ok1 = false, ok2 = false;
            const int interval = iv.size() == 2 ? iv[0].trimmed().toInt(&ok1) : 0;      // ("+2" reads as 2)
            const double ms = iv.size() == 2 ? iv[1].toDouble(&ok2) : 0;
            if (!ok1 || !ok2 || ms < 0)
                  return false;
            table.push_back({ interval, ms });
            values.push_back(ms);
            }
      if (values.empty())
            return false;
      std::sort(table.begin(), table.end());
      std::sort(values.begin(), values.end());
      const size_t n = values.size();
      value = n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
      return true;
      }

// the table's value at key: linear between its keys, the nearest end's beyond; none: value
static double keyedMsAt(const std::vector<std::pair<int, double>>& table, double value, int key)
      {
      if (table.empty())
            return value;
      if (key <= table.front().first)
            return table.front().second;
      if (key >= table.back().first)
            return table.back().second;
      for (size_t i = 1; i < table.size(); ++i) {
            const auto& b = table[i];
            if (key > b.first)
                  continue;
            const auto& a = table[i - 1];
            if (b.first == a.first)
                  return b.second;
            return a.second + (b.second - a.second) * (key - a.first) / double(b.first - a.first);
            }
      return table.back().second;
      }

static double levelGridAt(const std::vector<Articulation::QuickLevel>& grid, int pitch, double ms);

double octaveDelayAt(const std::vector<std::pair<int, double>>& table, double fallback, int fromPitch)
      {
      if (table.empty() || fromPitch < 0)
            return fallback;
      auto hi = std::lower_bound(table.begin(), table.end(), std::make_pair(fromPitch, -1e300));
      if (hi != table.end() && hi->first == fromPitch)
            return hi->second;
      if (hi == table.begin())
            return hi->second;
      auto lo = hi - 1;
      if (hi == table.end())
            return lo->second;
      const int dLo = fromPitch - lo->first;
      const int dHi = hi->first - fromPitch;
      if (dLo != dHi)
            return dLo < dHi ? lo->second : hi->second;
      // a tie: the shorter run of like values (its zone lacks this start)
      static constexpr double LIKE_MS = 50;
      int runLo = 1;
      for (auto it = lo; it != table.begin(); --it) {
            auto prev = it - 1;
            if (prev->first != it->first - 1 || std::abs(prev->second - lo->second) >= LIKE_MS)
                  break;
            ++runLo;
            }
      int runHi = 1;
      for (auto it = hi; it + 1 != table.end(); ++it) {
            auto next = it + 1;
            if (next->first != it->first + 1 || std::abs(next->second - hi->second) >= LIKE_MS)
                  break;
            ++runHi;
            }
      return runHi < runLo ? hi->second : lo->second;
      }

double Articulation::legatoDelayAt(int interval, int fromPitch) const
      {
      if (interval == 12 && !octaveUp.empty())
            return octaveDelayAt(octaveUp, octaveUpMs, fromPitch);
      if (interval == -12 && !octaveDown.empty())
            return octaveDelayAt(octaveDown, octaveDownMs, fromPitch);
      return keyedMsAt(legatoDelays, legatoDelayMs, interval);
      }

bool Articulation::bendGlideAt(double lenBeforeMs, double* leaveMs, double* arriveMs) const
      {
      if (bendGlides.empty())
            return false;
      const BendGlide* lo = &bendGlides.front();
      const BendGlide* hi = &bendGlides.back();
      for (size_t i = 0; i + 1 < bendGlides.size(); ++i)
            if (bendGlides[i].lenBeforeMs <= lenBeforeMs && lenBeforeMs <= bendGlides[i + 1].lenBeforeMs) {
                  lo = &bendGlides[i];
                  hi = &bendGlides[i + 1];
                  break;
                  }
      double f = 0;
      if (lenBeforeMs >= hi->lenBeforeMs)
            f = 1;
      else if (lenBeforeMs > lo->lenBeforeMs && hi->lenBeforeMs > lo->lenBeforeMs)
            f = (lenBeforeMs - lo->lenBeforeMs) / (hi->lenBeforeMs - lo->lenBeforeMs);
      if (lenBeforeMs <= bendGlides.front().lenBeforeMs) {
            lo = hi = &bendGlides.front();
            f = 0;
            }
      *leaveMs = lo->leaveMs + (hi->leaveMs - lo->leaveMs) * f;
      *arriveMs = lo->arriveMs + (hi->arriveMs - lo->arriveMs) * f;
      return true;
      }

double Articulation::onsetAt(int pitch) const
      {
      return keyedMsAt(onsets, onsetMs, pitch);
      }

double Articulation::Technique::onsetAt(int pitch) const
      {
      return keyedMsAt(onsets, onsetMs, pitch);
      }

double Articulation::startOnset(const Technique* attack, int pitch, bool byPitch) const
      {
      if (attack && attack->hasOnset())
            return byPitch ? attack->onsetAt(pitch) : attack->onsetMs;
      return byPitch ? onsetAt(pitch) : onsetMedian();
      }

double Articulation::onsetMedian() const
      {
      if (onsets.empty())
            return onsetMs;
      std::vector<double> v;
      for (const auto& p : onsets)
            v.push_back(p.second);
      std::sort(v.begin(), v.end());
      const size_t n = v.size();
      return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
      }

int Articulation::avoidVelocity(int key, int velocity) const
      {
      for (const Avoid& a : avoid)
            if (key >= a.keyLow && key <= a.keyHigh && velocity >= a.low && velocity <= a.high)
                  return a.velocity;
      return velocity;
      }

const Articulation::Technique* Articulation::attack(const QString& name) const
      {
      for (const Technique& t : attacks)
            if (t.name == name)
                  return &t;
      return nullptr;
      }

const Articulation::Technique* Articulation::transition(const QString& name) const
      {
      for (const Technique& t : transitions)
            if (t.name == name)
                  return &t;
      return nullptr;
      }

const std::vector<Articulation::ContextLevel>& Articulation::contextGrid(ContextKind kind) const
      {
      if (kind == ContextKind::SLUR)
            return contextLevels;
      const std::vector<ContextLevel>& g = plainContextLevels[int(kind) - 1];
      return g.empty() ? contextLevels : g;
      }

double Articulation::contextLevelAt(double cc, double ms, double pitch, ContextKind kind) const
      {
      const std::vector<ContextLevel>& contextLevels = contextGrid(kind);
      if (contextLevels.empty())
            return 0.0;
      std::vector<double> ccs, mss, pitches;
      for (const ContextLevel& l : contextLevels) {
            ccs.push_back(l.cc);
            mss.push_back(std::log(l.ms));
            pitches.push_back(l.pitch);
            }
      for (std::vector<double>* v : { &ccs, &mss, &pitches }) {
            std::sort(v->begin(), v->end());
            v->erase(std::unique(v->begin(), v->end()), v->end());
            }
      // the two grid values around x and x's share of the way between them (one value: both it)
      struct Around { size_t lo, hi; double t; };
      auto around = [](const std::vector<double>& v, double x) {
            if (x <= v.front())
                  return Around { 0, 0, 0.0 };
            if (x >= v.back())
                  return Around { v.size() - 1, v.size() - 1, 0.0 };
            size_t hi = 1;
            while (v[hi] < x)
                  ++hi;
            return Around { hi - 1, hi, (x - v[hi - 1]) / (v[hi] - v[hi - 1]) };
            };
      const Around a = around(ccs, cc), b = around(mss, std::log(qMax(ms, 1.0))), c = around(pitches, pitch);
      auto at = [&](size_t i, size_t j, size_t k) {
            for (const ContextLevel& l : contextLevels)
                  if (l.cc == ccs[i] && std::fabs(std::log(l.ms) - mss[j]) < 1e-9 && l.pitch == pitches[k])
                        return l.db;
            return 0.0;       // (not a full grid: the map's generator writes one)
            };
      double db = 0.0;
      for (int x = 0; x < 2; ++x)
            for (int y = 0; y < 2; ++y)
                  for (int z = 0; z < 2; ++z)
                        db += (x ? a.t : 1 - a.t) * (y ? b.t : 1 - b.t) * (z ? c.t : 1 - c.t)
                              * at(x ? a.hi : a.lo, y ? b.hi : b.lo, z ? c.hi : c.lo);
      return db;
      }

double Articulation::accentedLevelAt(int pitch, double ms) const
      {
      return levelGridAt(accentedLevels, pitch, ms);
      }

double Articulation::heldOffsetAt(int cc) const
      {
      if (heldOffsets.empty())
            return 0.0;
      if (heldOffsets.size() == 1 || cc <= heldOffsets.front().first)
            return heldOffsets.front().second;
      if (cc >= heldOffsets.back().first)
            return heldOffsets.back().second;
      auto hi = std::lower_bound(heldOffsets.begin(), heldOffsets.end(), cc,
                                 [](const std::pair<int, double>& x, int c) { return x.first < c; });
      auto lo = hi - 1;
      return lo->second + (hi->second - lo->second) * double(cc - lo->first) / double(hi->first - lo->first);
      }

double Articulation::quickLevelAt(int pitch, double ms) const
      {
      return levelGridAt(quickLevels, pitch, ms);
      }

// a pitch/ms:dB grid's value (quickLevel, accentedLevel): log-linear in ms, linear in pitch, the nearest end's beyond; 0: none
static double levelGridAt(const std::vector<Articulation::QuickLevel>& quickLevels, int pitch, double ms)
      {
      // each measured pitch's level at ms, then between the two pitches around the note
      std::vector<std::pair<int, double>> byPitch;
      const double x = std::log(qMax(ms, 1.0));
      for (size_t i = 0; i < quickLevels.size(); ) {
            size_t j = i;
            while (j < quickLevels.size() && quickLevels[j].pitch == quickLevels[i].pitch)
                  ++j;
            double db = quickLevels[i].db;
            for (size_t k = i; k < j; ++k) {
                  if (x <= std::log(quickLevels[k].ms) || k + 1 == j) {
                        db = quickLevels[k].db;
                        if (k > i && x < std::log(quickLevels[k].ms)) {
                              const double x0 = std::log(quickLevels[k - 1].ms), x1 = std::log(quickLevels[k].ms);
                              db = quickLevels[k - 1].db + (quickLevels[k].db - quickLevels[k - 1].db) * (x - x0) / (x1 - x0);
                              }
                        break;
                        }
                  }
            byPitch.push_back({ quickLevels[i].pitch, db });
            i = j;
            }
      if (byPitch.empty())
            return 0.0;
      if (pitch <= byPitch.front().first)
            return byPitch.front().second;
      for (size_t i = 1; i < byPitch.size(); ++i)
            if (pitch <= byPitch[i].first) {
                  const auto& a = byPitch[i - 1];
                  const auto& b = byPitch[i];
                  return a.second + (b.second - a.second) * (pitch - a.first) / double(b.first - a.first);
                  }
      return byPitch.back().second;
      }

// <Drum pitch="38" key="62" name="Snare hit" [velocity="127"] [ids="snare-drum"] [technique="roll"]
// [default="off"]/>; without pitch: a key no MuseScore sound plays (listed for reference and checked, never chosen);
// without key (default="off", no pitch): a technique the patch has, off at its defaults, with no key (reference)
static bool readDrum(const QXmlStreamAttributes& aa, DrumKey& d)
      {
      bool ok1 = true, ok2 = true;
      if (aa.hasAttribute("pitch"))
            d.pitch = aa.value("pitch").toInt(&ok1);
      if (aa.hasAttribute("key"))
            d.key = aa.value("key").toInt(&ok2);
      else if (aa.value("default") != "off" || aa.hasAttribute("pitch"))
            return false;
      if (aa.hasAttribute("velocity"))
            d.velocity = aa.value("velocity").toInt();
      d.ids = words(aa.value("ids").toString().toLower());
      d.name = aa.value("name").toString();
      d.technique = aa.value("technique").toString().toLower();
      d.offByDefault = aa.value("default") == "off";
      if (aa.hasAttribute("default") && aa.value("default") != "off")
            return false;
      if (!d.technique.isEmpty() && d.technique != "roll")
            return false;
      return ok1 && ok2 && !(aa.hasAttribute("pitch") && d.pitch < 0) && d.pitch <= 127
             && !(aa.hasAttribute("key") && d.key < 0) && d.key <= 127 && d.velocity <= 127;
      }

static bool readArticulation(const QXmlStreamAttributes& a, LibInstrument& li)
      {
      Articulation art;
      art.name = a.value("name").toString();
      art.techniques = words(a.value("techniques").toString());
      art.modifiers = words(a.value("modifiers").toString());
      art.expect = a.value("expect").toString();
      art.prefer = words(a.value("prefer").toString());
      art.length = a.hasAttribute("length") ? a.value("length").toDouble() : -1;
      art.fromSeconds = a.hasAttribute("from") ? a.value("from").toDouble() : -1;
      art.releaseMs = a.hasAttribute("release") ? a.value("release").toDouble() : -1;
      art.peakMs = a.hasAttribute("peak") ? a.value("peak").toDouble() : -1;
      for (const QString& t : a.value("quickLevel").toString().split(' ', QString::SkipEmptyParts)) {
            const int slash = t.indexOf('/'), colon = t.indexOf(':');
            bool ok1 = false, ok2 = false, ok3 = false;
            const Articulation::QuickLevel l { t.left(slash).toInt(&ok1), t.mid(slash + 1, colon - slash - 1).toDouble(&ok2),
                                                t.mid(colon + 1).toDouble(&ok3) };
            if (slash < 0 || colon < slash || !ok1 || !ok2 || !ok3 || l.ms <= 0)
                  return false;
            art.quickLevels.push_back(l);
            }
      std::sort(art.quickLevels.begin(), art.quickLevels.end(), [](const Articulation::QuickLevel& x, const Articulation::QuickLevel& y) {
            return x.pitch != y.pitch ? x.pitch < y.pitch : x.ms < y.ms;
            });
      // heldOffset="dB" or "cc:dB …" ([levels] calibrated 2, option B')
      for (const QString& t : a.value("heldOffset").toString().split(' ', QString::SkipEmptyParts)) {
            const int colon = t.indexOf(':');
            bool ok1 = true, ok2 = false;
            const int cc = colon < 0 ? -1 : t.left(colon).toInt(&ok1);
            const double db = t.mid(colon + 1).toDouble(&ok2);
            if (!ok1 || !ok2 || (colon < 0 && !art.heldOffsets.empty()) || (colon >= 0 && cc < 0))
                  return false;
            art.heldOffsets.push_back({ cc, db });
            }
      std::sort(art.heldOffsets.begin(), art.heldOffsets.end());
      if (art.heldOffsets.size() > 1 && art.heldOffsets.front().first < 0)
            return false;
      // contextLevel and [slurs] plainPerformance's per kind: "cc/ms/pitch:dB …"
      auto contextGrid = [&a](const char* attribute, std::vector<Articulation::ContextLevel>& out) {
            for (const QString& t : a.value(attribute).toString().split(' ', QString::SkipEmptyParts)) {
                  const QStringList f = t.left(t.indexOf(':')).split('/');
                  bool ok[4] = { false, false, false, false };
                  if (f.size() != 3 || t.indexOf(':') < 0)
                        return false;
                  const Articulation::ContextLevel l { f[0].toInt(&ok[0]), f[1].toDouble(&ok[1]), f[2].toInt(&ok[2]),
                                                       t.mid(t.indexOf(':') + 1).toDouble(&ok[3]) };
                  if (!ok[0] || !ok[1] || !ok[2] || !ok[3] || l.ms <= 0)
                        return false;
                  out.push_back(l);
                  }
            std::sort(out.begin(), out.end(), [](const Articulation::ContextLevel& x, const Articulation::ContextLevel& y) {
                  return x.cc != y.cc ? x.cc < y.cc : x.ms != y.ms ? x.ms < y.ms : x.pitch < y.pitch;
                  });
            return true;
            };
      if (!contextGrid("contextLevel", art.contextLevels) || !contextGrid("contextLevelPlain", art.plainContextLevels[0])
          || !contextGrid("contextLevelStaccato", art.plainContextLevels[1]) || !contextGrid("contextLevelSingle", art.plainContextLevels[2]))
            return false;
      art.legatoVelocity = a.hasAttribute("legatoVelocity") ? a.value("legatoVelocity").toInt() : -1;
      if (a.hasAttribute("legatoVelocity") && (art.legatoVelocity < 1 || art.legatoVelocity > 127))
            return false;
      // name:low-high:velocity …, the bands inside 1-127 and the velocity inside its band
      auto readTechniques = [&](const char* attr, std::vector<Articulation::Technique>& list) {
            if (!a.hasAttribute(attr))
                  return true;
            for (const QString& t : a.value(attr).toString().split(' ', QString::SkipEmptyParts)) {
                  const QStringList f = t.split(':');
                  const QStringList band = f.value(1).split('-');
                  bool ok1 = false, ok2 = false, ok3 = false;
                  Articulation::Technique tq { f.value(0), band.value(0).toInt(&ok1), band.value(1).toInt(&ok2), f.value(2).toInt(&ok3) };
                  if (f.size() != 3 || band.size() != 2 || !ok1 || !ok2 || !ok3 || tq.name.isEmpty() || tq.low < 1 || tq.high > 127
                      || tq.low > tq.high || tq.velocity < tq.low || tq.velocity > tq.high)
                        return false;
                  list.push_back(tq);
                  }
            return true;
            };
      if (!readTechniques("attacks", art.attacks) || !readTechniques("transitions", art.transitions))
            return false;
      // attackOnset="name:median[;pitch:ms,…] …": an attack's own onset (ms >= 0), the attack listed in attacks=
      if (a.hasAttribute("attackOnset")) {
            for (const QString& t : a.value("attackOnset").toString().split(' ', QString::SkipEmptyParts)) {
                  const int colon = t.indexOf(':');
                  const QStringList parts = t.mid(colon + 1).split(';');
                  Articulation::Technique* tq = nullptr;
                  for (Articulation::Technique& x : art.attacks)
                        if (x.name == t.left(colon))
                              tq = &x;
                  bool ok = false;
                  const double median = parts.value(0).toDouble(&ok);
                  if (colon <= 0 || !tq || !ok || median < 0 || parts.size() > 2)
                        return false;
                  double tableMedian = 0;
                  if (parts.size() == 2 && (!parts[1].contains(':')
                                            || !readKeyedMs(QString(parts[1]).replace(',', ' '), tableMedian, tq->onsets)))
                        return false;
                  tq->onsetMs = median;
                  }
            }
      // accentedLevel="pitch/ms:dB …" (as quickLevel), with an accented attack listed in attacks=
      if (a.hasAttribute("accentedLevel")) {
            if (!art.attack("accented"))
                  return false;
            for (const QString& t : a.value("accentedLevel").toString().split(' ', QString::SkipEmptyParts)) {
                  const int slash = t.indexOf('/'), colon = t.indexOf(':');
                  bool ok1 = false, ok2 = false, ok3 = false;
                  const Articulation::QuickLevel l { t.left(slash).toInt(&ok1), t.mid(slash + 1, colon - slash - 1).toDouble(&ok2),
                                                      t.mid(colon + 1).toDouble(&ok3) };
                  if (slash < 0 || colon < slash || !ok1 || !ok2 || !ok3 || l.ms <= 0 || l.pitch < 0 || l.pitch > 127)
                        return false;
                  art.accentedLevels.push_back(l);
                  }
            if (art.accentedLevels.empty())
                  return false;
            std::sort(art.accentedLevels.begin(), art.accentedLevels.end(), [](const Articulation::QuickLevel& x, const Articulation::QuickLevel& y) {
                  return x.pitch != y.pitch ? x.pitch < y.pitch : x.ms < y.ms;
                  });
            }
      // key[-key]:low-high:velocity …, the velocity outside its band
      if (a.hasAttribute("avoid")) {
            for (const QString& t : a.value("avoid").toString().split(' ', QString::SkipEmptyParts)) {
                  const QStringList f = t.split(':');
                  const QStringList keys = f.value(0).split('-');
                  const QStringList band = f.value(1).split('-');
                  bool ok[5] = { false, false, false, false, false };
                  Articulation::Avoid v { keys.value(0).toInt(&ok[0]), keys.value(keys.size() - 1).toInt(&ok[1]),
                                          band.value(0).toInt(&ok[2]), band.value(1).toInt(&ok[3]), f.value(2).toInt(&ok[4]) };
                  if (f.size() != 3 || keys.size() > 2 || band.size() != 2 || !ok[0] || !ok[1] || !ok[2] || !ok[3] || !ok[4]
                      || v.keyLow < 0 || v.keyHigh > 127 || v.keyLow > v.keyHigh || v.low < 1 || v.high > 127 || v.low > v.high
                      || v.velocity < 1 || v.velocity > 127 || (v.velocity >= v.low && v.velocity <= v.high))
                        return false;
                  art.avoid.push_back(v);
                  }
            }
      art.reattackVelocity = a.hasAttribute("reattack") ? a.value("reattack").toInt() : -1;
      if (a.hasAttribute("reattack") && (art.reattackVelocity < 1 || art.reattackVelocity > 127))
            return false;
      art.reattackGapMs = a.hasAttribute("reattackGap") ? a.value("reattackGap").toDouble() : -1;
      art.overlapMs = a.hasAttribute("overlap") ? a.value("overlap").toDouble() : -1;
      art.legatoGapMs = a.hasAttribute("legatoGap") ? a.value("legatoGap").toDouble() : -1;
      if (a.hasAttribute("legatoDelay") && !readKeyedMs(a.value("legatoDelay").toString(), art.legatoDelayMs, art.legatoDelays))
            return false;
      if (a.hasAttribute("onset") && !readKeyedMs(a.value("onset").toString(), art.onsetMs, art.onsets))
            return false;
      if (a.hasAttribute("octaveUp") && !readKeyedMs(a.value("octaveUp").toString(), art.octaveUpMs, art.octaveUp))
            return false;
      // bendGlide="lenBefore:leave:arrive …" (ms; leave may be negative: the f0 already on its way)
      for (const QString& g : a.value("bendGlide").toString().split(' ', QString::SkipEmptyParts)) {
            const QStringList f = g.split(':');
            bool ok[3] = { false, false, false };
            Articulation::BendGlide bg { f.value(0).toDouble(&ok[0]), f.value(1).toDouble(&ok[1]), f.value(2).toDouble(&ok[2]) };
            if (f.size() != 3 || !ok[0] || !ok[1] || !ok[2] || bg.lenBeforeMs < 0 || bg.arriveMs < bg.leaveMs)
                  return false;
            art.bendGlides.push_back(bg);
            }
      std::sort(art.bendGlides.begin(), art.bendGlides.end(), [](const Articulation::BendGlide& x, const Articulation::BendGlide& y) {
            return x.lenBeforeMs < y.lenBeforeMs;
            });
      // bendSmoothing="tau" (ms, the patch's one-pole smoothing of a bend; negative: malformed)
      if (a.hasAttribute("bendSmoothing")) {
            bool ok = false;
            art.bendSmoothingMs = a.value("bendSmoothing").toDouble(&ok);
            if (!ok || art.bendSmoothingMs < 0)
                  return false;
            }
      if (a.hasAttribute("octaveDown") && !readKeyedMs(a.value("octaveDown").toString(), art.octaveDownMs, art.octaveDown))
            return false;
      // (legatoLevel / legatoLevelLong: the legato level balance's, removed 2026-10-07; an older map's are ignored)
      // keyShift="-12": the key sent for its notes (Ample's natural harmonics: the sounding pitch - 12)
      if (a.hasAttribute("keyShift")) {
            bool okk = false;
            art.keyShift = a.value("keyShift").toInt(&okk);
            if (!okk || art.keyShift < -48 || art.keyShift > 48)
                  return false;
            }
      bool ok = false;
      art.value = a.value("value").toInt(&ok);
      if (!art.expect.isEmpty() && art.expect != "silent" && art.expect != "ignored" && art.expect != "unclear")
            return false;
      if (!ok || art.value < 0 || art.value > 127)
            return false;
      li.articulations.push_back(art);
      return true;
      }

// <Guitar stringKeys="36 35 34 33 32 31" harmonic="25" … strumKeys="72 76 79 83" artificial="12:12 7:19" pinch="19 60 2 45" …>
//   <StrumLevel set="AGMSt" levels="20:8 40:16 …"/>  <Fx text="slap" key="90"/>
// </Guitar> (soundlibrary.h Guitar); the reader is on the element and reads it to its end
static bool readGuitar(QXmlStreamReader& r, Guitar& g)
      {
      const QXmlStreamAttributes a = r.attributes();
      bool good = true;
      auto key = [&](const char* name, int& v, int low = 0, int high = 127) {
            if (!a.hasAttribute(name))
                  return;
            bool ok = false;
            v = a.value(name).toInt(&ok);
            good = good && ok && v >= low && v <= high;
            };
      auto number = [&](const char* name, double& v) {
            if (!a.hasAttribute(name))
                  return;
            bool ok = false;
            v = a.value(name).toDouble(&ok);
            good = good && ok && v >= 0.0 && v <= 10000.0;
            };
      auto ints = [&](const char* name, int n) {
            std::vector<int> v;
            for (const QString& t : a.value(name).toString().split(' ', QString::SkipEmptyParts)) {
                  bool ok = false;
                  v.push_back(t.toInt(&ok));
                  good = good && ok && v.back() >= 0 && v.back() <= 127;
                  }
            if (a.hasAttribute(name) && int(v.size()) != n)
                  good = false;
            return v;
            };
      auto pairs = [&](const QString& text) {
            std::vector<std::pair<int, int>> v;
            for (const QString& t : text.split(' ', QString::SkipEmptyParts)) {
                  bool ok1 = false, ok2 = false;
                  v.push_back({ t.section(':', 0, 0).toInt(&ok1), t.section(':', 1).toInt(&ok2) });
                  good = good && ok1 && ok2 && t.count(':') == 1 && (v.size() < 2 || v.back().first > v[v.size() - 2].first);
                  }
            return v;
            };
      const std::vector<int> strings = ints("stringKeys", 6);
      for (size_t i = 0; i < strings.size() && i < 6; ++i)
            g.stringKeys[i] = strings[i];
      const std::vector<int> rolls = ints("rollKeys", 6);
      for (size_t i = 0; i < rolls.size() && i < 6; ++i)
            g.rollKeys[i] = rolls[i];
      const std::vector<int> strums = ints("strumKeys", 4);          // down, up, down muted, up muted
      if (strums.size() == 4) {
            g.strumDown = strums[0];
            g.strumUp = strums[1];
            g.strumDownMuted = strums[2];
            g.strumUpMuted = strums[3];
            }
      key("stringVelocity", g.stringVelocity, 1);
      key("harmonic", g.harmonic);
      key("harmonicShift", g.harmonicShift, -48, 48);
      key("palmMute", g.palmMute);
      key("slide", g.slide);
      key("legatoSlide", g.legatoSlide);
      key("hammer", g.hammer);
      key("strumMode", g.strumMode);
      key("strumOn", g.strumOn, 1);
      key("strumOff", g.strumOff, 1);
      key("detect", g.detect);
      key("detectVelocity", g.detectVelocity, 1);
      key("chordLow", g.chordLow, 0, 104);
      key("strumVelocity", g.strumVelocity, 1);
      key("softStrum", g.softStrum, 0);
      number("rollGap", g.rollGapMs);
      key("capoMax", g.capoMax, 1, 24);
      number("capoLead", g.capoLeadMs);
      number("overlap", g.overlapMs);
      number("slideOut", g.slideOutMs);
      number("fxLength", g.fxMs);
      g.sampleSet = a.value("sampleSet").toString();
      g.capo = a.value("capo").toString();
      g.artificial = pairs(a.value("artificial").toString());
      g.softRoll = pairs(a.value("softRoll").toString());
      for (const auto& p : g.softRoll)
            good = good && p.first >= 1 && p.first <= 127 && p.second >= 1 && p.second <= 200;
      if (a.hasAttribute("pinch")) {                                  // partial, velocity, delay ms, length ms
            const QStringList p = a.value("pinch").toString().split(' ', QString::SkipEmptyParts);
            bool ok[4] = { false, false, false, false };
            if (p.size() == 4) {
                  g.pinchPartial = p[0].toInt(&ok[0]);
                  g.pinchVelocity = p[1].toInt(&ok[1]);
                  g.pinchDelayMs = p[2].toDouble(&ok[2]);
                  g.pinchOffMs = p[3].toDouble(&ok[3]);
                  }
            good = good && ok[0] && ok[1] && ok[2] && ok[3] && g.pinchPartial > 0 && g.pinchPartial <= 48
                   && g.pinchVelocity >= 1 && g.pinchVelocity <= 127 && g.pinchDelayMs >= 0 && g.pinchOffMs > 0;
            }
      while (r.readNextStartElement()) {
            const QXmlStreamAttributes aa = r.attributes();
            if (r.name() == "StrumLevel") {
                  const QString set = aa.value("set").toString();
                  const auto levels = pairs(aa.value("levels").toString());
                  good = good && !set.isEmpty() && !levels.empty();
                  for (const auto& l : levels)
                        good = good && l.first >= 1 && l.first <= 127 && l.second >= 1 && l.second <= 127;
                  g.strumLevels[set] = levels;
                  }
            else if (r.name() == "Fx") {
                  Guitar::Fx f;
                  bool ok = false;
                  f.key = aa.value("key").toInt(&ok);
                  f.text = QRegularExpression("^(?:" + aa.value("text").toString() + ")$", QRegularExpression::CaseInsensitiveOption);
                  good = good && ok && f.key >= 0 && f.key <= 127 && f.text.isValid() && !aa.value("text").isEmpty();
                  g.fx.push_back(f);
                  }
            r.skipCurrentElement();
            }
      return good;
      }

int Guitar::strumLevel(const QString& set, int velocity) const
      {
      auto t = strumLevels.find(set);
      if (t == strumLevels.end() || t->second.empty())
            return 64;
      const auto& v = t->second;
      if (velocity <= v.front().first)
            return v.front().second;
      if (velocity >= v.back().first)
            return v.back().second;
      for (size_t i = 1; i < v.size(); ++i) {
            if (velocity > v[i].first)
                  continue;
            const double f = double(velocity - v[i - 1].first) / (v[i].first - v[i - 1].first);
            const double db = (1.0 - f) * std::log10(v[i - 1].second) + f * std::log10(v[i].second);
            return qBound(1, int(std::lround(std::pow(10.0, db))), 127);
            }
      return v.back().second;
      }

int Guitar::softRollVelocity(int velocity) const
      {
      if (softRoll.empty())
            return qBound(1, velocity, 127);
      return qBound(softRoll.front().first, velocity, softRoll.back().first);
      }

double Guitar::softRollGapMs(int velocity) const
      {
      if (softRoll.empty())
            return rollGapMs;
      const int v = softRollVelocity(velocity);
      for (size_t i = 1; i < softRoll.size(); ++i) {
            if (v > softRoll[i].first)
                  continue;
            const double f = double(v - softRoll[i - 1].first) / (softRoll[i].first - softRoll[i - 1].first);
            return (1.0 - f) * softRoll[i - 1].second + f * softRoll[i].second;
            }
      return softRoll.back().second;
      }

int Guitar::artificialShift(int frets) const
      {
      for (const auto& p : artificial)
            if (p.first == frets)
                  return p.second;
      return 0;
      }

// <Controller id="vibrato" name="Vibrato" cc="21" default="64">
//   <Text match="senza vib\.?" value="0"/>
// </Controller>
// or param="<the plug-in parameter's title>" instead of cc (patchDefault="0-1": its measured own value, Controller::
// patchDefault); the reader is on the element
static bool readController(QXmlStreamReader& r, Controller& c)
      {
      const QXmlStreamAttributes a = r.attributes();
      c.id = a.value("id").toString();
      c.name = a.value("name").toString();
      if (c.name.isEmpty())
            c.name = c.id;
      bool ok = true;
      if (a.hasAttribute("cc"))
            c.cc = a.value("cc").toInt(&ok);
      c.param = a.value("param").toString();
      // hosted="m-master-vol": the parameter controller that plays it when hosted (Controller::hosted; checked at the end)
      c.hosted = a.value("hosted").toString();
      ok = ok && (c.hosted.isEmpty() || c.cc >= 0);
      // state="DBName" values="AGMFg AGMPk AGMSt": a load-time setting of the plug-in's state (Controller::state)
      c.state = a.value("state").toString();
      c.stateValues = a.value("values").toString().split(' ', QString::SkipEmptyParts);
      // range="0 100" decimals="1": a number (Controller::stateRange)
      if (a.hasAttribute("range")) {
            const QStringList r = a.value("range").toString().split(' ', QString::SkipEmptyParts);
            bool ok1 = false, ok2 = false;
            c.stateRange = true;
            c.stateMin = r.size() == 2 ? r[0].toDouble(&ok1) : 0;
            c.stateMax = r.size() == 2 ? r[1].toDouble(&ok2) : 0;
            ok = ok && ok1 && ok2 && c.stateMax > c.stateMin;
            }
      if (a.hasAttribute("decimals")) {
            bool okd = false;
            c.stateDecimals = a.value("decimals").toInt(&okd);
            ok = ok && okd && c.stateDecimals >= 0 && c.stateDecimals <= 6 && c.stateRange;
            }
      if (a.hasAttribute("default")) {
            bool okd = false;
            c.defaultValue = a.value("default").toInt(&okd);
            ok = ok && okd && c.defaultValue >= 0 && c.defaultValue <= 127;
            }
      if (a.hasAttribute("patchDefault")) {
            bool okp = false;
            c.patchDefault = a.value("patchDefault").toDouble(&okp);
            ok = ok && okp && c.patchDefault >= 0 && c.patchDefault <= 1 && (!c.param.isEmpty() || c.isState());
            }
      // steps="N" display="0:Off 1:On" (how the plug-in shows the value: the lane editor's text)
      if (a.hasAttribute("steps")) {
            bool oks = false;
            c.steps = a.value("steps").toInt(&oks);
            ok = ok && oks && c.steps >= 1 && c.steps <= 1000;
            }
      double last = -1;
      for (const QString& t : a.value("display").toString().split(' ', QString::SkipEmptyParts)) {
            const int colon = t.indexOf(':');
            bool okv = false;
            const double v = colon > 0 ? t.left(colon).toDouble(&okv) : -1;
            if (!okv || v < 0 || v > 1 || v <= last)
                  return false;
            last = v;
            c.display.emplace_back(v, t.mid(colon + 1));
            }
      const int kinds = (c.cc >= 0 ? 1 : 0) + (c.param.isEmpty() ? 0 : 1) + (c.isState() ? 1 : 0);
      if (!ok || c.id.isEmpty() || kinds != 1 || c.cc > 119 || c.isState() != (!c.stateValues.isEmpty() || c.stateRange)
          || (c.stateRange && !c.stateValues.isEmpty()))
            return false;
      while (r.readNextStartElement()) {
            if (r.name() == "Text") {
                  const QXmlStreamAttributes aa = r.attributes();
                  ControllerText t;
                  t.match = QRegularExpression("^(?:" + aa.value("match").toString() + ")$", QRegularExpression::CaseInsensitiveOption);
                  bool okv = false;
                  t.value = aa.value("value").toInt(&okv);
                  if (aa.value("match").isEmpty() || !t.match.isValid() || !okv || t.value < 0 || t.value > 127 || c.cc < 0)
                        return false;
                  c.texts.push_back(t);
                  }
            r.skipCurrentElement();
            }
      return true;
      }

//---------------------------------------------------------
//   controllerText
//    a stepped controller: the text of its position nearest the value (the display entry nearest k / steps); a
//    continuous one: numeric texts linear between the two entries around the value, to the decimals of the nearer, else
//    the nearer entry's text
//---------------------------------------------------------

QString Controller::stateText(double value) const
      {
      const double v = std::min(1.0, std::max(0.0, value));
      if (!stateRange) {
            const int n = stateValues.size();
            return n ? stateValues[std::min(n - 1, std::max(0, int(std::lround(v * (n - 1)))))] : QString();
            }
      const double scale = std::pow(10.0, stateDecimals);
      const double x = std::round((stateMin + v * (stateMax - stateMin)) * scale) / scale;
      QString t = QString::number(x, 'f', stateDecimals);
      if (t.contains('.')) {
            while (t.endsWith('0'))
                  t.chop(1);
            if (t.endsWith('.'))
                  t.chop(1);
            }
      return t == "-0" ? QString("0") : t;
      }

int hostedParameter(const LibInstrument& li, int cc)
      {
      for (const Controller& c : li.allControllers) {
            if (c.cc != cc || c.hosted.isEmpty())
                  continue;
            for (int i = 0; i < int(li.allControllers.size()); ++i)
                  if (li.allControllers[size_t(i)].id == c.hosted && !li.allControllers[size_t(i)].param.isEmpty())
                        return i;
            }
      return -1;
      }

QString controllerText(const Controller& controller, double value)
      {
      const auto& d = controller.display;
      if (d.empty())
            return QString();
      double v = std::min(1.0, std::max(0.0, value));
      if (controller.steps > 0)
            v = std::round(v * controller.steps) / controller.steps;
      auto hi = std::lower_bound(d.begin(), d.end(), v, [](const std::pair<double, QString>& p, double x) { return p.first < x; });
      if (hi == d.end())
            return d.back().second;
      if (hi == d.begin() || hi->first == v)
            return hi->second;
      auto lo = std::prev(hi);
      const bool nearLo = v - lo->first <= hi->first - v;
      // (a stepped controller listing every position: the nearest's text; one listing its ends only, a state range's
      // "0:1 1:127", linear between them like a continuous one)
      if (controller.steps > 0 && int(d.size()) > controller.steps)
            return (nearLo ? lo : hi)->second;
      bool ok1 = false, ok2 = false;
      const double y1 = lo->second.toDouble(&ok1);
      const double y2 = hi->second.toDouble(&ok2);
      if (!ok1 || !ok2)
            return (nearLo ? lo : hi)->second;
      const QString& n = (nearLo ? lo : hi)->second;
      const int dot = n.indexOf('.');
      const int decimals = dot < 0 ? 0 : n.size() - dot - 1;
      const double y = y1 + (y2 - y1) * (v - lo->first) / (hi->first - lo->first);
      return QString::number(y, 'f', decimals);
      }

// the library's controllers, then the instrument's own: one of the same id replaces it
static std::vector<Controller> mergeControllers(const std::vector<Controller>& library, const std::vector<Controller>& own)
      {
      std::vector<Controller> all = library;
      for (const Controller& c : own) {
            auto it = std::find_if(all.begin(), all.end(), [&c](const Controller& x) { return x.id == c.id; });
            if (it != all.end())
                  *it = c;
            else
                  all.push_back(c);
            }
      return all;
      }

// setup="$iooxo=3;$name=value;%array=48 52 0": the script values a made setup sets, each value
// exactly as written (the bytes Kontakt saves: an array's elements separated by single spaces)
static std::vector<std::pair<QString, QString>> readSetupValues(const QString& text)
      {
      std::vector<std::pair<QString, QString>> values;
      for (const QString& item : text.split(';', QString::SkipEmptyParts)) {
            const int eq = item.indexOf('=');
            if (eq > 0)
                  values.emplace_back(item.left(eq).trimmed(), item.mid(eq + 1));
            }
      return values;
      }

std::shared_ptr<Library> Library::load(const QString& path, QString* error)
      {
      auto fail = [error](const QString& msg) {
            if (error)
                  *error = msg;
            return std::shared_ptr<Library>();
            };
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly))
            return fail(QString("cannot open %1").arg(path));
      auto lib = std::make_shared<Library>();
      lib->path = path;
      SwitchType defType = SwitchType::CC;
      int defNumber = 32;
      QXmlStreamReader r(&f);
      if (!r.readNextStartElement() || r.name() != "SoundLibrary")
            return fail(QString("%1: not a sound library map").arg(path));
      lib->name = r.attributes().value("name").toString();
      while (r.readNextStartElement()) {
            const QXmlStreamAttributes a = r.attributes();
            if (r.name() == "Switch") {
                  if (!readSwitch(a, defType, defNumber))
                        return fail(QString("%1:%2: bad Switch").arg(path).arg(r.lineNumber()));
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Plugin") {
                  // <Plugin files="Kontakt 8.vst3;Kontakt 7.vst3" [backgroundLoad="60"]/>
                  for (const QString& f : a.value("files").toString().split(';'))
                        if (!f.trimmed().isEmpty())
                              lib->plugins.append(f.trimmed());
                  if (a.hasAttribute("backgroundLoad"))
                        lib->backgroundLoad = std::max(0.0, a.value("backgroundLoad").toDouble());
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Tuning") {
                  // <Tuning method="varispeed" tolerance="3" tail="1.5"/>
                  lib->varispeed = a.value("method") == "varispeed";
                  if (a.hasAttribute("tolerance"))
                        lib->laneTolerance = a.value("tolerance").toDouble();
                  if (a.hasAttribute("tail"))
                        lib->laneTail = a.value("tail").toDouble();
                  if (a.hasAttribute("maxLanes"))
                        lib->maxLanes = std::max(1, a.value("maxLanes").toInt());
                  // (left out: computed, Library::laneTolerance / laneTail / maxLanes)
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Legato") {
                  // (<Legato early>: a legato transition's early start, removed 2026-10-07, unused; an older
                  // map's is ignored)
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Onset") {
                  // <Onset early="100"/>
                  lib->onsetEarly = qBound(0, a.value("early").toInt(), 200);
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Mics") {
                  // <Mics fader="16:-30.27 ... 127:0" mix="0:-/-/0 ... 127:0/-/-"/> ([levels] micCompensation)
                  for (const QString& t : a.value("fader").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok1 = false, ok2 = false;
                        const int v = t.section(':', 0, 0).toInt(&ok1);
                        const double db = t.section(':', 1).toDouble(&ok2);
                        if (!ok1 || !ok2 || v < 1 || v > 127 || db > 0.0)
                              return fail(QString("%1:%2: bad Mics fader \"%3\"").arg(path).arg(r.lineNumber()).arg(t));
                        lib->micFader[v] = db;
                        }
                  for (const QString& t : a.value("mix").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok = false;
                        const int v = t.section(':', 0, 0).toInt(&ok);
                        const QStringList dbs = t.section(':', 1).split('/');
                        if (!ok || v < 0 || v > 127 || dbs.size() != 3)
                              return fail(QString("%1:%2: bad Mics mix \"%3\"").arg(path).arg(r.lineNumber()).arg(t));
                        std::array<double, 3> m;
                        for (int i = 0; i < 3; ++i) {
                              bool okd = false;
                              m[size_t(i)] = dbs[i] == "-" ? -INFINITY : dbs[i].toDouble(&okd);
                              if (dbs[i] != "-" && !okd)
                                    return fail(QString("%1:%2: bad Mics mix \"%3\"").arg(path).arg(r.lineNumber()).arg(t));
                              }
                        lib->micMix[v] = m;
                        }
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Dynamics") {
                  if (a.hasAttribute("cc"))
                        lib->dynamicsCC = a.value("cc").toInt();
                  if (a.hasAttribute("expression"))
                        lib->expressionValue = a.value("expression").toInt();
                  if (a.hasAttribute("velocity"))
                        lib->velocityDynamics = a.value("velocity").toString().split(' ', QString::SkipEmptyParts);
                  if (a.hasAttribute("crest"))
                        lib->crestDb = a.value("crest").toDouble();
                  // (heard=: the short notes' balance's references, removed 2026-10-07; ignored)
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Controller") {
                  Controller c;
                  if (!readController(r, c))
                        return fail(QString("%1:%2: bad Controller").arg(path).arg(r.lineNumber()));
                  lib->controllers.push_back(c);
                  }
            else if (r.name() == "State") {
                  // <State name="Control[ID=12]@Parameter" value="M-Master Vol"/> (Library::fixedState)
                  if (a.value("name").isEmpty() || !a.hasAttribute("value"))
                        return fail(QString("%1:%2: bad State").arg(path).arg(r.lineNumber()));
                  lib->fixedState[a.value("name").toString()] = a.value("value").toString().toUtf8();
                  r.skipCurrentElement();
                  }
            else if (r.name() == "PluginSettings") {
                  // <PluginSettings file="…" name="…" bad="…" text="…"/> (Library::settingsChecks)
                  // (folder="local", format="nka", good="…": SSO's shared settings)
                  Library::SettingsCheck check { a.value("file").toString(), a.value("name").toString(),
                                                 a.value("bad").toString().split(' ', QString::SkipEmptyParts),
                                                 a.value("text").toString(), a.value("folder").toString(),
                                                 a.value("format").toString(),
                                                 a.value("good").toString().split(' ', QString::SkipEmptyParts) };
                  if (check.file.isEmpty() || check.name.isEmpty() || (check.bad.isEmpty() && check.good.isEmpty())
                      || check.text.isEmpty() || !(check.folder.isEmpty() || check.folder == "local")
                      || !(check.format.isEmpty() || check.format == "nka"))
                        return fail(QString("%1:%2: bad PluginSettings").arg(path).arg(r.lineNumber()));
                  lib->settingsChecks.push_back(check);
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Files") {
                  // <Files registry="Spitfire Symphony Orchestra"/>: where the library is installed
                  lib->registryName = a.value("registry").toString();
                  r.skipCurrentElement();
                  }
            else if (r.name() == "Patch") {
                  // <Patch name="Violins 1 - Core techniques" nki="Instruments/…/….nki" setup="$iooxo=3" scan="values"/>
                  LibInstrument li;
                  li.name = a.value("name").toString();
                  li.nki = a.value("nki").toString();
                  li.setupValues = readSetupValues(a.value("setup").toString());
                  li.scan = a.value("scan").toString();
                  li.testPitch = a.hasAttribute("pitch") ? a.value("pitch").toInt() : -1;
                  li.keyScan = li.scan == "keys";
                  li.switchType = defType;
                  li.switchNumber = defNumber;
                  if (li.name.isEmpty() || li.nki.isEmpty() || !(li.scan.isEmpty() || li.scan == "values" || li.scan == "keys"))
                        return fail(QString("%1:%2: bad Patch").arg(path).arg(r.lineNumber()));
                  // its articulations or drum keys, when known (from a scan or its window): listed for reference,
                  // never chosen; its own switch (a keyswitch patch: Harp glissandi)
                  while (r.readNextStartElement()) {
                        if (r.name() == "Articulation" && !readArticulation(r.attributes(), li))
                              return fail(QString("%1:%2: bad Articulation").arg(path).arg(r.lineNumber()));
                        if (r.name() == "Drum") {
                              DrumKey d;
                              if (!readDrum(r.attributes(), d) || d.pitch >= 0)
                                    return fail(QString("%1:%2: bad Drum").arg(path).arg(r.lineNumber()));
                              li.drums.push_back(d);
                              }
                        if (r.name() == "Switch" && !readSwitch(r.attributes(), li.switchType, li.switchNumber))
                              return fail(QString("%1:%2: bad Switch").arg(path).arg(r.lineNumber()));
                        r.skipCurrentElement();
                        }
                  lib->otherPatches.push_back(li);
                  }
            else if (r.name() == "Instrument") {
                  LibInstrument li;
                  li.name = a.value("name").toString();
                  li.nki = a.value("nki").toString();
                  li.testPitch = a.hasAttribute("pitch") ? a.value("pitch").toInt() : -1;
                  li.setupValues = readSetupValues(a.value("setup").toString());
                  li.ids = words(a.value("ids").toString().toLower());
                  li.with = a.value("with").toString();
                  li.kit = a.value("kit").toString() == "1";
                  li.keyScan = a.value("keyScan").toString() == "1";
                  li.bendCents = std::max(0.0, a.value("bend").toDouble());
                  li.mono = a.value("mono").toString() == "1";
                  if (a.hasAttribute("keyShift"))
                        li.keyShift = qBound(-48, a.value("keyShift").toInt(), 48);
                  if (a.hasAttribute("maxVelocity"))
                        li.maxVelocity = qBound(1, a.value("maxVelocity").toInt(), 127);
                  if (a.hasAttribute("plainShift")) {
                        bool ok = false;
                        const double v = a.value("plainShift").toDouble(&ok);
                        if (ok && std::isfinite(v))
                              li.plainShiftMs = std::max(0.0, std::min(1000.0, v));   // (TrackDelays' range)
                        }
                  if (a.hasAttribute("plainAttack")) {
                        bool ok = false;
                        const double v = a.value("plainAttack").toDouble(&ok);
                        if (ok && std::isfinite(v))
                              li.plainAttackMs = std::max(0.0, std::min(1000.0, v));
                        }
                  li.transportResetScript = a.value("transportResetScript").toString();
                  li.hallDrop = qBound(0, a.value("hallDrop").toInt(), 127);
                  for (const QString& c : a.value("avoidCC").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok = false;
                        const int n = c.toInt(&ok);
                        if (!ok || n < 0 || n > 127)
                              return fail(QString("%1:%2: bad avoidCC \"%3\"").arg(path).arg(r.lineNumber()).arg(c));
                        li.avoidCC.insert(n);
                        }
                  if (a.hasAttribute("speedCC") && a.hasAttribute("speedScript")) {
                        li.speedCC = qBound(0, a.value("speedCC").toInt(), 127);
                        li.speedScript = a.value("speedScript").toString();
                        if (li.speedScript.isEmpty())
                              li.speedCC = 0;
                        }
                  li.sectionLevelDb = a.value("sectionLevel").toDouble();
                  li.hasSectionLevel2 = a.hasAttribute("sectionLevel2");
                  li.sectionLevel2Db = a.value("sectionLevel2").toDouble();
                  // realSpan="pitch:dB ...", longLevel="cc/pitch:dB ..." ([levels] realRange)
                  for (const QString& t : a.value("realSpan").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok1 = false, ok2 = false;
                        const int p = t.section(':', 0, 0).toInt(&ok1);
                        const double db = t.section(':', 1).toDouble(&ok2);
                        if (!ok1 || !ok2)
                              return fail(QString("%1:%2: bad realSpan \"%3\"").arg(path).arg(r.lineNumber()).arg(t));
                        li.realSpan[p] = db;
                        }
                  for (const QString& t : a.value("longLevel").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok1 = false, ok2 = false, ok3 = false;
                        const QString key = t.section(':', 0, 0);
                        const int cc = key.section('/', 0, 0).toInt(&ok1);
                        const int p = key.section('/', 1).toInt(&ok2);
                        const double db = t.section(':', 1).toDouble(&ok3);
                        if (!ok1 || !ok2 || !ok3 || !key.contains('/'))
                              return fail(QString("%1:%2: bad longLevel \"%3\"").arg(path).arg(r.lineNumber()).arg(t));
                        li.longLevel[p][cc] = db;
                        }
                  if (a.hasAttribute("realPlay")) {
                        const QStringList ab = a.value("realPlay").toString().split(' ', QString::SkipEmptyParts);
                        bool ok1 = false, ok2 = false;
                        const double pp = ab.size() == 2 ? ab[0].toDouble(&ok1) : 0.0;
                        const double ff = ab.size() == 2 ? ab[1].toDouble(&ok2) : 0.0;
                        if (!ok1 || !ok2 || !(pp >= 0.0 && pp < ff && ff <= 1.0))
                              return fail(QString("%1:%2: bad realPlay \"%3\"").arg(path).arg(r.lineNumber()).arg(a.value("realPlay").toString()));
                        li.realPlayPp = pp;
                        li.realPlayFf = ff;
                        }
                  if (a.hasAttribute("realSteps")) {
                        bool ok = false;
                        li.realStepsDb = a.value("realSteps").toDouble(&ok);
                        if (!ok || !(li.realStepsDb > 0.0))
                              return fail(QString("%1:%2: bad realSteps \"%3\"").arg(path).arg(r.lineNumber()).arg(a.value("realSteps").toString()));
                        }
                  // micLevels="Close:dB Tree:dB ...", micDefault="Tree:127" ([levels] micCompensation)
                  static const QStringList micNames { "Close", "Tree", "Ambient", "Outrigger", "Leader" };
                  for (const QString& t : a.value("micLevels").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok = false;
                        const int i = micNames.indexOf(t.section(':', 0, 0));
                        const double db = t.section(':', 1).toDouble(&ok);
                        if (i < 0 || !ok)
                              return fail(QString("%1:%2: bad micLevels \"%3\"").arg(path).arg(r.lineNumber()).arg(t));
                        li.micLevelDb[size_t(i)] = db;
                        li.hasMicLevels = true;
                        }
                  for (const QString& t : a.value("micDefault").toString().split(' ', QString::SkipEmptyParts)) {
                        bool ok = false;
                        const int i = micNames.indexOf(t.section(':', 0, 0));
                        const int v = t.section(':', 1).toInt(&ok);
                        if (i < 0 || !ok || v < 0 || v > 127)
                              return fail(QString("%1:%2: bad micDefault \"%3\"").arg(path).arg(r.lineNumber()).arg(t));
                        li.micDefault[size_t(i)] = v;
                        }
                  if (a.hasAttribute("partName"))
                        li.partName = QRegularExpression(a.value("partName").toString(), QRegularExpression::CaseInsensitiveOption);
                  li.switchType = defType;
                  li.switchNumber = defNumber;
                  while (r.readNextStartElement()) {
                        const QXmlStreamAttributes aa = r.attributes();
                        if (r.name() == "Switch") {
                              if (!readSwitch(aa, li.switchType, li.switchNumber))
                                    return fail(QString("%1:%2: bad Switch").arg(path).arg(r.lineNumber()));
                              }
                        else if (r.name() == "Drum") {
                              DrumKey d;
                              if (!readDrum(aa, d))
                                    return fail(QString("%1:%2: bad Drum").arg(path).arg(r.lineNumber()));
                              li.drums.push_back(d);
                              }
                        else if (r.name() == "Articulation") {
                              if (!readArticulation(aa, li))
                                    return fail(QString("%1:%2: bad Articulation").arg(path).arg(r.lineNumber()));
                              }
                        else if (r.name() == "Controller") {
                              Controller c;
                              if (!readController(r, c))
                                    return fail(QString("%1:%2: bad Controller").arg(path).arg(r.lineNumber()));
                              li.controllers.push_back(c);
                              continue;         // (readController read the element to its end)
                              }
                        else if (r.name() == "Guitar") {
                              auto g = std::make_shared<Guitar>();
                              if (!readGuitar(r, *g))
                                    return fail(QString("%1:%2: bad Guitar").arg(path).arg(r.lineNumber()));
                              li.guitar = g;
                              continue;         // (readGuitar read the element to its end)
                              }
                        r.skipCurrentElement();
                        }
                  // (an extra patch has no ids, and may hold only articulations listed for reference,
                  // or only drum keys, or, a percussion patch not scanned yet, nothing; a kit only ids)
                  const bool playable = std::any_of(li.articulations.begin(), li.articulations.end(),
                                                    [](const Articulation& a) { return !a.techniques.isEmpty(); });
                  if (li.extra() ? (li.articulations.empty() && li.drums.empty() && !li.keyScan)
                                 : (li.ids.isEmpty() || (!li.kit && !playable)))
                        return fail(QString("%1: instrument \"%2\" without ids or articulations").arg(path, li.name));
                  lib->instruments.push_back(li);
                  }
            else
                  r.skipCurrentElement();
            }
      if (r.hasError())
            return fail(QString("%1:%2: %3").arg(path).arg(r.lineNumber()).arg(r.errorString()));
      if (lib->name.isEmpty())
            lib->name = QFileInfo(path).completeBaseName();
      // (a <Controller> of the library may come after the instruments)
      for (LibInstrument& li : lib->instruments) {
            li.allControllers = mergeControllers(lib->controllers, li.controllers);
            for (const Controller& c : li.allControllers)
                  if (!c.hosted.isEmpty() && hostedParameter(li, c.cc) < 0)
                        return fail(QString("%1: \"%2\": controller \"%3\" is hosted by \"%4\", not a parameter controller of the patch")
                                    .arg(path, li.name, c.id, c.hosted));
            }
      // extra patches to their main patch (the vector is complete: the pointers stay valid);
      // they take its instrument ids (the articulation check's test pitch; match() skips them)
      for (LibInstrument& li : lib->instruments) {
            if (!li.extra())
                  continue;
            auto main = std::find_if(lib->instruments.begin(), lib->instruments.end(),
                                     [&li](const LibInstrument& m) { return m.name == li.with && !m.extra(); });
            if (main == lib->instruments.end())
                  return fail(QString("%1: \"%2\" is with \"%3\", which the map lacks").arg(path, li.name, li.with));
            if (li.ids.isEmpty())
                  li.ids = main->ids;
            if (li.sectionLevelDb == 0.0)
                  li.sectionLevelDb = main->sectionLevelDb;
            if (!li.hasSectionLevel2) {
                  li.hasSectionLevel2 = main->hasSectionLevel2;
                  li.sectionLevel2Db = main->sectionLevel2Db;
                  }
            if (li.realSpan.empty() && li.longLevel.empty()) {
                  li.realSpan = main->realSpan;
                  li.longLevel = main->longLevel;
                  li.realPlayPp = main->realPlayPp;
                  li.realPlayFf = main->realPlayFf;
                  li.realStepsDb = main->realStepsDb;
                  }
            if (!li.hasMicLevels) {
                  li.hasMicLevels = main->hasMicLevels;
                  li.micLevelDb = main->micLevelDb;
                  li.micDefault = main->micDefault;
                  }
            main->extras.push_back(&li);
            }
      return lib;
      }

//---------------------------------------------------------
//   Library::match
//    the library instrument for a MuseScore instrument: by its id (instruments.xml, else its
//    MusicXML sound id); of several, the one whose partName fits the part's name, else the
//    first without a partName
//---------------------------------------------------------

const LibInstrument* Library::match(const Instrument* instrument, const Part* part) const
      {
      const QString id = instrument->getId().toLower();
      const QString soundId = instrument->instrumentId().toLower();
      if (id.isEmpty() && soundId.isEmpty())
            return nullptr;
      const QString names = part ? part->partName() + " " + part->longName() : QString();
      const LibInstrument* plain = nullptr;
      const LibInstrument* first = nullptr;
      for (const LibInstrument& li : instruments) {
            if (li.extra())
                  continue;
            if (!(!id.isEmpty() && li.ids.contains(id)) && !(!soundId.isEmpty() && li.ids.contains(soundId)))
                  continue;
            if (!first)
                  first = &li;
            if (li.partName.pattern().isEmpty()) {
                  if (!plain)
                        plain = &li;
                  }
            else if (li.partName.match(names).hasMatch())
                  return &li;
            }
      return plain ? plain : first;
      }

//---------------------------------------------------------
//   choose
//---------------------------------------------------------

bool Choice::sampledOrnament() const
      {
      return base == "tremolo" || base.startsWith("trill");
      }

std::vector<const LibInstrument*> LibInstrument::patches() const
      {
      std::vector<const LibInstrument*> p { this };
      p.insert(p.end(), extras.begin(), extras.end());
      return p;
      }

bool checkedAsExpected(const LibInstrument& instrument, const QString& line, QString* newLine)
      {
      *newLine = line;
      if (!instrument.drums.empty()) {
            static const QRegularExpression keysRe("^\\d+ keys sound \\(([0-9, -]*)\\)");
            const QRegularExpressionMatch m = keysRe.match(line);
            if (!m.hasMatch())
                  return false;
            std::set<int> sounding;
            for (const QString& range : m.captured(1).split(", ", QString::SkipEmptyParts)) {
                  const QStringList ends = range.split('-');
                  for (int k = ends[0].toInt(); k <= ends.last().toInt(); ++k)
                        sounding.insert(k);
                  }
            for (const DrumKey& d : instrument.drums)
                  if (d.key >= 0 && !d.offByDefault && !sounding.count(d.key))    // (what is off doesn't sound)
                        return false;
            newLine->remove("; the map has no keys for it yet");
            return true;
            }
      static const QRegularExpression countsRe("^(\\d+) switch, (\\d+) ignored, (\\d+) unclear, (\\d+) silent"
                                               "(; scan: \\d+ not in the map, 0 map values missing)?$");
      const QRegularExpressionMatch m = countsRe.match(line);
      if (!m.hasMatch())
            return false;
      int ignored = 0, unclear = 0, silent = 0;
      std::set<int> values;
      for (const Articulation& a : instrument.articulations) {
            if (a.expect.isEmpty() || !values.insert(a.value).second)
                  continue;
            ignored += a.expect == "ignored";
            unclear += a.expect == "unclear";
            silent += a.expect == "silent";
            }
      if (values.empty() || m.captured(2).toInt() != ignored || m.captured(3).toInt() != unclear
          || m.captured(4).toInt() != silent)
            return false;
      *newLine += " (as expected)";
      return true;
      }

bool drumRoll(const Chord* chord)
      {
      const Tremolo* t = chord->tremolo();
      return t && (!t->twoNotes() || t->tremoloType() == TremoloType::BUZZ_ROLL);
      }

DrumChoice drum(const std::vector<const LibInstrument*>& patches, int pitch, const QString& instrumentId,
                const QString& technique)
      {
      // an entry for the instrument before one for all
      for (int pass = 0; pass < 2; ++pass) {
            for (int p = 0; p < int(patches.size()); ++p) {
                  for (const DrumKey& d : patches[p]->drums) {
                        if (d.pitch != pitch || d.technique != technique || (pass == 0) == d.ids.isEmpty())
                              continue;
                        if (pass == 1 || d.ids.contains(instrumentId.toLower()))
                              return DrumChoice { p, &d };
                        }
                  }
            }
      return DrumChoice();
      }

double noteSeconds(const Note* note)
      {
      const Note* first = note->firstTiedNote();
      const int t0 = first->chord()->tick().ticks();
      return note->score()->masterScore()->tempomap()->writtenTime(t0, t0 + note->playTicks());
      }

Choice choose(const LibInstrument& instrument, const Want& want)
      {
      return choose(std::vector<const LibInstrument*> { &instrument }, want);
      }

static Choice chooseBases(const std::vector<const LibInstrument*>& patches, const Want& want)
      {
      for (const QString& base : want.bases) {
            const Articulation* best = nullptr;
            int bestPatch = 0;
            int bestCount = -1;
            for (int p = 0; p < int(patches.size()); ++p) {
                  for (const Articulation& a : patches[p]->articulations) {
                        if (!a.techniques.contains(base))
                              continue;
                        // a short that lasts longer than the note (Short 0'5 for a fast eighth): not this one
                        // (measured: from where it sounds closer to the note's meant length than the next choice.
                        // Without a measured from= a short isn't skipped: the earlier rule, its sample's nominal
                        // length= against 90 % of the written one, had no source; every SSO short with a length= has
                        // a from= (numbers-measured, 2026-10-03))
                        // (playback.ini [shorts.from] by patch|articulation: an offset or its own number)
                        const double from = a.fromSeconds > 0
                              ? Playback::adjust("shorts.from", patches[p]->name, a.name, 0, a.fromSeconds) : -1;
                        const double meant = want.byMeantLength ? want.soundSeconds : want.seconds;
                        if (from > 0 && meant > 0 && meant < from)
                              continue;
                        bool fits = true;
                        for (const QString& m : a.modifiers)
                              fits &= want.modifiers.contains(m);
                        // of equal fits in different patches, one that prefers the base (a held note
                        // on the Performance legato patch, with the slurred ones), else the one made
                        // for it (listed first: a Staccatissimo patch over a staccato that also plays it)
                        const bool prefers = a.prefer.contains(base);
                        const bool bestPrefers = best && best->prefer.contains(base);
                        const bool better = a.modifiers.size() > bestCount
                           || (a.modifiers.size() == bestCount && p != bestPatch
                               && (prefers != bestPrefers ? prefers
                                   : a.techniques.indexOf(base) < best->techniques.indexOf(base)));
                        if (fits && better) {
                              best = &a;
                              bestPatch = p;
                              bestCount = a.modifiers.size();
                              }
                        }
                  }
            if (best)
                  return Choice { best, base, bestPatch };
            }
      return Choice();
      }

// a slurred note shorter than its held articulation's measured peak never gets past the swell (SSO's violin Long
// peaks after ~1.06 s: no attack is heard on 136 ms slurred notes; the owner, 2026-10-08). [slurs] quick plays it on
// a quicker technique of the same patches with the note's modifiers: 1 the "tenuto" short (Short 1.0; its from= is
// not applied: the slur's note-off ends it), 2 the "espressivo" long (Long (Rachm.)). Without one: the held choice.
static Choice chooseSlurred(const std::vector<const LibInstrument*>& patches, const Want& want, const Choice& held)
      {
      if (want.slurQuick < 1 || want.slurQuick > 2 || !held || held.articulation->peakMs <= 0 || want.seconds <= 0
          || want.seconds * 1000 >= held.articulation->peakMs)
            return held;
      QStringList modifiers = want.modifiers;
      if (want.slurQuick == 2)
            modifiers.append("espressivo");
      const QString base = want.slurQuick == 1 ? "tenuto" : "long";
      const QString needed = want.slurQuick == 1 ? QString() : QString("espressivo");
      for (int p = 0; p < int(patches.size()); ++p) {
            for (const Articulation& a : patches[p]->articulations) {
                  if (!a.techniques.contains(base) || (!needed.isEmpty() && !a.modifiers.contains(needed)))
                        continue;
                  bool fits = true;
                  for (const QString& m : a.modifiers)
                        fits &= modifiers.contains(m);
                  if (fits && a.modifiers.size() == modifiers.size()) {
                        Choice c { &a, base, p };
                        c.swapped = true;
                        return c;
                        }
                  }
            }
      return held;
      }

// a slurred note with slurs/performance (Want::slurPerformance): as under staff text "performance" when the note
// would otherwise play a plain technique (no modifier: a muted or sul pont. slur keeps its own) and "performance" then
// gives a legato articulation with measured attacks (<Articulation attacks>: SSO's five string sections' Performance
// legato; the solo strings' and the winds' have none and keep [slurs] quick). No swap: the Performance legato is the
// quick technique (the owner, 2026-10-10)
static Choice choosePerformance(const std::vector<const LibInstrument*>& patches, const Want& want, const Choice& held)
      {
      if (!want.slurPerformance || !held || !held.articulation->modifiers.isEmpty() || want.modifiers.contains("performance"))
            return Choice();
      Want w = want;
      w.modifiers.append("performance");
      const Choice c = chooseBases(patches, w);
      if (!c || c.base != "legato" || !c.articulation->modifiers.contains("performance") || c.articulation->attacks.empty())
            return Choice();
      return c;
      }

//---------------------------------------------------------
//   speedFromWritten
//    [slurs] speedFromWritten (the owner approved, 2026-10-17; private design: the Performance script's "Leg. Speed CC"):
//    SSO's Performance script writes CC119 at each legato note-on from the spacing it measures (ms since the voice's
//    previous note-on), and the string sections' transition groups pick their speed variant by it (RUNS 114-127, RUNS RT
//    95-113, FAST LEGATO 0-94 on 1-3 semitone steps). Notes started early by their onset or modelled arrival moved
//    that spacing and so the band; with the setting the script's own writing is off (speedScript = 0 in the patch's
//    state) and the renderer sends the controller from the written spacing, by the same formula
//---------------------------------------------------------

bool speedFromWritten(const LibInstrument* patch, const Score* score)
      {
      return patch && patch->speedCC > 0 && !patch->speedScript.isEmpty() && Playback::on("slurs/speedFromWritten", score);
      }

int speedValue(double ms)
      {
      const long whole = std::max(0L, std::lround(ms));
      return 126 - int(std::min(126L, whole * 127 / 1000));
      }

//---------------------------------------------------------
//   [slurs] fixedTransitions (2026-10-20, branch sso-legato-fixes; 0 by default and in both presets until the owner has
//   heard it): on the patches with a speed script (SSO's five string sections' Performance patches, the ones measured:
//   each has 100-130 modulators from Kontakt's random generator, 90 of them on its PORTAMENTO FAST / RUNS / RUNS RT
//   groups' envelope level, read from the patches' group data) those modulators' intensity is 0 in the state loaded, so
//   every play of a passage sounds the same (Windows test VM, kthost: two renders of Whence null at -71..-97 dB, -7..-21 dB
//   as shipped; per note as loud as the shipped takes' mean, median within 0.15 dB; RESULT of sso-legato-fixes). The
//   script has no setting for it (its "random detune" switch acts on LFO_RAND modulators, which these patches don't
//   have, and is read back from the groups at load). Carried as the pseudo script value "@randomSources" = the intensity
//   ("0"): SoundLibraryHost::setupStateWith sets it by KontaktSetup::withRandomSources, never in the setup file (resave
//   puts the made intensities back)
//---------------------------------------------------------

//---------------------------------------------------------
//   [notes] transportReset (branch sso-script-fixes2, 2026-10-21; 0 by default and in both presets until the owner has
//   decided): a patch with transportResetScript (SSO's "Reset on transport", saved off in the string sections' Performance
//   patches and Tuba Solo - Performance: read from the patch's script) loads with it at 1, so its round robins start from
//   the first take at every playback / export / verification pass, as the library's other patches do. Without it they
//   reset only after 1.5 s without a note-on (read from the patch's script): a play started sooner, or a pass rendered
//   after another, picks other takes. It changes which take plays: a setting
//---------------------------------------------------------

static bool transportReset(const LibInstrument* patch, const Score* score)
      {
      return patch && !patch->transportResetScript.isEmpty() && Playback::on("notes/transportReset", score);
      }

std::vector<QString> playbackScriptKeys(const LibInstrument& patch)
      {
      std::vector<QString> keys;
      if (patch.speedCC > 0 && !patch.speedScript.isEmpty())
            keys.push_back(patch.speedScript);
      if (!patch.transportResetScript.isEmpty())
            keys.push_back(patch.transportResetScript);
      return keys;
      }

bool Library::SettingsCheck::isBad(const QString& value) const
      {
      if (bad.contains(value, Qt::CaseInsensitive))
            return true;
      return !good.isEmpty() && !good.contains(value, Qt::CaseInsensitive);
      }

static bool fixedTransitions(const LibInstrument* patch, const Score* score)
      {
      return patch && !patch->speedScript.isEmpty() && Playback::on("slurs/fixedTransitions", score);
      }

std::map<QString, QByteArray> playbackScriptValues(const LibInstrument& patch, const Score* score)
      {
      std::map<QString, QByteArray> out;
      if (speedFromWritten(&patch, score))
            out[patch.speedScript] = "0";
      if (fixedTransitions(&patch, score))
            out["@randomSources"] = "0";
      if (transportReset(&patch, score))
            out[patch.transportResetScript] = "1";
      return out;
      }

const Articulation* plainPerformanceArticulation(const LibInstrument& patch)
      {
      for (const Articulation& a : patch.articulations)
            if (a.playsTransitions() && a.modifiers == QStringList { "performance" } && !a.attacks.empty())
                  return &a;
      return nullptr;
      }

std::map<const Part*, std::map<QString, double>> plainPerformancePatches(const Score* score, const Library& library)
      {
      std::map<const Part*, std::map<QString, double>> out;
      if (!score || !Playback::on("slurs/plainPerformance", score))
            return out;
      for (const Part* part : score->masterScore()->parts()) {
            for (const auto& ip : *part->instruments()) {
                  const LibInstrument* li = library.match(ip.second, part);
                  if (!li)
                        continue;
                  for (const LibInstrument* p : li->patches())
                        if (plainPerformanceArticulation(*p))
                              out[part][p->name] = p->plainShiftMs;
                  }
            }
      return out;
      }

// [slurs] plainPerformance (the owner's "C", 2026-10-17, by ear on Whence's Violas bars 8-13, against Recommended): every
// note of the part on its Performance legato, as SSO's patch plays a plain MIDI part (abl.py's PLAIN on branch qt-bends,
// tools/soundlibraries/qt_bends/abl.py). A technique the Performance legato doesn't play (pizzicato, col legno, tremolo,
// trills, falls, harmonics, mutes, sul pont. / tasto, espressivo, ... : a base outside BOWED or any modifier) keeps the
// choice made otherwise. Under staff text "performance" the note is already there (held): plain too
static Choice choosePlain(const std::vector<const LibInstrument*>& patches, const Want& want, const Choice& held)
      {
      static const QStringList BOWED = { "long", "legato", "longmarcato", "marcato", "tenuto", "short", "spiccato", "staccatissimo" };
      if (!want.plainPerformance || !held)
            return Choice();
      // (a modifier asked for: the note keeps its technique; with [slurs] plainTiming only a modifier the choice kept, its
      // articulation's own, below: one the patches can't play (SSO's Basses have no con sord. or espressivo sample, their
      // choice the plain Long) stays on the plain Performance legato with the rest of the line)
      if (!want.plainTiming)
            for (const QString& m : want.modifiers)
                  if (m != "performance")
                        return Choice();
      for (int p = 0; p < int(patches.size()); ++p) {
            const Articulation* a = plainPerformanceArticulation(*patches[size_t(p)]);
            if (!a)
                  continue;
            if (held.articulation == a) {
                  Choice c = held;
                  c.plain = true;
                  return c;
                  }
            if (!BOWED.contains(held.base) || !held.articulation->modifiers.isEmpty())
                  return Choice();
            // (its base among the articulation's: "legato" for a slurred note, a slur's transitions; else "long", so that
            // no short's velocity dynamics apply: the Performance legato's level is CC1's)
            Choice c { a, held.base == "legato" ? QString("legato") : QString("long"), p };
            c.plain = true;
            c.plainFrom = held.articulation;
            c.plainFromPatch = held.patch;
            return c;
            }
      return Choice();
      }

Choice choose(const std::vector<const LibInstrument*>& patches, const Want& want)
      {
      const Choice held = chooseBases(patches, want);
      const Choice plain = choosePlain(patches, want, held);
      if (plain)
            return plain;
      const Choice performance = choosePerformance(patches, want, held);
      return performance ? performance : chooseSlurred(patches, want, held);
      }

QString ownTechniqueKey(const LibInstrument& patch, const Articulation& articulation)
      {
      return patch.name + "|" + articulation.name;
      }

Choice chooseOwn(const std::vector<const LibInstrument*>& patches, const QString& key)
      {
      if (key.isEmpty())
            return Choice();
      const int bar = key.indexOf('|');
      const QString patch = key.left(bar);
      const QString name = key.mid(bar + 1);
      for (int p = 0; p < int(patches.size()); ++p) {
            if (patches[size_t(p)]->name != patch)
                  continue;
            for (const Articulation& a : patches[size_t(p)]->articulations)
                  if (a.name == name)
                        return Choice { &a, a.techniques.value(0), p };
            }
      return Choice();
      }

Choice chooseNote(const std::vector<const LibInstrument*>& patches, const Want& want, const Note* note)
      {
      Choice own = note ? chooseOwn(patches, note->firstTiedNote()->libraryTechnique()) : Choice();
      // (its own technique the plain Performance legato: plain as the others, its patch's events going early together)
      if (own && want.plainPerformance && own.articulation == plainPerformanceArticulation(*patches[size_t(own.patch)]))
            own.plain = true;
      return own ? own : choose(patches, want);
      }

//---------------------------------------------------------
//   current
//---------------------------------------------------------

static std::mutex currentMutex;
static std::shared_ptr<const Library> currentLibrary;
static std::atomic<bool> currentActive { false };
static std::atomic<Output> currentOutput { Output::MIDI };

void setOutput(Output output)
      {
      currentOutput = output;
      }

Output output()
      {
      return currentOutput;
      }

void setCurrent(std::shared_ptr<const Library> library)
      {
      {
            std::lock_guard<std::mutex> lock(currentMutex);
            currentLibrary = library;
            currentActive = bool(library);
      }
      // a library set is the one the parts play by default; the application then says whether
      // the global playback mode is the library (partplayback.h, mscore/playbackmode.h)
      PartPlaybackModes::setLibraryDefault(bool(library));
      }

static std::mutex availableMutex;
static std::function<bool(const LibInstrument&)> availableFn;
static std::atomic<int> generation { 0 };

void setAvailable(std::function<bool(const LibInstrument&)> available)
      {
      {
            std::lock_guard<std::mutex> lock(availableMutex);
            availableFn = available;
      }
      ++generation;
      }

void routesChanged()
      {
      ++generation;
      }

//---------------------------------------------------------
//   DynamicsCalibration
//---------------------------------------------------------

static double interpolate(const std::vector<std::pair<int, double>>& points, int x)
      {
      if (points.empty())
            return -200;
      if (x <= points.front().first)
            return points.front().second;
      for (size_t i = 1; i < points.size(); ++i) {
            if (x <= points[i].first) {
                  const auto& a = points[i - 1];
                  const auto& b = points[i];
                  return a.second + (b.second - a.second) * (x - a.first) / double(b.first - a.first);
                  }
            }
      return points.back().second;
      }

double DynamicsCurve::at(int x) const
      {
      return interpolate(points, x);
      }

double DynamicsCurve::perceivedAt(int x) const
      {
      return interpolate(perceived, x);
      }

double DynamicsCurve::attackAt(int x) const
      {
      return interpolate(attack, x);
      }

// the lowest x that reaches db (a curve with a dip from round robins: the first crossing);
// under the first point, the first segment's slope goes on (ppp under a curve measured from 32)
static int inverseOf(const std::vector<std::pair<int, double>>& points, double db)
      {
      if (points.empty())
            return -1;
      if (db <= points.front().second) {
            if (points.size() >= 2 && points[1].second > points[0].second) {
                  const auto& a = points[0];
                  const auto& b = points[1];
                  return qBound(1, int(std::lround(a.first + (db - a.second) * (b.first - a.first) / (b.second - a.second))), a.first);
                  }
            return std::max(1, points.front().first);
            }
      for (size_t i = 1; i < points.size(); ++i) {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            if (db <= b.second && b.second > a.second)
                  return qBound(1, int(std::lround(a.first + (db - a.second) * (b.first - a.first) / (b.second - a.second))), 127);
            }
      return 127;
      }

int DynamicsCurve::inverse(double db) const
      {
      return inverseOf(points, db);
      }

int DynamicsCurve::perceivedInverse(double db) const
      {
      return inverseOf(perceived, db);
      }

int DynamicsCurve::louder(int x, double db) const
      {
      return byEar() ? perceivedInverse(perceivedAt(x) + db) : inverse(at(x) + db);
      }

int DynamicsCurve::raise(int x, double db) const
      {
      const double target = at(x) + db;
      const int first = inverse(target);              // the first crossing (as before where the curve rises)
      if (first >= x)
            return first;
      for (int y = std::max(1, x); y <= 127; ++y) {   // it lies under x: the curve dips; the first one over x
            if (at(y) >= target - 1e-9)
                  return y;
            }
      return 127;
      }

// two pitches' curves mixed in dB, w of b's (0 … 1), on the x of both
static std::vector<std::pair<int, double>> mixPoints(const std::vector<std::pair<int, double>>& a,
                                                     const std::vector<std::pair<int, double>>& b, double w)
      {
      if (a.empty() || b.empty())
            return w < 0.5 ? a : b;
      std::vector<int> xs;
      for (const auto& p : a)
            xs.push_back(p.first);
      for (const auto& p : b)
            xs.push_back(p.first);
      std::sort(xs.begin(), xs.end());
      xs.erase(std::unique(xs.begin(), xs.end()), xs.end());
      std::vector<std::pair<int, double>> out;
      for (int x : xs)
            out.push_back({ x, (1 - w) * interpolate(a, x) + w * interpolate(b, x) });
      return out;
      }

DynamicsCurve DynamicsCurve::atPitch(int pitch) const
      {
      if (pitches.empty() || pitch < 0)
            return *this;
      DynamicsCurve c = *this;
      c.pitches.clear();
      auto hi = pitches.lower_bound(pitch);
      const PitchCurve* a;
      const PitchCurve* b;
      double w = 0;
      if (hi == pitches.end())                        // over the highest measured pitch: the highest
            a = b = &std::prev(hi)->second;
      else if (hi->first == pitch || hi == pitches.begin())
            a = b = &hi->second;                      // a measured pitch, or under the lowest: the lowest
      else {
            auto lo = std::prev(hi);
            a = &lo->second;
            b = &hi->second;
            w = double(pitch - lo->first) / (hi->first - lo->first);
            }
      c.points = mixPoints(a->points, b->points, w);
      c.perceived = mixPoints(a->perceived, b->perceived, w);
      return c;
      }

const DynamicsCurve* DynamicsCalibration::curve(const QString& patch, int value) const
      {
      auto p = _patches.find(patch);
      if (p == _patches.end())
            return nullptr;
      auto v = p->second.find(value);
      return v == p->second.end() ? nullptr : &v->second;
      }

bool DynamicsCalibration::read(const QString& file)
      {
      QFile f(file);
      if (!f.open(QIODevice::ReadOnly))
            return false;
      // (an older file's short notes' balance -- balanceDb, familyBalanceDb, heardBalanceDb -- is ignored: its
      // setting went 2026-10-07, unused)
      const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
      _patches.clear();
      const QJsonObject patches = o.value("patches").toObject();
      for (auto p = patches.begin(); p != patches.end(); ++p) {
            const QJsonObject arts = p.value().toObject();
            for (auto a = arts.begin(); a != arts.end(); ++a) {
                  const QJsonObject c = a.value().toObject();
                  DynamicsCurve curve;
                  curve.drivenBy = c.value("drivenBy").toString();
                  for (const QJsonValue& pt : c.value("curve").toArray())
                        curve.points.push_back({ pt.toArray().at(0).toInt(), pt.toArray().at(1).toDouble() });
                  auto readPoints = [&](const char* key, std::vector<std::pair<int, double>>* out) {
                        for (const QJsonValue& pt : c.value(key).toArray())
                              out->push_back({ pt.toArray().at(0).toInt(), pt.toArray().at(1).toDouble() });
                        std::sort(out->begin(), out->end());
                        };
                  readPoints("perceived", &curve.perceived);
                  readPoints("attack", &curve.attack);
                  readPoints("expression", &curve.expression);
                  readPoints("expressionPerceived", &curve.expressionPerceived);
                  std::sort(curve.points.begin(), curve.points.end());
                  const QJsonObject pitches = c.value("pitches").toObject();
                  for (auto pp = pitches.begin(); pp != pitches.end(); ++pp) {
                        const QJsonObject pc = pp.value().toObject();
                        PitchCurve& out = curve.pitches[pp.key().toInt()];
                        for (const char* key : { "curve", "perceived" }) {
                              auto& pts = key[0] == 'c' ? out.points : out.perceived;
                              for (const QJsonValue& pt : pc.value(key).toArray())
                                    pts.push_back({ pt.toArray().at(0).toInt(), pt.toArray().at(1).toDouble() });
                              std::sort(pts.begin(), pts.end());
                              }
                        if (out.points.size() < 2)
                              curve.pitches.erase(pp.key().toInt());
                        }
                  _patches[p.key()][a.key().toInt()] = curve;
                  }
            }
      return true;
      }

std::shared_ptr<DynamicsCalibration> readCalibration(const QString& userFile, const QString& shippedFile,
                                                     RenderPreflight::Calibration* sources)
      {
      DynamicsCalibration shipped, user;
      const bool fromShipped = !shippedFile.isEmpty() && shipped.read(shippedFile);
      const bool own = !userFile.isEmpty() && user.read(userFile);
      if (sources) {
            *sources = RenderPreflight::Calibration();
            sources->shipped = RenderPreflight::fileId(shippedFile);
            sources->user = RenderPreflight::fileId(userFile);
            sources->shippedRead = fromShipped;
            sources->userRead = own;
            }
      if (!fromShipped && !own)
            return nullptr;
      auto c = std::make_shared<DynamicsCalibration>();
      for (const auto& p : shipped.patches())
            if (!user.patches().count(p.first)) {
                  for (const auto& a : p.second)
                        c->setCurve(p.first, a.first, a.second);
                  if (sources)
                        sources->patchFrom[p.first] = "shipped";
                  }
      for (const auto& p : user.patches()) {
            for (const auto& a : p.second)
                  c->setCurve(p.first, a.first, a.second);
            if (sources)
                  sources->patchFrom[p.first] = "user";
            }
      return c;
      }

bool DynamicsCalibration::write(const QString& file) const
      {
      QJsonObject patches;
      for (const auto& p : _patches) {
            QJsonObject arts;
            for (const auto& a : p.second) {
                  QJsonArray pts;
                  for (const auto& pt : a.second.points)
                        pts.append(QJsonArray({ pt.first, std::round(pt.second * 10) / 10 }));
                  QJsonObject o({ { "drivenBy", a.second.drivenBy }, { "curve", pts } });
                  auto writePoints = [&](const char* key, const std::vector<std::pair<int, double>>& in) {
                        if (in.empty())
                              return;
                        QJsonArray per;
                        for (const auto& pt : in)
                              per.append(QJsonArray({ pt.first, std::round(pt.second * 10) / 10 }));
                        o[key] = per;
                        };
                  writePoints("perceived", a.second.perceived);
                  writePoints("attack", a.second.attack);
                  writePoints("expression", a.second.expression);
                  writePoints("expressionPerceived", a.second.expressionPerceived);
                  if (!a.second.pitches.empty()) {
                        QJsonObject pitches;
                        for (const auto& pc : a.second.pitches) {
                              QJsonObject po;
                              for (const char* key : { "curve", "perceived" }) {
                                    const auto& in = key[0] == 'c' ? pc.second.points : pc.second.perceived;
                                    if (in.empty())
                                          continue;
                                    QJsonArray pts;
                                    for (const auto& pt : in)
                                          pts.append(QJsonArray({ pt.first, std::round(pt.second * 10) / 10 }));
                                    po[key] = pts;
                                    }
                              pitches[QString::number(pc.first)] = po;
                              }
                        o["pitches"] = pitches;
                        }
                  arts[QString::number(a.first)] = o;
                  }
            patches[p.first] = arts;
            }
      QJsonObject o;
      o["patches"] = patches;
      QFile f(file);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
      f.write(QJsonDocument(o).toJson());
      return true;
      }

static std::mutex calibrationMutex;
static std::shared_ptr<const DynamicsCalibration> calibration;

void setDynamicsCalibration(std::shared_ptr<const DynamicsCalibration> c)
      {
      {
            std::lock_guard<std::mutex> lock(calibrationMutex);
            calibration = c;
      }
      ++generation;
      }

std::shared_ptr<const DynamicsCalibration> dynamicsCalibration()
      {
      std::lock_guard<std::mutex> lock(calibrationMutex);
      return calibration;
      }

int calibratedController(const DynamicsCalibration& cal, const QString& patch, int longValue,
                         const QString& refPatch, int refValue, int cc, int pitch)
      {
      const DynamicsCurve* cp = cal.curve(patch, longValue);
      const DynamicsCurve* rp = cal.curve(refPatch, refValue);
      if (!cp || !rp || (cp->drivenBy != "controller" && cp->drivenBy != "both"))
            return -1;
      const DynamicsCurve c = cp->atPitch(pitch);
      const DynamicsCurve ref = rp->atPitch(pitch);
      if (c.points.size() < 2 || ref.points.size() < 2)
            return -1;
      if (c.byEar() && ref.byEar())                           // as loud by ear, as calibratedVelocity
            return c.perceivedInverse(ref.perceivedAt(cc));
      return c.inverse(ref.at(cc));
      }

int calibratedVelocity(const DynamicsCalibration& cal, const QString& patch, int value,
                       const QString& refPatch, int refValue, int cc, double db, int pitch)
      {
      const DynamicsCurve* cp = cal.curve(patch, value);
      const DynamicsCurve* rp = cal.curve(refPatch, refValue);
      if (!cp || !rp || (cp->drivenBy != "velocity" && cp->drivenBy != "both"))
            return -1;
      const DynamicsCurve c = cp->atPitch(pitch);
      const DynamicsCurve ref = rp->atPitch(pitch);
      if (c.points.size() < 2 || ref.points.size() < 2)
            return -1;
      if (c.byEar() && ref.byEar())                           // as loud by ear (the 50 ms level misjudges slow swells)
            return c.perceivedInverse(ref.perceivedAt(cc) + db);
      return c.inverse(ref.at(cc) + db);
      }

double loudEndDb(const DynamicsCalibration& cal, const QString& patch, int value, const QString& refPatch, int refValue, int pitch)
      {
      const DynamicsCurve* cp = cal.curve(patch, value);
      const DynamicsCurve* rp = cal.curve(refPatch, refValue);
      if (!cp || !rp || (cp->drivenBy != "velocity" && cp->drivenBy != "both"))
            return NAN;
      const DynamicsCurve c = cp->atPitch(pitch);
      const DynamicsCurve ref = rp->atPitch(pitch);
      if (c.points.size() < 2 || ref.points.size() < 2)
            return NAN;
      if (c.byEar() && ref.byEar())
            return c.perceivedAt(127) - ref.perceivedAt(127);
      return c.at(127) - ref.at(127);
      }

double techniqueOffsetDb(const DynamicsCalibration& cal, const QString& patch, const Articulation& a, const QString& refPatch,
                         int refValue, int pitch, int cc)
      {
      if (!a.heldOffsets.empty())
            return a.heldOffsetAt(cc);
      return loudEndDb(cal, patch, a.value, refPatch, refValue, pitch);
      }

int calibratedVelocityFloored(const DynamicsCalibration& cal, const QString& patch, int value,
                              const QString& refPatch, int refValue, int cc, double db, int pitch, double* under)
      {
      if (under)
            *under = 0.0;
      const int v = calibratedVelocity(cal, patch, value, refPatch, refValue, cc, db, pitch);
      if (v < 0)
            return v;
      const DynamicsCurve c = cal.curve(patch, value)->atPitch(pitch);
      const DynamicsCurve ref = cal.curve(refPatch, refValue)->atPitch(pitch);
      const bool byEar = c.byEar() && ref.byEar();
      const std::vector<std::pair<int, double>>& pts = byEar ? c.perceived : c.points;
      const double target = (byEar ? ref.perceivedAt(cc) : ref.at(cc)) + db;
      auto lowest = std::min_element(pts.begin(), pts.end());       // (the lowest velocity measured)
      if (lowest != pts.end() && target < lowest->second && v <= lowest->first) {
            if (under)
                  *under = target - lowest->second;
            return std::max(1, lowest->first);
            }
      return v;
      }

const DynamicsCurve* heldCurve(const DynamicsCalibration& cal, const std::vector<const LibInstrument*>& patches)
      {
      const Choice held = choose(patches, Want { { "long" }, {} });
      return held ? cal.curve(patches[size_t(held.patch)]->name, held.articulation->value) : nullptr;
      }

int routesGeneration()
      {
      return generation;
      }

static bool available(const LibInstrument& li)
      {
      std::lock_guard<std::mutex> lock(availableMutex);
      return !availableFn || availableFn(li);
      }

std::vector<std::pair<QString, QString>> ownTechniques(const Note* note)
      {
      std::vector<std::pair<QString, QString>> out;
      const std::shared_ptr<const Library> library = current();
      const Part* part = note ? note->part() : nullptr;
      if (!library || !part)
            return out;
      const Part* master = PartPlaybackModes::masterPart(part);
      if (!PartPlaybackModes::playsLibrary(master, PartPlaybackModes::read(note->score()->masterScore())))
            return out;
      const LibInstrument* li = library->match(part->instrument(note->chord()->tick()), part);
      if (!li || li->kit)
            return out;
      for (const LibInstrument* p : li->patches()) {
            if (p != li && !available(*p))
                  continue;
            for (const Articulation& a : p->articulations)
                  if (a.expect != "silent")
                        out.push_back({ ownTechniqueKey(*p, a), p == li ? a.name : QString("%1 (%2)").arg(a.name, p->name) });
            }
      return out;
      }

std::shared_ptr<const Library> current()
      {
      std::lock_guard<std::mutex> lock(currentMutex);
      return currentLibrary;
      }

bool active()
      {
      return currentActive;
      }

//---------------------------------------------------------
//   partMix
//---------------------------------------------------------

PartMix partMix(const Part* part, bool withSolo)
      {
      PartMix m;
      if (!part || part->instruments()->empty())
            return m;
      MasterScore* ms = const_cast<Part*>(part)->masterScore();
      const Instrument* first = part->instruments()->begin()->second;
      auto playback = [ms](const Channel* c) -> const Channel* {
            return (ms && c->channel() >= 0 && c->channel() < int(ms->midiMapping().size())) ? ms->playbackChannel(c) : c;
            };
      if (!first->channel().empty()) {
            const Channel* c = playback(first->channel(0));
            m.volume = c->volume();
            m.pan = c->pan();
            m.reverb = c->reverb();
            m.chorus = c->chorus();
            }
      bool any = false;
      bool all = true;
      for (const auto& ip : *part->instruments()) {
            for (const Channel* ch : ip.second->channel()) {
                  const Channel* c = playback(ch);
                  any = true;
                  all = all && (c->mute() || (withSolo && c->soloMute()));
                  }
            }
      m.muted = any && all;
      return m;
      }

//---------------------------------------------------------
//   routes
//---------------------------------------------------------

bool anyMono(const std::vector<const LibInstrument*>& patches)
      {
      return std::any_of(patches.begin(), patches.end(), [](const LibInstrument* p) { return p && p->mono; });
      }

std::vector<Route> routes(const Score* score, const Library& library, bool forLive)
      {
      std::vector<Route> result;
      int k = 0;
      // a part plays the library by its own playback mode, else by the global one (partplayback.h)
      const std::map<const Part*, PartPlayback> modes = PartPlaybackModes::read(score->masterScore());
      for (const Part* part : score->parts()) {
            if (!PartPlaybackModes::playsLibrary(part, modes))
                  continue;
            const LibInstrument* li = library.match(part->instrument(), part);
            if (!li)
                  continue;
            const std::vector<const LibInstrument*> patches = li->patches();
            std::vector<const LibInstrument*> offered { li };            // the main one, and the extras that can play
            for (const LibInstrument* e : li->extras)
                  if (available(*e))
                        offered.push_back(e);
            const std::vector<bool> usedOffered = offered.size() > 1 ? usedPatches(score, part, offered)
                                                                      : std::vector<bool>(1, true);
            std::vector<bool> used(patches.size(), false);
            for (size_t o = 0, p = 0; o < offered.size(); ++o) {
                  while (patches[p] != offered[o])
                        ++p;
                  used[p] = usedOffered[o];
                  }
            // a kit none of whose patches plays a sound of the part: the part stays built-in
            if (li->kit && std::find(used.begin() + 1, used.end(), true) == used.end())
                  continue;
            std::vector<int> laneCount(patches.size(), 1);
            std::vector<std::vector<double>> fixed(patches.size());
            std::vector<std::vector<int>> voices(patches.size());
            // (lanes: copies by tuning where the library plays tunings by varispeed; a monophonic patch's voices in any
            // library, without varispeed one copy per voice, untuned)
            const bool laned = (library.varispeed || anyMono(patches)) && !li->kit;
            if (laned) {
                  const LaneSettings ls = laneSettings(score, library);
                  const Lanes l = library.varispeed
                                  ? lanes(score, part, patches, ls.tolerance, ls.tail, ls.maxLanes, fixedLanes(score, library, forLive))
                                  : lanes(score, part, patches, std::numeric_limits<double>::infinity(), ls.tail, 1, false);
                  laneCount = l.count;
                  fixed = l.fixed;
                  fixed.resize(patches.size());
                  voices = l.voice;
                  voices.resize(patches.size());
                  }
            for (int p = 0; p < int(patches.size()); ++p) {
                  if (p > 0 && (!used[p] || !available(*patches[p])))
                        continue;
                  const int count = std::max(1, laneCount[size_t(p)]);
                  for (int lane = 0; lane < count; ++lane) {
                        if (k / 16 >= MAX_PORTS)
                              return result;
                        // (a lane's tuning: fixed; untuned without lanes; else it may vary)
                        const double cents = lane < int(fixed[size_t(p)].size()) ? fixed[size_t(p)][size_t(lane)]
                                             : !laned || !library.varispeed ? 0.0 : std::numeric_limits<double>::quiet_NaN();
                        const int voice = lane < int(voices[size_t(p)].size()) ? voices[size_t(p)][size_t(lane)] : 0;
                        result.push_back(Route { part, patches[p], k / 16, k % 16, p, lane, cents, voice });
                        ++k;
                        }
                  }
            }
      return result;
      }

//---------------------------------------------------------
//   hostedSlots / fixedLanes / instances
//---------------------------------------------------------

bool hostedSlots(const Score* score, const Library& library)
      {
      return output() == Output::PLUGIN && library.varispeed && !library.registryName.isEmpty()
             && Playback::value("tuning/hostedTuning", score) >= 0.5;
      }

bool fixedLanes(const Score* score, const Library& library, bool forLive)
      {
      return forLive || output() == Output::MIDI || hostedSlots(score, library);
      }

// a note of the part tuned beyond the patch's bend range (the renderer plays it by varispeed: libraryPitchBends)
static bool beyondBend(const Score* score, const Part* part, double bendCents)
      {
      Score* sc = const_cast<Score*>(score);
      const ScoreTuningScope tuningScope(sc);
      for (Segment* seg = sc->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* e = seg->element(track);
                  if (!e || !e->isChord())
                        continue;
                  const Chord* chord = toChord(e);
                  for (const Chord* g : chord->graceNotes())
                        for (const Note* n : g->notes())
                              if (bendValue(playbackTuning(n), bendCents) < 0)
                                    return true;
                  for (const Note* n : chord->notes())
                        if (bendValue(playbackTuning(n), bendCents) < 0)
                              return true;
                  }
            }
      return false;
      }

std::vector<Instance> instances(const Score* score, const Library& library, const std::vector<Route>& routes)
      {
      std::vector<Instance> out;
      const bool bySlots = hostedSlots(score, library);
      const bool bend = Playback::on("tuning/pitchBend", score);
      std::map<std::pair<const Part*, int>, bool> shares;        // (part, patch) -> its copies share an instance
      for (const Route& r : routes) {
            if (!r.instrument || r.instrument->kit)
                  continue;
            const int slot = r.port * 16 + r.channel;
            Instance* host = nullptr;
            if (bySlots && r.lane > 0 && !out.empty()) {
                  Instance& last = out.back();
                  auto s = shares.find({ r.part, r.patch });
                  if (last.part == r.part && last.instrument == r.instrument && s != shares.end() && s->second
                      && last.routes.size() < 16)
                        host = &last;
                  }
            if (host) {
                  host->routes.push_back(slot);
                  host->cents.push_back(r.cents);
                  continue;
                  }
            Instance in;
            in.slot = slot;
            in.part = r.part;
            in.instrument = r.instrument;
            in.routes.push_back(slot);
            if (bySlots) {
                  const bool bends = bend && r.instrument->bendCents > 0;
                  if (r.lane == 0 && !shares.count({ r.part, r.patch }))
                        shares[{ r.part, r.patch }] = !(bends && beyondBend(score, r.part, r.instrument->bendCents));
                  // (a copy alone: by its Tune when it has a fixed tuning; a bending copy alone plays as before)
                  if (std::isfinite(r.cents) || shares[{ r.part, r.patch }])
                        in.cents.push_back(r.cents);
                  }
            out.push_back(in);
            }
      // (a lone copy that bends, or a patch with one copy at 0 cents: nothing to tune, plain as before)
      for (Instance& in : out)
            if (in.routes.size() == 1 && !in.cents.empty() && (!std::isfinite(in.cents[0]) || std::fabs(in.cents[0]) < 0.005))
                  in.cents.clear();
      return out;
      }

//---------------------------------------------------------
//   usedPatches
//    what each note of the part asks for (as the renderer asks it, without repeats), chosen
//    from all the patches: the main patch always counts as used
//---------------------------------------------------------
//   laneSettings
//---------------------------------------------------------

const char* laneSettingsMetaTag = "soundLibraryLanes";

double defaultLaneTolerance()
      {
      return ScoreTuning::smallestAccidentalGap() / 2;
      }

double laneRing(const LibInstrument& patch, const Articulation* articulation, const std::vector<const LibInstrument*>& patches)
      {
      auto longest = [](const LibInstrument& p) {
            double r = 0;
            for (const Articulation& a : p.articulations)
                  r = std::max(r, a.releaseMs);
            return r;
            };
      double ms = articulation && articulation->releaseMs > 0 ? articulation->releaseMs : longest(patch);
      if (ms <= 0)
            for (const LibInstrument* p : patches)
                  ms = std::max(ms, longest(*p));
      return ms > 0 ? 2 * ms / 1000.0 : 0.0;          // (T30 extrapolated to 60 dB)
      }

// the memory the copies may take: what is free now plus what the system can still be given (installed beyond what is
// visible, past a 2 GB allowance for hardware reservations). Branch voice-determinism, 2026-10-21: the Windows test VM
// runs on Hyper-V dynamic memory (16 GB installed, 4.9 GB visible and 1.9 GB free when idle, more added as programs ask
// for it), so the free memory alone gave each run its own copies: Whence's Violins 1 - Performance on 1, 2 or 4 routes
// between runs of one score (1 + 1.9 GB / (245 MB x 5 parts) = 2)
qint64 memoryForCopies(qint64 available, qint64 visible, qint64 installed)
      {
      if (available < 0)
            return -1;
      const qint64 reserve = qint64(2) * 1024 * 1024 * 1024;
      const qint64 more = visible > 0 && installed > visible + reserve ? installed - visible - reserve : 0;
      return available + more;
      }

static qint64 testAvailable = -2, testVisible = -1, testInstalled = -1;

void setMemoryForTest(qint64 available, qint64 visible, qint64 installed)
      {
      testAvailable = available;
      testVisible = visible;
      testInstalled = installed;
      }

qint64 freeMemoryBytes()
      {
      if (testAvailable > -2)
            return memoryForCopies(testAvailable, testVisible, testInstalled);
      static const qint64 bytes = [] {
            // MS_FREE_MEMORY_MB: the tools' pin (the heard check's dump and export, dumps), so that the copies don't depend
            // on the machine's state
            bool ok = false;
            const qint64 pinned = qEnvironmentVariable("MS_FREE_MEMORY_MB").toLongLong(&ok);
            if (ok && pinned >= 0)
                  return pinned * 1024 * 1024;
#if defined(Q_OS_WIN)
            MEMORYSTATUSEX m;
            m.dwLength = sizeof(m);
            if (!GlobalMemoryStatusEx(&m))
                  return qint64(-1);
            ULONGLONG installedKb = 0;
            const qint64 installed = GetPhysicallyInstalledSystemMemory(&installedKb) ? qint64(installedKb) * 1024 : qint64(-1);
            return memoryForCopies(qint64(m.ullAvailPhys), qint64(m.ullTotalPhys), installed);
#elif defined(Q_OS_LINUX)
            QFile f("/proc/meminfo");                 // MemAvailable: the kernel's estimate of memory available to start applications
            if (f.open(QIODevice::ReadOnly))          // (no installed size for an unprivileged process: as visible)
                  for (const QByteArray& line : f.readAll().split('\n'))
                        if (line.startsWith("MemAvailable:"))
                              return line.mid(13).trimmed().split(' ').value(0).toLongLong() * 1024;
            return qint64(-1);
#else
            return qint64(-1);
#endif
            }();
      return bytes;
      }

int memoryMaxLanes(qint64 freeBytes, int parts)
      {
      if (freeBytes < 0)
            return 1;                                 // (unknown: no copies beyond the patch itself)
      return 1 + int(freeBytes / (LANE_COPY_BYTES * std::max(1, parts)));
      }

// the parts whose notes play at more than one tuning (beyond the tolerance, cents): only they need copies (a part's
// first copy takes any tuning). Needs a ScoreTuningScope (playbackTuning). A part MuseScore's own sounds play counts
// too when it has microtones (an overcount: its share of the memory goes unused)
int partsNeedingCopies(const Score* score, double toleranceCents)
      {
      if (!score)
            return 0;
      const Score* ms = score->masterScore();
      int count = 0;
      for (const Part* part : ms->parts()) {
            bool first = true;
            double cents0 = 0;
            bool needs = false;
            for (const Segment* s = ms->firstSegment(SegmentType::ChordRest); s && !needs; s = s->next1(SegmentType::ChordRest)) {
                  for (int t = part->startTrack(); t < part->endTrack() && !needs; ++t) {
                        const Element* e = s->element(t);
                        if (!e || !e->isChord())
                              continue;
                        const Chord* ch = toChord(e);
                        std::vector<const Chord*> chords { ch };
                        for (const Chord* g : ch->graceNotes())
                              chords.push_back(g);
                        for (const Chord* c : chords) {
                              for (const Note* n : c->notes()) {
                                    if (!n->play())
                                          continue;
                                    const double cents = playbackTuning(n);
                                    if (first) {
                                          cents0 = cents;
                                          first = false;
                                          }
                                    else if (std::fabs(cents - cents0) > toleranceCents)
                                          needs = true;
                                    }
                              }
                        }
                  }
            if (needs)
                  ++count;
            }
      return count;
      }

// the free memory split evenly between the parts that need copies (the owner, 2026-10-04: not between all the score's
// parts; a part with one tuning needs none), at least one share
int memoryMaxLanes(const Score* score, double toleranceCents)
      {
      return memoryMaxLanes(freeMemoryBytes(), std::max(1, partsNeedingCopies(score, toleranceCents)));
      }

LaneSettings libraryLaneSettings(const Library& library)
      {
      // (the map's, unless playback.ini [tuning] sets its own; neither: computed, see the Library's fields)
      LaneSettings s;
      s.tolerance = Playback::value("tuning/tolerance", nullptr, library.laneTolerance >= 0 ? library.laneTolerance
                                                                                          : defaultLaneTolerance());
      s.tail = Playback::iniHas("tuning/tail") || library.laneTail >= 0 ? Playback::value("tuning/tail", nullptr, library.laneTail) : -1.0;
      s.maxLanes = Playback::iniHas("tuning/maxLanes") || library.maxLanes > 0
                   ? int(Playback::value("tuning/maxLanes", nullptr, library.maxLanes)) : 0;
      return s;
      }

LaneSettings laneSettings(const Score* score, const Library& library)
      {
      LaneSettings s = libraryLaneSettings(library);
      if (!score)
            return s;
      const QString tag = score->masterScore()->metaTag(laneSettingsMetaTag);
      for (const QString& item : tag.split(' ', QString::SkipEmptyParts)) {
            const QString key = item.section('=', 0, 0);
            bool ok = false;
            const double v = item.section('=', 1).toDouble(&ok);
            if (!ok)
                  continue;
            if (key == "tolerance" && v >= 0.0)
                  s.tolerance = v;
            else if (key == "tail" && v >= 0.0)
                  s.tail = v;
            else if (key == "max" && v >= 1.0)
                  s.maxLanes = int(v);
            }
      return s;
      }

const char* onsetEarlyMetaTag = "soundLibraryOnsetEarly";

int onsetEarly(const Score* score, const Library& library)
      {
      return int(std::lround(Playback::value("heldNotes/early", score, library.onsetEarly)));
      }

int bendValue(double cents, double bendCents)
      {
      if (bendCents <= 0 || std::fabs(cents) > bendCents + 1e-6)
            return -1;
      return qBound(0, int(std::lround(8192.0 + cents / bendCents * (cents < 0 ? 8192.0 : 8191.0))), 16383);
      }

QString writeLaneSettings(const LaneSettings& s, const Library& library)
      {
      const LaneSettings d = libraryLaneSettings(library);
      QStringList items;
      if (s.tolerance != d.tolerance)
            items << QString("tolerance=%1").arg(s.tolerance);
      if (s.tail != d.tail)
            items << QString("tail=%1").arg(s.tail);
      if (s.maxLanes != d.maxLanes)
            items << QString("max=%1").arg(s.maxLanes);
      return items.join(' ');
      }

//---------------------------------------------------------

std::vector<bool> usedPatches(const Score* score, const Part* part, const std::vector<const LibInstrument*>& patches)
      {
      std::vector<bool> used(patches.size(), false);
      if (!used.empty())
            used[0] = true;
      Score* sc = const_cast<Score*>(score);
      Ms4::Dynamics dynamics;
      dynamics.build(sc, const_cast<Part*>(part));
      TextTechniques text;
      text.build(sc, part);
      const int strack = part->startTrack();
      const int etrack = part->endTrack();
      for (Segment* seg = sc->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (int track = strack; track < etrack; ++track) {
                  Element* e = seg->element(track);
                  if (!e || !e->isChord())
                        continue;
                  const Chord* chord = toChord(e);
                  if (patches[0]->kit) {
                        const QString id = chord->part()->instrument(chord->tick())->getId();
                        const bool roll = drumRoll(chord);
                        for (const Note* note : chord->notes()) {
                              DrumChoice d = roll ? drum(patches, note->pitch(), id, "roll") : DrumChoice();
                              if (d.patch < 0)
                                    d = drum(patches, note->pitch(), id);
                              if (d.patch >= 0)
                                    used[d.patch] = true;
                              }
                        continue;
                        }
                  const std::vector<Ms4::ArtRef> chordArts = Ms4::chordArticulations(chord, dynamics);
                  const int tick = chord->tick().ticks();
                  for (const Note* note : chord->notes()) {
                        const double seconds = noteSeconds(note);
                        const std::vector<Ms4::ArtRef> arts = Ms4::noteArticulations(note, chordArts);
                        int trill = 0;
                        for (const Ms4::ArtRef& a : arts)
                              if (a.art == Ms4::Art::Trill || a.art == Ms4::Art::TrillBaroque)
                                    trill = trillSemitones(note);
                        const Choice c = chooseNote(patches, want(arts, text.at(tick), seconds, trill, score), note);
                        if (c)
                              used[c.patch] = true;
                        }
                  }
            }
      return used;
      }

//---------------------------------------------------------
//   lanes
//---------------------------------------------------------

double divisiDb(int voices)
      {
      return voices > 1 ? -10.0 * std::log10(double(voices)) : 0.0;
      }

Lanes lanes(const Score* score, const Part* part, const std::vector<const LibInstrument*>& patches,
            double toleranceCents, double tailSeconds, int maxLanes, bool fixedTuning)
      {
      Lanes out;
      out.count.assign(patches.size(), patches.empty() ? 0 : 1);
      if (patches.empty() || patches[0]->kit)
            return out;
      Score* sc = const_cast<Score*>(score);
      const ScoreTuningScope tuningScope(sc);
      if (maxLanes <= 0)
            maxLanes = memoryMaxLanes(score, toleranceCents);
      const bool waitForRelease = Playback::on("tuning/waitForRelease", score);   // (playback.ini [tuning])
      Ms4::Dynamics dynamics;
      dynamics.build(sc, const_cast<Part*>(part));
      TextTechniques text;
      text.build(sc, part);
      const TempoMap* tm = score->tempomap();

      struct Item {
            const Note* note;
            int patch;
            double on, off;               // seconds
            double ring;                  // how long its copy rings after its end (seconds: the tail, or laneRing)
            double cents;
            bool slurred;                 // under a slur: legato from the note before on its track
            int track;
            const Chord* grace { nullptr };     // a grace note's: the chord it belongs to (no voice of its own)
            int voice { 0 };              // a monophonic patch's voice (0 the line)
            bool plain { false };         // [slurs] plainPerformance's (Choice::plain): touching the note before, a transition
            int chordNotes { 1 };         // its chord's notes on its patch (a monophonic patch's chord: > 1)
            };
      std::vector<Item> items;
      std::map<const Note*, const Note*> tiedTo;     // a tied note -> the note it continues
      auto end = [&](const Note* n) {                // the end of a note and the notes tied to it
            const Note* last = n->lastTiedNote();
            const Chord* c = last->chord();
            return tm->tick2time(c->tick().ticks() + c->actualTicks().ticks());
            };
      for (Segment* seg = sc->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                  Element* e = seg->element(track);
                  if (!e || !e->isChord())
                        continue;
                  const Chord* chord = toChord(e);
                  const std::vector<Ms4::ArtRef> chordArts = Ms4::chordArticulations(chord, dynamics);
                  const int tick = chord->tick().ticks();
                  auto add = [&](const Note* note, double on, double off, const Chord* graceOf) {
                        const double seconds = noteSeconds(note);
                        if (note->tieBack() && note->firstTiedNote() && note->firstTiedNote() != note) {
                              tiedTo[note] = note->firstTiedNote();
                              return;
                              }
                        const std::vector<Ms4::ArtRef> arts = Ms4::noteArticulations(note, chordArts);
                        int trill = 0;
                        bool slurred = false;
                        for (const Ms4::ArtRef& a : arts) {
                              if (a.art == Ms4::Art::Trill || a.art == Ms4::Art::TrillBaroque)
                                    trill = trillSemitones(note);
                              if (a.art == Ms4::Art::Legato)
                                    slurred = true;
                              }
                        const Choice c = chooseNote(patches, want(arts, text.at(tick), seconds, trill, score), note);
                        const double measured = c && c.articulation->releaseMs > 0 ? c.articulation->releaseMs / 1000.0 : 0.0;
                        // (the ring: a fixed tail with the release waited for, else the release to 60 dB under)
                        const double ring = tailSeconds < 0 ? laneRing(*patches[size_t(c ? c.patch : 0)], c ? c.articulation : nullptr, patches)
                                                            : std::max(tailSeconds, waitForRelease ? measured : 0.0);
                        items.push_back({ note, c ? c.patch : 0, on, off, ring,
                                          playbackTuning(note), slurred, track, graceOf, 0, c && c.plain });
                        };
                  for (const Chord* g : chord->graceNotes())
                        for (const Note* n : g->notes())
                              add(n, tm->tick2time(tick), tm->tick2time(tick) + 0.1, chord);
                  for (const Note* n : chord->notes())
                        add(n, tm->tick2time(tick), end(n), nullptr);
                  }
            }
      std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.on < b.on; });

      {                                                 // (a chord's notes on each patch)
      std::map<std::pair<const Chord*, int>, int> perChord;
      for (const Item& it : items)
            if (!it.grace)
                  ++perChord[{ it.note->chord(), it.patch }];
      for (Item& it : items)
            if (!it.grace)
                  it.chordNotes = perChord[{ it.note->chord(), it.patch }];
      }
      // voices (a monophonic patch: LibInstrument::mono): the notes starting together from the lowest pitch up, each
      // the lowest voice free by its start as written; a grace note its chord's lowest note's voice
      int voiceCount = 1;
      {
      std::map<const Note*, int> voiceOfNote;
      for (size_t p = 0; p < patches.size(); ++p) {
            if (!patches[p]->mono)
                  continue;
            std::vector<double> voiceEnd;                // per voice: its last note's written end
            std::vector<size_t> group;
            auto place = [&]() {
                  std::stable_sort(group.begin(), group.end(), [&](size_t a, size_t b) {
                        const Note* na = items[a].note;
                        const Note* nb = items[b].note;
                        return na->ppitch() != nb->ppitch() ? na->ppitch() < nb->ppitch() : items[a].cents < items[b].cents;
                        });
                  for (size_t i : group) {
                        Item& it = items[i];
                        int v = 0;
                        while (v < int(voiceEnd.size()) && voiceEnd[size_t(v)] > it.on + 1e-6)
                              ++v;
                        if (v == int(voiceEnd.size()))
                              voiceEnd.push_back(-1);
                        voiceEnd[size_t(v)] = it.off;
                        it.voice = v;
                        voiceOfNote[it.note] = v;
                        voiceCount = std::max(voiceCount, v + 1);
                        }
                  group.clear();
                  };
            for (size_t i = 0; i < items.size(); ++i) {
                  if (items[i].patch != int(p) || items[i].grace)
                        continue;
                  if (!group.empty() && std::fabs(items[group.front()].on - items[i].on) > 1e-9)
                        place();
                  group.push_back(i);
                  }
            place();
            }
      for (Item& it : items) {
            if (!it.grace || !patches[size_t(it.patch)]->mono)
                  continue;
            int v = -1;
            for (const Note* n : it.grace->notes()) {                    // (its lowest note on the patch)
                  auto f = voiceOfNote.find(n->tieBack() && n->firstTiedNote() ? n->firstTiedNote() : n);
                  if (f != voiceOfNote.end()) {
                        v = f->second;
                        break;
                        }
                  }
            it.voice = std::max(0, v);
            }
      }

      // [levels] divisi: each monophonic patch's voices sounding at each note's start, as written (the notes starting
      // together all counted; a grace note its chord's count)
      for (size_t p = 0; p < patches.size(); ++p) {
            if (!patches[p]->mono)
                  continue;
            std::vector<double> ends;                     // the started notes' written ends
            std::map<double, int> atStart;                // a start (seconds) -> the voices sounding then
            for (size_t i = 0; i < items.size();) {
                  if (items[i].patch != int(p) || items[i].grace) {
                        ++i;
                        continue;
                        }
                  const double on = items[i].on;
                  ends.erase(std::remove_if(ends.begin(), ends.end(), [on](double e) { return e <= on + 1e-6; }), ends.end());
                  std::vector<size_t> group;
                  for (; i < items.size() && (items[i].patch != int(p) || items[i].grace || std::fabs(items[i].on - on) <= 1e-9); ++i) {
                        if (items[i].patch != int(p) || items[i].grace)
                              continue;
                        group.push_back(i);
                        if (items[i].off > on + 1e-6)
                              ends.push_back(items[i].off);
                        }
                  const int n = std::max(int(ends.size()), 1);
                  atStart[on] = n;
                  if (n > 1)
                        for (size_t g : group)
                              out.sounding[items[g].note] = n;
                  }
            for (const Item& it : items) {
                  if (it.patch != int(p) || !it.grace)
                        continue;
                  auto a = atStart.find(it.on);
                  if (a != atStart.end() && a->second > 1)
                        out.sounding[it.note] = a->second;
                  }
            }

      struct Lane {
            double cents { 0 };
            bool tuned { false };         // (a new lane takes any tuning)
            double busyUntil { -1 };      // its notes' end plus the tail (or their release, if longer)
            double lastOn { -1 };         // its last note's start and end
            double lastEnd { -1 };
            int lastTrack { -1 };
            int lastChordNotes { 1 };
            };
      // per patch, per voice: its lanes (a voice's lanes take only its own notes: the lanes by tuning, as ever)
      std::vector<std::vector<std::vector<Lane>>> byPool(patches.size(), std::vector<std::vector<Lane>>(size_t(voiceCount)));
      // (fixedTuning: the patches whose lanes keep their tuning: those that don't bend)
      std::vector<bool> fixed(patches.size(), false);
      if (fixedTuning) {
            const bool bend = Playback::on("tuning/pitchBend", score);
            for (size_t p = 0; p < patches.size(); ++p)
                  fixed[p] = !(bend && patches[p]->bendCents > 0);
            }
      for (const Item& it : items) {
            std::vector<Lane>& lanes = byPool[size_t(it.patch)][size_t(it.voice)];
            if (lanes.empty())
                  lanes.emplace_back();
            int chosen = -1;
            double cents = it.cents;
            if (fixed[size_t(it.patch)]) {
                  for (int l = 0; l < int(lanes.size()) && chosen < 0; ++l)
                        if (!lanes[l].tuned || std::fabs(lanes[l].cents - it.cents) <= toleranceCents)
                              chosen = l;
                  if (chosen < 0) {
                        chosen = int(lanes.size());
                        lanes.emplace_back();
                        }
                  Lane& lane = lanes[size_t(chosen)];
                  if (!lane.tuned) {
                        lane.cents = it.cents;
                        lane.tuned = true;
                        }
                  out.lane[it.note] = chosen;
                  out.cents[it.note] = lane.cents;
                  continue;
                  }
            // (a monophonic patch's chord, or a note after one, played plain: touching the note before on its voice, SSO
            // plays a transition, which glides on the lane as a slur's does)
            const bool mono = patches[size_t(it.patch)]->mono;
            for (int l = 0; l < int(lanes.size()) && chosen < 0 && (it.slurred || mono); ++l) {     // legato: its lane, which glides
                  const bool legato = it.slurred || (it.plain && (it.chordNotes > 1 || lanes[l].lastChordNotes > 1));
                  if (legato && lanes[l].lastTrack == it.track && lanes[l].lastOn < it.on && std::fabs(lanes[l].lastEnd - it.on) <= 0.1)
                        chosen = l;
                  }
            for (int l = 0; l < int(lanes.size()) && chosen < 0; ++l)       // already at its tuning
                  if (!lanes[l].tuned || std::fabs(lanes[l].cents - it.cents) <= toleranceCents)
                        chosen = l;
            if (chosen >= 0 && lanes[chosen].tuned && std::fabs(lanes[chosen].cents - it.cents) <= toleranceCents)
                  cents = lanes[chosen].cents;
            for (int l = 0; l < int(lanes.size()) && chosen < 0; ++l)       // silent by then
                  if (lanes[l].busyUntil <= it.on)
                        chosen = l;
            if (chosen < 0 && int(lanes.size()) >= maxLanes) {                // (memory: the lane quiet longest)
                  chosen = 0;
                  for (int l = 1; l < int(lanes.size()); ++l)
                        if (lanes[l].busyUntil < lanes[size_t(chosen)].busyUntil)
                              chosen = l;
                  }
            if (chosen < 0) {
                  chosen = int(lanes.size());
                  lanes.emplace_back();
                  }
            Lane& lane = lanes[size_t(chosen)];
            lane.cents = cents;
            lane.tuned = true;
            // (a release rings up to 2.9 s, SSO's Flautando: retuning the lane before would move its pitch)
            lane.busyUntil = std::max(lane.busyUntil, it.off + it.ring);
            lane.lastOn = it.on;
            lane.lastEnd = it.off;
            lane.lastTrack = it.track;
            lane.lastChordNotes = it.chordNotes;
            out.lane[it.note] = chosen;
            out.cents[it.note] = cents;
            }
      // (fixed: within each voice, lane 0 the tuning nearest 0, then by distance, below first)
      out.fixed.assign(patches.size(), std::vector<double>());
      out.voice.assign(patches.size(), std::vector<int>());
      std::vector<std::vector<int>> offset(patches.size(), std::vector<int>(size_t(voiceCount), 0));
      for (size_t p = 0; p < patches.size(); ++p) {
            int at = 0;
            for (int v = 0; v < voiceCount; ++v) {
                  std::vector<Lane>& pool = byPool[p][size_t(v)];
                  offset[p][size_t(v)] = at;
                  if (v == 0 && pool.empty()) {           // (no notes: its one lane, untuned)
                        if (fixed[p])
                              out.fixed[p].push_back(0.0);
                        out.voice[p].push_back(0);
                        ++at;
                        continue;
                        }
                  at += int(pool.size());
                  for (size_t l = 0; l < pool.size(); ++l)
                        out.voice[p].push_back(v);
                  if (!fixed[p])
                        continue;
                  std::vector<int> order(pool.size());
                  for (size_t l = 0; l < order.size(); ++l)
                        order[l] = int(l);
                  std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
                        const double ca = pool[size_t(a)].cents, cb = pool[size_t(b)].cents;
                        return std::fabs(ca) != std::fabs(cb) ? std::fabs(ca) < std::fabs(cb) : ca < cb;
                        });
                  std::vector<int> newIndex(order.size());
                  for (size_t l = 0; l < order.size(); ++l) {
                        newIndex[size_t(order[l])] = int(l);
                        out.fixed[p].push_back(pool[size_t(order[l])].cents);
                        }
                  for (const Item& it : items)
                        if (it.patch == int(p) && it.voice == v) {
                              auto nl = out.lane.find(it.note);
                              if (nl != out.lane.end())
                                    nl->second = newIndex[size_t(nl->second)];
                              }
                  }
            out.count[p] = std::max(1, at);
            }
      for (const Item& it : items) {                  // (a voice's lanes after the voices before)
            auto nl = out.lane.find(it.note);
            if (nl != out.lane.end())
                  nl->second += offset[size_t(it.patch)][size_t(it.voice)];
            }
      for (const auto& t : tiedTo) {
            auto l = out.lane.find(t.second);
            if (l != out.lane.end()) {
                  out.lane[t.first] = l->second;
                  out.cents[t.first] = out.cents[t.second];
                  }
            }
      return out;
      }

//---------------------------------------------------------
//   TextTechniques
//---------------------------------------------------------

static void addModifier(TextState& s, const char* m)
      {
      if (!s.modifiers.contains(m))
            s.modifiers.append(m);
      }

void TextTechniques::apply(const QString& text, TextState& s)
      {
      // lower case, without accents (cuivré, naturale …)
      QString t = text.toLower().normalized(QString::NormalizationForm_D);
      t.remove(QRegularExpression("[\\x{0300}-\\x{036f}]"));
      auto has = [&t](const char* re) { return t.contains(QRegularExpression(re)); };

      // back to normal first: "ord." may come with a new technique ("ord. pizz.")
      if (has("\\b(ord|ordin|ordinario|ordinary|nat|naturale|natural|norm|normale|normal|modo ordinario)\\b")) {
            for (const char* m : { "sulpont", "sultasto", "flautando", "cuivre", "sulg", "sulc", "bellsup", "pdlt", "multitongue", "espressivo", "performance" })
                  s.modifiers.removeAll(m);
            s.harmonics = false;
            s.tremolo = false;
            s.colLegno = false;
            }
      if (has("\\b(senza|via|without)\\s+(sord|sordin|sordino|sordini|mute|mutes)") || has("\\b(open|aperto|offen)\\b"))
            s.modifiers.removeAll("muted");
      else if (has("\\b(con\\s+sord|sord\\.|sordin|mute|muted|harmon|stopped|gestopft|bouche|copert[oi]|muffled|damped)"))
            addModifier(s, "muted");
      // a guitar's sample set (Ample's Finger / Pick / Strum: an extra patch per set, guitarplayback.cpp; the whole text,
      // so "strum mute" stays an FX noise)
      if (has("^\\s*(pick|picked|plectrum|with pick)\\.?\\s*$")) {
            s.modifiers.removeAll("strum");
            addModifier(s, "pick");
            }
      else if (has("^\\s*(strum|strums|strummed|strumming)\\.?\\s*$")) {
            s.modifiers.removeAll("pick");
            addModifier(s, "strum");
            }
      else if (has("^\\s*(finger|fingers|fingerstyle|fingerpicking|fingerpicked)\\.?\\s*$")) {
            s.modifiers.removeAll("pick");
            s.modifiers.removeAll("strum");
            }
      if (has("\\bpizz"))
            s.pizzicato = true, s.colLegno = false;
      if (has("\\barco\\b"))
            s.pizzicato = false, s.colLegno = false;
      if (has("\\bcol\\s+legno"))
            s.colLegno = true, s.pizzicato = false;
      if (has("\\b(sul\\s+pont|s\\.\\s*p\\.|pont\\.)")) {
            addModifier(s, "sulpont");
            s.modifiers.removeAll("sultasto");
            s.modifiers.removeAll("flautando");
            }
      if (has("\\b(sul\\s+tasto|s\\.\\s*t\\.)")) {
            addModifier(s, "sultasto");
            s.modifiers.removeAll("sulpont");
            }
      if (has("\\bflaut"))
            addModifier(s, "flautando");
      if (has("\\b(cuivre|brassy)"))
            addModifier(s, "cuivre");
      // on one string (the strings' lowest: violins G, violas and celli C)
      if (has("\\bsul\\s+g\\b")) {
            addModifier(s, "sulg");
            s.modifiers.removeAll("sulc");
            }
      if (has("\\bsul\\s+c\\b")) {
            addModifier(s, "sulc");
            s.modifiers.removeAll("sulg");
            }
      if (has("\\bbells\\s+(down|normal)\\b"))
            s.modifiers.removeAll("bellsup");
      else if (has("\\b(bells\\s+up|bells\\s+in\\s+the\\s+air|campana\\s+in\\s+aria|campane\\s+in\\s+aria|pavillons?\\s+en\\s+l.air|schalltrichter\\s+(auf|hoch))"))
            addModifier(s, "bellsup");
      // espressivo / molto vibrato (the owner, 2026-09-28: SSO's Long (Rachm.) for those passages, not
      // as the default held sound); non / senza vibrato ends it
      if (has("\\b(non|senza)\\s+vib"))
            s.modifiers.removeAll("espressivo");
      else if (has("\\b(espr|espress)") || has("\\bmolto\\s+vib") || has("\\bcon\\s+(molto\\s+)?vibrato"))
            addModifier(s, "espressivo");
      if (has("\\b(pres\\s+de\\s+la\\s+table|p\\.?\\s*d\\.?\\s*l\\.?\\s*t\\b)"))
            addModifier(s, "pdlt");
      // Spitfire's Performance legato (the owner, 2026-10-07: only where the score asks for it); "ord." ends it too.
      // "non performance" also keeps slurred notes off it with slurs/performance on (Want::slurPerformance)
      if (has("\\b(non|senza|no)\\s+performance\\b")) {
            s.modifiers.removeAll("performance");
            s.performanceOff = true;
            }
      else if (has("\\bperformance\\b")) {
            addModifier(s, "performance");
            s.performanceOff = false;
            }
      if (has("\\b(multi|double|triple)[\\s-]*tongu") || has("\\b(doppel|tripel)zunge"))
            addModifier(s, "multitongue");
      if (has("\\bharm(?!on)"))
            s.harmonics = true;
      if (has("\\b(non|senza)\\s+(trem|flz|flutter)"))
            s.tremolo = false;
      else if (has("\\b(trem|flz|flatt|flutter|frull)"))
            s.tremolo = true;
      }

void TextTechniques::build(Score* score, const Part* part, const Guitar* guitar)
      {
      _states.clear();
      const int strack = part->startTrack();
      const int etrack = part->endTrack();
      TextState state;
      for (Segment* seg = score->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
            for (Element* e : seg->annotations()) {
                  if (!e->isStaffText() || e->track() < strack || e->track() >= etrack)
                        continue;
                  const QString plain = toStaffText(e)->plainText();
                  if (guitar) {
                        const QString simple = plain.simplified();
                        if (std::any_of(guitar->fx.begin(), guitar->fx.end(),
                                        [&](const Guitar::Fx& f) { return f.text.match(simple).hasMatch(); }))
                              continue;
                        }
                  const TextState before = state;
                  apply(plain, state);
                  if (!state.sameTechnique(before))
                        state.db = toStaffText(e)->level();
                  _states[seg->tick().ticks()] = state;
                  }
            }
      }

TextState TextTechniques::at(int tick) const
      {
      auto it = _states.upper_bound(tick);
      if (it == _states.begin())
            return TextState();
      return std::prev(it)->second;
      }

//---------------------------------------------------------
//   controllerTexts
//---------------------------------------------------------

std::map<int, int> controllerTexts(Score* score, const Part* part, const Controller& controller)
      {
      std::map<int, int> ticks;
      if (controller.texts.empty())
            return ticks;
      const int strack = part->startTrack();
      const int etrack = part->endTrack();
      for (Segment* seg = score->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
            for (Element* e : seg->annotations()) {
                  if (!e->isStaffText() || e->track() < strack || e->track() >= etrack)
                        continue;
                  const QString text = toStaffText(e)->plainText().simplified();
                  for (const ControllerText& t : controller.texts) {
                        if (t.match.match(text).hasMatch()) {
                              ticks[seg->tick().ticks()] = t.value;
                              break;
                              }
                        }
                  }
            }
      return ticks;
      }

//---------------------------------------------------------
//   want
//---------------------------------------------------------

Want want(const std::vector<Ms4::ArtRef>& arts, const TextState& text, double seconds, int trillSemitones, const Score* score)
      {
      using Ms4::Art;
      auto has = [&arts](Art a) {
            for (const Ms4::ArtRef& r : arts)
                  if (r.art == a)
                        return true;
            return false;
            };
      Want w;
      w.seconds = seconds;
      // (the strings' factors: the measured from= are on string patches; staccato, staccatissimo and tenuto are
      // the same in every family)
      // (playback settings [shorts]: a factor set in playback.ini or the score replaces MS4's for its articulation)
      w.byMeantLength = Playback::on("shorts/byMeantLength", score);
      if (seconds > 0) {
            double factor = Ms4::note(Ms4::Family::Strings, arts, 0, false).dur / double(Ms4::HUNDRED);
            const bool stacc = has(Art::Staccato), ten = has(Art::Tenuto);
            const char* id = stacc && ten ? "shorts/portato" : has(Art::Staccatissimo) ? "shorts/staccatissimo"
                             : stacc ? "shorts/staccato" : ten ? "shorts/tenuto" : nullptr;
            if (id && Playback::source(id, score) != Playback::Source::DEFAULT)
                  factor = Playback::value(id, score) / 100.0;
            w.soundSeconds = seconds * factor;
            }
      w.modifiers = text.modifiers;
      w.plainPerformance = !text.performanceOff && Playback::on("slurs/plainPerformance", score);
      w.plainTiming = w.plainPerformance && Playback::on("slurs/plainTiming", score);
      w.staccato = has(Art::Staccato) || has(Art::Staccatissimo);
      if (has(Art::Mute) || has(Art::PalmMute)) {
            if (!w.modifiers.contains("muted"))
                  w.modifiers.append("muted");
            }
      // "open" ends a mute. MS4 keeps a staff text "open" in force as its technique Art::Open until "natural" /
      // "mute" … (Dynamics::addTechnique), so it would also end a later text "muted" (which isn't in MS4's table):
      // for the library the text state decides, as it holds the newest of the two texts (TextTechniques::apply
      // drops "muted" at "open"); only an "open" articulation on the note itself ends it here
      bool openArt = false;
      for (const Ms4::ArtRef& r : arts)
            openArt |= r.art == Art::Open && !r.technique;
      if (openArt)
            w.modifiers.removeAll("muted");
      if (has(Art::Harmonic) || has(Art::DiamondNote) || text.harmonics)
            w.modifiers.append("harmonics");
      if (has(Art::SulPont) && !w.modifiers.contains("sulpont"))
            w.modifiers.append("sulpont");
      if (has(Art::SulTasto) && !w.modifiers.contains("sultasto"))
            w.modifiers.append("sultasto");

      QStringList& b = w.bases;
      if (has(Art::SnapPizzicato)) {
            b << "bartok" << "pizzicato";
            return w;
            }
      if (has(Art::Pizzicato) || text.pizzicato) {
            b << "pizzicato";
            return w;
            }
      if (has(Art::ColLegno) || text.colLegno) {
            b << "collegno" << "short";
            return w;
            }

      // sampled ornaments first, the note's own articulation as a fallback
      if (has(Art::Trill) || has(Art::TrillBaroque)) {
            static const char* const TRILL[] = { nullptr, "trill-m2", "trill-M2", "trill-m3", "trill-M3" };
            if (trillSemitones >= 1 && trillSemitones <= 4)
                  b << TRILL[trillSemitones];
            }
      if (has(Art::Tremolo8th) || has(Art::Tremolo16th) || has(Art::Tremolo32nd) || has(Art::Tremolo64th)
          || has(Art::TremoloBuzz) || text.tremolo)
            b << "tremolo";
      if (has(Art::Fall) || has(Art::QuickFall))
            b << "fall";
      if (has(Art::Scoop))
            b << "rip";

      const bool accent = has(Art::Accent) || has(Art::Marcato);
      const bool shortNote = seconds < 0.6;
      // (a short with a length - Short 0'5, Short 1'0 - is skipped for a note shorter than it: then
      // the next shorter one, down to spiccato; the owner, 2026-09-28: a fast staccato on Short 0'5 rang on)
      if (has(Art::Staccatissimo))
            b << "staccatissimo" << "spiccato" << "short";
      else if (has(Art::Staccato)) {
            if (accent)
                  b << "marcato";
            else if (has(Art::Tenuto))
                  b << "tenuto";
            b << "short" << "spiccato";
            }
      else if (accent) {
            if (shortNote)
                  b << "marcato";
            b << "longmarcato" << "long";
            }
      else if (has(Art::Tenuto)) {
            // (a fast tenuto: never a bouncing spiccato; Short 0'5 if it fits, else the held note)
            if (shortNote)
                  b << "tenuto" << "short";
            b << "long";
            }
      else if (has(Art::Legato)) {
            b << "legato" << "long";
            w.slurQuick = int(Playback::value("slurs/quick", score));
            w.slurPerformance = !text.performanceOff && Playback::on("slurs/performance", score);
            }
      else
            b << "long";
      return w;
      }

//---------------------------------------------------------
//   trillSemitones
//    to the upper note of the chord's trill: the trill line's accidental if it has one, else
//    the diatonic neighbour in the key (as MS4 plays it)
//---------------------------------------------------------

int trillSemitones(const Note* note)
      {
      const Chord* chord = note->chord();
      const int tick = chord->tick().ticks();
      const Trill* trill = nullptr;
      for (const auto& iv : chord->score()->spannerMap().findOverlapping(tick, tick)) {
            const Spanner* sp = iv.value;
            if (sp->isTrill() && sp->staffIdx() == chord->staffIdx() && sp->tick().ticks() <= tick && tick < sp->tick2().ticks()) {
                  trill = toTrill(sp);
                  break;
                  }
            }
      if (trill && trill->accidental()) {
            static const int NATURAL_PC[7] = { 0, 2, 4, 5, 7, 9, 11 };      // C D E F G A B
            const int tpc = note->tpc1();
            const int step = tpc2step(tpc);
            const int natural = note->pitch() - int(tpc2alter(tpc));
            const int up = (NATURAL_PC[(step + 1) % 7] - NATURAL_PC[step] + 12) % 12;
            const int alter = int(Accidental::subtype2value(trill->accidental()->accidentalType()));
            return natural + up + alter - note->pitch();
            }
      return Ms4::neighbourSemitones(note, 1);
      }

} // namespace SoundLib
} // namespace Ms
