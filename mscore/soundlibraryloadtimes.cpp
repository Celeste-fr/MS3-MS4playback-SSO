//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  LoadTimes: the sound library's load times, measured (see soundlibraryloadtimes.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "soundlibraryloadtimes.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <future>
#include <iterator>
#include <map>
#include <set>

#include <QApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>

#include "musescore.h"
#include "soundlibrarycheck.h"
#include "soundlibraryhost.h"
#include "audio/midi/event.h"
#include "libmscore/part.h"
#include "libmscore/score.h"

#ifdef USE_VST3
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/vst3plugin.h"
#endif

namespace Ms {

#ifdef USE_VST3
namespace {

//---------------------------------------------------------
//   Report
//    the report file, a line at a time (a run that stops half way leaves what it measured), and
//    the background log (stderr)
//---------------------------------------------------------

struct Report {
      QString path;
      void line(const QString& s = QString()) const
            {
            QFile f(path);
            if (f.open(QIODevice::Append | QIODevice::Text))
                  f.write((s + "\n").toUtf8());
            if (!s.isEmpty())
                  ArticulationCheckDialog::logBackground(s);
            }
      };

QString sec(double ms)
      {
      return ms < 0 ? QString("-") : QString::number(ms / 1000.0, 'f', ms < 10000 ? 2 : 1) + " s";
      }

QString mb(qint64 bytes)
      {
      return QString("%1 MB").arg(double(bytes) / (1 << 20), 0, 'f', 0);
      }

double msSince(const QElapsedTimer& t)
      {
      return t.nsecsElapsed() / 1e6;
      }

//---------------------------------------------------------
//   Pumper
//    the instances alive, played in real time (their output dropped) while the run waits, as
//    MuseScore's audio thread plays them; the peak of one of them
//---------------------------------------------------------

struct Pumper {
      std::vector<Vst3Plugin*> plugins;
      Vst3Plugin* listen { nullptr };
      double peak { 0 };
      QElapsedTimer clock;
      qint64 frames { 0 };
      std::vector<float> buffer;

      void set(const std::vector<Vst3Plugin*>& ps)
            {
            plugins = ps;
            clock.start();
            frames = 0;
            }
      void run(int ms)
            {
            QElapsedTimer t;
            t.start();
            if (!clock.isValid())
                  clock.start();
            while (t.elapsed() < ms) {
                  const qint64 due = clock.elapsed() * qint64(MScore::sampleRate) / 1000;
                  if (due - frames > qint64(MScore::sampleRate))     // (fell behind by more than a second: skip ahead)
                        frames = due - 512;
                  while (frames < due) {
                        const int n = int(std::min<qint64>(512, due - frames));
                        for (Vst3Plugin* p : plugins) {
                              buffer.assign(size_t(2 * n), 0.f);
                              p->process(n, buffer.data());
                              if (p == listen)
                                    for (float x : buffer)
                                          peak = std::max(peak, double(std::fabs(x)));
                              }
                        frames += n;
                        }
                  for (Vst3Plugin* p : plugins)
                        p->idle();
                  QApplication::processEvents();
                  QThread::msleep(5);
                  }
            }
      };

//---------------------------------------------------------
//   settle
//    until the process's memory stops growing (by 8 MB) for stableMs, at most capMs: when it last
//    grew (ms since start) and by how much since before
//---------------------------------------------------------

struct Settled {
      double ms { -1 };
      qint64 growth { 0 };
      bool capped { false };
      };

Settled settle(Pumper& pump, const QElapsedTimer& start, qint64 before, int stableMs = 3000, int capMs = 120000)
      {
      Settled s;
      qint64 last = SoundLibraryHost::processMemory();
      double lastGrowth = msSince(start);
      QElapsedTimer stable;
      stable.start();
      while (stable.elapsed() < stableMs) {
            if (msSince(start) > capMs) {
                  s.capped = true;
                  break;
                  }
            pump.run(200);
            const qint64 now = SoundLibraryHost::processMemory();
            if (now > last + (8 << 20)) {
                  last = now;
                  lastGrowth = msSince(start);
                  stable.restart();
                  }
            }
      s.ms = lastGrowth;
      s.growth = last - before;
      return s;
      }

// after instances went: until the memory stops going down, and by how much
Settled freed(Pumper& pump, qint64 before)
      {
      Settled s;
      QElapsedTimer t;
      t.start();
      qint64 last = SoundLibraryHost::processMemory();
      double lastDrop = 0;
      QElapsedTimer stable;
      stable.start();
      while (stable.elapsed() < 2000 && t.elapsed() < 60000) {
            pump.run(200);
            const qint64 now = SoundLibraryHost::processMemory();
            if (now < last - (8 << 20)) {
                  last = now;
                  lastDrop = msSince(t);
                  stable.restart();
                  }
            }
      s.ms = lastDrop;
      s.growth = last - before;
      return s;
      }

//---------------------------------------------------------
//   what is measured
//---------------------------------------------------------

struct Route {
      QString name;                       // the patch
      QString part;
      int lane { 0 };
      const SoundLib::LibInstrument* instrument { nullptr };
      };

struct Load {                             // one instance loaded
      std::unique_ptr<Vst3Plugin> plugin;
      QString name;
      QString kind;                       // what its setup was
      double createMs { -1 };
      double setupMs { 0 };
      double setStateMs { 0 };
      double resaveMs { 0 };
      Vst3Plugin::Times times;
      bool ok { false };
      QString error;
      };

QByteArray writeState(const QString& name, const QByteArray& component, const QByteArray& controller)
      {
      QByteArray result("MSV3");
      QDataStream ds(&result, QIODevice::WriteOnly | QIODevice::Append);
      ds << quint32(1) << name << component << controller;
      return result;
      }

bool readState(const QByteArray& state, QString* name, QByteArray* component, QByteArray* controller)
      {
      if (!state.startsWith("MSV3"))
            return false;
      QDataStream ds(state.mid(4));
      quint32 version = 0;
      ds >> version >> *name >> *component >> *controller;
      return ds.status() == QDataStream::Ok && version == 1;
      }

class Run {
      std::shared_ptr<const SoundLib::Library> _lib;
      const SoundLib::Library& lib;
      LoadTimes::Options opt;
      Report rep;
      QString path;                       // the plug-in
      Pumper pump;

