//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Remote: hosted plug-in instances in helper processes (see vst3remote.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "vst3remote.h"
#include "vst3plugin.h"
#include "backgroundload.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QThread>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <xmmintrin.h>
#define VST3REMOTE_MXCSR 1
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if !defined(VST3_HOST_CHILD) && !defined(VST3PLUGIN_NO_SETTINGS)
#include "libmscore/playbacksettings.h"
#elif defined(VST3_HOST_CHILD)
#include <QApplication>
#include <QWidget>
#include "mscore/vst3editor.h"
#include "processallocator.h"
#endif

namespace Ms {
namespace Vst3Remote {

//---------------------------------------------------------
//   the platform: shared memory, semaphores, pipes
//---------------------------------------------------------

#ifdef _WIN32
using Pipe = HANDLE;
static const Pipe NO_PIPE = INVALID_HANDLE_VALUE;
#else
using Pipe = int;
static const Pipe NO_PIPE = -1;
#endif

static bool writeAll(Pipe p, const char* data, size_t n)
      {
      while (n > 0) {
#ifdef _WIN32
            DWORD w = 0;
            if (!WriteFile(p, data, DWORD(std::min<size_t>(n, 1 << 30)), &w, nullptr) || w == 0)
                  return false;
#else
            const ssize_t w = ::write(p, data, n);
            if (w < 0 && errno == EINTR)
                  continue;
            if (w <= 0)
                  return false;
#endif
            data += w;
            n -= size_t(w);
            }
      return true;
      }

static bool readAll(Pipe p, char* data, size_t n)
      {
      while (n > 0) {
#ifdef _WIN32
            DWORD r = 0;
            if (!ReadFile(p, data, DWORD(std::min<size_t>(n, 1 << 30)), &r, nullptr) || r == 0)
                  return false;
#else
            const ssize_t r = ::read(p, data, n);
            if (r < 0 && errno == EINTR)
                  continue;
            if (r <= 0)
                  return false;
#endif
            data += r;
            n -= size_t(r);
            }
      return true;
      }

static bool writeMessage(Pipe p, const QByteArray& m)
      {
      const quint32 n = quint32(m.size());
      return writeAll(p, reinterpret_cast<const char*>(&n), sizeof(n)) && writeAll(p, m.constData(), size_t(m.size()));
      }

static bool readMessage(Pipe p, QByteArray* m)
      {
      quint32 n = 0;
      if (!readAll(p, reinterpret_cast<char*>(&n), sizeof(n)))
            return false;
      m->resize(int(n));
      return n == 0 || readAll(p, m->data(), n);
      }

static void closePipe(Pipe& p)
      {
      if (p == NO_PIPE)
            return;
#ifdef _WIN32
      CloseHandle(p);
#else
      ::close(p);
#endif
      p = NO_PIPE;
      }

// a named shared-memory segment: made by MuseScore, opened by the helper
class Shm {
#ifdef _WIN32
      HANDLE _h { nullptr };
#else
      QByteArray _name;
      bool _owner { false };
#endif
      void* _data { nullptr };
      size_t _size { 0 };
   public:
      ~Shm() { close(); }
      bool create(const QString& name, size_t size)
            {
#ifdef _WIN32
            _h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, DWORD(quint64(size) >> 32), DWORD(size & 0xffffffffu),
                                    reinterpret_cast<const wchar_t*>(name.utf16()));
            if (!_h)
                  return false;
            _data = MapViewOfFile(_h, FILE_MAP_ALL_ACCESS, 0, 0, size);
#else
            _name = name.toUtf8();
            const int fd = shm_open(_name.constData(), O_CREAT | O_EXCL | O_RDWR, 0600);
            if (fd < 0)
                  return false;
            _owner = true;
            if (ftruncate(fd, off_t(size)) != 0) {
                  ::close(fd);
                  return false;
                  }
            _data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            ::close(fd);
            if (_data == MAP_FAILED)
                  _data = nullptr;
#endif
            _size = size;
            if (_data)
                  std::memset(_data, 0, size);
            return _data != nullptr;
            }
      bool open(const QString& name, size_t size)
            {
#ifdef _WIN32
            _h = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, reinterpret_cast<const wchar_t*>(name.utf16()));
            if (!_h)
                  return false;
            _data = MapViewOfFile(_h, FILE_MAP_ALL_ACCESS, 0, 0, size);
#else
            _name = name.toUtf8();
            const int fd = shm_open(_name.constData(), O_RDWR, 0600);
            if (fd < 0)
                  return false;
            _data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            ::close(fd);
            if (_data == MAP_FAILED)
                  _data = nullptr;
#endif
            _size = size;
            return _data != nullptr;
            }
      // (Linux: the name goes once the helper has it open; the memory stays until both unmap it)
      void unlink()
            {
#ifndef _WIN32
            if (_owner)
                  shm_unlink(_name.constData());
            _owner = false;
#endif
            }
      void close()
            {
#ifdef _WIN32
            if (_data)
                  UnmapViewOfFile(_data);
            if (_h)
                  CloseHandle(_h);
            _h = nullptr;
#else
            if (_data)
                  munmap(_data, _size);
            unlink();
#endif
            _data = nullptr;
            }
      void* data() const { return _data; }
      };

// a counting semaphore between the processes: Windows a named one, Linux a process-shared sem_t in a segment
class Signal {
#ifdef _WIN32
      HANDLE _h { nullptr };
#else
      sem_t* _s { nullptr };
      bool _owner { false };
#endif
   public:
      ~Signal()
            {
#ifdef _WIN32
            if (_h)
                  CloseHandle(_h);
#else
            if (_owner && _s)
                  sem_destroy(_s);
#endif
            }
#ifdef _WIN32
      bool create(const QString& name, void*)
            {
            _h = CreateSemaphoreW(nullptr, 0, LONG_MAX, reinterpret_cast<const wchar_t*>(name.utf16()));
            return _h != nullptr;
            }
      bool open(const QString& name, void*)
            {
            _h = OpenSemaphoreW(SEMAPHORE_ALL_ACCESS, FALSE, reinterpret_cast<const wchar_t*>(name.utf16()));
            return _h != nullptr;
            }
      void post() { ReleaseSemaphore(_h, 1, nullptr); }
      void wait() { WaitForSingleObject(_h, INFINITE); }
      bool waitFor(int ms) { return WaitForSingleObject(_h, DWORD(ms)) == WAIT_OBJECT_0; }
#else
      bool create(const QString&, void* place)
            {
            _s = static_cast<sem_t*>(place);
            _owner = sem_init(_s, 1, 0) == 0;
            return _owner;
            }
      bool open(const QString&, void* place)
            {
            _s = static_cast<sem_t*>(place);
            return true;
            }
      void post() { sem_post(_s); }
      void wait()
            {
            while (sem_wait(_s) != 0 && errno == EINTR) {}
            }
      bool waitFor(int ms)
            {
            timespec t;
            clock_gettime(CLOCK_REALTIME, &t);
            t.tv_sec += ms / 1000;
            t.tv_nsec += long(ms % 1000) * 1000000L;
            if (t.tv_nsec >= 1000000000L) {
                  ++t.tv_sec;
                  t.tv_nsec -= 1000000000L;
                  }
            int r;
            while ((r = sem_timedwait(_s, &t)) != 0 && errno == EINTR) {}
            return r == 0;
            }
#endif
      };

#ifdef _WIN32
static constexpr size_t SEM_SPACE = 0;
#else
static constexpr size_t SEM_SPACE = sizeof(sem_t);
#endif

static constexpr int HUNG_MS = 15000;
static constexpr int HUNG_STEP_MS = 250;

static qint64 nowNs()
      {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      }

//---------------------------------------------------------
//   the audio channel's segment, per instance
//---------------------------------------------------------

struct Header {
      std::atomic<quint32> reqSeq;        // MuseScore's last request (filled in before)
      std::atomic<quint32> doneSeq;       // the helper's last done
      qint32 kind;                        // RequestKind
      qint32 frames;                      // REQ_PROCESS: frames; REQ_FETCH: how many
      qint32 offset;                      // REQ_FETCH: from
      qint32 commands;                    // in the command area
      qint32 restart;                     // restartTransport() before it
      qint32 cmdCapacity;
      qint32 outFrames;
      quint32 mxcsr;                      // the caller's floating-point mode (x86: MXCSR), the block runs in it
      qint32 hasMxcsr;
      double seconds;                     // REQ_SETTLE
      std::atomic<double> sinceState;     // the helper's secondsSinceState(), after each request
      std::atomic<qint64> serveNs;        // the helper's time for the last request (replay + process)
      };

