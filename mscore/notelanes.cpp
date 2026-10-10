//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "notelanes.h"
#include "musescore.h"
#include "scoreview.h"
#include "seq.h"

#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/score.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <QInputDialog>
#include <QMenu>
#include <QPainter>
#include <QTimer>

namespace Ms {

using namespace NoteLane;

static const double STEM_PX = 4.0;        // a stem's head, radius on screen
static const int RENDER_DELAY_MS = 250;   // after a change: rendered again (edits in a row render once)
static const int JOIN_RANGE_MS = 100;     // the Join lane at least, ms either way (shows SSO's +24 / -54 / -39)

// the bands' and stems' colours: a technique's by its name (the set's order else), the dynamics' from blue (below ppp)
// to red (from fff); light behind, full on the stem
static QColor bandColor(const Band& b, bool light)
      {
      int hue;
      if (b.kind == Band::Kind::DYNAMIC)
            hue = 230 - b.index * 230 / 8;
      else {
            static const std::map<QString, int> named { { "smooth", 210 }, { "spiccato", 130 }, { "accented", 25 },
                                                        { "portamento", 280 }, { "fingered", 175 }, { "bowed", 45 } };
            auto it = named.find(b.name);
            hue = it != named.end() ? it->second : (b.index * 97 + (b.kind == Band::Kind::TRANSITION ? 40 : 0)) % 360;
            }
      return light ? QColor::fromHsv(hue, 70, 250) : QColor::fromHsv(hue, 210, 175);
      }

NoteLanes::NoteLanes(AutomationLanes* lanes, ScoreView* view)
   : QObject(lanes), _lanes(lanes), _view(view)
      {
      _timer = new QTimer(this);
      _timer->setSingleShot(true);
      _timer->setInterval(RENDER_DELAY_MS);
      connect(_timer, &QTimer::timeout, this, [this]() {
            // (rendered when lanes show: else when a part's lanes open, AutomationLanes::selectionChanged)
            if (!_lanes->enabled() || !_lanes->score() || !_lanes->score()->lineMode() || _lanes->_unfolded.empty())
                  return;
            if (seq && seq->isPlaying()) {
                  _timer->start();
                  return;
                  }
            std::set<const Part*> before;
            for (const auto& p : _marks)
                  before.insert(p.first);
            bool perfBefore = false;
            for (const auto& p : _marks)
                  for (const Mark& m : p.second)
                        perfBefore |= m.joinable();
            render();
            std::set<const Part*> after;
            bool perfAfter = false;
            for (const auto& p : _marks) {
                  after.insert(p.first);
                  for (const Mark& m : p.second)
                        perfAfter |= m.joinable();
                  }
            if (before != after || perfBefore != perfAfter) {        // (lanes come or go)
                  _lanes->_targets.clear();
                  _lanes->updateSpace();
                  }
            _view->update();
            });
      }

void NoteLanes::scoreChanged()
      {
      if (_lanes->score() != _score) {        // (another score: its notes)
            _marks.clear();
            _rendered = false;
            }
      _sel.clear();
      _work.clear();
      _drag = Drag::NONE;
      _valid = false;
      renderLater();
      }

void NoteLanes::changed()
      {
      _valid = false;
      renderLater();
      }

void NoteLanes::renderLater()
      {
      if (_drag == Drag::NONE)
            _timer->start();
      }

void NoteLanes::render() const
      {
      Score* s = _lanes->score();
      if (seq)
            seq->waitForRendering();      // (Seq's chunk renderer reads the same score)
      _marks = marks(s ? s->masterScore() : nullptr);
      _score = s;
      _valid = true;
      _rendered = true;
      }

bool NoteLanes::fresh() const
      {
      if (!_valid && !(seq && seq->isPlaying()))
            render();
      return _valid;
      }

const std::map<const Part*, std::vector<Mark>>& NoteLanes::currentMarks() const
      {
      if (!_rendered)
            render();
      return _marks;
      }

const std::vector<Mark>& NoteLanes::partMarks(const Part* master) const
      {
      static const std::vector<Mark> none;
      if (!_rendered)
            render();
      auto it = _marks.find(master);
      return it == _marks.end() ? none : it->second;
      }

std::vector<AutomationLanes::Target> NoteLanes::targets(const Part* master) const
      {
      std::vector<AutomationLanes::Target> out;
      const std::vector<Mark>& ms = partMarks(master);
      if (ms.empty())
            return out;
      out.push_back({ VELOCITY, tr("Velocity (notes)"), false, false });
      if (std::any_of(ms.begin(), ms.end(), [](const Mark& m) { return m.joinable(); }))
            out.push_back({ JOIN, tr("Join (ms)"), false, false });
      return out;
      }

//---------------------------------------------------------
//   values
//---------------------------------------------------------

bool NoteLanes::isJoin(const Row& r) const
      {
      return r.target == JOIN;
      }

// a stem's value: the gesture's, else as rendered
int NoteLanes::value(const Mark& m, bool join) const
      {
      if (_drag != Drag::NONE && (_selTarget == JOIN) == join) {
            auto it = _work.find(m.note);
            if (it != _work.end())
                  return it->second;
            }
      return join ? m.joinMs : m.velocity;
      }

bool NoteLanes::own(const Mark& m, bool join) const
      {
      if (_drag != Drag::NONE && (_selTarget == JOIN) == join && _work.count(m.note))
            return true;
      return join ? m.ownJoin : m.ownVelocity;
      }

int NoteLanes::joinOf(const Mark& m) const
      {
      return m.joinable() ? value(m, true) : m.joinMs;
      }

int NoteLanes::range(const Row& r) const
      {
      if (_drag != Drag::NONE && _row.master == r.master && _selTarget == JOIN)
            return _range;
      int most = JOIN_RANGE_MS;
      for (const Mark& m : partMarks(r.master))
            if (m.joinable())
                  most = std::max(most, std::abs(value(m, true)));
      return (most + 49) / 50 * 50;
      }

// Velocity: 0 at the bottom, 127 at the top; Join: -range … +range
double NoteLanes::yOf(const Row& r, double v, int rng) const
      {
      const QRectF vr = _lanes->valueRect(r);
      const double f = isJoin(r) ? (v + rng) / (2.0 * rng) : v / 127.0;
      return vr.bottom() - std::min(1.0, std::max(0.0, f)) * vr.height();
      }

double NoteLanes::valueAtY(const Row& r, double y, int rng) const
      {
      const QRectF vr = _lanes->valueRect(r);
      const double f = std::min(1.0, std::max(0.0, (vr.bottom() - y) / vr.height()));
      return isJoin(r) ? f * 2.0 * rng - rng : f * 127.0;
      }

std::vector<NoteLanes::Stem> NoteLanes::stems(const Row& r) const
      {
      std::vector<Stem> out;
      const bool join = isJoin(r);
      const int rng = range(r);
      const double px = _lanes->pixel();
      int lastTick = INT_MIN;
      int k = 0;
      for (const Mark& m : partMarks(r.master)) {
            if (join && !m.joinable())
                  continue;
            k = m.tick == lastTick ? k + 1 : 0;        // (a chord's notes side by side)
            lastTick = m.tick;
            const int v = value(m, join);
            const double x = _lanes->tickToX(m.tick) + k * 2.5 * STEM_PX * px;
            out.push_back({ &m, QPointF(x, yOf(r, v, rng)), yOf(r, 0, rng), v, own(m, join) });
            }
      return out;
      }

const Mark* NoteLanes::stemAt(const Row& r, const QPointF& p) const
      {
      const double rad = STEM_PX * 1.8 * _lanes->pixel();
      const Mark* best = nullptr;
      double bestD = rad;
      for (const Stem& s : stems(r)) {
            const double d = std::hypot(s.head.x() - p.x(), s.head.y() - p.y());
            if (d <= bestD) {
                  bestD = d;
                  best = s.mark;
                  }
            }
      return best;
      }

// the note sounding at tick (the last starting at or before it)
const Mark* NoteLanes::markAt(const Row& r, int tick) const
      {
      const Mark* at = nullptr;
      for (const Mark& m : partMarks(r.master)) {
            if (m.tick > tick)
                  break;
            if (!isJoin(r) || m.joinable())
                  at = &m;
            }
      return at;
      }

QString NoteLanes::describe(const Mark& m, bool join, int v) const
      {
      if (join) {
            const bool transition = performanceAt(m, v) == MidiRenderer::LibPerformance::TRANSITION;
            return transition ? tr("%1 ms · transition").arg(v) : tr("%1 ms · re-attack").arg(v);
            }
      const std::vector<Band> bands = velocityBands(m, joinOf(m));
      const Band* b = bandAt(bands, v);
      return b ? QString("%1 · %2").arg(v).arg(b->name) : QString::number(v);
      }

QString NoteLanes::title(const Row& r) const
      {
      return isJoin(r) ? tr("Join (ms)") : tr("Velocity (notes)");
      }

QString NoteLanes::valueText(const Row& r, int tick) const
      {
      const Mark* m = markAt(r, tick);
      if (!m)
            return QString::fromUtf8("–");
      const bool join = isJoin(r);
      QString s = describe(*m, join, value(*m, join));
      if (own(*m, join))
            s += "  " + tr("own");
      return s;
      }

QString NoteLanes::hoverText(const Row& r, const QPointF& p) const
      {
      const Mark* m = stemAt(r, p);
      return m ? describe(*m, isJoin(r), value(*m, isJoin(r))) : QString();
      }

//---------------------------------------------------------
//   paint
//---------------------------------------------------------

void NoteLanes::paintLane(QPainter& p, const Row& r, const QRectF& visible) const
      {
      const double px = _lanes->pixel();
      const bool join = isJoin(r);
      const int rng = range(r);
      const std::vector<Mark>& ms = partMarks(r.master);
      p.setPen(Qt::NoPen);
      p.setBrush(_lanes->rowColor(r));
      p.drawRect(r.rect);
      QFont f = p.font();
      f.setPointSizeF(9 * px);
      f.setItalic(!join);
      p.setFont(f);
      const QFontMetricsF fm(f);
      // the bands behind each note, to the next note (or its end)
      QString lastBands;
      for (size_t i = 0; i < ms.size(); ) {
            const Mark& m = ms[i];
            size_t j = i + 1;
            while (j < ms.size() && ms[j].tick == m.tick)
                  ++j;
            const int endTick = j < ms.size() ? std::min(ms[j].tick, std::max(m.endTick, ms[j].tick)) : m.endTick;
            const double x0 = std::max(r.rect.left(), _lanes->tickToX(m.tick));
            const double x1 = std::min(r.rect.right(), _lanes->tickToX(std::max(endTick, m.tick + 1)));
            i = j;
            if (x1 < visible.left() || x0 > visible.right() || x1 <= x0)
                  continue;
            if (join) {
                  if (!m.joinable())
                        continue;
                  // transition from the dashed line up, re-attack below it
                  const double yGap = yOf(r, -legatoGap(m) - 0.5, rng);
                  const Band tb { Band::Kind::TRANSITION, 0, 0, "bowed", 2 };
                  const Band ab { Band::Kind::ATTACK, 0, 0, "spiccato", 1 };
                  p.setPen(Qt::NoPen);
                  p.setBrush(bandColor(tb, true));
                  p.drawRect(QRectF(QPointF(x0, r.rect.top()), QPointF(x1, yGap)));
                  p.setBrush(bandColor(ab, true));
                  p.drawRect(QRectF(QPointF(x0, yGap), QPointF(x1, r.rect.bottom())));
                  p.setPen(QPen(QColor(110, 110, 110), px, Qt::DashLine));
                  p.drawLine(QPointF(x0, yGap), QPointF(x1, yGap));
                  continue;
                  }
            const std::vector<Band> bands = velocityBands(m, joinOf(m));
            QString key;
            for (const Band& b : bands)
                  key += QString("%1:%2-%3 ").arg(b.name).arg(b.low).arg(b.high);
            for (const Band& b : bands) {
                  const QRectF br(QPointF(x0, yOf(r, b.high + 1, rng)), QPointF(x1, yOf(r, b.low, rng)));
                  p.setPen(Qt::NoPen);
                  p.setBrush(bandColor(b, true));
                  p.drawRect(br);
                  // its name where the bands change (and room is)
                  if (key != lastBands && br.height() >= fm.height() * 0.9 && x1 - x0 > fm.horizontalAdvance(b.name) + 4 * px) {
                        p.setPen(bandColor(b, false));
                        p.drawText(br.adjusted(3 * px, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, b.name);
                        }
                  }
            lastBands = key;
            }
      _lanes->paintGrid(p, r, visible);
      if (join) {
            p.setPen(QPen(QColor(150, 150, 150), px));
            p.drawLine(QPointF(r.rect.left(), yOf(r, 0, rng)), QPointF(r.rect.right(), yOf(r, 0, rng)));
            }
      // the stems: the band's colour (none: gray); filled: the note's own value
      for (const Stem& s : stems(r)) {
            if (s.head.x() < visible.left() - 10 * px || s.head.x() > visible.right() + 10 * px)
                  continue;
            const Mark& m = *s.mark;
            QColor ink(110, 110, 110);
            if (join) {
                  const bool transition = performanceAt(m, s.value) == MidiRenderer::LibPerformance::TRANSITION;
                  ink = bandColor(transition ? Band { Band::Kind::TRANSITION, 0, 0, "bowed", 2 } : Band { Band::Kind::ATTACK, 0, 0, "spiccato", 1 }, false);
                  }
            else {
                  const std::vector<Band> bands = velocityBands(m, joinOf(m));
                  if (const Band* b = bandAt(bands, s.value))
                        ink = bandColor(*b, false);
                  }
            p.setPen(QPen(ink, 1.6 * px));
            p.drawLine(QPointF(s.head.x(), s.baseY), s.head);
            const bool sel = _selTarget == r.target && _sel.count(m.note);
            if (sel) {
                  p.setPen(QPen(QColor(29, 79, 140), 1.4 * px));
                  p.setBrush(Qt::NoBrush);
                  p.drawEllipse(s.head, (STEM_PX + 2.2) * px, (STEM_PX + 2.2) * px);
                  }
            p.setPen(QPen(ink, 1.6 * px));
            p.setBrush(s.own ? ink : QColor(Qt::white));
            p.drawEllipse(s.head, STEM_PX * px, STEM_PX * px);
            }
      p.setPen(QPen(QColor(200, 200, 200), px));
      p.setBrush(Qt::NoBrush);
      p.drawRect(r.rect);
      }

//---------------------------------------------------------
//   editing
//---------------------------------------------------------

bool NoteLanes::press(const Row& r, const QPointF& p, Qt::KeyboardModifiers mods, bool drawMode)
      {
      if (!fresh())
            return true;                  // (playing, changed since: edited when stopped)
      if (_selTarget != r.target)
            _sel.clear();
      _selTarget = r.target;
      _row = r;
      _pressPos = p;
      _last = p;
      _range = range(r);
      _work.clear();
      _base.clear();
      if (drawMode) {
            _drag = Drag::DRAW;
            move(p, mods);
            return true;
            }
      const Mark* m = stemAt(r, p);
      if (!m) {
            if (!(mods & Qt::ControlModifier))
                  _sel.clear();
            _drag = Drag::PENDING;
            _view->update();
            return true;
            }
      if (mods & Qt::ControlModifier) {
            if (!_sel.erase(m->note))
                  _sel.insert(m->note);
            _view->update();
            return true;
            }
      if (!_sel.count(m->note))
            _sel = { m->note };
      for (const Mark& x : partMarks(r.master))
            if (_sel.count(x.note) && (!isJoin(r) || x.joinable()))
                  _base[x.note] = isJoin(r) ? x.joinMs : x.velocity;
      _drag = Drag::MOVE;
      _view->update();
      return true;
      }

void NoteLanes::move(const QPointF& p, Qt::KeyboardModifiers mods)
      {
      const Row& r = _row;
      const bool join = isJoin(r);
      const int lo = join ? -_range : 1;
      const int hi = join ? _range : 127;
      switch (_drag) {
            case Drag::MOVE: {
                  // (Shift: a quarter as fast)
                  double dv = valueAtY(r, p.y(), _range) - valueAtY(r, _pressPos.y(), _range);
                  if (mods & Qt::ShiftModifier)
                        dv *= 0.25;
                  for (const auto& b : _base)
                        _work[b.first] = std::max(lo, std::min(hi, int(std::lround(b.second + dv))));
                  break;
                  }
            case Drag::PENDING:
                  if (std::hypot(p.x() - _pressPos.x(), p.y() - _pressPos.y()) < 3 * _lanes->pixel())
                        return;
                  _drag = Drag::RUBBER;
                  // fall through
            case Drag::RUBBER: {
                  const QRectF band = QRectF(_pressPos, p).normalized();
                  if (!(mods & Qt::ControlModifier))
                        _sel.clear();
                  for (const Stem& s : stems(r))
                        if (band.contains(s.head))
                              _sel.insert(s.mark->note);
                  break;
                  }
            case Drag::DRAW: {
                  // every stem between the last position and this one: the value along the path
                  const double xa = std::min(_last.x(), p.x()) - STEM_PX * _lanes->pixel();
                  const double xb = std::max(_last.x(), p.x()) + STEM_PX * _lanes->pixel();
                  for (const Stem& s : stems(r)) {
                        if (s.head.x() < xa || s.head.x() > xb)
                              continue;
                        const double t = std::fabs(p.x() - _last.x()) < 1e-9 ? 1.0 : (s.head.x() - _last.x()) / (p.x() - _last.x());
                        const double y = _last.y() + std::min(1.0, std::max(0.0, t)) * (p.y() - _last.y());
                        _work[s.mark->note] = std::max(lo, std::min(hi, int(std::lround(valueAtY(r, y, _range)))));
                        }
                  _last = p;
                  break;
                  }
            case Drag::NONE:
                  return;
            }
      _view->update();
      }

void NoteLanes::release()
      {
      const Drag d = _drag;
      _drag = Drag::NONE;
      if (d == Drag::MOVE || d == Drag::DRAW) {
            std::map<const Note*, int> values;
            values.swap(_work);
            commit(values, _selTarget == JOIN, d == Drag::DRAW ? tr("Notes: drawn") : tr("Notes: moved"));
            }
      _work.clear();
      _base.clear();
      _view->update();
      }

void NoteLanes::doubleClick(const Row& r, const QPointF& p)
      {
      if (!fresh())
            return;
      const Mark* m = stemAt(r, p);
      if (!m)
            return;
      _selTarget = r.target;
      if (!_sel.count(m->note))
            _sel = { m->note };
      resetSelected();
      }

void NoteLanes::resetSelected()
      {
      if (_sel.empty() || !fresh())
            return;
      const bool join = _selTarget == JOIN;
      std::map<const Note*, int> values;
      for (const Note* n : _sel)
            values[n] = join ? PerformanceTechnique::JOIN_AUTO : 0;
      commit(values, join, tr("Notes: Auto"));
      }

void NoteLanes::clearSelection()
      {
      if (_sel.empty())
            return;
      _sel.clear();
      _view->update();
      }

void NoteLanes::contextMenu(const Row& r, const QPointF& p, const QPoint& globalPos)
      {
      if (!fresh())
            return;
      const bool join = isJoin(r);
      if (const Mark* m = stemAt(r, p)) {
            if (_selTarget != r.target || !_sel.count(m->note))
                  _sel = { m->note };
            _selTarget = r.target;
            }
      else if (_selTarget != r.target)
            _sel.clear();
      _view->update();
      QMenu menu;
      QAction* edit = menu.addAction(tr("Edit Value…"));
      QAction* reset = menu.addAction(tr("Reset to Auto"));
      edit->setEnabled(!_sel.empty());
      reset->setEnabled(!_sel.empty());
      menu.addSeparator();
      QAction* hide = menu.addAction(tr("Hide Lane"));
      QAction* a = menu.exec(globalPos);
      if (a == hide)
            _lanes->showLane(r.part, r.target, false);
      else if (a == reset)
            resetSelected();
      else if (a == edit) {
            int start = join ? 0 : 64;
            for (const Mark& m : partMarks(r.master))
                  if (_sel.count(m.note)) {
                        start = join ? m.joinMs : m.velocity;
                        break;
                        }
            bool ok = false;
            const int v = join ? QInputDialog::getInt(_view, tr("Join"), tr("ms (> 0: the note before overlaps; < 0: a gap):"),
                                                      start, -PerformanceTechnique::JOIN_MAX, PerformanceTechnique::JOIN_MAX, 1, &ok)
                               : QInputDialog::getInt(_view, tr("Velocity"), tr("Velocity (1-127):"), start, 1, 127, 1, &ok);
            if (!ok)
                  return;
            std::map<const Note*, int> values;
            for (const Mark& m : partMarks(r.master))
                  if (_sel.count(m.note) && (!join || m.joinable()))
                        values[m.note] = v;
            commit(values, join, tr("Notes: value"));
            }
      }

// the values as one undoable step (each note's linked ones too)
void NoteLanes::commit(const std::map<const Note*, int>& values, bool join, const QString& what)
      {
      Score* s = _lanes->score();
      if (!s || values.empty())
            return;
      const Pid pid = join ? Pid::LIBRARY_JOIN : Pid::LIBRARY_VELOCITY;
      s->startCmd();
      for (const auto& v : values) {
            Note* n = const_cast<Note*>(v.first);
            if (n->getProperty(pid).toInt() != v.second)
                  n->undoChangeProperty(pid, v.second);
            }
      s->endCmd();
      if (mscore)
            mscore->showMessage(what, 1500);
      // (as rendered now: the stems where they were let go until then)
      render();
      _view->update();
      }

}     // namespace Ms
