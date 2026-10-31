//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "followcut.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <set>

#include "audio/midi/event.h"
#include "score.h"

namespace Ms {
namespace FollowCut {

double frequency(int key, double cents)
      {
      return 440.0 * std::pow(2.0, (key - 69 + cents / 100.0) / 12.0);
      }

double stepFactor(double rate)
      {
      return std::exp(-double(STEP_FRAMES) / (SMOOTH_S * (rate > 0 ? rate : 44100.0)));
      }

//---------------------------------------------------------
//   steps
//    a sweep over the notes' spans [on - LEAD_S, off + RELEASE_S): at each boundary the lowest fundamental of the spans
//    open there
//---------------------------------------------------------

std::vector<std::pair<double, double>> steps(const std::vector<Note>& notes, double floorHz)
      {
      std::vector<std::pair<double, double>> out;
      if (!(floorHz > 0))
            return out;
      struct Edge { double t; bool open; double hz; };
      std::vector<Edge> edges;
      for (const Note& n : notes) {
            if (!(n.hz > 0) || !(n.off >= n.on))
                  continue;
            edges.push_back({ n.on - LEAD_S, true, n.hz });
            if (std::isfinite(n.off))
                  edges.push_back({ n.off + RELEASE_S, false, n.hz });
            }
      // (at one time: the spans ending first, then the ones starting)
      std::stable_sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
            return a.t < b.t || (a.t == b.t && !a.open && b.open); });
      std::multiset<double> open;
      double value = floorHz;
      for (size_t i = 0; i < edges.size();) {
            const double t = edges[i].t;
            for (; i < edges.size() && edges[i].t == t; ++i) {
                  if (edges[i].open)
                        open.insert(edges[i].hz);
                  else {
                        auto o = open.find(edges[i].hz);
                        if (o != open.end())
                              open.erase(o);
                        }
                  }
            const double v = open.empty() ? floorHz : std::max(floorHz, RATIO * *open.begin());
            if (v != value) {
                  out.push_back({ t, v });
                  value = v;
                  }
            }
      return out;
      }

//---------------------------------------------------------
//   notes
//---------------------------------------------------------

std::vector<Note> notes(const Score* score, const EventMap& events, const std::vector<int>& routes,
                        const std::function<bool(int, const NPlayEvent&)>& own)
      {
      std::vector<Note> out;
      if (!score)
            return out;
      const std::set<int> wanted(routes.begin(), routes.end());
      // (route, key) -> the open note-ons: their index in out, -1 a note-on that doesn't count
      std::map<std::pair<int, int>, std::deque<long>> open;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch() || (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF))
                  continue;
            const int route = e.extPort() * 16 + e.extChannel();
            if (!wanted.count(route))
                  continue;
            const auto key = std::make_pair(route, e.pitch());
            const double t = score->utick2utime(te.first);
            if (e.type() == ME_NOTEON && e.velo() > 0) {
                  if (own && !own(te.first, e)) {
                        open[key].push_back(-1);
                        continue;
                        }
                  open[key].push_back(long(out.size()));
                  out.push_back({ t, std::numeric_limits<double>::infinity(), frequency(e.pitch(), double(e.tuning())) });
                  continue;
                  }
            auto o = open.find(key);
            if (o == open.end() || o->second.empty())
                  continue;               // (a note-on before these events: not known)
            const long k = o->second.front();
            o->second.pop_front();
            if (k >= 0)
                  out[size_t(k)].off = t;
            }
      return out;
      }

//---------------------------------------------------------
//   smoothedPoints
//    Vst3Synth's smoothing, step by step from time 0 (a step every STEP_FRAMES frames; a goal from the step at or after
//    its frame), written as straight lines within toleranceHz of it (a corridor of slopes from the last point: the point
//    goes where no line through the corridor reaches the next step)
//---------------------------------------------------------

std::vector<std::pair<double, double>> smoothedPoints(const std::vector<std::pair<double, double>>& steps, double floorHz,
                                                      double rate, double toleranceHz)
      {
      std::vector<std::pair<double, double>> out;
      if (!(floorHz > 0) || steps.empty())
            return out;
      if (!(rate > 0))
            rate = 44100.0;
      const double dt = double(STEP_FRAMES) / rate;
      const double a = stepFactor(rate);
      // (the goals at frame 0: taken at once, as Vst3Synth takes the cutoff in force when a playback or an export starts)
      size_t next = 0;
      double goal = floorHz;
      while (next < steps.size() && steps[next].first * rate < 1.0)
            goal = std::max(floorHz, steps[next++].second);
      double s = goal;
      out.push_back({ 0.0, s });
      long long k = 0;
      // (the corridor from the last point written: the slopes that keep a line within the tolerance of every step since)
      double t0 = 0.0, v0 = s, lo = -1e300, hi = 1e300;
      double lastT = 0.0, lastV = s;
      bool moving = false;
      for (;;) {
            // the goals whose frame falls at or before this step
            const long long frame = k * STEP_FRAMES;
            while (next < steps.size() && (long long)(std::max(0.0, steps[next].first) * rate) <= frame)
                  goal = std::max(floorHz, steps[next++].second);
            const double t = double(k) * dt;
            if (s == goal && !moving) {
                  if (next >= steps.size())
                        break;
                  // (still: on to the step before the next goal's)
                  const long long f = (long long)(std::max(0.0, steps[next].first) * rate);
                  const long long kNext = (f + STEP_FRAMES - 1) / STEP_FRAMES;
                  if (kNext > k) {
                        k = kNext;
                        continue;
                        }
                  }
            const double before = s;
            s = a * s + (1.0 - a) * goal;
            if (std::fabs(s - goal) < 0.01)
                  s = goal;
            if (!moving) {
                  if (s == before)
                        { ++k; continue; }
                  // a glide starts: a point holding the value before it
                  if (out.back().first < t)
                        out.push_back({ t, before });
                  t0 = t;
                  v0 = before;
                  lo = -1e300;
                  hi = 1e300;
                  moving = true;
                  }
            const double tt = t + dt;           // (this step's value holds from its time to the next step)
            const double span = tt - t0;
            const double l = (s - toleranceHz - v0) / span, h = (s + toleranceHz - v0) / span;
            if (std::max(lo, l) > std::min(hi, h)) {
                  // the line can't reach this step: a point at the last step, on the corridor
                  const double slope = std::max(lo, std::min(hi, (lastV - v0) / (lastT - t0)));
                  const double v = std::max(floorHz, v0 + slope * (lastT - t0));     // (never under the floor)
                  out.push_back({ lastT, v });
                  t0 = lastT;
                  v0 = v;
                  lo = (s - toleranceHz - v0) / (tt - t0);
                  hi = (s + toleranceHz - v0) / (tt - t0);
                  }
            else {
                  lo = std::max(lo, l);
                  hi = std::min(hi, h);
                  }
            lastT = tt;
            lastV = s;
            if (s == goal) {
                  out.push_back({ tt, s });
                  moving = false;
                  }
            ++k;
            }
      return out;
      }

}     // namespace FollowCut
}     // namespace Ms
