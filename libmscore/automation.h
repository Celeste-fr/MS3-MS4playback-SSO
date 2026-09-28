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
//   (step: stays until it; linear: a ramp to it). Before the first point the lane says nothing
//   (the controller's own value, as without a lane); after the last it stays.
//
//   Kept in the score as the metaTag "automation" (MuseScore 3.6 keeps metaTags through a round
//   trip), JSON:
//     [{"part": index, "name": part name,
//       "lanes": [{"target": "vibrato", "points": [[tick, value, "step" | "linear"], …]}, …]}]
//   Parts found as partplayback.h finds them; parts of an excerpt follow their master's part.
//   Played (rendermidi, the sound library's parts): a lane takes the place of its controller's
//   part value and staff texts; a MIDI controller's lane as controller events, a plug-in
//   parameter's as parameter events to the hosted plug-in (Vst3Synth).
//---------------------------------------------------------

#include <map>
#include <vector>
#include <QString>

namespace Ms {

class MasterScore;
class Part;

namespace Automation {

extern const char* const metaTag;

enum class Curve : signed char { STEP, LINEAR };

struct Point {
      int tick { 0 };
      double value { 0 };                 // 0-1
      Curve curve { Curve::STEP };        // to the next point
      bool operator<(const Point& o) const { return tick < o.tick; }
      };

struct Lane {
      QString target;                     // a controller id, or "cc<n>"
      std::vector<Point> points;          // by tick
      int cc() const;                     // "cc<n>": n (0-127); else -1
      // the value at tick: -1 before the first point
      double valueAt(int tick) const;
      // what to send in [tick1, tick2): the value in force at tick1 (when there is one), then each
      // point, and along a ramp every stepTicks as far as the value moves by resolution or more
      std::vector<std::pair<int, double>> events(int tick1, int tick2, int stepTicks, double resolution) const;
      };

using PartLanes = std::vector<Lane>;

std::map<const Part*, PartLanes> read(const MasterScore* score);
QString write(const MasterScore* score, const std::map<const Part*, PartLanes>& lanes);    // empty: none
// a part's lanes (its master part's)
PartLanes lanes(const Part* part, const std::map<const Part*, PartLanes>& all);

}     // namespace Automation
}     // namespace Ms
#endif
