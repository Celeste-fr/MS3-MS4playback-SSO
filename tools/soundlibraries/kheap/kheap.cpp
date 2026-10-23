// kheap: the heap's share of Kontakt's load (sso-load, 2026-10-23). Loaded by ktprof (--trace / --dll) before
// Kontakt's module, so every allocation of the process goes through it from the start.
//
//   KHEAP=count (default): read-only. Counts and times the CRT's malloc family (ucrtbase) and ntdll's
//       RtlAllocateHeap / RtlFreeHeap / RtlReAllocateHeap (direct calls, i.e. not from inside a CRT call), by phase
//       (ktmark) and by caller module (Kontakt / ucrtbase / other). ktdump writes it.
//   KHEAP=mi: the CRT's malloc family answered by mimalloc (v2.1.7, static). A pointer mimalloc doesn't own (made
//       before the hooks, or by another allocator) goes to the original function. KHEAP_RTL=1 also answers Kontakt's
//       own direct HeapAlloc / HeapReAlloc calls (caller in Kontakt 8.vst3) by mimalloc; every RtlFreeHeap /
//       RtlSizeHeap / RtlReAllocateHeap of a mimalloc pointer goes to mimalloc, whoever calls it.
//   KHEAP=lfh / segment: nothing hooked (the process's heap settings are the experiment); counts only.
//
//   KHEAP_NOCOUNT=1: no counts and no timing (for speed runs). KHEAP_SKIP=crt|rtl|<function>[;…]: hooks left out (debugging).
//   The ntdll hooks stay on in mi mode: a mimalloc block that reaches HeapFree / HeapSize / HeapReAlloc goes to mimalloc.
//
// Measured (Windows VM, Kontakt 8.13.1 + SSO, Whence's 10 instance states, private/sso-load/RESULT.md): Violins 1 -
// Performance's setState makes 18.9 million malloc calls (2.2 GB, 1.5 million freed) from Kontakt on the calling thread;
// 2.73 s inside the CRT's heap against 1.31 s inside mimalloc; setState -12 % (median of alternating pairs, 7 of 8
// faster; the VM's run-to-run noise is +-30 %), page faults -23 %, private memory +17 % (Whence 11.3 -> 13.3 GB).
// The allocator decides where a block lies, never what is in it: the bytes Kontakt computes are the
// same unless it reads memory it never wrote; MuseScore's verify renders of Whence with and without it null at the floor
// of two identical passes in every part.
//
// Load it before Kontakt's module: ktload --dll / --trace, or launchw <kheap.dll> <program> (launchw.cpp here).
// kheap keeps its per-thread state in a TlsAlloc slot read from the TEB, not thread_local: ntdll allocates a new thread's TLS block
// through RtlAllocateHeap before the thread has this DLL's implicit TLS (that crashed every Kontakt load).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "MinHook.h"
#include "mimalloc.h"

#pragma intrinsic(_ReturnAddress)

// "inside one of our hooks" per thread. Not thread_local: ntdll allocates a new thread's TLS block with RtlAllocateHeap
// before that thread has ours (an implicit-TLS access there crashed every Kontakt load). A TlsAlloc slot read from the
// TEB directly (TlsGetValue would reset the last error).
static DWORD g_tls = 0xffffffff;
static inline intptr_t tinGet() { return g_tls < 64 ? (intptr_t)__readgsqword(0x1480 + 8 * g_tls) : 1; }
static inline void tinSet(intptr_t v) { if (g_tls < 64) __writegsqword(0x1480 + 8 * g_tls, (unsigned long long)v); }
struct TIn {
      operator intptr_t() const { return tinGet(); }
      TIn& operator++() { tinSet(tinGet() + 1); return *this; }
      TIn& operator--() { tinSet(tinGet() - 1); return *this; }
      };
static TIn t_in;
static bool g_mi = false, g_miRtl = false, g_count = true;   // g_count false (KHEAP_NOCOUNT=1): no timing, no counts
static uintptr_t g_kBase = 0, g_kEnd = 0, g_uBase = 0, g_uEnd = 0;
static LARGE_INTEGER g_freq;

