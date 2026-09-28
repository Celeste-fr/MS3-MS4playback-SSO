//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playabilitydiagram.h"
#include "playabilityrules.h"

#include "chord.h"
#include "instrument.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "score.h"
#include "segment.h"
#include "select.h"
#include "slur.h"
#include "staff.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <QRegularExpression>

namespace Ms {
namespace Playability {

static double jsRound(double x) { return std::floor(x + 0.5); }

static DrawItem line(double x1, double y1, double x2, double y2, const QColor& color, double width, double opacity = 1)
      {
      DrawItem i;
      i.kind = DrawItem::Kind::LINE;
      i.x1 = x1; i.y1 = y1; i.x2 = x2; i.y2 = y2;
      i.color = color;
      i.width = width;
      i.opacity = opacity;
      return i;
      }

static DrawItem rect(double x, double y, double w, double h, const QColor& fill, double opacity = 1, bool smooth = false)
      {
      DrawItem i;
      i.kind = DrawItem::Kind::RECT;
      i.x = x; i.y = y; i.w = w; i.h = h;
      i.fill = fill;
      i.opacity = opacity;
      i.smooth = smooth;
      return i;
      }

static DrawItem circle(double x, double y, double r, const QColor& fill, const QColor& stroke = QColor(), double width = 1)
      {
      DrawItem i;
      i.kind = DrawItem::Kind::CIRCLE;
      i.x = x; i.y = y; i.r = r;
      i.fill = fill;
      i.stroke = stroke;
      i.width = width;
      return i;
      }

static DrawItem text(double x, double y, const QString& t, double size, const QColor& color, int align, bool bold = false)
      {
      DrawItem i;
      i.kind = DrawItem::Kind::LABEL;
      i.x = x; i.y = y;
      i.text = t;
      i.size = size;
      i.color = color;
      i.align = align;
      i.bold = bold;
      return i;
      }

static DrawItem meta(bool names)
      {
      DrawItem i;
      i.kind = DrawItem::Kind::META;
      i.namesShown = names;
      return i;
      }

bool namesShown(const DisplayList& items)
      {
      for (const DrawItem& i : items)
            if (i.kind == DrawItem::Kind::META)
                  return i.namesShown;
      return false;
      }

//---------------------------------------------------------
//   fingerboard (strings/fingerboard.js)
//    chord-chart convention: strings vertical, lowest on the left, nut at the top; a stop n
//    semitones up sits at L(1 − 2^(−n/12)), as in the stretch model
//---------------------------------------------------------

namespace FB {
static const QColor INK("#333333"), FAINT("#8a8a8a"), STRING("#7a7a7a"), NUT("#2b2b2b"), TICK("#e6e6e6"),
                    MARK("#c8c8c8"), STOPPED("#1f1f1f");
static const QColor RING("#1f1f1f"), SOLO("#9a9a9a"), FAINT_STRING("#d0d0d0"), BRIDGE("#2b2b2b");
static const double MAX_GAP = 40;
static const double MAX_HEIGHT = 420;
static bool isMark(int s) { return s == 5 || s == 7 || s == 12 || s == 19 || s == 24 || s == 31; }
}

// at least an octave, enough for the highest stop and the hand's reach, never over three octaves
static int fingerboardSpan(const ChordInfo& g)
      {
      double top = 0;
      for (const FingerNote& n : g.notes)
            top = std::max(top, n.offset);
      if (g.stopped)
            top = std::max(top, g.position + g.reach);
      return std::max(12, std::min(36, int(std::ceil(top)) + 3));
      }

static DisplayList layoutHarmonic(const ChordInfo& g, double w, double h);

DisplayList layoutFingerboard(const ChordInfo& g, double w, double h)
      {
      DisplayList items;
      if (g.kind == ChordInfo::Kind::NONE || g.strings.empty() || w < 70 || h < 80)
            return items;
      if (g.kind == ChordInfo::Kind::HARMONIC)
            return layoutHarmonic(g, w, h);

      const QColor open = openStringColor;
      int n = int(g.strings.size());
      bool tiny = h < 170;            // in a short panel the string names above the nut would eat the board
      double left = 26, right = 34, top = tiny ? 14 : 44, bottom = 10;
      double avail = w - left - right;
      double gap = n > 1 ? std::min(FB::MAX_GAP, avail / (n - 1)) : 0;
      double x0 = left + std::max(0.0, (avail - gap * (n - 1)) / 2);
      double bh = std::min(h, FB::MAX_HEIGHT) - top - bottom;
      int span = fingerboardSpan(g);
      double norm = 1 - std::pow(2.0, -span / 12.0);
      auto yAt = [&](double semi) { return top + bh * (1 - std::pow(2.0, -semi / 12)) / norm; };
      auto xAt = [&](int s) { return x0 + gap * (n - 1 - s); };          // string I rightmost
      double xLow = xAt(n - 1), xHigh = xAt(0);
      double dotR = std::max(3.5, std::min(7.0, gap * 0.28));
      bool showNames = n == 1 || gap >= 2 * dotR + 24;
      items.push_back(meta(showNames));

      // semitone lines, landmarks labelled unless crowding the one above
      double lastLabelY = -99;
      for (int s = 1; s <= span; ++s) {
            bool mark = FB::isMark(s);
            items.push_back(line(xLow - 8, yAt(s), xHigh + 8, yAt(s), mark ? FB::MARK : FB::TICK, mark ? 1.2 : 0.8));
            if (mark && yAt(s) - lastLabelY >= 12) {
                  items.push_back(text(xLow - 14, yAt(s) + 3, QString::number(s), 9, FB::FAINT, 1));
                  lastLabelY = yAt(s);
                  }
            }

      // the hand's reach, as a band across the strings it stops
      if (g.stopped) {
            double xa = 1e9, xb = -1e9;
            for (const FingerNote& nt : g.notes)
                  if (nt.offset > 0) {
                        xa = std::min(xa, xAt(nt.string));
                        xb = std::max(xb, xAt(nt.string));
                        }
            xa -= 13;
            xb += 13;
            double ya = yAt(g.position), yb = yAt(std::min(double(span), g.position + g.reach));
            items.push_back(rect(xa, ya - 5, xb - xa, (yb - ya) + 10, open, 0.13));
            }

      // strings (lower strings heavier), their names, the nut
      for (int i = 0; i < n; ++i) {
            items.push_back(line(xAt(i), top, xAt(i), top + bh, FB::STRING, 1 + 0.45 * i));
            if (!tiny) {
                  items.push_back(text(xAt(i), top - 31, g.stringNames.value(i), 11, FB::INK, 0, true));
                  items.push_back(text(xAt(i), top - 19, i < 5 ? ROMAN[i] : "", 9, FB::FAINT, 0));
                  }
            else
                  items.push_back(text(xAt(i), top - 3, g.stringNames.value(i), 9, FB::INK, 0, true));
            }
      items.push_back(line(xLow - 8, top, xHigh + 8, top, FB::NUT, 3));

      // the chord: open strings above the nut, stops on the board
      for (const FingerNote& nt : g.notes) {
            double x = xAt(nt.string);
            if (nt.offset == 0)
                  items.push_back(circle(x, top - 9, 5.5, QColor(), open, 2.2));
            else {
                  double y = yAt(nt.offset);
                  items.push_back(circle(x, y, dotR, FB::STOPPED));
                  if (showNames)
                        items.push_back(text(x + dotR + 4, y + 4, nt.name, 10, FB::INK, -1, true));
                  }
            }
      return items;
      }

//---------------------------------------------------------
//   layoutHarmonic
//    every string the natural harmonic can be played on, each at its FULL length from the nut
//    (top) to the bridge (bottom) on a linear scale, so a node's place shows how the string
//    divides; strings that can't give it faint; each node a ring labelled with the touched note,
//    its fraction on the left, the sounding note under the bridge; a 2/5 node (solo) grey
//---------------------------------------------------------

static DisplayList layoutHarmonic(const ChordInfo& g, double w, double h)
      {
      DisplayList items;
      int n = int(g.strings.size());
      bool anySolo = false;
      for (const HarmonicNoteInfo& hn : g.harmonics)
            for (const HarmonicOptionInfo& o : hn.options)
                  for (const HarmonicNodeInfo& nd : o.nodes)
                        anySolo |= nd.solo;
      bool tiny = h < 170;
      double left = 26, right = 34, top = tiny ? 14 : 44, bottom = tiny ? 18 : (anySolo ? 46 : 32);
      double avail = w - left - right;
      double gap = n > 1 ? std::min(FB::MAX_GAP, avail / (n - 1)) : 0;
      double x0 = left + std::max(0.0, (avail - gap * (n - 1)) / 2);
      double bh = std::min(h, FB::MAX_HEIGHT) - top - bottom;
      auto xAt = [&](int i) { return x0 + gap * (n - 1 - i); };
      auto yAt = [&](double frac) { return top + bh * frac; };
      double xLow = xAt(n - 1), xHigh = xAt(0);
      double r = std::max(3.5, std::min(6.0, gap * 0.28));
      bool showNames = gap >= 2 * r + 20;
      items.push_back(meta(showNames));

      // what each string carries; the fractions used, in the order first met
      std::map<int, std::vector<const HarmonicOptionInfo*>> used;
      std::vector<std::pair<QString, double>> fracs;
      for (const HarmonicNoteInfo& hn : g.harmonics)
            for (const HarmonicOptionInfo& o : hn.options) {
                  used[o.string].push_back(&o);
                  for (const HarmonicNodeInfo& nd : o.nodes) {
                        QString key = QString("%1/%2").arg(nd.num).arg(nd.den);
                        auto f = std::find_if(fracs.begin(), fracs.end(), [&](const std::pair<QString, double>& p) { return p.first == key; });
                        if (f == fracs.end())
                              fracs.push_back({ key, double(nd.num) / nd.den });
                        }
                  }
      std::stable_sort(fracs.begin(), fracs.end(), [](const std::pair<QString, double>& a, const std::pair<QString, double>& b) { return a.second < b.second; });
      double fracSize = bh < 140 ? 8 : 9, fracGap = bh < 140 ? 8 : 12;
      double lastY = -99;
      for (const auto& f : fracs) {
            double y = yAt(f.second);
            items.push_back(line(xLow - 8, y, xHigh + 8, y, FB::TICK, 1));
            if (y - lastY >= fracGap) {
                  items.push_back(text(xLow - 14, y + 3, f.first, fracSize, FB::FAINT, 1));
                  lastY = y;
                  }
            }

      // strings, names, nut and bridge
      for (int i = 0; i < n; ++i) {
            bool on = used.count(i);
            items.push_back(line(xAt(i), top, xAt(i), top + bh, on ? FB::STRING : FB::FAINT_STRING, 1 + 0.45 * i));
            if (!tiny) {
                  items.push_back(text(xAt(i), top - 31, g.stringNames.value(i), 11, on ? FB::INK : FB::FAINT, 0, on));
                  items.push_back(text(xAt(i), top - 19, i < 5 ? ROMAN[i] : "", 9, FB::FAINT, 0));
                  }
            else
                  items.push_back(text(xAt(i), top - 3, g.stringNames.value(i), 9, on ? FB::INK : FB::FAINT, 0, on));
            }
      items.push_back(line(xLow - 8, top, xHigh + 8, top, FB::NUT, 3));
      items.push_back(line(xLow - 8, top + bh, xHigh + 8, top + bh, FB::BRIDGE, 2));
      items.push_back(text(2, top - 4, "nut", 9, FB::FAINT, -1));
      items.push_back(text(2, top + bh + 11, "bridge", 9, FB::FAINT, -1));

      // nodes and sounding notes
      const QString arrow = QString(" ") + QChar(0x2192) + " ";
      for (const auto& u : used) {
            double x = xAt(u.first);
            QStringList sounds;
            for (const HarmonicOptionInfo* o : u.second)
                  if (!sounds.contains(o->soundsName))
                        sounds << o->soundsName;
            bool several = sounds.size() > 1;             // name each node's sound when a string gives two
            sounds.clear();
            for (const HarmonicOptionInfo* o : u.second) {
                  if (!sounds.contains(o->soundsName))
                        sounds << o->soundsName;
                  for (const HarmonicNodeInfo& nd : o->nodes) {
                        double ny = yAt(double(nd.num) / nd.den);
                        QColor col = nd.solo ? FB::SOLO : FB::RING;
                        items.push_back(circle(x, ny, r + 1.5, QColor("#ffffff")));
                        items.push_back(circle(x, ny, r, QColor(), col, 2));
                        if (showNames) {
                              QString label = nd.name + (nd.solo ? "*" : "");
                              QString longer = label + arrow + o->soundsName;
                              // ~6.2 px per bold 10 px character; the label ends before the next string's ring
                              if (several && longer.length() * 6.2 <= gap - 2 * r - 10)
                                    label = longer;
                              double room = w - (x + r + 4);
                              if (label.length() * 6.2 <= room)
                                    items.push_back(text(x + r + 4, ny + 4, label, 10, col, -1, true));
                              else
                                    items.push_back(text(x - r - 4, ny + 4, label, 10, col, 1, true));
                              }
                        }
                  }
            for (int t = 0; t < sounds.size(); ++t)
                  items.push_back(text(x, top + bh + (tiny ? 13 : 24) + 12 * t, sounds[t], 10, FB::INK, 0, true));
            }
      if (anySolo)
            items.push_back(text(2, top + bh + 42, "* solo and chamber only", 9, FB::SOLO, -1));
      return items;
      }

//---------------------------------------------------------
//   W1 register graph (winds/registergraph.js)
//---------------------------------------------------------

// the key a part's name gives: "Bb", "Eb", "F#", "C" …, or "" ("Clarinet 2" has none)
static QString nameKey(const QString& name)
      {
      static const QRegularExpression KEY(QString::fromUtf8("(?:^|\\s)([A-G])(♭|b|♯|#)?(?=\\s|$)"));
      QRegularExpressionMatch m = KEY.match(name);
      if (!m.hasMatch())
            return QString();
      QString acc = m.captured(2);
      return m.captured(1) + (acc == QString::fromUtf8("♭") || acc == "b" ? "b" : acc.isEmpty() ? "" : "#");
      }

QString windResolve(const QString& instrumentId, const QString& name)
      {
      auto e = windIds().find(instrumentId);
      if (instrumentId.isEmpty() || e == windIds().end())
            return QString();
      QString key = e->second.anyKey ? QString() : nameKey(name);
      if (key.isEmpty())
            return e->second.variants.front().second;
      for (const auto& v : e->second.variants)
            if (key == v.first)
                  return v.second;
      return QString();
      }

// the instrument in force at a tick: its id and long name, else the part's (as the pass)
static void instrumentAt(const Part* part, const Fraction& tick, QString* id, QString* name)
      {
      const Instrument* in = part->instrument(tick);
      *id = in->instrumentId();
      *name = in->longNames().isEmpty() ? QString() : in->longNames().front().name();
      if (id->isEmpty() && name->isEmpty()) {
            *id = part->instrumentId();
            *name = part->longName();
            if (name->isEmpty())
                  *name = part->partName();
            }
      }

static QString windAt(const Part* part, int tick)
      {
      QString id, name;
      instrumentAt(part, Fraction::fromTicks(tick), &id, &name);
      return windResolve(id, name);
      }

static QString stripNumber(const QString& name)
      {
      static const QRegularExpression NUM("\\s*(\\d+|[IVX]+)\\.?\\s*$");
      static const QRegularExpression TRAIL("\\s+$");
      return QString(name).remove(NUM).remove(TRAIL);
      }

static QString chipLabel(const QStringList& shorts)
      {
      QString base = stripNumber(shorts[0]);
      if (base.isEmpty())
            base = shorts[0].isEmpty() ? QString("?") : shorts[0];
      return shorts.size() > 1 ? base + QString(" ") + QChar(0x00d7) + QString::number(shorts.size()) : shorts[0];
      }

static QString graphTitle(const QStringList& names)
      {
      if (names.size() == 1)
            return names[0];
      QString base = stripNumber(names[0]);
      for (const QString& n : names)
            if (stripNumber(n) != base)
                  return names.join(", ");
      static const QRegularExpression NUM("(\\d+|[IVX]+)\\.?\\s*$");
      QStringList nums;
      for (int j = 0; j < names.size(); ++j) {
            QRegularExpressionMatch m = NUM.match(names[j]);
            nums << (m.hasMatch() ? m.captured(1) : QString::number(j + 1));
            }
      return base + " " + nums.join(", ");
      }

// the axis: the instrument's range (T1, else MuseScore's), stretched to hold every note; the
// stripes' "(bottom)" / "(top)" become the axis ends
static void resolveAxis(WindGraph& g)
      {
      const WindData* d = g.data;
      int lo, hi;
      if (d->hasRange) {
            lo = d->rangeLo;
            hi = d->rangeHi;
            g.axisSrc = "sourcebook";
            }
      else if (g.hasMsRange) {
            lo = g.msLo;
            hi = g.msHi;
            g.axisSrc = "MuseScore";
            }
      else {
            lo = g.lo - 2;
            hi = g.hi + 2;
            g.axisSrc = "notes";
            }
      if (g.hi >= 0) {
            lo = std::min(lo, g.lo);
            hi = std::max(hi, g.hi);
            }
      g.axisLo = lo;
      g.axisHi = hi;
      for (const WindBand& b : d->bands) {
            int a = b.lo == WIND_BOTTOM ? lo : b.lo;
            int c = b.hi == WIND_TOP ? hi : b.hi;
            g.bands.push_back({ std::max(a, lo), std::min(c, hi), QString::fromUtf8(b.words) });
            }
      }

WindModel windModel(Score* score, bool museScoreRange)
      {
      WindModel m;
      struct Sel { int track, tick, end, pitch; };
      std::vector<Sel> sel;
      for (Element* e : score->selection().elements()) {
            if (!e->isNote())
                  continue;
            Chord* c = toNote(e)->chord();
            if (c->isGrace())
                  continue;
            sel.push_back({ e->track(), c->tick().ticks(), (c->tick() + c->actualTicks()).ticks(), toNote(e)->ppitch() });
            }
      if (sel.empty())
            return m;

      std::vector<Sel> windSel;
      std::set<int> selStaves;
      int from = -1, to = -1;
      for (const Sel& s : sel) {
            int st = s.track / VOICES;
            if (windAt(score->staff(st)->part(), s.tick).isEmpty())
                  continue;
            windSel.push_back(s);
            selStaves.insert(st);
            if (from < 0 || s.tick < from)
                  from = s.tick;
            to = std::max(to, s.end);
            }
      // a range selection also names wind staves with no note selected
      if (score->selection().isRange() && from >= 0)
            for (int sx = score->selection().staffStart(); sx < score->selection().staffEnd(); ++sx)
                  if (!windAt(score->staff(sx)->part(), from).isEmpty())
                        selStaves.insert(sx);
      if (windSel.empty())
            return m;

      // whole bars
      std::vector<int> starts;
      for (Measure* ms = score->firstMeasure(); ms; ms = ms->nextMeasure())
            starts.push_back(ms->tick().ticks());
      int endTick = score->lastSegment() ? score->lastSegment()->tick().ticks() : -1;
      int b0 = 0, b1 = 0;
      for (int b = 0; b < int(starts.size()); ++b) {
            if (starts[b] <= from)
                  b0 = b;
            if (starts[b] < to)
                  b1 = b;
            }
      if (b1 - b0 + 1 > WIND_MAX_BARS) {
            b1 = b0 + WIND_MAX_BARS - 1;
            m.clipped = true;
            }
      int tFrom = starts[b0];
      int tTo = b1 + 1 < int(starts.size()) ? starts[b1 + 1] : std::max(endTick, to);
      for (int bb = b0; bb <= b1; ++bb)
            m.bars.push_back({ bb + 1, starts[bb] });

      std::set<std::tuple<int, int, int>> selKey;
      for (const Sel& s : windSel)
            selKey.insert({ s.track, s.tick, s.pitch });

      // slurs per track, in order
      std::map<int, std::vector<std::pair<int, int>>> slurs;
      for (const auto& sp : score->spanner())
            if (sp.second->isSlur())
                  slurs[sp.second->track()].push_back({ sp.second->tick().ticks(), sp.second->tick2().ticks() });
      for (auto& t : slurs)
            std::stable_sort(t.second.begin(), t.second.end());

      // one graph per instrument, in score order
      std::map<QString, size_t> byId;
      for (int st : selStaves) {
            Part* part = score->staff(st)->part();
            QString id = windAt(part, from);
            if (id.isEmpty())
                  continue;
            auto gi = byId.find(id);
            if (gi == byId.end()) {
                  WindGraph g;
                  g.id = id;
                  g.data = &windData().at(id);
                  m.graphs.push_back(g);
                  gi = byId.insert({ id, m.graphs.size() - 1 }).first;
                  }
            WindGraph& g = m.graphs[gi->second];
            QString name = part->longName();
            if (name.isEmpty())
                  name = part->partName();
            if (name.isEmpty())
                  name = QString("staff %1").arg(st + 1);
            g.staves.push_back(st);
            g.names << name;
            g.shorts << shortStaffName(part, name, Fraction::fromTicks(from));
            if (!g.hasMsRange && museScoreRange) {
                  const Instrument* in = part->instrument(Fraction::fromTicks(from));
                  g.hasMsRange = true;
                  g.msLo = in->minPitchP();
                  g.msHi = in->maxPitchP();
                  }
            for (int v = 0; v < VOICES; ++v) {
                  int trk = st * VOICES + v;
                  const std::vector<std::pair<int, int>>* ts = slurs.count(trk) ? &slurs[trk] : nullptr;
                  for (Segment* s = score->tick2segment(Fraction::fromTicks(tFrom), true, SegmentType::ChordRest);
                       s && s->tick().ticks() < tTo; s = s->next1(SegmentType::ChordRest)) {
                        Element* e = s->element(trk);
                        if (!e || !e->isChord())
                              continue;
                        Chord* c = toChord(e);
                        int tick = s->tick().ticks();
                        int slur = -1;
                        if (ts)
                              for (size_t i = 0; i < ts->size(); ++i)
                                    if ((*ts)[i].first <= tick && tick <= (*ts)[i].second) {
                                          slur = int(i);
                                          break;
                                          }
                        for (Note* n : c->notes()) {
                              int p = n->ppitch();
                              bool isSel = selKey.count({ trk, tick, p });
                              g.notes.push_back({ p, tick, c->actualTicks().ticks(), trk, slur, isSel });
                              g.hasSel |= isSel;
                              g.lo = std::min(g.lo, p);
                              g.hi = std::max(g.hi, p);
                              }
                        }
                  }
            }
      if (m.graphs.empty())
            return m;
      QStringList ids;
      for (size_t i = 0; i < m.graphs.size(); ++i) {
            WindGraph& g = m.graphs[i];
            g.chip = chipLabel(g.shorts);
            g.title = graphTitle(g.names);
            resolveAxis(g);
            if (g.hasSel && !m.graphs[m.first].hasSel)
                  m.first = int(i);
            ids << g.id;
            }
      m.from = tFrom;
      m.to = tTo;
      m.selFrom = from;
      m.selTo = to;
      m.key = ids.join(",") + QString("|%1|%2").arg(tFrom).arg(tTo);
      return m;
      }

// the width of a curve at pitch p (piecewise linear), or -1 outside it
static double curveAt(const std::vector<std::pair<double, double>>& curve, double p)
      {
      if (curve.empty() || p < curve.front().first || p > curve.back().first)
            return -1;
      for (size_t i = 1; i < curve.size(); ++i) {
            const auto& a = curve[i - 1];
            const auto& b = curve[i];
            if (p <= b.first)
                  return b.first == a.first ? b.second : a.second + (b.second - a.second) * (p - a.first) / (b.first - a.first);
            }
      return curve.back().second;
      }

static QString rgName(int p)
      {
      static const char* const NAMES[12] = { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };
      return QString::fromUtf8(NAMES[((p % 12) + 12) % 12]) + QString::number(int(std::floor(p / 12.0)) - 1);
      }

namespace RG {
static const QColor BAND[4] = { QColor("#e2e5f7"), QColor("#cfd5f3"), QColor("#bcc4ee"), QColor("#a9b3e9") };
static const QColor LABEL("#222222"), FAINT("#777777"), FRAME("#c4c4c4"), NOTE("#222222"), SEL("#d0432b"),
                    CURVE("#a3a3a3"), BAR("#9a9a9a");
}

// rough text width: the panel's sans-serif is about 0.56 em per character
static QString fit(const QString& t, double size, double room)
      {
      if (t.length() * size * 0.56 <= room)
            return t;
      int n = std::max(1, int(std::floor(room / (size * 0.56))) - 1);
      if (n >= t.length())
            return t;
      static const QRegularExpression TAIL("[\\s,;.]+$");
      return t.left(n).remove(TAIL) + QChar(0x2026);
      }

DisplayList layoutWindGraph(const WindModel& model, int gi, double w, double h)
      {
      DisplayList items;
      if (!model.valid() || gi < 0 || gi >= int(model.graphs.size()) || w < 90 || h < 44)
            return items;
      const WindGraph& G = model.graphs[gi];
      const WindData* d = G.data;
      bool tight = w < 230;                         // a narrow panel: narrower gutter, smaller names
      double maxHalf = tight ? 6 : 9;               // half the dynamic strip's widest point
      double DX = tight ? 28 : 46, X0 = DX + maxHalf + 7, X1 = w - 6;
      double nameRight = DX - maxHalf - 4;
      double nameSize = tight ? 8 : 9;
      bool squat = h < 150;                         // a short panel: no title line
      double top = squat ? 4 : 20, bottom = h - (squat ? 12 : 18);
      int lo = G.axisLo, hi = G.axisHi;
      double step = (bottom - top) / (hi - lo + 1);
      auto y = [&](double p) { return bottom - (p - lo + 0.5) * step; };
      auto yTop = [&](double p) { return bottom - (p - lo + 1) * step; };
      auto yBot = [&](double p) { return bottom - (p - lo) * step; };
      double span = std::max(1, model.to - model.from);
      auto xt = [&](double t) { return X0 + (X1 - X0) * (t - model.from) / span; };

      if (!squat) {
            DrawItem t = text(X0, 12, fit(G.title, 11, X1 - X0), 11, RG::LABEL, -1, true);
            items.push_back(t);
            }

      // stripes, their first notes on the left, their words on the right
      double lastLabel = 1e9;
      for (size_t i = 0; i < G.bands.size(); ++i) {
            const WindGraphBand& bd = G.bands[i];
            double y1 = yTop(bd.hi), y2 = yBot(bd.lo);
            if (bd.hi < bd.lo)
                  continue;
            items.push_back(rect(X0, y1, X1 - X0, y2 - y1, RG::BAND[i % 4]));
            if (X1 - X0 - 8 >= 40) {
                  DrawItem t = text(X1 - 4, y1 + 11, fit(bd.words, 9, X1 - X0 - 8), 9, RG::LABEL, 1);
                  t.halo = RG::BAND[i % 4];
                  items.push_back(t);
                  }
            if (lastLabel - y2 >= 10) {
                  items.push_back(text(nameRight, y2 - 1, rgName(bd.lo), nameSize, RG::FAINT, 1));
                  lastLabel = y2;
                  }
            }
      if (G.bands.empty())
            items.push_back(text((X0 + X1) / 2, top + 16, "No register descriptions in any source", 9, RG::FAINT, 0));
      if (G.bands.empty() || G.bands[0].lo > lo)
            items.push_back(text(nameRight, bottom - 1, rgName(lo), nameSize, RG::FAINT, 1));
      items.push_back(text(nameRight, top + 8, rgName(hi), nameSize, RG::FAINT, 1));

      // the dynamic curve, centred on DX, in thin slices so it reads as a ribbon; widths stretched
      // over this instrument's own narrowest and widest point (Blatter's curve per family)
      const double SLICE = 2, MIN_HALF = 1.5;
      auto pitchAtY = [&](double yy) { return lo - 0.5 + (bottom - yy) / step; };
      double mn = 9, mx = 0;
      for (const auto* c : { &d->curve, &d->pedal })
            for (const auto& pt : *c) {
                  mn = std::min(mn, pt.second);
                  mx = std::max(mx, pt.second);
                  }
      auto strip = [&](const std::vector<std::pair<double, double>>& curve) {
            for (double yy = top; yy < bottom; yy += SLICE) {
                  double wv = curveAt(curve, pitchAtY(yy + SLICE / 2));
                  if (wv < 0)
                        continue;
                  double t = mx > mn ? (wv - mn) / (mx - mn) : 1;
                  double half = MIN_HALF + t * (maxHalf - MIN_HALF);
                  // y on whole pixels, 1 px overlap (a fractional gap paints a seam); the width stays
                  // fractional and is drawn antialiased
                  items.push_back(rect(DX - half, jsRound(yy), 2 * half, SLICE + 1, RG::CURVE, 1, true));
                  }
            };
      if (!d->curve.empty()) {
            strip(d->curve);
            if (!d->pedal.empty())
                  strip(d->pedal);
            }
      items.push_back(text(DX, bottom + 13, d->curve.empty() ? "" : "dyn.", 8, RG::FAINT, 0));

      // selection column
      if (G.hasSel || model.selFrom >= 0)
            items.push_back(rect(xt(model.selFrom), top, std::max(2.0, xt(model.selTo) - xt(model.selFrom)), bottom - top, RG::SEL, 0.1));

      // bar lines and numbers
      for (size_t b = 0; b < model.bars.size(); ++b) {
            double bx = xt(model.bars[b].second);
            if (b > 0)
                  items.push_back(line(bx, top, bx, bottom, RG::BAR, 1, 0.6));
            items.push_back(text(bx + 2, bottom + 13, QString::number(model.bars[b].first), 9, RG::FAINT, -1));
            }
      if (model.clipped)
            items.push_back(text(X1, bottom + 13, QString("first %1 bars").arg(model.bars.size()), 9, RG::FAINT, 1));

      // frame
      items.push_back(line(X0, top, X1, top, RG::FRAME, 1));
      items.push_back(line(X0, bottom, X1, bottom, RG::FRAME, 1));
      items.push_back(line(X0, top, X0, bottom, RG::FRAME, 1));
      items.push_back(line(X1, top, X1, bottom, RG::FRAME, 1));

      // slur joins: in one voice and one slur, chord to next chord, notes joined by rank
      std::map<std::pair<int, int>, std::map<int, std::vector<const WindNote*>>> groups;
      for (const WindNote& nt : G.notes)
            if (nt.slur >= 0)
                  groups[{ nt.v, nt.slur }][nt.s].push_back(&nt);
      for (const auto& gg : groups) {
            std::vector<int> ts;
            for (const auto& t : gg.second)
                  ts.push_back(t.first);
            for (size_t t = 1; t < ts.size(); ++t) {
                  std::vector<const WindNote*> A = gg.second.at(ts[t - 1]), B = gg.second.at(ts[t]);
                  auto high = [](const WindNote* a, const WindNote* b) { return b->p < a->p; };
                  std::stable_sort(A.begin(), A.end(), high);
                  std::stable_sort(B.begin(), B.end(), high);
                  size_t n = std::max(A.size(), B.size());
                  for (size_t r = 0; r < n; ++r) {
                        const WindNote* a1 = A[std::min(r, A.size() - 1)];
                        const WindNote* c1 = B[std::min(r, B.size() - 1)];
                        if (a1->p == c1->p)
                              continue;
                        double jx = xt(c1->s);
                        bool bothSel = a1->sel && c1->sel;
                        items.push_back(line(jx, y(a1->p), jx, y(c1->p), bothSel ? RG::SEL : RG::NOTE, bothSel ? 3 : 1.5));
                        }
                  }
            }
      // notes: flat steps
      for (const WindNote& no : G.notes) {
            bool loose = no.slur < 0;
            double xa = xt(no.s) + (loose ? 1 : 0), xb = xt(no.s + no.l) - (loose ? 2 : 0);
            if (xb <= xa)
                  xb = xa + 1;
            items.push_back(line(xa, y(no.p), xb, y(no.p), no.sel ? RG::SEL : RG::NOTE, no.sel ? 3 : 2));
            }
      return items;
      }

}     // namespace Playability
}     // namespace Ms