static size_t headerSpace()
      {
      return (sizeof(Header) + SEM_SPACE + 63) / 64 * 64;
      }
static size_t segmentSize(int cmdCapacity, int outFrames)
      {
      return headerSpace() + size_t(cmdCapacity) * sizeof(Command) + size_t(outFrames) * 2 * sizeof(float);
      }
static Command* commandArea(void* seg)
      {
      return reinterpret_cast<Command*>(static_cast<char*>(seg) + headerSpace());
      }
static float* outputArea(void* seg, int cmdCapacity)
      {
      return reinterpret_cast<float*>(reinterpret_cast<char*>(commandArea(seg)) + size_t(cmdCapacity) * sizeof(Command));
      }
static void* semPlace(void* seg)
      {
      return static_cast<char*>(seg) + sizeof(Header);
      }

// the command area holds what Kontakt can take in a block in MuseScore's process (vst3plugin.cpp: an EventList of 512
// events, ParameterChanges of 512 parameters); more go in extra REQ_COMMANDS requests, in order (nothing dropped here)
static constexpr int COMMAND_CAPACITY = 512 + 512;

static int counter()
      {
      static std::atomic<int> n { 0 };
      return ++n;
      }

static QString objectName(const char* what, int n)
      {
#ifdef _WIN32
      return QString("Local\\MsLibHost-%1-%2-%3").arg(GetCurrentProcessId()).arg(what).arg(n);
#else
      return QString("/mslibhost-%1-%2-%3").arg(getpid()).arg(what).arg(n);
#endif
      }

static QByteArray pack(const std::function<void(QDataStream&)>& f)
      {
      QByteArray b;
      QDataStream ds(&b, QIODevice::WriteOnly);
      ds.setVersion(QDataStream::Qt_5_12);
      f(ds);
      return b;
      }

// the calls that may take long in a helper (Kontakt's instrument load, its teardown, the description's 4145 parameters'
// texts …): its helper is "busy" while one is in flight (Instance::busy)
static bool longOp(Op op)
      {
      switch (op) {
            case Op::LOAD:
            case Op::SET_STATE:
            case Op::DESTROY:
            case Op::DESCRIBE:
            case Op::SET_OFFLINE:
            case Op::SET_SAMPLE_RATE:
            case Op::STATE:
            case Op::BACKGROUND_WAIT:
                  return true;
            default:
                  return false;
            }
      }

static const char* opName(Op op)
      {
      switch (op) {
            case Op::LOAD: return "LOAD";
            case Op::DESTROY: return "DESTROY";
            case Op::SET_OFFLINE: return "SET_OFFLINE";
            case Op::SET_SAMPLE_RATE: return "SET_SAMPLE_RATE";
            case Op::STATE: return "STATE";
            case Op::SET_STATE: return "SET_STATE";
            case Op::DESCRIBE: return "DESCRIBE";
            case Op::BACKGROUND_WAIT: return "BACKGROUND_WAIT";
            case Op::KEY_NAMES: return "KEY_NAMES";
            case Op::IDLE: return "IDLE";
            case Op::COMPONENT_STATE: return "COMPONENT_STATE";
            case Op::CONTROLLER_STATE: return "CONTROLLER_STATE";
            case Op::PARAMETERS: return "PARAMETERS";
            case Op::PARAMETER: return "PARAMETER";
            case Op::PARAMETER_TEXT: return "PARAMETER_TEXT";
            case Op::SET_PARAMETER: return "SET_PARAMETER";
            case Op::SET_CONTROLLER_PARAMETER: return "SET_CONTROLLER_PARAMETER";
            case Op::PARAMETER_ID: return "PARAMETER_ID";
            case Op::CONTROLLER_PARAMETER: return "CONTROLLER_PARAMETER";
            case Op::TAKE_REPORTED: return "TAKE_REPORTED";
            case Op::SHOW_EDITOR: return "SHOW_EDITOR";
            case Op::HOST_QUERIES: return "HOST_QUERIES";
            case Op::QUIT: return "QUIT";
            case Op::PARAMETER_VALUES: return "PARAMETER_VALUES";
            case Op::BACKGROUND_MARK: return "BACKGROUND_MARK";
            case Op::CLOSE_EDITOR: return "CLOSE_EDITOR";
            default: return "a call";
            }
      }

//---------------------------------------------------------
//   the log
//---------------------------------------------------------

static std::mutex& logMutex()
      {
      static std::mutex m;
      return m;
      }

static std::function<void(const QString&)>& logFunction()
      {
      static auto* f = new std::function<void(const QString&)>;
      return *f;
      }

void setLog(std::function<void(const QString&)> log)
      {
      std::lock_guard<std::mutex> lock(logMutex());
      logFunction() = log;
      }

static void log(const QString& line)
      {
      std::function<void(const QString&)> f;
      {
            std::lock_guard<std::mutex> lock(logMutex());
            f = logFunction();
      }
      qWarning("Vst3Remote: %s", qPrintable(line));
      if (f)
            f(line);
      }

//---------------------------------------------------------
//   timing of the audio round trips
//---------------------------------------------------------

struct TimingEntry {
      quint32 rtNs;                       // the caller's post to its wait's return
      quint32 serveNs;                    // the helper's replay + process
      };
static constexpr size_t TIMING_ENTRIES = size_t(1) << 20;

static std::vector<TimingEntry>& timingEntries()
      {
      static auto* t = new std::vector<TimingEntry>(TIMING_ENTRIES);
      return *t;
      }
static std::atomic<size_t> s_timingCount { 0 };

static void noteTiming(qint64 rt, qint64 serve)
      {
      const size_t i = s_timingCount.fetch_add(1, std::memory_order_relaxed);
      if (i < TIMING_ENTRIES)
            timingEntries()[i] = { quint32(std::min<qint64>(rt, UINT_MAX)), quint32(std::max<qint64>(0, std::min<qint64>(serve, UINT_MAX))) };
      }

QString timingSummary(bool reset)
      {
      const size_t n = std::min(TIMING_ENTRIES, s_timingCount.load(std::memory_order_acquire));
      if (reset)
            s_timingCount.store(0, std::memory_order_release);
      if (n == 0)
            return QString();
      std::vector<double> over(n), rt(n);
      for (size_t i = 0; i < n; ++i) {
            const TimingEntry& e = timingEntries()[i];
            rt[i] = e.rtNs / 1000.0;
            over[i] = std::max(0.0, double(e.rtNs) - double(e.serveNs)) / 1000.0;
            }
      std::sort(over.begin(), over.end());
      std::sort(rt.begin(), rt.end());
      auto q = [n](const std::vector<double>& v, double f) { return v[std::min(n - 1, size_t(f * n))]; };
      return QString("%1 helper round trips: overhead median %2 p99 %3 max %4 µs, whole round trip median %5 p99 %6 max %7 µs")
             .arg(n).arg(q(over, 0.5), 0, 'f', 1).arg(q(over, 0.99), 0, 'f', 1).arg(over.back(), 0, 'f', 1)
             .arg(q(rt, 0.5), 0, 'f', 1).arg(q(rt, 0.99), 0, 'f', 1).arg(rt.back(), 0, 'f', 1);
      }

//---------------------------------------------------------
//   Process
//    a helper, MuseScore's side
//---------------------------------------------------------

class Process {
   public:
      int index { 0 };
      Pipe toChild { NO_PIPE };
      Pipe fromChild { NO_PIPE };
      std::mutex writeMutex;              // one message written at a time
      std::atomic<bool> dead { false };
      std::atomic<bool> quitting { false };
      std::atomic<bool> helloRead { false };
      std::atomic<bool> exited { false };      // its process is gone (watcher)
      static constexpr int QUIT_WAIT_MS = 20000;
      Shm reqShm;                         // (Linux: the request semaphore's place)
      Signal req;
      std::thread watcher;
      std::thread reader;
#ifdef _WIN32
      HANDLE process { nullptr };
#else
      pid_t pid { -1 };
#endif
      qint64 childPid { -1 };
      QString exitText;                   // how it ended (watcher)
      QString allocator;                  // its hello's

