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
//     at the top of the key range (CARRIER_LOW … 127: one pitch per controller, the value as the
//     velocity: carrierVelocity) and pitch bend (the microtones of a patch that bends, legato-timing's
//     libraryPitchBends: 14 bit on keys 115 (upper 7 bits) and 114 (lower 7), each written when it changes; a glide's
//     3 ms steps each one carrier). The MuseScore Link device (tools/live/), placed before the
//     library's plug-in on the track, turns each carrier's note-on into its controller and drops
//     its note-off. The Live Object Model can write notes but not a clip's MIDI controller
//     envelopes; carrier notes keep the controllers in the clip, played by Live's own clock:
//     sample-exact with the notes, in an offline export and a freeze too, and chased when
//     playback starts mid-way (Live's "Chase MIDI Notes", on by default: each carrier lasts
//     until that controller's next value, so the value in force is sent again at the start).
//   - A controller at the same tick as a note comes EPSILON units (and one EPSILON more for each
//     earlier one at that tick) before it, so the switch and the dynamics are in before the note:
//     switches first, then the controllers in the renderer's order, the pitch bend last (nearest the
//     note: the bend and the controllers also move what still rings). At a tick without a note (a
//     glide's step, a change under a held note) the last of them is at the tick itself. At the very
//     start, where nothing can come earlier, the notes wait instead.
//   - Plug-in parameter events (the automation lanes MuseScore plays: Track::params, rendered with
//     MidiRenderer::setForLiveClips) are not notes: they go to the device as /ms/params, which sets the
//     track's plug-in parameter at signal rate (live.remote~ from a table of the value in force at each
//     millisecond, read at Live's song position: tools/live). A lane Live's set plays itself
//     (Automation::Lane::playedByLive) is left to Live.
//   - Left out: other controllers (Track::dropped), program and bank changes.
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
//       /ms/params gen:i key:s lanes:i hash:i  (protocol 3: the route's plug-in parameter lanes follow; 0: none)
//       /ms/pvals  gen:i key:s lane:i title:s pid:i chunk:i chunks:i (time:i value:f) × n
//                                        (a lane: the parameter's title and plug-in id (-1: not known; Live 12.2
//                                         names Kontakt's slots "#001" …), each value from its time on, steps:
//                                         ramps and curves come sampled as the renderer plays them)
//       /ms/play   beat:f                (MuseScore's Play: Live starts there)
//       /ms/stop
//     device -> MuseScore
//       /live/hello     session:s protocol:i        (on load, then every 2 s)
//       /live/bye       session:s                   (the hub copy goes: deleted, its set closed)
//       /live/resync                                (send everything again)
//       /live/applied   key:s hash:i status:s track:s   (key "song" for the tempo and locators)
//       /live/papplied  key:s hash:i status:s track:s   (the parameter lanes: "ok", "missing: <titles>" …)
//       /live/transport playing:i beat:f bpm:f      (~25 a second while playing, on each change)
//---------------------------------------------------------

#include <map>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include "automation.h"

