//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __AUTOMATION_H__
#define __AUTOMATION_H__

//---------------------------------------------------------
//   Automation: a part's controllers changing over time (the owner, 2026-09-28: "start building a
//   full automation system … just build the infrastructure", after the SSO extraction named every
//   patch's automatable controls: Dynamics, Expression, Vibrato, Release, Tightness, Mic 1-5,
//   Mic Mix Distance, Variation, Reverb …).
//
//   A lane: a target and its points. The target is a sound library controller's id
//   (SoundLib::Controller::id: "vibrato", "mic1" …: whatever the library maps it to, a MIDI
//   controller or a plug-in parameter by title) or "cc<n>" (a MIDI controller as it is). A point:
//   a tick (of the score, not unrolled), a value 0-1, and how it goes on to the next point
//   (step: stays until it; linear: a ramp to it, straight or curved). Before the first point the
//   lane says nothing (the controller's own value, as without a lane); after the last it stays.
//   Two points at one tick are a jump (as in Live): the later one holds from that tick.
//
//   Curves (2026-09-30, the automation editor): a ramp may be curved as Live curves a segment
//   (Alt-drag): a cubic Bézier from the point to the next one, its two control points given in
//   the segment's box (x: time, y: value, each 0-1 from this point to the next), exactly Live's
//   CurveControl1X/1Y/2X/2Y (libmscore/liveset.h reads them, livesetwriter.h writes them), so a
//   curve drawn in MuseScore is Live's curve and back. Straight: both control points on the
//   diagonal (Live's own test). The editor's Alt-drag sets one number, the curvature k (-1 … 1):
//   the quadratic Bézier through (0.5 - k/2, 0.5 + k/2) raised to a cubic (curvePoint).
//
//   Kept in the score as the metaTag "automation" (MuseScore 3.6 keeps metaTags through a round
//   trip), JSON:
//     [{"part": index, "name": part name,
//       "lanes": [{"target": "vibrato", "points": [[tick, value, "step" | "linear" [, [c1x, c1y, c2x, c2y]]], …]}, …]}]
//   (the control points only on a curved ramp: a lane without curves is written as before).
//   Parts found as partplayback.h finds them; parts of an excerpt follow their master's part.
//   A lane may carry more keys, kept as they are (Lane::extra):
//     "source": "live": imported from an Ableton Live Set (libmscore/liveset.h, LIVE.md) with "set"
//       (the .als path), "setTime" (its modification time), "track", "param" / "paramId" (the
//       plug-in parameter as Live names it) or "clipCC".
//     "liveHash", "pointsHash": the lane as Live's set has it (Live's events, hashed: LiveSet::eventsHash)
//       and the points it gave here (pointsHash). Both written when a set is imported or written by
//       Create Live Set (the lane is then in Live's track automation and Live plays it); a lane edited
//       here since has other points (playedByLive() false: MuseScore's version is newer and the MuseScore
//       Link device plays it in Live, over Live's own automation). A set imported again: a lane whose
//       Live events changed since (another liveHash) takes Live's (Live's edit is newer); else MuseScore's
//       stays (Automation::merge).
//     Until 2026-09-30 a "live" lane was read-only here; the owner (2026-09-30): "why not make it so that
//     you can edit the automation curves in both and they sync up with each other?". Every lane is
//     editable now.
//     When the sound library plays through MIDI output (to Live), a lane Live plays (playedByLive)
//     sends nothing (Live plays its own automation) but still holds its controller's place (no part
//     value or staff text is sent for it); with the hosted plug-in it plays like any lane, so MuseScore
//     alone sounds as Live does.
//   Played (rendermidi, the sound library's parts): a lane takes the place of its controller's
//   part value and staff texts; a MIDI controller's lane as controller events, a plug-in
//   parameter's as parameter events to the hosted plug-in (Vst3Synth) and, when Live plays the
//   score, to the MuseScore Link device (liveclips.h: /ms/params), which sets Kontakt's parameter.
//---------------------------------------------------------

#include <map>
#include <vector>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace Ms {

class MasterScore;
class Measure;
class Part;
class Score;

