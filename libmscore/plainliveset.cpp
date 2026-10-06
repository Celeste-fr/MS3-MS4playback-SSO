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
#include <cstring>
#include <deque>
#include <set>

#include <QObject>

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

//---------------------------------------------------------
//   the tracks
//---------------------------------------------------------

static double beats(int units)
      {
      return double(units) / LiveClips::UNITS_PER_BEAT;
      }

int restValue(const SoundLib::LibInstrument* instrument)
      {
      std::set<int> used;
      if (instrument)
            for (const SoundLib::Articulation& a : instrument->articulations)
                  used.insert(a.value);
      for (int v = 0; v < 128; ++v)
            if (!used.count(v))
                  return v;
      return 0;
      }

// the runs of a technique's notes: (first start, last start), no other technique of the Kontakt starting in between
static std::vector<std::pair<int, int>> runs(const Kontakt& kontakt, size_t technique)
      {
      std::vector<std::pair<int, size_t>> starts;
      for (size_t i = 0; i < kontakt.techniques.size(); ++i)
            for (const LiveClips::Note& n : kontakt.techniques[i].notes)
                  if (!n.muted)
                        starts.push_back({ n.start, i });
      std::sort(starts.begin(), starts.end());
      std::vector<std::pair<int, int>> out;
      size_t previous = kontakt.techniques.size();
      for (const auto& s : starts) {
            if (s.second == technique) {
                  if (previous != technique)
                        out.push_back({ s.first, s.first });
                  out.back().second = s.first;
                  }
            previous = s.second;
            }
      return out;
      }

// the clip from..to (units in the song) with the technique's notes starting in it, times from the clip's start
static LiveSetWriter::Clip clipOf(const Technique& tq, int from, int to)
      {
      LiveSetWriter::Clip clip;
      clip.name = tq.name;
      clip.start = beats(from);
      clip.end = beats(to);
      for (const LiveClips::Note& n : tq.notes)
            if (!n.muted && n.start >= from && n.start < to)      // (muted: not played)
                  clip.notes.push_back({ n.pitch, beats(n.start - from), beats(n.length), n.velocity });
      return clip;
      }

std::vector<LiveSetWriter::Clip> switchClips(const Kontakt& kontakt, size_t technique, int length)
      {
      const Technique& tq = kontakt.techniques[technique];
      const SoundLib::LibInstrument* li = kontakt.instrument;
      const bool cc = li && tq.value >= 0 && li->switchType == SoundLib::SwitchType::CC;
      const bool keyswitch = li && tq.value >= 0 && li->switchType == SoundLib::SwitchType::KEYSWITCH;
      const std::vector<std::pair<int, int>> rs = cc || keyswitch ? runs(kontakt, technique) : std::vector<std::pair<int, int>>();

      // CC32 (Spitfire's UACC): Live keeps no clip envelope on CC0 or CC32 (12.4.6 drops it at load), but sends a clip's
      // Sub at its start: a clip per run from one unit before its first note, up to the next run's clip (or the end of
      // its last note)
      if (cc && li->switchNumber == 32 && !rs.empty()) {
            std::vector<LiveSetWriter::Clip> out;
            for (size_t i = 0; i < rs.size(); ++i) {
                  const int from = std::max(0, rs[i].first - 1);
                  const int next = i + 1 < rs.size() ? std::max(0, rs[i + 1].first - 1) : length;
                  int end = rs[i].second + 1;
                  for (const LiveClips::Note& n : tq.notes)
                        if (!n.muted && n.start >= from && n.start < next)
                              end = std::max(end, n.start + n.length);
                  LiveSetWriter::Clip c = clipOf(tq, from, std::min(end, next));
                  c.subBank = tq.value;
                  out.push_back(c);
                  }
            return out;
            }

      LiveSetWriter::Clip clip = clipOf(tq, 0, length);
      if (rs.empty())
            return { clip };
      if (keyswitch) {
            for (const auto& r : rs)
                  clip.notes.push_back({ tq.value, beats(std::max(0, r.first - 1)), beats(1), 100 });
            std::stable_sort(clip.notes.begin(), clip.notes.end(), [](const LiveSetWriter::Clip::Note& a, const LiveSetWriter::Clip::Note& b) {
                  return a.start < b.start;
                  });
            return { clip };
            }

      const int rest = restValue(li);
      const int v = tq.value;
      LiveSetWriter::Clip::Envelope e;
      e.controller = li->switchNumber;
      std::vector<std::pair<int, int>> points;                // (units, value)
      points.push_back({ 0, rs.front().first >= 1 ? rest : v });
      for (const auto& r : rs) {
            const int on = std::max(0, r.first - 1);
            if (points.size() > 1 && points.back().first >= on)
                  points.pop_back();                      // (back to rest no earlier than this switch: stays up)
            else if (points.size() == 1 && on == 0)
                  points.back().second = v;               // (at the start: the value before it too)
            else if (points.back().second == rest) {
                  points.push_back({ on, rest });
                  points.push_back({ on, v });
                  }
            const int off = r.second + 1;
            points.push_back({ off, v });
            points.push_back({ off, rest });
            }
      for (const auto& p : points)
            e.points.push_back({ beats(p.first), double(p.second) });
      clip.envelopes.push_back(e);
      return { clip };
      }