namespace Ms {

class EventMap;
class Score;

namespace SoundLib {
class Library;
}

namespace LiveClips {

constexpr int PROTOCOL           = 4;         // 2: editing Live clips (mscore/liveclipmodel.h); 3: parameter lanes; 4: clip tabs play through their track
constexpr int PVALS_PER_PACKET   = 100;       // (time, value) pairs a /ms/pvals
constexpr int UNITS_PER_BEAT     = 3840;
// EPSILON measured (numbers-measured, 2026-10-03, Live 12.4.6 on the Windows VM, the MuseScore Link hub writing the
// clip, Kontakt 8 + SSO Violins 1, frozen): 160 quarters alternating Spiccato / Tremolo, each after its CC32 carrier,
// rendered alike with the carrier 1 unit before the note (120 and 240 bpm) or 0 units (240 bpm) as with 96 units
// before it (every note's level envelope within 0.00 dB); 96 units after it changed 157 of 160. Rule: the smallest
// spacing the setting allows (1) that kept every carrier before its note
constexpr int EPSILON            = 1;         // units: ~0.13 ms at 120 bpm
constexpr int NOTES_PER_PACKET   = 48;        // 5 int32 + 5 type tags each: about 1.3 kB a datagram
constexpr int CUES_PER_PACKET    = 40;
constexpr int DEFAULT_PORT       = 9001;
constexpr int CARRIER_COUNT      = 12;
// the controller each carrier pitch stands for: pitch 127 - i -> CARRIER_CCS[i] (the device has the same table)
extern const int CARRIER_CCS[CARRIER_COUNT];
int carrierPitch(int cc);                     // -1: none
// pitch bend (14 bit, 0-16383, centre 8192) as two carriers below the controllers': its upper 7 bits on BEND_MSB,
// its lower 7 on BEND_LSB, each as carrierVelocity has it. The device keeps the last of each and sends the whole
// bend at either, so the pair is right in any order (a chase at a mid-song start). Only the half that changed is
// written; both: in the order whose value in between is nearer the new bend (the first bend: the upper half first,
// the device starting at the centre)
constexpr int BEND_MSB           = 115;
constexpr int BEND_LSB           = 114;
constexpr int CARRIER_LOW        = 114;       // keys CARRIER_LOW … 127 are carriers, never played as notes
// a carrier's velocity (1-127: 0 is a note-off) for a value 0-127, and the value the device makes of it. 128
// values in 127 velocities: the UACC switch (key 127) is value + 1 (UACC 1 … 126 exact; 127 is no articulation);
// the other controllers and the bend halves are the value, 0 as 1 (so 127, CC11's and a pedal's usual value, is
// exact; a value of 1 plays as 0: CC1 / CC11 1 is as silent as 0, the bend 1/16384 lower). Until 2026-09-30 every
// carrier was value + 1, and CC11 127 played as 126 on every Live track
int carrierVelocity(int pitch, int value);
int carrierValue(int pitch, int velocity);

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
      int dropped { 0 };            // controllers without a carrier
      int bends { 0 };              // pitch bend values carried (one like the value before isn't written again)
      int parameters { 0 };         // plug-in parameter events (the automation lanes MuseScore plays in Live: params)
      int highNotes { 0 };          // notes at a carrier's pitch (not played as notes by the device)
      // the plug-in parameter events by the controller's index (the main patch's allControllers): (units, value 0-1)
      std::map<int, std::vector<std::pair<int, float>>> params;
      };

// a part's lanes on parameters of its Live track ("live:<d>/<p>": the device sends the track's parameters,
// mscore/liveclipmodel.h), in order: the renderer's parameter events for them are of controller LIVE_PARAM with
// their index here as the second byte; their /ms/pvals title is the target, which the device resolves on the track
constexpr int LIVE_PARAM         = 255;
QStringList liveLanes(const std::vector<Automation::Lane>& lanes);
constexpr int LIVE_PARAM_KEY     = 1 << 16;   // RouteNotes::params' key of live lane k: LIVE_PARAM_KEY + k

// route index: port * 16 + channel (0-15), as NPlayEvent::extPort / extChannel
std::map<int, RouteNotes> clipNotes(const EventMap& events, const Timeline& tl, int endUtick);

struct Track {
      QString key;                  // "<port index>:<channel 1-16>", e.g. "0:3"
      int port { 0 };               // 0-3: MIDI output A-D
      int channel { 1 };            // 1-16
      QString portName;             // as Live shows it ("MuseScore A"); "" when that output isn't set
      QString part;                 // the part's name (the track found by name when no track has the route)
      QString clip;                 // "MuseScore: Violins 1", "MuseScore: Violins 1 – <patch>"
      QString patch;                // the patch it plays
      bool main { true };           // the part's main patch (its first tuning lane): may be found by name
      int length { 0 };             // units: the played score
      std::vector<Note> notes;
      int dropped { 0 };
      int bends { 0 };
      int parameters { 0 };
      int highNotes { 0 };
      quint32 hash { 0 };           // of all that is drawn
      // the automation lanes of plug-in parameters MuseScore plays (not those Live's set plays itself:
      // Automation::Lane::playedByLive), for the device to set on the track's plug-in (/ms/params)
      struct ParamLane {
            QString title;                                  // the parameter's title (the map's <Controller param>)
            long id { -1 };                                 // the plug-in's parameter id, when known (-1)
            std::vector<std::pair<int, float>> events;      // (units, value 0-1): the value from then on
            };
      std::vector<ParamLane> params;
      quint32 paramsHash { 0 };
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
std::vector<QByteArray> paramPackets(const Track& t, int generation); // /ms/params, then its lanes' /ms/pvals
std::vector<QByteArray> packets(const Song& s, int generation);       // /ms/song, then its /ms/cues
QByteArray clearPacket(const Track& t, int generation);

}     // namespace LiveClips
}     // namespace Ms
#endif
