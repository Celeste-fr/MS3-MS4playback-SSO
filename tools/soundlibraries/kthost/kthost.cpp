//=============================================================================
//  kthost: an offline VST 3 host on the command line, built from MuseScore's own hosting code
//  (audio/vst3/vst3plugin.h, kontaktsetup.h, wavwriter.h), for measuring a plug-in's patches
//  (tools/soundlibraries/measure_patch.py, the heard check, the sweeps: docs/MEASURING.md).
//
//    kthost <plugin.vst3> <state> <events.txt> <out.wav> [options]
//
//  It replaces the VM's kthost.exe, whose source was lost (only a restored binary was left, MD5 ea7e692e…):
//  the same positional arguments, event format and main options (--tail, --wait, --probe-pitch, --block,
//  --realtime, --param, --list-params), so the existing scripts run with either. What differs: docs/MEASURING.md
//  › kthost. As MuseScore's export renders a route (SoundLibraryExport, Vst3Synth::beginExport):
//
//    1. Vst3Plugin::load at the rate (44100) and block size (512); the state read (MuseScore's setup file,
//       "MSV3" …, or any state Vst3Plugin::setState takes), edited as MuseScore prepares it when asked
//       (--max-voices: KontaktSetup::withMaxVoices, 512 for every patch MuseScore sets up; --tune / --slots:
//       withTune / withSlots, the tuning copies as one instance's slots, slot i on MIDI channel i + 1;
//       --random-sources: withRandomSources, [slurs] fixedTransitions' 0; --script-value: withScriptValues),
//       then setState and settle (Vst3Plugin::SETTLE_SECONDS) so the patch's script has initialised;
//    2. --param values set (a flat lane: SoundLibraryHost::applyPartSettings), settle 0.25 s more;
//    3. the probe (--wait s > 0): a note at --probe-pitch played until the output passes -60 dBFS (the samples
//       have loaded) or --wait s of wall time have passed, then released and let ring out, all notes off;
//       without it Kontakt may still be streaming the samples of the first notes (they'd play silent);
//    4. offline mode (--realtime: realtime, paced to the clock), the transport restarted (one 64-frame block
//       with the transport stopped, as at every export and playback start: SSO's scripts reset their round
//       robins on a transport start), then the events rendered sample-exactly: the render is split at each
//       event's frame, so an event plays at its own sample (Vst3Plugin delivers events at the block's start);
//    5. the WAV (32-bit float stereo) from time 0 to the last event + --tail s, times --gain dB (the gain
//       stage / master trim: TrackDelays::gainStageDb, applied once per instance as for hosted slots);
//       --repeat n: the same events n times, each pass after the transport restarted, out.wav, out.2.wav …
//
//  Event file: one event a line, "seconds kind a b [channel]" (the format of tst_liveequivalence dumpEvents'
//  "<route> museScore.txt"), '#' starts a comment; at one time in file order:
//    on <pitch> <velocity>     note-on (velocity 0: note-off)
//    off <pitch> [<velocity>]  note-off
//    cc <number> <value>       MIDI controller (0-127), mapped to its parameter (IMidiMapping) as DAWs do
//    pb <value14> [0]          pitch bend, 0..16383 (8192 centre)
//    at <value>                channel aftertouch
//    param <id|title> <value>  a plug-in parameter, normalized 0..1 (title as Vst3Plugin::parameterId: loose)
//    tr                        the transport restarted here (Vst3Plugin::restartTransport)
//    vs <cents>                varispeed (Vst3Plugin::setPitch: hosted tuning by varispeed)
//  channel: 1-16, default 1 (the slot listening to it with --slots).
//
//  Exit codes: 0 rendered (also when the probe never sounded: the report says so), 2 usage, 3 the plug-in
//  didn't load, 4 the state didn't load or couldn't be edited, 5 a bad event file, 6 the WAV couldn't be
//  written. --report <file.json>: what was done and how long it took.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "audio/midi/event.h"
#include "audio/vst3/kontaktsetup.h"
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/wavwriter.h"

using namespace Ms;

