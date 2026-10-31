//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2019 Werner Schweer and others
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//=============================================================================

#ifndef __RENDERMIDI_H__
#define __RENDERMIDI_H__

#include <set>
#include <tuple>

#include "fraction.h"
#include "measure.h"

#include "ms4playback.h"
#include "soundlibrary.h"
#include "automation.h"
#include "trackdelays.h"
#include "performancetechnique.h"
#include "evenlevels.h"
#include <functional>
#include <limits>

namespace Ms {

class EventMap;
class MasterScore;
namespace ContextLevel { struct MicValues; }
class Staff;
class SynthesizerState;

enum class DynamicsRenderMethod : signed char {
      FIXED_MAX,
      SEG_START,
      SIMPLE,
      MS4               // MuseScore 4's note model and dynamics (ms4playback.h)
      };

//---------------------------------------------------------
//   RangeMap
///   Helper class to keep track of status of status of
///   certain parts of score or MIDI representation.
//---------------------------------------------------------

class RangeMap {
      enum class Range { BEGIN, END };
      std::map<int, Range> status;

   public:
      void setOccupied(int tick1, int tick2);
      void setOccupied(std::pair<int, int> range) { setOccupied(range.first, range.second); }

      int occupiedRangeEnd(int tick) const;

      void clear() { status.clear(); }
      };

//---------------------------------------------------------
//   MidiRenderer
///   MIDI renderer for a score
//---------------------------------------------------------

class MidiRenderer {
      Score* score{nullptr};
      bool needUpdate = true;
      std::map<const Part*, Ms4::PartContext> ms4Parts;     // DynamicsRenderMethod::MS4, per part
      bool ms4Mode { false };                               // the chunk being rendered uses it
      std::set<const Part*> ms4Active;                      // its parts that use it (partplayback.h)
      QString partModes;                                    // the parts' playback modes as last read
      QString partAutomation;                               // the parts' automation lanes as last read
      // Engine lanes (enginelane.h; rendermidi.cpp › Engine lanes): a part's engine from each tick on, from tick 0 (its
      // own engine there), a part without a lane (or for Live's clips) not listed. The switches as read (updateState),
      // their kinds resolved for the chunk (renderWindow: a library the part has no routes of plays MS4)
      enum EngineKind : signed char { ENGINE_MS3, ENGINE_MS4, ENGINE_LIB };
      struct EngineSeg {
            int tick { 0 };
            int kind { ENGINE_MS4 };
            int fadeMs { 0 };
            };
      std::map<const Part*, std::vector<std::pair<int, std::pair<QString, int>>>> engineSwitches;   // tick -> engine, fade
      std::map<const Part*, std::vector<EngineSeg>> engineSegs;
      const Part* libSuppress { nullptr };                  // a pass of a part's built-in notes: it has no LibPart
      int engineAt(const Part* part, int tick) const;      // its kind (-1: the part has no switches)
      bool engineCrosses(const Part* part, int tick1, int tick2) const;  // another engine at tick2 than at tick1
      bool forLiveClips { false };
      bool flatParamEvents { false };                       // setFlatParamEvents
      bool writtenTiming { false };                         // setWrittenTiming

