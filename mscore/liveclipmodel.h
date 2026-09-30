//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVECLIPMODEL_H__
#define __LIVECLIPMODEL_H__

//---------------------------------------------------------
//   Editing a Live MIDI clip in MuseScore (LIVE.md › Editing Live clips in MuseScore; the owner,
//   2026-09-29: "eventually I want to be able to just edit any midi clip in MuseScore", "it should
//   just render the midi in the Ctrl+Shift+V view").
//
//   The pure part (no GUI, no socket; mscore/liveclipedit.h is the session in the window):
//   - Clip: a clip as the MuseScore Link device read it with get_all_notes_extended (every field;
//     Live's own numbers, doubles, never rounded here).
//   - importClip(): the clip as a new score through MuseScore's MIDI import (quantization, voices,
//     tuplets, drums), laid out in Continuous View (LayoutMode::LINE) from the start.
//     Clip time = score time: beat b of the clip is tick b × 480.
//   - Baseline: each notation note (a tie chain: its first note) with its signature (Sig: pitch,
//     start tick, played length, velocity, played or not) and the Live notes it came from (their
//     data as Live has it). match() builds it right after the import.
//   - diff(): the score now against the baseline. A signature found unchanged keeps its Live notes
//     untouched (nothing is sent: Live keeps its exact timing, velocity, probability …). The rest
//     is paired (velocity only, pitch only, length only, moved), and a paired note is sent as a
//     modification of only the fields edited in the notation; unpaired baseline notes are
//     removed by id, unpaired notation notes added.
//   The diff is by content, not by object identity: a note MuseScore replaced by a copy (a
//   duration change) or brought back by undo is found by its signature.
//
//   Protocol additions (PROTOCOL 2; libmscore/liveclips.h has the rest). Times: Live's beats as
//   float32 where only shown (MuseScore never writes them back: the device keeps Live's doubles),
//   notation times as ticks (480 a beat, int32, exact).
//     device -> MuseScore
//       /live/clip/begin   key:s gen:i track:s clip:s drums:i bpm:f num:i den:i
//                          end:f loopStart:f loopEnd:f looping:i notes:i chunks:i hash:i
//       /live/clip/notes   key:s gen:i chunk:i (id:i pitch:i start:f duration:f velocity:f mute:i
//                          probability:f velocityDeviation:f releaseVelocity:f) × n
//       /live/clip/written key:s write:i status:s hash:i (addedId:i) × n   ("ok", "conflict", "gone", "error: …")
//       /live/clip/conflict key:s hash:i      (the clip changed in Live while edited here)
//       /live/clip/gone    key:s              (the clip was deleted in Live)
//     MuseScore -> device
//       /ms/clip/write  key:s write:i ops:i chunks:i
//       /ms/clip/ops    key:s write:i chunk:i (op:i id:i mask:i pitch:i start:i duration:i velocity:i mute:i) × n
//                       op 0: modify note id (only the fields in mask: 1 pitch, 2 start, 4 duration,
//                       8 velocity, 16 mute); 1: remove note id; 2: add (all fields; id 0)
//       /ms/clip/reload key:s               (read the clip again: begin + notes)
//       /ms/clip/close  key:s               (stop editing it)
//       /ms/clip/edit                       (edit the clip in Live's Detail View: the device's button; the tests)
//---------------------------------------------------------

#include <vector>

#include <QByteArray>
#include <QString>

