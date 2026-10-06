//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "liveequivalence.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>

#include "audio/midi/event.h"
#include "libmscore/liveclips.h"
#include "libmscore/liveset.h"
#include "libmscore/livesetwriter.h"
#include "libmscore/part.h"
#include "libmscore/score.h"
#include "libmscore/soundlibrary.h"
#include "libmscore/synthesizerstate.h"
#include "liveintegration.h"
#include "livesetexport.h"
#include "soundlibraryhost.h"
#ifdef USE_VST3
#include "audio/vst3/vst3plugin.h"
#include "audio/vst3/vst3synth.h"
#endif

namespace Ms {
namespace LiveEquivalence {

//---------------------------------------------------------
//   deviceMidi
//---------------------------------------------------------

std::vector<DeviceEvent> deviceParams(const std::vector<LiveClips::Track::ParamLane>& lanes, double bpm, int rate)
      {
      std::vector<DeviceEvent> out;
      for (int l = 0; l < int(lanes.size()); ++l) {
            for (const auto& e : lanes[size_t(l)].events) {
                  const double seconds = double(e.first) / LiveClips::UNITS_PER_BEAT * 60.0 / bpm;
                  const double ms = std::ceil(seconds * 1000.0 - 1e-9);
                  DeviceEvent d;
                  d.frame = qint64(std::llround(ms / 1000.0 * rate));
                  d.type = ME_PARAMETER;
                  d.a = l;
                  d.value = double(e.second);
                  out.push_back(d);
                  }
            }
      std::stable_sort(out.begin(), out.end(), [](const DeviceEvent& x, const DeviceEvent& y) { return x.frame < y.frame; });
      return out;
      }

std::vector<DeviceEvent> deviceMidi(const std::vector<LiveClips::Note>& notes, double bpm, int rate)
      {
      struct E { DeviceEvent e; int order; };
      std::vector<E> out;
      auto frameOf = [&](int units) {
            return qint64(std::llround(double(units) / LiveClips::UNITS_PER_BEAT * 60.0 / bpm * rate));
            };
      int order = 0;
      int lsb = 0, msb = 64;              // (the device's pack 224 0 64: the centre)
      std::vector<LiveClips::Note> sorted = notes;
      std::stable_sort(sorted.begin(), sorted.end());
      for (const LiveClips::Note& n : sorted) {
            DeviceEvent e;
            e.frame = frameOf(n.start);
            if (n.pitch >= LiveClips::CARRIER_LOW) {
                  if (n.velocity <= 0)
                        continue;
                  const int v = LiveClips::carrierValue(n.pitch, n.velocity);
                  if (n.pitch == LiveClips::BEND_MSB || n.pitch == LiveClips::BEND_LSB) {
                        (n.pitch == LiveClips::BEND_MSB ? msb : lsb) = v;
                        e.type = ME_PITCHBEND;
                        e.a = lsb;
                        e.b = msb;
                        }
                  else {
                        const int i = 127 - n.pitch;
                        if (i < 0 || i >= LiveClips::CARRIER_COUNT)
                              continue;
                        e.type = ME_CONTROLLER;
                        e.a = LiveClips::CARRIER_CCS[i];
                        e.b = v;
                        }
                  out.push_back({ e, 2 * order++ + 1 });
                  continue;
                  }
            if (n.muted)            // (a note muted in the clip: Live doesn't play it)
                  continue;
            e.type = ME_NOTEON;
            e.a = n.pitch;
            e.b = std::max(1, std::min(127, n.velocity));
            out.push_back({ e, 2 * order++ + 1 });
            DeviceEvent off = e;
            off.frame = frameOf(n.start + n.length);
            off.b = 0;
            out.push_back({ off, 0 });      // (note-offs first at their frame)
            }
      std::stable_sort(out.begin(), out.end(), [](const E& x, const E& y) {
            return x.e.frame != y.e.frame ? x.e.frame < y.e.frame : x.order < y.order;
            });
      std::vector<DeviceEvent> r;
      r.reserve(out.size());
      for (const E& x : out)
            r.push_back(x.e);
      return r;
      }

void liveMixer(double volume, double pan, bool active, float* left, float* right)
      {
      if (!active) {
            *left = *right = 0.f;
            return;
            }
      pan = std::max(-1.0, std::min(1.0, pan));
      const double angle = (pan + 1) * 3.14159265358979323846 / 4;
      *left = float(volume * std::sqrt(2.0) * std::cos(angle));
      *right = float(volume * std::sqrt(2.0) * std::sin(angle));
      if (pan <= -1.0)
            *right = 0.f;
      if (pan >= 1.0)
            *left = 0.f;
      }

//---------------------------------------------------------
//   analysis
//---------------------------------------------------------

static std::vector<float> mono(const std::vector<float>& stereo)
      {
      std::vector<float> m(stereo.size() / 2);
      for (size_t i = 0; i < m.size(); ++i)
            m[i] = 0.5f * (stereo[2 * i] + stereo[2 * i + 1]);
      return m;
      }

static double db(double energy, double n)
      {
      return n > 0 && energy > 1e-20 * n ? 10 * std::log10(energy / n) : -200;
      }

static double windowDb(const std::vector<float>& m, qint64 from, qint64 to)
      {
      from = std::max<qint64>(0, from);
      to = std::min<qint64>(qint64(m.size()), to);
      double e = 0;
      for (qint64 i = from; i < to; ++i)
            e += double(m[size_t(i)]) * m[size_t(i)];
      return db(e, double(to - from));
      }

// RMS over 5 ms every 1 ms, from..to
static std::vector<double> envelope(const std::vector<float>& m, qint64 from, qint64 to, int rate)
      {
      const qint64 hop = rate / 1000;
      const qint64 win = 5 * hop;
      std::vector<double> env;
      for (qint64 at = from; at < to; at += hop) {
            double e = 0;
            for (qint64 i = at - win / 2; i < at + win / 2; ++i)
                  if (i >= 0 && i < qint64(m.size()))
                        e += double(m[size_t(i)]) * m[size_t(i)];
            env.push_back(std::sqrt(e / double(win)));
            }
      return env;
      }

static double envelopeLagMs(const std::vector<float>& a, const std::vector<float>& b, qint64 at, int rate)
      {
      const qint64 ms = rate / 1000;
      const std::vector<double> ea = envelope(a, at - 30 * ms, at + 150 * ms, rate);
      const std::vector<double> eb = envelope(b, at - 60 * ms, at + 180 * ms, rate);       // (±30 ms around)
      double best = -1;
      int bestLag = 0;
      for (int lag = -30; lag <= 30; ++lag) {
            double dot = 0, na = 0, nb = 0;
            for (size_t i = 0; i < ea.size(); ++i) {
                  const int j = int(i) + 30 + lag;
                  if (j < 0 || j >= int(eb.size()))
                        continue;
                  dot += ea[i] * eb[size_t(j)];
                  na += ea[i] * ea[i];
                  nb += eb[size_t(j)] * eb[size_t(j)];
                  }
            const double c = na > 0 && nb > 0 ? dot / std::sqrt(na * nb) : 0;
            if (c > best + 1e-12 || (std::fabs(c - best) <= 1e-12 && std::abs(lag) < std::abs(bestLag))) {
                  best = c;
                  bestLag = lag;
                  }
            }
      return bestLag;
      }

static double median(std::vector<double> v)
      {
      if (v.empty())
            return 0;
      std::sort(v.begin(), v.end());
      return v.size() % 2 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
      }

//---------------------------------------------------------
//   compare
//---------------------------------------------------------

#ifdef USE_VST3
Result compare(MasterScore* score, const SoundLib::Library& library, const Options& options,
               std::function<void(const QString&)> log)
      {
      Result r;
      auto say = [&](const QString& s) { if (log) log(s); };
      const int rate = options.rate;
      r.rate = rate;
      if (!score) {
            r.error = "no score";
            return r;
            }
      r.score = score->title();
      QString error;
      const QString pluginPath = SoundLibraryHost::pluginPath(library, &error);
      if (pluginPath.isEmpty()) {
            r.error = error;
            return r;
            }
      r.plugin = QFileInfo(pluginPath).fileName();

      // the set as Create Live Set writes it
      LiveIntegration::LiveSetPlan plan;
      if (!LiveIntegration::planLiveSet(score, library, LiveIntegration::LiveSetKind::ROUTES, &plan, &error)) {
            r.error = error;
            return r;
            }
      r.setup = LiveIntegration::reportText(plan, "(compared, not written)", false).split("\n", QString::SkipEmptyParts);
      say(QString("the set: %1 track(s)").arg(plan.spec.tracks.size()));

      // the rendering both play (MuseScore's own, and the clips')
      EventMap events;
      score->renderMidi(&events, false, true, options.state);
      if (events.empty()) {
            r.error = "no events";
            return r;
            }
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, library);

