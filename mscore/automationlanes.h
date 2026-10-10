//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __AUTOMATIONLANES_H__
#define __AUTOMATIONLANES_H__

//---------------------------------------------------------
//   The automation editor (the owner, 2026-09-30: "when you select a MIDI track, MuseScore lets you edit
//   the notes AND show you automation tracks for every possible parameter in SSO, in which you can draw
//   automation curves just like you can in Ableton").
//
//   In Continuous View, a part the sound library plays shows its automation lanes under its staves when
//   it is selected (any element of it): a header row, then one lane per controller, on the score's own
//   time axis (each tick where its notes are). The notes stay editable as always. Room for them is made
//   by the layout (Score::setAutomationSpace: the view's state, never saved; System::layout2).
//   What a lane shows and edits: libmscore/automation.h (Automation::Lane, Automation::Edit), stored in
//   the score's metaTag "automation", one undoable step per gesture.
//
//   Lanes (Automation::Lane::target): Dynamics (the library's dynamics CC: it takes the notation's place
//   from its first point) and Expression (CC11), then every controller of the part's patches (the map's:
//   SSO's named controls, Vibrato, Release, Tightness, Mic 1-5 … as Kontakt parameters or CCs).
//   Not only the sound library's parts (the owner, 2026-10-02: "the automation display wouldn't only work for sso, it
//   works for any midi clip"): an Edit-in-MuseScore clip tab's part gets its Live track's parameters (the mixer's
//   volume and pan, every device's: a synth rack's macros, Sampler's, Operator's …; "live:<d>/<p>", read from Live by
//   the MuseScore Link device), whatever instrument the track has; those lanes are the clip's own envelopes in Live
//   (liveclipedit.h). A part Live plays (Live plays the score) gets its track's Live parameters after the map's
//   ("Live: Operator › Tone"); the device plays them in Live (liveclips.h), MuseScore's own playback can't. The "+"
//   menu has a submenu per Live device. Shown: the
//   lanes with points and those added with "+" (an empty lane is hidden until then), all of them with
//   "All"; "×" hides a lane (its points still play). The header's "▾" folds the part's lanes ("▸" shows them again).
//   "Even" lays Continuous View out with every beat the same width (Score::lineEvenBeats, layoutlinear.cpp; the view
//   only: not saved, not an undo step); the lanes follow the notes, so their grid is even too.
//
//   Editing, as in Live 12 (manual 25.5.1-5; the owner, 2026-10-06: "mimic the behavior of how you edit automation
//   curves in ableton"):
//     the grid: fixed, one step per beat of the time signature at first (the owner, 2026-10-07: no adaptive grid);
//       Ctrl+1 narrower (halves it, down to 1/64), Ctrl+2 wider (up to the beat, then the bar), Ctrl+4 snap on / off
//       (Live's keys for its editing grid; while a lane has the focus); the header's grid button shows the value
//       ("Off": no snap) and turns snap on / off; the lanes draw a line at each step;
//     click: a breakpoint (on the envelope's line: on it; elsewhere: at the mouse's value), snapped to the
//       grid; Alt: no snap;
//     drag a breakpoint: moves it (and the other selected ones); points passed over are removed; Shift: fine
//       vertical, time kept; Ctrl-click: add to / remove from the selection; drag on the background: a
//       rubber band selects;
//     drag the line (pressed within 6 px of it): the segment moves, both its points (both selected: the whole
//       selection); Shift: one axis, the one moved further; Alt: no snap;
//     double-click a breakpoint, or Delete / Backspace: removes it (them);
//     Alt-drag a segment: curves it (Live's curve: a cubic Bézier, the same control points Live keeps);
//       Alt-double-click: straight again;
//     Draw Mode (the header's pencil): dragging draws steps as wide as the grid; with Alt the line follows the
//       mouse, as breakpoints (straight or curved) within one MIDI step of the path (Automation::Edit::drawFree);
//     Ctrl+C / Ctrl+X: the selected points; Ctrl+V: at the mouse's time in the lane under it (another
//       parameter too, as Live allows), else after the copied ones; Ctrl+D: duplicated after themselves;
//     right-click: Edit Value…, Delete, Step / Linear, Straight, Simplify Envelope (the selected points' span; none
//       selected: the lane), Insert Shape ▸ sine, triangle, sawtooth, inverse, square (one cycle over the selected
//       points' span, else the grid cell; the full range), Clear Lane, Hide Lane.
//   Unlike Live: a click on the background adds a breakpoint (Live: a double-click off the line), a click on a
//   breakpoint selects it (Live: deletes it; here a double-click), a background drag selects points (Live: a time
//   selection); no stretch / skew handles on a selection (manual 25.5.3).
//   Values are shown 0-127 (the map's controller scale; SSO's controls are 0-127); a clip tab's Velocity lane
//   (liveclipmodel.h, offered first, always) in % (scale) or 1-127 (absolute), its right-click menu also choosing scale /
//   absolute and "shape while playing" / "write into the notes" (asks first).
//   Under a library part's header row, first: the Velocity and Join lanes, a stem per note (notelanes.h).
//   A lane Live's set has as it is (Automation::Lane::playedByLive) is marked "Live"; editing it here makes
//   MuseScore's the newer one (automation.h).
//---------------------------------------------------------

