//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  ProcessAllocator (processallocator.h): kheap.dll loaded first thing in main() on Windows
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "processallocator.h"

#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Ms {
namespace ProcessAllocator {

static char g_description[320] = "system (not started)";
static bool g_mimalloc = false;

void start()
      {
#ifdef _WIN32
      static bool started = false;
      if (started)
            return;
      started = true;
      char off[8] = {};
      if (GetEnvironmentVariableA("MS_KHEAP_OFF", off, sizeof off) && !strcmp(off, "1")) {
            snprintf(g_description, sizeof g_description, "system (MS_KHEAP_OFF=1)");
            return;
            }
      // kheap.dll from MuseScore's own folder only (never one found on the search path)
      wchar_t path[MAX_PATH * 2];
      DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH * 2);
      if (!n || n >= MAX_PATH * 2) {
            snprintf(g_description, sizeof g_description, "system (MuseScore's folder not found)");
            return;
            }
      wchar_t* slash = wcsrchr(path, L'\\');
      if (!slash || (slash - path) + 12 >= MAX_PATH * 2) {
            snprintf(g_description, sizeof g_description, "system (MuseScore's folder not found)");
            return;
            }
      wcscpy(slash + 1, L"kheap.dll");
      HMODULE h = LoadLibraryExW(path, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
      if (!h) {
            snprintf(g_description, sizeof g_description, "system (kheap.dll not loaded: error %lu)", GetLastError());
            return;
            }
      typedef int (*start_t)(const char*);
      typedef const char* (*status_t)();
      const start_t kstart = (start_t)GetProcAddress(h, "kheap_start");
      const status_t kstatus = (status_t)GetProcAddress(h, "kheap_status");
      if (!kstart || !kstatus) {
            snprintf(g_description, sizeof g_description, "system (kheap.dll has no kheap_start)");
            FreeLibrary(h);
            return;
            }
      HMODULE pinned = nullptr;           // (never unloaded: its blocks are freed to the end of the process)
      GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCWSTR)kstart, &pinned);
      g_mimalloc = kstart("mi") == 1;
      snprintf(g_description, sizeof g_description, "%s (kheap.dll: %s)", g_mimalloc ? "mimalloc" : "system", kstatus());
#else
      snprintf(g_description, sizeof g_description, "system");
#endif
      }

const char* description()
      {
      return g_description;
      }

bool mimalloc()
      {
      return g_mimalloc;
      }

}
}