namespace {

enum Exit { OK = 0, USAGE = 2, NO_PLUGIN = 3, NO_STATE = 4, BAD_EVENTS = 5, NO_WAV = 6 };

const char* USAGE_TEXT =
"usage: kthost plugin state events out.wav [options]\n"
"  plugin          a .vst3 bundle or file (Kontakt 8.vst3, the test synth …)\n"
"  state           a setup (.vst3state as MuseScore writes it) or '-' for the plug-in's defaults\n"
"  events          event file ('seconds on|off|cc|pb|at|param|tr|vs a b [channel]' lines; see kthost.cpp)\n"
"  out.wav         32-bit float stereo WAV, from time 0\n"
"options:\n"
"  --rate hz             sample rate (44100)\n"
"  --block n             frames per process call, at most (512)\n"
"  --tail s              seconds after the last event (2)\n"
"  --wait s              probe: wait at most s of wall time for the probe note to sound (0: no probe)\n"
"  --probe-pitch n       the probe note's pitch (60); -1: no probe\n"
"  --probe-velocity n    its velocity (64)\n"
"  --settle s            silence processed after setState (0.047, Vst3Plugin::SETTLE_SECONDS)\n"
"  --param id|title=v    a parameter set before the render, normalized (repeatable)\n"
"  --max-voices n        Kontakt: the patch's voice limit set in the state (MuseScore: 512)\n"
"  --tune cents          Kontakt: the patch's Tune set in the state\n"
"  --slots c0,c1,...     Kontakt: a multi of copies tuned c0, c1 … on MIDI channels 1, 2 …\n"
"  --random-sources v    Kontakt: random modulators' intensity (0: [slurs] fixedTransitions)\n"
"  --script-value n=v    Kontakt: a script value set in the state (repeatable)\n"
"  --prepared dir        keep the edited states there (as MuseScore's prepared states), reuse them\n"
"  --gain dB             the output times this (gain stage, trim; repeatable: added)\n"
"  --repeat n            render the events n times (out.wav, out.2.wav …), the transport restarted each time\n"
"  --no-transport-restart  no transport restart before the render\n"
"  --realtime            realtime process mode, paced to the clock (default offline)\n"
"  --rtfast              realtime process mode, not paced\n"
"  --save-state file     the plug-in's state after the render\n"
"  --list-params         print the parameters (id, title, units, steps, flags, value) and exit\n"
"  --report file.json    what was done, timings, the probe, the output's peak\n"
"  --quiet               print only errors\n";

struct HostEvent {
      double seconds { 0 };
      long long frame { 0 };
      int order { 0 };
      enum Kind { ON, OFF, CC, PB, AT, PARAM, TRANSPORT, VARISPEED } kind { ON };
      int a { 0 };
      int b { 0 };
      int channel { 0 };            // 0-15
      QString name;                 // param
      double value { 0 };           // param, vs
      };

bool quiet = false;

void say(const QString& s)
      {
      if (!quiet) {
            fprintf(stdout, "%s\n", qPrintable(s));
            fflush(stdout);
            }
      }

void fail(const QString& s)
      {
      fprintf(stderr, "kthost: %s\n", qPrintable(s));
      fflush(stderr);
      }

bool readEvents(const QString& path, std::vector<HostEvent>* out, QString* error)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            *error = "can't read " + path;
            return false;
            }
      int lineNo = 0;
      while (!f.atEnd()) {
            ++lineNo;
            QString line = QString::fromUtf8(f.readLine());
            const int hash = line.indexOf('#');
            if (hash >= 0)
                  line.truncate(hash);
            const QStringList t = line.simplified().split(' ', Qt::SkipEmptyParts);
            if (t.isEmpty())
                  continue;
            auto bad = [&](const QString& why) {
                  *error = QString("%1 line %2: %3").arg(path).arg(lineNo).arg(why);
                  return false;
                  };
            if (t.size() < 2)
                  return bad("no event");
            bool ok = true;
            HostEvent e;
            e.seconds = t[0].toDouble(&ok);
            if (!ok || !std::isfinite(e.seconds) || e.seconds < 0)
                  return bad("bad time " + t[0]);
            e.order = int(out->size());
            const QString k = t[1].toLower();
            auto num = [&](int i, int def, bool* good) {
                  if (i >= t.size())
                        return def;
                  bool o = true;
                  const int v = t[i].toInt(&o);
                  if (!o)
                        *good = false;
                  return v;
                  };
            auto channel = [&](int i, bool* good) {
                  const int c = num(i, 1, good);
                  if (c < 1 || c > 16)
                        *good = false;
                  return c - 1;
                  };
            bool good = true;
            if (k == "on" || k == "off" || k == "cc") {
                  if (t.size() < (k == "off" ? 3 : 4))
                        return bad("too few fields");
                  e.kind = k == "on" ? HostEvent::ON : k == "off" ? HostEvent::OFF : HostEvent::CC;
                  e.a = num(2, 0, &good);
                  e.b = num(3, 0, &good);
                  e.channel = channel(4, &good);
                  if (e.a < 0 || e.a > 127 || e.b < 0 || e.b > 127)
                        good = false;
                  }
            else if (k == "pb" || k == "at") {
                  if (t.size() < 3)
                        return bad("too few fields");
                  e.kind = k == "pb" ? HostEvent::PB : HostEvent::AT;
                  e.a = num(2, 0, &good);
                  e.channel = channel(4, &good);
                  if (e.a < 0 || e.a > (k == "pb" ? 16383 : 127))
                        good = false;
                  }
            else if (k == "param") {
                  if (t.size() < 4)
                        return bad("param needs a parameter and a value");
                  e.kind = HostEvent::PARAM;
                  e.name = QStringList(t.mid(2, t.size() - 3)).join(' ');
                  e.value = t.last().toDouble(&good);
                  }
            else if (k == "tr")
                  e.kind = HostEvent::TRANSPORT;
            else if (k == "vs") {
                  if (t.size() < 3)
                        return bad("vs needs cents");
                  e.kind = HostEvent::VARISPEED;
                  e.value = t[2].toDouble(&good);
                  }
            else
                  return bad("unknown event " + t[1]);
            if (!good)
                  return bad("bad value");
            out->push_back(e);
            }
      return true;
      }

