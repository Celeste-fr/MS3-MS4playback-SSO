//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "plainliveset.h"

#include <algorithm>
#include <deque>
#include <set>

#include "audio/midi/event.h"
#include "instrtemplate.h"
#include "instrument.h"
#include "part.h"
#include "score.h"
#include "soundlibrary.h"

namespace Ms {
namespace PlainLiveSet {

bool laneController(int cc, const SoundLib::LibInstrument* instrument)
      {
      if (instrument && instrument->switchType == SoundLib::SwitchType::CC && cc == instrument->switchNumber)
            return false;
      switch (cc) {
            case CTRL_HBANK:
            case CTRL_LBANK:
            case CTRL_VOLUME:
            case CTRL_PANPOT:
            case CTRL_REVERB_SEND:
            case CTRL_CHORUS_SEND:
                  return false;
            default:
                  return cc >= 0 && cc < 0x78;
            }
      }

QString sectionName(const QString& instrumentId)
      {
      const InstrumentIndex ii = searchTemplateIndexForId(instrumentId);
      if (ii.groupIndex < 0 || ii.groupIndex >= instrumentGroups.size())
            return QString("Other");
      const InstrumentGroup* g = instrumentGroups[ii.groupIndex];
      if (g->id.endsWith("percussion"))
            return QString("Percussion");
      if (g->id == "strings")
            return QString("Strings");
      if (g->id == "plucked-strings")
            return QString("Plucked strings");
      return g->name;
      }

namespace {

struct Builder {
      Kontakt* kontakt { nullptr };
      QString part;
      std::map<int, size_t> techniqueOf;            // switch value -> its index in techniques
      std::map<int, std::set<int>> startsAt;        // units -> the switch values of the notes starting there
      std::map<int, std::vector<std::pair<int, int>>> lanes;       // cc (PITCH_BEND) -> (units, value)
      std::map<int, std::vector<std::pair<int, float>>> params;    // the event's index -> (units, value)
      };

QString techniqueName(const SoundLib::LibInstrument* li, int value)
      {
      if (!li)
            return QString();
      if (value < 0 || li->switchType == SoundLib::SwitchType::NONE)
            return li->name;
      for (const SoundLib::Articulation& a : li->articulations)
            if (a.value == value)
                  return a.name;
      return QString("Switch %1").arg(value);
      }

// a value from a time on: one at the same time replaces it, one like the value before isn't kept
template <typename V>
void putPoint(std::vector<std::pair<int, V>>& points, int at, V value)
      {
      if (!points.empty() && points.back().first == at) {
            points.back().second = value;
            if (points.size() > 1 && points[points.size() - 2].second == value)
                  points.pop_back();
            }
      else if (points.empty() || points.back().second != value)
            points.push_back({ at, value });
      }

}     // namespace

Layout layout(const Score* score, const SoundLib::Library& library, const EventMap& events,
              const LiveClips::Timeline& tl)
      {
      Layout out;
      out.bpm = tl.bpm;
      if (!score)
            return out;
      const int endUnits = tl.units(LiveClips::playedTicks(score));
      out.length = std::max(1, endUnits);

      // the tracks: section, part, patch (a tuning lane's route joins its patch's)
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, library);
      std::vector<std::pair<const Part*, std::pair<int, int>>> order;     // (part, (patch, route index)) of lane 0
      std::map<int, std::pair<const Part*, int>> routeTo;                   // route -> (part, patch)
      std::map<int, bool> copyRoute;                                        // route -> a tuning lane's copy
      for (const SoundLib::Route& r : routes) {
            const int key = r.port * 16 + r.channel;
            routeTo[key] = { r.part, r.patch };
            copyRoute[key] = r.lane > 0;
            }
      for (const SoundLib::Route& r : routes) {
            if (r.lane > 0)
                  continue;
            const QString id = r.part && r.part->instrument() ? r.part->instrument()->getId() : QString();
            const QString sname = sectionName(id);
            auto s = std::find_if(out.sections.begin(), out.sections.end(), [&](const Section& x) { return x.name == sname; });
            if (s == out.sections.end()) {
                  out.sections.push_back(Section());
                  out.sections.back().name = sname;
                  s = out.sections.end() - 1;
                  }
            auto p = std::find_if(s->parts.begin(), s->parts.end(), [&](const PartTracks& x) { return x.part == r.part; });
            if (p == s->parts.end()) {
                  s->parts.push_back(PartTracks());
                  s->parts.back().part = r.part;
                  s->parts.back().name = r.part ? r.part->partName() : QString();
                  p = s->parts.end() - 1;
                  }
            Kontakt k;
            k.instrument = r.instrument;
            k.patch = r.instrument ? r.instrument->name : QString();
            k.port = r.port;
            k.channel = r.channel;
            k.patchIndex = r.patch;
            p->kontakts.push_back(k);
            }
      // (pointers once every vector has its size)
      std::map<std::pair<const Part*, int>, Builder> builders;
      for (Section& s : out.sections)
            for (PartTracks& p : s.parts)
                  for (Kontakt& k : p.kontakts) {
                        Builder& b = builders[{ p.part, k.patchIndex }];
                        b.kontakt = &k;
                        b.part = p.name;
                        }

