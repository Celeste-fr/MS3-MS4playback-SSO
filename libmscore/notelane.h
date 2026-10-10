//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __NOTELANE_H__
#define __NOTELANE_H__

//---------------------------------------------------------
//   NoteLane: what the Velocity and Join lanes show (mscore/notelanes.h draws and edits them): each library note as
//   rendered (MidiRenderer::LibTrace), the bands behind it (a Performance note's techniques, else the dynamics'
//   velocities) and the technique a join gives a Performance note (the map's legatoGap).
//---------------------------------------------------------

#include <map>
#include <vector>
#include <QString>

#include "rendermidi.h"

namespace Ms {

class MasterScore;
class Note;
class Part;

namespace NoteLane {

extern const char* const VELOCITY;        // "note:velocity"
extern const char* const JOIN;            // "note:join"
bool isTarget(const QString& target);

// a library note as rendered
struct Mark {
      Note* note { nullptr };                   // the master score's
      int tick { 0 };                           // its chord's
      int endTick { 0 };
      int velocity { -1 };                      // as sent
      bool ownVelocity { false };
      bool ownJoin { false };
      MidiRenderer::LibPerformance performance { MidiRenderer::LibPerformance::NONE };
      int joinMs { PerformanceTechnique::JOIN_AUTO };   // JOIN_AUTO: no note of its patch right before
      const SoundLib::Articulation* articulation { nullptr };
      std::vector<int> dynamicVelocities;       // ppp … fff; empty: its velocity is no dynamic
      bool joinable() const { return performance != MidiRenderer::LibPerformance::NONE && joinMs != PerformanceTechnique::JOIN_AUTO; }
      };

// each master part's library notes, by tick (rendered as playback renders: 10-measure chunks)
std::map<const Part*, std::vector<Mark>> marks(MasterScore* score);

struct Band {
      enum class Kind : char { ATTACK, TRANSITION, DYNAMIC };
      Kind kind;
      int low;                // velocities, both ends included
      int high;
      QString name;           // "spiccato", "p–mp"
      int index;              // the dynamics' (0: below ppp … 8: from fff); the technique's in its set
      };
// a Performance note's technique after a join: attack or transition (unchanged: as rendered)
MidiRenderer::LibPerformance performanceAt(const Mark& m, int joinMs);
// the bands behind a note's stem in the Velocity lane at that join; none: no bands
std::vector<Band> velocityBands(const Mark& m, int joinMs);
const Band* bandAt(const std::vector<Band>& bands, int velocity);
// the longest gap still joined by transition (ms, >= 0)
int legatoGap(const Mark& m);

}     // namespace NoteLane

}     // namespace Ms
#endif
