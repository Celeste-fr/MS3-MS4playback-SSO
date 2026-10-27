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
#include <set>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace Ms {
namespace PartPlaybackModes {

const char* const metaTag = "partPlayback";

static const char* const NAMES[] = { "", "ms3", "ms4", "library" };
static const QString LIBRARY_PREFIX("library:");

// a record's mode and its library's name ("library:<name>": LIBRARY and the name)
static PartPlayback modeOf(const QString& text, QString* library = nullptr)
      {
      if (text.startsWith(LIBRARY_PREFIX)) {
            if (library)
                  *library = text.mid(LIBRARY_PREFIX.size());
            return PartPlayback::LIBRARY;
            }
      for (int i = 1; i < 4; ++i)
            if (text == NAMES[i])
                  return PartPlayback(i);
      return PartPlayback::DEFAULT;
      }

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
      for (const QJsonValue& v : list) {
            const QJsonObject o = v.toObject();
            const PartPlayback mode = modeOf(o.value("mode").toString());
            if (mode == PartPlayback::DEFAULT)
                  continue;
            const Part* part = findPart(score, o.value("part").toInt(-1), o.value("name").toString(),
                                        [&modes](const Part* p) { return modes.count(p) > 0; });
            if (part)
                  modes[part] = mode;
            }
      return modes;
      }

//---------------------------------------------------------
//   libraries
//    (read()'s rule for finding the part, the same records)
//---------------------------------------------------------

std::map<const Part*, QString> libraries(const MasterScore* score)
      {
      std::map<const Part*, QString> out;
      if (!score)
            return out;
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty() || !tag.contains(LIBRARY_PREFIX))
            return out;
      std::set<const Part*> found;
      const QJsonArray list = QJsonDocument::fromJson(tag.toUtf8()).array();
      for (const QJsonValue& v : list) {
            const QJsonObject o = v.toObject();
            QString name;
            if (modeOf(o.value("mode").toString(), &name) == PartPlayback::DEFAULT)
                  continue;
            const Part* part = findPart(score, o.value("part").toInt(-1), o.value("name").toString(),
                                        [&found](const Part* p) { return found.count(p) > 0; });
            if (!part)
                  continue;
            found.insert(part);
            if (!name.isEmpty())
                  out[part] = name;
            }
      return out;
      }

QString libraryOf(const Part* part, const std::map<const Part*, QString>& libraries)
      {
      if (libraries.empty())
            return QString();
      auto it = libraries.find(masterPart(part));
      return it == libraries.end() ? QString() : it->second;
      }

QString libraryOf(const Part* part)
      {
      return part && part->score() ? libraryOf(part, libraries(part->score()->masterScore())) : QString();
      }

//---------------------------------------------------------
//   findPart
//---------------------------------------------------------

const Part* findPart(const MasterScore* score, int index, const QString& name, const std::function<bool(const Part*)>& taken)
      {
      const QList<Part*>& parts = score->parts();
      if (index >= 0 && index < parts.size() && parts[index]->partName() == name && !taken(parts[index]))
            return parts[index];
      for (const Part* p : parts)
            if (p->partName() == name && !taken(p))
                  return p;
      if (index >= 0 && index < parts.size() && !taken(parts[index]))
            return parts[index];            // renamed
      return nullptr;
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

QString write(const MasterScore* score, const std::map<const Part*, PartPlayback>& modes)
      {
      return write(score, modes, libraries(score));
      }

QString write(const MasterScore* score, const std::map<const Part*, PartPlayback>& modes,
              const std::map<const Part*, QString>& libraries)
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
            auto lib = libraries.find(parts[i]);
            if (it->second == PartPlayback::LIBRARY && lib != libraries.end() && !lib->second.isEmpty())
                  o["mode"] = LIBRARY_PREFIX + lib->second;
            else
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
