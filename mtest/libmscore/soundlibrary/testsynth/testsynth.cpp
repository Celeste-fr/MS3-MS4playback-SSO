//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  MS Test Synth: a VST 3 instrument standing in for a sound library's plug-in in the
//  mtests (tst_soundlibrary). Like Kontakt, it maps MIDI CCs to parameters (IMidiMapping):
//  CC32 -> "articulation" (what UACC switches), CC1 -> "level" (the dynamics). Both are in its
//  state. No editor. Like Kontakt's host automation, parameters no CC is mapped to: "Tone"
//  (the output's level: 0 is 20 %; not in its state) and twelve placeholders "Macro 1" …
//  "Macro 12" that do nothing (Extract: tst_soundlibrary::pluginExtract).
//
//  Each held note plays at velocity * level, with a timbre of the articulation that was
//  current at its note on (the articulation check listens for it), like a UACC patch:
//    1-29, 31-89   harmonics of their own; 40-60 short (decaying), 70-80 trills (tremolo)
//    25            like a harmonics patch: -66 dB under pitch 72 (no sample there)
//    26            very soft (-40 dB), like a super sul tasto
//    30            plays nothing
//    85-89         not in the patch: articulation 1 (a default)
//    90-127        not in the patch: ignored, the articulation stays
//  and round robins: each note a little louder or softer than the last, its harmonics a
//  little different (±8 %). Like Kontakt, it hears no MIDI when its event input is not
//  active, and it is silent when its output is not active.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#define _USE_MATH_DEFINES           // M_PI with MSVC
#include <cmath>
#include <map>
#include <string>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

enum : ParamID { kArticulation = 1, kLevel = 2, kTone = 3, kMacro = 100 };

static const FUID ProcessorUID(0x6d737473, 0x796e7468, 0x70726f63, 0x00000001);
static const FUID ControllerUID(0x6d737473, 0x796e7468, 0x6374726c, 0x00000001);

//---------------------------------------------------------
//   Processor
//---------------------------------------------------------

struct Voice {
      double phase { 0 };
      float velocity { 0 };
      int articulation { 1 };
      int pitch { 60 };
      float tuning { 0 };           // cents (NoteOnEvent::tuning), as Kontakt may or may not honour it
      double gain { 1 };            // the round robin
      int roundRobin { 0 };
      long t { 0 };                 // samples played
      };

static int articulationValue(ParamValue v)
      {
      return int(std::lround(v * 127));
      }

static bool inPatch(int value)
      {
      return value >= 1 && value < 90;
      }

static float timbre(const Voice& v, double sampleRate)
      {
      if (v.articulation == 30)
            return 0.f;
      double s = 0;
      for (int k = 1; k <= 8; ++k) {
            double x = std::sin(v.articulation * 12.9898 + k * 78.233) * 43758.5453;
            x -= std::floor(x);
            s += std::sin(k * v.phase) * (0.3 + 0.7 * x) / k * (1 + 0.08 * std::sin(v.roundRobin * 1.7 + k));
            }
      const double t = v.t / sampleRate;
      if (v.articulation == 25 && v.pitch < 72)
            s *= 0.0005;
      if (v.articulation == 26)
            s *= 0.01;
      if (v.articulation >= 40 && v.articulation <= 60)
            s *= std::exp(-t / 0.1);
      else if (v.articulation >= 70 && v.articulation <= 80)
            s *= 0.6 + 0.4 * std::sin(2 * M_PI * 8 * t);
      return float(s * 0.25 * v.gain);
      }

class Processor : public AudioEffect {
      ParamValue articulation { 0.0 };
      ParamValue level { 1.0 };
      ParamValue tone { 1.0 };
      int current { 1 };            // the articulation notes start with
      int roundRobin { 0 };
      std::map<int, Voice> voices;  // pitch -> voice

      void setArticulation(ParamValue v)
            {
            articulation = v;
            const int value = articulationValue(v);
            if (value >= 85 && value < 90)
                  current = 1;
            else if (inPatch(value))
                  current = value;
            }

   public:
      Processor() { setControllerClass(ControllerUID); }
      static FUnknown* create(void*) { return (IAudioProcessor*) new Processor; }

      tresult PLUGIN_API initialize(FUnknown* context) override
            {
            tresult r = AudioEffect::initialize(context);
            if (r != kResultOk)
                  return r;
            addAudioOutput(STR16("Out"), SpeakerArr::kStereo);
            addEventInput(STR16("MIDI"), 16);
            return kResultOk;
            }

      tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32, SpeakerArrangement* outputs, int32 numOuts) override
            {
            return (numOuts == 1 && outputs[0] == SpeakerArr::kStereo) ? kResultTrue : kResultFalse;
            }

      tresult PLUGIN_API process(ProcessData& data) override
            {
            if (IParameterChanges* changes = data.inputParameterChanges) {
                  for (int32 i = 0; i < changes->getParameterCount(); ++i) {
                        IParamValueQueue* q = changes->getParameterData(i);
                        int32 offset;
                        ParamValue v;
                        if (q && q->getPoint(q->getPointCount() - 1, offset, v) == kResultOk) {
                              if (q->getParameterId() == kArticulation)
                                    setArticulation(v);
                              else if (q->getParameterId() == kLevel)
                                    level = v;
                              else if (q->getParameterId() == kTone)
                                    tone = v;
                              }
                        }
                  }
            const bool eventsOn = getEventInput(0) && getEventInput(0)->isActive();
            if (IEventList* events = eventsOn ? data.inputEvents : nullptr) {
                  for (int32 i = 0; i < events->getEventCount(); ++i) {
                        Event e;
                        if (events->getEvent(i, e) != kResultOk)
                              continue;
                        if (e.type == Event::kNoteOnEvent && e.noteOn.velocity > 0) {
                              Voice v;
                              v.velocity = e.noteOn.velocity;
                              v.articulation = current;
                              v.pitch = e.noteOn.pitch;
                              v.tuning = e.noteOn.tuning;
                              v.roundRobin = roundRobin % 4;
                              v.gain = 1.0 + 0.06 * ((roundRobin++ % 3) - 1);
                              voices[e.noteOn.pitch] = v;
                              }
                        else if (e.type == Event::kNoteOnEvent || e.type == Event::kNoteOffEvent)
                              voices.erase(e.type == Event::kNoteOnEvent ? e.noteOn.pitch : e.noteOff.pitch);
                        }
                  }
            if (data.numOutputs < 1 || data.outputs[0].numChannels < 2)
                  return kResultOk;
            if (!getAudioOutput(0) || !getAudioOutput(0)->isActive()) {
                  for (int32 c = 0; c < data.outputs[0].numChannels; ++c)
                        for (int32 i = 0; i < data.numSamples; ++i)
                              data.outputs[0].channelBuffers32[c][i] = 0.f;
                  data.outputs[0].silenceFlags = 3;
                  return kResultOk;
                  }
            float* l = data.outputs[0].channelBuffers32[0];
            float* r = data.outputs[0].channelBuffers32[1];
            for (int32 i = 0; i < data.numSamples; ++i)
                  l[i] = r[i] = 0.f;
            for (auto& v : voices) {
                  const double inc = 2 * M_PI * 440.0 * std::pow(2.0, (v.first - 69 + v.second.tuning / 100.0) / 12.0) / processSetup.sampleRate;
                  Voice& vc = v.second;
                  for (int32 i = 0; i < data.numSamples; ++i) {
                        const float s = timbre(vc, processSetup.sampleRate) * vc.velocity * float(level) * float(0.2 + 0.8 * tone);
                        l[i] += s;
                        r[i] += s;
                        vc.phase += inc;
                        ++vc.t;
                        }
                  }
            data.outputs[0].silenceFlags = voices.empty() ? 3 : 0;
            return kResultOk;
            }

      tresult PLUGIN_API setState(IBStream* state) override
            {
            IBStreamer s(state, kLittleEndian);
            double a, lv;
            if (!s.readDouble(a) || !s.readDouble(lv))
                  return kResultFalse;
            setArticulation(a);
            level = lv;
            return kResultOk;
            }

      tresult PLUGIN_API getState(IBStream* state) override
            {
            IBStreamer s(state, kLittleEndian);
            s.writeDouble(articulation);
            s.writeDouble(level);
            return kResultOk;
            }
      };

//---------------------------------------------------------
//   Controller
//---------------------------------------------------------

// the names a DAW shows for its keys (Vst3Plugin::keyNames): pitch names of its one program
// (IUnitInfo) and two keyswitches (IKeyswitchController)
static const std::map<int, const char16_t*> PITCH_NAMES { { 36, u"Kick" }, { 38, u"Snare" }, { 42, u"Hi-Hat Closed" } };

static void copyString128(String128 to, const char16_t* from)
      {
      int i = 0;
      for (; from[i] && i < 127; ++i)
            to[i] = TChar(from[i]);
      to[i] = 0;
      }

class Controller : public EditController, public IMidiMapping, public IUnitInfo, public IKeyswitchController {
   public:
      static FUnknown* create(void*) { return (IEditController*) new Controller; }

      tresult PLUGIN_API initialize(FUnknown* context) override
            {
            tresult r = EditController::initialize(context);
            if (r != kResultOk)
                  return r;
            parameters.addParameter(STR16("Articulation"), nullptr, 0, 0.0, ParameterInfo::kCanAutomate, kArticulation);
            parameters.addParameter(STR16("Level"), nullptr, 0, 1.0, ParameterInfo::kCanAutomate, kLevel);
            parameters.addParameter(STR16("Tone"), STR16("%"), 0, 1.0, ParameterInfo::kCanAutomate, kTone);
            for (int i = 0; i < 12; ++i) {
                  char16_t title[16];
                  const std::string t = "Macro " + std::to_string(i + 1);
                  for (size_t k = 0; k <= t.size(); ++k)
                        title[k] = char16_t(t.c_str()[k]);
                  parameters.addParameter(reinterpret_cast<const TChar*>(title), nullptr, 0, 0.0, ParameterInfo::kCanAutomate, kMacro + i);
                  }
            return kResultOk;
            }

