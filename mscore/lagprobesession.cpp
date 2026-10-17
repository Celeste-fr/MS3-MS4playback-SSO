//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  LagProbeSession (lagprobesession.h has the design)
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "lagprobesession.h"

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <QAbstractEventDispatcher>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QWheelEvent>

#include "automationlanes.h"
#include "evenautomeasurer.h"
#include "musescore.h"
#include "playbackmode.h"
#include "preferences.h"
#include "scoreview.h"
#include "seq.h"
#include "shortcut.h"
#include "soundlibraryhost.h"
#include "inspector/inspector.h"
#include "libmscore/automation.h"
#include "libmscore/chord.h"
#include "libmscore/lagprobe.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/playbacksettings.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/staff.h"

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tlhelp32.h>
#elif defined(Q_OS_LINUX)
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace Ms {

namespace {

//---------------------------------------------------------
//   threads' CPU
//---------------------------------------------------------

struct ThreadCpu {
      QString label;
      qint64 cpuNs { 0 };
      };

QString hexOffset(quintptr v)
      {
      return QString("+0x%1").arg(qulonglong(v), 0, 16);
      }

std::map<quint64, ThreadCpu> threadsNow()
      {
      std::map<quint64, ThreadCpu> out;
#ifdef Q_OS_WIN
      typedef LONG (NTAPI *QueryThread)(HANDLE, ULONG, PVOID, ULONG, PULONG);
      typedef HRESULT (WINAPI *GetDescription)(HANDLE, PWSTR*);
      static const QueryThread query = reinterpret_cast<QueryThread>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
      static const GetDescription getDescription = reinterpret_cast<GetDescription>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
      HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snap == INVALID_HANDLE_VALUE)
            return out;
      THREADENTRY32 te;
      te.dwSize = sizeof(te);
      const DWORD pid = GetCurrentProcessId();
      if (Thread32First(snap, &te)) {
            do {
                  if (te.th32OwnerProcessID != pid)
                        continue;
                  HANDLE h = OpenThread(THREAD_QUERY_INFORMATION, FALSE, te.th32ThreadID);
                  if (!h)
                        h = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
                  if (!h)
                        continue;
                  ThreadCpu t;
                  FILETIME c, e, k, u;
                  if (GetThreadTimes(h, &c, &e, &k, &u)) {
                        const quint64 kk = (quint64(k.dwHighDateTime) << 32) | k.dwLowDateTime;
                        const quint64 uu = (quint64(u.dwHighDateTime) << 32) | u.dwLowDateTime;
                        t.cpuNs = qint64((kk + uu) * 100);
                        }
                  if (getDescription) {
                        PWSTR d = nullptr;
                        if (SUCCEEDED(getDescription(h, &d)) && d) {
                              t.label = QString::fromWCharArray(d);
                              LocalFree(d);
                              }
                        }
                  if (t.label.isEmpty() && query) {
                        PVOID addr = nullptr;
                        if (query(h, 9 /* ThreadQuerySetWin32StartAddress */, &addr, sizeof(addr), nullptr) == 0 && addr) {
                              HMODULE m = nullptr;
                              wchar_t name[1024] = { 0 };
                              if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                     reinterpret_cast<LPCWSTR>(addr), &m) && m
                                  && GetModuleFileNameW(m, name, 1024)) {
                                    t.label = QFileInfo(QString::fromWCharArray(name)).fileName()
                                              + hexOffset(quintptr(addr) - quintptr(m));
                                    }
                              else
                                    t.label = "unknown" + hexOffset(quintptr(addr));
                              }
                        }
                  if (t.label.isEmpty())
                        t.label = "unnamed";
                  out[te.th32ThreadID] = t;
                  CloseHandle(h);
                  } while (Thread32Next(snap, &te));
            }
      CloseHandle(snap);
#elif defined(Q_OS_LINUX)
      const long hz = sysconf(_SC_CLK_TCK);
      for (const QString& tid : QDir("/proc/self/task").entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            QFile f("/proc/self/task/" + tid + "/stat");
            if (!f.open(QIODevice::ReadOnly))
                  continue;
            const QByteArray s = f.readAll();
            const int open = s.indexOf('('), close = s.lastIndexOf(')');
            if (open < 0 || close < 0)
                  continue;
            ThreadCpu t;
            t.label = QString::fromUtf8(s.mid(open + 1, close - open - 1));
            const QList<QByteArray> fields = s.mid(close + 2).split(' ');
            // (after the name: state is field 3; utime 14, stime 15 -> index 11, 12)
            if (fields.size() > 12)
                  t.cpuNs = qint64((fields[11].toLongLong() + fields[12].toLongLong()) * (1e9 / hz));
            out[tid.toULongLong()] = t;
            }
