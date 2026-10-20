//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

//---------------------------------------------------------
//   Guitar notation on a guitar library (branch ample-phase2, 2026-10-20): MidiRenderer::libraryGuitar
//
//   A pass over a chunk's events of the parts whose patch has a <Guitar> (soundlibrary.h Guitar: Ample Guitar M, keys and
//   rules measured 2026-10-18, MIDI 60 = Ample's C3), after every other library pass (finishLibraryEvents: the events
//   are routed, delayed and capped already, so the keys it adds keep their exact velocities, e.g. 127 forcing a string).
//   From the notation MuseScore 3 has:
//   - capo (staff text's capo setting, Staff::capo; ppitch includes it): the capo parameter (<Guitar capo>, plug-in
//     output only) at the chunk's start (whenever the staff has a capo text) and at each change, every note's key
//     sounding - capo (Ample keeps the fingering: it transposes what it gets). Ample applies a change from its next
//     process block, so a change goes capoLead ms before its first note (not before the route's previous note-on), and
//     a capo at the start plays the score capoLead later (the start lead, guitarStartLeadMs). Not when the part has a
//     lane on that parameter (the lane is the user's);
//   - strums ([guitar] strums): a chord with a straight arpeggio (a strum line; stretch <= 1: a stretched one is an
//     arpeggio, its notes spread as MuseScore plays them) plays the strummer: CC7 (the strum as loud as its notes at
//     their velocity: Guitar::strumLevel by the part's sample set, on the CC7 in force), Detect mode, strum mode on, the
//     chord's notes as chord keys (the lowest note's pitch class from chordLow, the others above it), the strum key
//     (arpeggio up = low to high = Ample's down strum; muted keys when every note has a cross notehead) at strumVelocity;
//     at its end the keys off and strum mode off, before anything else at that tick; the CC7 in force again right
//     before the route's next note that is not a strum (channel gain: a strum's level also changes what still rings).
//     [guitar] softStrums: a strum at velocity <= softStrum plays as its notes rolled rollGap ms apart in strum order;
//   - artificial harmonics: a chord of fretted notes and diamond notes whose distance is listed (Guitar::artificial):
//     only the diamond notes sound, each as a natural harmonic (phase 1 chose the harmonic keyswitch for a diamond)
//     at the stopped note + the partial's semitones (key: that + harmonicShift);
//   - pinch harmonics ([guitar] pinchHarmonics): a chord under staff text "P.H." / "pinch harm." at its tick: the
//     harmonic keyswitch and the note as a natural harmonic at its partial (a diamond note's pitch, else fretted +
//     pinchPartial), then pinchDelay ms later palm mute and the fretted note at pinchVelocity for pinchOff ms;
//   - slides: a chord line (plop / scoop: slide in, the key before the note; fall / doit: slide out, the key
//     slideOut ms before the note's end, while it is held), a glissando line between two notes (the first note alone,
//     without MuseScore's steps, held into the second by overlap ms, the legato slide key before the second);
//   - hammer-on / pull-off: a single note in a legato slur after a single note of another pitch (not tied), or under
//     staff text "H" / "P" / "HO" / "PO" at its tick: the note before held into it by overlap ms, the hammer key before it;
//   - string forcing: a note with a string (a tablature note's, also on its linked staff; else a string number text
//     1-6) gets its string's key at stringVelocity right before it (not for strums or harmonics);
//   - FX noises: staff text naming one (Guitar::fx) plays its key at its tick, velocity 100, fxLength ms;
//   - sample sets: staff text "pick" / "strum" / "finger" (TextTechniques: modifiers pick / strum) chooses the map's
//     extra patch for that set (gen_ample.py: one per other set, its own state controller), loaded as an instance of
//     its own where used; a strum's level takes its route's set;
//   then each note of the renderer whose articulation keyswitch an added keyswitch replaced gets it again right before
//   it (Ample's keyswitches latch: measured 2026-10-18, meas3; finishLibraryEvents dropped it as already selected).
//   Palm mute (P.M. line, "muted" text) and natural harmonics are the map's articulations (phase 1). Pop is left: no
//   measured rule says when the notation asks for it.
//---------------------------------------------------------

#include <set>

#include "arpeggio.h"
#include "automation.h"
#include "chord.h"
#include "chordline.h"
#include "fingering.h"
#include "glissando.h"
#include "instrument.h"
#include "measure.h"
#include "navigate.h"
#include "note.h"
#include "part.h"
#include "partcontrollers.h"
#include "playbacksettings.h"
#include "rendermidi.h"
#include "score.h"
#include "segment.h"
#include "slur.h"
#include "soundlibrary.h"
#include "staff.h"
#include "stafftextbase.h"
#include "stringdata.h"
#include "tempo.h"
#include "tie.h"

