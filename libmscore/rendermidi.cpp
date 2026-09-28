//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2012 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

/**
 \file
 render score into event list
*/

#include <set>

#include "arpeggio.h"
#include "articulation.h"
#include "bend.h"
#include "changeMap.h"
#include "chord.h"
#include "durationtype.h"
#include "dynamic.h"
#include "easeInOut.h"
#include "glissando.h"
#include "hairpin.h"
#include "instrument.h"
#include "measure.h"
#include "navigate.h"
#include "note.h"
#include "noteevent.h"
#include "part.h"
#include "rendermidi.h"
#include "repeat.h"
#include "repeatlist.h"
#include "score.h"
#include "segment.h"
#include "slur.h"
#include "staff.h"
#include "stafftextbase.h"
#include "style.h"
#include "sym.h"
#include "synthesizerstate.h"
#include "tempo.h"
#include "tie.h"
#include "tremolo.h"
#include "trill.h"
#include "undo.h"
#include "utils.h"
#include "vibrato.h"
#include "partcontrollers.h"
#include "partplayback.h"
#include "tuning.h"
#include "volta.h"

#include "global/log.h"

#include "audio/midi/event.h"

namespace Ms {

    //int printNoteEventLists(NoteEventList el, int prefix, int j){
    //    int k=0;
    //    for (NoteEvent event : el) {
    //        qDebug("%d: %d: %d pitch=%d ontime=%d duration=%d",prefix, j, k, event.pitch(), event.ontime(), event.len());
    //        k++;
    //    }
    //    return 0;
    //}
    //int printNoteEventLists(QList<NoteEventList> ell, int prefix){
    //    int j=0;
    //    for (NoteEventList el : ell) {
    //        printNoteEventLists(el,prefix,j);
    //        j++;
    //    }
    //    return 0;
    //}

struct SndConfig {
      bool useSND = false;
      int controller = -1;
      DynamicsRenderMethod method = DynamicsRenderMethod::SEG_START;
      // DynamicsRenderMethod::MS4: the note as MuseScore 4 plays it (Ms4::note)
      bool ms4 = false;
      int ms4Velocity = 64;
      int ms4Dur = Ms4::HUNDRED;
      int ms4Ts = 0;
      int ms4Offset = 0;          // ticks the note starts late (arpeggio, grace notes before), taken off its length
      int ms4Cut = 0;             // ticks taken off the note's end (grace notes after)
      int ms4Layer = -1;          // FluidSynth channel layer (-1: the note's voice)
      int ms4TiedTicks = -1;      // what the tied notes add to the note (-1: their whole length)
      int ms4SwingOn = 0;         // swing: on-time offset, per mille of the chord's length
      int ms4SwingGate = 100;     // swing: the chord's length, percent
      bool ms4Once = false;       // the note once, as written, whatever MuseScore 3's play events are
      int libPatch = 0;           // a sound library part: the patch that plays the note
      int libOverlap = 0;         // ticks the note lasts into the next (a library's legato)
      int libKey = -1;            // a library kit: the patch's key that plays the drum sound
                                  // (a sound library plays the trill / tremolo: SoundLib)

