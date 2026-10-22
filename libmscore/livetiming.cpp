//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livetiming.h"

#include <algorithm>
#include <cmath>
#include <deque>

#include "audio/midi/event.h"

namespace Ms {
namespace LiveTiming {

//---------------------------------------------------------
//   noteTimes
//---------------------------------------------------------

std::map<Key, Span> noteTimes(const EventMap& events, std::map<const void*, Key>* keys)
      {
      std::map<Key, Span> out;
      Counter count;
      std::map<std::pair<int, int>, std::deque<Key>> open;      // (route, key) -> notes on
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch() || e.type() != ME_NOTEON && e.type() != ME_NOTEOFF)
                  continue;
            const std::pair<int, int> rk { e.extPort() * 16 + e.extChannel(), e.pitch() };
            if (e.type() == ME_NOTEON && e.velo() > 0) {
                  if (!e.note())
                        continue;
                  const Key k = count.next(e.note(), e.pitch());
                  out[k] = { te.first, te.first };
                  auto early = events.startEarlyMs.find(e.note());
                  if (te.first == 0 && k.k == 0 && early != events.startEarlyMs.end())
                        out[k].earlyMs = early->second;
                  if (keys)
                        (*keys)[&e] = k;
                  open[rk].push_back(k);
                  }
            else {
                  auto o = open.find(rk);
                  if (o == open.end() || o->second.empty())
                        continue;
                  out[o->second.front()].off = te.first;
                  o->second.pop_front();
                  }
            }
      return out;
      }

//---------------------------------------------------------
//   units and ms
//---------------------------------------------------------

double unitsToMs(double units, double bpm)
      {
      return units * 60000.0 / (bpm * LiveClips::UNITS_PER_BEAT);
      }

double msToUnits(double ms, double bpm)
      {
      return ms * bpm * LiveClips::UNITS_PER_BEAT / 60000.0;
      }

//---------------------------------------------------------
//   track
//---------------------------------------------------------

namespace {
double hundredths(double ms)
      {
      return std::round(ms * 100.0) / 100.0;
      }
// the most common value (ties: the smaller)
double mostCommon(const std::vector<double>& v)
      {
      std::map<double, int> n;
      for (double x : v)
            ++n[x];
      double best = 0;
      int most = -1;
      for (const auto& c : n)
            if (c.second > most) {
                  most = c.second;
                  best = c.first;
                  }
      return best;
      }
}

