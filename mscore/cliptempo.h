//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __MSCORE_CLIPTEMPO_H__
#define __MSCORE_CLIPTEMPO_H__

//---------------------------------------------------------
//   A clip tab plays at the song's tempo (LIVE.md › Editing Live clips › The song's tempo; the owner, 2026-10-03:
//   "when I play a midi clip inside musescore, it doesn't respect the song tempo automation in the main track").
//
//   - An arrangement clip: the tempo automation of the set's main track (libmscore/liveset.h: read from the saved
//     .als, as no Live API reads arrangement automation) under the clip's place in the song (the device's
//     /live/clip/span: Live's start_time, end_time, start_marker, end_marker, loop_start, loop_end, looping).
//     The clip score shows each clip beat once (clip time = score time), so each clip beat gets the song's tempo
//     where Live FIRST plays it (firstPasses): from start_time on, the clip plays from its start marker; a looping
//     clip reaches loop_end and goes on from loop_start, over and over until end_time. A beat Live plays again in a
//     later pass keeps the first pass's tempo here (later passes under other tempos are not shown); a beat Live
//     never plays under the clip (before the start marker, after end_time) holds the tempo next to it.
//   - A session clip (no arrangement automation applies to it), an arrangement clip whose set has no tempo
//     automation or isn't found (unsaved): Live's current song tempo, followed as it changes (/live/transport).
//
//   In the score, so MuseScore's playback and the display follow: a tempo marking at the start, at each jump and
//   where a ramp ends; a ramp (linear in beats, as Live's envelope lies on the song's beats; a curve, Live's Bézier,
//   as LiveSet::curve's straight pieces, within CURVE_TOLERANCE_BPM) as invisible tempo markings every 32nd (STEP_TICKS, the grid of MuseScore's
//   own rit. / accel. lines) at the ramp's tempo there, and the word "accel." / "rit." (system text) at its start.
//   (Not rit. / accel. lines: MuseScore 3 ends a line at the end of the note or rest it ends in, while Live's
//   breakpoints fall anywhere.) Times are rounded to the score's ticks (480 a beat). Markings change no note's tick:
//   the clip's diff (liveclipmodel.h) sends nothing for them. What this put in the score (owned) is changed in place
//   when only values differ (no undo step: Live's tempo followed live doesn't fill the undo stack), else replaced
//   (one undo step once the score has edits, none before).
//
//   Which file is the set: Live's API doesn't say. Candidates (setCandidates), each read and kept only when it
//   has the clip (setHasClip: the track at the clip's index named as in Live, with an arrangement clip at the
//   clip's start_time and end_time): the set of another clip tab, sets linked to open scores (Import automation
//   from Live Set), sets chosen before (QSettings liveIntegration/tempoSets), then Live's own lists in its
//   Preferences folder (each version, the newest first): Log.txt's 'Loading document "…"' lines (the sets Live
//   opened, the latest first; Live's own Core Library sets left out) and Preferences.cfg's RecentDocsList (UTF-16
//   paths; Live 12.4.6 on the VM, 2026-10-03). None: the clip tab asks once (a notice with "Choose Live Set…").
//---------------------------------------------------------

#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Ms {

class Element;
class MasterScore;

namespace LiveSet {
struct Set;
}

