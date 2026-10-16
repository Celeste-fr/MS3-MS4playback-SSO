//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  LagProbe (lagprobe.h has the design)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "lagprobe.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <pthread.h>
#endif

namespace Ms {
namespace LagProbe {

static const QString& path()
      {
      static const QString p = qEnvironmentVariable("MS_LAG_PROBE");
      return p;
      }

bool on()
      {
      static const bool o = !path().isEmpty();
      return o;
      }

QString logPath()
      {
      return path();
      }

qint64 nowNs()
      {
      static const auto t0 = std::chrono::steady_clock::now();
      return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
      }

static std::atomic<bool> mainKnown { false };
static std::thread::id mainId;

void markMainThread()
      {
      mainId = std::this_thread::get_id();
      mainKnown = true;
      nameThread("MuseScore main");
      }

bool isMainThread()
      {
      return mainKnown && std::this_thread::get_id() == mainId;
      }

void nameThread(const char* name)
      {
      if (!on() || !name)
            return;
#ifdef Q_OS_WIN
      typedef HRESULT (WINAPI *SetDescription)(HANDLE, PCWSTR);
      static const SetDescription set = reinterpret_cast<SetDescription>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
      if (set)
            set(GetCurrentThread(), reinterpret_cast<const wchar_t*>(QString(name).utf16()));
#elif defined(Q_OS_LINUX)
      pthread_setname_np(pthread_self(), QByteArray(name).left(15).constData());
#endif
      }

static std::mutex mutex;
static std::vector<Sample> samples;

void add(const char* name, qint64 startNs, qint64 durNs, const QString& detail, int depth, const char* parent, const char* root)
      {
      if (!on())
            return;
      const bool m = isMainThread();
      std::lock_guard<std::mutex> lock(mutex);
      samples.push_back({ name, detail, startNs, durNs, m, depth, parent, root });
      }

std::vector<Sample> take()
      {
      std::vector<Sample> out;
      std::lock_guard<std::mutex> lock(mutex);
      out.swap(samples);
      return out;
      }

static std::atomic<qint64> audioCalls { 0 }, audioSum { 0 }, audioMax { 0 }, audioOver { 0 }, audioPeriod { 0 };

void addAudio(qint64 durNs, qint64 periodNs)
      {
      if (!on())
            return;
      static thread_local bool named = false;
      if (!named) {
            named = true;
            nameThread("audio callback");
            }
      ++audioCalls;
      audioSum += durNs;
      qint64 m = audioMax.load();
      while (durNs > m && !audioMax.compare_exchange_weak(m, durNs)) {}
      if (durNs > periodNs)
            ++audioOver;
      audioPeriod = periodNs;
      }

AudioStats takeAudio()
      {
      AudioStats s;
      s.calls = audioCalls.exchange(0);
      s.sumNs = audioSum.exchange(0);
      s.maxNs = audioMax.exchange(0);
      s.overruns = audioOver.exchange(0);
      s.periodNs = audioPeriod.load();
      return s;
      }

static thread_local int depthNow = 0;
static thread_local const char* stack[64];

static void enter(const char* name, int* depth, const char** parent, const char** root)
      {
      *depth = depthNow;
      *parent = depthNow > 0 && depthNow <= 64 ? stack[depthNow - 1] : nullptr;
      *root = depthNow > 0 ? stack[0] : nullptr;
      if (depthNow < 64)
            stack[depthNow] = name;
      ++depthNow;
      }

Scope::Scope(const char* name)
   : _name(name)
      {
      if (!on())
            return;
      enter(name, &_depth, &_parent, &_root);
      _start = nowNs();
      }

Scope::Scope(const char* name, const QString& detail)
   : _name(name)
      {
      if (!on())
            return;
      _detail = detail;
      enter(name, &_depth, &_parent, &_root);
      _start = nowNs();
      }

Scope::~Scope()
      {
      if (_start < 0)
            return;
      --depthNow;
      add(_name, _start, nowNs() - _start, _detail, _depth, _parent, _root);
      }

} // namespace LagProbe
} // namespace Ms
