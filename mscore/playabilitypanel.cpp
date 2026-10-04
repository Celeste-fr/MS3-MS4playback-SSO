//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playabilitypanel.h"
#include "musescore.h"
#include "scoreview.h"

#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/select.h"
#include "libmscore/staff.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace Ms {

//---------------------------------------------------------
//   DisplayListView
//---------------------------------------------------------

DisplayListView::DisplayListView(QWidget* parent)
   : QWidget(parent)
      {
      setMinimumSize(0, 0);
      setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
      }

void DisplayListView::setLayout(Layout l)
      {
      _layout = l;
      relayout();
      }

void DisplayListView::relayout()
      {
      _items = _layout ? _layout(width(), height()) : DisplayList();
      update();
      emit relaidOut();
      }

void DisplayListView::resizeEvent(QResizeEvent*)
      {
      relayout();
      }

// the wheel over the graph changes instrument, a notch (120) at a time
void DisplayListView::wheelEvent(QWheelEvent* e)
      {
      if (!wheelSteps) {
            e->ignore();
            return;
            }
      _wheel += e->angleDelta().y();
      if (std::abs(_wheel) >= 120) {
            emit wheelStep(_wheel < 0 ? 1 : -1);
            _wheel = 0;
            }
      e->accept();
      }

static QColor withOpacity(QColor c, double opacity)
      {
      c.setAlphaF(c.alphaF() * opacity);
      return c;
      }

