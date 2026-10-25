//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "notepreview.h"

#include <algorithm>
#include <map>
#include <tuple>

#include "chord.h"
#include "note.h"
#include "part.h"
#include "partplayback.h"
#include "playbacksettings.h"
#include "rendermidi.h"
#include "repeatlist.h"
#include "score.h"
#include "soundlibrary.h"
#include "synthesizerstate.h"
#include "undo.h"
#include "lagprobe.h"
#include "evenlevels.h"

namespace Ms {
namespace NotePreview {

//---------------------------------------------------------
//   modelOf
//---------------------------------------------------------

Sound modelOf(const Part* part, const SynthesizerState& synthState)
      {
      switch (PartPlaybackModes::of(part)) {
            case PartPlayback::MS3:
                  return Sound::MS3;
            case PartPlayback::MS4:
            case PartPlayback::LIBRARY:
                  return Sound::MS4;
            case PartPlayback::DEFAULT:
                  break;
            }
      // (as MidiRenderer::renderChunk: the global method, else the score's, else MS4's)
      int method = synthState.method();
      if (method == -1 && part && part->score())
            method = part->score()->masterScore()->synthesizerState().method();
      if (method == -1)
            method = 3;
      return method == 3 ? Sound::MS4 : Sound::MS3;
      }

//---------------------------------------------------------
//   playedNote
//---------------------------------------------------------

const Note* playedNote(const Note* note)
      {
      if (!note)
            return nullptr;
      const Note* n = note;
      if (n->score() && !n->score()->isMaster()) {
            for (ScoreElement* se : n->linkList()) {
                  if (se->isNote() && se->score() == n->masterScore()) {
                        n = toNote(se);
                        break;
                        }
                  }
            }
      if (const Note* first = n->firstTiedNote())
            n = first;
      return n;
      }

//---------------------------------------------------------
//   fromEvents
//---------------------------------------------------------

Preview fromEvents(const EventMap& events, const std::vector<const Note*>& notes)
      {
      Preview p;
      p.sound = Sound::MS4;
      // each note's first note-on (an ornament's or a trill's first)
      std::vector<std::pair<int, const NPlayEvent*>> ons;
      for (const Note* n : notes) {
            for (const auto& ke : events) {
                  const NPlayEvent& e = ke.second;
                  if (e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch() && e.note() == n) {
                        ons.push_back({ ke.first, &e });
                        break;
                        }
                  }
            }
      if (ons.empty())
            return p;
      typedef std::tuple<int, int, int> RouteKey;       // channel, route port, route channel
      auto routeOf = [](const NPlayEvent& e) { return RouteKey(e.channel(), e.extPort(), e.extChannel()); };
      std::vector<RouteKey> order;
      std::map<RouteKey, int> firstOn;                  // each route's earliest note-on of these notes
      for (const auto& o : ons) {
            const RouteKey r = routeOf(*o.second);
            auto f = firstOn.find(r);
            if (f == firstOn.end()) {
                  firstOn[r] = o.first;
                  order.push_back(r);
                  }
            else
                  f->second = std::min(f->second, o.first);
            if (o.second->isExternal())
                  p.sound = Sound::LIBRARY;
            }
      for (const RouteKey& r : order) {
            const int at = firstOn[r];
            // the last of each controller, pitch bend, program and switch on the route up to the note-on's instant,
            // in the map's order; the sustain pedal and all-notes-off are not the note's sound (a click ends after
            // MScore::defaultPlayDuration, Seq::stopNotes)
            std::map<std::pair<int, int>, std::pair<int, const NPlayEvent*>> state;    // (type, controller): (order, event)
            std::pair<int, const NPlayEvent*> keySwitch { -1, nullptr };
            int index = 0;
            for (auto i = events.cbegin(); i != events.cend() && i->first <= at; ++i, ++index) {
                  const NPlayEvent& e = i->second;
                  if (routeOf(e) != r)
                        continue;
                  if (e.type() == ME_NOTEON) {
                        if (e.librarySwitch() && e.velo() > 0)
                              keySwitch = { index, &e };
                        continue;
                        }
                  if (e.type() == ME_CONTROLLER) {
                        if (e.controller() == CTRL_SUSTAIN || e.controller() == CTRL_ALL_NOTES_OFF)
                              continue;
                        state[{ ME_CONTROLLER, e.controller() }] = { index, &e };
                        }
                  else if (e.type() == ME_PITCHBEND || e.type() == ME_PROGRAM)
                        state[{ e.type(), 0 }] = { index, &e };
                  }
            std::vector<std::pair<int, const NPlayEvent*>> sorted;
            for (const auto& s : state)
                  sorted.push_back(s.second);
            if (keySwitch.second)
                  sorted.push_back(keySwitch);
            std::sort(sorted.begin(), sorted.end(), [](const std::pair<int, const NPlayEvent*>& a, const std::pair<int, const NPlayEvent*>& b) {
                  return a.first < b.first;
                  });
            for (const auto& s : sorted) {
                  p.on.push_back(*s.second);
                  if (s.second == keySwitch.second) {          // (a key switch: pressed and let go)
                        NPlayEvent off(*s.second);
                        off.setVelo(0);
                        p.on.push_back(off);
                        }
                  }
            for (const auto& o : ons) {
                  if (routeOf(*o.second) != r)
                        continue;
                  p.on.push_back(*o.second);
                  NPlayEvent off(*o.second);
                  off.setVelo(0);
                  p.off.push_back(off);
                  }
            }
      return p;
      }

//---------------------------------------------------------
//   render
//---------------------------------------------------------

Preview render(MidiRenderer& renderer, const SynthesizerState& synthState, const std::vector<const Note*>& notes,
               EventMap* chunkEvents, int* chunkKey)
      {
      LagProbe::Scope probe("NotePreview::render");
      std::vector<const Note*> played;
      for (const Note* n : notes) {
            const Note* p = playedNote(n);
            if (p && p->chord() && p->part() && std::find(played.begin(), played.end(), p) == played.end())
                  played.push_back(p);
            }
      Preview none;
      if (played.empty())
            return none;
      const Note* first = played.front();
      if (modelOf(first->part(), synthState) == Sound::MS3)
            return none;                        // (MuseScore 3.6's click: the caller's)
      none.sound = Sound::MS4;
      MasterScore* ms = first->masterScore();
      const int tick = std::max(0, first->chord()->tick().ticks());
      const MidiRenderer::Chunk chunk = renderer.getChunkAt(ms->repeatList().tick2utick(tick));
      if (!chunk)
            return none;
      EventMap local;
      EventMap* events = chunkEvents ? chunkEvents : &local;
      if (!chunkEvents || !chunkKey || *chunkKey != chunk.utick1()) {
            events->clear();
            MidiRenderer::Context ctx(synthState);          // (as Seq::renderChunk)
            ctx.metronome = true;
            ctx.renderHarmony = true;
            // (the chunk's window, its lead-in too: playback's chunks hold only their own events since branch chunk-equal,
            // as the whole render does, and the state in force at the note may come from before the chunk)
            renderer.setKeepWindow(true);
            renderer.renderChunk(chunk, events, ctx);
            if (chunkKey)
                  *chunkKey = chunk.utick1();
            }
      return fromEvents(*events, played);
      }

//---------------------------------------------------------
//   stateKey
//---------------------------------------------------------

QString stateKey(const MasterScore* score, const SynthesizerState& synthState)
      {
      if (!score)
            return QString();
      uint tags = 0;
      for (auto i = score->metaTags().cbegin(); i != score->metaTags().cend(); ++i)
            tags = tags * 31u + (qHash(i.key()) ^ (qHash(i.value()) * 7u));
      const UndoStack* undo = score->undoStack();
      return QString("%1|%2|%3|%4|%5|%6|%7|%8|%9").arg(quintptr(score)).arg(undo ? undo->getCurIdx() : -1)
             .arg(undo ? undo->state() : -1).arg(tags).arg(quintptr(SoundLib::current().get()))
             .arg(SoundLib::routesGeneration()).arg(Playback::generation()).arg(int(SoundLib::output()))
             .arg(synthState.method())
             + QString("|%1|%2|%3|%4").arg(int(SoundLib::active())).arg(int(PartPlaybackModes::libraryDefault()))
             .arg(score->nstaves()).arg(score->parts().isEmpty() ? 0 : quintptr(score->parts().first()))
             // (even levels written without an undo step: the chunk renders again, as Play would play it)
             + QString("|%1").arg(EvenLevels::generation());
      }

//---------------------------------------------------------
//   Previewer::preview
//---------------------------------------------------------

Preview Previewer::preview(const std::vector<const Note*>& notes, const SynthesizerState& synthState)
      {
      const Note* first = notes.empty() ? nullptr : playedNote(notes.front());
      if (!first)
            return Preview();
      MasterScore* ms = first->masterScore();
      if (_score != ms) {
            _renderer = MidiRenderer(ms);
            _renderer.setMinChunkSize(10);      // (as Seq's: the same chunks)
            _score = ms;
            _key.clear();
            }
      const QString key = stateKey(ms, synthState);
      if (key != _key) {
            _renderer.setScoreChanged();
            _chunkKey = -1;
            _key = key;
            }
      return render(_renderer, synthState, notes, &_chunk, &_chunkKey);
      }

}     // namespace NotePreview
}     // namespace Ms