      std::mutex instMutex;
      std::set<Instance::Impl*> instances;
      // (registry's mutex)
      int loading { 0 };                  // instances not yet set up
      int count { 0 };
      std::map<QString, int> keys;        // the patches its instances load (an instance of the same goes here)

      // the requests waiting for an answer, by sequence number
      struct Waiter {
            bool done { false };
            bool longOp { false };
            QByteArray reply;
            };
      std::mutex waitMutex;
      std::condition_variable waitCv;
      quint32 nextSeq { 0 };
      std::map<quint32, std::shared_ptr<Waiter>> waiting;
      std::atomic<int> longOps { 0 };

      ~Process();
      bool start(const QString& exe, const QString& prewarm, QString* error);
      void readerLoop();
      void died(const QString& how);
      void kill()
            {
#ifdef _WIN32
            TerminateProcess(process, 1);
#else
            ::kill(pid, SIGKILL);
#endif
            }
      bool send(Op op, quint32 id, const QByteArray& args, QByteArray* reply, bool wait = true);
      bool busy() const { return !helloRead || longOps.load() > 0; }
      };

struct Instance::Impl {
      std::shared_ptr<Process> proc;
      quint32 id { 0 };
      QString key;
      Shm seg;
      Signal done;
      Header* h { nullptr };
      Command* cmds { nullptr };
      float* out { nullptr };
      int cmdCapacity { 0 };
      int outFrames { 0 };
      int commands { 0 };
      quint32 seq { 0 };
      bool begun { false };
      int begunFrames { 0 };
      qint64 postedAt { 0 };
      bool loading { true };
      std::atomic<bool> restart { false };
      // setParameter's processor side from any thread (CMD_EDIT), added after the audio thread's commands at the next request
      std::mutex editMutex;
      std::vector<Command> edits;

      void post(RequestKind kind, int frames, int offset = 0, double seconds = 0);
      bool wait();
      bool request(RequestKind kind, int frames, int offset = 0, double seconds = 0)
            {
            post(kind, frames, offset, seconds);
            return wait();
            }
      };

static std::mutex& registryMutex()
      {
      static std::mutex m;
      return m;
      }
static std::vector<std::shared_ptr<Process>>& registry()
      {
      static auto* r = new std::vector<std::shared_ptr<Process>>;
      return *r;
      }
static std::atomic<int> s_setting { INT_MIN };      // (INT_MIN: not set, the playback setting)
static std::atomic<bool> s_unavailable { false };
static std::atomic<int> s_started { 0 };           // helpers started so far (their index)
static std::mutex s_endedMutex;
static std::vector<int> s_ended;

#ifdef _WIN32
// the helpers end with MuseScore, whatever ends it (a crash too)
static HANDLE job()
      {
      static HANDLE j = []() {
            HANDLE h = CreateJobObjectW(nullptr, nullptr);
            if (h) {
                  JOBOBJECT_EXTENDED_LIMIT_INFORMATION info {};
                  info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                  SetInformationJobObject(h, JobObjectExtendedLimitInformation, &info, sizeof(info));
                  }
            return h;
            }();
      return j;
      }
#endif

static QString hostExecutable()
      {
      const QByteArray env = qgetenv("MS_VST3_HOST_EXE");
      if (!env.isEmpty())
            return QString::fromLocal8Bit(env);
#ifdef _WIN32
      const QString name = "MuseScoreLibraryHost.exe";
#else
      const QString name = "MuseScoreLibraryHost";
#endif
      if (QCoreApplication::instance()) {
            const QString here = QCoreApplication::applicationDirPath() + "/" + name;
            if (QFileInfo::exists(here))
                  return here;
            }
#ifdef VST3HOST_BUILD_PATH
      if (QFileInfo::exists(QString(VST3HOST_BUILD_PATH)))
            return QString(VST3HOST_BUILD_PATH);       // (the build's, for the tests)
#endif
      return QString();
      }

bool Process::start(const QString& exe, const QString& prewarm, QString* error)
      {
      if (exe.isEmpty() || !QFileInfo::exists(exe)) {
            *error = QString("the helper MuseScoreLibraryHost isn't there (%1)").arg(exe.isEmpty() ? QString("next to MuseScore") : exe);
            return false;
            }
      const QString reqName = objectName("req", counter());
#ifdef _WIN32
      if (!req.create(reqName, nullptr)) {
            *error = QString("cannot make a semaphore (%1)").arg(GetLastError());
            return false;
            }
      SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };
      HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
      if (!CreatePipe(&inR, &inW, &sa, 0) || !CreatePipe(&outR, &outW, &sa, 0)) {
            *error = "cannot make pipes";
            return false;
            }
      SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
      SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
      HANDLE inherit[2] = { inR, outW };
      SIZE_T attrSize = 0;
      InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
      std::vector<char> attrs(attrSize);
      auto list = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrs.data());
      InitializeProcThreadAttributeList(list, 1, 0, &attrSize);
      UpdateProcThreadAttribute(list, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, sizeof(inherit), nullptr, nullptr);
      STARTUPINFOEXW si {};
      si.StartupInfo.cb = sizeof(si);
      si.lpAttributeList = list;
      QString cmd = QString("\"%1\" --ms-vst3-host %2 %3 %4").arg(QDir::toNativeSeparators(exe))
                    .arg(quint64(reinterpret_cast<quintptr>(inR))).arg(quint64(reinterpret_cast<quintptr>(outW))).arg(reqName);
      if (!prewarm.isEmpty())
            cmd += QString(" --prewarm \"%1\"").arg(QDir::toNativeSeparators(prewarm));
      std::vector<wchar_t> line(size_t(cmd.size()) + 1, 0);
      cmd.toWCharArray(line.data());
      PROCESS_INFORMATION pi {};
      const BOOL ok = CreateProcessW(nullptr, line.data(), nullptr, nullptr, TRUE,
                                     EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr,
                                     &si.StartupInfo, &pi);
      DeleteProcThreadAttributeList(list);
      CloseHandle(inR);
      CloseHandle(outW);
      if (!ok) {
            *error = QString("cannot start %1 (%2)").arg(exe).arg(GetLastError());
            CloseHandle(inW);
            CloseHandle(outR);
            return false;
            }
      if (HANDLE j = job())
            AssignProcessToJobObject(j, pi.hProcess);
      ResumeThread(pi.hThread);
      CloseHandle(pi.hThread);
      process = pi.hProcess;
      childPid = qint64(pi.dwProcessId);
      toChild = inW;
      fromChild = outR;
      watcher = std::thread([this]() {
            WaitForSingleObject(process, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(process, &code);
            died(QString("exit code 0x%1").arg(quint32(code), 8, 16, QChar('0')).toUpper().replace("0X", "0x"));
            });
#else
      if (!reqShm.create(reqName, 64 + SEM_SPACE) || !req.create(reqName, reqShm.data())) {
            *error = QString("cannot make a semaphore (%1)").arg(errno);
            return false;
            }
      int in[2], out[2];
      if (pipe2(in, O_CLOEXEC) != 0 || pipe2(out, O_CLOEXEC) != 0) {
            *error = "cannot make pipes";
            return false;
            }
      const QByteArray exe8 = QFile::encodeName(exe);
      QList<QByteArray> args { exe8, "--ms-vst3-host", QByteArray::number(in[0]), QByteArray::number(out[1]), reqName.toUtf8() };
      if (!prewarm.isEmpty())
            args << "--prewarm" << QFile::encodeName(prewarm);
      std::vector<char*> argv;
      for (QByteArray& a : args)
            argv.push_back(a.data());
      argv.push_back(nullptr);
      const pid_t parent = getpid();
      const pid_t p = fork();
      if (p == 0) {
            // (between fork and exec: async-signal-safe calls only)
            prctl(PR_SET_PDEATHSIG, SIGKILL);
            if (getppid() != parent)
                  _exit(1);
            fcntl(in[0], F_SETFD, 0);
            fcntl(out[1], F_SETFD, 0);
            execv(argv[0], argv.data());
            _exit(127);
            }
      ::close(in[0]);
      ::close(out[1]);
      if (p < 0) {
            ::close(in[1]);
            ::close(out[0]);
            *error = QString("cannot start %1").arg(exe);
            return false;
            }
      pid = p;
      childPid = qint64(p);
      toChild = in[1];
      fromChild = out[0];
      watcher = std::thread([this]() {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            died(WIFSIGNALED(status) ? QString("signal %1").arg(WTERMSIG(status)) : QString("exit code %1").arg(WEXITSTATUS(status)));
            });
#endif
      reader = std::thread([this]() { readerLoop(); });
      return true;
      }

