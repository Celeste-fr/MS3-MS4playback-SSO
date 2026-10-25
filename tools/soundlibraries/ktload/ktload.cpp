// ktload: load-time experiments with Kontakt on the Windows VM (load-profile, 2026-10-16).
//
// Build on Linux with msvc-wine (~/msvc): CC=CXX=<msvc>/bin/x64/cl cmake -G Ninja -B build -DCMAKE_SYSTEM_NAME=Windows
// -DCMAKE_BUILD_TYPE=Release -DCMAKE_LINKER=<msvc>/bin/x64/link -DCMAKE_MT=<msvc>/bin/x64/mt -DCMAKE_RC_COMPILER=<msvc>/bin/x64/rc
// tools/soundlibraries/ktload && ninja -C build ktload. The states: MuseScore's setups as SoundLibraryHost loads them
// (voice limit, tunings), never committed (Spitfire's patches).
//
//   ktload <plugin.vst3> <out.txt> [--pump <s>] [--save-after <s>] [--no-process] [--offline] [--inactive]
//          [--dll <x.dll>] [--trace <x.dll> <dump.txt>] [--play <s> [--block n]]... [--rate hz] [--note-ms ms] [--churn]
//          [--times <file>] <state.vst3state>[=<save.vst3state>] | @<state.vst3state> ...
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
//
// kheap-realtime (2026-10-24): --thread-loads: each new instance's setState on a thread of its own that ends after it, as
// MuseScore's load threads and its even-levels measurer's do (mimalloc then holds the load's memory in abandoned
// segments). --play, after the pump, the realtime test (playTest below: dense legato lines on every
// instance, processed block by block at the real-time pace on an MMCSS "Pro Audio" thread, each cycle's time logged).

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
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include <avrt.h>
#include <atomic>
#include <random>
#include <cmath>

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

static int RATE = 44100, BLOCK = 512;
typedef void (*mark_t)(const char*);
static std::vector<mark_t> g_marks, g_dumps;
static std::vector<std::string> g_dumpPaths;
static void mark(const std::string& m) { for (auto f : g_marks) f(m.c_str()); }
typedef void (*scope_t)(int);
static scope_t g_scope = nullptr;       // kheap's kheap_scope, around each setState as MuseScore's Vst3Plugin::setState
static void scope(int enter) { if (g_scope) g_scope(enter); }
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