      bool resaved(const QString& patch) const
            {
            QFile f(SoundLibraryHost::setupsFolder(lib) + "/made setups.json");
            return f.open(QIODevice::ReadOnly)
                   && QJsonDocument::fromJson(f.readAll()).object().value(patch).toObject().value("resaved").toBool();
            }

      // a new instance (else p), its setup read or made; setState here or (threaded) by the caller
      Load begin(const QString& name, std::unique_ptr<Vst3Plugin> reuse = nullptr)
            {
            Load l;
            l.name = name;
            QElapsedTimer t;
            t.start();
            if (reuse)
                  l.plugin = std::move(reuse);
            else {
                  l.plugin = Vst3Plugin::load(path, MScore::sampleRate, 4096, &l.error);
                  if (!l.plugin)
                        return l;
                  l.createMs = msSince(t);
                  }
            const bool before = resaved(name);
            t.restart();
            QByteArray state;
            if (SoundLibraryHost::hasSetup(lib, name))
                  state = SoundLibraryHost::setupState(lib, name, path, &l.error);
            l.setupMs = msSince(t);
            const bool after = resaved(name);
            l.kind = !SoundLibraryHost::makesSetups(lib) ? QString("a setup file")
                     : after ? (before ? QString("Kontakt's own state") : QString("Kontakt's own state, taken from another setups folder"))
                     : QString("made from the .nki");
            l.ok = !state.isEmpty();
            _states[name] = state;
            return l;
            }
      void set(Load& l)
            {
            if (!l.ok)
                  return;
            QElapsedTimer t;
            t.start();
            l.ok = l.plugin->setState(_states[l.name]);
            l.setStateMs = msSince(t);
            finish(l);
            }
      void finish(Load& l)
            {
            l.times = l.plugin->times();
            if (!l.ok) {
                  if (l.error.isEmpty())
                        l.error = "the plug-in did not take the setup";
                  return;
                  }
            QElapsedTimer t;
            t.start();
            SoundLibraryHost::setupLoaded(l.plugin.get(), lib, l.name, _states[l.name], l.setupMs);      // (resaves a made one)
            l.resaveMs = msSince(t);
            }
      QString describe(const Load& l) const
            {
            const Vst3Plugin::Times& tm = l.times;
            QString s = l.createMs >= 0 ? QString("new instance %1 (create %2, buses %3, activate %4)")
                        .arg(sec(l.createMs), sec(tm.create), sec(tm.buses), sec(tm.activate)) : QString("reused instance");
            if (!l.ok)
                  return s + " | FAILED: " + l.error;
            s += QString(" | setup: %1, %2 KB, read %3 | setState %4 (component %5, controller %6 + %7, MIDI mapping %8)")
                 .arg(l.kind).arg(_states.at(l.name).size() / 1024).arg(sec(l.setupMs)).arg(sec(l.setStateMs))
                 .arg(sec(tm.component), sec(tm.controllerComponent), sec(tm.controller), sec(tm.mapping));
            if (l.resaveMs > 5)
                  s += QString(" | log and resave %1").arg(sec(l.resaveMs));
            return s;
            }