// the answers (and the hello first): to whoever waits for each
void Process::readerLoop()
      {
      QByteArray m;
      if (readMessage(fromChild, &m)) {
            QDataStream ds(m);
            ds.setVersion(QDataStream::Qt_5_12);
            QByteArray hello;
            ds >> hello >> allocator;
            if (hello == "MSLIBHOST") {
                  helloRead = true;
                  reqShm.unlink();        // (Linux: the helper has it open; the name goes, the memory stays)
                  log(QString("helper %1 (pid %2) started; its allocator: %3").arg(index).arg(childPid).arg(allocator));
                  }
            }
      while (helloRead && readMessage(fromChild, &m)) {
            quint32 seq = 0;              // (the answer's first 4 bytes, as Child::reply writes them)
            if (m.size() < int(sizeof(seq)))
                  continue;
            std::memcpy(&seq, m.constData(), sizeof(seq));
            std::lock_guard<std::mutex> lock(waitMutex);
            auto i = waiting.find(seq);
            if (i == waiting.end())
                  continue;
            i->second->reply = m.mid(int(sizeof(quint32)));
            i->second->done = true;
            if (i->second->longOp)
                  --longOps;
            waiting.erase(i);
            waitCv.notify_all();
            }
      // (the pipe closed: the helper ended or is ending; the watcher says how)
      {
            std::lock_guard<std::mutex> lock(waitMutex);
            dead = true;
            waitCv.notify_all();
      }
      std::lock_guard<std::mutex> lock(instMutex);
      for (Instance::Impl* i : instances)
            i->done.post();
      }

void Process::died(const QString& how)
      {
      exitText = how;
      {
            std::lock_guard<std::mutex> lock(waitMutex);
            dead = true;
            exited = true;
            waitCv.notify_all();
      }
      QStringList held;
      {
            std::lock_guard<std::mutex> lock(instMutex);
            for (Instance::Impl* i : instances) {
                  i->done.post();               // (whoever waits for a block: it ends silent)
                  held << i->key;
                  }
      }
      if (!quitting) {
            log(QString("helper %1 (pid %2) ended (%3) with %4 instance(s): %5").arg(index).arg(childPid).arg(how)
                .arg(held.size()).arg(held.join(", ")));
            std::lock_guard<std::mutex> lock(s_endedMutex);
            s_ended.push_back(index);
            }
      }

Process::~Process()
      {
      reqShm.unlink();                    // (a helper that ended before its hello)
      closePipe(toChild);                 // (its input: a helper still running quits)
      // (one that doesn't within QUIT_WAIT_MS, hung in a plug-in: ended)
      {
            std::unique_lock<std::mutex> lock(waitMutex);
            waitCv.wait_for(lock, std::chrono::milliseconds(QUIT_WAIT_MS), [this]() { return exited.load(); });
      }
      if (!exited && childPid > 0) {
            log(QString("helper %1 (pid %2) didn't quit within %3 s: ended").arg(index).arg(childPid).arg(QUIT_WAIT_MS / 1000));
#ifdef _WIN32
            TerminateProcess(process, 1);
#else
            ::kill(pid, SIGKILL);
#endif
            }
      if (watcher.joinable())
            watcher.join();
      if (reader.joinable())
            reader.join();
      closePipe(fromChild);
#ifdef _WIN32
      if (process)
            CloseHandle(process);
#endif
      }

bool Process::send(Op op, quint32 id, const QByteArray& args, QByteArray* reply, bool wait)
      {
      if (dead)
            return false;
      std::shared_ptr<Waiter> w = std::make_shared<Waiter>();
      w->longOp = longOp(op);
      quint32 seq = 0;
      {
            std::lock_guard<std::mutex> lock(waitMutex);
            seq = ++nextSeq;
            waiting[seq] = w;
            if (w->longOp)
                  ++longOps;
      }
      QByteArray m = pack([&](QDataStream& ds) { ds << quint32(op) << id << seq; });
      m += args;
      bool written = false;
      {
            std::lock_guard<std::mutex> lock(writeMutex);
            written = !dead && writeMessage(toChild, m);
      }
      if (!written) {
            std::lock_guard<std::mutex> lock(waitMutex);
            if (waiting.erase(seq) && w->longOp)
                  --longOps;
            return false;
            }
      if (!wait)
            return true;
      std::unique_lock<std::mutex> lock(waitMutex);
      waitCv.wait(lock, [&]() { return w->done || dead.load(); });
      if (!w->done)
            return false;
      if (reply)
            *reply = w->reply;
      return true;
      }

//---------------------------------------------------------
//   the setting
//---------------------------------------------------------

void setProcesses(int k)
      {
      s_setting = k;
      }

bool unavailable()
      {
      return s_unavailable;
      }

// the computer's physical cores (SMT: two logical a core; where that can't be told, half the logical ones)
static int physicalCores()
      {
#ifdef _WIN32
      DWORD len = 0;
      GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
      if (len > 0) {
            std::vector<char> buf(len);
            if (GetLogicalProcessorInformationEx(RelationProcessorCore, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data()), &len)) {
                  int cores = 0;
                  for (DWORD off = 0; off < len;) {
                        auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buf.data() + off);
                        if (info->Relationship == RelationProcessorCore)
                              ++cores;
                        off += info->Size;
                        }
                  if (cores > 0)
                        return cores;
                  }
            }
#else
      QFile f("/proc/cpuinfo");
      if (f.open(QIODevice::ReadOnly)) {
            std::set<QString> cores;
            QString physical;
            for (const QByteArray& line : f.readAll().split('\n')) {
                  if (line.startsWith("physical id"))
                        physical = QString::fromLatin1(line.mid(line.indexOf(':') + 1)).trimmed();
                  else if (line.startsWith("core id"))
                        cores.insert(physical + "/" + QString::fromLatin1(line.mid(line.indexOf(':') + 1)).trimmed());
                  }
            if (!cores.empty())
                  return int(cores.size());
            }
#endif
      return std::max(1, QThread::idealThreadCount() / 2);
      }

int processes()
      {
#if defined(VST3_HOST_CHILD) || defined(VST3PLUGIN_NO_SETTINGS)
      return 0;                     // (a helper hosts in its own process; kthost, without the settings, in its own too)
#else
      if (s_unavailable)
            return 0;
      bool ok = false;
      int k = qEnvironmentVariableIntValue("MS_VST3_HOST_PROCESSES", &ok);
      if (!ok)
            k = s_setting != INT_MIN ? int(s_setting) : qRound(Playback::value("soundlibrary/hostProcesses"));
      if (k < 0) {
            // automatic: setState is one CPU-bound thread per process (oop-load, VM: 1 -> 37 s, 2 -> 21 s, 3 -> 19 s on its 2
            // cores / 4 logical; more gained nothing there): a helper per physical core, one core left for MuseScore, at
            // least 2 with 2 or more cores and 4 or more logical processors (the VM's case)
            const int cores = physicalCores();
            k = std::max(1, cores - 1);
            if (cores >= 2 && QThread::idealThreadCount() >= 4)
                  k = std::max(2, k);
            }
      return k;
#endif
      }

int running()
      {
      std::lock_guard<std::mutex> lock(registryMutex());
      return int(std::count_if(registry().begin(), registry().end(), [](const std::shared_ptr<Process>& p) { return !p->dead; }));
      }

std::vector<qint64> processIds()
      {
      std::vector<qint64> ids;
      std::lock_guard<std::mutex> lock(registryMutex());
      for (const auto& p : registry())
            if (!p->dead)
                  ids.push_back(p->childPid);
      return ids;
      }

std::vector<int> takeEnded()
      {
      std::lock_guard<std::mutex> lock(s_endedMutex);
      std::vector<int> e;
      e.swap(s_ended);
      return e;
      }

// (with the registry's mutex held)
static std::shared_ptr<Process> spawn(const QString& prewarm, QString* error)
      {
      std::shared_ptr<Process> p = std::make_shared<Process>();
      p->index = s_started++;
      if (!p->start(hostExecutable(), prewarm, error))
            return nullptr;
      registry().push_back(p);
      return p;
      }

static void dropDead()
      {
      auto& r = registry();
      r.erase(std::remove_if(r.begin(), r.end(), [](const std::shared_ptr<Process>& p) { return p->dead && p->count == 0; }), r.end());
      }