      // parts played by an external sound library (soundlibrary.h, MS4 note model only)
      struct LibPart {
            SoundLib::Route route;                          // the main patch's
            const SoundLib::Library* library { nullptr };   // the library it plays (of several: SoundLib::libraryOf)
            SoundLib::TextTechniques text;
            // the main patch's MIDI controllers (vibrato …): the part's setting, its flat lane's or the map's default
            // (partcontrollers.h; -1: the patch's own), and the staff text that changes it (tick -> value)
            struct Ctrl {
                  int cc;
                  int value;
                  std::map<int, int> texts;
                  };
            std::vector<Ctrl> controllers;
            // the part's automation lanes (automation.h) its main patch can play: a MIDI controller's
            // (cc) or a plug-in parameter's (param: the controller's index in allControllers)
            // A lane on a parameter of the part's Live track ("live:<d>/<p>": LiveClips::liveLanes) plays only where Live
            // plays the score (forLiveClips): live, its index among them (a parameter event of controller
            // LiveClips::LIVE_PARAM, that index as its second byte)
            struct Auto {
                  Automation::Lane lane;
                  int cc { -1 };
                  int param { -1 };
                  int live { -1 };
                  };
            std::vector<Auto> automation;
            // a lane on the library's dynamics controller (CC1): from its first point it, not the notation's
            // dynamics, sends that controller (the notation still sets the shorts' velocities)
            int dynamicsLaneFrom { std::numeric_limits<int>::max() };
            std::map<const Instrument*, const SoundLib::LibInstrument*> instruments;
            QStringList velocityDynamics;                   // Library::velocityDynamics
            std::vector<const SoundLib::LibInstrument*> patches;    // routed: the main one, then extras
            // the patches a note of an instrument of the part chooses from
            std::vector<const SoundLib::LibInstrument*> patchesFor(const SoundLib::LibInstrument* li) const {
                  return li == route.instrument ? patches : std::vector<const SoundLib::LibInstrument*> { li };
                  }
            };
      // the library the parts play (the current one: SoundLib::current; without it, the first a part names) and every
      // library the score's parts play (SoundLib::scoreLibraries: the current one first; each part's in LibPart::library,
      // each route's in libSlotLibrary)
      std::shared_ptr<const SoundLib::Library> library;
      std::shared_ptr<const SoundLib::Library> libCurrent;
      std::vector<std::shared_ptr<const SoundLib::Library>> libLibraries;
      std::map<int, const SoundLib::Library*> libSlotLibrary;
      // the library of a route (slot: port * 16 + channel), else the one the parts play
      const SoundLib::Library* libAt(int slot) const {
            auto it = libSlotLibrary.find(slot);
            return it != libSlotLibrary.end() ? it->second : library.get();
            }
      int libGeneration = -1;
      std::map<const Part*, LibPart> libParts;
      // every part's technique texts (SoundLib::TextTechniques), for their levels (TextState::db; articulation.h MarcatoLevel)
      std::map<const Part*, SoundLib::TextTechniques> partTexts;
      double textLevel(const Chord* chord) const; // the level of the technique text in effect at the chord, dB
      double levelOf(const Note* note) const;     // the note's chord's articulations' and its technique text's levels, dB
      // each part's Velocity lane (automation.h VELOCITY_TARGET: an Edit-in-MuseScore clip tab's), with points: every
      // note of MuseScore 3's playback shaped by it (playNote), as the MuseScore Link device or the clip's notes do in Live
      std::map<const Part*, Automation::Lane> velocityLanes;
      // channel -> MIDI out port and channel per patch, per lane (SoundLib::Lanes: copies of a patch by tuning)
      std::map<int, std::vector<std::vector<std::pair<int, int>>>> libRoutes;
      std::map<const Note*, int> libLanes;                        // a note's lane, when not 0
      std::map<const Note*, const Note*> libUnison;               // a mono patch's unison duplicate -> the note that plays it (SoundLib::Lanes::unison): not sent
      std::map<const Note*, int> libDivisi;                       // [levels] divisi: a mono patch's note's voices sounding at its start (> 1; SoundLib::Lanes::sounding)
      std::map<const Note*, double> libLaneCents;                 // a note's tuning as its lane plays it (varispeed)
      // channel -> per patch: its pitch bend range in cents (SoundLib::LibInstrument::bendCents; 0: none)
      std::map<int, std::vector<double>> libBend;
      std::map<const Note*, const Note*> libGlideFrom;            // a legato transition's note before (its lane glides)
      // its bend's glide, ms after its note-on: from leave to arrive (the patch's own transition as measured:
      // Articulation::bendGlideAt; else both at its legato delay)
      // (smoothing: bendSmoothing; uncut, [slurs] microtoneBends 1 / 2 on a fingered transition: the glide runs to its arrival
      // past the next note-on, up to the next transition's leave: libraryPitchBends)
      struct LibGlide { double leaveMs; double arriveMs; double smoothingMs { 0 }; bool uncut { false }; };
      std::map<const Note*, LibGlide> libGlide;
      // [levels] attackLevels (libraryNoteLevels): a library attack's (no legato transition) measured onset, ms after its
      // note-on (a plain Performance note: its patch's plainAttack; else its articulation's attack onset, startOnset), and
      // every library note's release (its articulation's release= to 30 dB under, else its patch's longest: laneRing / 2)
      std::map<const Note*, double> libAttackOnset;
      std::map<const Note*, double> libReleaseMs;
      // (2: a plain Performance attack's patch shift, ms: its plainShift, its events' track delay; 0 otherwise)
      std::map<const Note*, double> libAttackShiftMs;
      // (note, its note-on's utick) -> the utick its level went at instead (libraryNoteLevels; libraryEvenLevels starts and
      // ends its glide there)
      std::map<std::pair<const Note*, int>, int> libAttackLevelAt;
      // [slurs] speedFromWritten (soundlibrary.h speedFromWritten): a legato note-on's speed controller value, from the written
      // spacing (SoundLib::speedValue); put right before the note-on on its route (librarySpeeds)
      std::map<const Note*, int> libSpeed;
      // [slurs] microtoneBends 1 (microtoneSpelling): a microtonal note on a bending lane sent on another key than its
      // written one, and the bend it then plays (cents from that key)
      std::map<const Note*, int> libSpellKey;
      std::map<const Note*, double> libSpellCents;
      // … each (note, tickOffset)'s spelling as first made (key, or -1: its own; cents): playback's chunks render a note in
      // more than one window (renderChunk), the first with the notes before it as the whole render has them
      std::map<std::pair<const Note*, int>, std::pair<int, double>> libSpelled;
      int libChunkStart = 0;                                      // the chunk being rendered: its first utick
      int libOnsetEarly = 0;                                      // SoundLib::onsetEarly, percent (this chunk)
      bool libOnsetByPitch = false;                               // [heldNotes] byPitch (0: the patch's median onset)
      bool libAvoidBad = true;                                    // [notes] avoidBadSamples (the map's avoid)
      int libTransitionsEarly = 0;                                // [slurs] transitionsEarly 0-3 (this chunk; not written timing)
      // [slurs] plainTiming (this chunk; not written timing): plain Performance notes start early each by its own model (an
      // attack by the patch's plainAttack, a transition by its modelled arrival as transitionsEarly 1, or the mode set
      // there), instead of the patch's one plainShift
      bool libPlainTiming = false;
      bool libStaccatoLength = false;                             // [shorts] staccatoLength (this chunk; written timing too)
      int libMicrotoneBends = 0;                                  // [slurs] microtoneBends 0-3 (this chunk)
      int libPlainAuto = 0;                                       // [slurs] plainAuto 0-2 (this chunk)
      bool libRunBends = false;                                   // [slurs] runBends (this chunk)
      // [slurs] runBends (libraryPitchBends): a plain note on a patch whose articulation has runLag: its written start and
      // end (seconds), how early its patch's events go (plainShift, s: its heard start is written + this in event time), and
      // the patch's pitch model for the transition reaching it (an attack: none; a transition with delayMs < 0: none of its kind,
      // its key from its heard start)
      struct LibRunNote { double writtenOn; double writtenOff; double shift; SoundLib::Articulation::RunLag lag; bool transition; };
      std::map<const Note*, LibRunNote> libRunNote;
      // playback settings (libmscore/playbacksettings.h) for this chunk
      // a held note started early by its onset after a note on the same patch: that note keeps at least libKeep seconds
      // as played ([legato] keepMs); a transition's arrival after a short note (its bend's glide): libFastDelay ([legato]
      // fastShare, fastFullMs)
      double libKeep = 0.04, libFastShare = 0.50, libFastFull = 0.38;
      double libFastDelay(double delayMs, double lenBefore) const;
      // a library note's start as played (note, tickOffset -> utick), this chunk: what an early start after it may take
      std::map<std::pair<const Note*, int>, int> libPlayedOn;
      QString playbackSettingsTag;
      int playbackGeneration = -1;
      // held notes started early by their onset (this chunk): what ends on their patch between their new and
      // their written start ends at the new one, and their switch and controllers move with them (finishLibraryEvents)
      struct LibShift { int channel; int patch; int on; int written; int chordTick; };
      std::vector<LibShift> libShifts;
      // a Performance note's join (rendermidi.cpp performanceNote; finishLibraryEvents): the note before on its patch
      // (key) ends ms after (overlap) or before (re-attack gap) the note's note-on as played
      // (shortenOnly: the note before ends there only if it would end later: [slurs] plainPerformance's staccato gap)
      struct LibJoin { int channel; int patch; int key; const Note* note; int written; double ms; bool shortenOnly = false; };
      std::vector<LibJoin> libJoins;
      // a note's own level on its route by a controller (this chunk; libraryNoteLevels): a marcato's level
      // (articulation.h MarcatoLevel) on a patch whose level is the dynamics controller, and its track level
      // (trackdelays.h: volumeDb). Several sources on one note add up in dB on CC11 (volumeDb on top of a CC11 map)
      struct LibLevel {
            int controller { -1 };            // CC11 (softer) or the dynamics CC (louder); -1: none
            std::function<int(int)> map;      // the controller's value in force -> the note's
            int fallback { 127 };             // the value in force when the route has none before the note
            double volumeDb { 0.0 };          // more on the expression CC (CC11, a plain volume: x 10^(dB / 20))
            // [levels] realRange (contextlevel.h › Real range): CC11 dB by the dynamics CC in force as written (0 … 127:
            // realDb at the note's pitch), plus realExtra (a capped short's rest), less rangeTop (the part's R)
            std::shared_ptr<const std::vector<double>> range;
            double realExtra { 0.0 };
            // [levels] evenKeys: the articulation's keyLevel at the note's pitch (Articulation::keyLevels), on top of realDb:
            // in the part's R too, so a raised key has CC11 headroom
            double keyDb { 0.0 };
            double rangeTop { 0.0 };
            const Part* rangePart { nullptr };
            };
      std::map<const Note*, LibLevel> libLevels;
      // the per-note levels still in force on each route at a chunk's end (libraryNoteLevels: the level of the route's
      // last note; libraryEvenLevels: its glide), by the chunk's utick2: the next chunk takes them up, so what still sounds
      // there (a note held over, a release) keeps its level until the route's next note-on (LibCarry in rendermidi.cpp;
      // cleared with the renderer's state, updateState)
      struct LibCarry;
      std::map<int, std::shared_ptr<LibCarry>> libCarry;
      // playback's chunks rendered in windows (renderChunk): each boundary's cut (utick -> from where the chunk after it owns
      // the events: the boundary, or its pre-roll when that chunk was rendered first); cleared with the renderer's state
      std::map<int, int> libCut;
      bool libKeepWindow { false };     // (setKeepWindow)
      int libSnapshotAt { -1 };         // (renderChunk: the next chunk's window's start, where this one leaves its levels)
      static constexpr double WINDOW_BEFORE_S = 6.0;            // the window: this long before the chunk (seconds) …
      static constexpr double WINDOW_AFTER_S = 4.0;             // … and after it
      // track delays and levels (trackdelays.h): the parts with any, and each route's part and patch (port * 16 + channel)
      std::map<const Part*, TrackDelays::Delays> libTrackDelays;
      std::set<const Note*> libPlainNotes;                      // [slurs] plainPerformance: the notes played plain
      std::map<std::tuple<const Chord*, int, int>, double> libContextGain;  // [levels] context: a chord's phrase gain (contextlevel.h), per tickOffset and kind
      std::map<int, std::pair<const Part*, const SoundLib::LibInstrument*>> libTrackRoutes;
      // [levels] evenLevels (evenlevels.h): every route's part and patch, whether the setting is on, what the measurement
      // reads (setEvenLevelsInfo)
      std::map<int, std::pair<const Part*, const SoundLib::LibInstrument*>> libRoutePatches;
      bool libEvenOn { false };
      std::vector<EvenLevels::NoteInfo>* evenInfo { nullptr };
      double libDelayLead { 0.0 };      // ms everything plays later by, so the earliest negative delay fits (0: none)
      // the start lead (libraryDelayLead): the ms a note's early start (its onset) would go before the score's start,
      // per note (filled where the start cuts it: the first chunk's notes), their largest, known once the first chunk
      // is rendered (a later chunk rendered first renders it once more for this: libProbing)
      std::map<const Note*, double> libStartLost;
      double libStartLead { 0.0 };
      bool libStartKnown { false };
      bool libProbing { false };
      std::map<int, double> startLostByRoute(const EventMap* events) const;
      QString trackDelaysTag;
      int minChunkSize = 0;

