//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVETIMING_H__
#define __LIVETIMING_H__

//---------------------------------------------------------
//   Live's clips hold the written notes; the timing is put back at playback (the owner, 2026-10-11: "Live clips hold
//   the WRITTEN notes -- on the grid, exact written lengths, rests -- never the played timing"; the timing MuseScore gives
//   library notes "is applied at PLAYBACK": per technique by Live's Track Delay, the rest by the MuseScore Link device,
//   "a device is fine"). LIVE.md › Written clips.
//
//   The renderer gives a library note timing in seconds besides its written place (the note model's, in ticks): a held
//   note (since 2026-10-08 every measured technique) starts early by its measured onset (by its articulation's median,
//   or by its pitch's: [heldNotes] byPitch), capped so that the note before on its patch keeps [legato] keepMs; the
//   note before on the patch then ends at that early start; a Performance note's join ends the note before overlap ms
//   after its note-on or reattackGap ms before it; a key struck again ends its last note just before (the played times
//   decide which). MidiRenderer::setWrittenTiming renders without all of these; a clip's notes come from that render
//   and keep, each, its time as played (LiveClips::Note::playedStart / playedEnd, from the played render: noteTimes).
//
//   At playback (Live has no way to play a note before it has read it, so the earliest note needs a lookahead):
//     - the track's Track Delay is lookaheadMs earlier (the earliest MuseScore plays a note-on or note-off of the track
//       before its written place: Live's own lookahead; a Performance re-attack ends the note before its reattackGap
//       before the next one's early start, so a note-off can be earlier than any note-on);
//     - the MuseScore Link device on the track delays each note-on by onMs and each note-off by offMs, both >= 0, so
//       written + Track Delay + the device's delay = MuseScore's played time. Most notes share one pair (the defaults:
//       with [heldNotes] byPitch 0 a technique's notes all start early by its median onset, so the note-ons pass at once
//       and the note-offs wait lookaheadMs); the others are listed by their written place (Exception);
//     - so no delay is negative (offsLate counts any that would be: none by construction).
//   Controllers aren't notes and stay where they are played: the plain set's Controllers clip (on the Kontakt track,
//   which no technique delays) holds them at their played times; a route clip's carriers are moved by its lookahead,
//   so they come out of Live's Track Delay at their played times and the device passes them at once.
//   Times are LiveClips units (liveclips.h: UNITS_PER_BEAT a beat at Live's one tempo, so a ms is a fixed number of
//   units).
//---------------------------------------------------------

#include <map>
#include <tuple>
#include <vector>

#include <QtGlobal>

#include "liveclips.h"

namespace Ms {

class EventMap;
class Note;

namespace LiveTiming {

// a library note's place among a render's note-ons: the score's note, the key it sounds on, and which of that note's
// note-ons on that key it is (an ornament's or a tremolo's notes, repeats)
struct Key {
      const Note* note { nullptr };
      int pitch { 0 };
      int k { 0 };
      bool operator<(const Key& o) const { return std::tie(note, pitch, k) < std::tie(o.note, o.pitch, o.k); }
      };
struct Span {
      int on { 0 };                 // uticks
      int off { 0 };                // its note-off (first on, first off by route and key, as the clips pair them)
      };
// each note-on of a note a render plays on an external route (the library's), in time order; keys: each note-on
// event's Key
std::map<Key, Span> noteTimes(const EventMap& events, std::map<const void*, Key>* keys = nullptr);

// counts a render's note-ons per (note, key) in time order, giving each its Key
class Counter {
      std::map<std::pair<const Note*, int>, int> seen;
   public:
      Key next(const Note* note, int pitch) { return { note, pitch, seen[{ note, pitch }]++ }; }
      };

// the device's delays for a written note at start (units, song time): ms after Live (its Track Delay included) sends it
struct Exception {
      int start { 0 };              // units: the note's written start
      int end { 0 };                // and its written end
      int pitch { 0 };
      double onMs { 0 };
      double offMs { 0 };
      };

struct Track {
      double lookaheadMs { 0 };     // the Track Delay is this much earlier (>= 0)
      double onMs { 0 };            // the device's delays for most notes
      double offMs { 0 };
      std::vector<Exception> exceptions;      // the notes whose delays differ (by start)
      int offsLate { 0 };           // note-offs played earlier than lookaheadMs before their written end
      double commonEarlyMs { 0 };   // the most common early start (0.1 ms): the Track Delay without the device (the plain
                                    // set without MuseScore Link: the notes as written, all moved by it alone)
      bool any() const { return lookaheadMs > 0 || onMs > 0 || offMs > 0 || !exceptions.empty(); }
      };

// ms <-> units at Live's tempo
double unitsToMs(double units, double bpm);
double msToUnits(double ms, double bpm);

// a track's timing from its notes (written, each with its playedStart / playedEnd; notes without are as written).
// The lookahead is rounded up to a whole 0.1 ms (Live's Track Delay field), the delays to 0.01 ms
Track track(const std::vector<LiveClips::Note>& notes, double bpm);

// where Live plays a note of the track (its Track Delay, and the device's delays: on / off, units)
std::pair<double, double> playedBy(const Track& t, const LiveClips::Note& n, double bpm);

// the device's record of a track's timing, as atoms (MuseScoreLink.js › Written clips: "msl-timing" 1 lookahead on off
// count, then per exception start end (units) pitch on off); tools/live/MuseScoreLink.js timParse reads it the same way
std::vector<double> record(const Track& t);
quint32 hashOf(const Track& t);

}     // namespace LiveTiming
}     // namespace Ms
#endif
