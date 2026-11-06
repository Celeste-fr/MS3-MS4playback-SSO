//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "backgroundload.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <thread>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#endif

namespace Ms {

BackgroundLoad::BackgroundLoad()
      : _before(threads())
      {
      }

std::map<uint64_t, double> BackgroundLoad::threads()
      {
      std::map<uint64_t, double> out;
#if defined(Q_OS_WIN)
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snap == INVALID_HANDLE_VALUE)
            return out;
      THREADENTRY32 te;
      te.dwSize = sizeof(te);
      const DWORD self = GetCurrentProcessId();
      if (Thread32First(snap, &te)) {
            do {
                  if (te.th32OwnerProcessID != self)
                        continue;
                  ULONG64 cycles = 0;
                  HANDLE th = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                  if (th) {
                        QueryThreadCycleTime(th, &cycles);
                        CloseHandle(th);
                        }
                  out[uint64_t(te.th32ThreadID)] = double(cycles);
                  te.dwSize = sizeof(te);
                  } while (Thread32Next(snap, &te));
            }
      CloseHandle(snap);
#elif defined(Q_OS_LINUX)
      const QDir tasks("/proc/self/task");
      for (const QString& tid : tasks.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile f(tasks.filePath(tid + "/schedstat"));
            if (!f.open(QIODevice::ReadOnly))
                  continue;
            const QList<QByteArray> fields = f.readAll().simplified().split(' ');
            out[tid.toULongLong()] = fields.isEmpty() ? 0.0 : fields.front().toDouble();     // ns on a CPU
            }
#endif
      return out;
      }

double BackgroundLoad::memory()
      {
#if defined(Q_OS_WIN)
      PROCESS_MEMORY_COUNTERS pmc;
      pmc.cb = sizeof(pmc);
      if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
            return double(pmc.WorkingSetSize);
#elif defined(Q_OS_LINUX)
      QFile f("/proc/self/statm");
      if (f.open(QIODevice::ReadOnly)) {
            const QList<QByteArray> fields = f.readAll().simplified().split(' ');
            if (fields.size() > 1)
                  return fields[1].toDouble() * 4096;     // resident pages
            }
#endif
      return 0;
      }

BackgroundLoad::Result BackgroundLoad::wait(double limitSeconds) const
      {
      Result r;
      if (qEnvironmentVariableIsSet("MS_BACKGROUND_LOAD_WAIT"))
            limitSeconds = qEnvironmentVariable("MS_BACKGROUND_LOAD_WAIT").toDouble();
#if defined(Q_OS_WIN) || defined(Q_OS_LINUX)
      if (limitSeconds <= 0)
            return r;
#ifdef Q_OS_WIN
      const double quiet = QUIET_CYCLES;
#else
      const double quiet = QUIET_NS;
#endif
      const auto t0 = std::chrono::steady_clock::now();
      auto elapsed = [&t0]() { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };
      const size_t window = size_t(WINDOW_MS / POLL_MS) + 1;     // polls spanning WINDOW_MS
      struct Poll {
            std::map<uint64_t, double> cpu;
            double memory;
            double at;
            };
      std::deque<Poll> polls;
      std::map<uint64_t, double> lastBusy;                        // a new thread -> when it was last busy
      const double memory0 = memory();
      for (;;) {
            polls.push_back({ threads(), memory(), elapsed() });
            if (polls.size() > window)
                  polls.pop_front();
            if (polls.size() == window) {
                  const Poll& a = polls.front();
                  const Poll& b = polls.back();
                  r.threads = 0;
                  r.busiest = 0;
                  for (const auto& t : b.cpu) {
                        if (_before.count(t.first))
                              continue;
                        ++r.threads;
                        auto o = a.cpu.find(t.first);
                        // (a thread that wasn't there a window ago: all it has used)
                        const double used = o == a.cpu.end() ? t.second : t.second - o->second;
                        r.busiest = std::max(r.busiest, used);
                        if (used >= quiet)
                              lastBusy[t.first] = b.at;
                        }
                  bool pending = false;                     // (a thread that was busy, still there, not quiet for long)
                  for (const auto& lb : lastBusy)
                        pending = pending || (b.cpu.count(lb.first) && b.at - lb.second < LONG_MS / 1000.0);
                  if (r.busiest < quiet && std::fabs(b.memory - a.memory) < QUIET_BYTES && !pending) {
                        r.quiet = true;
                        break;
                        }
                  }
            if (elapsed() >= limitSeconds)
                  break;
            std::this_thread::sleep_for(std::chrono::milliseconds(POLL_MS));
            }
      r.memoryMB = (memory() - memory0) / (1024 * 1024);
      r.seconds = elapsed();
#else
      Q_UNUSED(limitSeconds);
      r.quiet = true;
#endif
      return r;
      }

}     // namespace Ms