      // the events, route by route
      std::map<int, std::vector<std::pair<int, const NPlayEvent*>>> byRoute;
      for (const auto& te : events)
            if (te.second.isExternal())
                  byRoute[te.second.extPort() * 16 + te.second.extChannel()].push_back({ te.first, &te.second });
      for (const auto& r : byRoute) {
            auto to = routeTo.find(r.first);
            if (to == routeTo.end())
                  continue;
            auto bi = builders.find(to->second);
            if (bi == builders.end())
                  continue;
            Builder& b = bi->second;
            Kontakt& k = *b.kontakt;
            const SoundLib::LibInstrument* li = k.instrument;
            const bool copy = copyRoute[r.first];
            int value = -1;                               // the switch in force
            std::map<int, std::deque<std::pair<size_t, LiveClips::Note>>> open;   // pitch -> (technique, note)
            for (const auto& item : r.second) {
                  const NPlayEvent& e = *item.second;
                  const int at = tl.units(item.first);
                  const bool on = e.type() == ME_NOTEON && e.velo() > 0;
                  const bool off = e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0);
                  if (e.librarySwitch()) {
                        if (on && li && li->switchType == SoundLib::SwitchType::KEYSWITCH)
                              value = e.pitch();
                        else if (e.type() == ME_CONTROLLER)
                              value = e.value();
                        continue;
                        }
                  if (e.type() == ME_CONTROLLER) {
                        if (li && li->switchType == SoundLib::SwitchType::CC && e.controller() == li->switchNumber)
                              value = e.value();
                        else if (li && li->switchType == SoundLib::SwitchType::PROGRAM && e.controller() == CTRL_PROGRAM)
                              value = e.value();
                        else if (laneController(e.controller(), li) && !copy)
                              putPoint(b.lanes[e.controller()], at, e.value());
                        }
                  else if (e.type() == ME_PITCHBEND) {
                        if (!copy)
                              putPoint(b.lanes[PITCH_BEND], at, (e.dataB() & 0x7f) << 7 | (e.dataA() & 0x7f));
                        }
                  else if (e.type() == ME_PARAMETER) {
                        if (!copy) {
                              const int pk = e.dataA() == LiveClips::LIVE_PARAM ? LiveClips::LIVE_PARAM_KEY + e.dataB() : e.dataA();
                              putPoint(b.params[pk], at, e.tuning());
                              }
                        }
                  else if (on) {
                        auto t = b.techniqueOf.find(value);
                        if (t == b.techniqueOf.end()) {
                              Technique tq;
                              tq.name = techniqueName(li, value);
                              tq.value = li && li->switchType != SoundLib::SwitchType::NONE ? value : -1;
                              k.techniques.push_back(tq);
                              t = b.techniqueOf.insert({ value, k.techniques.size() - 1 }).first;
                              }
                        LiveClips::Note n;
                        n.pitch = e.pitch();
                        n.start = at;
                        n.velocity = e.velo();
                        n.muted = e.note() ? e.isMuted() : false;
                        open[n.pitch].push_back({ t->second, n });
                        b.startsAt[at].insert(value);
                        if (copy)
                              ++k.untuned;
                        }
                  else if (off) {
                        auto o = open.find(e.pitch());
                        if (o == open.end() || o->second.empty())
                              continue;
                        auto tn = o->second.front();
                        o->second.pop_front();
                        tn.second.length = std::max(1, at - tn.second.start);
                        k.techniques[tn.first].notes.push_back(tn.second);
                        }
                  }
            for (auto& o : open)
                  for (auto tn : o.second) {
                        tn.second.length = std::max(1, endUnits - tn.second.start);
                        k.techniques[tn.first].notes.push_back(tn.second);
                        }
            }

      // the lanes, clashes; notes in order
      for (auto& bi : builders) {
            Builder& b = bi.second;
            Kontakt& k = *b.kontakt;
            for (Technique& t : k.techniques)
                  std::stable_sort(t.notes.begin(), t.notes.end());
            for (auto& l : b.lanes)
                  if (l.first != PITCH_BEND)
                        k.lanes.push_back({ l.first, l.second });
            auto bend = b.lanes.find(PITCH_BEND);
            if (bend != b.lanes.end())
                  k.lanes.push_back({ PITCH_BEND, bend->second });
            // the parameter lanes: titled by the part's main patch's controllers (the renderer's index), or the
            // part's lanes on its Live track ("live:<d>/<p>")
            const SoundLib::LibInstrument* main = nullptr;
            QStringList live;
            for (const SoundLib::Route& r : routes)
                  if (r.part == bi.first.first && r.patch == 0 && r.lane == 0)
                        main = r.instrument;
            if (bi.first.first)
                  live = LiveClips::liveLanes(Automation::lanes(bi.first.first, Automation::read(score->masterScore())));
            for (const auto& pe : b.params) {
                  QString title;
                  if (pe.first >= LiveClips::LIVE_PARAM_KEY) {
                        const int i = pe.first - LiveClips::LIVE_PARAM_KEY;
                        if (k.patchIndex == 0 && i < live.size())
                              title = live[i];
                        }
                  else if (main && pe.first >= 0 && pe.first < int(main->allControllers.size()))
                        title = main->allControllers[size_t(pe.first)].param;
                  if (!title.isEmpty())
                        k.params.push_back({ title, pe.second });
                  }
            for (const auto& s : b.startsAt) {
                  if (s.second.size() < 2)
                        continue;
                  Clash c;
                  c.part = b.part;
                  c.patch = k.patch;
                  c.at = s.first;
                  for (int v : s.second)
                        c.techniques << k.techniques[b.techniqueOf[v]].name;
                  out.clashes.push_back(c);
                  }
            }
      std::sort(out.clashes.begin(), out.clashes.end(), [](const Clash& a, const Clash& b) { return a.at < b.at; });
      return out;
      }

}     // namespace PlainLiveSet
}     // namespace Ms
