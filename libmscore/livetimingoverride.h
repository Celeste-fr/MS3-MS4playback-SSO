//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVETIMINGOVERRIDE_H__
#define __LIVETIMINGOVERRIDE_H__

//---------------------------------------------------------
//   Timing set in Live: a note's override (the owner, 2026-10-11: "Live plays the score" keeps the played timing in
//   its clips, "but if I manually adjust the timings in Live, it stays overridden like that"). LIVE.md › Live plays the
//   score › Timing you set in Live.
//
//   What it is. A library note whose start and / or end the owner moved in its "MuseScore: …" clip. Each is kept on
//   the note (Note::liveStart / liveEnd, Pid::LIVE_START / LIVE_END) as ms after the note's WRITTEN place: the start
//   after the written start (its chord's tick), the end after the written end (the end of its tie chain), both in
//   seconds as the score's tempo map plays them (utime, as the renderer's onsets). AUTO (either): as rendered.
//   The renderer (MidiRenderer::libraryTimingOverrides, the last of finishLibraryEvents' timing, before the track
//   delays) puts the note-on and / or note-off there after everything automatic (onset early, keepMs, the note
//   before cut, joins, the same-key rule) is done; the switch, controllers and pitch bend at the note's old note-on
//   on its route go along (copied, so a chord's other notes keep theirs). So MuseScore plays it, and Live's next clip
//   (the same render) holds it: the next push doesn't move it back.
//
//   Rules (why):
//   - Relative to the written note, not to MuseScore's automatic timing: "it stays overridden like that" means where
//     the owner put it, whatever later changes the automatic timing (another technique and its onset, a playback
//     setting, the note before): the override keeps its place against the note as written.
//   - In ms, not ticks or beats: the automatic timing it replaces is in ms (onsets, joins), and an ms offset keeps
//     its size when the tempo changes; the note's written place follows the tempo, the offset doesn't.
//   - The note moved or lengthened in MuseScore: the override goes with it (it is relative to the written place). A
//     note entered again (a new note) has none. A note's technique changed: kept (the owner's timing wins).
//   - Start and end are separate: a note trimmed at its start in Live keeps its automatic end, and the other way.
//   - Per note, not per repeat: a note in a repeated passage plays its override in every pass (the last edited pass's
//     value, when two passes were edited differently).
//   - Not overridden: grace notes, a note with more than one note-on in a pass (trill, tremolo, glissando steps) and
//     the clip's carriers and keyswitches: such edits are replaced at the next push, as before.
//   - Velocity, pitch, notes added or deleted in Live: not overrides; replaced by MuseScore's at the next push (as
//     before), counted in the status line.
//   - Precision: MuseScore plays a tick (1/480 quarter); Live gets that tick, so a note set between two ticks moves
//     by up to half a tick (0.52 ms at 120 bpm) at the next push, once.
//
//   Detecting it (fromLive): what MuseScore last wrote to the clip (its notes, units, each with its score note) against
//   what the MuseScore Link device reads from the clip when it changed in Live. Notes the same (pitch, start, length,
//   velocity): unchanged. The rest paired by pitch, nearest start within a beat: a pair whose start or end differs is
//   an override of that note; one whose timing is the same but velocity differs, and every note left over (added or
//   deleted), is another edit.
//
//   Saved in the score's metaTag "liveTimingOverrides" (kept by MuseScore 3.6 through a round trip; left out when no
//   note has one): JSON [{"tick", "track", "grace", "pitch", "start", "end"}] (start / end left out when AUTO), as
//   performancetechnique.h's. Cleared per note in the Inspector (Note › Start / End from Live, reset), all of a part's
//   by selecting its notes (e.g. right-click › Select › Similar on this staff) and resetting there: one undo step.
//---------------------------------------------------------

#include <vector>

#include <QString>

namespace Ms {

class Note;
class Score;

namespace LiveClips {
struct Note;
struct Timeline;
}

namespace LiveTimingOverride {

extern const char* const metaTag;

constexpr double AUTO = -10001.0;       // no override (the Inspector's minimum: shown as "Auto")
constexpr double MAX_MS = 10000.0;      // either way
inline bool isSet(double ms) { return ms > AUTO + 0.5; }
double clampMs(double ms);               // AUTO, or within MAX_MS, to 0.01 ms

// the metaTag: to the notes after loading (and out of the tags), from them on saving; empty: none
void read(Score* score);
QString write(const Score* score);
int count(const Score* score);           // notes with an override (either)

// the note's written place in a render pass: the pass whose start of the note is nearest played (uticks); its
// written start and end (the tie chain's), uticks; false: not found (not in a pass)
bool writtenPlace(const Score* score, const Note* first, int played, int* on, int* off);

struct Edit {
      const Note* note { nullptr };      // the tie chain's first note
      double startMs { AUTO };
      double endMs { AUTO };
      };
struct Result {
      std::vector<Edit> edits;
      int unchanged { 0 };
      int other { 0 };                   // edits that aren't timing (velocity, pitch, added, deleted, carriers)
      };
// sent: what MuseScore last wrote to the clip (each note with its score note and played utick); live: what the clip
// holds now. A sent note whose score note is no longer in the score counts as another edit
Result fromLive(const Score* score, const LiveClips::Timeline& tl, const std::vector<LiveClips::Note>& sent,
                const std::vector<LiveClips::Note>& live);
// the edits on their notes (undoChangeProperty: inside a command); how many values changed
int apply(const Result& r);

}     // namespace LiveTimingOverride
}     // namespace Ms
#endif
