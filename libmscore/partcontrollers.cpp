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

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace Ms {
namespace PartControllers {

const char* const metaTag = "partControllers";

//---------------------------------------------------------
//   read
//---------------------------------------------------------

std::map<const Part*, Values> read(const MasterScore* score)
      {
      std::map<const Part*, Values> values;
      if (!score)
            return values;
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return values;
      for (const QJsonValue& v : QJsonDocument::fromJson(tag.toUtf8()).array()) {
            const QJsonObject o = v.toObject();
            Values vals;
            const QJsonObject vo = o.value("values").toObject();
            for (auto it = vo.begin(); it != vo.end(); ++it) {
                  const int x = it.value().toInt(-1);
                  if (x >= 0 && x <= 127)
                        vals[it.key()] = x;
                  }
            if (vals.empty())
                  continue;
            const Part* part = PartPlaybackModes::findPart(score, o.value("part").toInt(-1), o.value("name").toString(),
                                                           [&values](const Part* p) { return values.count(p) > 0; });
            if (part)
                  values[part] = vals;
            }
      return values;
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

QString write(const MasterScore* score, const std::map<const Part*, Values>& values)
      {
      QJsonArray list;
      const QList<Part*>& parts = score->parts();
      for (int i = 0; i < parts.size(); ++i) {
            auto it = values.find(parts[i]);
            if (it == values.end() || it->second.empty())
                  continue;
            QJsonObject vals;
            for (const auto& v : it->second)
                  vals[v.first] = v.second;
            QJsonObject o;
            o["part"] = i;
            o["name"] = parts[i]->partName();
            o["values"] = vals;
            list.append(o);
            }
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

//---------------------------------------------------------
//   value
//---------------------------------------------------------

int value(const Part* part, const SoundLib::Controller& controller, const std::map<const Part*, Values>& values)
      {
      auto it = values.find(PartPlaybackModes::masterPart(part));
      if (it != values.end()) {
            auto v = it->second.find(controller.id);
            if (v != it->second.end())
                  return v->second;
            }
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
//   liveChanges
//---------------------------------------------------------

std::vector<LiveCc> liveChanges(Score* score, const std::vector<SoundLib::Route>& routes, const Part* part,
                                const Values& before, const Values& after, int tick)
      {
      std::vector<LiveCc> changes;
      const Part* master = PartPlaybackModes::masterPart(part);
      if (!score || !master)
            return changes;
      // the part's main patch: its controllers are the part's (rendermidi: LibPart::controllers)
      const SoundLib::LibInstrument* main = nullptr;
      for (const SoundLib::Route& r : routes)
            if (PartPlaybackModes::masterPart(r.part) == master && r.patch == 0 && r.lane == 0)
                  main = r.instrument;
      if (!main)
            return changes;
      QSet<QString> automated;
      for (const Automation::Lane& lane : Automation::lanes(master, Automation::read(score->masterScore())))
            automated.insert(lane.target);
      const std::map<const Part*, Values> was { { master, before } };
      const std::map<const Part*, Values> now { { master, after } };
      for (const SoundLib::Controller& c : main->allControllers) {
            if (c.cc < 0 || c.cc > 127 || automated.contains(c.id) || automated.contains(QString("cc%1").arg(c.cc)))
                  continue;
            const int from = value(master, c, was);
            const int to = value(master, c, now);
            if (from == to)
                  continue;
            bool text = false;
            valueAt(to, SoundLib::controllerTexts(score, master, c), tick, &text);
            for (const SoundLib::Route& r : routes) {
                  if (PartPlaybackModes::masterPart(r.part) != master || r.instrument->kit)
                        continue;
                  LiveCc l;
                  l.port = r.port;
                  l.channel = r.channel;
                  l.cc = c.cc;
                  l.from = from;
                  l.to = to;
                  l.send = to >= 0 && !text;
                  changes.push_back(l);
                  }
            }
      return changes;
      }

//---------------------------------------------------------
//   LiveOverrides
//---------------------------------------------------------

void LiveOverrides::set(int route, int cc, int from, int to)
      {
      if (cc < 0 || cc > 127)
            return;
      const int key = route * 128 + cc;
      Override& o = _overrides[key];
      if (from >= 0 && from <= 127)
            o.from.set(size_t(from));
      o.to = to;
      if (to >= 0 && to <= 127)
            o.from.reset(size_t(to));           // (events carrying it are right as they are)
      if (o.from.none())
            _overrides.erase(key);
      }

int LiveOverrides::apply(int route, int cc, int value) const
      {
      if (_overrides.empty() || value < 0 || value > 127)
            return value;
      auto i = _overrides.find(route * 128 + cc);
      if (i == _overrides.end() || !i->second.from.test(size_t(value)))
            return value;
      return i->second.to;
      }

}     // namespace PartControllers
}     // namespace Ms
