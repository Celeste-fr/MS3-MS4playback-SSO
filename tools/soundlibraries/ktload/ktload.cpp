// ktload: load-time experiments with Kontakt on the Windows VM (load-profile, 2026-10-16).
//
// Build on Linux with msvc-wine (~/msvc): CC=CXX=<msvc>/bin/x64/cl cmake -G Ninja -B build -DCMAKE_SYSTEM_NAME=Windows
// -DCMAKE_BUILD_TYPE=Release -DCMAKE_LINKER=<msvc>/bin/x64/link -DCMAKE_MT=<msvc>/bin/x64/mt -DCMAKE_RC_COMPILER=<msvc>/bin/x64/rc
// tools/soundlibraries/ktload && ninja -C build ktload. The states: MuseScore's setups as SoundLibraryHost loads them
// (voice limit, tunings), never committed (Spitfire's patches).
//
//   ktload <plugin.vst3> <out.txt> [--pump <s>] [--save-after <s>] [--no-process] [--offline] [--inactive]
//          [--dll <x.dll>] [--trace <x.dll> <dump.txt>] <state.vst3state>[=<save.vst3state>] | @<state.vst3state> ...
//
// Each state goes into an instance of its own, one after the other on this thread (as MuseScore loads them),
// timed: instance created, setState (component), then 47 ms of processing (MuseScore's settle). All instances are
// kept and processed in real time (silence in, output dropped; --no-process: not processed) for --pump seconds while the
// process's private memory and CPU time are logged each second (Kontakt loads the samples after setState). A state given "=<file>" is saved
// (component + controller, MuseScore's MSV3 form) --save-after seconds after its load. "@<state>": setState into the
// previous instance (no new one). Each setState's line also gives its user / kernel CPU time, page faults and private
// memory growth.
//
// sso-load (2026-10-23): --dll loads a DLL before Kontakt's module (tools/soundlibraries/kheap: the allocator
// experiment); --trace also calls its exported ktmark(phase) at each step and ktdump(file) at the end (kheap's counts,
// or the sampling tracer of private/sso-load-profile-work). --offline: kOffline (MuseScore's renders) instead of
// kRealtime; --inactive: setState before setActive / setProcessing (measured: no difference, 2026-10-23).

#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

class Handler : public U::ImplementsNonDestroyable<U::Directly<IComponentHandler>> {
   public:
      tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
      tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
      };
static Handler g_handler;

static uint32_t be32(const unsigned char* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }

static bool readState(const std::string& path, std::string& comp, std::string& ctrl)
      {
      std::ifstream f(path, std::ios::binary);
      std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
      if (d.size() < 12 || d.compare(0, 4, "MSV3") != 0)
            return false;
      const unsigned char* p = (const unsigned char*)d.data();
      size_t pos = 8;
      uint32_t n = be32(p + pos); pos += 4;
      if (n != 0xffffffff) pos += n;
      n = be32(p + pos); pos += 4;
      comp = d.substr(pos, n); pos += n;
      n = be32(p + pos); pos += 4;
      if (n != 0xffffffff && n > 0) ctrl = d.substr(pos, n);
      return true;
      }

static void writeState(const std::string& path, IComponent* component, IEditController* controller)
      {
      auto be = [](std::string& d, uint32_t v) { for (int k = 3; k >= 0; --k) d.push_back(char((v >> (8 * k)) & 0xff)); };
      IPtr<MemoryStream> cs = owned(new MemoryStream);
      component->getState(cs);
      IPtr<MemoryStream> ks = owned(new MemoryStream);
      if (controller) controller->getState(ks);
      std::string d = "MSV3";
      be(d, 1);
      const char16_t name[] = u"Kontakt 8";
      be(d, 9 * 2);
      for (int i = 0; i < 9; ++i) { d.push_back(char(name[i] >> 8)); d.push_back(char(name[i] & 0xff)); }
      be(d, uint32_t(cs->getSize())); d.append(cs->getData(), size_t(cs->getSize()));
      be(d, uint32_t(ks->getSize())); d.append(ks->getData(), size_t(ks->getSize()));
      FILE* f = fopen(path.c_str(), "wb"); fwrite(d.data(), 1, d.size(), f); fclose(f);
      }