// --play <s> (kheap-realtime, 2026-10-24): the realtime test. A thread as an audio callback's (MMCSS "Pro Audio",
// time-critical) processes every instance one block (--block, default 512; --rate) at the real-time pace, sleeping to
// each block's deadline, while every instance plays dense legato lines (a note each --note-ms ms, overlapping the
// next by 20 ms, a random walk over 43-84, velocity 40-110) and CC1 sweeping (by the plug-in's MIDI mapping, every
// block). Each cycle's wall time (all instances, as MuseScore's callback processes all slots) is recorded: logged as
// median / p99 / p99.9 / max and the count over the block's period (a crackle) and over half of it; --times <file>
// writes every cycle (ms). --churn: a thread meanwhile allocates and frees through ucrtbase as a GUI / renderer would
// (1-4 KB blocks, a few MB live, ~200k calls a second).
template <class Log, class Now>
static void playTest(std::vector<std::unique_ptr<Instance>>& ins, double seconds, int block, double noteMs, bool churn,
                     const std::string& timesPath, bool offline, Log log, Now now)
      {
      struct Voice { EventList events { 512 }; ParameterChanges changes { 64 }; ParameterChanges outChanges { 64 }; ParamID cc1 = kNoParamId;
                     int pitch = 64, sounding = -1; long long nextOn = 0, offAt = -1; std::mt19937 rng; };
      std::vector<std::unique_ptr<Voice>> voices;
      for (size_t k = 0; k < ins.size(); ++k) {
            std::unique_ptr<Voice> v(new Voice);
            v->rng.seed(unsigned(1234 + k));
            v->pitch = 55 + int(k % 5) * 4;
            v->nextOn = (long long)(k * RATE * noteMs / 1000.0 / ins.size());
            FUnknownPtr<IMidiMapping> map(ins[k]->controller);
            ParamID id;
            if (map && map->getMidiControllerAssignment(0, 0, 1, id) == kResultTrue) v->cc1 = id;
            voices.push_back(std::move(v));
            }
      std::atomic<bool> stop { false };
      std::thread churner;
      if (churn)
            churner = std::thread([&]() {
                  // ucrtbase's malloc / free, as Qt's and the plug-ins' (this exe's own CRT is static, on the process heap)
                  HMODULE u = LoadLibraryW(L"ucrtbase.dll");
                  typedef void* (__cdecl* m_t)(size_t);
                  typedef void (__cdecl* f_t)(void*);
                  m_t malloc = (m_t)GetProcAddress(u, "malloc");
                  f_t free = (f_t)GetProcAddress(u, "free");
                  std::vector<void*> live(2048, nullptr);
                  std::mt19937 r(7);
                  while (!stop.load()) {
                        for (int i = 0; i < 2000; ++i) {
                              void*& p = live[r() % live.size()];
                              free(p);
                              p = malloc(1024 + r() % 3072);
                              if (p) memset(p, 1, 64);
                              }
                        Sleep(10);
                        }
                  for (void* p : live) free(p);
                  });
      const long long total = (long long)(seconds * RATE);
      // the output, every instance's main bus added up (stereo), for --times' WAV (<times>.wav): a disk-streaming
      // underrun (Kontakt plays silence for a voice whose samples didn't arrive) shows in the sound, not in the cycle time
      std::vector<float> wav(timesPath.empty() ? 0 : size_t(total + block) * 2, 0.f);
      std::vector<float> cycles;
      cycles.reserve(size_t(total / block + 16));
      std::vector<float> lateness;
      lateness.reserve(cycles.capacity());
      double faults0 = pageFaults();
      double u0, k0; cpuSplit(u0, k0);
      mark("play " + std::to_string(block));
      std::thread audio([&]() {
            DWORD task = 0;
            HANDLE av = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
            timeBeginPeriod(1);
            HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
            LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
            LARGE_INTEGER t; QueryPerformanceCounter(&t);
            const double period = double(block) / RATE;
            const long long start = t.QuadPart;
            ProcessContext ctx {};
            for (long long frame = 0, n = 0; frame < total; frame += block, ++n) {
                  const long long deadline = start + (long long)(n * period * freq.QuadPart);
                  // sleep to the block's start (as a callback waits for the device)
                  for (;;) {
                        QueryPerformanceCounter(&t);
                        const long long left = deadline - t.QuadPart;
                        if (left <= 0) break;
                        const double ms = 1000.0 * left / freq.QuadPart;
                        if (ms > 1.5 && timer) {
                              LARGE_INTEGER due; due.QuadPart = -(long long)((ms - 1.0) * 10000);
                              SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
                              WaitForSingleObject(timer, INFINITE);
                              }
                        else if (ms > 1.5) Sleep(1);
                        else YieldProcessor();
                        }
                  QueryPerformanceCounter(&t);
                  const long long c0 = t.QuadPart;
                  lateness.push_back(float(1000.0 * (c0 - deadline) / freq.QuadPart));
                  for (size_t k = 0; k < ins.size(); ++k) {
                        Voice& v = *voices[k];
                        v.events.clear();
                        auto note = [&](bool on, int pitch, int off, int vel) {
                              Event e {};
                              e.busIndex = 0; e.sampleOffset = off; e.flags = Event::kIsLive;
                              if (on) { e.type = Event::kNoteOnEvent; e.noteOn.channel = 0; e.noteOn.pitch = int16(pitch);
                                        e.noteOn.velocity = vel / 127.f; e.noteOn.noteId = -1; }
                              else { e.type = Event::kNoteOffEvent; e.noteOff.channel = 0; e.noteOff.pitch = int16(pitch);
                                     e.noteOff.velocity = 0; e.noteOff.noteId = -1; }
                              v.events.addEvent(e);
                              };
                        if (v.offAt >= frame && v.offAt < frame + block) { note(false, v.sounding, int(v.offAt - frame), 0); v.offAt = -1; }
                        if (v.nextOn >= frame && v.nextOn < frame + block) {
                              const int prev = v.sounding;
                              int step = int(v.rng() % 9) - 4;
                              v.pitch = std::max(43, std::min(84, v.pitch + step));
                              note(true, v.pitch, int(v.nextOn - frame), 40 + int(v.rng() % 71));
                              if (prev >= 0 && prev != v.pitch) v.offAt = v.nextOn + RATE / 50;      // 20 ms overlap: legato
                              v.sounding = v.pitch;
                              v.nextOn += (long long)(RATE * noteMs / 1000.0 * (0.5 + (v.rng() % 100) / 100.0));
                              if (v.offAt >= frame && v.offAt < frame + block) { note(false, prev, int(v.offAt - frame), 0); v.offAt = -1; }
                              }
                        if (v.cc1 != kNoParamId) {
                              int32 idx;
                              if (auto q = v.changes.addParameterData(v.cc1, idx)) {
                                    int32 pi;
                                    q->addPoint(0, 0.5 + 0.45 * sin(6.2832 * (double(frame) / RATE) / (3.0 + k * 0.37)), pi);
                                    }
                              }
                        Instance& in = *ins[k];
                        ctx.state = ProcessContext::kPlaying | ProcessContext::kTempoValid;
                        ctx.sampleRate = RATE; ctx.tempo = 120; ctx.projectTimeSamples = frame;
                        in.data.processMode = offline ? kOffline : kRealtime;
                        in.data.numSamples = block;
                        in.data.inputEvents = &v.events;
                        in.data.inputParameterChanges = &v.changes;
                        in.data.outputParameterChanges = &v.outChanges;
                        in.data.processContext = &ctx;
                        in.processor->process(in.data);
                        v.changes.clearQueue(); v.outChanges.clearQueue();
                        if (!wav.empty() && in.data.numOutputs > 0 && in.data.outputs[0].numChannels >= 2) {
                              float** ch = in.data.outputs[0].channelBuffers32;
                              float* w = wav.data() + size_t(frame) * 2;
                              for (int i = 0; i < block; ++i) { w[2 * i] += ch[0][i]; w[2 * i + 1] += ch[1][i]; }
                              }
                        }
                  QueryPerformanceCounter(&t);
                  cycles.push_back(float(1000.0 * (t.QuadPart - c0) / freq.QuadPart));
                  }
            // every note off
            for (size_t k = 0; k < ins.size(); ++k) {
                  Voice& v = *voices[k];
                  v.events.clear();
                  for (int p = 0; p < 128; ++p) { Event e {}; e.type = Event::kNoteOffEvent; e.noteOff.pitch = int16(p); e.noteOff.noteId = -1; v.events.addEvent(e); }
                  ins[k]->data.inputEvents = &v.events; ins[k]->processor->process(ins[k]->data);
                  }
            timeEndPeriod(1);
            if (timer) CloseHandle(timer);
            if (av) AvRevertMmThreadCharacteristics(av);
            });
      audio.join();
      mark("after play " + std::to_string(block));
      stop = true;
      if (churner.joinable()) churner.join();
      double u1, k1; cpuSplit(u1, k1);
      const double periodMs = 1000.0 * block / RATE;
      std::vector<float> s = cycles;
      std::sort(s.begin(), s.end());
      auto q = [&](double f) { return s.empty() ? 0.0 : double(s[std::min(s.size() - 1, size_t(f * s.size()))]); };
      int over = 0, overHalf = 0, overDeadline = 0;
      for (size_t i = 0; i < cycles.size(); ++i) {
            if (cycles[i] > periodMs) ++over;
            if (cycles[i] > periodMs / 2) ++overHalf;
            if (cycles[i] + lateness[i] > periodMs) ++overDeadline;       // (finished after the next block's start)
            }
      std::vector<float> l = lateness; std::sort(l.begin(), l.end());
      log("play | %zu instances | block %d at %d Hz (%.2f ms) | %zu cycles | median %.3f p99 %.3f p99.9 %.3f max %.3f ms | over period %d | over half %d | late+cycle over period %d | wake late max %.3f ms | page faults %.0f | cpu user %.1f kernel %.1f s | private %.0f MB%s",
          ins.size(), block, RATE, periodMs, cycles.size(), q(0.5), q(0.99), q(0.999), s.empty() ? 0.0 : double(s.back()), over, overHalf, overDeadline,
          l.empty() ? 0.0 : double(l.back()), pageFaults() - faults0, u1 - u0, k1 - k0, privateMb(), churn ? " | churn" : "");
      // the 12 slowest cycles: when
      std::vector<size_t> idx(cycles.size());
      for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;
      std::partial_sort(idx.begin(), idx.begin() + std::min<size_t>(12, idx.size()), idx.end(), [&](size_t a, size_t b) { return cycles[a] > cycles[b]; });
      std::string worst;
      for (size_t i = 0; i < std::min<size_t>(12, idx.size()); ++i) { char b[48]; snprintf(b, sizeof b, " %.2fs:%.2f", idx[i] * periodMs / 1000, cycles[idx[i]]); worst += b; }
      log("play | slowest (time:ms)%s", worst.c_str());
      if (!timesPath.empty()) {
            FILE* f = fopen(timesPath.c_str(), "w");
            if (f) { for (size_t i = 0; i < cycles.size(); ++i) fprintf(f, "%.4f\t%.4f\n", cycles[i], lateness[i]); fclose(f); }
            const std::string wp = timesPath.substr(0, timesPath.size() - 4) + ".wav";
            FILE* w = fopen(wp.c_str(), "wb");
            if (w) {        // 32-bit float, stereo
                  const uint32_t frames = uint32_t(total), bytes = frames * 8;
                  auto u32 = [&](uint32_t v) { fwrite(&v, 4, 1, w); };
                  auto u16 = [&](uint16_t v) { fwrite(&v, 2, 1, w); };
                  fwrite("RIFF", 1, 4, w); u32(36 + bytes); fwrite("WAVEfmt ", 1, 8, w); u32(16); u16(3); u16(2); u32(RATE);
                  u32(RATE * 8); u16(8); u16(32); fwrite("data", 1, 4, w); u32(bytes);
                  fwrite(wav.data(), 4, size_t(frames) * 2, w);
                  fclose(w);
                  }
            }
      }