#include <map>
#include <set>
#include <vector>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "libmscore/automation.h"

class QContextMenuEvent;
class QKeyEvent;
class QMouseEvent;
class QPainter;

namespace Ms {

class Measure;
class NoteLanes;
class Part;
class Score;
class ScoreView;

class AutomationLanes : public QObject {
      Q_OBJECT

   public:
      struct Target {
            QString id;             // Automation::Lane::target
            QString name;
            bool param { false };   // a plug-in parameter (else a MIDI controller)
            bool live { false };    // a parameter of the Live track ("live:<d>/<p>", liveclipmodel.h)
            };
      struct Row {
            const Part* part { nullptr };       // the view score's
            const Part* master { nullptr };     // the master score's (the lanes are stored by it)
            QString target;                     // empty: the part's header row
            QString name;
            bool param { false };
            QRectF rect;                        // canvas
            double dataX { 0 };                 // canvas: where bar 1 starts (the staff lines)
            };

      AutomationLanes(ScoreView* view);

      static bool enabled();
      static void setEnabled(bool on);

      // the view's score changed (another score, undo / redo, a reload): selection and caches dropped
      void scoreChanged();
      void selectionChanged();
      void layoutChanged();
      // room for the shown lanes: Score::setAutomationSpace, laid out again when it changed
      void updateSpace();

      void paint(QPainter& p, const QRect& viewport);

      bool mousePress(QMouseEvent* ev);
      bool mouseMove(QMouseEvent* ev);
      bool mouseRelease(QMouseEvent* ev);
      bool mouseDoubleClick(QMouseEvent* ev);
      bool contextMenu(const QPoint& pos, const QPoint& globalPos);
      bool wantsKey(const QKeyEvent* ev) const;
      bool keyPress(QKeyEvent* ev);
      bool hasFocus() const { return _focus; }
      void hover(const QPoint& pos);

      // for tests and the GUI checks: the rows as last laid out, the tick and x of a canvas position
      std::vector<Row> rows() const;
      std::vector<Target> targets(const Part* part) const;
      double tickToX(int tick) const;
      int xToTick(double x) const;
      void unfold(const Part* part, bool on = true);
      void showLane(const Part* part, const QString& target, bool on = true);
      void setDrawMode(bool on) { _drawMode = on; }

   private:
      friend class NoteLanes;       // (the Velocity and Join lanes: notelanes.h)
      enum class Drag : signed char { NONE, PENDING, MOVE, CURVE, RUBBER, DRAW, FREE, SEGMENT };

      ScoreView* _view;
      NoteLanes* _notes;
      std::set<const Part*> _unfolded;                         // view parts
      std::set<const Part*> _folded;                           // view parts: the header row only ("▸")
      const Part* _lastSelected { nullptr };
      std::map<const Part*, std::set<QString>> _added;         // master part -> empty lanes shown
      std::map<const Part*, std::set<QString>> _hidden;
      std::set<const Part*> _showAll;
      bool _drawMode { false };
      int _gridLevel { 0 };                                    // 0: the beat; -n: the beat / 2^n; 1: the bar
      bool _snapOn { true };