   public:
      // a library note's choice as rendered (each switch put): the whole-score audit reads it (playbackaudit.h),
      // the Velocity and Join lanes too (mscore/notelanes.h); playback sets none
      enum class LibPerformance : char { NONE, ATTACK, TRANSITION };      // on a Performance patch (performanceNote)
      struct LibTrace {
            const Note* note;
            int utick;                                // its written start (as rendered: an ornament's sub-note's)
            SoundLib::Choice choice;
            const SoundLib::LibInstrument* patch;     // the chosen patch
            int velocity { -1 };                      // as sent (its own, Note::libraryVelocity, included)
            LibPerformance performance { LibPerformance::NONE };
            int joinMs { PerformanceTechnique::JOIN_AUTO };   // the note before ends this long after its note-on
                                                              // (< 0: before it); JOIN_AUTO: not joined
            // a Performance attack with its own onset that timed the note's early start ([slurs] smoothStart: smooth;
            // Articulation::startOnset); nullptr: the articulation's onset
            const SoundLib::Articulation::Technique* attack { nullptr };
            double glideLeaveMs { -1 };                       // a bent transition's glide (libraryPitchBends), ms
            double glideArriveMs { -1 };                      // after its note-on; -1: none
            // a technique on velocity: the velocity a plain note of it plays at each MS4 dynamic (DYNAMIC_LEVELS, ppp
            // … fff: libVelocity; without the articulations' Inspector levels); empty: its velocity is no dynamic
            std::vector<int> dynamicVelocities;
            // played as a legato transition from this note (the first of its tie chain; legatoTransition /
            // performanceNote), interval semitones from it (the playbackaudit's DROPPED and LEAP); nullptr: an attack
            const Note* from { nullptr };
            int interval { 0 };
            // [slurs] speedFromWritten: the speed controller sent right before its note-on (the patch's speedCC, from the
            // written spacing to `from`'s note-on); -1: none
            int speed { -1 };
            // [slurs] transitionsEarly: how early the transition was meant to start (ms; -1: not moved), and whether its patch
            // has no measured glide so that modes 2 / 3 fell back to the arrival (mode 1's model)
            double earlyMeantMs { -1 };
            bool earlyFallback { false };
            // [slurs] plainAuto: a plain new bow stroke on the bowed transition (heard the patch's plainShift + bowedEarly
            // after its note-on: the audit)
            bool plainBowed { false };
            };
      // SSO's Performance legato doesn't play a legato note-on less than this after the legato note-on before on its
      // route (at velocity 64 or lower; from reading its script, behaviour only): the audit's DROPPED, and the least a
      // transition started early ([slurs] transitionsEarly) leaves the note before
      static constexpr double TRANSITION_MIN_GAP_MS = 15.0;
      // [slurs] transitionsEarly 2: a legato note on a bowed string is heard this long after its pitch change starts
      // (Polfreman 2013, "Comparing onset detection and perceptual attack time", ISMIR 2013: a modelled bowed string,
      // 23.5 ms, SD 22.8)
      static constexpr double HEARD_AFTER_LEAVE_MS = 23.5;
      // [slurs] runBends (libraryPitchBends): a run's notes are written closer than this (s) and touch (the note before ends
      // within RUN_TOUCH_S of the next start); the bend is sent every RUN_STEP_S as the value that best meets the wanted heard
      // bend over the next RUN_HORIZON_S (chosen offline on Whence: 20 / 30 / 40 ms horizons, 5 / 10 / 20 ms steps); after a
      // run's last note the correction runs on RUN_TAIL_S past its written end
      static constexpr double RUN_FAST_S = 0.25;
      static constexpr double RUN_TOUCH_S = 0.03;
      static constexpr double RUN_STEP_S = 0.010;
      static constexpr double RUN_HORIZON_S = 0.030;
      static constexpr double RUN_TAIL_S = 0.3;
      // ... and no send more than RUN_SLEW_C from the one before (2026-10-30: unbounded, a least-squares send swung between the
      // range's ends, ±99.6 c, within 10 ms around Whence's bowed beat notes, 54-117 steps of over 100 c a part in bars 1-14;
      // capped at 80: 1-3, the in-tune shares within a few points, runlag_sim.py SLEW)
      static constexpr double RUN_SLEW_C = 80.0;
      // [slurs] plainPerformance (abl.py's PLAIN on branch qt-bends, the variant the owner chose by ear, 2026-10-17): every
      // plain note's velocity (the owner's "velocity 64"), and how long before a staccato's neighbour on the patch it ends
      // (and the note before it ends before it): SSO's Performance legato joins a note-on less than 40 ms after the note-off
      // before as a transition (the map's legatoGap, 39 ms, measured), so a spiccato attack needs 45 ms (abl.py's margin)
      static constexpr int PLAIN_VELOCITY = 64;
      // [slurs] microtoneBends 1: a note this far from 12-TET (cents) is microtonal (microtoneSpelling may send it on another key)
      static constexpr double MICROTONE_MIN_CENTS = 1.0;
      static constexpr double PLAIN_STACCATO_GAP_MS = 45.0;
      // [slurs] plainAccents 2 / 3: an accent or marcato on the accented attack plays this much louder than the spiccato
      // attack at PLAIN_VELOCITY, by CC11 against the map's measured accentedLevel (Articulation::accentedLevelAt):
      // real players' sf at f over the unaccented notes around it, +5.0 dB pooled over the five string sections (90 %
      // [+3.8, +6.0]; the sections +4.1 .. +5.5 agree within one instance's noise, so one value), the Spheres dataset's
      // Mozart K. 550 (Colibri Ensemble, arXiv 2511.21247), each section's close stem, perceived peak in the 0.4 s after
      // the onset, the measure accentedLevel uses (private accent-spheres study, 2026-10-20; readings approved by the
      // owner, 2026-10-21). An assumption: K. 550's strings have only sf, no ">" or marcato; a ">" may play softer
      static constexpr double PLAIN_ACCENT_DB = 5.0;
      static const int DYNAMIC_LEVELS[8];         // MS4's levels of ppp … fff (ms4playback.cpp's dynamic table)
      static const char* const DYNAMIC_NAMES[8];
   private:
      std::vector<LibTrace>* libTrace { nullptr };
      std::map<const Part*, double>* libRangeTops { nullptr };
      // [levels] realRange: realDb by the dynamics CC value per (patch, pitch) (contextlevel.h)
      std::map<std::pair<const SoundLib::LibInstrument*, int>, std::shared_ptr<const std::vector<double>>> libRangeTables;
      // [levels] micCompensation: the automated mics of each (part, patch) (contextlevel.h › Mics)
      std::map<std::pair<const Part*, const SoundLib::LibInstrument*>, std::shared_ptr<const ContextLevel::MicValues>> libMics;