      void prime(Vst3Plugin* p, const SoundLib::LibInstrument* ins, int value)
            {
            if (ins && ins->switchType == SoundLib::SwitchType::CC && value >= 0)
                  p->midi(ME_CONTROLLER, 0, ins->switchNumber, value);
            if (lib.dynamicsCC >= 0)
                  p->midi(ME_CONTROLLER, 0, lib.dynamicsCC, 100);
            if (lib.dynamicsCC != 11)
                  p->midi(ME_CONTROLLER, 0, 11, lib.expressionValue);
            }
      int firstValue(const SoundLib::LibInstrument* ins) const
            {
            return ins && !ins->articulations.empty() ? ins->articulations.front().value : -1;
            }
      // until a note sounds (from start), ms; -1: not within capMs
      double untilSounds(Vst3Plugin* p, const SoundLib::LibInstrument* ins, const QElapsedTimer& start, int capMs = 30000)
            {
            const int pitch = ins ? ArticulationCheckDialog::testPitch(*ins) : 60;
            pump.listen = p;
            double at = -1;
            while (at < 0 && msSince(start) < capMs) {
                  prime(p, ins, firstValue(ins));
                  p->midi(ME_NOTEON, 0, pitch, 100);
                  pump.peak = 0;
                  for (int i = 0; i < 5 && at < 0; ++i) {     // (a new note every 250 ms: one started before its samples were in may stay silent)
                        pump.run(50);
                        if (pump.peak > 1e-4)
                              at = msSince(start);
                        }
                  p->midi(ME_NOTEON, 0, pitch, 0);
                  }
            p->allNotesOff();
            pump.run(300);
            pump.listen = nullptr;
            return at;
            }
      // which of the patch's articulation values play at its test pitch (a note after a pause, louder
      // than what still rings from the last)
      std::set<int> sounding(Vst3Plugin* p, const SoundLib::LibInstrument* ins)
            {
            std::set<int> out;
            if (!ins)
                  return out;
            const int pitch = ArticulationCheckDialog::testPitch(*ins);
            std::vector<int> values;
            if (ins->switchType == SoundLib::SwitchType::CC)
                  for (const SoundLib::Articulation& a : ins->articulations)
                        if (a.value >= 0 && std::find(values.begin(), values.end(), a.value) == values.end())
                              values.push_back(a.value);
            if (values.empty())
                  values.push_back(-1);
            pump.listen = p;
            for (int v : values) {
                  prime(p, ins, v);
                  pump.peak = 0;
                  pump.run(100);
                  const double residual = pump.peak;
                  pump.peak = 0;
                  p->midi(ME_NOTEON, 0, pitch, 100);
                  pump.run(350);
                  p->midi(ME_NOTEON, 0, pitch, 0);
                  if (pump.peak > std::max(1e-4, 4 * residual))
                        out.insert(v);
                  p->allNotesOff();
                  pump.run(450);
                  }
            pump.listen = nullptr;
            return out;
            }
      QString names(const SoundLib::LibInstrument* ins, const std::set<int>& values) const
            {
            QStringList n;
            for (int v : values) {
                  QString name = QString::number(v);
                  if (ins)
                        for (const SoundLib::Articulation& a : ins->articulations)
                              if (a.value == v) {
                                    name = QString("%1 %2").arg(v).arg(a.name);
                                    break;
                                    }
                  n << name;
                  }
            return n.join(", ");
            }

      void phaseAlone(const std::vector<Route>& unique);
      void phaseScore(const QString& title, const std::vector<Route>& routes, int threads);
      void phaseReuse(const std::vector<Route>& unique);
      void phaseProbe(const Route& r);

      std::map<QString, QByteArray> _states;         // the setups as read
      std::map<QString, qint64> _alone;              // phase 1: each patch's memory

