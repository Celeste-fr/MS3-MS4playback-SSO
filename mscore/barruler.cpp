//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

// Design: barruler.h

#include "barruler.h"
#include "continuouspanel.h"
#include "scoreview.h"

#include "libmscore/measure.h"
#include "libmscore/score.h"
#include "libmscore/system.h"

namespace Ms {
namespace BarRuler {

static const char* SETTING = "ui/canvas/barRuler";

bool enabled()
      {
      return QSettings().value(SETTING, true).toBool();
      }

void setEnabled(bool on)
      {
      QSettings().setValue(SETTING, on);
      }

static QFont rulerFont(const ScoreView* view)
      {
      QFont f = view->font();
      f.setPointSizeF(qMax(7.0, f.pointSizeF() * 0.85));
      return f;
      }

int height(const ScoreView* view)
      {
      if (!view || !view->score() || view->score()->layoutMode() != LayoutMode::LINE || !enabled())
            return 0;
      return QFontMetrics(rulerFont(view)).height() + 4;
      }

//---------------------------------------------------------
//   label
//    what the measure-number style would print; empty for a measure it never numbers (irregular)
//---------------------------------------------------------

static QString label(const Measure* m, int* first, int* last)
      {
      *first = m->no() + 1;
      *last  = *first;
      if (m->isMMRest()) {
            const Measure* l = m->mmRestLast();
            if (l)
                  *last = l->no() + 1;
            if (*last > *first)
                  return QString("%1–%2").arg(*first).arg(*last);
            }
      if (m->irregular())
            return QString();
      return QString::number(*first);
      }

//---------------------------------------------------------
//   paint
//    in widget coordinates, over everything else ScoreView paints
//---------------------------------------------------------

void paint(const ScoreView* view, QPainter& p, ContinuousPanel* panel)
      {
      const int h = height(view);
      if (h <= 0)
            return;
      Score* score = view->score();
      const int w = view->width();

      p.save();
      p.resetTransform();
      p.setClipping(false);
      p.setRenderHint(QPainter::Antialiasing, false);
      const QFont font = rulerFont(view);
      p.setFont(font);
      const QFontMetrics fm(font);

      const QColor bg(236, 236, 236, 235);
      const QColor ink(60, 60, 60);
      const QColor tick(150, 150, 150);
      p.fillRect(QRect(0, 0, w, h), bg);
      p.setPen(tick);
      p.drawLine(0, h - 1, w, h - 1);

      // the Continuous panel covers the ruler's left end
      int left = 0;
      if (panel && panel->visible())
            left = qMax(0, int(panel->panelWidth() * view->physicalZoomLevel()));

      // the measures on screen, at their left barlines
      struct Bar { const Measure* m; int x; };
      std::vector<Bar> bars;
      for (const Measure* m = score->firstMeasureMM(); m; m = m->nextMeasureMM()) {
            if (!m->system())
                  continue;
            const QPointF cp = m->canvasPos();
            const int x  = qRound(view->toPhysical(cp).x());
            const int xr = qRound(view->toPhysical(QPointF(cp.x() + m->width(), cp.y())).x());
            if (xr < left)
                  continue;
            if (x > w)
                  break;
            bars.push_back({ m, x });
            }
      int lastNo = 1;
      if (const Measure* lm = score->lastMeasure())
            lastNo = lm->no() + 1;

      if (!bars.empty()) {
            // the step: 1, 2, 4, 8 ... bars, so that a label fits its bars (as Live thins its ruler)
            const int labelW = fm.horizontalAdvance(QString::number(lastNo)) + 6;
            double avg = 0.0;
            int n = 0;
            for (const Bar& b : bars) {
                  if (!b.m->isMMRest()) {
                        avg += b.m->width() * view->physicalZoomLevel();
                        ++n;
                        }
                  }
            avg = n ? avg / n : labelW;
            int step = 1;
            while (step * avg < labelW && step < (1 << 20))
                  step *= 2;

            const int ty = h - 3 - fm.descent();
            int lastRight = left - 1000;
            for (const Bar& b : bars) {
                  int first, last;
                  const QString s = label(b.m, &first, &last);
                  bool show = !s.isEmpty();
                  if (show && step > 1) {
                        // a bar on the step (1, 1 + step ...), or a multimeasure rest containing one
                        const int k = ((first - 1 + step - 1) / step) * step + 1;
                        show = k <= last;
                        }
                  const int tx = b.x + 3;
                  if (show && (tx < lastRight || tx < left))
                        show = false;
                  if (b.x >= left) {
                        p.setPen(tick);
                        if (show)
                              p.drawLine(b.x, 0, b.x, h - 1);
                        else if (avg >= 4.0 || b.m->isMMRest())
                              p.drawLine(b.x, h - 5, b.x, h - 1);
                        }
                  if (show) {
                        p.setPen(ink);
                        p.drawText(tx, ty, s);
                        lastRight = tx + fm.horizontalAdvance(s) + 6;
                        }
                  }
            }
      p.restore();
      }

}     // namespace BarRuler
}     // namespace Ms