#endif
      return out;
      }

qint64 processCpuNs()
      {
#ifdef Q_OS_WIN
      FILETIME c, e, k, u;
      if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u))
            return 0;
      const quint64 kk = (quint64(k.dwHighDateTime) << 32) | k.dwLowDateTime;
      const quint64 uu = (quint64(u.dwHighDateTime) << 32) | u.dwLowDateTime;
      return qint64((kk + uu) * 100);
#elif defined(Q_OS_LINUX)
      struct rusage r;
      getrusage(RUSAGE_SELF, &r);
      return qint64(r.ru_utime.tv_sec + r.ru_stime.tv_sec) * 1000000000LL + qint64(r.ru_utime.tv_usec + r.ru_stime.tv_usec) * 1000;
#else
      return 0;
#endif
      }

//---------------------------------------------------------
//   statistics
//---------------------------------------------------------

double percentile(std::vector<double> v, double p)
      {
      if (v.empty())
            return 0;
      std::sort(v.begin(), v.end());
      const double i = p * (v.size() - 1);
      const size_t lo = size_t(i);
      const size_t hi = std::min(v.size() - 1, lo + 1);
      return v[lo] + (v[hi] - v[lo]) * (i - lo);
      }

QJsonObject stats(const std::vector<double>& v)
      {
      QJsonObject o;
      o["n"] = int(v.size());
      o["p50"] = percentile(v, 0.5);
      o["p95"] = percentile(v, 0.95);
      o["p99"] = percentile(v, 0.99);
      o["max"] = v.empty() ? 0.0 : *std::max_element(v.begin(), v.end());
      double sum = 0;
      for (double x : v)
            sum += x;
      o["sum"] = sum;
      return o;
      }

double ms(qint64 ns)
      {
      return ns / 1e6;
      }

//---------------------------------------------------------
//   Probe
//---------------------------------------------------------

class Probe : public QObject {
      struct Tick {
            qint64 t;
            double late;
            };
      struct Action {
            QString name;
            int rep;
            qint64 t0, t1;
            qint64 response { -1 };
            QString note;
            };
      struct Busy {
            qint64 start, end;
            };
      struct Phase {
            QString name, mode;
            qint64 start { 0 };
            qint64 processCpu { 0 };
            std::map<quint64, ThreadCpu> threads;
            std::vector<Tick> ticks;
            std::vector<LagProbe::Sample> samples;
            std::vector<Action> actions;
            std::vector<Busy> busy;
            int replays { 0 };
            QStringList skipped;
            };
      struct Step {
            QString what;
            std::function<void()> run;
            std::function<bool()> until;        // polled every 100 ms after run
            int timeoutMs { 0 };
            int afterMs { 0 };
            };

      QFile _log;
      QTimer _tick, _drain;
      qint64 _lastTick { -1 };
      qint64 _awake { -1 };
      int _pending { -1 };                // the action whose response is awaited
      std::unique_ptr<Phase> _phase;
      std::deque<Step> _steps;
      int _reps { 20 };
      int _gapMs { 800 };
      std::vector<Note*> _notes;
      QString _origScoreSettings;
      QString _origLibrary, _origLibraryLast;
      bool _inspectorWasVisible { false };
      qint64 _stableSince { -1 };
      int _lanePoint { -1 };             // the tick of the point the lane action added (-1: none)