namespace Ms {

class MasterScore;
class Note;
class Score;

namespace LiveClipEdit {

constexpr int TICKS_PER_BEAT    = 480;          // MScore::division
constexpr int NOTES_PER_PACKET  = 24;           // 9 values each: about 0.9 kB a datagram
constexpr int OPS_PER_PACKET    = 32;

enum Mask { PITCH = 1, START = 2, DURATION = 4, VELOCITY = 8, MUTE = 16 };

struct LiveNote {
      int id { 0 };
      int pitch { 60 };
      double start { 0 };                 // beats, clip time
      double duration { 1 };
      double velocity { 100 };
      bool mute { false };
      double probability { 1 };
      double velocityDeviation { 0 };
      double releaseVelocity { 64 };
      };

struct Clip {
      QString key;                        // the device's name for the clip ("c<LOM id>")
      int gen { 0 };
      QString track;                      // the Live track's name
      QString name;                       // the clip's name
      bool drums { false };               // a Drum Rack on the track (or a drum-like track name)
      double bpm { 120 };                 // Live's tempo
      int num { 4 };                      // the clip's time signature
      int den { 4 };
      double end { 4 };                   // beats: the clip's end (end marker or loop end, the later)
      double loopStart { 0 };
      double loopEnd { 4 };
      bool looping { false };
      int count { 0 };                    // notes announced
      int chunks { 0 };
      int got { 0 };                      // chunks received
      qint32 hash { 0 };
      std::vector<LiveNote> notes;
      bool complete() const { return got >= chunks && int(notes.size()) >= count; }
      };

// the notation's view of a note (a tie chain)
struct Sig {
      int pitch { 60 };
      int tick { 0 };                     // start, 480 a beat
      int ticks { 0 };                    // the tie chain's played length
      int velocity { -1 };                // an absolute (user) velocity, else -1
      bool play { true };                 // not muted
      bool operator==(const Sig& o) const {
            return pitch == o.pitch && tick == o.tick && ticks == o.ticks && velocity == o.velocity && play == o.play;
            }
      bool operator!=(const Sig& o) const { return !(*this == o); }
      bool operator<(const Sig& o) const;
      };

struct Entry {
      Sig sig;
      std::vector<LiveNote> live;         // the Live notes this notation note stands for (usually one)
      };

struct Baseline {
      std::vector<Entry> entries;
      int unmatched { 0 };                // Live notes in range no notation note stands for (left alone in Live)
      int outside { 0 };                  // Live notes outside the clip's time (before 0 or after its end): left alone
      };

struct Op {
      enum Kind { MODIFY = 0, REMOVE = 1, ADD = 2 };
      Kind kind { MODIFY };
      int id { 0 };
      int mask { 0 };
      int pitch { 60 };
      int start { 0 };                    // ticks
      int duration { 0 };                 // ticks
      int velocity { 100 };
      bool mute { false };
      };

struct Diff {
      std::vector<Op> ops;
      Baseline next;                      // the baseline once Live has these ops (the added notes' ids still 0)
      std::vector<std::pair<int, int>> added;   // for each ADD op, in order: (entry, live index) in next
      int modified { 0 };
      int removed { 0 };
      bool empty() const { return ops.empty(); }
      };

// the score's notes as signatures, in order (tie-chain heads; grace notes left out)
std::vector<Sig> signatures(const Score* score, std::vector<Note*>* notes = nullptr);
// right after importClip: each notation note with the Live notes it came from
Baseline match(const Clip& clip, const Score* score);
Diff diff(const Baseline& base, const std::vector<Sig>& now);
// the ids Live gave the added notes (in the order of the ADD ops) into d.next; false: count differs
bool setAddedIds(Diff& d, const std::vector<int>& ids);

// the clip as a Standard MIDI File (format 0, 480 a beat): tempo, time signature, track name, notes
// (channel 10 for drums); notes outside [0, end) left out
QByteArray midiFile(const Clip& clip);
// the MuseScore instrument named like the Live track (instruments.xml ids, track and long names,
// compared loosely: case, digits, punctuation), or "" when none
QString instrumentForTrack(const QString& trackName);
// a piano clip needs a grand staff when it doesn't fit one clef
bool needsGrandStaff(const Clip& clip);
// a new score of the clip, in Continuous View, not laid out as pages at any time (nullptr: error)
MasterScore* importClip(const Clip& clip, QString* error);

std::vector<QByteArray> writePackets(const QString& key, int write, const std::vector<Op>& ops);

}     // namespace LiveClipEdit
}     // namespace Ms
#endif