namespace LiveClipTempo {

// an arrangement clip's place in the song, as Live's API gives it (beats)
struct Span {
      double start { 0 };           // start_time (song)
      double end { 0 };             // end_time (song)
      double startMarker { 0 };     // clip beats
      double endMarker { 0 };
      double loopStart { 0 };
      double loopEnd { 0 };
      bool looping { false };
      bool valid() const { return end > start; }
      bool operator==(const Span& o) const {
            return start == o.start && end == o.end && startMarker == o.startMarker && endMarker == o.endMarker
                   && loopStart == o.loopStart && loopEnd == o.loopEnd && looping == o.looping;
            }
      bool operator!=(const Span& o) const { return !(*this == o); }
      };

// clip beats [clipFrom, clipTo) are first played at song beats song + (b - clipFrom)
struct Piece {
      double clipFrom { 0 };
      double clipTo { 0 };
      double song { 0 };
      };
// in clip order; only [0, clipEnd) of the clip
std::vector<Piece> firstPasses(const Span& span, double clipEnd);

struct Point {
      double beat { 0 };
      double bpm { 120 };
      bool curve { false };         // the segment after it is a piece of one of Live's curves
      };
// a curve's straight pieces stay this close to Live's curve (bpm): half the smallest step Live shows a tempo in
// (two decimals, 0.01 bpm), so no piece differs from the curve by a tempo Live would show
constexpr double CURVE_TOLERANCE_BPM = 0.01 / 2;
// the song's tempo by song beat: straight between points, a jump where two points share a beat; one point:
// constant. Before the first breakpoint the default event's value holds (Live's line from its default event at
// -63072000 beats to the first breakpoint is flat to within (difference) × beats / 63072000), curves in pieces
// (CURVE_TOLERANCE_BPM)
std::vector<Point> songTempo(const LiveSet::Set& set);
// the tempo at beat (after the points there; left: before them)
double tempoAt(const std::vector<Point>& points, double beat, bool left = false);
// on the clip's beats (firstPasses' pieces): the song's tempo where each beat is first played, held where none is
std::vector<Point> clipTempo(const std::vector<Point>& song, const std::vector<Piece>& pieces);

//---------------------------------------------------------
//   the score
//---------------------------------------------------------

struct Mark {
      enum Kind { TEXT, STEP, WORD };
      Kind kind { TEXT };           // TEXT: a tempo marking; STEP: an invisible one (a ramp's step); WORD: "accel." / "rit."
      int tick { 0 };
      double tempo { 2 };           // TEXT, STEP: beats a second, as TempoText keeps it (bpm / 60)
      QString text;                 // WORD
      bool operator==(const Mark& o) const {
            return kind == o.kind && tick == o.tick && (kind == WORD ? text == o.text : tempo == o.tempo);
            }
      };
// a ramp's invisible markings are this far apart: TempoChange's grid for rit. / accel. lines (tempochange.cpp STEP,
// a 32nd), so a Live ramp plays as smoothly as MuseScore's own gradual tempo changes
constexpr int STEP_TICKS = 480 / 8;
// the markings and words for a clip's tempo points, in a score of endTick ticks
std::vector<Mark> marks(const std::vector<Point>& clip, int endTick);
// the text of a tempo marking: "♩ = 120", "♩ = 97.5" (Live shows a tempo with 2 decimals)
QString tempoText(double bpm);
// what is in the score of the owned elements (in score order), and the elements
std::vector<Mark> present(const MasterScore* score, const std::vector<Element*>& owned, std::vector<Element*>* elements = nullptr);
// the score's tempo markings now (all), e.g. right after the import: the elements to own
std::vector<Element*> tempoTexts(const MasterScore* score);
// puts marks into the score in place of the owned elements (updated: the new ones). Returns 0: nothing changed,
// 1: values changed in place (no undo step), 2: replaced (an undo step when undoable)
int apply(MasterScore* score, const std::vector<Mark>& marks, std::vector<Element*>* owned, bool undoable);

//---------------------------------------------------------
//   the set file
//---------------------------------------------------------

// the set has the clip: the track at trackIndex (Live's song.tracks: return tracks left out; -1: any track) named
// track, with an arrangement clip from span.start to span.end (each within twice the float32 rounding of the
// device's OSC float: |x| × 2^-23)
bool setHasClip(const LiveSet::Set& set, const QString& track, int trackIndex, const Span& span);
QStringList documentsFromLog(const QByteArray& log);            // the latest first
QStringList documentsFromPreferences(const QByteArray& cfg);    // as listed
// Live's own lists under each preferences base ("<base>/Live <version>/Preferences/…", newest version first),
// existing files only
QStringList setCandidates(const QStringList& prefsBases);

}     // namespace LiveClipTempo
}     // namespace Ms
#endif
