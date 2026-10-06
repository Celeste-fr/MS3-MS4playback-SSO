//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVESET_H__
#define __LIVESET_H__

//---------------------------------------------------------
//   LiveSet: the automation of an Ableton Live Set (.als), for the score (LIVE.md; the owner,
//   2026-09-28: "all automation is drawn in Live … saved back into the score so MuseScore alone
//   plays it identically", read-only in MuseScore).
//
//   No Live API reads arrangement automation, so the file is read: gzip-compressed XML. The
//   schema is Ableton's and undocumented; the element names below are those open-source readers
//   use (DawVert's ableton parser, dawtool, WaveSabre's LiveParser) and are **unverified against
//   a Live 12 file of the owner's** until one is tried (the tests' sets are built by hand):
//     Ableton/LiveSet/Tracks/MidiTrack (also AudioTrack, GroupTrack, ReturnTrack)
//       Id=, TrackGroupId (the group track's Id, -1: none)
//       Name/EffectiveName, Name/UserName, Name/Annotation (the track's Info text) (Value=)
//       DeviceChain/MidiOutputRouting/Target (MIDI To another track: "MidiOut/Track.<id>/TrackIn")
//       DeviceChain/Mixer/Volume, Pan, Speaker: Manual Value= and an AutomationTarget Id= (the track's mixer)
//       DeviceChain/MidiInputRouting/Target (Value="MidiIn/External.<device>/<channel>" …),
//         UpperDisplayString, LowerDisplayString
//       DeviceChain/DeviceChain/Devices/PluginDevice: PluginDesc/Vst3PluginInfo/Name (or
//         VstPluginInfo/PlugName), ParameterList/PluginFloatParameter: ParameterName, ParameterId,
//         ParameterValue/AutomationTarget Id= (the plug-in's value normalized 0-1)
//       AutomationEnvelopes/Envelopes/AutomationEnvelope: EnvelopeTarget/PointeeId (a target's
//         Id), Automation/Events/FloatEvent Time= (quarter notes from the arrangement's start)
//         Value= [CurveControl1X/1Y/2X/2Y]; the first event at Time -63072000 is the value
//         before everything else
//       MIDI clips (MainSequencer/ClipTimeable/ArrangerAutomation/Events/MidiClip Time=):
//         Envelopes/Envelopes/ClipEnvelope with EnvelopeTarget/PointeeId one of the track's
//         MidiControllers/ControllerTargets.<n> AutomationTarget Ids (n = 2 … 129: CC 0 … 127),
//         Automation/Events/FloatEvent (times relative to the clip's start, 0-127)
//   Everything is read by element name wherever it sits under its track, so a device inside a
//   rack or a moved element is still found.
//   The song's tempo (clip tabs follow it, liveclipmodel.h › Clip tempo): the main track (MainTrack in Live 12,
//   MasterTrack before) holds DeviceChain/Mixer/Tempo with Manual Value= (bpm) and an AutomationTarget Id=; its
//   AutomationEnvelopes/…/AutomationEnvelope whose PointeeId is that Id is the tempo automation: FloatEvent Time=
//   (song beats) Value= (bpm), the default event first, a curve's CurveControl… as any envelope's. (Checked with Live
//   12.2 and 12.4.6 sets: the owner's WASP_From_Mars set, the VM's; none had tempo breakpoints of Live's making.)
//   Each track's arrangement MIDI clips are kept too (their place: which file is the set a clip comes from).
//
//   Beats to ticks: 480 a quarter note; Live's beat 0 is the score's start (Live follows
//   MuseScore's clock, midisync.h), in played (unrolled) time: a point in a repeat's second pass
//   is left out (a lane is by score tick), the report says how many.
//---------------------------------------------------------

#include <map>
#include <vector>
#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include "automation.h"