   public:
      class Chunk {
            int _tickOffset;
            Measure const * first;
            Measure const * last;

         public:
            Chunk(int tickOffset, Measure const * fst, Measure const * lst)
               : _tickOffset(tickOffset), first(fst), last(lst) {}

            Chunk() // "invalid chunk" constructor
               : _tickOffset(0), first(nullptr), last(nullptr) {}

            operator bool() const { return bool(first); }
            int tickOffset() const { return _tickOffset; }
            Measure const * startMeasure() const { return first; }
            Measure const * endMeasure() const { return last ? last->nextMeasure() : nullptr; }
            Measure const * lastMeasure() const { return last; }
            int tick1() const { return first->tick().ticks(); }
            int tick2() const { return last ? last->endTick().ticks() : tick1(); }
            int utick1() const { return tick1() + tickOffset(); }
            int utick2() const { return tick2() + tickOffset(); }
            };

   private:
      std::vector<Chunk> chunks;

      struct StaffContext
            {
            Staff* staff{nullptr};
            DynamicsRenderMethod method{DynamicsRenderMethod::SIMPLE};
            int cc{0};
            bool renderHarmony{false};
            int engine{-1};         // an Engine lane's pass: only the chords on this engine (EngineKind; -1: all)
            bool extra{false};      // not the staff's first pass: no program changes, no chord symbols
            };

