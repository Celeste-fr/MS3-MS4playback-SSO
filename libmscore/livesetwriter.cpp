//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "livesetwriter.h"

#include <algorithm>
#include <initializer_list>
#include <map>
#include <cmath>
#include <QFile>
#include <QObject>
#include <QSaveFile>
#include <QXmlStreamReader>
#include <set>
#include <zlib.h>

#include "liveclips.h"
#include "livesetxml.h"
#include "liveset.h"
#include "part.h"
#include "score.h"
#include "sig.h"
#include "soundlibrary.h"

namespace Ms {
namespace LiveSetWriter {

const char* const CREATOR       = "Ableton Live 12.2";
const char* const MINOR_VERSION = "12.0_12203";
static const char* const REVISION = "1c7a2c5dacd710ba28150f2c1534c22b1c158263";   // Live 12.2's build, as its sets carry it
static const int OVERWRITE_PROTECTION = 3074;     // what Live 12.2 writes (the set, a Max device, a VST 3 preset)
static const int SCENES = 8;                      // a new set's
static const int PLUGIN_PARAMETER_SLOTS = 128;    // Live's placeholders for a plug-in's configured parameters

namespace {

//---------------------------------------------------------
//   the devices
//---------------------------------------------------------

void linkDevice(Writer& w, const LinkDevice& link, int listId, const Track& t)
      {
      w.open("MxDeviceMidiEffect", "Id=\"" + QByteArray::number(listId) + "\"");
      w.deviceHeader(true, true);
      w.value("OverwriteProtectionNumber", OVERWRITE_PROTECTION);
      w.lom("AudioOutputsListWrapper");
      w.lom("AudioInputsListWrapper");
      w.lom("MidiOutputsListWrapper");
      w.lom("MidiInputsListWrapper");
      w.open("PatchSlot");
      w.open("Value");
      w.open("MxPatchRef", "Id=\"0\"");
      w.open("FileRef");
      // 6: the path is relative to Live's User Library (as Live wrote it for the device there); else
      // the absolute path alone
      const bool inLibrary = !link.userLibraryPath.isEmpty();
      w.value("RelativePathType", inLibrary ? 6 : 0);
      w.value("RelativePath", inLibrary ? link.userLibraryPath : QString());
      w.value("Path", link.path);
      w.value("Type", 1);
      w.value("LivePackName", "");
      w.value("LivePackId", "");
      w.value("OriginalFileSize", link.size);
      w.value("OriginalCrc", link.crc);
      w.close("FileRef");
      w.value("LastModDate", link.modified);
      w.empty("SourceContext");
      w.value("SampleUsageHint", 0);
      w.close("MxPatchRef");
      w.close("Value");
      w.close("PatchSlot");
      w.open("ParameterList");
      w.empty("ParameterList");
      w.close("ParameterList");
      w.open("FileDropList");
      w.empty("FileDropList");
      w.close("FileDropList");
      w.open("IdRefList");
      w.empty("IdRefList");
      w.close("IdRefList");
      w.open("BlobSlot");
      w.open("Value");
      w.open("MxDBlob", "Id=\"1\"");
      w.hex("Blob", linkBlob(link.port, &t));
      w.value("HasData", true);
      w.close("MxDBlob");
      w.close("Value");
      w.close("BlobSlot");
      w.open("Routables");
      w.empty("InRoutings");
      w.empty("OutRoutings");
      w.empty("MidiInRoutings");
      w.empty("MidiOutRoutings");
      w.close("Routables");
      w.value("MpeEnabled", false);
      w.value("MpeTuningEnabled", false);
      w.close("MxDeviceMidiEffect");
      }

void uid(Writer& w, const Plugin& p)
      {
      w.open("Uid");
      for (int i = 0; i < 4; ++i)
            w.value(QByteArray("Fields.").append(QByteArray::number(i)).constData(), int(qint32(p.uid[i])));
      w.close("Uid");
      }

void pluginDevice(Writer& w, const Plugin& p, int listId, const std::vector<int>& parameterTargets)
      {
      w.open("PluginDevice", "Id=\"" + QByteArray::number(listId) + "\"");
      w.deviceHeader(false, true);
      w.open("PluginDesc");
      w.open("Vst3PluginInfo", "Id=\"0\"");
      w.value("WinPosX", 100);
      w.value("WinPosY", 100);
      w.value("NumAudioInputs", 0);
      w.value("NumAudioOutputs", p.audioOutputs);
      w.value("IsPlaceholderDevice", false);
      w.open("Preset");
      w.open("Vst3Preset", "Id=\"0\"");
      w.value("OverwriteProtectionNumber", OVERWRITE_PROTECTION);
      w.value("MpeEnabled", 0);
      w.mpeSettings();
      w.empty("ParameterSettings");
      w.value("IsOn", true);
      w.value("PowerMacroControlIndex", -1);
      w.range("PowerMacroMappingRange", "64", "127");
      w.value("IsFolded", false);
      w.value("StoredAllParameters", true);
      w.value("DeviceLomId", 0);
      w.value("DeviceViewLomId", 0);
      w.value("IsOnLomId", 0);
      w.value("ParametersListWrapperLomId", 0);
      uid(w, p);
      w.value("DeviceType", 1);
      w.hex("ProcessorState", p.component);
      w.hex("ControllerState", p.controller);
      w.value("Name", "");
      w.empty("PresetRef");
      w.close("Vst3Preset");
      w.close("Preset");
      w.value("Name", p.name);
      uid(w, p);
      w.value("DeviceType", 1);
      w.close("Vst3PluginInfo");
      w.close("PluginDesc");
      w.value("MpeEnabled", false);
      w.mpeSettings();
      // Live's panel of the plug-in's parameters (Configure): the ones the score sets (Plugin::parameters), with
      // their values, as Live saves a configured parameter (the owner's set: Vibrato, Release), so whether Live
      // sets them again after the state or not, both agree; then Live's empty slots
      w.open("ParameterList");
      for (int i = 0; i < PLUGIN_PARAMETER_SLOTS; ++i) {
            const bool used = i < int(p.parameters.size());
            w.open("PluginFloatParameter", "Id=\"" + QByteArray::number(i) + "\"");
            w.value("ParameterName", used ? p.parameters[size_t(i)].name : QString());
            w.value("ParameterId", used ? int(p.parameters[size_t(i)].id) : -1);
            w.value("ParameterIdFlankBool", false);
            w.value("VisualIndex", used ? i : 1073741823);
            w.param("ParameterValue", used ? Writer::num(float(p.parameters[size_t(i)].value)) : QString("0.1234567687"), "0", "1",
                    true, used && size_t(i) < parameterTargets.size() ? parameterTargets[size_t(i)] : 0);
            w.open("LastUserRange");
            if (used) {
                  w.value("First", 0);
                  w.value("Last", 1);
                  }
            else {
                  w.value("First", "Invalid");
                  w.value("Last", "Invalid");
                  }
            w.close("LastUserRange");
            w.open("LastInternalRange");
            w.value("First", 0);
            w.value("Last", 1);
            w.close("LastInternalRange");
            w.close("PluginFloatParameter");
            }
      w.close("ParameterList");
      w.value("AxisX", 0);
      w.value("AxisY", 0);
      w.open("SideChain");
      w.onOff("OnOff");
      w.open("RoutedInput");
      w.routing("Routable", "AudioIn/None", "No Output", "");
      w.param("Volume", "1", "0.0003162277571", "15.8489332");
      w.close("RoutedInput");
      w.param("DryWet", "1", "0", "1");
      w.close("SideChain");
      w.close("PluginDevice");
      }

//---------------------------------------------------------
//   tracks
//---------------------------------------------------------

// a track's Id in the set: its index in Spec::tracks + 1 (a group's too: they share the numbering); -1: none
static int trackIdOf(int index)
      {
      return index >= 0 ? index + 1 : -1;
      }

void midiTrack(Writer& w, const Spec& spec, int index)
      {
      const Track& t = spec.tracks[size_t(index)];
      const LinkDevice& link = spec.link;
      w.open("MidiTrack", "Id=\"" + QByteArray::number(trackIdOf(index)) + "\" SelectedToolPanel=\"7\" SelectedTransformationName=\"\" SelectedGeneratorName=\"\"");
      w.trackHead(t.name, t.name, t.color);
      // the plug-in parameters' automation targets, known before the envelopes pointing at them
      std::vector<int> parameterTargets;
      if (t.hasPlugin)
            for (size_t i = 0; i < t.plugin.parameters.size(); ++i)
                  parameterTargets.push_back(w.id());
      automationEnvelopes(w, t, parameterTargets);
      w.trackLists(true, trackIdOf(t.groupIndex));
      w.value("SavedPlayingSlot", -1);
      w.value("SavedPlayingOffset", 0);
      w.value("Freeze", false);
      w.value("NeedArrangerRefreeze", true);
      w.value("PostProcessFreezeClips", 0);
      w.open("DeviceChain");
      w.automationLanes(68);
      w.routing("AudioInputRouting", "AudioIn/External/S0", "Ext. In", "1/2");
      // MIDI From. All Ins is Live's default ("MidiIn/External.All/-1", "Ext: All Ins"). One port and channel
      // as Live writes a MIDI output to one ("MidiOut/External.Dev:<port>/<channel index>", "<port>", "Ch. n",
      // in a Live 10 set): the input's form by analogy, unconfirmed
      if (!t.portName.isEmpty() && t.channel >= 1 && t.channel <= 16)
            w.routing("MidiInputRouting", QString("MidiIn/External.Dev:%1/%2").arg(t.portName).arg(t.channel - 1), t.portName,
                      QString("Ch. %1").arg(t.channel));
      else
            w.routing("MidiInputRouting", "MidiIn/External.All/-1", "Ext: All Ins", "");
      if (t.groupIndex >= 0)
            w.routing("AudioOutputRouting", "AudioOut/GroupTrack", "Group", "");
      else
            w.routing("AudioOutputRouting", "AudioOut/Main", "Master", "");
      if (t.midiTo >= 0 && t.midiTo < int(spec.tracks.size())) {
            const Track& to = spec.tracks[size_t(t.midiTo)];
            midiToRouting(w, trackIdOf(t.midiTo), to.name);
            }
      else
            w.routing("MidiOutputRouting", "MidiOut/None", "None", "");
      w.open("Mixer");
      w.mixerStart(t.volume, 93, t.pan, t.active);
      w.close("Mixer");

      w.open("MainSequencer");
      w.deviceHeader(true, false);
      w.clipSlots(SCENES);
      // In on a track another's MIDI To plays (Auto plays nothing unarmed); else Auto: the clips play (the device sets it too)
      const bool receives = std::any_of(spec.tracks.begin(), spec.tracks.end(), [index](const Track& o) { return o.midiTo == index; });
      w.value("MonitoringEnum", receives ? 0 : 1);
      w.value("KeepRecordMonitoringLatency", true);
      // the MIDI controllers' ids, known before the clip envelopes pointing at them
      std::vector<int> controllerTargets;
      for (int i = 0; i < CONTROLLER_TARGETS; ++i)
            controllerTargets.push_back(w.id());
      const bool signature = timeSignatureId(spec.numerator, spec.denominator) >= 0;   // else 4/4, as the song's
      clipTimeable(w, t, signature ? spec.numerator : 4, signature ? spec.denominator : 4, controllerTargets);
      w.recorder(0);
      w.open("MidiControllers");
      for (int i = 0; i < CONTROLLER_TARGETS; ++i) {
            const QByteArray tag = "ControllerTargets." + QByteArray::number(i);
            w.open(tag.constData(), "Id=\"" + QByteArray::number(controllerTargets[size_t(i)]) + "\"");
            w.value("LockEnvelope", (i == 1 || i == 11 || i == 66) ? 1 : 0);    // (as Live writes a new track's)
            w.close(tag.constData());
            }
      w.close("MidiControllers");
      w.close("MainSequencer");

      w.open("FreezeSequencer");
      w.deviceHeader(true, false);
      w.clipSlots(SCENES);
      w.value("MonitoringEnum", 1);
      w.value("KeepRecordMonitoringLatency", true);
      w.sampleSequencerTail();
      w.close("FreezeSequencer");

      w.open("DeviceChain");
      if (!t.link && !t.hasPlugin)
            w.empty("Devices");
      else {
            w.open("Devices");
            int n = 0;
            if (t.link && link.valid())
                  linkDevice(w, link, n++, t);
            if (t.hasPlugin)
                  pluginDevice(w, t.plugin, n++, parameterTargets);
            w.close("Devices");
            }
      w.empty("SignalModulations");
      w.close("DeviceChain");
      w.close("DeviceChain");

      w.value("ReWireDeviceMidiTargetId", 0);
      w.value("PitchbendRange", 96);
      w.value("IsTuned", true);
      w.value("ControllerLayoutRemoteable", 0);
      w.open("ControllerLayoutCustomization");
      w.value("PitchClassSource", 0);
      w.value("OctaveSource", 2);
      w.value("KeyNoteTarget", 60);
      w.value("StepSize", 1);
      w.value("OctaveEvery", 12);
      w.value("AllowedKeys", 0);
      w.value("FillerKeysMapTo", 0);
      w.close("ControllerLayoutCustomization");
      w.close("MidiTrack");
      }

void mainTrack(Writer& w, double tempo, int timeSignature)
      {
      w.open("MainTrack", "SelectedToolPanel=\"7\" SelectedTransformationName=\"\" SelectedGeneratorName=\"\"");
      w.trackHead("Main", "", 25);
      // the song's tempo and time signature: each an automation whose only event is at Live's "before
      // everything" time, pointing at the mixer's Tempo and TimeSignature (their ids taken now)
      const int tempoId = w.id();
      const int signatureId = w.id();
      const QString before = Writer::num(LiveSet::DEFAULT_EVENT_TIME);
      w.open("AutomationEnvelopes");
      w.open("Envelopes");
      w.open("AutomationEnvelope", "Id=\"0\"");
      w.open("EnvelopeTarget");
      w.value("PointeeId", signatureId);
      w.close("EnvelopeTarget");
      w.open("Automation");
      w.open("Events");
      w.empty("EnumEvent", "Id=\"0\" Time=\"" + before.toUtf8() + "\" Value=\"" + QByteArray::number(timeSignature) + "\"");
      w.close("Events");
      w.open("AutomationTransformViewState");
      w.value("IsTransformPending", false);
      w.empty("TimeAndValueTransforms");
      w.close("AutomationTransformViewState");
      w.close("Automation");
      w.close("AutomationEnvelope");
      w.open("AutomationEnvelope", "Id=\"1\"");
      w.open("EnvelopeTarget");
      w.value("PointeeId", tempoId);
      w.close("EnvelopeTarget");
      w.open("Automation");
      w.open("Events");
      w.empty("FloatEvent", "Id=\"0\" Time=\"" + before.toUtf8() + "\" Value=\"" + Writer::num(tempo).toUtf8() + "\"");
      w.close("Events");
      w.open("AutomationTransformViewState");
      w.value("IsTransformPending", false);
      w.empty("TimeAndValueTransforms");
      w.close("AutomationTransformViewState");
      w.close("Automation");
      w.close("AutomationEnvelope");
      w.close("Envelopes");
      w.close("AutomationEnvelopes");
      w.trackLists(false);
      w.open("DeviceChain");
      w.automationLanes(85);
      w.routing("AudioInputRouting", "AudioIn/External/S0", "Ext. In", "1/2");
      w.routing("MidiInputRouting", "MidiIn/External.All/-1", "Ext: All Ins", "");
      w.routing("AudioOutputRouting", "AudioOut/External/S0", "Ext. Out", "1/2");
      w.routing("MidiOutputRouting", "MidiOut/None", "None", "");
      w.open("Mixer");
      w.mixerStart(1, 103);
      w.param("Tempo", Writer::num(tempo), "60", "200", true, tempoId);
      w.paramRangeAfter("TimeSignature", QString::number(timeSignature), "0", "494", signatureId);
      w.param("GlobalGrooveAmount", "0", "0", "131.25");
      w.param("CrossFade", "0", "-1", "1");
      w.value("TempoAutomationViewBottom", 60);
      w.value("TempoAutomationViewTop", 200);
      w.close("Mixer");
      w.open("FreezeSequencer");
      w.open("AudioSequencer", "Id=\"0\"");
      w.deviceHeader(true, false);
      w.clipSlots(0);
      w.value("MonitoringEnum", 1);
      w.value("KeepRecordMonitoringLatency", true);
      w.sampleSequencerTail();
      w.close("AudioSequencer");
      w.close("FreezeSequencer");
      w.open("DeviceChain");
      w.empty("Devices");
      w.empty("SignalModulations");
      w.close("DeviceChain");
      w.close("DeviceChain");
      w.close("MainTrack");
      }

void preHearTrack(Writer& w)
      {
      w.open("PreHearTrack", "SelectedToolPanel=\"7\" SelectedTransformationName=\"\" SelectedGeneratorName=\"\"");
      w.trackHead("Master", "", -1);
      w.open("AutomationEnvelopes");
      w.empty("Envelopes");
      w.close("AutomationEnvelopes");
      w.trackLists(false);
      w.open("DeviceChain");
      w.automationLanes(85);
      w.routing("AudioInputRouting", "AudioIn/External/S0", "Ext. In", "1/2");
      w.routing("MidiInputRouting", "MidiIn/External.All/-1", "Ext: All Ins", "");
      w.routing("AudioOutputRouting", "AudioOut/External/S0", "Ext. Out", "1/2");
      w.routing("MidiOutputRouting", "MidiOut/None", "None", "");
      w.open("Mixer");
      w.mixerStart(0.5012149811, 74);
      w.close("Mixer");
      w.open("DeviceChain");
      w.empty("Devices");
      w.empty("SignalModulations");
      w.close("DeviceChain");
      w.close("DeviceChain");
      w.close("PreHearTrack");
      }

void followAction(Writer& w, int jump)
      {
      w.open("FollowAction");
      w.value("FollowTime", 4);
      w.value("IsLinked", true);
      w.value("LoopIterations", 1);
      w.value("FollowActionA", 4);
      w.value("FollowActionB", 0);
      w.value("FollowChanceA", 100);
      w.value("FollowChanceB", 0);
      w.value("JumpIndexA", jump);
      w.value("JumpIndexB", jump);
      w.value("FollowActionEnabled", false);
      w.close("FollowAction");
      }

void grid(Writer& w, const char* tag, bool snap, bool fixed)
      {
      w.open(tag);
      w.value("FixedNumerator", 1);
      w.value("FixedDenominator", 16);
      w.value("GridIntervalPixel", 20);
      w.value("Ntoles", 2);
      w.value("SnapToGrid", snap);
      w.value("Fixed", fixed);
      w.close(tag);
      }

void scaleInformation(Writer& w)
      {
      w.open("ScaleInformation");
      w.value("Root", 0);
      w.value("Name", 0);
      w.close("ScaleInformation");
      }

void laneModel(Writer& w, int id, int type, int size, bool minimized)
      {
      w.open("MidiEditorLaneModel", "Id=\"" + QByteArray::number(id) + "\"");
      w.value("Type", type);
      w.value("Size", size);
      w.value("IsMinimized", minimized);
      w.close("MidiEditorLaneModel");
      }

// a new set's groove pool: Live's "Swing 16ths 66", the default groove (a set without it would need a
// DefaultGrooveId for none, which Live's own sets never show)
void groovePool(Writer& w)
      {
      w.open("GroovePool");
      w.value("LomId", 0);
      w.open("Grooves");
      w.open("Groove", "Id=\"4\"");
      w.value("LomId", 0);
      w.value("Name", "Swing 16ths 66");
      w.open("Clip");
      w.open("Value");
      w.open("MidiClip", "Id=\"0\" Time=\"0\"");
      w.value("LomId", 0);
      w.value("LomIdView", 0);
      w.value("CurrentStart", 0);
      w.value("CurrentEnd", 4);
      w.open("Loop");
      w.value("LoopStart", 0);
      w.value("LoopEnd", 4);
      w.value("StartRelative", 0);
      w.value("LoopOn", true);
      w.value("OutMarker", 4);
      w.value("HiddenLoopStart", 0);
      w.value("HiddenLoopEnd", "0.5");
      w.close("Loop");
      w.value("Name", "Swing 16ths 66");
      w.value("Annotation", "");
      w.value("Color", 7);
      w.value("LaunchMode", 0);
      w.value("LaunchQuantisation", 0);
      w.open("TimeSignature");
      w.open("TimeSignatures");
      w.open("RemoteableTimeSignature", "Id=\"0\"");
      w.value("Numerator", 4);
      w.value("Denominator", 4);
      w.value("Time", 0);
      w.close("RemoteableTimeSignature");
      w.close("TimeSignatures");
      w.close("TimeSignature");
      w.open("Envelopes");
      w.empty("Envelopes");
      w.close("Envelopes");
      w.open("ScrollerTimePreserver");
      w.value("LeftTime", 0);
      w.value("RightTime", 4);
      w.close("ScrollerTimePreserver");
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
      followAction(w, 2);
      grid(w, "Grid", true, true);
      w.value("FreezeStart", 0);
      w.value("FreezeEnd", 0);
      w.value("IsWarped", true);
      w.value("TakeId", 0);
      w.value("IsInKey", true);
      scaleInformation(w);
      w.open("Notes");
      w.open("KeyTracks");
      w.open("KeyTrack", "Id=\"31\"");
      w.open("Notes");
      // Live's default groove: each beat's two eighths split in sixteenths, the off ones at 2/3 (66 % swing)
      int noteId = 1;
      for (int beat = 0; beat < 4; ++beat) {
            const double times[4] = { double(beat), beat + 1.0 / 3.0, beat + 0.5, beat + 5.0 / 6.0 };
            for (double t : times) {
                  // (Live's own ids: 1, 2, then 18 … 31)
                  const int id = noteId <= 2 ? noteId : noteId + 15;
                  w.empty("MidiNoteEvent", "Time=\"" + QByteArray::number(t, 'g', 17) + "\" Duration=\"0.0625\" Velocity=\"127\" "
                          "OffVelocity=\"64\" NoteId=\"" + QByteArray::number(id) + "\"");
                  ++noteId;
                  }
            }
      w.close("Notes");
      w.value("MidiKey", 36);
      w.close("KeyTrack");
      w.close("KeyTracks");
      w.open("PerNoteEventStore");
      w.empty("EventLists");
      w.close("PerNoteEventStore");
      w.empty("NoteProbabilityGroups");
      w.open("ProbabilityGroupIdGenerator");
      w.value("NextId", 1);
      w.close("ProbabilityGroupIdGenerator");
      w.open("NoteIdGenerator");
      w.value("NextId", 32);
      w.close("NoteIdGenerator");
      w.close("Notes");
      w.value("BankSelectCoarse", -1);
      w.value("BankSelectFine", -1);
      w.value("ProgramChange", -1);
      w.value("NoteEditorFoldInZoom", 24);
      w.value("NoteEditorFoldInScroll", 0);
      w.value("NoteEditorFoldOutZoom", 2409);
      w.value("NoteEditorFoldOutScroll", -1553);
      w.value("NoteEditorFoldScaleZoom", -1);
      w.value("NoteEditorFoldScaleScroll", 0);
      w.value("NoteSpellingPreference", 0);
      w.value("AccidentalSpellingPreference", 3);
      w.value("PreferFlatRootNote", false);
      grid(w, "ExpressionGrid", false, false);
      w.close("MidiClip");
      w.close("Value");
      w.close("Clip");
      w.value("Grid", 3);
      w.value("QuantizationAmount", 0);
      w.value("TimingAmount", 100);
      w.value("RandomAmount", 0);
      w.value("VelocityAmount", 0);
      w.value("Annotation", "");
      w.value("Selection", true);
      w.empty("SourceContext");
      w.close("Groove");
      w.close("Grooves");
      w.value("DefaultGrooveId", 4);
      w.lom("GroovesListWrapper");
      w.close("GroovePool");
      }

// Live 12's MIDI generators and transformations, at their defaults
void noteAlgorithms(Writer& w)
      {
      auto remoteables = [&w](int first, std::initializer_list<const char*> names) {
            w.open("NamedKeyMidiRemoteables");
            int id = first;
            for (const char* n : names)
                  w.empty("NamedRemoteableKeyMidi", "Id=\"" + QByteArray::number(id++) + "\" Name=\"" + QByteArray(n) + "\"");
            w.close("NamedKeyMidiRemoteables");
            };
      auto values = [&w](std::initializer_list<std::pair<const char*, const char*>> vs) {
            for (const auto& v : vs)
                  w.value(v.first, v.second);
            };
      w.open("NoteAlgorithms");
      w.open("ArpeggiateAlgorithm", "Id=\"0\"");
      remoteables(15, { "Rate", "Steps", "Distance", "Gate", "Style" });
      values({ { "Rate", "10" }, { "Steps", "2" }, { "Distance", "12" }, { "Gate", "1" }, { "Style", "0" } });
      w.close("ArpeggiateAlgorithm");
      w.open("SpanAlgorithm", "Id=\"1\"");
      remoteables(9, { "Mode", "LengthVariation", "LengthOffset" });
      values({ { "Mode", "1" }, { "ChordThreshold", "10" }, { "LengthVariation", "0" }, { "LengthOffset", "0" } });
      w.close("SpanAlgorithm");
      w.open("ConnectAlgorithm", "Id=\"2\"");
      remoteables(12, { "Tie", "Density", "Spread", "Rate" });
      values({ { "Tie", "0" }, { "Density", "1" }, { "Spread", "0" }, { "Rate", "0" } });
      w.close("ConnectAlgorithm");
      w.open("OrnamentAlgorithm", "Id=\"3\"");
      remoteables(27, { "FlamEnabled", "FlamPosition", "FlamVelocity", "GraceNotesEnabled", "GraceNotesChance",
                        "GraceNotesVelocity", "GraceNotesPosition", "GraceNotesAmount", "GraceNotesPitch" });
      values({ { "FlamEnabled", "true" }, { "FlamPosition", "-0.200000003" }, { "FlamVelocity", "0.5" },
               { "GraceNotesEnabled", "false" }, { "GraceNotesChance", "1" }, { "GraceNotesVelocity", "0.5" },
               { "GraceNotesPosition", "-0.200000003" }, { "GraceNotesAmount", "3" }, { "GraceNotesPitch", "1" } });
      w.close("OrnamentAlgorithm");
      w.open("RecombineAlgorithm", "Id=\"4\"");
      remoteables(15, { "Dimension", "Shuffle", "Mirror", "RotateAmount", "RotateOnGrid" });
      values({ { "PermutedDimension", "0" }, { "Shuffle", "false" }, { "Mirror", "false" }, { "RotateAmount", "0" },
               { "RotateOnGrid", "false" } });
      w.close("RecombineAlgorithm");
      w.open("QuantizeAlgorithm", "Id=\"5\"");
      remoteables(15, { "TripletReference", "QuantizeReference", "Amount", "QuantizeNoteStarts", "QuantizeNoteEnds" });
      values({ { "Reference", "0" }, { "TripletReference", "false" }, { "QuantizeNoteStarts", "true" },
               { "QuantizeNoteEnds", "false" }, { "Amount", "100" } });
      w.close("QuantizeAlgorithm");
      w.open("StrumAlgorithm", "Id=\"6\"");
      remoteables(12, { "Threshold", "Low", "High", "Tension" });
      values({ { "ChordThreshold", "0" }, { "StrumLow", "0" }, { "StrumHigh", "0" }, { "TensionAmount", "0" } });
      w.close("StrumAlgorithm");
      w.open("TimeWarpAlgorithm", "Id=\"7\"");
      remoteables(63, { "StretchNoteEnd", "PreserveTimeRange", "Quantize", "Value1", "Time1", "IsActive1", "Value2", "Time2",
                        "IsActive2", "Value3", "Time3", "IsActive3" });
      w.open("Breakpoints");
      for (int i = 0; i < 3; ++i) {
            w.open("Breakpoint", "Id=\"" + QByteArray::number(i) + "\"");
            w.value("Value", "0.5");
            w.value("Time", i == 0 ? "0" : i == 1 ? "0.5" : "1");
            w.value("Control1X", "0.5");
            w.value("Control1Y", "0.5");
            w.value("Control2X", "0.5");
            w.value("Control2Y", "0.5");
            w.value("IsActive", i != 1);
            w.close("Breakpoint");
            }
      w.close("Breakpoints");
      values({ { "StretchNoteEnd", "true" }, { "PreserveTimeRange", "true" }, { "Quantize", "false" } });
      w.close("TimeWarpAlgorithm");
      w.open("RhythmAlgorithm", "Id=\"8\"");
      remoteables(42, { "Density", "Repetitions", "Pattern", "Steps", "Pitch", "Velocity", "Accent", "Period", "Offset", "Shift",
                        "StepDuration", "Split", "VelocityOffsetIncrement", "VelocityOffsetDecrement" });
      values({ { "Density", "4" }, { "Repetitions", "1" }, { "Pattern", "14" }, { "PatternLength", "8" }, { "Pitch", "36" },
               { "Velocity", "100" }, { "Accent", "127" }, { "Period", "4" }, { "Offset", "0" }, { "Shift", "0" },
               { "StepDuration", "7" }, { "Split", "0" } });
      w.close("RhythmAlgorithm");
      w.open("StacksAlgorithm", "Id=\"9\"");
      remoteables(36, { "AppendChord", "DeleteSelectedChord", "RootPitch1", "Inversion1", "Rule1", "Duration1", "Offset1" });
      w.open("Sequence");
      w.open("Chord", "Id=\"8\"");
      values({ { "RootDegree", "0" }, { "Octave", "3" }, { "RootPitch", "60" }, { "Inversion", "0" }, { "RuleNumber", "0" },
               { "Duration", "1" }, { "Offset", "0" } });
      w.close("Chord");
      w.close("Sequence");
      w.close("StacksAlgorithm");
      w.open("ShapeAlgorithm", "Id=\"10\"");
      remoteables(21, { "ShapePresets", "Rate", "Tie", "Density", "MinPitch", "MaxPitch", "PitchVariation" });
      w.open("ShapeLevels");
      static const char* const levels[21] = { "0", "0.05000000075", "0.1000000015", "0.150000006", "0.200000003", "0.25",
                                              "0.3000000119", "0.349999994", "0.400000006", "0.4499999881", "0.5",
                                              "0.5500000119", "0.6000000238", "0.6499999762", "0.6999999881", "0.75",
                                              "0.8000000119", "0.8500000238", "0.8999999762", "0.9499999881", "1" };
      for (int i = 0; i < 21; ++i)
            w.empty("RemoteableFloat", "Id=\"" + QByteArray::number(i) + "\" Value=\"" + QByteArray(levels[i]) + "\"");
      w.close("ShapeLevels");
      values({ { "ShapePresets", "2" }, { "Rate", "0" }, { "Tie", "0" }, { "Density", "1" }, { "MinPitch", "60" },
               { "MaxPitch", "84" }, { "PitchVariation", "0" } });
      w.close("ShapeAlgorithm");
      w.open("SeedAlgorithm", "Id=\"11\"");
      remoteables(24, { "Density", "MinPitch", "MaxPitch", "MinDuration", "MaxDuration", "MinVelocity", "MaxVelocity",
                        "VerticalLimit" });
      values({ { "NotesDensity", "0.5" }, { "MinPitch", "60" }, { "MaxPitch", "84" }, { "MinDuration", "-6" },
               { "MaxDuration", "-3" }, { "MinVelocity", "30" }, { "MaxVelocity", "100" }, { "VerticalLimit", "4" } });
      w.close("SeedAlgorithm");
      w.close("NoteAlgorithms");
      }

}     // namespace

//---------------------------------------------------------
//   xml
//---------------------------------------------------------

QByteArray xml(const Spec& spec, int* nextPointeeId)
      {
      Writer w;
      const double tempo = std::max(20.0, std::min(999.0, spec.tempo > 0 ? spec.tempo : 120.0));
      int ts = timeSignatureId(spec.numerator, spec.denominator);
      if (ts < 0)
            ts = timeSignatureId(4, 4);
      w.raw("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
      w.open("Ableton", QByteArray("MajorVersion=\"5\" MinorVersion=\"") + MINOR_VERSION + "\" SchemaChangeCount=\"3\" Creator=\""
             + CREATOR + "\" Revision=\"" + REVISION + "\"");
      w.open("LiveSet");
      const int nextAt = w.data().size();       // NextPointeeId goes here once known
      w.value("OverwriteProtectionNumber", OVERWRITE_PROTECTION);
      w.value("LomId", 0);
      w.value("LomIdView", 0);
      if (spec.tracks.empty())
            w.empty("Tracks");
      else {
            w.open("Tracks");
            for (int i = 0; i < int(spec.tracks.size()); ++i) {
                  const Track& t = spec.tracks[size_t(i)];
                  if (t.group)
                        groupTrack(w, t, trackIdOf(i), trackIdOf(t.groupIndex), SCENES);
                  else
                        midiTrack(w, spec, i);
                  }
            w.close("Tracks");
            }
      mainTrack(w, tempo, ts);
      preHearTrack(w);
      w.empty("SendsPre");
      w.open("Scenes");
      for (int i = 0; i < SCENES; ++i) {
            w.open("Scene", "Id=\"" + QByteArray::number(i) + "\"");
            followAction(w, 0);
            w.value("Name", "");
            w.value("Annotation", "");
            w.value("Color", -1);
            w.value("Tempo", Writer::num(tempo));
            w.value("IsTempoEnabled", false);
            w.value("TimeSignatureId", ts);
            w.value("IsTimeSignatureEnabled", false);
            w.value("LomId", 0);
            w.lom("ClipSlotsListWrapper");
            w.close("Scene");
            }
      w.close("Scenes");
      w.open("Transport");
      w.value("PhaseNudgeTempo", 10);
      w.value("LoopOn", false);                 // (LIVE.md: the loop stays off)
      w.value("LoopStart", 0);
      w.value("LoopLength", 16);
      w.value("LoopIsSongStart", false);
      w.value("CurrentTime", 0);
      w.value("PunchIn", false);
      w.value("PunchOut", false);
      w.value("MetronomeTickDuration", 0);
      w.value("DrawMode", false);
      w.close("Transport");
      w.empty("SessionScrollPos", "X=\"0\" Y=\"0\"");
      w.value("SelectedBreakpointValue", 0);
      w.empty("SignalModulations");
      w.value("GlobalQuantisation", 4);
      w.value("AutoQuantisation", 0);
      grid(w, "Grid", true, false);
      scaleInformation(w);
      w.value("InKey", true);
      w.value("SmpteFormat", 0);
      w.open("TimeSelection");
      w.value("AnchorTime", 0);
      w.value("OtherTime", 0);
      w.close("TimeSelection");
      w.open("SequencerNavigator");
      w.open("BeatTimeHelper");
      w.value("CurrentZoom", "0.254945054945054927");
      w.close("BeatTimeHelper");
      w.empty("ScrollerPos", "X=\"0\" Y=\"0\"");
      w.empty("ClientSize", "X=\"1319\" Y=\"628\"");
      w.close("SequencerNavigator");
      w.value("IsContentSplitterOpen", true);
      w.value("IsExpressionSplitterOpen", true);
      w.open("ExpressionLanes");
      laneModel(w, 0, 5, 41, true);
      laneModel(w, 1, 0, 41, false);
      laneModel(w, 2, 1, 41, false);
      laneModel(w, 3, 2, 41, true);
      laneModel(w, 4, 3, 41, true);
      w.close("ExpressionLanes");
      w.open("ContentLanes");
      laneModel(w, 0, 2, 41, false);
      laneModel(w, 2, 3, 25, true);
      laneModel(w, 1, 4, 25, true);
      w.close("ContentLanes");
      w.value("ViewStateFxSlotCount", 4);
      w.value("ViewStateSessionMixerVolumeSectionHeight", 120);
      w.value("ViewStateArrangerMixerVolumeSectionHeight", 120);
      w.value("ShouldSceneTempoAndTimeSignatureBeVisible", false);
      w.value("WaveformVerticalZoomFactor", 1);
      w.value("IsWaveformVerticalZoomActive", true);
      w.open("Locators");
      w.empty("Locators");
      w.close("Locators");
      w.empty("DetailClipKeyMidis");
      w.lom("TracksListWrapper");
      w.lom("VisibleTracksListWrapper");
      w.lom("ReturnTracksListWrapper");
      w.lom("ScenesListWrapper");
      w.lom("CuePointsListWrapper");
      w.value("SelectedDocumentViewInMainWindow", 0);
      w.value("Annotation", "");
      w.value("SoloOrPflSavedValue", true);
      w.value("SoloInPlace", true);
      w.value("CrossfadeCurve", 2);
      w.value("LatencyCompensation", 2);
      w.value("HighlightedTrackIndex", 0);
      groovePool(w);
      w.value("AutomationMode", false);
      w.value("SnapAutomationToGrid", true);
      w.value("ArrangementOverdub", false);
      w.value("ColorSequenceIndex", 360507111);
      w.open("AutoColorPickerForPlayerAndGroupTracks");
      w.value("NextColorIndex", 4);
      w.close("AutoColorPickerForPlayerAndGroupTracks");
      w.open("AutoColorPickerForReturnAndMainTracks");
      w.value("NextColorIndex", 13);
      w.close("AutoColorPickerForReturnAndMainTracks");
      w.value("ViewData", "{}");
      w.value("ResetNonautomatedMidiControllersOnClipStarts", true);
      w.value("MidiFoldIn", false);
      w.value("MidiFoldMode", -99);
      w.value("MultiClipFocusMode", false);
      w.value("MultiClipLoopBarHeight", 0);
      w.value("MidiPrelisten", false);
      w.empty("LinkedTrackGroups");
      w.value("NoteSpellingPreference", 0);
      w.value("AccidentalSpellingPreference", 3);
      w.value("PreferFlatRootNote", false);
      w.value("UseWarperLegacyHiQMode", false);
      w.empty("VideoWindowRect", "Top=\"-2147483648\" Left=\"-2147483648\" Bottom=\"-2147483648\" Right=\"-2147483648\"");
      w.value("ShowVideoWindow", true);
      w.empty("TuningSystems");
      w.value("TrackHeaderWidth", 93);
      w.value("ViewStateMainWindowClipDetailOpen", false);
      w.value("ViewStateMainWindowHiddenOtherDocViewTypeClipDetailOpen", false);
      w.value("ViewStateMainWindowHiddenOtherDocViewTypeDeviceDetailOpen", true);
      w.value("ViewStateMainWindowDeviceDetailOpen", true);
      w.value("ViewStateSecondWindowClipDetailOpen", true);
      w.value("ViewStateSecondWindowDeviceDetailOpen", false);
      w.open("ViewStates");
      static const std::pair<const char*, int> views[] = {
            { "MixerInArrangement", 0 }, { "ArrangerMixerIO", 1 }, { "ArrangerMixerSends", 1 }, { "ArrangerMixerReturns", 1 },
            { "ArrangerMixerVolume", 1 }, { "ArrangerMixerTrackOptions", 0 }, { "ArrangerMixerCrossFade", 0 },
            { "ArrangerMixerTrackPerformanceImpactMeter", 0 }, { "MixerInSession", 1 }, { "SessionIO", 1 },
            { "SessionSends", 1 }, { "SessionReturns", 1 }, { "SessionVolume", 1 }, { "SessionTrackOptions", 0 },
            { "SessionCrossFade", 0 }, { "SessionTrackPerformanceImpactMeter", 0 }, { "SessionShowOverView", 0 },
            { "ArrangerIO", 1 }, { "ArrangerReturns", 1 }, { "ArrangerVolume", 1 }, { "ArrangerTrackOptions", 0 },
            { "ArrangerShowOverView", 1 } };
      for (const auto& v : views)
            w.value(v.first, v.second);
      w.close("ViewStates");
      noteAlgorithms(w);
      w.close("LiveSet");
      w.close("Ableton");

      // NextPointeeId: above every pointee id written, the first element of the set (as Live writes it)
      const int next = w.nextId();
      if (nextPointeeId)
            *nextPointeeId = next;
      w.data().insert(nextAt, QByteArray("\t\t<NextPointeeId Value=\"") + QByteArray::number(next) + "\" />\r\n");
      return w.data();
      }

//---------------------------------------------------------
//   validate
//---------------------------------------------------------

static bool isPointee(const QStringRef& tag)
      {
      return tag == "Pointee" || tag == "AutomationTarget" || tag.endsWith(QLatin1String("ModulationTarget"))
             || tag.startsWith(QLatin1String("ControllerTargets."));
      }

QString validate(const QByteArray& data)
      {
      QXmlStreamReader r(data);
      int next = -1;
      std::set<int> ids;
      int scenes = -1;
      std::vector<int> slotLists;         // clip slots in each MainSequencer / FreezeSequencer of a MidiTrack, a GroupTrack's slots
      std::set<int> groups;               // the group tracks' ids so far
      std::vector<QString> path;
      std::vector<std::set<QString>> deviceIds;
      int slotCount = 0;
      while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement()) {
                  const QStringRef tag = r.name();
                  const QXmlStreamAttributes a = r.attributes();
                  if (tag == "NextPointeeId")
                        next = a.value("Value").toInt();
                  if (isPointee(tag) && a.hasAttribute("Id")) {
                        const int id = a.value("Id").toInt();
                        if (!ids.insert(id).second)
                              return QString("pointee id %1 twice (%2)").arg(id).arg(tag.toString());
                        }
                  if (tag == "Scenes")
                        scenes = 0;
                  if (tag == "Scene" && !path.empty() && path.back() == "Scenes")
                        ++scenes;
                  const QString parent = path.empty() ? QString() : path.back();
                  if (tag == "ClipSlotList" && path.size() >= 2 && (parent == "MainSequencer" || parent == "FreezeSequencer")
                      && std::find(path.begin(), path.end(), "MidiTrack") != path.end())
                        slotCount = 0;
                  if (tag == "ClipSlot" && parent == "ClipSlotList")
                        ++slotCount;
                  if (tag == "Slots" && parent == "GroupTrack")
                        slotCount = 0;
                  if (tag == "GroupTrackSlot" && parent == "Slots")
                        ++slotCount;
                  if (tag == "GroupTrack" && parent == "Tracks")
                        groups.insert(a.value("Id").toInt());
                  if (tag == "TrackGroupId" && (parent == "MidiTrack" || parent == "GroupTrack")) {
                        const int g = a.value("Value").toInt();
                        if (g != -1 && !groups.count(g))
                              return QString("a track in group %1 before the group").arg(g);
                        }
                  if (tag == "Devices")
                        deviceIds.emplace_back();
                  if (parent == "Devices" && !deviceIds.empty() && !deviceIds.back().insert(a.value("Id").toString()).second)
                        return QString("two devices with id %1 on a track").arg(a.value("Id").toString());
                  path.push_back(tag.toString());
                  }
            else if (r.isEndElement()) {
                  if (r.name() == "ClipSlotList" && path.size() >= 2 && std::find(path.begin(), path.end(), "MidiTrack") != path.end()) {
                        const QString owner = path[path.size() - 2];
                        if (owner == "MainSequencer" || owner == "FreezeSequencer")
                              slotLists.push_back(slotCount);
                        }
                  if (r.name() == "Slots" && path.size() >= 2 && path[path.size() - 2] == "GroupTrack")
                        slotLists.push_back(slotCount);
                  if (r.name() == "Devices")
                        deviceIds.pop_back();
                  path.pop_back();
                  }
            }
      if (r.hasError())
            return QString("not well formed: %1 (line %2)").arg(r.errorString()).arg(r.lineNumber());
      if (next < 0)
            return "no NextPointeeId";
      if (!ids.empty() && *ids.rbegin() >= next)
            return QString("pointee id %1 not below NextPointeeId %2").arg(*ids.rbegin()).arg(next);
      for (int n : slotLists)
            if (n != scenes)
                  return QString("a track has %1 clip slots for %2 scenes").arg(n).arg(scenes);
      return QString();
      }

bool write(const QString& path, const Spec& spec, QString* error)
      {
      const QByteArray data = xml(spec);
      const QString problem = validate(data);
      if (!problem.isEmpty()) {
            if (error)
                  *error = QObject::tr("The set came out wrong (%1); nothing was written.").arg(problem);
            return false;
            }
      const QByteArray gz = gzip(data);
      if (gz.isEmpty()) {
            if (error)
                  *error = QObject::tr("The set could not be compressed.");
            return false;
            }
      QSaveFile f(path);
      if (!f.open(QIODevice::WriteOnly) || f.write(gz) != gz.size() || !f.commit()) {
            if (error)
                  *error = QObject::tr("Cannot write %1: %2").arg(path, f.errorString());
            return false;
            }
      return true;
      }

//---------------------------------------------------------
//   tracks
//---------------------------------------------------------

std::vector<Track> tracks(const Score* score, const SoundLib::Library& library, const QStringList& portNames)
      {
      std::vector<Track> out;
      if (!score)
            return out;
      std::map<const Part*, int> colorOf;
      for (const SoundLib::Route& r : SoundLib::routes(score, library)) {
            Track t;
            t.part = r.part ? r.part->partName() : QString();
            t.instrument = r.instrument;
            t.patch = r.instrument ? r.instrument->name : QString();
            t.mainPatch = r.patch == 0 && r.lane == 0;
            t.name = trackName(t.part, t.patch, r.patch == 0, r.lane);
            t.channel = r.channel + 1;
            t.routeKey = QString("%1:%2").arg(r.port).arg(t.channel);
            t.portName = r.port < portNames.size() ? portNames[r.port] : QString();
            if (!colorOf.count(r.part))
                  colorOf[r.part] = partColor(int(colorOf.size()));
            t.color = colorOf[r.part];
            t.partRef = r.part;
            t.port = r.port;
            t.routePatch = r.patch;
            t.lane = r.lane;
            // the Mixer as MuseScore's host plays the part (mute, not solo: as MuseScore's export)
            const SoundLib::PartMix m = SoundLib::partMix(r.part, false);
            t.volume = mixGain(m.volume);
            t.pan = mixPan(m.pan);
            t.active = !m.muted;
            out.push_back(t);
            }
      return out;
      }

int partColor(int n)
      {
      // Live's colour chooser, a colour per part, every few steps apart (0-69)
      static const int colors[] = { 0, 3, 5, 9, 12, 15, 17, 20, 24, 26, 30, 33, 36, 39, 42, 45, 48, 52, 56, 60 };
      return colors[std::max(0, n) % int(sizeof(colors) / sizeof(colors[0]))];
      }

void setSong(const Score* score, Spec* spec)
      {
      if (!score || !spec)
            return;
      spec->tempo = LiveClips::timeline(score).bpm;
      const TimeSigFrac ts = score->sigmap()->timesig(0).timesig();
      if (timeSignatureId(ts.numerator(), ts.denominator()) >= 0) {
            spec->numerator = ts.numerator();
            spec->denominator = ts.denominator();
            }
      }

//---------------------------------------------------------
//   helpers
//---------------------------------------------------------

double mixGain(int volume)
      {
      const double v = std::max(0, std::min(127, volume)) / 100.0;
      return std::max(v * v, 0.0003162277571);
      }

double mixPan(int pan)
      {
      pan = std::max(0, std::min(127, pan));
      return pan < 64 ? (pan - 64) / 64.0 : (pan - 64) / 63.0;
      }

int timeSignatureId(int numerator, int denominator)
      {
      if (numerator < 1 || numerator > 99)
            return -1;
      int log = -1;
      for (int i = 0, d = 1; i <= 4; ++i, d *= 2)
            if (d == denominator)
                  log = i;
      return log < 0 ? -1 : (numerator - 1) + 99 * log;
      }

quint16 fileCrc(const QByteArray& data)
      {
      quint16 crc = 0;
      const int n = std::min(data.size(), 16384);
      for (int i = 0; i < n; ++i) {
            crc ^= quint16(uchar(data[i])) << 8;
            for (int b = 0; b < 8; ++b)
                  crc = (crc & 0x8000) ? quint16((crc << 1) ^ 0x8005) : quint16(crc << 1);
            }
      return crc;
      }

static QByteArray jsonString(const QString& s)
      {
      QByteArray out = "\"";
      for (const QChar c : s) {
            if (c == '"' || c == '\\')
                  out += '\\';
            if (c.unicode() < 0x20)
                  out += QString("\\u%1").arg(int(c.unicode()), 4, 16, QChar('0')).toUtf8();
            else
                  out += QString(c).toUtf8();
            }
      return out + "\"";
      }

static QByteArray jsonNumber(double v)
      {
      if (v == std::floor(v) && std::fabs(v) < 1e15)
            return QByteArray::number(qint64(v));
      return QByteArray::number(v, 'g', 9);
      }

// a lane's events packed as MuseScoreLink.js packLane does (the same rule, the same atoms): an event "time value",
// or a run of m >= 3 evenly spaced steps "-m t1 v1 tm vm vh" on the parabola through v1, vh (step floor((m-1)/2)), vm
// (PACK_DV: one MIDI step of the parameter's range, Automation::CC_RESOLUTION, the owner's criterion for Live's copy of
// MuseScore's curves (2026-10-03); PACK_DT: one tick in clip units, LiveClips::UNITS_PER_BEAT / 480: the grid a lane's
// events sit on)
static constexpr double PACK_DV = 1.0 / 127;
static constexpr double PACK_DT = 8;
static constexpr int PACK_MAX = 4096;

static double runValue(int k, int m, double v1, double vh, double vm)
      {
      const double h = (m - 1) / 2, e = m - 1;
      if (h == 0)
            return v1 + k * (vm - v1) / e;
      return v1 * (k - h) * (k - e) / (h * e) - vh * k * (k - e) / (h * (e - h)) + vm * k * (k - h) / (e * (e - h));
      }

std::vector<double> packLane(const std::vector<std::pair<int, float>>& ev)
      {
      // (the values as the device has them: the float the OSC packet carries, as a double)
      auto fits = [&ev](size_t i, size_t j) {
            const int m = int(j - i + 1);
            const double t1 = ev[i].first, tm = ev[j].first;
            if (!(tm > t1))
                  return false;
            const double v1 = ev[i].second, vm = ev[j].second, vh = ev[i + size_t((m - 1) / 2)].second;
            // (the staircase of events i … j at time t: the last event at or before it, as MuseScoreLink.js stairAt)
            auto stairAt = [&ev, i, j](double t) {
                  size_t lo = i, hi = j;
                  while (lo < hi) {
                        const size_t h = (lo + hi + 1) / 2;
                        if (ev[h].first <= t)
                              lo = h;
                        else
                              hi = h - 1;
                        }
                  return double(ev[lo].second);
                  };
            for (int k = 1; k < m - 1; ++k) {
                  const double t = std::round(t1 + k * (tm - t1) / (m - 1)), o = ev[i + size_t(k)].second;
                  if (std::fabs(runValue(k, m, v1, vh, vm) - o) > PACK_DV / 2)
                        return false;
                  if (std::fabs(t - ev[i + size_t(k)].first) > PACK_DT && std::fabs(stairAt(t) - o) > PACK_DV / 2)
                        return false;
                  }
            return true;
            };
      // the longest run from step i (as MuseScoreLink.js lastFit: doubling while it fits, then halving)
      auto lastFit = [&fits](size_t i, size_t n) -> long {
            if (n == 0)
                  return -1;
            const size_t limit = std::min(n - 1, i + size_t(PACK_MAX) - 1);
            if (i + 2 > limit || !fits(i, i + 2))
                  return -1;
            size_t good = i + 2, bad = limit + 1, step = 1;
            while (good < limit) {
                  const size_t c = std::min(limit, good + step);
                  if (fits(i, c)) {
                        good = c;
                        step *= 2;
                        }
                  else {
                        bad = c;
                        break;
                        }
                  }
            while (bad - good > 1) {
                  const size_t h = (good + bad) / 2;
                  if (fits(i, h))
                        good = h;
                  else
                        bad = h;
                  }
            return long(good);
            };
      std::vector<double> out;
      const size_t n = ev.size();
      for (size_t i = 0; i < n;) {
            const long best = lastFit(i, n);
            if (best >= 0) {
                  const size_t b = size_t(best), m = b - i + 1;
                  out.insert(out.end(), { -double(m), double(ev[i].first), double(ev[i].second), double(ev[b].first),
                                          double(ev[b].second), double(ev[i + (m - 1) / 2].second) });
                  i = b + 1;
                  }
            else {
                  out.insert(out.end(), { double(ev[i].first), double(ev[i].second) });
                  ++i;
                  }
            }
      return out;
      }

QByteArray linkBlob(int port, const Track* track, bool* lanesKept)
      {
      QByteArray out = QByteArray("{\r\n\t\"Port\" : [ ") + QByteArray::number(port) + " ]";
      if (lanesKept)
            *lanesKept = true;
      if (track && !track->linkLanes.empty()) {
            // the value as MuseScoreLink.js encodeSaved, without its "msl-lanes 1": <length> <routes> then the route
            std::vector<QByteArray> data { jsonNumber(track->linkLength), "1", jsonString(track->routeKey),
                                           QByteArray::number(qint32(track->linkHash)),
                                           QByteArray::number(int(track->linkLanes.size())) };
            for (const Track::LinkLane& l : track->linkLanes) {
                  data.push_back(jsonString(l.title));
                  data.push_back(QByteArray::number(qint64(l.id)));
                  const std::vector<double> packed = packLane(l.events);
                  data.push_back(QByteArray::number(int(packed.size())));
                  for (double x : packed)
                        data.push_back(jsonNumber(x));
                  }
            const size_t per = LINK_STORE_ATOMS - 5;
            const size_t parts = std::max<size_t>(1, (data.size() + per - 1) / per);
            if (parts > size_t(LINK_STORES)) {
                  if (lanesKept)
                        *lanesKept = false;
                  }
            else {
                  const QByteArray stamp = QByteArray::number(qHash(track->routeKey) % 1000000000u);
                  for (size_t k = 0; k < parts; ++k) {
                        out += ",\r\n\t\"" + (k ? QByteArray("Lanes") + QByteArray::number(int(k + 1)) : QByteArray("Lanes")) + "\" : [ \"msl-lanes\", 2, "
                               + stamp + ", " + QByteArray::number(int(k)) + ", " + QByteArray::number(int(parts));
                        for (size_t i = k * per; i < std::min(data.size(), (k + 1) * per); ++i)
                              out += ", " + data[i];
                        out += " ]";
                        }
                  }
            }
      return out + "\r\n}\r\n" + QByteArray(1, '\0');
      }

QString trackName(const QString& part, const QString& patch, bool mainPatch, int lane)
      {
      QString n = part;
      if (!mainPatch && !patch.isEmpty())
            n += QString(" – %1").arg(patch);
      if (lane > 0)
            n += QString(" (%1)").arg(lane + 1);
      return n;
      }

QString loose(const QString& s)
      {
      QString r;
      for (const QChar c : s.toLower())
            if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
                  r += c;
      return r;
      }

bool hasTrack(const LiveSet::Set& set, const Track& t)
      {
      // by MIDI From (the device: loosePort; the reader has taken off "Ext:")
      if (!t.portName.isEmpty())
            for (const LiveSet::Track& lt : set.tracks)
                  if (lt.inputChannel == t.channel && !lt.inputDevice.isEmpty()) {
                        const QString a = loose(lt.inputDevice), b = loose(t.portName);
                        if (!b.isEmpty() && (a == b || a.startsWith(b)))
                              return true;
                        }
      // by name: the part's (main patch), else "<part> – <patch>", else the patch's alone on one track only
      const QString want = loose(t.mainPatch ? t.part : t.name);
      for (const LiveSet::Track& lt : set.tracks)
            if (!want.isEmpty() && loose(lt.name) == want)
                  return true;
      if (!t.mainPatch && !t.patch.isEmpty()) {
            int hits = 0;
            for (const LiveSet::Track& lt : set.tracks)
                  if (loose(lt.name) == loose(t.patch))
                        ++hits;
            return hits == 1;
            }
      return false;
      }

QByteArray gzip(const QByteArray& data)
      {
      z_stream zs {};
      if (deflateInit2(&zs, 6, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return QByteArray();
      QByteArray out;
      out.resize(int(deflateBound(&zs, uLong(data.size()))) + 64);
      zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData()));
      zs.avail_in = uInt(data.size());
      zs.next_out = reinterpret_cast<Bytef*>(out.data());
      zs.avail_out = uInt(out.size());
      const int r = deflate(&zs, Z_FINISH);
      const int size = int(zs.total_out);
      deflateEnd(&zs);
      if (r != Z_STREAM_END)
            return QByteArray();
      out.resize(size);
      return out;
      }
}
}
