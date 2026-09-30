//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  the process's own memory, for the sound library's cost per patch (in a file of its own:
//  the Windows headers' macros stay out of the host's code)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "soundlibraryhost.h"

#include <QtGlobal>
#include <QFile>

#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#define PSAPI_VERSION 2             // GetProcessMemoryInfo in kernel32 (no psapi library)
#include <windows.h>
#include <psapi.h>
#elif defined(Q_OS_LINUX)
#include <unistd.h>
#endif

namespace Ms {

//---------------------------------------------------------
//   processMemory
//---------------------------------------------------------

qint64 SoundLibraryHost::processMemory()
      {
#if defined(Q_OS_WIN)
      PROCESS_MEMORY_COUNTERS_EX pmc;
      if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
            return qint64(pmc.PrivateUsage);
      return -1;
#elif defined(Q_OS_LINUX)
      QFile f("/proc/self/statm");
      if (!f.open(QIODevice::ReadOnly))
            return -1;
      const QList<QByteArray> fields = f.readAll().simplified().split(' ');
      if (fields.size() < 2)
            return -1;
      return fields[1].toLongLong() * qint64(sysconf(_SC_PAGESIZE));      // resident
#else
      return -1;
#endif
      }

//---------------------------------------------------------
//   systemMemory
//---------------------------------------------------------

void SoundLibraryHost::systemMemory(qint64* total, qint64* available)
      {
      *total = *available = -1;
#if defined(Q_OS_WIN)
      MEMORYSTATUSEX m;
      m.dwLength = sizeof(m);
      if (GlobalMemoryStatusEx(&m)) {
            *total = qint64(m.ullTotalPhys);
            *available = qint64(m.ullAvailPhys);
            }
#elif defined(Q_OS_LINUX)
      QFile f("/proc/meminfo");
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
            return;
      for (const QByteArray& line : f.readAll().split('\n')) {
            const QList<QByteArray> w = line.simplified().split(' ');
            if (w.size() >= 2 && w[0] == "MemTotal:")
                  *total = w[1].toLongLong() * 1024;
            else if (w.size() >= 2 && w[0] == "MemAvailable:")
                  *available = w[1].toLongLong() * 1024;
            }
#endif
      }

//---------------------------------------------------------
//   workerThread
//    a thread that sets a plug-in's state (SoundLibraryHost::loadThreads): COM as on the GUI thread
//    (a single-threaded apartment), in case the plug-in uses it while it loads. Looked up in ole32,
//    which every Qt GUI process has loaded, so nothing more is linked
//---------------------------------------------------------

void SoundLibraryHost::workerThread(bool start)
      {
#if defined(Q_OS_WIN)
      static thread_local bool initialized = false;
      HMODULE ole = GetModuleHandleW(L"ole32.dll");
      if (!ole)
            return;
      if (start) {
            using Init = long (WINAPI*)(void*, unsigned long);
            if (Init init = reinterpret_cast<Init>(reinterpret_cast<void*>(GetProcAddress(ole, "CoInitializeEx"))))
                  initialized = init(nullptr, 0x2 /* COINIT_APARTMENTTHREADED */) >= 0;
            }
      else if (initialized) {
            using Uninit = void (WINAPI*)();
            if (Uninit uninit = reinterpret_cast<Uninit>(reinterpret_cast<void*>(GetProcAddress(ole, "CoUninitialize"))))
                  uninit();
            initialized = false;
            }
#else
      Q_UNUSED(start);
#endif
      }

} // namespace Ms