      SndConfig() {}
      SndConfig(bool use, int c, DynamicsRenderMethod me) : useSND(use), controller(c), method(me) {}
      };

bool graceNotesMerged(Chord *chord);
bool glissandoPitchOffsets(const Spanner* spanner, std::vector<int>& pitchOffsets);

//---------------------------------------------------------
//   ms4SameRepeat
//    notesInSameRepeat: whether a tied-to note plays in the same pass of the unrolled score as
//    the note the tie comes from (a tie into a first ending does not carry on into the second)
//---------------------------------------------------------

static bool ms4SameRepeat(const Score* score, const Note* first, const Note* second, int tickOffset)
      {
      const RepeatList& repeats = score->repeatList();
      if (repeats.size() == 1)
            return true;
      auto it = repeats.findRepeatSegmentFromUTick(first->chord()->tick().ticks() + tickOffset);
      if (it == repeats.end())
            return true;
      const int t = second->chord()->tick().ticks();
      return (*it)->tick <= t && t < (*it)->tick + (*it)->len();
      }

//---------------------------------------------------------
//   ms4EndPause
//    pauseUs: the pause (breath, caesura; a section break's at the end of its pass) at a note's
//    end, in seconds -- MS4 leaves it out of the note's length
//---------------------------------------------------------

static qreal ms4EndPause(const Score* score, int endUtick)
      {
      const RepeatList& repeats = score->repeatList();
      auto it = repeats.findRepeatSegmentFromUTick(endUtick - 1);
      if (it == repeats.end())
            return 0.0;
      const RepeatSegment* rs = *it;
      qreal pause = 0.0;
      auto e = score->tempomap()->find(endUtick - (rs->utick - rs->tick));
      if (e != score->tempomap()->end())
            pause += e->second.pause;
      if (endUtick == rs->utick + rs->len())
            pause += rs->pause;
      return pause;
      }

//---------------------------------------------------------
//   ms4PartialTieIncoming
//    findIncomingNoteInNextRepeat: a tie from the last note of a pass carries on into the next
//    pass's first note of that pitch if a tie leads into it (e.g. into a coda that starts tied)
//---------------------------------------------------------

static const Note* ms4PartialTieIncoming(const Score* score, const Note* outgoing, int tickOffset, int& nextOffset)
      {
      const RepeatList& repeats = score->repeatList();
      const int utick = outgoing->chord()->tick().ticks() + tickOffset;
      auto it = repeats.findRepeatSegmentFromUTick(utick);
      if (it == repeats.end())
            return nullptr;
      if (utick != (*it)->utick + (*it)->len() - outgoing->chord()->actualTicks().ticks())
            return nullptr;                   // not the pass's last note
      auto next = it + 1;
      if (next == repeats.end() || !(*next)->firstMeasure())
            return nullptr;
      const int track = outgoing->track();
      for (Segment* s = (*next)->firstMeasure()->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            Element* e = s->element(track);
            if (!e)
                  continue;
            if (!e->isChord())
                  return nullptr;
            const Note* incoming = toChord(e)->findNote(outgoing->pitch());
            if (!incoming || !incoming->tieBack())
                  return nullptr;
            nextOffset = (*next)->utick - (*next)->tick;
            return incoming;
            }
      return nullptr;
      }

//---------------------------------------------------------
//   ms4DiscreteGlissando
//    the steps of a note's discrete (non-portamento) glissando, as MuseScore 4 plays them
//    (Glissando::pitchSteps); empty if the note has none
//---------------------------------------------------------

static std::vector<int> ms4DiscreteGlissando(const Note* note)
      {
      std::vector<int> steps;
      for (Spanner* sp : note->spannerFor()) {
            if (sp->isGlissando()) {
                  if (!glissandoPitchOffsets(sp, steps))
                        steps.clear();
                  break;
                  }
            }
      return steps;
      }

//---------------------------------------------------------
//   updateSwing
//---------------------------------------------------------

void Score::updateSwing()
      {
      for (Staff* s : qAsConst(_staves)) {
            s->clearSwingList();
            }
      Measure* fm = firstMeasure();
      if (!fm)
            return;
      for (Segment* s = fm->first(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (const Element* e : s->annotations()) {
                  if (!e->isStaffTextBase())
                        continue;
                  const StaffTextBase* st = toStaffTextBase(e);
                  if (st->xmlText().isEmpty())
                        continue;
                  Staff* staff = st->staff();
                  if (!st->swing())
                        continue;
                  SwingParameters sp;
                  sp.swingRatio = st->swingParameters()->swingRatio;
                  sp.swingUnit = st->swingParameters()->swingUnit;
                  if (st->systemFlag()) {
                        for (Staff* sta : qAsConst(_staves)) {
                              sta->insertIntoSwingList(s->tick(),sp);
                              }
                        }
                  else
                        staff->insertIntoSwingList(s->tick(),sp);
                  }
            }
      }

//---------------------------------------------------------
//   updateCapo
//---------------------------------------------------------

void Score::updateCapo()
      {
      for (Staff* s : qAsConst(_staves)) {
            s->clearCapoList();
            }
      Measure* fm = firstMeasure();
      if (!fm)
            return;
      for (Segment* s = fm->first(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (Element* e : s->annotations()) {
                  if (e->isHarmony()) {
                        toHarmony(e)->realizedHarmony().setDirty(true);
                        }
                  if (!e->isStaffTextBase())
                        continue;
                  const StaffTextBase* st = toStaffTextBase(e);
                  if (st->xmlText().isEmpty())
                        continue;
                  Staff* staff = st->staff();
                  if (st->capo() == 0)
                        continue;
                  staff->insertIntoCapoList(s->tick(),st->capo());
                  }
            }
      }

//---------------------------------------------------------
//   updateChannel
//---------------------------------------------------------

void Score::updateChannel()
      {
      for (Staff*& s : staves()) {
            for (int i = 0; i < VOICES; ++i)
                  s->clearChannelList(i);
            }
      Measure* fm = firstMeasure();
      if (!fm)
            return;
      for (Segment* s = fm->first(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (const Element* e : s->annotations()) {
                  if (e->isInstrumentChange()) {
                        for (Staff*& staff : *e->part()->staves()) {
                              for (int voice = 0; voice < VOICES; ++voice)
                                    staff->insertIntoChannelList(voice, s->tick(), 0);
                              }
                        continue;
                        }
                  if (!e->isStaffTextBase())
                        continue;
                  const StaffTextBase* st = toStaffTextBase(e);
                  for (int voice = 0; voice < VOICES; ++voice) {
                        QString an(st->channelName(voice));
                        if (an.isEmpty())
                              continue;
                        Staff* staff = Score::staff(st->staffIdx());
                        int a = staff->part()->instrument(s->tick())->channelIdx(an);
                        if (a != -1)
                              staff->insertIntoChannelList(voice, s->tick(), a);
                        }
                  }
            }

      for (auto it = spanner().cbegin(); it != spanner().cend(); ++it) {
            Spanner* spanner = (*it).second;
            if (!spanner->isVolta())
                  continue;
            Volta* volta = toVolta(spanner);
            volta->setChannel();
            }

      for (Segment* s = fm->first(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (Staff*& st : staves()) {
                  int strack = st->idx() * VOICES;
                  int etrack = strack + VOICES;
                  for (int track = strack; track < etrack; ++track) {
                        if (!s->element(track))
                              continue;
                        Element* e = s->element(track);
                        if (e->type() != ElementType::CHORD)
                              continue;
                        Chord* c = toChord(e);
                        int channel = st->channel(c->tick(), c->voice());
                        Instrument* instr = c->part()->instrument(c->tick());
                        if (channel >= instr->channel().size()) {
                              qDebug() << "Channel " << channel << " too high. Max " << instr->channel().size();
                              channel = 0;
                              }
                        for (Note* note : c->notes()) {
                              if (note->hidden())
                                    continue;
                              note->setSubchannel(channel);
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   Converts midi time (noteoff - noteon) to milliseconds
//---------------------------------------------------------
int toMilliseconds(float tempo, float midiTime) {
      float ticksPerSecond = (float)DIVISION * tempo;
      int time = (int)((midiTime / ticksPerSecond) * 1000.0f);
      if (time > 0x7fff) //maximum possible value
            time = 0x7fff;
      return time;
      }

//---------------------------------------------------------
//   Detects if a note is a start of a glissando
//---------------------------------------------------------
bool isGlissandoFor(const Note* note) {
      for (Spanner* spanner : note->spannerFor())
            if (spanner->type() == ElementType::GLISSANDO)
                  return true;
      return false;
      }

//---------------------------------------------------------
//   playNote
//---------------------------------------------------------
//---------------------------------------------------------
//   ms3PitchBend
//    MuseScore 3's bend value p (100 = a whole tone) as a 14-bit pitch wheel value. The synth
//    uses MuseScore 4's pitch wheel range of 24 semitones (it was 12 in MuseScore 3).
//---------------------------------------------------------

static int ms3PitchBend(int p)
      {
      return qBound(0, (p * 8192) / 1200 + 8192, 16383);
      }

static void playNote(EventMap* events, const Note* note, int channel, int pitch,
   int velo, int onTime, int offTime, int staffIdx, int layer = -1, int libPatch = 0)
      {
      if (!note->play())
            return;
      // a note's own velocity (Inspector: user value / offset); MS4 mode (layer >= 0) plays none,
      // as MuseScore 4 ignores MuseScore 3's note velocities when it reads a score
      if (layer < 0)
            velo = note->customizeVelocity(velo);
      NPlayEvent ev(ME_NOTEON, channel, pitch, velo);
      ev.setOriginatingStaff(staffIdx);
      ev.setLayer(layer);
      ev.setLibraryPatch(libPatch);
      ev.setTuning(playbackTuning(note));
      ev.setNote(note);
      if (offTime < onTime)
            offTime = onTime;
      events->insert(std::pair<int, NPlayEvent>(onTime, ev));
      // adds portamento for continuous glissando (MuseScore 3; MS4 notes bend by their pitch curve)
      for (Spanner* spanner : layer < 0 ? note->spannerFor() : QVector<Spanner*>()) {
            if (spanner->type() == ElementType::GLISSANDO) {
                  Glissando *glissando = toGlissando(spanner);
                  if (glissando->glissandoStyle() == GlissandoStyle::PORTAMENTO) {
                        Note* nextNote = toNote(spanner->endElement());
                        double pitchDelta = (static_cast<double>(nextNote->ppitch()) - pitch) * 50.0;
                        double timeDelta = static_cast<double>(offTime - onTime);
                        if (!qFuzzyIsNull(pitchDelta) && !qFuzzyIsNull(timeDelta)) {
                              double timeStep = std::abs(timeDelta / pitchDelta * 20.0);
                              double t = 0.0;
                              QList<int> onTimes;
                              EaseInOut easeInOut(static_cast<qreal>(glissando->easeIn()) / 100.0,
                                    static_cast<qreal>(glissando->easeOut()) / 100.0);
                              easeInOut.timeList(static_cast<int>((timeDelta + timeStep * 0.5) / timeStep), int(timeDelta), &onTimes);
                              double nTimes = static_cast<double>(onTimes.size() - 1);
                              for (int& time : onTimes) {
                                    int p = static_cast<int>((t / nTimes) * pitchDelta);
                                    int timeStamp = std::min(onTime + time, offTime - 1);
                                    int midiPitch = ms3PitchBend(p);
                                    NPlayEvent evb(ME_PITCHBEND, channel, midiPitch % 128, midiPitch / 128);
                                    evb.setOriginatingStaff(staffIdx);
                                    events->insert(std::pair<int, NPlayEvent>(timeStamp, evb));
                                    t += 1.0;
                                    }
                              ev.setVelo(0);
                              events->insert(std::pair<int, NPlayEvent>(offTime, ev));
                              NPlayEvent evb(ME_PITCHBEND, channel, 0, 64); // 0:64 is 8192 - no pitch bend
                              evb.setOriginatingStaff(staffIdx);
                              events->insert(std::pair<int, NPlayEvent>(offTime, evb));
                              return;
                              }
                        }
                  }
            }

      ev.setVelo(0);
      events->insert(std::pair<int, NPlayEvent>(offTime, ev));
      }

//---------------------------------------------------------
//   collectNote
//---------------------------------------------------------

static void collectNote(EventMap* events, int channel, const Note* note, qreal velocityMultiplier, int tickOffset, Staff* staff, SndConfig config)
      {
      if (!note->play() || (note->hidden() && !config.ms4))      // MS3: do not play overlapping notes; MS4 plays both
            return;
      Chord* chord = note->chord();

      int staffIdx = staff->idx();
      int ticks;
      int tieLen = 0;
      if (chord->isGrace()) {
            Q_ASSERT( !graceNotesMerged(chord)); // this function should not be called on a grace note if grace notes are merged
            chord = toChord(chord->parent());
            }

      ticks = chord->actualTicks().ticks(); // ticks of the actual note
      // calculate additional length due to ties forward
      // taking NoteEvent length adjustments into account
      // but stopping at any note with multiple NoteEvents
      // and processing those notes recursively
      if (note->tieFor() && !config.ms4) {       // MS4 mode walks the tie chain itself (below)
            Note* n = note->tieFor()->endNote();
            while (n) {
                  NoteEventList nel = n->playEvents();
                  if (nel.size() == 1 && !isGlissandoFor(n)) {
                        // add value of this note to main note
                        // if we wish to suppress first note of ornament,
                        // then do this regardless of number of NoteEvents
                        tieLen += (n->chord()->actualTicks().ticks() * (nel[0].len())) / 1000;
                        }
                  else {
                        // recurse
                        collectNote(events, channel, n, velocityMultiplier, tickOffset, staff, config);
                        break;
                        }
                  if (n->tieFor() && n != n->tieFor()->endNote())
                        n = n->tieFor()->endNote();
                  else
                        break;
                  }
            }

      int tick1    = chord->tick().ticks() + tickOffset;
      bool tieFor  = note->tieFor();
      bool tieBack = note->tieBack();

      NoteEventList nel = note->playEvents();
      int nels = nel.size();

      // MuseScore 4: a plain note (one play event, not edited in the Piano Roll) and the notes tied
      // to it are one note of the chain's whole length, scaled by the articulations' duration factor
      if (config.ms4 && (nels >= 1 || config.ms4Once)) {   // MS4 plays no stored play events (MS3's ornaments, Piano Roll edits, MuseScore 2 events)
            if (tieBack)
                  return;                         // played with the note the tie comes from
            int chainTicks = ticks;
            for (const Note* n = note; config.ms4TiedTicks < 0 && n->tieFor() && n->tieFor()->endNote(); ) {
                  const Note* next = n->tieFor()->endNote();
                  if (next == n || !ms4SameRepeat(note->score(), note, next, tickOffset))
                        break;
                  if (isGlissandoFor(next)) {
                        const std::vector<int> steps = ms4DiscreteGlissando(next);
                        if (!steps.empty())
                              chainTicks += next->chord()->actualTicks().ticks() / int(steps.size());
                        break;
                        }
                  chainTicks += next->chord()->actualTicks().ticks();
                  n = next;
                  }
            if (config.ms4TiedTicks >= 0)
                  chainTicks = ticks + config.ms4TiedTicks;
            int p = config.libKey >= 0 ? config.libKey : qBound(0, note->ppitch(), 127);   // MS4: no play-event pitch offsets
            const int offset = qMin(config.ms4Offset, ticks);
            // swing on the chord's own length only, not on the tied notes' (NoteRenderer::applySwingIfNeed)
            const int swungTicks = (ticks * config.ms4SwingGate) / 100 + (chainTicks - ticks);
            int on  = tick1 + (ticks * config.ms4SwingOn) / 1000 + offset + ((ticks - offset) * config.ms4Ts) / Ms4::HUNDRED;
            int off = on + int((qint64(swungTicks - offset - config.ms4Cut) * config.ms4Dur) / Ms4::HUNDRED);   // MS4 ends the note there (MS3 one tick early)
            {
                  // MS4 takes the share of the note's time, not of its ticks: where the tempo
                  // changes under the note (rit., fermata) the end falls elsewhere
                  Score* sc = note->score();
                  const int n0 = on - ((ticks - offset) * config.ms4Ts) / Ms4::HUNDRED;
                  const int n1 = n0 + swungTicks - offset - config.ms4Cut;
                  const qreal t0 = sc->utick2utime(n0);
                  const qreal span = sc->utick2utime(n1) - t0 - ms4EndPause(sc, n1);
                  const qreal slope = sc->utick2utime(n0 + 1) - t0;
                  if (n1 > n0 && qAbs(span - slope * (n1 - n0)) > 1e-6)
                        off = sc->utime2utick(sc->utick2utime(on) + span * config.ms4Dur / Ms4::HUNDRED);
            }
            off += config.libOverlap;
            playNote(events, note, channel, p, qBound(1, config.ms4Velocity, 127), on, qMax(on, off), staffIdx, config.ms4Layer >= 0 ? config.ms4Layer : note->voice(), config.libPatch);
            nels = 0;                             // done; bends below still apply
            }
      for (int i = 0, pitch = note->ppitch(); i < nels; ++i) {
            const NoteEvent& e = nel[i]; // we make an explicit const ref, not a const copy.  no need to copy as we won't change the original object.

            // skip if note has a tie into it and only one NoteEvent
            // its length was already added to previous note
            // if we wish to suppress first note of ornament
            // then change "nels == 1" to "i == 0", and change "break" to "continue"
            if (tieBack && nels == 1 && !isGlissandoFor(note))
                  break;
            int p = pitch + e.pitch();
            if (p < 0)
                  p = 0;
            else if (p > 127)
                  p = 127;
            int on  = tick1 + (ticks * e.ontime())/1000;
            int off = on + (ticks * e.len())/1000 - 1;
            if (tieFor && i == nels - 1)
                  off += tieLen;

            // Get the velocity used for this note from the staff
            // This allows correct playback of tremolos even without SND enabled.
            int velo;
            Fraction nonUnwoundTick = Fraction::fromTicks(on - tickOffset);
            if (config.ms4) {
                  playNote(events, note, channel, p, qBound(1, config.ms4Velocity, 127), on, off, staffIdx, config.ms4Layer >= 0 ? config.ms4Layer : note->voice(), config.libPatch);
                  continue;
                  }
            if (config.useSND) {
                  switch (config.method) {
                        case DynamicsRenderMethod::FIXED_MAX:
                              velo = 127;
                              break;
                        case DynamicsRenderMethod::SEG_START:
                        default:
                              velo = staff->velocities().val(nonUnwoundTick);
                              break;
                        }
                  }
            else {
                  velo = staff->velocities().val(nonUnwoundTick);
                  }

            velo *= velocityMultiplier;
            playNote(events, note, channel, p, qBound(1, velo, 127), on, off, staffIdx);
            }

      // Single-note dynamics
      // Find any changes, and apply events
      if (config.useSND && !config.ms4) {
            ChangeMap& veloEvents = staff->velocities();
            ChangeMap& multEvents = staff->velocityMultiplications();
            Fraction stick = chord->tick();
            Fraction etick = stick + chord->ticks();
            auto changes = veloEvents.changesInRange(stick, etick);
            auto multChanges = multEvents.changesInRange(stick, etick);

            std::map<int, int> velocityMap;
            for (auto& change : changes) {
                  int lastVal = -1;
                  int endPoint = change.second.ticks();
                  for (int t = change.first.ticks(); t <= endPoint; t++) {
                        int velo = veloEvents.val(Fraction::fromTicks(t));
                        if (velo == lastVal)
                              continue;
                        lastVal = velo;

                        velocityMap[t] = velo;
                        }
                  }


            qreal CONVERSION_FACTOR = MidiRenderer::ARTICULATION_CONV_FACTOR;
            for (auto& change : multChanges) {
                  // Ignore fix events: they are available as cached ramp starts
                  // and considering them ends up with multiplying twice effectively
                  if (change.first == change.second)
                        continue;

                  int lastVal = MidiRenderer::ARTICULATION_CONV_FACTOR;
                  int endPoint = change.second.ticks();
                  int lastVelocity = 0;
                  auto lastValocityIt = velocityMap.upper_bound(change.first.ticks());
                  if (lastValocityIt != velocityMap.end())
                        lastVelocity = lastValocityIt->second;
                  else if (!velocityMap.empty())
                        lastVelocity = velocityMap.cbegin()->second;

                  for (int t = change.first.ticks(); t <= endPoint; t++) {
                        int mult = multEvents.val(Fraction::fromTicks(t));
                        if (mult == lastVal || mult == CONVERSION_FACTOR)
                              continue;
                        lastVal = mult;

                        qreal realMult = mult / CONVERSION_FACTOR;
                        if (velocityMap.find(t) != velocityMap.end()) {
                              lastVelocity = velocityMap[t];
                              velocityMap[t] *= realMult;
                              }
                        else {
                              velocityMap[t] = lastVelocity * realMult;
                              }
                        }
                  }


            for (auto point = velocityMap.cbegin(); point != velocityMap.cend(); ++point) {
                  // NOTE:JT if we ever want to use poly aftertouch instead of CC, this is where we want to
                  // be using it. Instead of ME_CONTROLLER, use ME_POLYAFTER (but duplicate for each note in chord)
                  NPlayEvent event = NPlayEvent(ME_CONTROLLER, channel, config.controller, qBound(0, point->second, 127));
                  event.setOriginatingStaff(staffIdx);
                  events->insert(std::make_pair(point->first + tickOffset, event));
                  }
            }

      // Bends (MuseScore 3's; MS4 plays a score's old bends straight -- only its own guitar
      // bends bend)
      for (Element* e : note->el()) {
            if (config.ms4)
                  break;
            if (e == 0 || e->type() != ElementType::BEND)
                  continue;
            Bend* bend = toBend(e);
            if (!bend->playBend())
                  break;
            const QList<PitchValue>& points = bend->points();
            int pitchSize = points.size();

            double noteLen = note->playTicks();
            int lastPointTick = tick1;
            int lastPitch = INT_MAX;
            for (int pitchIndex = 0; pitchIndex < pitchSize-1; pitchIndex++) {
                  PitchValue pitchValue = points[pitchIndex];
                  PitchValue nextPitch  = points[pitchIndex+1];
                  int nextPointTick = tick1 + nextPitch.time / 60.0 * noteLen;
                  int pitch = pitchValue.pitch;

                  if (pitchIndex == 0 && (pitch == nextPitch.pitch) && (pitch != lastPitch)) {
                        int midiPitch = ms3PitchBend(pitch);
                        int msb = midiPitch / 128;
                        int lsb = midiPitch % 128;
                        NPlayEvent ev(ME_PITCHBEND, channel, lsb, msb);
                        ev.setOriginatingStaff(staffIdx);
                        events->insert(std::pair<int, NPlayEvent>(lastPointTick, ev));
                        lastPointTick = nextPointTick;
                        lastPitch = pitch;
                        continue;
                        }
                  if (pitch == nextPitch.pitch && !(pitchIndex == 0 && pitch != 0)) {
                        lastPointTick = nextPointTick;
                        continue;
                        }

                  double pitchDelta = nextPitch.pitch - pitch;
                  double tickDelta  = nextPitch.time - pitchValue.time;
                  /*         B
                            /.                   pitch is 1/100 semitones
                    bend   / .  pitchDelta       time is in noteDuration/60
                          /  .                   midi pitch is 12/16384 semitones
                         A....
                       tickDelta   */
                  // We need to be careful to add the final event at nextPointTick exactly -- even if
                  // it's not on a multiple of 16 -- or else the bend might be left slightly unfinished
                  for (int i = lastPointTick; i <= nextPointTick + 15; i += 16) {
                        if (i > nextPointTick)
                              i = nextPointTick;

                        double dx = ((i-lastPointTick) * 60) / noteLen;
                        int p = pitch + dx * pitchDelta / tickDelta;

                        if (p != lastPitch) {
                              // We don't support negative pitch, but Midi does. Let's center by adding 8192.
                              int midiPitch = ms3PitchBend(p);
                              // Representing pitch as two bytes
                              int msb = midiPitch / 128;
                              int lsb = midiPitch % 128;
                              NPlayEvent ev(ME_PITCHBEND, channel, lsb, msb);
                              ev.setOriginatingStaff(staffIdx);
                              events->insert(std::pair<int, NPlayEvent>(i, ev));
                              lastPitch = p;
                              }
                        }
                  lastPointTick = nextPointTick;
                  }
            NPlayEvent ev(ME_PITCHBEND, channel, 0, 64); // 0:64 is 8192 - no pitch bend
            ev.setOriginatingStaff(staffIdx);
            events->insert(std::pair<int, NPlayEvent>(tick1+int(noteLen), ev));
            }
      }

//---------------------------------------------------------
//   aeolusSetStop
//---------------------------------------------------------

static void aeolusSetStop(int tick, int channel, int i, int k, bool val, EventMap* events)
      {
      NPlayEvent event;
      event.setType(ME_CONTROLLER);
      event.setController(98);
      if (val)
            event.setValue(0x40 + 0x20  + i);
      else
            event.setValue(0x40 + 0x10  + i);

      event.setChannel(channel);
      events->insert(std::pair<int,NPlayEvent>(tick, event));

      event.setValue(k);
      events->insert(std::pair<int,NPlayEvent>(tick, event));
//      event.setValue(0x40 + i);
//      events->insert(std::pair<int,NPlayEvent>(tick, event));
      }

//---------------------------------------------------------
//   collectProgramChanges
//---------------------------------------------------------

static void collectProgramChanges(EventMap* events, Measure const * m, Staff* staff, int tickOffset)
      {
      int firstStaffIdx = staff->idx();
      int nextStaffIdx  = firstStaffIdx + 1;

      //
      // collect program changes and controller
      //
      for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (Element* e : s->annotations()) {
                  if (!e->isStaffTextBase() || e->staffIdx() < firstStaffIdx || e->staffIdx() >= nextStaffIdx)
                        continue;
                  const StaffTextBase* st1 = toStaffTextBase(e);
                  Fraction tick = s->tick() + Fraction::fromTicks(tickOffset);

                  Instrument* instr = e->part()->instrument(tick);
                  for (const ChannelActions& ca : *st1->channelActions()) {
                        int channel = instr->channel().at(ca.channel)->channel();
                        for (const QString& ma : ca.midiActionNames) {
                              NamedEventList* nel = instr->midiAction(ma, ca.channel);
                              if (!nel)
                                    continue;
                              for (MidiCoreEvent event : nel->events) {
                                    event.setChannel(channel);
                                    NPlayEvent e1(event);
                                    e1.setOriginatingStaff(firstStaffIdx);
                                    if (e1.dataA() == CTRL_PROGRAM)
                                          events->insert(std::pair<int, NPlayEvent>(tick.ticks()-1, e1));
                                    else
                                          events->insert(std::pair<int, NPlayEvent>(tick.ticks(), e1));
                                    }
                              }
                        }
                  if (st1->setAeolusStops()) {
                        Staff* s1 = st1->staff();
                        int voice   = 0;
                        int channel = s1->channel(tick, voice);

                        for (int i = 0; i < 4; ++i) {
                              static int num[4] = { 12, 13, 16, 16 };
                              for (int k = 0; k < num[i]; ++k)
                                    aeolusSetStop(tick.ticks(), channel, i, k, st1->getAeolusStop(i, k), events);
                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   getControllerFromCC
//---------------------------------------------------------

static int getControllerFromCC(int cc)
      {
      int controller = -1;

      switch (cc) {
            case 1:
                  controller = CTRL_MODULATION;
                  break;
            case 2:
                  controller = CTRL_BREATH;
                  break;
            case 4:
                  controller = CTRL_FOOT;
                  break;
            case 11:
                  controller = CTRL_EXPRESSION;
                  break;
            default:
                  break;
            }

      return controller;
      }

//---------------------------------------------------------
//    renderHarmony
///    renders chord symbols
//---------------------------------------------------------
static void renderHarmony(EventMap* events, Measure const * m, Harmony* h, int tickOffset)
      {
      if (!h->isRealizable())
            return;

      Staff* staff = m->score()->staff(h->track() / VOICES);
      IF_ASSERT_FAILED(staff) {
          return;
      }

      const Channel* channel = staff->part()->harmonyChannel();
      IF_ASSERT_FAILED(channel)
            return;

      events->registerChannel(channel->channel());
      if (!staff->primaryStaff())
            return;

      int staffIdx = staff->idx();
      int velocity = staff->velocities().val(h->tick());

      RealizedHarmony r = h->getRealizedHarmony();
      QList<int> pitches = r.pitches();

      NPlayEvent ev(ME_NOTEON, channel->channel(), 0, velocity);
      ev.setHarmony(h);
      Fraction duration = r.getActualDuration(h->tick().ticks() + tickOffset);

      int onTime = h->tick().ticks() + tickOffset;
      int offTime = onTime + duration.ticks();

      ev.setOriginatingStaff(staffIdx);
      ev.setTuning(0.0);

      //add play events
      for (int p : qAsConst(pitches)) {
            ev.setPitch(p);
            ev.setVelo(velocity);
            events->insert(std::pair<int, NPlayEvent>(onTime, ev));
            ev.setVelo(0);
            events->insert(std::pair<int, NPlayEvent>(offTime, ev));
            }
      }

//---------------------------------------------------------
//   collectMeasureEventsSimple
//    the original, velocity-only method of collecting events.
//---------------------------------------------------------

void MidiRenderer::collectMeasureEventsSimple(EventMap* events, Measure const * m, const StaffContext& sctx, int tickOffset)
      {
      int firstStaffIdx = sctx.staff->idx();
      int nextStaffIdx  = firstStaffIdx + 1;

      SegmentType st = SegmentType::ChordRest;
      int strack = firstStaffIdx * VOICES;
      int etrack = nextStaffIdx * VOICES;

      for (Segment* seg = m->first(st); seg; seg = seg->next(st)) {
            int tick = seg->tick().ticks();

            //render harmony
            if (sctx.renderHarmony) {
                  for (Element* e : seg->annotations()) {
                        if (!e || (e->track() < strack) || (e->track() >= etrack))
                              continue;
                        Harmony* h = nullptr;
                        if (e->isHarmony())
                              h = toHarmony(e);
                        else if (e->isFretDiagram())
                              h = toFretDiagram(e)->harmony();
                        if (!h || !h->play())
                              continue;
                        renderHarmony(events, m, h, tickOffset);
                        }
                  }

            for (int track = strack; track < etrack; ++track) {
                  // skip linked staves, except primary
                  if (!m->score()->staff(track / VOICES)->primaryStaff()) {
                        track += VOICES-1;
                        continue;
                        }
                  Element* cr = seg->element(track);
                  if (cr == 0 || cr->type() != ElementType::CHORD)
                        continue;

                  Chord* chord = toChord(cr);
                  Staff* st1   = chord->staff();
                  Instrument* instr = chord->part()->instrument(Fraction::fromTicks(tick));
                  int channel = instr->channel(chord->upNote()->subchannel())->channel();
                  events->registerChannel(channel);

                  qreal veloMultiplier = 1;
                  for (Articulation*& a : chord->articulations()) {
                        if (a->playArticulation()) {
                              veloMultiplier *= instr->getVelocityMultiplier(a->articulationName());
                              }
                        }

                  SndConfig config;       // dummy

                  if (!graceNotesMerged(chord))
                        for (Chord*& c : chord->graceNotesBefore())
                              for (const Note* note : c->notes())
                                    collectNote(events, channel, note, veloMultiplier, tickOffset, st1, config);

                  for (const Note* note : chord->notes())
                        collectNote(events, channel, note, veloMultiplier, tickOffset, st1, config);

                  if (!graceNotesMerged(chord))
                        for (Chord*& c : chord->graceNotesAfter())
                              for (const Note* note : c->notes())
                                    collectNote(events, channel, note, veloMultiplier, tickOffset, st1, config);
                  }
            }
      }

//---------------------------------------------------------
//   collectMeasureEventsDefault
//    this uses only CC events to control note velocity, and sets the
//    note-on velocity to always be 127 (max). This is the method that allows
//    single note dynamics, but only works if the soundfont supports it.
//    Method is one of:
//          FIXED_MAX - default: velocity is fixed at 127
//          SEG_START - note-on velocity is the same as the start velocity of the seg
//---------------------------------------------------------

void MidiRenderer::collectMeasureEventsDefault(EventMap* events, Measure const * m, const StaffContext& sctx, int tickOffset)
      {
      int controller = getControllerFromCC(sctx.cc);

      if (controller == -1) {
            qDebug("controller for CC %d not valid", sctx.cc);
            return;
            }

      int firstStaffIdx = sctx.staff->idx();
      int nextStaffIdx  = firstStaffIdx + 1;

      SegmentType st = SegmentType::ChordRest;
      int strack = firstStaffIdx * VOICES;
      int etrack = nextStaffIdx * VOICES;
      for (Segment* seg = m->first(st); seg; seg = seg->next(st)) {
            Fraction tick = seg->tick();

            //render harmony
            if (sctx.renderHarmony) {
                  for (Element* e : seg->annotations()) {
                        if (!e || (e->track() < strack) || (e->track() >= etrack))
                              continue;
                        Harmony* h = nullptr;
                        if (e->isHarmony())
                              h = toHarmony(e);
                        else if (e->isFretDiagram())
                              h = toFretDiagram(e)->harmony();
                        if (!h || !h->play())
                              continue;
                        renderHarmony(events, m, h, tickOffset);
                        }
                  }

            for (int track = strack; track < etrack; ++track) {
                  // Skip linked staves, except primary
                  Staff* st1 = m->score()->staff(track / VOICES);
                  if (!st1->primaryStaff()) {
                        track += VOICES - 1;
                        continue;
                        }

                  Element* cr = seg->element(track);
                  if (!cr)
                        continue;

                  if (!cr->isChord())
                        continue;

                  Chord* chord = toChord(cr);

                  Instrument* instr = st1->part()->instrument(tick);
                  int subchannel = chord->upNote()->subchannel();
                  int channel = instr->channel(subchannel)->channel();

                  events->registerChannel(channel);

                  // Get a velocity multiplier
                  qreal veloMultiplier = 1;
                  for (Articulation*& a : chord->articulations()) {
                        if (a->playArticulation()) {
                              veloMultiplier *= instr->getVelocityMultiplier(a->articulationName());
                              }
                        }

                  bool useSND = instr->singleNoteDynamics();
                  SndConfig config = SndConfig(useSND, controller, sctx.method);

                  //
                  // Add normal note events
                  //

                  if (!graceNotesMerged(chord))
                        for (Chord*& c : chord->graceNotesBefore())
                              for (const Note* note : c->notes())
                                    collectNote(events, channel, note, veloMultiplier, tickOffset, st1, config);

                  for (const Note* note : chord->notes())
                        collectNote(events, channel, note, veloMultiplier, tickOffset, st1, config);

                  if (!graceNotesMerged(chord))
                        for (Chord*& c : chord->graceNotesAfter())
                              for (const Note* note : c->notes())
                                    collectNote(events, channel, note, veloMultiplier, tickOffset, st1, config);
                  }
            }
      }

//---------------------------------------------------------
//   ms4Swing
//    Swing::applySwing (MuseScore 4): MuseScore 3's swing, but with the position in a pickup
//    bar counted from where the full bar would start (anacrusisOffset); not in tuplets
//---------------------------------------------------------

static void ms4Swing(const Chord* chord, int& onTime, int& gateTime)
      {
      onTime = 0;
      gateTime = 100;
      if (!chord || chord->tuplet() || chord->isGrace())
            return;
      const SwingParameters params = chord->staff()->swing(chord->tick());
      if (params.swingUnit == 0)
            return;
      auto isSubdivided = [&](ChordRest* cr) {
            if (!cr)
                  return false;
            ChordRest* prev = prevChordRest(cr);
            return cr->actualTicks().ticks() < params.swingUnit || (prev && prev->actualTicks().ticks() < params.swingUnit);
            };
      Chord* c = const_cast<Chord*>(chord);
      const int startTick = (chord->rtick() + chord->measure()->anacrusisOffset()).ticks();
      const int swingBeat = params.swingUnit * 2;
      const double ticksDuration = chord->actualTicks().ticks();
      const double swingTickAdjust = swingBeat * ((params.swingRatio - 50) / 100.0);
      const double swingActualAdjust = (swingTickAdjust / ticksDuration) * 1000.0;
      if (startTick % swingBeat == params.swingUnit && !isSubdivided(c)) {
            onTime = int(onTime + swingActualAdjust);
            gateTime = int(200 - (gateTime + (swingActualAdjust / 10)));
            }
      const int endTick = startTick + int(ticksDuration);
      if (endTick % swingBeat == params.swingUnit && !isSubdivided(nextChordRest(c)))
            gateTime = int(gateTime + (swingActualAdjust / 10));
      if (gateTime <= 0)
            gateTime = 100;
      }

//---------------------------------------------------------
//   addMs4PitchCurve
//    FluidSequencer::addPitchCurve: a bend reset where the articulation (the note's nominal
//    length) or the note ends, and the curve's segments interpolated in steps of 1/25 semitone
//---------------------------------------------------------

static void addMs4PitchCurve(EventMap* events, int channel, int staffIdx, int artStart, int artTicks, int noteEnd, const int* curve, int layer)
      {
      auto bend = [&](int tick, int value) {
            NPlayEvent ev(ME_PITCHBEND, channel, value % 128, value / 128);
            ev.setOriginatingStaff(staffIdx);
            ev.setLayer(layer);
            events->insert(std::make_pair(tick, ev));
            };
      const int resetAt = std::min(artStart + artTicks, noteEnd);
      bend(resetAt, 8192);
      int prev = -1;
      for (int i = 0; i < 10; ++i) {
            const int currValue = Ms4::pitchBendLevel(curve[i]);
            const int nextValue = Ms4::pitchBendLevel(curve[i + 1]);
            const double currTick = artStart + artTicks * (i * 1000) / double(Ms4::HUNDRED);
            const double nextTick = artStart + artTicks * ((i + 1) * 1000) / double(Ms4::HUNDRED);
            const size_t count = std::max<size_t>(std::abs(curve[i + 1] - curve[i]) / 2, 1);   // PITCH_LEVEL_STEP / 25
            for (size_t k = 0; k <= count; ++k) {
                  const double t = double(k) / count;
                  const int tick = int(std::round(currTick + (nextTick - currTick) * t));
                  const int value = int(std::round(currValue + (nextValue - currValue) * t));
                  if (tick < resetAt && value != prev)
                        bend(tick, value);
                  prev = value;
                  }
            }
      }

//---------------------------------------------------------
//   collectMeasureEventsMs4
//    MuseScore 4's note model: each note's articulations from its context, their averaged
//    length and dynamic pattern at the dynamic level in force, and FluidSequencer's velocity.
//    Single-note dynamics are CC11 events from the part's dynamics (renderMs4Dynamics).
//---------------------------------------------------------

void MidiRenderer::collectMeasureEventsMs4(EventMap* events, Measure const * m, const StaffContext& sctx, int tickOffset)
      {
      const int firstStaffIdx = sctx.staff->idx();
      const int strack = firstStaffIdx * VOICES;
      const int etrack = strack + VOICES;

      for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            Fraction tick = seg->tick();
            if (sctx.renderHarmony) {
                  for (Element* e : seg->annotations()) {
                        if (!e || (e->track() < strack) || (e->track() >= etrack))
                              continue;
                        Harmony* h = nullptr;
                        if (e->isHarmony())
                              h = toHarmony(e);
                        else if (e->isFretDiagram())
                              h = toFretDiagram(e)->harmony();
                        if (!h || !h->play() || !h->isRealizable())
                              continue;
                        // PlaybackEventsRenderer::renderChordSymbol: the realized notes as a plain
                        // keyboard chord at the part's dynamic, on its own track (MS4: piano)
                        Staff* hs = m->score()->staff(h->track() / VOICES);
                        const Channel* hc = hs ? hs->part()->harmonyChannel() : nullptr;
                        auto hpc = hs ? ms4Parts.find(hs->part()) : ms4Parts.end();
                        if (!hc || !hs->primaryStaff() || hpc == ms4Parts.end())
                              continue;
                        events->registerChannel(hc->channel());
                        const int htick = h->tick().ticks();
                        const Ms4::NoteResult r = Ms4::note(Ms4::Family::Keyboards, {}, hpc->second.dynamics.levelAt(h->track(), htick + tickOffset), false);
                        RealizedHarmony rh = h->getRealizedHarmony();
                        const int on = htick + tickOffset;
                        const int length = rh.getActualDuration(on).ticks();
                        const int off = on + (length * r.dur) / Ms4::HUNDRED;
                        for (int p : rh.pitches()) {
                              NPlayEvent ev(ME_NOTEON, hc->channel(), p, r.velocity);
                              ev.setHarmony(h);
                              ev.setOriginatingStaff(hs->idx());
                              events->insert(std::make_pair(on, ev));
                              ev.setVelo(0);
                              events->insert(std::make_pair(qMax(on, off), ev));
                              }
                        }
                  }

            for (int track = strack; track < etrack; ++track) {
                  Staff* st1 = m->score()->staff(track / VOICES);
                  if (!st1->primaryStaff()) {
                        track += VOICES - 1;
                        continue;
                        }
                  Element* cr = seg->element(track);
                  if (!cr || !cr->isChord())
                        continue;
                  Chord* chord = toChord(cr);
                  Instrument* instr = st1->part()->instrument(tick);
                  int channel = instr->channel(chord->upNote()->subchannel())->channel();
                  events->registerChannel(channel);

                  auto pc = ms4Parts.find(st1->part());
                  if (pc == ms4Parts.end())
                        continue;
                  const Ms4::PartContext& ctx = pc->second;
                  const int level = ctx.dynamics.levelAt(chord->track(), tick.ticks() + tickOffset);
                  const std::vector<Ms4::ArtRef> chordArts = Ms4::chordArticulations(chord, ctx.dynamics, tickOffset);

                  auto sit = ctx.sounds.find(instr);

                  // a sound library part: all on the instrument's first channel, each note after
                  // the switch to the articulation the notation asks for, without pitch curves
                  const LibPart* lp = nullptr;
                  const SoundLib::LibInstrument* li = nullptr;
                  {
                        auto lpit = libParts.find(st1->part());
                        if (lpit != libParts.end()) {
                              auto iit = lpit->second.instruments.find(instr);
                              if (iit != lpit->second.instruments.end() && iit->second) {
                                    lp = &lpit->second;
                                    li = iit->second;
                                    }
                              }
                  }
                  const int libChannel = li ? instr->channel(0)->channel() : -1;
                  if (li)
                        events->registerChannel(libChannel);
                  const std::vector<const SoundLib::LibInstrument*> libPatches = li ? lp->patchesFor(li)
                                                                                     : std::vector<const SoundLib::LibInstrument*>();
                  // a short (Spitfire: velocity, not CC1, sets its dynamics): measured (Check articulations ›
                  // Dynamics), the velocity at which it is as loud as the part's held note at this dynamic;
                  // else, listed in <Dynamics velocity>, its level on the CC's scale. An accent's share
                  // (levelVelocity over the plain level) on top. -1: MS4's velocity
                  const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = li ? SoundLib::dynamicsCalibration() : nullptr;
                  auto libVelocity = [&](const SoundLib::Choice& c, const Ms4::NoteResult& r, int dynLevel) {
                        if (!c)
                              return -1;
                        const int level = Ms4::expressionLevel(dynLevel);
                        const double accent = level > 0 ? double(r.levelVelocity) / level : 1.0;
                        if (cal) {
                              const SoundLib::Choice held = SoundLib::choose(libPatches, SoundLib::Want { { "long" }, {} });
                              if (held) {
                                    // as loud as the held note plays: at the dynamics CC even steps send
                                    // (their volume turns both down alike)
                                    const int cc = SoundLib::evenStep(SoundLib::heldCurve(*cal, libPatches), SoundLib::evenSteps(score),
                                                                      level).dynamics;
                                    const int v = SoundLib::calibratedVelocity(*cal, libPatches[c.patch]->name, c.articulation->value,
                                                                               libPatches[held.patch]->name, held.articulation->value, cc,
                                                                               SoundLib::family(*libPatches.front()), score);
                                    if (v > 0)
                                          return qBound(1, int(std::lround(v * accent)), 127);
                                    }
                              }
                        return lp->velocityDynamics.contains(c.base) ? r.levelVelocity : -1;
                        };
                  auto librarySwitch = [&](const Note* note, const std::vector<Ms4::ArtRef>& noteArts, int start, int length) {
                        const SoundLib::Choice c = libraryChoice(*lp, *li, note, noteArts, start, length);
                        if (c)
                              putLibrarySwitch(events, *libPatches[c.patch], libChannel, c, start + tickOffset, st1->idx());
                        return c;
                        };
                  // a kit: the drum sound's patch and key (rolled: its roll key, else its hit); none: the
                  // built-in synthesizer plays it (patch -1: not routed)
                  struct LibNote { SoundLib::Choice choice; int key = -1; int velocity = -1; bool builtIn = false; };
                  auto kitNote = [&](const Note* note, bool roll = false) {
                        LibNote n;
                        SoundLib::DrumChoice d = roll ? SoundLib::drum(libPatches, note->pitch(), instr->getId(), "roll")
                                                      : SoundLib::DrumChoice();
                        if (d.patch < 0)
                              d = SoundLib::drum(libPatches, note->pitch(), instr->getId());
                        if (d.patch < 0) {
                              n.builtIn = true;
                              n.choice.patch = -1;
                              }
                        else {
                              n.choice.patch = d.patch;
                              n.key = d.key->key;
                              n.velocity = d.key->velocity;
                              }
                        return n;
                        };
                  // a legato articulation needs the next note to start before this one ends
                  auto libOverlap = [](const SoundLib::Choice& c) { return c && c.base == "legato" ? DIVISION / 16 : 0; };

                  std::function<void(const Note*, const std::vector<Ms4::ArtRef>&, int, int, int, int)> renderAtFn;
                  auto collect = [&](const Note* note, const std::vector<Ms4::ArtRef>& arts, int offset = 0, int cut = 0, bool once = false) {
                        // a discrete glissando: its steps over the note's length, each a note of its own at the
                        // dynamic of its time; tied into, the first step is left out (the tie took it)
                        const std::vector<int> steps = ms4DiscreteGlissando(note);
                        if (!steps.empty() && renderAtFn) {
                              std::vector<Ms4::ArtRef> gArts = arts;
                              gArts.push_back({ Ms4::Art::DiscreteGlissando, false });
                              const int start = note->chord()->tick().ticks() + offset;
                              const int length = note->chord()->actualTicks().ticks() - offset - cut;
                              const double step = length / double(steps.size());
                              for (size_t i = note->tieBack() ? 1 : 0; i < steps.size(); ++i)
                                    renderAtFn(note, gArts, int(std::round(start + i * step)), int(step), steps[i], 0);
                              return;
                              }
                        std::vector<Ms4::ArtRef> noteArts = Ms4::noteArticulations(note, arts);
                        // NoteRenderer::renderNormalTie / addTiedNote: a tied note with articulations of its
                        // own adds its length scaled by their duration factor and hands them on to the
                        // note (not staccato / staccatissimo); Standard goes where there are others
                        int tiedTicks = -1;
                        if (note->tieFor() && !note->tieBack()) {
                              double extra = 0;
                              const Note* anchor = note;          // the chain's note in the pass being walked
                              int anchorOffset = tickOffset;
                              for (const Note* n = note; n->tieFor() && n->tieFor()->endNote(); ) {
                                    const Note* next = n->tieFor()->endNote();
                                    if (next == n || !next->play())
                                          break;
                                    if (!ms4SameRepeat(score, anchor, next, anchorOffset)) {
                                          // the tie leads out of this pass: only a pass's last note carries on,
                                          // into the next pass's first note if a tie leads there (renderPartialTie)
                                          int nextOffset = 0;
                                          next = (n == anchor) ? ms4PartialTieIncoming(score, n, anchorOffset, nextOffset) : nullptr;
                                          if (!next || !next->play())
                                                break;
                                          anchor = next;
                                          anchorOffset = nextOffset;
                                          }
                                    const int t = next->chord()->actualTicks().ticks();
                                    if (isGlissandoFor(next)) {
                                          const std::vector<int> steps = ms4DiscreteGlissando(next);
                                          if (!steps.empty())
                                                extra += t / int(steps.size());
                                          break;
                                          }
                                    const std::vector<Ms4::ArtRef> nextArts = Ms4::noteArticulations(next, Ms4::chordArticulations(next->chord(), ctx.dynamics, tickOffset));
                                    const Ms4::NoteResult rn = Ms4::note(ctx.family, nextArts, level, ctx.snd);
                                    if (rn.arts.size() == 1 && rn.arts[0] == Ms4::Art::Standard)
                                          extra += t;
                                    else {
                                          extra += t * double(rn.dur) / Ms4::HUNDRED;
                                          // merged in the tied note's map order (Standard is dropped from the
                                          // merged map afterwards: iterationOrder)
                                          for (Ms4::Art art : rn.arts) {
                                                if (art == Ms4::Art::Staccato || art == Ms4::Art::Staccatissimo)
                                                      continue;
                                                bool fallback = false;
                                                for (const Ms4::ArtRef& a : nextArts)
                                                      fallback |= a.art == art && a.fallback;
                                                noteArts.push_back({ art, fallback, 2 });
                                                }
                                          }
                                    n = next;
                                    }
                              tiedTicks = int(std::lround(extra));
                              }
                        Ms4::NoteResult r = Ms4::note(ctx.family, noteArts, level, ctx.snd);
                        if (qEnvironmentVariableIsSet("MS4_DEBUG_NOTES")) {
                              QString all;
                              for (Ms4::Art a : r.arts)
                                    all += QString(Ms4::ART_NAMES[int(a)]) + " ";
                              qDebug("MS4NOTE tick %d pitch %d level %d snd %d velo %d dur %d arts %s", note->chord()->tick().ticks(),
                                     note->pitch(), level, int(ctx.snd), r.velocity, r.dur, qPrintable(all));
                              }
                        // the preset MS4 plays this note with -> the channel slot programmed with it
                        int noteChannel = channel;
                        int layer = note->voice();
                        LibNote libNote;
                        if (li && li->kit)
                              libNote = kitNote(note, once);
                        SoundLib::Choice& libChoice = libNote.choice;
                        if (li && !libNote.builtIn) {
                              noteChannel = libChannel;
                              if (!li->kit && !note->tieBack()) {
                                    const Chord* ch = note->chord();
                                    libChoice = librarySwitch(note, noteArts, ch->tick().ticks() + offset, ch->actualTicks().ticks() - offset - cut);
                                    libNote.velocity = libVelocity(libChoice, r, level);
                                    }
                              }
                        else if (sit != ctx.sounds.end()) {
                              const Ms4::Slot& slot = sit->second.channelSlots[sit->second.slotFor(r.arts)];
                              noteChannel = instr->channel(slot.channel)->channel();
                              layer = sit->second.layerFor(r.arts, layer);
                              events->registerChannel(noteChannel);
                              }
                        SndConfig config;
                        config.ms4 = true;
                        config.method = DynamicsRenderMethod::MS4;
                        config.ms4Velocity = r.velocity;
                        config.ms4Dur = r.dur;
                        config.ms4Ts = r.ts;
                        config.ms4Offset = offset;
                        config.ms4Cut = cut;
                        config.ms4Layer = layer;
                        config.ms4TiedTicks = tiedTicks;
                        config.ms4Once = once;
                        config.libPatch = libChoice.patch;
                        config.libOverlap = libOverlap(libChoice);
                        config.libKey = libNote.key;
                        if (libNote.velocity > 0)
                              config.ms4Velocity = libNote.velocity;
                        ms4Swing(note->chord(), config.ms4SwingOn, config.ms4SwingGate);
                        collectNote(events, noteChannel, note, 1.0, tickOffset, st1, config);
                        if (r.bend && (!li || libNote.builtIn) && !note->chord()->isGrace() && !note->tieBack()) {
                              const Chord* ch = note->chord();
                              const int ticks = ch->actualTicks().ticks();
                              const int artStart = ch->tick().ticks() + tickOffset;
                              const int on = artStart + (ticks * r.ts) / Ms4::HUNDRED;
                              const int off = on + (ticks * r.dur) / Ms4::HUNDRED;
                              addMs4PitchCurve(events, noteChannel, st1->idx(), artStart, ticks, off, r.pitchCurve, layer);
                              }
                        };

                  // the second chord of a two-note tremolo is played with the first
                  Tremolo* trem = chord->tremolo();
                  if (trem && trem->twoNotes() && trem->chord2() == chord && trem->chord1() && trem->tremoloType() != TremoloType::BUZZ_ROLL)
                        continue;

                  // a note of its own at a given start, length and pitch offset
                  auto renderAt = [&](const Note* note, const std::vector<Ms4::ArtRef>& arts, int start, int length, int pitchOffset = 0) {
                        // (glissando notes need the tieBack exemption: they play their later steps)
                        if (!note->play())
                              return;
                        const std::vector<Ms4::ArtRef> noteArts = Ms4::noteArticulations(note, arts);
                        Ms4::NoteResult r = Ms4::note(ctx.family, noteArts, ctx.dynamics.levelAt(note->track(), start + tickOffset), ctx.snd);
                        int noteChannel = channel;
                        int layer = note->voice();
                        LibNote libNote;
                        if (li && li->kit)
                              libNote = kitNote(note);
                        SoundLib::Choice& libChoice = libNote.choice;
                        if (li && !libNote.builtIn) {
                              noteChannel = libChannel;
                              if (!li->kit) {
                                    libChoice = librarySwitch(note, noteArts, start, length);
                                    libNote.velocity = libVelocity(libChoice, r, ctx.dynamics.levelAt(note->track(), start + tickOffset));
                                    }
                              }
                        else if (sit != ctx.sounds.end()) {
                              noteChannel = instr->channel(sit->second.channelSlots[sit->second.slotFor(r.arts)].channel)->channel();
                              layer = sit->second.layerFor(r.arts, layer);
                              }
                        events->registerChannel(noteChannel);
                        const int on = start + tickOffset + (length * r.ts) / Ms4::HUNDRED;
                        const int off = on + (length * r.dur) / Ms4::HUNDRED + (length > 0 ? libOverlap(libChoice) : 0);
                        if (length <= 0) {
                              // no length (an ornament's body squeezed out by its prefix and suffix): MS4
                              // sends no note-on but still the note-off, at its start plus the (negative)
                              // length -- it ends whatever the key has sounding on the channel
                              NPlayEvent ev(ME_NOTEON, noteChannel, libNote.key >= 0 ? libNote.key : qBound(0, note->ppitch() + pitchOffset, 127), 0);
                              ev.setOriginatingStaff(st1->idx());
                              ev.setLayer(layer);
                              ev.setNote(note);
                              ev.setLibraryPatch(libChoice.patch);
                              events->insert(std::make_pair(off, ev));
                              return;
                              }
                        playNote(events, note, noteChannel, libNote.key >= 0 ? libNote.key : qBound(0, note->ppitch() + pitchOffset, 127),
                                 qBound(1, libNote.velocity > 0 ? libNote.velocity : r.velocity, 127), on, qMax(on, off), st1->idx(), layer, libChoice.patch);
                        };

                  renderAtFn = [&](const Note* n, const std::vector<Ms4::ArtRef>& a, int st, int len, int po, int) { renderAt(n, a, st, len, po); };

                  // OrnamentsRenderer: prefix, body (repeated while it fits for trills) and suffix of
                  // sub-notes on the diatonic neighbours; too short a note plays as it is
                  auto renderOrnament = [&](const Chord* c, const Ms4::OrnamentRule* orn, const std::vector<Ms4::ArtRef>& arts, int start, int length) {
                        bool isSymbol = false;
                        for (Articulation* a : c->articulations()) {
                              const QString name = Sym::id2name(a->symId());
                              if (name.startsWith("ornament") || name == "brassJazzTurn")
                                    isSymbol = true;
                              }
                        const double bps = score->tempomap()->tempo(start);
                        const float sub = orn->subNoteTicks(bps);
                        for (const Note* note : c->notes()) {
                              int T = length;
                              if (isSymbol && note->tieFor()) {             // applyTiedNotesDuration
                                    const Note* last = note->lastTiedNote();
                                    if (last && last != note)
                                          T = last->chord()->tick().ticks() + last->chord()->actualTicks().ticks() - start;
                                    }
                              if (T <= orn->lowTempoTicks) {
                                    renderAt(note, arts, start, T);
                                    continue;
                                    }
                              const int up = Ms4::neighbourSemitones(note, 1);
                              const int down = Ms4::neighbourSemitones(note, -1);
                              auto semis = [&](int step) { return step > 0 ? step * up : (step < 0 ? step * down : 0); };
                              const int preT = int(std::round(orn->prefix.size() * sub));   // buildActualPattern
                              const int sufT = int(std::round(orn->suffix.size() * sub));
                              double t = start;
                              // (createEvents: a negative span -- prefix and suffix longer than the note -- gives
                              // sub-notes of negative length, and walks the time back)
                              auto run = [&](const std::vector<int>& steps, double span, int times) {
                                    const double d = span / (steps.size() * times);
                                    for (int k = 0; k < times; ++k)
                                          for (int step : steps) {
                                                renderAt(note, arts, int(std::round(t)), int(std::lround(d)), semis(step));
                                                t += d;
                                                }
                                    };
                              if (!orn->prefix.empty())
                                    run(orn->prefix, preT, 1);
                              if (!orn->body.empty()) {
                                    const int alterations = orn->repeat ? int(std::max((T / sub) / 2.f, 0.f)) : 1;
                                    if (alterations == 0)
                                          renderAt(note, arts, int(std::round(t)), T);     // as MS4: the whole length, from here
                                    else
                                          run(orn->body, double(T - preT - sufT), alterations);
                                    }
                              if (!orn->suffix.empty())
                                    run(orn->suffix, sufT, 1);
                              }
                        };

                  // a chord's articulations in the order of its ArticulationMap (chord level only)
                  auto chordOrder = [&](const std::vector<Ms4::ArtRef>& refs) {
                        std::vector<Ms4::ArtRef> chordRefs;
                        for (const Ms4::ArtRef& a : refs)
                              if (a.phase == 0)
                                    chordRefs.push_back(a);
                        std::vector<Ms4::ArtRef> out;
                        for (Ms4::Art art : Ms4::iterationOrder(chordRefs, int(ctx.family)))
                              for (const Ms4::ArtRef& a : chordRefs)
                                    if (a.art == art && !a.erased) {
                                          out.push_back(a);
                                          break;
                                          }
                        return out;
                        };

                  // grace notes (GraceChordCtx): the first grace type of the chord (map order) is
                  // played, its grace chords in at most half the principal (2/3 in compound time for
                  // a lone appoggiatura or after-graces; acciaccaturas and groups at most 1/64 each),
                  // keeping their proportions; the principal starts after them / ends before them
                  std::vector<Ms4::ArtRef> principalArts = chordArts;
                  int principalOffset = 0;
                  int principalCut = 0;
                  {
                        Ms4::Art graceType = Ms4::Art::COUNT;
                        for (const Ms4::ArtRef& a : chordOrder(chordArts))
                              if (a.art == Ms4::Art::PreAppoggiatura || a.art == Ms4::Art::PostAppoggiatura || a.art == Ms4::Art::Acciaccatura) {
                                    graceType = a.art;
                                    break;
                                    }
                        // the principal's context goes on without it (erased, keeping the map's history)
                        for (Ms4::ArtRef& a : principalArts)
                              if (a.art == graceType && a.phase == 0)
                                    a.erased = true;
                        if (graceType != Ms4::Art::COUNT) {
                              const bool before = graceType != Ms4::Art::PostAppoggiatura;
                              QVector<Chord*> graces;
                              for (Chord* g : before ? chord->graceNotesBefore() : chord->graceNotesAfter()) {
                                    bool playable = false;
                                    for (Note* n : g->notes())
                                          playable |= n->play();
                                    if (playable)
                                          graces.push_back(g);
                                    }
                              const int nominal = chord->actualTicks().ticks();
                              const Fraction ts = score->sigmap()->timesig(tick).nominal();
                              const bool compound = ts.numerator() % 3 == 0 && ts.numerator() > 3;
                              double available;
                              if (graceType == Ms4::Art::PostAppoggiatura || (graceType == Ms4::Art::PreAppoggiatura && graces.size() == 1))
                                    available = (compound && nominal > DIVISION / 2) ? 2.0 * nominal / 3 : 0.5 * nominal;
                              else
                                    available = std::min(30.0 * graces.size(), 0.5 * nominal);     // DEMISEMIQUAVER_TICKS / 2 each
                              double total = 0;
                              for (Chord* g : graces)
                                    total += g->durationTypeTicks().ticks();
                              if (total > 0) {
                                    const double actual = std::min(available, total);
                                    const double factor = actual / total;
                                    if (before)
                                          principalOffset = int(std::round(actual));
                                    else
                                          principalCut = int(std::round(actual));
                                    double t = before ? tick.ticks() : tick.ticks() + nominal - actual;
                                    // a grace chord renders with the principal's articulations: an ornament of the
                                    // principal ornaments its grace notes too (renderChord with graceCtx, as MS4)
                                    const Ms4::OrnamentRule* graceOrnament = nullptr;
                                    {
                                          for (const Ms4::ArtRef& a : chordOrder(chordArts))
                                                if ((graceOrnament = Ms4::ornamentRule(a.art)))
                                                      break;
                                    }
                                    for (Chord* g : graces) {
                                          const int length = int(std::round(factor * g->durationTypeTicks().ticks()));
                                          if (graceOrnament)
                                                renderOrnament(g, graceOrnament, chordArts, int(std::round(t)), length);
                                          else
                                                for (const Note* note : g->notes())
                                                      renderAt(note, chordArts, int(std::round(t)), length);
                                          t += length;
                                          }
                                    }
                              }
                  }
                  const int pStart = tick.ticks() + principalOffset;
                  const int pLength = chord->actualTicks().ticks() - principalOffset - principalCut;

                  // the principal chord: the first of its articulations (map order) that a renderer
                  // takes (ChordArticulationsRenderer::renderChordArticulations), else its notes as they are
                  const std::vector<Ms4::ArtRef> sorted = chordOrder(principalArts);
                  Arpeggio* arp = chord->arpeggio();
                  enum class Special { NONE, TREMOLO, ORNAMENT, ARPEGGIO } special = Special::NONE;
                  const Ms4::OrnamentRule* ornament = nullptr;
                  // the library plays the trill or tremolo itself: the notes once, as they are
                  bool sampledOrnament = false;
                  if (li && li->kit) {
                        // a rolled chord on a kit: every note has a roll key, else the tremolo's hits
                        sampledOrnament = SoundLib::drumRoll(chord);
                        for (const Note* note : chord->notes())
                              sampledOrnament = sampledOrnament
                                 && SoundLib::drum(libPatches, note->pitch(), instr->getId(), "roll").patch >= 0;
                        }
                  else if (li && chord->upNote()) {
                        const Note* up = chord->upNote();
                        sampledOrnament = libraryChoice(*lp, *li, up, Ms4::noteArticulations(up, principalArts), pStart, pLength).sampledOrnament();
                        }
                  for (const Ms4::ArtRef& a : sorted) {
                        const bool tremoloType = a.art >= Ms4::Art::Tremolo8th && a.art <= Ms4::Art::Tremolo64th;
                        if (sampledOrnament && (tremoloType || Ms4::ornamentRule(a.art)))
                              continue;
                        if (tremoloType && trem && trem->tremoloType() != TremoloType::BUZZ_ROLL) {
                              special = Special::TREMOLO;
                              break;
                              }
                        if ((ornament = Ms4::ornamentRule(a.art))) {
                              special = Special::ORNAMENT;
                              break;
                              }
                        if (a.art >= Ms4::Art::Arpeggio && a.art <= Ms4::Art::ArpeggioStraightDown && arp && arp->playArpeggio()
                            && chord->notes().size() > 1) {
                              special = Special::ARPEGGIO;
                              break;
                              }
                        }

                  if (special == Special::TREMOLO) {
                        // TremoloRenderer: the chord (both chords of a two-note tremolo) repeated in
                        // steps of DIVISION / 2^(beams + lines), each step a note of its own
                        Chord* c1 = trem->twoNotes() ? trem->chord1() : chord;
                        Chord* c2 = trem->twoNotes() ? trem->chord2() : chord;
                        if (c1 && c2) {
                              const int overall = trem->twoNotes() ? c1->actualTicks().ticks() + c2->actualTicks().ticks() : pLength;
                              int step = qMax(1, DIVISION / (1 << (chord->beams() + trem->lines())));
                              const int steps = int(std::round(overall / float(step)));
                              if (steps > 0) {
                                    step = overall / steps;
                                    for (int i = 0; i < steps; ++i) {
                                          const Chord* c = (i % 2) ? c2 : c1;
                                          const std::vector<Ms4::ArtRef> arts = (c == chord) ? principalArts : Ms4::chordArticulations(c, ctx.dynamics, tickOffset);
                                          for (const Note* note : c->notes())
                                                renderAt(note, arts, pStart + i * step, step);
                                          }
                                    }
                              }
                        }
                  else if (special == Special::ORNAMENT) {
                        if (qEnvironmentVariableIsSet("MS4_DEBUG_ORN")) {
                              QString all;
                              for (const Ms4::ArtRef& a : sorted)
                                    all += QString(Ms4::ART_NAMES[int(a.art)]) + (Ms4::ornamentRule(a.art) == ornament ? "* " : " ");
                              qDebug("MS4ORN tick %d utick %d staff %d pitch %d len %d arts %s", pStart, pStart + tickOffset, st1->idx(),
                                     chord->upNote()->pitch(), pLength, qPrintable(all));
                              }
                        renderOrnament(chord, ornament, principalArts, pStart, pLength);
                        }
                  else if (special == Special::ARPEGGIO) {
                        // ArpeggioRenderer: by pitch, a step later each (the note's length / the number
                        // of notes, at most 60 ms, times the stretch), each shortened by its delay
                        std::map<int, const Note*> byPitch;
                        for (const Note* note : chord->notes())
                              byPitch.emplace(note->pitch(), note);
                        const int n = int(chord->notes().size());
                        const double bps = score->tempomap()->tempo(tick.ticks());
                        const double durMs = pLength / (bps * DIVISION) * 1000.0;
                        const double stepMs = std::min(durMs / n, 60.0);
                        const bool up = arp->arpeggioType() != ArpeggioType::DOWN && arp->arpeggioType() != ArpeggioType::DOWN_STRAIGHT;
                        std::vector<const Note*> order;
                        for (const auto& bp : byPitch)
                              order.push_back(bp.second);
                        if (!up)
                              std::reverse(order.begin(), order.end());
                        for (int i = 0; i < int(order.size()); ++i) {
                              const double offsetMs = stepMs * i * arp->Stretch();
                              collect(order[i], principalArts, principalOffset + int(std::round(offsetMs / 1000.0 * bps * DIVISION)), principalCut);
                              }
                        }
                  else {
                        if (qEnvironmentVariableIsSet("MS4_DEBUG_ORN") && !chord->articulations().empty()) {
                              QString all, syms;
                              for (const Ms4::ArtRef& a : sorted)
                                    all += QString(Ms4::ART_NAMES[int(a.art)]) + " ";
                              for (Articulation* a : chord->articulations())
                                    syms += QString(Sym::id2name(a->symId())) + " ";
                              qDebug("MS4PLAIN utick %d pitch %d arts %s syms %s", pStart + tickOffset, chord->upNote()->pitch(),
                                     qPrintable(all), qPrintable(syms));
                              }
                        for (const Note* note : chord->notes())
                              collect(note, principalArts, principalOffset, principalCut, sampledOrnament);
                        }
                  }
            }
      }

//---------------------------------------------------------
//   renderMs4Dynamics
//    single-note dynamics as MuseScore 4 plays them: CC11 at every point of the part's dynamics
//    (FluidSequencer::addDynamicEvents), to all of the instrument's channels (MS4's CC11 goes
//    to its whole synth), and the level in force at the chunk's start
//---------------------------------------------------------

void MidiRenderer::renderMs4Dynamics(const Chunk& chunk, EventMap* events)
      {
      const int tick1 = chunk.tick1();
      const int tick2 = chunk.tick2();
      const int tickOffset = chunk.tickOffset();
      for (const auto& pc : ms4Parts) {
            const Part* part = pc.first;
            const Ms4::PartContext& ctx = pc.second;
            if (!ms4Active.count(part))         // a part in MuseScore 3's mode (partplayback.h)
                  continue;

            // chord symbols: MS4 plays them on a track of their own with the piano (Program(0, 0))
            if (const Channel* hc = const_cast<Part*>(part)->harmonyChannel()) {
                  auto pos = events->lower_bound(tick1 + tickOffset);
                  for (const NPlayEvent& ev : { NPlayEvent(ME_CONTROLLER, hc->channel(), CTRL_HBANK, 0),
                                                NPlayEvent(ME_CONTROLLER, hc->channel(), CTRL_LBANK, 0),
                                                NPlayEvent(ME_CONTROLLER, hc->channel(), CTRL_PROGRAM, 0) }) {
                        NPlayEvent e(ev);
                        e.setOriginatingStaff(part->staff(0)->idx());
                        events->insert(pos, std::make_pair(tick1 + tickOffset, e));
                        }
                  }

            // a sound library part: its dynamics on the library's controller, on the channel it
            // plays on; no presets (a program change would reach the library)
            auto lpit = libParts.find(part);
            const LibPart* lp = lpit != libParts.end() ? &lpit->second : nullptr;
            auto libraryPlays = [lp](const Instrument* instr) {
                  if (!lp)
                        return false;
                  auto it = lp->instruments.find(instr);
                  return it != lp->instruments.end() && it->second;
                  };

            // the presets MS4 plays: each channel slot of each of the part's instruments; ahead of
            // the notes at the chunk's start, which would otherwise sound with the preset before
            // (the score's MuseScore 3 program at the start of playback)
            auto pos = events->lower_bound(tick1 + tickOffset);
            for (const auto& is : ctx.sounds) {
                  const Instrument* instr = is.first;
                  // (a kit: for the drum sounds the built-in synthesizer plays, not routed)
                  const bool kit = libraryPlays(instr) && lp->instruments.at(instr)->kit;
                  if (libraryPlays(instr) && !kit)
                        continue;
                  for (const Ms4::Slot& slot : is.second.channelSlots) {
                        const int ch = score->masterScore()->playbackChannel(instr->channel(slot.channel))->channel();
                        for (const NPlayEvent& ev : { NPlayEvent(ME_CONTROLLER, ch, CTRL_HBANK, (slot.bank >> 7) & 0x7f),
                                                      NPlayEvent(ME_CONTROLLER, ch, CTRL_LBANK, slot.bank & 0x7f),
                                                      NPlayEvent(ME_CONTROLLER, ch, CTRL_PROGRAM, slot.program) }) {
                              NPlayEvent e(ev);
                              e.setOriginatingStaff(part->staff(0)->idx());
                              if (kit)
                                    e.setLibraryPatch(-1);
                              events->insert(pos, std::make_pair(tick1 + tickOffset, e));
                              }
                        }
                  }

            int controller = CTRL_EXPRESSION;
            std::vector<int> channels;
            // a library part's: per channel, its held note's curve for even steps (SoundLib::evenStep), and
            // whether they turn the expression CC (not when an automation lane has it)
            std::map<int, const SoundLib::DynamicsCurve*> heldCurves;
            const SoundLib::EvenSteps evenSteps = lp ? SoundLib::evenSteps(score) : SoundLib::EvenSteps::OFF;
            bool evenVolume = evenSteps == SoundLib::EvenSteps::VOLUME_HEARING || evenSteps == SoundLib::EvenSteps::VOLUME_ENERGY;
            if (lp)
                  for (const LibPart::Auto& a : lp->automation)
                        if (a.cc == CTRL_EXPRESSION)
                              evenVolume = false;
            std::vector<int> builtInChannels;     // a kit's drum sounds the built-in synthesizer plays
            if (lp) {
                  if (ctx.snd)
                        for (const auto& ip : *part->instruments())
                              if (libraryPlays(ip.second) && lp->instruments.at(ip.second)->kit)
                                    for (const Channel* c : ip.second->channel())
                                          builtInChannels.push_back(score->masterScore()->playbackChannel(c)->channel());
                  // the library's controllers (vibrato …): the value in force at the chunk's start,
                  // ahead of its notes, then the staff text's changes
                  for (const LibPart::Ctrl& c : lp->controllers) {
                        auto putCtrl = [&](int tick, int value) {
                              for (const auto& ip : *part->instruments()) {
                                    if (!libraryPlays(ip.second))
                                          continue;
                                    NPlayEvent ev(ME_CONTROLLER, ip.second->channel(0)->channel(), c.cc, value);
                                    ev.setOriginatingStaff(part->staff(0)->idx());
                                    events->insert(events->lower_bound(tick + tickOffset), std::make_pair(tick + tickOffset, ev));
                                    }
                              };
                        auto t = c.texts.upper_bound(tick1);
                        const int start = t == c.texts.begin() ? c.value : std::prev(t)->second;
                        if (start >= 0)
                              putCtrl(tick1, start);
                        for (; t != c.texts.end() && t->first < tick2; ++t)
                              putCtrl(t->first, t->second);
                        }
                  // automation lanes: the value in force at the chunk's start, then their points and ramps
                  // (a MIDI controller to 1/127, a plug-in parameter to 1/1000, every 30 ticks along a ramp)
                  for (const LibPart::Auto& a : lp->automation) {
                        const bool param = a.param >= 0;
                        for (const auto& tv : a.lane.events(tick1, tick2, 30, param ? 0.001 : 1.0 / 127)) {
                              for (const auto& ip : *part->instruments()) {
                                    if (!libraryPlays(ip.second))
                                          continue;
                                    NPlayEvent ev = param ? NPlayEvent(ME_PARAMETER, ip.second->channel(0)->channel(), a.param, 0)
                                                          : NPlayEvent(ME_CONTROLLER, ip.second->channel(0)->channel(), a.cc,
                                                                       int(std::lround(tv.second * 127)));
                                    if (param)
                                          ev.setTuning(float(tv.second));
                                    ev.setOriginatingStaff(part->staff(0)->idx());
                                    events->insert(events->lower_bound(tv.first + tickOffset), std::make_pair(tv.first + tickOffset, ev));
                                    }
                              }
                        }
                  if (library->dynamicsCC < 0 || library->dynamicsCC > 127)
                        continue;
                  controller = library->dynamicsCC;
                  if (controller == CTRL_EXPRESSION)
                        evenVolume = false;
                  const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
                  for (const auto& ip : *part->instruments()) {
                        if (!libraryPlays(ip.second))
                              continue;
                        const int ch = ip.second->channel(0)->channel();
                        channels.push_back(ch);
                        auto li = lp->instruments.find(ip.second);
                        if (cal && evenSteps != SoundLib::EvenSteps::OFF && li != lp->instruments.end() && li->second)
                              heldCurves[ch] = SoundLib::heldCurve(*cal, lp->patchesFor(li->second));
                        // (even steps' volume: put() sends it with each level)
                        if (controller != CTRL_EXPRESSION && !(evenVolume && heldCurves[ch] && heldCurves[ch]->expression.size() >= 2)) {
                              NPlayEvent ev(ME_CONTROLLER, ch, CTRL_EXPRESSION, qBound(0, library->expressionValue, 127));
                              ev.setOriginatingStaff(part->staff(0)->idx());
                              events->insert(std::make_pair(tick1 + tickOffset, ev));
                              }
                        }
                  }
            else {
                  if (!ctx.snd)
                        continue;
                  for (const auto& ip : *part->instruments())
                        for (const Channel* c : ip.second->channel())
                              channels.push_back(score->masterScore()->playbackChannel(c)->channel());
                  }
            auto put = [&](int tick, int level) {
                  const int value = Ms4::expressionLevel(level);
                  for (int ch : channels) {
                        auto hc = heldCurves.find(ch);
                        const SoundLib::Step step = SoundLib::evenStep(hc == heldCurves.end() ? nullptr : hc->second, evenSteps, value);
                        if (evenVolume && step.expression >= 0) {
                              NPlayEvent ev(ME_CONTROLLER, ch, CTRL_EXPRESSION, step.expression);
                              ev.setOriginatingStaff(part->staff(0)->idx());
                              events->insert(events->lower_bound(tick + tickOffset), std::make_pair(tick + tickOffset, ev));
                              }
                        NPlayEvent ev(ME_CONTROLLER, ch, controller, step.dynamics);
                        ev.setOriginatingStaff(part->staff(0)->idx());
                        // a library's dynamics CC ahead of the notes at its tick (a long starting on a
                        // new dynamic would start at the old one); MS4's CC11 after them, as MS4 sends it
                        if (lp)
                              events->insert(events->lower_bound(tick + tickOffset), std::make_pair(tick + tickOffset, ev));
                        else
                              events->insert(std::make_pair(tick + tickOffset, ev));
                        }
                  for (int ch : builtInChannels) {
                        NPlayEvent ev(ME_CONTROLLER, ch, CTRL_EXPRESSION, value);
                        ev.setOriginatingStaff(part->staff(0)->idx());
                        ev.setLibraryPatch(-1);
                        events->insert(std::make_pair(tick + tickOffset, ev));
                        }
                  };
            const std::map<int, int>& levels = ctx.dynamics.levels();
            // An MS3 hairpin can fade the channels to silence (CC11 0; MS4 never goes under ppp).
            // When the level comes back, what still rings from before the silence (a note's
            // release, the pedal) would sound again at the new level, so it is stopped first
            // (all sound off, just before), unless a note started during the silence (a
            // crescendo from nothing). Built-in synthesizer only; ticks here are unrolled.
            int silentSince = -1;
            for (auto it = levels.begin(); it != levels.end() && it->first <= tick1 + tickOffset; ++it) {
                  if (Ms4::expressionLevel(it->second) > 0)
                        silentSince = -1;
                  else if (silentSince < 0)
                        silentSince = it->first;
                  }
            auto level = [&](int utick, int lvl) {
                  const bool sounds = Ms4::expressionLevel(lvl) > 0;
                  if (sounds && silentSince >= 0 && !lp) {
                        bool noteStarted = false;
                        for (auto e = events->lower_bound(silentSince); e != events->end() && e->first < utick && !noteStarted; ++e)
                              noteStarted = e->second.type() == ME_NOTEON && e->second.velo() > 0
                                            && std::find(channels.begin(), channels.end(), e->second.channel()) != channels.end();
                        if (!noteStarted) {
                              for (int ch : channels) {
                                    NPlayEvent ev(ME_CONTROLLER, ch, CTRL_ALL_SOUNDS_OFF, 0);
                                    ev.setOriginatingStaff(part->staff(0)->idx());
                                    events->insert(std::make_pair(qMax(silentSince, utick - 1), ev));
                                    }
                              }
                        }
                  if (sounds)
                        silentSince = -1;
                  else if (silentSince < 0)
                        silentSince = utick;
                  put(utick - tickOffset, lvl);
                  };
            {
                  auto it = levels.upper_bound(tick1 + tickOffset);
                  put(tick1, it == levels.begin() ? Ms4::NATURAL : std::prev(it)->second);
            }
            for (auto it = levels.upper_bound(tick1 + tickOffset); it != levels.end() && it->first < tick2 + tickOffset; ++it)
                  level(it->first, it->second);
            }
      }

//---------------------------------------------------------
//   libraryChoice
//    the library's articulation for a note: what its articulations and the staff text in
//    force ask for (SoundLib::want)
//---------------------------------------------------------

SoundLib::Choice MidiRenderer::libraryChoice(const LibPart& lp, const SoundLib::LibInstrument& li, const Note* note,
                                             const std::vector<Ms4::ArtRef>& noteArts, int tick, int ticks) const
      {
      int trill = 0;
      for (const Ms4::ArtRef& a : noteArts)
            if (a.art == Ms4::Art::Trill || a.art == Ms4::Art::TrillBaroque)
                  trill = SoundLib::trillSemitones(note);
      // its written length (not the Play Panel's speed), with the notes tied to it
      const int tied = note->tieFor() && !note->tieBack() ? note->playTicks() - note->chord()->actualTicks().ticks() : 0;
      const double seconds = score->tempomap()->writtenTime(tick, tick + qMax(0, ticks) + qMax(0, tied));
      return SoundLib::choose(lp.patchesFor(&li), SoundLib::want(noteArts, lp.text.at(tick), seconds, trill));
      }

//---------------------------------------------------------
//   putLibrarySwitch
//    the switch to an articulation, before the note (redundant ones go in finishLibraryEvents)
//---------------------------------------------------------

void MidiRenderer::putLibrarySwitch(EventMap* events, const SoundLib::LibInstrument& li, int channel,
                                    const SoundLib::Choice& choice, int utick, int staffIdx)
      {
      const int value = choice.articulation->value;
      auto put = [&](NPlayEvent ev) {
            ev.setLibrarySwitch(true);
            ev.setLibraryPatch(choice.patch);
            ev.setOriginatingStaff(staffIdx);
            events->insert(std::make_pair(utick, ev));
            };
      switch (li.switchType) {
            case SoundLib::SwitchType::NONE:          // (nothing to switch)
                  break;
            case SoundLib::SwitchType::CC:
                  put(NPlayEvent(ME_CONTROLLER, channel, li.switchNumber, value));
                  break;
            case SoundLib::SwitchType::PROGRAM:
                  put(NPlayEvent(ME_CONTROLLER, channel, CTRL_PROGRAM, value));
                  break;
            case SoundLib::SwitchType::KEYSWITCH:
                  put(NPlayEvent(ME_NOTEON, channel, value, 100));
                  put(NPlayEvent(ME_NOTEON, channel, value, 0));
                  break;
            }
      }

//---------------------------------------------------------
//   finishLibraryEvents
//    the library parts' events of the chunk: each switch that selects what is selected
//    already dropped (a channel's first switch in the chunk stays: playback can start or
//    seek anywhere), and all of them marked with the part's MIDI out
//---------------------------------------------------------

void MidiRenderer::finishLibraryEvents(const Chunk& chunk, EventMap* events)
      {
      if (libRoutes.empty())
            return;
      const int utick2 = chunk.utick2();
      std::map<int, int> selected;              // channel and patch -> the switch in force
      std::map<int, int> dropOff;               // channel and patch -> the keyswitch whose note off goes too
      std::vector<std::pair<int, NPlayEvent>> copies;
      for (auto i = events->lower_bound(chunk.utick1()); i != events->end();) {
            NPlayEvent& ev = i->second;
            auto r = libRoutes.find(ev.channel());
            if (r == libRoutes.end()) {
                  ++i;
                  continue;
                  }
            if (ev.libraryPatch() < 0) {          // a kit's drum sound the built-in synthesizer plays
                  ++i;
                  continue;
                  }
            const std::vector<std::vector<std::pair<int, int>>>& outs = r->second;
            const int patch = qBound(0, ev.libraryPatch(), int(outs.size()) - 1);
            const std::vector<std::pair<int, int>>& lanes = outs[size_t(patch)];
            int lane = 0;
            if (lanes.size() > 1 && ev.note()) {
                  auto l = libLanes.find(ev.note());
                  if (l != libLanes.end())
                        lane = qBound(0, l->second, int(lanes.size()) - 1);
                  }
            if (ev.librarySwitch() && i->first < utick2) {
                  const int ch = ev.channel() * 128 + patch;
                  const bool keyswitch = ev.type() == ME_NOTEON;
                  if (keyswitch && ev.velo() == 0) {
                        auto d = dropOff.find(ch);
                        if (d != dropOff.end() && d->second == ev.pitch()) {
                              dropOff.erase(d);
                              i = events->erase(i);
                              continue;
                              }
                        }
                  else {
                        const int value = keyswitch ? ev.pitch() : ev.value();
                        auto sel = selected.find(ch);
                        if (sel != selected.end() && sel->second == value) {
                              if (keyswitch)
                                    dropOff[ch] = ev.pitch();
                              i = events->erase(i);
                              continue;
                              }
                        selected[ch] = value;
                        }
                  }
            ev.setExternal(lanes[size_t(lane)].first, lanes[size_t(lane)].second);
            if (ev.type() == ME_NOTEON && ev.note() && !ev.librarySwitch()) {
                  auto c = libLaneCents.find(ev.note());
                  if (c != libLaneCents.end())
                        ev.setTuning(float(c->second));
                  }
            // a switch goes to every lane of its patch (whichever plays the next note)
            if (ev.librarySwitch() && i->first < utick2) {
                  for (int l = 1; l < int(lanes.size()); ++l) {
                        NPlayEvent c(ev);
                        c.setExternal(lanes[size_t(l)].first, lanes[size_t(l)].second);
                        copies.emplace_back(i->first, c);
                        }
                  }
            // the part's controllers (dynamics, pedal …) for each of its patches and lanes
            if (i->first < utick2 && ev.type() != ME_NOTEON && ev.type() != ME_NOTEOFF && !ev.librarySwitch() && patch == 0) {
                  for (int p = 0; p < int(outs.size()); ++p) {
                        for (int l = (p == 0 ? 1 : 0); l < int(outs[size_t(p)].size()); ++l) {
                              NPlayEvent c(ev);
                              c.setLibraryPatch(p);
                              c.setExternal(outs[size_t(p)][size_t(l)].first, outs[size_t(p)][size_t(l)].second);
                              copies.emplace_back(i->first, c);
                              }
                        }
                  }
            ++i;
            }
      for (const auto& c : copies)
            events->insert(c);
      }

//---------------------------------------------------------
//   collectMeasureEvents
//    redirects to the correct function based on the passed method
//---------------------------------------------------------

void MidiRenderer::collectMeasureEvents(EventMap* events, Measure const * m, const StaffContext& sctx, int tickOffset)
      {
      switch (sctx.method) {
            case DynamicsRenderMethod::SIMPLE:
                  collectMeasureEventsSimple(events, m, sctx, tickOffset);
                  break;
            case DynamicsRenderMethod::SEG_START:
            case DynamicsRenderMethod::FIXED_MAX:
                  collectMeasureEventsDefault(events, m, sctx, tickOffset);
                  break;
            case DynamicsRenderMethod::MS4:
                  collectMeasureEventsMs4(events, m, sctx, tickOffset);
                  break;
            default:
                  qDebug("Unrecognized dynamics method: %d", int(sctx.method));
                  break;
            }

      collectProgramChanges(events, m, sctx.staff, tickOffset);
      }

//---------------------------------------------------------
//   updateHairpin
//---------------------------------------------------------

void Score::updateHairpin(Hairpin* h)
      {
      Staff* st = h->staff();
      Fraction tick  = h->tick();
      Fraction tick2 = h->tick2();
      int veloChange  = h->veloChange();
      ChangeMethod method = h->veloChangeMethod();

      // Make the change negative when the hairpin is a diminuendo
      HairpinType htype = h->hairpinType();
      ChangeDirection direction = ChangeDirection::INCREASING;
      if (htype == HairpinType::DECRESC_HAIRPIN || htype == HairpinType::DECRESC_LINE) {
            veloChange *= -1;
            direction = ChangeDirection::DECREASING;
            }

      switch (h->dynRange()) {
            case Dynamic::Range::STAFF:
                  st->velocities().addRamp(tick, tick2, veloChange, method, direction);
                  break;
            case Dynamic::Range::PART:
                  for (Staff*& s : *st->part()->staves()) {
                        s->velocities().addRamp(tick, tick2, veloChange, method, direction);
                        }
                  break;
            case Dynamic::Range::SYSTEM:
                  for (Staff*& s : _staves) {
                        s->velocities().addRamp(tick, tick2, veloChange, method, direction);
                        }
                  break;
            }
      }

//---------------------------------------------------------
//   updateVelo
//    calculate velocity for all notes
//---------------------------------------------------------

void Score::updateVelo()
      {
      //
      //    collect Dynamics
      //
      if (!firstMeasure())
            return;

      for (Staff* st : qAsConst(_staves)) {
            st->velocities().clear();
            st->velocityMultiplications().clear();
            }
      for (int staffIdx = 0; staffIdx < nstaves(); ++staffIdx) {
            Staff* st      = staff(staffIdx);
            ChangeMap& velo = st->velocities();
            ChangeMap& mult = st->velocityMultiplications();
            Part* prt      = st->part();
            int partStaves = prt->nstaves();
            int partStaff  = Score::staffIdx(prt);

            for (Segment* s = firstMeasure()->first(); s; s = s->next1()) {
                  Fraction tick = s->tick();
                  for (const Element* e : s->annotations()) {
                        if (e->staffIdx() != staffIdx)
                              continue;
                        if (e->type() != ElementType::DYNAMIC)
                              continue;
                        const Dynamic* d = toDynamic(e);
                        int v            = d->velocity();

                        // treat an invalid dynamic as no change, i.e. a dynamic set to 0
                        if (v < 1)
                              continue;

                        v = qBound(1, v, 127);     //  illegal values

                        // If a dynamic has 'velocity change' update its ending
                        int change = d->changeInVelocity();
                        ChangeDirection direction = ChangeDirection::INCREASING;
                        if (change < 0) {
                              direction = ChangeDirection::DECREASING;
                              }

                        int dStaffIdx = d->staffIdx();
                        switch(d->dynRange()) {
                              case Dynamic::Range::STAFF:
                                    if (dStaffIdx == staffIdx) {
                                          velo.addFixed(tick, v);
                                          if (change != 0) {
                                                Fraction etick = tick + d->velocityChangeLength();
                                                ChangeMethod method = ChangeMethod::NORMAL;
                                                velo.addRamp(tick, etick, change, method, direction);
                                                }
                                          }
                                    break;
                              case Dynamic::Range::PART:
                                    if (dStaffIdx >= partStaff && dStaffIdx < partStaff+partStaves) {
                                          for (int i = partStaff; i < partStaff+partStaves; ++i) {
                                                ChangeMap& stVelo = staff(i)->velocities();
                                                stVelo.addFixed(tick, v);
                                                if (change != 0) {
                                                      Fraction etick = tick + d->velocityChangeLength();
                                                      ChangeMethod method = ChangeMethod::NORMAL;
                                                      stVelo.addRamp(tick, etick, change, method, direction);
                                                      }
                                                }
                                          }
                                    break;
                              case Dynamic::Range::SYSTEM:
                                    for (int i = 0; i < nstaves(); ++i) {
                                          ChangeMap& stVelo = staff(i)->velocities();
                                          stVelo.addFixed(tick, v);
                                          if (change != 0) {
                                                Fraction etick = tick + d->velocityChangeLength();
                                                ChangeMethod method = ChangeMethod::NORMAL;
                                                stVelo.addRamp(tick, etick, change, method, direction);
                                                }
                                          }
                                    break;
                              }
                        }
                  if (s->isChordRestType()) {
                        for (int i = staffIdx * VOICES; i < (staffIdx + 1) * VOICES; ++i) {
                              Element* el = s->element(i);
                              if (!el || !el->isChord())
                                    continue;

                              Chord* chord = toChord(el);
                              Instrument* instr = chord->part()->instrument();

                              qreal veloMultiplier = 1;
                              for (Articulation*& a : chord->articulations()) {
                                    if (a->playArticulation()) {
                                          veloMultiplier *= instr->getVelocityMultiplier(a->articulationName());
                                          }
                                    }

                              if (qFuzzyCompare(veloMultiplier, 1.0))
                                    continue;

                              // TODO this should be a (configurable?) constant somewhere
                              static Fraction ARTICULATION_CHANGE_TIME_MAX = Fraction(1, 16);
                              Fraction ARTICULATION_CHANGE_TIME = qMin(s->ticks(), ARTICULATION_CHANGE_TIME_MAX);
                              int start = veloMultiplier * MidiRenderer::ARTICULATION_CONV_FACTOR;
                              int change = (veloMultiplier - 1) * MidiRenderer::ARTICULATION_CONV_FACTOR;
                              mult.addFixed(chord->tick(), start);
                              mult.addRamp(chord->tick(), chord->tick() + ARTICULATION_CHANGE_TIME, change, ChangeMethod::NORMAL, ChangeDirection::DECREASING);
                              }
                        }
                  }
            for (const auto& sp : _spanner.map()) {
                  Spanner* s = sp.second;
                  if (s->type() != ElementType::HAIRPIN || sp.second->staffIdx() != staffIdx)
                        continue;
                  Hairpin* h = toHairpin(s);
                  updateHairpin(h);
                  }
            }

      for (Staff* st : qAsConst(_staves)) {
            st->velocities().cleanup();
            st->velocityMultiplications().cleanup();
            }

      for (auto it = spanner().cbegin(); it != spanner().cend(); ++it) {
            Spanner* spanner = (*it).second;
            if (!spanner->isVolta())
                  continue;
            Volta* volta = toVolta(spanner);
            volta->setVelocity();
            }
      }

//---------------------------------------------------------
//   renderStaffSegment
//---------------------------------------------------------

void MidiRenderer::renderStaffChunk(const Chunk& chunk, EventMap* events, const StaffContext& sctx)
      {
      Measure const * const start = chunk.startMeasure();
      Measure const * const end = chunk.endMeasure();
      const int tickOffset = chunk.tickOffset();

      Measure const * lastMeasure = start->prevMeasure();

      for (Measure const * m = start; m != end; m = m->nextMeasure()) {
            if (lastMeasure && m->isRepeatMeasure(sctx.staff)) {
                  int offset = (m->tick() - lastMeasure->tick()).ticks();
                  collectMeasureEvents(events, lastMeasure, sctx, tickOffset + offset);
                  }
            else {
                  lastMeasure = m;
                  collectMeasureEvents(events, lastMeasure, sctx, tickOffset);
                  }
            }
      }

//---------------------------------------------------------
//   renderSpanners
//---------------------------------------------------------

void MidiRenderer::renderSpanners(const Chunk& chunk, EventMap* events)
      {
      const int tickOffset = chunk.tickOffset();
      const int tick1 = chunk.tick1();
      const int tick2 = chunk.tick2();

      std::map<int, std::vector<std::pair<int, std::pair<bool, int> > > > channelPedalEvents;
      for (const auto& sp : score->spannerMap().map()) {
            Spanner* s = sp.second;

            int staff = s->staffIdx();
            int idx = s->staff()->channel(s->tick(), 0);
            int channel = s->part()->instrument(s->tick())->channel(idx)->channel();

            if (ms4Mode && ms4Active.count(s->part()) && (s->isPedal() || s->isLetRing())) {
                  // FluidSequencer::addNoteEvent: the pedal's notes send it down at its start and up at
                  // its end -- the collision-free interval's (a pedal followed by another ends a tick
                  // before it) -- both ahead of the notes of that moment
                  auto pc = ms4Parts.find(s->part());
                  if (pc == ms4Parts.end() || (s->staff() && !s->staff()->primaryStaff()))
                        continue;
                  const int from = s->tick().ticks();
                  const int to = pc->second.dynamics.spannerStop(s);
                  if (to <= from)
                        continue;
                  // a sound library part: a pedal change after the chord it comes with, as a pianist
                  // changes it (legato pedalling: up 40 ms after the chord, down again at 90 ms). The
                  // owner, 2026-09-28: SSO's Grand Piano dropped about 1 chord in 8 at a pedal change
                  // (28 of 220, 1 of 2442 elsewhere, in a piano piece's export), the pedal lifted a tick
                  // before the chord and put down with it
                  int down = from;
                  int up = to;
                  if (libParts.count(s->part())) {
                        auto isPedal = [&](const Spanner* o) {
                              return o != s && o->part() == s->part() && (o->isPedal() || o->isLetRing())
                                     && (!o->staff() || o->staff()->primaryStaff());
                              };
                        auto after = [&](int tick, double ms) {
                              const double beatsPerSecond = score->tempomap()->tempo(tick);
                              return std::max(1, int(std::lround(ms / 1000.0 * beatsPerSecond * DIVISION)));
                              };
                        const Spanner* prev = nullptr;
                        const Spanner* next = nullptr;
                        for (const auto& o : score->spannerMap().map()) {
                              if (!isPedal(o.second))
                                    continue;
                              const int oFrom = o.second->tick().ticks();
                              const int oTo = pc->second.dynamics.spannerStop(o.second);
                              if (oFrom < from && (oTo == from - 1 || oTo == from))
                                    prev = o.second;
                              if (oFrom > from && (oFrom == to + 1 || oFrom == to))
                                    next = o.second;
                              }
                        if (prev)
                              down = from + std::min(after(from, 90), std::max(1, (to - from) / 2));
                        if (next) {
                              const int nextFrom = next->tick().ticks();
                              const int nextLength = pc->second.dynamics.spannerStop(next) - nextFrom;
                              up = nextFrom + std::min(after(nextFrom, 40), std::max(0, nextLength / 4));
                              }
                        }
                  auto put = [&](int tick, int value) {
                        NPlayEvent ev(ME_CONTROLLER, channel, CTRL_SUSTAIN, value);
                        ev.setOriginatingStaff(staff);
                        // on the channel of the notes that carry it: without technique mappings a
                        // Pedal note plays on the first voice's (resolveChannelForEvent)
                        bool firstLayer = true;
                        for (const auto& snd : pc->second.sounds)
                              firstLayer &= !snd.second.techniques;
                        if (firstLayer)
                              ev.setLayer(0);
                        events->insert(events->lower_bound(tick + tickOffset), std::make_pair(tick + tickOffset, ev));
                        };
                  // (put in the chunk the pedal's own tick is in; moved past its end, where playback may
                  // jump (a repeat), it stays at that tick)
                  const bool lastChunk = score->lastMeasure() && tick2 >= score->lastMeasure()->endTick().ticks();
                  if (from >= tick1 && from < tick2)
                        put(down < tick2 ? down : from, 127);
                  if ((to >= tick1 && to < tick2) || (lastChunk && to == tick2))
                        put(up < tick2 || (lastChunk && up == tick2) ? up : to, 0);
                  continue;
                  }
            if (s->isPedal() || s->isLetRing()) {
                  channelPedalEvents.insert({channel, std::vector<std::pair<int, std::pair<bool, int> > >()});
                  std::vector<std::pair<int, std::pair<bool, int> > > pedalEventList = channelPedalEvents.at(channel);
                  std::pair<int, std::pair<bool, int> > lastEvent;

                  if (!pedalEventList.empty())
                        lastEvent = pedalEventList.back();
                  else
                        lastEvent = std::pair<int, std::pair<bool, int> >(0, std::pair<bool, int>(true, staff));

                  int st = s->tick().ticks();
                  if (st >= tick1 && st < tick2) {
                        // Handle "overlapping" pedal segments (usual case for connected pedal line)
                        if (lastEvent.second.first == false && lastEvent.first >= (st + tickOffset + 2)) {
                              channelPedalEvents.at(channel).pop_back();
                              channelPedalEvents.at(channel).push_back(std::pair<int, std::pair<bool, int> >(st + tickOffset + (2 - MScore::pedalEventsMinTicks), std::pair<bool, int>(false, staff)));
                              }
                        int a = st + tickOffset + (3 - MScore::pedalEventsMinTicks);
                        channelPedalEvents.at(channel).push_back(std::pair<int, std::pair<bool, int> >(a, std::pair<bool, int>(true, staff)));
                        }
                  if (s->tick2().ticks() >= tick1 && s->tick2().ticks() <= tick2) {
                        int t = s->tick2().ticks() + tickOffset + (2 - MScore::pedalEventsMinTicks);
                        if (!score->repeatList().empty()) {
                              const RepeatSegment& lastRepeat = *score->repeatList().back();
                              if (t > lastRepeat.utick + lastRepeat.len())
                                    t = lastRepeat.utick + lastRepeat.len();
                              }
                        channelPedalEvents.at(channel).push_back(std::pair<int, std::pair<bool, int> >(t, std::pair<bool, int>(false, staff)));
                        }
                  }
            else if (s->isVibrato() && !ms4Active.count(s->part())) {        // MS4: a vibrato line shapes the notes (Vibrato), no bends
                  int stick = s->tick().ticks();
                  int etick = s->tick2().ticks();
                  if (stick >= tick2 || etick < tick1)
                        continue;

                  if (stick < tick1)
                        stick = tick1;
                  if (etick > tick2)
                        etick = tick2;

                  // from start to end of trill, send bend events at regular interval
                  Vibrato* t = toVibrato(s);
                  // guitar vibrato, up only
                  int spitch = 0; // 1/8 (100 is a semitone)
                  int epitch = 12;
                  if (t->vibratoType() == Vibrato::Type::GUITAR_VIBRATO_WIDE) {
                        spitch = 0; // 1/4
                        epitch = 25;
                        }
                  // vibrato with whammy bar up and down
                  else if (t->vibratoType() == Vibrato::Type::VIBRATO_SAWTOOTH_WIDE) {
                        spitch = 25; // 1/16
                        epitch = -25;
                        }
                  else if (t->vibratoType() == Vibrato::Type::VIBRATO_SAWTOOTH) {
                        spitch = 12;
                        epitch = -12;
                        }

                  int j = 0;
                  int delta = DIVISION / 8; // 1/8 note
                  int lastPointTick = stick;
                  while (lastPointTick < etick) {
                        int pitch = (j % 4 < 2) ? spitch : epitch;
                        int nextPitch = ((j+1) % 4 < 2) ? spitch : epitch;
                        int nextPointTick = lastPointTick + delta;
                        for (int i = lastPointTick; i <= nextPointTick; i += 16) {
                              double dx = ((i - lastPointTick) * 60) / delta;
                              int p = pitch + dx * (nextPitch - pitch) / delta;
                              int midiPitch = ms3PitchBend(p);
                              int msb = midiPitch / 128;
                              int lsb = midiPitch % 128;
                              NPlayEvent ev(ME_PITCHBEND, channel, lsb, msb);
                              ev.setOriginatingStaff(staff);
                              events->insert(std::pair<int, NPlayEvent>(i + tickOffset, ev));
                              }
                        lastPointTick = nextPointTick;
                        j++;
                        }
                  NPlayEvent ev(ME_PITCHBEND, channel, 0, 64); // no pitch bend
                  ev.setOriginatingStaff(staff);
                  events->insert(std::pair<int, NPlayEvent>(etick + tickOffset, ev));
                  }
            else
                  continue;
            }

      for (const auto& pedalEvents : channelPedalEvents) {
            int channel = pedalEvents.first;
            for (const auto& pe : pedalEvents.second) {
                  NPlayEvent event;
                  if (pe.second.first == true)
                        event = NPlayEvent(ME_CONTROLLER, channel, CTRL_SUSTAIN, 127);
                  else
                        event = NPlayEvent(ME_CONTROLLER, channel, CTRL_SUSTAIN, 0);
                  event.setOriginatingStaff(pe.second.second);
                  events->insert(std::pair<int,NPlayEvent>(pe.first, event));
                  }
            }
      }

//--------------------------------------------------------
//   swingAdjustParams
//--------------------------------------------------------

void Score::swingAdjustParams(Chord* chord, int& gateTime, int& ontime, int swingUnit, int swingRatio)
      {
      Fraction tick = chord->rtick() + chord->measure()->anacrusisOffset();

      int swingBeat           = swingUnit * 2;
      qreal ticksDuration     = (qreal)chord->actualTicks().ticks();
      qreal swingTickAdjust   = ((qreal)swingBeat) * (((qreal)(swingRatio-50))/100.0);
      qreal swingActualAdjust = (swingTickAdjust/ticksDuration) * 1000.0;
      ChordRest *ncr          = nextChordRest(chord);

      //Check the position of the chord to apply changes accordingly
      if (tick.ticks() % swingBeat == swingUnit) {
            if (!isSubdivided(chord,swingUnit)) {
                  ontime = ontime + swingActualAdjust;
                  }
            }
      int endTick = tick.ticks() + ticksDuration;
      if ((endTick % swingBeat == swingUnit) && (!isSubdivided(ncr,swingUnit))) {
            gateTime = gateTime + (swingActualAdjust/10);
            }
      }

//---------------------------------------------------------
//   isSubdivided
//   Check for subdivided beat
//---------------------------------------------------------

bool Score::isSubdivided(ChordRest* chord, int swingUnit)
      {
      if (!chord)
            return false;
      ChordRest* prev = prevChordRest(chord);
      if (chord->actualTicks().ticks() < swingUnit || (prev && prev->actualTicks().ticks() < swingUnit))
            return true;
      else
            return false;
      }

const Drumset* getDrumset(const Chord* chord)
      {
      if (chord->staff() && chord->staff()->isDrumStaff(chord->tick())) {
            const Drumset* ds = chord->staff()->part()->instrument(chord->tick())->drumset();
            return ds;
            }
      return nullptr;
      }

//---------------------------------------------------------
//   renderTremolo
//---------------------------------------------------------

void renderTremolo(Chord* chord, QList<NoteEventList>& ell)
      {
      Segment* seg = chord->segment();
      Tremolo* tremolo = chord->tremolo();
      int notes = int(chord->notes().size());

      // check if tremolo was rendered before for drum staff
      const Drumset* ds = getDrumset(chord);
      if (ds) {
            for (Note* n : chord->notes()) {
                  DrumInstrumentVariant div = ds->findVariant(n->pitch(), chord->articulations(), chord->tremolo());
                  if (div.pitch != INVALID_PITCH && div.tremolo == tremolo->tremoloType())
                        return; // already rendered
                  }
            }

      // we cannot render buzz roll with MIDI events only
      if (tremolo->tremoloType() == TremoloType::BUZZ_ROLL)
            return;

      // render tremolo with multiple events
      if (chord->tremoloChordType() == TremoloChordType::TremoloFirstNote) {
            int t = DIVISION / (1 << (tremolo->lines() + chord->durationType().hooks()));
            if (t == 0) // avoid crash on very short tremolo
                  t = 1;
            SegmentType st = SegmentType::ChordRest;
            Segment* seg2 = seg->next(st);
            int track = chord->track();
            while (seg2 && !seg2->element(track))
                  seg2 = seg2->next(st);

            if (!seg2)
                  return;

            Element* s2El = seg2->element(track);
            if (s2El) {
                  if (!s2El->isChord())
                        return;
                  }
            else
                  return;

            Chord* c2 = toChord(s2El);
            if (c2->type() == ElementType::CHORD) {
                  int notes2 = int(c2->notes().size());
                  int tnotes = qMax(notes, notes2);
                  int tticks = chord->ticks().ticks() * 2; // use twice the size
                  int n = tticks / t;
                  n /= 2;
                  int l = 2000 * t / tticks;
                  for (int k = 0; k < tnotes; ++k) {
                        NoteEventList* events;
                        if (k < notes) {
                              // first chord has note
                              events = &ell[k];
                              events->clear();
                              }
                        else {
                              // otherwise reuse note 0
                              events = &ell[0];
                              }
                        if (k < notes && k < notes2) {
                              // both chords have note
                              int p1 = chord->notes()[k]->pitch();
                              int p2 = c2->notes()[k]->pitch();
                              int dpitch = p2 - p1;
                              for (int i = 0; i < n; ++i) {
                                    events->append(NoteEvent(0, l * i * 2, l));
                                    events->append(NoteEvent(dpitch, l * i * 2 + l, l));
                                    }
                              }
                        else if (k < notes) {
                              // only first chord has note
                              for (int i = 0; i < n; ++i)
                                    events->append(NoteEvent(0, l * i * 2, l));
                              }
                        else {
                              // only second chord has note
                              // reuse note 0 of first chord
                              int p1 = chord->notes()[0]->pitch();
                              int p2 = c2->notes()[k]->pitch();
                              int dpitch = p2-p1;
                              for (int i = 0; i < n; ++i)
                                    events->append(NoteEvent(dpitch, l * i * 2 + l, l));
                              }
                        }
                  }
            else
                  qDebug("Chord::renderTremolo: cannot find 2. chord");
            }
      else if (chord->tremoloChordType() == TremoloChordType::TremoloSecondNote) {
            for (int k = 0; k < notes; ++k) {
                  NoteEventList* events = &(ell)[k];
                  events->clear();
                  }
            }
      else if (chord->tremoloChordType() == TremoloChordType::TremoloSingle) {
            int t = DIVISION / (1 << (tremolo->lines() + chord->durationType().hooks()));
            if (t == 0) // avoid crash on very short tremolo
                  t = 1;
            int n = chord->ticks().ticks() / t;
            int l = 1000 / n;
            for (int k = 0; k < notes; ++k) {
                  NoteEventList* events = &(ell)[k];
                  events->clear();
                  for (int i = 0; i < n; ++i)
                        events->append(NoteEvent(0, l * i, l));
                  }
            }
      }

//---------------------------------------------------------
//   renderArpeggio
//---------------------------------------------------------

void renderArpeggio(Chord *chord, QList<NoteEventList> & ell)
      {
      int notes = int(chord->notes().size());
      int l = 64;
      while (l && (l * notes > chord->upNote()->playTicks()))
            l = 2*l / 3;
      int start, end, step;
      bool up = chord->arpeggio()->arpeggioType() != ArpeggioType::DOWN && chord->arpeggio()->arpeggioType() != ArpeggioType::DOWN_STRAIGHT;
      if (up) {
            start = 0;
            end   = notes;
            step  = 1;
            }
      else {
            start = notes - 1;
            end   = -1;
            step  = -1;
            }
      int j = 0;
      for (int i = start; i != end; i += step) {
            NoteEventList* events = &(ell)[i];
            events->clear();

            auto tempoRatio = chord->score()->tempomap()->tempo(chord->tick().ticks()) / Score::defaultTempo();
            int ot = (l * j * 1000) / chord->upNote()->playTicks() *
               tempoRatio * chord->arpeggio()->Stretch();

            events->append(NoteEvent(0, ot, 1000 - ot));
            j++;
            }
      }

//---------------------------------------------------------
//   convertLine
// find the line in clefF corresponding to lineL2 in clefR
//---------------------------------------------------------

int convertLine (int lineL2, ClefType clefL, ClefType clefR) {
      int lineR2 = lineL2;
      int goalpitch = line2pitch(lineL2, clefL, Key::C);
      int p;
      while ( (p = line2pitch(lineR2, clefR, Key::C)) > goalpitch && p < 127)
            lineR2++;
      while ( (p = line2pitch(lineR2, clefR, Key::C)) < goalpitch &&  p > 0)
            lineR2--;
      return lineR2;
      }

//---------------------------------------------------------
//   convertLine
// find the line in clef for NoteL corresponding to lineL2 in clef for noteR
// for example middle C is line 10 in Treble clef, but is line -2 in Bass clef.
//---------------------------------------------------------

int convertLine(int lineL2, Note *noteL, Note *noteR)
      {
      return convertLine(lineL2,
         noteL->chord()->staff()->clef(noteL->chord()->tick()),
         noteR->chord()->staff()->clef(noteR->chord()->tick()));
      }

//---------------------------------------------------------
//   articulationExcursion -- an articulation such as a trill, or modant consists of several notes
// played in succession.  The pitch offsets of each such note in the sequence can be represented either
// as a number of steps in the diatonic scale, or in half steps as on a piano keyboard.
// this function, articulationExcursion, takes deltastep indicating the number of steps in the
// diatonic scale, and calculates (and returns) the number of half steps, taking several things into account.
// E.g., the key signature, a trill from e to f, is to be understood as a trill between E and F# if we are
// in the key of G.
// E.g., if previously (looking backward in time) in the same measure there is another note on the same
// staff line/space, and that note has an accidental (sharp,flat,natural,etc), then we want to match that
// tone exactly.
// E.g., If there are multiple notes on the same line/space, then we only consider the most
// recent one, but avoid looking forward in time after the current note.
// E.g., Also if there is an accidental     // on a note one (or more) octaves above or below we
// observe its accidental as well.
// E.g., Still another case is that if two staves are involved (such as a glissando between two
// notes on different staves) then we have to search both staves for the most recent accidental.
//
// noteL is the note to measure the deltastep from, i.e., ornaments are w.r.t. this note
// noteR is the note to search backward from to find accidentals.
//    for ornament calculation noteL and noteR are the same, but for glissando they are
//     the start end end note of glissando.
// deltastep is the desired number of diatonic steps between the base note and this articulation step.
//---------------------------------------------------------

int articulationExcursion(Note *noteL, Note *noteR, int deltastep)
      {
      if (0 == deltastep)
            return 0;
      Chord *chordL = noteL->chord();
      Chord *chordR = noteR->chord();
      int epitchL = noteL->epitch();
      Fraction tickL = chordL->tick();
      // we cannot use staffL = chord->staff() because that won't correspond to the noteL->line()
      //   in the case the user has pressed Shift-Cmd->Up or Shift-Cmd-Down.
      //   Therefore we have to take staffMove() into account using vStaffIdx().
      Staff * staffL = noteL->score()->staff(chordL->vStaffIdx());
      ClefType clefL = staffL->clef(tickL);
      // line represents the ledger line of the staff.  0 is the top line, 1, is the space between the top 2 lines,
      //  ... 8 is the bottom line.
      int lineL     = noteL->line();
      // we use line - deltastep, because lines are oriented from top to bottom, while step is oriented from bottom to top.
      int lineL2    = lineL - deltastep;
      Measure* measureR = chordR->segment()->measure();

      Segment* segment = noteL->chord()->segment();
      int lineR2 = convertLine(lineL2, noteL, noteR);
      // is there another note in this segment on the same line?
      // if so, use its pitch exactly.
      int halfsteps = 0;
      int staffIdx = noteL->chord()->staff()->idx(); // cannot use staffL->idx() because of staffMove()
      int startTrack = staffIdx * VOICES;
      int endTrack   = startTrack + VOICES;
      bool done = false;
      for (int track = startTrack; track < endTrack; ++track) {
            Element *e = segment->element(track);
            if (!e || e->type() != ElementType::CHORD)
                  continue;
            Chord* chord = toChord(e);
            if (chord->vStaffIdx() != chordL->vStaffIdx())
                  continue;
            for (Note* note : chord->notes()) {
                  if (note->tieBack())
                        continue;
                  int pc = (note->line() + 700) % 7;
                  int pc2 = (lineL2 + 700) % 7;
                  if (pc2 == pc) {
                        // e.g., if there is an F# note at this staff/tick, then force every F to be F#.
                        int octaves = (note->line() - lineL2) / 7;
                        halfsteps = note->epitch() + 12 * octaves - epitchL;
                        done = true;
                        break;
                        }
                  }
            if (!done) {
                  if (staffL->isPitchedStaff(segment->tick())) {
                        bool error = false;
                        AccidentalVal acciv2 = measureR->findAccidental(chordR->segment(), chordR->vStaffIdx(), lineR2, error);
                        int acci2 = int(acciv2);
                        // epitch (effective pitch) is a visible pitch so line2pitch returns exactly that.
                        halfsteps = line2pitch(lineL-deltastep, clefL, Key::C) + acci2 - epitchL;
                        }
                  else {
                        // cannot rely on accidentals or key signatures
                        halfsteps = deltastep;
                        }
                  }
            }
      return halfsteps;
      }

//---------------------------------------------------------
// totalTiedNoteTicks
//      return the total of the actualTicks of the given note plus
//      the chain of zero or more notes tied to it to the right.
//---------------------------------------------------------

int totalTiedNoteTicks(Note* note)
      {
      Fraction total = note->chord()->actualTicks();
      while (note->tieFor() && note->tieFor()->endNote() && (note->chord()->tick() < note->tieFor()->endNote()->chord()->tick())) {
            note = note->tieFor()->endNote();
            total += note->chord()->actualTicks();
            }
      return total.ticks();
      }

//---------------------------------------------------------
//   renderNoteArticulation
// prefix, vector of int, normally something like {0,-1,0,1} modeling the prefix of tremblement relative to the base note
// body, vector of int, normally something like {0,-1,0,1} modeling the possibly repeated tremblement relative to the base note
// tickspernote, number of ticks, either _16h or _32nd, i.e., DIVISION/4 or DIVISION/8
// repeatp, true means repeat the body as many times as possible to fill the time slice.
// sustainp, true means the last note of the body is sustained to fill remaining time slice
//---------------------------------------------------------

bool renderNoteArticulation(NoteEventList* events, Note* note, bool chromatic, int requestedTicksPerNote,
   const std::vector<int>& prefix, const std::vector<int>& body,
   bool repeatp, bool sustainp, const std::vector<int>& suffix,
   int fastestFreq=64, int slowestFreq=8 // 64 Hz and 8 Hz
   )
      {
      events->clear();
      Chord *chord = note->chord();
      int maxticks = totalTiedNoteTicks(note);
      int space = 1000 * maxticks;
      int numrepeat = 1;
      int sustain   = 0;
      int ontime    = 0;

      int gnb = note->chord()->graceNotesBefore().size();
      int p = int(prefix.size());
      int b = int(body.size());
      int s = int(suffix.size());
      int gna = note->chord()->graceNotesAfter().size();

      int ticksPerNote = 0;

      if (gnb + p + b + s + gna <= 0 )
            return false;

      Fraction tick = chord->tick();
      qreal tempo = chord->score()->tempo(tick);
      int ticksPerSecond = tempo * DIVISION;

      int minTicksPerNote = int(ticksPerSecond / fastestFreq);
      int maxTicksPerNote = (0 == slowestFreq) ? 0 : int(ticksPerSecond / slowestFreq);

      // for fast tempos, we have to slow down the tremblement frequency, i.e., increase the ticks per note
      if (requestedTicksPerNote >= minTicksPerNote)
            ;
      else { // try to divide the requested frequency by a power of 2 if possible, if not, use the maximum frequency, ie., minTicksPerNote
            ticksPerNote = requestedTicksPerNote;
            while (ticksPerNote < minTicksPerNote) {
                  ticksPerNote *= 2; // decrease the tremblement frequency
                  }
            if (ticksPerNote > maxTicksPerNote)
                  ticksPerNote = minTicksPerNote;
            }

      ticksPerNote = std::max(requestedTicksPerNote, minTicksPerNote);

      if (slowestFreq <= 0) // no slowest freq given such as something silly like glissando with 4 notes over 8 counts.
            ;
      else if (ticksPerNote <= maxTicksPerNote) // in a good range, so we don't need to adjust ticksPerNote
            ;
      else {
            // for slow tempos, such as adagio, we may need to speed up the tremblement frequency, i.e., decrease the ticks per note, to make it sound reasonable.
            ticksPerNote = requestedTicksPerNote;
            while (ticksPerNote > maxTicksPerNote) {
                  ticksPerNote /= 2;
                  }
            if (ticksPerNote < minTicksPerNote)
                  ticksPerNote = minTicksPerNote;
            }
      // calculate whether to shorten the duration value.
      if ( ticksPerNote*(gnb + p + b + s + gna) <= maxticks )
            ; // plenty of space to play the notes without changing the requested trill note duration
      else if ( ticksPerNote == minTicksPerNote )
            return false; // the ornament is impossible to implement respecting the minimum duration and all the notes it contains
      else {
            ticksPerNote = maxticks / (gnb + p + b + s + gna);  // integer division ignoring remainder
            if ( slowestFreq <= 0 )
                  ;
            else if ( ticksPerNote < minTicksPerNote )
                  return false;
            }

      int millespernote = space * ticksPerNote  / maxticks;  // rescale duration into per mille

      // local function:
      // look ahead in the given vector to see if the current note is the same pitch as the next note or next several notes.
      // If so, increment the duration by the appropriate note duration, and increment the index, j, to the next note index
      // of a different pitch.
      // The total duration of the tied note is returned, and the index is modified.
      auto tieForward = [millespernote] (int & j, const std::vector<int> & vec) {
            int size = int(vec.size());
            int duration = millespernote;
            while ( j < size-1 && vec[j] == vec[j+1] ) {
                  duration += millespernote;
                  j++;
                  }
            return duration;
            };

      // local function:
      //   append a NoteEvent either by calculating an articulationExcursion or by
      //   the given chromatic relative pitch.
      //   RETURNS the new ontime value.  The caller is expected to assign this value.
      auto makeEvent = [note,chord,chromatic,events] (int pitch, int ontime, int duration) {
            events->append( NoteEvent(chromatic ? pitch : articulationExcursion(note,note,pitch),
               ontime/chord->actualTicks().ticks(),
               duration/chord->actualTicks().ticks()));
            return ontime + duration;
            };

      // local function:
      //    Given a chord from a grace note, (normally the chord contains a single note) and create
      //    a NoteEvent as if the grace note were part of the articulation (such as trill).  This
      //    local function works for the graceNotesBefore() and also graceNotesAfter().
      //    If the grace note has play=false, then it will sound as a rest, but the other grace
      //    notes will still play.  This means graceExtend simply omits the call to append( NoteEvent(...))
      //    but still updates ontime +=millespernote.
      //    RETURNS the new value of ontime, so caller must make an assignment to the return value.
      auto graceExtend = [millespernote,chord,events] (int notePitch, QVector<Chord*> graceNotes, int ontime) {
            for (Chord* c : graceNotes) {
                  for (Note* n : c->notes()) {
                        // NoteEvent takes relative pitch as first argument.
                        // The pitch is relative to the pitch of the note, the event is rendering
                        if (n->play())
                              events->append( NoteEvent(n->pitch() - notePitch,
                                 ontime/chord->actualTicks().ticks(),
                                 millespernote/chord->actualTicks().ticks()));
                        }
                  ontime += millespernote;
                  }
            return ontime;
            };

      // calculate the number of times to repeat the body, and sustain the last note of the body
      // 1000 = P + numrepeat*B+sustain + S
      if (repeatp)
            numrepeat = (space - millespernote*(gnb + p + s + gna)) / (millespernote * b);
      if (sustainp)
            sustain   = space - millespernote*(gnb + p + numrepeat * b + s + gna);
      // render the graceNotesBefore
      ontime = graceExtend(note->pitch(),note->chord()->graceNotesBefore(), ontime);

      // render the prefix
      for (int j=0; j < p; j++)
            ontime = makeEvent(prefix[j], ontime, tieForward(j,prefix));

      if (b > 0) {
            // Check that we are doing a glissando
            bool isGlissando = false;
            QList<int> onTimes;
            for (Spanner* spanner : note->spannerFor()) {
                  if (spanner->type() == ElementType::GLISSANDO) {
                        Glissando* glissando = toGlissando(spanner);
                        EaseInOut easeInOut(static_cast<qreal>(glissando->easeIn())/100.0,
                              static_cast<qreal>(glissando->easeOut())/100.0);
                        easeInOut.timeList(b, millespernote * b, &onTimes);
                        isGlissando = true;
                        break;
                        }
                  }
            if (isGlissando) {
                  // render the body, i.e. the glissando
                  for (int j = 0; j < b - 1; j++)
                        makeEvent(body[j], onTimes[j], onTimes[j + 1] - onTimes[j]);
                  makeEvent(body[b - 1], onTimes[b-1], (millespernote * b - onTimes[b-1]) + sustain);
                  }
            else {
                  // render the body, but not the final repetition
                  for (int r = 0; r < numrepeat - 1; r++) {
                        for (int j = 0; j < b; j++)
                              ontime = makeEvent(body[j], ontime, millespernote);
                        }
                  // render the final repetition of body, but not the final note of the repetition
                  for (int j = 0; j < b - 1; j++)
                        ontime = makeEvent(body[j], ontime, millespernote);
                  // render the final note of the final repeat of body
                  ontime = makeEvent(body[b - 1], ontime, millespernote + sustain);
                  }
            }
      // render the suffix
      for (int j = 0; j < s; j++)
            ontime = makeEvent(suffix[j], ontime, tieForward(j,suffix));
      // render graceNotesAfter
      graceExtend(note->pitch(), note->chord()->graceNotesAfter(), ontime);
      return true;
      }

// This struct specifies how to render an articulation.
//   atype - the articulation type to implement, such as SymId::ornamentTurn
//   ostyles - the actual ornament has a property called ornamentStyle whose value is
//             a value of type MScore::OrnamentStyle.  This ostyles field indicates the
//             the set of ornamentStyles which apply to this rendition.
//   duration - the default duration for each note in the rendition, the final duration
//            rendered might be less than this if an articulation is attached to a note of
//            short duration.
//   prefix - vector of integers. indicating which notes to play at the beginning of rendering the
//            articulation.  0 represents the principle note, 1==> the note diatonically 1 above
//            -1 ==> the note diatonically 1 below.  E.g., in the key of G, if a turn articulation
//            occurs above the note F#, then 0==>F#, 1==>G, -1==>E.
//            These integers indicate which notes actual notes to play when rendering the ornamented
//            note.   However, if the same integer appears several times adjacently such as {0,0,0,1}
//            That means play the notes tied.  e.g., F# followed by G, but the duration of F# is 3x the
//            duration of the G.
//    body   - notes to play comprising the body of the rendered ornament.
//            The body differs from the prefix and suffix in several ways.
//            * body does not support tied notes: {0,0,0,1} means play 4 distinct notes (not tied).
//            * if there is sufficient duration in the principle note, AND repeatep is true, then body
//               will be rendered multiple times, as the duration allows.
//            * to avoid a time gap (or rest) in rendering the articulation, if sustainp is true,
//               then the final note of the body will be sustained to fill the left-over time.
//    suffix - similar to prefix but played once at the end of the rendered ornament.
//    repeatp  - whether the body is repeatable in its entirety.
//    sustainp - whether the final note of the body should be sustained to fill the remaining duration.

struct OrnamentExcursion {
      SymId atype;
      std::set<MScore::OrnamentStyle> ostyles;
      int duration;
      std::vector<int> prefix;
      std::vector<int> body;
      bool repeatp;
      bool sustainp;
      std::vector<int> suffix;
      };

std::set<MScore::OrnamentStyle> baroque  = {MScore::OrnamentStyle::BAROQUE};
std::set<MScore::OrnamentStyle> defstyle = {MScore::OrnamentStyle::DEFAULT};
std::set<MScore::OrnamentStyle> any; // empty set has the special meaning of any-style, rather than no-styles.
int _16th = DIVISION / 4;
int _32nd = _16th / 2;

std::vector<OrnamentExcursion> excursions = {
      //  articulation type            set of  duration       body         repeatp      suffix
      //                               styles          prefix                    sustainp
      { SymId::ornamentTurn,                any, _32nd, {},    {1,0,-1,0},   false, true, {}}
      ,{SymId::ornamentTurnUp,              any, _32nd, {},    {1,0,-1,0},   false, true, {}}
      ,{SymId::ornamentHaydn,               any, _32nd, {},    {1,0,-1,0},   false, true, {}}
      ,{SymId::ornamentTurnInverted,        any, _32nd, {},    {-1,0,1,0},   false, true, {}}
      ,{SymId::ornamentTurnUpS,             any, _32nd, {},    {-1,0,1,0},   false, true, {}}
      ,{SymId::ornamentTurnSlash,           any, _32nd, {},    {-1,0,1,0},   false, true, {}}
      ,{SymId::ornamentTrill,           baroque, _32nd, {1,0}, {1,0},        true,  true, {}}
      ,{SymId::ornamentTrill,          defstyle, _32nd, {0,1}, {0,1},        true,  true, {}}
      ,{SymId::brassMuteClosed,         baroque, _32nd, {0,-1},{0, -1},      true,  true, {}}
      ,{SymId::brassMuteClosed,        defstyle, _32nd, {},    {0},          false, true, {}} // regular hand-stopped brass
      ,{SymId::ornamentMordent,             any, _32nd, {},    {0,-1,0},     false, true, {}}
      ,{SymId::ornamentShortTrill,     defstyle, _32nd, {},    {0,1,0},      false, true, {}} // inverted mordent
      ,{SymId::ornamentShortTrill,      baroque, _32nd, {1,0,1},{0},         false, true, {}} // short trill
      ,{SymId::ornamentTremblement,         any, _32nd, {1,0}, {1,0},        false, true, {}}
      ,{SymId::ornamentPrallMordent,        any, _32nd, {},    {1,0,-1,0},   false, true, {}}
      ,{SymId::ornamentLinePrall,           any, _32nd, {2,2,2},{1,0},       true,  true, {}}
      ,{SymId::ornamentUpPrall,             any, _16th, {-1,0},{1,0},        true,  true, {1,0}} // p 144 Ex 152 [1]
      ,{SymId::ornamentUpMordent,           any, _16th, {-1,0},{1,0},        true,  true, {-1,0}} // p 144 Ex 152 [1]

      ,{SymId::ornamentPrecompMordentUpperPrefix, any, _16th, {1,1,1,0}, {1,0},    true,  true, {}} // p136 Cadence Appuyee [1] [2]
      ,{SymId::ornamentDownMordent,         any, _16th, {1,1,1,0}, {1,0},    true,  true, {-1, 0}} // p136 Cadence Appuyee + mordent [1] [2]
      ,{SymId::ornamentPrallUp,             any, _16th, {1,0}, {1,0},        true,  true, {-1,0}} // p136 Double Cadence [1]
      ,{SymId::ornamentPrallDown,           any, _16th, {1,0}, {1,0},        true,  true, {-1,0,0,0}} // p144 ex 153 [1]
      ,{SymId::ornamentPrecompSlide,        any, _32nd, {},    {0},          false, true, {}}

      ,{SymId::ornamentShake3,              any, _32nd, {1,0}, {1,0},        true,  true, {}}
      ,{SymId::ornamentShakeMuffat1,        any, _32nd, {1,0}, {1,0},        true,  true, {}}

      ,{ SymId::ornamentTremblementCouperin,any, _32nd, { 1, 1 }, { 0, 1 },  true, true, { 0, 0 } }
      ,{ SymId::ornamentPinceCouperin,      any, _32nd, { 0 },    { 0, -1 }, true, true, { 0, 0 } }

      // [1] Some of the articulations/ornaments in the excursions table above come from
      // Baroque Music, Style and Performance A Handbook, by Robert Donington,(c) 1982
      // ISBN 0-393-30052-8, W. W. Norton & Company, Inc.

      // [2] In some cases, the example from [1] does not preserve the timing.
      // For example, illustrates 2+1/4 counts per half note.
      };

//---------------------------------------------------------
//   renderNoteArticulation
//---------------------------------------------------------

bool renderNoteArticulation(NoteEventList* events, Note * note, bool chromatic, SymId articulationType, MScore::OrnamentStyle ornamentStyle)
      {
      if (!note->staff()->isPitchedStaff(note->tick())) // not enough info in tab staff
            return false;

      std::vector<int> emptypattern = {};
      for (auto& oe : excursions) {
            if (oe.atype == articulationType && (0 == oe.ostyles.size()
               || oe.ostyles.end() != oe.ostyles.find(ornamentStyle))) {
                  return renderNoteArticulation(events, note, chromatic, oe.duration,
                     oe.prefix, oe.body, oe.repeatp, oe.sustainp, oe.suffix);
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   renderNoteArticulation
//---------------------------------------------------------

bool renderNoteArticulation(NoteEventList* events, Note * note, bool chromatic, Trill::Type trillType, MScore::OrnamentStyle ornamentStyle)
      {
      std::map<Trill::Type,SymId> articulationMap = {
             {Trill::Type::TRILL_LINE,      SymId::ornamentTrill      }
            ,{Trill::Type::UPPRALL_LINE,    SymId::ornamentUpPrall    }
            ,{Trill::Type::DOWNPRALL_LINE,  SymId::ornamentPrecompMordentUpperPrefix  }
            ,{Trill::Type::PRALLPRALL_LINE, SymId::ornamentTrill      }
            };
      auto it = articulationMap.find(trillType);
      if (it == articulationMap.cend())
            return false;
      else
            return renderNoteArticulation(events, note, chromatic, it->second, ornamentStyle);
      }

//---------------------------------------------------------
//   noteHasGlissando
// true if note is the end of a glissando
//---------------------------------------------------------

bool noteHasGlissando(Note *note)
      {
      for (Spanner* spanner : note->spannerFor()) {
            if ((spanner->type() == ElementType::GLISSANDO)
               && spanner->endElement()
               && (ElementType::NOTE == spanner->endElement()->type()))
                  return true;
            }
      return false;
      }

//---------------------------------------------------------
//   glissandoPitchOffsets
//---------------------------------------------------------

bool glissandoPitchOffsets(const Spanner* spanner, std::vector<int>& pitchOffsets)
{
      if (!spanner->endElement()->isNote())
            return false;
      const Glissando* glissando = toGlissando(spanner);
      if (!glissando->playGlissando())
            return false;
      GlissandoStyle glissandoStyle = glissando->glissandoStyle();
      if (glissandoStyle == GlissandoStyle::PORTAMENTO)
            return false;
      // only consider glissando connected to NOTE.
      Note* noteStart = toNote(spanner->startElement());
      Note* noteEnd = toNote(spanner->endElement());
      int pitchStart = noteStart->ppitch();
      int pitchEnd = noteEnd->ppitch();
      if (pitchEnd == pitchStart)
            return false;
      int direction = pitchEnd > pitchStart ? 1 : -1;
      pitchOffsets.clear();
      if (glissandoStyle == GlissandoStyle::DIATONIC) {
            int lineStart = noteStart->line();
            // scale obeying accidentals
            for (int line = lineStart, pitch = pitchStart; (direction == 1) ? (pitch < pitchEnd) : (pitch > pitchEnd); line -= direction) {
                  int halfSteps = articulationExcursion(noteStart, noteEnd, lineStart - line);
                  pitch = pitchStart + halfSteps;
                  if ((direction == 1) ? (pitch < pitchEnd) : (pitch > pitchEnd))
                        pitchOffsets.push_back(halfSteps);
                  }
            return pitchOffsets.size() > 0;
            }
      if (glissandoStyle == GlissandoStyle::CHROMATIC) {
            for (int pitch = pitchStart; pitch != pitchEnd; pitch += direction)
                  pitchOffsets.push_back(pitch - pitchStart);
            return true;
            }
      static std::vector<bool> whiteNotes = { true, false, true, false, true, true, false, true, false, true, false, true };
      int Cnote = 60; // pitch of middle C
      bool notePick = glissandoStyle == GlissandoStyle::WHITE_KEYS;
      for (int pitch = pitchStart; pitch != pitchEnd; pitch += direction) {
            int idx = ((pitch - Cnote) + 1200) % 12;
            if (whiteNotes[idx] == notePick)
                  pitchOffsets.push_back(pitch - pitchStart);
            }
      return true;
}

//---------------------------------------------------------
//   renderGlissando
//---------------------------------------------------------

void renderGlissando(NoteEventList* events, Note *notestart)
      {
      std::vector<int> empty = {};
      std::vector<int> body;
      for (Spanner* spanner : notestart->spannerFor()) {
            if (spanner->type() == ElementType::GLISSANDO
            && toGlissando(spanner)->playGlissando()
            && glissandoPitchOffsets(spanner, body))
                  renderNoteArticulation(events, notestart, true, DIVISION, empty, body, false, true, empty, 16, 0);
            }
      }



//---------------------------------------------------------
// findFirstTrill
//  search the spanners in the score, finding the first one
//  which overlaps this chord and is of type ElementType::TRILL
//---------------------------------------------------------

Trill* findFirstTrill(Chord *chord)
      {
      auto spanners = chord->score()->spannerMap().findOverlapping(1+chord->tick().ticks(),
         chord->tick().ticks() + chord->actualTicks().ticks() - 1);
      for (auto i : spanners) {
            if (i.value->type() != ElementType::TRILL)
                  continue;
            if (i.value->track() != chord->track())
                  continue;
            Trill *trill = toTrill (i.value);
            if (trill->playArticulation() == false)
                  continue;
            return trill;
            }
      return nullptr;
      }

// In the case that graceNotesBefore or graceNotesAfter are attached to a note
// with an articulation such as a trill, then the grace notes are/will-be/have-been
// already merged into the articulation.
// So this predicate, graceNotesMerged, checks for this condition to avoid calling
// functions which would re-emit the grace notes by a different algorithm.

bool graceNotesMerged(Chord* chord)
      {
      if (findFirstTrill(chord))
            return true;
      for (Articulation*& a : chord->articulations())
            for (auto& oe : excursions)
                  if ( oe.atype == a->symId() )
                        return true;
      return false;
      }

//---------------------------------------------------------
//   renderChordArticulation
//---------------------------------------------------------

void renderChordArticulation(Chord* chord, QList<NoteEventList> & ell, int & gateTime)
      {
      Segment* seg = chord->segment();
      Instrument* instr = chord->part()->instrument(seg->tick());
      int channel  = 0;  // note->subchannel();

      for (unsigned k = 0; k < chord->notes().size(); ++k) {
            NoteEventList* events = &ell[k];
            Note *note = chord->notes()[k];
            Trill *trill;

            if (noteHasGlissando(note))
                  renderGlissando(events, note);
            else if (chord->staff()->isPitchedStaff(chord->tick())  && (trill = findFirstTrill(chord)) != nullptr) {
                  renderNoteArticulation(events, note, false, trill->trillType(), trill->ornamentStyle());
                  }
            else {
                  for (Articulation*& a : chord->articulations()) {
                        if (!a->playArticulation())
                              continue;
                        if (!renderNoteArticulation(events, note, false, a->symId(), a->ornamentStyle()))
                              instr->updateGateTime(&gateTime, channel, a->articulationName());
                        }
                  }
            }
      }

//---------------------------------------------------------
//   shouldRenderNote
//---------------------------------------------------------

static bool shouldRenderNote(Note* n)
      {
      while (n->tieBack() && n != n->tieBack()->startNote()) {
            n = n->tieBack()->startNote();
            if (findFirstTrill(n->chord()))
                  // The previous tied note probably has events for this note too.
                  // That is, we don't need to render this note separately.
                  return false;
            for (Articulation*& a : n->chord()->articulations()) {
                  if (a->isOrnament()) {
                        return false;
                        }
                  }
            }
      return true;
      }

//---------------------------------------------------------
//   renderChord
//    ontime and trailtime in 1/1000 of duration
//    ontime signifies how much gap to leave, i.e., how late the note should start because of graceNotesBefore which have already been rendered
//    trailtime signifies how much gap to leave after the note to allow for graceNotesAfter to be rendered
//---------------------------------------------------------

static QList<NoteEventList> renderChord(Chord* chord, int gateTime, int ontime, int trailtime)
      {
      QList<NoteEventList> ell;
      if (chord->notes().empty())
            return ell;

      size_t notes = chord->notes().size();
      for (size_t i = 0; i < notes; ++i)
            ell.append(NoteEventList());

      bool arpeggio = false;
      if (chord->tremolo()) {
            renderTremolo(chord, ell);
            }
      else if (chord->arpeggio() && chord->arpeggio()->playArpeggio()) {
            renderArpeggio(chord, ell);
            arpeggio = true;
            }
      else
            renderChordArticulation(chord, ell, gateTime);

      // Check each note and apply gateTime
      for (unsigned i = 0; i < notes; ++i) {
            NoteEventList* el = &ell[i];
            if (!shouldRenderNote(chord->notes()[i])) {
                  el->clear();
                  continue;
                  }
            if (arpeggio)
                  continue; // don't add extra events and apply gateTime to arpeggio

            // If we are here then we still need to render the note.
            // Render its body if necessary and apply gateTime.
            if (el->size() == 0 && chord->tremoloChordType() != TremoloChordType::TremoloSecondNote) {
                  el->append(NoteEvent(0, ontime, 1000 - ontime - trailtime));
                  }
            if (trailtime == 0) // if trailtime is non-zero that means we have graceNotesAfter, so we don't need additional gate time.
                  for (NoteEvent& e : ell[i])
                        e.setLen(e.len() * gateTime / 100);
            }
      return ell;
      }

//---------------------------------------------------------
//   createGraceNotesPlayEvent
// as a side effect of createGraceNotesPlayEvents, ontime and trailtime (passed by ref)
// are modified.  ontime reflects the time needed to play the grace-notes-before, and
// trailtime reflects the time for the grace-notes-after.  These are used by the caller
// to effect the on/off time of the main note
//---------------------------------------------------------

void Score::createGraceNotesPlayEvents(const Fraction& tick, Chord* chord, int& ontime, int& trailtime)
      {
      QVector<Chord*> gnb = {};
      int nb = 0;
      // exclude grace chords where all notes are set not to play
      for (Chord* c : chord->graceNotesBefore()) {
            bool play = false;
            for (Note* note : c->notes()) {
                  if (note->play()) {
                        play = true;;
                        break;
                        }
                  }
            if (play) {
                  gnb.push_back(c);
                  nb++;
                  }
            }

      QVector<Chord*> gna = chord->graceNotesAfter();
      int na = gna.size();
      if (0 == nb + na) {
            return; // return immediately if no grace notes to deal with
            }
      // return immediately if the chord has a trill or articulation which effectively plays the graces notes.
      if (graceNotesMerged(chord)) {
            return;
            }
      // if there are graceNotesBefore and also graceNotesAfter, and the before grace notes are
      // not ACCIACCATURA, then the total time of all of them will be 50% of the time of the main note.
      // if the before grace notes are ACCIACCATURA then the grace notes after (if there are any).
      // get 50% of the time of the main note.
      // this is achieved by the two floating point weights: weighta and weightb whose total is 1.0
      // assuring that all the grace notes get the same duration, and their total is 50%.
      // exception is if the note is dotted or double-dotted; see below.
      float weighta = float(na) / (nb+na);
      float weightb = float(nb) / (nb+na);

      int graceDuration = 0;
      bool drumset = (getDrumset(chord) != nullptr);
      const qreal ticksPerSecond = tempo(tick) * DIVISION;
      const qreal chordTimeMS = (chord->actualTicks().ticks() / ticksPerSecond) * 1000;
      if (drumset) {
            int flamDuration = 15; //ms
            graceDuration = flamDuration / chordTimeMS * 1000; //ratio 1/1000 from the main note length
            ontime = graceDuration * nb;
            }
      else if (nb) {
            //
            //  render grace notes:
            //  simplified implementation:
            //  - grace notes start on the beat of the main note
            //  - duration: appoggiatura: 0.5  * duration of main note (2/3 for dotted notes, 4/7 for double-dotted)
            //              acciacatura: min of 0.5 * duration or 65ms fixed (independent of duration or tempo)
            //  - for appoggiaturas, the duration is divided by the number of grace notes
            //  - the grace note duration as notated does not matter
            //
            Chord* graceChord = gnb[0];
            if (graceChord->noteType() ==  NoteType::ACCIACCATURA || nb > 1) { // treat multiple subsequent grace notes as acciaccaturas
                  int graceTimeMS = 65 * nb;     // value determined empirically (TODO: make instrument-specific, like articulations)
                  // 1000 occurs below as a unit for ontime
                  ontime = qMin(500, static_cast<int>((graceTimeMS / chordTimeMS) * 1000));
                  weightb = 0.0;
                  weighta = 1.0;
                  }
            else if (chord->dots() == 1)
                  ontime = floor(667 * weightb);
            else if (chord->dots() == 2)
                  ontime = floor(571 * weightb);
            else
                  ontime = floor(500 * weightb);

            graceDuration = ontime / nb;
            }

      for (int i = 0, on = 0; i < nb; ++i) {
            QList<NoteEventList> el;
            Chord* gc = gnb.at(i);
            size_t nn = gc->notes().size();
            for (unsigned ii = 0; ii < nn; ++ii) {
                  NoteEventList nel;
                  nel.append(NoteEvent(0, on, graceDuration));
                  el.append(nel);
                  }

            if (gc->playEventType() == PlayEventType::Auto)
                  gc->setNoteEventLists(el);
            on += graceDuration;
            }
      if (na) {
            if (chord->dots() == 1)
                  trailtime = floor(667 * weighta);
            else if (chord->dots() == 2)
                  trailtime = floor(571 * weighta);
            else
                  trailtime = floor(500 * weighta);
            int graceDuration1 = trailtime / na;
            int on = 1000 - trailtime;
            for (int i = 0; i < na; ++i) {
                  QList<NoteEventList> el;
                  Chord* gc = gna.at(i);
                  size_t nn = gc->notes().size();
                  for (size_t ii = 0; ii < nn; ++ii) {
                        NoteEventList nel;
                        nel.append(NoteEvent(0, on, graceDuration1)); // NoteEvent(pitch,ontime,len)
                        el.append(nel);
                        }

                  if (gc->playEventType() == PlayEventType::Auto)
                        gc->setNoteEventLists(el);
                  on += graceDuration1;
                  }
            }
      }

//---------------------------------------------------------
//   createPlayEvents
//    create default play events
//---------------------------------------------------------

void Score::createPlayEvents(Chord* chord)
      {
      int gateTime = 100;

      Fraction tick = chord->tick();
      Slur* slur = 0;
      for (auto sp : _spanner.map()) {
            if (!sp.second->isSlur() || sp.second->staffIdx() != chord->staffIdx())
                  continue;
            Slur* s = toSlur(sp.second);
            if (tick >= s->tick() && tick < s->tick2()) {
                  slur = s;
                  break;
                  }
            }
      // gateTime is 100% for slured notes
      if (!slur) {
            Instrument* instr = chord->part()->instrument(tick);
            instr->updateGateTime(&gateTime, 0, "");
            }

      int ontime    = 0;
      int trailtime = 0;
      createGraceNotesPlayEvents(tick, chord, ontime, trailtime); // ontime and trailtime are modified by this call depending on grace notes before and after

      SwingParameters st = chord->staff()->swing(tick);
      int unit           = st.swingUnit;
      int ratio          = st.swingRatio;
      // Check if swing needs to be applied
      if (unit && !chord->tuplet()) {
            swingAdjustParams(chord, gateTime, ontime, unit, ratio);
            }
      //
      //    render normal (and articulated) chords
      //
      QList<NoteEventList> el = renderChord(chord, gateTime, ontime, trailtime);
      if (chord->playEventType() == PlayEventType::Auto)
            chord->setNoteEventLists(el);
      // don't change event list if type is PlayEventType::User
      }

void Score::createPlayEvents(Measure const * start, Measure const * const end)
      {
      if (!start)
            start = firstMeasure();

      int etrack = nstaves() * VOICES;
      for (int track = 0; track < etrack; ++track) {
            bool rangeEnded = false;
            for (Measure const * m = start; m; m = m->nextMeasure()) {
                  constexpr SegmentType st = SegmentType::ChordRest;

                  if (m == end)
                        rangeEnded = true;
                  if (rangeEnded) {
                        // The range has ended, but we should collect events
                        // for tied notes. So we'll check if this is the case.
                        const Segment* seg = m->first(st);
                        const Element* e = seg->element(track);
                        bool tie = false;
                        if (e && e->isChord()) {
                              for (const Note* n : toChord(e)->notes()) {
                                    if (n->tieBack()) {
                                          tie = true;
                                          break;
                                          }
                                    }
                              }
                        if (!tie)
                              break;
                        }

                  // skip linked staves, except primary
                  if (!m->score()->staff(track / VOICES)->primaryStaff())
                        continue;
                  for (Segment* seg = m->first(st); seg; seg = seg->next(st)) {
                        Element* e = seg->element(track);
                        if (e == 0 || !e->isChord())
                              continue;
                        createPlayEvents(toChord(e));
                        }
                  }
            }
      }

//---------------------------------------------------------
//   renderMetronome
///   add metronome tick events
//---------------------------------------------------------

void MidiRenderer::renderMetronome(const Chunk& chunk, EventMap* events)
      {
      const int tickOffset = chunk.tickOffset();
      Measure const * const start = chunk.startMeasure();
      Measure const * const end = chunk.endMeasure();

      for (Measure const * m = start; m != end; m = m->nextMeasure())
            renderMetronome(events, m, Fraction::fromTicks(tickOffset));
      }

//---------------------------------------------------------
//   renderMetronome
///   add metronome tick events
//---------------------------------------------------------

void MidiRenderer::renderMetronome(EventMap* events, Measure const * m, const Fraction& tickOffset)
      {
      int msrTick         = m->tick().ticks();
      qreal tempo         = score->tempomap()->tempo(msrTick);
      TimeSigFrac timeSig = score->sigmap()->timesig(msrTick).nominal();

      int clickTicks      = timeSig.isBeatedCompound(tempo) ? timeSig.beatTicks() : timeSig.dUnitTicks();
      int endTick         = m->endTick().ticks();

      int rtick;

      if (m->isAnacrusis()) {
            int rem = m->ticks().ticks() % clickTicks;
            msrTick += rem;
            rtick = rem + timeSig.ticksPerMeasure() - m->ticks().ticks();
            }
      else
            rtick = 0;

      for (int tick = msrTick; tick < endTick; tick += clickTicks, rtick += clickTicks)
            events->insert(std::pair<int,NPlayEvent>(tick + tickOffset.ticks(), NPlayEvent(timeSig.rtick2beatType(rtick))));
      }

//---------------------------------------------------------
//   renderMidi
//    export score to event list
//---------------------------------------------------------

void Score::renderMidi(EventMap* events, const SynthesizerState& synthState)
      {
      renderMidi(events, true, MScore::playRepeats, synthState);
      }

void Score::renderMidi(EventMap* events, bool metronome, bool expandRepeats, const SynthesizerState& synthState)
      {
      bool expandRepeatsBackup = masterScore()->expandRepeats();
      masterScore()->setExpandRepeats(expandRepeats);
      MidiRenderer::Context ctx(synthState);
      ctx.metronome = metronome;
      ctx.renderHarmony = true;
      MidiRenderer(this).renderScore(events, ctx);
      masterScore()->setExpandRepeats(expandRepeatsBackup);
      }

void MidiRenderer::renderScore(EventMap* events, const Context& ctx)
      {
      updateState();
      for (const Chunk& chunk : chunks) {
            renderChunk(chunk, events, ctx);
            }
      }

void MidiRenderer::renderChunk(const Chunk& chunk, EventMap* events, const Context& ctx)
      {
      // TODO: avoid doing it multiple times for the same measures
      score->createPlayEvents(chunk.startMeasure(), chunk.endMeasure());
      const ScoreTuningScope tuning(score);     // each note's tuning: temperament, accidental, its own (tuning.h)

      score->updateChannel();
      score->updateVelo();

      // the global synthesizer's method is the playback mode's (mscore/playbackmode.h): it wins
      // over a method saved in the score; without one (tests), the score's, else the default
      SynthesizerState s = ctx.synthState.method() != -1 ? ctx.synthState : score->synthesizerState();
      int method = s.method();
      int cc = s.ccToUse();

      // check if the score synth settings are actually set
      // if not, use the global synth state
      if (method == -1) {
            method = ctx.synthState.method();
            cc = ctx.synthState.ccToUse();

            if (method == -1) {
                  // fall back to defaults - this may be needed to pass tests,
                  // since sometimes the synth state is not init
                  method = 3;
                  cc = 2;
                  qDebug("Had to fall back to defaults to render measure");
                  }
            }

      DynamicsRenderMethod renderMethod = DynamicsRenderMethod::SIMPLE;
      switch (method) {
            case 0:
                  renderMethod = DynamicsRenderMethod::SIMPLE;
                  break;
            case 1:
                  renderMethod = DynamicsRenderMethod::SEG_START;
                  break;
            case 2:
                  renderMethod = DynamicsRenderMethod::FIXED_MAX;
                  break;
            case 3:
                  renderMethod = DynamicsRenderMethod::MS4;
                  break;
            default:
                  qDebug("Unrecognized dynamics method: %d", method);
                  break;
            }

      // a part's own playback mode (partplayback.h) over the global one: MS3 is MuseScore 3.6's
      // method (SND and changes at the start of a segment) on CC2 unless the global one has a CC;
      // MS4 and the sound library the MuseScore 4 note model
      const std::map<const Part*, PartPlayback> modes = PartPlaybackModes::read(score->masterScore());
      auto methodOf = [&](const Part* part) {
            switch (PartPlaybackModes::of(part, modes)) {
                  case PartPlayback::MS3:
                        return DynamicsRenderMethod::SEG_START;
                  case PartPlayback::MS4:
                  case PartPlayback::LIBRARY:
                        return DynamicsRenderMethod::MS4;
                  case PartPlayback::DEFAULT:
                        break;
                  }
            return renderMethod;
            };
      ms4Active.clear();
      for (Part* part : score->parts())
            if (methodOf(part) == DynamicsRenderMethod::MS4)
                  ms4Active.insert(part);

      // create note & other events
      for (Staff*& st : score->staves()) {
            StaffContext sctx;
            sctx.staff = st;
            sctx.method = methodOf(st->part());
            sctx.cc = sctx.method == renderMethod || cc > 0 ? cc : 2;
            sctx.renderHarmony = ctx.renderHarmony;
            renderStaffChunk(chunk, events, sctx);
            }
      ms4Mode = !ms4Active.empty();
      if (ms4Mode)
            renderMs4Dynamics(chunk, events);

      events->fixupMIDI();

      // create sustain pedal events
      renderSpanners(chunk, events);

      if (ms4Mode)
            finishLibraryEvents(chunk, events);

      if (ctx.metronome)
            renderMetronome(chunk, events);

      // NOTE:JT this is a temporary fix for duplicate events until polyphonic aftertouch support
      // can be implemented. This removes duplicate SND events.
      int lastChannel = -1;
      int lastController = -1;
      int lastValue = -1;
      for (auto i = events->begin(); i != events->end();) {
            if (i->second.type() == ME_CONTROLLER) {
                  auto& event = i->second;
                  // (a sound library part's events on one channel can go to several routes)
                  const int channel = event.isExternal() ? 0x10000 + event.extPort() * 16 + event.extChannel() : event.channel();
                  if (channel == lastChannel &&
                     event.controller() == lastController &&
                     event.value() == lastValue) {
                        i = events->erase(i);
                        }
                  else {
                        lastChannel = channel;
                        lastController = event.controller();
                        lastValue = event.value();
                        i++;
                        }
                  }
            else {
                  i++;
                  }
            }
      }

//---------------------------------------------------------
//   MidiRenderer::updateState
//---------------------------------------------------------

void MidiRenderer::updateState()
      {
      const QString modes = score->masterScore()->metaTag(PartPlaybackModes::metaTag);
      const QString controllers = score->masterScore()->metaTag(PartControllers::metaTag);
      const QString automation = score->masterScore()->metaTag(Automation::metaTag);
      if (library != SoundLib::current() || libGeneration != SoundLib::routesGeneration() || modes != partModes
          || controllers != partControllers || automation != partAutomation)
            needUpdate = true;
      if (needUpdate) {
            partModes = modes;
            partControllers = controllers;
            partAutomation = automation;
            // Update the related structures inside score
            // to avoid doing it multiple times on chunks rendering
            score->updateSwing();
            score->updateCapo();

            libParts.clear();
            libRoutes.clear();
            libLanes.clear();
            libLaneCents.clear();
            library = SoundLib::current();
            libGeneration = SoundLib::routesGeneration();
            if (library) {
                  const std::map<const Part*, PartControllers::Values> values = PartControllers::read(score->masterScore());
                  const std::map<const Part*, Automation::PartLanes> allLanes = Automation::read(score->masterScore());
                  const std::vector<SoundLib::Route> routes = SoundLib::routes(score, *library);
                  for (const SoundLib::Route& r : routes) {
                        if (r.patch != 0 || r.lane != 0)
                              continue;
                        Part* part = const_cast<Part*>(r.part);
                        LibPart& lp = libParts[part];
                        lp.route = r;
                        lp.velocityDynamics = library->velocityDynamics;
                        lp.text.build(score, part);
                        // automation: a lane takes its controller's place (its part value, its staff texts)
                        QSet<QString> automated;
                        for (const Automation::Lane& lane : Automation::lanes(part, allLanes)) {
                              LibPart::Auto a;
                              a.lane = lane;
                              a.cc = lane.cc();
                              if (a.cc < 0) {
                                    const auto& all = r.instrument->allControllers;
                                    for (int i = 0; i < int(all.size()); ++i) {
                                          if (all[size_t(i)].id != lane.target)
                                                continue;
                                          if (all[size_t(i)].cc >= 0)
                                                a.cc = all[size_t(i)].cc;
                                          else if (!all[size_t(i)].param.isEmpty() && SoundLib::output() == SoundLib::Output::PLUGIN)
                                                a.param = i;
                                          }
                                    }
                              if (a.cc < 0 && a.param < 0)
                                    continue;         // (a controller this library doesn't have)
                              automated.insert(lane.target);
                              lp.automation.push_back(a);
                              }
                        for (const SoundLib::Controller& c : r.instrument->allControllers) {
                              if (c.cc < 0)       // a plug-in parameter: set on the instance (SoundLibraryHost)
                                    continue;
                              if (automated.contains(c.id) || automated.contains(QString("cc%1").arg(c.cc)))
                                    continue;
                              lp.controllers.push_back({ c.cc, PartControllers::value(part, c, values),
                                                         SoundLib::controllerTexts(score, part, c) });
                              }
                        // the main patch's instrument plays all the part's routed patches, an
                        // instrument change only its own on the part's route
                        // (a patch's lanes follow it: copies of it for other tunings)
                        std::vector<std::vector<std::pair<int, int>>> outs;
                        for (const SoundLib::Route& e : routes) {
                              if (e.part != r.part)
                                    continue;
                              if (e.lane == 0) {
                                    lp.patches.push_back(e.instrument);
                                    outs.emplace_back();
                                    }
                              outs.back().push_back({ e.port, e.channel });
                              }
                        // varispeed: a note plays at its lane's tuning (within the tolerance of its own),
                        // so a note joining a lane never retunes what still sounds on it
                        if (library->varispeed && !r.instrument->kit) {
                              const SoundLib::LaneSettings ls = SoundLib::laneSettings(score, *library);
                              const SoundLib::Lanes l = SoundLib::lanes(score, part, lp.patches, ls.tolerance, ls.tail, ls.maxLanes);
                              for (const auto& nl : l.lane)
                                    if (nl.second > 0)
                                          libLanes[nl.first] = nl.second;
                              for (const auto& nc : l.cents)
                                    libLaneCents[nc.first] = nc.second;
                              }
                        for (const auto& ip : *part->instruments()) {
                              const SoundLib::LibInstrument* li = library->match(ip.second, part);
                              lp.instruments[ip.second] = li;
                              if (li)
                                    libRoutes[ip.second->channel(0)->channel()] = li == r.instrument ? outs
                                       : std::vector<std::vector<std::pair<int, int>>> { { outs.front().front() } };
                              }
                        }
                  }

            ms4Parts.clear();
            for (Part* part : score->parts()) {
                  Ms4::PartContext& ctx = ms4Parts[part];
                  const Instrument* instr = part->instrument();
                  ctx.family = Ms4::family(instr);
                  ctx.snd = instr->singleNoteDynamics();
                  if (qEnvironmentVariableIsSet("MS4_DEBUG_PARTS"))
                        qDebug("MS4PART %s id=%s musicXml=%s family=%d snd=%d", qPrintable(part->partName()), qPrintable(instr->getId()),
                               qPrintable(instr->instrumentId()), int(ctx.family), int(ctx.snd));
                  ctx.dynamics.build(score, part);
                  for (const auto& ip : *part->instruments())
                        ctx.sounds[ip.second] = Ms4::sounds(ip.second);
                  }

            updateChunksPartition();

            if (qEnvironmentVariableIsSet("MS4_DEBUG_REPEATS")) {
                  for (const RepeatSegment* rs : score->repeatList()) {
                        const Measure* m = score->tick2measure(Fraction::fromTicks(rs->tick));
                        qDebug("MS4REPEAT bar %d tick %d len %d utick %d utime %.3f count %d", m ? m->no() + 1 : -1,
                               rs->tick, rs->len(), rs->utick, rs->utime, rs->playbackCount);
                        }
                  }

            needUpdate = false;
            }
      }

//---------------------------------------------------------
//   MidiRenderer::canBreakChunk
///   Helper function for updateChunksPartition
///   Determines whether it is allowed to break MIDI
///   rendering chunk at given measure.
//---------------------------------------------------------

bool MidiRenderer::canBreakChunk(const Measure* last)
      {
      Score* score = last->score();

      // Check for hairpins that overlap measure end:
      // hairpins should be inside one chunk, if possible
      const int endTick = last->endTick().ticks();
      const auto& spanners = score->spannerMap().findOverlapping(endTick - 1, endTick);
      for (const auto& interval : spanners) {
            const Spanner* sp = interval.value;
            if (sp->isHairpin() && sp->tick2().ticks() > endTick)
                  return false;
            }

      // Repeat measures rely on the previous measure
      // being properly rendered, disallow breaking
      // chunk at repeat measure.
      if (const Measure* next = last->nextMeasure())
            for (Staff*& staff : score->staves()) {
                  if (next->isRepeatMeasure(staff))
                        return false;
                  }

      return true;
      }

//---------------------------------------------------------
//   MidiRenderer::updateChunksPartition
//---------------------------------------------------------

void MidiRenderer::updateChunksPartition()
      {
      chunks.clear();

      const RepeatList& repeatList = score->repeatList();

      for (const RepeatSegment* rs : repeatList) {
            const int tickOffset = rs->utick - rs->tick;

            if (!minChunkSize) {
                  // just make chunks corresponding to repeat segments
                  chunks.emplace_back(tickOffset, rs->firstMeasure(), rs->lastMeasure());
                  continue;
                  }

            Measure const * const end = rs->lastMeasure()->nextMeasure();
            int count = 0;
            bool needBreak = false;
            Measure const * chunkStart = nullptr;
            for (Measure const * m = rs->firstMeasure(); m != end; m = m->nextMeasure()) {
                  if (!chunkStart)
                        chunkStart = m;
                  if ((++count) >= minChunkSize)
                        needBreak = true;
                  if (needBreak && canBreakChunk(m)) {
                        chunks.emplace_back(tickOffset, chunkStart, m);
                        chunkStart = nullptr;
                        needBreak = false;
                        count = 0;
                        }
                  }
            if (chunkStart) // last measures did not get added to chunk list
                  chunks.emplace_back(tickOffset, chunkStart, rs->lastMeasure());
            }

      if (score != repeatList.score()) {
            // Repeat list may belong to another linked score (e.g. MasterScore).
            // Update chunks to make them contain measures from the currently
            // rendered score.
            for (Chunk& ch : chunks) {
                  Measure* first = score->tick2measure(ch.startMeasure()->tick());
                  Measure* last = score->tick2measure(ch.lastMeasure()->tick());
                  ch = Chunk(ch.tickOffset(), first, last);
                  }
            }
      }

//---------------------------------------------------------
//   MidiRenderer::getChunkAt
//---------------------------------------------------------

MidiRenderer::Chunk MidiRenderer::getChunkAt(int utick)
      {
      updateState();

      auto it = std::upper_bound(chunks.begin(), chunks.end(), utick, [](int utick, const Chunk& ch) {
                  return utick < ch.utick1();
                  });
      if (it == chunks.begin())
            return Chunk();
      --it;
      const Chunk& ch = *it;
      if (ch.utick2() <= utick)
            return Chunk();
      return ch;
      }

//---------------------------------------------------------
//   RangeMap::setOccupied
//---------------------------------------------------------

void RangeMap::setOccupied(int tick1, int tick2)
      {
      auto it1 = status.upper_bound(tick1);
      const bool beforeBegin = (it1 == status.begin());
      if (beforeBegin || (--it1)->second != Range::BEGIN) {
            if (!beforeBegin && it1->first == tick1)
                  status.erase(it1);
            else
                  status.insert(std::make_pair(tick1, Range::BEGIN));
            }

      const auto it2 = status.lower_bound(tick2);
      const bool afterEnd = (it2 == status.end());
      if (afterEnd || it2->second != Range::END) {
            if (!afterEnd && it2->first == tick2)
                  status.erase(it2);
            else
                  status.insert(std::make_pair(tick2, Range::END));
            }
      }

//---------------------------------------------------------
//   RangeMap::occupiedRangeEnd
//---------------------------------------------------------

int RangeMap::occupiedRangeEnd(int tick) const
      {
      const auto it = status.upper_bound(tick);
      if (it == status.begin())
            return tick;
      const int rangeEnd = (it == status.end()) ? tick : it->first;
      if (it->second == Range::END)
            return rangeEnd;
      return tick;
      }
}
