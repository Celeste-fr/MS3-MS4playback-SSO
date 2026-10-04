//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "automationlanes.h"
#include "liveclipedit.h"
#include "liveclips.h"
#include "scoreview.h"
#include "musescore.h"
#include "seq.h"

#include "libmscore/measure.h"
#include "libmscore/page.h"
#include "libmscore/part.h"
#include "libmscore/partplayback.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/sig.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/staff.h"
#include "libmscore/system.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSettings>

namespace Ms {

std::vector<Automation::Point> AutomationLanes::_clipboard;

static const double HEAD_SP = 2.4;        // the part's header row, spatium
static const double LANE_SP = 5.0;        // a lane
static const double GAP_SP = 0.4;
static const int HEADER_PX = 150;         // the headers' width on screen
static const int HEADER_MAX_PX = 420;     // the header row's at most (its label: headerRect)
static const double POINT_PX = 4.5;       // a breakpoint's radius on screen
static const char* SETTING = "ui/canvas/automationLanes";

using namespace Automation;

AutomationLanes::AutomationLanes(ScoreView* view)
   : QObject(view), _view(view)
      {
      }

bool AutomationLanes::enabled()
      {
      return QSettings().value(SETTING, true).toBool();
      }

void AutomationLanes::setEnabled(bool on)
      {
      QSettings().setValue(SETTING, on);
      }

Score* AutomationLanes::score() const
      {
      return _view ? _view->score() : nullptr;
      }

//---------------------------------------------------------
//   state
//---------------------------------------------------------

void AutomationLanes::scoreChanged()
      {
      _anchorsValid = false;
      _cachedTag = QString("\x01");           // read again
      _rowsValid = false;
      _targets.clear();
      _working = false;
      _work.clear();
      _drag = Drag::NONE;
      // (the selection's indices may point elsewhere now)
      Score* s = score();
      if (_selPart && s) {
            const Lane l = lane(_selPart, _selTarget);
            std::vector<int> keep;
            for (int i : _sel)
                  if (i < int(l.points.size()))
                        keep.push_back(i);
            _sel = keep;
            }
      std::set<const Part*> parts;
      if (s)
            for (const Part* p : s->parts())
                  parts.insert(p);
      for (auto i = _unfolded.begin(); i != _unfolded.end(); )
            i = parts.count(*i) ? std::next(i) : _unfolded.erase(i);
      if (_lastSelected && !parts.count(_lastSelected))
            _lastSelected = nullptr;
      _view->update();
      }

void AutomationLanes::layoutChanged()
      {
      _anchorsValid = false;
      _rowsValid = false;
      _targets.clear();
      }

void AutomationLanes::unfold(const Part* part, bool on)
      {
      if (on)
            _unfolded.insert(part);
      else
            _unfolded.erase(part);
      updateSpace();
      }

void AutomationLanes::showLane(const Part* part, const QString& target, bool on)
      {
      const Part* master = PartPlaybackModes::masterPart(part);
      if (on) {
            _added[master].insert(target);
            _hidden[master].erase(target);
            }
      else {
            _added[master].erase(target);
            _hidden[master].insert(target);
            }
      updateSpace();
      }

void AutomationLanes::selectionChanged()
      {
      Score* s = score();
      if (!s || !enabled() || !s->lineMode())
            return;
      const Part* part = nullptr;
      const Selection& sel = s->selection();
      if (sel.isRange()) {
            if (sel.staffStart() >= 0 && sel.staffStart() < s->nstaves()
                && s->staff(sel.staffStart())->part() == s->staff(std::max(sel.staffStart(), sel.staffEnd() - 1))->part())
                  part = s->staff(sel.staffStart())->part();
            }
      else if (sel.element() && sel.element()->staff())
            part = sel.element()->staff()->part();
      if (!part || part == _lastSelected)
            return;
      _lastSelected = part;
      if (targets(part).empty())
            return;                 // (not a part the library plays)
      _unfolded.clear();
      _unfolded.insert(part);
      updateSpace();
      }

std::vector<AutomationLanes::Target> AutomationLanes::targets(const Part* part) const
      {
      const int gen = LiveClipEdit::TrackParams::instance()->generation();
      if (gen != _targetsGeneration) {          // (a Live track's parameters came, a clip tab's lanes became ready)
            _targetsGeneration = gen;
            _targets.clear();
            }
      auto cached = _targets.find(part);
      if (cached != _targets.end())
            return cached->second;
      std::vector<Target> out = computeTargets(part);
      _targets[part] = out;
      return out;
      }

std::vector<AutomationLanes::Target> AutomationLanes::computeTargets(const Part* part) const
      {
      std::vector<Target> out;
      Score* s = score();
      if (!s || !part)
            return out;
      // an Edit-in-MuseScore clip tab: the clip's track's Live parameters, whatever plays there (no sound library)
      LiveIntegration::LiveClipEditor* ed = LiveIntegration::LiveClipEditor::instance();
      if (ed->isClipScore(s)) {
            // the notes' velocities first (always: liveclipmodel.h › The Velocity lane)
            out.push_back({ LiveClipEdit::VELOCITY_TARGET, tr("Velocity"), false, false });
            if (const std::vector<LiveClipEdit::LiveParam>* lp = ed->liveParams(s))
                  for (const LiveClipEdit::LiveParam& p : *lp)
                        out.push_back({ LiveClipEdit::liveTarget(p.d, p.p), p.name, false, true });
            return out;
            }
      out = libraryTargets(part);
      // a part Live plays (Live plays the score): its Live track's parameters too
      const QString key = LiveIntegration::LiveClipsLink::instance()->routeKey(part);
      if (!key.isEmpty())
            if (const std::vector<LiveClipEdit::LiveParam>* lp = LiveClipEdit::TrackParams::instance()->params(key))
                  for (const LiveClipEdit::LiveParam& p : *lp)
                        out.push_back({ LiveClipEdit::liveTarget(p.d, p.p), tr("Live: %1").arg(p.name), false, true });
      return out;
      }

std::vector<AutomationLanes::Target> AutomationLanes::libraryTargets(const Part* part) const
      {
      std::vector<Target> out;
      Score* s = score();
      const std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
      if (!s || !lib || !part)
            return out;
      const Part* master = PartPlaybackModes::masterPart(part);
      std::vector<SoundLib::Controller> ctrls;
      bool any = false;
      for (const SoundLib::Route& r : SoundLib::routes(s->masterScore(), *lib)) {
            if (r.part != master || r.lane != 0 || !r.instrument)
                  continue;
            any = true;
            for (const SoundLib::Controller& c : r.instrument->allControllers) {
                  bool have = false;
                  for (const SoundLib::Controller& d : ctrls)
                        have = have || d.id == c.id;
                  if (!have)
                        ctrls.push_back(c);
                  }
            }
      if (!any)
            return out;
      auto byCC = [&ctrls](int cc) -> const SoundLib::Controller* {
            for (const SoundLib::Controller& c : ctrls)
                  if (c.cc == cc)
                        return &c;
            return nullptr;
            };
      std::set<QString> taken;
      if (lib->dynamicsCC >= 0 && lib->dynamicsCC <= 127) {
            const SoundLib::Controller* c = byCC(lib->dynamicsCC);
            out.push_back({ c ? c->id : QString("cc%1").arg(lib->dynamicsCC), tr("Dynamics (CC%1)").arg(lib->dynamicsCC), false });
            taken.insert(out.back().id);
            }
      if (lib->dynamicsCC != 11) {
            const SoundLib::Controller* c = byCC(11);
            out.push_back({ c ? c->id : QString("cc11"), tr("Expression (CC11)"), false });
            taken.insert(out.back().id);
            }
      for (const SoundLib::Controller& c : ctrls) {
            if (taken.count(c.id))
                  continue;
            out.push_back({ c.id, c.name.isEmpty() ? c.id : c.name, c.cc < 0 && !c.param.isEmpty() });
            taken.insert(c.id);
            }
      return out;
      }

const std::map<const Part*, PartLanes>& AutomationLanes::lanes() const
      {
      if (_working)
            return _work;
      Score* s = score();
      const QString tag = s ? s->masterScore()->metaTag(Automation::metaTag) : QString();
      if (tag != _cachedTag) {
            _cachedTag = tag;
            _cached = s ? Automation::read(s->masterScore()) : std::map<const Part*, PartLanes>();
            }
      return _cached;
      }

Lane AutomationLanes::lane(const Part* master, const QString& target) const
      {
      const auto& all = lanes();
      auto it = all.find(master);
      if (it != all.end())
            for (const Lane& l : it->second)
                  if (l.target == target)
                        return l;
      Lane l;
      l.target = target;
      return l;
      }

void AutomationLanes::setLane(const Part* master, const Lane& l)
      {
      if (!_working) {
            _work = lanes();
            _working = true;
            }
      PartLanes& pl = _work[master];
      for (Lane& x : pl)
            if (x.target == l.target) {
                  x = l;
                  return;
                  }
      pl.push_back(l);
      }

// the working copy as one undoable step
void AutomationLanes::commit(const QString& what)
      {
      if (!_working)
            return;
      std::map<const Part*, PartLanes> all = _work;
      _working = false;
      _work.clear();
      Score* s = score();
      if (!s)
            return;
      const std::map<const Part*, PartLanes>& stored = lanes();
      const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
      for (auto& pl : all) {
            PartLanes keep;
            for (Lane l : pl.second) {
                  // (an empty lane goes; the Velocity lane keeps its settings: scale or absolute, shaped or written)
                  if (l.points.empty() && !(l.target == LiveClipEdit::VELOCITY_TARGET
                                            && (l.extra.contains("velocityMode") || l.extra.contains("velocityOutput"))))
                        continue;
                  // when it was last changed here (the Live conflict dialog shows it)
                  bool same = false;
                  auto s = stored.find(pl.first);
                  if (s != stored.end())
                        for (const Lane& o : s->second)
                              same = same || (o.target == l.target && pointsHash(o.points) == pointsHash(l.points));
                  if (!same)
                        l.extra["edited"] = now;
                  if (l.target.startsWith("live:") && !l.extra.contains("name"))      // (shown when the track isn't known)
                        for (const Target& t : targets(pl.first))
                              if (t.id == l.target)
                                    l.extra["name"] = t.name;
                  keep.push_back(l);
                  }
            pl.second = keep;
            }
      if (seq)
            seq->waitForRendering();      // (the background renderer reads the metaTag)
      if (Automation::undoWrite(s->masterScore(), all) && mscore)
            mscore->showMessage(what, 1500);
      _cachedTag = QString("\x01");
      updateSpace();
      _view->update();
      }

std::vector<QString> AutomationLanes::shownTargets(const Part* master, const std::vector<Target>& all) const
      {
      std::vector<QString> out;
      const auto& ls = lanes();
      auto it = ls.find(master);
      const PartLanes empty;
      const PartLanes& pl = it == ls.end() ? empty : it->second;
      auto has = [&pl](const QString& t) {
            for (const Lane& l : pl)
                  if (l.target == t && !l.points.empty())
                        return true;
            return false;
            };
      const bool every = _showAll.count(master) > 0;
      auto addedIt = _added.find(master);
      auto hiddenIt = _hidden.find(master);
      for (const Target& t : all) {
            const bool hidden = hiddenIt != _hidden.end() && hiddenIt->second.count(t.id);
            const bool added = addedIt != _added.end() && addedIt->second.count(t.id);
            if (every || (!hidden && (has(t.id) || added)))
                  out.push_back(t.id);
            }
      // lanes of controllers this part's patches don't list (a Live import's raw CC …)
      for (const Lane& l : pl) {
            if (l.points.empty() || std::find(out.begin(), out.end(), l.target) != out.end())
                  continue;
            bool listed = false;
            for (const Target& t : all)
                  listed = listed || t.id == l.target;
            if (!listed && !(hiddenIt != _hidden.end() && hiddenIt->second.count(l.target)))
                  out.push_back(l.target);
            }
      return out;
      }

void AutomationLanes::updateSpace()
      {
      Score* s = score();
      if (!s)
            return;
      std::map<const Part*, qreal> space;
      if (enabled() && s->lineMode()) {
            for (const Part* p : _unfolded) {
                  const std::vector<Target> t = targets(p);
                  if (t.empty())
                        continue;
                  const size_t n = _folded.count(p) ? 0 : shownTargets(PartPlaybackModes::masterPart(p), t).size();
                  space[p] = HEAD_SP + double(n) * (LANE_SP + GAP_SP);
                  }
            }
      _rowsValid = false;
      if (space == s->automationSpace()) {
            _view->update();
            return;
            }
      s->setAutomationSpace(space);
      _anchorsValid = false;
      s->setLayoutAll();
      s->update();
      _view->update();
      }

//---------------------------------------------------------
//   geometry
//---------------------------------------------------------

std::vector<AutomationLanes::Row> AutomationLanes::rows() const
      {
      if (_rowsValid)
            return _rows;
      _rows = computeRows();
      _rowsValid = true;
      return _rows;
      }

std::vector<AutomationLanes::Row> AutomationLanes::computeRows() const
      {
      std::vector<Row> out;
      Score* s = score();
      if (!s || !enabled() || !s->lineMode() || s->automationSpace().empty())
            return out;
      Measure* m = s->firstMeasureMM();
      System* system = m ? m->system() : nullptr;
      if (!system)
            return out;
      const qreal sp = s->spatium();
      const QPointF origin = system->canvasPos();
      buildAnchors();
      const double x0 = _anchors.empty() ? origin.x() : std::min(origin.x(), _anchors.front().second - sp);    // (from the system's left edge: no gap after the header box)
      double x1 = origin.x() + system->width();
      if (!_anchors.empty())
            x1 = std::max(x1, _anchors.back().second);
      for (const Part* part : s->parts()) {
            auto sp1 = s->automationSpace().find(part);
            if (sp1 == s->automationSpace().end())
                  continue;
            qreal lanesY = -1;
            for (const Staff* st : *part->staves())
                  if (st->idx() < system->staves()->size() && system->staff(st->idx())->lanesY() >= 0)
                        lanesY = system->staff(st->idx())->lanesY();
            if (lanesY < 0)
                  continue;
            const Part* master = PartPlaybackModes::masterPart(part);
            const std::vector<Target> all = targets(part);
            double y = origin.y() + lanesY;
            Row head;
            head.part = part;
            head.master = master;
            head.name = part->partName();
            head.rect = QRectF(x0, y, x1 - x0, HEAD_SP * sp);
            out.push_back(head);
            y += HEAD_SP * sp;
            for (const QString& t : _folded.count(part) ? std::vector<QString>() : shownTargets(master, all)) {
                  Row r;
                  r.part = part;
                  r.master = master;
                  r.target = t;
                  r.name = t;
                  bool listed = false;
                  for (const Target& x : all)
                        if (x.id == t)
                              r.name = x.name, r.param = x.param, listed = true;
                  if (!listed) {                // (a Live parameter of a track not known now: the name it was given)
                        const QString n = lane(master, t).extra.value("name").toString();
                        if (!n.isEmpty())
                              r.name = n;
                        }
                  r.rect = QRectF(x0, y, x1 - x0, LANE_SP * sp);
                  out.push_back(r);
                  y += (LANE_SP + GAP_SP) * sp;
                  }
            }
      return out;
      }

void AutomationLanes::buildAnchors() const
      {
      if (_anchorsValid)
            return;
      _anchors = Automation::timeAxis(score());
      _anchorsValid = true;
      }

double AutomationLanes::tickToX(int tick) const
      {
      buildAnchors();
      return Automation::xAtTick(_anchors, tick);
      }

int AutomationLanes::xToTick(double x) const
      {
      buildAnchors();
      if (_anchors.empty())
            return 0;
      if (x <= _anchors.front().second)
            return _anchors.front().first;
      if (x >= _anchors.back().second)
            return _anchors.back().first;
      for (size_t i = 1; i < _anchors.size(); ++i) {
            const auto& a = _anchors[i - 1];
            const auto& b = _anchors[i];
            if (x >= a.second && x <= b.second) {
                  if (b.second - a.second < 1e-9 || b.first == a.first)
                        return b.first;
                  return a.first + int(std::lround(double(b.first - a.first) * (x - a.second) / (b.second - a.second)));
                  }
            }
      return _anchors.back().first;
      }

double AutomationLanes::pixel() const
      {
      const double m = _view->matrix().m11();
      return m > 0 ? 1.0 / m : 1.0;
      }

// the grid: the finest of a bar, half, quarter … 1/64 whose steps are at least 10 px apart here
int AutomationLanes::gridTicks(int tick) const
      {
      const double px = pixel();
      const double x = tickToX(tick);
      for (int g : { 30, 60, 120, 240, 480, 960, 1920 }) {
            if ((tickToX(tick + g) - x) / px >= 10)
                  return g;
            }
      Score* s = score();
      const Measure* m = s ? s->tick2measure(Fraction::fromTicks(tick)) : nullptr;
      return m ? m->ticks().ticks() : 1920;
      }

int AutomationLanes::snap(int tick, bool fine) const
      {
      Score* s = score();
      tick = std::max(0, tick);
      if (fine || !s)
            return tick;
      const Measure* m = s->tick2measure(Fraction::fromTicks(tick));
      if (!m)
            return tick;
      const int g = gridTicks(tick);
      const int t0 = m->tick().ticks();
      int t = t0 + int(std::lround(double(tick - t0) / g)) * g;
      return std::min(t, m->endTick().ticks());
      }

QRectF AutomationLanes::valueRect(const Row& r) const
      {
      const double pad = 0.45 * (score() ? score()->spatium() : 5.0);
      return r.rect.adjusted(0, pad, 0, -pad);
      }

double AutomationLanes::valueAtY(const Row& r, double y) const
      {
      const QRectF v = valueRect(r);
      return std::min(1.0, std::max(0.0, (v.bottom() - y) / v.height()));
      }

double AutomationLanes::yOfValue(const Row& r, double v) const
      {
      const QRectF vr = valueRect(r);
      return vr.bottom() - v * vr.height();
      }

QString AutomationLanes::valueText(const QString& target, double v) const
      {
      if (target == LiveClipEdit::VELOCITY_TARGET) {
            const Score* s = score();
            const Part* p = s && !s->masterScore()->parts().empty() ? s->masterScore()->parts().front() : nullptr;
            return LiveClipEdit::velocityText(v, LiveClipEdit::velMode(lane(p, target)));
            }
      const double x = v * 127;
      return std::fabs(x - std::round(x)) < 0.05 ? QString::number(int(std::lround(x))) : QString::number(x, 'f', 1);
      }

// the header row's label, and its font (bold, as tall as the row allows)
QString AutomationLanes::headLabel(const Row& r) const
      {
      return QString::fromUtf8(_folded.count(r.part) ? "▸ " : "▾ ") + tr("Automation") + QString::fromUtf8(" · ") + r.name;
      }

static QFont headFont(double height)
      {
      QFont f = QApplication::font();
      f.setPixelSize(std::max(9, std::min(13, int(height * 0.42))));
      f.setBold(true);
      return f;
      }

QRectF AutomationLanes::headerRect(const Row& r) const
      {
      const QRectF vr = _view->matrix().mapRect(r.rect);
      if (!r.target.isEmpty())
            return QRectF(0, vr.top(), HEADER_PX, vr.height());
      // the header row as wide as its label needs (the part's name whole: "Automation · Violin" was cut to "Vio…"),
      // with room for its three buttons; at most HEADER_MAX_PX (then the name is shortened in the middle)
      const int label = QFontMetrics(headFont(vr.height())).horizontalAdvance(headLabel(r));
      return QRectF(0, vr.top(), std::max(HEADER_PX + 90, std::min(HEADER_MAX_PX, 6 + label + 12 + 84)), vr.height());
      }

const AutomationLanes::Row* AutomationLanes::rowAt(const QPointF& canvas) const
      {
      rows();
      for (const Row& r : _rows)
            if (r.rect.contains(canvas))
                  return &r;
      return nullptr;
      }

int AutomationLanes::pointAt(const Row& r, const Lane& l, const QPointF& p) const
      {
      const double rad = POINT_PX * 1.6 * pixel();
      int best = -1;
      double bestD = rad;
      for (int i = 0; i < int(l.points.size()); ++i) {
            const QPointF q(tickToX(l.points[size_t(i)].tick), yOfValue(r, l.points[size_t(i)].value));
            const double d = std::hypot(q.x() - p.x(), q.y() - p.y());
            if (d <= bestD) {
                  bestD = d;
                  best = i;
                  }
            }
      return best;
      }

// the segment a tick is on: the last point at or before it, if one comes after it
int AutomationLanes::segmentAt(const Lane& l, int tick) const
      {
      int s = -1;
      for (int i = 0; i < int(l.points.size()); ++i)
            if (l.points[size_t(i)].tick <= tick)
                  s = i;
      return s >= 0 && s + 1 < int(l.points.size()) ? s : -1;
      }

//---------------------------------------------------------
//   paint
//---------------------------------------------------------

void AutomationLanes::paint(QPainter& p, const QRect& viewport)
      {
      const std::vector<Row> rs = rows();
      if (rs.empty())
            return;
      if (!_view->hasMouseTracking())
            _view->setMouseTracking(true);        // (the value under the mouse, Ctrl+V where it is)
      const QRectF visible = _view->toLogical(QRectF(viewport));
      p.save();
      p.setTransform(_view->matrix());
      for (const Row& r : rs)
            if (r.rect.intersects(visible))
                  paintLane(p, r, visible);
      // the rubber band
      if (_drag == Drag::RUBBER && !_rubber.isEmpty()) {
            p.setPen(QPen(QColor(47, 109, 181), 1.0 * pixel(), Qt::DashLine));
            p.setBrush(QColor(47, 109, 181, 30));
            p.drawRect(_rubber);
            }
      // the value at the mouse
      if (!_hoverText.isEmpty() && _hover.x() >= 0) {
            QFont f = p.font();
            f.setPixelSize(11);
            f.setPointSizeF(11 * pixel());
            p.setFont(f);
            const QFontMetricsF fm(f);
            const QRectF tr = fm.boundingRect(_hoverText).adjusted(-3 * pixel(), -1 * pixel(), 3 * pixel(), 1 * pixel());
            const QRectF box(_hover.x() + 8 * pixel(), _hover.y() - tr.height() - 4 * pixel(), tr.width(), tr.height());
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(29, 79, 140));
            p.drawRoundedRect(box, 2 * pixel(), 2 * pixel());
            p.setPen(Qt::white);
            p.drawText(box, Qt::AlignCenter, _hoverText);
            }
      p.restore();
      // the headers, fixed at the view's left
      p.save();
      p.resetTransform();
      for (const Row& r : rs)
            if (headerRect(r).intersects(QRectF(viewport)))
                  paintHeader(p, r);
      p.restore();
      }

