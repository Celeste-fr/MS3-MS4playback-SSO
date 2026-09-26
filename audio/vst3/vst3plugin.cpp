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

#include <algorithm>
#include <cstring>
#include <map>

#include <QDataStream>
#include <QDebug>
#include <QFileInfo>

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstnoteexpression.h"
#include "pluginterfaces/vst/ivstunits.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
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

class MuseScoreHostApplication : public HostApplication {
   public:
      tresult PLUGIN_API getName(String128 name) override
            {
            const char16_t* n = u"MuseScore";
            std::memcpy(name, n, (std::char_traits<char16_t>::length(n) + 1) * sizeof(char16_t));
            return kResultTrue;
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
   public:
      std::mutex mutex;
      std::vector<std::pair<ParamID, ParamValue>> edits;
      bool midiMappingChanged { false };

      tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
            {
            std::lock_guard<std::mutex> lock(mutex);
            edits.emplace_back(id, value);
            return kResultOk;
            }
      tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API restartComponent(int32 flags) override
            {
            if (flags & kMidiCCAssignmentChanged) {
                  std::lock_guard<std::mutex> lock(mutex);
                  midiMappingChanged = true;
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
      bool offline { false };
      bool active { false };
      HostProcessData data;
      Steinberg::Vst::EventList events { 512 };
      ParameterChanges inChanges { 512 };
      ParameterChanges outChanges { 512 };
      ProcessContext context {};
      std::vector<std::pair<ParamID, ParamValue>> fromProcessor;      // for the controller (idle)
      std::mutex fromProcessorMutex;

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

void Vst3Plugin::midi(int type, int channel, int a, int b)
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

void Vst3Plugin::process(int frames, float* buffer)
      {
      if (!d->active)
            return;
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
                              if (q && q->getPointCount() > 0 && q->getPoint(q->getPointCount() - 1, offset, value) == kResultOk)
                                    d->fromProcessor.emplace_back(q->getParameterId(), value);
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

void Vst3Plugin::idle()
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
      if (remap)
            d->mapControllers();
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

} // namespace Ms