// no helper could be started: every instance from now on in MuseScore's process (said once)
static void giveUp(const QString& why)
      {
      if (s_unavailable.exchange(true))
            return;
      log(QString("no helper could be started (%1): the plug-in's instances load in MuseScore's process, as before").arg(why));
      }

void prepare(const QString& path, int instances)
      {
      const int k = processes();
      if (k <= 0 || instances <= 0)
            return;
      std::lock_guard<std::mutex> lock(registryMutex());
      dropDead();
      int alive = int(std::count_if(registry().begin(), registry().end(), [](const std::shared_ptr<Process>& p) { return !p->dead; }));
      int idle = int(std::count_if(registry().begin(), registry().end(), [](const std::shared_ptr<Process>& p) {
            return !p->dead && p->loading == 0;
            }));
      // (started together, each loading the module meanwhile: their starts overlap)
      while (alive < k && idle < instances) {
            QString err;
            if (!spawn(path, &err)) {
                  if (alive == 0)
                        giveUp(err);
                  else
                        log(QString("another helper could not be started: %1").arg(err));
                  break;
                  }
            ++alive;
            ++idle;
            }
      }

void shutdown(bool quick)
      {
      std::vector<std::shared_ptr<Process>> all;
      {
            std::lock_guard<std::mutex> lock(registryMutex());
            all.swap(registry());
      }
      for (auto& p : all) {
            p->quitting = true;
            if (p->dead)
                  continue;
            if (quick) {
                  // (MuseScore closes: nothing a helper holds is kept, its state is MuseScore's; it ends now, not after a load
                  // in flight and Kontakt's teardown of gigabytes: 17-52 s at #480's close)
#ifdef _WIN32
                  TerminateProcess(p->process, 1);
#else
                  kill(p->pid, SIGKILL);
#endif
                  continue;
                  }
            p->send(Op::QUIT, 0, QByteArray(), nullptr, false);
            }
      all.clear();                        // (each waits for its helper to end: ~Process)
      }

void allowForeground(qint64 pid)
      {
#ifdef _WIN32
      if (pid > 0)
            AllowSetForegroundWindow(DWORD(pid));
#else
      Q_UNUSED(pid);
#endif
      }

//---------------------------------------------------------
//   Instance
//---------------------------------------------------------

Instance::Instance()
   : d(new Impl)
      {
      }

Instance::~Instance()
      {
      if (!d->proc)
            return;
      if (d->loading)
            loaded();
      d->proc->send(Op::DESTROY, d->id, QByteArray(), nullptr, false);       // (not waited for: Kontakt takes its time to go)
      {
            std::lock_guard<std::mutex> lock(d->proc->instMutex);
            d->proc->instances.erase(d.get());
      }
      std::lock_guard<std::mutex> lock(registryMutex());
      --d->proc->count;
      auto k = d->proc->keys.find(d->key);
      if (k != d->proc->keys.end() && --k->second <= 0)
            d->proc->keys.erase(k);
      }

//---------------------------------------------------------
//   the environment
//    a helper is started once and kept, but an in-process plug-in reads MuseScore's environment as it is at the call
//    (the test synth's MSTESTSYNTH_*, set by a test before it loads an instance): each LOAD and SET_STATE carries
//    MuseScore's environment and the helper takes it over first (only the variables that differ: none in a normal
//    session, so no setenv while other instances play). Not taken: what the helper sets for itself
//---------------------------------------------------------

static bool ownVariable(const QString& name)
      {
      return name.isEmpty() || name.startsWith('=') || name == "MS_VST3_HOST_PROCESSES" || name == "QT_QPA_PLATFORM";
      }

static QStringList environment()
      {
      return QProcessEnvironment::systemEnvironment().toStringList();
      }

#ifdef VST3_HOST_CHILD
static void takeEnvironment(const QStringList& parent)
      {
      const QProcessEnvironment now = QProcessEnvironment::systemEnvironment();
      QProcessEnvironment want;
      for (const QString& e : parent) {
            const int eq = e.indexOf('=', 1);
            if (eq > 0)
                  want.insert(e.left(eq), e.mid(eq + 1));
            }
      for (const QString& k : want.keys())
            if (!ownVariable(k) && (!now.contains(k) || now.value(k) != want.value(k)))
                  qputenv(k.toLocal8Bit().constData(), want.value(k).toLocal8Bit());
      for (const QString& k : now.keys())
            if (!ownVariable(k) && !want.contains(k))
                  qunsetenv(k.toLocal8Bit().constData());
      }
#endif

std::unique_ptr<Instance> Instance::load(const QString& path, double sampleRate, int maxBlock, const QString& key, QString* error,
                                         QByteArray* loadReply, bool* noHelper)
      {
      if (noHelper)
            *noHelper = false;
      auto fail = [error](const QString& m) {
            if (error)
                  *error = m;
            return std::unique_ptr<Instance>();
            };
      std::shared_ptr<Process> proc;
      {
            std::lock_guard<std::mutex> lock(registryMutex());
            dropDead();
            const int k = std::max(1, processes());
            const int alive = int(std::count_if(registry().begin(), registry().end(), [](const std::shared_ptr<Process>& p) { return !p->dead; }));
            // the helper that hosts the same patch when it isn't loading (Kontakt shares its samples in a process: the even
            // levels' measurer's copy of a playback instance), else the one loading the fewest now, then holding the fewest;
            // a new one while all are loading. (Copies of one patch loading together spread over the helpers: the Linux GUI
            // run, 22 instances by varispeed, 4-5 copies of a patch each, piled into one helper behind each other; with
            // Kontakt's hosted slots a patch's copies are one instance)
            if (!key.isEmpty())
                  for (const auto& p : registry())
                        if (!p->dead && p->keys.count(key) && p->loading == 0)
                              proc = p;
            if (!proc) {
                  for (const auto& p : registry())
                        if (!p->dead && (!proc || std::make_pair(p->loading, p->count) < std::make_pair(proc->loading, proc->count)))
                              proc = p;
                  if (!proc || (proc->loading > 0 && alive < k)) {
                        QString err;
                        if (std::shared_ptr<Process> p = spawn(path, &err))
                              proc = p;
                        else if (!proc) {
                              if (alive == 0)
                                    giveUp(err);
                              if (noHelper)
                                    *noHelper = true;
                              return fail(err);
                              }
                        }
                  }
            ++proc->loading;
            ++proc->count;
            if (!key.isEmpty())
                  ++proc->keys[key];
      }
      std::unique_ptr<Instance> in(new Instance);
      Impl& d = *in->d;
      d.proc = proc;
      d.key = key;
      d.id = quint32(counter());
      d.cmdCapacity = COMMAND_CAPACITY;
      d.outFrames = std::max(1, maxBlock);
      const QString segName = objectName("seg", int(d.id));
      if (!d.seg.create(segName, segmentSize(d.cmdCapacity, d.outFrames))) {
            in.reset();
            return fail("cannot make the shared memory");
            }
      d.h = static_cast<Header*>(d.seg.data());
      d.h->cmdCapacity = d.cmdCapacity;
      d.h->outFrames = d.outFrames;
      d.cmds = commandArea(d.seg.data());
      d.out = outputArea(d.seg.data(), d.cmdCapacity);
      if (!d.done.create(segName + "-done", semPlace(d.seg.data()))) {
            in.reset();
            return fail("cannot make a semaphore");
            }
      {
            std::lock_guard<std::mutex> lock(proc->instMutex);
            proc->instances.insert(&d);
      }
      const QByteArray args = pack([&](QDataStream& ds) {
            ds << path << sampleRate << qint32(maxBlock) << segName << qint32(d.cmdCapacity) << environment();
            });
      QByteArray reply;
      const bool sent = proc->send(Op::LOAD, d.id, args, &reply);
      d.seg.unlink();
      QDataStream ds(reply);
      ds.setVersion(QDataStream::Qt_5_12);
      bool ok = false;
      QString err;
      if (sent)
            ds >> ok >> err;
      if (!ok) {
            const QString why = sent ? err : QString("the helper %1 ended (%2)").arg(proc->index).arg(proc->exitText);
            in.reset();
            return fail(why);
            }
      if (loadReply)
            *loadReply = reply.mid(int(ds.device()->pos()));
      return in;
      }

bool Instance::alive() const
      {
      return d->proc && !d->proc->dead;
      }

int Instance::process() const
      {
      return d->proc ? d->proc->index : -1;
      }

