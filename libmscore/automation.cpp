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
#include "undo.h"
#include "barline.h"
#include "measure.h"
#include "segment.h"

#include <algorithm>
#include <functional>
#include <set>
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

//---------------------------------------------------------
//   curves
//---------------------------------------------------------

static bool onDiagonal(double x, double y)
      {
      return std::fabs(x - y) <= 1e-6;
      }

bool Point::curved() const
      {
      return curve == Curve::LINEAR && !(onDiagonal(c1x, c1y) && onDiagonal(c2x, c2y));
      }

void setCurvature(Point& p, double k)
      {
      k = std::max(-1.0, std::min(1.0, k));
      if (std::fabs(k) < 1e-4) {
            p.straighten();
            return;
            }
      // the quadratic Bézier (0,0) Q (1,1), Q = (0.5 - k/2, 0.5 + k/2), as a cubic
      const double qx = 0.5 - 0.5 * k;
      const double qy = 0.5 + 0.5 * k;
      p.c1x = 2.0 / 3 * qx;
      p.c1y = 2.0 / 3 * qy;
      p.c2x = 1.0 / 3 + 2.0 / 3 * qx;
      p.c2y = 1.0 / 3 + 2.0 / 3 * qy;
      }

double curvature(const Point& p)
      {
      if (!p.curved())
            return 0;
      // Q from both control points (exact for setCurvature's), the nearest k
      const double qx = (1.5 * p.c1x + 1.5 * p.c2x - 0.5) * 0.5;
      const double qy = (1.5 * p.c1y + 1.5 * p.c2y - 0.5) * 0.5;
      return std::max(-1.0, std::min(1.0, qy - qx));
      }

static double bez(double p1, double p2, double t)
      {
      const double u = 1 - t;
      return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;             // p0 0, p3 1
      }

double curveAt(double c1x, double c1y, double c2x, double c2y, double x)
      {
      if (x <= 0)
            return 0;
      if (x >= 1)
            return 1;
      if (onDiagonal(c1x, c1y) && onDiagonal(c2x, c2y))
            return x;
      // t for x (x(t) rises when the control points' x are in 0-1, as Live keeps them): bisection
      double lo = 0, hi = 1, t = x;
      for (int i = 0; i < 60; ++i) {
            t = 0.5 * (lo + hi);
            if (bez(c1x, c2x, t) < x)
                  lo = t;
            else
                  hi = t;
            }
      return bez(c1y, c2y, 0.5 * (lo + hi));
      }

double Lane::valueAt(int tick) const
      {
      if (points.empty() || tick < points.front().tick)
            return -1;
      auto next = std::upper_bound(points.begin(), points.end(), Point { tick, 0, Curve::STEP });
      const Point& p = *std::prev(next);
      if (next == points.end() || p.curve == Curve::STEP || next->tick == p.tick)
            return p.value;
      const double x = double(tick - p.tick) / double(next->tick - p.tick);
      const double y = p.curved() ? curveAt(p.c1x, p.c1y, p.c2x, p.c2y, x) : x;
      return p.value + (next->value - p.value) * y;
      }

const char* const VELOCITY_TARGET = "velocity";

bool velocityAbsolute(const Lane& lane)
      {
      return lane.extra.value("velocityMode").toString() == "absolute";
      }

// MIDI 1.0: a note-on's velocity has 7 bits and 0 is a note-off, so a played note's is 1-127
int shapeVelocity(int v, double u, bool absolute)
      {
      if (u < 0)
            return v;
      const long r = absolute ? std::lround(127 * u) : std::lround(v * 2 * u);
      return int(std::max(1L, std::min(127L, r)));
      }

bool Lane::playedByLive() const
      {
      if (extra.contains("pointsHash"))
            return extra.value("pointsHash").toString() == pointsHash(points);
      return source() == SOURCE_LIVE;
      }

static QJsonArray pointJson(const Point& p)
      {
      QJsonArray a({ p.tick, std::round(p.value * 10000) / 10000, p.curve == Curve::LINEAR ? "linear" : "step" });
      if (p.curved())
            a.append(QJsonArray({ std::round(p.c1x * 1e6) / 1e6, std::round(p.c1y * 1e6) / 1e6,
                                  std::round(p.c2x * 1e6) / 1e6, std::round(p.c2y * 1e6) / 1e6 }));
      return a;
      }

QString pointsHash(const std::vector<Point>& points)
      {
      QJsonArray a;
      for (const Point& p : points)
            a.append(pointJson(p));
      // FNV-1a 32 (stable across runs and Qt versions)
      quint32 h = 2166136261u;
      for (char c : QJsonDocument(a).toJson(QJsonDocument::Compact)) {
            h ^= quint32(quint8(c));
            h *= 16777619u;
            }
      return QString::number(h, 16);
      }

