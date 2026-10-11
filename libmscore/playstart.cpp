//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playstart.h"

#include "audio/midi/event.h"
#include "chord.h"
#include "note.h"
#include "repeatlist.h"
#include "score.h"

#include <algorithm>
#include <cstdlib>

namespace Ms {

//---------------------------------------------------------
//   writtenUtick
//    the unrolled tick a note is written at, among the passes of a repeat the one nearest the event
//    (which sits some tens of ms from it, 2 s at most)
//---------------------------------------------------------

int PlayStart::writtenUtick(const NPlayEvent& ev, int key) const
      {
      const Note* note = ev.note();
      if (!note || !note->chord())
            return -1;
      const int tick = note->chord()->tick().ticks();
      int best = -1;
      for (const RepeatSegment* s : _score->repeatList()) {
            if (tick < s->tick || tick >= s->tick + s->len())
                  continue;
            const int u = s->utick + (tick - s->tick);
            if (best < 0 || std::abs(u - key) < std::abs(best - key))
                  best = u;
            }
      return best < 0 ? _score->repeatList().tick2utick(tick) : best;
      }

//---------------------------------------------------------
//   PlayStart
//---------------------------------------------------------

PlayStart::PlayStart(const Score* score, const EventMap& events, int utick, double lookBackSeconds)
   : _utick(utick), _start(utick), _score(score)
      {
      if (!score || events.empty() || utick <= 0)
            return;
      const int from = std::max(0, score->utime2utick(score->utick2utime(utick) - lookBackSeconds));
      for (auto i = events.lower_bound(from); i != events.end() && i->first < utick; ++i) {
            const NPlayEvent& ev = i->second;
            if (ev.type() == ME_NOTEON && ev.velo() > 0 && !ev.librarySwitch() && writtenUtick(ev, i->first) >= utick) {
                  _start = i->first;        // (the events are in time order: the earliest)
                  return;
                  }
            }
      }

//---------------------------------------------------------
//   skips
//---------------------------------------------------------

bool PlayStart::skips(int key, const NPlayEvent& ev)
      {
      if (key >= _utick)
            return false;
      const int type = ev.type();
      if (type == ME_TICK1 || type == ME_TICK2)
            return true;
      if (type != ME_NOTEON && type != ME_NOTEOFF)
            return false;                         // state: controllers, bends, programs …
      if (ev.librarySwitch())
            return false;                         // a keyswitch is state too (its note off with it)
      const auto k = std::make_tuple(int(ev.channel()), ev.extPort(), ev.extChannel(), ev.pitch(), ev.libraryPatch());
      if (type == ME_NOTEON && ev.velo() > 0) {
            if (!_score || writtenUtick(ev, key) < _utick)
                  return true;                    // the previous notes: not started now
            _started.push_back(k);
            return false;
            }
      auto s = std::find(_started.begin(), _started.end(), k);
      if (s == _started.end())
            return true;                          // the note off of a note not started
      _started.erase(s);
      return false;
      }

}     // namespace Ms