int main(int argc, char** argv)
      {
      if (argc < 4) { fprintf(stderr, "usage: ktload plugin out.txt [--pump s] [--save-after s] state[=save] ...\n"); return 2; }
      std::string pluginPath = argv[1];
      FILE* out = fopen(argv[2], "w");
      double pump = 60, saveAfter = 20;
      std::vector<std::pair<double, int>> plays;      // --play <s> [--block n], repeatable: the realtime play test (below)
      double noteMs = 90;
      int playBlock = 512;
      bool churn = false;
      bool threadLoads = false;       // --thread-loads: each setState on a thread of its own that then ends (MuseScore's load threads)
      std::string timesPath;
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
                        if (auto sc = (scope_t)GetProcAddress(h, "kheap_scope")) g_scope = sc;
                        if (a == "--trace" && i + 1 < argc)
                              if (auto d = (mark_t)GetProcAddress(h, "ktdump")) { g_dumps.push_back(d); g_dumpPaths.push_back(argv[i + 1]); }
                        }
                  if (a == "--trace") ++i;
                  fprintf(stderr, "%s %s %s\n", a.c_str(), dll, h ? "loaded" : "NOT loaded");
                  continue;
                  }
            if (a == "--pump" && i + 1 < argc) pump = atof(argv[++i]);
            else if (a == "--play" && i + 1 < argc) plays.push_back({ atof(argv[++i]), playBlock });
            else if (a == "--block" && i + 1 < argc) { playBlock = atoi(argv[++i]); if (!plays.empty()) plays.back().second = playBlock; }
            else if (a == "--rate" && i + 1 < argc) RATE = atoi(argv[++i]);
            else if (a == "--note-ms" && i + 1 < argc) noteMs = atof(argv[++i]);
            else if (a == "--churn") churn = true;
            else if (a == "--thread-loads") threadLoads = true;
            else if (a == "--times" && i + 1 < argc) timesPath = argv[++i];
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
                  scope(1);
                  tresult r = in->component->setState(st);
                  scope(0);
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
            tresult r = kResultOk;
            auto doSet = [&]() { scope(1); r = component->setState(st); scope(0); };
            if (threadLoads) { std::thread th(doSet); th.join(); }
            else doSet();
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
      for (size_t k = 0; k < plays.size(); ++k)
            playTest(ins, plays[k].first, plays[k].second, noteMs, churn, timesPath.empty() ? timesPath : timesPath + "." + std::to_string(k + 1) + ".txt", offline, log, now);
      log("done | private %.0f MB", privateMb());
      for (size_t k = 0; k < g_dumps.size(); ++k) g_dumps[k](g_dumpPaths[k].c_str());
      fclose(out);
      _exit(0);       // (Kontakt's teardown: not measured)
      }