      void write(const QJsonObject& o)
            {
            _log.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + "\n");
            _log.flush();
            }
      static qint64 now() { return LagProbe::nowNs(); }
      Score* cs() const { return mscore ? mscore->currentScore() : nullptr; }
      ScoreView* cv() const { return mscore ? mscore->currentScoreView() : nullptr; }

      void onTick()
            {
            const qint64 t = now();
            if (_lastTick >= 0 && _phase)
                  _phase->ticks.push_back({ t, std::max(0.0, ms(t - _lastTick) - 10.0) });
            _lastTick = t;
            }
      void drain()
            {
            std::vector<LagProbe::Sample> s = LagProbe::take();
            if (_phase)
                  _phase->samples.insert(_phase->samples.end(), s.begin(), s.end());
            }
      void awake()
            {
            _awake = now();
            }
      void aboutToBlock()
            {
            const qint64 t = now();
            if (_phase && _pending >= 0 && _pending < int(_phase->actions.size())) {
                  _phase->actions[size_t(_pending)].response = t - _phase->actions[size_t(_pending)].t0;
                  _pending = -1;
                  }
            if (_phase && _awake >= 0 && t - _awake > 30000000)
                  _phase->busy.push_back({ _awake, t });
            _awake = -1;
            }

      // the steps
      void add(const QString& what, std::function<void()> run, int afterMs = 0, std::function<bool()> until = nullptr,
               int timeoutMs = 0)
            {
            Step s;
            s.what = what;
            s.run = run;
            s.until = until;
            s.timeoutMs = timeoutMs;
            s.afterMs = afterMs;
            _steps.push_back(s);
            }
      void next()
            {
            if (_steps.empty())
                  return;
            Step s = _steps.front();
            _steps.pop_front();
            if (s.run)
                  s.run();
            if (s.until) {
                  const qint64 started = now();
                  auto poll = std::make_shared<std::function<void()>>();
                  *poll = [this, s, started, poll]() {
                        if (s.until() || (s.timeoutMs > 0 && ms(now() - started) > s.timeoutMs)) {
                              if (!s.until()) {
                                    QJsonObject o;
                                    o["k"] = "timeout";
                                    o["what"] = s.what;
                                    o["after_s"] = ms(now() - started) / 1000;
                                    write(o);
                                    }
                              QTimer::singleShot(s.afterMs, this, [this]() { next(); });
                              return;
                              }
                        QTimer::singleShot(100, this, [poll]() { (*poll)(); });
                        };
                  QTimer::singleShot(100, this, [poll]() { (*poll)(); });
                  }
            else
                  QTimer::singleShot(s.afterMs, this, [this]() { next(); });
            }

      void beginPhase(const QString& name, const QString& mode)
            {
            drain();
            _phase.reset(new Phase);
            _phase->name = name;
            _phase->mode = mode;
            _phase->start = now();
            _phase->processCpu = processCpuNs();
            _phase->threads = threadsNow();
            LagProbe::takeAudio();
            QJsonObject o;
            o["k"] = "phaseStart";
            o["phase"] = name;
            o["mode"] = mode;
            o["t"] = ms(now()) / 1000;
            write(o);
            }
      void endPhase();

      bool valid(const char* action) const
            {
            const Shortcut* sc = Shortcut::getShortcut(action);
            return sc && (sc->state() & mscore->state());
            }
      void measured(const QString& name, int rep, std::function<QString()> fn)
            {
            if (!_phase)
                  return;
            if (_phase->mode == "playing" && !seq->isPlaying() && !EvenAutoMeasurer::instance()->holding()) {
                  ++_phase->replays;
                  if (QAction* play = getAction("play"))
                        if (!play->isChecked())
                              play->trigger();
                  }
            Action a;
            a.name = name;
            a.rep = rep;
            a.t0 = now();
            a.note = fn();
            a.t1 = now();
            if (a.note == "skipped") {
                  if (!_phase->skipped.contains(name))
                        _phase->skipped.append(name);
                  return;
                  }
            _phase->actions.push_back(a);
            _pending = int(_phase->actions.size()) - 1;
            }
      QString trigger(const char* action)
            {
            if (!valid(action))
                  return "skipped";
            if (QAction* a = getAction(action))
                  a->trigger();
            return QString();
            }
      QString selectNote(int rep)
            {
            Score* s = cs();
            if (!s || _notes.empty())
                  return "skipped";
            Note* n = _notes[size_t(rep) % _notes.size()];
            // as a click on it does (ScoreView::mousePressEventNormal): selected, its preview played, endCmd
            s->deselectAll();
            s->select(n, SelectType::SINGLE, 0);
            s->updateCapo();
            mscore->play(n);
            s->setUpdateAll();
            s->update();
            mscore->endCmd();
            return QString("%1 tick %2").arg(n->staff() ? n->staff()->partName() : QString()).arg(n->tick().ticks());
            }
      QString scroll(int rep)
            {
            ScoreView* v = cv();
            if (!v)
                  return "skipped";
            const QPoint pos(v->width() / 2, v->height() / 2);
            const QPoint angle(rep % 2 ? 360 : -360, 0);         // (three wheel steps sideways, back the next round)
            QWheelEvent ev(QPointF(pos), QPointF(v->mapToGlobal(pos)), QPoint(), angle, Qt::NoButton, Qt::NoModifier,
                           Qt::NoScrollPhase, false);
            QApplication::sendEvent(v, &ev);
            return QString();
            }
      QString lanePoint(bool add)
            {
            // as the automation lanes write a point (AutomationLanes' working copy: one undo step, heard at once while
            // playing): a step point in the middle of the first part's first lane, then removed
            Score* s = cs();
            if (!s)
                  return "skipped";
            MasterScore* ms = s->masterScore();
            std::map<const Part*, Automation::PartLanes> all = Automation::read(ms);
            const Part* part = nullptr;
            for (auto& pl : all)
                  if (!pl.second.empty()) {
                        part = pl.first;
                        break;
                        }
            if (!part)
                  return "skipped";
            Automation::Lane& l = all[part].front();
            if (add) {
                  _lanePoint = ms->endTick().ticks() / 2;
                  const double v = l.valueAt(_lanePoint);
                  l.points.push_back(Automation::Point(_lanePoint, v >= 0 ? std::min(1.0, v + 0.1) : 0.5, Automation::Curve::STEP));
                  std::sort(l.points.begin(), l.points.end());
                  }
            else {
                  if (_lanePoint < 0)
                        return "skipped";
                  auto i = std::find_if(l.points.begin(), l.points.end(), [this](const Automation::Point& p) { return p.tick == _lanePoint; });
                  if (i == l.points.end())
                        return "skipped";
                  l.points.erase(i);
                  _lanePoint = -1;
                  }
            if (seq)
                  seq->waitForRendering();
            if (Automation::undoWrite(ms, all)) {
                  SoundLibraryHost::instance()->applyPartSettings(ms, part);
                  if (seq)
                        seq->renderAgainPlaying();
                  }
            if (ScoreView* v = cv())
                  v->update();
            return l.target;
            }

      void collectNotes()
            {
            _notes.clear();
            Score* s = cs();
            if (!s)
                  return;
            std::vector<Note*> all;
            for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest))
                  for (int track = 0; track < s->ntracks(); ++track) {
                        Element* e = seg->element(track);
                        if (e && e->isChord() && !toChord(e)->notes().empty())
                              all.push_back(toChord(e)->notes().front());
                        }
            const int n = 20;
            for (int i = 0; i < n && !all.empty(); ++i)
                  _notes.push_back(all[size_t((0.05 + 0.9 * i / (n - 1)) * (all.size() - 1))]);
            }

      void setEvenLevels(bool on)
            {
            Score* s = cs();
            if (!s)
                  return;
            MasterScore* ms = s->masterScore();
            std::map<QString, double> v = Playback::scoreValues(ms);
            if (on)
                  ms->setMetaTag(Playback::metaTag, _origScoreSettings);
            else {
                  v["levels/evenLevels"] = 0;
                  ms->setMetaTag(Playback::metaTag, Playback::writeScoreValues(v));
                  }
            ms->setPlaylistDirty();
            emit ms->playlistChanged();
            EvenAutoMeasurer::instance()->probeRefresh(s);
            }
      void setLanesOpen(bool on)
            {
            ScoreView* v = cv();
            if (!v || !v->automationLanes())
                  return;
            for (const Part* p : v->automationLanes()->headerParts())
                  v->automationLanes()->unfold(p, on);
            v->update();
            }
      void play(bool on)
            {
            QAction* a = getAction("play");
            if (!a)
                  return;
            if (on) {
                  if (QAction* r = getAction("rewind"))
                        r->trigger();
                  if (!a->isChecked())
                        a->trigger();
                  }
            else if (a->isChecked())
                  a->trigger();
            }
      void addRounds(const QString& phase, const QString& mode)
            {
            add("begin " + phase + " " + mode, [this, phase, mode]() { beginPhase(phase, mode); }, 1000);
            for (int rep = 0; rep < _reps; ++rep) {
                  const std::vector<std::pair<QString, std::function<QString()>>> actions = {
                        { "select note", [this, rep]() { return selectNote(rep); } },
                        { "pitch-up", [this]() { return trigger("pitch-up"); } },
                        { "pitch-down", [this]() { return trigger("pitch-down"); } },
                        { "staccato add", [this]() { return trigger("add-staccato"); } },
                        { "staccato remove", [this]() { return trigger("add-staccato"); } },
                        { "undo", [this]() { return trigger("undo"); } },
                        { "redo", [this]() { return trigger("redo"); } },
                        { "scroll", [this, rep]() { return scroll(rep); } },
                        { _inspectorWasVisible ? "inspector close" : "inspector open", [this]() { return trigger("inspector"); } },
                        { _inspectorWasVisible ? "inspector open" : "inspector close", [this]() { return trigger("inspector"); } },
                        { "lane point add", [this]() { return lanePoint(true); } },
                        { "lane point remove", [this]() { return lanePoint(false); } },
                        };
                  for (const auto& a : actions) {
                        const QString name = a.first;
                        const std::function<QString()> fn = a.second;
                        add(name, [this, name, rep, fn]() { measured(name, rep, fn); }, _gapMs);
                        }
                  }
            add("end " + phase + " " + mode, [this]() { endPhase(); }, 1000);
            }
      void addPhase(const QString& phase, const QStringList& modes)
            {
            for (const QString& mode : modes) {
                  if (mode == "playing")
                        add("play " + phase, [this]() { play(true); }, 2000, []() { return seq && seq->isPlaying(); }, 600000);
                  addRounds(phase, mode);
                  if (mode == "playing")
                        add("stop " + phase, [this]() { play(false); }, 2000, []() { return seq && !seq->isPlaying(); }, 20000);
                  }
            }
      bool quiet()
            {
            // the library's preload and the even levels' measurement done, for 5 s in a row
            const bool busy = !cs() || SoundLibraryHost::instance()->preloading() || EvenAutoMeasurer::instance()->probeBusy();
            if (busy) {
                  _stableSince = -1;
                  return false;
                  }
            if (_stableSince < 0)
                  _stableSince = now();
            return ms(now() - _stableSince) > 5000;
            }

   public:
      Probe()
            {
            _log.setFileName(LagProbe::logPath());
            _log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
            bool ok = false;
            const int reps = qEnvironmentVariableIntValue("MS_LAG_PROBE_REPS", &ok);
            if (ok && reps > 0)
                  _reps = reps;
            const int gap = qEnvironmentVariableIntValue("MS_LAG_PROBE_GAP_MS", &ok);
            if (ok && gap > 0)
                  _gapMs = gap;
            _tick.setTimerType(Qt::PreciseTimer);
            _tick.setInterval(10);
            connect(&_tick, &QTimer::timeout, this, [this]() { onTick(); });
            _drain.setInterval(250);
            connect(&_drain, &QTimer::timeout, this, [this]() { drain(); });
            QAbstractEventDispatcher* d = QAbstractEventDispatcher::instance();
            connect(d, &QAbstractEventDispatcher::awake, this, [this]() { awake(); });
            connect(d, &QAbstractEventDispatcher::aboutToBlock, this, [this]() { aboutToBlock(); });
            }

      void start()
            {
            LagProbe::markMainThread();
            _tick.start();
            _drain.start();
            Score* s = cs();
            if (s)
                  _origScoreSettings = s->masterScore()->metaTag(Playback::metaTag);
            _origLibrary = preferences.getString(PREF_IO_SOUNDLIBRARY);
            _origLibraryLast = preferences.getString(PREF_IO_SOUNDLIBRARY_LAST);
            _inspectorWasVisible = mscore->inspector() && mscore->inspector()->isVisible();
            if (s && s->layoutMode() != LayoutMode::LINE)
                  trigger("viewmode");
            collectNotes();
            QJsonObject o;
            o["k"] = "start";
            o["score"] = s ? s->masterScore()->fileInfo()->completeBaseName() : QString();
            o["cores"] = QThread::idealThreadCount();
            o["os"] = QSysInfo::prettyProductName();
            o["qt"] = QString(qVersion());
            o["reps"] = _reps;
            o["gap_ms"] = _gapMs;
            o["playbackMode"] = playbackModeName(playbackMode());
            o["evenLevels"] = s ? Playback::value("levels/evenLevels", s) : -1;
            o["inspectorVisible"] = _inspectorWasVisible;
            o["notes"] = int(_notes.size());
            o["parts"] = s ? s->parts().size() : 0;
            write(o);

            const QStringList phases = qEnvironmentVariableIsSet("MS_LAG_PROBE_PHASES")
                                       ? qEnvironmentVariable("MS_LAG_PROBE_PHASES").split(QChar(0x2c))
                                       : QStringList({ "o", "b", "c", "d", "a", "e" });
            const QStringList modes = qEnvironmentVariableIsSet("MS_LAG_PROBE_MODES")
                                      ? qEnvironmentVariable("MS_LAG_PROBE_MODES").split(QChar(0x2c))
                                      : QStringList({ "stopped", "playing" });
            const int openTimeout = 1000 * (qEnvironmentVariableIsSet("MS_LAG_PROBE_OPEN_S") ? qEnvironmentVariableIntValue("MS_LAG_PROBE_OPEN_S") : 2400);
            for (const QString& p : phases) {
                  if (p == "o") {
                        add("begin o", [this]() { beginPhase("o", "opening"); });
                        add("wait opening", nullptr, 0, [this]() { return quiet(); }, openTimeout);
                        add("end o", [this]() { endPhase(); collectNotes(); }, 1000);
                        }
                  else if (p == "b") {
                        add("setup b", [this]() { EvenAutoMeasurer::instance()->setProbePaused(false); setEvenLevels(true); }, 2000,
                            [this]() { return quiet(); }, openTimeout);
                        addPhase("b", modes);
                        }
                  else if (p == "c") {
                        add("setup c", [this]() { EvenAutoMeasurer::instance()->setProbePaused(false); setEvenLevels(true); }, 2000,
                            [this]() { return quiet(); }, openTimeout);
                        add("pause c", [this]() { EvenAutoMeasurer::instance()->setProbePaused(true); }, 1000);
                        addPhase("c", modes);
                        }
                  else if (p == "d") {
                        add("setup d", [this]() { setEvenLevels(true); }, 2000, [this]() { return quiet(); }, openTimeout);
                        add("pause d", [this]() { EvenAutoMeasurer::instance()->setProbePaused(true); setLanesOpen(true); }, 2000);
                        addPhase("d", modes);
                        add("fold d", [this]() { setLanesOpen(false); }, 1000);
                        }
                  else if (p == "a") {
                        add("setup a", [this]() { EvenAutoMeasurer::instance()->setProbePaused(false); setEvenLevels(false); }, 3000);
                        addPhase("a", modes);
                        }
                  else if (p == "e") {
                        add("setup e", [this]() { EvenAutoMeasurer::instance()->setProbePaused(false); setEvenLevels(true);
                                                  setPlaybackMode(PlaybackMode::MS4); }, 3000,
                            []() { return !SoundLibraryHost::instance()->preloading(); }, 600000);
                        addPhase("e", modes);
                        }
                  }
            add("finish", [this]() { finish(); });
            QTimer::singleShot(0, this, [this]() { next(); });
            }

      void finish()
            {
            EvenAutoMeasurer::instance()->setProbePaused(false);
            if (Score* s = cs())
                  s->masterScore()->setMetaTag(Playback::metaTag, _origScoreSettings);
            preferences.setPreference(PREF_IO_SOUNDLIBRARY, _origLibrary);
            preferences.setPreference(PREF_IO_SOUNDLIBRARY_LAST, _origLibraryLast);
            QJsonObject o;
            o["k"] = "end";
            o["t"] = ms(now()) / 1000;
            write(o);
            _log.close();
            if (!qEnvironmentVariableIsSet("MS_LAG_PROBE_KEEP")) {
                  // (no save, no questions: the probe's edits are undone; the session file and the autosaves go, as at a
                  // normal quit, so that the next start asks nothing)
                  for (Score* s : mscore->scores())
                        if (!s->tmpName().isEmpty())
                              QFile::remove(s->tmpName());
                  QFile::remove(dataPath + "/session");
                  QTimer::singleShot(500, []() { std::_Exit(0); });
                  }
            }
      };

