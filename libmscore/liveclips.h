//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVECLIPS_H__
#define __LIVECLIPS_H__

//---------------------------------------------------------
//   "Live plays the score" (LIVE.md › Live plays the score; the owner, 2026-09-29: "is it possible to
//   have entered notes in the score live update in ableton?", then "what if it's the other way, when
//   ableton is playing sound MuseScore switches off").
//
//   Each sound-library route of the score becomes one MIDI clip in Live's arrangement, on the
//   track that route plays on, playable and kept up to date with every edit of the score. Live
//   hosts the library and plays; Live is the clock; MuseScore follows it (mscore/liveclips.h).
//
//   The clip holds what MuseScore's playback renders for that route (MidiRenderer, repeats
//   unrolled as the Play Panel plays them), so Live sounds as MuseScore did:
//   - the notes, velocities, and keyswitch notes of libraries that switch by key;
//   - the controllers (UACC CC32 switches, CC1 dynamics, CC11, CC64 pedal …) as *carrier notes*
//     at the top of the key range (CARRIER_LOW … 127: one pitch per controller, velocity = value
//     + 1, 127 played as 126). The MuseScore Link device (tools/live/), placed before the
//     library's plug-in on the track, turns each carrier's note-on into its controller and drops
//     its note-off. The Live Object Model can write notes but not a clip's MIDI controller
//     envelopes; carrier notes keep the controllers in the clip, played by Live's own clock:
//     sample-exact with the notes, in an offline export and a freeze too, and chased when
//     playback starts mid-way (Live's "Chase MIDI Notes", on by default: each carrier lasts
//     until that controller's next value, so the value in force is sent again at the start).
//   - A controller at the same tick as a note comes EPSILON units (and one EPSILON more for each
//     earlier one at that tick) before it, so the switch and the dynamics are in before the note,
//     in the renderer's order. At the very start, where nothing can come earlier, the notes wait
//     instead.
//   - Left out: plug-in parameter events (Track::parameters: Live's own automation lanes play
//     those), other controllers and pitch bend (Track::dropped), program and bank changes.
//
//   Timeline. Live can't be given the score's tempo map (the Live Object Model can't write the
//   song tempo automation), so Live plays at one tempo, the score's first (Timeline::bpm), and
//   the clips hold the notes at their real times: beat = seconds × bpm / 60, seconds as MuseScore
//   plays them (tempo changes, fermatas, repeats; the Play Panel's relative tempo left out). With
//   one tempo throughout, beat = tick / 480 exactly: Live's bars are the score's. Else Live's grid
//   drifts from the score's bars; the device puts a locator ("MS 12") at each played bar.
//   Positions are sent in units of 1/UNITS_PER_BEAT beat (int32, exact).
//
//   The protocol (OSC over UDP on localhost; MuseScore sends to `port`, the device answers on
//   port + 1):
//     MuseScore -> device
//       /ms/mode   mode:s                ("clips": Live plays the score; "stream": MuseScore plays
//                                         through Live, the tracks' Monitor on In)
//       /ms/song   gen:i bpm:f length:i cues:i chunks:i hash:i
//       /ms/cues   gen:i chunk:i (time:i name:s) × n
//       /ms/track  gen:i key:s port:s channel:i part:s clip:s main:i length:i notes:i chunks:i hash:i
//       /ms/notes  gen:i key:s chunk:i  (pitch:i start:i length:i velocity:i mute:i) × n
//       /ms/clear  gen:i key:s port:s channel:i part:s clip:s          (a route gone: its clip goes)
//       /ms/play   beat:f                (MuseScore's Play: Live starts there)
//       /ms/stop
//     device -> MuseScore
//       /live/hello     session:s protocol:i        (on load, then every 2 s)
//       /live/resync                                (send everything again)
//       /live/applied   key:s hash:i status:s track:s   (key "song" for the tempo and locators)
//       /live/transport playing:i beat:f bpm:f      (~25 a second while playing, on each change)
//---------------------------------------------------------

#include <map>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace Ms {

class EventMap;
class Score;

namespace SoundLib {
class Library;
}

