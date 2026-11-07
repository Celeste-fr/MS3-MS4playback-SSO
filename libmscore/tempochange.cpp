//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "tempochange.h"
#include "changeMap.h"
#include "measure.h"
#include "score.h"
#include "segment.h"
#include "spannermap.h"
#include "tempo.h"
#include "tempotext.h"
#include "textline.h"

#include <cmath>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace Ms {
namespace TempoChange {

const char* const metaTag = "tempoChanges";

// a point of the tempo map every 32nd
static const int STEP = DIVISION / 8;

//---------------------------------------------------------
//   defaultFactor
//    MuseScore 4's defaults per term (GradualTempoChange), in % of the start tempo
//---------------------------------------------------------

double defaultFactor(const QString& text)
      {
      QString t = text;
      t.remove(QRegularExpression("<[^>]*>"));
      t = t.trimmed().toLower();
      static const std::pair<const char*, double> TERMS[] = {
            { "accel", 133 }, { "allarg", 75 }, { "calando", 50 }, { "lentando", 75 },
            { "morendo", 50 }, { "precip", 115 }, { "rall", 75 }, { "rit", 75 },
            { "smorz", 50 }, { "sostenuto", 95 }, { "string", 150 },
            };
      for (const auto& term : TERMS)
            if (t.startsWith(term.first))
                  return term.second;
      return 0.0;
      }

bool isTempoChange(const TextLine* line)
      {
      return line && (line->tempoChangeFactor() > 0.0 || defaultFactor(line->beginText()) > 0.0);
      }

double factor(const TextLine* line)
      {
      return line->tempoChangeFactor() > 0.0 ? line->tempoChangeFactor() : defaultFactor(line->beginText());
      }

//---------------------------------------------------------
//   curve
//    the hairpins' shapes (ChangeMap::interpolate), for x from 0 to 1
//---------------------------------------------------------

double curve(ChangeMethod method, double x)
      {
      switch (method) {
            case ChangeMethod::EASE_IN:
                  return 1.0 - std::cos(x * M_PI / 2.0);
            case ChangeMethod::EASE_OUT:
                  return std::sin(x * M_PI / 2.0);
            case ChangeMethod::EASE_IN_OUT:
                  return (1.0 - std::cos(x * M_PI)) / 2.0;
            case ChangeMethod::EXPONENTIAL:           // geometric in tempo (addToTempoMap)
            case ChangeMethod::NORMAL:
                  break;
            }
      return x;
      }

//---------------------------------------------------------
//   startTempo
//    the tempo in force where the line starts, as the whole rebuild of the tempo map gives it:
//    the measure being rebuilt (from) has its tempo markings added after the lines' points
//---------------------------------------------------------

static qreal startTempo(Score* score, int tick, const Fraction& from)
      {
      const TempoMap* map = score->tempomap();
      int mapTick = -1;
      auto it = map->upper_bound(tick);
      if (it != map->begin())
            mapTick = std::prev(it)->first;
      qreal tempo = map->tempo(tick);
      if (tick >= from.ticks()) {
            // the tempo markings of this measure up to the tick, not in the map yet
            Measure* m = score->tick2measure(from);
            for (Segment* s = m ? m->first(SegmentType::ChordRest) : nullptr; s && s->tick().ticks() <= tick; s = s->next(SegmentType::ChordRest)) {
                  for (Element* e : s->annotations()) {
                        if (e->isTempoText() && s->tick().ticks() >= mapTick) {
                              tempo = toTempoText(e)->tempo();
                              mapTick = s->tick().ticks();
                              }
                        }
                  }
            }
      return tempo;
      }

//---------------------------------------------------------
//   addToTempoMap
//---------------------------------------------------------

void addToTempoMap(Score* score, const Fraction& from, const Fraction& to)
      {
      if (!score->isMaster())
            return;
      std::vector<TextLine*> lines;
      for (const auto& i : score->spannerMap().findOverlapping(from.ticks(), to.ticks())) {
            Spanner* s = i.value;
            if (s->isTextLine() && s->tick2() > s->tick() && s->tick2() >= from && s->tick() < to
                && isTempoChange(toTextLine(s)) && std::find(lines.begin(), lines.end(), s) == lines.end())
                  lines.push_back(toTextLine(s));
            }
      std::sort(lines.begin(), lines.end(), [](const TextLine* a, const TextLine* b) { return a->tick() < b->tick(); });

      for (const TextLine* line : lines) {
            const int t1 = line->tick().ticks();
            const int t2 = line->tick2().ticks();
            const qreal start = startTempo(score, t1, from);
            const qreal end = start * factor(line) / 100.0;
            const ChangeMethod method = line->tempoChangeMethod();
            int k = t1 >= from.ticks() ? 0 : (from.ticks() - t1 + STEP - 1) / STEP;
            for (int tick = t1 + k * STEP; tick < t2 && tick < to.ticks(); tick = t1 + (++k) * STEP) {
                  const double x = double(tick - t1) / double(t2 - t1);
                  const qreal tempo = method == ChangeMethod::EXPONENTIAL ? start * std::pow(end / start, x)
                                                                          : start + (end - start) * curve(method, x);
                  score->setTempo(Fraction::fromTicks(tick), tempo);
                  }
            // the end tempo stays (a tempo marking there takes over: it is added after this)
            if (t2 >= from.ticks() && t2 < to.ticks())
                  score->setTempo(Fraction::fromTicks(t2), end);
            }
      }

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void read(MasterScore* score)
      {
      const QString tag = score->metaTag(metaTag);
      if (tag.isEmpty())
            return;
      score->metaTags().remove(metaTag);        // written again from the lines on saving
      const QJsonArray list = QJsonDocument::fromJson(tag.toUtf8()).array();
      for (const QJsonValue& v : list) {
            const QJsonObject o = v.toObject();
            const int tick = o.value("tick").toInt(-1);
            const int tick2 = o.value("tick2").toInt(-1);
            const int track = o.value("track").toInt(-1);
            TextLine* found = nullptr;
            for (auto it = score->spannerMap().map().lower_bound(tick); it != score->spannerMap().map().end() && it->first == tick; ++it) {
                  Spanner* s = it->second;
                  if (!s->isTextLine() || s->track() != track)
                        continue;
                  if (!found || s->tick2().ticks() == tick2)
                        found = toTextLine(s);
                  }
            if (!found)
                  continue;
            found->setProperty(Pid::TEMPO_CHANGE_FACTOR, o.value("factor").toDouble(0.0));
            found->setProperty(Pid::TEMPO_CHANGE_METHOD, o.value("method").toInt(0));
            }
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

QString write(const Score* score)
      {
      QJsonArray list;
      for (const auto& i : score->spannerMap().map()) {
            const Spanner* s = i.second;
            if (!s->isTextLine())
                  continue;
            const TextLine* line = toTextLine(s);
            if (line->tempoChangeFactor() <= 0.0 && line->tempoChangeMethod() == ChangeMethod::NORMAL)
                  continue;
            QJsonObject o;
            o["tick"] = line->tick().ticks();
            o["tick2"] = line->tick2().ticks();
            o["track"] = line->track();
            o["factor"] = line->tempoChangeFactor();
            o["method"] = int(line->tempoChangeMethod());
            list.append(o);
            }
      return list.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
      }

}     // namespace TempoChange
}     // namespace Ms
