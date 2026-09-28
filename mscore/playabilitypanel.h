//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYABILITYPANEL_H__
#define __PLAYABILITYPANEL_H__

//---------------------------------------------------------
//   The playability panel (View › Playability Panel): the built-in successor of the Playability
//   Checker plugin's dock. A table of the checker's problems (a red or dark yellow mark, Bar,
//   Staff, Reason; by bar, then staff); a click selects what a row is about and scrolls to it, a
//   selection in the score highlights its row. While a playable stop or a natural harmonic is
//   selected, its fingerboard replaces the table; while wind notes are selected, the register
//   graph of their bars (W1). The Selected line names the selected notes as written.
//   The layouts are libmscore/playabilitydiagram.h's display lists.
//---------------------------------------------------------

#include "libmscore/playability.h"
#include "libmscore/playabilitydiagram.h"

#include <QDockWidget>
#include <functional>

class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTableWidget;
class QToolButton;
class QHBoxLayout;

namespace Ms {

class Score;

//---------------------------------------------------------
//   DisplayListView: paints a display list; the list is made for the view's size
//---------------------------------------------------------

class DisplayListView : public QWidget {
      Q_OBJECT
   public:
      typedef std::function<DisplayList(double w, double h)> Layout;

   private:
      Layout _layout;
      DisplayList _items;
      int _wheel { 0 };

      void relayout();

   protected:
      void paintEvent(QPaintEvent*) override;
      void resizeEvent(QResizeEvent*) override;
      void wheelEvent(QWheelEvent*) override;

   signals:
      void relaidOut();
      void wheelStep(int);      // -1 back, +1 on (a whole notch)

   public:
      DisplayListView(QWidget* parent = nullptr);
      void setLayout(Layout l);
      const DisplayList& items() const { return _items; }
      bool wheelSteps { false };
      };

//---------------------------------------------------------
//   PlayabilityPanel
//---------------------------------------------------------

class PlayabilityPanel : public QDockWidget {
      Q_OBJECT

      Score* _score { nullptr };
      std::vector<PlayabilityRow> _rows;
      QLabel* _off;
      QStackedWidget* _stack;
      QTableWidget* _table;
      DisplayListView* _board;
      QWidget* _graphPage;
      QScrollArea* _chipArea;
      QWidget* _chips;
      QHBoxLayout* _chipLayout;
      QToolButton* _prev;
      QToolButton* _next;
      QLabel* _count;
      DisplayListView* _graph;
      QLabel* _selected;
      QPushButton* _toggle;

      ChordInfo _info;
      Playability::WindModel _wind;
      int _graphIndex { 0 };
      QString _windKey;
      QString _selKey;              // what is selected, to know when the selection changed
      bool _showList { false };
      bool _syncing { false };

      void rebuildTable();
      void fitReasonColumn();
      void syncRow();
      void showGraph(int i);
      void rebuildChips();
      void updatePage();
      void updateSelectedLine();

   private slots:
      void rowClicked(int row);
      void toggleList();

   protected:
      void resizeEvent(QResizeEvent*) override;
      void changeEvent(QEvent*) override;
      bool eventFilter(QObject*, QEvent*) override;

   public:
      PlayabilityPanel(QWidget* parent = nullptr);
      void setScore(Score* s);      // and refresh: rows, selection, diagrams
      void retranslate();
      };

}     // namespace Ms
#endif
