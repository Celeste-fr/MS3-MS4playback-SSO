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
//
//   A clip tab plays through the clip's own Live track (PROTOCOL 4; the owner, 2026-10-02: "when I press playback
//   in musescore, it plays through the live plugins, but doesn't affect the time cursor in live"). MuseScore keeps
//   its own transport and cursor; Live's transport, song time and clips are never touched. The notes MuseScore
//   plays for the clip score (LiveMidi below: notes, the pedals, the pitch bend; channel 1) go to the MuseScore
//   Link copy on that track, whose patcher plays them into the track's chain (before the instrument: not recorded,
//   no arming or monitoring needed); MuseScore's own synthesizer stays silent for them (mscore/livemidiout.h).
//     device -> MuseScore
//       /live/clip/track key:s trackId:i copy:i track:s  the clip's track (its LOM id), copy 1: a MuseScore Link copy
//                                                        of protocol 4 or later is on it (sent after begin and when it
//                                                        changes: a copy added or removed)
//       /live/bye session:s                               the hub copy is going (deleted, its set closed)
//     MuseScore -> device
//       /ms/midi trackId:i status:i data1:i data2:i       one MIDI message, played at once into that track's chain
//                                                        (the hub's patcher: route /ms/midi -> forward msl_m<trackId>;
//                                                        each copy's [receive msl_m<its track>] -> midiout)
//       /ms/clip/adopt key:s hash:i                       a new hub (the old one went): edit this clip again; hash: its
//                                                        notes as MuseScore last knew them (another: a conflict)
//---------------------------------------------------------

#include <map>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QVariantList>

#include "libmscore/automation.h"
#include "libmscore/clef.h"

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
// right after match: a notation note whose Live notes are all muted doesn't play (the Inspector's Play off, no
// undo step), as in Live; its baseline says so, so nothing is written for it. Returns how many
int applyMutes(Baseline& base, Score* score);
// the ids Live gave the added notes (in the order of the ADD ops) into d.next; false: count differs
bool setAddedIds(Diff& d, const std::vector<int>& ids);

// the clip as a Standard MIDI File (format 0, 480 a beat): tempo, time signature, track name, notes
// (channel 10 for drums); notes outside [0, end) left out
QByteArray midiFile(const Clip& clip);
// the MuseScore instrument named like the Live track (instruments.xml ids, track and long names,
// compared loosely: case, digits, punctuation), or "" when none
QString instrumentForTrack(const QString& trackName);
// The staves of a pitched clip (the owner, 2026-10-03: "show bass, treble, bass 15mb and treble 15ma staffs
// whenever there are any notes that fall inside them. they should act as ONE STAFF, not separate staffs that you
// have to switch notes from one to the other"): four band staves braced together, top to bottom treble 15ma,
// treble, bass, bass 15mb, each with one clef for the whole clip (no clef changes). The notes stay in the staff
// and voice the import or the editing put them in; each chord is drawn on its band's staff (cross-staff,
// ChordRest::staffMove), and a staff shows only while a chord is drawn on it (Score::lineHidesEmptyStaves).
// A note's band: the staff where it needs the fewest ledger lines (ledgerLines; equal on two: C4 treble / bass,
// B5 treble / treble 15ma, D2 bass / bass 15mb): the band of the chord before it, else the band whose middle
// line is nearest the part's median pitch. A chord over two bands is split by band into other voices (a voice
// free for its length: the chord's staff first, then the other band staves), the band with the most notes
// staying; a chord with ties, a tuplet or grace notes, or with no free voice, stays whole on that band.
// Rests: voice 1's shown where nothing is drawn on their staff, the others hidden.
// assignBands runs after the import and at the end of every command, inside its undo step (Score::setEndCmdHook)
constexpr int BANDS = 4;
constexpr ClefType BAND_CLEFS[BANDS] = { ClefType::G15_MA, ClefType::G, ClefType::F, ClefType::F15_MB };
// the ledger lines a note of this pitch needs in this clef (spelled as MuseScore spells it in C major)
int ledgerLines(int pitch, ClefType clef);
// the bands (0-3, BAND_CLEFS) where a note of this pitch needs the fewest ledger lines
std::vector<int> bandsOf(int pitch);
void makeBandStaves(MasterScore* score, bool noRange);
// each chord of a band part drawn on its band, rests shown or hidden (undoable); the number of changes
int assignBands(Score* score);
// the import's grid in ticks: 120 (a sixteenth), or 60 (a thirty-second) when the clip's notes are on 32nds (every
// start and end within GRID_TOLERANCE ticks of that grid, at least one on an odd 32nd)
constexpr int GRID_TOLERANCE = TICKS_PER_BEAT / 32;
int importGrid(const Clip& clip);
// a new score of the clip, in Continuous View, not laid out as pages at any time (nullptr: error); its file name
// (the tab, the window title) is clipTitle(clip)
MasterScore* importClip(const Clip& clip, QString* error);
// the clip's name in its tab, the window title and the status line: its name; an unnamed one (the owner, 2026-10-03:
// "35-BuzzWave ›" with nothing after it) by its place: slot the session slot (from 0, shown from 1), ARRANGEMENT
// an arrangement clip, NO_PLACE not known yet ("(clip)"; the device says where after the notes, /live/clip/where)
constexpr int NO_PLACE = -2;
constexpr int ARRANGEMENT = -1;
QString clipLabel(const Clip& clip, int slot = NO_PLACE);
// "<track> › <label>", fit for a file name (\ / : * ? " < > | as _)
QString clipTitle(const Clip& clip, int slot = NO_PLACE);

