// kheap: mimalloc for the whole process in place of the C runtime's heap (sso-load, 2026-10-23), and the
// measurement of that heap's share of Kontakt's load.
//
// MuseScore on Windows loads it first thing in main() (audio/vst3/processallocator.cpp: kheap_start("mi"), before any
// plug-in exists; MS_KHEAP_OFF=1 leaves the system allocator, to compare): from then on the CRT's malloc family
// (ucrtbase: malloc, free, realloc, calloc, _msize, _expand, _recalloc, their _base forms and the whole _aligned family)
// is answered by mimalloc 2.1.7 (static). Rules that keep every block on its own heap:
//   - a block mimalloc doesn't own (mi_is_in_heap_region: made before the switch, by Qt's start-up, inside a hook, or by
//     another heap) always goes to the original function; realloc / _recalloc / the aligned reallocs of such a block
//     move it into mimalloc (copied, the old one freed by the CRT) or leave it the CRT's;
//   - ntdll's RtlFreeHeap / RtlSizeHeap / RtlReAllocateHeap give a mimalloc block to mimalloc, whoever calls them
//     (code that frees a malloc block with HeapFree on the CRT's heap);
//   - all hooks or none (start()); the DLL is never unloaded and does nothing at detach: Qt's, the CRT's and the
//     plug-ins' teardown (Vst3Plugin::unloadModules' ExitDll, static destructors after main) still free through it.
//
// The tools (ktload --dll / --trace, launchw) start it from the environment at load instead:
//   KHEAP=count: read-only. Counts and times the CRT's malloc family and ntdll's RtlAllocateHeap / RtlFreeHeap /
//       RtlReAllocateHeap (direct calls, not from inside a CRT call), by phase (ktmark) and by caller module (Kontakt /
//       ucrtbase / other). ktdump writes it.
//   KHEAP=mi: as MuseScore's; KHEAP_RTL=1 also answers Kontakt's own direct HeapAlloc / HeapReAlloc (4 calls in a load).
//   KHEAP_NOCOUNT=1: no counts and no timing (speed runs). KHEAP_SKIP=crt|rtl|<function>[;…] (count mode only),
//   KHEAP_CRASH=<file> (the first exceptions to a file): debugging.
//
// Measured (Windows VM, Kontakt 8.13.1 + SSO, Whence's 10 instance states, private/sso-load/RESULT.md): Violins 1 -
// Performance's setState makes 18.9 million malloc calls (2.2 GB, 1.5 million freed) from Kontakt on the calling thread;
// 2.73 s inside the CRT's heap against 1.31 s inside mimalloc; setState -12 % (median of alternating pairs, 7 of 8
// faster; the VM's run-to-run noise is +-30 %), page faults -23 %, private memory +17 % (Whence 11.3 -> 13.3 GB).
// The owner's PC (Ryzen 5 5600X, 15 states, 5 + 5 alternating): setState in all 56.7 -> 48.2 s median (-15 %), memory 14.5 ->
// 16.9 GB. The allocator decides where a block lies, never what is in it: the bytes Kontakt computes are the same
// unless it reads memory it never wrote; MuseScore's verify renders of Whence with and without it null at the floor of
// two identical passes in every part (HANDOFF.md › SSO load).
//
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

// A block is mimalloc's when mi_is_in_heap_region says so; every other block (made before the hooks, inside a hook, or
// by another heap) goes to the original function, also while inside a hook (t_in), so no block ever reaches the wrong
// heap. Inside a hook, new blocks come from the original functions (the CRT's own nested calls stay the CRT's).
#define GUARD(orig_call) if (t_in) return orig_call; ++t_in; const long long t0 = qpc();
static void* doMalloc(malloc_t o, size_t n, void* ra)
      {
      GUARD(o(n))
      void* p = g_mi ? mi_malloc(n) : o(n);
      note(MALLOC, ra, t0, n, g_mi); --t_in; return p;
      }