      // the lanes: as stored (cached by the metaTag's text), or the working copy during a gesture
      mutable QString _cachedTag;
      mutable std::map<const Part*, Automation::PartLanes> _cached;
      std::map<const Part*, Automation::PartLanes> _work;
      bool _working { false };

      // the selection: points of one lane
      bool _focus { false };
      const Part* _selPart { nullptr };       // master
      QString _selTarget;
      std::vector<int> _sel;

      // the gesture
      Drag _drag { Drag::NONE };
      Row _dragRow;
      QPointF _pressPos;                      // canvas
      QPoint _pressPixel;
      int _grab { -1 };                       // the point pressed
      int _segment { -1 };                    // the segment curved
      double _k0 { 0 };
      Automation::Lane _before;               // the lane at the press
      std::vector<int> _beforeSel;
      QRectF _rubber;
      int _drawCell { -1 };
      bool _nearLine { false };
      int _lineSegment { -1 };                // the segment under the press (on its line: dragged)
      std::vector<int> _segMoved;             // what a segment drag moves: its two points, or the selection
      std::map<int, double> _freePath;        // Draw Mode with Alt: the mouse's path, tick -> value
      int _freeLast { -1 };
      QPointF _hover { -1, -1 };              // canvas
      QString _hoverText;
      // a point added by a click, kept by the double-click that follows it
      int _lastAddedTick { -1 };
      qint64 _lastAddedAt { 0 };

      static std::vector<Automation::Point> _clipboard;

      // the tick <-> x anchors of the laid out system (canvas)
      mutable std::vector<Row> _rows;
      mutable bool _rowsValid { false };
      mutable std::map<const Part*, std::vector<Target>> _targets;
      mutable int _targetsGeneration { -1 };
      std::vector<Row> computeRows() const;
      std::vector<Target> computeTargets(const Part* part) const;
      std::vector<Target> libraryTargets(const Part* part) const;      // the sound library's controls (SSO's map)
      mutable std::vector<std::pair<int, double>> _anchors;
      mutable bool _anchorsValid { false };
      void buildAnchors() const;

      Score* score() const;
      const std::map<const Part*, Automation::PartLanes>& lanes() const;
      Automation::Lane lane(const Part* master, const QString& target) const;
      void setLane(const Part* master, const Automation::Lane& lane);       // the working copy
      void commit(const QString& what);
      std::vector<QString> shownTargets(const Part* master, const std::vector<Target>& all) const;
      double pixel() const;                                                  // a screen pixel in canvas units
      int gridTicks(int tick) const;
      int snap(int tick, bool fine) const;
      void changeGrid(int by);                                 // Ctrl+1 (-1), Ctrl+2 (+1)
      void toggleSnap();                                       // Ctrl+4
      QString gridLabel() const;                               // "1/16", "Bar", "Off"
      std::pair<int, int> gridCell(int tick, bool fine) const;              // the grid cell at tick (in its measure)
      double valueAtY(const Row& r, double y) const;
      double yOfValue(const Row& r, double v) const;
      QRectF valueRect(const Row& r) const;
      const Row* rowAt(const QPointF& canvas) const;
      int pointAt(const Row& r, const Automation::Lane& l, const QPointF& p) const;
      int segmentAt(const Automation::Lane& l, int tick) const;
      bool headerClick(const QPoint& pixel);
      void paintLane(QPainter& p, const Row& r, const QRectF& visible) const;
      void paintGrid(QPainter& p, const Row& r, const QRectF& visible) const;   // bars, beats, the grid's steps
      void paintHeader(QPainter& p, const Row& r) const;
      QRectF headerRect(const Row& r) const;                                 // viewport
      QColor rowColor(const Row& r) const;                                   // a row's band
      double textPx() const;                                                 // the headers' text size on screen
      QRectF buttonRect(const QRectF& header, int i) const;                  // the header row's +, All, pencil, Even
      QString headLabel(const Row& r) const;
      QString valueText(const QString& target, double v) const;     // (0-127; the Velocity lane: % or 1-127)
      void select(const Row& r, const std::vector<int>& sel);
      void dropFocus();
      void addLaneMenu(const Row& r, const QPoint& globalPos);
      };

}     // namespace Ms
#endif