qint64 Instance::pid() const
      {
      return d->proc ? d->proc->childPid : -1;
      }

bool Instance::busy() const
      {
      return d->proc && d->proc->busy();
      }

void Instance::loaded()
      {
      if (!d->loading)
            return;
      d->loading = false;
      std::lock_guard<std::mutex> lock(registryMutex());
      --d->proc->loading;
      }

QByteArray Instance::call(Op op, const QByteArray& args, bool* ok, bool flush, bool wait)
      {
      if (flush && d->commands > 0 && alive())
            d->request(REQ_COMMANDS, 0);
      QByteArray reply;
      // (setState: the environment too, as at LOAD; the test synth reads it there)
      QElapsedTimer t;
      t.start();
      const bool busy = d->proc->busy();
      const bool sent = d->proc->send(op, d->id, op == Op::SET_STATE ? args + pack([](QDataStream& ds) { ds << environment(); }) : args,
                                      wait ? &reply : nullptr, wait);
      // (what MuseScore waited for besides its own call: a short call that found the helper busy with a long one)
      if (wait && busy && !longOp(op) && t.elapsed() >= 100)
            log(QString("%1 waited %2 ms for helper %3, busy with a load").arg(opName(op)).arg(t.elapsed()).arg(d->proc->index));
      if (ok)
            *ok = sent;
      return reply;
      }

void Instance::Impl::post(RequestKind kind, int frames, int offset, double seconds)
      {
      // (the edits since the last request, after the commands: Vst3Plugin::processDirect takes them at a block's start, after
      // what the audio thread's calls queued; more than the area holds go first in requests of their own)
      if (kind != REQ_FETCH) {
            std::lock_guard<std::mutex> lock(editMutex);
            for (const Command& e : edits) {
                  if (commands >= cmdCapacity) {
                        h->kind = REQ_COMMANDS;
                        h->commands = commands;
                        h->restart = 0;
                        h->hasMxcsr = 0;
                        commands = 0;
                        postedAt = 0;
                        h->reqSeq.store(++seq, std::memory_order_release);
                        proc->req.post();
                        wait();
                        }
                  cmds[commands++] = e;
                  }
            edits.clear();
      }
      h->kind = kind;
      h->frames = frames;
      h->offset = offset;
      h->seconds = seconds;
      h->commands = (kind == REQ_FETCH) ? 0 : commands;
      h->restart = (kind == REQ_PROCESS || kind == REQ_SETTLE) ? int(restart.exchange(false)) : 0;
#ifdef VST3REMOTE_MXCSR
      h->mxcsr = _mm_getcsr();
      h->hasMxcsr = 1;
#else
      h->hasMxcsr = 0;
#endif
      if (kind != REQ_FETCH)
            commands = 0;
      postedAt = kind == REQ_PROCESS ? nowNs() : 0;
      h->reqSeq.store(++seq, std::memory_order_release);
      proc->req.post();
      }

bool Instance::Impl::wait()
      {
      // (a helper that doesn't answer a block for HUNG_MS, alive but stuck in a plug-in, is ended: its instances then go
      // silent and are loaded again as after a crash, instead of the audio thread waiting for ever. Offline blocks of a
      // plug-in streaming samples take up to seconds: far under it)
      qint64 since = 0;
      while (h->doneSeq.load(std::memory_order_acquire) != seq) {
            if (proc->dead)
                  return false;
            if (done.waitFor(HUNG_STEP_MS))
                  continue;
            if (!since)
                  since = nowNs();
            else if (nowNs() - since >= qint64(HUNG_MS) * 1000000 && !proc->dead) {
                  log(QString("helper %1 (pid %2) didn't answer a block for %3 s: ended").arg(proc->index).arg(proc->childPid)
                      .arg(HUNG_MS / 1000));
                  proc->kill();
                  since = nowNs();          // (died() follows from its watcher)
                  }
            }
      if (postedAt)
            noteTiming(nowNs() - postedAt, h->serveNs.load(std::memory_order_relaxed));
      return true;
      }

void Instance::command(const Command& c)
      {
      if (!alive())
            return;
      if (d->commands >= d->cmdCapacity)
            d->request(REQ_COMMANDS, 0);
      d->cmds[d->commands++] = c;
      }

void Instance::restartTransport()
      {
      d->restart = true;
      }

void Instance::edit(unsigned id, double normalized)
      {
      std::lock_guard<std::mutex> lock(d->editMutex);
      d->edits.push_back({ CMD_EDIT, qint32(id), 0, 0, 0, 0.f, normalized, 0 });
      }

void Instance::beginProcess(int frames)
      {
      if (d->begun || !alive())
            return;
      d->post(REQ_PROCESS, frames);
      d->begun = true;
      d->begunFrames = frames;
      }

void Instance::process(int frames, float* buffer)
      {
      if (!d->begun || d->begunFrames != frames) {
            if (d->begun)
                  d->wait();
            d->begun = false;
            beginProcess(frames);
            }
      if (!d->begun)
            return;
      d->begun = false;
      if (!d->wait())
            return;
      if (frames <= d->outFrames) {
            for (int i = 0; i < 2 * frames; ++i)
                  buffer[i] += d->out[i];
            return;
            }
      // (longer than the output area: rendered whole in the helper, fetched in pieces)
      for (int from = 0; from < frames; from += d->outFrames) {
            const int n = std::min(d->outFrames, frames - from);
            if (!d->request(REQ_FETCH, n, from))
                  return;
            float* b = buffer + 2 * from;
            for (int i = 0; i < 2 * n; ++i)
                  b[i] += d->out[i];
            }
      }

void Instance::settle(double seconds)
      {
      if (alive())
            d->request(REQ_SETTLE, 0, 0, seconds);
      }

double Instance::secondsSinceState() const
      {
      return d->h ? d->h->sinceState.load(std::memory_order_acquire) : 0.0;
      }


//---------------------------------------------------------
//   the helper
//---------------------------------------------------------

#ifdef VST3_HOST_CHILD

struct ChildInstance {
      quint32 id { 0 };
      std::unique_ptr<Vst3Plugin> plugin;
      Shm seg;
      Signal done;
      Header* h { nullptr };
      std::vector<float> rendered;        // a REQ_PROCESS longer than the output area, for REQ_FETCH
      std::unique_ptr<BackgroundLoad> since;      // the threads before it was made (or BACKGROUND_MARK)
      std::mutex serving;                 // held while a thread plays it (the audio or the settle thread; Vst3Plugin::idle)
      std::atomic<bool> settling { false };       // its REQ_SETTLE is with the settle thread
      };

class Child {
   public:
      Pipe in { NO_PIPE };
      Pipe out { NO_PIPE };
      Shm reqShm;
      Signal req;
      std::mutex table;                   // the instances: the main thread changes, the audio thread plays
      std::map<quint32, std::unique_ptr<ChildInstance>> instances;
      std::atomic<bool> stop { false };
      std::thread audio;
      // REQ_SETTLE (a load's last step, SoundLibraryHost: silence rendered right after setState, when a sampler is slowest)
      // on a thread of its own, not the audio thread's: there it held up every other instance's block in this helper, and
      // MuseScore's audio callback waited for them (VM build 483: ~2.6 s per load while stopped; helper-stall, 2026-10-27)
      std::thread settler;
      std::mutex settleMutex;
      std::condition_variable settleCv;
      std::deque<ChildInstance*> settles;
      std::thread reader;
      std::map<quint32, QWidget*> editors;
      // the requests, run one at a time on the main thread in the order they came (drain)
      std::mutex queueMutex;
      std::deque<QByteArray> queue;
      bool draining { false };

      void audioLoop();
      void settleLoop();
      void serve(ChildInstance* in);
      void drain();
      void handle(const QByteArray& request);
      void reply(quint32 seq, const QByteArray& r)
            {
            QByteArray m(reinterpret_cast<const char*>(&seq), int(sizeof(seq)));
            m += r;
            writeMessage(out, m);
            }
      bool showEditor(quint32 id, Vst3Plugin* p, const QString& title);
      };


