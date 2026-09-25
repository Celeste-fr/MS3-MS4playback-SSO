//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  MS Test Synth: a VST 3 instrument standing in for a sound library's plug-in in the
//  mtests (tst_soundlibrary). A sine per held note at velocity * level. Like Kontakt, it maps
//  MIDI CCs to parameters (IMidiMapping): CC32 -> "articulation" (what UACC switches),
//  CC1 -> "level" (the dynamics). Both are in its state. No editor.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#define _USE_MATH_DEFINES           // M_PI with MSVC
#include <cmath>
#include <map>

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/main/pluginfactory.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

enum : ParamID { kArticulation = 1, kLevel = 2 };

static const FUID ProcessorUID(0x6d737473, 0x796e7468, 0x70726f63, 0x00000001);
static const FUID ControllerUID(0x6d737473, 0x796e7468, 0x6374726c, 0x00000001);

//---------------------------------------------------------
//   Processor
//---------------------------------------------------------

class Processor : public AudioEffect {
      ParamValue articulation { 0.0 };
      ParamValue level { 1.0 };
      std::map<int, std::pair<double, float>> voices;       // pitch -> phase, velocity

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
                                    articulation = v;
                              else if (q->getParameterId() == kLevel)
                                    level = v;
                              }
                        }
                  }
            if (IEventList* events = data.inputEvents) {
                  for (int32 i = 0; i < events->getEventCount(); ++i) {
                        Event e;
                        if (events->getEvent(i, e) != kResultOk)
                              continue;
                        if (e.type == Event::kNoteOnEvent && e.noteOn.velocity > 0)
                              voices[e.noteOn.pitch] = { 0.0, e.noteOn.velocity };
                        else if (e.type == Event::kNoteOnEvent || e.type == Event::kNoteOffEvent)
                              voices.erase(e.type == Event::kNoteOnEvent ? e.noteOn.pitch : e.noteOff.pitch);
                        }
                  }
            if (data.numOutputs < 1 || data.outputs[0].numChannels < 2)
                  return kResultOk;
            float* l = data.outputs[0].channelBuffers32[0];
            float* r = data.outputs[0].channelBuffers32[1];
            for (int32 i = 0; i < data.numSamples; ++i)
                  l[i] = r[i] = 0.f;
            for (auto& v : voices) {
                  const double inc = 2 * M_PI * 440.0 * std::pow(2.0, (v.first - 69) / 12.0) / processSetup.sampleRate;
                  for (int32 i = 0; i < data.numSamples; ++i) {
                        const float s = float(std::sin(v.second.first) * 0.2 * v.second.second * level);
                        l[i] += s;
                        r[i] += s;
                        v.second.first += inc;
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
            articulation = a;
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

class Controller : public EditController, public IMidiMapping {
   public:
      static FUnknown* create(void*) { return (IEditController*) new Controller; }

      tresult PLUGIN_API initialize(FUnknown* context) override
            {
            tresult r = EditController::initialize(context);
            if (r != kResultOk)
                  return r;
            parameters.addParameter(STR16("Articulation"), nullptr, 0, 0.0, ParameterInfo::kCanAutomate, kArticulation);
            parameters.addParameter(STR16("Level"), nullptr, 0, 1.0, ParameterInfo::kCanAutomate, kLevel);
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

      OBJ_METHODS(Controller, EditController)
      DEFINE_INTERFACES
            DEF_INTERFACE(IMidiMapping)
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