      // MuseScore: its own instances (setup, script settled, Controllers), the export's mix, offline
      qint64 total = 0;
      std::map<int, std::vector<float>> msTrack, liveTrack;        // by slot (port × 16 + channel)
      {
            std::shared_ptr<Vst3Synth> vst = SoundLibraryExport::loadInstances(score, float(rate), &error);
            if (!vst) {
                  r.error = error;
                  return r;
                  }
            for (int k = 0; k < Vst3Synth::MAX_SLOTS; ++k)
                  vst->setExportMix(k, 100, 64, false);
            for (const SoundLib::Route& rt : routes) {
                  const SoundLib::PartMix m = SoundLib::partMix(rt.part, false);
                  vst->setExportMix(rt.port * 16 + rt.channel, m.volume, m.pan, m.muted);
                  }
            const qint64 last = qint64((score->utick2utime(events.rbegin()->first)) * rate);
            total = last + 3 * rate;
            r.museScore.assign(size_t(2 * total), 0.f);
            // one route at a time (the same instances; Vst3Synth sums its slots, so the mix is their sum): each
            // part's notes measured on its own, not through another part's
            std::set<int> routeSlots;
            for (const SoundLib::Route& rt : routes)
                  if (rt.instrument && !rt.instrument->kit)
                        routeSlots.insert(rt.port * 16 + rt.channel);
            for (int slot : routeSlots) {
                  std::vector<float>& out = msTrack[slot];
                  out.assign(size_t(2 * total), 0.f);
                  vst->beginExport(float(rate));
                  const unsigned FRAMES = 512;
                  qint64 pos = 0;
                  auto processTo = [&](qint64 to) {
                        while (pos < to) {
                              const unsigned n = unsigned(std::min<qint64>(FRAMES, to - pos));
                              vst->process(n, out.data() + 2 * pos, nullptr, nullptr);
                              pos += n;
                              }
                        };
                  for (const auto& te : events) {
                        const NPlayEvent& e = te.second;
                        if (!e.isExternal() || e.extPort() * 16 + e.extChannel() != slot)
                              continue;
                        processTo(std::min(total, qint64(score->utick2utime(te.first) * rate)));
                        PlayEvent pe(e);
                        pe.setChannel(slot);
                        vst->play(pe);
                        }
                  processTo(total);
                  vst->endExport();
                  for (size_t i = 0; i < out.size(); ++i)
                        r.museScore[i] += out[i];
                  }
      }
      say("MuseScore's render done");

