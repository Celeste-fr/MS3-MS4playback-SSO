//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

//---------------------------------------------------------
//   The plain set's pieces of a Live set (plainliveset.h): group tracks, arrangement MIDI clips with their notes and
//   controller envelopes, a plug-in's parameter automation, a track's MIDI To another track's plug-in.
//
//   As livesetwriter.cpp: every element Live 12.2 writes, in its order, with its values, from sets the owner's
//   Live 12.2 saved (a group track and its tracks; an arrangement clip with notes and a MIDI CC envelope; Kontakt 8
//   parameters automated on its track; not in the repository). Unconfirmed until a set is opened in Live (LIVE.md ›
//   Create Live Set › The plain set): which MidiControllers index a CC is (controllerTarget), the MIDI To string
//   (midiToRouting, as Live 12.4.6 saves it).
//
//   Times: a clip's start and end and the automation in beats in the song; a clip's notes and envelopes in beats from
//   its start. An envelope's first event at -63072000 (Live's "before everything") holds its value before the first
//   point.
//---------------------------------------------------------

#include "livesetxml.h"

#include <algorithm>
#include <map>

namespace Ms {
namespace LiveSetWriter {

static const char* const BEFORE_EVERYTHING = "-63072000";

static void automationEvents(Writer& w, const std::vector<std::pair<double, double>>& points)
      {
      w.open("Automation");
      w.open("Events");
      int id = 0;
      if (!points.empty())
            w.empty("FloatEvent", "Id=\"" + QByteArray::number(id++) + "\" Time=\"" + BEFORE_EVERYTHING + "\" Value=\""
                    + Writer::num(points.front().second).toUtf8() + "\"");
      for (const auto& p : points)
            w.empty("FloatEvent", "Id=\"" + QByteArray::number(id++) + "\" Time=\"" + Writer::num(p.first).toUtf8() + "\" Value=\""
                    + Writer::num(p.second).toUtf8() + "\"");
      w.close("Events");
      w.open("AutomationTransformViewState");
      w.value("IsTransformPending", false);
      w.empty("TimeAndValueTransforms");
      w.close("AutomationTransformViewState");
      w.close("Automation");
      }

// a mixer envelope's events as Live keeps them: FloatEvent (with its curve's control points when curved), BoolEvent for
// the Speaker (Value true / false); the first at -63072000 its value before everything
static void mixerEvents(Writer& w, const MixerAutomation& m)
      {
      w.open("Automation");
      w.open("Events");
      const bool speaker = m.target == MixerAutomation::Target::SPEAKER;
      auto value = [speaker](double v) -> QByteArray {
            if (speaker)
                  return v >= 0.5 ? "true" : "false";
            return Writer::num(v).toUtf8();
            };
      const char* tag = speaker ? "BoolEvent" : "FloatEvent";
      int id = 0;
      if (!m.events.empty() || m.initial >= 0)
            w.empty(tag, "Id=\"" + QByteArray::number(id++) + "\" Time=\"" + BEFORE_EVERYTHING + "\" Value=\""
                    + value(m.initial >= 0 || m.events.empty() ? m.initial : m.events.front().value) + "\"");
      for (const MixerAutomation::Event& e : m.events) {
            QByteArray a = "Id=\"" + QByteArray::number(id++) + "\" Time=\"" + Writer::num(e.time).toUtf8() + "\" Value=\""
                           + value(e.value) + "\"";
            if (e.curved && !speaker)
                  a += " CurveControl1X=\"" + Writer::num(e.c1x).toUtf8() + "\" CurveControl1Y=\"" + Writer::num(e.c1y).toUtf8()
                       + "\" CurveControl2X=\"" + Writer::num(e.c2x).toUtf8() + "\" CurveControl2Y=\"" + Writer::num(e.c2y).toUtf8() + "\"";
            w.empty(tag, a);
            }
      w.close("Events");
      w.open("AutomationTransformViewState");
      w.value("IsTransformPending", false);
      w.empty("TimeAndValueTransforms");
      w.close("AutomationTransformViewState");
      w.close("Automation");
      }

static void scroller(Writer& w, double right)
      {
      w.open("ScrollerTimePreserver");
      w.value("LeftTime", 0);
      w.value("RightTime", Writer::num(right));
      w.close("ScrollerTimePreserver");
      }

//---------------------------------------------------------
//   groupTrack
//---------------------------------------------------------

void groupTrack(Writer& w, const Track& t, int trackId, int groupTrackId, int scenes)
      {
      w.open("GroupTrack", "Id=\"" + QByteArray::number(trackId) + "\" SelectedToolPanel=\"7\" SelectedTransformationName=\"\" SelectedGeneratorName=\"\"");
      w.trackHead(t.name, t.name, t.color, t.annotation, t.delayMs);
      const MixerTargets mixer = mixerTargets(w, t);
      automationEnvelopes(w, t, {}, mixer);
      w.trackLists(t.unfolded, groupTrackId);
      w.open("Slots");
      for (int i = 0; i < scenes; ++i) {
            w.open("GroupTrackSlot", "Id=\"" + QByteArray::number(i) + "\"");
            w.value("LomId", 0);
            w.close("GroupTrackSlot");
            }
      w.close("Slots");
      w.value("Freeze", false);
      w.value("NeedArrangerRefreeze", true);
      w.open("DeviceChain");
      w.automationLanes(17);
      w.routing("AudioInputRouting", "AudioIn/External/S0", "Ext. In", "1/2");
      w.routing("MidiInputRouting", "MidiIn/External.All/-1", "Ext: All Ins", "");
      if (groupTrackId >= 0)
            w.routing("AudioOutputRouting", "AudioOut/GroupTrack", "Group", "");
      else
            w.routing("AudioOutputRouting", "AudioOut/Main", "Master", "");
      w.routing("MidiOutputRouting", "MidiOut/None", "None", "");
      w.open("Mixer");
      w.mixerStart(t.volume, 74, t.pan, t.active, mixer);
      w.close("Mixer");
      w.open("DeviceChain");
      w.empty("Devices");
      w.empty("SignalModulations");
      w.close("DeviceChain");
      w.open("FreezeSequencer");
      w.deviceHeader(true, false);
      w.clipSlots(0);
      w.value("MonitoringEnum", 1);
      w.value("KeepRecordMonitoringLatency", true);
      w.sampleSequencerTail();
      w.close("FreezeSequencer");
      w.close("DeviceChain");
      w.close("GroupTrack");
      }

//---------------------------------------------------------
//   automationEnvelopes
//---------------------------------------------------------

MixerTargets mixerTargets(Writer& w, const Track& t)
      {
      MixerTargets m;
      for (const MixerAutomation& a : t.mixerAutomation) {
            int& id = a.target == MixerAutomation::Target::VOLUME ? m.volume : (a.target == MixerAutomation::Target::PAN ? m.pan : m.speaker);
            if (!id)
                  id = w.id();
            }
      return m;
      }

void automationEnvelopes(Writer& w, const Track& t, const std::vector<int>& parameterTargets, const MixerTargets& mixer)
      {
      std::vector<std::pair<int, const ParameterAutomation*>> found;      // (target id, automation)
      if (t.hasPlugin) {
            for (const ParameterAutomation& a : t.automation) {
                  for (size_t i = 0; i < t.plugin.parameters.size() && i < parameterTargets.size(); ++i)
                        if (t.plugin.parameters[i].name == a.name && !a.points.empty()) {
                              found.push_back({ parameterTargets[i], &a });
                              break;
                              }
                  }
            }
      // the mixer's: one envelope a target (the first given)
      std::vector<std::pair<int, const MixerAutomation*>> mixed;
      for (const MixerAutomation& a : t.mixerAutomation) {
            const int id = a.target == MixerAutomation::Target::VOLUME ? mixer.volume
                           : (a.target == MixerAutomation::Target::PAN ? mixer.pan : mixer.speaker);
            if (id && (!a.events.empty() || a.initial >= 0)
                && std::none_of(mixed.begin(), mixed.end(), [id](const std::pair<int, const MixerAutomation*>& m) { return m.first == id; }))
                  mixed.push_back({ id, &a });
            }
      w.open("AutomationEnvelopes");
      if (found.empty() && mixed.empty())
            w.empty("Envelopes");
      else {
            w.open("Envelopes");
            int id = 0;
            for (const auto& f : found) {
                  w.open("AutomationEnvelope", "Id=\"" + QByteArray::number(id++) + "\"");
                  w.open("EnvelopeTarget");
                  w.value("PointeeId", f.first);
                  w.close("EnvelopeTarget");
                  automationEvents(w, f.second->points);
                  w.close("AutomationEnvelope");
                  }
            for (const auto& m : mixed) {
                  w.open("AutomationEnvelope", "Id=\"" + QByteArray::number(id++) + "\"");
                  w.open("EnvelopeTarget");
                  w.value("PointeeId", m.first);
                  w.close("EnvelopeTarget");
                  mixerEvents(w, *m.second);
                  w.close("AutomationEnvelope");
                  }
            w.close("Envelopes");
            }
      w.close("AutomationEnvelopes");
      }

//---------------------------------------------------------
//   controllerTarget
//    ControllerTargets.0 is the pitch bend, .1 channel pressure, .<n+2> CC n (Live 12.4.6 showed .1 as Channel
//    Pressure, .11 as CC9, .32 as CC30: LIVE.md › The plain set). Live's clip-envelope chooser has no CC0 and no CC32.
//---------------------------------------------------------

int controllerTarget(int controller)
      {
      if (controller == PITCH_BEND_ENVELOPE)
            return 0;
      if (controller >= 0 && controller < 128)
            return controller + 2;
      return -1;
      }

//---------------------------------------------------------
//   midiToRouting
//    as Live 12.4.6 saves a MIDI track's MIDI To another track: "MidiOut/Track.<id>/TrackIn", the track's name,
//    "Track In" (the receiving track plays it when its monitoring is In)
//---------------------------------------------------------

void midiToRouting(Writer& w, int trackId, const QString& trackName)
      {
      w.routing("MidiOutputRouting", QString("MidiOut/Track.%1/TrackIn").arg(trackId), trackName, "Track In");
      }

//---------------------------------------------------------
//   clipTimeable
//---------------------------------------------------------

static void midiClip(Writer& w, const Clip& c, int clipId, int color, int numerator, int denominator,
                     const std::vector<int>& controllerTargets)
      {
      const double length = std::max(0.0, c.end - c.start);
      w.open("MidiClip", "Id=\"" + QByteArray::number(clipId) + "\" Time=\"" + Writer::num(c.start).toUtf8() + "\"");
      w.value("LomId", 0);
      w.value("LomIdView", 0);
      w.number("CurrentStart", c.start);
      w.number("CurrentEnd", c.end);
      w.open("Loop");
      w.value("LoopStart", 0);
      w.number("LoopEnd", length);
      w.value("StartRelative", 0);
      w.value("LoopOn", false);
      w.number("OutMarker", length);
      w.value("HiddenLoopStart", 0);
      w.number("HiddenLoopEnd", length);
      w.close("Loop");
      w.value("Name", c.name);
      w.value("Annotation", "");
      w.value("Color", color);
      w.value("LaunchMode", 0);
      w.value("LaunchQuantisation", 0);
      w.open("TimeSignature");
      w.open("TimeSignatures");
      w.open("RemoteableTimeSignature", "Id=\"0\"");
      w.value("Numerator", numerator);
      w.value("Denominator", denominator);
      w.value("Time", 0);
      w.close("RemoteableTimeSignature");
      w.close("TimeSignatures");
      w.close("TimeSignature");
      w.open("Envelopes");
      std::vector<std::pair<int, const Clip::Envelope*>> envelopes;           // (pointee id, envelope)
      for (const Clip::Envelope& e : c.envelopes) {
            const int i = controllerTarget(e.controller);
            if (i >= 0 && i < int(controllerTargets.size()) && !e.points.empty())
                  envelopes.push_back({ controllerTargets[size_t(i)], &e });
            }
      if (envelopes.empty())
            w.empty("Envelopes");
      else {
            w.open("Envelopes");
            int id = 0;
            for (const auto& e : envelopes) {
                  w.open("ClipEnvelope", "Id=\"" + QByteArray::number(id++) + "\"");
                  w.open("EnvelopeTarget");
                  w.value("PointeeId", e.first);
                  w.close("EnvelopeTarget");
                  automationEvents(w, e.second->points);
                  w.emptyValue("LoopSlot");
                  scroller(w, 0);
                  w.close("ClipEnvelope");
                  }
            w.close("Envelopes");
            }
      w.close("Envelopes");
      scroller(w, length);
      w.open("TimeSelection");
      w.value("AnchorTime", 0);
      w.value("OtherTime", 0);
      w.close("TimeSelection");
      w.value("Legato", false);
      w.value("Ram", false);
      w.open("GrooveSettings");
      w.value("GrooveId", -1);
      w.close("GrooveSettings");
      w.value("Disabled", false);
      w.value("VelocityAmount", 0);
      w.open("FollowAction");
      w.value("FollowTime", 4);
      w.value("IsLinked", true);
      w.value("LoopIterations", 1);
      w.value("FollowActionA", 4);
      w.value("FollowActionB", 0);
      w.value("FollowChanceA", 100);
      w.value("FollowChanceB", 0);
      w.value("JumpIndexA", 1);
      w.value("JumpIndexB", 1);
      w.value("FollowActionEnabled", false);
      w.close("FollowAction");
      w.open("Grid");
      w.value("FixedNumerator", 1);
      w.value("FixedDenominator", 16);
      w.value("GridIntervalPixel", 20);
      w.value("Ntoles", 2);
      w.value("SnapToGrid", true);
      w.value("Fixed", true);
      w.close("Grid");
      w.value("FreezeStart", 0);
      w.value("FreezeEnd", 0);
      w.value("IsWarped", true);
      w.value("TakeId", 1);
      w.value("IsInKey", true);
      w.open("ScaleInformation");
      w.value("Root", 0);
      w.value("Name", 0);
      w.close("ScaleInformation");

      // the notes, a key track per pitch (low to high)
      std::map<int, std::vector<const Clip::Note*>> byKey;
      for (const Clip::Note& n : c.notes)
            byKey[n.pitch].push_back(&n);
      w.open("Notes");
      int noteId = 1;
      if (byKey.empty())
            w.empty("KeyTracks");
      else {
            w.open("KeyTracks");
            int keyId = 0;
            for (const auto& k : byKey) {
                  w.open("KeyTrack", "Id=\"" + QByteArray::number(keyId++) + "\"");
                  w.open("Notes");
                  for (const Clip::Note* n : k.second)
                        w.empty("MidiNoteEvent", "Time=\"" + Writer::num(n->start).toUtf8() + "\" Duration=\""
                                + Writer::num(n->length).toUtf8() + "\" Velocity=\"" + QByteArray::number(n->velocity)
                                + "\" OffVelocity=\"64\" NoteId=\"" + QByteArray::number(noteId++) + "\"");
                  w.close("Notes");
                  w.value("MidiKey", k.first);
                  w.close("KeyTrack");
                  }
            w.close("KeyTracks");
            }
      w.open("PerNoteEventStore");
      w.empty("EventLists");
      w.close("PerNoteEventStore");
      w.empty("NoteProbabilityGroups");
      w.open("ProbabilityGroupIdGenerator");
      w.value("NextId", 1);
      w.close("ProbabilityGroupIdGenerator");
      w.open("NoteIdGenerator");
      w.value("NextId", noteId);
      w.close("NoteIdGenerator");
      w.close("Notes");
      w.value("BankSelectCoarse", c.bank);
      w.value("BankSelectFine", c.subBank);
      w.value("ProgramChange", c.program);
      w.value("NoteEditorFoldInZoom", -1);
      w.value("NoteEditorFoldInScroll", 0);
      w.value("NoteEditorFoldOutZoom", 3072);
      w.value("NoteEditorFoldOutScroll", 0);
      w.value("NoteEditorFoldScaleZoom", -1);
      w.value("NoteEditorFoldScaleScroll", 0);
      w.value("NoteSpellingPreference", 0);
      w.value("AccidentalSpellingPreference", 3);
      w.value("PreferFlatRootNote", false);
      w.open("ExpressionGrid");
      w.value("FixedNumerator", 1);
      w.value("FixedDenominator", 16);
      w.value("GridIntervalPixel", 20);
      w.value("Ntoles", 2);
      w.value("SnapToGrid", false);
      w.value("Fixed", false);
      w.close("ExpressionGrid");
      w.close("MidiClip");
      }

void clipTimeable(Writer& w, const Track& t, int numerator, int denominator, const std::vector<int>& controllerTargets)
      {
      if (t.clips.empty()) {
            w.arrangerAutomation("ClipTimeable");
            return;
            }
      w.open("ClipTimeable");
      w.open("ArrangerAutomation");
      w.open("Events");
      int id = 0;
      for (const Clip& c : t.clips)
            midiClip(w, c, id++, t.color, numerator, denominator, controllerTargets);
      w.close("Events");
      w.open("AutomationTransformViewState");
      w.value("IsTransformPending", false);
      w.empty("TimeAndValueTransforms");
      w.close("AutomationTransformViewState");
      w.close("ArrangerAutomation");
      w.close("ClipTimeable");
      }

}     // namespace LiveSetWriter
}     // namespace Ms
