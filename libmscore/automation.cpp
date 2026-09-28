//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "automation.h"
#include "partplayback.h"
#include "part.h"
#include "score.h"

#include <algorithm>
#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ms {
namespace Automation {

const char* const metaTag = "automation";
const char* const SOURCE_LIVE = "live";

//---------------------------------------------------------
//   Lane
//---------------------------------------------------------

QString Lane::source() const
      {
      return extra.value("source").toString();
      }

int Lane::cc() const
      {
      if (!target.startsWith("cc"))
            return -1;
      bool ok = false;
      const int n = target.mid(2).toInt(&ok);
      return ok && n >= 0 && n <= 127 ? n : -1;
      }

double Lane::valueAt(int tick) const
      {
      if (points.empty() || tick < points.front().tick)
            return -1;
      auto next = std::upper_bound(points.begin(), points.end(), Point { tick, 0, Curve::STEP });
      const Point& p = *std::prev(next);
      if (next == points.end() || p.curve == Curve::STEP || next->tick == p.tick)
            return p.value;
      return p.value + (next->value - p.value) * double(tick - p.tick) / double(next->tick - p.tick);
      }

std::vector<std::pair<int, double>> Lane::events(int tick1, int tick2, int stepTicks, double resolution) const
      {
      std::vector<std::pair<int, double>> out;
      if (points.empty() || tick2 <= tick1)
            return out;
      double last = valueAt(tick1);
      if (last >= 0)
            out.push_back({ tick1, last });
      for (size_t i = 0; i < points.size(); ++i) {
            const Point& p = points[i];
            if (p.tick >= tick2)
                  break;
            if (p.tick >= tick1 && p.tick > (out.empty() ? tick1 - 1 : out.back().first)) {
                  out.push_back({ p.tick, p.value });
                  last = p.value;
                  }
            // along a ramp to the next point, inside [tick1, tick2)
            if (p.curve == Curve::LINEAR && i + 1 < points.size() && stepTicks > 0) {
                  const Point& n = points[i + 1];
                  for (int t = std::max(p.tick, tick1) + stepTicks; t < std::min(n.tick, tick2); t += stepTicks) {
                        const double v = valueAt(t);
                        if (std::fabs(v - last) >= resolution) {
                              out.push_back({ t, v });
                              last = v;
                              }
                        }
                  }
            }
      return out;
      }

//---------------------------------------------------------
//   read / write
//---------------------------------------------------------

std::map<const Part*, PartLanes> read(const MasterScore* score)
      {
      std::map<const Part*, PartLanes> all;
      if (!score)
            return all;
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return all;
      for (const QJsonValue& v : QJsonDocument::fromJson(tag.toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            PartLanes lanes;
            for (const QJsonValue& lv : o.value("lanes").toArray()) {
                  const QJsonObject lo = lv.toObject();
                  Lane lane;
                  lane.target = lo.value("target").toString();
                  if (lane.target.isEmpty())
                        continue;
                  lane.extra = lo;
                  lane.extra.remove("target");
                  lane.extra.remove("points");
                  for (const QJsonValue& pv : lo.value("points").toArray()) {
                        const QJsonArray pa = pv.toArray();
                        if (pa.size() < 2)
                              continue;
                        Point p;
                        p.tick = pa.at(0).toInt();
                        p.value = std::min(1.0, std::max(0.0, pa.at(1).toDouble()));
                        p.curve = pa.size() > 2 && pa.at(2).toString() == "linear" ? Curve::LINEAR : Curve::STEP;
                        lane.points.push_back(p);
                        }
                  std::stable_sort(lane.points.begin(), lane.points.end());
                  if (!lane.points.empty())
                        lanes.push_back(lane);
                  }
            if (lanes.empty())
                  continue;
            const Part* part = PartPlaybackModes::findPart(score, o.value("part").toInt(-1), o.value("name").toString(),
                                                           [&all](const Part* p) { return all.count(p) > 0; });
            if (part)
                  all[part] = lanes;
            }
      return all;
      }

QString write(const MasterScore* score, const std::map<const Part*, PartLanes>& lanes)
      {
      QJsonArray list;
      const QList<Part*>& parts = score->parts();
      for (int i = 0; i < parts.size(); ++i) {
            auto it = lanes.find(parts[i]);
            if (it == lanes.end() || it->second.empty())
                  continue;
            QJsonArray la;
            for (const Lane& lane : it->second) {
                  if (lane.points.empty())
                        continue;
                  QJsonArray pts;
                  for (const Point& p : lane.points)
                        pts.append(QJsonArray({ p.tick, std::round(p.value * 10000) / 10000,
                                                p.curve == Curve::LINEAR ? "linear" : "step" }));
                  QJsonObject lo = lane.extra;
                  lo["target"] = lane.target;
                  lo["points"] = pts;
                  la.append(lo);
                  }
            if (la.isEmpty())
                  continue;
            QJsonObject o;
            o["part"] = i;
            o["name"] = parts[i]->partName();
            o["lanes"] = la;
            list.append(o);
            }
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

PartLanes lanes(const Part* part, const std::map<const Part*, PartLanes>& all)
      {
      auto it = all.find(PartPlaybackModes::masterPart(part));
      return it == all.end() ? PartLanes() : it->second;
      }

std::map<const Part*, PartLanes> replaceSource(const std::map<const Part*, PartLanes>& all, const QString& source,
                                               const std::map<const Part*, PartLanes>& with)
      {
      std::map<const Part*, PartLanes> out;
      for (const auto& pl : all)
            for (const Lane& l : pl.second)
                  if (l.source() != source)
                        out[pl.first].push_back(l);
      for (const auto& pl : with)
            for (const Lane& l : pl.second)
                  out[pl.first].push_back(l);
      for (auto i = out.begin(); i != out.end(); )
            i = i->second.empty() ? out.erase(i) : std::next(i);
      return out;
      }

}     // namespace Automation
}     // namespace Ms
