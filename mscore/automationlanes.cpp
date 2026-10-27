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
#include "libmscore/lagprobe.h"
#include "notelanes.h"
#include "liveclipedit.h"
#include "liveclips.h"
#include "scoreview.h"
#include "musescore.h"
#include "seq.h"
#include "soundlibraryhost.h"

#include "libmscore/measure.h"
#include "libmscore/page.h"
#include "libmscore/partcontrollers.h"
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
#include <QTimer>
#include <QToolTip>
#include <QActionGroup>

namespace Ms {

std::vector<Automation::Point> AutomationLanes::_clipboard;

static const double HEAD_SP = 3.2;        // the part's header row, spatium (the instrument names' 10 pt fit: textPx)
static const double LANE_SP = 5.0;        // a lane
static const double GAP_SP = 0.4;
static const int HEADER_PX = 150;         // the headers' width on screen
static const int HEADER_MIN_PX = 80;      // a lane's at least, left of bar 1 (headerRect)
static const int HEADER_MAX_PX = 420;     // the header row's at most (its label: headerRect)
static const double POINT_PX = 4.5;       // a breakpoint's radius on screen
static const char* SETTING = "ui/canvas/automationLanes";

using namespace Automation;

AutomationLanes::AutomationLanes(ScoreView* view)
   : QObject(view), _view(view)
      {
      _notes = new NoteLanes(this, view);
      SoundLibraryHost::instance();       // (it records the setups' own values: unautomatedValue's)
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
      clearTargets();
      _working = false;
      _work.clear();
      _drag = Drag::NONE;
      _notes->scoreChanged();
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
      _folds.keepOnly(s, parts);          // (the folding stays: only parts gone are dropped)
      queueSpace();                       // (a score opened: its headers)
      _view->update();
      }

void AutomationLanes::layoutChanged()
      {
      _anchorsValid = false;
      _rowsValid = false;
      clearTargets();
      _notes->changed();
      queueSpace();                 // (a part that came to play the library: its header; Continuous View on / off)
      }

// updateSpace once the layout is done (it may lay out again: not from within one)
void AutomationLanes::queueSpace()
      {
      if (_spaceQueued)
            return;
      _spaceQueued = true;
      QTimer::singleShot(0, this, [this]() {
            _spaceQueued = false;
            updateSpace();
            });
      }

void AutomationLanes::clearTargets() const
      {
      _targets.clear();
      _routesValid = false;
      }

// the library's routes, once for every part's header and targets (SoundLib::routes reads the whole score)
const std::vector<SoundLib::Route>& AutomationLanes::libraryRoutes() const
      {
      if (!_routesValid) {
            _routes.clear();
            Score* s = score();
            const std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
            // (several libraries: every one's parts, each with its own controllers; SoundLib::allRoutes)
            if (s && SoundLib::severalLibraries(s->masterScore()))
                  _routes = SoundLib::allRoutes(s->masterScore());
            else if (s && lib)
                  _routes = SoundLib::routes(s->masterScore(), *lib);
            _routesValid = true;
            }
      return _routes;
      }

bool AutomationLanes::isOpen(const Part* part) const
      {
      return _folds.isOpen(score(), part);
      }

void AutomationLanes::unfold(const Part* part, bool on)
      {
      _folds.setOpen(score(), part, on);
      if (on && _notes->fresh())          // (the notes' lanes as the score is now)
            clearTargets();
      updateSpace();
      }

// a header row: a part the library plays, a part Live plays with its track's parameters known, a clip tab's part; without
// rendering the notes (targets() does, for the Velocity and Join lanes: called for open parts only)
bool AutomationLanes::hasHeader(const Part* part) const
      {
      Score* s = score();
      if (!s || !part)
            return false;
      if (LiveIntegration::LiveClipEditor::instance()->isClipScore(s))
            return true;
      const Part* master = PartPlaybackModes::masterPart(part);
      for (const SoundLib::Route& r : libraryRoutes())
            if (r.part == master && r.lane == 0 && r.instrument)
                  return true;
      const QString key = LiveIntegration::LiveClipsLink::instance()->routeKey(part);
      const std::vector<LiveClipEdit::LiveParam>* lp = key.isEmpty() ? nullptr : LiveClipEdit::TrackParams::instance()->params(key);
      return lp && !lp->empty();
      }

std::vector<const Part*> AutomationLanes::headerParts() const
      {
      std::vector<const Part*> out;
      Score* s = score();
      if (!s || !enabled() || !s->lineMode())
            return out;
      for (const Part* p : s->parts())
            if (hasHeader(p))
                  out.push_back(p);
      return out;
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

// (the folding doesn't follow the selection: lanefolds.h; the time selection does, timeSelection)
void AutomationLanes::selectionChanged()
      {
      _timeOff = false;             // (the score's selection is the newer one)
      _view->update();
      }

std::vector<AutomationLanes::Target> AutomationLanes::targets(const Part* part) const
      {
      const int gen = LiveClipEdit::TrackParams::instance()->generation();
      if (gen != _targetsGeneration) {          // (a Live track's parameters came, a clip tab's lanes became ready)
            _targetsGeneration = gen;
            clearTargets();
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
      // the notes' Velocity and Join lanes first, then the library's controls
      out = _notes->targets(PartPlaybackModes::masterPart(part));
      for (const Target& t : libraryTargets(part))
            out.push_back(t);
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
      if (!s || !(lib || SoundLib::severalLibraries(s->masterScore())) || !part)
            return out;
      const Part* master = PartPlaybackModes::masterPart(part);
      std::vector<SoundLib::Controller> ctrls;
      bool any = false;
      const SoundLib::Library* own = lib.get();         // (the part's library: several, its route's)
      for (const SoundLib::Route& r : libraryRoutes()) {
            if (r.part != master || r.lane != 0 || !r.instrument)
                  continue;
            if (!any && r.library)
                  own = r.library;
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
      if (own->dynamicsCC >= 0 && own->dynamicsCC <= 127) {
            const SoundLib::Controller* c = byCC(own->dynamicsCC);
            out.push_back({ c ? c->id : QString("cc%1").arg(own->dynamicsCC), tr("Dynamics (CC%1)").arg(own->dynamicsCC), false });
            taken.insert(out.back().id);
            }
      if (own->dynamicsCC != 11) {
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
                  // (its last point deleted: the lane stays shown, its dashed line again, as in Live)
                  if (l.points.empty() && !NoteLane::isTarget(l.target))
                        _added[pl.first].insert(l.target);
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
      if (Automation::undoWrite(s->masterScore(), all)) {
            if (mscore)
                  mscore->showMessage(what, 1500);
            // heard at once, also while playing (the owner, 2026-10-07: playback edits never stop playback): the parts'
            // plug-in parameter settings (flat lanes) on their instances, the rest rendered again from the play position
            for (const auto& pl : all)
                  SoundLibraryHost::instance()->applyPartSettings(s->masterScore(), pl.first);
            if (seq)
                  seq->renderAgainPlaying();
            }
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
            if (every || (!hidden && (has(t.id) || added || NoteLane::isTarget(t.id))))
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
      for (const Part* p : headerParts()) {
            // (folded: the header row only; targets() for open parts only: it renders the notes once)
            const size_t n = isOpen(p) ? shownTargets(PartPlaybackModes::masterPart(p), targets(p)).size() : 0;
            space[p] = HEAD_SP + double(n) * (LANE_SP + GAP_SP);
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
            const bool open = isOpen(part);
            const std::vector<Target> all = open ? targets(part) : std::vector<Target>();
            double y = origin.y() + lanesY;
            Row head;
            head.part = part;
            head.master = master;
            head.name = part->partName();
            head.rect = QRectF(x0, y, x1 - x0, HEAD_SP * sp);
            out.push_back(head);
            y += HEAD_SP * sp;
            for (const QString& t : open ? shownTargets(master, all) : std::vector<QString>()) {
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
                  r.dataX = m->canvasPos().x();
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
      return Automation::tickAtX(_anchors, x);
      }

double AutomationLanes::pointX(int tick) const
      {
      buildAnchors();
      return Automation::pointX(score(), _anchors, tick);
      }

double AutomationLanes::pixel() const
      {
      const double m = _view->matrix().m11();
      return m > 0 ? 1.0 / m : 1.0;
      }

double AutomationLanes::barX(int tick) const
      {
      Score* s = score();
      Measure* prev = tick > 0 ? s->tick2measureMM(Fraction::fromTicks(tick - 1)) : nullptr;
      if (prev && prev->endTick().ticks() == tick && prev->system())
            return Automation::barLineX(prev);
      Measure* m = s->tick2measureMM(Fraction::fromTicks(tick));
      return m && m->system() ? m->canvasPos().x() : tickToX(tick);
      }

//---------------------------------------------------------
//   the time selection
//---------------------------------------------------------

AutomationLanes::TimeSelection AutomationLanes::timeSelection() const
      {
      TimeSelection t;
      Score* s = score();
      if (!s || _timeOff || !enabled() || !s->lineMode())
            return t;
      const Selection& sel = s->selection();
      if (!sel.isRange() || sel.staffStart() < 0 || sel.staffEnd() > s->nstaves() || sel.staffEnd() <= sel.staffStart())
            return t;
      const Part* part = s->staff(sel.staffStart())->part();
      if (s->staff(sel.staffEnd() - 1)->part() != part)
            return t;
      bool shown = false;
      for (const Row& r : rows())
            shown = shown || r.part == part;
      if (!shown)
            return t;
      // (rounded out to whole bars)
      Measure* m1 = s->tick2measure(sel.tickStart());
      Measure* m2 = sel.tickEnd() > sel.tickStart() ? s->tick2measure(Fraction::fromTicks(sel.tickEnd().ticks() - 1)) : m1;
      if (!m1 || !m2)
            return t;
      t.part = part;
      t.start = m1->tick().ticks();
      t.end = m2->endTick().ticks();
      t.staffStart = sel.staffStart();
      t.staffEnd = sel.staffEnd();
      return t;
      }

bool AutomationLanes::duplicateTimeSelection()
      {
      const TimeSelection t = timeSelection();
      if (!t.valid())
            return false;
      Score* s = score();
      if (seq)
            seq->waitForRendering();      // (the background renderer reads the metaTag and the notes)
      const std::pair<int, int> next = Automation::duplicateTimeSelection(s->masterScore(), t.part, t.start, t.end);
      _cachedTag = QString("\x01");
      if (next.first < 0) {
            if (mscore)
                  mscore->showMessage(tr("Duplicate: the score ends here"), 1500);
            return true;
            }
      // the bars after selected: Ctrl+D again goes on
      Measure* m1 = s->tick2measure(Fraction::fromTicks(next.first));
      Measure* m2 = s->tick2measure(Fraction::fromTicks(next.second - 1));
      if (m1 && m2) {
            s->select(m1, SelectType::SINGLE, t.staffStart);
            s->select(m2, SelectType::RANGE, t.staffEnd - 1);
            s->setUpdateAll();
            s->update();
            if (mscore)
                  mscore->endCmd();       // (the selection's views: the Inspector, the lanes …)
            }
      if (mscore)
            mscore->showMessage(tr("Duplicated: bars %1-%2").arg(m1 ? m1->no() + 1 : 0).arg(m2 ? m2->no() + 1 : 0), 1500);
      _notes->changed();
      updateSpace();
      _view->update();
      return true;
      }

// the grid (fixed, set by Ctrl+1 / Ctrl+2): the beat of the measure at tick (1920 / the time signature's
// denominator), halved per level below 0 (at least 1/64: 30 ticks); level 1: the measure
int AutomationLanes::gridTicks(int tick) const
      {
      return gridTicksAt(tick, _gridLevel);
      }

int AutomationLanes::gridTicksAt(int tick, int level) const
      {
      Score* s = score();
      const Measure* m = s ? s->tick2measure(Fraction::fromTicks(std::max(0, tick))) : nullptr;
      if (level > 0)
            return m ? m->ticks().ticks() : 1920;
      const int beat = 1920 / std::max(1, m ? m->timesig().denominator() : 4);
      return std::max(30, beat >> -level);
      }

// (the grid's label and limits are read at the mouse's time, else at the start)
void AutomationLanes::changeGrid(int by)
      {
      const int tick = _hover.x() >= 0 ? xToTick(_hover.x()) : 0;
      if (by < 0 && _gridLevel <= 0 && gridTicks(tick) <= 30)
            return;
      _gridLevel = std::min(1, _gridLevel + (by < 0 ? -1 : 1));
      _view->update();
      }

void AutomationLanes::toggleSnap()
      {
      _snapOn = !_snapOn;
      _view->update();
      }

QString AutomationLanes::gridLabel() const
      {
      if (!_snapOn)
            return tr("Off");
      if (_gridLevel > 0)
            return tr("Bar");
      return QString("1/%1").arg(1920 / gridTicks(_hover.x() >= 0 ? xToTick(_hover.x()) : 0));
      }

int AutomationLanes::snap(int tick, bool fine) const
      {
      Score* s = score();
      tick = std::max(0, tick);
      if (fine || !_snapOn || !s)
            return tick;
      const Measure* m = s->tick2measure(Fraction::fromTicks(tick));
      if (!m)
            return tick;
      const int g = gridTicks(tick);
      const int t0 = m->tick().ticks();
      int t = t0 + int(std::lround(double(tick - t0) / g)) * g;
      return std::min(t, m->endTick().ticks());
      }

std::pair<int, int> AutomationLanes::gridCell(int tick, bool fine) const
      {
      Score* s = score();
      tick = std::max(0, tick);
      const Measure* m = s ? s->tick2measure(Fraction::fromTicks(tick)) : nullptr;
      if (!m)
            return { -1, -1 };
      const int g = fine ? 30 : gridTicks(tick);
      const int t0 = m->tick().ticks() + (tick - m->tick().ticks()) / g * g;
      return { t0, std::min(t0 + g, m->endTick().ticks()) };
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
      // a controller whose map lists how the plug-in shows it (<Controller display>: Ample's parameters): that text
      const Score* shownScore = score();
      std::vector<std::shared_ptr<const SoundLib::Library>> libs = shownScore ? SoundLib::scoreLibraries(shownScore->masterScore())
                                                                              : std::vector<std::shared_ptr<const SoundLib::Library>>();
      if (libs.empty() && SoundLib::current())
            libs.push_back(SoundLib::current());
      for (const std::shared_ptr<const SoundLib::Library>& lib : libs) {       // (several libraries: each one's)
            auto shown = [&](const SoundLib::Controller& c) {
                  return c.id == target && !c.display.empty() ? SoundLib::controllerText(c, v) : QString();
                  };
            for (const SoundLib::Controller& c : lib->controllers)
                  if (!shown(c).isEmpty())
                        return shown(c);
            for (const SoundLib::LibInstrument& li : lib->instruments)
                  for (const SoundLib::Controller& c : li.controllers)
                        if (!shown(c).isEmpty())
                              return shown(c);
            }
      const double x = v * 127;
      return std::fabs(x - std::round(x)) < 0.05 ? QString::number(int(std::lround(x))) : QString::number(x, 'f', 1);
      }

// the header row's label, and its font (bold, as tall as the row allows)
QString AutomationLanes::headLabel(const Row& r) const
      {
      return QString::fromUtf8(isOpen(r.part) ? "▾ " : "▸ ") + tr("Automation") + QString::fromUtf8(" · ") + r.name;
      }

// the headers' text as large on screen as the score's instrument names (the owner, 2026-10-06: it was a fixed 9-13 px,
// "too small relative to the text size of everything else"), at least 9 px
double AutomationLanes::textPx() const
      {
      const Score* s = score();
      double pt = s ? s->styleD(Sid::longInstrumentFontSize) : 10.0;
      if (s && s->styleB(Sid::longInstrumentFontSpatiumDependent))
            pt *= s->spatium() / SPATIUM20;
      return std::max(9.0, pt * DPI / 72.0 * _view->matrix().m11());
      }

static QFont headFont(double px, bool bold)
      {
      QFont f = QApplication::font();
      f.setPixelSize(int(std::lround(px)));
      f.setBold(bold);
      return f;
      }

// the header row's controls (automationlanes.h), right-aligned: ( + | All ), (✎), [Grid 1/4 ▾]; sizes from the text's
static const double CONTROL_GAP_PX = 6.0;

static QString gridButtonText(const QString& value)
      {
      return QCoreApplication::translate("Ms::AutomationLanes", "Grid") + " " + value + QString::fromUtf8(" ▾");
      }

QRectF AutomationLanes::controlRect(const QRectF& header, Control c) const
      {
      const QFontMetricsF fm(headFont(textPx(), false));
      const double h = header.height() - 4;
      const double top = header.top() + 2;
      // (the Grid button as wide as its widest label: it doesn't move while the value changes)
      const double gridW = fm.horizontalAdvance(gridButtonText("1/64")) + 14;
      const double drawW = h;
      const double addW = std::max(20.0, fm.horizontalAdvance("+") + 14);
      const double allW = fm.horizontalAdvance(tr("All")) + 14;
      double x = header.right() - 2 - gridW;
      if (c == Control::GRID)
            return QRectF(x, top, gridW, h);
      x -= CONTROL_GAP_PX + drawW;
      if (c == Control::DRAW)
            return QRectF(x, top, drawW, h);
      x -= CONTROL_GAP_PX + allW;
      if (c == Control::ALL)
            return QRectF(x, top, allW, h);
      x -= addW;
      if (c == Control::ADD)
            return QRectF(x, top, addW, h);
      return QRectF();
      }

double AutomationLanes::controlsWidth() const
      {
      const QRectF h(0, 0, 10000, 2.2 * textPx());
      return h.right() - controlRect(h, Control::ADD).left() + CONTROL_GAP_PX;
      }

AutomationLanes::Control AutomationLanes::controlAt(const QRectF& header, const QPointF& pixel) const
      {
      for (Control c : { Control::ADD, Control::ALL, Control::GRID })
            if (controlRect(header, c).contains(pixel))
                  return c;
      // (the pencil: its circle)
      const QRectF d = controlRect(header, Control::DRAW);
      const QPointF v = pixel - d.center();
      if (std::hypot(v.x(), v.y()) <= d.width() / 2 + 1)
            return Control::DRAW;
      return Control::NONE;
      }

QString AutomationLanes::controlTip(Control c) const
      {
      switch (c) {
            case Control::ADD:  return tr("Add a lane");
            case Control::ALL:  return tr("Show all lanes");
            case Control::DRAW: return tr("Draw Mode: drag to draw steps as wide as the grid (Alt: a free line)");
            case Control::GRID: return tr("Snap grid and beat spacing (Ctrl+1 narrower, Ctrl+2 wider, Ctrl+4 snap on / off)");
            default:            return QString();
            }
      }

void AutomationLanes::paintControls(QPainter& p, const Row& r, const QRectF& h) const
      {
      p.save();
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setFont(headFont(textPx(), false));
      const QColor line(150, 146, 140);
      const QColor face(243, 242, 239);
      const QColor onFace(220, 232, 246);
      const QColor text(29, 31, 34);
      const QColor onText(29, 79, 140);
      const double radius = 4.0;
      // ( + | All ): one pill, a line between them; All lit while every lane is shown
      const QRectF add = controlRect(h, Control::ADD);
      const QRectF all = controlRect(h, Control::ALL);
      const QRectF pill = add.united(all).adjusted(0.5, 0.5, -0.5, -0.5);
      QPainterPath pillPath;
      pillPath.addRoundedRect(pill, radius, radius);
      p.fillPath(pillPath, face);
      const bool every = _showAll.count(r.master) > 0;
      if (every) {
            p.save();
            p.setClipPath(pillPath);
            p.fillRect(all, onFace);
            p.restore();
            }
      p.setPen(QPen(line, 1.0));
      p.setBrush(Qt::NoBrush);
      p.drawPath(pillPath);
      p.drawLine(QPointF(all.left(), pill.top() + 3), QPointF(all.left(), pill.bottom() - 3));
      p.setPen(text);
      p.drawText(add, Qt::AlignCenter, "+");
      p.setPen(every ? onText : text);
      p.drawText(all, Qt::AlignCenter, tr("All"));
      // (✎): round, in the palette's highlight colour; filled while Draw Mode is on
      const QColor accent = QApplication::palette().color(QPalette::Highlight);
      const QRectF draw = controlRect(h, Control::DRAW).adjusted(0.5, 0.5, -0.5, -0.5);
      QColor soft = accent;
      soft.setAlpha(45);
      p.setPen(QPen(accent, _drawMode ? 1.0 : 1.4));
      p.setBrush(_drawMode ? accent : QColor(255, 255, 255));
      p.drawEllipse(draw);
      if (!_drawMode) {
            p.setBrush(soft);
            p.setPen(Qt::NoPen);
            p.drawEllipse(draw);
            }
      p.setPen(_drawMode ? QApplication::palette().color(QPalette::HighlightedText) : accent.darker(125));
      p.drawText(draw, Qt::AlignCenter, QString::fromUtf8("✎"));
      // [Grid 1/4 ▾]: lit while Even beat spacing is on
      const Score* sc = score();
      const bool even = sc && sc->lineEvenBeats();
      const QRectF grid = controlRect(h, Control::GRID).adjusted(0.5, 0.5, -0.5, -0.5);
      p.setPen(QPen(line, 1.0));
      p.setBrush(even ? onFace : face);
      p.drawRoundedRect(grid, radius, radius);
      p.setPen(even ? onText : text);
      p.drawText(grid, Qt::AlignCenter, gridButtonText(gridLabel()));
      p.restore();
      }

// the Grid button's menu: the snap (Off, Bar, the beat … 1/64 at the mouse's time signature) and Even beat spacing
void AutomationLanes::gridMenu(const QPoint& globalPos)
      {
      QMenu menu;
      QActionGroup* group = new QActionGroup(&menu);
      group->setExclusive(true);
      auto choice = [&](const QString& text, bool on, bool snap, int level) {
            QAction* a = menu.addAction(text);
            a->setCheckable(true);
            a->setChecked(on);
            a->setActionGroup(group);
            QObject::connect(a, &QAction::triggered, this, [this, snap, level]() {
                  _snapOn = snap;
                  if (snap)
                        _gridLevel = level;
                  _view->update();
                  });
            };
      choice(tr("Off"), !_snapOn, false, _gridLevel);
      choice(tr("Bar"), _snapOn && _gridLevel > 0, true, 1);
      const int tick = _hover.x() >= 0 ? xToTick(_hover.x()) : 0;
      for (int level = 0; ; --level) {
            const int t = gridTicksAt(tick, level);
            choice(QString("1/%1").arg(1920 / t), _snapOn && _gridLevel == level, true, level);
            if (t <= 30)
                  break;
            }
      menu.addSeparator();
      QAction* even = menu.addAction(tr("Even beat spacing"));
      even->setCheckable(true);
      Score* sc = score();
      even->setChecked(sc && sc->lineEvenBeats());
      even->setToolTip(tr("Every beat the same width in Continuous View (the view only)"));
      QAction* a = menu.exec(globalPos);
      if (a == even && (sc = score())) {
            // (the view only, every beat the same width: not saved, not an undo step)
            sc->setLineEvenBeats(!sc->lineEvenBeats());
            sc->setLayoutAll();
            sc->update();
            updateSpace();
            }
      }

QRectF AutomationLanes::headerRect(const Row& r) const
      {
      const QRectF vr = _view->matrix().mapRect(r.rect);
      if (!r.target.isEmpty()) {
            // a lane's: fixed at the view's left, as wide as the room left of bar 1 when scrolled to the start (the
            // page's left at the view's: zoomed out the box covered bar 1's points), at least HEADER_MIN_PX; by the
            // zoom only, not the scroll position (it doesn't change while scrolling)
            // (wider as the text grows: about 14 characters' room)
            const double room = r.dataX * _view->matrix().m11() - 2;
            const double want = std::max(double(HEADER_PX), 14 * textPx());
            return QRectF(0, vr.top(), std::max(double(HEADER_MIN_PX), std::min(want, room)), vr.height());
            }
      // the header row as wide as its label needs (the part's name whole: "Automation · Violin" was cut to "Vio…"),
      // with room for its buttons; at most HEADER_MAX_PX (then the name is shortened in the middle)
      const int label = QFontMetrics(headFont(textPx(), true)).horizontalAdvance(headLabel(r));
      const double buttons = controlsWidth() + 6;
      return QRectF(0, vr.top(), std::max(HEADER_PX + buttons, std::min(HEADER_MAX_PX + buttons, 6 + label + 12 + buttons)), vr.height());
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
            const QPointF q(pointX(l.points[size_t(i)].tick), yOfValue(r, l.points[size_t(i)].value));
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
      LagProbe::Scope probe("AutomationLanes::paint");
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
      // the time selection: a light band over its part's rows (as Live shows one)
      const TimeSelection ts = timeSelection();
      if (ts.valid()) {
            const double xa = barX(ts.start);
            const double xb = barX(ts.end);
            for (const Row& r : rs)
                  if (r.part == ts.part && r.rect.intersects(visible))
                        p.fillRect(QRectF(QPointF(xa, r.rect.top()), QPointF(xb, r.rect.bottom())), QColor(47, 109, 181, 40));
            }
      // the rubber band
      if (_drag == Drag::RUBBER && !_rubber.isEmpty())
            NoteLanes::paintRubber(p, _rubber, pixel());
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
      // each row's band from the view's left to the score's (the owner, 2026-10-06: the gray strip was cut in two
      // between the header box and the system's left edge)
      for (const Row& r : rs) {
            const QRectF vr = _view->matrix().mapRect(r.rect);
            const QRectF gap(0, vr.top(), vr.left(), vr.height());
            if (vr.left() > 0 && gap.intersects(QRectF(viewport)))
                  p.fillRect(gap, rowColor(r));
            }
      for (const Row& r : rs)
            if (headerRect(r).intersects(QRectF(viewport)))
                  paintHeader(p, r);
      p.restore();
      }

QColor AutomationLanes::rowColor(const Row& r) const
      {
      if (r.target.isEmpty())
            return QColor(222, 219, 214);
      if (NoteLane::isTarget(r.target))
            return QColor(246, 246, 246);
      const Lane l = lane(r.master, r.target);
      const bool live = !l.points.empty() && l.playedByLive();
      return live ? QColor(246, 241, 250) : (r.param ? QColor(251, 245, 239) : QColor(242, 246, 251));
      }

void AutomationLanes::paintLane(QPainter& p, const Row& r, const QRectF& visible) const
      {
      const double px = pixel();
      if (r.target.isEmpty()) {
            // the header row: a band
            p.setPen(Qt::NoPen);
            p.setBrush(rowColor(r));
            p.drawRect(r.rect);
            return;
            }
      if (NoteLane::isTarget(r.target)) {
            _notes->paintLane(p, r, visible);
            return;
            }
      const Lane l = lane(r.master, r.target);
      const bool live = !l.points.empty() && l.playedByLive();
      const QColor ink = live ? QColor(118, 72, 160) : (r.param ? QColor(200, 100, 30) : QColor(47, 109, 181));
      p.setPen(Qt::NoPen);
      p.setBrush(rowColor(r));
      p.drawRect(r.rect);
      paintGrid(p, r, visible);
      p.setPen(QPen(QColor(200, 200, 200), px));
      p.setBrush(Qt::NoBrush);
      p.drawRect(r.rect);
      const bool selLane = _focus && r.master == _selPart && r.target == _selTarget;
      const QRectF v = valueRect(r);
      paintEnvelope(p, r.rect, v, l, unautomatedValue(r), [this](int t) { return pointX(t); }, px, ink,
                    selLane ? _sel : std::vector<int>(), visible);
      }

//---------------------------------------------------------
//   paintEnvelope
//    a lane's line in rect (its values 0-1 in valueRect, bottom to top): with points, the envelope (solid, filled
//    under) and its breakpoints, before the first point the controller's unautomated value dashed; without points,
//    Live's dashed line at the unautomated value (unautomated < 0: not known, nothing drawn)
//---------------------------------------------------------

void AutomationLanes::paintEnvelope(QPainter& p, const QRectF& rect, const QRectF& valueRect, const Lane& l, double unautomated,
                                    const std::function<double(int)>& tickToX, double px, const QColor& ink,
                                    const std::vector<int>& selected, const QRectF& visible)
      {
      auto yOf = [&valueRect](double v) { return valueRect.bottom() - std::min(1.0, std::max(0.0, v)) * valueRect.height(); };
      QPen dashed(ink, 1.4 * px, Qt::CustomDashLine);
      dashed.setDashPattern({ 4.0, 3.0 });
      if (l.points.empty()) {
            if (unautomated < 0)
                  return;
            p.setPen(dashed);
            p.drawLine(QPointF(rect.left(), yOf(unautomated)), QPointF(rect.right(), yOf(unautomated)));
            return;
            }
      // the envelope
      QPainterPath path;
      const double xFirst = tickToX(l.points.front().tick);
      path.moveTo(xFirst, yOf(l.points.front().value));
      for (size_t i = 0; i < l.points.size(); ++i) {
            const Point& a = l.points[i];
            const double xa1 = tickToX(a.tick);
            const double ya = yOf(a.value);
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
                        path.lineTo(tickToX(t), yOf(l.valueAt(t)));
                        }
                  }
            }
      path.lineTo(rect.right(), yOf(l.points.back().value));
      QPainterPath fill = path;
      fill.lineTo(rect.right(), rect.bottom());
      fill.lineTo(xFirst, rect.bottom());
      fill.closeSubpath();
      QColor f = ink;
      f.setAlpha(34);
      p.setPen(Qt::NoPen);
      p.setBrush(f);
      p.drawPath(fill);
      // before the first point the lane says nothing: the controller plays its unautomated value there (dashed; not
      // known: at the first point's value, as before)
      if (xFirst > rect.left()) {
            const double y = yOf(unautomated >= 0 ? unautomated : l.points.front().value);
            p.setPen(dashed);
            p.drawLine(QPointF(rect.left(), y), QPointF(xFirst, y));
            }
      p.setPen(QPen(ink, 1.8 * px));
      p.setBrush(Qt::NoBrush);
      p.drawPath(path);
      // the breakpoints
      for (int i = 0; i < int(l.points.size()); ++i) {
            const Point& a = l.points[size_t(i)];
            const QPointF c(tickToX(a.tick), yOf(a.value));
            if (c.x() < visible.left() - 10 * px || c.x() > visible.right() + 10 * px)
                  continue;
            const bool sel = std::find(selected.begin(), selected.end(), i) != selected.end();
            p.setPen(QPen(ink, 1.6 * px));
            p.setBrush(sel ? ink : QColor(Qt::white));
            p.drawEllipse(c, POINT_PX * px, POINT_PX * px);
            }
      }

//---------------------------------------------------------
//   unautomatedValue
//    what the row's controller plays without a point (0-1; -1: not known, or no map controller: Dynamics,
//    Expression, a Live track's parameter)
//---------------------------------------------------------

double AutomationLanes::unautomatedValue(const Row& r) const
      {
      for (const SoundLib::Route& rt : libraryRoutes()) {
            if (PartPlaybackModes::masterPart(rt.part) != r.master || rt.patch != 0 || rt.lane != 0 || !rt.instrument)
                  continue;
            for (const SoundLib::Controller& c : rt.instrument->allControllers)
                  if (c.id == r.target || (c.cc >= 0 && r.target == QString("cc%1").arg(c.cc)))
                        return PartControllers::unautomatedValue(*rt.instrument, c);
            }
      return -1;
      }

void AutomationLanes::paintGrid(QPainter& p, const Row& r, const QRectF& visible) const
      {
      const double px = pixel();
      // the grid: bars, beats and the snap grid's steps between them (those only while 3 px apart or more on screen,
      // so a fine grid zoomed out doesn't fill the lane)
      const double xa = std::max(r.rect.left(), visible.left());
      const double xb = std::min(r.rect.right(), visible.right());
      Score* s = score();
      const int ta = xToTick(xa);
      const int tb = xToTick(xb);
      for (Measure* m = s->tick2measureMM(Fraction::fromTicks(ta)); m && m->tick().ticks() <= tb; m = m->nextMeasureMM()) {
            const int t0 = m->tick().ticks();
            const int beat = 1920 / std::max(1, m->timesig().denominator());
            int step = std::min(beat, gridTicks(t0));
            if ((tickToX(t0 + step) - tickToX(t0)) / px < 3)
                  step = beat;
            for (int t = t0; t < m->endTick().ticks(); t += step) {
                  // (a bar: where the staff's bar line is; the beats and steps where their notes are)
                  const bool bar = t == t0;
                  const Measure* prev = bar ? m->prevMeasureMM() : nullptr;
                  const double x = prev && prev->system() == m->system() ? Automation::barLineX(prev) : tickToX(t);
                  const QColor c = bar ? QColor(170, 170, 170) : (t - t0) % beat == 0 ? QColor(220, 220, 220) : QColor(232, 232, 232);
                  p.setPen(QPen(c, px));
                  p.drawLine(QPointF(x, r.rect.top()), QPointF(x, r.rect.bottom()));
                  }
            }
      }

void AutomationLanes::paintHeader(QPainter& p, const Row& r) const
      {
      const QRectF h = headerRect(r);
      p.setFont(headFont(textPx(), false));
      if (r.target.isEmpty()) {
            p.fillRect(h, QColor(214, 211, 206));
            p.setPen(QColor(29, 31, 34));
            const double buttons = controlsWidth();
            p.setFont(headFont(textPx(), true));
            p.drawText(h.adjusted(6, 0, -buttons, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       QFontMetrics(p.font()).elidedText(headLabel(r), Qt::ElideMiddle, int(h.width() - 6 - buttons - 4)));
            paintControls(p, r, h);
            return;
            }
      const Lane l = lane(r.master, r.target);
      const bool live = !l.points.empty() && l.playedByLive();
      p.fillRect(h, live ? QColor(236, 228, 244) : (r.param ? QColor(246, 236, 226) : QColor(226, 234, 245)));
      p.setPen(QColor(200, 200, 200));
      p.drawLine(h.topRight(), h.bottomRight());
      p.setPen(QColor(29, 31, 34));
      const double close = 1.6 * textPx();                  // the × at the right
      const QRectF text = h.adjusted(10, 0, -close - 2, 0);
      // its name, and the value where playback is (or at the mouse)
      Score* s = score();
      int at = s ? s->playPos().ticks() : 0;
      if (_hover.x() >= 0 && r.rect.contains(_hover))
            at = xToTick(_hover.x());
      double v = l.valueAt(at);
      if (v < 0 && !NoteLane::isTarget(r.target))
            v = unautomatedValue(r);          // (no point there: the controller's own value, Live's dashed line)
      QString line2 = v >= 0 ? valueText(r.target, v) : QString::fromUtf8("–");
      if (live)
            line2 += "  " + tr("Live");
      QString title = r.name;
      if (NoteLane::isTarget(r.target)) {
            title = _notes->title(r);
            line2 = _notes->valueText(r, at);
            }
      if (r.target == LiveClipEdit::VELOCITY_TARGET)        // its unit, and how Live gets it
            title = (LiveClipEdit::velMode(l) == LiveClipEdit::VelMode::SET ? tr("Velocity (1-127)") : tr("Velocity (%)"))
                    + QString::fromUtf8(" · ")
                    + (LiveClipEdit::velOutput(l) == LiveClipEdit::VelOutput::WRITE ? tr("written") : tr("shaped"));
      const QString name = QFontMetrics(p.font()).elidedText(title, Qt::ElideRight, int(text.width()));
      if (h.height() >= 2.2 * textPx()) {        // two lines
            p.drawText(text.adjusted(0, 2, 0, -h.height() / 2), Qt::AlignBottom | Qt::AlignLeft, name);
            p.setPen(QColor(85, 88, 92));
            p.setFont(headFont(0.85 * textPx(), false));     // the value a little smaller
            p.drawText(text.adjusted(0, h.height() / 2, 0, -2), Qt::AlignTop | Qt::AlignLeft, line2);
            p.setFont(headFont(textPx(), false));
            }
      else
            p.drawText(text, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(p.font()).elidedText(title + "  " + line2, Qt::ElideRight, int(text.width())));
      // × hides the lane
      p.setPen(QColor(110, 110, 110));
      p.drawText(QRectF(h.right() - close - 2, h.top(), close, h.height()), Qt::AlignCenter, QString::fromUtf8("×"));
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
      if (!sel.empty())
            _timeOff = true;        // (points selected: Ctrl+D duplicates them, not the time selection)
      }

void AutomationLanes::dropFocus()
      {
      if (!_focus)
            return;
      _focus = false;
      _sel.clear();
      _notes->clearSelection();
      _view->update();
      }

bool AutomationLanes::headerClick(const QPoint& pixel)
      {
      for (const Row& r : rows()) {
            const QRectF h = headerRect(r);
            if (!h.contains(pixel))
                  continue;
            if (r.target.isEmpty()) {
                  const Control c = controlAt(h, pixel);
                  if (c != Control::NONE) {
                        const QRectF b = controlRect(h, c);
                        QToolTip::hideText();
                        if (c == Control::ADD)
                              addLaneMenu(r, _view->mapToGlobal(QPoint(int(b.left()), int(b.bottom()))));
                        else if (c == Control::ALL) {
                              if (_showAll.count(r.master))
                                    _showAll.erase(r.master);
                              else
                                    _showAll.insert(r.master);
                              unfold(r.part);
                              }
                        else if (c == Control::DRAW) {
                              _drawMode = !_drawMode;
                              _view->update();
                              }
                        else
                              gridMenu(_view->mapToGlobal(QPoint(int(b.left()), int(b.bottom()))));
                        return true;
                        }
                  // the rest of the header (its ▸ / ▾ and name) opens or folds the part's lanes, as Live's track header;
                  // Alt: every part's, to this one's new state (lanefolds.h)
                  if (QApplication::keyboardModifiers() & Qt::AltModifier)
                        _folds.toggleAll(score(), r.part, headerParts());
                  else
                        _folds.toggle(score(), r.part);
                  if (isOpen(r.part) && _notes->fresh())      // (the notes' lanes as the score is now)
                        clearTargets();
                  updateSpace();
                  return true;
                  }
            if (pixel.x() >= h.right() - 1.6 * textPx() - 2) {
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
            _folds.setOpen(score(), r.part, true);
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
      if (NoteLane::isTarget(r.target)) {
            _focus = true;
            _selPart = r.master;
            _selTarget = r.target;
            _sel.clear();
            _drag = Drag::NONE;
            return _notes->press(r, p, ev->modifiers(), _drawMode);
            }
      _dragRow = r;
      _pressPos = p;
      _pressPixel = ev->pos();
      _before = lane(r.master, r.target);
      const bool sameLane = _focus && _selPart == r.master && _selTarget == r.target;
      _beforeSel = sameLane ? _sel : std::vector<int>();
      const Qt::KeyboardModifiers mods = ev->modifiers();
      if (_drawMode) {
            // Alt: freehand (the line follows the mouse; on release: breakpoints, Edit::drawFree)
            _drag = (mods & Qt::AltModifier) ? Drag::FREE : Drag::DRAW;
            _drawCell = -1;
            _freePath.clear();
            _freeLast = -1;
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
      _lineSegment = _nearLine ? segmentAt(_before, tick) : -1;
      // an empty lane's dashed line (Live's unautomated value): a click puts a point on it, a drag sets the value
      _dashedValue = -1;
      if (_before.points.empty()) {
            const double u = unautomatedValue(r);
            if (u >= 0 && std::fabs(yOfValue(r, u) - p.y()) <= 6 * pixel())
                  _dashedValue = u;
            }
      _valueOnly = false;
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
      if (_notes->dragging()) {
            _notes->move(_view->toLogical(ev->pos()), ev->modifiers());
            return true;
            }
      if (_drag == Drag::NONE) {
            hover(ev->pos());
            return false;
            }
      const QPointF p = _view->toLogical(ev->pos());
      const Row& r = _dragRow;
      const Qt::KeyboardModifiers mods = ev->modifiers();
      const bool fineTime = (mods & Qt::AltModifier) || !_snapOn;
      switch (_drag) {
            case Drag::PENDING:
                  if ((ev->pos() - _pressPixel).manhattanLength() <= 4)
                        return true;
                  if (_dashedValue >= 0) {
                        // the dashed line dragged (Live: the parameter's value set): one point at the score's start,
                        // moved up or down (a flat lane: the controller's setting, partcontrollers.h)
                        _before.points = { Point(0, _dashedValue, Curve::STEP) };
                        _grab = 0;
                        select(r, { 0 });
                        _valueOnly = true;
                        _drag = Drag::MOVE;
                        _working = false;
                        return mouseMove(ev);
                        }
                  if (_lineSegment >= 0) {
                        // pressed on the line: the segment moves (its two points; both selected: the selection)
                        const int s = _lineSegment;
                        const bool both = std::find(_beforeSel.begin(), _beforeSel.end(), s) != _beforeSel.end()
                                          && std::find(_beforeSel.begin(), _beforeSel.end(), s + 1) != _beforeSel.end();
                        _segMoved = both ? _beforeSel : std::vector<int>{ s, s + 1 };
                        _drag = Drag::SEGMENT;
                        return mouseMove(ev);
                        }
                  _drag = Drag::RUBBER;
                  // fall through
            case Drag::RUBBER: {
                  _rubber = QRectF(_pressPos, p).normalized().intersected(r.rect);
                  std::vector<int> sel = (mods & Qt::ControlModifier) ? _beforeSel : std::vector<int>();
                  for (int i = 0; i < int(_before.points.size()); ++i) {
                        const QPointF c(pointX(_before.points[size_t(i)].tick), yOfValue(r, _before.points[size_t(i)].value));
                        if (_rubber.contains(c) && std::find(sel.begin(), sel.end(), i) == sel.end())
                              sel.push_back(i);
                        }
                  select(r, sel);
                  _view->update();
                  return true;
                  }
            case Drag::MOVE: {
                  if ((ev->pos() - _pressPixel).manhattanLength() <= 2 && !_working && !_valueOnly)
                        return true;
                  const Point& g = _before.points[size_t(_grab)];
                  const bool fineValue = mods & Qt::ShiftModifier;
                  int dtick = 0;
                  if (!fineValue && !_valueOnly) {
                        const double gx = pointX(g.tick) + (p.x() - _pressPos.x());
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
                  _hover = QPointF(pointX(g.tick + dtick), yOfValue(r, std::min(1.0, std::max(0.0, g.value + dv))));
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
            case Drag::FREE: {
                  // the path, tick -> value; a stretch the mouse skipped (or went back over) filled in straight
                  const int tick = std::max(0, xToTick(p.x()));
                  const double v = valueAtY(r, p.y());
                  if (_freeLast < 0 || _freePath.empty())
                        _freePath[tick] = v;
                  else {
                        const double v0 = _freePath[_freeLast];
                        const int n = std::abs(tick - _freeLast);
                        const int step = std::max(1, n / 8);
                        for (int k = step; k < n; k += step) {
                              const int t = _freeLast + (tick > _freeLast ? k : -k);
                              _freePath[t] = v0 + (v - v0) * k / n;
                              }
                        _freePath[tick] = v;
                        }
                  _freeLast = tick;
                  Lane l = _before;
                  Edit::drawFree(l, std::vector<std::pair<int, double>>(_freePath.begin(), _freePath.end()), &_before);
                  setLane(r.master, l);
                  _hover = p;
                  _hoverText = valueText(r.target, v);
                  _view->update();
                  return true;
                  }
            case Drag::SEGMENT: {
                  // both points by as much (Live: "click and drag a line segment"); Shift: one axis, the one
                  // moved further; Alt: no snap
                  const Point& a = _before.points[size_t(_lineSegment)];
                  double dx = p.x() - _pressPos.x();
                  double dy = p.y() - _pressPos.y();
                  if (mods & Qt::ShiftModifier) {
                        if (std::fabs(dx) > std::fabs(dy))
                              dy = 0;
                        else
                              dx = 0;
                        }
                  const int dtick = dx == 0 ? 0 : snap(xToTick(pointX(a.tick) + dx), fineTime) - a.tick;
                  const double dv = valueAtY(r, yOfValue(r, a.value) + dy) - a.value;
                  Lane l = _before;
                  const std::vector<int> moved = Edit::movePoints(l, _segMoved, dtick, dv);
                  setLane(r.master, l);
                  select(r, moved);
                  _hover = p;
                  _hoverText = valueText(r.target, std::min(1.0, std::max(0.0, a.value + dv)));
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
      if (_notes->dragging()) {
            _notes->release();
            return true;
            }
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
                  const int i = _nearLine ? Edit::addPointOnLine(l, tick)
                              : Edit::addPoint(l, tick, _dashedValue >= 0 ? _dashedValue : valueAtY(r, _pressPos.y()));
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
            case Drag::FREE:
                  _hoverText.clear();
                  _freePath.clear();
                  commit(tr("Automation: drawn"));
                  return true;
            case Drag::SEGMENT:
                  _hoverText.clear();
                  commit(tr("Automation: segment moved"));
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
      if (NoteLane::isTarget(r.target)) {
            _notes->doubleClick(r, p);
            return true;
            }
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
            if (NoteLane::isTarget(r->target))
                  _hoverText = _notes->hoverText(*r, p);
            else {
                  const Lane l = lane(r->master, r->target);
                  const int pt = pointAt(*r, l, p);
                  if (pt >= 0)
                        _hoverText = valueText(r->target, l.points[size_t(pt)].value);
                  else if (l.points.empty() || xToTick(p.x()) < l.points.front().tick) {
                        // on the dashed line (no point there): the value the controller plays, as Live shows it
                        const double u = unautomatedValue(*r);
                        if (u >= 0 && std::fabs(p.y() - yOfValue(*r, u)) <= 6 * pixel())
                              _hoverText = valueText(r->target, u);
                        }
                  }
            }
      if (old != _hover || oldText != _hoverText)
            _view->update();
      // a header row's control, or its name: a tooltip
      QString tip;
      if (r && r->target.isEmpty()) {
            const QRectF h = headerRect(*r);
            if (h.contains(pos)) {
                  const Control c = controlAt(h, pos);
                  tip = c != Control::NONE ? controlTip(c)
                                           : tr("Click: open or fold this part's lanes; Alt+click: every part's");
                  }
            }
      if (!tip.isEmpty()) {
            QToolTip::showText(_view->mapToGlobal(pos), tip, _view);
            _tipShown = true;
            }
      else if (_tipShown) {
            QToolTip::hideText();
            _tipShown = false;
            }
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
      if (NoteLane::isTarget(r.target)) {
            _notes->contextMenu(r, p, globalPos);
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
      // the selection's span (Live's time selection: the selected points' first to last tick); without one, Simplify
      // takes the lane, a shape the grid cell under the mouse (manual 25.5.5)
      const bool laneSel = _focus && _selPart == r.master && _selTarget == r.target && _sel.size() >= 2;
      int selT1 = -1, selT2 = -1;
      if (laneSel)
            for (int i : _sel)
                  if (i >= 0 && i < int(l.points.size())) {
                        const int t = l.points[size_t(i)].tick;
                        selT1 = selT1 < 0 ? t : std::min(selT1, t);
                        selT2 = std::max(selT2, t);
                        }
      const bool span = selT2 > selT1;
      menu.addSeparator();
      QAction* simplify = l.points.size() >= 3 ? menu.addAction(span ? tr("Simplify Envelope") : tr("Simplify Lane")) : nullptr;
      QMenu* shapes = menu.addMenu(tr("Insert Shape"));
      std::map<QAction*, Edit::Shape> shapeOf;
      for (const auto& sh : std::vector<std::pair<Edit::Shape, QString>> {
            { Edit::Shape::SINE, tr("Sine") }, { Edit::Shape::TRIANGLE, tr("Triangle") },
            { Edit::Shape::SAW, tr("Sawtooth") }, { Edit::Shape::INVERSE_SAW, tr("Inverse Sawtooth") },
            { Edit::Shape::SQUARE, tr("Square") } })
            shapeOf[shapes->addAction(sh.second)] = sh.first;
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
      else if (a == simplify) {
            const int t1 = span ? selT1 : l.points.front().tick;
            const int t2 = span ? selT2 : l.points.back().tick;
            Edit::simplify(l, t1, t2);
            setLane(r.master, l);
            std::vector<int> sel;
            for (int i = 0; span && i < int(l.points.size()); ++i)
                  if (l.points[size_t(i)].tick >= t1 && l.points[size_t(i)].tick <= t2)
                        sel.push_back(i);
            select(r, sel);
            commit(tr("Automation: simplified"));
            }
      else if (shapeOf.count(a)) {
            const std::pair<int, int> cell = span ? std::make_pair(selT1, selT2) : gridCell(tick, false);
            if (cell.second <= cell.first)
                  return true;
            Edit::insertShape(l, cell.first, cell.second, shapeOf[a]);
            setLane(r.master, l);
            std::vector<int> sel;
            for (int i = 0; i < int(l.points.size()); ++i)
                  if (l.points[size_t(i)].tick >= cell.first && l.points[size_t(i)].tick <= cell.second)
                        sel.push_back(i);
            select(r, sel);
            commit(tr("Automation: shape"));
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
      const Qt::KeyboardModifiers m = ev->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
      // Ctrl+D over a time selection, the focus in the score or in a lane (MuseScore 3 has no Ctrl+D of its own)
      if (ev->key() == Qt::Key_D && m == Qt::ControlModifier && timeSelection().valid())
            return true;
      // (Ctrl+D on selected stems of the Velocity and Join lanes: NoteLanes::duplicateSelected)
      if (ev->key() == Qt::Key_D && m == Qt::ControlModifier && _focus && NoteLane::isTarget(_selTarget) && !_notes->selected().empty())
            return true;
      if (!_focus)
            return false;
      switch (ev->key()) {
            case Qt::Key_Delete:
            case Qt::Key_Backspace:
            case Qt::Key_Escape:
                  return m == 0;
            case Qt::Key_C:             // (the notes' lanes: Ctrl+C / V, NoteLanes::copySelected / pasteCopied; no X)
            case Qt::Key_V:
                  return m == Qt::ControlModifier;
            case Qt::Key_X:
            case Qt::Key_D:
                  return m == Qt::ControlModifier && !NoteLane::isTarget(_selTarget);
            case Qt::Key_1:             // the grid: narrower, wider, snap on / off (Live's keys)
            case Qt::Key_2:
            case Qt::Key_4:
                  return m == Qt::ControlModifier;
            default:
                  return false;
            }
      }

bool AutomationLanes::keyPress(QKeyEvent* ev)
      {
      if (!wantsKey(ev))
            return false;
      if (ev->key() == Qt::Key_D && duplicateTimeSelection())
            return true;
      if (ev->key() == Qt::Key_D && NoteLane::isTarget(_selTarget)) {
            _notes->duplicateSelected();
            return true;
            }
      if (NoteLane::isTarget(_selTarget) && ev->key() == Qt::Key_C) {
            _notes->copySelected();
            return true;
            }
      if (NoteLane::isTarget(_selTarget) && ev->key() == Qt::Key_V) {
            if (!_notes->pasteCopied(_hover) && mscore)
                  mscore->showMessage(tr("Paste: copy stems of the Velocity or Join lane first (Ctrl+C)"), 2500);
            return true;
            }
      if (NoteLane::isTarget(_selTarget) && (ev->key() == Qt::Key_Delete || ev->key() == Qt::Key_Backspace)) {
            _notes->resetSelected();      // (back to Auto)
            return true;
            }
      Lane l = lane(_selPart, _selTarget);
      Row r;
      for (const Row& x : rows())
            if (x.master == _selPart && x.target == _selTarget)
                  r = x;
      switch (ev->key()) {
            case Qt::Key_Escape:
                  dropFocus();
                  return true;
            case Qt::Key_1:
            case Qt::Key_2:
                  changeGrid(ev->key() == Qt::Key_1 ? -1 : 1);
                  return true;
            case Qt::Key_4:
                  toggleSnap();
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
