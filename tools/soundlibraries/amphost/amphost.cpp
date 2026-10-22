// amphost: a small offline VST 3 instrument host for measuring a plug-in (Windows; built with MSVC or msvc-wine against
// the fork's thirdparty/vst3sdk: tools/soundlibraries/amphost/build.sh). Not part of MuseScore. It contains nothing of
// any plug-in vendor's: it only calls the VST 3 API. Used for the Ample guitars (docs/AMPLE_PIPELINE.md): `info` is the
// dump tools/soundlibraries/gen_ample.py reads, `render` the renders tools/ample/measure_ample.py analyses.
//
//   amphost <plugin.vst3> info <out.txt> [<state out>] [--state in.state]
//        the parameters (index, id, title, units, steps, default, now 'text', flags; and for every parameter whose
//        title doesn't start with "MIDI CC", "FX-" or "AMP-" a `values:` line with its text at i / 22, i = 0..22), the
//        buses, the IMidiMapping of channel 0 and the component state's size; <state out> saves the component state
//        (after --state: what the plug-in made of a loaded state)
//   amphost <plugin.vst3> load [--state in.state]
//        loads, prints "loaded <s>" and exits 0 (a load-crash loop counts the processes that don't)
//   amphost <plugin.vst3> render <events.txt> <out.wav> [--pre s] [--tail s] [--rt] [--offline] [--block n]
//        [--repeat n] [--state in.state] [--savestate out.state] [--set id=norm]... [--midiout out.txt]
//        [--tempo bpm] [--stopped] [--wait s] [--nopump] [--fastpre s] [--probe s] [--gui]
// events: one per line, time in seconds after the pre-roll: "t on note vel" | "t off note" | "t cc num val" |
//   "t cch ch*1000+num val" | "t pb val14 ch" | "t param id norm" (note = ch*1000 + pitch). CC / pitch bend go through
//   IMidiMapping (channel 0), as the fork's Vst3Plugin does. Output: 32-bit float stereo WAV at 44.1 kHz.
// --pre: silence processed real-time paced first (background sample loaders run); --rt paces the render too;
// --offline: kOffline process mode; --repeat n: n renders _1.._n with 2 s between; --tempo / --stopped: the
// ProcessContext's tempo (120) and transport (playing); --probe s: every 50 ms CPU, threads, working set and hashes of
// the parameters and the state (what tells a finished background load); --wait s: wall-clock wait before the pre-roll.
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <winternl.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/gui/iplugview.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

class Handler : public U::ImplementsNonDestroyable<U::Directly<IComponentHandler>> {
   public:
      tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API performEdit(ParamID, ParamValue) override { return kResultOk; }
      tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
      tresult PLUGIN_API restartComponent(int32 f) override { fprintf(stderr, "restartComponent %d\n", f); return kResultOk; }
      };
static Handler g_handler;

static std::string u8(const char16_t* s)
      {
      std::string o;
      for (; *s; ++s) o.push_back(*s < 128 ? char(*s) : '?');
      return o;
      }

static bool g_nopump = false;
static void pump()
      {
      if (g_nopump) return;
      MSG m;
      while (PeekMessage(&m, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessage(&m); }
      }

struct Ev { double t; std::string type; int a; double b; };

static void writeWav(const std::string& path, const std::vector<float>& lr, int rate)
      {
      FILE* f = fopen(path.c_str(), "wb");
      const uint32_t n = uint32_t(lr.size() * 4);
      auto w32 = [&](uint32_t v) { fwrite(&v, 4, 1, f); };
      auto w16 = [&](uint16_t v) { fwrite(&v, 2, 1, f); };
      fwrite("RIFF", 1, 4, f); w32(36 + n); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(3); w16(2); w32(rate); w32(rate * 8); w16(8); w16(32);
      fwrite("data", 1, 4, f); w32(n); fwrite(lr.data(), 4, lr.size(), f); fclose(f);
      }

static const int RATE = 44100;

static HWND g_wnd = nullptr;
class Frame : public U::ImplementsNonDestroyable<U::Directly<IPlugFrame>> {
   public:
      tresult PLUGIN_API resizeView(IPlugView* v, ViewRect* r) override {
            RECT rc { 0, 0, r->getWidth(), r->getHeight() }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
            SetWindowPos(g_wnd, nullptr, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOMOVE | SWP_NOZORDER);
            return v->onSize(r); }
      };
static Frame g_frame;
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcW(h, m, w, l); }

