//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Plugin: a hosted VST 3 instrument (see vst3plugin.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "vst3plugin.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include <QDataStream>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/iplugincompatibility.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstautomationstate.h"
#include "pluginterfaces/vst/ivstchannelcontextinfo.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivstcontextmenu.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstinterappaudio.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstmidilearn.h"
#include "pluginterfaces/vst/ivstmidimapping2.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstnoteonorchestralarticulationinfo.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstparameterfunctionname.h"
#include "pluginterfaces/vst/ivstphysicalui.h"
#include "pluginterfaces/vst/ivstpluginterfacesupport.h"
#include "pluginterfaces/vst/ivstplugview.h"
#include "pluginterfaces/vst/ivstprefetchablesupport.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstremapparamid.h"
#include "pluginterfaces/vst/ivstrepresentation.h"
#include "pluginterfaces/vst/ivsttransportcontrol.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

#include "audio/midi/event.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Ms {

//---------------------------------------------------------
//   host context and modules
//    one IHostApplication for all plug-ins; a module (the plug-in's library), once loaded,
//    stays loaded, as in DAWs: a library unloaded and loaded again can hang (sfizz: its
//    fonts, pango types registered in GLib twice)
//---------------------------------------------------------

//---------------------------------------------------------
//   interface names and the host's query log
//    what plug-ins ask MuseScore for tells what they would use (Extract: hostQueries())
//---------------------------------------------------------

struct NamedInterface {
      const FUID* iid;
      const char* name;
      };

static const std::vector<NamedInterface>& interfaceNames()
      {
      static const std::vector<NamedInterface> names {
            { &FUnknown::iid, "FUnknown" },
            { &IPluginBase::iid, "IPluginBase" },
            { &IPluginFactory::iid, "IPluginFactory" },
            { &IPluginFactory2::iid, "IPluginFactory2" },
            { &IPluginFactory3::iid, "IPluginFactory3" },
            { &IPluginCompatibility::iid, "IPluginCompatibility" },
            { &IBStream::iid, "IBStream" },
            { &ISizeableStream::iid, "ISizeableStream" },
            { &IPlugView::iid, "IPlugView" },
            { &IPlugFrame::iid, "IPlugFrame" },
            { &IPlugViewContentScaleSupport::iid, "IPlugViewContentScaleSupport" },
            { &Vst::IComponent::iid, "IComponent" },
            { &Vst::IAudioProcessor::iid, "IAudioProcessor" },
            { &Vst::IAudioPresentationLatency::iid, "IAudioPresentationLatency" },
            { &Vst::IProcessContextRequirements::iid, "IProcessContextRequirements" },
            { &Vst::IEditController::iid, "IEditController" },
            { &Vst::IEditController2::iid, "IEditController2" },
            { &Vst::IEditControllerHostEditing::iid, "IEditControllerHostEditing" },
            { &Vst::IMidiMapping::iid, "IMidiMapping" },
            { &Vst::IMidiMapping2::iid, "IMidiMapping2" },
            { &Vst::IMidiLearn::iid, "IMidiLearn" },
            { &Vst::IMidiLearn2::iid, "IMidiLearn2" },
            { &Vst::IUnitInfo::iid, "IUnitInfo" },
            { &Vst::IProgramListData::iid, "IProgramListData" },
            { &Vst::IUnitData::iid, "IUnitData" },
            { &Vst::IKeyswitchController::iid, "IKeyswitchController" },
            { &Vst::INoteExpressionController::iid, "INoteExpressionController" },
            { &Vst::INoteExpressionPhysicalUIMapping::iid, "INoteExpressionPhysicalUIMapping" },
            { &Vst::NoteOnOrchestralArticulation::IInfo::iid, "NoteOnOrchestralArticulation::IInfo" },
            { &Vst::IXmlRepresentationController::iid, "IXmlRepresentationController" },
            { &Vst::IParameterFunctionName::iid, "IParameterFunctionName" },
            { &Vst::IParameterFinder::iid, "IParameterFinder" },
            { &Vst::IAutomationState::iid, "IAutomationState" },
            { &Vst::IPrefetchableSupport::iid, "IPrefetchableSupport" },
            { &Vst::ChannelContext::IInfoListener::iid, "ChannelContext::IInfoListener" },
            { &Vst::IDataExchangeReceiver::iid, "IDataExchangeReceiver" },
            { &Vst::IDataExchangeHandler::iid, "IDataExchangeHandler" },
            { &Vst::IRemapParamID::iid, "IRemapParamID" },
            { &Vst::IConnectionPoint::iid, "IConnectionPoint" },
            { &Vst::IMessage::iid, "IMessage" },
            { &Vst::IAttributeList::iid, "IAttributeList" },
            { &Vst::IStreamAttributes::iid, "IStreamAttributes" },
            { &Vst::IHostApplication::iid, "IHostApplication" },
            { &Vst::IPlugInterfaceSupport::iid, "IPlugInterfaceSupport" },
            { &Vst::IComponentHandler::iid, "IComponentHandler" },
            { &Vst::IComponentHandler2::iid, "IComponentHandler2" },
            { &Vst::IComponentHandler3::iid, "IComponentHandler3" },
            { &Vst::IComponentHandlerBusActivation::iid, "IComponentHandlerBusActivation" },
            { &Vst::IComponentHandlerSystemTime::iid, "IComponentHandlerSystemTime" },
            { &Vst::IProgress::iid, "IProgress" },
            { &Vst::IUnitHandler::iid, "IUnitHandler" },
            { &Vst::IUnitHandler2::iid, "IUnitHandler2" },
            { &Vst::IContextMenuTarget::iid, "IContextMenuTarget" },
            { &Vst::IContextMenu::iid, "IContextMenu" },
            { &Vst::ITransportControl::iid, "ITransportControl" },
            { &Vst::IInterAppAudioHost::iid, "IInterAppAudioHost" },
            { &Vst::IVst3ToVst2Wrapper::iid, "IVst3ToVst2Wrapper" },
            { &Vst::IVst3ToAUWrapper::iid, "IVst3ToAUWrapper" },
            { &Vst::IVst3ToAAXWrapper::iid, "IVst3ToAAXWrapper" },
            { &Vst::IVst3WrapperMPESupport::iid, "IVst3WrapperMPESupport" },
            };
      return names;
      }

static QString interfaceName(const TUID iid)
      {
      for (const NamedInterface& n : interfaceNames())
            if (FUnknownPrivate::iidEqual(iid, n.iid->toTUID()))
                  return n.name;
      char8 s[64];
      FUID::fromTUID(iid).toString(s);
      return QString(s);
      }

static std::mutex& queryMutex()
      {
      static std::mutex m;
      return m;
      }

// "who: interface" -> answer
static std::map<QString, QString>& queryLog()
      {
      static auto* log = new std::map<QString, QString>;
      return *log;
      }

static void noteQuery(const char* who, const TUID iid, tresult answer)
      {
      std::lock_guard<std::mutex> lock(queryMutex());
      queryLog()[QString("%1: %2").arg(who, interfaceName(iid))] = answer == kResultOk ? "yes" : "no";
      }

// IPlugInterfaceSupport: the plug-in asks whether MuseScore supports one of its interfaces
class LoggedInterfaceSupport : public U::ImplementsNonDestroyable<U::Directly<IPlugInterfaceSupport>> {
   public:
      IPlugInterfaceSupport* inner { nullptr };
      tresult PLUGIN_API isPlugInterfaceSupported(const TUID iid) override
            {
            const tresult r = inner ? inner->isPlugInterfaceSupported(iid) : kResultFalse;
            noteQuery("isPlugInterfaceSupported", iid, r == kResultTrue ? kResultOk : kResultFalse);
            return r;
            }
      };

class MuseScoreHostApplication : public HostApplication {
      LoggedInterfaceSupport support;
   public:
      tresult PLUGIN_API getName(String128 name) override
            {
            const char16_t* n = u"MuseScore";
            std::memcpy(name, n, (std::char_traits<char16_t>::length(n) + 1) * sizeof(char16_t));
            return kResultTrue;
            }
      tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
            {
            if (FUnknownPrivate::iidEqual(iid, IPlugInterfaceSupport::iid) && getPlugInterfaceSupport()) {
                  support.inner = getPlugInterfaceSupport();
                  *obj = static_cast<IPlugInterfaceSupport*>(&support);
                  noteQuery("host", iid, kResultOk);
                  return kResultOk;
                  }
            const tresult r = HostApplication::queryInterface(iid, obj);
            noteQuery("host", iid, r);
            return r;
            }
      };