void AutomationLanes::paintLane(QPainter& p, const Row& r, const QRectF& visible) const
      {
      const double px = pixel();
      if (r.target.isEmpty()) {
            // the header row: a band
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(222, 219, 214));
            p.drawRect(r.rect);
            return;
            }
      const Lane l = lane(r.master, r.target);
      const bool live = !l.points.empty() && l.playedByLive();
      const QColor ink = live ? QColor(118, 72, 160) : (r.param ? QColor(200, 100, 30) : QColor(47, 109, 181));
      const QColor bg = live ? QColor(246, 241, 250) : (r.param ? QColor(251, 245, 239) : QColor(242, 246, 251));
      p.setPen(Qt::NoPen);
      p.setBrush(bg);
      p.drawRect(r.rect);
      // the grid: bars, beats
      const double xa = std::max(r.rect.left(), visible.left());
      const double xb = std::min(r.rect.right(), visible.right());
      Score* s = score();
      const int ta = xToTick(xa);
      const int tb = xToTick(xb);
      for (Measure* m = s->tick2measureMM(Fraction::fromTicks(ta)); m && m->tick().ticks() <= tb; m = m->nextMeasureMM()) {
            const int beat = 1920 / std::max(1, m->timesig().denominator());
            for (int t = m->tick().ticks(); t < m->endTick().ticks(); t += beat) {
                  // (a bar: where the staff's bar line is; the beats where their notes are)
                  const bool bar = t == m->tick().ticks();
                  const Measure* prev = bar ? m->prevMeasureMM() : nullptr;
                  const double x = prev && prev->system() == m->system() ? Automation::barLineX(prev) : tickToX(t);
                  p.setPen(QPen(bar ? QColor(170, 170, 170) : QColor(220, 220, 220), px));
                  p.drawLine(QPointF(x, r.rect.top()), QPointF(x, r.rect.bottom()));
                  }
            }
      p.setPen(QPen(QColor(200, 200, 200), px));
      p.setBrush(Qt::NoBrush);
      p.drawRect(r.rect);
      if (l.points.empty())
            return;
      // the envelope
      QPainterPath path;
      const double xFirst = tickToX(l.points.front().tick);
      path.moveTo(xFirst, yOfValue(r, l.points.front().value));
      for (size_t i = 0; i < l.points.size(); ++i) {
            const Point& a = l.points[i];
            const double xa1 = tickToX(a.tick);
            const double ya = yOfValue(r, a.value);
            path.lineTo(xa1, ya);
            if (i + 1 == l.points.size())
                  break;
            const Point& b = l.points[i + 1];
            const double xb1 = tickToX(b.tick);
            if (a.curve == Curve::STEP) {
                  path.lineTo(xb1, ya);
                  }
            else if (a.curved()) {
                  const int n = std::max(8, std::min(64, int((xb1 - xa1) / (3 * px))));
                  for (int k = 1; k < n; ++k) {
                        const int t = a.tick + int(std::lround(double(b.tick - a.tick) * k / n));
                        path.lineTo(tickToX(t), yOfValue(r, l.valueAt(t)));
                        }
                  }
            }
      path.lineTo(r.rect.right(), yOfValue(r, l.points.back().value));
      QPainterPath fill = path;
      fill.lineTo(r.rect.right(), r.rect.bottom());
      fill.lineTo(xFirst, r.rect.bottom());
      fill.closeSubpath();
      QColor f = ink;
      f.setAlpha(34);
      p.setPen(Qt::NoPen);
      p.setBrush(f);
      p.drawPath(fill);
      // before the first point: says nothing (the controller's own value): dashed
      p.setPen(QPen(ink, 1.0 * px, Qt::DashLine));
      p.drawLine(QPointF(r.rect.left(), yOfValue(r, l.points.front().value)), QPointF(xFirst, yOfValue(r, l.points.front().value)));
      p.setPen(QPen(ink, 1.8 * px));
      p.setBrush(Qt::NoBrush);
      p.drawPath(path);
      // the breakpoints
      const bool selLane = _focus && r.master == _selPart && r.target == _selTarget;
      for (int i = 0; i < int(l.points.size()); ++i) {
            const Point& a = l.points[size_t(i)];
            const QPointF c(tickToX(a.tick), yOfValue(r, a.value));
            if (c.x() < visible.left() - 10 * px || c.x() > visible.right() + 10 * px)
                  continue;
            const bool sel = selLane && std::find(_sel.begin(), _sel.end(), i) != _sel.end();
            p.setPen(QPen(ink, 1.6 * px));
            p.setBrush(sel ? ink : QColor(Qt::white));
            p.drawEllipse(c, POINT_PX * px, POINT_PX * px);
            }
      }

