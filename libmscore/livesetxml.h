//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LIVESETXML_H__
#define __LIVESETXML_H__

//---------------------------------------------------------
//   The XML writer of LiveSetWriter (livesetwriter.cpp: the set, its tracks and devices; livesetclips.cpp: group
//   tracks, arrangement clips, automation), with the pieces Live repeats everywhere. Only for those two files.
//---------------------------------------------------------

#include <cmath>
#include <vector>

#include <QByteArray>
#include <QString>

#include "livesetwriter.h"

namespace Ms {
namespace LiveSetWriter {

//---------------------------------------------------------
//   Writer
//    Live's layout: tabs, CRLF, "<Tag Value="…" />", empty elements "<Tag />"
//---------------------------------------------------------

class Writer {
      QByteArray _b;
      int _depth { 0 };
      int _nextId { 1 };            // the pointee ids

      void indent() { _b.append(QByteArray(_depth, '\t')); }

   public:
      Writer() { _b.reserve(1 << 20); }
      QByteArray& data() { return _b; }
      int nextId() const { return _nextId; }
      int id() { return _nextId++; }

      static QByteArray esc(const QString& s)
            {
            QString r;
            r.reserve(s.size());
            for (const QChar c : s) {
                  switch (c.unicode()) {
                        case '&': r += "&amp;"; break;
                        case '<': r += "&lt;"; break;
                        case '>': r += "&gt;"; break;
                        case '"': r += "&quot;"; break;
                        case '\'': r += "&apos;"; break;
                        default:
                              if (c.unicode() < 0x20 && c != '\t')
                                    r += QString("&#x%1;").arg(int(c.unicode()), 0, 16);
                              else
                                    r += c;
                        }
                  }
            return r.toUtf8();
            }

      void raw(const QByteArray& line) { indent(); _b.append(line); _b.append("\r\n"); }
      void open(const char* tag, const QByteArray& attrs = QByteArray())
            {
            raw(QByteArray("<") + tag + (attrs.isEmpty() ? QByteArray() : " " + attrs) + ">");
            ++_depth;
            }
      void close(const char* tag) { --_depth; raw(QByteArray("</") + tag + ">"); }
      void empty(const char* tag, const QByteArray& attrs = QByteArray())
            {
            raw(QByteArray("<") + tag + " " + (attrs.isEmpty() ? QByteArray() : attrs + " ") + "/>");
            }
      void value(const char* tag, const QString& v) { empty(tag, "Value=\"" + esc(v) + "\""); }
      void value(const char* tag, const char* v) { value(tag, QString::fromUtf8(v)); }
      void value(const char* tag, int v) { value(tag, QString::number(v)); }
      void value(const char* tag, qint64 v) { value(tag, QString::number(v)); }
      void value(const char* tag, bool v) { value(tag, v ? "true" : "false"); }
      void number(const char* tag, double v) { value(tag, num(v)); }
      void lom(const char* tag) { empty(tag, "LomId=\"0\""); }
      static QString num(double v)
            {
            if (v == std::floor(v) && std::fabs(v) < 1e15)
                  return QString::number(qint64(v));
            return QString::number(v, 'g', 10);
            }
      // binary data as Live writes it: upper-case hex, 40 bytes a line
      void hex(const char* tag, const QByteArray& data)
            {
            if (data.isEmpty()) {
                  empty(tag);
                  return;
                  }
            open(tag);
            const QByteArray h = data.toHex().toUpper();
            for (int i = 0; i < h.size(); i += 80)
                  raw(h.mid(i, 80));
            close(tag);
            }

      //---------------------------------------------------------
      //   Live's recurring pieces
      //---------------------------------------------------------

