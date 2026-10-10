//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __NOTELANES_H__
#define __NOTELANES_H__

//---------------------------------------------------------
//   The Velocity and Join lanes (the owner, 2026-10-10: "what if the techniques and note velocities also had their
//   own "automation" graph"; "3 horizontal bands of different colors indicating which technique they fall under …
//   also used to tell apart dynamic ranges between ppp, pp, p, mp, mf"): two lanes of the automation editor
//   (automationlanes.h) with a stem per note instead of an envelope, first under a library part's header row.
//
//   Velocity (1-127): each library note's velocity as sent. A Performance legato note (performancetechnique.h) shows
//   its patch's technique bands behind it: the attacks' (smooth, spiccato, accented) or, joined by transition, the
//   transitions' (portamento, fingered, bowed), the map's bands; a note whose technique plays on velocity shows the
//   dynamics' bands: between each two of ppp … fff the velocity a plain note of it gets at those dynamics
//   (MidiRenderer::LibTrace::dynamicVelocities), below ppp, from fff. Its stem takes its band's colour. A note on a
//   technique with dynamics on the controller (the longs: CC1) has no bands: its velocity is no dynamic there.
//   Join (ms; Performance legato notes after a note of their patch only): how long the note before lasts into it (> 0)
//   or ends before it (< 0); a gap up to the map's legatoGap (SSO: 39 ms) still joins by transition, longer re-attacks:
//   the dashed line, the bands behind (transition / re-attack).
//   A stem dragged is the note's own value (Note::libraryVelocity / libraryJoin: filled), replacing the rendered one;
//   double-click, Delete or the menu's Reset to Auto give it back (empty: as rendered). An Inspector technique resets
//   them too (Note::undoChangeProperty). Live gets them as MuseScore plays them (the renderer's events: liveclips.h).
//
//   Editing, as the automation lanes (each gesture one undo step): drag a stem (the selected ones together; Shift:
//   fine); Ctrl-click: add to / remove from the selection; drag on the background: a rubber band selects; Draw Mode
//   (the header's pencil): every stem the mouse passes gets its value; right-click: Edit Value…, Reset to Auto, Hide
//   Lane.
//
//   What the lanes show comes from rendering the score as playback does (MidiRenderer with a LibTrace) when it
//   changed, a moment later (not while playing: Seq renders then), each note's first rendering (a repeat's second
//   time isn't shown).
//---------------------------------------------------------

#include <map>
#include <set>
#include <vector>
#include <QObject>
#include <QPointF>
#include <QString>

#include "automationlanes.h"
#include "libmscore/notelane.h"

class QPainter;
class QTimer;

namespace Ms {

class MasterScore;
class Note;
class Part;
class Score;
class ScoreView;


//---------------------------------------------------------
//   NoteLanes
//    the Velocity and Join lanes' drawing and editing; AutomationLanes hands their rows over
//---------------------------------------------------------

class NoteLanes : public QObject {
      Q_OBJECT

   public:
      using Row = AutomationLanes::Row;

      NoteLanes(AutomationLanes* lanes, ScoreView* view);

      void scoreChanged();          // another score, undo / redo: the selection goes, rendered again soon
      void changed();               // laid out again: rendered again soon
      // the lanes a part has: Velocity when it has library notes, Join when Performance legato ones
      std::vector<AutomationLanes::Target> targets(const Part* master) const;

      void paintLane(QPainter& p, const Row& r, const QRectF& visible) const;
      QString valueText(const Row& r, int tick) const;            // the header's second line
      QString title(const Row& r) const;
      QString hoverText(const Row& r, const QPointF& p) const;

      bool press(const Row& r, const QPointF& p, Qt::KeyboardModifiers mods, bool drawMode);
      bool dragging() const { return _drag != Drag::NONE; }
      void move(const QPointF& p, Qt::KeyboardModifiers mods);
      void release();
      void doubleClick(const Row& r, const QPointF& p);
      void contextMenu(const Row& r, const QPointF& p, const QPoint& globalPos);
      void resetSelected();
      void clearSelection();
      bool fresh() const;                     // rendered now if it may be (not while playing)

      // for tests: the marks as last rendered
      const std::map<const Part*, std::vector<NoteLane::Mark>>& currentMarks() const;

   private:
      enum class Drag : signed char { NONE, PENDING, MOVE, RUBBER, DRAW };
      struct Stem {
            const NoteLane::Mark* mark;
            QPointF head;
            double baseY;
            int value;
            bool own;
            };

      AutomationLanes* _lanes;
      ScoreView* _view;
      QTimer* _timer;
      mutable std::map<const Part*, std::vector<NoteLane::Mark>> _marks;
      mutable bool _valid { false };
      mutable bool _rendered { false };       // (_marks from this score)
      mutable const Score* _score { nullptr };  // the score _marks are from

      // the selection (one lane's) and the gesture
      QString _selTarget;
      std::set<const Note*> _sel;
      Drag _drag { Drag::NONE };
      Row _row;
      QPointF _pressPos;
      QPointF _last;
      int _range { 100 };                     // the Join lane's ms either way while dragging
      std::map<const Note*, int> _base;       // the dragged stems' values at the press
      std::map<const Note*, int> _work;       // the gesture's values

      void render() const;
      void renderLater();
      const std::vector<NoteLane::Mark>& partMarks(const Part* master) const;
      bool isJoin(const Row& r) const;
      int value(const NoteLane::Mark& m, bool join) const;
      bool own(const NoteLane::Mark& m, bool join) const;
      int joinOf(const NoteLane::Mark& m) const;
      int range(const Row& r) const;          // the Join lane's ms either way
      double yOf(const Row& r, double v, int range) const;
      double valueAtY(const Row& r, double y, int range) const;
      std::vector<Stem> stems(const Row& r) const;
      const NoteLane::Mark* stemAt(const Row& r, const QPointF& p) const;
      const NoteLane::Mark* markAt(const Row& r, int tick) const;
      void commit(const std::map<const Note*, int>& values, bool join, const QString& what);
      QString describe(const NoteLane::Mark& m, bool join, int v) const;
      };

}     // namespace Ms
#endif
