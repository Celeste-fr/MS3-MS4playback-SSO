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
            struct Auto {
                  Automation::Lane lane;
                  int cc { -1 };
                  int param { -1 };
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
      // channel -> MIDI out port and channel per patch, per lane (SoundLib::Lanes: copies of a patch by tuning)
      std::map<int, std::vector<std::vector<std::pair<int, int>>>> libRoutes;
      std::map<const Note*, int> libLanes;                        // a note's lane, when not 0
      std::map<const Note*, double> libLaneCents;                 // a note's tuning as its lane plays it (varispeed)
      // channel -> per patch: its pitch bend range in cents (SoundLib::LibInstrument::bendCents; 0: none)
      std::map<int, std::vector<double>> libBend;
      std::map<const Note*, const Note*> libGlideFrom;            // a legato transition's note before (its lane glides)
      std::map<const Note*, double> libGlideDelayMs;              // its measured legato delay, ms (the bend glides then)
      int libChunkStart = 0;                                      // the chunk being rendered: its first utick
      int libLegatoEarly = 0;                                     // SoundLib::legatoEarly, percent (this chunk)
      int libOnsetEarly = 0;                                      // SoundLib::onsetEarly, percent (this chunk)
      // playback settings (libmscore/playbacksettings.h) for this chunk
      int libOverlapTicks = 30;
      bool libSlurEndOverlap = false;
      // an early start (a legato transition, a held note's onset) after a note on the same patch: that note keeps at
      // least libKeep seconds as played ([legato] keepMs); a transition after a short note: libFastDelay ([legato]
      // fastShare, fastFullMs)
      double libKeep = 0.04, libFastShare = 0.65, libFastFull = 0.8;
      // the fast technique ([legato] fastTechnique, fastBelowShare): a slurred note after a note shorter than this share
      // of its transition's delay plays its own attack (no legato transition)
      bool libFastTechnique = false;
      bool libFastFirsts = true;          // [legato] fastFirsts: a slur's first note in a fast run starts as early as a transition
      double libFastBelow = 1.0;
      double libFastDelay(double delayMs, double lenBefore) const;
      // a library note's start as played (note, tickOffset -> utick), this chunk: what an early start after it may take
      std::map<std::pair<const Note*, int>, int> libPlayedOn;
      QString playbackSettingsTag;
      int playbackGeneration = -1;
      // held notes started early by their onset (this chunk): what ends on their patch between their new and
      // their written start ends at the new one, and their switch and controllers move with them (finishLibraryEvents)
      struct LibShift { int channel; int patch; int on; int written; int chordTick; };
      std::vector<LibShift> libShifts;
      // legato transitions started early (this chunk): the note before ends overlapTicks after the new start as
      // played, not after the written one, so that only one note overlaps the next; a slurred note played by the
      // fast technique (cut): the note before ends at its start (finishLibraryEvents)
      struct LibLegatoOff { const Note* from; int channel; int on; bool cut; };
      std::vector<LibLegatoOff> libLegatoOffs;
      int minChunkSize = 0;

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
      void libraryPitchBends(const Chunk&, EventMap* events);

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
      void setMinChunkSize(int sizeMeasures) { minChunkSize = sizeMeasures; needUpdate = true; }

      Chunk getChunkAt(int utick);

      static const int ARTICULATION_CONV_FACTOR { 100000 };
      };

class Spanner;
extern bool glissandoPitchOffsets(const Spanner* spanner, std::vector<int>& pitchOffsets);

} // namespace Ms

#endif
