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
#include "partplayback.h"
#include "part.h"
#include "score.h"
#include "soundlibrary.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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

}     // namespace PartControllers
}     // namespace Ms
