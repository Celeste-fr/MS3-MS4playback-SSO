//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Vst3Remote: hosted plug-in instances in separate host processes (see vst3remote.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "vst3remote.h"
#include "vst3plugin.h"

#include <algorithm>
#include <climits>
#include <functional>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcessEnvironment>
#include <QThread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <climits>
#include <csignal>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#ifndef VST3_HOST_CHILD
#include "libmscore/playbacksettings.h"
#else
#include <QApplication>
#include "mscore/vst3editor.h"
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

// a named shared-memory segment: made by the parent, opened by the child
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
      // (Linux: the name goes once the child has it open; the memory stays until both unmap it)
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
#endif
      };

#ifdef _WIN32
static constexpr size_t SEM_SPACE = 0;
#else
static constexpr size_t SEM_SPACE = sizeof(sem_t);
#endif

//---------------------------------------------------------
//   the audio channel's segment, per instance
//---------------------------------------------------------

struct Header {
      std::atomic<quint32> reqSeq;        // the parent's last request (filled in before)
      std::atomic<quint32> doneSeq;       // the child's last done
      qint32 kind;                        // RequestKind
      qint32 frames;                      // REQ_PROCESS: frames; REQ_FETCH: how many
      qint32 offset;                      // REQ_FETCH: from
      qint32 commands;                    // in the command area
      qint32 restart;                     // restartTransport() before it
      qint32 cmdCapacity;
      qint32 outFrames;
      double seconds;                     // REQ_SETTLE
      std::atomic<double> sinceState;     // the child's secondsSinceState(), after each request
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
      return QString("Local\\MsVst3-%1-%2-%3").arg(GetCurrentProcessId()).arg(what).arg(n);
#else
      return QString("/msvst3-%1-%2-%3").arg(getpid()).arg(what).arg(n);
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

//---------------------------------------------------------
//   Process
//    a host process, the parent's side
//---------------------------------------------------------

class Process {
   public:
      int index { 0 };
      std::mutex rpc;                     // one request at a time
      Pipe toChild { NO_PIPE };
      Pipe fromChild { NO_PIPE };
      bool helloRead { false };
      std::atomic<bool> dead { false };
      std::atomic<bool> quitting { false };
      Shm reqShm;                         // (Linux: the request semaphore's place)
      Signal req;
      std::thread watcher;
#ifdef _WIN32
      HANDLE process { nullptr };
#else
      pid_t pid { -1 };
#endif
      std::mutex instMutex;
      std::set<Instance::Impl*> instances;
      int loading { 0 };                  // instances not yet set up (registry's mutex)
      int count { 0 };