static FUnknown* hostContext()
      {
      static IPtr<MuseScoreHostApplication> host = owned(new MuseScoreHostApplication);
      PluginContextFactory::instance().setPluginContext(host);
      return host;
      }

static std::map<QString, std::shared_ptr<VST3::Hosting::Module>>& modules()
      {
      // never destroyed: no unloading at exit either, after the plug-ins' own teardown
      static auto* m = new std::map<QString, std::shared_ptr<VST3::Hosting::Module>>;
      return *m;
      }

//---------------------------------------------------------
//   ComponentHandler
//    the controller's edits (the plug-in's editor) for the processor
//---------------------------------------------------------

class ComponentHandler : public U::ImplementsNonDestroyable<U::Directly<IComponentHandler>> {
      using Base = U::ImplementsNonDestroyable<U::Directly<IComponentHandler>>;
   public:
      std::mutex mutex;
      std::vector<std::pair<ParamID, ParamValue>> edits;
      std::vector<std::pair<ParamID, ParamValue>> reported;     // for takeReported() (bounded)
      bool midiMappingChanged { false };
      bool titlesChanged { false };             // (Vst3Plugin::parameterId's index)

      tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
            {
            const tresult r = Base::queryInterface(iid, obj);
            noteQuery("component handler", iid, r);
            return r;
            }
      tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
            {
            std::lock_guard<std::mutex> lock(mutex);
            edits.emplace_back(id, value);
            if (reported.size() < 100000)
                  reported.emplace_back(id, value);
            return kResultOk;
            }
      tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API restartComponent(int32 flags) override
            {
            if (flags & kMidiCCAssignmentChanged) {
                  std::lock_guard<std::mutex> lock(mutex);
                  midiMappingChanged = true;
                  }
            if (flags & (kParamTitlesChanged | kIoTitlesChanged | kReloadComponent)) {
                  std::lock_guard<std::mutex> lock(mutex);
                  titlesChanged = true;
                  }
            return kResultOk;
            }
      };

//---------------------------------------------------------
//   Vst3PluginPrivate
//---------------------------------------------------------

class Vst3PluginPrivate {
   public:
      QString path;
      QString name;
      std::shared_ptr<VST3::Hosting::Module> module;
      IPtr<PlugProvider> provider;
      IPtr<IComponent> component;
      IPtr<IAudioProcessor> processor;
      IPtr<IEditController> controller;
      ComponentHandler handler;

      double sampleRate { 44100.0 };
      int maxBlock { 4096 };

      // varispeed (setPitch): the plug-in's output read at a speed, resampled; engaged from the first
      // pitch other than 0 on. fifo: what it rendered (interleaved stereo), pos: the read position in it
      bool varispeed { false };
      std::vector<float> fifo;
      double pos { 0 };
      double ratio { 1.0 };
      double targetRatio { 1.0 };
      double ratioStep { 0 };             // per output frame, while gliding
      std::vector<float> scratch;
      bool offline { false };
      bool active { false };
      HostProcessData data;
      Steinberg::Vst::EventList events { 512 };
      ParameterChanges inChanges { 512 };
      ParameterChanges outChanges { 512 };
      ProcessContext context {};
      std::vector<std::pair<ParamID, ParamValue>> fromProcessor;      // for the controller (idle)
      std::vector<std::pair<ParamID, ParamValue>> reported;           // the processor's own, for takeReported()
      std::mutex fromProcessorMutex;

      // parameterId's index: loose title -> id, of all the parameters (Kontakt has 4145; each play
      // looks up every controller of every instance). Remade after setState, a change of titles, or
      // a title not found once a second has passed (a patch's script names its slots after loading)
      std::map<QString, ParamID> titleIndex;
      bool titleIndexValid { false };
      QElapsedTimer titleIndexAge;

      // MIDI controller (0 … 129) per channel -> parameter, kNoParamId: not mapped
      std::vector<ParamID> ccParam;
      int eventBus { -1 };
      int outputBus { -1 };
      int outputChannels { 0 };

      bool setup();
      void startProcessing();
      void stopProcessing();
      void mapControllers();
      void addParam(ParamID id, ParamValue value);
      };

void Vst3PluginPrivate::mapControllers()
      {
      ccParam.assign(16 * 130, kNoParamId);
      FUnknownPtr<IMidiMapping> mapping(controller);
      if (!mapping)
            return;
      for (int ch = 0; ch < 16; ++ch) {
            for (int cc = 0; cc < 130; ++cc) {
                  ParamID id;
                  if (mapping->getMidiControllerAssignment(eventBus < 0 ? 0 : eventBus, int16(ch), CtrlNumber(cc), id) == kResultTrue)
                        ccParam[ch * 130 + cc] = id;
                  }
            }
      }

// the buses an instrument needs: the first event input and the main output (stereo). Runs
// again for offline mode or another sample rate: the buses chosen first stay on (a second
// run used to switch them all off, and Kontakt then heard and played nothing)
bool Vst3PluginPrivate::setup()
      {
      const int eventIns = component->getBusCount(kEvent, kInput);
      if (eventBus < 0 && eventIns > 0)
            eventBus = 0;
      for (int i = 0; i < eventIns; ++i)
            component->activateBus(kEvent, kInput, i, i == eventBus);
      for (int i = 0; i < component->getBusCount(kAudio, kInput); ++i)
            component->activateBus(kAudio, kInput, i, false);
      const int outs = component->getBusCount(kAudio, kOutput);
      for (int i = 0; i < outs && outputBus < 0; ++i) {
            BusInfo info;
            component->getBusInfo(kAudio, kOutput, i, info);
            if (info.busType == kMain)
                  outputBus = i;
            }
      if (outputBus < 0)
            return false;
      for (int i = 0; i < outs; ++i)
            component->activateBus(kAudio, kOutput, i, i == outputBus);

      // stereo main out where the plug-in accepts it
      std::vector<SpeakerArrangement> outArr(outs, SpeakerArr::kStereo);
      std::vector<SpeakerArrangement> inArr(component->getBusCount(kAudio, kInput), SpeakerArr::kStereo);
      processor->setBusArrangements(inArr.empty() ? nullptr : inArr.data(), int32(inArr.size()),
                                    outArr.data(), int32(outArr.size()));
      SpeakerArrangement arr = SpeakerArr::kStereo;
      processor->getBusArrangement(kOutput, outputBus, arr);
      outputChannels = SpeakerArr::getChannelCount(arr);

      ProcessSetup ps { offline ? kOffline : kRealtime, kSample32, maxBlock, sampleRate };
      if (processor->setupProcessing(ps) != kResultOk)
            return false;
      data.unprepare();
      if (!data.prepare(*component, maxBlock, kSample32))
            return false;
      mapControllers();
      return true;
      }

void Vst3PluginPrivate::startProcessing()
      {
      if (active)
            return;
      component->setActive(true);
      processor->setProcessing(true);
      active = true;
      }

void Vst3PluginPrivate::stopProcessing()
      {
      if (!active)
            return;
      processor->setProcessing(false);
      component->setActive(false);
      active = false;
      }

void Vst3PluginPrivate::addParam(ParamID id, ParamValue value)
      {
      int32 index = 0;
      if (IParamValueQueue* q = inChanges.addParameterData(id, index)) {
            int32 point = 0;
            q->addPoint(0, value, point);
            }
      }

//---------------------------------------------------------
//   Vst3Plugin
//---------------------------------------------------------

Vst3Plugin::Vst3Plugin()
   : d(new Vst3PluginPrivate)
      {
      }

Vst3Plugin::~Vst3Plugin()
      {
      d->stopProcessing();
      if (d->controller)
            d->controller->setComponentHandler(nullptr);
      d->data.unprepare();
      d->processor = nullptr;
      d->controller = nullptr;
      d->component = nullptr;
      d->provider = nullptr;           // terminates the plug-in before its module goes
      d->module.reset();
      }