namespace Ms {

class MasterScore;
class Part;

namespace LiveSet {

constexpr double DEFAULT_EVENT_TIME = -63072000;      // Live's "before everything" event

struct Point {
      double beat { 0 };            // quarter notes from the arrangement's start
      double value { 0 };           // as Live stores it (plug-in: 0-1; clip CC: 0-127)
      };

// an envelope's event as Live keeps it (FloatEvent): a curved one carries the Bézier of the segment after it
struct Event {
      double time { 0 };            // beats (Live's axis: the arrangement's, or a clip's)
      double value { 0 };
      bool curved { false };
      double c1x { 1.0 / 3 }, c1y { 1.0 / 3 }, c2x { 2.0 / 3 }, c2y { 2.0 / 3 };
      };
// a hash of Live's events (rounded: Live may write the same numbers otherwise when it saves), "initial" the default
// event's value (-1: none): the lane's liveHash (automation.h), the same whether the set was written here or by Live
QString eventsHash(double initial, const std::vector<Event>& events);

struct Envelope {
      enum class Kind : signed char { PARAMETER, CLIP_CC, OTHER };
      Kind kind { Kind::OTHER };
      QString device;               // the plug-in (PARAMETER)
      QString parameter;            // its parameter's name (PARAMETER), or what Live's own is (OTHER)
      int parameterId { -1 };       // the plug-in's id of it (PARAMETER; -1: not known)
      int cc { -1 };                // CLIP_CC: 0-127
      double initial { -1 };        // the value before the first point (Live's default event), -1: none
      std::vector<Point> points;    // by beat; curves already made into straight segments
      std::vector<Event> events;    // as Live has them (the default event left out: initial), by time
      bool inClip { false };        // a clip's envelope (its own axis, looped: points placed in the arrangement)
      // the track's own mixer (OTHER): "volume", "pan" or "speaker" (the Track Activator, 0 / 1); "" : not the mixer's
      QString mixer;
      };

// an arrangement MIDI clip (MainSequencer/…/ArrangerAutomation/Events/MidiClip)
struct ArrangementClip {
      double time { 0 };            // Time=
      double start { 0 };           // CurrentStart (song beats; Live's API: start_time)
      double end { 0 };             // CurrentEnd (end_time)
      double loopStart { 0 };       // Loop/LoopStart (clip beats)
      double loopEnd { 0 };         // Loop/LoopEnd
      double startRelative { 0 };   // Loop/StartRelative (the start marker is LoopStart + StartRelative)
      bool loopOn { false };
      QString name;
      };

struct Track {
      QString kind;                 // MidiTrack, AudioTrack, GroupTrack, ReturnTrack (Live's API lists the returns apart)
      QString name;                 // UserName, else EffectiveName
      QString inputTarget;          // MidiInputRouting/Target as written
      QString inputDevice;          // the MIDI port it listens to ("" : none or all)
      int inputChannel { -1 };      // 1-16; -1: all
      QStringList devices;          // its plug-ins
      std::vector<Envelope> envelopes;
      std::vector<ArrangementClip> clips;
      // the plain set's read-back (livetracks.h): the track's Id= and its group's (TrackGroupId, -1: none), its Info
      // text (Name/Annotation: the plain set's track key), MIDI To (MidiOutputRouting/Target: "MidiOut/Track.<id>/TrackIn")
      int id { -1 };
      int groupId { -1 };
      QString annotation;
      QString outputTarget;
      // its mixer's static values (DeviceChain/Mixer/<Volume|Pan|Speaker>/Manual); hasMixer: read
      bool hasMixer { false };
      double volume { 1 };          // a linear gain (1 = 0 dB)
      double pan { 0 };             // -1 … 1
      bool active { true };         // the Track Activator (Speaker)
      };

struct Set {
      QString creator;              // "Ableton Live 12.x"
      double tempo { 0 };           // the arrangement's first tempo (bpm): the main track's Tempo Manual
      // the main track's tempo automation as Live keeps it (bpm by song beat; the default event left out: tempoInitial,
      // -1 none); empty: no automation (the tempo is `tempo` throughout)
      double tempoInitial { -1 };
      std::vector<Event> tempoEvents;
      // a clip named "MuseScore: …" (liveclips.h): Live played the score as clips, at one tempo, the
      // notes at their real times: a beat is seconds × tempo / 60, not a quarter note of the score
      bool museScoreClips { false };
      std::vector<Track> tracks;
      QString error;                // "": read
      };

QByteArray gunzip(const QByteArray& data, QString* error);
Set parse(const QByteArray& xml);
Set read(const QString& path);                // a .als (gzip) or its XML

// Live's curve between two points (CurveControl1X/1Y/2X/2Y: a cubic Bézier's control points in
// the segment, 0-1 each way) as straight pieces: the points after `a` up to and including `b`, nowhere further
// than tolValue (in the envelope's own units: one MIDI step of its range) from the curve (Automation::flattenCurve)
std::vector<Point> curve(const Point& a, const Point& b, double c1x, double c1y, double c2x, double c2y, double tolValue);

//---------------------------------------------------------
//   Matching to the score
//---------------------------------------------------------

struct Target {
      QString id;                   // SoundLib::Controller::id (the lane's target)
      QString title;                // its plug-in parameter title ("" for a CC)
      QString name;                 // shown
      int cc { -1 };
      };

struct PartInfo {
      const Part* part { nullptr };
      QString portName;             // the MIDI output it plays on, as Live would show it
      int channel { 0 };            // 1-16
      std::vector<Target> targets;  // its main patch's controllers
      };

// the sound library's parts: route, port name (portNames: output A-D's device names), controllers
std::vector<PartInfo> partInfos(const MasterScore* score, const QStringList& portNames);

struct Report {
      QStringList matched;          // "track -> part: n lanes"
      QStringList unmatched;        // tracks, parameters that found no part or controller
      int lanes { 0 };
      int repeatedPoints { 0 };     // points left out in a repeat's later pass
      QString text() const;
      };

// titles compared loosely (Vst3Plugin::parameterId's rule: case, spaces, punctuation and a slot
// number in front ignored)
QString looseTitle(const QString& t);
// a port name without its driver's prefix ("MMSystem,MuseScore A" -> "MuseScore A")
QString portDisplayName(const QString& interfaceAndName);

// a track whose part is already known (the plain set's, livetracks.h): its part (nullptr: none) and which of its
// envelopes become lanes (by index in Track::envelopes)
struct Bound {
      const Part* part { nullptr };
      std::vector<bool> take;
      };

// the lanes for the score: marked "source": "live" with the set's path and time (automation.h); `bound`: by index in
// set.tracks, the tracks matched already (the others by MIDI input, else name)
std::map<const Part*, Automation::PartLanes> lanes(const MasterScore* score, const Set& set, const std::vector<PartInfo>& parts,
                                                   const QString& path, const QDateTime& modified, Report* report,
                                                   const std::map<size_t, Bound>* bound = nullptr);

}     // namespace LiveSet
}     // namespace Ms
#endif