enum Api { MALLOC, FREE, REALLOC, CALLOC, AMALLOC, AFREE, AREALLOC, MSIZE, RTL_ALLOC, RTL_FREE, RTL_REALLOC, RTL_SIZE, NAPI };
static const char* kApi[NAPI] = { "malloc", "free", "realloc", "calloc", "_aligned_malloc", "_aligned_free", "_aligned_realloc",
                                  "_msize", "RtlAllocateHeap", "RtlFreeHeap", "RtlReAllocateHeap", "RtlSizeHeap" };
enum Caller { C_KONTAKT, C_UCRT, C_OTHER, NCALLER };
static const char* kCaller[NCALLER] = { "Kontakt", "ucrtbase", "other" };
static const int MAXPH = 256;
struct Cell { std::atomic<long long> calls { 0 }, ticks { 0 }, bytes { 0 }, mi { 0 }; };
static Cell g_cell[MAXPH][NAPI][NCALLER][2];   // [phase][api][caller][main thread?]
static std::atomic<int> g_phase { 0 };
static char g_phaseName[MAXPH][160];
static double g_phaseT[MAXPH];
static DWORD g_mainTid = 0;
static LARGE_INTEGER g_t0;

static Caller callerOf(void* ra)
      {
      const uintptr_t a = (uintptr_t)ra;
      if (a >= g_kBase && a < g_kEnd) return C_KONTAKT;
      if (a >= g_uBase && a < g_uEnd) return C_UCRT;
      return C_OTHER;
      }