std::unique_ptr<Vst3Plugin> Vst3Plugin::load(const QString& path, double sampleRate, int maxBlock, QString* error)
      {
      auto fail = [error](const QString& msg) {
            if (error)
                  *error = msg;
            return std::unique_ptr<Vst3Plugin>();
            };
      FUnknown* context = hostContext();
      Q_UNUSED(context);

      std::unique_ptr<Vst3Plugin> p(new Vst3Plugin);
      p->d->path = path;
      p->d->sampleRate = sampleRate;
      p->d->maxBlock = maxBlock;

      const QString key = QFileInfo(path).absoluteFilePath();
      p->d->module = modules()[key];
      if (!p->d->module) {
            std::string err;
            p->d->module = VST3::Hosting::Module::create(path.toStdString(), err);
            if (!p->d->module)
                  return fail(QString("cannot load %1: %2").arg(path, QString::fromStdString(err)));
            modules()[key] = p->d->module;
            }
      const VST3::Hosting::PluginFactory& factory = p->d->module->getFactory();
      VST3::Hosting::ClassInfo chosen;
      bool found = false;
      for (const VST3::Hosting::ClassInfo& ci : factory.classInfos()) {
            if (ci.category() != kVstAudioEffectClass)
                  continue;
            const bool instrument = ci.subCategoriesString().find("Instrument") != std::string::npos;
            if (!found || instrument) {
                  chosen = ci;
                  found = true;
                  if (instrument)
                        break;
                  }
            }
      if (!found)
            return fail(QString("%1 has no audio module class").arg(path));
      p->d->name = QString::fromStdString(chosen.name());
      p->d->provider = owned(new PlugProvider(factory, chosen, true));
      if (!p->d->provider->initialize())
            return fail(QString("cannot initialize %1").arg(p->d->name));
      p->d->component = p->d->provider->getComponentPtr();
      p->d->controller = p->d->provider->getControllerPtr();
      p->d->processor = FUnknownPtr<IAudioProcessor>(p->d->component);
      if (!p->d->component || !p->d->processor)
            return fail(QString("%1 has no audio processor").arg(p->d->name));
      if (p->d->controller)
            p->d->controller->setComponentHandler(&p->d->handler);
      if (!p->d->setup())
            return fail(QString("%1: no stereo output").arg(p->d->name));
      p->d->startProcessing();
      return p;
      }

QString Vst3Plugin::name() const
      {
      return d->name;
      }

QString Vst3Plugin::path() const
      {
      return d->path;
      }

//---------------------------------------------------------
//   midi
//    audio thread: for the next process()
//---------------------------------------------------------

void Vst3Plugin::midi(int type, int channel, int a, int b, float tuning)
      {
      channel = qBound(0, channel, 15);
      auto note = [&](bool on, int pitch, int velo) {
            Steinberg::Vst::Event e {};
            e.busIndex = d->eventBus < 0 ? 0 : d->eventBus;
            e.sampleOffset = 0;
            if (on) {
                  e.type = Steinberg::Vst::Event::kNoteOnEvent;
                  e.noteOn.channel = int16(channel);
                  e.noteOn.pitch = int16(pitch);
                  e.noteOn.velocity = velo / 127.f;
                  e.noteOn.tuning = tuning;
                  e.noteOn.noteId = -1;
                  }
            else {
                  e.type = Steinberg::Vst::Event::kNoteOffEvent;
                  e.noteOff.channel = int16(channel);
                  e.noteOff.pitch = int16(pitch);
                  e.noteOff.velocity = 0.f;
                  e.noteOff.noteId = -1;
                  }
            d->events.addEvent(e);
            };
      auto controller = [&](int cc, ParamValue value) {
            if (cc < 0 || cc >= 130 || d->ccParam.empty())
                  return;
            const ParamID id = d->ccParam[channel * 130 + cc];
            if (id == kNoParamId)
                  return;
            d->addParam(id, qBound(0.0, value, 1.0));
            // and to the controller, as DAWs do: its editor shows the switch (idle())
            std::unique_lock<std::mutex> lock(d->fromProcessorMutex, std::try_to_lock);
            if (lock.owns_lock())
                  d->fromProcessor.emplace_back(id, qBound(0.0, value, 1.0));
            };
      switch (type) {
            case ME_NOTEON:
                  note(b > 0, a & 0x7f, b);
                  break;
            case ME_NOTEOFF:
                  note(false, a & 0x7f, 0);
                  break;
            case ME_CONTROLLER:
                  if (a >= 0 && a < 128)
                        controller(a, b / 127.0);
                  break;
            case ME_PITCHBEND:
                  controller(kPitchBend, ((b << 7) | a) / 16383.0);
                  break;
            case ME_AFTERTOUCH:
                  controller(kAfterTouch, a / 127.0);
                  break;
            default:
                  break;
            }
      }

void Vst3Plugin::allNotesOff()
      {
      for (int ch = 0; ch < 16; ++ch) {
            midi(ME_CONTROLLER, ch, CTRL_SUSTAIN, 0);
            midi(ME_CONTROLLER, ch, CTRL_ALL_NOTES_OFF, 0);
            }
      }

//---------------------------------------------------------
//   process
//    audio thread: the queued events and edits, then the output added to buffer
//---------------------------------------------------------

//---------------------------------------------------------
//   varispeed
//    windowed-sinc interpolation (Lanczos, 8 samples each side, 512 steps a sample)
//---------------------------------------------------------

static constexpr int VS_HALF = 8;
static constexpr int VS_STEPS = 512;

static const std::vector<float>& varispeedKernel()
      {
      static const std::vector<float> table = []() {
            std::vector<float> t(size_t((VS_STEPS + 1) * 2 * VS_HALF));
            for (int s = 0; s <= VS_STEPS; ++s) {
                  const double frac = double(s) / VS_STEPS;
                  for (int k = 0; k < 2 * VS_HALF; ++k) {
                        const double x = frac - double(k - VS_HALF + 1);    // (taps i0-H+1 … i0+H)
                        double v = 1.0;
                        if (std::fabs(x) > 1e-9) {
                              const double px = 3.14159265358979323846 * x;
                              v = std::sin(px) / px * std::sin(px / VS_HALF) / (px / VS_HALF);
                              }
                        if (std::fabs(x) >= VS_HALF)
                              v = 0;
                        t[size_t(s * 2 * VS_HALF + k)] = float(v);
                        }
                  }
            return t;
            }();
      return table;
      }

void Vst3Plugin::setPitch(double cents, double glideSeconds)
      {
      const double target = std::pow(2.0, cents / 1200.0);
      if (!d->varispeed) {
            if (std::fabs(cents) < 1e-6)
                  return;
            d->varispeed = true;
            d->fifo.assign(size_t(2 * (VS_HALF - 1)), 0.f);           // the history before the first frame
            d->pos = VS_HALF - 1;
            d->ratio = target;
            }
      d->targetRatio = target;
      if (glideSeconds <= 0 || d->sampleRate <= 0) {
            d->ratio = target;
            d->ratioStep = 0;
            }
      else
            d->ratioStep = (target - d->ratio) / (glideSeconds * d->sampleRate);
      }

double Vst3Plugin::pitch() const
      {
      return 1200.0 * std::log2(d->targetRatio);
      }

void Vst3Plugin::process(int frames, float* buffer)
      {
      if (!d->active || frames <= 0)
            return;
      if (!d->varispeed) {
            processDirect(frames, buffer);
            return;
            }
      // render what the frames will read: up to the last one's taps
      const double fastest = std::max(d->ratio, d->targetRatio);
      const long needed = long(std::floor(d->pos + fastest * frames)) + VS_HALF + 2;
      long have = long(d->fifo.size() / 2);
      while (have < needed) {
            const int n = int(std::min<long>(needed - have, d->maxBlock));
            d->scratch.assign(size_t(2 * n), 0.f);
            processDirect(n, d->scratch.data());
            d->fifo.insert(d->fifo.end(), d->scratch.begin(), d->scratch.end());
            have += n;
            }
      const std::vector<float>& kernel = varispeedKernel();
      const float* x = d->fifo.data();
      for (int j = 0; j < frames; ++j) {
            const long i0 = long(std::floor(d->pos));
            const double frac = d->pos - double(i0);
            const float* k = kernel.data() + size_t(std::lround(frac * VS_STEPS)) * 2 * VS_HALF;
            float l = 0, r = 0;
            const float* s = x + 2 * (i0 - VS_HALF + 1);
            for (int t = 0; t < 2 * VS_HALF; ++t) {
                  l += k[t] * s[2 * t];
                  r += k[t] * s[2 * t + 1];
                  }
            buffer[2 * j] += l;
            buffer[2 * j + 1] += r;
            d->pos += d->ratio;
            if (d->ratioStep != 0) {
                  d->ratio += d->ratioStep;
                  if ((d->ratioStep > 0 && d->ratio >= d->targetRatio) || (d->ratioStep < 0 && d->ratio <= d->targetRatio)) {
                        d->ratio = d->targetRatio;
                        d->ratioStep = 0;
                        }
                  }
            }
      // keep the taps' history only
      const long drop = long(std::floor(d->pos)) - (VS_HALF - 1);
      if (drop > 0) {
            d->fifo.erase(d->fifo.begin(), d->fifo.begin() + 2 * drop);
            d->pos -= double(drop);
            }
      }