      // Live: the clips as the device plays them, each track's plug-in from the set's state, Live's mixer
      const LiveClips::Timeline tl = LiveClips::timeline(score);
      const std::vector<LiveClips::Track> clips = LiveClips::tracks(score, library, events, LiveIntegration::outputPortNames(), tl);
      r.live.assign(r.museScore.size(), 0.f);
      const QStringList fault = qEnvironmentVariable("MS_LIVE_EQUIVALENCE_FAULT").split(",", QString::SkipEmptyParts);
      std::map<QString, QString> trackOfKey;
      for (const LiveSetWriter::Track& t : plan.spec.tracks) {
            trackOfKey[t.routeKey] = t.name;
            if (!t.hasPlugin)
                  continue;
            const LiveClips::Track* clip = nullptr;
            for (const LiveClips::Track& c : clips)
                  if (c.key == t.routeKey)
                        clip = &c;
            if (!clip)
                  continue;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, rate, 4096, &error);
            QByteArray state = p ? Vst3Plugin::joinState(p->name(), t.plugin.component, t.plugin.controller) : QByteArray();
            if (fault.contains("no-controllers"))
                  state = SoundLibraryHost::setupState(library, t.patch, pluginPath, &error);
            if (!p || !p->setState(state)) {
                  r.error = QString("%1: the set's state could not be loaded (%2)").arg(t.name, error);
                  return r;
                  }
            // Live runs the plug-in from the moment the set is open: its script long settled before play
            p->settle();
            if (!fault.contains("no-controllers"))
                  for (const LiveSetWriter::Plugin::Parameter& lp : t.plugin.parameters)
                        p->setParameter(unsigned(lp.id), lp.value);      // (Live may set its configured values again)
            p->settle(p->secondsSinceState() + 0.25);
            p->allNotesOff();
            p->setOffline(true);
            const int slot = t.port * 16 + t.channel - 1;
            std::vector<float>& out = liveTrack[slot];
            out.assign(r.museScore.size(), 0.f);
            float gl, gr;
            liveMixer(t.volume, t.pan, t.active, &gl, &gr);
            if (fault.contains("no-mixer"))
                  gl = gr = 1.f;
            std::vector<float> buf(size_t(2 * 512));
            qint64 pos = 0;
            auto processTo = [&](qint64 to) {
                  while (pos < to) {
                        const unsigned n = unsigned(std::min<qint64>(512, to - pos));
                        std::fill(buf.begin(), buf.begin() + 2 * n, 0.f);
                        p->process(int(n), buf.data());
                        for (unsigned i = 0; i < n; ++i) {
                              out[size_t(2 * (pos + i))] += gl * buf[2 * i];
                              out[size_t(2 * (pos + i) + 1)] += gr * buf[2 * i + 1];
                              }
                        pos += n;
                        }
                  };
            // the device's MIDI and its parameter lanes (live.remote~ on the track's plug-in), in time; at one frame the
            // parameters first
            std::vector<DeviceEvent> dev = deviceParams(clip->params, tl.bpm, rate);
            if (fault.contains("no-params"))
                  dev.clear();
            std::vector<long> laneIds;
            for (const LiveClips::Track::ParamLane& pl : clip->params)
                  laneIds.push_back(p->parameterId(pl.title));
            const std::vector<DeviceEvent> midi = deviceMidi(clip->notes, tl.bpm, rate);
            dev.insert(dev.end(), midi.begin(), midi.end());
            std::stable_sort(dev.begin(), dev.end(), [](const DeviceEvent& x, const DeviceEvent& y) { return x.frame < y.frame; });
            for (const DeviceEvent& e : dev) {
                  if (e.type == ME_PITCHBEND && fault.contains("no-bend"))
                        continue;
                  processTo(std::min(total, e.frame));
                  if (e.type == ME_PARAMETER) {
                        if (e.a < int(laneIds.size()) && laneIds[size_t(e.a)] >= 0)
                              p->queueParameter(unsigned(laneIds[size_t(e.a)]), e.value);
                        continue;
                        }
                  p->midi(e.type, 0, e.a, e.b);
                  }
            processTo(total);
            for (size_t i = 0; i < out.size(); ++i)
                  r.live[i] += out[i];
            say(QString("Live's render of %1 done").arg(t.name));
            }