      void updateChunksPartition();
      Chunk libWindow(const Chunk&, bool* segmentStart, bool* segmentEnd, bool* windowStart, bool* windowEnd) const;
      static bool canBreakChunk(const Measure* last);
      bool libSlurAcross(const Measure* last) const;
      bool libNoteAfter(const Measure* last) const;
      void updateState();

      void renderStaffChunk(const Chunk&, EventMap* events, const StaffContext& sctx);
      void renderSpanners(const Chunk&, EventMap* events);
      void renderMetronome(const Chunk&, EventMap* events);
      void renderMetronome(EventMap* events, Measure const * m, const Fraction& tickOffset);

      void collectMeasureEvents(EventMap* events, Measure const * m, const MidiRenderer::StaffContext& sctx, int tickOffset);
      void collectMeasureEventsSimple(EventMap* events, Measure const * m, const StaffContext& sctx, int tickOffset);
      void collectMeasureEventsDefault(EventMap* events, Measure const * m, const StaffContext& sctx, int tickOffset);
      void collectMeasureEventsMs4(EventMap* events, Measure const * m, const StaffContext& sctx, int tickOffset);
      void renderMs4Dynamics(const Chunk&, EventMap* events, const Part* only = nullptr);   // (only: that part alone)
      // Engine lanes (rendermidi.cpp › Engine lanes): a switching part's staff pass by pass, its MS4 dynamics, its pedal on
      // both engines, a fade's levels
      void renderEngineStaff(const Chunk&, EventMap* events, const StaffContext& sctx, DynamicsRenderMethod globalMethod, int cc);
      void renderEngineDynamics(const Chunk&, EventMap* events);
      void renderEnginePedals(const Chunk&, EventMap* events);
      void mergeBuiltIn(EventMap* events, const EventMap& builtIn, bool mark);
      SoundLib::Choice libraryChoice(const LibPart& lp, const SoundLib::LibInstrument& li, const Note* note,
                                     const std::vector<Ms4::ArtRef>& noteArts, int tick, int ticks) const;
      static void putLibrarySwitch(EventMap* events, const SoundLib::LibInstrument& li, int channel,
                                   const SoundLib::Choice& choice, int utick, int staffIdx);
      void finishLibraryEvents(const Chunk&, EventMap* events);
      void libraryMaxVelocity(const Chunk&, EventMap* events);
      void libraryGuitar(const Chunk&, EventMap* events);              // guitarplayback.cpp
      double guitarStartLeadMs() const;                                // … the start lead a capo at the start needs
      void libraryPitchBends(const Chunk&, EventMap* events);
      void librarySpeeds(const Chunk&, EventMap* events);
      void libraryNoteLevels(const Chunk&, EventMap* events);
      void libraryTrackDelays(const Chunk&, EventMap* events);
      void libraryTimingOverrides(const Chunk&, EventMap* events);      // livetimingoverride.h
      void libraryEvenLevels(const Chunk&, EventMap* events);           // evenlevels.h
      void libraryTransitionTails(const Chunk&, EventMap* events);      // [levels] transitionLevels 2
      void libraryDelayLead(const Chunk&, EventMap* events);
      void libraryLowCut(const Chunk&, EventMap* events);              // [levels] lowCut 2 (followcut.h)
      void libraryHostedParameters(const Chunk&, EventMap* events);    // soundlibrary.h Controller::hosted

