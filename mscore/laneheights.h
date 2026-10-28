//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LANEHEIGHTS_H__
#define __LANEHEIGHTS_H__

//---------------------------------------------------------
//   The automation editor's lane heights (automationlanes.h; the owner, 2026-10-28: "can we add a way to resize the
//   automation lanes like in Live, with a way to reset them to the default height"). Every lane under a part's header
//   row (automation / controller lanes, the Engine lane, the Velocity and Join lanes) has its own height, in spatium
//   as the rest of the layout (so it zooms with the score; Live's are screen pixels): DEFAULT_SP at first, MIN_SP ..
//   MAX_SP (MIN_SP still shows the lane's header text on one line, as Live's smallest lane does: the header text is as
//   large as the instrument names, 10 pt = 2 sp, textPx; paintHeader puts name and value on one line under 2.2 text
//   heights; MAX_SP six lanes' worth). The header row itself keeps its height (it holds the + / All / pencil / Grid controls).
//
//   Gestures (automationlanes.cpp):
//     Live's (manual 6.9 "Selecting Clips and Time", 42.16 shortcuts):
//       drag the lower edge of a lane's header box (the split line under it; the cursor turns to a vertical resize
//         cursor there): that lane's height;
//       Alt while dragging: every lane of the score to that height (Live: "hold Alt while resizing a single track"
//         resizes all tracks);
//       Alt + "+" / Alt + "-" (Alt + "=" too): the focused lane (the one last clicked) a step (STEP_SP) taller / shorter
//         (Live's "Adjust Height of Selected Tracks").
//     Ours (Live has no reset to a default height, only Optimize Height, H, which fits the window):
//       double-click the lower edge: the lane back to DEFAULT_SP; Alt + double-click: every lane (resetAll);
//       the lane's right-click menu: "Reset Lane Height" (shown when it isn't the default) and "Reset All Lane Heights"
//         (when any lane of the score isn't).
//
//   Kept as the folding is (lanefolds.h): per score and view part, for the session in memory only; never in the file
//   (a score must round-trip through MuseScore 3.6) and never marking the score changed; undo / redo keep it, a part
//   that no longer exists is dropped (keepOnly). A lane hidden and shown again keeps its height.
//   No score, no painting: tested in tst_liveintegration (laneHeights), with the geometry the lanes use (spaceOf,
//   stack, edgeAt).
//---------------------------------------------------------

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>
#include <QString>

namespace Ms {

class Part;
class Score;

class LaneHeights {
   public:
      static constexpr double DEFAULT_SP = 5.0;      // a lane (automationlanes.cpp's old LANE_SP)
      static constexpr double MIN_SP = 2.5;
      static constexpr double MAX_SP = 30.0;
      static constexpr double STEP_SP = 1.0;         // Alt + / Alt -
      static constexpr double GAP_SP = 0.4;          // between lanes
      static constexpr double EDGE_PX = 4.0;         // the lower edge's grip, screen pixels above it (and the gap below)

   private:
      struct ScoreHeights {
            double all { DEFAULT_SP };                            // every lane without its own (Alt-drag)
            std::map<std::pair<const Part*, QString>, double> own;
            };
      std::map<const Score*, ScoreHeights> _h;

      void tidy(const Score* s)
            {
            auto it = _h.find(s);
            if (it != _h.end() && it->second.own.empty() && it->second.all == DEFAULT_SP)
                  _h.erase(it);
            }

   public:
      static double clamp(double sp) { return std::max(MIN_SP, std::min(MAX_SP, sp)); }

      double height(const Score* s, const Part* p, const QString& target) const
            {
            auto it = _h.find(s);
            if (it == _h.end())
                  return DEFAULT_SP;
            auto o = it->second.own.find({ p, target });
            return o != it->second.own.end() ? o->second : it->second.all;
            }
      bool isDefault(const Score* s, const Part* p, const QString& target) const { return height(s, p, target) == DEFAULT_SP; }
      // any lane of the score not at the default
      bool anyChanged(const Score* s) const { return _h.count(s) > 0; }

      void set(const Score* s, const Part* p, const QString& target, double sp)
            {
            ScoreHeights& h = _h[s];
            const double v = clamp(sp);
            if (v == h.all)
                  h.own.erase({ p, target });
            else
                  h.own[{ p, target }] = v;
            tidy(s);
            }
      // Alt: every lane of the score (also those not shown now) to sp
      void setAll(const Score* s, double sp)
            {
            ScoreHeights& h = _h[s];
            h.all = clamp(sp);
            h.own.clear();
            tidy(s);
            }
      // a step taller (+1) or shorter (-1); the new height
      double step(const Score* s, const Part* p, const QString& target, int by)
            {
            set(s, p, target, height(s, p, target) + by * STEP_SP);
            return height(s, p, target);
            }
      void reset(const Score* s, const Part* p, const QString& target) { set(s, p, target, DEFAULT_SP); }
      void resetAll(const Score* s) { _h.erase(s); }

      // the score's parts now (after undo / redo, a part removed): the others' heights go
      void keepOnly(const Score* s, const std::set<const Part*>& parts)
            {
            auto it = _h.find(s);
            if (it == _h.end())
                  return;
            auto& own = it->second.own;
            for (auto i = own.begin(); i != own.end(); )
                  i = parts.count(i->first.first) ? std::next(i) : own.erase(i);
            tidy(s);
            }

      //---------------------------------------------------------
      //   geometry (spatium or canvas units, as given)
      //---------------------------------------------------------

      // the room a part's block takes: the header row, then each shown lane and its gap (Score::setAutomationSpace)
      static double spaceOf(double headSp, const std::vector<double>& lanesSp)
            {
            double s = headSp;
            for (double h : lanesSp)
                  s += h + GAP_SP;
            return s;
            }
      // the lanes' [top, bottom) under a header row starting at y0, in units of sp
      static std::vector<std::pair<double, double>> stack(double y0, double headSp, const std::vector<double>& lanesSp, double sp)
            {
            std::vector<std::pair<double, double>> out;
            double y = y0 + headSp * sp;
            for (double h : lanesSp) {
                  out.push_back({ y, y + h * sp });
                  y += (h + GAP_SP) * sp;
                  }
            return out;
            }
      // the lane whose lower edge is at y (screen pixels; lanes [top, bottom) on screen): from EDGE_PX above its bottom
      // to the next lane's top (at most EDGE_PX, at least 2 px below); the upper lane first. -1: none
      static int edgeAt(const std::vector<std::pair<double, double>>& lanes, double y)
            {
            for (size_t i = 0; i < lanes.size(); ++i) {
                  const double bottom = lanes[i].second;
                  const double below = i + 1 < lanes.size() ? std::max(std::min(lanes[i + 1].first - bottom, EDGE_PX), 2.0) : 2.0;
                  const double top = std::max(lanes[i].first + 1.0, bottom - EDGE_PX);     // (a short lane: its top pixel stays the lane's)
                  if (y >= top && y <= bottom + below)
                        return int(i);
                  }
            return -1;
            }
      };

}     // namespace Ms
#endif