std::vector<QByteArray> writePackets(const QString& key, int write, const std::vector<Op>& ops);

//---------------------------------------------------------
//   LiveMidi: MuseScore's play events for a clip tab -> the MIDI messages its Live track gets (audio thread;
//   no allocation). Notes (velocity as played), sustain / sostenuto / soft pedal, the pitch bend; all on channel
//   1, whatever MuseScore's channel. Nothing else: the Mixer's volume, pan, reverb and chorus, programs, banks
//   and other controllers stay MuseScore's (Live's instrument keeps its own settings). A key held by two
//   notes at once is released with the last; all notes off (CC 123, 120: MuseScore's stop) releases the keys
//   still down and lifts the pedals; repeated values (a stop's per-channel pedal-up, the bend's centre) are
//   left out.
//---------------------------------------------------------

constexpr int MIDI_PROTOCOL     = 4;            // the device's protocol from which clip tabs play through Live

struct MidiMsg {
      unsigned char b[3];
      };

class LiveMidi {
      unsigned char _count[128];
      int _pedal[3];                      // CC 64, 66, 67: the last value sent
      int _bend;                          // the last 14-bit value sent

   public:
      static constexpr int MAX_OUT = 132; // room accept() and allOff() need
      LiveMidi() { reset(); }
      void reset();
      // type, a, b as NPlayEvent has them (ME_NOTEON …, the pitch bend: a = lower, b = upper 7 bits); returns
      // how many messages it wrote into out (MAX_OUT room)
      int accept(int type, int a, int b, MidiMsg* out);
      int allOff(MidiMsg* out);           // the keys still down released, the pedals up, the bend centred
      bool sounding() const;
      };

QByteArray midiPacket(int trackId, const MidiMsg& m);

//---------------------------------------------------------
//   Automation lanes of any Live track (the owner, 2026-10-02: "the automation display wouldn't only work for sso, it
//   works for any midi clip"; clip-tab lanes "written into the Live clip's own envelopes", Live 12.4's API).
//
//   - The track's parameters: the device sends them (protocol 5, tools/live/MuseScoreLink.js) for an edited clip
//     (key "c<id>") and for each route's track (key "<port>:<channel>"):
//       /live/params key:s hash:i chunk:i chunks:i (d:i p:i name:s min:f max:f quantized:i) × n
//     d -1: the mixer (p 0 volume, 1 pan); else the track's devices[d].parameters[p]. A lane on one has the target
//     "live:<d>/<p>" (liveTarget) and keeps the name in its extra ("name"), shown when the track isn't known.
//       /live/clip/where key:s track:i slot:i   the edited clip's place: the track's index, the session slot's (-1:
//                                               an arrangement clip)
//     MuseScore -> device: /ms/params/ask (every track's parameters again: MuseScore started anew).
//   - A clip tab's lanes are the clip's own envelopes in Live. Max for Live can't reach them (Live 12.4.6: a Clip
//     has has_envelopes, clear_envelope and clear_all_envelopes only); Live's Python API can, for Session clips
//     only (Clip.automation_envelope / create_automation_envelope; Envelope.create_event, from Live 12.4,
//     delete_events_in_range, events_in_range, value_at_time). So a Control Surface script does it,
//     tools/live/MuseScoreEnvelopes (installed once in Live's User Library and chosen in Live's settings), and
//     MuseScore talks to it directly on ENV_PORT; its protocol: tools/live/MuseScoreEnvelopes/core.py.
//     A lane goes as breakpoints (envelopeEvents): each point; a step's value again just before the next point
//     (two breakpoints at one time are a jump in Live); a curved ramp as straight pieces nowhere further than one
//     MIDI step (1/127 of the parameter's range) from MuseScore's curve (Automation::flattenCurve; the owner's
//     criterion, 2026-10-03), on the tick grid (Live's create_event ignores a breakpoint's curve: tried in 12.4.6). Read back (lanePoints): each breakpoint a point,
//     a flat piece before a jump a step again. Before a lane's first point Live's envelope holds the first value
//     (a MuseScore lane: the parameter's own value); after the last both hold it.
//---------------------------------------------------------