const char* const KEY_PREFIX = "MuseScore: ";

QString trackKey(const QStringList& path)
      {
      QStringList l;
      for (const QString& n : path)
            l << QString(n).replace(" / ", "/");
      return KEY_PREFIX + l.join(" / ");
      }

QStringList keyPath(const QString& annotation)
      {
      if (!annotation.startsWith(KEY_PREFIX) || annotation.size() == int(strlen(KEY_PREFIX)))
            return QStringList();
      return annotation.mid(int(strlen(KEY_PREFIX))).split(" / ");
      }

std::vector<LiveSetWriter::Track> tracks(const Layout& layout)
      {
      std::vector<LiveSetWriter::Track> out;
      int partNumber = 0;
      for (const Section& s : layout.sections) {
            const int sectionIndex = int(out.size());
            LiveSetWriter::Track sg;
            sg.name = s.name;
            sg.group = true;
            sg.link = false;
            sg.color = LiveSetWriter::partColor(partNumber);
            sg.annotation = trackKey({ s.name });
            out.push_back(sg);
            for (const PartTracks& p : s.parts) {
                  const int color = LiveSetWriter::partColor(partNumber++);
                  const int partIndex = int(out.size());
                  LiveSetWriter::Track pg;
                  pg.name = p.name;
                  pg.group = true;
                  pg.link = false;
                  pg.color = color;
                  pg.groupIndex = sectionIndex;
                  pg.annotation = trackKey({ s.name, p.name });
                  out.push_back(pg);
                  const SoundLib::PartMix mix = SoundLib::partMix(p.part, false);
                  for (const Kontakt& k : p.kontakts) {
                        LiveSetWriter::Track kt;
                        kt.name = LiveSetWriter::trackName(p.name, k.patch, k.patchIndex == 0, 0);
                        kt.link = false;
                        kt.color = color;
                        kt.groupIndex = partIndex;
                        kt.part = p.name;
                        kt.patch = k.patch;
                        kt.mainPatch = k.patchIndex == 0;
                        kt.instrument = k.instrument;
                        kt.partRef = p.part;
                        kt.port = k.port;
                        kt.channel = k.channel + 1;
                        kt.routeKey = QString("%1:%2").arg(k.port).arg(kt.channel);
                        kt.routePatch = k.patchIndex;
                        kt.annotation = trackKey({ s.name, p.name, kt.name });
                        kt.volume = LiveSetWriter::mixGain(mix.volume);
                        kt.pan = LiveSetWriter::mixPan(mix.pan);
                        kt.active = !mix.muted;
                        if (!k.lanes.empty()) {
                              LiveSetWriter::Clip c;
                              c.name = QObject::tr("Controllers");
                              c.end = beats(layout.length);
                              for (const Lane& l : k.lanes) {
                                    if (l.cc == PITCH_BEND)             // the scale of Live's clip bend envelope isn't measured: reported
                                          continue;
                                    LiveSetWriter::Clip::Envelope e;
                                    e.controller = l.cc;
                                    for (const auto& pt : l.points)
                                          e.points.push_back({ beats(pt.first), double(pt.second) });
                                    c.envelopes.push_back(e);
                                    }
                              if (!c.envelopes.empty())
                                    kt.clips.push_back(c);
                              }
                        for (const ParamLane& pl : k.params) {
                              LiveSetWriter::ParameterAutomation a;
                              a.name = pl.title;
                              for (const auto& pt : pl.points)
                                    a.points.push_back({ beats(pt.first), double(pt.second) });
                              kt.automation.push_back(a);
                              }
                        const int kontaktIndex = int(out.size());
                        out.push_back(kt);
                        for (size_t i = 0; i < k.techniques.size(); ++i) {
                              LiveSetWriter::Track tt;
                              tt.name = QString("%1 – %2").arg(out[size_t(kontaktIndex)].name, k.techniques[i].name);
                              tt.link = false;
                              tt.color = color;
                              tt.groupIndex = partIndex;
                              tt.midiTo = kontaktIndex;
                              tt.part = p.name;
                              tt.partRef = p.part;
                              tt.clips = switchClips(k, i, layout.length);
                        tt.annotation = trackKey({ s.name, p.name, out[size_t(kontaktIndex)].name, k.techniques[i].name });
                              out.push_back(tt);
                              }
                        }
                  }
            }
      // a key met again (two parts of one name): " (2)", " (3)" …
      std::map<QString, int> seen;
      for (LiveSetWriter::Track& t : out) {
            const int n = ++seen[t.annotation];
            if (n > 1)
                  t.annotation += QString(" (%1)").arg(n);
            }
      return out;
      }

}     // namespace PlainLiveSet
}     // namespace Ms