bool Child::showEditor(quint32 id, Vst3Plugin* p, const QString& title)
      {
      auto e = editors.find(id);
      if (e != editors.end() && e->second) {
            QWidget* w = e->second;
            w->show();
            w->raise();
            w->activateWindow();
            return true;
            }
      Steinberg::IPlugView* view = p->createEditor();
      if (!view)
            return false;
      // a window of its own, not owned by MuseScore's: Windows attaches the input queues of a cross-process owner or parent
      // window, so a helper busy loading would hold MuseScore's input
      Vst3EditorWindow* w = new Vst3EditorWindow(view, title, nullptr);
      editors[id] = w;
      QObject::connect(w, &QObject::destroyed, w, [this, id]() { editors.erase(id); });
      w->show();
      w->raise();
      w->activateWindow();
      return true;
      }

static void replay(Vst3Plugin* p, const Command* c, int n)
      {
      for (int i = 0; i < n; ++i, ++c) {
            switch (c->kind) {
                  case CMD_MIDI:
                        p->midi(c->i1, c->i2, c->i3, c->i4, c->f);
                        break;
                  case CMD_PITCH:
                        p->setPitch(c->x, c->y);
                        break;
                  case CMD_ALL_NOTES_OFF:
                        p->allNotesOff();
                        break;
                  case CMD_QUEUE_PARAMETER:
                        p->queueParameter(unsigned(c->i1), c->x);
                        break;
                  case CMD_EDIT:
                        p->setParameter(unsigned(c->i1), c->x, Vst3Plugin::EDIT_PROCESSOR);
                        break;
                  default:
                        break;
                  }
            }
      }

void Child::serve(ChildInstance* in)
      {
      Header* h = in->h;
      Vst3Plugin* p = in->plugin.get();
      const qint64 t0 = nowNs();
#ifdef VST3REMOTE_MXCSR
      // (the block in the caller's floating-point mode, as in MuseScore's process: its thread's, then this thread's back)
      const unsigned int own = _mm_getcsr();
      if (h->hasMxcsr)
            _mm_setcsr(h->mxcsr);
#endif
      const int cmdCapacity = h->cmdCapacity;
      if (h->kind != REQ_FETCH)
            replay(p, commandArea(in->seg.data()), std::min(h->commands, cmdCapacity));
      if (h->restart)
            p->restartTransport();
      float* out = outputArea(in->seg.data(), cmdCapacity);
      switch (h->kind) {
            case REQ_PROCESS: {
                  const int frames = h->frames;
                  if (frames <= h->outFrames) {
                        std::fill(out, out + 2 * frames, 0.f);
                        p->process(frames, out);
                        }
                  else {
                        in->rendered.assign(size_t(2 * frames), 0.f);
                        p->process(frames, in->rendered.data());
                        }
                  break;
                  }
            case REQ_FETCH: {
                  const size_t from = size_t(2 * h->offset);
                  const size_t n = size_t(2 * h->frames);
                  if (from + n <= in->rendered.size())
                        std::copy(in->rendered.begin() + long(from), in->rendered.begin() + long(from + n), out);
                  break;
                  }
            case REQ_SETTLE:
                  p->settle(h->seconds);
                  break;
            default:
                  break;
            }
#ifdef VST3REMOTE_MXCSR
      _mm_setcsr(own);
#endif
      h->sinceState.store(p->secondsSinceState(), std::memory_order_release);
      h->serveNs.store(nowNs() - t0, std::memory_order_relaxed);
      }

void Child::audioLoop()
      {
#ifdef _WIN32
      // (MMCSS "Pro Audio", as PortAudio raises its own callback's thread)
      if (HMODULE avrt = LoadLibraryW(L"avrt.dll")) {
            using Fn = HANDLE (WINAPI*)(LPCWSTR, LPDWORD);
            if (auto f = reinterpret_cast<Fn>(GetProcAddress(avrt, "AvSetMmThreadCharacteristicsW"))) {
                  DWORD index = 0;
                  f(L"Pro Audio", &index);
                  }
            }
#else
      // (realtime where the system allows it; else as it is)
      sched_param sp {};
      sp.sched_priority = 10;
      pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
#endif
      while (true) {
            req.wait();
            if (stop)
                  return;
            std::lock_guard<std::mutex> lock(table);
            for (auto& i : instances) {
                  ChildInstance* in = i.second.get();
                  const quint32 want = in->h->reqSeq.load(std::memory_order_acquire);
                  if (want == in->h->doneSeq.load(std::memory_order_relaxed))
                        continue;
                  if (in->h->kind == REQ_SETTLE) {
                        if (!in->settling.exchange(true)) {
                              std::lock_guard<std::mutex> s(settleMutex);
                              settles.push_back(in);
                              settleCv.notify_one();
                              }
                        continue;
                        }
                  {
                        std::lock_guard<std::mutex> playing(in->serving);
                        serve(in);
                  }
                  in->h->doneSeq.store(want, std::memory_order_release);
                  in->done.post();
                  }
            }
      }

// the settles the audio thread handed over, one at a time (an instance settling gets no other request: its caller waits)
void Child::settleLoop()
      {
      while (true) {
            ChildInstance* in = nullptr;
            {
                  std::unique_lock<std::mutex> lock(settleMutex);
                  settleCv.wait(lock, [this]() { return stop || !settles.empty(); });
                  if (stop)
                        return;
                  in = settles.front();
                  settles.pop_front();
            }
            const quint32 want = in->h->reqSeq.load(std::memory_order_acquire);
            {
                  std::lock_guard<std::mutex> playing(in->serving);
                  serve(in);
            }
            in->h->doneSeq.store(want, std::memory_order_release);
            in->settling = false;
            in->done.post();
            }
      }

static void writeTimes(QDataStream& ds, const Vst3Plugin::Times& t)
      {
      ds << t.module << t.create << t.buses << t.activate << t.component << t.controllerComponent << t.controller << t.mapping;
      }

// the queue's requests, one after the other; a plug-in's nested event loop (Kontakt pumps messages inside setState) runs
// none of them: the outer drain goes on once the call returns
void Child::drain()
      {
      if (draining)
            return;
      draining = true;
      while (true) {
            QByteArray r;
            {
                  std::lock_guard<std::mutex> lock(queueMutex);
                  if (queue.empty())
                        break;
                  r = queue.front();
                  queue.pop_front();
            }
            handle(r);
            }
      draining = false;
      }