constexpr int PARAMS_PROTOCOL   = 5;            // the device's protocol from which it sends the tracks' parameters
constexpr int ENV_PORT          = 9005;         // tools/live/MuseScoreEnvelopes/core.py PORT
constexpr int ENV_PAIRS         = 100;          // (tick, value) pairs a /ms/env/lane

struct LiveParam {
      int d { -1 };
      int p { 0 };
      QString name;                       // "Operator › Tone", "Mixer › Volume"
      double min { 0 };
      double max { 1 };
      bool quantized { false };
      };
QString liveTarget(int d, int p);         // "live:<d>/<p>"
bool parseLiveTarget(const QString& target, int* d, int* p);

// the parameters the device sent, by key (an edited clip's, a route's)
class TrackParams {
      struct Entry {
            qint32 hash { 0 };            // the list being received
            int chunks { 0 };
            qint32 doneHash { 0 };        // the list in params
            std::map<int, std::vector<LiveParam>> parts;
            std::vector<LiveParam> params;
            bool complete { false };
            };
      std::map<QString, Entry> _entries;
      int _generation { 0 };
   public:
      static TrackParams* instance();
      // a /live/params; true when a key's list is complete and changed
      bool accept(const QVariantList& args);
      const std::vector<LiveParam>* params(const QString& key) const;   // nullptr: none known
      const LiveParam* param(const QString& key, const QString& target) const;
      void clear() { _entries.clear(); ++_generation; }
      void touch() { ++_generation; }     // (what the lanes offer changed otherwise: a clip tab's envelopes read)
      int generation() const { return _generation; }
      };

std::vector<std::pair<int, double>> envelopeEvents(const std::vector<Automation::Point>& points);
std::vector<Automation::Point> lanePoints(const std::vector<std::pair<int, double>>& events);

struct EnvLane {
      int d { -1 };
      int p { 0 };
      std::vector<std::pair<int, double>> events;     // empty: the envelope cleared
      };
// /ms/env/write, then each lane's /ms/env/lane
std::vector<QByteArray> envWritePackets(const QString& key, int write, int track, int slot, qint32 hash,
                                        const std::vector<EnvLane>& lanes);

//---------------------------------------------------------
//   The Velocity lane of a clip tab (protocol 6; the owner, 2026-10-03: "I want to be able to automate the velocity
//   in MuseScore, and I can toggle override the existing note velocities vs. use data saved in MuseScore Link"; then:
//   one curve a clip, scale or absolute a mode of that curve; in "Write" the notes' velocities before the curve kept,
//   so the curve never scales already scaled ones).
//
//   One lane, target "velocity", on the clip score's part (Automation::Lane; its points as any lane's, values 0-1),
//   with two settings in its extra:
//     "velocityMode": "scale" (the default): a note's velocity v × 2u (u the lane's value at the note's start: 0-200 %,
//       the middle of the lane 100 %, unchanged), rounded, 1-127; "absolute": round(127 u), 1-127. Before the lane's
//       first point a note keeps its velocity (a lane says nothing there, automation.h).
//       (0-200 %: the owner's suggestion, "e.g. 0-200 % with 100 = unchanged": an owner decision, LIVE.md.)
//     "velocityOutput": "shape" (the default, the notes untouched): the MuseScore Link copy on the clip's track changes
//       the note-ons as Live plays them (tools/live/MuseScoreLink.js › velocity curves; the curve is kept in the set);
//       "write": the velocities are written into the clip's notes (the edit path: a velocity-only modification of
//       each note whose velocity changes, undo and conflicts as any edit).
//   In "write" the notation keeps each note's velocity before the curve (its "original"), the signatures Live is
//   compared with have the curve applied (signaturesForLive), so a curve edit writes the notes it changes, from the
//   originals. The originals go to the device with the curve (by Live's note id, with pitch and start for a set
//   reopened, and the velocity written): read back when the tab opens, a note still at the velocity written gets its
//   original in the notation; a note changed in Live since (another velocity) takes Live's as its original; a note
//   without one (added later) its own. "write" -> "shape": the originals are written back (one write) and the device
//   shapes; "shape" -> "write": written from the originals.
//   MuseScore's own playback of the tab (MuseScore's sounds, or the notes it sends to the Live track) plays every note
//   shaped by the lane in both outputs (rendermidi: the clip score's "velocity" lane), as Live plays it.
//   The device's record (MuseScoreLink.js): mode (0 scale, 1 absolute), output (0 shape, 1 write), points n, per point
//   tick value curve (0 step, 1 ramp) c1x c1y c2x c2y, originals m, per note id pitch start (UNITS, clip time) velocity
//   written.
//     MuseScore -> device: /ms/vel/set key:s serial:i chunk:i chunks:i (atom) × n   (n 0 or no points and no originals:
//                          removed);  /ms/vel/ask key:s
//     device -> MuseScore: /live/vel/set key:s serial:i status:s kept:i;
//                          /live/vel/curve key:s found:i kept:i chunk:i chunks:i (atom) × n
//---------------------------------------------------------