#include "audio/midi/event.h"

namespace Ms {

// the capo parameter's index in li's allControllers when the pass sends it (plug-in output or Live's clips, no lane of
// the part on it: the lane is the user's), else -1
static int capoParamIndex(const Part* part, const SoundLib::LibInstrument* li,
                          const std::map<const Part*, Automation::PartLanes>& lanes, bool forLiveClips)
      {
      if (!li || !li->guitar || li->guitar->capo.isEmpty()
          || !(SoundLib::output() == SoundLib::Output::PLUGIN || forLiveClips))
            return -1;
      for (int i = 0; i < int(li->allControllers.size()); ++i) {
            const SoundLib::Controller& c = li->allControllers[size_t(i)];
            if (c.id != li->guitar->capo || c.param.isEmpty())
                  continue;
            if (lanes.find(part) != lanes.end())
                  for (const Automation::Lane& l : Automation::lanes(part, lanes))
                        if (l.target == c.id)
                              return -1;
            return i;
            }
      return -1;
      }

// a capo staff text anywhere on the staff (also "capo 0": fretId 1)
static bool staffHasCapo(const Score* score, const Staff* staff)
      {
      for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest))
            for (const Element* e : seg->annotations())
                  if (e->isStaffTextBase() && e->staff() == staff && toStaffTextBase(e)->capo() > 0)
                        return true;
      return false;
      }

//---------------------------------------------------------
//   guitarStartLeadMs
//    the capo in force at the score's start goes out at tick 0 (the instance may still hold another one); a guitar
//    note within capoLead of the start would share Ample's process block with it and play at the old capo, so the
//    score plays later by the rest of capoLead (the start lead, libraryDelayLead: everything else moves, the capo
//    parameter at 0 stays). Not for Live's clips (no start lead there)
//---------------------------------------------------------

double MidiRenderer::guitarStartLeadMs() const
      {
      if (forLiveClips || libParts.empty())
            return 0.0;
      double lead = 0.0;
      std::map<const Part*, Automation::PartLanes> lanes;
      bool lanesRead = false;
      for (const auto& pl : libParts) {
            const SoundLib::LibInstrument* li = pl.second.route.instrument;
            if (!li || !li->guitar || li->guitar->capoLeadMs <= 0.0 || pl.first->staves()->empty())
                  continue;
            if (!lanesRead) {
                  lanes = Automation::read(score->masterScore());
                  lanesRead = true;
                  }
            const Staff* staff = pl.first->staves()->front();
            if (capoParamIndex(pl.first, li, lanes, forLiveClips) < 0 || !staffHasCapo(score, staff))
                  continue;
            // the part's first chord
            for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                  bool found = false;
                  for (int t = pl.first->startTrack(); t < pl.first->endTrack() && !found; ++t)
                        found = seg->element(t) && seg->element(t)->isChord();
                  if (found) {
                        lead = std::max(lead, li->guitar->capoLeadMs - score->utick2utime(seg->tick().ticks()) * 1000.0);
                        break;
                        }
                  }
            }
      return std::max(0.0, lead);
      }

