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
#include "partplayback.h"
#include "part.h"
#include "score.h"

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

std::map<const Part*, Delays> read(const MasterScore* score)
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

double ms(const Delays& d, const QString& patch, const QString& technique)
      {
      double x = d.ms + own(d, trackKey(patch));
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

}     // namespace TrackDelays
}     // namespace Ms
