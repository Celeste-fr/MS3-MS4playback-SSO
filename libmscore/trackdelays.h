//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __TRACKDELAYS_H__
#define __TRACKDELAYS_H__

//---------------------------------------------------------
//   Track delays (the owner, 2026-10-06: "adjust all the automation, dynamics, track delay, etc. needed to make a
//   strings section sound good inside MuseScore (without having to use Ableton)"): a sound library part plays this many
//   ms later (negative: earlier), as Live's Track Delay. A part's delay, plus an override for each track the plain Live
//   set gives it (plainliveset.h): its Kontakt track per patch ("<patch>") and its technique tracks ("<patch> /
//   <technique>"), added up as Live adds a group's, a Kontakt track's and a MIDI track's delays.
//
//   Range: Live 12's manual (18.7 Track Delays) gives none; a tutorial says Live allows 1000 ms either way
//   (musicgurus.com, "Ableton Live delay - Track Delay time"); each value is kept within -1000 .. 1000 ms.
//
//   Kept in the score as the metaTag "trackDelays" (MuseScore 3.6 keeps metaTags through a round trip), left out
//   when every value is 0; JSON:
//     [{"part": index, "name": part name, "ms": the part's, "tracks": {"<patch>": ms, "<patch> / <technique>": ms}}]
//   A part is found as partplayback.h finds it; parts of an excerpt follow their master score's part.
//
//   MuseScore plays it (MidiRenderer::libraryTrackDelays): each library event moves by its track's delay in time
//   (tempo changes followed), a note and its switch by its technique's, controllers, pitch bends and parameters by
//   the patch's; with a negative delay everything else plays later by the earliest one (libraryDelayLead), so the
//   track is early from its first note (before 2026-10-07 what went before the start stayed at the start; nothing
//   is clamped now). The plain Live set writes it as each track's
//   TrackDelay (not into the clips) and reads it back (livetracks.h).
//---------------------------------------------------------

#include <map>
#include <QString>

namespace Ms {

class MasterScore;
class Part;

namespace TrackDelays {

extern const char* const metaTag;
constexpr double MIN_MS = -1000.0;
constexpr double MAX_MS = 1000.0;

struct Delays {
      double ms { 0.0 };                      // the part's (its group track in Live)
      std::map<QString, double> tracks;       // trackKey -> ms added to the part's
      bool empty() const;                     // all 0
      };

// a track of the part: its Kontakt track ("<patch>"), a technique track ("<patch> / <technique>")
QString trackKey(const QString& patch, const QString& technique = QString());

double clampMs(double ms);

// the parts of the master score with delays
std::map<const Part*, Delays> read(const MasterScore* score);
// the metaTag's value (empty: none)
QString write(const MasterScore* score, const std::map<const Part*, Delays>& delays);
// the delays of a part (of an excerpt: its master score's part)
Delays of(const Part* part, const std::map<const Part*, Delays>& delays);

// what a note of the technique on the patch plays at (technique empty: the patch's controllers): the part's, the
// patch's and the technique's added up
double ms(const Delays& d, const QString& patch, const QString& technique = QString());
// one track's own value (0: none)
double own(const Delays& d, const QString& key);
// the earliest any of the part's tracks plays at: the least of the part's, each patch's and each technique's ms()
double earliest(const Delays& d);

}     // namespace TrackDelays
}     // namespace Ms
#endif