constexpr int VEL_PROTOCOL      = 6;            // the device's protocol from which it keeps and shapes velocity curves
constexpr int VEL_UNITS         = 3840;         // the device's UNITS a beat (LiveClips::UNITS_PER_BEAT): originals' starts
extern const char* const VELOCITY_TARGET;       // "velocity"

enum class VelMode : signed char { SCALE, SET };      // (SET: absolute; ABSOLUTE is a macro of windows.h)
enum class VelOutput : signed char { SHAPE, WRITE };

VelMode velMode(const Automation::Lane& lane);
VelOutput velOutput(const Automation::Lane& lane);
void setVelMode(Automation::Lane& lane, VelMode m);
void setVelOutput(Automation::Lane& lane, VelOutput o);
// a note's velocity v with the lane's value u at its start (u < 0: before the first point, v as it is)
int shapeVelocity(int v, double u, VelMode m);
// the lane's value as shown: "100 %" / "64"; and the number in a value dialog (%, or 1-127) and back
QString velocityText(double u, VelMode m);
double velocityShown(double u, VelMode m);
double velocityFromShown(double x, VelMode m);

struct VelocityLane {
      bool present { false };             // the score has a "velocity" lane with points
      Automation::Lane lane;
      VelMode mode { VelMode::SCALE };
      VelOutput output { VelOutput::SHAPE };
      };
VelocityLane velocityLane(const Score* score);

// the score's notes as Live should have them: in "write", each velocity shaped by the lane (the notation keeps the
// originals); else signatures()
std::vector<Sig> signaturesForLive(const Score* score, std::vector<Note*>* notes = nullptr);

struct Original {
      int id { 0 };
      int pitch { 60 };
      int start { 0 };                    // VEL_UNITS a beat, clip time
      int velocity { 100 };               // before the curve
      int written { 100 };                // as written into Live
      };
// in "write": each Live note a notation note stands for, with the notation's velocity (the original) and Live's (as
// written), after a write (the baseline is Live's then)
std::vector<Original> originals(const Baseline& base, const Score* score);
// a clip opened again (its curve and originals read back): each notation note whose Live notes are all at the
// velocity written gets its original (no undo step, as the import's own velocities); returns how many
int applyOriginals(const Baseline& base, Score* score, const std::vector<Original>& originals);

// the device's record and back
QVariantList velRecord(const VelocityLane& v, const std::vector<Original>& originals);
bool parseVelRecord(const QVariantList& atoms, VelocityLane* v, std::vector<Original>* originals);
std::vector<QByteArray> velSetPackets(const QString& key, int serial, const QVariantList& atoms);

}     // namespace LiveClipEdit

namespace LiveIntegration {

//---------------------------------------------------------
//   LinkWatch: the connection to the MuseScore Link device lost and back (the owner, 2026-10-02: "musescore
//   should notify you if a connection stopped"). The device says hello every 2 s; none for 6 s (Live or the set
//   closed, the device deleted, its port changed), or its /live/bye (the hub copy deleted): lost. One notice per
//   loss, only when something used the link (a clip tab, Live plays the score, Play through Live), and one when
//   it answers again. Shown as a bar across the top of the score area until dismissed or back (LiveClipsLink::notice in
//   liveclips.h), never a dialog. (Here, with the pure part, so the tests needn't link the window.)
//---------------------------------------------------------

struct LinkWatch {
      enum Change { NONE, LOST, BACK };
      struct Uses {
            int clipTabs { 0 };           // Live clips open here
            int clipTabsThroughLive { 0 };// of them playing through their Live track
            bool livePlaysScore { false };
            bool playThroughLive { false };
            int port { 9001 };
            bool any() const { return clipTabs || livePlaysScore || playThroughLive; }
            };
      bool up { false };
      bool lost { false };                // a loss announced, not back yet
      // answers: the device said hello lately; inUse: something uses the link now
      Change update(bool answers, bool inUse);
      static QString lostText(const Uses& u);
      static QString backText(const Uses& u);
      };

}     // namespace LiveIntegration
}     // namespace Ms
#endif