   public:
      Run(std::shared_ptr<const SoundLib::Library> library, const LoadTimes::Options& o)
         : _lib(library), lib(*library), opt(o) {}
      bool run(QString* reportPath);
      };

//---------------------------------------------------------
//   phase 1: each patch alone
//---------------------------------------------------------

void Run::phaseAlone(const std::vector<Route>& unique)
      {
      rep.line();
      rep.line(QString("# 1 Each patch alone (%1): a new instance, its setup, until a note sounds, until the memory settles, "
                       "then freed").arg(unique.size()));
      for (const Route& r : unique) {
            pump.set({});
            const qint64 before = SoundLibraryHost::processMemory();
            QElapsedTimer start;
            start.start();
            Load l = begin(r.name);
            if (!l.plugin) {
                  rep.line(QString("%1 | no instance: %2").arg(r.name, l.error));
                  continue;
                  }
            set(l);
            const double blocking = msSince(start);
            pump.set({ l.plugin.get() });
            const double sounds = l.ok ? untilSounds(l.plugin.get(), r.instrument, start) : -1;
            const Settled s = settle(pump, start, before);
            _alone[r.name] = s.growth;
            rep.line(QString("%1 | %2 | blocked %3 | sounds after %4 | memory +%5, settled after %6%7")
                     .arg(r.name, describe(l), sec(blocking), sec(sounds), mb(s.growth), sec(s.ms), s.capped ? " (still growing)" : ""));
            const qint64 loaded = SoundLibraryHost::processMemory();
            l.plugin.reset();
            pump.set({});
            const Settled f = freed(pump, loaded);
            rep.line(QString("%1 | freed %2 in %3").arg(r.name, mb(-f.growth), sec(f.ms)));
            }
      }

//---------------------------------------------------------
//   phase 2 and 3: a score's instances as MuseScore loads them at score open, one after the
//   other on this thread (threads 0), or setState on worker threads
//---------------------------------------------------------

void Run::phaseScore(const QString& title, const std::vector<Route>& routes, int threads)
      {
      rep.line();
      rep.line(threads ? QString("# 3 %1 on %2 worker threads (setState; instances made here): %3 instances")
                         .arg(title).arg(threads).arg(routes.size())
                       : QString("# 2 %1 as MuseScore loads it at score open (one after the other on this thread): %2 instances")
                         .arg(title).arg(routes.size()));
      pump.set({});
      const qint64 before = SoundLibraryHost::processMemory();
      QElapsedTimer start;
      start.start();
      std::vector<Load> loads;
      loads.reserve(routes.size());
      double longest = 0;
      if (!threads) {
            for (const Route& r : routes) {
                  QElapsedTimer t;
                  t.start();
                  Load l = begin(r.name);
                  if (l.plugin)
                        set(l);
                  longest = std::max(longest, msSince(t));
                  rep.line(QString("%1%2 | %3").arg(r.name, r.lane ? QString(" (other tuning %1)").arg(r.lane) : QString(),
                                                   l.plugin ? describe(l) : "no instance: " + l.error));
                  QApplication::processEvents();
                  loads.push_back(std::move(l));
                  }
            }
      else {
            struct Running {
                  size_t index;
                  std::future<bool> done;
                  QElapsedTimer clock;
                  };
            std::vector<Running> running;
            size_t next = 0;
            auto harvest = [&](bool wait) {
                  for (auto i = running.begin(); i != running.end();) {
                        if (i->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                              if (wait && msSince(i->clock) > 180000) {
                                    rep.line(QString("%1 | no answer from the plug-in 180 s after setState started on a worker thread: "
                                                     "the run stops here (the plug-in doesn't take its state on another thread)")
                                             .arg(loads[i->index].name));
                                    std::_Exit(3);
                                    }
                              ++i;
                              continue;
                              }
                        Load& l = loads[i->index];
                        l.ok = i->done.get();
                        l.setStateMs = msSince(i->clock);
                        finish(l);
                        rep.line(QString("%1 | %2 (setState from start to done, on a worker thread)").arg(l.name, describe(l)));
                        i = running.erase(i);
                        }
                  };
            while (next < routes.size() || !running.empty()) {
                  while (next < routes.size() && int(running.size()) < threads) {
                        QElapsedTimer t;
                        t.start();
                        loads.push_back(begin(routes[next].name));
                        Load& l = loads.back();
                        longest = std::max(longest, msSince(t));
                        if (l.plugin && l.ok) {
                              Vst3Plugin* p = l.plugin.get();
                              const QByteArray state = _states[l.name];
                              Running run;
                              run.index = loads.size() - 1;
                              run.clock.start();
                              run.done = std::async(std::launch::async, [p, state]() {
                                    SoundLibraryHost::workerThread(true);
                                    const bool ok = p->setState(state);
                                    SoundLibraryHost::workerThread(false);
                                    return ok;
                                    });
                              running.push_back(std::move(run));
                              }
                        else
                              rep.line(QString("%1 | %2").arg(l.name, l.plugin ? describe(l) : "no instance: " + l.error));
                        ++next;
                        }
                  harvest(true);
                  QApplication::processEvents(QEventLoop::AllEvents, 5);
                  if (!running.empty())
                        running.front().done.wait_for(std::chrono::milliseconds(5));
                  }
            }
      const double loaded = msSince(start);
      std::vector<Vst3Plugin*> ps;
      int failed = 0;
      double create = 0, setup = 0, setState = 0;
      for (const Load& l : loads) {
            if (l.plugin)
                  ps.push_back(l.plugin.get());
            failed += !l.ok;
            create += std::max(0.0, l.createMs);
            setup += l.setupMs;
            setState += l.setStateMs;
            }
      pump.set(ps);
      const Settled s = settle(pump, start, before, 5000, 300000);
      rep.line(QString("%1 | all %2 loaded in %3 (new instances %4, setups read %5, setState %6%7; the longest single block of "
                       "the window %8)%9 | memory +%10, settled %11 after the start%12")
               .arg(threads ? QString("SUMMARY %1 threads").arg(threads) : QString("SUMMARY one after the other"))
               .arg(loads.size()).arg(sec(loaded), sec(create), sec(setup), sec(setState),
                                     threads ? QString(" summed over the threads") : QString(), sec(longest))
               .arg(failed ? QString(", %1 FAILED").arg(failed) : QString()).arg(mb(s.growth), sec(s.ms))
               .arg(s.capped ? " (still growing after 5 minutes)" : ""));
      const qint64 full = SoundLibraryHost::processMemory();
      loads.clear();
      pump.set({});
      const Settled f = freed(pump, full);
      rep.line(QString("freed %1 in %2").arg(mb(-f.growth), sec(f.ms)));
      }

//---------------------------------------------------------
//   phase 4: an instance that has a patch given another (SoundLibraryHost's spares)
//---------------------------------------------------------

void Run::phaseReuse(const std::vector<Route>& unique)
      {
      if (unique.size() < 2)
            return;
      const Route& a = unique[0];
      const Route& b = unique[1];
      rep.line();
      rep.line(QString("# 4 Reuse: %1 loaded, then %2 on the same instance (a spare taking a new patch), against %2 on a new "
                       "instance (phase 1)").arg(a.name, b.name));
      pump.set({});
      const qint64 before = SoundLibraryHost::processMemory();
      QElapsedTimer start;
      start.start();
      Load la = begin(a.name);
      if (!la.plugin)
            return;
      set(la);
      pump.set({ la.plugin.get() });
      const Settled sa = settle(pump, start, before);
      rep.line(QString("%1 | %2 | memory +%3").arg(a.name, describe(la), mb(sa.growth)));
      QElapsedTimer t;
      t.start();
      Load lb = begin(b.name, std::move(la.plugin));
      set(lb);
      const double blocking = msSince(t);
      pump.set({ lb.plugin.get() });
      const double sounds = lb.ok ? untilSounds(lb.plugin.get(), b.instrument, t) : -1;
      const Settled sb = settle(pump, t, before);
      rep.line(QString("%1 on it | %2 | blocked %3 | sounds after %4 | memory now +%5 against before %6 (settled %7; alone in phase 1: "
                       "%1 +%8, %6 +%9: +%8 means %6's samples went)")
               .arg(b.name, describe(lb), sec(blocking), sec(sounds), mb(sb.growth), a.name, sec(sb.ms))
               .arg(_alone.count(b.name) ? mb(_alone[b.name]) : QString("-"), _alone.count(a.name) ? mb(_alone[a.name]) : QString("-")));
      lb.plugin.reset();
      pump.set({});
      freed(pump, SoundLibraryHost::processMemory());
      }

//---------------------------------------------------------
//   phase 5: what unloads samples (on one patch)
//---------------------------------------------------------

void Run::phaseProbe(const Route& r)
      {
      QElapsedTimer total;
      total.start();
      const qint64 cap = qint64(opt.probeMinutes) * 60000;
      rep.line();
      rep.line(QString("# 5 Probe on %1: the mic levels at 0; each saved script value holding only 0 and 1 turned over "
                       "(memory, articulations that go silent at pitch %2)").arg(r.name).arg(r.instrument ? ArticulationCheckDialog::testPitch(*r.instrument) : 60));
      // the patch as it loads: its memory, what sounds, its script's saved values
      auto loadVariant = [&](const QByteArray& state, qint64* growth, double* setMs, bool* ok) {
            pump.set({});
            const qint64 before = SoundLibraryHost::processMemory();
            QElapsedTimer start;
            start.start();
            QString err;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(path, MScore::sampleRate, 4096, &err);
            if (!p) {
                  *ok = false;
                  return p;
                  }
            QElapsedTimer t;
            t.start();
            *ok = p->setState(state);
            *setMs = msSince(t);
            pump.set({ p.get() });
            *growth = settle(pump, start, before, 2500, 60000).growth;
            return p;
            };
      auto drop = [&](std::unique_ptr<Vst3Plugin>& p) {
            const qint64 full = SoundLibraryHost::processMemory();
            p.reset();
            pump.set({});
            freed(pump, full);
            };
      Load base = begin(r.name);
      if (!base.plugin || !base.ok) {
            rep.line(QString("%1 | no setup: %2").arg(r.name, base.error));
            return;
            }
      base.plugin.reset();
      const QByteArray state0 = _states[r.name];
      QString stateName;
      QByteArray component0, controller0;
      if (!readState(state0, &stateName, &component0, &controller0)) {
            rep.line("its setup could not be read");
            return;
            }
      qint64 baseMem = 0;
      double setMs = 0;
      bool ok = false;
      std::unique_ptr<Vst3Plugin> p = loadVariant(state0, &baseMem, &setMs, &ok);
      if (!p || !ok) {
            rep.line("the patch did not load");
            return;
            }
      const std::set<int> baseSounding = sounding(p.get(), r.instrument);
      rep.line(QString("as it loads | setState %1 | memory +%2 | sounding: %3").arg(sec(setMs), mb(baseMem), names(r.instrument, baseSounding)));

      // the mic levels (Spitfire's faders; does 0 unload a mic's samples?)
      std::vector<std::pair<unsigned, double>> mics;
      for (const Vst3Plugin::Parameter& par : p->parameters())
            if (par.title.startsWith("Mic ", Qt::CaseInsensitive) && !par.title.contains("Distance", Qt::CaseInsensitive)
                && !par.title.contains("Unused", Qt::CaseInsensitive))
                  mics.push_back({ par.id, p->parameter(par.id) });
      if (!mics.empty()) {
            QStringList titles;
            const qint64 m0 = SoundLibraryHost::processMemory();
            for (const auto& m : mics) {
                  titles << QString("%1 %2").arg(p->parameterText(m.first, m.second)).arg(m.second, 0, 'f', 2);
                  p->setParameter(m.first, 0.0);
                  }
            QElapsedTimer t;
            t.start();
            pump.run(1000);
            const Settled s = settle(pump, t, m0, 3000, 20000);
            rep.line(QString("mic levels at 0 (%1 parameters titled Mic …; were %2) | memory %3%4 in %5")
                     .arg(mics.size()).arg(titles.join(", ")).arg(s.growth >= 0 ? "+" : "").arg(mb(s.growth)).arg(sec(s.ms)));
            for (const auto& m : mics)
                  p->setParameter(m.first, m.second);
            pump.run(500);
            }
      else
            rep.line("no parameter titled Mic …");

      // the script's saved values, as Kontakt saves them once the patch has loaded
      QString n;
      QByteArray loadedComponent, dummy;
      readState(p->state(), &n, &loadedComponent, &dummy);
      const std::map<QString, QByteArray> values = KontaktSetup::scriptValues(KontaktSetup::slotProgram(loadedComponent, nullptr));
      const std::map<QString, QByteArray> setupValues = KontaktSetup::scriptValues(KontaktSetup::slotProgram(component0, nullptr));
      drop(p);
      QStringList all;
      std::vector<QString> candidates;
      static const QRegularExpression binary("^[01](?:[^0-9A-Za-z]+[01])*$");
      for (const auto& v : values) {
            const QString text = QString::fromLatin1(v.second).trimmed();
            all << QString("%1=%2").arg(v.first, text.left(80));
            if (binary.match(text).hasMatch())
                  candidates.push_back(v.first);
            }
      rep.line(QString("saved script values (%1; %2 hold only 0 and 1): %3").arg(values.size()).arg(candidates.size()).arg(all.join(" ")));
      int differ = 0;
      for (const auto& v : values)
            differ += setupValues.count(v.first) && setupValues.at(v.first) != v.second;
      rep.line(QString("%1 of them differ from the setup's (the script changed them as it loaded)").arg(differ));

      // each candidate turned over: 1s to 0 (else 0s to 1)
      auto turned = [](const QByteArray& v) {
            QByteArray out = v;
            const bool ones = v.contains('1');
            for (char& c : out)
                  if (c == (ones ? '1' : '0'))
                        c = ones ? '0' : '1';
            return out;
            };
      struct Hit {
            QString name;
            qint64 saved;
            int elements;
            };
      std::vector<Hit> hits;
      for (const QString& name : candidates) {
            if (total.elapsed() > cap) {
                  rep.line(QString("stopped after %1 minutes (%2 values left)").arg(opt.probeMinutes).arg(int(candidates.size()) - int(hits.size())));
                  break;
                  }
            const QByteArray was = setupValues.count(name) ? setupValues.at(name) : values.at(name);
            const QByteArray now = turned(values.at(name));
            QString err;
            int count = 0;
            const QByteArray component = KontaktSetup::withScriptValues(component0, { { name, now } }, &err, &count);
            if (component.isEmpty() || !count) {
                  rep.line(QString("%1 | not set (%2)").arg(name, err.isEmpty() ? QString("not in the setup's program with that length") : err));
                  continue;
                  }
            qint64 mem = 0;
            p = loadVariant(writeState(stateName, component, controller0), &mem, &setMs, &ok);
            if (!p || !ok) {
                  rep.line(QString("%1 = %2 | the plug-in did not take it").arg(name, QString::fromLatin1(now)));
                  if (p)
                        drop(p);
                  continue;
                  }
            QString n2;
            QByteArray c2, d2;
            readState(p->state(), &n2, &c2, &d2);
            const QByteArray kept = KontaktSetup::scriptValues(KontaktSetup::slotProgram(c2, nullptr))[name];
            const std::set<int> s = sounding(p.get(), r.instrument);
            std::set<int> silent;
            std::set_difference(baseSounding.begin(), baseSounding.end(), s.begin(), s.end(), std::inserter(silent, silent.begin()));
            const qint64 saved = baseMem - mem;
            rep.line(QString("%1 = %2 (setup %3, loaded %4) | setState %5 | memory +%6 (%7%8 %) | %9 | silent now: %10")
                     .arg(name, QString::fromLatin1(now).left(80), QString::fromLatin1(was).left(40), QString::fromLatin1(values.at(name)).left(40))
                     .arg(sec(setMs), mb(mem)).arg(saved > 0 ? "-" : "+").arg(baseMem > 0 ? std::abs(100.0 * saved / baseMem) : 0.0, 0, 'f', 0)
                     .arg(kept == now ? QString("kept") : QString("the script set it back to %1").arg(QString::fromLatin1(kept).left(40)))
                     .arg(silent.empty() ? QString("none") : names(r.instrument, silent)));
            drop(p);
            const int elements = QString::fromLatin1(now).count(QRegularExpression("[01]"));
            hits.push_back({ name, saved, elements });
            }

      // the value that saved the most, if it saved a lot and has several elements: each element alone
      auto best = std::max_element(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.saved < b.saved; });
      if (best == hits.end() || best->saved < baseMem / 7 || best->elements < 2) {
            rep.line(QString("no value with several elements saved a seventh of the memory or more: no element-by-element probe"));
            return;
            }
      const QString name = best->name;
      const QByteArray value = values.at(name);
      rep.line(QString("%1 saved %2: its %3 elements one at a time").arg(name, mb(best->saved)).arg(best->elements));
      int element = 0;
      for (int i = 0; i < value.size(); ++i) {
            if (value[i] != '0' && value[i] != '1')
                  continue;
            ++element;
            if (total.elapsed() > cap) {
                  rep.line(QString("stopped after %1 minutes").arg(opt.probeMinutes));
                  break;
                  }
            QByteArray now = value;
            now[i] = value[i] == '1' ? '0' : '1';
            QString err;
            int count = 0;
            const QByteArray component = KontaktSetup::withScriptValues(component0, { { name, now } }, &err, &count);
            if (component.isEmpty() || !count)
                  continue;
            qint64 mem = 0;
            p = loadVariant(writeState(stateName, component, controller0), &mem, &setMs, &ok);
            if (!p || !ok) {
                  if (p)
                        drop(p);
                  continue;
                  }
            const std::set<int> s = sounding(p.get(), r.instrument);
            std::set<int> silent;
            std::set_difference(baseSounding.begin(), baseSounding.end(), s.begin(), s.end(), std::inserter(silent, silent.begin()));
            rep.line(QString("%1 element %2 (%3 → %4) | memory +%5 (%6 %) | silent now: %7")
                     .arg(name).arg(element).arg(QChar(value[i])).arg(QChar(now[i])).arg(mb(mem))
                     .arg(baseMem > 0 ? 100.0 * (mem - baseMem) / baseMem : 0.0, 0, 'f', 0)
                     .arg(silent.empty() ? QString("none") : names(r.instrument, silent)));
            drop(p);
            }
      }

//---------------------------------------------------------
//   run
//---------------------------------------------------------

bool Run::run(QString* reportPath)
      {
      QString error;
      path = SoundLibraryHost::pluginPath(lib, &error);
      const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/MuseScore Sound Library Check";
      QDir().mkpath(folder);
      rep.path = folder + QString("/%1 load times %2.txt").arg(lib.name, QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm"));
      QFile::remove(rep.path);
      if (reportPath)
            *reportPath = rep.path;
      rep.line(QString("# Load times of %1, %2").arg(lib.name, QDateTime::currentDateTime().toString(Qt::ISODate)));
      if (path.isEmpty()) {
            rep.line(error);
            return false;
            }
      qint64 total = -1, available = -1;
      SoundLibraryHost::systemMemory(&total, &available);
      rep.line(QString("MuseScore %1 (%2) | plug-in %3 | %4 Hz | memory %5 of %6 free | io/soundLibraryLoadThreads %7")
               .arg(VERSION, revision, QFileInfo(path).fileName()).arg(MScore::sampleRate)
               .arg(available >= 0 ? mb(available) : QString("?"), total >= 0 ? mb(total) : QString("?"))
               .arg(SoundLibraryHost::loadThreads()));
      rep.line(QString("setups: %1 (a copy of the working MuseScore's)").arg(QDir::toNativeSeparators(SoundLibraryHost::setupsFolder(lib))));

      // what to load: the scores' routes, else the patches listed, else a few of each family
      std::vector<std::pair<QString, std::vector<Route>>> sets;
      SoundLib::setAvailable([](const SoundLib::LibInstrument& li) {
            std::shared_ptr<const SoundLib::Library> l = SoundLib::current();
            return l && SoundLibraryHost::hasSetup(*l, li.name);
            });
      for (const QString& file : opt.scores) {
            Score* score = mscore ? mscore->openScore(file, false, false) : nullptr;
            if (!score) {
                  rep.line(QString("%1: could not be opened").arg(QDir::toNativeSeparators(file)));
                  continue;
                  }
            std::vector<Route> rs;
            QStringList parts;
            QElapsedTimer t;
            t.start();
            const std::vector<SoundLib::Route> routes = SoundLib::routes(score->masterScore(), lib);
            const double routesMs = msSince(t);
            for (const SoundLib::Route& r : routes) {
                  if (r.instrument->kit)
                        continue;
                  rs.push_back({ r.instrument->name, r.part->partName(), r.lane, r.instrument });
                  parts << QString("%1: %2%3").arg(r.part->partName(), r.instrument->name, r.lane ? QString(" ~%1").arg(r.lane) : QString());
                  }
            rep.line(QString("score %1 | %2 instances | its routes worked out in %3 | %4").arg(QFileInfo(file).fileName()).arg(rs.size())
                     .arg(sec(routesMs), parts.join("; ")));
            sets.push_back({ "Score " + QFileInfo(file).fileName(), rs });
            delete score;
            }
      if (opt.scores.isEmpty()) {
            QStringList wanted;
            if (!opt.patchesFile.isEmpty()) {
                  QFile f(opt.patchesFile);
                  if (f.open(QIODevice::ReadOnly | QIODevice::Text))
                        for (const QString& l : QString::fromUtf8(f.readAll()).split('\n'))
                              if (!l.trimmed().isEmpty() && !l.trimmed().startsWith('#'))
                                    wanted << l.trimmed();
                  }
            if (wanted.isEmpty())
                  wanted = QStringList { "Violins 1", "Violas", "Celli", "Violins 1 - Performance", "Flutes a2", "Horns a2",
                                         "Trumpet Solo", "Timpani" };
            std::vector<Route> rs;
            for (const QString& w : wanted) {
                  const SoundLib::LibInstrument* li = SoundLibraryHost::findPatch(lib, w);
                  if (!li || !SoundLibraryHost::hasSetup(lib, w)) {
                        rep.line(QString("%1: not in the map, or no setup (its .nki not found)").arg(w));
                        continue;
                        }
                  rs.push_back({ li->name, QString(), 0, li });
                  }
            rep.line(QString("patches | %1").arg(wanted.join(", ")));
            sets.push_back({ "The patches", rs });
            }
      std::vector<Route> unique;
      for (const auto& s : sets)
            for (const Route& r : s.second)
                  if (std::none_of(unique.begin(), unique.end(), [&r](const Route& u) { return u.name == r.name; }))
                        unique.push_back(r);
      if (unique.empty()) {
            rep.line("nothing to load");
            return false;
            }

      phaseAlone(unique);
      for (const auto& s : sets)
            phaseScore(s.first, s.second, 0);
      for (int t : opt.threads)
            for (const auto& s : sets)
                  if (t > 0)
                        phaseScore(s.first, s.second, t);
      phaseReuse(unique);
      if (opt.probe) {
            const Route* probe = nullptr;
            for (const Route& r : unique)
                  if (opt.probePatch.isEmpty() ? (r.instrument && r.instrument->articulations.size() >= 5)
                                               : r.name.compare(opt.probePatch, Qt::CaseInsensitive) == 0) {
                        probe = &r;
                        break;
                        }
            Route other;
            if (!probe && !opt.probePatch.isEmpty()) {
                  if (const SoundLib::LibInstrument* li = SoundLibraryHost::findPatch(lib, opt.probePatch)) {
                        other = { li->name, QString(), 0, li };
                        probe = &other;
                        }
                  }
            if (probe)
                  phaseProbe(*probe);
            else
                  rep.line("\n# 5 Probe: no patch with 5 or more articulations measured");
            }
      rep.line();
      rep.line("# Done");
      return true;
      }

} // namespace
#endif

bool LoadTimes::run(std::shared_ptr<const SoundLib::Library> library, const Options& options, QString* report)
      {
#ifdef USE_VST3
      if (!library)
            return false;
      Run r(library, options);
      return r.run(report);
#else
      Q_UNUSED(library);
      Q_UNUSED(options);
      Q_UNUSED(report);
      return false;
#endif
      }

} // namespace Ms