      tresult PLUGIN_API setComponentState(IBStream* state) override
            {
            IBStreamer s(state, kLittleEndian);
            double a, lv;
            if (!s.readDouble(a) || !s.readDouble(lv))
                  return kResultFalse;
            setParamNormalized(kArticulation, a);
            setParamNormalized(kLevel, lv);
            return kResultOk;
            }

      tresult PLUGIN_API getMidiControllerAssignment(int32 busIndex, int16, CtrlNumber cc, ParamID& id) override
            {
            if (busIndex != 0)
                  return kResultFalse;
            if (cc == 32)
                  id = kArticulation;
            else if (cc == kCtrlModWheel)
                  id = kLevel;
            else
                  return kResultFalse;
            return kResultTrue;
            }

      // IUnitInfo: one unit, one program list of one program, with pitch names
      int32 PLUGIN_API getUnitCount() override { return 1; }
      tresult PLUGIN_API getUnitInfo(int32 unitIndex, UnitInfo& info) override
            {
            if (unitIndex != 0)
                  return kResultFalse;
            info.id = kRootUnitId;
            info.parentUnitId = kNoParentUnitId;
            copyString128(info.name, u"Root");
            info.programListId = 1;
            return kResultTrue;
            }
      int32 PLUGIN_API getProgramListCount() override { return 1; }
      tresult PLUGIN_API getProgramListInfo(int32 listIndex, ProgramListInfo& info) override
            {
            if (listIndex != 0)
                  return kResultFalse;
            info.id = 1;
            copyString128(info.name, u"Kits");
            info.programCount = 1;
            return kResultTrue;
            }
      tresult PLUGIN_API getProgramName(ProgramListID listId, int32 programIndex, String128 name) override
            {
            if (listId != 1 || programIndex != 0)
                  return kResultFalse;
            copyString128(name, u"Test Kit");
            return kResultTrue;
            }
      tresult PLUGIN_API getProgramInfo(ProgramListID, int32, Steinberg::Vst::CString, String128) override { return kResultFalse; }
      tresult PLUGIN_API hasProgramPitchNames(ProgramListID listId, int32 programIndex) override
            {
            return listId == 1 && programIndex == 0 ? kResultTrue : kResultFalse;
            }
      tresult PLUGIN_API getProgramPitchName(ProgramListID listId, int32 programIndex, int16 midiPitch, String128 name) override
            {
            auto it = PITCH_NAMES.find(midiPitch);
            if (listId != 1 || programIndex != 0 || it == PITCH_NAMES.end())
                  return kResultFalse;
            copyString128(name, it->second);
            return kResultTrue;
            }
      UnitID PLUGIN_API getSelectedUnit() override { return kRootUnitId; }
      tresult PLUGIN_API selectUnit(UnitID) override { return kResultTrue; }
      tresult PLUGIN_API getUnitByBus(MediaType, BusDirection, int32, int32, UnitID& unitId) override
            {
            unitId = kRootUnitId;
            return kResultTrue;
            }
      tresult PLUGIN_API setUnitProgramData(int32, int32, IBStream*) override { return kResultFalse; }

      // IKeyswitchController: Legato on key 24, Staccato on 25
      int32 PLUGIN_API getKeyswitchCount(int32 busIndex, int16) override { return busIndex == 0 ? 2 : 0; }
      tresult PLUGIN_API getKeyswitchInfo(int32 busIndex, int16, int32 index, KeyswitchInfo& info) override
            {
            if (busIndex != 0 || index < 0 || index > 1)
                  return kResultFalse;
            info = KeyswitchInfo {};
            info.typeId = kNoteOnKeyswitchTypeID;
            copyString128(info.title, index == 0 ? u"Legato" : u"Staccato");
            copyString128(info.shortTitle, index == 0 ? u"Leg" : u"Stac");
            info.keyswitchMin = info.keyswitchMax = 24 + index;
            info.keyRemapped = -1;
            info.unitId = -1;
            info.flags = 0;
            return kResultTrue;
            }

      OBJ_METHODS(Controller, EditController)
      DEFINE_INTERFACES
            DEF_INTERFACE(IMidiMapping)
            DEF_INTERFACE(IUnitInfo)
            DEF_INTERFACE(IKeyswitchController)
      END_DEFINE_INTERFACES(EditController)
      REFCOUNT_METHODS(EditController)
      };

//---------------------------------------------------------
//   factory
//---------------------------------------------------------

BEGIN_FACTORY_DEF("MuseScore", "https://musescore.org", "")
      DEF_CLASS2(INLINE_UID_FROM_FUID(ProcessorUID), PClassInfo::kManyInstances, kVstAudioEffectClass,
                 "MS Test Synth", Vst::kDistributable, "Instrument|Synth", "1.0.0", kVstVersionString, Processor::create)
      DEF_CLASS2(INLINE_UID_FROM_FUID(ControllerUID), PClassInfo::kManyInstances, kVstComponentControllerClass,
                 "MS Test Synth Controller", 0, "", "1.0.0", kVstVersionString, Controller::create)
END_FACTORY
