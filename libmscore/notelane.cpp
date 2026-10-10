//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "notelane.h"

#include "audio/midi/event.h"
#include "chord.h"
#include "note.h"
#include "part.h"
#include "partplayback.h"
#include "score.h"
#include "synthesizerstate.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace Ms {

namespace NoteLane {

const char* const VELOCITY = "note:velocity";
const char* const JOIN = "note:join";

bool isTarget(const QString& target)
      {
      return target == VELOCITY || target == JOIN;
      }

std::map<const Part*, std::vector<Mark>> marks(MasterScore* score)
      {
      std::map<const Part*, std::vector<Mark>> out;
      if (!score)
            return out;
      // (as Seq renders: its chunks; the score's repeats as they are)
      std::vector<MidiRenderer::LibTrace> trace;
      MidiRenderer midi(score);
      midi.setMinChunkSize(10);
      midi.setLibraryTrace(&trace);
      SynthesizerState ss;
      MidiRenderer::Context ctx { ss };
      ctx.metronome = false;
      for (MidiRenderer::Chunk chunk = midi.getChunkAt(0); chunk; chunk = midi.getChunkAt(chunk.utick2())) {
            EventMap events;
            midi.renderChunk(chunk, &events, ctx);
            }
      std::set<const Note*> seen;
      for (const MidiRenderer::LibTrace& t : trace) {
            if (!t.note || !seen.insert(t.note).second)
                  continue;
            Note* n = const_cast<Note*>(t.note);
            const Chord* c = n->chord();
            Mark m;
            m.note = n;
            m.tick = c->tick().ticks();
            m.endTick = (c->tick() + c->actualTicks()).ticks();
            m.velocity = t.velocity;
            m.ownVelocity = n->libraryVelocity() > 0;
            m.ownJoin = n->libraryJoin() != PerformanceTechnique::JOIN_AUTO;
            m.performance = t.performance;
            m.joinMs = t.joinMs;
            m.articulation = t.choice.articulation;
            m.dynamicVelocities = t.dynamicVelocities;
            out[PartPlaybackModes::masterPart(n->part())].push_back(m);
            }
      for (auto& p : out)
            std::stable_sort(p.second.begin(), p.second.end(), [](const Mark& a, const Mark& b) { return a.tick < b.tick; });
      return out;
      }

int legatoGap(const Mark& m)
      {
      return m.articulation ? std::max(0, int(std::lround(m.articulation->legatoGapMs))) : 0;
      }

MidiRenderer::LibPerformance performanceAt(const Mark& m, int joinMs)
      {
      if (!m.joinable() || (!m.ownJoin && joinMs == m.joinMs))
            return m.performance;
      return joinMs >= -legatoGap(m) ? MidiRenderer::LibPerformance::TRANSITION : MidiRenderer::LibPerformance::ATTACK;
      }

std::vector<Band> velocityBands(const Mark& m, int joinMs)
      {
      std::vector<Band> out;
      const MidiRenderer::LibPerformance perf = performanceAt(m, joinMs);
      if (perf != MidiRenderer::LibPerformance::NONE && m.articulation) {
            const bool tr = perf == MidiRenderer::LibPerformance::TRANSITION;
            const auto& set = tr ? m.articulation->transitions : m.articulation->attacks;
            for (size_t i = 0; i < set.size(); ++i)
                  out.push_back({ tr ? Band::Kind::TRANSITION : Band::Kind::ATTACK, set[i].low, set[i].high, set[i].name, int(i) });
            return out;
            }
      const std::vector<int>& d = m.dynamicVelocities;
      if (d.size() != 8)
            return out;
      // below ppp, ppp–pp … ff–fff, from fff: a note between two dynamics' velocities (the lower included)
      auto add = [&out](int low, int high, const QString& name, int index) {
            if (high >= low)
                  out.push_back({ Band::Kind::DYNAMIC, low, high, name, index });
            };
      add(1, d[0] - 1, QString::fromUtf8("< ") + MidiRenderer::DYNAMIC_NAMES[0], 0);
      for (int i = 0; i < 7; ++i)
            add(d[size_t(i)], d[size_t(i + 1)] - 1,
                QString("%1%2%3").arg(MidiRenderer::DYNAMIC_NAMES[i]).arg(QString::fromUtf8("–")).arg(MidiRenderer::DYNAMIC_NAMES[i + 1]), i + 1);
      add(d[7], 127, MidiRenderer::DYNAMIC_NAMES[7], 8);
      return out;
      }

const Band* bandAt(const std::vector<Band>& bands, int velocity)
      {
      for (const Band& b : bands)
            if (velocity >= b.low && velocity <= b.high)
                  return &b;
      return nullptr;
      }

}     // namespace NoteLane

}     // namespace Ms
