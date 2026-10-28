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
#include "livetiming.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <set>

#include <QObject>

#include "audio/midi/event.h"
#include "instrtemplate.h"
#include "instrument.h"
#include "note.h"
#include "part.h"
#include "score.h"
#include "soundlibrary.h"
#include "staff.h"

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

// a note of a voice not played (NPlayEvent::isMuted without the Mixer: the plain set leaves mute and solo to Live)
static bool voiceOff(const NPlayEvent& e)
      {
      const Note* n = e.note();
      return n && n->staff() && !n->staff()->playbackVoice(n->voice());
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

namespace {

struct Builder {
      Kontakt* kontakt { nullptr };
      const Part* partRef { nullptr };
      QString part;
      std::map<int, size_t> techniqueOf;            // switch value -> its index in techniques
      std::map<int, std::set<int>> startsAt;        // units -> the switch values of the notes starting there
      std::map<int, std::vector<std::pair<int, int>>> lanes;       // cc (PITCH_BEND) -> (units, value)
      std::map<int, std::set<int>> glides;          // cc -> Lane::glides
      std::map<int, int> lastTick;                  // cc -> the tick of its last point
      std::map<int, std::vector<std::pair<int, float>>> params;    // the event's index -> (units, value)
      };

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
              const LiveClips::Timeline& tl, const EventMap* written)
      {
      Layout out;
      out.bpm = tl.bpm;
      if (!score)
            return out;
      const int endUnits = tl.units(LiveClips::playedTicks(score));
      out.length = std::max(1, endUnits);

      // the tracks: section, part, patch and tuning (Live's routes, a Kontakt for each: a patch that doesn't bend has a
      // copy per tuning, tuned by its Tune; a bending patch's copies are tuned by their bends)
      const std::vector<SoundLib::Route> routes = SoundLib::liveRoutes(score, library);
      const std::map<const Part*, TrackDelays::Delays> delays = TrackDelays::read(score->masterScore());
      for (const SoundLib::Route& r : routes) {
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
                  s->parts.back().delays = TrackDelays::of(r.part, delays);
                  p = s->parts.end() - 1;
                  }
            Kontakt k;
            k.instrument = r.instrument;
            k.patch = r.instrument ? r.instrument->name : QString();
            const SoundLib::Library& rl = r.library ? *r.library : library;   // (several libraries: the route's own)
            k.gain = TrackDelays::patchGain(rl, p->delays, k.patch);
            k.faderGain = TrackDelays::faderGain(rl, p->delays, k.patch);
            k.gainStageDb = TrackDelays::gainStageDb(rl, p->delays, k.patch);
            k.trimDb = p->delays.trim;
            k.legacyGain = TrackDelays::legacyPatchGain(rl, p->delays, k.patch);
            k.port = r.port;
            k.channel = r.channel;
            k.patchIndex = r.patch;
            k.lane = r.lane;
            k.cents = r.cents;
            k.voice = r.voice;
            p->kontakts.push_back(k);
            }
      // (pointers once every vector has its size)
      std::map<int, Builder> builders;                                      // by its route
      for (Section& s : out.sections)
            for (PartTracks& p : s.parts)
                  for (Kontakt& k : p.kontakts) {
                        Builder& b = builders[k.port * 16 + k.channel];
                        b.kontakt = &k;
                        b.partRef = p.part;
                        b.part = p.name;
                        }

      // the events, route by route
      std::map<int, std::vector<std::pair<int, const NPlayEvent*>>> byRoute;
      for (const auto& te : events)
            if (te.second.isExternal())
                  byRoute[te.second.extPort() * 16 + te.second.extChannel()].push_back({ te.first, &te.second });
      // written (livetiming.h): the written render's events by route, its note-ons' keys, and each note's played times
      std::map<int, std::vector<std::pair<int, const NPlayEvent*>>> writtenByRoute;
      std::map<const void*, LiveTiming::Key> writtenKeys;
      std::map<LiveTiming::Key, LiveTiming::Span> playedTimes;
      if (written) {
            for (const auto& te : *written)
                  if (te.second.isExternal())
                        writtenByRoute[te.second.extPort() * 16 + te.second.extChannel()].push_back({ te.first, &te.second });
            LiveTiming::noteTimes(*written, &writtenKeys);
            playedTimes = LiveTiming::noteTimes(events);
            }
      for (const auto& r : byRoute) {
            auto bi = builders.find(r.first);
            if (bi == builders.end())
                  continue;
            Builder& b = bi->second;
            Kontakt& k = *b.kontakt;
            const SoundLib::LibInstrument* li = k.instrument;
            // a switch sets the switch in force (true); anything else doesn't
            auto setsSwitch = [li](const NPlayEvent& e, int& value) {
                  if (e.librarySwitch()) {
                        if (e.type() == ME_NOTEON && e.velo() > 0 && li && li->switchType == SoundLib::SwitchType::KEYSWITCH)
                              value = e.pitch();
                        else if (e.type() == ME_CONTROLLER)
                              value = e.value();
                        return true;
                        }
                  if (e.type() == ME_CONTROLLER && li
                      && ((li->switchType == SoundLib::SwitchType::CC && e.controller() == li->switchNumber)
                          || (li->switchType == SoundLib::SwitchType::PROGRAM && e.controller() == CTRL_PROGRAM))) {
                        value = e.value();
                        return true;
                        }
                  return false;
                  };
            // [slurs] speedFromWritten: the patch's speed controller (CC119) right before a note-on at its tick is that note's:
            // its technique clip's envelope (the note's Track Delay and timing), not the Controllers clip
            const int speedCC = SoundLib::speedFromWritten(li, score) ? li->speedCC : -1;
            auto speeds = [speedCC](const std::vector<std::pair<int, const NPlayEvent*>>& list) {
                  std::map<const NPlayEvent*, int> out;     // note-on -> value
                  if (speedCC < 0)
                        return out;
                  std::pair<int, int> last { -1, -1 };     // (tick, value)
                  for (const auto& item : list) {
                        const NPlayEvent& e = *item.second;
                        if (e.type() == ME_CONTROLLER && e.controller() == speedCC && !e.librarySwitch())
                              last = { item.first, e.value() };
                        else if (e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch() && last.first == item.first)
                              out[&e] = last.second;
                        }
                  return out;
                  };
            const std::map<const NPlayEvent*, int> speedOf = speeds(r.second);
            // a controller or bend right before a note-on at its tick (its track level's CC11, a dynamic, a lane's
            // bend) is that note's: its lane point moves by the note's technique's own delay, which the technique's
            // MIDI track plays (the Kontakt track has the patch's, the part group the part's), as the renderer moves
            // it (MidiRenderer::libraryTrackDelays)
            std::vector<std::pair<int, const NPlayEvent*>> items = r.second;
            auto pd = delays.find(b.partRef);
            if (pd != delays.end() && !pd->second.tracks.empty()) {
                  int sw = -1;
                  for (size_t i = 0; i < items.size(); ++i) {
                        const NPlayEvent& e = *items[i].second;
                        if (setsSwitch(e, sw) || (e.type() != ME_CONTROLLER && e.type() != ME_PITCHBEND))
                              continue;
                        int nsw = sw;
                        for (size_t j = i + 1; j < items.size() && items[j].first == items[i].first; ++j) {
                              const NPlayEvent& n = *items[j].second;
                              if (setsSwitch(n, nsw) || n.type() != ME_NOTEON || n.velo() == 0)
                                    continue;
                              const double ms = TrackDelays::own(pd->second, TrackDelays::trackKey(k.patch, techniqueName(li, nsw)));
                              if (ms != 0.0)
                                    items[i].first = std::max(0, score->utime2utick(score->utick2utime(items[i].first) + ms / 1000.0));
                              break;
                              }
                        }
                  std::stable_sort(items.begin(), items.end(), [](const std::pair<int, const NPlayEvent*>& a,
                                                                  const std::pair<int, const NPlayEvent*>& b) { return a.first < b.first; });
                  }
            // the technique of a switch value (made at its first note)
            auto techniqueFor = [&](int value) {
                  auto t = b.techniqueOf.find(value);
                  if (t == b.techniqueOf.end()) {
                        Technique tq;
                        tq.name = techniqueName(li, value);
                        tq.value = li && li->switchType != SoundLib::SwitchType::NONE ? value : -1;
                        k.techniques.push_back(tq);
                        t = b.techniqueOf.insert({ value, k.techniques.size() - 1 }).first;
                        }
                  return t->second;
                  };
            int value = -1;                               // the switch in force
            std::map<int, std::deque<std::pair<size_t, LiveClips::Note>>> open;   // pitch -> (technique, note)
            for (const auto& item : items) {
                  const NPlayEvent& e = *item.second;
                  const int at = tl.units(item.first);
                  const bool on = e.type() == ME_NOTEON && e.velo() > 0;
                  const bool off = e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0);
                  if (setsSwitch(e, value))
                        continue;
                  if (speedCC >= 0 && e.type() == ME_CONTROLLER && e.controller() == speedCC)
                        continue;
                  // (a point a tick after the lane's last one: a glide's step, Lane::glides)
                  auto lanePoint = [&](int cc, int v) {
                        std::vector<std::pair<int, int>>& pts = b.lanes[cc];
                        auto lt = b.lastTick.find(cc);
                        const bool glide = lt != b.lastTick.end() && lt->second == item.first - 1;
                        const size_t before = pts.size();
                        putPoint(pts, at, v);
                        if (!pts.empty() && pts.back().first == at) {
                              if (glide && pts.size() > before)
                                    b.glides[cc].insert(at);
                              else if (!glide)
                                    b.glides[cc].erase(at);
                              b.lastTick[cc] = item.first;
                              }
                        };
                  if (e.type() == ME_CONTROLLER) {
                        if (laneController(e.controller(), li))
                              lanePoint(e.controller(), e.value());
                        }
                  else if (e.type() == ME_PITCHBEND)
                        lanePoint(PITCH_BEND, (e.dataB() & 0x7f) << 7 | (e.dataA() & 0x7f));
                  else if (e.type() == ME_PARAMETER) {
                        const int pk = e.dataA() == LiveClips::LIVE_PARAM ? LiveClips::LIVE_PARAM_KEY + e.dataB() : e.dataA();
                        putPoint(b.params[pk], at, e.tuning());
                        }
                  else if (on) {
                        const size_t t = techniqueFor(value);
                        b.startsAt[at].insert(value);
                        if (written)
                              continue;               // (the notes as written, below)
                        LiveClips::Note n;
                        n.pitch = e.pitch();
                        n.start = at;
                        n.velocity = e.velo();
                        n.muted = voiceOff(e);
                        n.source = e.note();             // (Update Live Set: a timing edit in Live becomes its override)
                        n.utick = item.first;
                        auto sp = speedOf.find(&e);
                        if (sp != speedOf.end())
                              n.speed = sp->second;
                        open[n.pitch].push_back({ t, n });
                        }
                  else if (off && !written) {
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
            // written (livetiming.h): the notes as written, each with its played start and end
            if (written) {
                  auto wi = writtenByRoute.find(r.first);
                  if (wi == writtenByRoute.end())
                        continue;
                  int wvalue = -1;
                  const std::map<const NPlayEvent*, int> wspeedOf = speeds(wi->second);
                  std::map<int, std::deque<std::pair<size_t, LiveClips::Note>>> wopen;
                  for (const auto& item : wi->second) {
                        const NPlayEvent& e = *item.second;
                        const int at = tl.units(item.first);
                        if (setsSwitch(e, wvalue))
                              continue;
                        if (speedCC >= 0 && e.type() == ME_CONTROLLER && e.controller() == speedCC)
                              continue;
                        if (e.type() == ME_NOTEON && e.velo() > 0) {
                              LiveClips::Note n;
                              n.pitch = e.pitch();
                              n.start = at;
                              n.velocity = e.velo();
                              n.muted = voiceOff(e);
                              n.source = e.note();             // (Update Live Set: a timing edit in Live becomes its override)
                              n.utick = item.first;
                              auto sp = wspeedOf.find(&e);
                              if (sp != wspeedOf.end())
                                    n.speed = sp->second;
                              auto key = writtenKeys.find(&e);
                              auto span = key == writtenKeys.end() ? playedTimes.end() : playedTimes.find(key->second);
                              if (span != playedTimes.end()) {
                                    n.timed = true;
                                    n.playedStart = tl.units(span->second.on)
                                                    - int(std::lround(LiveTiming::msToUnits(span->second.earlyMs, tl.bpm)));
                                    n.playedEnd = tl.units(span->second.off);
                                    }
                              wopen[n.pitch].push_back({ techniqueFor(wvalue), n });
                              }
                        else if (e.type() == ME_NOTEOFF || e.type() == ME_NOTEON) {
                              auto o = wopen.find(e.pitch());
                              if (o == wopen.end() || o->second.empty())
                                    continue;
                              auto tn = o->second.front();
                              o->second.pop_front();
                              tn.second.length = std::max(1, at - tn.second.start);
                              k.techniques[tn.first].notes.push_back(tn.second);
                              }
                        }
                  for (auto& o : wopen)
                        for (auto tn : o.second) {
                              tn.second.length = std::max(1, endUnits - tn.second.start);
                              k.techniques[tn.first].notes.push_back(tn.second);
                              }
                  }
            }

      // the lanes, clashes; notes in order
      for (auto& bi : builders) {
            Builder& b = bi.second;
            Kontakt& k = *b.kontakt;
            // (every technique's note-ons on this Kontakt by key: a key struck again on another technique's track)
            std::multimap<int, int> keyOns;
            for (const Technique& t : k.techniques)
                  for (const LiveClips::Note& n : t.notes)
                        keyOns.insert({ n.pitch, n.timed ? n.playedStart : n.start });
            for (Technique& t : k.techniques) {
                  std::stable_sort(t.notes.begin(), t.notes.end());
                  if (written)
                        t.timing = LiveTiming::track(t.notes, tl.bpm, &keyOns);
                  }
            for (auto& l : b.lanes)
                  if (l.first != PITCH_BEND)
                        k.lanes.push_back({ l.first, l.second, b.glides[l.first] });
            auto bend = b.lanes.find(PITCH_BEND);
            if (bend != b.lanes.end())
                  k.lanes.push_back({ PITCH_BEND, bend->second, b.glides[PITCH_BEND] });
            // the parameter lanes: titled by the part's main patch's controllers (the renderer's index), or the
            // part's lanes on its Live track ("live:<d>/<p>")
            const SoundLib::LibInstrument* main = nullptr;
            QStringList live;
            for (const SoundLib::Route& r : routes)
                  if (r.part == b.partRef && r.patch == 0 && r.lane == 0)
                        main = r.instrument;
            if (b.partRef)
                  live = LiveClips::liveLanes(Automation::lanes(b.partRef, Automation::read(score->masterScore())));
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
//   multis
//    a part's Kontakts grouped by patch: a patch's tuning copies (its lanes, fixed Tune or bent) play from one
//    Kontakt, a slot per copy, each slot on its own MIDI channel (KontaktSetup::withSlots). Measured identical to
//    a Kontakt per copy (Kontakt 8 + SSO offline, 2026-10-10: All techniques nulls -111 dB, Performance with
//    bends -107.9 dB, run to run -109 dB; Live 12.4.6: MIDI To a channel plays its slot only), ~85-90 MB less per
//    copy. Copies share a Kontakt only when nothing tells them apart but the tuning: a copy with parameter
//    automation (Live automates the instrument, every slot at once) keeps a Kontakt of its own; the others share
//    one per headroom (the track's Volume), at most 16 (MIDI channels) in one.
//---------------------------------------------------------

std::vector<std::vector<size_t>> multis(const std::vector<Kontakt>& kontakts)
      {
      std::vector<std::vector<size_t>> byPatch;
      for (size_t i = 0; i < kontakts.size(); ++i) {
            bool found = false;
            for (std::vector<size_t>& g : byPatch) {
                  const Kontakt& f = kontakts[g.front()];
                  if (f.patchIndex == kontakts[i].patchIndex && f.patch == kontakts[i].patch) {
                        g.push_back(i);
                        found = true;
                        break;
                        }
                  }
            if (!found)
                  byPatch.push_back({ i });
            }
      std::vector<std::vector<size_t>> out;
      for (const std::vector<size_t>& g : byPatch) {
            // (a copy with parameter automation alone; the others by headroom, 16 at most in one)
            std::vector<std::vector<size_t>> merged;
            for (size_t i : g) {
                  bool found = false;
                  if (kontakts[i].params.empty()) {
                        for (std::vector<size_t>& m : merged) {
                              const Kontakt& f = kontakts[m.front()];
                              if (f.params.empty() && f.gain == kontakts[i].gain && m.size() < 16) {
                                    m.push_back(i);
                                    found = true;
                                    break;
                                    }
                              }
                        }
                  if (!found)
                        merged.push_back({ i });
                  }
            out.insert(out.end(), merged.begin(), merged.end());
            }
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
      // its last note). Live sends a clip's Bank / Sub only when they differ from what that track sent last (12.4.6: a
      // track's second Long clip switched nothing), so the runs alternate Bank 0 / 1 (SSO ignores CC0)
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
                  c.bank = int(i % 2);
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

static QString tuningOnlyName(const Kontakt& k);

// (a monophonic patch's voice copy, Route::voice: " (voice n)" after its tuning's name, so that two voices at one tuning
// have tracks of their own)
QString tuningName(const Kontakt& k)
      {
      if (k.voice <= 0)
            return tuningOnlyName(k);
      // (a voice's copy at a fixed tuning named as its voice's first: no "+0 cents"; a bending copy by its copy number)
      Kontakt named = k;
      if (!std::isnan(k.cents))
            named.lane = 0;
      return tuningOnlyName(named) + QString(" (%1 %2)").arg(QObject::tr("voice")).arg(k.voice + 1);
      }

static QString tuningOnlyName(const Kontakt& k)
      {
      if (std::isnan(k.cents))
            return k.lane == 0 ? QString() : QString(" (%1 %2)").arg(QObject::tr("copy")).arg(k.lane + 1);
      if (k.lane == 0 && std::fabs(k.cents) < 0.005)
            return QString();
      const double c = std::round(k.cents * 100.0) / 100.0;
      QString n = QString::number(std::fabs(c), 'f', std::fabs(c - std::round(c)) < 0.005 ? 0 : 2);
      return QString(" (%1%2 cents)").arg(c < 0 ? QString(QChar(0x2212)) : QString("+"), n);
      }

//---------------------------------------------------------
//   speedEnvelope
//    [slurs] speedFromWritten: the speed controller (CC119) of a technique's notes as an envelope of its clips, a step one
//    unit before each note as Live plays it (the controllers pass the MuseScore Link device at once, the notes it delays:
//    so with the device, written timing, the step is at the note's played start plus the track's lookahead; without it
//    at the written note, moved with it by the Track Delay). On the technique's track, not the Kontakt's Controllers
//    clip: it gets the note's own Track Delay (lookaheadMs < 0: no device)
//---------------------------------------------------------

static void speedEnvelope(const Kontakt& k, const Technique& tq, double lookaheadMs, double bpm, std::vector<LiveSetWriter::Clip>* clips)
      {
      if (!k.instrument || k.instrument->speedCC <= 0)
            return;
      std::vector<std::pair<int, int>> points;             // (units, value)
      const int look = lookaheadMs >= 0 ? int(std::lround(LiveTiming::msToUnits(lookaheadMs, bpm))) : 0;
      for (const LiveClips::Note& n : tq.notes) {
            if (n.speed < 0 || n.muted)
                  continue;
            const int base = lookaheadMs >= 0 ? (n.timed ? n.playedStart : n.start) + look : n.start;
            points.push_back({ std::max(0, base - 1), n.speed });
            }
      if (points.empty())
            return;
      std::stable_sort(points.begin(), points.end(), [](const std::pair<int, int>& a, const std::pair<int, int>& b) { return a.first < b.first; });
      bool sent = false;                  // (a step written: Live has sent a value on this track)
      for (LiveSetWriter::Clip& c : *clips) {
            const int from = int(std::lround(c.start * LiveClips::UNITS_PER_BEAT));
            const int to = int(std::lround(c.end * LiveClips::UNITS_PER_BEAT));
            LiveSetWriter::Clip::Envelope e;
            e.controller = k.instrument->speedCC;
            // (before the first: 0, as MuseScore plays a patch just loaded. Live sends a clip envelope's value only where it
            // changes: an envelope that starts at its first note's value sends nothing, and the slot plays the transitions at
            // the speed it had (real Live 12.4.7, Kontakt 8 + SSO, 2026-10-22, branch live-celli: Whence's Celli chord voices,
            // every transition at speed 0, so the copies' envelopes held 0 throughout, played +5.4 .. +7.6 dB louder for
            // ~0.4 s at each chord change, the new pitch ~0.3 s after the note-on where MuseScore takes ~0.48 s; the same
            // set with each copy's envelope stepping 1 -> 0 before its first note matched MuseScore: corr 0.895 -> 0.975,
            // null -6.5 -> -13.1 dB). So the track's first value is always a step, from 0, or from 1 when it is 0
            int value = 0;
            bool any = false;
            for (const auto& p : points) {
                  if (p.first >= to)
                        break;
                  if (p.first < from) {
                        value = p.second;
                        continue;
                        }
                  if (!sent && e.points.empty() && p.second == value)
                        value = p.second == 0 ? 1 : 0;   // (a value nothing has sent yet: the step sends it)
                  any = true;
                  if (e.points.empty())
                        e.points.push_back({ 0.0, double(value) });
                  if (p.second != value) {
                        const double at = beats(p.first - from);
                        e.points.push_back({ at, double(value) });
                        e.points.push_back({ at, double(p.second) });
                        value = p.second;
                        sent = true;
                        }
                  }
            if (any)
                  c.envelopes.push_back(e);
            }
      }

std::vector<LiveSetWriter::Track> tracks(const Layout& layout, bool link)
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
                  pg.delayMs = p.delays.ms;
                  pg.partRef = p.part;
                  out.push_back(pg);
                  const SoundLib::PartMix mix = SoundLib::partMix(p.part, false);
                  // a Kontakt's lanes as a "Controllers" clip
                  auto controllersClip = [&layout](const Kontakt& k, std::vector<LiveSetWriter::Clip>* clips) {
                        if (k.lanes.empty())
                              return;
                        LiveSetWriter::Clip c;
                        c.name = QObject::tr("Controllers");
                        c.end = beats(layout.length);
                        for (const Lane& l : k.lanes) {
                              // Live's clip bend envelope: -8192 .. 8191, MIDI's bend less 8192 (measured: Live 12.4.6
                              // with SSO's Violins 1 - Performance, bend= 99.1 cents, 2026-10-10: 4096, -4096 and 8191
                              // bent it +55, -50 and +103 cents, within the section's ±10-20 cents of spread)
                              const bool bend = l.cc == PITCH_BEND;
                              LiveSetWriter::Clip::Envelope e;
                              e.controller = bend ? LiveSetWriter::PITCH_BEND_ENVELOPE : l.cc;
                              auto value = [bend](int v) { return double(bend ? v - 8192 : v); };
                              // Live ramps linearly between a clip envelope's breakpoints (measured, Live 12.4.6,
                              // 2026-10-10: a bend 0.8 s between two points went 50 -> 0 cents in a straight line), so a
                              // step holds the value before up to it, then the new one at the same time; a glide's steps
                              // (a tick apart) are breakpoints only (the ramp between two is the step within a tick), and
                              // one on the line through its neighbours (to half a value unit: Live's ramp plays it) is
                              // left out
                              const std::vector<std::pair<int, int>>& pts = l.points;
                              auto onLine = [&pts](size_t from, size_t to) {         // (every point between on the line)
                                    for (size_t m = from + 1; m < to; ++m) {
                                          const double line = pts[from].second + double(pts[to].second - pts[from].second)
                                                * (pts[m].first - pts[from].first) / double(pts[to].first - pts[from].first);
                                          if (std::fabs(line - pts[m].second) > 0.5)
                                                return false;
                                          }
                                    return true;
                                    };
                              for (size_t i = 0; i < pts.size(); ) {
                                    if (i > 0)
                                          e.points.push_back({ beats(pts[i].first), value(pts[i - 1].second) });
                                    e.points.push_back({ beats(pts[i].first), value(pts[i].second) });
                                    // the glide's steps after it (a run): the ends of its straight stretches
                                    size_t k = i;
                                    while (k + 1 < pts.size() && l.glides.count(pts[k + 1].first))
                                          ++k;
                                    size_t anchor = i;
                                    while (anchor < k) {
                                          size_t j = anchor + 1;
                                          while (j + 1 <= k && onLine(anchor, j + 1))
                                                ++j;
                                          e.points.push_back({ beats(pts[j].first), value(pts[j].second) });
                                          anchor = j;
                                          }
                                    i = k + 1;
                                    }
                              c.envelopes.push_back(e);
                              }
                        if (!c.envelopes.empty())
                              clips->push_back(c);
                        };
                  // a patch's tuning copies (its lanes) share one Kontakt, a slot each (multi() below)
                  std::vector<std::vector<size_t>> groups = multis(p.kontakts);
                  for (const std::vector<size_t>& g : groups) {
                        const Kontakt& k = p.kontakts[g.front()];
                        LiveSetWriter::Track kt;
                        kt.name = LiveSetWriter::trackName(p.name, k.patch, k.patchIndex == 0, 0) + tuningName(k);
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
                        kt.lane = k.lane;
                        kt.cents = k.cents;
                        kt.annotation = trackKey({ s.name, p.name, kt.name });
                        const LiveSetWriter::KontaktGains kg = LiveSetWriter::kontaktGains(mix.volume, k.faderGain, k.gainStageDb);
                        kt.volume = kg.volume;
                        kt.utility = kg.hasUtility;
                        kt.utilityGain = kg.utility;
                        kt.utilityInfo = LiveSetWriter::utilityInfo(k.gainStageDb, k.trimDb);
                        kt.legacyVolume = LiveSetWriter::kontaktVolume(mix.volume, k.legacyGain);
                        kt.pan = LiveSetWriter::mixPan(mix.pan);
                        kt.active = !mix.muted;
                        kt.delayKey = TrackDelays::trackKey(k.patch);
                        kt.delayMs = TrackDelays::own(p.delays, kt.delayKey);
                        // ([slurs] plainPerformance: the patch's shift on its Kontakt track, every event of the patch early by
                        // it as MuseScore plays them; recorded as the track's lookahead, so reading the set back takes it off)
                        kt.lookaheadMs = -TrackDelays::plainMs(p.delays, k.patch);
                        kt.delayMs -= kt.lookaheadMs;
                        controllersClip(k, &kt.clips);
                        for (const ParamLane& pl : k.params) {
                              LiveSetWriter::ParameterAutomation a;
                              a.name = pl.title;
                              // (steps, as the clip's envelopes: Live ramps between breakpoints)
                              for (size_t i = 0; i < pl.points.size(); ++i) {
                                    if (i > 0)
                                          a.points.push_back({ beats(pl.points[i].first), double(pl.points[i - 1].second) });
                                    a.points.push_back({ beats(pl.points[i].first), double(pl.points[i].second) });
                                    }
                              kt.automation.push_back(a);
                              }
                        if (g.size() > 1) {
                              for (size_t i : g)
                                    kt.slotCents.push_back(p.kontakts[i].cents);
                              }
                        const int kontaktIndex = int(out.size());
                        out.push_back(kt);
                        for (size_t slot = 0; slot < g.size(); ++slot) {
                              const Kontakt& kc = p.kontakts[g[slot]];
                              const int channel = g.size() > 1 ? int(slot) + 1 : 0;
                              QString name = out[size_t(kontaktIndex)].name;
                              if (slot > 0) {
                                    // the copy's own MIDI track: its Controllers (bends, CC1 …) on its slot's channel
                                    LiveSetWriter::Track ct;
                                    ct.name = LiveSetWriter::trackName(p.name, kc.patch, kc.patchIndex == 0, 0) + tuningName(kc);
                                    name = ct.name;
                                    ct.link = false;
                                    ct.tuningCopy = true;
                                    ct.color = color;
                                    ct.groupIndex = partIndex;
                                    ct.midiTo = kontaktIndex;
                                    ct.midiToChannel = channel;
                                    ct.part = p.name;
                                    ct.partRef = p.part;
                                    ct.patch = kc.patch;
                                    ct.lane = kc.lane;
                                    ct.cents = kc.cents;
                                    controllersClip(kc, &ct.clips);
                                    ct.annotation = trackKey({ s.name, p.name, ct.name });
                                    out.push_back(ct);
                                    }
                              for (size_t i = 0; i < kc.techniques.size(); ++i) {
                                    LiveSetWriter::Track tt;
                                    tt.name = QString("%1 – %2").arg(name, kc.techniques[i].name);
                                    tt.link = link;
                                    tt.color = color;
                                    tt.groupIndex = partIndex;
                                    tt.midiTo = kontaktIndex;
                                    tt.midiToChannel = channel;
                                    tt.part = p.name;
                                    tt.partRef = p.part;
                                    tt.clips = switchClips(kc, i, layout.length);
                                    tt.delayKey = TrackDelays::trackKey(kc.patch, kc.techniques[i].name);
                                    tt.delayMs = TrackDelays::own(p.delays, tt.delayKey);
                                    // written notes (livetiming.h): Live's lookahead on the track, the device's delays
                                    // into place on its MuseScore Link copy; without the device the most common early
                                    // start alone (each note as written, moved by it)
                                    const LiveTiming::Track& timing = kc.techniques[i].timing;
                                    tt.lookaheadMs = link ? timing.lookaheadMs : timing.commonEarlyMs;
                                    tt.delayMs -= tt.lookaheadMs;
                                    if (link && timing.any())
                                          tt.linkTiming = LiveTiming::record(timing);
                                    speedEnvelope(kc, kc.techniques[i], link ? tt.lookaheadMs : -1.0, layout.bpm, &tt.clips);
                                    for (const LiveClips::Note& n : kc.techniques[i].notes)
                                          if (!n.muted)
                                                tt.sourceNotes.push_back({ n.pitch, n.start, n.length, n.velocity, n.source, n.utick });
                                    tt.annotation = trackKey({ s.name, p.name, name, kc.techniques[i].name });
                                    out.push_back(tt);
                                    }
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