void AutomationLanes::paintHeader(QPainter& p, const Row& r) const
      {
      const QRectF h = headerRect(r);
      QFont f = p.font();
      f.setPixelSize(std::max(9, std::min(13, int(h.height() * 0.42))));
      p.setFont(f);
      if (r.target.isEmpty()) {
            p.fillRect(h, QColor(214, 211, 206));
            p.setPen(QColor(29, 31, 34));
            f.setBold(true);
            p.setFont(f);
            p.setFont(headFont(h.height()));
            p.drawText(h.adjusted(6, 0, -84, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(p.font()).elidedText(headLabel(r), Qt::ElideMiddle, int(h.width()) - 6 - 84 - 4));
            f.setBold(false);
            p.setFont(f);
            // + (add a lane), All, the pencil (Draw Mode)
            const char* labels[] = { "+", "All", "✎" };
            for (int i = 0; i < 3; ++i) {
                  const QRectF b(h.right() - 26 * (3 - i) - 2, h.top() + 2, 24, h.height() - 4);
                  const bool on = (i == 1 && _showAll.count(r.master)) || (i == 2 && _drawMode);
                  p.setPen(QColor(150, 146, 140));
                  p.setBrush(on ? QColor(220, 232, 246) : QColor(243, 242, 239));
                  p.drawRoundedRect(b, 3, 3);
                  p.setPen(on ? QColor(29, 79, 140) : QColor(29, 31, 34));
                  p.drawText(b, Qt::AlignCenter, QString::fromUtf8(labels[i]));
                  }
            return;
            }
      const Lane l = lane(r.master, r.target);
      const bool live = !l.points.empty() && l.playedByLive();
      p.fillRect(h, live ? QColor(236, 228, 244) : (r.param ? QColor(246, 236, 226) : QColor(226, 234, 245)));
      p.setPen(QColor(200, 200, 200));
      p.drawLine(h.topRight(), h.bottomRight());
      p.setPen(QColor(29, 31, 34));
      const QRectF text = h.adjusted(10, 0, -22, 0);
      // its name, and the value where playback is (or at the mouse)
      Score* s = score();
      int at = s ? s->playPos().ticks() : 0;
      if (_hover.x() >= 0 && r.rect.contains(_hover))
            at = xToTick(_hover.x());
      const double v = l.valueAt(at);
      QString line2 = v >= 0 ? valueText(r.target, v) : QString::fromUtf8("–");
      if (live)
            line2 += "  " + tr("Live");
      QString title = r.name;
      if (r.target == LiveClipEdit::VELOCITY_TARGET)        // its unit, and how Live gets it
            title = (LiveClipEdit::velMode(l) == LiveClipEdit::VelMode::SET ? tr("Velocity (1-127)") : tr("Velocity (%)"))
                    + QString::fromUtf8(" · ")
                    + (LiveClipEdit::velOutput(l) == LiveClipEdit::VelOutput::WRITE ? tr("written") : tr("shaped"));
      const QString name = QFontMetrics(p.font()).elidedText(title, Qt::ElideRight, int(text.width()));
      if (h.height() >= 30) {
            p.drawText(text.adjusted(0, 2, 0, -h.height() / 2), Qt::AlignBottom | Qt::AlignLeft, name);
            p.setPen(QColor(85, 88, 92));
            p.drawText(text.adjusted(0, h.height() / 2, 0, -2), Qt::AlignTop | Qt::AlignLeft, line2);
            }
      else
            p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(p.font()).elidedText(title + "  " + line2, Qt::ElideRight, int(text.width())));
      // × hides the lane
      p.setPen(QColor(110, 110, 110));
      p.drawText(QRectF(h.right() - 20, h.top(), 18, h.height()), Qt::AlignCenter, QString::fromUtf8("×"));
      }