static void doFree(free_t o, void* p, void* ra)
      {
      const bool mi = g_mi && ours(p);
      if (t_in) { if (mi) mi_free(p); else o(p); return; }
      ++t_in; const long long t0 = qpc();
      if (mi) mi_free(p); else o(p);
      note(FREE, ra, t0, 0, mi); --t_in;
      }
static void* doRealloc(realloc_t o, void* p, size_t n, void* ra)
      {
      const bool mine = g_mi && ours(p);
      if (t_in && !mine) return o(p, n);
      ++t_in; const long long t0 = qpc();
      void* r;
      bool mi = false;
      if (!g_mi) r = o(p, n);
      else if (mine && n == 0) { mi_free(p); r = nullptr; mi = true; }     // (the CRT's realloc(p, 0): freed, NULL)
      else if (mine || !p) { r = mi_realloc(p, n); mi = true; }
      else if (!p || n == 0) r = o(p, n);
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
      const bool mi = g_mi && ours(p);
      if (t_in) return mi ? mi_usable_size(p) : o(p);
      ++t_in; const long long t0 = qpc();
      const size_t r = mi ? mi_usable_size(p) : o(p);
      note(MSIZE, ra, t0, 0, mi); --t_in; return r;
      }
static void* doRecalloc(recalloc_t o, void* p, size_t a, size_t b, void* ra)
      {
      const bool mine = g_mi && ours(p);
      if (t_in && !mine) return o(p, a, b);
      ++t_in; const long long t0 = qpc();
      void* r;
      bool mi = false;
      if (!g_mi) r = o(p, a, b);
      else if (mine || !p) { r = mi_recalloc(p, a, b); mi = true; }
      else r = o(p, a, b);       // (a block from before the hooks stays the CRT's)
      note(REALLOC, ra, t0, a * b, mi); --t_in; return r;
      }