//---------------------------------------------------------
//   endPhase
//---------------------------------------------------------

void Probe::endPhase()
      {
      if (!_phase)
            return;
      drain();
      _pending = -1;
      Phase& p = *_phase;
      const qint64 end = now();
      const double wall = ms(end - p.start);
      const LagProbe::AudioStats audio = LagProbe::takeAudio();

      // the event loop
      std::vector<double> late;
      int over50 = 0, over100 = 0;
      for (const Tick& t : p.ticks) {
            late.push_back(t.late);
            over50 += t.late > 50;
            over100 += t.late > 100;
            }
      QJsonObject loop = stats(late);
      loop["over50"] = over50;
      loop["over100"] = over100;

      // the scopes: by name and thread (main / other)
      std::map<QString, std::vector<double>> byName;
      for (const LagProbe::Sample& s : p.samples)
            byName[QString(s.main ? "main: " : "other: ") + s.name].push_back(ms(s.durNs));
      QJsonArray scopes;
      for (const auto& b : byName) {
            QJsonObject o = stats(b.second);
            o["name"] = b.first;
            scopes.append(o);
            }
      // (on the main thread, also by the scope they ran in: what calls the renders)
      std::map<QString, std::vector<double>> byParent;
      for (const LagProbe::Sample& s : p.samples)
            if (s.main && s.parent)
                  byParent[QString("%1 < %2 < %3").arg(s.name, s.parent, s.root ? s.root : "")].push_back(ms(s.durNs));
      QJsonArray parents;
      for (const auto& b : byParent) {
            QJsonObject o = stats(b.second);
            if (o["sum"].toDouble() < 20)
                  continue;
            o["name"] = b.first;
            parents.append(o);
            }

      // the threads' CPU over the phase
      const std::map<quint64, ThreadCpu> threads = threadsNow();
      std::map<QString, std::pair<int, qint64>> byLabel;
      for (const auto& t : threads) {
            auto s = p.threads.find(t.first);
            const qint64 d = t.second.cpuNs - (s != p.threads.end() ? s->second.cpuNs : 0);
            auto& l = byLabel[t.second.label];
            ++l.first;
            l.second += d;
            }
      std::vector<std::pair<qint64, QString>> order;
      for (const auto& l : byLabel)
            order.push_back({ l.second.second, l.first });
      std::sort(order.rbegin(), order.rend());
      QJsonArray cpu;
      for (const auto& oc : order) {
            if (oc.first <= 0)
                  continue;
            QJsonObject o;
            o["thread"] = oc.second;
            o["count"] = byLabel[oc.second].first;
            o["cpu_ms"] = ms(oc.first);
            o["pct"] = 100.0 * ms(oc.first) / wall;
            cpu.append(o);
            }

      QJsonObject o;
      o["k"] = "phase";
      o["phase"] = p.name;
      o["mode"] = p.mode;
      o["wall_s"] = wall / 1000;
      o["loop"] = loop;
      o["process_cpu_pct"] = 100.0 * ms(processCpuNs() - p.processCpu) / wall;
      o["memory_mb"] = SoundLibraryHost::processMemory() / 1048576.0;
      QJsonObject a;
      a["calls"] = audio.calls;
      a["mean_ms"] = audio.calls ? ms(audio.sumNs) / audio.calls : 0.0;
      a["max_ms"] = ms(audio.maxNs);
      a["overruns"] = audio.overruns;
      a["period_ms"] = ms(audio.periodNs);
      a["load_pct"] = 100.0 * ms(audio.sumNs) / wall;
      o["audio"] = a;
      o["replays"] = p.replays;
      o["skipped"] = QJsonArray::fromStringList(p.skipped);
      o["evenBusyAtEnd"] = EvenAutoMeasurer::instance()->probeBusy();
      o["scopes"] = scopes;
      o["scopesByParent"] = parents;
      o["cpu"] = cpu;
      write(o);

      // each action: its window (to the next action) with the event loop's worst lateness and the main thread's
      // outermost scopes in it
      for (size_t i = 0; i < p.actions.size(); ++i) {
            const Action& ac = p.actions[i];
            const qint64 winEnd = i + 1 < p.actions.size() ? p.actions[i + 1].t0 : end;
            double worst = 0;
            for (const Tick& t : p.ticks)
                  if (t.t >= ac.t0 && t.t < winEnd)
                        worst = std::max(worst, t.late);
            std::map<QString, double> in, sync;
            for (const LagProbe::Sample& s : p.samples) {
                  if (!s.main || s.startNs < ac.t0 || s.startNs >= winEnd)
                        continue;
                  if (s.depth == 0)
                        in[s.name] += ms(s.durNs);
                  if (s.startNs + s.durNs <= ac.t1 && s.depth <= 2)
                        sync[QString("%1%2").arg(QString(s.depth, '>')).arg(s.name)] += ms(s.durNs);
                  }
            QJsonObject w, sy;
            for (const auto& x : in)
                  if (x.second >= 1)
                        w[x.first] = x.second;
            for (const auto& x : sync)
                  if (x.second >= 1)
                        sy[x.first] = x.second;
            QJsonObject l;
            l["k"] = "action";
            l["phase"] = p.name;
            l["mode"] = p.mode;
            l["name"] = ac.name;
            l["rep"] = ac.rep;
            l["sync_ms"] = ms(ac.t1 - ac.t0);
            // (the response: to the event loop's next idle; where the dispatcher doesn't say, to the probe's next firing)
            qint64 back = -1;
            for (const Tick& t : p.ticks)
                  if (t.t > ac.t1) {
                        back = t.t - ac.t0;
                        break;
                        }
            l["response_ms"] = ac.response >= 0 ? ms(ac.response) : (back >= 0 ? ms(back) : -1.0);
            l["loop_back_ms"] = back >= 0 ? ms(back) : -1.0;
            l["window_ms"] = ms(winEnd - ac.t0);
            l["window_worst_late_ms"] = worst;
            l["window_scopes"] = w;
            l["sync_scopes"] = sy;
            if (!ac.note.isEmpty())
                  l["note"] = ac.note;
            write(l);
            }

      // the main thread's busy stretches, with what ran in them: each event-loop firing more than 30 ms late (the
      // main thread held from its due time on: [due, fired]; the dispatcher's awake / aboutToBlock are not emitted
      // alike on every platform), the outermost scopes by their overlap with it
      std::vector<Busy> stretches;
      for (const Tick& t : p.ticks)
            if (t.late > 30)
                  stretches.push_back({ t.t - qint64((t.late + 10) * 1e6), t.t });
      for (const Busy& b : stretches) {
            std::map<QString, double> in;
            double attributed = 0;
            for (const LagProbe::Sample& s : p.samples) {
                  if (!s.main || s.depth != 0)
                        continue;
                  const qint64 o = std::min(b.end, s.startNs + s.durNs) - std::max(b.start, s.startNs);
                  if (o <= 0)
                        continue;
                  in[QString(s.name) + (s.detail.isEmpty() ? QString() : " (" + s.detail + ")")] += ms(o);
                  attributed += ms(o);
                  }
            QJsonObject w;
            for (const auto& x : in)
                  if (x.second >= 1)
                        w[x.first] = x.second;
            QJsonObject l;
            l["k"] = "busy";
            l["phase"] = p.name;
            l["mode"] = p.mode;
            l["t"] = ms(b.start - p.start) / 1000;
            l["dur_ms"] = ms(b.end - b.start);
            l["scopes"] = w;
            l["unattributed_ms"] = ms(b.end - b.start) - attributed;
            write(l);
            }

      // every slow scope on the main thread (and the other threads' longest)
      for (const LagProbe::Sample& s : p.samples) {
            if (ms(s.durNs) < (s.main ? 50 : 500))
                  continue;
            QJsonObject l;
            l["k"] = "slow";
            l["phase"] = p.name;
            l["mode"] = p.mode;
            l["name"] = s.name;
            l["main"] = s.main;
            l["depth"] = s.depth;
            if (s.parent)
                  l["parent"] = s.parent;
            if (s.root)
                  l["root"] = s.root;
            l["t"] = ms(s.startNs - p.start) / 1000;
            l["dur_ms"] = ms(s.durNs);
            if (!s.detail.isEmpty())
                  l["detail"] = s.detail;
            write(l);
            }
      _phase.reset();
      }

} // namespace

//---------------------------------------------------------
//   startIfRequested
//---------------------------------------------------------

void LagProbeSession::startIfRequested()
      {
      if (!LagProbe::on() || !mscore)
            return;
      static Probe* probe = nullptr;
      if (probe)
            return;
      probe = new Probe;
      // (once the event loop runs: the score shown, its preload started)
      QTimer::singleShot(1000, probe, [] { probe->start(); });
      }

} // namespace Ms