void MidiRenderer::libraryGuitar(const Chunk& chunk, EventMap* events)
      {
      if (libRoutes.empty())
            return;
      struct GuitarPart {
            const Part* part;
            const SoundLib::Guitar* g;
            std::vector<QString> sampleSets;      // by patch (libraryPatch): an extra patch is another instance's set
            int capoParam { -1 };         // the capo controller's index in allControllers, -1: none / not sent
            };
      std::map<int, GuitarPart> byChannel;          // the part's MIDI channel (as rendered, before routing)
      std::map<const Part*, Automation::PartLanes> lanes;
      bool lanesRead = false;
      for (const auto& pl : libParts) {
            const SoundLib::LibInstrument* li = pl.second.route.instrument;
            if (!li || !li->guitar)
                  continue;
            if (!lanesRead) {
                  lanes = Automation::read(score->masterScore());
                  lanesRead = true;
                  }
            GuitarPart gp { pl.first, li->guitar.get(), {}, -1 };
            for (int i = 0; i < int(li->allControllers.size()); ++i) {
                  const SoundLib::Controller& c = li->allControllers[size_t(i)];
                  if (c.id == gp.g->sampleSet && c.isState() && !c.stateValues.isEmpty()) {
                        // no value (the main patch without a lane): the plug-in's own, the map's patchDefault (Finger)
                        const int n = c.stateValues.size();
                        const QString own = c.stateValues[qBound(0, int(std::lround(qMax(0.0, c.patchDefault) * (n - 1))), n - 1)];
                        for (const SoundLib::LibInstrument* patch : pl.second.patches) {
                              const auto values = PartControllers::stateValues(pl.first, *patch, lanes);
                              const auto v = values.find(c.state);
                              gp.sampleSets.push_back(v != values.end() ? QString::fromUtf8(v->second) : own);
                              }
                        }
                  }
            gp.capoParam = capoParamIndex(pl.first, li, lanes, forLiveClips);
            for (const auto& ip : *pl.first->instruments()) {
                  const int ch = ip.second->channel(0)->channel();
                  if (libRoutes.count(ch))
                        byChannel.emplace(ch, gp);
                  }
            }
      if (byChannel.empty())
            return;

      const bool strums = Playback::on("guitar/strums", score);
      const bool softStrums = Playback::on("guitar/softStrums", score);
      const bool pinch = Playback::on("guitar/pinchHarmonics", score);
      const int utick1 = chunk.utick1();
      const int utick2 = chunk.utick2();
      const int offset = chunk.tickOffset();
      auto msTicks = [&](int utick, double ms) {
            const double bps = score->tempomap()->tempo(qMax(0, utick - offset));
            return qMax(1, int(std::lround(ms / 1000.0 * bps * DIVISION)));
            };
      auto sameRoute = [](const NPlayEvent& a, const NPlayEvent& b) {
            return a.extPort() == b.extPort() && a.extChannel() == b.extChannel();
            };

      std::set<const NPlayEvent*> mine;             // the events this pass added
      std::set<const NPlayEvent*> mySwitches;       // … of them, articulation keyswitches
      std::set<const NPlayEvent*> dropped;          // the renderer's events it takes out (erased at the end)
      std::set<const NPlayEvent*> switched;         // the renderer's note-ons whose keyswitch it set
      auto make = [](const NPlayEvent& like, int type, int a, int b) {
            NPlayEvent ev(uchar(type), uchar(like.channel()), uchar(a), uchar(b));
            ev.setExternal(like.extPort(), like.extChannel());
            ev.setLibraryPatch(like.libraryPatch());
            ev.setOriginatingStaff(like.getOriginatingStaff());
            return ev;
            };
      // at the end of the events at tick (after what is there), or right before hint
      auto add = [&](int tick, const NPlayEvent& ev) {
            auto i = events->insert(events->upper_bound(tick), std::make_pair(tick, ev));
            mine.insert(&i->second);
            return i;
            };
      // (hint: the event it goes right before at that event's tick, or the first event after tick / end())
      auto addBefore = [&](EventMap::iterator hint, const NPlayEvent& ev, int tick = -1) {
            if (tick < 0)
                  tick = hint->first;
            auto i = events->insert(hint, std::make_pair(tick, ev));
            mine.insert(&i->second);
            return i;
            };
      auto addFirst = [&](int tick, const NPlayEvent& ev) {      // before everything at tick
            auto i = events->insert(events->lower_bound(tick), std::make_pair(tick, ev));
            mine.insert(&i->second);
            return i;
            };
      // a key tapped (note-on, then its note-off) right before hint; an articulation keyswitch is marked as one
      auto tapBefore = [&](EventMap::iterator hint, const NPlayEvent& like, int key, int velocity, bool articulation, int tick = -1) {
            auto on = addBefore(hint, make(like, ME_NOTEON, key, velocity), tick);
            addBefore(hint, make(like, ME_NOTEON, key, 0), tick);
            if (articulation)
                  mySwitches.insert(&on->second);
            };
      auto tapAt = [&](int tick, const NPlayEvent& like, int key, int velocity, bool articulation) {
            auto on = add(tick, make(like, ME_NOTEON, key, velocity));
            add(tick, make(like, ME_NOTEON, key, 0));
            if (articulation)
                  mySwitches.insert(&on->second);
            };

      // the renderer's notes of the chunk on the guitar parts, each with its note-off
      struct On {
            EventMap::iterator on;
            EventMap::iterator off;
            const Note* note;
            const GuitarPart* gp;
            bool strummed { false };
            bool harmonic { false };            // played as a harmonic (artificial / pinch): no string key
            };
      std::vector<On> ons;
      for (auto i = events->lower_bound(utick1); i != events->end() && i->first < utick2; ++i) {
            const NPlayEvent& ev = i->second;
            if (ev.type() != ME_NOTEON || ev.velo() == 0 || ev.librarySwitch() || !ev.note() || !ev.isExternal())
                  continue;
            auto g = byChannel.find(ev.channel());
            if (g == byChannel.end())
                  continue;
            On o { i, events->end(), ev.note(), &g->second };
            for (auto j = std::next(i); j != events->end(); ++j) {
                  const NPlayEvent& e = j->second;
                  if (e.type() == ME_NOTEON && e.velo() == 0 && e.note() == ev.note() && e.pitch() == ev.pitch() && sameRoute(e, ev)) {
                        o.off = j;
                        break;
                        }
                  }
            ons.push_back(o);
            }
      if (ons.empty())
            return;
      auto drop = [&](EventMap::iterator i) {
            if (i != events->end())
                  dropped.insert(&i->second);
            };
      auto isDropped = [&](const On& o) { return dropped.count(&o.on->second) > 0; };
      auto offTick = [&](const On& o) {
            return o.off != events->end() ? o.off->first : o.on->first + o.note->chord()->actualTicks().ticks();
            };
      // a note's end moved to tick (a new note-off; the old one dropped)
      auto moveOff = [&](On& o, int tick) {
            NPlayEvent off = o.off != events->end() ? o.off->second : o.on->second;
            off.setVelo(0);
            drop(o.off);
            o.off = events->insert(events->lower_bound(tick), std::make_pair(tick, off));
            };
      auto onsOf = [&](const Chord* chord) {
            std::vector<On*> v;
            for (On& o : ons)
                  if (o.note->chord() == chord && !isDropped(o))
                        v.push_back(&o);
            return v;
            };
      auto onOf = [&](const Note* note) -> On* {
            for (On& o : ons)
                  if (o.note == note && !isDropped(o))
                        return &o;
            return nullptr;
            };
      std::vector<const Chord*> chords;             // in the order of their first note
      for (const On& o : ons)
            if (std::find(chords.begin(), chords.end(), o.note->chord()) == chords.end())
                  chords.push_back(o.note->chord());
      auto sampleSetOf = [](const On& o) {
            const auto& sets = o.gp->sampleSets;
            const int p = o.on->second.libraryPatch();
            return p >= 0 && p < int(sets.size()) ? sets[size_t(p)] : QString();
            };
      auto capoOf = [](const Note* n) {
            const int c = n->staff()->capo(n->chord()->tick());
            return c > 0 ? c - 1 : 0;
            };
      auto staffTextAt = [](const Chord* c, const QRegularExpression& re) {
            if (c->isGrace() || !c->segment())
                  return false;
            for (const Element* e : c->segment()->annotations()) {
                  if (e->isStaffTextBase() && e->staffIdx() == c->staffIdx()
                      && re.match(toStaffTextBase(e)->plainText().simplified()).hasMatch())
                        return true;
                  }
            return false;
            };

      // capo: the parameter, and every note's key relative to it. Ample applies a change from its next process block
      // (Guitar::capoLeadMs): a change goes capoLead before the first note under it, not before the route's previous
      // note-on (a ringing note keeps its pitch, a note starting after the change takes it); at the chunk's start the
      // capo in force whenever the staff has a capo text anywhere (an instance keeps the last one it got: playing
      // again from the start after "capo 2" would otherwise stay at 2), at 0 kept there by the start lead
      // (guitarStartLeadMs), at a chunk boundary where it changes capoLead before
      {
      std::map<int, int> sent;                    // channel -> capo sent in this chunk
      std::map<const Staff*, bool> anyCapo;
      for (size_t k = 0; k < ons.size(); ++k) {
            On& o = ons[k];
            const int capo = capoOf(o.note);
            if (capo > 0) {
                  o.on->second.setPitch(qMax(0, o.on->second.pitch() - capo));
                  if (o.off != events->end())
                        o.off->second.setPitch(qMax(0, o.off->second.pitch() - capo));
                  }
            if (o.gp->capoParam < 0)
                  continue;
            const int ch = o.on->second.channel();
            const Staff* staff = o.note->staff();
            auto ac = anyCapo.find(staff);
            if (ac == anyCapo.end())
                  ac = anyCapo.emplace(staff, staffHasCapo(score, staff)).first;
            auto s = sent.find(ch);
            if (s == sent.end() && !ac->second)
                  continue;
            const int lead = o.gp->g->capoLeadMs > 0.0 ? msTicks(o.on->first, o.gp->g->capoLeadMs) : 0;
            auto value = [&](int c) { return float(qBound(0, c, o.gp->g->capoMax)) / o.gp->g->capoMax; };
            int floor = utick1;                   // a change not before the chunk's own capo event
            if (s == sent.end()) {
                  const int startFret = staff->capo(Fraction::fromTicks(utick1 - offset));
                  const int start = startFret > 0 ? startFret - 1 : 0;
                  NPlayEvent p = make(o.on->second, ME_PARAMETER, o.gp->capoParam, 0);
                  p.setTuning(value(start));
                  if (utick1 == 0) {
                        p.setLibraryDelayed(true);      // (stays at 0: the start lead moves the rest)
                        addFirst(0, p);
                        }
                  else {
                        const int beforeFret = staff->capo(Fraction::fromTicks(qMax(0, utick1 - offset - 1)));
                        if ((beforeFret > 0 ? beforeFret - 1 : 0) != start && lead > 0)
                              floor = qMax(0, utick1 - lead);
                        addFirst(floor, p);
                        }
                  s = sent.emplace(ch, start).first;
                  }
            if (s->second != capo) {
                  NPlayEvent p = make(o.on->second, ME_PARAMETER, o.gp->capoParam, 0);
                  p.setTuning(value(capo));
                  const int on = o.on->first;
                  // the route's previous note-on (before this chord's)
                  int previous = -1;
                  for (size_t j = k; j-- > 0;) {
                        if (ons[j].on->first < on && sameRoute(ons[j].on->second, o.on->second)) {
                              previous = ons[j].on->first;
                              break;
                              }
                        }
                  const int at = std::min(on, std::max({ on - lead, previous + 1, floor }));
                  if (at >= on)
                        addFirst(on, p);              // (no room: before the note, as close as it gets)
                  else
                        add(at, p);
                  s->second = capo;
                  }
            }
      }

      // strums
      struct Strum { const NPlayEvent* like; int t0; int end; int base; int cc; };
      std::vector<Strum> strumList;
      std::set<const Chord*> strumChords;
      for (const Chord* chord : chords) {
            if (!strums || chord->notes().size() < 2)
                  continue;
            Arpeggio* a = const_cast<Chord*>(chord)->arpeggio();
            if (!a || !a->playArpeggio() || a->Stretch() > 1.0 + 1e-9
                || (a->arpeggioType() != ArpeggioType::UP_STRAIGHT && a->arpeggioType() != ArpeggioType::DOWN_STRAIGHT))
                  continue;
            std::vector<On*> group = onsOf(chord);
            if (group.size() < 2)
                  continue;
            const SoundLib::Guitar& g = *group.front()->gp->g;
            const bool down = a->arpeggioType() == ArpeggioType::UP_STRAIGHT;
            bool muted = true;
            int t0 = std::numeric_limits<int>::max(), end = 0, v = 0;
            for (On* o : group) {
                  muted = muted && o->note->headGroup() == NoteHead::Group::HEAD_CROSS;
                  t0 = std::min(t0, o->on->first);
                  end = std::max(end, offTick(*o));
                  v = std::max(v, o->on->second.velo());
                  }
            if (softStrums && v <= g.softStrum) {
                  // a roll in strum order, rollGap apart
                  std::sort(group.begin(), group.end(), [down](const On* x, const On* y) {
                        return down ? x->note->ppitch() < y->note->ppitch() : x->note->ppitch() > y->note->ppitch();
                        });
                  const int gap = msTicks(t0, g.rollGapMs);
                  for (int k = 0; k < int(group.size()); ++k) {
                        On* o = group[size_t(k)];
                        const int t = t0 + k * gap;
                        if (t == o->on->first || t >= offTick(*o))
                              continue;
                        NPlayEvent ev = o->on->second;
                        drop(o->on);
                        o->on = events->insert(events->upper_bound(t), std::make_pair(t, ev));
                        }
                  for (On* o : group)
                        o->strummed = true;           // (no string keys: the roll stands for a strum)
                  continue;
                  }
            const int key = muted ? (down ? g.strumDownMuted : g.strumUpMuted) : (down ? g.strumDown : g.strumUp);
            if (key < 0 || g.detect < 0 || g.strumMode < 0)
                  continue;
            const NPlayEvent like = group.front()->on->second;
            // the CC7 in force on the route (the part's Volume), else unity
            int base = 64;
            for (auto i = events->lower_bound(t0); i != events->begin();) {
                  --i;
                  const NPlayEvent& e = i->second;
                  if (e.type() == ME_CONTROLLER && e.controller() == CTRL_VOLUME && sameRoute(e, like) && !mine.count(&e)) {
                        base = e.value();
                        break;
                        }
                  }
            const int cc = qBound(1, int(std::lround(base * g.strumLevel(sampleSetOf(*group.front()), v) / 64.0)), 127);
            std::set<int> keys;
            int low = 128;
            for (On* o : group)
                  low = std::min(low, o->note->ppitch());
            const int root = g.chordLow + (low % 12);
            for (On* o : group)
                  keys.insert(root + ((o->note->ppitch() - low) % 12 + 12) % 12);
            for (On* o : group) {
                  o->strummed = true;
                  drop(o->on);
                  drop(o->off);
                  }
            auto first = add(t0, make(like, ME_CONTROLLER, CTRL_VOLUME, cc));
            tapAt(t0, like, g.detect, g.detectVelocity, false);
            tapAt(t0, like, g.strumMode, g.strumOn, false);
            for (int k : keys)
                  add(t0, make(like, ME_NOTEON, k, 100));
            add(t0, make(like, ME_NOTEON, key, g.strumVelocity));
            // at its end, before the notes there
            auto at = events->lower_bound(end);
            for (int k : keys)
                  addBefore(at, make(like, ME_NOTEON, k, 0), end);
            addBefore(at, make(like, ME_NOTEON, key, 0), end);
            tapBefore(at, like, g.strumMode, g.strumOff, false, end);
            strumList.push_back({ &first->second, t0, end, base, cc });
            strumChords.insert(chord);
            }

      // artificial and pinch harmonics
      static const QRegularExpression pinchText("^(p\\.? ?h\\.?|pinch( harm(\\.|onics?)?)?)$", QRegularExpression::CaseInsensitiveOption);
      for (const Chord* chord : chords) {
            if (strumChords.count(chord))
                  continue;
            std::vector<On*> group = onsOf(chord);
            if (group.empty())
                  continue;
            const SoundLib::Guitar& g = *group.front()->gp->g;
            if (g.harmonic < 0)
                  continue;
            const Note* stopped = nullptr;
            const Note* diamond = nullptr;
            for (const Note* n : chord->notes()) {
                  const bool d = n->headGroup() == NoteHead::Group::HEAD_DIAMOND || n->headGroup() == NoteHead::Group::HEAD_DIAMOND_OLD;
                  if (d && !diamond)
                        diamond = n;
                  if (!d && (!stopped || n->ppitch() < stopped->ppitch()))
                        stopped = n;
                  }
            if (pinch && g.palmMute >= 0 && stopped && staffTextAt(chord, pinchText)) {
                  for (On* o : group) {
                        if (o->note == diamond || (diamond && o->note != stopped)) {
                              if (o->note != stopped) {     // (a diamond note gives the partial; it doesn't sound itself)
                                    drop(o->on);
                                    drop(o->off);
                                    }
                              continue;
                              }
                        const int capo = capoOf(o->note);
                        const int target = diamond ? diamond->ppitch() : o->note->ppitch() + g.pinchPartial;
                        const int nhKey = target + g.harmonicShift - capo;
                        if (nhKey < 0 || nhKey > 127)
                              continue;
                        const int fretKey = o->note->ppitch() - capo;
                        const NPlayEvent like = o->on->second;
                        const int t = o->on->first;
                        tapBefore(o->on, like, g.harmonic, 100, true);
                        switched.insert(&o->on->second);
                        o->on->second.setPitch(nhKey);
                        if (o->off != events->end())
                              o->off->second.setPitch(nhKey);
                        o->harmonic = true;
                        const int t2 = t + msTicks(t, g.pinchDelayMs);
                        tapAt(t2, like, g.palmMute, 100, true);
                        add(t2, make(like, ME_NOTEON, fretKey, g.pinchVelocity));
                        const int t3 = t2 + msTicks(t2, g.pinchOffMs);
                        addFirst(t3, make(like, ME_NOTEON, fretKey, 0));
                        }
                  continue;
                  }
            if (!stopped || !diamond || g.artificial.empty())
                  continue;
            bool any = false;
            for (On* o : group) {
                  const Note* n = o->note;
                  if (n->headGroup() != NoteHead::Group::HEAD_DIAMOND && n->headGroup() != NoteHead::Group::HEAD_DIAMOND_OLD)
                        continue;
                  const int shift = g.artificialShift(n->ppitch() - stopped->ppitch());
                  if (!shift)
                        continue;
                  const int key = stopped->ppitch() + shift + g.harmonicShift - capoOf(n);
                  if (key < 0 || key > 127)
                        continue;
                  o->on->second.setPitch(key);
                  if (o->off != events->end())
                        o->off->second.setPitch(key);
                  o->harmonic = true;
                  any = true;
                  }
            if (any)
                  for (On* o : group)
                        if (!o->harmonic) {
                              drop(o->on);
                              drop(o->off);
                              }
            }

      // slides in / out (chord lines)
      for (const Chord* chord : chords) {
            if (strumChords.count(chord))
                  continue;
            std::vector<On*> group = onsOf(chord);
            if (group.empty() || group.front()->gp->g->slide < 0)
                  continue;
            const SoundLib::Guitar& g = *group.front()->gp->g;
            for (const Element* e : chord->el()) {
                  if (!e->isChordLine())
                        continue;
                  const ChordLineType type = toChordLine(e)->chordLineType();
                  if (type == ChordLineType::PLOP || type == ChordLineType::SCOOP) {
                        On* first = *std::min_element(group.begin(), group.end(), [](const On* x, const On* y) { return x->on->first < y->on->first; });
                        tapBefore(first->on, first->on->second, g.slide, 100, true);
                        for (On* o : group)
                              switched.insert(&o->on->second);
                        }
                  else if (type == ChordLineType::FALL || type == ChordLineType::DOIT) {
                        int end = 0, start = 0;
                        for (On* o : group) {
                              end = std::max(end, offTick(*o));
                              start = std::max(start, o->on->first);
                              }
                        const int t = std::max(start + 1, end - msTicks(end, g.slideOutMs));
                        if (t < end)
                              tapAt(t, group.front()->on->second, g.slide, 100, true);
                        }
                  }
            }

      // glissando lines: legato slides
      for (On& b : ons) {
            if (isDropped(b) || b.strummed || b.gp->g->legatoSlide < 0)
                  continue;
            const Note* to = b.note;
            const Note* from = nullptr;
            for (const Spanner* sp : to->spannerBack())
                  if (sp->isGlissando() && sp->startElement() && sp->startElement()->isNote())
                        from = toNote(sp->startElement());
            if (!from)
                  continue;
            On* a = nullptr;
            for (On& o : ons) {                     // its first note-on; MuseScore's steps after it go
                  if (o.note != from || isDropped(o))
                        continue;
                  if (!a || o.on->first < a->on->first)
                        a = &o;
                  }
            if (!a || a->strummed || a->on->first >= b.on->first)
                  continue;
            for (On& o : ons)
                  if (o.note == from && &o != a) {
                        drop(o.on);
                        drop(o.off);
                        }
            moveOff(*a, b.on->first + msTicks(b.on->first, a->gp->g->overlapMs));
            tapBefore(b.on, b.on->second, b.gp->g->legatoSlide, 100, true);
            switched.insert(&b.on->second);
            }

      // hammer-ons / pull-offs
      static const QRegularExpression hoText("^(h|p|ho|po|h\\.o\\.|p\\.o\\.)$", QRegularExpression::CaseInsensitiveOption);
      for (On& b : ons) {
            if (isDropped(b) || b.strummed || b.harmonic || b.gp->g->hammer < 0 || switched.count(&b.on->second))
                  continue;
            Chord* cb = const_cast<Chord*>(b.note->chord());
            if (cb->notes().size() != 1 || cb->isGrace())
                  continue;
            ChordRest* prev = prevChordRest(cb, true);
            if (!prev || !prev->isChord() || toChord(prev)->notes().size() != 1)
                  continue;
            const Note* an = toChord(prev)->notes().front();
            if (an->ppitch() == b.note->ppitch() || an->tieFor())
                  continue;
            bool slurred = false;
            const int ta = prev->tick().ticks();
            const int tb = cb->tick().ticks();
            for (const auto& iv : score->spannerMap().findOverlapping(ta, tb)) {
                  const Spanner* sp = iv.value;
                  if (sp->isSlur() && toSlur(sp)->isLegatoSlur() && sp->track() == cb->track()
                      && sp->tick().ticks() <= ta && sp->tick2().ticks() >= tb)
                        slurred = true;
                  }
            if (!slurred && !staffTextAt(cb, hoText))
                  continue;
            On* a = onOf(an);
            if (!a || a->strummed || a->on->first >= b.on->first)
                  continue;
            const int until = b.on->first + msTicks(b.on->first, b.gp->g->overlapMs);
            if (offTick(*a) < until)
                  moveOff(*a, until);
            tapBefore(b.on, b.on->second, b.gp->g->hammer, 1, true);    // (low velocity: the hand stays in position)
            switched.insert(&b.on->second);
            }

      // string forcing
      for (On& o : ons) {
            if (isDropped(o) || o.strummed || o.harmonic)
                  continue;
            const Note* n = o.note;
            if (n->headGroup() == NoteHead::Group::HEAD_DIAMOND || n->headGroup() == NoteHead::Group::HEAD_DIAMOND_OLD)
                  continue;
            int string = -1;                        // 0: string 1 (the highest)
            const StringData* sd = n->part()->instrument(n->chord()->tick())->stringData();
            if (n->string() >= 0 && n->fret() >= 0 && sd && sd->strings() == 6)
                  string = n->string();
            for (const Element* e : n->el()) {
                  if (e->isFingering() && toFingering(e)->tid() == Tid::STRING_NUMBER) {
                        bool ok = false;
                        const int s = toFingering(e)->plainText().trimmed().toInt(&ok);
                        if (ok && s >= 1 && s <= 6)
                              string = s - 1;
                        }
                  }
            if (string < 0 || string > 5 || o.gp->g->stringKeys[size_t(string)] < 0)
                  continue;
            tapBefore(o.on, o.on->second, o.gp->g->stringKeys[size_t(string)], o.gp->g->stringVelocity, false);
            }

      // FX noise texts
      for (Segment* s = chunk.startMeasure()->first(SegmentType::ChordRest); s && s->tick().ticks() < chunk.tick2();
           s = s->next1(SegmentType::ChordRest)) {
            for (const Element* e : s->annotations()) {
                  if (!e->isStaffTextBase())
                        continue;
                  for (const auto& bc : byChannel) {
                        if (e->part() != bc.second.part)
                              continue;
                        const QString text = toStaffTextBase(e)->plainText().simplified();
                        for (const SoundLib::Guitar::Fx& f : bc.second.g->fx) {
                              if (!f.text.match(text).hasMatch())
                                    continue;
                              const auto& outs = libRoutes.at(bc.first);
                              if (outs.empty() || outs.front().empty())
                                    continue;
                              NPlayEvent like(ME_NOTEON, uchar(bc.first), 0, 0);
                              like.setExternal(outs.front().front().first, outs.front().front().second);
                              like.setOriginatingStaff(e->staffIdx());
                              const int t = s->tick().ticks() + offset;
                              add(t, make(like, ME_NOTEON, f.key, 100));
                              const int t2 = t + msTicks(t, bc.second.g->fxMs);
                              addFirst(t2, make(like, ME_NOTEON, f.key, 0));
                              }
                        break;
                        }
                  }
            }

      // after a strum, the CC7 in force again before the route's next note that is not a strum
      for (const Strum& st : strumList) {
            if (st.cc == st.base)
                  continue;
            EventMap::iterator next = events->end();
            bool nextStrum = false;
            for (auto i = events->upper_bound(st.t0); i != events->end(); ++i) {
                  const NPlayEvent& e = i->second;
                  if (!sameRoute(e, *st.like))
                        continue;
                  if (e.type() == ME_CONTROLLER && e.controller() == CTRL_VOLUME && mine.count(&e) && i->first > st.t0) {
                        nextStrum = true;
                        break;
                        }
                  if (e.type() == ME_NOTEON && e.velo() > 0 && e.note() && !dropped.count(&e) && !mine.count(&e)) {
                        next = i;
                        break;
                        }
                  }
            if (nextStrum)
                  continue;
            NPlayEvent v = make(*st.like, ME_CONTROLLER, CTRL_VOLUME, st.base);
            if (next != events->end()) {
                  // before its keyswitch and string key too, if any at its tick
                  auto at = next;
                  while (at != events->begin()) {
                        auto p = std::prev(at);
                        if (p->first != next->first || !sameRoute(p->second, *st.like) || p->second.type() != ME_NOTEON
                            || (p->second.note() && !p->second.librarySwitch()))
                              break;
                        at = p;
                        }
                  addBefore(at, v);
                  }
            else
                  add(std::max(st.end, utick2 - 1), v);
            }

      // the renderer's keyswitch again where an added one replaced it (keyswitches latch)
      {
      std::map<int, int> rendered, actual;          // route -> the renderer's keyswitch in force / the one sent last
      for (auto i = events->lower_bound(utick1); i != events->end(); ++i) {
            const NPlayEvent& e = i->second;
            if (e.type() != ME_NOTEON || e.velo() == 0 || !e.isExternal() || dropped.count(&e) || !byChannel.count(e.channel()))
                  continue;
            const int route = e.extPort() * 16 + e.extChannel();
            if (e.librarySwitch()) {
                  rendered[route] = actual[route] = e.pitch();
                  continue;
                  }
            if (mySwitches.count(&e)) {
                  actual[route] = e.pitch();
                  continue;
                  }
            if (!e.note() || mine.count(&e) || switched.count(&e))
                  continue;
            if (i->first >= utick2)
                  break;
            auto r = rendered.find(route);
            auto a = actual.find(route);
            if (r != rendered.end() && a != actual.end() && r->second != a->second) {
                  // before the note's string key, if any
                  auto at = i;
                  while (at != events->begin()) {
                        auto p = std::prev(at);
                        if (p->first != i->first || !mine.count(&p->second) || mySwitches.count(&p->second)
                            || !sameRoute(p->second, e) || p->second.type() != ME_NOTEON)
                              break;
                        at = p;
                        }
                  const NPlayEvent like = e;
                  auto on = events->insert(at, std::make_pair(i->first, make(like, ME_NOTEON, r->second, 100)));
                  events->insert(at, std::make_pair(i->first, make(like, ME_NOTEON, r->second, 0)));
                  on->second.setLibrarySwitch(true);
                  a->second = r->second;
                  }
            }
      }

      for (auto i = events->lower_bound(utick1 - 1); i != events->end() && !dropped.empty();) {
            if (dropped.erase(&i->second))
                  i = events->erase(i);
            else
                  ++i;
            }
      // (a dropped event before the chunk: none are; the search above starts at its first note)
      for (auto i = events->begin(); i != events->end() && !dropped.empty();) {
            if (dropped.erase(&i->second))
                  i = events->erase(i);
            else
                  ++i;
            }
      }

} // namespace Ms