   public:
      explicit MidiRenderer(Score* s) : score(s) {}

      struct Context
            {
            const SynthesizerState& synthState;
            bool metronome{true};
            bool renderHarmony{false};
            Context(const SynthesizerState& ss) : synthState(ss) {}
            };
   private:
      void renderWindow(const Chunk&, EventMap* events, const Context& ctx);     // (renderChunk: a chunk or its window)
   public:

      void renderScore(EventMap* events, const Context& ctx);
      void renderChunk(const Chunk&, EventMap* events, const Context& ctx);

      void setScoreChanged() { needUpdate = true; }
      // rendering the clips Live plays (liveclips.h): plug-in parameter lanes as ME_PARAMETER events whatever the
      // output (the MuseScore Link device sets them in Live), lanes Live plays itself (Lane::playedByLive) left out
      void setForLiveClips(bool v) { forLiveClips = v; needUpdate = true; }
      // a flat lane of a plug-in parameter (a controller's setting, partcontrollers.h) as parameter events too, not only on
      // the instance: for the MuseScore Link device while Live plays the score (liveclips.h), which sets Kontakt's
      // parameter in a set MuseScore didn't write. Off: the sets Create Live Set writes have the setting in Kontakt's state
      void setFlatParamEvents(bool v) { flatParamEvents = v; needUpdate = true; }
      // the library notes at their written times (the note model's, in ticks): none of the timing in seconds the
      // renderer gives them otherwise (a held note early by its onset with keepMs' cap, the note before cut at that early
      // start, a Performance note's join). Live's clips hold these (the owner, 2026-10-11); livetiming.h pairs them with
      // the played render's and puts that timing back at playback (Track Delay and the MuseScore Link device)
      void setWrittenTiming(bool v) { writtenTiming = v; needUpdate = true; }
      void setMinChunkSize(int sizeMeasures) { minChunkSize = sizeMeasures; needUpdate = true; }
      // renderChunk gives a chunk's whole window (renderChunk: the measures before it too, with the controllers and switches
      // they leave in force), not only the chunk's own events: a clicked note's state in force (notepreview.h)
      void setKeepWindow(bool v) { libKeepWindow = v; }
      void setLibraryTrace(std::vector<LibTrace>* trace) { libTrace = trace; }
      // every note-on on a Performance legato patch as rendered, with its context (evenlevels.h; the measurement job)
      void setEvenLevelsInfo(std::vector<EvenLevels::NoteInfo>* info) { evenInfo = info; }
      // [levels] realRange's headroom (ContextLevel::realRangeTops): each part's largest realDb + realExtra as rendered
      void setRealRangeTops(std::map<const Part*, double>* tops) { libRangeTops = tops; }

      Chunk getChunkAt(int utick);

      static const int ARTICULATION_CONV_FACTOR { 100000 };
      };

class Spanner;
extern bool glissandoPitchOffsets(const Spanner* spanner, std::vector<int>& pitchOffsets);

} // namespace Ms

#endif