//---------------------------------------------------------
//   editing
//---------------------------------------------------------

void AutomationLanes::select(const Row& r, const std::vector<int>& sel)
      {
      _focus = true;
      _selPart = r.master;
      _selTarget = r.target;
      _sel = sel;
      }

void AutomationLanes::dropFocus()
      {
      if (!_focus)
            return;
      _focus = false;
      _sel.clear();
      _view->update();
      }

bool AutomationLanes::headerClick(const QPoint& pixel)
      {
      if (pixel.x() >= HEADER_MAX_PX)
            return false;
      for (const Row& r : rows()) {
            const QRectF h = headerRect(r);
            if (!h.contains(pixel))
                  continue;
            if (r.target.isEmpty()) {
                  for (int i = 0; i < 3; ++i) {
                        const QRectF b(h.right() - 26 * (3 - i) - 2, h.top() + 2, 24, h.height() - 4);
                        if (!b.contains(pixel))
                              continue;
                        if (i == 0)
                              addLaneMenu(r, _view->mapToGlobal(QPoint(int(b.left()), int(b.bottom()))));
                        else if (i == 1) {
                              if (_showAll.count(r.master))
                                    _showAll.erase(r.master);
                              else
                                    _showAll.insert(r.master);
                              _folded.erase(r.part);
                              updateSpace();
                              }
                        else {
                              _drawMode = !_drawMode;
                              _view->update();
                              }
                        return true;
                        }
                  if (pixel.x() < h.left() + 20) {
                        // ▾ folds the lanes away, ▸ shows them again; the header row stays (the owner, 2026-10-04: the
                        // whole panel went, and selecting the part again didn't bring it back)
                        if (_folded.count(r.part))
                              _folded.erase(r.part);
                        else
                              _folded.insert(r.part);
                        updateSpace();
                        return true;
                        }
                  return true;
                  }
            if (pixel.x() >= h.right() - 20) {
                  showLane(r.part, r.target, false);       // × hides
                  return true;
                  }
            return true;
            }
      return false;
      }