      ~Process();
      bool start(const QString& exe, const QString& prewarm, QString* error);
      void died();
      bool send(Op op, quint32 id, const QByteArray& args, QByteArray* reply, bool wait = true);
      };

struct Instance::Impl {
      std::shared_ptr<Process> proc;
      quint32 id { 0 };
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
      bool loading { true };
      std::atomic<bool> restart { false };

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

#ifdef _WIN32
// the host processes end with MuseScore, whatever ends it (a crash too)
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
      const QString name = "msvst3host.exe";
#else
      const QString name = "msvst3host";
#endif
      if (QCoreApplication::instance()) {
            const QString here = QCoreApplication::applicationDirPath() + "/" + name;
            if (QFileInfo::exists(here))
                  return here;
            }
#ifdef VST3HOST_BUILD_PATH
      return QString(VST3HOST_BUILD_PATH);       // (the build's, for the tests)
#else
      return name;
#endif
      }

bool Process::start(const QString& exe, const QString& prewarm, QString* error)
      {
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
      toChild = inW;
      fromChild = outR;
      watcher = std::thread([this]() {
            WaitForSingleObject(process, INFINITE);
            died();
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
      toChild = in[1];
      fromChild = out[0];
      watcher = std::thread([this]() {
            int status = 0;
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            died();
            });
#endif
      return true;
      }

void Process::died()
      {
      dead = true;
      std::lock_guard<std::mutex> lock(instMutex);
      for (Instance::Impl* i : instances)
            i->done.post();               // (whoever waits for a block: it ends silent)
      if (!quitting)
            qWarning("Vst3Remote: host process %d ended", index);
      }

Process::~Process()
      {
      reqShm.unlink();                    // (a child that ended before its hello)
      if (watcher.joinable())
            watcher.join();
      closePipe(toChild);
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
      if (!helloRead) {
            QByteArray hello;
            if (!readMessage(fromChild, &hello) || hello != "MSVST3HOST") {
                  dead = true;
                  return false;
                  }
            helloRead = true;
            reqShm.unlink();              // (Linux: the child has it open; the name goes, the memory stays)
            }
      QByteArray m = pack([&](QDataStream& ds) { ds << quint32(op) << id; });
      m += args;
      if (!writeMessage(toChild, m)) {
            dead = true;
            return false;
            }
      if (!wait)
            return true;
      QByteArray r;
      if (!readMessage(fromChild, &r)) {
            dead = true;
            return false;
            }
      if (reply)
            *reply = r;
      return true;
      }

//---------------------------------------------------------
//   the setting
//---------------------------------------------------------

void setProcesses(int k)
      {
      s_setting = k;
      }

int processes()
      {
#ifdef VST3_HOST_CHILD
      return 0;                     // (a host process hosts in its own process)
#else
      bool ok = false;
      int k = qEnvironmentVariableIntValue("MS_VST3_HOST_PROCESSES", &ok);
      if (!ok)
            k = s_setting != INT_MIN ? int(s_setting) : qRound(Playback::value("soundlibrary/hostProcesses"));
      if (k < 0)        // one per physical core: setState is one CPU-bound thread per process (SMT: two logical a core)
            k = std::max(1, QThread::idealThreadCount() / 2);
      return k;
#endif
      }

int running()
      {
      std::lock_guard<std::mutex> lock(registryMutex());
      return int(std::count_if(registry().begin(), registry().end(), [](const std::shared_ptr<Process>& p) { return !p->dead; }));
      }

static std::shared_ptr<Process> spawn(const QString& prewarm, QString* error)
      {
      std::shared_ptr<Process> p = std::make_shared<Process>();
      p->index = int(registry().size());
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
                  qWarning("Vst3Remote: %s", qPrintable(err));
                  break;
                  }
            ++alive;
            ++idle;
            }
      }

void shutdown()
      {
      std::vector<std::shared_ptr<Process>> all;
      {
            std::lock_guard<std::mutex> lock(registryMutex());
            all.swap(registry());
      }
      for (auto& p : all) {
            std::lock_guard<std::mutex> lock(p->rpc);
            p->quitting = true;
            if (!p->dead)
                  p->send(Op::QUIT, 0, QByteArray(), nullptr, false);
            closePipe(p->toChild);        // (and the child's end of input: it quits too)
            }
      all.clear();                        // (each waits for its process to end: ~Process)
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
      {
            std::lock_guard<std::mutex> lock(d->proc->rpc);
            d->proc->send(Op::DESTROY, d->id, QByteArray(), nullptr);
      }
      {
            std::lock_guard<std::mutex> lock(d->proc->instMutex);
            d->proc->instances.erase(d.get());
      }
      std::lock_guard<std::mutex> lock(registryMutex());
      --d->proc->count;
      }

//---------------------------------------------------------
//   the environment
//    a host process is started once and kept, but an in-process plug-in reads MuseScore's environment as it is at
//    the call (the test synth's MSTESTSYNTH_*, set by a test before it loads an instance): each LOAD carries the
//    parent's environment and the child takes it over before loading (only the variables that differ: none in a
//    normal session, so no setenv while other instances play). Not taken: what the child sets for itself
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

std::unique_ptr<Instance> Instance::load(const QString& path, double sampleRate, int maxBlock, QString* error, QByteArray* loadReply)
      {
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
            // the process loading the fewest now, then holding the fewest; a new one while all are loading
            for (const auto& p : registry())
                  if (!p->dead && (!proc || std::make_pair(p->loading, p->count) < std::make_pair(proc->loading, proc->count)))
                        proc = p;
            const int alive = int(std::count_if(registry().begin(), registry().end(), [](const std::shared_ptr<Process>& p) { return !p->dead; }));
            if (!proc || (proc->loading > 0 && alive < k)) {
                  QString err;
                  if (std::shared_ptr<Process> p = spawn(path, &err))
                        proc = p;
                  else if (!proc)
                        return fail(err);
                  }
            ++proc->loading;
            ++proc->count;
      }
      std::unique_ptr<Instance> in(new Instance);
      Impl& d = *in->d;
      d.proc = proc;
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
      bool sent = false;
      {
            std::lock_guard<std::mutex> lock(proc->rpc);
            sent = proc->send(Op::LOAD, d.id, args, &reply);
      }
      d.seg.unlink();
      QDataStream ds(reply);
      bool ok = false;
      QString err;
      if (sent)
            ds >> ok >> err;
      if (!ok) {
            in.reset();
            return fail(sent ? err : QString("the plug-in host process %1 ended").arg(proc->index));
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

void Instance::loaded()
      {
      if (!d->loading)
            return;
      d->loading = false;
      std::lock_guard<std::mutex> lock(registryMutex());
      --d->proc->loading;
      }

QByteArray Instance::call(Op op, const QByteArray& args, bool* ok, bool flush)
      {
      if (flush && d->commands > 0)
            d->request(REQ_COMMANDS, 0);
      QByteArray reply;
      bool sent = false;
      {
            std::lock_guard<std::mutex> lock(d->proc->rpc);
            // (setState: the environment too, as at LOAD; the test synth reads it there)
            sent = d->proc->send(op, d->id, op == Op::SET_STATE ? args + pack([](QDataStream& ds) { ds << environment(); }) : args,
                                 &reply);
      }
      if (ok)
            *ok = sent;
      return reply;
      }

bool Instance::tryCall(Op op, const QByteArray& args, QByteArray* reply)
      {
      std::unique_lock<std::mutex> lock(d->proc->rpc, std::try_to_lock);
      if (!lock.owns_lock())
            return false;
      return d->proc->send(op, d->id, args, reply);
      }

void Instance::Impl::post(RequestKind kind, int frames, int offset, double seconds)
      {
      h->kind = kind;
      h->frames = frames;
      h->offset = offset;
      h->seconds = seconds;
      h->commands = (kind == REQ_FETCH) ? 0 : commands;
      h->restart = (kind == REQ_PROCESS || kind == REQ_SETTLE) ? int(restart.exchange(false)) : 0;
      if (kind != REQ_FETCH)
            commands = 0;
      h->reqSeq.store(++seq, std::memory_order_release);
      proc->req.post();
      }

bool Instance::Impl::wait()
      {
      while (h->doneSeq.load(std::memory_order_acquire) != seq) {
            if (proc->dead)
                  return false;
            done.wait();
            }
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
      // (longer than the output area: rendered whole in the child, fetched in pieces)
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
//   the child
//---------------------------------------------------------

#ifdef VST3_HOST_CHILD

struct ChildInstance {
      quint32 id { 0 };
      std::unique_ptr<Vst3Plugin> plugin;
      Shm seg;
      Signal done;
      Header* h { nullptr };
      std::vector<float> rendered;        // a REQ_PROCESS longer than the output area, for REQ_FETCH
      };

class Child {
   public:
      Pipe in { NO_PIPE };
      Pipe out { NO_PIPE };
      Shm reqShm;
      Signal req;
      std::mutex table;                   // the instances: the main thread changes, the audio thread plays
      std::map<quint32, std::unique_ptr<ChildInstance>> instances;
      std::mutex processing;              // held while the audio thread plays (Vst3Plugin::idle)
      std::atomic<bool> stop { false };
      std::thread audio;
      std::thread reader;
      std::map<quint32, QObject*> editors;

      void audioLoop();
      void serve(ChildInstance* in);
      void handle(const QByteArray& request);
      void reply(const QByteArray& r) { writeMessage(out, r); }
      bool showEditor(quint32 id, Vst3Plugin* p, const QString& title);
      };


bool Child::showEditor(quint32 id, Vst3Plugin* p, const QString& title)
      {
      auto e = editors.find(id);
      if (e != editors.end() && e->second) {
            QWidget* w = static_cast<QWidget*>(e->second);
            w->show();
            w->raise();
            w->activateWindow();
            return true;
            }
      Steinberg::IPlugView* view = p->createEditor();
      if (!view)
            return false;
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
                  default:
                        break;
                  }
            }
      }

void Child::serve(ChildInstance* in)
      {
      Header* h = in->h;
      Vst3Plugin* p = in->plugin.get();
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
      h->sinceState.store(p->secondsSinceState(), std::memory_order_release);
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
#endif
      while (true) {
            req.wait();
            if (stop)
                  return;
            std::lock_guard<std::mutex> lock(table);
            std::lock_guard<std::mutex> playing(processing);
            for (auto& i : instances) {
                  ChildInstance* in = i.second.get();
                  const quint32 want = in->h->reqSeq.load(std::memory_order_acquire);
                  if (want == in->h->doneSeq.load(std::memory_order_relaxed))
                        continue;
                  serve(in);
                  in->h->doneSeq.store(want, std::memory_order_release);
                  in->done.post();
                  }
            }
      }


static void writeTimes(QDataStream& ds, const Vst3Plugin::Times& t)
      {
      ds << t.module << t.create << t.buses << t.activate << t.component << t.controllerComponent << t.controller << t.mapping;
      }

void Child::handle(const QByteArray& request)
      {
      QDataStream ds(request);
      ds.setVersion(QDataStream::Qt_5_12);
      quint32 op32 = 0, id = 0;
      ds >> op32 >> id;
      const Op op = Op(op32);
      if (op == Op::QUIT) {
            QCoreApplication::quit();
            return;
            }
      if (op == Op::HOST_QUERIES) {
            reply(QJsonDocument(Vst3Plugin::hostQueries()).toJson(QJsonDocument::Compact));
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
            in->plugin = Vst3Plugin::load(path, rate, maxBlock, &err);
            const int outFrames = std::max(1, int(maxBlock));
            if (in->plugin && (!in->seg.open(segName, segmentSize(cmdCapacity, outFrames))
                               || !in->done.open(segName + "-done", semPlace(in->seg.data())))) {
                  in->plugin.reset();
                  err = "cannot open the shared memory";
                  }
            if (!in->plugin) {
                  reply(pack([&](QDataStream& o) { o << false << err; }));
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
            reply(r);
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
            gone.reset();
            reply(QByteArray());
            return;
            }
      if (!in) {
            reply(QByteArray());
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
                  p->idle(&processing);
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
            case Op::PARAMETERS: {
                  const std::vector<Vst3Plugin::Parameter> params = p->parameters();
                  o << quint32(params.size());
                  for (const auto& pp : params)
                        o << quint32(pp.id) << pp.title << pp.units << qint32(pp.stepCount) << qint32(pp.flags) << qint32(pp.unitId);
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
            default:
                  break;
            }
      reply(r);
      }

int childMain(int argc, char** argv)
      {
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
                  // (the module, now: the first instance waits less; MuseScore starts the processes together)
                  QString name;
                  quint32 uid[4];
                  Vst3Plugin::classInfo(QFile::decodeName(argv[i + 1]), &name, uid);
                  }
            }
      if (!writeMessage(c.out, "MSVST3HOST"))
            return 4;
      c.audio = std::thread([&c]() { c.audioLoop(); });
      c.reader = std::thread([&c, &app]() {
            QByteArray m;
            while (readMessage(c.in, &m)) {
                  const QByteArray request = m;
                  QMetaObject::invokeMethod(&app, [&c, request]() { c.handle(request); }, Qt::QueuedConnection);
                  }
            QMetaObject::invokeMethod(&app, []() { QCoreApplication::quit(); }, Qt::QueuedConnection);
            });
      app.exec();
      // the instances, then the modules (Kontakt's ExitDll before the process ends: Vst3Plugin::unloadModules)
      c.stop = true;
      c.req.post();
      c.audio.join();
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
