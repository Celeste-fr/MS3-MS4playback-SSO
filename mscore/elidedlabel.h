//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __MSCORE_ELIDEDLABEL_H__
#define __MSCORE_ELIDEDLABEL_H__

//---------------------------------------------------------
//   ElidedLabel: a one-line plain-text label for the main window's status bar that never asks for more width
//   than it is given (the owner, 2026-10-03: a clip tab's long status line made the window wider than the screen
//   and pushed the docks off it). Its minimum width is 0: QMainWindow's minimum width is the status bar's, so a
//   QLabel there widens the window to its whole text. It asks for its whole text (sizeHint) and, given less,
//   draws the text cut with "…"; text() stays the whole text, and a cut one is the tooltip (unless a tooltip is
//   set). A drop-in QLabel: setText, text, alignment as usual (plain text only).
//---------------------------------------------------------

#include <QEvent>
#include <QHelpEvent>
#include <QLabel>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

namespace Ms {

class ElidedLabel : public QLabel {
   public:
      explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent)
            {
            setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            setWordWrap(false);
            }
      QSize minimumSizeHint() const override
            {
            return QSize(0, QLabel::minimumSizeHint().height());
            }
      // the text as drawn in the label's width now
      QString shownText() const
            {
            return fontMetrics().elidedText(text(), Qt::ElideRight, qMax(0, contentsRect().width() - 2 * margin()));
            }
      bool elided() const { return shownText() != text(); }

   protected:
      void paintEvent(QPaintEvent*) override
            {
            QPainter p(this);
            const QRect r = contentsRect().adjusted(margin(), margin(), -margin(), -margin());
            style()->drawItemText(&p, r, int(alignment()) | Qt::TextSingleLine, palette(), isEnabled(), shownText(),
                                  foregroundRole());
            }
      bool event(QEvent* e) override
            {
            if (e->type() == QEvent::ToolTip && toolTip().isEmpty()) {
                  if (elided())
                        QToolTip::showText(static_cast<QHelpEvent*>(e)->globalPos(), text(), this);
                  else
                        QToolTip::hideText();
                  return true;
                  }
            return QLabel::event(e);
            }
      };

}     // namespace Ms
#endif