void DisplayListView::paintEvent(QPaintEvent*)
      {
      QPainter p(this);
      for (const DrawItem& it : _items) {
            switch (it.kind) {
                  case DrawItem::Kind::LINE: {
                        // every line is horizontal or vertical: drawn as the plugin's rectangles, sharp
                        p.setRenderHint(QPainter::Antialiasing, false);
                        QColor c = withOpacity(it.color, it.opacity);
                        if (it.x1 == it.x2)
                              p.fillRect(QRectF(it.x1 - it.width / 2, std::min(it.y1, it.y2), it.width, std::abs(it.y2 - it.y1)), c);
                        else
                              p.fillRect(QRectF(std::min(it.x1, it.x2), it.y1 - it.width / 2, std::abs(it.x2 - it.x1), it.width), c);
                        break;
                        }
                  case DrawItem::Kind::RECT:
                        p.setRenderHint(QPainter::Antialiasing, it.smooth);
                        if (it.fill.isValid())
                              p.fillRect(QRectF(it.x, it.y, it.w, it.h), withOpacity(it.fill, it.opacity));
                        break;
                  case DrawItem::Kind::CIRCLE:
                        p.setRenderHint(QPainter::Antialiasing, true);
                        p.setBrush(it.fill.isValid() ? QBrush(withOpacity(it.fill, it.opacity)) : Qt::NoBrush);
                        if (it.stroke.isValid() && !it.fill.isValid())
                              p.setPen(QPen(withOpacity(it.stroke, it.opacity), it.width));
                        else
                              p.setPen(Qt::NoPen);
                        // the ring's width sits inside the circle, as a QML border does
                        if (it.stroke.isValid() && !it.fill.isValid())
                              p.drawEllipse(QPointF(it.x, it.y), it.r - it.width / 2, it.r - it.width / 2);
                        else
                              p.drawEllipse(QPointF(it.x, it.y), it.r, it.r);
                        break;
                  case DrawItem::Kind::LABEL: {
                        if (it.text.isEmpty())
                              break;
                        p.setRenderHint(QPainter::Antialiasing, true);
                        QFont f = font();
                        f.setPixelSize(std::max(1, int(std::round(it.size))));
                        f.setBold(it.bold);
                        QFontMetricsF fm(f);
                        double w = fm.horizontalAdvance(it.text);
                        double x = it.align == 0 ? it.x - w / 2 : it.align > 0 ? it.x - w : it.x;
                        if (it.halo.isValid()) {
                              QPainterPath path;
                              path.addText(QPointF(x, it.y), f, it.text);
                              p.setPen(QPen(it.halo, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                              p.setBrush(Qt::NoBrush);
                              p.drawPath(path);
                              }
                        p.setFont(f);
                        p.setPen(withOpacity(it.color.isValid() ? it.color : QColor("#333333"), it.opacity));
                        p.drawText(QPointF(x, it.y), it.text);
                        break;
                        }
                  case DrawItem::Kind::META:
                        break;
                  }
            }
      }

//---------------------------------------------------------
//   PlayabilityPanel
//---------------------------------------------------------

static const int ROLE_ROW = Qt::UserRole + 1;

PlayabilityPanel::PlayabilityPanel(QWidget* parent)
   : QDockWidget(parent)
      {
      setObjectName("playability-panel");
      setAllowedAreas(Qt::DockWidgetAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea));

      QWidget* w = new QWidget;
      QVBoxLayout* vl = new QVBoxLayout(w);
      vl->setContentsMargins(10, 10, 10, 10);
      vl->setSpacing(8);

      _off = new QLabel;
      _off->setWordWrap(true);
      vl->addWidget(_off);

      _stack = new QStackedWidget;
      _stack->setMinimumSize(0, 0);

      _table = new QTableWidget(0, 4);
      _table->verticalHeader()->hide();
      _table->setSelectionBehavior(QAbstractItemView::SelectRows);
      _table->setSelectionMode(QAbstractItemView::SingleSelection);
      _table->setEditTriggers(QAbstractItemView::NoEditTriggers);
      _table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
      _table->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
      _table->setWordWrap(false);
      _table->setShowGrid(false);
      _table->setFocusPolicy(Qt::NoFocus);
      _table->horizontalHeader()->setHighlightSections(false);
      _table->horizontalHeader()->setStretchLastSection(false);
      _table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
      _table->verticalHeader()->setDefaultSectionSize(_table->fontMetrics().height() + 6);
      connect(_table, &QTableWidget::cellClicked, this, [this](int row, int) { rowClicked(row); });
      _stack->addWidget(_table);

      _board = new DisplayListView;
      connect(_board, &DisplayListView::relaidOut, this, [this]() { updateSelectedLine(); });
      _stack->addWidget(_board);

      // W1: the chips of the graphs (instruments), arrows, and the graph
      _graphPage = new QWidget;
      QVBoxLayout* gl = new QVBoxLayout(_graphPage);
      gl->setContentsMargins(0, 0, 0, 0);
      gl->setSpacing(4);
      QHBoxLayout* chipRow = new QHBoxLayout;
      chipRow->setSpacing(4);
      _chipArea = new QScrollArea;
      _chipArea->setFrameShape(QFrame::NoFrame);
      _chipArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      _chipArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      _chipArea->setWidgetResizable(true);
      _chipArea->setFixedHeight(28);
      _chips = new QWidget;
      _chipLayout = new QHBoxLayout(_chips);
      _chipLayout->setContentsMargins(0, 2, 0, 2);
      _chipLayout->setSpacing(5);
      _chipLayout->addStretch(1);
      _chipArea->setWidget(_chips);
      _chipArea->viewport()->installEventFilter(this);
      chipRow->addWidget(_chipArea, 1);
      _prev = new QToolButton;
      _prev->setText(QString::fromUtf8("‹"));
      _next = new QToolButton;
      _next->setText(QString::fromUtf8("›"));
      connect(_prev, &QToolButton::clicked, this, [this]() { showGraph(_graphIndex - 1); });
      connect(_next, &QToolButton::clicked, this, [this]() { showGraph(_graphIndex + 1); });
      chipRow->addWidget(_prev);
      chipRow->addWidget(_next);
      gl->addLayout(chipRow);
      _count = new QLabel;
      _count->setAlignment(Qt::AlignRight);
      QFont cf = _count->font();
      cf.setPixelSize(10);
      _count->setFont(cf);
      gl->addWidget(_count);
      _graph = new DisplayListView;
      _graph->wheelSteps = true;
      connect(_graph, &DisplayListView::wheelStep, this, [this](int d) { showGraph(_graphIndex + d); });
      gl->addWidget(_graph, 1);
      _stack->addWidget(_graphPage);

      vl->addWidget(_stack, 1);

      _selected = new QLabel;
      _selected->setWordWrap(true);
      _selected->setTextInteractionFlags(Qt::TextSelectableByMouse);
      QFont sf = _selected->font();
      sf.setPixelSize(11);
      _selected->setFont(sf);
      vl->addWidget(_selected);

      QHBoxLayout* buttons = new QHBoxLayout;
      _toggle = new QPushButton;
      connect(_toggle, &QPushButton::clicked, this, &PlayabilityPanel::toggleList);
      buttons->addWidget(_toggle);
      buttons->addStretch(1);
      vl->addLayout(buttons);

      setWidget(w);
      retranslate();
      setScore(nullptr);
      }

void PlayabilityPanel::retranslate()
      {
      setWindowTitle(tr("Playability"));
      _table->setHorizontalHeaderLabels({ "", tr("Bar"), tr("Staff"), tr("Reason") });
      _prev->setToolTip(tr("Previous instrument"));
      _next->setToolTip(tr("Next instrument"));
      _toggle->setToolTip(tr("Switch between the diagram of the selection and the list"));
      _off->setText(tr("The playability check is off (View › Check Playability)."));
      }

void PlayabilityPanel::changeEvent(QEvent* e)
      {
      QDockWidget::changeEvent(e);
      if (e->type() == QEvent::LanguageChange)
            retranslate();
      }

// the wheel over the chips slides them sideways
bool PlayabilityPanel::eventFilter(QObject* o, QEvent* e)
      {
      if (o == _chipArea->viewport() && e->type() == QEvent::Wheel) {
            QWheelEvent* we = static_cast<QWheelEvent*>(e);
            QPoint d = we->angleDelta();
            int delta = std::abs(d.x()) > std::abs(d.y()) ? d.x() : d.y();
            QScrollBar* sb = _chipArea->horizontalScrollBar();
            sb->setValue(sb->value() - delta / 2);
            return true;
            }
      return QDockWidget::eventFilter(o, e);
      }

//---------------------------------------------------------
//   setScore
//    after every command and selection change (MuseScore::updateInspector), and on a tab switch
//---------------------------------------------------------

static bool sameRows(const std::vector<PlayabilityRow>& a, const std::vector<PlayabilityRow>& b)
      {
      if (a.size() != b.size())
            return false;
      for (size_t i = 0; i < a.size(); ++i)
            if (a[i].tick != b[i].tick || a[i].tickEnd != b[i].tickEnd || a[i].track != b[i].track || a[i].grace != b[i].grace
               || a[i].verdict != b[i].verdict || a[i].reason != b[i].reason || a[i].staffShort != b[i].staffShort || a[i].bar != b[i].bar)
                  return false;
      return true;
      }

void PlayabilityPanel::setScore(Score* s)
      {
      bool newScore = s != _score;
      _score = s;
      _off->setVisible(!Playability::enabled);

      // rows, the way the score reads: by bar, then staff
      std::vector<PlayabilityRow> rows;
      if (_score && _score->playability())
            rows = _score->playability()->rows;
      std::stable_sort(rows.begin(), rows.end(), [](const PlayabilityRow& a, const PlayabilityRow& b) {
            if (a.tick != b.tick)
                  return a.tick < b.tick;
            if (a.track != b.track)
                  return a.track < b.track;
            return a.grace < b.grace;
            });
      if (newScore || !sameRows(rows, _rows)) {
            _rows = rows;
            rebuildTable();
            }

      // the selection: what it is, its fingerboard, or its wind bars
      Chord* chord = _score ? Playability::selectedChord(_score) : nullptr;
      QString key;
      bool spans = false;
      if (chord) {
            const Chord* main = chord->isGrace() ? toChord(chord->parent()) : chord;
            key = QString("%1|%2|%3").arg(chord->track()).arg(main->tick().ticks()).arg(chord->isGrace() ? main->graceNotes().indexOf(chord) : -1);
            for (Element* e : _score->selection().elements()) {
                  Element* c = e;
                  for (int d = 0; c && d < 4 && !c->isChord(); ++d)
                        c = c->parent();
                  if (c && c->isChord() && c != chord)
                        spans = true;
                  }
            }
      if (key != _selKey || newScore) {
            _selKey = key;
            _showList = false;
            }
      _info = chord ? Playability::inspect(chord) : ChordInfo();
      if (spans)
            _info.kind = ChordInfo::Kind::NONE;         // several chords: no fingerboard
      _wind = (_score && _info.kind == ChordInfo::Kind::NONE) ? Playability::windModel(_score) : Playability::WindModel();
      if (_wind.valid()) {
            if (_wind.key != _windKey) {
                  _windKey = _wind.key;
                  _graphIndex = _wind.first;
                  }
            _graphIndex = std::max(0, std::min(_graphIndex, int(_wind.graphs.size()) - 1));
            }
      rebuildChips();
      const ChordInfo info = _info;
      _board->setLayout([info](double w, double h) { return Playability::layoutFingerboard(info, w, h); });
      updatePage();
      syncRow();
      }

void PlayabilityPanel::rebuildTable()
      {
      _table->setRowCount(int(_rows.size()));
      for (int i = 0; i < int(_rows.size()); ++i) {
            const PlayabilityRow& r = _rows[i];
            // what the row is: red for something unplayable, dark yellow for a stretch or a limit
            // that is over, the colours the noteheads get
            QPixmap px(10, 10);
            px.fill(Qt::transparent);
            {
            QPainter p(&px);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(Qt::NoPen);
            p.setBrush(r.error() ? Playability::IMPOSSIBLE_COLOR : Playability::OUT_OF_REACH_COLOR);
            p.drawRoundedRect(QRectF(0, 0, 10, 10), 2, 2);
            }
            QTableWidgetItem* mark = new QTableWidgetItem(QIcon(px), "");
            mark->setData(ROLE_ROW, i);
            mark->setToolTip(r.error() ? tr("Cannot be played")
                              : r.advisory() ? tr("Playable, but may sound unclear") : tr("Hard to play, or over a limit"));
            _table->setItem(i, 0, mark);
            QTableWidgetItem* bar = new QTableWidgetItem(QString::number(r.bar));
            _table->setItem(i, 1, bar);
            QTableWidgetItem* staff = new QTableWidgetItem(r.staffShort);
            staff->setToolTip(r.staff);
            _table->setItem(i, 2, staff);
            QTableWidgetItem* reason = new QTableWidgetItem(r.reason);
            reason->setToolTip(r.notes);
            _table->setItem(i, 3, reason);
            }
      fitReasonColumn();
      }

// Bar and Staff as wide as their longest entry; Reason as wide as its longest reason, or the rest
// of the panel when that is wider: a horizontal scroll bar appears when they don't fit
void PlayabilityPanel::fitReasonColumn()
      {
      QFontMetrics fm = _table->fontMetrics();
      QFontMetrics hm = _table->horizontalHeader()->fontMetrics();
      int bw = hm.horizontalAdvance(tr("Bar")), sw = hm.horizontalAdvance(tr("Staff")), rw = hm.horizontalAdvance(tr("Reason"));
      for (const PlayabilityRow& r : _rows) {
            bw = std::max(bw, fm.horizontalAdvance(QString::number(r.bar)));
            sw = std::max(sw, fm.horizontalAdvance(r.staffShort));
            rw = std::max(rw, fm.horizontalAdvance(r.reason));
            }
      _table->setColumnWidth(0, 22);
      _table->setColumnWidth(1, bw + 18);
      _table->setColumnWidth(2, sw + 18);
      int rest = _table->viewport()->width() - 22 - (bw + 18) - (sw + 18);
      _table->setColumnWidth(3, std::max(rw + 18, rest));
      }

void PlayabilityPanel::resizeEvent(QResizeEvent* e)
      {
      QDockWidget::resizeEvent(e);
      fitReasonColumn();
      }

//---------------------------------------------------------
//   W1 chips
//---------------------------------------------------------

void PlayabilityPanel::rebuildChips()
      {
      while (_chipLayout->count() > 1) {
            QLayoutItem* it = _chipLayout->takeAt(0);
            delete it->widget();
            delete it;
            }
      if (!_wind.valid())
            return;
      for (int i = 0; i < int(_wind.graphs.size()); ++i) {
            QPushButton* b = new QPushButton(_wind.graphs[i].chip);
            b->setCheckable(true);
            b->setChecked(i == _graphIndex);
            b->setFocusPolicy(Qt::NoFocus);
            b->setToolTip(_wind.graphs[i].title);
            b->setStyleSheet("QPushButton { border: 1px solid #c4c4c4; border-radius: 11px; padding: 2px 9px; background: #ffffff; color: #222222; font-size: 11px; }"
                             "QPushButton:checked { background: #222222; border-color: #222222; color: #ffffff; }");
            connect(b, &QPushButton::clicked, this, [this, i]() { showGraph(i); });
            _chipLayout->insertWidget(i, b);
            }
      }

void PlayabilityPanel::showGraph(int i)
      {
      if (!_wind.valid() || i < 0 || i >= int(_wind.graphs.size()))
            return;
      _graphIndex = i;
      for (int k = 0; k < _chipLayout->count() - 1; ++k)
            if (QPushButton* b = qobject_cast<QPushButton*>(_chipLayout->itemAt(k)->widget()))
                  b->setChecked(k == i);
      if (QWidget* chip = _chipLayout->itemAt(i)->widget())
            _chipArea->ensureWidgetVisible(chip, 8, 0);
      updatePage();
      }

//---------------------------------------------------------
//   updatePage
//    while a playable stop or a natural harmonic is selected, its fingerboard replaces the list;
//    while wind notes are, their register graph; "List" goes back to the list
//---------------------------------------------------------

void PlayabilityPanel::updatePage()
      {
      bool board = _info.kind != ChordInfo::Kind::NONE;
      bool graph = !board && _wind.valid();
      if (!_showList && board)
            _stack->setCurrentWidget(_board);
      else if (!_showList && graph) {
            const Playability::WindModel model = _wind;
            int gi = _graphIndex;
            _graph->setLayout([model, gi](double w, double h) { return Playability::layoutWindGraph(model, gi, w, h); });
            _prev->setEnabled(_graphIndex > 0);
            _next->setEnabled(_graphIndex < int(_wind.graphs.size()) - 1);
            _count->setText(QString("%1 / %2").arg(_graphIndex + 1).arg(_wind.graphs.size()));
            _count->setVisible(_wind.graphs.size() > 1);
            _stack->setCurrentWidget(_graphPage);
            }
      else
            _stack->setCurrentWidget(_table);
      _toggle->setVisible(board || graph);
      _toggle->setText(_showList ? (board ? tr("Fingerboard") : tr("Graph")) : tr("List"));
      updateSelectedLine();
      }

// Selected: the selected notes as written; left out under the graph, and under a fingerboard that
// names its notes itself
void PlayabilityPanel::updateSelectedLine()
      {
      bool show = !_info.text.isEmpty() && _stack->currentWidget() != _graphPage
                  && (_stack->currentWidget() != _board || !Playability::namesShown(_board->items()));
      _selected->setVisible(show);
      QString text = tr("Selected: %1").arg(_info.text);
      if (!_info.tuning.isEmpty())
            text += "\n" + tr("Tuning: %1").arg(_info.tuning);        // a scordatura in force
      _selected->setText(text);
      }

void PlayabilityPanel::toggleList()
      {
      _showList = !_showList;
      updatePage();
      }

//---------------------------------------------------------
//   syncRow
//    score -> panel: the row of the selected chord, else the row of the stroke it belongs to
//---------------------------------------------------------

void PlayabilityPanel::syncRow()
      {
      int row = -1, best = 0;
      QStringList k = _selKey.split('|');
      if (k.size() == 3) {
            int track = k[0].toInt(), tick = k[1].toInt(), grace = k[2].toInt();
            for (int i = 0; i < int(_rows.size()) && best < 2; ++i) {
                  const PlayabilityRow& r = _rows[i];
                  if (r.track != track)
                        continue;
                  int m = 0;
                  if (r.tick.ticks() == tick && r.grace == grace)
                        m = 2;
                  else if (r.tickEnd > r.tick && tick >= r.tick.ticks() && tick <= r.tickEnd.ticks())
                        m = 1;
                  if (m > best) {
                        best = m;
                        row = i;
                        }
                  }
            }
      QSignalBlocker block(_table);
      _table->clearSelection();
      if (row >= 0) {
            _table->selectRow(row);
            _table->scrollToItem(_table->item(row, 0), QAbstractItemView::EnsureVisible);
            }
      }

//---------------------------------------------------------
//   rowClicked
//    panel -> score: select everything the row is about, from its chord to the end of the last
//    chord it covers, as a range (MuseScore's blue box) when no other voice of the staff has notes
//    there, else note by note (a range would take the other voice too); a grace-note row selects
//    its chord. Then scroll to it.
//---------------------------------------------------------

static Chord* findChord(Score* score, int track, const Fraction& tick, int grace)
      {
      Segment* s = score->tick2segment(tick, true, SegmentType::ChordRest);
      Element* e = s ? s->element(track) : nullptr;
      if (!e || !e->isChord())
            return nullptr;
      Chord* c = toChord(e);
      if (grace < 0)
            return c;
      return grace < c->graceNotes().size() ? c->graceNotes()[grace] : nullptr;
      }

// does another voice of this staff have a chord sounding in [from, to)?
static bool otherVoiceBetween(Score* score, int track, const Fraction& from, const Fraction& to)
      {
      int st = track / VOICES;
      for (int v = 0; v < VOICES; ++v) {
            int t = st * VOICES + v;
            if (t == track)
                  continue;
            for (Segment* s = score->firstSegment(SegmentType::ChordRest); s && s->tick() < to; s = s->next1(SegmentType::ChordRest)) {
                  Element* e = s->element(t);
                  if (e && e->isChord() && s->tick() + toChord(e)->actualTicks() > from)
                        return true;
                  }
            }
      return false;
      }

void PlayabilityPanel::rowClicked(int row)
      {
      if (_syncing || !_score || row < 0 || row >= int(_rows.size()))
            return;
      const PlayabilityRow r = _rows[row];
      Chord* c = findChord(_score, r.track, r.tick, r.grace);
      if (!c) {
            _selected->setText(tr("That chord has changed since the check."));
            _selected->setVisible(true);
            return;
            }
      _syncing = true;
      int st = r.track / VOICES;
      _score->deselectAll();
      if (r.grace >= 0) {
            for (Note* n : c->notes())
                  _score->select(n, SelectType::ADD);
            }
      else {
            Chord* last = findChord(_score, r.track, r.tickEnd, -1);
            Fraction end = r.tickEnd + (last ? last->actualTicks() : Fraction(1, 1920));
            if (otherVoiceBetween(_score, r.track, r.tick, end)) {
                  for (Segment* s = c->segment(); s && s->tick() <= r.tickEnd; s = s->next1(SegmentType::ChordRest)) {
                        Element* e = s->element(r.track);
                        if (!e || !e->isChord())
                              continue;
                        for (Chord* g : toChord(e)->graceNotes())
                              for (Note* n : g->notes())
                                    _score->select(n, SelectType::ADD);
                        for (Note* n : toChord(e)->notes())
                              _score->select(n, SelectType::ADD);
                        }
                  }
            else {
                  Segment* endSeg = _score->tick2segment(end, true, SegmentType::ChordRest);    // nullptr: to the end
                  _score->selection().setRange(c->segment(), endSeg, st, st + 1);
                  _score->selection().updateSelectedElements();
                  }
            }
      _score->setSelectionChanged(true);
      if (ScoreView* v = mscore->currentScoreView())
            v->adjustCanvasPosition(c->upNote(), false, st);
      _score->setUpdateAll();
      mscore->endCmd();
      _score->update();
      _syncing = false;
      }

}     // namespace Ms