      // note by note: each note-on MuseScore plays on a library route
      std::map<std::pair<int, int>, qint64> openAt;       // (route, pitch) -> frame, for the length
      struct On { qint64 frame; qint64 end; int pitch; int route; };
      std::vector<On> ons;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal() || e.librarySwitch() || (e.type() != ME_NOTEON && e.type() != ME_NOTEOFF))
                  continue;
            const int route = e.extPort() * 16 + e.extChannel();
            const qint64 f = qint64(score->utick2utime(te.first) * rate);
            if (e.type() == ME_NOTEON && e.velo() > 0 && !(e.note() && e.isMuted()))
                  ons.push_back({ f, -1, e.pitch(), route });
            else {
                  for (auto it = ons.rbegin(); it != ons.rend(); ++it)
                        if (it->route == route && it->pitch == e.pitch() && it->end < 0) {
                              it->end = f;
                              break;
                              }
                  }
            }
      // a note at the very start: Live can't put its carriers before 0, so it waits for them (n × EPSILON: the
      // controllers and switch at 0, ~0.26 ms each at 120 bpm), which a waveform comparison sees; its span (to its
      // end plus 0.5 s) is left out of the whole-render measures and its onset allowed that wait
      const double waitMs = 1000.0 * 14 * LiveClips::EPSILON / LiveClips::UNITS_PER_BEAT * 60.0 / tl.bpm;
      qint64 maskTo = 0;
      std::map<int, qint64> trackMask;                    // by slot: its own start notes' span
      for (const On& o : ons)
            if (o.frame < rate / 100) {
                  const qint64 to = (o.end > o.frame ? o.end : o.frame + rate) + rate / 2;
                  maskTo = std::max(maskTo, to);
                  trackMask[o.route] = std::max(trackMask[o.route], to);
                  }
      r.maskedSeconds = double(maskTo) / rate;
      // the whole render: mono, the best lag within ±2 ms
      const std::vector<float> a = mono(r.museScore);
      const std::vector<float> b = mono(r.live);
      const size_t from = size_t(std::min<qint64>(maskTo, qint64(a.size())));
      r.seconds = double(a.size()) / rate;
      double ea = 0, eb = 0;
      for (size_t i = from; i < a.size(); ++i) {
            ea += double(a[i]) * a[i];
            eb += double(b[i]) * b[i];
            }
      r.museScoreRmsDb = db(ea, double(a.size() - from));
      r.liveRmsDb = db(eb, double(b.size() - from));
      const int maxLag = 2 * rate / 1000;
      double best = -2;
      int bestLag = 0;
      for (int lag = -maxLag; lag <= maxLag; ++lag) {
            double dot = 0, na = 0, nb = 0;
            for (size_t i = from; i < a.size(); ++i) {
                  const qint64 j = qint64(i) + lag;
                  if (j < 0 || j >= qint64(b.size()))
                        continue;
                  dot += double(a[i]) * b[size_t(j)];
                  na += double(a[i]) * a[i];
                  nb += double(b[size_t(j)]) * b[size_t(j)];
                  }
            const double c = na > 0 && nb > 0 ? dot / std::sqrt(na * nb) : 0;
            if (c > best) {
                  best = c;
                  bestLag = lag;
                  }
            }
      r.correlation = best;
      r.lagMs = 1000.0 * bestLag / rate;
      double res = 0;
      for (size_t i = from; i < a.size(); ++i) {
            const qint64 j = qint64(i) + bestLag;
            const double d = (j >= 0 && j < qint64(b.size()) ? b[size_t(j)] : 0.f) - a[i];
            res += d * d;
            }
      r.residualDb = ea > 0 ? 10 * std::log10(std::max(res, 1e-30) / ea) : 0;

      std::map<int, std::pair<std::vector<float>, std::vector<float>>> trackMono;
      for (const auto& mt : msTrack)
            trackMono[mt.first] = { mono(mt.second), liveTrack.count(mt.first) ? mono(liveTrack[mt.first]) : std::vector<float>() };
      std::map<QString, std::vector<double>> dbs, lags;
      for (const On& o : ons) {
            NoteResult n;
            n.track = trackOfKey[QString("%1:%2").arg(o.route / 16).arg(o.route % 16 + 1)];
            n.time = double(o.frame) / rate;
            n.pitch = o.pitch;
            const qint64 len = std::min<qint64>(rate / 4, o.end > o.frame ? o.end - o.frame : rate / 4);
            const std::vector<float>& ta = trackMono[o.route].first;
            const std::vector<float>& tb = trackMono[o.route].second;
            if (ta.empty() || tb.empty())
                  continue;
            n.museScoreDb = windowDb(ta, o.frame, o.frame + len);
            n.liveDb = windowDb(tb, o.frame, o.frame + len);
            n.silent = n.museScoreDb < -80 && n.liveDb < -80;
            n.atStart = o.frame < trackMask[o.route];
            if (!n.silent) {
                  n.lagMs = envelopeLagMs(ta, tb, o.frame, rate);
                  dbs[n.track].push_back(std::fabs(n.liveDb - n.museScoreDb));
                  if (n.atStart) {
                        // (the notes that start at the very start wait; a later one in their span, overlapping
                        // one of them, is reported only: its envelope carries the first one's shift)
                        if (o.frame < rate / 100 && (n.lagMs < -1 || n.lagMs > waitMs + 1))
                              r.failures << QString("%1: a note at the start %2 ms off (a wait of up to %3 ms expected)")
                                            .arg(n.track).arg(n.lagMs, 0, 'f', 0).arg(waitMs, 0, 'f', 1);
                        }
                  else
                        lags[n.track].push_back(std::fabs(n.lagMs));
                  }
            r.notes.push_back(n);
            }
      for (const LiveSetWriter::Track& t : plan.spec.tracks) {
            TrackResult tr;
            tr.name = t.name;
            tr.patch = t.patch;
            tr.notes = int(dbs[t.name].size());
            tr.medianDb = median(dbs[t.name]);
            tr.maxDb = dbs[t.name].empty() ? 0 : *std::max_element(dbs[t.name].begin(), dbs[t.name].end());
            tr.medianLagMs = median(lags[t.name]);
            tr.maxLagMs = lags[t.name].empty() ? 0 : *std::max_element(lags[t.name].begin(), lags[t.name].end());
            r.tracks.push_back(tr);
            }

      // the verdict
      const Thresholds& th = options.thresholds;
      if (!th.roundRobins) {
            if (r.correlation < th.correlation)
                  r.failures << QString("correlation %1 < %2").arg(r.correlation, 0, 'f', 5).arg(th.correlation);
            if (r.residualDb > th.residualDb)
                  r.failures << QString("residual %1 dB > %2 dB").arg(r.residualDb, 0, 'f', 1).arg(th.residualDb);
            }
      for (const TrackResult& tr : r.tracks) {
            if (!tr.notes)
                  continue;
            const double maxDb = th.roundRobins ? th.noteMaxDb : th.noteDb;
            const double maxLag = th.roundRobins ? th.lagRoundRobinMs : th.lagMs;
            if (th.roundRobins && tr.medianDb > th.noteMedianDb)
                  r.failures << QString("%1: notes' median %2 dB > %3 dB").arg(tr.name).arg(tr.medianDb, 0, 'f', 2).arg(th.noteMedianDb);
            if (tr.maxDb > maxDb)
                  r.failures << QString("%1: a note %2 dB off > %3 dB").arg(tr.name).arg(tr.maxDb, 0, 'f', 2).arg(maxDb);
            if (tr.maxLagMs > maxLag)
                  r.failures << QString("%1: an onset %2 ms off > %3 ms").arg(tr.name).arg(tr.maxLagMs, 0, 'f', 0).arg(maxLag);
            }
      if (r.notes.empty())
            r.failures << "no notes on library routes";
      r.passed = r.failures.isEmpty();
      return r;
      }