      void target(const char* tag, int fixedId = 0)   // an AutomationTarget, a ModulationTarget …
            {
            open(tag, "Id=\"" + QByteArray::number(fixedId ? fixedId : id()) + "\"");
            value("LockEnvelope", 0);
            close(tag);
            }
      void range(const char* tag, const QString& min, const QString& max)
            {
            open(tag);
            value("Min", min);
            value("Max", max);
            close(tag);
            }
      // an on / off switch (a device's On, the Mixer's Speaker)
      void onOff(const char* tag, bool on = true)
            {
            open(tag);
            value("LomId", 0);
            value("Manual", on);
            target("AutomationTarget");
            range("MidiCCOnOffThresholds", "64", "127");
            close(tag);
            }
      // a continuous parameter (volume, pan …)
      void param(const char* tag, const QString& manual, const QString& min, const QString& max, bool modulation = true,
                 int targetId = 0)
            {
            open(tag);
            value("LomId", 0);
            value("Manual", manual);
            range("MidiControllerRange", min, max);
            target("AutomationTarget", targetId);
            if (modulation)
                  target("ModulationTarget");
            close(tag);
            }
      // one whose range comes after its target (the crossfade assignment, the time signature)
      void paramRangeAfter(const char* tag, const QString& manual, const QString& min, const QString& max, int targetId = 0)
            {
            open(tag);
            value("LomId", 0);
            value("Manual", manual);
            target("AutomationTarget", targetId);
            range("MidiControllerRange", min, max);
            close(tag);
            }
      void routing(const char* tag, const QString& target, const QString& upper, const QString& lower)
            {
            open(tag);
            value("Target", target);
            value("UpperDisplayString", upper);
            value("LowerDisplayString", lower);
            mpeSettings();
            value("MpePitchBendUsesTuning", true);
            close(tag);
            }
      void mpeSettings()
            {
            open("MpeSettings");
            value("ZoneType", 0);
            value("FirstNoteChannel", 1);
            value("LastNoteChannel", 15);
            close("MpeSettings");
            }
      void emptyValue(const char* tag)                // <Tag><Value /></Tag>
            {
            open(tag);
            empty("Value");
            close(tag);
            }
      // what every device (and a track's mixer and sequencers) starts with
      void deviceHeader(bool expanded, bool showPresetName)
            {
            value("LomId", 0);
            value("LomIdView", 0);
            value("IsExpanded", expanded);
            value("BreakoutIsExpanded", false);
            onOff("On");
            value("ModulationSourceCount", 0);
            lom("ParametersListWrapper");
            empty("Pointee", "Id=\"" + QByteArray::number(id()) + "\"");
            value("LastSelectedTimeableIndex", 0);
            value("LastSelectedClipEnvelopeIndex", 0);
            emptyValue("LastPresetRef");
            empty("LockedScripts");
            value("IsFolded", false);
            value("ShouldShowPresetName", showPresetName);
            value("UserName", "");
            value("Annotation", "");
            emptyValue("SourceContext");
            value("MpePitchBendUsesTuning", true);
            }
      void arrangerAutomation(const char* tag)
            {
            open(tag);
            open("ArrangerAutomation");
            empty("Events");
            open("AutomationTransformViewState");
            value("IsTransformPending", false);
            empty("TimeAndValueTransforms");
            close("AutomationTransformViewState");
            close("ArrangerAutomation");
            close(tag);
            }
      void clipSlots(int n)
            {
            if (!n) {
                  empty("ClipSlotList");
                  return;
                  }
            open("ClipSlotList");
            for (int i = 0; i < n; ++i) {
                  open("ClipSlot", "Id=\"" + QByteArray::number(i) + "\"");
                  value("LomId", 0);
                  emptyValue("ClipSlot");
                  value("HasStop", true);
                  value("NeedRefreeze", true);
                  close("ClipSlot");
                  }
            close("ClipSlotList");
            }
      void recorder(int takeCounter)
            {
            open("Recorder");
            value("IsArmed", false);
            value("TakeCounter", takeCounter);
            close("Recorder");
            }
      // the audio sequencer's modulation targets and view state (a track's FreezeSequencer, the main track's)
      void sampleSequencerTail()
            {
            arrangerAutomation("Sample");
            target("VolumeModulationTarget");
            target("TranspositionModulationTarget");
            target("TransientEnvelopeModulationTarget");
            target("GrainSizeModulationTarget");
            target("FluxModulationTarget");
            target("SampleOffsetModulationTarget");
            target("ComplexProFormantsModulationTarget");
            target("ComplexProEnvelopeModulationTarget");
            value("PitchViewScrollPosition", -1073741824);
            value("SampleOffsetModulationScrollPosition", -1073741824);
            recorder(1);
            }
      void trackHead(const QString& effectiveName, const QString& userName, int color)
            {
            value("LomId", 0);
            value("LomIdView", 0);
            value("IsContentSelectedInDocument", false);
            value("PreferredContentViewMode", 0);
            open("TrackDelay");
            value("Value", 0);
            value("IsValueSampleBased", false);
            close("TrackDelay");
            open("Name");
            value("EffectiveName", effectiveName);
            value("UserName", userName);
            value("Annotation", "");
            value("MemorizedFirstClipName", "");
            close("Name");
            value("Color", color);
            }
      void trackLists(bool unfolded, int groupTrackId = -1)
            {
            value("TrackGroupId", groupTrackId);
            value("TrackUnfolded", unfolded);
            lom("DevicesListWrapper");
            lom("ClipSlotsListWrapper");
            lom("ArrangementClipsListWrapper");
            lom("TakeLanesListWrapper");
            value("ViewData", "{}");
            open("TakeLanes");
            empty("TakeLanes");
            value("AreTakeLanesFolded", true);
            close("TakeLanes");
            value("LinkedTrackGroupId", -1);
            }
      void automationLanes(int laneHeight)
            {
            open("AutomationLanes");
            open("AutomationLanes");
            open("AutomationLane", "Id=\"0\"");
            value("SelectedDevice", 0);
            value("SelectedEnvelope", 0);
            value("IsContentSelectedInDocument", false);
            value("LaneHeight", laneHeight);
            close("AutomationLane");
            close("AutomationLanes");
            value("AreAdditionalAutomationLanesFolded", false);
            close("AutomationLanes");
            open("ClipEnvelopeChooserViewState");
            value("SelectedDevice", 0);
            value("SelectedEnvelope", 0);
            value("PreferModulationVisible", false);
            close("ClipEnvelopeChooserViewState");
            }
      // a track's mixer, up to its SendsListWrapper (the main track's goes on with the song's tempo …)
      // (speaker: the Track Activator, off = the track muted; pan -1 … 1, volume a linear gain, 1 = 0 dB)
      void mixerStart(double volume, int trackWidth, double pan = 0, bool speaker = true)
            {
            deviceHeader(true, false);
            empty("Sends");                         // (no return tracks)
            onOff("Speaker", speaker);
            value("SoloSink", false);
            value("PanMode", 0);
            param("Pan", num(pan), "-1", "1");
            param("SplitStereoPanL", "-1", "-1", "1");
            param("SplitStereoPanR", "1", "-1", "1");
            param("Volume", num(volume), "0.0003162277571", "1.99526238");
            value("ViewStateSessionTrackWidth", trackWidth);
            paramRangeAfter("CrossFadeState", "1", "0", "2");
            lom("SendsListWrapper");
            }
      };

//---------------------------------------------------------
//   the plain set's pieces (livesetclips.cpp)
//---------------------------------------------------------

// a group track (Live 12.2's, as the owner's sets have them): no devices, a group slot per scene; groupTrackId: the
// group it is in (its track Id), -1: none
void groupTrack(Writer& w, const Track& t, int trackId, int groupTrackId, int scenes);
// a track's AutomationEnvelopes: its plug-in parameters' automation; parameterTargets: the AutomationTarget id of each
// of Plugin::parameters' ParameterValue (pluginDevice writes them with these ids)
void automationEnvelopes(Writer& w, const Track& t, const std::vector<int>& parameterTargets);
// the MainSequencer's ClipTimeable: the track's arrangement clips; controllerTargets: the ids of the track's
// MidiControllers (ControllerTargets.<n>, CONTROLLER_TARGETS of them), written after it with these ids
constexpr int CONTROLLER_TARGETS = 131;
void clipTimeable(Writer& w, const Track& t, int numerator, int denominator, const std::vector<int>& controllerTargets);
// the MidiControllers index of a controller (a CC, PITCH_BEND_ENVELOPE); -1: none
int controllerTarget(int controller);
// MIDI To: the track (its Id) whose plug-in gets the MIDI
void midiToRouting(Writer& w, int trackId, const QString& trackName);

}     // namespace LiveSetWriter
}     // namespace Ms
#endif
