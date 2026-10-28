//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "trackdelays.h"
#include "contextlevel.h"
#include "evenlevels.h"
#include "playbacksettings.h"
#include "partplayback.h"
#include "part.h"
#include "score.h"
#include "soundlibrary.h"
#include "audio/midi/event.h"

#include <algorithm>
#include <cmath>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ms {
namespace TrackDelays {

const char* const metaTag = "trackDelays";

static bool zero(double ms)
      {
      return std::fabs(ms) < 1e-6;
      }

bool Delays::empty() const
      {
      if (!zero(ms))
            return false;
      for (const auto& t : tracks)
            if (!zero(t.second))
                  return false;
      for (const auto& l : levels)
            if (!zero(l.second))
                  return false;
      return true;
      }

QString trackKey(const QString& patch, const QString& technique)
      {
      return technique.isEmpty() ? patch : patch + " / " + technique;
      }

double clampMs(double ms)
      {
      if (!std::isfinite(ms))
            return 0.0;
      return std::max(MIN_MS, std::min(MAX_MS, ms));
      }

double clampDb(double db)
      {
      if (!std::isfinite(db))
            return 0.0;
      return std::max(MIN_DB, std::min(MAX_DB, db));
      }

//---------------------------------------------------------
//   read
//---------------------------------------------------------

static std::map<const Part*, Delays> readTag(const MasterScore* score)
      {
      std::map<const Part*, Delays> delays;
      if (!score)
            return delays;
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return delays;
      for (const QJsonValue& v : QJsonDocument::fromJson(tag.toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            Delays d;
            d.ms = clampMs(o.value("ms").toDouble(0.0));
            const QJsonObject to = o.value("tracks").toObject();
            for (auto it = to.begin(); it != to.end(); ++it) {
                  const double x = clampMs(it.value().toDouble(0.0));
                  if (!zero(x))
                        d.tracks[it.key()] = x;
                  }
            const QJsonObject lo = o.value("levels").toObject();
            for (auto it = lo.begin(); it != lo.end(); ++it) {
                  const double x = clampDb(it.value().toDouble(0.0));
                  if (!zero(x))
                        d.levels[it.key()] = x;
                  }
            if (d.empty())
                  continue;
            const Part* part = PartPlaybackModes::findPart(score, o.value("part").toInt(-1), o.value("name").toString(),
                                                           [&delays](const Part* p) { return delays.count(p) > 0; });
            if (part)
                  delays[part] = d;
            }
      return delays;
      }

std::map<const Part*, Delays> read(const MasterScore* score)
      {
      std::map<const Part*, Delays> delays = readTag(score);
      for (const auto& v : ContextLevel::volumes(score)) {
            Delays& d = delays[v.first];
            d.rule = v.second.patchDb;
            d.ruleTop = v.second.top;
            d.rangeTop = v.second.range;
            d.micTop = v.second.micTop;
            d.shift = v.second.shift;
            }
      // [levels] evenLevels: each patch's largest correction on its gain stage, its notes that much lower. (evenAll: the
      // old volumes' rest that didn't fit under MAX_DB, legacyPatchGain only)
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (library && library->dynamicsCC != CTRL_EXPRESSION && Playback::on("levels/evenLevels", score)) {
            double rest = 0.0;
            for (const auto& pt : EvenLevels::tops(score)) {
                  for (const auto& t : pt.second) {
                        if (t.second <= 0.0)
                              continue;
                        Delays& d = delays[pt.first];
                        auto r = d.rule.find(t.first);
                        const double now = std::max(MIN_DB, std::min(MAX_DB, headroomDb(d, t.first) + (r == d.rule.end() ? 0.0 : r->second) - d.shift));
                        d.even[t.first] = t.second;
                        rest = std::max(rest, t.second - (MAX_DB - now));
                        }
                  }
            if (rest > 0.0) {
                  for (const Part* part : score->parts())
                        delays[part].evenAll = -rest;
                  }
            }
      // [slurs] plainPerformance: each plain Performance patch's measured shift (the map's plainShift), early; not with
      // [slurs] plainTiming, whose notes start early each by its own model (the renderer's early starts)
      if (library && !Playback::on("slurs/plainTiming", score)) {
            for (const auto& pp : SoundLib::plainPerformancePatches(score, *library))
                  for (const auto& p : pp.second)
                        if (std::isfinite(p.second) && p.second > 0.0)
                              delays[pp.first].plain[p.first] = -clampMs(p.second);
            }
      // the master trim (0 while it is being computed: ContextLevel::masterTrim renders the score with these gains)
      const double trim = ContextLevel::masterTrim(score).db;
      if (trim != 0.0)
            for (const Part* part : score->parts())
                  delays[part].trim = trim;
      return delays;
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

QString write(const MasterScore* score, const std::map<const Part*, Delays>& delays)
      {
      QJsonArray list;
      const QList<Part*>& parts = score->parts();
      for (int i = 0; i < parts.size(); ++i) {
            auto it = delays.find(parts[i]);
            if (it == delays.end() || it->second.empty())
                  continue;
            QJsonObject o;
            o["part"] = i;
            o["name"] = parts[i]->partName();
            if (!zero(it->second.ms))
                  o["ms"] = clampMs(it->second.ms);
            QJsonObject tracks;
            for (const auto& t : it->second.tracks)
                  if (!zero(t.second))
                        tracks[t.first] = clampMs(t.second);
            if (!tracks.isEmpty())
                  o["tracks"] = tracks;
            QJsonObject levels;
            for (const auto& l : it->second.levels)
                  if (!zero(l.second))
                        levels[l.first] = clampDb(l.second);
            if (!levels.isEmpty())
                  o["levels"] = levels;
            list.append(o);
            }
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

Delays of(const Part* part, const std::map<const Part*, Delays>& delays)
      {
      auto it = delays.find(PartPlaybackModes::masterPart(part));
      return it == delays.end() ? Delays() : it->second;
      }

double own(const Delays& d, const QString& key)
      {
      auto it = d.tracks.find(key);
      return it == d.tracks.end() ? 0.0 : it->second;
      }

double plainMs(const Delays& d, const QString& patch)
      {
      auto it = d.plain.find(patch);
      return it == d.plain.end() ? 0.0 : it->second;
      }

double ms(const Delays& d, const QString& patch, const QString& technique)
      {
      double x = d.ms + own(d, trackKey(patch)) + plainMs(d, patch);
      if (!technique.isEmpty())
            x += own(d, trackKey(patch, technique));
      return x;
      }

double earliest(const Delays& d)
      {
      double x = d.ms;
      for (const auto& t : d.tracks) {
            const int sep = t.first.indexOf(" / ");
            x = std::min(x, sep < 0 ? ms(d, t.first) : ms(d, t.first.left(sep), t.first.mid(sep + 3)));
            }
      for (const auto& p : d.plain)
            x = std::min(x, ms(d, p.first));
      return x;
      }

double ownDb(const Delays& d, const QString& key)
      {
      auto it = d.levels.find(key);
      return it == d.levels.end() ? 0.0 : it->second;
      }

double db(const Delays& d, const QString& patch, const QString& technique)
      {
      return ownDb(d, trackKey(patch)) + ownDb(d, trackKey(patch, technique));
      }

double headroomDb(const Delays& d, const QString& patch)
      {
      const double own = ownDb(d, trackKey(patch));
      const QString techniques = trackKey(patch, "-").chopped(1);         // "<patch> / "
      double top = own;                                                   // (a technique with no level of its own)
      for (const auto& l : d.levels)
            if (l.first.startsWith(techniques))
                  top = std::max(top, own + l.second);
      return std::max(0.0, std::min(MAX_DB, top));
      }

double noteDb(const Delays& d, const QString& patch, const QString& technique)
      {
      return std::max(MIN_DB, std::min(MAX_DB, db(d, patch, technique))) - headroomDb(d, patch);
      }

static double ruleDb(const Delays& d, const QString& patch)
      {
      auto r = d.rule.find(patch);
      return r == d.rule.end() ? 0.0 : r->second;
      }

double faderGain(const SoundLib::Library& library, const Delays& d, const QString& patch)
      {
      if (library.dynamicsCC == CTRL_EXPRESSION)
            return 1.0;
      return std::pow(10.0, headroomDb(d, patch) / 20.0);
      }

double gainStageDb(const SoundLib::Library& library, const Delays& d, const QString& patch)
      {
      if (library.dynamicsCC == CTRL_EXPRESSION)
            return 0.0;
      return ruleDb(d, patch) + evenDb(d, patch) - d.trim;
      }

double patchGain(const SoundLib::Library& library, const Delays& d, const QString& patch)
      {
      return faderGain(library, d, patch) * std::pow(10.0, gainStageDb(library, d, patch) / 20.0);
      }

double legacyPatchGain(const SoundLib::Library& library, const Delays& d, const QString& patch)
      {
      if (library.dynamicsCC == CTRL_EXPRESSION)
            return 1.0;
      const double volume = std::max(MIN_DB, std::min(MAX_DB, headroomDb(d, patch) + ruleDb(d, patch) - d.shift));
      return std::pow(10.0, (volume + evenDb(d, patch) + d.evenAll) / 20.0);
      }

double evenDb(const Delays& d, const QString& patch)
      {
      auto e = d.even.find(patch);
      return e == d.even.end() ? 0.0 : e->second;
      }

std::map<int, int> midiVolumes(const MasterScore* score, const SoundLib::Library& library, bool withSolo)
      {
      std::map<int, int> out;
      if (!score || library.dynamicsCC == CTRL_EXPRESSION)
            return out;
      const std::map<const Part*, Delays> delays = read(score);
      std::map<int, std::pair<double, bool>> want;      // route -> its gain, the Mixer's included (dB); its patch's gain isn't 1
      double over = 0.0;
      bool any = false;
      const double top = 40.0 * std::log10(1.27);                         // CC7 127: (127 / 100)², +4.15 dB
      for (const SoundLib::Route& r : SoundLib::liveRoutes(score, library)) {
            const double gain = patchGain(r.library ? *r.library : library, of(r.part, delays), r.instrument->name);
            const int v = qBound(0, SoundLib::partMix(r.part, withSolo).volume, 127);
            if (v == 0 || !(gain > 0.0))
                  continue;
            const bool own = std::fabs(gain - 1.0) > 1e-9;
            const double db = 40.0 * std::log10(v / 100.0) + 20.0 * std::log10(gain);
            want[r.port * 16 + r.channel] = { db, own };
            over = std::max(over, db - top);
            any = any || own;
            }
      if (!any)
            return out;
      for (const auto& w : want)
            if (w.second.second || over > 0.0)
                  out[w.first] = qBound(0, int(std::lround(100.0 * std::pow(10.0, (w.second.first - over) / 40.0))), 127);
      return out;
      }

}     // namespace TrackDelays
}     // namespace Ms