#else
Result compare(MasterScore*, const SoundLib::Library&, const Options&, std::function<void(const QString&)>)
      {
      Result r;
      r.error = "This MuseScore was built without plug-in hosting.";
      return r;
      }
#endif

//---------------------------------------------------------
//   readBack
//---------------------------------------------------------

ReadBack readBack(const QString& alsPath, const QString& pluginPath)
      {
      ReadBack out;
      QFile f(alsPath);
      if (!f.open(QIODevice::ReadOnly)) {
            out.error = "cannot read " + alsPath;
            return out;
            }
      const QByteArray data = f.readAll();
      QString error;
      const QByteArray xml = data.startsWith("\x1f\x8b") ? LiveSet::gunzip(data, &error) : data;
      if (xml.isEmpty()) {
            out.error = error;
            return out;
            }
      struct Device { QString track; QByteArray component, controller; std::vector<ReadBackValue> params; };
      std::vector<Device> devices;
      QXmlStreamReader r(xml);
      QStringList path;
      QString track;
      ReadBackValue param;
      while (!r.atEnd()) {
            const QXmlStreamReader::TokenType t = r.readNext();
            if (t == QXmlStreamReader::EndElement) {
                  if (path.last() == "PluginFloatParameter" && !devices.empty() && param.id >= 0)
                        devices.back().params.push_back(param);
                  path.removeLast();
                  continue;
                  }
            if (t != QXmlStreamReader::StartElement)
                  continue;
            const QString name = r.name().toString();
            path << name;
            const QString value = r.attributes().value("Value").toString();
            const int n = path.size();
            if (name == "MidiTrack")
                  track.clear();
            else if (name == "EffectiveName" && n >= 2 && path[n - 2] == "Name" && path.contains("MidiTrack") && !path.contains("DeviceChain"))
                  track = value;
            else if (name == "PluginDevice") {
                  Device d;
                  d.track = track;
                  devices.push_back(d);
                  }
            else if (name == "PluginFloatParameter") {
                  param = ReadBackValue();
                  param.track = track;
                  }
            else if (!devices.empty() && path.contains("PluginDevice")) {
                  if (name == "ProcessorState") {
                        devices.back().component = QByteArray::fromHex(r.readElementText().simplified().replace(" ", "").toLatin1());
                        path.removeLast();
                        }
                  else if (name == "ControllerState") {
                        devices.back().controller = QByteArray::fromHex(r.readElementText().simplified().replace(" ", "").toLatin1());
                        path.removeLast();
                        }
                  else if (name == "ParameterName" && path.contains("PluginFloatParameter"))
                        param.title = value;
                  else if (name == "ParameterId" && path.contains("PluginFloatParameter"))
                        param.id = value.toLong();
                  else if (name == "Manual" && n >= 2 && path[n - 2] == "ParameterValue")
                        param.manual = value.toDouble();
                  }
            }
      if (r.hasError()) {
            out.error = r.errorString();
            return out;
            }
      out.devices = int(devices.size());
#ifdef USE_VST3
      for (const Device& d : devices) {
            if (d.params.empty())
                  continue;
            std::unique_ptr<Vst3Plugin> p = Vst3Plugin::load(pluginPath, 48000, 4096, &error);
            if (!p || !p->setState(Vst3Plugin::joinState(p->name(), d.component, d.controller))) {
                  out.error = QString("%1: the state could not be loaded into %2 %3").arg(d.track, pluginPath, error);
                  return out;
                  }
            p->settle();
            for (ReadBackValue v : d.params) {
                  v.pluginId = p->parameterId(v.title);
                  v.fromState = v.pluginId >= 0 ? p->parameter(unsigned(v.pluginId)) : -1;
                  out.values.push_back(v);
                  }
            }
#else
      Q_UNUSED(pluginPath);
      out.error = "This MuseScore was built without plug-in hosting.";
#endif
      return out;
      }

