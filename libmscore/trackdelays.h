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
//   Track levels (the owner, 2026-10-07, asked whether each technique's "volume offset" could be edited): a patch's
//   and a technique's own level in dB, added up (the part's is the Mixer's volume). Played by CC11 per note
//   (MidiRenderer::libraryNoteLevels, as a marcato's level: dB add up), so it is in Live's clips and the plain set's
//   CC11 lane alike. A level is kept within MIN_DB .. MAX_DB: MIN_DB is CC11's least non-zero value (1 of 127:
//   20 log10(1/127) = -42.08 dB); MAX_DB is the top of Live's track Volume (1.99526238, +6 dB: livesetxml.h), so the
//   plain set plays what MuseScore does (the owner, 2026-10-08: Violins' Long "just has to be 8 dB louder", +6 dB
//   chosen over a higher top Live couldn't follow). Louder (the owner, 2026-10-08): CC11 rests at the library's
//   expression value (SSO: 127, its top), so a patch with a level above 0 dB has headroom (headroomDb): its loudest
//   level, played by its volume (its Kontakt slot's in MuseScore, Vst3Synth::setMix; its Kontakt track's Volume in
//   Live, with the Mixer's: within Live's +6 dB), and every note on it plays its level less that by CC11 (noteDb). A
//   library whose dynamics are CC11 (no plain volume left) ignores levels (patchGain 1).
//
//   Kept in the score as the metaTag "trackDelays" (MuseScore 3.6 keeps metaTags through a round trip), left out
//   when every value is 0; JSON:
//     [{"part": index, "name": part name, "ms": the part's, "tracks": {"<patch>": ms, "<patch> / <technique>": ms},
//       "levels": {"<patch>": dB, "<patch> / <technique>": dB}}]
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
namespace SoundLib { class Library; }

namespace TrackDelays {

extern const char* const metaTag;
constexpr double MIN_MS = -1000.0;
constexpr double MAX_MS = 1000.0;
constexpr double MIN_DB = -42.08;             // CC11 1 of 127 (header)
constexpr double MAX_DB = 6.0;              // Live's track Volume top, 1.99526238 (header)

struct Delays {
      double ms { 0.0 };                      // the part's (its group track in Live)
      std::map<QString, double> tracks;       // trackKey -> ms added to the part's
      std::map<QString, double> levels;       // trackKey -> dB (track levels)
      bool empty() const;                     // all 0
      };

// a track of the part: its Kontakt track ("<patch>"), a technique track ("<patch> / <technique>")
QString trackKey(const QString& patch, const QString& technique = QString());

double clampMs(double ms);
double clampDb(double db);

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
// the level a note of the technique on the patch plays at: the patch's and the technique's dB added up
double db(const Delays& d, const QString& patch, const QString& technique);
// one track's own level (0: none)
double ownDb(const Delays& d, const QString& key);
// a patch's headroom (0 .. MAX_DB): the loudest level a note on it plays at, the patch's own or with a technique's (db(),
// within MAX_DB)
double headroomDb(const Delays& d, const QString& patch);
// what CC11 plays for a note of the technique on the patch: db() within MIN_DB .. MAX_DB less the patch's headroom
double noteDb(const Delays& d, const QString& patch, const QString& technique);
// the patch's volume factor on top of the Mixer's: its headroom as a gain (1 for a library whose dynamics are CC11)
double patchGain(const SoundLib::Library& library, const Delays& d, const QString& patch);

}     // namespace TrackDelays
}     // namespace Ms
#endif