int main(int argc, char** argv)
      {
      if (argc < 3) { fprintf(stderr, "usage: see source\n"); return 2; }
      std::string pluginPath = argv[1], cmd = argv[2];
      std::string a1 = argc > 3 ? argv[3] : "", a2 = argc > 4 ? argv[4] : "";
      double pre = 5, tail = 3; bool rt = false, offline = false, gui = false; int block = 512, repeat = 1; double waitS = 0, fastPre = 0, probeS = 0, tempo = 120; bool stopped = false;
      std::string stateIn, stateOut, midiOut;
      std::vector<std::pair<ParamID, double>> sets;
      for (int i = cmd == "render" ? 5 : cmd == "load" ? 3 : 4; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--pre") pre = atof(argv[++i]);
            else if (a == "--tail") tail = atof(argv[++i]);
            else if (a == "--rt") rt = true;
            else if (a == "--nopump") g_nopump = true;
            else if (a == "--tempo") tempo = atof(argv[++i]);
            else if (a == "--stopped") stopped = true;
            else if (a == "--probe") probeS = atof(argv[++i]);
            else if (a == "--wait") waitS = atof(argv[++i]);
            else if (a == "--fastpre") fastPre = atof(argv[++i]);
            else if (a == "--gui") gui = true;
            else if (a == "--offline") offline = true;
            else if (a == "--block") block = atoi(argv[++i]);
            else if (a == "--repeat") repeat = atoi(argv[++i]);
            else if (a == "--state") stateIn = argv[++i];
            else if (a == "--savestate") stateOut = argv[++i];
            else if (a == "--midiout") midiOut = argv[++i];
            else if (a == "--set") { std::string s = argv[++i]; size_t e = s.find('='); sets.push_back({ ParamID(std::stoul(s.substr(0, e))), atof(s.substr(e + 1).c_str()) }); }
            }
      CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
      static IPtr<HostApplication> host = owned(new HostApplication);
      PluginContextFactory::instance().setPluginContext(host);
      std::string err;
      auto t0 = std::chrono::steady_clock::now();
      auto now = [&]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
      auto module = VST3::Hosting::Module::create(pluginPath, err);
      if (!module) { fprintf(stderr, "module: %s\n", err.c_str()); return 1; }
      VST3::Hosting::ClassInfo chosen;
      for (auto& ci : module->getFactory().classInfos()) {
            fprintf(stderr, "class %s | %s | %s\n", ci.name().c_str(), ci.category().c_str(), ci.subCategoriesString().c_str());
            if (ci.category() == kVstAudioEffectClass && chosen.name().empty()) chosen = ci;
            }
      IPtr<PlugProvider> provider = owned(new PlugProvider(module->getFactory(), chosen, true));
      if (!provider->initialize()) { fprintf(stderr, "init failed\n"); return 1; }
      IPtr<IComponent> component = provider->getComponentPtr();
      IPtr<IEditController> controller = provider->getControllerPtr();
      FUnknownPtr<IAudioProcessor> proc(component);
      if (controller) controller->setComponentHandler(&g_handler);
      const int evIn = component->getBusCount(kEvent, kInput);
      for (int i = 0; i < evIn; ++i) component->activateBus(kEvent, kInput, i, i == 0);
      for (int i = 0; i < component->getBusCount(kAudio, kInput); ++i) component->activateBus(kAudio, kInput, i, false);
      const int outs = component->getBusCount(kAudio, kOutput);
      for (int i = 0; i < outs; ++i) component->activateBus(kAudio, kOutput, i, i == 0);
      std::vector<SpeakerArrangement> arr(outs, SpeakerArr::kStereo);
      proc->setBusArrangements(nullptr, 0, arr.data(), int32(arr.size()));
      ProcessSetup ps { offline ? kOffline : kRealtime, kSample32, block, double(RATE) };
      proc->setupProcessing(ps);
      HostProcessData data;
      data.prepare(*component, block, kSample32);
      if (!stateIn.empty()) {
            std::ifstream f(stateIn, std::ios::binary);
            std::string d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            IPtr<MemoryStream> st = owned(new MemoryStream((void*)d.data(), (TSize)d.size()));
            fprintf(stderr, "setState %d\n", int(component->setState(st)));
            st->seek(0, IBStream::kIBSeekSet, nullptr);
            if (controller) controller->setComponentState(st);
            }
      component->setActive(true);
      proc->setProcessing(true);
      fprintf(stderr, "loaded %.2f s; latency %u samples; tail %u\n", now(), proc->getLatencySamples(), proc->getTailSamples());

      FUnknownPtr<IMidiMapping> mm(controller);
      auto mapCC = [&](int cc) -> long {
            ParamID id; if (mm && mm->getMidiControllerAssignment(0, 0, CtrlNumber(cc), id) == kResultOk) return long(id); return -1; };

      if (cmd == "load") {
            fprintf(stderr, "loaded %.2f\n", now());
            fflush(stderr);
            _exit(0);
            }
      if (cmd == "info") {
            FILE* o = fopen(a1.c_str(), "w");
            fprintf(o, "plugin %s class %s\n", pluginPath.c_str(), chosen.name().c_str());
            fprintf(o, "latency_samples %u tail_samples %u\n", proc->getLatencySamples(), proc->getTailSamples());
            for (auto dir : { kInput, kOutput }) for (auto t : { kAudio, kEvent }) {
                  int n = component->getBusCount(t, dir);
                  for (int i = 0; i < n; ++i) { BusInfo bi {}; component->getBusInfo(t, dir, i, bi);
                        fprintf(o, "bus %s %s %d '%s' channels %d flags %u\n", t == kAudio ? "audio" : "event", dir == kInput ? "in" : "out", i, u8(bi.name).c_str(), bi.channelCount, bi.flags); }
                  }
            fprintf(o, "midi_mapping (ch0): ");
            int mapped = 0;
            for (int cc = 0; cc < 130; ++cc) { long id = mapCC(cc); if (id >= 0) { fprintf(o, "%d->%ld ", cc, id); ++mapped; } }
            fprintf(o, "\nmapped %d\n", mapped);
            int n = controller ? controller->getParameterCount() : 0;
            fprintf(o, "parameters %d\n", n);
            for (int k = 0; k < n; ++k) {
                  ParameterInfo pi {}; controller->getParameterInfo(k, pi);
                  String128 s {}; controller->getParamStringByValue(pi.id, controller->getParamNormalized(pi.id), s);
                  fprintf(o, "%d\tid=%u\t%s\tunits=%s\tsteps=%d\tdefault=%.6f\tnow=%.6f '%s'\tflags=%d\n", k, pi.id, u8(pi.title).c_str(), u8(pi.units).c_str(),
                          pi.stepCount, pi.defaultNormalizedValue, controller->getParamNormalized(pi.id), u8(s).c_str(), pi.flags);
                  std::string t = u8(pi.title);
                  if (t.find("MIDI CC") == std::string::npos && t.rfind("FX-", 0) != 0 && t.rfind("AMP-", 0) != 0) {
                        fprintf(o, "\t  values:");
                        for (int q = 0; q <= 22; ++q) { String128 v {}; controller->getParamStringByValue(pi.id, q / 22.0, v); fprintf(o, " %.3f='%s'", q / 22.0, u8(v).c_str()); }
                        fprintf(o, "\n");
                        }
                  }
            IPtr<MemoryStream> cs = owned(new MemoryStream); component->getState(cs);
            fprintf(o, "component_state_bytes %lld\n", (long long)cs->getSize());
            fclose(o);
            if (!a2.empty() && a2.rfind("--", 0) != 0) { FILE* f = fopen(a2.c_str(), "wb"); fwrite(cs->getData(), 1, size_t(cs->getSize()), f); fclose(f); }
            fflush(stderr);
            _exit(0);
            }

      // render
      IPtr<IPlugView> view;
      if (gui) {
            view = owned(controller->createView(ViewType::kEditor));
            WNDCLASSW wc {}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"amphostgui";
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW); RegisterClassW(&wc);
            ViewRect r {}; if (view) view->getSize(&r);
            RECT rc { 0, 0, r.getWidth() > 0 ? r.getWidth() : 800, r.getHeight() > 0 ? r.getHeight() : 600 }; AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
            g_wnd = CreateWindowW(L"amphostgui", L"amphost3 - plug-in window", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, wc.hInstance, nullptr);
            if (view) { view->setFrame(&g_frame); fprintf(stderr, "attached %d size %dx%d\n", int(view->attached(g_wnd, kPlatformTypeHWND)), r.getWidth(), r.getHeight()); }
            SetForegroundWindow(g_wnd);
            }
      std::vector<Ev> evs;
      { std::ifstream f(a1); std::string line;
        while (std::getline(f, line)) { if (line.empty() || line[0] == '#') continue; std::istringstream is(line); Ev e {}; is >> e.t >> e.type >> e.a >> e.b; evs.push_back(e); } }
      std::stable_sort(evs.begin(), evs.end(), [](const Ev& x, const Ev& y) { return x.t < y.t; });
      double end = evs.empty() ? 0 : evs.back().t;
      EventList events(512); EventList outEv(2048); FILE* mo = nullptr;
      ParameterChanges changes(256), outChanges(256);
      ProcessContext ctx {};
      long long pos = 0;   // song position in samples since start of processing
      auto process = [&](int n, std::vector<float>* rec) {
            ctx.state = (stopped ? 0 : ProcessContext::kPlaying) | ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
            ctx.sampleRate = RATE; ctx.tempo = tempo; ctx.projectTimeSamples = pos; ctx.projectTimeMusic = pos / double(RATE) * tempo / 60.0;
            data.processMode = offline ? kOffline : kRealtime;
            data.numSamples = n; data.inputEvents = &events; data.inputParameterChanges = &changes;
            data.outputParameterChanges = &outChanges; data.processContext = &ctx; data.outputEvents = &outEv;
            proc->process(data);
            if (rec && data.numOutputs > 0) {
                  float** ch = data.outputs[0].channelBuffers32;
                  for (int i = 0; i < n; ++i) { rec->push_back(ch[0][i]); rec->push_back(data.outputs[0].numChannels > 1 ? ch[1][i] : ch[0][i]); }
                  }
            if (!midiOut.empty() && !mo) mo = fopen(midiOut.c_str(), "w");
            if (mo) {
                  for (int32 k = 0; k < outEv.getEventCount(); ++k) { Event e {}; outEv.getEvent(k, e); double t = (pos + e.sampleOffset) / double(RATE);
                        if (e.type == Event::kNoteOnEvent) fprintf(mo, "%.4f on ch%d %d %d\n", t, e.noteOn.channel, e.noteOn.pitch, int(e.noteOn.velocity * 127 + 0.5));
                        else if (e.type == Event::kNoteOffEvent) fprintf(mo, "%.4f off ch%d %d %d\n", t, e.noteOff.channel, e.noteOff.pitch, int(e.noteOff.velocity * 127 + 0.5));
                        else if (e.type == Event::kLegacyMIDICCOutEvent) fprintf(mo, "%.4f cc ch%d %d %d %d\n", t, e.midiCCOut.channel, e.midiCCOut.controlNumber, e.midiCCOut.value, e.midiCCOut.value2);
                        else fprintf(mo, "%.4f type%d\n", t, e.type); }
                  for (int32 k = 0; k < outChanges.getParameterCount(); ++k) { IParamValueQueue* q = outChanges.getParameterData(k); for (int32 j = 0; j < q->getPointCount(); ++j) { int32 so; ParamValue v; q->getPoint(j, so, v); fprintf(mo, "%.4f outparam %u %.5f\n", (pos + so) / double(RATE), q->getParameterId(), v); } }
                  fflush(mo);
                  }
            outEv.clear(); events.clear(); changes.clearQueue(); outChanges.clearQueue();
            pos += n;
            };
      auto paced = std::chrono::steady_clock::now();
      long long pacedFrames = 0;
      auto runBlock = [&](int n, std::vector<float>* rec) {
            process(n, rec);
            pacedFrames += n;
            if (rt) { auto due = paced + std::chrono::microseconds((long long)(pacedFrames * 1e6 / RATE)); std::this_thread::sleep_until(due); }
            pump();
            };
      for (auto& s : sets) { int32 idx; IParamValueQueue* q = changes.addParameterData(s.first, idx); q->addPoint(0, s.second, idx); if (controller) controller->setParamNormalized(s.first, s.second); }
      // amphost4 --probe s: every 50 ms, the process's CPU time, thread count, working set, a hash of the
      // controller's parameters and of the component state (what could tell a finished sample load)
      if (probeS > 0) {
            HMODULE mod = nullptr; MODULEINFO mi {};
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)((*(void***)component.get())[0]), &mod);
            GetModuleInformation(GetCurrentProcess(), mod, &mi, sizeof(mi));
            typedef LONG (NTAPI *NQIT)(HANDLE, int, PVOID, ULONG, PULONG);
            NQIT nqit = (NQIT)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
            std::map<DWORD, double> lastT;
            auto st = std::chrono::steady_clock::now();
            double lastCpu = 0;
            while (std::chrono::duration<double>(std::chrono::steady_clock::now() - st).count() < probeS) {
                  FILETIME c, e, k, u; GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
                  auto ft = [](FILETIME f) { return (double(f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) / 1e7; };
                  const double cpu = ft(k) + ft(u);
                  int threads = 0; HANDLE sn = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0); THREADENTRY32 te { sizeof(te) };
                  int modThreads = 0; double modCpu = 0;
                  if (Thread32First(sn, &te)) do { if (te.th32OwnerProcessID == GetCurrentProcessId()) { ++threads;
                        HANDLE th = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID); void* sa = nullptr;
                        if (th && nqit) nqit(th, 9, &sa, sizeof(sa), nullptr);
                        if (th && (char*)sa >= (char*)mi.lpBaseOfDll && (char*)sa < (char*)mi.lpBaseOfDll + mi.SizeOfImage) {
                              ++modThreads; FILETIME a, b, kk, uu; GetThreadTimes(th, &a, &b, &kk, &uu);
                              auto f2 = [](FILETIME f) { return (double(f.dwHighDateTime) * 4294967296.0 + f.dwLowDateTime) / 1e7; };
                              double t = f2(kk) + f2(uu); modCpu += t - (lastT.count(te.th32ThreadID) ? lastT[te.th32ThreadID] : 0); lastT[te.th32ThreadID] = t; }
                        if (th) CloseHandle(th); } } while (Thread32Next(sn, &te));
                  CloseHandle(sn);
                  PROCESS_MEMORY_COUNTERS pmc {}; pmc.cb = sizeof(pmc);
                  GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
                  unsigned long long ph = 1469598103934665603ull;
                  if (controller) for (int32 i = 0; i < controller->getParameterCount(); ++i) { ParameterInfo pi; controller->getParameterInfo(i, pi);
                        double v = controller->getParamNormalized(pi.id); unsigned long long b; memcpy(&b, &v, 8); ph = (ph ^ b) * 1099511628211ull; }
                  IPtr<MemoryStream> ms = owned(new MemoryStream()); component->getState(ms);
                  unsigned long long sh = 1469598103934665603ull; const char* d = ms->getData(); for (TSize i = 0; i < ms->getSize(); ++i) sh = (sh ^ (unsigned char)d[i]) * 1099511628211ull;
                  fprintf(stderr, "probe mod threads %d cpu+ %.3f | ", modThreads, modCpu); fprintf(stderr, "%.2f s cpu %.3f (+%.3f) threads %d ws %.0f MB params %016llx state %lld %016llx\n",
                          std::chrono::duration<double>(std::chrono::steady_clock::now() - st).count(), cpu, cpu - lastCpu, threads,
                          pmc.WorkingSetSize / 1048576.0, ph, (long long)ms->getSize(), sh);
                  lastCpu = cpu;
                  pump();
                  std::this_thread::sleep_for(std::chrono::milliseconds(50));
                  }
            }
      // amphost4: --wait s: wall-clock sleep without processing (pumping unless --nopump); --fastpre s: silence processed at once
      { auto st = std::chrono::steady_clock::now();
        while (std::chrono::duration<double>(std::chrono::steady_clock::now() - st).count() < waitS) { pump(); std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
        bool keep = rt; rt = false;
        for (long long f = 0; f < (long long)(fastPre * RATE); f += block) runBlock(block, nullptr);
        rt = keep; fprintf(stderr, "wait %.2f s, fastpre %.2f s audio\n", waitS, fastPre); }
      // pre-roll: (sample loading) silence processed for --pre seconds, real-time paced so background loaders run
      { auto st = std::chrono::steady_clock::now(); bool keep = rt; rt = true;
        for (long long f = 0; f < (long long)(pre * RATE); f += block) runBlock(block, nullptr);
        rt = keep; fprintf(stderr, "pre-roll %.2f s wall\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - st).count()); }
      for (int r = 0; r < repeat; ++r) {
            std::vector<float> rec;
            const long long total = (long long)((end + tail) * RATE);
            size_t ei = 0;
            auto wall0 = std::chrono::steady_clock::now();
            for (long long f = 0; f < total; f += block) {
                  const int n = int(std::min<long long>(block, total - f));
                  while (ei < evs.size() && (long long)std::llround(evs[ei].t * RATE) < f + n) {
                        const Ev& e = evs[ei++];
                        const int off = int(std::max<long long>(0, std::llround(e.t * RATE) - f));
                        if (e.type == "on" || e.type == "off") {
                              Event ve {}; ve.busIndex = 0; ve.sampleOffset = off;
                              if (e.type == "on" && e.b > 0) { ve.type = Event::kNoteOnEvent; ve.noteOn.channel = int16(e.a / 1000); ve.noteOn.pitch = int16(e.a % 1000); ve.noteOn.velocity = float(e.b / 127.0); ve.noteOn.noteId = -1; }
                              else { ve.type = Event::kNoteOffEvent; ve.noteOff.channel = int16(e.a / 1000); ve.noteOff.pitch = int16(e.a % 1000); ve.noteOff.velocity = 0; ve.noteOff.noteId = -1; }
                              events.addEvent(ve);
                              }
                        else {
                              long id = -1; double v = 0;
                              if (e.type == "cc") { id = mapCC(e.a); v = e.b / 127.0; }
                              else if (e.type == "pb") { ParamID pid; id = (mm && mm->getMidiControllerAssignment(0, int16(e.b), kPitchBend, pid) == kResultOk) ? long(pid) : -1; v = e.a / 16383.0; }
                              else if (e.type == "cch") { ParamID pid; int ch = e.a / 1000, num = e.a % 1000; id = (mm && mm->getMidiControllerAssignment(0, int16(ch), CtrlNumber(num), pid) == kResultOk) ? long(pid) : -1; v = e.b / 127.0; }
                              else if (e.type == "param") { id = e.a; v = e.b; }
                              if (id < 0) { fprintf(stderr, "unmapped %s %d\n", e.type.c_str(), e.a); continue; }
                              int32 idx; IParamValueQueue* q = changes.addParameterData(ParamID(id), idx); int32 pi; q->addPoint(off, v, pi);
                              }
                        }
                  runBlock(n, &rec);
                  }
            double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
            std::string out = a2;
            if (repeat > 1) { size_t d = out.rfind('.'); out = out.substr(0, d) + "_" + std::to_string(r + 1) + out.substr(d); }
            writeWav(out, rec, RATE);
            fprintf(stderr, "wrote %s (%.2f s audio, %.2f s wall)\n", out.c_str(), rec.size() / 2.0 / RATE, wall);
            // gap between repeats: 2 s silence
            if (r + 1 < repeat) for (long long f = 0; f < 2LL * RATE; f += block) runBlock(block, nullptr);
            }
      if (!stateOut.empty()) { IPtr<MemoryStream> cs = owned(new MemoryStream); component->getState(cs); FILE* f = fopen(stateOut.c_str(), "wb"); fwrite(cs->getData(), 1, size_t(cs->getSize()), f); fclose(f); }
      fflush(stderr);
      _exit(0);
      }