namespace LiveClips {

constexpr int PROTOCOL           = 2;         // 2: editing Live clips (mscore/liveclipmodel.h)
constexpr int UNITS_PER_BEAT     = 3840;
constexpr int EPSILON            = 2;         // units: ~0.26 ms at 120 bpm
constexpr int NOTES_PER_PACKET   = 48;        // 5 int32 + 5 type tags each: about 1.3 kB a datagram
constexpr int CUES_PER_PACKET    = 40;
constexpr int DEFAULT_PORT       = 9001;
constexpr int CARRIER_LOW        = 116;
constexpr int CARRIER_COUNT      = 12;
// the controller each carrier pitch stands for: pitch 127 - i -> CARRIER_CCS[i] (the device has the same table)
extern const int CARRIER_CCS[CARRIER_COUNT];
int carrierPitch(int cc);                     // -1: none

//---------------------------------------------------------
//   Timeline
//---------------------------------------------------------

struct Timeline {
      const Score* score { nullptr };
      double bpm { 120 };                     // Live's tempo: the score's first
      double relTempo { 1 };                  // (the Play Panel's: left out)
      double beats(int utick) const;          // played tick -> Live's beat
      int units(int utick) const;
      int utick(double beat) const;           // Live's beat -> played tick (MuseScore follows Live)
      };
Timeline timeline(const Score* score);
// the played length (the repeats unrolled as playback plays them), in ticks
int playedTicks(const Score* score);

//---------------------------------------------------------
//   the clips
//---------------------------------------------------------

struct Note {
      int pitch { 60 };
      int start { 0 };              // units
      int length { 1 };             // units
      int velocity { 100 };
      bool muted { false };         // silenced in MuseScore (the Mixer's mute or solo, a voice not played)
      bool operator==(const Note& o) const {
            return pitch == o.pitch && start == o.start && length == o.length && velocity == o.velocity && muted == o.muted;
            }
      bool operator<(const Note& o) const { return start != o.start ? start < o.start : pitch < o.pitch; }
      };

struct RouteNotes {
      std::vector<Note> notes;      // notes and carriers, by start
      int dropped { 0 };            // controllers without a carrier, pitch bend
      int parameters { 0 };         // plug-in parameter events (MuseScore's lanes and Controllers: Live's own there)
      int highNotes { 0 };          // notes at a carrier's pitch (not played as notes by the device)
      };

// route index: port * 16 + channel (0-15), as NPlayEvent::extPort / extChannel
std::map<int, RouteNotes> clipNotes(const EventMap& events, const Timeline& tl, int endUtick);

struct Track {
      QString key;                  // "<port index>:<channel 1-16>", e.g. "0:3"
      int port { 0 };               // 0-3: MIDI output A-D
      int channel { 1 };            // 1-16
      QString portName;             // as Live shows it ("MuseScore A"); "" when that output isn't set
      QString part;                 // the part's name (the track found by name when no track has the route)
      QString clip;                 // "MuseScore: Violins 1", "MuseScore: Violins 1 – <patch>"
      bool main { true };           // the part's main patch (its first tuning lane): may be found by name
      int length { 0 };             // units: the played score
      std::vector<Note> notes;
      int dropped { 0 };
      int parameters { 0 };
      int highNotes { 0 };
      quint32 hash { 0 };           // of all that is drawn
      };

QString clipName(const QString& part, const QString& patch, bool main, int lane);
std::vector<Track> tracks(const Score* score, const SoundLib::Library& library, const EventMap& events,
                          const QStringList& portNames, const Timeline& tl);
quint32 hashOf(const Track& t);

struct Cue {
      int time { 0 };               // units
      QString name;                 // "MS 12", "MS 17 B" (a rehearsal mark)
      };
struct Song {
      double bpm { 120 };
      int length { 0 };             // units
      std::vector<Cue> cues;
      quint32 hash { 0 };
      };
Song song(const Score* score, const Timeline& tl);

//---------------------------------------------------------
//   OSC 1.0: int32 ('i', from int / bool / uint), float32 ('f', from double), string ('s')
//---------------------------------------------------------

QByteArray osc(const QString& address, const QVariantList& args);
bool parseOsc(const QByteArray& data, QString* address, QVariantList* args);

std::vector<QByteArray> packets(const Track& t, int generation);      // /ms/track, then its /ms/notes
std::vector<QByteArray> packets(const Song& s, int generation);       // /ms/song, then its /ms/cues
QByteArray clearPacket(const Track& t, int generation);

}     // namespace LiveClips
}     // namespace Ms
#endif