void AutomationLanes::addLaneMenu(const Row& r, const QPoint& globalPos)
      {
      QMenu menu;
      const std::vector<Target> all = targets(r.part);
      const std::vector<QString> shown = shownTargets(r.master, all);
      // (a Live track's parameters: a submenu per device, "Operator › Tone"; the library's controls as they are)
      std::map<QString, QMenu*> sub;
      for (const Target& t : all) {
            if (std::find(shown.begin(), shown.end(), t.id) != shown.end())
                  continue;
            const int sep = t.live ? t.name.indexOf(QString::fromUtf8(" › ")) : -1;
            QAction* a = nullptr;
            if (sep > 0) {
                  const QString dev = t.name.left(sep);
                  QMenu*& m = sub[dev];
                  if (!m)
                        m = menu.addMenu(dev);
                  a = m->addAction(t.name.mid(sep + 3));
                  }
            else
                  a = menu.addAction(t.name);
            a->setData(t.id);
            }
      if (menu.isEmpty())
            menu.addAction(tr("(every lane is shown)"))->setEnabled(false);
      QAction* a = menu.exec(globalPos);
      if (a && !a->data().toString().isEmpty()) {
            _folded.erase(r.part);
            showLane(r.part, a->data().toString(), true);
            }
      }

bool AutomationLanes::mousePress(QMouseEvent* ev)
      {
      if (rows().empty())
            return false;
      if (ev->button() != Qt::LeftButton)
            return rowAt(_view->toLogical(ev->pos())) != nullptr && ev->button() == Qt::RightButton;
      if (headerClick(ev->pos()))
            return true;
      const QPointF p = _view->toLogical(ev->pos());
      const Row* rp = rowAt(p);
      if (!rp) {
            dropFocus();
            return false;
            }
      const Row r = *rp;
      if (r.target.isEmpty())
            return true;                  // (the header band)
      if (score() && !score()->selection().isNone() && !_view->noteEntryMode())
            _view->deselectAll();         // (in note input the selection is the input position: kept)
      _dragRow = r;
      _pressPos = p;
      _pressPixel = ev->pos();
      _before = lane(r.master, r.target);
      const bool sameLane = _focus && _selPart == r.master && _selTarget == r.target;
      _beforeSel = sameLane ? _sel : std::vector<int>();
      const Qt::KeyboardModifiers mods = ev->modifiers();
      if (_drawMode) {
            _drag = Drag::DRAW;
            _drawCell = -1;
            select(r, {});
            mouseMove(ev);
            return true;
            }
      const int pt = pointAt(r, _before, p);
      if (pt >= 0) {
            std::vector<int> sel = _beforeSel;
            const bool in = std::find(sel.begin(), sel.end(), pt) != sel.end();
            if (mods & Qt::ControlModifier) {
                  if (in)
                        sel.erase(std::find(sel.begin(), sel.end(), pt));
                  else
                        sel.push_back(pt);
                  select(r, sel);
                  _drag = Drag::NONE;
                  _view->update();
                  return true;
                  }
            if (!in)
                  sel = { pt };
            select(r, sel);
            _grab = pt;
            _drag = Drag::MOVE;
            _working = false;
            _hover = p;
            _hoverText = valueText(r.target, _before.points[size_t(pt)].value);
            _view->update();
            return true;
            }
      const int tick = xToTick(p.x());
      const double lineV = _before.valueAt(tick);
      _nearLine = lineV >= 0 && std::fabs(yOfValue(r, lineV) - p.y()) <= 6 * pixel();
      if ((mods & Qt::AltModifier) && segmentAt(_before, tick) >= 0) {
            _segment = segmentAt(_before, tick);
            _k0 = curvature(_before.points[size_t(_segment)]);
            if (_before.points[size_t(_segment)].curve == Curve::STEP)
                  _k0 = 0;
            _drag = Drag::CURVE;
            select(r, {});
            return true;
            }
      _drag = Drag::PENDING;
      select(r, (mods & Qt::ControlModifier) ? _beforeSel : std::vector<int>());
      _view->update();
      return true;
      }

