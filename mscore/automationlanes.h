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
//   In Continuous View, every part the sound library plays (and an Edit-in-MuseScore clip tab's part) always shows its
//   header row under its staves, "Automation · <part>" (the owner, 2026-10-12: "every instruments' automation rows get
//   shown at the same time, and I can expand or contract them at will"), as Live's track headers: a click on the header
//   (its "▸" / "▾" or name; not its buttons) opens or folds that part's lanes, several parts open at once; Alt-click
//   every part's, to the clicked one's new state. Folded: the header row only. Which parts are open: lanefolds.h (per
//   score, for the session, never saved; all folded at first; the score's selection doesn't change it). Open: one lane
//   per controller under the header, on the score's own time axis (each tick where its notes are). The notes stay
//   editable as always. Room for the rows is made by the layout (Score::setAutomationSpace: the view's state, never
//   saved; System::layout2), checked after every layout (queueSpace: a part that comes to play the library gets its
//   header). A lane's focus (its selected points, Delete, Ctrl+C …) is the lane last clicked, whichever part.
//   What a lane shows and edits: libmscore/automation.h (Automation::Lane, Automation::Edit), stored in
//   the score's metaTag "automation", one undoable step per gesture.
//
//   Lanes (Automation::Lane::target): Dynamics (the library's dynamics CC: it takes the notation's place
//   from its first point) and Expression (CC11), then every controller of the part's patches (the map's:
//   SSO's named controls, Vibrato, Release, Tightness, Mic 1-5 … as Kontakt parameters or CCs). The lanes are the only
//   way to set a controller (since 2026-10-15: the Sound Library window's Controllers… went; a flat lane is the
//   controller's setting, partcontrollers.h); an edit is heard at once, also while playing (the parameters' settings on
//   the instances, SoundLibraryHost::applyPartSettings; the rest by Seq::renderAgainPlaying).
//   Not only the sound library's parts (the owner, 2026-10-02: "the automation display wouldn't only work for sso, it
//   works for any midi clip"): an Edit-in-MuseScore clip tab's part gets its Live track's parameters (the mixer's
//   volume and pan, every device's: a synth rack's macros, Sampler's, Operator's …; "live:<d>/<p>", read from Live by
//   the MuseScore Link device), whatever instrument the track has; those lanes are the clip's own envelopes in Live
//   (liveclipedit.h). A part Live plays (Live plays the score) gets its track's Live parameters after the map's
//   ("Live: Operator › Tone"); the device plays them in Live (liveclips.h), MuseScore's own playback can't. The "+"
//   menu has a submenu per Live device. Shown: the
//   lanes with points and those added with "+" (an empty lane is hidden until then), all of them with
//   "All"; "×" hides a lane (its points still play). "+" and "All" open a folded part.
//   The header row's controls, right-aligned (the owner, 2026-10-12: "the + and All buttons should be part of a same
//   rounded rectangle, with a line separating them … the pencil icon should be in a round button, with a different
//   color"): ( + | All ) one pill, both choosing which lanes are shown; (✎) Draw Mode, round, in the palette's highlight
//   colour (filled while on); [Grid 1/4 ▾] the snap grid's value ("Off", "Bar", "1/8" …), its menu choosing it (radio)
//   and "Even beat spacing": Continuous View laid out with every beat the same width (Score::lineEvenBeats,
//   layoutlinear.cpp; the view only: not saved, not an undo step; the lanes follow the notes, so their grid is even
//   too). Each control has a tooltip (hover).
//
//   Editing, as in Live 12 (manual 25.5.1-5; the owner, 2026-10-06: "mimic the behavior of how you edit automation
//   curves in ableton"):
//     the grid: fixed, one step per beat of the time signature at first (the owner, 2026-10-07: no adaptive grid);
//       Ctrl+1 narrower (halves it, down to 1/64), Ctrl+2 wider (up to the beat, then the bar), Ctrl+4 snap on / off
//       (Live's keys for its editing grid; while a lane has the focus); the header's Grid button shows the value
//       ("Off": no snap), its menu sets it; the lanes draw a line at each step;
//     a lane without points (the owner, 2026-10-15: "if something has no automated value yet, I want it to show a
//       dashed line like Ableton does … just make it like ableton"): a dashed line at the value the controller plays
//       unautomated (libmscore/partcontrollers.h unautomatedValue: the map's default, the patch's measured mic
//       faders, else its setup's own value as last loaded here; not known: no line), in the lane's colour, its value
//       on hover and in the header; before a lane's first point the same dashed line. Click on it: a point at that
//       value; drag it up or down: the value from the score's start (one point at tick 0: a flat lane, the
//       controller's setting); the last point deleted: the dashed line again (the lane stays shown);
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
//       parameter too, as Live allows), else after the copied ones; Ctrl+D: duplicated after themselves (without a
//       time selection, below);
//     right-click: Edit Value…, Delete, Step / Linear, Straight, Simplify Envelope (the selected points' span; none
//       selected: the lane), Insert Shape ▸ sine, triangle, sawtooth, inverse, square (one cycle over the selected
//       points' span, else the grid cell; the full range), Clear Lane, Hide Lane.
//   Unlike Live: a click on the background adds a breakpoint (Live: a double-click off the line), a click on a
//   breakpoint selects it (Live: deletes it; here a double-click), a background drag selects points (Live: a time
//   selection); no stretch / skew handles on a selection (manual 25.5.3).
//   Values are shown 0-127 (the map's controller scale; SSO's controls are 0-127); a clip tab's Velocity lane
//   (liveclipmodel.h, offered first, always) in % (scale) or 1-127 (absolute), its right-click menu also choosing scale /
//   absolute and "shape while playing" / "write into the notes" (asks first).
//   Time selection (Live's; the owner, 2026-10-12: "select all the automations in a bar, like I'm able to do in ableton,
//   and just spam Ctrl+D to copy it over bars"): the score's range selection (click a measure, Shift-click to extend) on
//   one part with a header row (open or folded: whichever part it is on), rounded out to whole bars, is shown as a light
//   band over all of the part's rows.
//   Ctrl+D (with the focus in the score or in a lane) duplicates everything of the part's lanes in it to the bars after
//   it, one undo step (Automation::duplicateTimeSelection): every lane of the part, shown or not (the target bars' points
//   replaced, the envelope around them as before), and each note's attack, transition, velocity and join onto the note
//   at the same place (PerformanceTechnique::duplicateNoteValues); then the bars after are selected, so Ctrl+D again
//   goes on, up to the score's end (no measures added). MuseScore 3 has no Ctrl+D shortcut (shortcuts.xml); the lanes
//   take it (ShortcutOverride) only while there is a time selection or a lane has the focus. Points selected in a lane
//   since the score's selection last changed take over: Ctrl+D then duplicates those (as before).
//   Not here: a lane's rubber band as a time selection (it selects points).
//   Lane heights (the owner, 2026-10-28; laneheights.h has the gestures, which are Live's and which ours): every lane
//   under the header row has its own height; drag the lower edge of its header box (Alt: every lane), Alt + "+" / "-"
//   on the focused lane, double-click the edge or the menu's Reset Lane Height / Reset All Lane Heights for the default.
//   computeRows stacks the rows by them and updateSpace gives the layout their sum, so the score below moves with them.
//   Under a library part's header row, first: the Velocity and Join lanes, a stem per note (notelanes.h); there a
//   background drag draws its rubber band as the lanes do, and Ctrl+D with stems selected copies their values onto the
//   notes at the same places right after them (the span rounded out to the grid; NoteLanes::duplicateSelected; a time
//   selection on the part wins); Ctrl+C / V copy and paste their values (NoteLanes::copySelected / pasteCopied; target: the mouse's time over the lane, else the first selected stem, else after the copied ones; a Velocity clip never into Join); X stays the automation lanes' only.
//   A point on a bar's tick is drawn and hit on the bar line, where the grid draws the bar (Automation::pointX; the owner,
//   2026-10-15: "points on automation lane don't always snap to grid correctly"); the stems stay under their notes.
//   A lane Live's set has as it is (Automation::Lane::playedByLive) is marked "Live"; editing it here makes
//   MuseScore's the newer one (automation.h).
//---------------------------------------------------------