QString readBackText(const ReadBack& r)
      {
      QString s = QString("%1 plug-in device(s)\n").arg(r.devices);
      if (!r.error.isEmpty())
            s += "ERROR: " + r.error + "\n";
      for (const ReadBackValue& v : r.values)
            s += QString("%1: \"%2\" (Live's id %3, value %4 = %5/127): the plug-in's parameter %6 holds %7 = %8/127 after loading the "
                         "state%9\n")
                 .arg(v.track, v.title).arg(v.id).arg(v.manual, 0, 'f', 6).arg(v.manual * 127, 0, 'f', 2).arg(v.pluginId)
                 .arg(v.fromState, 0, 'f', 6).arg(v.fromState * 127, 0, 'f', 2)
                 .arg(std::fabs(v.fromState - v.manual) < 0.5 / 127 ? ": the same" : ": DIFFERENT");
      return s;
      }

//---------------------------------------------------------
//   report
//---------------------------------------------------------

QString reportText(const Result& r, const Thresholds& t)
      {
      QString s;
      s += QString("Live against MuseScore: %1 (plug-in %2, %3 Hz, %4 s)\n").arg(r.score, r.plugin).arg(r.rate).arg(r.seconds, 0, 'f', 1);
      if (!r.error.isEmpty())
            return s + "ERROR: " + r.error + "\n";
      s += QString("Verdict: %1%2\n").arg(r.passed ? "PASS" : "FAIL").arg(r.failures.isEmpty() ? QString() : ": " + r.failures.join("; "));
      s += QString("Thresholds: %1\n").arg(t.roundRobins
                                             ? QString("round robins (whole render reported only); notes: median ≤ %1 dB, each ≤ %2 dB, onsets ≤ %3 ms")
                                               .arg(t.noteMedianDb).arg(t.noteMaxDb).arg(t.lagRoundRobinMs)
                                             : QString("deterministic: correlation ≥ %1, residual ≤ %2 dB, each note ≤ %3 dB and ≤ %4 ms")
                                               .arg(t.correlation).arg(t.residualDb).arg(t.noteDb).arg(t.lagMs));
      s += QString("Whole render: correlation %1 at lag %2 ms, residual %3 dB; RMS MuseScore %4 dB, Live %5 dB%6\n")
           .arg(r.correlation, 0, 'f', 5).arg(r.lagMs, 0, 'f', 3).arg(r.residualDb, 0, 'f', 1)
           .arg(r.museScoreRmsDb, 0, 'f', 2).arg(r.liveRmsDb, 0, 'f', 2)
           .arg(r.maskedSeconds > 0 ? QString(" (from %1 s: the notes at the very start wait for their carriers in Live)")
                                      .arg(r.maskedSeconds, 0, 'f', 2) : QString());
      s += "Per track (|Live - MuseScore| over each note's first 250 ms; onset lag of the envelope):\n";
      for (const TrackResult& tr : r.tracks)
            s += QString("  %1 (%2): %3 notes, level median %4 dB, max %5 dB; onset median %6 ms, max %7 ms\n")
                 .arg(tr.name, tr.patch).arg(tr.notes).arg(tr.medianDb, 0, 'f', 2).arg(tr.maxDb, 0, 'f', 2)
                 .arg(tr.medianLagMs, 0, 'f', 0).arg(tr.maxLagMs, 0, 'f', 0);
      s += "\nThe set (as Create Live Set writes it):\n  " + r.setup.join("\n  ") + "\n";
      s += "\nNotes (time s, track, pitch, MuseScore dB, Live dB, lag ms):\n";
      for (const NoteResult& n : r.notes)
            s += QString("  %1\t%2\t%3\t%4\t%5\t%6%7\n").arg(n.time, 0, 'f', 3).arg(n.track).arg(n.pitch)
                 .arg(n.museScoreDb, 0, 'f', 2).arg(n.liveDb, 0, 'f', 2).arg(n.lagMs, 0, 'f', 0)
                 .arg(n.silent ? "\t(silent)" : n.atStart ? "\t(at the start)" : "");
      return s;
      }