QByteArray readFile(const QString& path)
      {
      QFile f(path);
      return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
      }

// the state edited as MuseScore prepares it; key: what was done, for --prepared
QByteArray editState(const QByteArray& state, int maxVoices, double tune, bool hasTune, const std::vector<double>& slotCents,
                     double randomSources, bool hasRandom, const std::map<QString, QByteArray>& scriptValues,
                     QString* what, QString* error)
      {
      QString name;
      QByteArray component, controller;
      if (!Vst3Plugin::splitState(state, &name, &component, &controller)) {
            *error = "the state isn't a setup MuseScore writes (MSV3): can't edit it";
            return QByteArray();
            }
      QStringList done;
      QString err;
      if (!scriptValues.empty()) {
            int set = 0;
            component = KontaktSetup::withScriptValues(component, scriptValues, &err, &set);
            if (component.isEmpty()) {
                  *error = "script values: " + err;
                  return QByteArray();
                  }
            QStringList v;
            for (const auto& sv : scriptValues)
                  v << sv.first + "=" + QString::fromUtf8(sv.second);
            done << "script values " + v.join(',') + QString(" (%1 set)").arg(set);
            }
      if (maxVoices > 0) {
            int before = -1;
            component = KontaktSetup::withMaxVoices(component, maxVoices, &err, &before);
            if (component.isEmpty()) {
                  *error = "max voices: " + err;
                  return QByteArray();
                  }
            done << QString("max voices %1 (was %2)").arg(maxVoices).arg(before);
            }
      if (hasRandom) {
            int count = 0;
            component = KontaktSetup::withRandomSources(component, { float(randomSources) }, &err, &count);
            if (component.isEmpty()) {
                  *error = "random sources: " + err;
                  return QByteArray();
                  }
            done << QString("random sources %1 (%2 changed)").arg(randomSources).arg(count);
            }
      if (!slotCents.empty()) {
            component = KontaktSetup::withSlots(component, slotCents, &err);
            if (component.isEmpty()) {
                  *error = "slots: " + err;
                  return QByteArray();
                  }
            QStringList c;
            for (double x : slotCents)
                  c << QString::number(x, 'f', 3);
            done << "slots " + c.join(' ');
            }
      else if (hasTune) {
            double before = 0;
            component = KontaktSetup::withTune(component, tune, &err, &before);
            if (component.isEmpty()) {
                  *error = "tune: " + err;
                  return QByteArray();
                  }
            done << QString("tune %1 (was %2)").arg(tune, 0, 'f', 3).arg(before, 0, 'f', 3);
            }
      *what = done.join("; ");
      return Vst3Plugin::joinState(name, component, controller);
      }