Track track(const std::vector<LiveClips::Note>& notes, double bpm, const std::multimap<int, int>* keyOns)
      {
      Track t;
      std::multimap<int, int> ownOns;
      if (!keyOns) {
            for (const LiveClips::Note& n : notes)
                  ownOns.insert({ n.pitch, n.timed ? n.playedStart : n.start });
            keyOns = &ownOns;
            }
      const double margin = msToUnits(RETRIGGER_MARGIN_MS, bpm);
      // a note-on of the note's key at its note-off (or just after; not its own): the off goes margin before it.
      // Returns the played time the off must not pass (units), or a huge value
      auto offLimit = [&](const LiveClips::Note& n) {
            const int playedOff = n.timed ? n.playedEnd : n.start + n.length;
            const int playedOn = n.timed ? n.playedStart : n.start;
            double limit = 1e300;
            const auto keys = keyOns->equal_range(n.pitch);
            for (auto k = keys.first; k != keys.second; ++k)
                  if (k->second > playedOn && k->second >= playedOff && k->second - playedOff < margin)
                        limit = std::min(limit, k->second - margin);
            return limit;
            };
      double early = 0;                         // the latest early start or end (units)
      for (const LiveClips::Note& n : notes) {
            if (n.timed)
                  early = std::max(early, double(std::max(n.start - n.playedStart, n.start + n.length - n.playedEnd)));
            // (the lookahead makes room for a retrigger's earlier off too: a technique played as written has none
            // otherwise, and the device's delays can't be negative; real Live 12.4.7, 2026-10-22, Whence's Basses)
            const double limit = offLimit(n);
            if (limit < 1e300)
                  early = std::max(early, double(n.start + n.length) - limit);
            }
      t.lookaheadMs = std::ceil(unitsToMs(early, bpm) * 10.0 - 1e-6) / 10.0;
      if (t.lookaheadMs < 0)
            t.lookaheadMs = 0;
      const double l = msToUnits(t.lookaheadMs, bpm);
      std::vector<double> earlies;
      for (const LiveClips::Note& n : notes)
            earlies.push_back(n.timed ? std::max(0.0, std::round(unitsToMs(n.start - n.playedStart, bpm) * 10.0) / 10.0) : 0.0);
      t.commonEarlyMs = earlies.empty() ? 0.0 : mostCommon(earlies);
      std::vector<double> ons, offs;
      for (const LiveClips::Note& n : notes) {
            const double on = n.timed ? n.playedStart + l - n.start : l;
            double off = n.timed ? n.playedEnd + l - (n.start + n.length) : l;
            // a note-on of its key at its note-off (or just after): the off RETRIGGER_MARGIN_MS before it
            const double limit = offLimit(n);
            const bool retrigger = limit < 1e300;
            if (retrigger)
                  off = std::min(off, limit + l - (n.start + n.length));
            if (off < 0) {
                  ++t.offsLate;
                  off = 0;
                  }
            ons.push_back(hundredths(unitsToMs(std::max(0.0, on), bpm)));
            // (rounded down: the margin kept)
            offs.push_back(retrigger ? std::floor(unitsToMs(off, bpm) * 100.0 + 1e-9) / 100.0 : hundredths(unitsToMs(off, bpm)));
            }
      t.onMs = ons.empty() ? 0.0 : mostCommon(ons);
      t.offMs = offs.empty() ? t.lookaheadMs : mostCommon(offs);
      for (size_t i = 0; i < notes.size(); ++i)
            if (ons[i] != t.onMs || offs[i] != t.offMs)
                  t.exceptions.push_back({ notes[i].start, notes[i].start + notes[i].length, notes[i].pitch, ons[i], offs[i] });
      std::stable_sort(t.exceptions.begin(), t.exceptions.end(), [](const Exception& a, const Exception& b) {
            return a.start < b.start;
            });
      return t;
      }

//---------------------------------------------------------
//   playedBy
//---------------------------------------------------------

std::pair<double, double> playedBy(const Track& t, const LiveClips::Note& n, double bpm)
      {
      double on = t.onMs, off = t.offMs;
      for (const Exception& e : t.exceptions)
            if (e.start == n.start && e.pitch == n.pitch) {
                  on = e.onMs;
                  off = e.offMs;
                  break;
                  }
      const double l = msToUnits(t.lookaheadMs, bpm);
      return { n.start - l + msToUnits(on, bpm), n.start + n.length - l + msToUnits(off, bpm) };
      }

//---------------------------------------------------------
//   record
//---------------------------------------------------------

std::vector<double> record(const Track& t)
      {
      std::vector<double> r { 1, t.lookaheadMs, t.onMs, t.offMs, double(t.exceptions.size()) };
      for (const Exception& e : t.exceptions) {
            r.push_back(e.start);
            r.push_back(e.end);
            r.push_back(e.pitch);
            r.push_back(e.onMs);
            r.push_back(e.offMs);
            }
      return r;
      }

quint32 hashOf(const Track& t)
      {
      quint32 h = 2166136261u;
      for (double v : record(t)) {
            const qint64 x = qint64(std::llround(v * 100.0));
            for (int b = 0; b < 8; ++b) {
                  h ^= quint32((x >> (8 * b)) & 0xff);
                  h *= 16777619u;
                  }
            }
      return h;
      }

}     // namespace LiveTiming
}     // namespace Ms