bool AutomationLanes::mouseMove(QMouseEvent* ev)
      {
      if (_drag == Drag::NONE) {
            hover(ev->pos());
            return false;
            }
      const QPointF p = _view->toLogical(ev->pos());
      const Row& r = _dragRow;
      const Qt::KeyboardModifiers mods = ev->modifiers();
      const bool fineTime = mods & Qt::AltModifier;
      switch (_drag) {
            case Drag::PENDING:
                  if ((ev->pos() - _pressPixel).manhattanLength() <= 4)
                        return true;
                  _drag = Drag::RUBBER;
                  // fall through
            case Drag::RUBBER: {
                  _rubber = QRectF(_pressPos, p).normalized().intersected(r.rect);
                  std::vector<int> sel = (mods & Qt::ControlModifier) ? _beforeSel : std::vector<int>();
                  for (int i = 0; i < int(_before.points.size()); ++i) {
                        const QPointF c(tickToX(_before.points[size_t(i)].tick), yOfValue(r, _before.points[size_t(i)].value));
                        if (_rubber.contains(c) && std::find(sel.begin(), sel.end(), i) == sel.end())
                              sel.push_back(i);
                        }
                  select(r, sel);
                  _view->update();
                  return true;
                  }
            case Drag::MOVE: {
                  if ((ev->pos() - _pressPixel).manhattanLength() <= 2 && !_working)
                        return true;
                  const Point& g = _before.points[size_t(_grab)];
                  const bool fineValue = mods & Qt::ShiftModifier;
                  int dtick = 0;
                  if (!fineValue) {
                        const double gx = tickToX(g.tick) + (p.x() - _pressPos.x());
                        dtick = snap(xToTick(gx), fineTime) - g.tick;
                        }
                  double dv = valueAtY(r, yOfValue(r, g.value) + (p.y() - _pressPos.y())) - g.value;
                  if (fineValue)
                        dv *= 0.1;
                  Lane l = _before;
                  const std::vector<int> moved = Edit::movePoints(l, _sel.empty() ? std::vector<int>{ _grab } : _sel, dtick, dv);
                  setLane(r.master, l);
                  // the grabbed point: where it went
                  std::vector<int> sel = moved;
                  select(r, sel);
                  _hover = QPointF(tickToX(g.tick + dtick), yOfValue(r, std::min(1.0, std::max(0.0, g.value + dv))));
                  _hoverText = valueText(r.target, std::min(1.0, std::max(0.0, g.value + dv)));
                  _view->update();
                  return true;
                  }
            case Drag::CURVE: {
                  // up bows the segment up (rises early), down the other way; a lane's height is the whole range
                  const Point& a = _before.points[size_t(_segment)];
                  const Point& b = _before.points[size_t(_segment + 1)];
                  const double dir = b.value >= a.value ? 1.0 : -1.0;
                  const double k = std::max(-1.0, std::min(1.0, _k0 + dir * (_pressPos.y() - p.y()) / valueRect(r).height() * 2));
                  Lane l = _before;
                  Edit::setSegmentCurvature(l, _segment, k);
                  setLane(r.master, l);
                  _hover = p;
                  _hoverText = tr("curve %1").arg(int(std::lround(k * 100)));
                  _view->update();
                  return true;
                  }
            case Drag::DRAW: {
                  const int tick = xToTick(p.x());
                  Score* s = score();
                  const Measure* m = s ? s->tick2measure(Fraction::fromTicks(std::max(0, tick))) : nullptr;
                  if (!m)
                        return true;
                  const int g = fineTime ? 30 : gridTicks(tick);
                  const int t0 = m->tick().ticks() + (tick - m->tick().ticks()) / g * g;
                  const int t1 = std::min(t0 + g, m->endTick().ticks());
                  Lane l = lane(r.master, r.target);
                  const double v = valueAtY(r, p.y());
                  Edit::drawStep(l, t0, t1, v, &_before);
                  setLane(r.master, l);
                  _drawCell = t0;
                  _hover = p;
                  _hoverText = valueText(r.target, v);
                  _view->update();
                  return true;
                  }
            case Drag::NONE:
                  break;
            }
      return true;
      }

