//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "partcontrollers.h"
#include "automation.h"
#include "partplayback.h"
#include "part.h"
#include "score.h"
#include "soundlibrary.h"

#include <algorithm>
#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace Ms {
namespace PartControllers {

const char* const metaTag = "partControllers";

//---------------------------------------------------------
//   migrate
//---------------------------------------------------------

bool migrate(MasterScore* score)
      {
      if (!score)
            return false;
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return false;
      std::map<const Part*, Automation::PartLanes> all = Automation::read(score);
      std::map<const Part*, bool> taken;
      for (const QJsonValue& v : QJsonDocument::fromJson(tag.toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            const Part* part = PartPlaybackModes::findPart(score, o.value("part").toInt(-1), o.value("name").toString(),
                                                           [&taken](const Part* p) { return taken.count(p) > 0; });
            if (!part)
                  continue;         // (a part no longer in the score: its values went with it, as they were never played)
            taken[part] = true;
            Automation::PartLanes& lanes = all[part];
            const QJsonObject vo = o.value("values").toObject();
            for (auto it = vo.begin(); it != vo.end(); ++it) {
                  const int x = it.value().toInt(-1);
                  if (x < 0 || x > 127)
                        continue;
                  bool has = false;
                  for (const Automation::Lane& l : lanes)
                        has = has || (l.target == it.key() && !l.points.empty());
                  if (has)
                        continue;         // (its lane played over the value)
                  lanes.erase(std::remove_if(lanes.begin(), lanes.end(), [&it](const Automation::Lane& l) {
                        return l.target == it.key();
                        }), lanes.end());
                  Automation::Lane lane;
                  lane.target = it.key();
                  lane.points.push_back(Automation::Point(0, x / 127.0, Automation::Curve::STEP));
                  lanes.push_back(lane);
                  }
            }
      QMap<QString, QString> tags = score->metaTags();
      tags.remove(metaTag);
      const QString lanes = Automation::write(score, all);
      if (lanes.isEmpty())
            tags.remove(Automation::metaTag);
      else
            tags.insert(Automation::metaTag, lanes);
      score->setMetaTags(tags);
      return true;
      }

//---------------------------------------------------------
//   laneOf / flatLane
//---------------------------------------------------------

const Automation::Lane* laneOf(const Automation::PartLanes& lanes, const SoundLib::Controller& controller)
      {
      const QString cc = controller.cc >= 0 ? QString("cc%1").arg(controller.cc) : QString();
      for (const Automation::Lane& l : lanes)
            if (!l.points.empty() && (l.target == controller.id || (!cc.isEmpty() && l.target == cc)))
                  return &l;
      return nullptr;
      }

const Automation::Lane* flatLane(const Automation::PartLanes& lanes, const SoundLib::Controller& controller)
      {
      const Automation::Lane* l = laneOf(lanes, controller);
      return l && l->flatValue() >= 0 ? l : nullptr;
      }

//---------------------------------------------------------
//   toScale
//---------------------------------------------------------

double toScale(double laneValue)
      {
      const double v = 127.0 * std::min(1.0, std::max(0.0, laneValue));
      const double r = std::round(v);
      return std::fabs(v - r) <= 127.0 * 0.5e-4 + 1e-9 ? r : v;
      }

//---------------------------------------------------------
//   value
//---------------------------------------------------------

double value(const Part* part, const SoundLib::Controller& controller, const std::map<const Part*, Automation::PartLanes>& lanes)
      {
      const Automation::PartLanes own = Automation::lanes(part, lanes);
      if (const Automation::Lane* l = flatLane(own, controller))
            return toScale(l->flatValue());
      return controller.defaultValue;
      }

//---------------------------------------------------------
//   valueAt
//---------------------------------------------------------

int valueAt(int partValue, const std::map<int, int>& texts, int tick, bool* fromText)
      {
      auto t = texts.upper_bound(tick);
      if (fromText)
            *fromText = t != texts.begin();
      return t == texts.begin() ? partValue : std::prev(t)->second;
      }

//---------------------------------------------------------
//   unautomatedValue
//---------------------------------------------------------

static std::function<double(const QString&, const QString&)>& setupValues()
      {
      static std::function<double(const QString&, const QString&)> f;
      return f;
      }

void setSetupValues(std::function<double(const QString& patch, const QString& param)> lookup)
      {
      setupValues() = lookup;
      }

double unautomatedValue(const SoundLib::LibInstrument& patch, const SoundLib::Controller& controller)
      {
      if (controller.defaultValue >= 0)
            return controller.defaultValue / 127.0;
      if (controller.isState())                 // (a state setting: the value shipped, the plug-in's default state)
            return controller.patchDefault;
      if (controller.param.isEmpty())
            return -1;
      // the patch as it loaded on this computer (Kontakt's live value, as Live's panel shows it once loaded)
      if (setupValues()) {
            const double v = setupValues()(patch.name, controller.param);
            if (v >= 0)
                  return v;
            }
      // never loaded here: the value shipped with the map (measured in the real plug-in)
      if (controller.patchDefault >= 0)
            return controller.patchDefault;
      static const QRegularExpression mic("^Mic ([1-5]) level$");
      const QRegularExpressionMatch m = mic.match(controller.param);
      if (m.hasMatch() && patch.hasMicLevels)
            return patch.micDefault[size_t(m.captured(1).toInt() - 1)] / 127.0;
      return -1;
      }

//---------------------------------------------------------
//   stateValue / stateValues
//---------------------------------------------------------

double stateValue(const Part* part, const SoundLib::Controller& controller, const std::map<const Part*, Automation::PartLanes>& lanes)
      {
      const Automation::PartLanes own = Automation::lanes(part, lanes);
      if (const Automation::Lane* l = laneOf(own, controller))
            if (!l->points.empty())
                  return std::min(1.0, std::max(0.0, l->points.front().value));
      return controller.defaultValue >= 0 ? controller.defaultValue / 127.0 : -1.0;
      }

std::map<QString, QByteArray> stateValues(const Part* part, const SoundLib::LibInstrument& patch,
                                          const std::map<const Part*, Automation::PartLanes>& lanes)
      {
      std::map<QString, QByteArray> out;
      for (const SoundLib::Controller& c : patch.allControllers) {
            if (!c.isState() || c.stateValues.isEmpty())
                  continue;
            const double v = stateValue(part, c, lanes);
            if (v < 0)
                  continue;
            const int n = c.stateValues.size();
            const int k = std::min(n - 1, std::max(0, int(std::lround(v * (n - 1)))));
            out[c.state] = c.stateValues[k].toUtf8();
            }
      return out;
      }

}     // namespace PartControllers
}     // namespace Ms
