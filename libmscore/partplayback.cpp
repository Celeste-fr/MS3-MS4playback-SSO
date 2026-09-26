//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "partplayback.h"
#include "part.h"
#include "score.h"
#include "staff.h"

#include <atomic>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ms {
namespace PartPlaybackModes {

const char* const metaTag = "partPlayback";

static const char* const NAMES[] = { "", "ms3", "ms4", "library" };

//---------------------------------------------------------
//   read
//---------------------------------------------------------

std::map<const Part*, PartPlayback> read(const MasterScore* score)
      {
      std::map<const Part*, PartPlayback> modes;
      if (!score)
            return modes;
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return modes;
      const QJsonArray list = QJsonDocument::fromJson(tag.toUtf8()).array();
      const QList<Part*>& parts = score->parts();
      for (const QJsonValue& v : list) {
            const QJsonObject o = v.toObject();
            PartPlayback mode = PartPlayback::DEFAULT;
            for (int i = 1; i < 4; ++i)
                  if (o.value("mode").toString() == NAMES[i])
                        mode = PartPlayback(i);
            if (mode == PartPlayback::DEFAULT)
                  continue;
            const int index = o.value("part").toInt(-1);
            const QString name = o.value("name").toString();
            const Part* part = nullptr;
            if (index >= 0 && index < parts.size() && parts[index]->partName() == name && !modes.count(parts[index]))
                  part = parts[index];
            for (int i = 0; !part && i < parts.size(); ++i)
                  if (parts[i]->partName() == name && !modes.count(parts[i]))
                        part = parts[i];
            if (!part && index >= 0 && index < parts.size() && !modes.count(parts[index]))
                  part = parts[index];            // renamed
            if (part)
                  modes[part] = mode;
            }
      return modes;
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

QString write(const MasterScore* score, const std::map<const Part*, PartPlayback>& modes)
      {
      QJsonArray list;
      const QList<Part*>& parts = score->parts();
      for (int i = 0; i < parts.size(); ++i) {
            auto it = modes.find(parts[i]);
            if (it == modes.end() || it->second == PartPlayback::DEFAULT)
                  continue;
            QJsonObject o;
            o["part"] = i;
            o["name"] = parts[i]->partName();
            o["mode"] = NAMES[int(it->second)];
            list.append(o);
            }
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

//---------------------------------------------------------
//   masterPart
//---------------------------------------------------------

const Part* masterPart(const Part* part)
      {
      if (!part || !part->score() || part->score()->isMaster() || part->staves()->isEmpty())
            return part;
      const Staff* staff = part->staff(0);
      if (staff->links()) {
            for (ScoreElement* e : *staff->links()) {
                  const Staff* s = static_cast<const Staff*>(e);
                  if (s->score() && s->score()->isMaster())
                        return s->part();
                  }
            }
      return part;
      }

//---------------------------------------------------------
//   of
//---------------------------------------------------------

PartPlayback of(const Part* part, const std::map<const Part*, PartPlayback>& modes)
      {
      auto it = modes.find(masterPart(part));
      return it == modes.end() ? PartPlayback::DEFAULT : it->second;
      }

PartPlayback of(const Part* part)
      {
      return part && part->score() ? of(part, read(part->score()->masterScore())) : PartPlayback::DEFAULT;
      }

//---------------------------------------------------------
//   the global mode's sound library
//---------------------------------------------------------

static std::atomic<bool> libraryOn { false };

void setLibraryDefault(bool on)
      {
      libraryOn = on;
      }

bool libraryDefault()
      {
      return libraryOn;
      }

bool playsLibrary(const Part* part, const std::map<const Part*, PartPlayback>& modes)
      {
      const PartPlayback mode = of(part, modes);
      return mode == PartPlayback::LIBRARY || (mode == PartPlayback::DEFAULT && libraryOn);
      }

}     // namespace PartPlaybackModes
}     // namespace Ms