void Vst3Plugin::processDirect(int frames, float* buffer)
      {
      {
            std::unique_lock<std::mutex> lock(d->handler.mutex, std::try_to_lock);
            if (lock.owns_lock()) {
                  for (const auto& e : d->handler.edits)
                        d->addParam(e.first, e.second);
                  d->handler.edits.clear();
                  }
      }
      int done = 0;
      while (done < frames) {
            const int n = std::min(frames - done, d->maxBlock);
            d->context.state = ProcessContext::kPlaying | ProcessContext::kTempoValid;
            d->context.sampleRate = d->sampleRate;
            d->context.tempo = 120.0;
            d->data.processMode = d->offline ? kOffline : kRealtime;
            d->data.numSamples = n;
            d->data.inputEvents = &d->events;
            d->data.inputParameterChanges = &d->inChanges;
            d->data.outputParameterChanges = &d->outChanges;
            d->data.processContext = &d->context;
            d->processor->process(d->data);
            d->context.projectTimeSamples += n;
            d->events.clear();
            d->inChanges.clearQueue();

            // the output, added (a mono bus to both sides)
            const AudioBusBuffers& out = d->data.outputs[d->outputBus];
            if (out.numChannels > 0 && out.channelBuffers32) {
                  const float* l = out.channelBuffers32[0];
                  const float* r = out.numChannels > 1 ? out.channelBuffers32[1] : l;
                  float* p = buffer + 2 * done;
                  for (int i = 0; i < n; ++i) {
                        p[2 * i] += l[i];
                        p[2 * i + 1] += r[i];
                        }
                  }

            // parameter changes of the processor, for the controller
            const int32 count = d->outChanges.getParameterCount();
            if (count > 0) {
                  std::unique_lock<std::mutex> lock(d->fromProcessorMutex, std::try_to_lock);
                  if (lock.owns_lock()) {
                        for (int32 i = 0; i < count; ++i) {
                              IParamValueQueue* q = d->outChanges.getParameterData(i);
                              int32 offset;
                              ParamValue value;
                              if (q && q->getPointCount() > 0 && q->getPoint(q->getPointCount() - 1, offset, value) == kResultOk) {
                                    d->fromProcessor.emplace_back(q->getParameterId(), value);
                                    if (d->reported.size() < 100000)
                                          d->reported.emplace_back(q->getParameterId(), value);
                                    }
                              }
                        }
                  }
            d->outChanges.clearQueue();
            done += n;
            }
      }

//---------------------------------------------------------
//   idle
//    GUI thread: what the processor changed, to the controller (its editor)
//---------------------------------------------------------

void Vst3Plugin::idle(std::mutex* processing)
      {
      std::vector<std::pair<ParamID, ParamValue>> changes;
      {
            std::lock_guard<std::mutex> lock(d->fromProcessorMutex);
            changes.swap(d->fromProcessor);
      }
      if (d->controller) {
            for (const auto& c : changes)
                  d->controller->setParamNormalized(c.first, c.second);
            }
      bool remap = false;
      {
            std::lock_guard<std::mutex> lock(d->handler.mutex);
            remap = d->handler.midiMappingChanged;
            d->handler.midiMappingChanged = false;
      }
      if (remap) {
            std::unique_lock<std::mutex> lock;
            if (processing)
                  lock = std::unique_lock<std::mutex>(*processing);   // (midi reads the mapping)
            d->mapControllers();
            }
      }

//---------------------------------------------------------
//   setOffline / setSampleRate
//---------------------------------------------------------

bool Vst3Plugin::setOffline(bool offline)
      {
      if (d->offline == offline)
            return true;
      d->stopProcessing();
      d->offline = offline;
      const bool ok = d->setup();
      d->startProcessing();
      return ok;
      }

bool Vst3Plugin::setSampleRate(double sampleRate)
      {
      if (d->sampleRate == sampleRate)
            return true;
      d->stopProcessing();
      d->sampleRate = sampleRate;
      const bool ok = d->setup();
      d->startProcessing();
      return ok;
      }

//---------------------------------------------------------
//   state
//    "MSV3", version, the component's state, the controller's state
//---------------------------------------------------------

static const char STATE_MAGIC[] = "MSV3";

QByteArray Vst3Plugin::state() const
      {
      QByteArray componentState;
      QByteArray controllerState;
      {
            IPtr<MemoryStream> s = owned(new MemoryStream);
            if (d->component->getState(s) == kResultOk)
                  componentState = QByteArray(s->getData(), int(s->getSize()));
      }
      if (d->controller) {
            IPtr<MemoryStream> s = owned(new MemoryStream);
            if (d->controller->getState(s) == kResultOk)
                  controllerState = QByteArray(s->getData(), int(s->getSize()));
            }
      QByteArray result;
      QDataStream ds(&result, QIODevice::WriteOnly);
      ds.writeRawData(STATE_MAGIC, 4);
      ds << quint32(1) << d->name << componentState << controllerState;
      return result;
      }

bool Vst3Plugin::setState(const QByteArray& state)
      {
      QDataStream ds(state);
      char magic[4];
      if (ds.readRawData(magic, 4) != 4 || std::memcmp(magic, STATE_MAGIC, 4) != 0)
            return false;
      quint32 version;
      QString name;
      QByteArray componentState;
      QByteArray controllerState;
      ds >> version >> name >> componentState >> controllerState;
      if (ds.status() != QDataStream::Ok || version != 1)
            return false;
      {
            IPtr<MemoryStream> s = owned(new MemoryStream(componentState.data(), componentState.size()));
            if (d->component->setState(s) != kResultOk)
                  return false;
            if (d->controller) {
                  s->seek(0, IBStream::kIBSeekSet, nullptr);
                  d->controller->setComponentState(s);
                  }
      }
      if (d->controller && !controllerState.isEmpty()) {
            IPtr<MemoryStream> s = owned(new MemoryStream(controllerState.data(), controllerState.size()));
            d->controller->setState(s);
            }
      d->mapControllers();
      d->titleIndexValid = false;
      return true;
      }

//---------------------------------------------------------
//   createEditor
//---------------------------------------------------------

IPlugView* Vst3Plugin::createEditor()
      {
      if (!d->controller)
            return nullptr;
      return d->controller->createView(ViewType::kEditor);
      }

//---------------------------------------------------------
//   keyNames
//---------------------------------------------------------

static QString fromString128(const Steinberg::Vst::String128 s)
      {
      return QString::fromUtf16(reinterpret_cast<const ushort*>(s)).trimmed();
      }

