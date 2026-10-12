//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __BARRULER_H__
#define __BARRULER_H__

//---------------------------------------------------------
//   BarRuler: every bar's number in Continuous View (view only)
//
//   MuseScore 3's measure-number style numbers the first measure of each system, and Continuous View is one
//   system, so only bar 1 had a number there (the owner, 2026-10-12: "why isn't there a measure number for every
//   measure in continuous view?"). Like Live's bar ruler, a thin strip at the top of the score view, drawn by
//   ScoreView::paintEvent in Continuous View only, in widget coordinates: it scrolls sideways with the score and
//   stays at the top when the view scrolls up or down.
//
//   Nothing reaches the score: no style, no element, no undo step, nothing saved; Page View is unchanged.
//
//   - Each measure's number at its left barline, as the measure-number style would print it: Measure::no() + 1
//     (the pickup and other irregular measures, excluded from the count, get a tick but no number; "Add to
//     measure number" offsets are in no()). A multimeasure rest shows its range, "12-19".
//   - Too narrow for every number: every 2nd, 4th, 8th ... bar (1, 5, 9 ... at a step of 4), as Live thins its
//     ruler; a label that would still overlap the one before is left out.
//   - The Continuous panel (continuouspanel.h: the fixed clef / key / time signature strip at the left, which
//     shows the current measure's number itself) covers the ruler's left end: no labels under it.
//   - View > Bar Ruler (QSettings "ui/canvas/barRuler", default on) hides it.
//   - ScoreView repaints the strip after its bit-blit scrolls (scroll() would otherwise move the strip's pixels).
//   - The strip mustn't cover the music: ScoreView::pageTop puts the score's top under it in Continuous View (was
//     at 0), and ScoreView::setLogicalZoom keeps it there when the top was in view (zoomed out, the page's top
//     margin alone is thinner than the strip). Scrolling the score up can still put music under it.
//---------------------------------------------------------

class QPainter;

namespace Ms {

class ScoreView;
class ContinuousPanel;

namespace BarRuler {

bool enabled();
void setEnabled(bool on);
int height(const ScoreView* view);         // the strip's height in widget pixels (0 when off or not in Continuous View)
void paint(const ScoreView* view, QPainter& p, ContinuousPanel* panel);

}     // namespace BarRuler
}     // namespace Ms
#endif