bool AutomationLanes::mouseRelease(QMouseEvent* ev)
      {
      if (_drag == Drag::NONE)
            return false;
      const Drag d = _drag;
      _drag = Drag::NONE;
      _rubber = QRectF();
      const Row r = _dragRow;
      switch (d) {
            case Drag::PENDING: {
                  // a click: a breakpoint (on the line: on it; elsewhere at the mouse's value)
                  Lane l = _before;
                  const int tick = snap(xToTick(_pressPos.x()), ev->modifiers() & Qt::AltModifier);
                  const int i = _nearLine ? Edit::addPointOnLine(l, tick) : Edit::addPoint(l, tick, valueAtY(r, _pressPos.y()));
                  setLane(r.master, l);
                  select(r, { i });
                  _lastAddedTick = tick;
                  _lastAddedAt = QDateTime::currentMSecsSinceEpoch();
                  commit(tr("Automation: point added"));
                  return true;
                  }
            case Drag::MOVE:
                  _hoverText.clear();
                  commit(tr("Automation: moved"));
                  return true;
            case Drag::CURVE:
                  _hoverText.clear();
                  commit(tr("Automation: curve"));
                  return true;
            case Drag::DRAW:
                  _hoverText.clear();
                  commit(tr("Automation: drawn"));
                  return true;
            case Drag::RUBBER:
            case Drag::NONE:
                  _view->update();
                  return true;
            }
      return true;
      }

bool AutomationLanes::mouseDoubleClick(QMouseEvent* ev)
      {
      const QPointF p = _view->toLogical(ev->pos());
      const Row* rp = rowAt(p);
      if (!rp || rp->target.isEmpty())
            return rp != nullptr;
      const Row r = *rp;
      Lane l = lane(r.master, r.target);
      const int tick = xToTick(p.x());
      if (ev->modifiers() & Qt::AltModifier) {
            const int s = segmentAt(l, tick);
            if (s >= 0) {
                  Edit::setSegmentCurvature(l, s, 0);
                  setLane(r.master, l);
                  commit(tr("Automation: straight"));
                  }
            return true;
            }
      const int pt = pointAt(r, l, p);
      if (pt >= 0) {
            // (the point the double-click's first click added stays)
            const bool justAdded = l.points[size_t(pt)].tick == _lastAddedTick
                                   && QDateTime::currentMSecsSinceEpoch() - _lastAddedAt < 2 * QApplication::doubleClickInterval();
            _lastAddedTick = -1;
            if (justAdded)
                  return true;
            Edit::removePoints(l, { pt });
            setLane(r.master, l);
            select(r, {});
            commit(tr("Automation: point removed"));
            }
      return true;
      }

void AutomationLanes::hover(const QPoint& pos)
      {
      if (rows().empty())
            return;
      const QPointF p = _view->toLogical(pos);
      const Row* r = rowAt(p);
      const QPointF old = _hover;
      const QString oldText = _hoverText;
      _hover = QPointF(-1, -1);
      _hoverText.clear();
      if (r && !r->target.isEmpty() && !headerRect(*r).contains(pos)) {
            _hover = p;
            const Lane l = lane(r->master, r->target);
            const int pt = pointAt(*r, l, p);
            if (pt >= 0)
                  _hoverText = valueText(r->target, l.points[size_t(pt)].value);
            }
      if (old != _hover || oldText != _hoverText)
            _view->update();
      }

