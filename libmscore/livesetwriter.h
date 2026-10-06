//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVESETWRITER_H__
#define __LIVESETWRITER_H__

//---------------------------------------------------------
//   LiveSetWriter: a ready-made Ableton Live 12 Set (.als) for a score (LIVE.md › Create Live Set; the
//   owner, 2026-09-30, about setting up Live's tracks by hand: "that's so many manual steps. is the
//   creating MIDI track and renaming it, dragging in Kontakt 8, etc. possible to be automated?").
//
//   One MIDI track per sound-library route (SoundLib::routes: each part's patch, its extra patches, its
//   copies for other tunings), in that order, named as the MuseScore Link device finds a route's track
//   (trackName), MIDI From = the route's port and channel, and on it the MuseScore Link device (a Max
//   MIDI Effect, referencing its .amxd) before the library's plug-in (a VST 3 PluginDevice holding the
//   patch's state, as MuseScore's own setups hold it). The song's tempo and time signature are the
//   score's first. No clips: the device writes them ("Live plays the score"), no return tracks.
//
//   The format is Ableton's own and undocumented. Live refuses a set it can't read ("The document could
//   not be opened"), and only real Live can say what it accepts. So nothing here is a guess at which
//   elements Live could do without: every element is written that Live 12.2 writes itself, in its
//   order, with Live's own default values, learned from sets saved by the owner's Live 12.2
//   (MinorVersion 12.0_12203; not in the repository) and written out here by hand. What is left out is
//   content, not structure: return tracks (a set may have none: SendsPre and every track's Sends are
//   then empty), clips, automation, other devices. tools/live/test/compare_als_skeleton.py compares a
//   generated set's element tree with a set Live saved.
//
//   Bookkeeping Live keeps (and was kept consistent here):
//   - "pointee" ids: the Id of every AutomationTarget, ModulationTarget, Pointee, *ModulationTarget and
//     MidiControllers' ControllerTargets.<n> is unique in the whole set, and NextPointeeId is above all
//     of them. Other Id attributes are indices in their own list (tracks, devices, clip slots, scenes).
//   - every track has one clip slot per scene (MainSequencer and FreezeSequencer).
//   - binary data (a plug-in's state, a Max device's saved data) is upper-case hex, 40 bytes a line.
//   - the time signature is one number: (numerator - 1) + 99 × log2(denominator) (4/4 = 201).
//---------------------------------------------------------