void Child::handle(const QByteArray& request)
      {
      QDataStream ds(request);
      ds.setVersion(QDataStream::Qt_5_12);
      quint32 op32 = 0, id = 0, seq = 0;
      ds >> op32 >> id >> seq;
      const Op op = Op(op32);
      if (op == Op::QUIT) {
            reply(seq, QByteArray());
            QCoreApplication::quit();
            return;
            }
      if (op == Op::HOST_QUERIES) {
            reply(seq, pack([](QDataStream& o) { o << QJsonDocument(Vst3Plugin::hostQueries()).toJson(QJsonDocument::Compact); }));
            return;
            }
      if (op == Op::LOAD) {
            QString path, segName;
            double rate = 0;
            qint32 maxBlock = 0, cmdCapacity = 0;
            QStringList env;
            ds >> path >> rate >> maxBlock >> segName >> cmdCapacity >> env;
            takeEnvironment(env);
            QString err;
            std::unique_ptr<ChildInstance> in(new ChildInstance);
            in->id = id;
            in->since.reset(new BackgroundLoad);
            in->plugin = Vst3Plugin::load(path, rate, maxBlock, &err);
            const int outFrames = std::max(1, int(maxBlock));
            if (in->plugin && (!in->seg.open(segName, segmentSize(cmdCapacity, outFrames))
                               || !in->done.open(segName + "-done", semPlace(in->seg.data())))) {
                  in->plugin.reset();
                  err = "cannot open the shared memory";
                  }
            if (!in->plugin) {
                  reply(seq, pack([&](QDataStream& o) { o << false << err; }));
                  return;
                  }
            in->h = static_cast<Header*>(in->seg.data());
            in->h->sinceState.store(in->plugin->secondsSinceState());
            const QByteArray r = pack([&](QDataStream& o) {
                  o << true << QString() << in->plugin->name() << in->plugin->singleComponent();
                  writeTimes(o, in->plugin->times());
                  });
            {
                  std::lock_guard<std::mutex> lock(table);
                  instances[id] = std::move(in);
            }
            reply(seq, r);
            return;
            }
      ChildInstance* in = nullptr;
      {
            std::lock_guard<std::mutex> lock(table);
            auto i = instances.find(id);
            if (i != instances.end())
                  in = i->second.get();
      }
      if (op == Op::DESTROY) {
            auto e = editors.find(id);
            if (e != editors.end()) {
                  delete e->second;
                  editors.erase(e);
                  }
            std::unique_ptr<ChildInstance> gone;
            {
                  std::lock_guard<std::mutex> lock(table);     // (not while the audio thread plays it)
                  auto i = instances.find(id);
                  if (i != instances.end()) {
                        gone = std::move(i->second);
                        instances.erase(i);
                        }
            }
            // (not while the settle thread plays it)
            while (gone && gone->settling)
                  std::this_thread::sleep_for(std::chrono::milliseconds(1));
            gone.reset();
            reply(seq, QByteArray());
            return;
            }
      if (!in) {
            reply(seq, QByteArray());
            return;
            }
      Vst3Plugin* p = in->plugin.get();
      QByteArray r;
      QDataStream o(&r, QIODevice::WriteOnly);
      o.setVersion(QDataStream::Qt_5_12);
      switch (op) {
            case Op::SET_OFFLINE: {
                  bool b = false;
                  ds >> b;
                  o << p->setOffline(b);
                  break;
                  }
            case Op::SET_SAMPLE_RATE: {
                  double rate = 0;
                  ds >> rate;
                  o << p->setSampleRate(rate);
                  break;
                  }
            case Op::STATE:
                  o << p->state();
                  break;
            case Op::SET_STATE: {
                  QByteArray state;
                  QStringList env;
                  ds >> state >> env;
                  takeEnvironment(env);
                  const bool ok = p->setState(state);
                  in->h->sinceState.store(p->secondsSinceState(), std::memory_order_release);
                  o << ok;
                  writeTimes(o, p->times());
                  break;
                  }
            case Op::KEY_NAMES: {
                  QString source;
                  const std::map<int, QString> names = p->keyNames(&source);
                  o << quint32(names.size());
                  for (const auto& n : names)
                        o << qint32(n.first) << n.second;
                  o << source;
                  break;
                  }
            case Op::IDLE:
                  p->idle(&in->serving);
                  break;
            case Op::DESCRIBE: {
                  bool hasBase = false;
                  QByteArray base;
                  ds >> hasBase >> base;
                  const QJsonObject b = QJsonDocument::fromJson(base).object();
                  o << QJsonDocument(p->describe(hasBase ? &b : nullptr)).toJson(QJsonDocument::Compact);
                  break;
                  }
            case Op::COMPONENT_STATE:
                  o << p->componentState();
                  break;
            case Op::CONTROLLER_STATE:
                  o << p->controllerState();
                  break;
            case Op::PARAMETERS:
            case Op::PARAMETER_VALUES: {
                  const std::vector<Vst3Plugin::Parameter> params = p->parameters();
                  o << quint32(params.size());
                  for (const auto& pp : params) {
                        o << quint32(pp.id) << pp.title << pp.units << qint32(pp.stepCount) << qint32(pp.flags) << qint32(pp.unitId);
                        if (op == Op::PARAMETER_VALUES)
                              o << p->parameter(pp.id);
                        }
                  break;
                  }
            case Op::PARAMETER: {
                  quint32 pid = 0;
                  ds >> pid;
                  o << p->parameter(pid);
                  break;
                  }
            case Op::PARAMETER_TEXT: {
                  quint32 pid = 0;
                  double v = 0;
                  ds >> pid >> v;
                  o << p->parameterText(pid, v);
                  break;
                  }
            case Op::SET_PARAMETER: {
                  quint32 pid = 0;
                  double v = 0;
                  ds >> pid >> v;
                  p->setParameter(pid, v);
                  break;
                  }
            case Op::SET_CONTROLLER_PARAMETER: {
                  quint32 pid = 0;
                  double v = 0;
                  ds >> pid >> v;
                  p->setParameter(pid, v, Vst3Plugin::EDIT_CONTROLLER);
                  break;
                  }
            case Op::PARAMETER_ID: {
                  QString title;
                  ds >> title;
                  o << qint64(p->parameterId(title));
                  break;
                  }
            case Op::CONTROLLER_PARAMETER: {
                  qint32 ch = 0, cc = 0;
                  ds >> ch >> cc;
                  o << qint64(p->controllerParameter(ch, cc));
                  break;
                  }
            case Op::TAKE_REPORTED: {
                  const auto rep = p->takeReported();
                  o << quint32(rep.size());
                  for (const auto& c : rep)
                        o << quint32(c.first) << c.second;
                  break;
                  }
            case Op::SHOW_EDITOR: {
                  QString title;
                  ds >> title;
                  o << showEditor(id, p, title);
                  break;
                  }
            case Op::CLOSE_EDITOR: {
                  auto e = editors.find(id);
                  if (e != editors.end()) {
                        delete e->second;
                        editors.erase(e);
                        }
                  break;
                  }
            case Op::BACKGROUND_MARK:
                  in->since.reset(new BackgroundLoad);
                  break;
            case Op::BACKGROUND_WAIT: {
                  double limit = 0;
                  ds >> limit;
                  if (!in->since)
                        in->since.reset(new BackgroundLoad);
                  const BackgroundLoad::Result res = in->since->wait(limit);
                  o << res.seconds << res.quiet << qint32(res.threads) << res.busiest << res.memoryMB;
                  break;
                  }
            default:
                  break;
            }
      reply(seq, r);
      }

int childMain(int argc, char** argv)
      {
      // (kheap's scoped mode, as in MuseScore: mimalloc only inside a load, Vst3Plugin::setState on this main thread)
      ProcessAllocator::start();
      setProcesses(0);
      qunsetenv("MS_VST3_HOST_PROCESSES");
      if (argc < 5 || QByteArray(argv[1]) != "--ms-vst3-host")
            return 2;
#ifndef _WIN32
      if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY")
          && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
      QApplication app(argc, argv);
      app.setQuitOnLastWindowClosed(false);
      Child c;
#ifdef _WIN32
      c.in = reinterpret_cast<HANDLE>(quintptr(QByteArray(argv[2]).toULongLong()));
      c.out = reinterpret_cast<HANDLE>(quintptr(QByteArray(argv[3]).toULongLong()));
      if (!c.req.open(QString::fromUtf8(argv[4]), nullptr))
            return 3;
#else
      c.in = QByteArray(argv[2]).toInt();
      c.out = QByteArray(argv[3]).toInt();
      if (!c.reqShm.open(QString::fromUtf8(argv[4]), 64 + SEM_SPACE) || !c.req.open(QString(), c.reqShm.data()))
            return 3;
#endif
      for (int i = 5; i + 1 < argc; ++i) {
            if (QByteArray(argv[i]) == "--prewarm") {
                  // (the module, now: the first instance waits less; MuseScore starts the helpers together)
                  QString name;
                  quint32 uid[4];
                  Vst3Plugin::classInfo(QFile::decodeName(argv[i + 1]), &name, uid);
                  }
            }
      if (!writeMessage(c.out, pack([](QDataStream& o) { o << QByteArray("MSLIBHOST") << QString::fromUtf8(ProcessAllocator::description()); })))
            return 4;
      c.audio = std::thread([&c]() { c.audioLoop(); });
      c.settler = std::thread([&c]() { c.settleLoop(); });
      c.reader = std::thread([&c, &app]() {
            QByteArray m;
            while (readMessage(c.in, &m)) {
                  {
                        std::lock_guard<std::mutex> lock(c.queueMutex);
                        c.queue.push_back(m);
                  }
                  QMetaObject::invokeMethod(&app, [&c]() { c.drain(); }, Qt::QueuedConnection);
                  }
            QMetaObject::invokeMethod(&app, []() { QCoreApplication::quit(); }, Qt::QueuedConnection);
            });
      app.exec();
      // the instances, then the modules (Kontakt's ExitDll before the process ends: Vst3Plugin::unloadModules)
      c.stop = true;
      c.req.post();
      c.audio.join();
      {
            std::lock_guard<std::mutex> lock(c.settleMutex);
            c.settleCv.notify_all();
      }
      c.settler.join();
      for (auto& e : c.editors)
            delete e.second;
      c.editors.clear();
      {
            std::lock_guard<std::mutex> lock(c.table);
            c.instances.clear();
      }
      Vst3Plugin::unloadModules();
      closePipe(c.out);
      // (the reader ends with MuseScore's end of the pipe; it isn't waited for)
      c.reader.detach();
      return 0;
      }

#endif // VST3_HOST_CHILD

} // namespace Vst3Remote
} // namespace Ms