bool AutomationLanes::contextMenu(const QPoint& pos, const QPoint& globalPos)
      {
      const QPointF p = _view->toLogical(pos);
      const Row* rp = rowAt(p);
      if (!rp)
            return false;
      const Row r = *rp;
      if (r.target.isEmpty()) {
            addLaneMenu(r, globalPos);
            return true;
            }
      Lane l = lane(r.master, r.target);
      const int pt = pointAt(r, l, p);
      const int tick = xToTick(p.x());
      const int seg = pt >= 0 ? (pt + 1 < int(l.points.size()) ? pt : -1) : segmentAt(l, tick);
      QMenu menu;
      QAction* editValue = nullptr;
      QAction* del = nullptr;
      QAction* step = nullptr;
      QAction* linear = nullptr;
      QAction* straight = nullptr;
      QAction* add = nullptr;
      if (pt >= 0) {
            editValue = menu.addAction(tr("Edit Value…"));
            del = menu.addAction(tr("Delete"));
            }
      else
            add = menu.addAction(tr("Add Value…"));
      if (seg >= 0) {
            menu.addSeparator();
            step = menu.addAction(tr("Step"));
            step->setCheckable(true);
            step->setChecked(l.points[size_t(seg)].curve == Curve::STEP);
            linear = menu.addAction(tr("Ramp"));
            linear->setCheckable(true);
            linear->setChecked(l.points[size_t(seg)].curve == Curve::LINEAR);
            if (l.points[size_t(seg)].curved())
                  straight = menu.addAction(tr("Straight"));
            }
      // the Velocity lane: scale or absolute (one curve, read either way), shaped in Live or written into the notes
      const bool velocity = r.target == LiveClipEdit::VELOCITY_TARGET;
      QAction* scale = nullptr;
      QAction* absolute = nullptr;
      QAction* shape = nullptr;
      QAction* writeNotes = nullptr;
      if (velocity) {
            menu.addSeparator();
            scale = menu.addAction(tr("Scale the Notes' Velocities (0-200 %)"));
            absolute = menu.addAction(tr("Set the Velocities (1-127)"));
            menu.addSeparator();
            shape = menu.addAction(tr("Shape While Playing (Notes Unchanged)"));
            writeNotes = menu.addAction(tr("Write into the Notes"));
            for (QAction* x : { scale, absolute, shape, writeNotes })
                  x->setCheckable(true);
            scale->setChecked(LiveClipEdit::velMode(l) == LiveClipEdit::VelMode::SCALE);
            absolute->setChecked(!scale->isChecked());
            shape->setChecked(LiveClipEdit::velOutput(l) == LiveClipEdit::VelOutput::SHAPE);
            writeNotes->setChecked(!shape->isChecked());
            }
      menu.addSeparator();
      QAction* clear = l.points.empty() ? nullptr : menu.addAction(tr("Clear Lane"));
      QAction* hide = menu.addAction(tr("Hide Lane"));
      QAction* a = menu.exec(globalPos);
      if (!a)
            return true;
      const LiveClipEdit::VelMode vm = LiveClipEdit::velMode(l);
      if (a == scale || a == absolute) {
            LiveClipEdit::setVelMode(l, a == scale ? LiveClipEdit::VelMode::SCALE : LiveClipEdit::VelMode::SET);
            setLane(r.master, l);
            commit(a == scale ? tr("Velocity: scales the notes' velocities") : tr("Velocity: sets the velocities"));
            }
      else if (a == shape || a == writeNotes) {
            if (a == writeNotes && LiveClipEdit::velOutput(l) != LiveClipEdit::VelOutput::WRITE
                && !LiveIntegration::LiveClipEditor::confirmVelocityWrite(_view))
                  return true;
            LiveClipEdit::setVelOutput(l, a == shape ? LiveClipEdit::VelOutput::SHAPE : LiveClipEdit::VelOutput::WRITE);
            setLane(r.master, l);
            commit(a == shape ? tr("Velocity: shaped while playing") : tr("Velocity: written into the notes"));
            }
      else if (a == editValue || a == add) {
            bool ok = false;
            const double start = a == add ? std::max(0.0, l.valueAt(tick)) : l.points[size_t(pt)].value;
            // (shown as the lane shows it: 0-127; the Velocity lane % or 1-127)
            const double shown = velocity ? LiveClipEdit::velocityShown(start, vm) : start * 127;
            const double hi = velocity && vm == LiveClipEdit::VelMode::SCALE ? 200 : 127;
            const QString label = !velocity ? tr("Value (0-127):")
                                  : vm == LiveClipEdit::VelMode::SCALE ? tr("Velocity (0-200 %):") : tr("Velocity (1-127):");
            const double x = QInputDialog::getDouble(_view, r.name, label, shown, velocity && vm == LiveClipEdit::VelMode::SET ? 1 : 0,
                                                     hi, 1, &ok);
            if (!ok)
                  return true;
            const double v = velocity ? LiveClipEdit::velocityFromShown(x, vm) : x / 127;
            if (a == add)
                  Edit::addPoint(l, snap(tick, false), v);
            else {
                  // the selected points move by as much (Live: "they will all be moved relatively")
                  std::vector<int> sel = (_focus && _selPart == r.master && _selTarget == r.target && !_sel.empty()) ? _sel : std::vector<int>{ pt };
                  if (std::find(sel.begin(), sel.end(), pt) == sel.end())
                        sel = { pt };
                  Edit::movePoints(l, sel, 0, v - l.points[size_t(pt)].value);
                  }
            setLane(r.master, l);
            commit(tr("Automation: value"));
            }
      else if (a == del) {
            Edit::removePoints(l, { pt });
            setLane(r.master, l);
            select(r, {});
            commit(tr("Automation: point removed"));
            }
      else if (a == step || a == linear) {
            Edit::setSegmentCurve(l, seg, a == step ? Curve::STEP : Curve::LINEAR);
            setLane(r.master, l);
            commit(tr("Automation: segment"));
            }
      else if (a == straight) {
            Edit::setSegmentCurvature(l, seg, 0);
            setLane(r.master, l);
            commit(tr("Automation: straight"));
            }
      else if (a == clear) {
            l.points.clear();
            setLane(r.master, l);
            select(r, {});
            commit(tr("Automation: lane cleared"));
            }
      else if (a == hide)
            showLane(r.part, r.target, false);
      return true;
      }

bool AutomationLanes::wantsKey(const QKeyEvent* ev) const
      {
      if (!_focus)
            return false;
      const Qt::KeyboardModifiers m = ev->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
      switch (ev->key()) {
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
            case Qt::Key_Escape:
                  return m == 0;
            case Qt::Key_C:
            case Qt::Key_X:
            case Qt::Key_V:
            case Qt::Key_D:
                  return m == Qt::ControlModifier;
            default:
                  return false;
            }
      }

bool AutomationLanes::keyPress(QKeyEvent* ev)
      {
      if (!wantsKey(ev))
            return false;
      Lane l = lane(_selPart, _selTarget);
      Row r;
      for (const Row& x : rows())
            if (x.master == _selPart && x.target == _selTarget)
                  r = x;
      switch (ev->key()) {
            case Qt::Key_Escape:
                  dropFocus();
                  return true;
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
                  if (_sel.empty())
                        return true;
                  Edit::removePoints(l, _sel);
                  setLane(_selPart, l);
                  _sel.clear();
                  commit(tr("Automation: points removed"));
                  return true;
            case Qt::Key_C:
            case Qt::Key_X:
                  if (_sel.empty())
                        return true;
                  _clipboard = Edit::copyPoints(l, _sel);
                  if (ev->key() == Qt::Key_X) {
                        Edit::removePoints(l, _sel);
                        setLane(_selPart, l);
                        _sel.clear();
                        commit(tr("Automation: cut"));
                        }
                  else if (mscore)
                        mscore->showMessage(tr("Automation: %n point(s) copied", "", int(_clipboard.size())), 1500);
                  return true;
            case Qt::Key_D:
            case Qt::Key_V: {
                  std::vector<Point> clip = _clipboard;
                  int at = -1;
                  const Part* toPart = _selPart;
                  QString toTarget = _selTarget;
                  if (ev->key() == Qt::Key_D) {
                        if (_sel.empty())
                              return true;
                        clip = Edit::copyPoints(l, _sel);
                        int last = 0;
                        for (int i : _sel)
                              last = std::max(last, l.points[size_t(i)].tick);
                        at = last + gridTicks(last);          // right after the selection, a grid step on
                        }
                  else {
                        if (clip.empty())
                              return true;
                        const Row* hr = _hover.x() >= 0 ? rowAt(_hover) : nullptr;
                        if (hr && !hr->target.isEmpty()) {
                              toPart = hr->master;
                              toTarget = hr->target;
                              at = snap(xToTick(_hover.x()), false);
                              }
                        else if (!l.points.empty()) {
                              int last = 0;
                              for (int i : _sel)
                                    last = std::max(last, l.points[size_t(i)].tick);
                              at = (_sel.empty() ? l.points.back().tick : last) + gridTicks(last);
                              }
                        else
                              at = 0;
                        }
                  Lane to = lane(toPart, toTarget);
                  const std::vector<int> pasted = Edit::pastePoints(to, at, clip);
                  setLane(toPart, to);
                  _selPart = toPart;
                  _selTarget = toTarget;
                  _sel = pasted;
                  commit(ev->key() == Qt::Key_D ? tr("Automation: duplicated") : tr("Automation: pasted"));
                  return true;
                  }
            default:
                  break;
            }
      return false;
      }

}     // namespace Ms
