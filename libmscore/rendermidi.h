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
      QString partControllers;                              // the parts' library controllers as last read
      QString partAutomation;                               // the parts' automation lanes as last read
      bool forLiveClips { false };
      bool writtenTiming { false };                         // setWrittenTiming

      // parts played by an external sound library (soundlibrary.h, MS4 note model only)
      struct LibPart {
            SoundLib::Route route;                          // the main patch's
            SoundLib::TextTechniques text;
            // the main patch's MIDI controllers (vibrato …): the part's value (-1: the patch's
            // own), and the staff text that changes it (tick -> value)
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
      std::shared_ptr<const SoundLib::Library> library;
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
      std::map<const Note*, double> libLaneCents;                 // a note's tuning as its lane plays it (varispeed)
      // channel -> per patch: its pitch bend range in cents (SoundLib::LibInstrument::bendCents; 0: none)
      std::map<int, std::vector<double>> libBend;
      std::map<const Note*, const Note*> libGlideFrom;            // a legato transition's note before (its lane glides)
      // its bend's glide, ms after its note-on: from leave to arrive (the patch's own transition as measured:
      // Articulation::bendGlideAt; else both at its legato delay)
      struct LibGlide { double leaveMs; double arriveMs; double smoothingMs { 0 }; };   // (smoothing: bendSmoothing)
      std::map<const Note*, LibGlide> libGlide;
      int libChunkStart = 0;                                      // the chunk being rendered: its first utick
      int libOnsetEarly = 0;                                      // SoundLib::onsetEarly, percent (this chunk)
      bool libOnsetByPitch = false;                               // [heldNotes] byPitch (0: the patch's median onset)
      bool libAvoidBad = true;                                    // [notes] avoidBadSamples (the map's avoid)
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
      struct LibJoin { int channel; int patch; int key; const Note* note; int written; double ms; };
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
            double rangeTop { 0.0 };
            const Part* rangePart { nullptr };
            };
      std::map<const Note*, LibLevel> libLevels;
      // track delays and levels (trackdelays.h): the parts with any, and each route's part and patch (port * 16 + channel)
      std::map<const Part*, TrackDelays::Delays> libTrackDelays;
      std::map<std::pair<const Chord*, int>, double> libContextGain;  // [levels] context: a chord's phrase gain (contextlevel.h), per tickOffset
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
            };
      static const int DYNAMIC_LEVELS[8];         // MS4's levels of ppp … fff (ms4playback.cpp's dynamic table)
      static const char* const DYNAMIC_NAMES[8];
   private:
      std::vector<LibTrace>* libTrace { nullptr };
      std::map<const Part*, double>* libRangeTops { nullptr };
      // [levels] realRange: realDb by the dynamics CC value per (patch, pitch) (contextlevel.h)
      std::map<std::pair<const SoundLib::LibInstrument*, int>, std::shared_ptr<const std::vector<double>>> libRangeTables;

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
            };

      void updateChunksPartition();
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
      void renderMs4Dynamics(const Chunk&, EventMap* events);
      SoundLib::Choice libraryChoice(const LibPart& lp, const SoundLib::LibInstrument& li, const Note* note,
                                     const std::vector<Ms4::ArtRef>& noteArts, int tick, int ticks) const;
      static void putLibrarySwitch(EventMap* events, const SoundLib::LibInstrument& li, int channel,
                                   const SoundLib::Choice& choice, int utick, int staffIdx);
      void finishLibraryEvents(const Chunk&, EventMap* events);
      void libraryMaxVelocity(const Chunk&, EventMap* events);
      void libraryPitchBends(const Chunk&, EventMap* events);
      void libraryNoteLevels(const Chunk&, EventMap* events);
      void libraryTrackDelays(const Chunk&, EventMap* events);
      void libraryTimingOverrides(const Chunk&, EventMap* events);      // livetimingoverride.h
      void libraryEvenLevels(const Chunk&, EventMap* events);           // evenlevels.h
      void libraryDelayLead(const Chunk&, EventMap* events);

   public:
      explicit MidiRenderer(Score* s) : score(s) {}

      struct Context
            {
            const SynthesizerState& synthState;
            bool metronome{true};
            bool renderHarmony{false};
            Context(const SynthesizerState& ss) : synthState(ss) {}
            };

      void renderScore(EventMap* events, const Context& ctx);
      void renderChunk(const Chunk&, EventMap* events, const Context& ctx);

      void setScoreChanged() { needUpdate = true; }
      // rendering the clips Live plays (liveclips.h): plug-in parameter lanes as ME_PARAMETER events whatever the
      // output (the MuseScore Link device sets them in Live), lanes Live plays itself (Lane::playedByLive) left out
      void setForLiveClips(bool v) { forLiveClips = v; needUpdate = true; }
      // the library notes at their written times (the note model's, in ticks): none of the timing in seconds the
      // renderer gives them otherwise (a held note early by its onset with keepMs' cap, the note before cut at that early
      // start, a Performance note's join). Live's clips hold these (the owner, 2026-10-11); livetiming.h pairs them with
      // the played render's and puts that timing back at playback (Track Delay and the MuseScore Link device)
      void setWrittenTiming(bool v) { writtenTiming = v; needUpdate = true; }
      void setMinChunkSize(int sizeMeasures) { minChunkSize = sizeMeasures; needUpdate = true; }
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