static inline long long qpc() { if (!g_count) return 0; LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static inline void note(Api api, void* ra, long long t0, size_t bytes, bool mi)
      {
      if (!g_count) return;
      Cell& c = g_cell[g_phase.load(std::memory_order_relaxed)][api][callerOf(ra)][GetCurrentThreadId() == g_mainTid];
      c.calls.fetch_add(1, std::memory_order_relaxed);
      c.ticks.fetch_add(qpc() - t0, std::memory_order_relaxed);
      if (bytes) c.bytes.fetch_add((long long)bytes, std::memory_order_relaxed);
      if (mi) c.mi.fetch_add(1, std::memory_order_relaxed);
      }

static inline bool ours(const void* p) { return p && mi_is_in_heap_region(p); }

// --- the CRT (ucrtbase) ---
typedef void* (__cdecl* malloc_t)(size_t);
typedef void (__cdecl* free_t)(void*);
typedef void* (__cdecl* realloc_t)(void*, size_t);
typedef void* (__cdecl* calloc_t)(size_t, size_t);
typedef void* (__cdecl* amalloc_t)(size_t, size_t);
typedef void* (__cdecl* arealloc_t)(void*, size_t, size_t);
typedef size_t (__cdecl* msize_t)(void*);
typedef void* (__cdecl* recalloc_t)(void*, size_t, size_t);
typedef void* (__cdecl* expand_t)(void*, size_t);
static malloc_t o_malloc, o_malloc_base;
static free_t o_free, o_free_base, o_afree;
static realloc_t o_realloc, o_realloc_base;
static calloc_t o_calloc, o_calloc_base;
static amalloc_t o_amalloc;
static arealloc_t o_arealloc;
static msize_t o_msize, o_msize_base;
static recalloc_t o_recalloc, o_recalloc_base;
static expand_t o_expand, o_expand_base;

#define GUARD(orig_call) if (t_in) return orig_call; ++t_in; const long long t0 = qpc();
static void* doMalloc(malloc_t o, size_t n, void* ra)
      {
      GUARD(o(n))
      void* p = g_mi ? mi_malloc(n) : o(n);
      note(MALLOC, ra, t0, n, g_mi); --t_in; return p;
      }
static void doFree(free_t o, void* p, void* ra)
      {
      if (t_in) { o(p); return; }
      ++t_in; const long long t0 = qpc();
      const bool mi = g_mi && ours(p);
      if (mi) mi_free(p); else o(p);
      note(FREE, ra, t0, 0, mi); --t_in;
      }
static void* doRealloc(realloc_t o, void* p, size_t n, void* ra)
      {
      GUARD(o(p, n))
      void* r;
      bool mi = false;
      if (!g_mi) r = o(p, n);
      else if (!p || ours(p)) { r = mi_realloc(p, n); mi = true; }
      else if (n == 0) r = o(p, 0);
      else {        // a block made before the hooks: move it into mimalloc
            const size_t old = o_msize_base ? o_msize_base(p) : o_msize(p);
            r = mi_malloc(n);
            if (r) { memcpy(r, p, old < n ? old : n); o_free_base ? o_free_base(p) : o_free(p); }
            mi = true;
            }
      note(REALLOC, ra, t0, n, mi); --t_in; return r;
      }
static void* doCalloc(calloc_t o, size_t a, size_t b, void* ra)
      {
      GUARD(o(a, b))
      void* p = g_mi ? mi_calloc(a, b) : o(a, b);
      note(CALLOC, ra, t0, a * b, g_mi); --t_in; return p;
      }
static size_t doMsize(msize_t o, void* p, void* ra)
      {
      if (t_in) return o(p);
      ++t_in; const long long t0 = qpc();
      const bool mi = g_mi && ours(p);
      const size_t r = mi ? mi_usable_size(p) : o(p);
      note(MSIZE, ra, t0, 0, mi); --t_in; return r;
      }
static void* doRecalloc(recalloc_t o, void* p, size_t a, size_t b, void* ra)
      {
      GUARD(o(p, a, b))
      void* r;
      bool mi = false;
      if (!g_mi) r = o(p, a, b);
      else if (!p || ours(p)) { r = mi_recalloc(p, a, b); mi = true; }
      else r = o(p, a, b);       // (not seen; left to the CRT)
      note(REALLOC, ra, t0, a * b, mi); --t_in; return r;
      }
static void* doExpand(expand_t o, void* p, size_t n, void* ra)
      {
      GUARD(o(p, n))
      void* r = (g_mi && ours(p)) ? mi_expand(p, n) : o(p, n);
      note(REALLOC, ra, t0, n, g_mi && ours(p)); --t_in; return r;
      }
static void* __cdecl h_malloc(size_t n) { return doMalloc(o_malloc, n, _ReturnAddress()); }
static void* __cdecl h_malloc_base(size_t n) { return doMalloc(o_malloc_base, n, _ReturnAddress()); }
static void __cdecl h_free(void* p) { doFree(o_free, p, _ReturnAddress()); }
static void __cdecl h_free_base(void* p) { doFree(o_free_base, p, _ReturnAddress()); }
static void* __cdecl h_realloc(void* p, size_t n) { return doRealloc(o_realloc, p, n, _ReturnAddress()); }
static void* __cdecl h_realloc_base(void* p, size_t n) { return doRealloc(o_realloc_base, p, n, _ReturnAddress()); }
static void* __cdecl h_calloc(size_t a, size_t b) { return doCalloc(o_calloc, a, b, _ReturnAddress()); }
static void* __cdecl h_calloc_base(size_t a, size_t b) { return doCalloc(o_calloc_base, a, b, _ReturnAddress()); }
static size_t __cdecl h_msize(void* p) { return doMsize(o_msize, p, _ReturnAddress()); }
static size_t __cdecl h_msize_base(void* p) { return doMsize(o_msize_base, p, _ReturnAddress()); }
static void* __cdecl h_recalloc(void* p, size_t a, size_t b) { return doRecalloc(o_recalloc, p, a, b, _ReturnAddress()); }
static void* __cdecl h_recalloc_base(void* p, size_t a, size_t b) { return doRecalloc(o_recalloc_base, p, a, b, _ReturnAddress()); }
static void* __cdecl h_expand(void* p, size_t n) { return doExpand(o_expand, p, n, _ReturnAddress()); }
static void* __cdecl h_expand_base(void* p, size_t n) { return doExpand(o_expand_base, p, n, _ReturnAddress()); }
static void* __cdecl h_amalloc(size_t n, size_t al)
      {
      GUARD(o_amalloc(n, al))
      void* ra = _ReturnAddress();
      void* p = g_mi ? mi_malloc_aligned(n, al) : o_amalloc(n, al);
      note(AMALLOC, ra, t0, n, g_mi); --t_in; return p;
      }
static void __cdecl h_afree(void* p)
      {
      if (t_in) { o_afree(p); return; }
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      const bool mi = g_mi && ours(p);
      if (mi) mi_free(p); else o_afree(p);
      note(AFREE, ra, t0, 0, mi); --t_in;
      }
static void* __cdecl h_arealloc(void* p, size_t n, size_t al)
      {
      GUARD(o_arealloc(p, n, al))
      void* ra = _ReturnAddress();
      void* r;
      bool mi = false;
      if (!g_mi) r = o_arealloc(p, n, al);
      else if (!p || ours(p)) { r = mi_realloc_aligned(p, n, al); mi = true; }
      else r = o_arealloc(p, n, al);      // (a block from before the hooks stays the CRT's)
      note(AREALLOC, ra, t0, n, mi); --t_in; return r;
      }

// --- ntdll's heap (direct calls; inside a CRT call they are counted by the CRT's hook) ---
typedef PVOID (NTAPI* RtlAllocateHeap_t)(PVOID, ULONG, SIZE_T);
typedef BOOLEAN (NTAPI* RtlFreeHeap_t)(PVOID, ULONG, PVOID);
typedef PVOID (NTAPI* RtlReAllocateHeap_t)(PVOID, ULONG, PVOID, SIZE_T);
typedef SIZE_T (NTAPI* RtlSizeHeap_t)(PVOID, ULONG, PVOID);
static RtlAllocateHeap_t o_RtlAllocateHeap;
static RtlFreeHeap_t o_RtlFreeHeap;
static RtlReAllocateHeap_t o_RtlReAllocateHeap;
static RtlSizeHeap_t o_RtlSizeHeap;

static PVOID NTAPI h_RtlAllocateHeap(PVOID heap, ULONG flags, SIZE_T n)
      {
      if (t_in) return o_RtlAllocateHeap(heap, flags, n);
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      const bool mi = g_miRtl && callerOf(ra) == C_KONTAKT && !(flags & ~(HEAP_ZERO_MEMORY | HEAP_NO_SERIALIZE | HEAP_GENERATE_EXCEPTIONS));
      PVOID p = mi ? ((flags & HEAP_ZERO_MEMORY) ? mi_zalloc(n) : mi_malloc(n)) : o_RtlAllocateHeap(heap, flags, n);
      note(RTL_ALLOC, ra, t0, n, mi); --t_in; return p;
      }
static BOOLEAN NTAPI h_RtlFreeHeap(PVOID heap, ULONG flags, PVOID p)
      {
      if (t_in) return (g_mi && ours(p)) ? (mi_free(p), TRUE) : o_RtlFreeHeap(heap, flags, p);
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      const bool mi = g_mi && ours(p);
      BOOLEAN r = mi ? (mi_free(p), TRUE) : o_RtlFreeHeap(heap, flags, p);
      note(RTL_FREE, ra, t0, 0, mi); --t_in; return r;
      }
static PVOID NTAPI h_RtlReAllocateHeap(PVOID heap, ULONG flags, PVOID p, SIZE_T n)
      {
      const bool mine = g_mi && ours(p);
      if (t_in && !mine) return o_RtlReAllocateHeap(heap, flags, p, n);
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      PVOID r;
      if (mine) {
            if (flags & HEAP_REALLOC_IN_PLACE_ONLY) r = mi_expand(p, n);
            else r = (flags & HEAP_ZERO_MEMORY) ? mi_rezalloc(p, n) : mi_realloc(p, n);
            }
      else r = o_RtlReAllocateHeap(heap, flags, p, n);
      note(RTL_REALLOC, ra, t0, n, mine); --t_in; return r;
      }
static SIZE_T NTAPI h_RtlSizeHeap(PVOID heap, ULONG flags, PVOID p)
      {
      if (g_mi && ours(p)) return mi_usable_size(p);
      return o_RtlSizeHeap(heap, flags, p);
      }

template <class F> static void hook(HMODULE m, const char* name, F h, F* o)
      {
      void* p = m ? (void*)GetProcAddress(m, name) : nullptr;
      if (p && MH_CreateHook(p, (void*)h, (void**)o) == MH_OK) MH_EnableHook(p);
      }

static void findModules()
      {
      if (!g_kBase) {
            HMODULE k = GetModuleHandleW(L"Kontakt 8.vst3");
            if (k) {
                  auto dos = (IMAGE_DOS_HEADER*)k;
                  auto nt = (IMAGE_NT_HEADERS*)((BYTE*)k + dos->e_lfanew);
                  g_kEnd = (uintptr_t)k + nt->OptionalHeader.SizeOfImage;
                  g_kBase = (uintptr_t)k;
                  }
            }
      }

extern "C" __declspec(dllexport) void ktmark(const char* name)
      {
      ++t_in;
      if (!g_mainTid) g_mainTid = GetCurrentThreadId();
      findModules();
      const int k = g_phase.load() + 1;
      if (k < MAXPH) {
            snprintf(g_phaseName[k], sizeof g_phaseName[k], "%s", name);
            { LARGE_INTEGER t; QueryPerformanceCounter(&t); g_phaseT[k] = double(t.QuadPart - g_t0.QuadPart) / g_freq.QuadPart; }
            g_phase.store(k);
            }
      --t_in;
      }

extern "C" __declspec(dllexport) void ktdump(const char* path)
      {
      ++t_in;
      FILE* f = fopen(path, "w");
      if (!f) { --t_in; return; }
      const char* mode = getenv("KHEAP");
      fprintf(f, "kheap mode %s rtl %d\n", mode ? mode : "count", int(g_miRtl));
      const int n = g_phase.load();
      fprintf(f, "phase\tt\tname\tapi\tcaller\tthread\tcalls\tms\tMB\tmi\n");
      for (int k = 0; k <= n; ++k)
            for (int a = 0; a < NAPI; ++a)
                  for (int c = 0; c < NCALLER; ++c)
                        for (int t = 0; t < 2; ++t) {
                              Cell& x = g_cell[k][a][c][t];
                              if (!x.calls) continue;
                              fprintf(f, "%d\t%.3f\t%s\t%s\t%s\t%s\t%lld\t%.1f\t%.1f\t%lld\n", k, g_phaseT[k], k ? g_phaseName[k] : "start",
                                      kApi[a], kCaller[c], t ? "main" : "other", x.calls.load(), 1000.0 * x.ticks.load() / g_freq.QuadPart,
                                      x.bytes.load() / 1048576.0, x.mi.load());
                              }
      mi_stats_print_out([](const char* msg, void* arg) { fputs(msg, (FILE*)arg); }, f);
      fclose(f);
      --t_in;
      }

static LONG CALLBACK onCrash(EXCEPTION_POINTERS* e)
      {
      const DWORD code = e->ExceptionRecord->ExceptionCode;
      if (code != EXCEPTION_ACCESS_VIOLATION && code != 0xC0000374 && code != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
      static int once = 0;
      if (once++ > 3) return EXCEPTION_CONTINUE_SEARCH;
      ++t_in;
      char path[MAX_PATH]; DWORD n = GetEnvironmentVariableA("KHEAP_CRASH", path, MAX_PATH);
      FILE* f = fopen(n ? path : "C:\\claude\\ssoload\\out\\kheap-crash.txt", "a");
      if (f) {
            void* frames[32]; USHORT k = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
            fprintf(f, "exception %08lx at %p, info %llx %llx\n", code, e->ExceptionRecord->ExceptionAddress,
                    (unsigned long long)e->ExceptionRecord->ExceptionInformation[0], (unsigned long long)e->ExceptionRecord->ExceptionInformation[1]);
            void* pcs[1] = { e->ExceptionRecord->ExceptionAddress };
            for (int i = -1; i < k; ++i) {
                  void* a = i < 0 ? pcs[0] : frames[i];
                  HMODULE m = nullptr; char name[MAX_PATH] = "?";
                  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)a, &m))
                        GetModuleFileNameA(m, name, MAX_PATH);
                  const char* b = strrchr(name, '\\');
                  fprintf(f, "   %s+0x%llx\n", b ? b + 1 : name, (unsigned long long)((uintptr_t)a - (uintptr_t)m));
                  }
            // the faulting thread's stack, from its context
            CONTEXT c = *e->ContextRecord;
            for (int i = 0; i < 24; ++i) {
                  DWORD64 base = 0; auto fn = RtlLookupFunctionEntry(c.Rip, &base, nullptr);
                  if (!fn) { c.Rip = *(DWORD64*)c.Rsp; c.Rsp += 8; }
                  else { void* hd; DWORD64 ef; RtlVirtualUnwind(0, base, c.Rip, fn, &c, &hd, &ef, nullptr); }
                  if (!c.Rip) break;
                  HMODULE m = nullptr; char name[MAX_PATH] = "?";
                  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)c.Rip, &m))
                        GetModuleFileNameA(m, name, MAX_PATH);
                  const char* b = strrchr(name, '\\');
                  fprintf(f, "   ctx %s+0x%llx\n", b ? b + 1 : name, (unsigned long long)(c.Rip - (uintptr_t)m));
                  }
            fclose(f);
            }
      --t_in;
      return EXCEPTION_CONTINUE_SEARCH;
      }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
      {
      if (reason != DLL_PROCESS_ATTACH) return TRUE;
      QueryPerformanceFrequency(&g_freq);
      QueryPerformanceCounter(&g_t0);
      g_tls = TlsAlloc();
      if (g_tls >= 64) return TRUE;       // (no fast slot: nothing hooked)
      const char* mode = getenv("KHEAP");
      g_mi = mode && !strcmp(mode, "mi");
      const char* rtl = getenv("KHEAP_RTL");
      g_miRtl = g_mi && rtl && !strcmp(rtl, "1");
      const char* noCount = getenv("KHEAP_NOCOUNT");      // "1": no counts, no timing (the speed runs; with counting, QueryPerformanceCounter alone cost ~11 %)
      AddVectoredExceptionHandler(1, onCrash);
      HMODULE u = GetModuleHandleW(L"ucrtbase.dll");
      if (!u) u = LoadLibraryW(L"ucrtbase.dll");
      {
            auto dos = (IMAGE_DOS_HEADER*)u;
            auto nt = (IMAGE_NT_HEADERS*)((BYTE*)u + dos->e_lfanew);
            g_uBase = (uintptr_t)u; g_uEnd = g_uBase + nt->OptionalHeader.SizeOfImage;
      }
      HMODULE nt = GetModuleHandleW(L"ntdll.dll");
      ++t_in;
      mi_option_set(mi_option_verbose, 0);
      void* warm = mi_malloc(16); mi_free(warm);    // (mimalloc's process init before any hook)
      MH_Initialize();
      const char* skip = getenv("KHEAP_SKIP");       // (debugging: "crt", "rtl", or a hook's name)
      auto want = [&](const char* name, bool rtl) {
            if (!skip) return true;
            std::string list = std::string(",") + skip + ",";
            for (auto& ch : list) if (ch == ';') ch = ',';
            return list.find(std::string(",") + (rtl ? "rtl" : "crt") + ",") == std::string::npos
                && list.find(std::string(",") + name + ",") == std::string::npos;
            };
#define hook(m, name, h, o) if (want(name, m == nt)) hook(m, name, h, o)
      hook(u, "malloc", h_malloc, &o_malloc);
      hook(u, "_malloc_base", h_malloc_base, &o_malloc_base);
      hook(u, "free", h_free, &o_free);
      hook(u, "_free_base", h_free_base, &o_free_base);
      hook(u, "realloc", h_realloc, &o_realloc);
      hook(u, "_realloc_base", h_realloc_base, &o_realloc_base);
      hook(u, "calloc", h_calloc, &o_calloc);
      hook(u, "_calloc_base", h_calloc_base, &o_calloc_base);
      hook(u, "_msize", h_msize, &o_msize);
      hook(u, "_msize_base", h_msize_base, &o_msize_base);
      hook(u, "_recalloc", h_recalloc, &o_recalloc);
      hook(u, "_recalloc_base", h_recalloc_base, &o_recalloc_base);
      hook(u, "_expand", h_expand, &o_expand);
      hook(u, "_expand_base", h_expand_base, &o_expand_base);
      hook(u, "_aligned_malloc", h_amalloc, &o_amalloc);
      hook(u, "_aligned_free", h_afree, &o_afree);
      hook(u, "_aligned_realloc", h_arealloc, &o_arealloc);
      g_count = !(noCount && !strcmp(noCount, "1"));
      {
            hook(nt, "RtlAllocateHeap", h_RtlAllocateHeap, &o_RtlAllocateHeap);
            hook(nt, "RtlFreeHeap", h_RtlFreeHeap, &o_RtlFreeHeap);
            hook(nt, "RtlReAllocateHeap", h_RtlReAllocateHeap, &o_RtlReAllocateHeap);
            hook(nt, "RtlSizeHeap", h_RtlSizeHeap, &o_RtlSizeHeap);
            }
      --t_in;
      return TRUE;
      }