namespace Automation {

extern const char* const metaTag;

enum class Curve : signed char { STEP, LINEAR };

struct Point {
      int tick { 0 };
      double value { 0 };                 // 0-1
      Curve curve { Curve::STEP };        // to the next point
      // a LINEAR ramp's shape: Live's Bézier control points in the segment's box (see above); on the
      // diagonal: straight
      double c1x { 1.0 / 3 }, c1y { 1.0 / 3 }, c2x { 2.0 / 3 }, c2y { 2.0 / 3 };
      Point() {}
      Point(int t, double v, Curve c) : tick(t), value(v), curve(c) {}
      bool operator<(const Point& o) const { return tick < o.tick; }
      bool curved() const;                // a LINEAR ramp off the straight line
      void straighten() { c1x = c1y = 1.0 / 3; c2x = c2y = 2.0 / 3; }
      };

// the editor's curvature k (-1 … 1; 0 straight; > 0 rises early, bowed towards the next value) as
// control points, and back (from any control points: the nearest k)
void setCurvature(Point& p, double k);
double curvature(const Point& p);
// a Bézier segment in its box: the y (0-1 of the way to the next value) at time x (0-1)
double curveAt(double c1x, double c1y, double c2x, double c2y, double x);
// the curve as straight pieces, the points after (0, 0) up to and including (1, 1), so that the line through
// them is nowhere further than tolY (box units) from the curve at the same x: de Casteljau halving until the
// control points are within tolY of their piece's chord (a Bézier lies within its control points' hull, so
// its distance from the chord does too). Used with tolY = one MIDI step (1/127 of the parameter's range) over
// the segment's rise: Live's clip envelopes and a Live Set's curves read here
std::vector<std::pair<double, double>> flattenCurve(double c1x, double c1y, double c2x, double c2y, double tolY);

// the resolution a lane's value is sent at (Lane::events): a MIDI controller's (MIDI 1.0: 7-bit data, 0-127, so
// 1/127 of the range a step); a plug-in parameter's: the lane's own (its points are kept to 1e-4: pointJson;
// a VST 3 parameter is a double, Kontakt's automation slots give no step count MuseScore reads)
constexpr double CC_RESOLUTION = 1.0 / 127;
constexpr double PARAM_RESOLUTION = 1e-4;

// the time axis (canvas x) of a score laid out in Continuous View, for the lanes (mscore/automationlanes.h): tick -> x
// anchors (a note's tick: the middle of its note heads; a measure's end: its bar line), x at a tick, the middle
// of a measure's end bar line (where the lanes' grid draws the bar)
std::vector<std::pair<int, double>> timeAxis(Score* score);
double xAtTick(const std::vector<std::pair<int, double>>& anchors, int tick);
double barLineX(const Measure* m);

extern const char* const SOURCE_LIVE;     // "live"

struct Lane {
      QString target;                     // a controller id, or "cc<n>"
      std::vector<Point> points;          // by tick
      QJsonObject extra;                  // the lane's other keys ("source" …), written back as read
      QString source() const;             // "": MuseScore's own; "live": from a Live Set
      // the lane as Live's set has it (see above): Live plays it, MuseScore sends nothing for it to Live
      bool playedByLive() const;
      int cc() const;                     // "cc<n>": n (0-127); else -1
      // the value at tick: -1 before the first point
      double valueAt(int tick) const;
      // what to send in [tick1, tick2): the value in force at tick1 (when there is one), then each
      // point, and along a ramp each tick at which the value, counted in steps of resolution (rounded), changes:
      // a MIDI controller gets every one of its 128 values a ramp passes, at the tick it reaches it (the
      // owner's criterion, 2026-10-03: a step whenever the value changes by one MIDI step), no fixed step
      std::vector<std::pair<int, double>> events(int tick1, int tick2, double resolution) const;
      };

// a hash of points (ticks, values to 1e-4 as written, curves), for pointsHash
QString pointsHash(const std::vector<Point>& points);

// The Velocity lane of an Edit-in-MuseScore clip tab (mscore/liveclipmodel.h › The Velocity lane): target "velocity";
// "velocityMode" "absolute" in its extra: a note's velocity is round(127 u), else (scale) v × 2u, both 1-127; before
// its first point (u < 0) a note keeps its own. MuseScore 3's playback plays every note so (rendermidi, playNote)
extern const char* const VELOCITY_TARGET;
bool velocityAbsolute(const Lane& lane);
int shapeVelocity(int v, double u, bool absolute);

using PartLanes = std::vector<Lane>;

std::map<const Part*, PartLanes> read(const MasterScore* score);
QString write(const MasterScore* score, const std::map<const Part*, PartLanes>& lanes);    // empty: none
// the lanes stored as one undoable step (ChangeMetaTags, its own command); false: nothing changed
bool undoWrite(MasterScore* score, const std::map<const Part*, PartLanes>& lanes);
// a part's lanes (its master part's)
PartLanes lanes(const Part* part, const std::map<const Part*, PartLanes>& all);
// all lanes with the given source replaced by `with` (by part; lanes of other sources kept)
std::map<const Part*, PartLanes> replaceSource(const std::map<const Part*, PartLanes>& all, const QString& source,
                                               const std::map<const Part*, PartLanes>& with);
// a Live Set imported again (`with`: its lanes, each with liveHash / pointsHash) over the score's lanes, per part and
// target: Live's where Live's events changed since the score's lane came from Live and it is unedited here, or the
// two are the same; the score's where Live's are as they were (an edit here is newer); a lane that came from Live and
// is no longer in the set: removed if unedited here, else kept as MuseScore's own. Both changed since they last
// agreed (or they never agreed and differ): a conflict (the owner, 2026-10-01: "whenever there's conflict, it asks
// the user to preserve one"), resolved by `choices` (none given: MuseScore's kept, now against Live's latest, so it
// is asked again only when Live's changes again). `report`: a line per conflict and what was kept
struct Conflict {
      const Part* part { nullptr };
      QString target;
      Lane mine;                          // the score's
      Lane live;                          // Live's, as imported
      };
enum class Keep : signed char { MUSESCORE, LIVE };
std::vector<Conflict> conflicts(const std::map<const Part*, PartLanes>& all, const std::map<const Part*, PartLanes>& with);
std::map<const Part*, PartLanes> merge(const std::map<const Part*, PartLanes>& all, const std::map<const Part*, PartLanes>& with,
                                       const std::map<std::pair<const Part*, QString>, Keep>& choices = {},
                                       QStringList* report = nullptr);

//---------------------------------------------------------
//   Editing (the automation editor, mscore/automationlanes.h): each a pure change of a lane's points,
//   the editor stores the result as one undoable step. Indices are into lane.points (sorted by tick).
//---------------------------------------------------------

namespace Edit {

// a breakpoint at tick: on a ramp it keeps the ramp's shape (a curve split in two), on a step a step;
// a point already at that tick: its value set. Returns its index
int addPoint(Lane& lane, int tick, double value);
// a breakpoint where the envelope already is (value = valueAt; before the first point: the first's)
int addPointOnLine(Lane& lane, int tick);
void removePoints(Lane& lane, const std::vector<int>& indices);
// move the points by dtick (not before 0) and dvalue (clamped 0-1); points the move passes over are
// removed (Live: "remove a neighboring breakpoint by continuing to drag … over it"). Returns the moved
// points' new indices
std::vector<int> movePoints(Lane& lane, const std::vector<int>& indices, int dtick, double dvalue);
// set the segment from point i to the next: curvature k (a step becomes a ramp)
void setSegmentCurvature(Lane& lane, int i, double k);
void setSegmentCurve(Lane& lane, int i, Curve c);
// Draw Mode: a step [tick1, tick2) at value; the envelope after tick2 as before (as `original` had it: the lane
// before the gesture, when the steps of one drag are drawn one after the other)
void drawStep(Lane& lane, int tick1, int tick2, double value, const Lane* original = nullptr);
// Simplify, Draw Mode's freehand line and Insert Shape (Live 12, manual 25.5.3-5). Their tolerance: one MIDI step,
// CC_RESOLUTION (MIDI 1.0's 7-bit data: 1/127 of the range; the step Lane::events and flattenCurve already use).
//
// the fewest breakpoints through the first and last of `samples` (tick, value 0-1; by tick, a continuous line)
// whose envelope is nowhere further than tol from any sample: greedy, each segment as long as one segment fits,
// straight when a straight line does, else curved, Live's cubic Bézier with its control points' x at 1/3 and 2/3
// (so its time runs evenly: x(t) = t) and their y fitted by least squares (closed form), kept within the box
// (0-1). Each point LINEAR (the last one too: the caller sets its curve); a sample count under 2: as given
std::vector<Point> fitPoints(const std::vector<std::pair<int, double>>& samples, double tol);
// the envelope in [tick1, tick2] simplified (Live: "Simplify Envelope"): each continuous stretch (between steps
// and jumps, which stay) refitted by fitPoints from the envelope sampled densely; returns the number of points
// removed (negative: added, never expected)
int simplify(Lane& lane, int tick1, int tick2, double tol = CC_RESOLUTION);
// the envelope in [tick1, tick2] replaced by `pts` (ticks inside the span, by tick): the envelope before tick1
// arrives where it was (a curve split, keeping its shape; a jump to pts' first value when it differs), the one
// after tick2 goes on as it was (same; a jump back), as drawStep and pastePoints do. `original`: the envelope to
// keep outside the span (the lane before the gesture); none: the lane itself
void replaceSpan(Lane& lane, int tick1, int tick2, const std::vector<Point>& pts, const Lane* original = nullptr);
// Draw Mode with Alt: the freehand line (samples, by tick: the mouse's path) as breakpoints (fitPoints), over
// the span it covers
void drawFree(Lane& lane, const std::vector<std::pair<int, double>>& samples, const Lane* original = nullptr,
              double tol = CC_RESOLUTION);
// Insert Shape: one cycle over [tick1, tick2], the parameter's full range (0-1). Sine: from the middle up,
// sin(2πx); triangle: middle, top, bottom, middle; sawtooth: bottom to top; inverse: top to bottom; square:
// top for the first half, bottom for the second (steps). Curves from fitPoints (sine: within tol)
enum class Shape : signed char { SINE, TRIANGLE, SAW, INVERSE_SAW, SQUARE };
void insertShape(Lane& lane, int tick1, int tick2, Shape shape, double tol = CC_RESOLUTION);
// the shape's value at x (0-1 of the cycle)
double shapeAt(Shape shape, double x);
// copy: the points' copies with ticks from the first one's (0); paste: at tick, over the clip's span
// (the points there replaced), the envelope after it as before
std::vector<Point> copyPoints(const Lane& lane, const std::vector<int>& indices);
std::vector<int> pastePoints(Lane& lane, int tick, const std::vector<Point>& clip);

}     // namespace Edit

}     // namespace Automation
}     // namespace Ms
#endif