static double privateMb()
      {
      PROCESS_MEMORY_COUNTERS_EX pmc {};
      GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
      return pmc.PrivateUsage / 1048576.0;
      }

static double cpuSeconds()
      {
      FILETIME c, e, k, u;
      GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
      auto s = [](FILETIME f) { return ((unsigned long long)f.dwHighDateTime << 32 | f.dwLowDateTime) / 1e7; };
      return s(k) + s(u);
      }

struct Instance {
      IPtr<PlugProvider> provider;
      IPtr<IComponent> component;
      IPtr<IEditController> controller;
      IAudioProcessor* processor { nullptr };
      HostProcessData data;
      int outBus { 0 };
      std::string save;
      double loadedAt { 0 };
      bool saved { false };
      };

static const int RATE = 44100, BLOCK = 512;
typedef void (*mark_t)(const char*);
static std::vector<mark_t> g_marks, g_dumps;
static std::vector<std::string> g_dumpPaths;
static void mark(const std::string& m) { for (auto f : g_marks) f(m.c_str()); }
static void cpuSplit(double& user, double& kernel)
      {
      FILETIME c, e, k, u;
      GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
      auto s = [](FILETIME f) { return ((unsigned long long)f.dwHighDateTime << 32 | f.dwLowDateTime) / 1e7; };
      user = s(u); kernel = s(k);
      }
static double pageFaults()
      {
      PROCESS_MEMORY_COUNTERS_EX pmc {};
      GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc));
      return pmc.PageFaultCount;
      }