std::vector<std::pair<double, double>> flattenCurve(double c1x, double c1y, double c2x, double c2y, double tolY)
      {
      struct P { double x, y; };
      std::vector<std::pair<double, double>> out;
      // (depth: a guard only; halving shrinks a piece's distance from its chord fourfold, so tolY is met long before)
      std::function<void(P, P, P, P, int)> split = [&](P a, P b, P c, P d, int depth) {
            auto off = [&](P q) {
                  const double dx = d.x - a.x;
                  const double l = dx > 1e-12 ? a.y + (d.y - a.y) * (q.x - a.x) / dx : a.y;
                  return std::fabs(q.y - l);
                  };
            if (depth >= 24 || (off(b) <= tolY && off(c) <= tolY)) {
                  out.push_back({ d.x, d.y });
                  return;
                  }
            auto mid = [](P p, P q) { return P { 0.5 * (p.x + q.x), 0.5 * (p.y + q.y) }; };
            const P ab = mid(a, b), bc = mid(b, c), cd = mid(c, d);
            const P abc = mid(ab, bc), bcd = mid(bc, cd), m = mid(abc, bcd);
            split(a, ab, abc, m, depth + 1);
            split(m, bcd, cd, d, depth + 1);
            };
      split({ 0, 0 }, { c1x, c1y }, { c2x, c2y }, { 1, 1 }, 0);
      out.back() = { 1.0, 1.0 };
      return out;
      }