std::map<int, QString> Vst3Plugin::keyNames(QString* source) const
      {
      std::map<int, QString> names;
      QStringList said;
      if (!d->controller) {
            if (source)
                  *source = "no controller";
            return names;
            }
      // pitch names of the programs (Cubase's drum maps): the first program that has any
      FUnknownPtr<IUnitInfo> units(d->controller);
      if (!units)
            said << "no IUnitInfo";
      else {
            const int lists = units->getProgramListCount();
            int named = 0;
            for (int l = 0; l < lists && names.empty(); ++l) {
                  ProgramListInfo list {};
                  if (units->getProgramListInfo(l, list) != kResultTrue)
                        continue;
                  for (int p = 0; p < std::max(1, int(list.programCount)) && names.empty(); ++p) {
                        if (units->hasProgramPitchNames(list.id, p) != kResultTrue)
                              continue;
                        for (int key = 0; key < 128; ++key) {
                              String128 name {};
                              if (units->getProgramPitchName(list.id, p, int16(key), name) == kResultTrue) {
                                    const QString n = fromString128(name);
                                    if (!n.isEmpty()) {
                                          names[key] = n;
                                          ++named;
                                          }
                                    }
                              }
                        }
                  }
            said << QString("IUnitInfo: %1 program list(s), %2 pitch name(s)").arg(lists).arg(named);
            }
      // keyswitches
      FUnknownPtr<IKeyswitchController> keyswitches(d->controller);
      if (!keyswitches)
            said << "no IKeyswitchController";
      else {
            const int n = keyswitches->getKeyswitchCount(d->eventBus < 0 ? 0 : d->eventBus, 0);
            for (int k = 0; k < n; ++k) {
                  KeyswitchInfo info {};
                  if (keyswitches->getKeyswitchInfo(d->eventBus < 0 ? 0 : d->eventBus, 0, k, info) != kResultTrue)
                        continue;
                  const QString title = fromString128(info.title);
                  for (int key = std::max(0, int(info.keyswitchMin)); key <= std::min(127, int(info.keyswitchMax)); ++key)
                        if (!names.count(key))
                              names[key] = "KS " + title;
                  }
            said << QString("IKeyswitchController: %1 keyswitch(es)").arg(n);
            }
      if (source)
            *source = said.join("; ");
      return names;
      }

//---------------------------------------------------------
//   Extract: what the plug-in says about itself
//---------------------------------------------------------

static QString fromTChars(const TChar* s)
      {
      return QString::fromUtf16(reinterpret_cast<const ushort*>(s)).trimmed();
      }

static QString uidString(const TUID uid)
      {
      char8 s[64];
      FUID::fromTUID(uid).toString(s);
      return QString(s);
      }

static QJsonArray flagNames(int flags, const std::vector<std::pair<int, const char*>>& names)
      {
      QJsonArray a;
      for (const auto& n : names)
            if (flags & n.first)
                  a.append(n.second);
      return a;
      }

static QByteArray streamData(MemoryStream* s)
      {
      return QByteArray(s->getData(), int(s->getSize()));
      }

// a binary: its size, and its text when it is text (else the start in hex)
static QJsonObject blob(const QByteArray& data)
      {
      QJsonObject o;
      o["bytes"] = data.size();
      int printable = 0;
      for (char c : data)
            printable += (c >= 32 && c < 127) || c == '\n' || c == '\r' || c == '\t';
      if (!data.isEmpty() && printable > data.size() * 9 / 10)
            o["text"] = QString::fromUtf8(data.left(1 << 20));
      else
            o["start"] = QString(data.left(64).toHex(' '));
      return o;
      }

QByteArray Vst3Plugin::componentState() const
      {
      IPtr<MemoryStream> s = owned(new MemoryStream);
      return d->component->getState(s) == kResultOk ? streamData(s) : QByteArray();
      }

QByteArray Vst3Plugin::controllerState() const
      {
      if (!d->controller)
            return QByteArray();
      IPtr<MemoryStream> s = owned(new MemoryStream);
      return d->controller->getState(s) == kResultOk ? streamData(s) : QByteArray();
      }

std::vector<Vst3Plugin::Parameter> Vst3Plugin::parameters() const
      {
      std::vector<Parameter> params;
      if (!d->controller)
            return params;
      const int32 n = d->controller->getParameterCount();
      for (int32 i = 0; i < n; ++i) {
            ParameterInfo info {};
            if (d->controller->getParameterInfo(i, info) != kResultOk)
                  continue;
            params.push_back({ info.id, fromTChars(info.title), fromTChars(info.units), int(info.stepCount), int(info.flags), int(info.unitId) });
            }
      return params;
      }

// loosely: letters and digits only, lower case, and a slot number in front left out ("Mic 1 level" =
// "MIC 1 Level" = "07 Mic 1 level"; Kontakt's own titles aren't known here)
static QString looseTitle(const QString& t)
      {
      static const QRegularExpression slot("^\\s*#?\\d+\\s*[:.)-]?\\s+");
      static const QRegularExpression other("[^a-z0-9]");
      QString s = t.toLower();
      s.remove(slot);
      s.remove(other);
      return s;
      }

long Vst3Plugin::parameterId(const QString& title) const
      {
      const QString want = looseTitle(title);
      if (want.isEmpty())
            return -1;
      {
            std::lock_guard<std::mutex> lock(d->handler.mutex);
            if (d->handler.titlesChanged)
                  d->titleIndexValid = false;
            d->handler.titlesChanged = false;
      }
      for (int pass = 0; pass < 2; ++pass) {
            if (!d->titleIndexValid || (pass == 1 && d->titleIndexAge.elapsed() > 1000)) {
                  d->titleIndex.clear();
                  for (const Parameter& p : parameters()) {
                        const QString t = looseTitle(p.title);
                        if (!t.isEmpty() && !d->titleIndex.count(t))          // (the first, as before)
                              d->titleIndex[t] = p.id;
                        }
                  d->titleIndexValid = true;
                  d->titleIndexAge.start();
                  }
            auto i = d->titleIndex.find(want);
            if (i != d->titleIndex.end())
                  return long(i->second);
            }
      return -1;
      }

double Vst3Plugin::parameter(unsigned id) const
      {
      return d->controller ? d->controller->getParamNormalized(id) : 0.0;
      }

QString Vst3Plugin::parameterText(unsigned id, double normalized) const
      {
      String128 text {};
      if (!d->controller || d->controller->getParamStringByValue(id, normalized, text) != kResultOk)
            return QString();
      return fromTChars(text);
      }

void Vst3Plugin::setParameter(unsigned id, double normalized)
      {
      normalized = qBound(0.0, normalized, 1.0);
      d->addParam(id, normalized);
      if (d->controller)
            d->controller->setParamNormalized(id, normalized);
      }

long Vst3Plugin::controllerParameter(int channel, int cc) const
      {
      if (channel < 0 || channel > 15 || cc < 0 || cc >= 130 || d->ccParam.empty())
            return -1;
      const ParamID id = d->ccParam[channel * 130 + cc];
      return id == kNoParamId ? -1 : long(id);
      }

std::vector<std::pair<unsigned, double>> Vst3Plugin::takeReported()
      {
      std::vector<std::pair<unsigned, double>> r;
      {
            std::lock_guard<std::mutex> lock(d->fromProcessorMutex);
            for (const auto& c : d->reported)
                  r.emplace_back(c.first, c.second);
            d->reported.clear();
      }
      {
            std::lock_guard<std::mutex> lock(d->handler.mutex);
            for (const auto& c : d->handler.reported)
                  r.emplace_back(c.first, c.second);
            d->handler.reported.clear();
      }
      return r;
      }

QJsonObject Vst3Plugin::hostQueries()
      {
      QJsonObject o;
      std::lock_guard<std::mutex> lock(queryMutex());
      for (const auto& q : queryLog())
            o[q.first] = q.second;
      return o;
      }

//---------------------------------------------------------
//   describe
//---------------------------------------------------------

static const char* ccName(int cc)
      {
      switch (cc) {
            case kAfterTouch:        return "channel pressure";
            case kPitchBend:         return "pitch bend";
            case kCtrlProgramChange: return "program change";
            case kCtrlPolyPressure:  return "poly pressure";
            case kCtrlQuarterFrame:  return "quarter frame";
            default:                 return nullptr;
            }
      }