#include <cstdint>
#include <vector>

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace Ms {
class Part;
class Score;
namespace LiveSet {
struct Set;
}
namespace SoundLib {
class Library;
struct LibInstrument;
}

namespace LiveSetWriter {

// the library's plug-in on a track (a VST 3 instrument)
struct Plugin {
      QString name;                       // the class's name, as Live shows it ("Kontakt 8")
      quint32 uid[4] { 0, 0, 0, 0 };      // its class id (FUID) as four 32-bit words, most significant byte first
      int audioOutputs { 2 };             // channels (Kontakt 8: 16)
      QByteArray component;               // IComponent's state: the patch (ProcessorState)
      QByteArray controller;              // IEditController's (ControllerState; Kontakt's: empty)
      // parameters shown in Live's panel (Configure), each with its value: the part's Controllers set on the plug-in
      // (the same values are in the component state)
      struct Parameter {
            long id { -1 };               // the plug-in's parameter id
            QString name;                 // its title
            double value { 0 };           // normalized 0-1
            };
      std::vector<Parameter> parameters;
      };

// the MuseScore Link device (tools/live/): its .amxd, referenced by path as Live does
struct LinkDevice {
      QString path;                       // absolute ('/' separators)
      QString userLibraryPath;            // relative to Live's User Library when it is in there ("Presets/…"), else ""
      qint64 size { 0 };
      int crc { 0 };                      // Live's OriginalCrc (fileCrc)
      qint64 modified { 0 };              // seconds since 1970 (LastModDate)
      int port { 9001 };                  // the device's saved Port (MuseScore's io/live/clipsPort)
      bool valid() const { return !path.isEmpty(); }
      };

// an arrangement MIDI clip (the plain set: plainliveset.h)
struct Clip {
      QString name;
      double start { 0 };                 // beats in the song
      double end { 0 };
      struct Note {
            int pitch { 60 };
            double start { 0 };           // beats from the clip's start
            double length { 0 };
            int velocity { 100 };
            };
      std::vector<Note> notes;
      // a MIDI controller's envelope in the clip: (beats from the clip's start, value), by time; the first point's value
      // is the value before it too; two points at one time are a step
      struct Envelope {
            int controller { 0 };         // the CC; PITCH_BEND_ENVELOPE: the bend (0-16383)
            std::vector<std::pair<double, double>> points;
            };
      std::vector<Envelope> envelopes;
      // the clip's Bank / Sub / Pgm (-1: none): Live sends CC0 (0 when Bank is none), CC32 Sub and the program change
      // at the clip's start, also when playback starts inside it, and only when Bank or Sub differ from what the track
      // sent last (Live 12.4.6, the VM, 2026-10-06)
      int bank { -1 };
      int subBank { -1 };
      int program { -1 };
      };
constexpr int PITCH_BEND_ENVELOPE = -1;

// the automation of a parameter of the track's plug-in (in Plugin::parameters by its name: the export adds the ones
// automated): (beats in the song, value 0-1), by time
struct ParameterAutomation {
      QString name;
      std::vector<std::pair<double, double>> points;
      };

// an envelope of the track's own mixer as Live keeps it (the owner's, read back from a saved set: livetracks.h): its
// events (beats in the song) with their curves, the value before the first ("initial", -1: the first event's)
struct MixerAutomation {
      enum class Target : signed char { VOLUME, PAN, SPEAKER };
      Target target { Target::VOLUME };
      double initial { -1 };
      struct Event {
            double time { 0 };
            double value { 0 };           // Volume: a linear gain; Pan -1 … 1; Speaker 0 / 1 (written as Live's BoolEvent)
            bool curved { false };        // CurveControl1X/1Y/2X/2Y written
            double c1x { 1.0 / 3 }, c1y { 1.0 / 3 }, c2x { 2.0 / 3 }, c2y { 2.0 / 3 };
            };
      std::vector<Event> events;
      };

struct Track {
      QString name;
      QString portName;                   // MIDI From: "MuseScore A" …; "" : All Ins
      int channel { 0 };                  // 1-16 with a port; 0: all channels
      int color { 0 };                    // Live's colour index 0-69
      bool link { true };                 // the MuseScore Link device on it
      bool hasPlugin { false };
      Plugin plugin;
      // Live's mixer (the part's Mixer values as MuseScore's host plays them: SoundLib::partMix, mixGain / mixPan)
      double volume { 1 };                // a linear gain (1 = 0 dB), Live's range 0.000316 (-70 dB) … 1.995 (+6 dB)
      double pan { 0 };                   // -1 (left) … 1 (right)
      bool active { true };               // the Track Activator: off for a muted part
      // the route's plug-in parameter lanes as the MuseScore Link device keeps them in the set (its "Lanes" stores), so
      // the set plays them without MuseScore: LiveClips::Track::params, paramsHash; empty: none
      struct LinkLane {
            QString title;
            long id { -1 };
            std::vector<std::pair<int, float>> events;      // (units, value 0-1)
            };
      std::vector<LinkLane> linkLanes;
      quint32 linkHash { 0 };
      double linkLength { 0 };            // the song's length in beats
      // the plain set (plainliveset.h)
      bool group { false };               // a group track: no devices, no clips
      int groupIndex { -1 };              // the group it is in (its index in Spec::tracks, before this one); -1: none
      bool unfolded { true };             // a group: its tracks shown
      int midiTo { -1 };                  // MIDI To: the track (its index in Spec::tracks) whose plug-in plays this one's MIDI
      std::vector<Clip> clips;            // arrangement clips, by start, not overlapping
      std::vector<ParameterAutomation> automation;
      std::vector<MixerAutomation> mixerAutomation;   // (a group's too)
      QString annotation;                 // the track's Info text (Name/Annotation): the plain set's track key
      // (not written)
      QString routeKey;                   // "<port>:<channel 1-16>", as LiveClips::Track::key
      QString part;                       // the part's name
      QString patch;                      // the patch's
      bool mainPatch { true };            // the part's main patch (its first copy): found by the part's name
      const SoundLib::LibInstrument* instrument { nullptr };  // the patch
      const Part* partRef { nullptr };    // the route (SoundLib::Route)
      int port { 0 };
      int routePatch { 0 };
      int lane { 0 };
      };

struct Spec {
      double tempo { 120 };               // bpm
      int numerator { 4 };
      int denominator { 4 };
      std::vector<Track> tracks;
      LinkDevice link;
      };

// the score's routes (SoundLib::routes, in order) as tracks: names, MIDI From (portNames: MIDI outputs
// A-D as Live shows them, "" when not set), a colour per part; with the device, without a plug-in (the
// caller adds each patch's)
std::vector<Track> tracks(const Score* score, const SoundLib::Library& library, const QStringList& portNames);
// Live's colour (0-69) of the n-th part: a few steps apart in Live's colour chooser
int partColor(int n);
// the song: the score's first tempo (as "Live plays the score" plays it: LiveClips::timeline) and time signature
void setSong(const Score* score, Spec* spec);

// what the file says it is: Live 12.2's own
extern const char* const CREATOR;
extern const char* const MINOR_VERSION;

QByteArray xml(const Spec& spec, int* nextPointeeId = nullptr);
// Live's bookkeeping checked in a set's XML (write() checks what it writes): well formed; every pointee id
// unique and below NextPointeeId; each track's MainSequencer and FreezeSequencer with one clip slot per scene (a
// group track's Slots one group slot per scene); a track in a group after its group;
// a track's devices with different ids. "" : fine, else the first problem
QString validate(const QByteArray& xml);
QByteArray gzip(const QByteArray& data);
bool write(const QString& path, const Spec& spec, QString* error);

// the Mixer's volume (0-127) as Live's track Volume: MuseScore's host gain (v / 100)² (Vst3Synth::volumeGain), within
// Live's range (0 → -70 dB, Live's lowest; 127 → +4.15 dB)
double mixGain(int volume);
// the Mixer's pan (0-127, 64 centre) as Live's Pan (-1 … 1): Vst3Synth::panGains' position. Both are constant-power
// sine/cosine laws, 0 dB at the centre and +3 dB fully panned (Live 12 manual, Audio Fact Sheet › Panning), so the
// same position gives the same gains
double mixPan(int pan);
// back (a Live track's mixer read for the part's Mixer, livetracks.h): the nearest Mixer value, 0-127
int mixVolume(double gain);
int mixPanValue(double pan);

// Live's time signature number; -1: one Live can't have (numerator 1-99, denominator 1, 2, 4, 8, 16)
int timeSignatureId(int numerator, int denominator);
// Live's OriginalCrc of a file: CRC-16 (polynomial 0x8005, no reflection, start 0: "CRC-16/UMTS") of its first
// 16 KiB. Found by trying the usual CRC-16s on the owner's MuseScore Link.amxd (49211 bytes, OriginalCrc 35326):
// this is the one match at a round length; a single example, so unconfirmed (Live finds the file by its path first)
quint16 fileCrc(const QByteArray& data);
// the device's saved data (MxDBlob) with its port, as Max writes it
// a lane's events packed as the device keeps them (MuseScoreLink.js packLane: events and runs of steps)
std::vector<double> packLane(const std::vector<std::pair<int, float>>& events);
// the device's saved data (Max's dictionary as JSON, as in the owner's set): its Port, and the track's lanes in its
// stores "Lanes", "Lanes2" … (MuseScoreLink.js: msl-lanes 2 <stamp> <part> <parts> <length> <routes> …, at most
// LINK_STORE_ATOMS atoms each, LINK_STORES of them); lanes too long for the stores are left out (*lanesKept false)
constexpr int LINK_STORES = 4;
constexpr int LINK_STORE_ATOMS = 30000;
QByteArray linkBlob(int port, const Track* track = nullptr, bool* lanesKept = nullptr);

// the name of a route's track, as the MuseScore Link device finds it (tools/live/MuseScoreLink.js,
// findTrack): the part's name for its main patch; "<part> – <patch>" for another patch; " (n)" added for
// the n-th copy for another tuning (LiveClips::clipName without "MuseScore: ")
QString trackName(const QString& part, const QString& patch, bool mainPatch, int lane);

// names compared as the device compares them: lower case, letters and digits only
QString loose(const QString& s);
// does the set have the track (by MIDI From = port and channel, or by name, as the device looks)
bool hasTrack(const LiveSet::Set& set, const Track& t);

}     // namespace LiveSetWriter
}     // namespace Ms
#endif
