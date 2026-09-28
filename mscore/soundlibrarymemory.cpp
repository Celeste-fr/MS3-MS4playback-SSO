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

} // namespace Ms