static bool writeWav(const QString& path, const std::vector<float>& stereo, int rate)
      {
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly))
            return false;
      const quint32 bytes = quint32(stereo.size() * 4);
      auto u32 = [&](quint32 v) { char c[4]; for (int i = 0; i < 4; ++i) c[i] = char((v >> (8 * i)) & 0xff); f.write(c, 4); };
      auto u16 = [&](quint16 v) { char c[2] = { char(v & 0xff), char(v >> 8) }; f.write(c, 2); };
      f.write("RIFF", 4); u32(36 + bytes); f.write("WAVE", 4);
      f.write("fmt ", 4); u32(16); u16(3); u16(2); u32(quint32(rate)); u32(quint32(rate) * 8); u16(8); u16(32);
      f.write("data", 4); u32(bytes);
      f.write(reinterpret_cast<const char*>(stereo.data()), qint64(bytes));
      return true;
      }

bool write(const Result& r, const Thresholds& t, const QString& folder, bool wav, QString* error)
      {
      QDir().mkpath(folder);
      QFile txt(folder + "/report.txt");
      if (!txt.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (error)
                  *error = "cannot write " + txt.fileName();
            return false;
            }
      txt.write(reportText(r, t).toUtf8());
      QJsonObject js;
      js["score"] = r.score;
      js["plugin"] = r.plugin;
      js["error"] = r.error;
      js["passed"] = r.passed;
      js["failures"] = QJsonArray::fromStringList(r.failures);
      js["rate"] = r.rate;
      js["seconds"] = r.seconds;
      js["correlation"] = r.correlation;
      js["lagMs"] = r.lagMs;
      js["residualDb"] = r.residualDb;
      js["museScoreRmsDb"] = r.museScoreRmsDb;
      js["liveRmsDb"] = r.liveRmsDb;
      js["maskedSeconds"] = r.maskedSeconds;
      QJsonArray tracks;
      for (const TrackResult& tr : r.tracks)
            tracks.append(QJsonObject { { "name", tr.name }, { "patch", tr.patch }, { "notes", tr.notes }, { "medianDb", tr.medianDb },
                                        { "maxDb", tr.maxDb }, { "medianLagMs", tr.medianLagMs }, { "maxLagMs", tr.maxLagMs } });
      js["tracks"] = tracks;
      QJsonArray notes;
      for (const NoteResult& n : r.notes)
            notes.append(QJsonObject { { "time", n.time }, { "track", n.track }, { "pitch", n.pitch }, { "museScoreDb", n.museScoreDb },
                                       { "liveDb", n.liveDb }, { "lagMs", n.lagMs }, { "silent", n.silent } });
      js["notes"] = notes;
      js["setup"] = QJsonArray::fromStringList(r.setup);
      QFile jf(folder + "/report.json");
      if (jf.open(QIODevice::WriteOnly))
            jf.write(QJsonDocument(js).toJson());
      if (wav && !r.museScore.empty()) {
            writeWav(folder + "/museScore.wav", r.museScore, r.rate);
            writeWav(folder + "/live.wav", r.live, r.rate);
            }
      return true;
      }

}     // namespace LiveEquivalence
}     // namespace Ms