static void* doExpand(expand_t o, void* p, size_t n, void* ra)
      {
      const bool mine = g_mi && ours(p);
      if (t_in) return mine ? mi_expand(p, n) : o(p, n);
      ++t_in; const long long t0 = qpc();
      void* r = mine ? mi_expand(p, n) : o(p, n);
      note(REALLOC, ra, t0, n, mine); --t_in; return r;
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

// The aligned family: the CRT's own return a pointer inside a malloc block, which mimalloc's free can't take, so every
// aligned function is answered here (mimalloc's aligned blocks are freed by mi_free from any of their pointers).
typedef void* (__cdecl* aomalloc_t)(size_t, size_t, size_t);
typedef void* (__cdecl* aorealloc_t)(void*, size_t, size_t, size_t);
typedef void* (__cdecl* arecalloc_t)(void*, size_t, size_t, size_t);
typedef void* (__cdecl* aorecalloc_t)(void*, size_t, size_t, size_t, size_t);
typedef size_t (__cdecl* amsize_t)(void*, size_t, size_t);
static aomalloc_t o_aomalloc;
static aorealloc_t o_aorealloc;
static arecalloc_t o_arecalloc;
static aorecalloc_t o_aorecalloc;
static amsize_t o_amsize;
static void* __cdecl h_amalloc(size_t n, size_t al)
      {
      GUARD(o_amalloc(n, al))
      void* ra = _ReturnAddress();
      void* p = g_mi ? mi_malloc_aligned(n, al < sizeof(void*) ? sizeof(void*) : al) : o_amalloc(n, al);
      note(AMALLOC, ra, t0, n, g_mi); --t_in; return p;
      }
static void* __cdecl h_aomalloc(size_t n, size_t al, size_t off)
      {
      GUARD(o_aomalloc(n, al, off))
      void* ra = _ReturnAddress();
      void* p = g_mi ? mi_malloc_aligned_at(n, al < sizeof(void*) ? sizeof(void*) : al, off) : o_aomalloc(n, al, off);
      note(AMALLOC, ra, t0, n, g_mi); --t_in; return p;
      }
static void __cdecl h_afree(void* p)
      {
      const bool mi = g_mi && ours(p);
      if (t_in) { if (mi) mi_free(p); else o_afree(p); return; }
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      if (mi) mi_free(p); else o_afree(p);
      note(AFREE, ra, t0, 0, mi); --t_in;
      }
// a block of the CRT's aligned family made before the hooks: into mimalloc, the old one freed by the CRT
static void* moveAligned(void* p, size_t n, size_t al, size_t off, bool zero, size_t count)
      {
      const size_t old = o_amsize ? o_amsize(p, al, off) : 0;
      void* r = mi_malloc_aligned_at(n, al < sizeof(void*) ? sizeof(void*) : al, off);
      if (r) {
            memcpy(r, p, old < n ? old : n);
            if (zero && n > old) memset((char*)r + old, 0, n - old);
            o_afree(p);
            }
      (void)count;
      return r;
      }
static void* __cdecl h_arealloc(void* p, size_t n, size_t al)
      {
      const bool mine = g_mi && ours(p);
      if (t_in && !mine) return o_arealloc(p, n, al);
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      void* r;
      bool mi = g_mi;
      if (!g_mi) r = o_arealloc(p, n, al);
      else if (mine && n == 0) { mi_free(p); r = nullptr; }
      else if (mine || !p) r = mi_realloc_aligned(p, n, al < sizeof(void*) ? sizeof(void*) : al);
      else if (n == 0) { r = o_arealloc(p, 0, al); mi = false; }
      else r = moveAligned(p, n, al, 0, false, 0);
      note(AREALLOC, ra, t0, n, mi); --t_in; return r;
      }
static void* __cdecl h_aorealloc(void* p, size_t n, size_t al, size_t off)
      {
      const bool mine = g_mi && ours(p);
      if (t_in && !mine) return o_aorealloc(p, n, al, off);
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      void* r;
      bool mi = g_mi;
      if (!g_mi) r = o_aorealloc(p, n, al, off);
      else if (mine && n == 0) { mi_free(p); r = nullptr; }
      else if (mine || !p) r = mi_realloc_aligned_at(p, n, al < sizeof(void*) ? sizeof(void*) : al, off);
      else if (n == 0) { r = o_aorealloc(p, 0, al, off); mi = false; }
      else r = moveAligned(p, n, al, off, false, 0);
      note(AREALLOC, ra, t0, n, mi); --t_in; return r;
      }
static void* __cdecl h_arecalloc(void* p, size_t c, size_t n, size_t al)
      {
      const bool mine = g_mi && ours(p);
      if (t_in && !mine) return o_arecalloc(p, c, n, al);
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      void* r;
      bool mi = g_mi;
      if (!g_mi) r = o_arecalloc(p, c, n, al);
      else if (mine || !p) r = mi_recalloc_aligned(p, c, n, al < sizeof(void*) ? sizeof(void*) : al);
      else { r = o_arecalloc(p, c, n, al); mi = false; }       // (a block from before the hooks stays the CRT's)
      note(AREALLOC, ra, t0, c * n, mi); --t_in; return r;
      }
static void* __cdecl h_aorecalloc(void* p, size_t c, size_t n, size_t al, size_t off)
      {
      const bool mine = g_mi && ours(p);
      if (t_in && !mine) return o_aorecalloc(p, c, n, al, off);
      ++t_in; const long long t0 = qpc(); void* ra = _ReturnAddress();
      void* r;
      bool mi = g_mi;
      if (!g_mi) r = o_aorecalloc(p, c, n, al, off);
      else if (mine || !p) r = mi_recalloc_aligned_at(p, c, n, al < sizeof(void*) ? sizeof(void*) : al, off);
      else { r = o_aorecalloc(p, c, n, al, off); mi = false; }
      note(AREALLOC, ra, t0, c * n, mi); --t_in; return r;
      }
static size_t __cdecl h_amsize(void* p, size_t al, size_t off)
      {
      if (g_mi && ours(p)) return mi_usable_size(p);
      return o_amsize(p, al, off);
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
      FILE* f = n ? fopen(path, "a") : nullptr;
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

// start(): every hook made first, then all enabled at once (MinHook holds the other threads meanwhile), or none: a
// malloc answered by mimalloc while its free still went to the CRT would hand the CRT a block it doesn't own. A function
// ucrtbase doesn't export is left out (then nothing can make its blocks); one that is an alias of another already hooked
// (MH_ERROR_ALREADY_CREATED) is covered by that hook.
static std::atomic<int> g_started { 0 };       // 0 not yet, 1 hooks on, -1 failed (system allocator)
static char g_why[200] = "not started";

static int start(const char* mode, bool count)
      {
      if (g_started.load()) return g_started.load() > 0;
      if (g_tls >= 64) { snprintf(g_why, sizeof g_why, "no TLS slot below 64 (%lu)", g_tls); g_started = -1; return 0; }
      ++t_in;
      g_mi = mode && !strcmp(mode, "mi");
      const char* rtl = getenv("KHEAP_RTL");
      g_miRtl = g_mi && rtl && !strcmp(rtl, "1");
      g_count = count;
      HMODULE u = GetModuleHandleW(L"ucrtbase.dll");
      if (!u) u = LoadLibraryW(L"ucrtbase.dll");
      HMODULE nt = GetModuleHandleW(L"ntdll.dll");
      if (!u || !nt) { snprintf(g_why, sizeof g_why, "ucrtbase.dll or ntdll.dll not found"); g_started = -1; --t_in; return 0; }
      {
            auto dos = (IMAGE_DOS_HEADER*)u;
            auto h = (IMAGE_NT_HEADERS*)((BYTE*)u + dos->e_lfanew);
            g_uBase = (uintptr_t)u; g_uEnd = g_uBase + h->OptionalHeader.SizeOfImage;
      }
      mi_option_set(mi_option_verbose, 0);
      void* warm = mi_malloc(16); mi_free(warm);    // (mimalloc's process init before any hook)
      if (MH_Initialize() != MH_OK) { snprintf(g_why, sizeof g_why, "MinHook did not initialise"); g_started = -1; --t_in; return 0; }
      const char* skip = getenv("KHEAP_SKIP");       // (debugging: "crt", "rtl", or a hook's name; never with mi)
      auto want = [&](const char* name, bool isRtl) {
            if (!skip || g_mi) return true;
            std::string list = std::string(",") + skip + ",";
            for (auto& ch : list) if (ch == ';') ch = ',';
            return list.find(std::string(",") + (isRtl ? "rtl" : "crt") + ",") == std::string::npos
                && list.find(std::string(",") + name + ",") == std::string::npos;
            };
      std::vector<void*> made;
      bool ok = true;
      auto hook = [&](HMODULE m, const char* name, void* h, void** o) {
            if (!ok || !want(name, m == nt)) return;
            void* p = (void*)GetProcAddress(m, name);
            if (!p) return;                                 // (not exported here: nothing makes such blocks)
            const MH_STATUS st = MH_CreateHook(p, h, o);
            if (st == MH_OK) made.push_back(p);
            else if (st != MH_ERROR_ALREADY_CREATED) { ok = false; snprintf(g_why, sizeof g_why, "hook of %s failed (%d)", name, int(st)); }
            };
#define HOOK(m, name, h, o) hook(m, name, (void*)h, (void**)o)
      HOOK(u, "malloc", h_malloc, &o_malloc);
      HOOK(u, "_malloc_base", h_malloc_base, &o_malloc_base);
      HOOK(u, "free", h_free, &o_free);
      HOOK(u, "_free_base", h_free_base, &o_free_base);
      HOOK(u, "realloc", h_realloc, &o_realloc);
      HOOK(u, "_realloc_base", h_realloc_base, &o_realloc_base);
      HOOK(u, "calloc", h_calloc, &o_calloc);
      HOOK(u, "_calloc_base", h_calloc_base, &o_calloc_base);
      HOOK(u, "_msize", h_msize, &o_msize);
      HOOK(u, "_msize_base", h_msize_base, &o_msize_base);
      HOOK(u, "_recalloc", h_recalloc, &o_recalloc);
      HOOK(u, "_recalloc_base", h_recalloc_base, &o_recalloc_base);
      HOOK(u, "_expand", h_expand, &o_expand);
      HOOK(u, "_expand_base", h_expand_base, &o_expand_base);
      HOOK(u, "_aligned_malloc", h_amalloc, &o_amalloc);
      HOOK(u, "_aligned_offset_malloc", h_aomalloc, &o_aomalloc);
      HOOK(u, "_aligned_free", h_afree, &o_afree);
      HOOK(u, "_aligned_realloc", h_arealloc, &o_arealloc);
      HOOK(u, "_aligned_offset_realloc", h_aorealloc, &o_aorealloc);
      HOOK(u, "_aligned_recalloc", h_arecalloc, &o_arecalloc);
      HOOK(u, "_aligned_offset_recalloc", h_aorecalloc, &o_aorecalloc);
      HOOK(u, "_aligned_msize", h_amsize, &o_amsize);
      HOOK(nt, "RtlAllocateHeap", h_RtlAllocateHeap, &o_RtlAllocateHeap);
      HOOK(nt, "RtlFreeHeap", h_RtlFreeHeap, &o_RtlFreeHeap);
      HOOK(nt, "RtlReAllocateHeap", h_RtlReAllocateHeap, &o_RtlReAllocateHeap);
      HOOK(nt, "RtlSizeHeap", h_RtlSizeHeap, &o_RtlSizeHeap);
#undef HOOK
      // the frees the mimalloc mode can't do without
      if (ok && g_mi && !(o_free && o_realloc && o_msize && o_afree && o_RtlFreeHeap && o_RtlSizeHeap && o_RtlReAllocateHeap)) {
            ok = false; snprintf(g_why, sizeof g_why, "a needed function was not found");
            }
      if (ok && MH_EnableHook(MH_ALL_HOOKS) != MH_OK) { ok = false; snprintf(g_why, sizeof g_why, "MinHook could not enable the hooks"); }
      if (!ok) {
            for (void* p : made) MH_RemoveHook(p);
            g_mi = g_miRtl = false;
            g_started = -1;
            --t_in;
            return 0;
            }
      snprintf(g_why, sizeof g_why, "%s, %zu hooks", g_mi ? "mimalloc 2.1.7" : "counting (the C runtime's heap)", made.size());
      g_started = 1;
      --t_in;
      return 1;
      }

// MuseScore (audio/vst3/processallocator.cpp): kheap_start("mi") before any plug-in exists; 1 when mimalloc answers
// from now on, 0 when it doesn't (kheap_status() says why: the system allocator stays, nothing was changed).
extern "C" __declspec(dllexport) int kheap_start(const char* mode)
      {
      return start(mode, false);
      }
extern "C" __declspec(dllexport) const char* kheap_status()
      {
      return g_why;
      }

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
      {
      // (detach: nothing. The hooks and mimalloc stay to the end of the process: blocks it made are freed by Qt's and
      // the CRT's own teardown after this DLL's detach; mimalloc never gives back memory at exit, destroy_on_exit off)
      if (reason != DLL_PROCESS_ATTACH) return TRUE;
      QueryPerformanceFrequency(&g_freq);
      QueryPerformanceCounter(&g_t0);
      g_tls = TlsAlloc();
      if (getenv("KHEAP_CRASH")) AddVectoredExceptionHandler(1, onCrash);    // (debugging: the first exceptions to a file)
      // the measurement tools (ktload --dll / --trace, launchw): KHEAP=count|mi starts it at load
      if (const char* mode = getenv("KHEAP")) {
            const char* noCount = getenv("KHEAP_NOCOUNT");      // "1": no counts, no timing (speed runs; with counting, QueryPerformanceCounter alone cost ~11 %)
            start(mode, !(noCount && !strcmp(noCount, "1")));
            }
      return TRUE;
      }
