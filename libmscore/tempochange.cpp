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
#include "playbacksettings.h"
#include "sig.h"
#include "slur.h"
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
//   phrase marks' Endings (slur.h › Phrase shape and ending)
//---------------------------------------------------------

struct Ending {
      int t1;           // the slowing starts
      int t2;           // the last note's end: the tempo in force before comes back
      double factor;    // the end tempo, % of the start
      };

// the slur's Ending: over the setting's beats before its last note's end, not before its start; factor 100: none
static Ending endingOf(Score* score, const Slur* slur)
      {
      const int end = PhraseMark::endTick(slur);
      const int beat = score->sigmap()->timesig(slur->tick2()).timesig().beatTicks();
      const double beats = Playback::value("phrasing/endingLength", score);
      const int t1 = std::max(slur->tick().ticks(), end - int(std::lround(beats * beat)));
      const double factor = slur->phraseEnding() == PhraseMark::ENDING_BROAD ? Playback::value("phrasing/endingBroad", score)
                                                                               : Playback::value("phrasing/endingSlight", score);
      return { t1, end, factor };
      }

// a written tempo change in [t1, t2): a rit. / accel. line overlapping it, or a tempo marking after its start
static bool writtenTempoIn(Score* score, int t1, int t2)
      {
      const auto spanners = score->spannerMap().findOverlapping(t1, t2);
      for (const auto& i : spanners) {
            const Spanner* s = i.value;
            if (s->isTextLine() && s->tick2() > s->tick() && s->tick2().ticks() > t1 && s->tick().ticks() < t2
                && isTempoChange(toTextLine(s)))
                  return true;
            }
      Measure* m = score->tick2measure(Fraction::fromTicks(t1));
      for (Segment* seg = m ? m->first(SegmentType::ChordRest) : nullptr; seg && seg->tick().ticks() < t2; seg = seg->next1(SegmentType::ChordRest)) {
            if (seg->tick().ticks() <= t1)
                  continue;
            for (Element* e : seg->annotations())
                  if (e->isTempoText())
                        return true;
            }
      return false;
      }

// an Ending lies within its phrase's [tick, last note's end]: a phrase whose last note ends at or after a tick starts
// no more than its last chord's length before it (searched this far back)
static const int LOOKBACK = 16 * DIVISION;

// the Endings sounding in [from, to) or ending at from (the tempo comes back there), each with those it overlaps in time joined into one (any staff: the tempo is
// the score's): the earliest start, the latest end, the slowest end tempo
static std::vector<Ending> endingsIn(Score* score, int from, int to)
      {
      std::vector<const Slur*> seen;
      std::vector<Ending> endings;
      auto collect = [&](int a, int b) {
            const auto spanners = score->spannerMap().findOverlapping(std::max(0, a - LOOKBACK), b);
            bool added = false;
            for (const auto& i : spanners) {
                  const Spanner* s = i.value;
                  if (!s->isSlur() || !PhraseMark::hasEnding(toSlur(s)) || std::find(seen.begin(), seen.end(), s) != seen.end())
                        continue;
                  const Ending e = endingOf(score, toSlur(s));
                  if (e.t2 <= e.t1 || e.t2 < a || e.t1 >= b)
                        continue;
                  seen.push_back(toSlur(s));
                  endings.push_back(e);
                  added = true;
                  }
            return added;
            };
      collect(from, to);
      // (an Ending reaching past [from, to) may overlap others there: the same joined Ending in every measure's rebuild)
      for (bool more = !endings.empty(); more; ) {
            more = false;
            for (size_t k = 0; k < endings.size() && !more; ++k)
                  more = collect(endings[k].t1, endings[k].t2);
            }
      std::sort(endings.begin(), endings.end(), [](const Ending& a, const Ending& b) { return a.t1 < b.t1; });
      std::vector<Ending> joined;
      for (const Ending& e : endings) {
            if (!joined.empty() && e.t1 < joined.back().t2) {
                  joined.back().t2 = std::max(joined.back().t2, e.t2);
                  joined.back().factor = std::min(joined.back().factor, e.factor);
                  }
            else
                  joined.push_back(e);
            }
      return joined;
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

      // phrase marks' Endings (slur.h › Phrase shape and ending): as a rit. line over the Ending, then the tempo in force
      // before it again; none where a written tempo change overlaps it (the written one wins)
      const std::vector<Ending> endings = endingsIn(score, from.ticks(), to.ticks());
      if (endings.empty())
            return;
      const ChangeMethod method = ChangeMethod(qBound(0, int(Playback::value("phrasing/endingCurve", score)), int(ChangeMethod::EASE_IN_OUT)));
      for (const Ending& e : endings) {
            if (e.factor >= 100.0 || writtenTempoIn(score, e.t1, e.t2))
                  continue;
            const qreal start = startTempo(score, e.t1, from);
            const qreal end = start * e.factor / 100.0;
            int k = e.t1 >= from.ticks() ? 0 : (from.ticks() - e.t1 + STEP - 1) / STEP;
            for (int tick = e.t1 + k * STEP; tick < e.t2 && tick < to.ticks(); tick = e.t1 + (++k) * STEP) {
                  const double x = double(tick - e.t1) / double(e.t2 - e.t1);
                  const qreal tempo = method == ChangeMethod::EXPONENTIAL ? start * std::pow(end / start, x)
                                                                          : start + (end - start) * curve(method, x);
                  score->setTempo(Fraction::fromTicks(tick), tempo);
                  }
            // after the phrase: the tempo in force before its Ending (a tempo marking there takes over: added after this)
            if (e.t2 >= from.ticks() && e.t2 < to.ticks())
                  score->setTempo(Fraction::fromTicks(e.t2), start);
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