QJsonObject Vst3Plugin::describe() const
      {
      QJsonObject out;
      out["path"] = d->path;
      out["name"] = d->name;

      // the module: its moduleinfo.json, snapshots, factory and classes
      {
            QJsonObject module;
            if (d->module) {
                  module["name"] = QString::fromStdString(d->module->getName());
                  module["path"] = QString::fromStdString(d->module->getPath());
                  if (auto info = VST3::Hosting::Module::getModuleInfoPath(d->module->getPath())) {
                        QFile f(QString::fromStdString(*info));
                        if (f.open(QIODevice::ReadOnly)) {
                              const QByteArray json = f.readAll();
                              QJsonParseError err;
                              const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
                              // (moduleinfo.json is JSON5: comments and trailing commas may fail)
                              if (err.error == QJsonParseError::NoError)
                                    module["moduleInfo"] = doc.object();
                              else
                                    module["moduleInfoText"] = QString::fromUtf8(json);
                              }
                        }
                  QJsonArray snapshots;
                  for (const auto& snap : VST3::Hosting::Module::getSnapshots(d->module->getPath()))
                        for (const auto& image : snap.images)
                              snapshots.append(QString("%1 x%2: %3").arg(QString::fromStdString(snap.uid.toString()))
                                               .arg(image.scaleFactor).arg(QString::fromStdString(image.path)));
                  module["snapshots"] = snapshots;

                  const VST3::Hosting::PluginFactory& factory = d->module->getFactory();
                  const VST3::Hosting::FactoryInfo fi = factory.info();
                  QJsonObject f;
                  f["vendor"] = QString::fromStdString(fi.vendor());
                  f["url"] = QString::fromStdString(fi.url());
                  f["email"] = QString::fromStdString(fi.email());
                  f["flags"] = flagNames(fi.flags(), { { PFactoryInfo::kClassesDiscardable, "classesDiscardable" },
                                                        { PFactoryInfo::kLicenseCheck, "licenseCheck" },
                                                        { PFactoryInfo::kComponentNonDiscardable, "componentNonDiscardable" },
                                                        { PFactoryInfo::kUnicode, "unicode" } });
                  module["factory"] = f;
                  QJsonArray classes;
                  for (const VST3::Hosting::ClassInfo& ci : factory.classInfos()) {
                        QJsonObject c;
                        c["cid"] = QString::fromStdString(ci.ID().toString());
                        c["name"] = QString::fromStdString(ci.name());
                        c["category"] = QString::fromStdString(ci.category());
                        c["subCategories"] = QString::fromStdString(ci.subCategoriesString());
                        c["vendor"] = QString::fromStdString(ci.vendor());
                        c["version"] = QString::fromStdString(ci.version());
                        c["sdkVersion"] = QString::fromStdString(ci.sdkVersion());
                        c["cardinality"] = int(ci.cardinality());
                        c["classFlags"] = flagNames(int(ci.classFlags()), { { Vst::kDistributable, "distributable" },
                                                                          { Vst::kSimpleModeSupported, "simpleModeSupported" } });
                        // the compatibility class: which other plug-ins (VST 2 …) this one replaces
                        if (ci.category() == kPluginCompatibilityClass) {
                              if (IPtr<IPluginCompatibility> compat = factory.createInstance<IPluginCompatibility>(ci.ID())) {
                                    IPtr<MemoryStream> st = owned(new MemoryStream);
                                    if (compat->getCompatibilityJSON(st) == kResultOk)
                                          c["compatibility"] = QString::fromUtf8(streamData(st));
                                    }
                              }
                        classes.append(c);
                        }
                  module["classes"] = classes;
                  }
            out["module"] = module;
      }

      // which optional interfaces the component and the controller have
      {
            auto has = [](FUnknown* obj, const FUID& iid) {
                  void* o = nullptr;
                  if (!obj || obj->queryInterface(iid.toTUID(), &o) != kResultOk || !o)
                        return false;
                  static_cast<FUnknown*>(o)->release();
                  return true;
                  };
            QJsonArray comp;
            QJsonArray ctrl;
            for (const NamedInterface& n : interfaceNames()) {
                  if (has(d->component, *n.iid))
                        comp.append(n.name);
                  if (has(d->controller, *n.iid))
                        ctrl.append(n.name);
                  }
            QJsonObject ifs;
            ifs["component"] = comp;
            ifs["controller"] = ctrl;
            ifs["singleComponent"] = d->controller && static_cast<FUnknown*>(d->controller.get()) == static_cast<FUnknown*>(d->component.get());
            out["interfaces"] = ifs;
      }

      // the component: controller class, buses, processing
      {
            QJsonObject c;
            TUID cid {};
            if (d->component->getControllerClassId(cid) == kResultOk)
                  c["controllerClassId"] = uidString(cid);
            QJsonArray buses;
            for (int media : { kAudio, kEvent }) {
                  for (int dir : { kInput, kOutput }) {
                        const int32 n = d->component->getBusCount(media, dir);
                        for (int32 i = 0; i < n; ++i) {
                              BusInfo info {};
                              if (d->component->getBusInfo(media, dir, i, info) != kResultOk)
                                    continue;
                              QJsonObject b;
                              b["media"] = media == kAudio ? "audio" : "event";
                              b["direction"] = dir == kInput ? "input" : "output";
                              b["index"] = int(i);
                              b["name"] = fromTChars(info.name);
                              b["type"] = info.busType == kMain ? "main" : "aux";
                              b["channels"] = int(info.channelCount);
                              b["flags"] = flagNames(int(info.flags), { { BusInfo::kDefaultActive, "defaultActive" },
                                                                        { BusInfo::kIsControlVoltage, "controlVoltage" } });
                              if (media == kAudio) {
                                    SpeakerArrangement arr = 0;
                                    if (d->processor->getBusArrangement(dir, i, arr) == kResultOk)
                                          b["arrangement"] = QString(SpeakerArr::getSpeakerArrangementString(arr, true));
                                    }
                              b["usedByMuseScore"] = (media == kEvent && dir == kInput && i == d->eventBus)
                                                     || (media == kAudio && dir == kOutput && i == d->outputBus);
                              buses.append(b);
                              }
                        }
                  }
            c["buses"] = buses;
            c["latencySamples"] = int(d->processor->getLatencySamples());
            const uint32 tail = d->processor->getTailSamples();
            c["tailSamples"] = tail == kInfiniteTail ? QJsonValue("infinite") : QJsonValue(double(tail));
            c["canProcess64bit"] = d->processor->canProcessSampleSize(kSample64) == kResultTrue;
            if (FUnknownPtr<IProcessContextRequirements> req = FUnknownPtr<IProcessContextRequirements>(d->processor)) {
                  using R = IProcessContextRequirements;
                  c["processContextRequirements"] = flagNames(int(req->getProcessContextRequirements()), {
                        { R::kNeedSystemTime, "systemTime" }, { R::kNeedContinousTimeSamples, "continuousTimeSamples" },
                        { R::kNeedProjectTimeMusic, "projectTimeMusic" }, { R::kNeedBarPositionMusic, "barPositionMusic" },
                        { R::kNeedCycleMusic, "cycleMusic" }, { R::kNeedSamplesToNextClock, "samplesToNextClock" },
                        { R::kNeedTempo, "tempo" }, { R::kNeedTimeSignature, "timeSignature" }, { R::kNeedChord, "chord" },
                        { R::kNeedFrameRate, "frameRate" }, { R::kNeedTransportState, "transportState" } });
                  }
            if (FUnknownPtr<IPrefetchableSupport> pre = FUnknownPtr<IPrefetchableSupport>(d->component)) {
                  PrefetchableSupport ps = kIsNeverPrefetchable;
                  if (pre->getPrefetchableSupport(ps) == kResultOk)
                        c["prefetchable"] = ps == kIsYetPrefetchable ? "yes" : ps == kIsNotYetPrefetchable ? "not yet" : "never";
                  }
            c["state"] = blob(componentState());
            out["component"] = c;
      }

      if (!d->controller)
            return out;
      IEditController* ctl = d->controller;
      const int32 bus = d->eventBus < 0 ? 0 : d->eventBus;
      const int32 eventBuses = std::max(1, int(d->component->getBusCount(kEvent, kInput)));

      // parameters, with the text of their values
      std::map<ParamID, QString> titles;
      {
            QJsonArray params;
            const int32 n = ctl->getParameterCount();
            for (int32 i = 0; i < n; ++i) {
                  ParameterInfo info {};
                  if (ctl->getParameterInfo(i, info) != kResultOk)
                        continue;
                  titles[info.id] = fromTChars(info.title);
                  QJsonObject p;
                  p["index"] = int(i);
                  p["id"] = double(info.id);
                  p["title"] = fromTChars(info.title);
                  p["shortTitle"] = fromTChars(info.shortTitle);
                  p["units"] = fromTChars(info.units);
                  p["stepCount"] = int(info.stepCount);
                  p["default"] = info.defaultNormalizedValue;
                  p["unitId"] = int(info.unitId);
                  p["flags"] = flagNames(int(info.flags), {
                        { ParameterInfo::kCanAutomate, "canAutomate" }, { ParameterInfo::kIsReadOnly, "readOnly" },
                        { ParameterInfo::kIsWrapAround, "wrapAround" }, { ParameterInfo::kIsList, "list" },
                        { ParameterInfo::kIsHidden, "hidden" }, { ParameterInfo::kIsProgramChange, "programChange" },
                        { ParameterInfo::kIsBypass, "bypass" } });
                  const ParamValue now = ctl->getParamNormalized(info.id);
                  p["value"] = now;
                  p["valueText"] = parameterText(info.id, now);
                  p["defaultText"] = parameterText(info.id, info.defaultNormalizedValue);
                  p["plainMin"] = ctl->normalizedParamToPlain(info.id, 0.0);
                  p["plainMax"] = ctl->normalizedParamToPlain(info.id, 1.0);
                  // the text of its values: every step (up to 128), else 17 points; runs of one text as one
                  const int points = info.stepCount > 0 && info.stepCount <= 127 ? int(info.stepCount) : 16;
                  QJsonArray texts;
                  QString last;
                  for (int k = 0; k <= points; ++k) {
                        const double v = double(k) / points;
                        const QString t = parameterText(info.id, v);
                        if (k > 0 && t == last)
                              continue;
                        texts.append(QJsonArray { info.stepCount > 0 && info.stepCount <= 127 ? QJsonValue(k) : QJsonValue(v), t });
                        last = t;
                        }
                  p["texts"] = texts;
                  params.append(p);
                  }
            out["parameters"] = params;
      }

      // the MIDI controllers' parameters (IMidiMapping), per event bus and channel
      if (FUnknownPtr<IMidiMapping> mapping = FUnknownPtr<IMidiMapping>(ctl)) {
            QJsonObject m;
            for (int32 b = 0; b < eventBuses; ++b) {
                  QJsonObject busMap;
                  for (int ch = 0; ch < 16; ++ch) {
                        QJsonObject chMap;
                        for (int cc = 0; cc < kCountCtrlNumber; ++cc) {
                              ParamID id = kNoParamId;
                              if (mapping->getMidiControllerAssignment(b, int16(ch), CtrlNumber(cc), id) != kResultTrue)
                                    continue;
                              QJsonObject e;
                              e["id"] = double(id);
                              e["title"] = titles.count(id) ? titles[id] : QString("(not a listed parameter)");
                              if (const char* n = ccName(cc))
                                    e["controller"] = n;
                              chMap[QString::number(cc)] = e;
                              }
                        if (!chMap.isEmpty())
                              busMap[QString("channel %1").arg(ch + 1)] = chMap;
                        }
                  m[QString("bus %1").arg(b)] = busMap;
                  }
            out["midiMapping"] = m;
            }
      if (FUnknownPtr<IMidiMapping2> mapping2 = FUnknownPtr<IMidiMapping2>(ctl)) {
            QJsonObject m;
            for (int dir : { kInput, kOutput }) {
                  const uint32 n1 = mapping2->getNumMidi1ControllerAssignments(BusDirections(dir));
                  std::vector<Midi1ControllerParamIDAssignment> a1(n1);
                  Midi1ControllerParamIDAssignmentList l1 { n1, a1.data() };
                  QJsonArray list1;
                  if (n1 && mapping2->getMidi1ControllerAssignments(BusDirections(dir), l1) == kResultTrue)
                        for (const auto& a : a1)
                              list1.append(QJsonArray { int(a.busIndex), int(a.channel) + 1, int(a.controller), double(a.pId),
                                                        titles.count(a.pId) ? titles[a.pId] : QString() });
                  const uint32 n2 = mapping2->getNumMidi2ControllerAssignments(BusDirections(dir));
                  std::vector<Midi2ControllerParamIDAssignment> a2(n2);
                  Midi2ControllerParamIDAssignmentList l2 { n2, a2.data() };
                  QJsonArray list2;
                  if (n2 && mapping2->getMidi2ControllerAssignments(BusDirections(dir), l2) == kResultTrue)
                        for (const auto& a : a2)
                              list2.append(QJsonArray { int(a.busIndex), int(a.channel) + 1,
                                                        Midi2Controller::isRegisteredController(a.controller) ? "registered" : "assignable",
                                                        int(Midi2Controller::bank(a.controller)), int(Midi2Controller::index(a.controller)),
                                                        double(a.pId), titles.count(a.pId) ? titles[a.pId] : QString() });
                  const char* dirName = dir == kInput ? "input" : "output";
                  m[QString("%1 midi1 [bus, channel, cc, id, title]").arg(dirName)] = list1;
                  m[QString("%1 midi2 [bus, channel, kind, bank, index, id, title]").arg(dirName)] = list2;
                  }
            out["midiMapping2"] = m;
            }

      // units and programs
      if (FUnknownPtr<IUnitInfo> units = FUnknownPtr<IUnitInfo>(ctl)) {
            QJsonObject u;
            QJsonArray list;
            for (int32 i = 0; i < units->getUnitCount(); ++i) {
                  UnitInfo info {};
                  if (units->getUnitInfo(i, info) != kResultOk)
                        continue;
                  QJsonObject o;
                  o["id"] = int(info.id);
                  o["parent"] = int(info.parentUnitId);
                  o["name"] = fromTChars(info.name);
                  o["programListId"] = int(info.programListId);
                  list.append(o);
                  }
            u["units"] = list;
            u["selectedUnit"] = int(units->getSelectedUnit());
            QJsonObject byBus;
            for (int32 b = 0; b < eventBuses; ++b)
                  for (int ch = 0; ch < 16; ++ch) {
                        UnitID id = kNoParentUnitId;
                        if (units->getUnitByBus(kEvent, kInput, b, ch, id) == kResultTrue)
                              byBus[QString("bus %1 channel %2").arg(b).arg(ch + 1)] = int(id);
                        }
            u["unitByBus"] = byBus;
            FUnknownPtr<IProgramListData> programData(ctl);
            QJsonArray lists;
            for (int32 l = 0; l < units->getProgramListCount(); ++l) {
                  ProgramListInfo info {};
                  if (units->getProgramListInfo(l, info) != kResultOk)
                        continue;
                  QJsonObject o;
                  o["id"] = int(info.id);
                  o["name"] = fromTChars(info.name);
                  o["programCount"] = int(info.programCount);
                  const bool dataSupported = programData && programData->programDataSupported(info.id) == kResultTrue;
                  o["programDataSupported"] = dataSupported;
                  QJsonArray programs;
                  for (int32 p = 0; p < std::min(int(info.programCount), 4096); ++p) {
                        QJsonObject po;
                        String128 name {};
                        if (units->getProgramName(info.id, p, name) == kResultOk)
                              po["name"] = fromTChars(name);
                        QJsonObject attrs;
                        for (const char* key : { PresetAttributes::kPlugInName, PresetAttributes::kPlugInCategory,
                                                 PresetAttributes::kInstrument, PresetAttributes::kStyle,
                                                 PresetAttributes::kCharacter, PresetAttributes::kStateType,
                                                 PresetAttributes::kFilePathStringType, PresetAttributes::kName,
                                                 PresetAttributes::kFileName }) {
                              String128 v {};
                              if (units->getProgramInfo(info.id, p, key, v) == kResultOk)
                                    attrs[key] = fromTChars(v);
                              }
                        if (!attrs.isEmpty())
                              po["info"] = attrs;
                        if (p < 512 && units->hasProgramPitchNames(info.id, p) == kResultTrue) {
                              QJsonObject pitches;
                              for (int key = 0; key < 128; ++key) {
                                    String128 pn {};
                                    if (units->getProgramPitchName(info.id, p, int16(key), pn) == kResultTrue && !fromTChars(pn).isEmpty())
                                          pitches[QString::number(key)] = fromTChars(pn);
                                    }
                              po["pitchNames"] = pitches;
                              }
                        if (dataSupported && p < 512) {
                              IPtr<MemoryStream> st = owned(new MemoryStream);
                              if (programData->getProgramData(info.id, p, st) == kResultOk)
                                    po["data"] = blob(streamData(st));
                              }
                        programs.append(po);
                        }
                  o["programs"] = programs;
                  lists.append(o);
                  }
            u["programLists"] = lists;
            if (FUnknownPtr<IUnitData> unitData = FUnknownPtr<IUnitData>(ctl)) {
                  QJsonObject data;
                  for (const QJsonValue& v : list) {
                        const int id = v.toObject().value("id").toInt();
                        if (unitData->unitDataSupported(id) != kResultTrue)
                              continue;
                        IPtr<MemoryStream> st = owned(new MemoryStream);
                        if (unitData->getUnitData(id, st) == kResultOk)
                              data[QString::number(id)] = blob(streamData(st));
                        }
                  u["unitData"] = data;
                  }
            out["units"] = u;
            }

      // per event bus and channel: keyswitches, note expressions, physical UI, orchestral articulations
      {
            FUnknownPtr<IKeyswitchController> keyswitches(ctl);
            FUnknownPtr<INoteExpressionController> expressions(ctl);
            FUnknownPtr<INoteExpressionPhysicalUIMapping> physical(ctl);
            FUnknownPtr<NoteOnOrchestralArticulation::IInfo> orchestral(ctl);
            QJsonObject channels;
            for (int32 b = 0; b < eventBuses; ++b) {
                  for (int ch = 0; ch < 16; ++ch) {
                        QJsonObject c;
                        if (keyswitches) {
                              QJsonArray ks;
                              const int32 n = keyswitches->getKeyswitchCount(b, int16(ch));
                              for (int32 k = 0; k < n; ++k) {
                                    KeyswitchInfo info {};
                                    if (keyswitches->getKeyswitchInfo(b, int16(ch), k, info) != kResultTrue)
                                          continue;
                                    QJsonObject o;
                                    o["type"] = int(info.typeId);
                                    o["title"] = fromTChars(info.title);
                                    o["shortTitle"] = fromTChars(info.shortTitle);
                                    o["keyMin"] = int(info.keyswitchMin);
                                    o["keyMax"] = int(info.keyswitchMax);
                                    o["keyRemapped"] = int(info.keyRemapped);
                                    o["unitId"] = int(info.unitId);
                                    o["flags"] = int(info.flags);
                                    ks.append(o);
                                    }
                              if (!ks.isEmpty())
                                    c["keyswitches"] = ks;
                              }
                        if (expressions) {
                              QJsonArray ne;
                              const int32 n = expressions->getNoteExpressionCount(b, int16(ch));
                              for (int32 k = 0; k < n; ++k) {
                                    NoteExpressionTypeInfo info {};
                                    if (expressions->getNoteExpressionInfo(b, int16(ch), k, info) != kResultTrue)
                                          continue;
                                    QJsonObject o;
                                    o["typeId"] = double(info.typeId);
                                    o["title"] = fromTChars(info.title);
                                    o["shortTitle"] = fromTChars(info.shortTitle);
                                    o["units"] = fromTChars(info.units);
                                    o["unitId"] = int(info.unitId);
                                    o["default"] = info.valueDesc.defaultValue;
                                    o["min"] = info.valueDesc.minimum;
                                    o["max"] = info.valueDesc.maximum;
                                    o["stepCount"] = int(info.valueDesc.stepCount);
                                    if (info.flags & NoteExpressionTypeInfo::kAssociatedParameterIDValid)
                                          o["parameter"] = double(info.associatedParameterId);
                                    o["flags"] = flagNames(int(info.flags), {
                                          { NoteExpressionTypeInfo::kIsBipolar, "bipolar" }, { NoteExpressionTypeInfo::kIsOneShot, "oneShot" },
                                          { NoteExpressionTypeInfo::kIsAbsolute, "absolute" } });
                                    QJsonArray texts;
                                    for (double v : { 0.0, 0.5, 1.0 }) {
                                          String128 t {};
                                          if (expressions->getNoteExpressionStringByValue(b, int16(ch), info.typeId, v, t) == kResultTrue)
                                                texts.append(QJsonArray { v, fromTChars(t) });
                                          }
                                    o["texts"] = texts;
                                    ne.append(o);
                                    }
                              if (!ne.isEmpty())
                                    c["noteExpressions"] = ne;
                              }
                        if (physical) {
                              PhysicalUIMap maps[kPUITypeCount];
                              for (int k = 0; k < kPUITypeCount; ++k)
                                    maps[k] = { PhysicalUITypeID(k), kInvalidTypeID };
                              PhysicalUIMapList list { kPUITypeCount, maps };
                              if (physical->getPhysicalUIMapping(b, int16(ch), list) == kResultTrue) {
                                    QJsonObject pm;
                                    const char* names[] = { "x", "y", "pressure" };
                                    for (int k = 0; k < kPUITypeCount; ++k)
                                          if (maps[k].noteExpressionTypeID != kInvalidTypeID)
                                                pm[names[k]] = double(maps[k].noteExpressionTypeID);
                                    if (!pm.isEmpty())
                                          c["physicalUI"] = pm;
                                    }
                              }
                        if (orchestral) {
                              NoteOnOrchestralArticulation::ClassificationVariations v {};
                              if (orchestral->getVariationsInfo(b, int16(ch), v) == kResultTrue) {
                                    QJsonObject oa;
                                    for (int k = 0; k < NoteOnOrchestralArticulation::ClassificationVariations::kNumClassifications; ++k) {
                                          QJsonArray sub;
                                          bool any = false;
                                          for (int j = 0; j < NoteOnOrchestralArticulation::Variations::kNumSubClasses; ++j) {
                                                sub.append(int(v.classification[k].variation[j]));
                                                any = any || v.classification[k].variation[j];
                                                }
                                          if (any)
                                                oa[QString::number(k)] = sub;
                                          }
                                    c["orchestralArticulations"] = oa;
                                    }
                              }
                        if (!c.isEmpty())
                              channels[QString("bus %1 channel %2").arg(b).arg(ch + 1)] = c;
                        }
                  }
            out["channels"] = channels;
      }

      // parameters by function (IParameterFunctionName)
      if (FUnknownPtr<IParameterFunctionName> fn = FUnknownPtr<IParameterFunctionName>(ctl)) {
            QJsonObject o;
            for (const char* name : { FunctionNameType::kDryWetMix, FunctionNameType::kRandomize, FunctionNameType::kRandomizeAroundCurrent,
                                      FunctionNameType::kLowLatencyMode, FunctionNameType::kPanPosCenterX,
                                      FunctionNameType::kPanPosCenterY, FunctionNameType::kPanPosCenterZ,
                                      FunctionNameType::kCompGainReduction }) {
                  ParamID id = kNoParamId;
                  if (fn->getParameterIDFromFunctionName(kRootUnitId, name, id) == kResultTrue)
                        o[name] = QString("%1 %2").arg(id).arg(titles.count(id) ? titles[id] : QString());
                  }
            out["parameterFunctions"] = o;
            }

      // a remote control's representation (IXmlRepresentationController)
      if (FUnknownPtr<IXmlRepresentationController> xml = FUnknownPtr<IXmlRepresentationController>(ctl)) {
            RepresentationInfo info;
            std::strncpy(info.host, "MuseScore", RepresentationInfo::kNameSize - 1);
            IPtr<MemoryStream> st = owned(new MemoryStream);
            if (xml->getXmlRepresentationStream(info, st) == kResultTrue)
                  out["xmlRepresentation"] = QString::fromUtf8(streamData(st));
            }

      // the editor
      if (IPtr<IPlugView> view = owned(ctl->createView(ViewType::kEditor))) {
            QJsonObject e;
            ViewRect r;
            if (view->getSize(&r) == kResultOk)
                  e["size"] = QString("%1 x %2").arg(r.getWidth()).arg(r.getHeight());
            e["canResize"] = view->canResize() == kResultTrue;
            QJsonArray platforms;
            for (const char* t : { kPlatformTypeHWND, kPlatformTypeNSView, kPlatformTypeHIView, kPlatformTypeX11EmbedWindowID })
                  if (view->isPlatformTypeSupported(t) == kResultTrue)
                        platforms.append(t);
            e["platforms"] = platforms;
            e["contentScaleSupport"] = bool(FUnknownPtr<IPlugViewContentScaleSupport>(view));
            out["editor"] = e;
            }
      else
            out["editor"] = QJsonValue::Null;

      out["controllerState"] = blob(controllerState());
      out["hostQueries"] = hostQueries();
      return out;
      }

} // namespace Ms
