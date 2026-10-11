//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __PLAYSTART_H__
#define __PLAYSTART_H__

#include <tuple>
#include <vector>

namespace Ms {

class Score;
class EventMap;
class NPlayEvent;

//---------------------------------------------------------
//   PlayStart
//    where the sequencer's transport starts for playback from utick (Seq::setPos: play from a
//    selection or a click, the loop's start and every wrap back to it, a seek while playing).
//
//    A note's events can sit BEFORE its written beat: a sound library's notes start early by their
//    measured onset (held notes, every measured technique, quick slurs), a negative track delay
//    moves a route's events earlier, grace notes before the beat sound before it. Starting the
//    playlist at the first event at or after utick skipped them, and the first note of the measure
//    did not sound (or sounded on the previous technique: its switch and dynamic were skipped too).
//
//    So the transport starts at start(): the earliest note-on, within 2 s before utick (the
//    renderer's look-back), of a note written at or after utick (a pre-roll of the onset's length:
//    some tens of ms, a few hundred at most). From start() to utick, skips() says which events
//    stay unsent: the note-ons of notes written before utick and their note offs (they are not
//    started now, as before), the metronome's ticks. Controllers, pitch bends, program changes and
//    the library's switches (state) are sent, in order, so the first notes find the state they
//    would have found coming through. Nothing changes when no note starts early (start() == utick).
//
//    The displayed position stays utick (the sequencer's cursor follows its own events from there).
//    Not for Live's clips or MIDI export: they render whole scores.
//---------------------------------------------------------

class PlayStart {
      int _utick = 0;
      int _start = 0;
      const Score* _score = nullptr;
      // note ons let through before utick: channel, route port, route channel, pitch, patch (a note off follows its on)
      std::vector<std::tuple<int, int, int, int, int>> _started;

      int writtenUtick(const NPlayEvent&, int key) const;

   public:
      PlayStart() {}
      PlayStart(const Score*, const EventMap&, int utick, double lookBackSeconds = 2.0);

      int utick() const { return _utick; }                  // where playback is meant to start (the written position)
      int start() const { return _start; }                  // where the transport starts (<= utick)
      bool preRoll(int key) const { return key >= _start && key < _utick; }
      // an event at key (< utick) that stays unsent (not const: it follows which notes it let through)
      bool skips(int key, const NPlayEvent&);
      };

}     // namespace Ms
#endif