std::vector<std::pair<int, double>> Lane::events(int tick1, int tick2, double resolution) const
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
            // along a ramp to the next point, inside [tick1, tick2): at each tick where it reaches another step
            if (p.curve == Curve::LINEAR && i + 1 < points.size() && resolution > 0) {
                  const Point& n = points[i + 1];
                  for (int t = std::max(p.tick, tick1) + 1; t < std::min(n.tick, tick2); ++t) {
                        const double v = valueAt(t);
                        if (std::lround(v / resolution) != std::lround(last / resolution)) {
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
                        const QJsonArray c = pa.size() > 3 ? pa.at(3).toArray() : QJsonArray();
                        if (p.curve == Curve::LINEAR && c.size() == 4) {
                              auto in01 = [](double v) { return std::min(1.0, std::max(0.0, v)); };
                              p.c1x = in01(c.at(0).toDouble());
                              p.c1y = c.at(1).toDouble();
                              p.c2x = in01(c.at(2).toDouble());
                              p.c2y = c.at(3).toDouble();
                              }
                        lane.points.push_back(p);
                        }
                  std::stable_sort(lane.points.begin(), lane.points.end());
                  if (!lane.points.empty() || lane.target == VELOCITY_TARGET)      // (the Velocity lane: its settings)
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
                  // (an empty lane isn't written; the Velocity lane's settings are, scale / absolute, shaped / written)
                  if (lane.points.empty() && !(lane.target == VELOCITY_TARGET
                                               && (lane.extra.contains("velocityMode") || lane.extra.contains("velocityOutput"))))
                        continue;
                  QJsonArray pts;
                  for (const Point& p : lane.points)
                        pts.append(pointJson(p));
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

bool undoWrite(MasterScore* score, const std::map<const Part*, PartLanes>& lanes)
      {
      if (!score)
            return false;
      const QString tag = write(score, lanes);
      if (tag == score->metaTag(metaTag))
            return false;
      QMap<QString, QString> tags = score->metaTags();
      if (tag.isEmpty())
            tags.remove(metaTag);
      else
            tags.insert(metaTag, tag);
      score->startCmd();
      score->undo(new ChangeMetaTags(score, tags));
      score->endCmd();
      score->setPlaylistDirty();
      return true;
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

//---------------------------------------------------------
//   merge
//---------------------------------------------------------

// one target in both: whose lane goes on (a conflict: both changed since they last agreed, or never agreed and differ)
enum class Side : signed char { MINE, LIVE, CONFLICT };
static Side classify(const Lane& m, const Lane& l)
      {
      const QString base = m.extra.value("liveHash").toString();
      const bool liveChanged = base.isEmpty() || base != l.extra.value("liveHash").toString();
      if (!liveChanged)
            return Side::MINE;                    // Live's as it was: MuseScore's (edited here or not) goes on
      if (!base.isEmpty() && m.playedByLive())
            return Side::LIVE;                    // unedited here: Live's newer one
      if (pointsHash(m.points) == pointsHash(l.points))
            return Side::LIVE;                    // the same envelope: they agree (Live's marks taken)
      return Side::CONFLICT;
      }

std::vector<Conflict> conflicts(const std::map<const Part*, PartLanes>& all, const std::map<const Part*, PartLanes>& with)
      {
      std::vector<Conflict> out;
      for (const auto& pl : with) {
            auto mi = all.find(pl.first);
            if (mi == all.end())
                  continue;
            for (const Lane& l : pl.second)
                  for (const Lane& m : mi->second)
                        if (m.target == l.target && classify(m, l) == Side::CONFLICT)
                              out.push_back({ pl.first, l.target, m, l });
            }
      return out;
      }

std::map<const Part*, PartLanes> merge(const std::map<const Part*, PartLanes>& all, const std::map<const Part*, PartLanes>& with,
                                       const std::map<std::pair<const Part*, QString>, Keep>& choices, QStringList* report)
      {
      std::map<const Part*, PartLanes> out;
      std::set<const Part*> parts;
      for (const auto& pl : all)
            parts.insert(pl.first);
      for (const auto& pl : with)
            parts.insert(pl.first);
      for (const Part* part : parts) {
            const PartLanes mine = all.count(part) ? all.at(part) : PartLanes();
            const PartLanes live = with.count(part) ? with.at(part) : PartLanes();
            PartLanes res;
            std::set<QString> taken;
            for (const Lane& l : live) {
                  const Lane* m = nullptr;
                  for (const Lane& x : mine)
                        if (x.target == l.target)
                              m = &x;
                  taken.insert(l.target);
                  if (!m) {
                        res.push_back(l);
                        continue;
                        }
                  Side side = classify(*m, l);
                  if (side == Side::CONFLICT) {
                        auto c = choices.find({ part, l.target });
                        const Keep k = c == choices.end() ? Keep::MUSESCORE : c->second;
                        side = k == Keep::LIVE ? Side::LIVE : Side::MINE;
                        if (report)
                              *report << QObject::tr("%1: changed in MuseScore and in Live: %2's kept").arg(l.target)
                                         .arg(k == Keep::LIVE ? QObject::tr("Live") : QObject::tr("MuseScore"));
                        if (side == Side::MINE) {
                              // MuseScore's goes on, now against Live's latest: asked again only when Live's changes again
                              Lane k2 = *m;
                              k2.extra["liveHash"] = l.extra.value("liveHash");
                              k2.extra["pointsHash"] = l.extra.value("pointsHash");
                              res.push_back(k2);
                              continue;
                              }
                        }
                  res.push_back(side == Side::LIVE ? l : *m);
                  }
            for (const Lane& m : mine) {
                  if (taken.count(m.target))
                        continue;
                  const bool fromLive = m.source() == SOURCE_LIVE || m.extra.contains("liveHash");
                  if (fromLive && m.playedByLive())
                        continue;                     // gone from Live, unedited here
                  Lane k = m;
                  if (fromLive) {
                        k.extra.remove("source");     // edited here: MuseScore's own now
                        k.extra.remove("liveHash");
                        k.extra.remove("pointsHash");
                        }
                  res.push_back(k);
                  }
            if (!res.empty())
                  out[part] = res;
            }
      return out;
      }

//---------------------------------------------------------
//   Edit
//---------------------------------------------------------

//---------------------------------------------------------
//   the lanes' time axis (mscore/automationlanes.h draws on it)
//---------------------------------------------------------

// The score's time axis as laid out (the owner's report, 2026-10-02: the lanes' bar lines stood ~10 px right of the
// staff's and a point left of its note). Anchors: each chord or rest at the middle of its note heads (the segment's x
// is the heads' left edge), so a point stands under its note; each measure's end at the middle of its bar line, so
// the time between the last note and the bar line runs to the bar line. Two anchors at a bar's tick: the bar line
// (the end of the measure before) and the first note after it: a point at that tick stands under the note (the last
// anchor), the grid's bar line at the bar line (barLineX).
std::vector<std::pair<int, double>> timeAxis(Score* s)
      {
      std::vector<std::pair<int, double>> out;
      if (!s)
            return out;
      const double half = 0.5 * s->noteHeadWidth();
      for (Measure* m = s->firstMeasureMM(); m; m = m->nextMeasureMM()) {
            if (!m->system())
                  continue;
            bool first = true;
            for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                  if (!seg->visible())
                        continue;
                  const double x = seg->canvasPos().x() + half;
                  if (first && seg->tick() != m->tick())
                        out.push_back({ m->tick().ticks(), x });
                  first = false;
                  out.push_back({ seg->tick().ticks(), x });
                  }
            out.push_back({ m->endTick().ticks(), barLineX(m) });
            }
      return out;
      }

double barLineX(const Measure* m)
      {
      auto middle = [](const Segment* s, double* x) {
            if (!s || !s->enabled() || s->width() <= 0)
                  return false;
            for (Element* e : s->elist())
                  if (e && e->isBarLine() && !e->bbox().isEmpty()) {
                        *x = e->canvasBoundingRect().center().x();
                        return true;
                        }
            return false;
            };
      double x = 0;
      if (middle(m->findSegment(SegmentType::EndBarLine, m->endTick()), &x))
            return x;
      // (no end bar line drawn: the next measure starts with a start repeat's, in the same system)
      const Measure* next = m->nextMeasureMM();
      if (next && next->system() == m->system()
          && middle(next->findSegment(SegmentType::StartRepeatBarLine, next->tick()), &x))
            return x;
      return m->canvasPos().x() + m->width();
      }

double xAtTick(const std::vector<std::pair<int, double>>& anchors, int tick)
      {
      if (anchors.empty())
            return 0;
      if (tick <= anchors.front().first)
            return anchors.front().second;
      if (tick >= anchors.back().first)
            return anchors.back().second;
      // the last anchor at or before the tick (of several at one tick: the last, the first note's), the next after
      auto it = std::upper_bound(anchors.begin(), anchors.end(), tick,
                                 [](int t, const std::pair<int, double>& a) { return t < a.first; });
      const auto& b = *it;
      const auto& a = *std::prev(it);
      if (b.first == a.first)
            return a.second;
      return a.second + (b.second - a.second) * double(tick - a.first) / double(b.first - a.first);
      }

namespace Edit {

static void sortPoints(Lane& lane)
      {
      std::stable_sort(lane.points.begin(), lane.points.end());
      }

// de Casteljau: the curved segment p -> n split at its time fraction x; p's shape becomes the first half's,
// the returned point (at the split) carries the second half's
static Point splitCurve(Point& p, const Point& n, double x, int tick, double value)
      {
      Point m(tick, value, Curve::LINEAR);
      if (!p.curved())
            return m;
      double lo = 0, hi = 1;
      for (int i = 0; i < 60; ++i) {
            const double t = 0.5 * (lo + hi);
            if (bez(p.c1x, p.c2x, t) < x)
                  lo = t;
            else
                  hi = t;
            }
      const double t = 0.5 * (lo + hi);
      // control polygon (box units): P0 (0,0), P1 c1, P2 c2, P3 (1,1)
      auto lerp = [](double a, double b, double t) { return a + (b - a) * t; };
      const double p01x = lerp(0, p.c1x, t), p01y = lerp(0, p.c1y, t);
      const double p12x = lerp(p.c1x, p.c2x, t), p12y = lerp(p.c1y, p.c2y, t);
      const double p23x = lerp(p.c2x, 1, t), p23y = lerp(p.c2y, 1, t);
      const double a1x = lerp(p01x, p12x, t), a1y = lerp(p01y, p12y, t);
      const double a2x = lerp(p12x, p23x, t), a2y = lerp(p12y, p23y, t);
      const double sx = lerp(a1x, a2x, t), sy = lerp(a1y, a2y, t);
      // each half in its own box (a half without time or value span: straight)
      auto box = [](double x0, double y0, double x1, double y1, double& cx, double& cy, double px, double py) {
            cx = x1 > x0 ? (px - x0) / (x1 - x0) : 0;
            cy = std::fabs(y1 - y0) > 1e-9 ? (py - y0) / (y1 - y0) : cx;
            cx = std::min(1.0, std::max(0.0, cx));
            };
      Point first = p;
      box(0, 0, sx, sy, first.c1x, first.c1y, p01x, p01y);
      box(0, 0, sx, sy, first.c2x, first.c2y, a1x, a1y);
      box(sx, sy, 1, 1, m.c1x, m.c1y, a2x, a2y);
      box(sx, sy, 1, 1, m.c2x, m.c2y, p23x, p23y);
      if (std::fabs(sy) < 1e-9)
            first.straighten();
      if (std::fabs(1 - sy) < 1e-9)
            m.straighten();
      p.c1x = first.c1x; p.c1y = first.c1y; p.c2x = first.c2x; p.c2y = first.c2y;
      (void)n;
      return m;
      }

int addPoint(Lane& lane, int tick, double value)
      {
      tick = std::max(0, tick);
      value = std::min(1.0, std::max(0.0, value));
      for (size_t i = 0; i < lane.points.size(); ++i)
            if (lane.points[i].tick == tick) {
                  lane.points[i].value = value;
                  return int(i);
                  }
      auto next = std::upper_bound(lane.points.begin(), lane.points.end(), Point(tick, 0, Curve::STEP));
      Point np(tick, value, Curve::LINEAR);
      if (next != lane.points.begin()) {
            Point& p = *std::prev(next);
            np.curve = p.curve;
            if (p.curve == Curve::LINEAR && next != lane.points.end()) {
                  const double x = double(tick - p.tick) / double(next->tick - p.tick);
                  np = splitCurve(p, *next, x, tick, value);
                  }
            }
      else if (!lane.points.empty())
            np.curve = Curve::LINEAR;
      auto it = lane.points.insert(next, np);
      return int(it - lane.points.begin());
      }

int addPointOnLine(Lane& lane, int tick)
      {
      double v = lane.valueAt(tick);
      if (v < 0)
            v = lane.points.empty() ? 0.5 : lane.points.front().value;
      return addPoint(lane, tick, v);
      }

void removePoints(Lane& lane, const std::vector<int>& indices)
      {
      std::vector<int> idx = indices;
      std::sort(idx.begin(), idx.end());
      idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
      for (auto i = idx.rbegin(); i != idx.rend(); ++i)
            if (*i >= 0 && *i < int(lane.points.size()))
                  lane.points.erase(lane.points.begin() + *i);
      }

std::vector<int> movePoints(Lane& lane, const std::vector<int>& indices, int dtick, double dvalue)
      {
      std::vector<int> idx;
      for (int i : indices)
            if (i >= 0 && i < int(lane.points.size()))
                  idx.push_back(i);
      std::sort(idx.begin(), idx.end());
      idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
      if (idx.empty())
            return {};
      int a0 = lane.points[size_t(idx.front())].tick, b0 = a0;
      for (int i : idx) {
            a0 = std::min(a0, lane.points[size_t(i)].tick);
            b0 = std::max(b0, lane.points[size_t(i)].tick);
            }
      dtick = std::max(dtick, -a0);
      const int a1 = a0 + dtick, b1 = b0 + dtick;
      std::vector<Point> moved, kept;
      std::set<int> sel(idx.begin(), idx.end());
      for (int i = 0; i < int(lane.points.size()); ++i) {
            Point p = lane.points[size_t(i)];
            if (sel.count(i)) {
                  p.tick += dtick;
                  p.value = std::min(1.0, std::max(0.0, p.value + dvalue));
                  moved.push_back(p);
                  }
            else {
                  // passed over: removed
                  if (dtick > 0 && p.tick > b0 && p.tick < b1)
                        continue;
                  if (dtick < 0 && p.tick < a0 && p.tick > a1)
                        continue;
                  kept.push_back(p);
                  }
            }
      // the moved group goes after kept points at an equal tick when it moved right (left: before)
      std::vector<Point> all;
      std::vector<bool> isMoved;
      size_t k = 0, m = 0;
      while (k < kept.size() || m < moved.size()) {
            bool takeMoved;
            if (k == kept.size())
                  takeMoved = true;
            else if (m == moved.size())
                  takeMoved = false;
            else if (moved[m].tick != kept[k].tick)
                  takeMoved = moved[m].tick < kept[k].tick;
            else
                  takeMoved = dtick < 0;
            all.push_back(takeMoved ? moved[m++] : kept[k++]);
            isMoved.push_back(takeMoved);
            }
      lane.points = all;
      std::vector<int> out;
      for (int i = 0; i < int(isMoved.size()); ++i)
            if (isMoved[size_t(i)])
                  out.push_back(i);
      return out;
      }

void setSegmentCurvature(Lane& lane, int i, double k)
      {
      if (i < 0 || i + 1 >= int(lane.points.size()))
            return;
      Point& p = lane.points[size_t(i)];
      p.curve = Curve::LINEAR;
      setCurvature(p, k);
      }

void setSegmentCurve(Lane& lane, int i, Curve c)
      {
      if (i < 0 || i >= int(lane.points.size()))
            return;
      lane.points[size_t(i)].curve = c;
      if (c == Curve::STEP)
            lane.points[size_t(i)].straighten();
      }

void drawStep(Lane& lane, int tick1, int tick2, double value, const Lane* original)
      {
      tick1 = std::max(0, tick1);
      if (tick2 <= tick1)
            return;
      value = std::min(1.0, std::max(0.0, value));
      const Lane& was = original ? *original : lane;
      const double after = was.valueAt(tick2);
      // the envelope arriving at tick1 (a ramp's end at a point there: that point's first value)
      double arriving = lane.valueAt(tick1);
      for (const Point& p : lane.points)
            if (p.tick == tick1) {
                  arriving = p.value;
                  break;
                  }
      const bool laterPoints = !was.points.empty() && was.points.back().tick > tick2;
      // the envelope's segment at tick2 goes on from there as it was (a step or a ramp)
      Curve afterCurve = Curve::STEP;
      for (const Point& p : was.points)
            if (p.tick <= tick2)
                  afterCurve = p.curve;
      bool pointAtEnd = false;
      std::vector<Point> pts;
      for (const Point& p : lane.points) {
            if (p.tick >= tick1 && p.tick < tick2)
                  continue;
            pointAtEnd = pointAtEnd || p.tick == tick2;
            pts.push_back(p);
            }
      // a ramp running into the step: it ends where the step starts, at the value it had there (a jump then)
      Point before(tick1, -1, Curve::LINEAR);
      for (const Point& p : pts)
            if (p.tick < tick1)
                  before.curve = p.curve, before.value = p.curve == Curve::LINEAR ? 0 : -1;
      lane.points = pts;
      if (before.value >= 0 && arriving >= 0) {
            before.value = arriving;
            lane.points.push_back(before);
            }
      lane.points.push_back(Point(tick1, value, Curve::STEP));
      if (!pointAtEnd && after >= 0 && laterPoints)
            lane.points.push_back(Point(tick2, after, afterCurve));
      else if (!pointAtEnd && after >= 0 && !laterPoints && std::fabs(after - value) > 1e-9)
            lane.points.push_back(Point(tick2, after, Curve::STEP));
      sortPoints(lane);
      // a step that repeats the step before it says nothing (a drag across cells at one height)
      for (size_t i = 1; i < lane.points.size(); ) {
            const Point& a = lane.points[i - 1];
            const Point& b = lane.points[i];
            if (a.curve == Curve::STEP && b.curve == Curve::STEP && std::fabs(a.value - b.value) < 1e-9)
                  lane.points.erase(lane.points.begin() + long(i));
            else
                  ++i;
            }
      }

// the segment a -> b's value at tick (a jump, b at a's tick: b's)
static double segmentValue(const Point& a, const Point& b, int tick)
      {
      if (a.curve == Curve::STEP || b.tick <= a.tick)
            return tick < b.tick ? a.value : b.value;
      const double x = std::min(1.0, std::max(0.0, double(tick - a.tick) / double(b.tick - a.tick)));
      const double y = a.curved() ? curveAt(a.c1x, a.c1y, a.c2x, a.c2y, x) : x;
      return a.value + (b.value - a.value) * y;
      }

// samples[i] -> samples[j] as one segment within tol of the samples between: straight, else curved (control x
// at 1/3 and 2/3: y(x) = 3(1-x)²x c1y + 3(1-x)x² c2y + x³, c1y and c2y by least squares). p: the segment's start
static bool fitSegment(const std::vector<std::pair<int, double>>& s, size_t i, size_t j, double tol, Point& p)
      {
      const double t0 = s[i].first, v0 = s[i].second;
      const double span = s[j].first - t0, d = s[j].second - v0;
      p = Point(s[i].first, v0, Curve::LINEAR);
      double err = 0;
      for (size_t k = i + 1; k < j && err <= tol; ++k)
            err = std::max(err, std::fabs(v0 + d * (s[k].first - t0) / span - s[k].second));
      if (err <= tol)
            return true;
      if (std::fabs(d) < 1e-9)
            return false;                 // (a segment without rise: straight only)
      double saa = 0, sab = 0, sbb = 0, sae = 0, sbe = 0;
      for (size_t k = i + 1; k < j; ++k) {
            const double x = (s[k].first - t0) / span;
            const double A = 3 * (1 - x) * (1 - x) * x * d, B = 3 * (1 - x) * x * x * d;
            const double e = s[k].second - v0 - d * x * x * x;
            saa += A * A; sab += A * B; sbb += B * B; sae += A * e; sbe += B * e;
            }
      const double det = saa * sbb - sab * sab;
      if (std::fabs(det) < 1e-18)
            return false;
      p.c1y = std::min(1.0, std::max(0.0, (sae * sbb - sbe * sab) / det));
      p.c2y = std::min(1.0, std::max(0.0, (sbe * saa - sae * sab) / det));
      for (size_t k = i + 1; k < j; ++k) {
            const double x = (s[k].first - t0) / span;
            const double y = 3 * (1 - x) * (1 - x) * x * p.c1y + 3 * (1 - x) * x * x * p.c2y + x * x * x;
            if (std::fabs(v0 + d * y - s[k].second) > tol)
                  return false;
            }
      return true;
      }

std::vector<Point> fitPoints(const std::vector<std::pair<int, double>>& samples, double tol)
      {
      // one sample per tick (the last at a tick), values 0-1
      std::vector<std::pair<int, double>> s;
      for (const auto& x : samples) {
            const double v = std::min(1.0, std::max(0.0, x.second));
            if (!s.empty() && s.back().first >= x.first)
                  s.back().second = v;
            else
                  s.push_back({ x.first, v });
            }
      std::vector<Point> out;
      if (s.size() < 2) {
            for (const auto& x : s)
                  out.push_back(Point(x.first, x.second, Curve::LINEAR));
            return out;
            }
      size_t i = 0;
      while (i + 1 < s.size()) {
            Point best;
            fitSegment(s, i, i + 1, tol, best);
            size_t bestJ = i + 1;
            for (size_t j = i + 2; j < s.size(); ++j) {
                  Point p;
                  if (!fitSegment(s, i, j, tol, p))
                        break;
                  best = p;
                  bestJ = j;
                  }
            out.push_back(best);
            i = bestJ;
            }
      out.push_back(Point(s.back().first, s.back().second, Curve::LINEAR));
      return out;
      }

int simplify(Lane& lane, int tick1, int tick2, double tol)
      {
      const std::vector<Point>& P = lane.points;
      int a = -1, b = -1;
      for (int i = 0; i < int(P.size()); ++i)
            if (P[size_t(i)].tick >= tick1 && P[size_t(i)].tick <= tick2) {
                  if (a < 0)
                        a = i;
                  b = i;
                  }
      if (a < 0 || b - a < 2)
            return 0;
      std::vector<Point> out(P.begin(), P.begin() + a);
      int k = a;
      while (k < b) {
            // a continuous stretch: ramps, no jump inside
            int m = k;
            while (m < b && P[size_t(m)].curve == Curve::LINEAR && P[size_t(m + 1)].tick > P[size_t(m)].tick)
                  ++m;
            if (m == k) {
                  out.push_back(P[size_t(k)]);
                  ++k;
                  continue;
                  }
            std::vector<Point> fitted;
            if (m - k >= 2) {
                  // sampled densely: each segment's ends and up to 32 ticks between (a curve's shape)
                  std::vector<std::pair<int, double>> samples;
                  for (int i = k; i < m; ++i) {
                        const Point& p = P[size_t(i)];
                        const Point& n = P[size_t(i + 1)];
                        const int step = std::max(1, (n.tick - p.tick) / 32);
                        for (int t = p.tick; t < n.tick; t += step)
                              samples.push_back({ t, t == p.tick ? p.value : segmentValue(p, n, t) });
                        }
                  samples.push_back({ P[size_t(m)].tick, P[size_t(m)].value });
                  fitted = fitPoints(samples, tol);
                  }
            if (fitted.size() >= 2 && int(fitted.size()) < m - k + 1) {
                  Point first = P[size_t(k)];
                  first.curve = Curve::LINEAR;
                  first.c1x = fitted[0].c1x; first.c1y = fitted[0].c1y;
                  first.c2x = fitted[0].c2x; first.c2y = fitted[0].c2y;
                  out.push_back(first);
                  out.insert(out.end(), fitted.begin() + 1, fitted.end() - 1);
                  }
            else
                  out.insert(out.end(), P.begin() + k, P.begin() + m);
            k = m;
            }
      out.insert(out.end(), P.begin() + b, P.end());
      const int removed = int(P.size()) - int(out.size());
      lane.points = out;
      return removed;
      }

void replaceSpan(Lane& lane, int tick1, int tick2, const std::vector<Point>& pts, const Lane* original)
      {
      tick1 = std::max(0, tick1);
      if (tick2 < tick1)
            return;
      Lane w = original ? *original : lane;
      auto at = [&w](int tick) {
            return std::any_of(w.points.begin(), w.points.end(), [tick](const Point& p) { return p.tick == tick; });
            };
      const bool hasBefore = !w.points.empty() && w.points.front().tick < tick1;
      const bool hasAfter = !w.points.empty() && w.points.back().tick > tick2;
      // split at the ends: the curves outside keep their shapes
      if (hasBefore && !at(tick1))
            addPoint(w, tick1, w.valueAt(tick1));
      if (hasAfter && !at(tick2))
            addPoint(w, tick2, w.valueAt(tick2));
      std::vector<Point> in;
      for (Point p : pts) {
            p.tick = std::min(tick2, std::max(tick1, p.tick));
            p.value = std::min(1.0, std::max(0.0, p.value));
            in.push_back(p);
            }
      std::stable_sort(in.begin(), in.end());
      std::vector<Point> out;
      for (const Point& p : w.points)
            if (p.tick < tick1)
                  out.push_back(p);
      // arriving: the first point at tick1 ends the ramp running in (after a step the envelope is there anyway)
      if (hasBefore && out.back().curve == Curve::LINEAR) {
            const Point arrive = *std::find_if(w.points.begin(), w.points.end(), [tick1](const Point& p) { return p.tick == tick1; });
            if (in.empty() || in.front().tick != tick1 || std::fabs(in.front().value - arrive.value) > 1e-9)
                  out.push_back(arrive);
            }
      out.insert(out.end(), in.begin(), in.end());
      // leaving: the last point at tick2 goes on as it was
      if (hasAfter) {
            const Point leave = *std::find_if(w.points.rbegin(), w.points.rend(), [tick2](const Point& p) { return p.tick == tick2; });
            if (!in.empty() && in.back().tick == tick2 && std::fabs(in.back().value - leave.value) < 1e-9)
                  out.back() = leave;
            else
                  out.push_back(leave);
            }
      else if (!in.empty()) {
            // nothing later: back to the value the lane ended on (as drawStep)
            const double after = w.valueAt(tick2);
            if (after >= 0 && std::fabs(after - in.back().value) > 1e-9)
                  out.push_back(Point(tick2, after, Curve::STEP));
            }
      for (const Point& p : w.points)
            if (p.tick > tick2)
                  out.push_back(p);
      lane.points = out;
      }

void drawFree(Lane& lane, const std::vector<std::pair<int, double>>& samples, const Lane* original, double tol)
      {
      std::vector<Point> pts = fitPoints(samples, tol);
      if (pts.size() < 2)
            return;
      replaceSpan(lane, pts.front().tick, pts.back().tick, pts, original);
      }

double shapeAt(Shape shape, double x)
      {
      x = std::min(1.0, std::max(0.0, x));
      switch (shape) {
            case Shape::SINE:        return 0.5 + 0.5 * std::sin(2 * M_PI * x);
            case Shape::TRIANGLE:    return x < 0.25 ? 0.5 + 2 * x : x < 0.75 ? 1 - 2 * (x - 0.25) : 2 * (x - 0.75);
            case Shape::SAW:         return x;
            case Shape::INVERSE_SAW: return 1 - x;
            case Shape::SQUARE:      return x < 0.5 ? 1 : 0;
            }
      return 0;
      }

void insertShape(Lane& lane, int tick1, int tick2, Shape shape, double tol)
      {
      tick1 = std::max(0, tick1);
      if (tick2 <= tick1)
            return;
      const int span = tick2 - tick1;
      auto tickAt = [&](double x) { return tick1 + int(std::lround(x * span)); };
      std::vector<Point> pts;
      switch (shape) {
            case Shape::SQUARE:
                  pts = { Point(tick1, 1, Curve::STEP), Point(tickAt(0.5), 0, Curve::STEP), Point(tick2, 0, Curve::STEP) };
                  break;
            case Shape::TRIANGLE:
                  pts = { Point(tick1, 0.5, Curve::LINEAR), Point(tickAt(0.25), 1, Curve::LINEAR),
                          Point(tickAt(0.75), 0, Curve::LINEAR), Point(tick2, 0.5, Curve::LINEAR) };
                  break;
            case Shape::SAW:
            case Shape::INVERSE_SAW:
                  pts = { Point(tick1, shapeAt(shape, 0), Curve::LINEAR), Point(tick2, shapeAt(shape, 1), Curve::LINEAR) };
                  break;
            case Shape::SINE: {
                  // sampled at 256 even steps of the cycle (each quarter's peak among them), then fitted
                  std::vector<std::pair<int, double>> s;
                  for (int k = 0; k <= 256; ++k)
                        s.push_back({ tickAt(k / 256.0), shapeAt(shape, k / 256.0) });
                  pts = fitPoints(s, tol);
                  break;
                  }
            }
      replaceSpan(lane, tick1, tick2, pts);
      }

std::vector<Point> copyPoints(const Lane& lane, const std::vector<int>& indices)
      {
      std::vector<Point> out;
      for (int i : indices)
            if (i >= 0 && i < int(lane.points.size()))
                  out.push_back(lane.points[size_t(i)]);
      std::stable_sort(out.begin(), out.end());
      if (!out.empty()) {
            const int t0 = out.front().tick;
            for (Point& p : out)
                  p.tick -= t0;
            }
      return out;
      }

std::vector<int> pastePoints(Lane& lane, int tick, const std::vector<Point>& clip)
      {
      if (clip.empty())
            return {};
      tick = std::max(0, tick);
      const int span = clip.back().tick;
      const double after = lane.valueAt(tick + span);
      const bool laterPoints = span > 0 && !lane.points.empty() && lane.points.back().tick > tick + span;
      std::vector<Point> pts;
      for (const Point& p : lane.points)
            if (p.tick < tick || p.tick > tick + span)
                  pts.push_back(p);
      lane.points = pts;
      std::vector<Point> added;
      for (Point p : clip) {
            p.tick += tick;
            added.push_back(p);
            }
      for (const Point& p : added)
            lane.points.push_back(p);
      // after the pasted span the envelope goes on as before (a jump back at its end, as Live pastes)
      if (laterPoints && after >= 0 && std::fabs(after - added.back().value) > 1e-9)
            lane.points.push_back(Point(tick + span, after, Curve::LINEAR));
      sortPoints(lane);
      std::vector<int> out;
      for (int i = 0; i < int(lane.points.size()); ++i)
            if (lane.points[size_t(i)].tick >= tick && lane.points[size_t(i)].tick <= tick + span)
                  out.push_back(i);
      return out;
      }

}     // namespace Edit

}     // namespace Automation
}     // namespace Ms