#include <functional>
#include <map>
#include <set>
#include <vector>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include "libmscore/automation.h"
#include "libmscore/soundlibrary.h"
#include "lanefolds.h"
#include "laneheights.h"

class QAction;
class QColor;
class QMenu;
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

      // the view's score changed (another score, undo / redo, a reload): selection and caches dropped (not the folding)
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
      double pointX(int tick) const;          // a lane point's x: on a bar's tick the bar line (Automation::pointX)
      void unfold(const Part* part, bool on = true);          // open / fold a part's lanes (lanefolds.h)
      bool isOpen(const Part* part) const;
      std::vector<const Part*> headerParts() const;           // the parts with a header row, in score order
      void showLane(const Part* part, const QString& target, bool on = true);
      void setDrawMode(bool on) { _drawMode = on; }
      // a lane's height (laneheights.h; spatium), per score and view part, for the session; set (all: every lane of the
      // score) / reset lay the lanes out again
      double laneHeight(const Part* part, const QString& target) const;
      void setLaneHeight(const Part* part, const QString& target, double sp, bool all = false);
      void resetLaneHeight(const Part* part, const QString& target);
      void resetAllLaneHeights();
      // the lane whose header box's lower edge is at pixel (viewport; the resize grip), else nullptr
      const Row* resizeEdgeAt(const QPoint& pixel) const;
      // a lane's line as paintLane draws it (also for the tests' pictures): rect the lane, valueRect its values (0 at
      // the bottom, 1 at the top), unautomated the controller's value without a point (partcontrollers.h; -1: not
      // known), px a screen pixel, selected the selected points' indices
      static void paintEnvelope(QPainter& p, const QRectF& rect, const QRectF& valueRect, const Automation::Lane& l,
                                double unautomated, const std::function<double(int)>& tickToX, double px, const QColor& ink,
                                const std::vector<int>& selected, const QRectF& visible);
      // what a lane's controller plays without a point, 0-1 (Live's dashed line; -1: not known, or not a map controller)
      double unautomatedValue(const Row& r) const;

      // the time selection (above): whole bars [start, end) of a view part, from the score's range selection
      struct TimeSelection {
            const Part* part { nullptr };       // the view score's
            int start { -1 };
            int end { -1 };
            int staffStart { -1 };              // the score's selection's staves
            int staffEnd { -1 };
            bool valid() const { return part && end > start; }
            };
      TimeSelection timeSelection() const;
      // Ctrl+D over it (the bars after selected); false: no time selection
      bool duplicateTimeSelection();

   private:
      friend class NoteLanes;       // (the Velocity and Join lanes: notelanes.h)
      enum class Drag : signed char { NONE, PENDING, MOVE, CURVE, RUBBER, DRAW, FREE, SEGMENT, RESIZE };

      ScoreView* _view;
      NoteLanes* _notes;
      LaneFolds _folds;                                        // view parts whose lanes are open, per score
      LaneHeights _heights;                                    // the lanes' heights, per score and view part
      // a lane's height being dragged (Drag::RESIZE): the lane, its height at the press; the resize cursor shown
      const Part* _resizePart { nullptr };
      QString _resizeTarget;
      double _resizeStart { 0 };
      bool _resizeCursor { false };
      void setResizeCursor(bool on);
      bool _spaceQueued { false };                             // updateSpace after the layout (queueSpace)
      std::map<const Part*, std::set<QString>> _added;         // master part -> empty lanes shown
      std::map<const Part*, std::set<QString>> _hidden;
      std::set<const Part*> _showAll;
      bool _drawMode { false };
      int _gridLevel { 0 };                                    // 0: the beat; -n: the beat / 2^n; 1: the bar
      bool _timeOff { false };                                 // points selected since the score's selection changed
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
      double _dashedValue { -1 };             // pressed on an empty lane's dashed line: its value (else -1)
      bool _valueOnly { false };              // the drag moves the value only (the dashed line dragged)
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
      mutable std::vector<SoundLib::Route> _routes;            // the library's routes, once for all parts
      mutable bool _routesValid { false };
      void clearTargets() const;                               // (the targets and the routes)
      const std::vector<SoundLib::Route>& libraryRoutes() const;
      bool hasHeader(const Part* part) const;                  // a part with lanes (without rendering its notes)
      void queueSpace();
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
      double barX(int tick) const;                                           // a bar's start (its bar line)
      int gridTicks(int tick) const;
      int gridTicksAt(int tick, int level) const;              // (gridTicks at another level: the Grid menu)
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
      enum class Control : signed char { NONE, ADD, ALL, DRAW, GRID };      // the header row's controls
      QRectF controlRect(const QRectF& header, Control c) const;             // viewport
      Control controlAt(const QRectF& header, const QPointF& pixel) const;
      double controlsWidth() const;                                          // from the first control to the right edge
      QString controlTip(Control c) const;
      void gridMenu(const QPoint& globalPos);                                // snap choices, Even beat spacing
      void paintControls(QPainter& p, const Row& r, const QRectF& h) const;
      bool _tipShown { false };
      QString headLabel(const Row& r) const;
      QString valueText(const QString& target, const Part* master, double v) const;     // (0-127; the Velocity lane: % or 1-127)
      // the Engine lane (enginelane.h): the engines it offers, a part's own, an engine's name as shown
      mutable QStringList _engines;
      mutable bool _enginesValid { false };
      QStringList engineChoices() const;
      QString ownEngine(const Part* master) const;
      static QString engineName(const QString& engine);
      bool engineMenu(const Row& r, const QPointF& p, const QPoint& globalPos);
      void select(const Row& r, const std::vector<int>& sel);
      void dropFocus();
      void addLaneMenu(const Row& r, const QPoint& globalPos);
      // a lane's right-click menu: Reset Lane Height, Reset All Lane Heights (each only when there is one to reset)
      struct HeightActions {
            QAction* reset { nullptr };
            QAction* resetAll { nullptr };
            };
      HeightActions addHeightActions(QMenu& menu, const Row& r) const;
      bool heightAction(const QAction* a, const HeightActions& h, const Row& r);      // true: it was one of them
      };

}     // namespace Ms
#endif