int main(int argc, char** argv)
      {
      if (argc < 4) { fprintf(stderr, "usage: ktload plugin out.txt [--pump s] [--save-after s] state[=save] ...\n"); return 2; }
      std::string pluginPath = argv[1];
      FILE* out = fopen(argv[2], "w");
      double pump = 60, saveAfter = 20;
      bool noProcess = false;
      bool offline = false, inactive = false;   // --offline: kOffline (MuseScore's renders); --inactive: setState before setActive          // --no-process: the instances not processed while waiting (MuseScore before a play)
      std::vector<std::pair<std::string, std::string>> states;
      for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if ((a == "--trace" || a == "--dll") && i + 1 < argc) {      // --trace <dll> <dump file>; --dll <dll> (loaded first, no dump)
                  const char* dll = argv[++i];
                  HMODULE h = LoadLibraryA(dll);
                  if (h) {
                        if (auto m = (mark_t)GetProcAddress(h, "ktmark")) g_marks.push_back(m);
                        if (a == "--trace" && i + 1 < argc)
                              if (auto d = (mark_t)GetProcAddress(h, "ktdump")) { g_dumps.push_back(d); g_dumpPaths.push_back(argv[i + 1]); }
                        }
                  if (a == "--trace") ++i;
                  fprintf(stderr, "%s %s %s\n", a.c_str(), dll, h ? "loaded" : "NOT loaded");
                  continue;
                  }
            if (a == "--pump" && i + 1 < argc) pump = atof(argv[++i]);
            else if (a == "--save-after" && i + 1 < argc) saveAfter = atof(argv[++i]);
            else if (a == "--no-process") noProcess = true;
            else if (a == "--offline") offline = true;
            else if (a == "--inactive") inactive = true;
            else {
                  const size_t eq = a.find('=');
                  states.push_back({ a.substr(0, eq), eq == std::string::npos ? std::string() : a.substr(eq + 1) });
                  }
            }
      auto t0 = std::chrono::steady_clock::now();
      auto now = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
      auto log = [&](const char* fmt, auto... args) {
            fprintf(out, "%8.2f ", now()); fprintf(out, fmt, args...); fprintf(out, "\n"); fflush(out);
            fprintf(stderr, "%8.2f ", now()); fprintf(stderr, fmt, args...); fprintf(stderr, "\n");
            };
      static IPtr<HostApplication> host = owned(new HostApplication);
      PluginContextFactory::instance().setPluginContext(host);
      std::string err;
      mark("module load");
      auto module = VST3::Hosting::Module::create(pluginPath, err);
      if (!module) { log("module: %s", err.c_str()); return 1; }
      VST3::Hosting::ClassInfo chosen;
      for (auto& ci : module->getFactory().classInfos())
            if (ci.category() == kVstAudioEffectClass) { chosen = ci; break; }
      log("module loaded | private %.0f MB", privateMb());

      std::vector<std::unique_ptr<Instance>> ins;
      std::vector<float> scratch(2 * BLOCK);
      EventList events(64);
      ParameterChanges changes(64), outChanges(64);
      ProcessContext ctx {};
      auto process = [&](Instance& in, int n) {
            ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid;
            ctx.sampleRate = RATE; ctx.tempo = 120;
            in.data.processMode = offline ? kOffline : kRealtime;
            in.data.numSamples = n;
            in.data.inputEvents = &events;
            in.data.inputParameterChanges = &changes;
            in.data.outputParameterChanges = &outChanges;
            in.data.processContext = &ctx;
            in.processor->process(in.data);
            changes.clearQueue(); outChanges.clearQueue();
            };
      int idx = 0;
      for (auto& s0 : states) {
            auto s = s0;
            const bool reuse = !s.first.empty() && s.first[0] == '@';       // "@state": setState into the previous instance
            if (reuse) s.first = s.first.substr(1);
            std::string comp, ctrl;
            if (!readState(s.first, comp, ctrl)) { log("bad state %s", s.first.c_str()); continue; }
            const std::string tag = std::to_string(++idx) + " " + s.first + (reuse ? " (reuse)" : "");
            double c0 = cpuSeconds();
            double u0, k0; cpuSplit(u0, k0);
            const double pf0 = pageFaults(), pm0 = privateMb();
            double a = now();
            mark("create " + tag);
            if (reuse && !ins.empty()) {
                  Instance* in = ins.back().get();
                  a = now();
                  mark("setState " + tag);
                  IPtr<MemoryStream> st = owned(new MemoryStream((void*)comp.data(), (TSize)comp.size()));
                  tresult r = in->component->setState(st);
                  const double setState = now() - a;
                  a = now();
                  mark("controller " + tag);
                  if (in->controller) { st->seek(0, IBStream::kIBSeekSet, nullptr); in->controller->setComponentState(st); }
                  const double ctl = now() - a;
                  a = now();
                  mark("settle " + tag);
                  for (int f = 0; f < 2074; f += BLOCK) process(*in, std::min(BLOCK, 2074 - f));
                  log("%s | REUSE | setState %d %.3f s | controller %.3f s | settle %.3f s | cpu %.2f s | private %.0f MB",
                      s.first.c_str(), int(r), setState, ctl, now() - a, cpuSeconds() - c0, privateMb());
                  in->loadedAt = now();
                  continue;
                  }
            std::unique_ptr<Instance> in(new Instance);
            in->provider = owned(new PlugProvider(module->getFactory(), chosen, true));
            if (!in->provider->initialize()) { log("init failed"); return 1; }
            in->component = in->provider->getComponentPtr();
            in->controller = in->provider->getControllerPtr();
            FUnknownPtr<IAudioProcessor> proc(in->component);
            in->processor = proc.getInterface();
            in->processor->addRef();
            if (in->controller) in->controller->setComponentHandler(&g_handler);
            IComponent* component = in->component;
            for (int i = 0; i < component->getBusCount(kEvent, kInput); ++i) component->activateBus(kEvent, kInput, i, i == 0);
            for (int i = 0; i < component->getBusCount(kAudio, kInput); ++i) component->activateBus(kAudio, kInput, i, false);
            const int outs = component->getBusCount(kAudio, kOutput);
            in->outBus = 0;
            for (int i = 0; i < outs; ++i) component->activateBus(kAudio, kOutput, i, i == 0);
            std::vector<SpeakerArrangement> arr(outs, SpeakerArr::kStereo);
            in->processor->setBusArrangements(nullptr, 0, arr.data(), int32(arr.size()));
            ProcessSetup ps { offline ? kOffline : kRealtime, kSample32, 4096, double(RATE) };
            in->processor->setupProcessing(ps);
            in->data.prepare(*component, 4096, kSample32);
            if (!inactive) { component->setActive(true); in->processor->setProcessing(true); }
            const double created = now() - a;
            a = now();
            mark("setState " + tag);
            IPtr<MemoryStream> st = owned(new MemoryStream((void*)comp.data(), (TSize)comp.size()));
            tresult r = component->setState(st);
            const double setState = now() - a;
            double u1, k1; cpuSplit(u1, k1);
            const double pf1 = pageFaults(), pm1 = privateMb();
            a = now();
            mark("controller " + tag);
            if (in->controller) { st->seek(0, IBStream::kIBSeekSet, nullptr); in->controller->setComponentState(st); }
            const double ctl = now() - a;
            a = now();
            if (inactive) { component->setActive(true); in->processor->setProcessing(true); }
            const double activate = now() - a;
            if (inactive) log("activated after setState in %.3f s", activate);
            a = now();
            mark("settle " + tag);
            for (int f = 0; f < 2074; f += BLOCK) process(*in, std::min(BLOCK, 2074 - f));     // (47 ms: MuseScore's settle)
            const double settle = now() - a;
            a = now();
            int params = 0;
            if (in->controller) {           // (every parameter's info, as Vst3Plugin's title index reads them)
                  params = in->controller->getParameterCount();
                  for (int k = 0; k < params; ++k) { ParameterInfo info {}; in->controller->getParameterInfo(k, info); }
                  }
            const double titles = now() - a;
            log("%s | %zu KB | created %.3f s | setState %d %.3f s | controller %.3f s | settle %.3f s | %d parameters' info %.3f s | cpu %.2f s | private %.0f MB"
                " | in setState: user %.3f s kernel %.3f s page faults %.0f private +%.0f MB",
                s.first.c_str(), comp.size() / 1024, created, int(r), setState, ctl, settle, params, titles, cpuSeconds() - c0, privateMb(),
                u1 - u0, k1 - k0, pf1 - pf0, pm1 - pm0);
            in->save = s.second;
            in->loadedAt = now();
            mark("idle " + tag);
            ins.push_back(std::move(in));
            }
      log("all %zu loaded | private %.0f MB | cpu %.1f s | since start %.3f s", ins.size(), privateMb(), cpuSeconds(), now());
      mark("pump");
      auto start = std::chrono::steady_clock::now();
      long long frames = 0;
      double nextLog = now() + 1;
      while (now() < ins.back()->loadedAt + pump) {
            const double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            while (frames < (long long)(el * RATE)) {
                  if (!noProcess) for (auto& in : ins) process(*in, BLOCK);
                  frames += BLOCK;
                  }
            for (auto& in : ins)
                  if (!in->save.empty() && !in->saved && now() > in->loadedAt + saveAfter) {
                        writeState(in->save, in->component, in->controller);
                        in->saved = true;
                        log("saved %s", in->save.c_str());
                        }
            if (now() >= nextLog) {
                  log("pump | private %.0f MB | cpu %.1f s", privateMb(), cpuSeconds());
                  nextLog += 1;
                  }
            Sleep(5);
            }
      log("done | private %.0f MB", privateMb());
      for (size_t k = 0; k < g_dumps.size(); ++k) g_dumps[k](g_dumpPaths[k].c_str());
      fclose(out);
      _exit(0);       // (Kontakt's teardown: not measured)
      }