float peakOf(const std::vector<float>& b, size_t n)
      {
      float p = 0;
      for (size_t i = 0; i < n; ++i)
            p = std::max(p, std::fabs(b[i]));
      return p;
      }

double dBFS(float peak)
      {
      return peak > 0 ? 20 * std::log10(double(peak)) : -200.0;
      }

} // namespace

int main(int argc, char* argv[])
      {
      QCoreApplication app(argc, argv);
      QStringList pos;
      double rate = 44100, tail = 2, wait = 0, settle = Vst3Plugin::SETTLE_SECONDS, gainDb = 0, tune = 0, randomSources = 0;
      int block = 512, probePitch = 60, probeVelocity = 64, maxVoices = 0, repeat = 1;
      bool hasTune = false, hasRandom = false, transportRestart = true, realtime = false, paced = false, listParams = false;
      std::vector<double> slotCents;
      std::vector<std::pair<QString, double>> params;
      std::map<QString, QByteArray> scriptValues;
      QString prepared, saveState, reportPath;
      const QStringList args = app.arguments();
      auto need = [&](int& i) -> QString {
            if (i + 1 >= args.size()) {
                  fail(args[i] + " needs a value");
                  exit(USAGE);
                  }
            return args[++i];
            };
      auto number = [&](int& i) {
            const QString a = args[i];
            bool ok = false;
            const double v = need(i).toDouble(&ok);
            if (!ok || !std::isfinite(v)) {
                  fail(a + ": not a number");
                  exit(USAGE);
                  }
            return v;
            };
      for (int i = 1; i < args.size(); ++i) {
            const QString a = args[i];
            if (a == "--help" || a == "-h") {
                  fputs(USAGE_TEXT, stdout);
                  return OK;
                  }
            else if (a == "--rate") rate = number(i);
            else if (a == "--block") block = int(number(i));
            else if (a == "--tail") tail = number(i);
            else if (a == "--wait") wait = number(i);
            else if (a == "--probe-pitch") probePitch = int(number(i));
            else if (a == "--probe-velocity") probeVelocity = int(number(i));
            else if (a == "--settle") settle = number(i);
            else if (a == "--gain") gainDb += number(i);
            else if (a == "--max-voices") maxVoices = int(number(i));
            else if (a == "--tune") { tune = number(i); hasTune = true; }
            else if (a == "--random-sources") { randomSources = number(i); hasRandom = true; }
            else if (a == "--repeat") repeat = std::max(1, int(number(i)));
            else if (a == "--slots") {
                  for (const QString& c : need(i).split(',', Qt::SkipEmptyParts)) {
                        bool ok = false;
                        slotCents.push_back(c.toDouble(&ok));
                        if (!ok) {
                              fail("--slots: not a number: " + c);
                              return USAGE;
                              }
                        }
                  if (slotCents.size() > 16) {
                        fail("--slots: at most 16");
                        return USAGE;
                        }
                  }
            else if (a == "--param" || a == "--script-value") {
                  const QString v = need(i);
                  const int eq = v.lastIndexOf('=');
                  if (eq <= 0) {
                        fail(a + " wants name=value");
                        return USAGE;
                        }
                  if (a == "--param") {
                        bool ok = false;
                        params.emplace_back(v.left(eq), v.mid(eq + 1).toDouble(&ok));
                        if (!ok) {
                              fail("--param: not a number: " + v);
                              return USAGE;
                              }
                        }
                  else
                        scriptValues[v.left(eq)] = v.mid(eq + 1).toUtf8();
                  }
            else if (a == "--prepared") prepared = need(i);
            else if (a == "--save-state") saveState = need(i);
            else if (a == "--report") reportPath = need(i);
            else if (a == "--no-transport-restart") transportRestart = false;
            else if (a == "--realtime") { realtime = true; paced = true; }
            else if (a == "--rtfast") { realtime = true; paced = false; }
            else if (a == "--list-params") listParams = true;
            else if (a == "--quiet") quiet = true;
            else if (a.startsWith("--")) {
                  fail("unknown option " + a + " (kthost --help)");
                  return USAGE;
                  }
            else
                  pos << a;
            }
      if (pos.size() < (listParams ? 2 : 4) || block < 16 || rate < 8000 || tail < 0) {
            fputs(USAGE_TEXT, stderr);
            return USAGE;
            }
      QJsonObject report;
      report["plugin"] = QFileInfo(pos[0]).fileName();
      report["state"] = QFileInfo(pos[1]).fileName();
      using Clock = std::chrono::steady_clock;
      const auto t0 = Clock::now();
      auto msSince = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };
      int code = OK;
      auto finish = [&](int c) {
            report["exit"] = c;
            report["totalMs"] = msSince(t0);
            if (!reportPath.isEmpty()) {
                  QFile f(reportPath);
                  if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                        f.write(QJsonDocument(report).toJson());
                  }
            Vst3Plugin::unloadModules();        // (before exit: Kontakt 8's own teardown crashes otherwise, vst3plugin.h)
            return c;
            };

      std::vector<HostEvent> events;
      if (!listParams) {
            QString err;
            if (!readEvents(pos[2], &events, &err)) {
                  fail(err);
                  report["error"] = err;
                  return finish(BAD_EVENTS);
                  }
            for (HostEvent& e : events)
                  e.frame = (long long)std::llround(e.seconds * rate);
            std::stable_sort(events.begin(), events.end(), [](const HostEvent& x, const HostEvent& y) { return x.frame < y.frame; });
            say(QString("%1 events").arg(events.size()));
            report["events"] = int(events.size());
            }

      // 1. the plug-in and its state
      QString error;
      auto tl = Clock::now();
      std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pos[0], rate, block, &error);
      if (!p) {
            fail("the plug-in didn't load: " + error);
            report["error"] = error;
            return finish(NO_PLUGIN);
            }
      report["loadMs"] = msSince(tl);
      say("load: " + p->name());
      if (pos[1] != "-") {
            QByteArray state = readFile(pos[1]);
            if (state.isEmpty()) {
                  fail("can't read the state " + pos[1]);
                  report["error"] = "bad state file";
                  return finish(NO_STATE);
                  }
            const bool edit = maxVoices > 0 || hasTune || !slotCents.empty() || hasRandom || !scriptValues.empty();
            if (edit) {
                  QString what, cacheFile;
                  const QString key = QString::fromLatin1(QCryptographicHash::hash(state, QCryptographicHash::Sha1).toHex());
                  // (the key needs what will be done: computed from the options, the edit's own description afterwards)
                  QStringList opts;
                  opts << QString::number(maxVoices) << (hasTune ? QString::number(tune, 'f', 3) : QString("-"));
                  for (double c : slotCents)
                        opts << QString::number(c, 'f', 3);
                  opts << (hasRandom ? QString::number(randomSources) : QString("-"));
                  for (const auto& sv : scriptValues)
                        opts << sv.first + "=" + QString::fromUtf8(sv.second);
                  const QString editKey = QString::fromLatin1(QCryptographicHash::hash((key + "|" + opts.join('|')).toUtf8(),
                                                                                        QCryptographicHash::Sha1).toHex());
                  if (!prepared.isEmpty()) {
                        QDir().mkpath(prepared);
                        cacheFile = prepared + "/" + editKey + ".vst3state";
                        }
                  QByteArray edited = cacheFile.isEmpty() ? QByteArray() : readFile(cacheFile);
                  if (!edited.isEmpty())
                        what = "prepared " + editKey;
                  else {
                        const auto te = Clock::now();
                        edited = editState(state, maxVoices, tune, hasTune, slotCents, randomSources, hasRandom, scriptValues, &what, &error);
                        if (edited.isEmpty()) {
                              fail(error);
                              report["error"] = error;
                              return finish(NO_STATE);
                              }
                        report["editMs"] = msSince(te);
                        if (!cacheFile.isEmpty()) {
                              QFile f(cacheFile);
                              if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                                    f.write(edited);
                              }
                        }
                  say("state: " + what);
                  report["stateEdits"] = what;
                  state = edited;
                  }
            const auto ts = Clock::now();
            if (!p->setState(state)) {
                  fail("the plug-in didn't take the state " + pos[1]);
                  report["error"] = "setState failed";
                  return finish(NO_STATE);
                  }
            report["setStateMs"] = msSince(ts);
            say(QString("setState in %1 ms").arg(msSince(ts), 0, 'f', 0));
            }
      p->settle(settle);

      if (listParams) {
            for (const Vst3Plugin::Parameter& pr : p->parameters())
                  fprintf(stdout, "%u\t%s\t%s\t%d\t%d\t%g\n", pr.id, qPrintable(pr.title), qPrintable(pr.units), pr.stepCount, pr.flags,
                          p->parameter(pr.id));
            return finish(OK);
            }

      // 2. parameters
      auto paramId = [&](const QString& n) -> long {
            bool isId = false;
            const unsigned id = n.toUInt(&isId);
            return isId ? long(id) : p->parameterId(n);
            };
      if (!params.empty()) {
            QJsonArray set;
            for (const auto& pr : params) {
                  const long id = paramId(pr.first);
                  if (id < 0) {
                        fail("no parameter " + pr.first + " (--list-params)");
                        report["error"] = "no parameter " + pr.first;
                        return finish(USAGE);
                        }
                  p->setParameter(unsigned(id), std::max(0.0, std::min(1.0, pr.second)));
                  say(QString("param %1 = %2: %3").arg(pr.first).arg(pr.second).arg(p->parameterText(unsigned(id), pr.second)));
                  set.append(QJsonArray({ pr.first, double(id), pr.second }));
                  }
            report["params"] = set;
            p->settle(p->secondsSinceState() + 0.25);
            }
      std::map<QString, long> eventParams;
      for (const HostEvent& e : events)
            if (e.kind == HostEvent::PARAM && !eventParams.count(e.name)) {
                  const long id = paramId(e.name);
                  if (id < 0) {
                        fail("no parameter " + e.name + " (--list-params)");
                        report["error"] = "no parameter " + e.name;
                        return finish(BAD_EVENTS);
                        }
                  eventParams[e.name] = id;
                  }

      std::vector<float> buf(size_t(2 * block));
      auto run = [&](int n) {
            std::fill(buf.begin(), buf.begin() + 2 * n, 0.f);
            p->process(n, buf.data());
            return peakOf(buf, size_t(2 * n));
            };

      // 3. the probe
      if (wait > 0 && probePitch >= 0 && probePitch < 128) {
            const auto tp = Clock::now();
            p->setOffline(true);
            p->midi(ME_NOTEON, 0, probePitch, probeVelocity);
            const float threshold = float(std::pow(10.0, -60.0 / 20));
            bool heard = false;
            long long frames = 0;
            while (msSince(tp) < wait * 1000) {
                  const float pk = run(block);
                  frames += block;
                  if (pk > threshold) {
                        heard = true;
                        break;
                        }
                  if (pk == 0.f)
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));    // (the plug-in's loader threads)
                  }
            p->midi(ME_NOTEOFF, 0, probePitch, 0);
            // rung out: until 0.25 s under -100 dBFS, at most 4 s of audio
            const float silent = float(std::pow(10.0, -100.0 / 20));
            long long quietFor = 0, after = 0;
            while (quietFor < (long long)(0.25 * rate) && after < (long long)(4 * rate)) {
                  const float pk = run(block);
                  after += block;
                  quietFor = pk < silent ? quietFor + block : 0;
                  }
            p->allNotesOff();
            run(block);
            report["probeHeard"] = heard;
            report["probeMs"] = msSince(tp);
            report["probeAudioSeconds"] = double(frames) / rate;
            say(QString("probe %1 after %2 ms").arg(heard ? "heard" : "NOT heard").arg(msSince(tp), 0, 'f', 0));
            }

      // 4. the render
      p->setOffline(!realtime);
      const double gain = std::pow(10.0, gainDb / 20);
      const long long lastFrame = events.empty() ? 0 : events.back().frame;
      const long long total = lastFrame + (long long)std::llround(tail * rate);
      QJsonArray passes;
      for (int pass = 1; pass <= repeat; ++pass) {
            QString outPath = pos[3];
            if (pass > 1) {
                  const QFileInfo fi(pos[3]);
                  outPath = fi.path() + "/" + fi.completeBaseName() + QString(".%1.").arg(pass) + fi.suffix();
                  p->allNotesOff();
                  run(block);
                  }
            WavWriter wav(outPath, 2, int(rate), true);
            if (!wav.ok()) {
                  fail("can't write " + outPath);
                  report["error"] = "can't write " + outPath;
                  return finish(NO_WAV);
                  }
            if (transportRestart)
                  p->restartTransport();
            const auto tr = Clock::now();
            long long at = 0;
            float peak = 0;
            auto renderTo = [&](long long to) {
                  while (at < to) {
                        const int n = int(std::min<long long>(block, to - at));
                        peak = std::max(peak, run(n));
                        wav.write(buf.data(), n, float(gain));
                        at += n;
                        if (paced) {
                              const double ahead = double(at) / rate * 1000 - msSince(tr);
                              if (ahead > 1)
                                    std::this_thread::sleep_for(std::chrono::microseconds(long(ahead * 1000)));
                              }
                        }
                  };
            for (const HostEvent& e : events) {
                  renderTo(e.frame);
                  switch (e.kind) {
                        case HostEvent::ON:
                              p->midi(ME_NOTEON, e.channel, e.a, e.b);
                              break;
                        case HostEvent::OFF:
                              p->midi(ME_NOTEOFF, e.channel, e.a, 0);
                              break;
                        case HostEvent::CC:
                              p->midi(ME_CONTROLLER, e.channel, e.a, e.b);
                              break;
                        case HostEvent::PB:
                              p->midi(ME_PITCHBEND, e.channel, e.a & 0x7f, (e.a >> 7) & 0x7f);
                              break;
                        case HostEvent::AT:
                              p->midi(ME_AFTERTOUCH, e.channel, e.a, 0);
                              break;
                        case HostEvent::PARAM:
                              p->queueParameter(unsigned(eventParams[e.name]), std::max(0.0, std::min(1.0, e.value)));
                              break;
                        case HostEvent::TRANSPORT:
                              p->restartTransport();
                              break;
                        case HostEvent::VARISPEED:
                              p->setPitch(e.value);
                              break;
                        }
                  }
            renderTo(total);
            wav.close();
            const double ms = msSince(tr);
            say(QString("main render %1 ms").arg(ms, 0, 'f', 0));
            say(QString("wrote %1 (%2 s)").arg(outPath).arg(double(total) / rate, 0, 'f', 1));
            QJsonObject po;
            po["wav"] = QFileInfo(outPath).fileName();
            po["seconds"] = double(total) / rate;
            po["renderMs"] = ms;
            po["peakDbfs"] = dBFS(float(peak * gain));
            passes.append(po);
            }
      report["passes"] = passes;
      report["gainDb"] = gainDb;
      report["rate"] = rate;
      report["block"] = block;
      report["offline"] = !realtime;
      report["transportRestart"] = transportRestart;
      if (!saveState.isEmpty()) {
            QFile f(saveState);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                  const QByteArray s = p->state();
                  f.write(s);
                  say(QString("state written: %1 (%2 bytes)").arg(saveState).arg(s.size()));
                  }
            }
      p.reset();
      return finish(code);
      }
