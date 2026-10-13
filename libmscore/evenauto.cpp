//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "evenauto.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "chord.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "playbacksettings.h"
#include "rendermidi.h"
#include "score.h"
#include "soundlibrary.h"
#include "trackdelays.h"

namespace Ms {
namespace EvenAuto {

using EvenLevels::NoteInfo;

static int nextWorkId = 1;

bool active(const Score* score)
      {
      if (!score || !SoundLib::current() || SoundLib::output() != SoundLib::Output::PLUGIN)
            return false;
      return Playback::value("levels/evenLevels", score) > 0.5;
      }

//---------------------------------------------------------
//   Work: a window being measured
//---------------------------------------------------------

struct Session::Work {
      int id { 0 };
      int round { 0 };                  // the round in flight
      int route { -1 };
      const Part* part { nullptr };
      int partIndex { -1 };
      const SoundLib::LibInstrument* patch { nullptr };
      std::vector<const Note*> notes;   // rendered: run-in, measured, run-out
      std::vector<QString> contexts;
      std::vector<QString> keys;
      std::vector<bool> fixed;          // run-in / run-out: values held
      std::vector<double> windowMs;     // each note's heard window after its note-on (NoteInfo)
      std::vector<bool> newSlur;
      std::vector<double> values[3];    // round 0: as played now (0: none)
      std::vector<double> levels[3];
      std::vector<double> env[3];
      EvenLevels::Stats stats[3];
      bool measured[3] { false, false, false };
      EvenLevels::Passage fit[3];
      std::vector<double> ons;          // the measured notes' note-ons (seconds)
      QString text;                     // "<part> bars a-b"
      QString instance;                 // its plug-in instance (Job::instance)
      double audioSeconds { 0 };        // one round's
      };

Session::Session(MasterScore* score, EvenLevels::Cache* cache)
   : _score(score), _cache(cache)
      {
      }

Session::~Session()
      {
      }

//---------------------------------------------------------
//   render
//    the score as playback renders it (Seq's 10-measure chunks), each Performance legato note's context
//---------------------------------------------------------

bool Session::render(EventMap* events, std::vector<NoteInfo>* infos)
      {
      MidiRenderer midi(_score);
      midi.setMinChunkSize(10);
      midi.setEvenLevelsInfo(infos);
      MidiRenderer::Context ctx(_synthState);
      ctx.metronome = false;
      ctx.renderHarmony = true;
      midi.renderScore(events, ctx);
      return true;
      }

//---------------------------------------------------------
//   write
//    a value on the note and its linked notes (parts), no undo step
//---------------------------------------------------------

void Session::write(const Note* note, double db, const QString& context)
      {
      bool changed = false;
      for (ScoreElement* l : const_cast<Note*>(note)->linkList()) {
            if (!l->isNote())
                  continue;
            Note* n = toNote(l);
            if (std::fabs(n->evenLevel() - db) > 1e-9 || n->evenContext() != context) {
                  n->setEvenLevel(db);
                  n->setEvenContext(context);
                  changed = true;
                  }
            }
      if (changed) {
            _score->setEvenLevelsWritten(true);
            _score->setPlaylistDirty();
            }
      }

//---------------------------------------------------------
//   makeJob
//    the window's notes as the plug-in gets them (their note-on on the route nearest the one the renderer reported,
//    after the track delays: as the job's actualTimes) and the route's events in seconds
//---------------------------------------------------------

Job Session::makeJob(Work& w, int round, const EventMap& events)
      {
      Job j;
      j.id = w.id;
      j.round = round;
      j.partIndex = w.partIndex;
      j.patch = w.patch ? w.patch->name : QString();
      j.instance = QString("%1|%2").arg(w.partIndex).arg(j.patch);
      w.instance = j.instance;
      j.route = w.route;
      auto routeOf = [](const NPlayEvent& e) { return e.extPort() * 16 + e.extChannel(); };
      const size_t n = w.notes.size();
      EvenLevels::Passage& f = w.fit[round];
      f = EvenLevels::Passage();
      f.on.assign(n, 0.0);
      f.off.assign(n, 0.0);
      f.window.assign(n, 0.0);
      f.slur.assign(n, 0);
      // the renderer's own report of each note (its note-on before the track delays, its heard window)
      std::map<const Note*, std::vector<EventMap::const_iterator>> ons;
      for (auto i = events.cbegin(); i != events.cend(); ++i) {
            const NPlayEvent& e = i->second;
            if (e.isExternal() && e.type() == ME_NOTEON && e.velo() > 0 && !e.librarySwitch() && e.note() && routeOf(e) == w.route)
                  ons[e.note()].push_back(i);
            }
      for (size_t k = 0; k < n; ++k) {
            auto o = ons.find(w.notes[k]);
            if (o == ons.end() || o->second.empty())
                  continue;
            // (a passage has each note once: the first)
            EventMap::const_iterator at = o->second.front();
            f.on[k] = _score->utick2utime(at->first);
            f.off[k] = f.on[k];
            const int pitch = at->second.pitch();
            for (auto i = std::next(at); i != events.cend(); ++i) {
                  const NPlayEvent& e = i->second;
                  if (!e.isExternal() || e.librarySwitch() || routeOf(e) != w.route || e.pitch() != pitch)
                        continue;
                  if (e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0)) {
                        f.off[k] = _score->utick2utime(i->first);
                        break;
                        }
                  }
            }
      // windows and slurs from the renderer's report (evenInfo: windowMs, newSlur), kept from detect()
      for (size_t k = 0; k < n; ++k)
            f.window[k] = f.on[k] + w.windowMs[k] / 1000.0;
      int slur = 0;
      for (size_t k = 0; k < n; ++k) {
            if (k > 0 && w.newSlur[k])
                  ++slur;
            f.slur[k] = slur;
            }
      j.fit = f;
      j.from = std::max(0.0, f.on.front() - EvenLevels::LEAD_S);
      j.to = f.on.back() + 0.001;
      const double end = *std::max_element(f.off.begin(), f.off.end());
      std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
      j.gap = std::max(0.0, end - j.to) + (lib ? EvenLevels::gapFor(*lib, w.patch) : EvenLevels::GAP_S);
      j.fixed = w.fixed;
      j.earlyStopDb = lib ? EvenLevels::earlyStopFor(*lib, w.patch) : 0.0;
      j.firstOn = 1e9;
      j.lastOn = -1e9;
      for (size_t k = 0; k < n; ++k) {
            if (!w.fixed[k]) {
                  j.firstOn = std::min(j.firstOn, f.on[k]);
                  j.lastOn = std::max(j.lastOn, f.on[k]);
                  }
            }
      for (auto i = events.cbegin(); i != events.cend(); ++i) {
            const NPlayEvent& e = i->second;
            if (!e.isExternal() || routeOf(e) != w.route)
                  continue;
            const double t = _score->utick2utime(i->first);
            if (t >= j.to + j.gap)
                  break;
            NPlayEvent p(e);
            p.setChannel(0);
            j.times.push_back(t);
            j.events.push_back(p);
            }
      // the slot's export mix (SoundLibraryExport::setExportMix)
      if (std::shared_ptr<const SoundLib::Library> library = SoundLib::current()) {
            const SoundLib::PartMix m = SoundLib::partMix(w.part, false);
            j.volume = m.volume;
            j.pan = m.pan;
            j.gain = float(TrackDelays::patchGain(*library, TrackDelays::of(w.part, TrackDelays::read(_score)), j.patch));
            }
      w.audioSeconds = j.audioSeconds();
      if (round == 0) {
            w.ons.clear();
            for (size_t k = 0; k < n; ++k)
                  if (!w.fixed[k])
                        w.ons.push_back(f.on[k]);
            }
      return j;
      }

//---------------------------------------------------------
//   detect
//---------------------------------------------------------

std::vector<Job> Session::detect()
      {
      std::vector<Job> jobs;
      _edited = false;
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!_score || !library || !active(_score)) {
            _works.clear();
            _held.clear();
            return jobs;
            }
      EventMap events;
      std::vector<NoteInfo> infos;
      render(&events, &infos);
      std::map<const Note*, int> played;
      for (const NoteInfo& in : infos)
            ++played[in.note];
      std::map<int, std::vector<NoteInfo>> byRoute;
      for (const NoteInfo& in : infos)
            byRoute[in.route].push_back(in);
      for (auto& br : byRoute)
            std::stable_sort(br.second.begin(), br.second.end(), [](const NoteInfo& a, const NoteInfo& b) { return a.on < b.on; });

      // the Works going on: kept when their notes are all there, in order, with the same contexts
      std::set<const Note*> covered;
      for (auto i = _works.begin(); i != _works.end();) {
            Work& w = *i->second;
            bool same = false;
            auto br = byRoute.find(w.route);
            if (br != byRoute.end()) {
                  const std::vector<NoteInfo>& ns = br->second;
                  for (size_t s = 0; s < ns.size() && !same; ++s) {
                        if (ns[s].note != w.notes.front())
                              continue;
                        same = s + w.notes.size() <= ns.size();
                        for (size_t k = 0; same && k < w.notes.size(); ++k)
                              same = ns[s + k].note == w.notes[k] && ns[s + k].context == w.contexts[k] && played[w.notes[k]] == 1;
                        }
                  }
            if (!same) {
                  i = _works.erase(i);
                  continue;
                  }
            for (const Note* n : w.notes)
                  covered.insert(n);
            ++i;
            }
      // results that came after the edit, for the Works kept
      std::vector<Result> held;
      held.swap(_held);
      for (const Result& r : held) {
            Job next;
            if (process(r, &next))
                  jobs.push_back(next);
            }

      // the passages, the notes that need a value: the cache's first
      std::map<int, SoundLib::Route> routeInfo;
      for (const SoundLib::Route& r : SoundLib::routes(_score, *library))
            routeInfo[r.port * 16 + r.channel] = r;
      struct Plan {
            int route;
            std::vector<NoteInfo> notes;
            std::vector<QString> contexts;
            std::vector<bool> needs;
            };
      std::vector<Plan> plans;
      int wrote = 0;
      for (auto& br : byRoute) {
            const std::vector<NoteInfo>& ns = br.second;
            for (size_t s = 0; s < ns.size();) {
                  size_t e = s;
                  while (e + 1 < ns.size() && ns[e + 1].joined)
                        ++e;
                  bool twice = false;
                  for (size_t k = s; k <= e; ++k)
                        twice = twice || played[ns[k].note] > 1;
                  if (e - s + 1 >= 3 && !twice) {
                        Plan p;
                        p.route = br.first;
                        p.notes.assign(ns.begin() + long(s), ns.begin() + long(e) + 1);
                        for (const NoteInfo& in : p.notes)
                              p.contexts.push_back(in.context);
                        p.needs.assign(p.notes.size(), false);
                        bool any = false;
                        for (size_t j = 0; j < p.notes.size(); ++j) {
                              const Note* note = p.notes[j].note;
                              if (covered.count(note))
                                    continue;
                              const QString key = EvenLevels::cacheKey(p.contexts, j);
                              const bool current = EvenLevels::isSet(note->evenLevel()) && note->evenContext() == p.contexts[j];
                              auto was = _keyOf.find(note);
                              // a current value measured in the neighbourhood it has now (or not known otherwise: as
                              // loaded, applied by the job): it stays, the cache learns it
                              if (current && (was == _keyOf.end() || was->second == key)) {
                                    if (_cache)
                                          _cache->put(key, note->evenLevel());
                                    _keyOf[note] = key;
                                    continue;
                                    }
                              // stale, none, or current but measured among other neighbours (an undo brought these
                              // back): the cache's value for the neighbourhood now, else measured
                              double db = 0;
                              if (_cache && _cache->get(key, &db)) {
                                    if (!current || std::fabs(note->evenLevel() - db) > 1e-9)
                                          ++wrote;
                                    write(note, db, p.contexts[j]);
                                    _keyOf[note] = key;
                                    continue;
                                    }
                              // (a current note whose neighbourhood changed, not in the cache: it is within +-3 of the
                              // note whose context changed, and measured with it)
                              if (current)
                                    continue;
                              p.needs[j] = true;
                              any = true;
                              }
                        if (any)
                              plans.push_back(p);
                        }
                  s = e + 1;
                  }
            }
      _fromCache += wrote;
      if (_log && wrote)
            _log(QString("even levels: %1 note(s) from the cache").arg(wrote));
      if (plans.empty())
            return jobs;
      if (wrote) {
            // (the cached values play in the windows' renders)
            events.clear();
            render(&events, nullptr);
            }

      // the windows
      for (const Plan& p : plans) {
            auto ri = routeInfo.find(p.route);
            if (ri == routeInfo.end() || !ri->second.part || !ri->second.instrument)
                  continue;
            const SoundLib::Route& route = ri->second;
            const int runIn = EvenLevels::runInFor(*library, route.instrument);
            for (const EvenLevels::Window& win : EvenLevels::windows(int(p.notes.size()), p.needs, runIn)) {
                  std::unique_ptr<Work> w(new Work);
                  w->id = nextWorkId++;          // (unique across sessions: a late result of another score never matches)
                  w->route = p.route;
                  w->part = route.part;
                  w->partIndex = _score->parts().indexOf(const_cast<Part*>(route.part));
                  w->patch = route.instrument;
                  for (int k = win.renderFirst; k <= win.renderLast; ++k) {
                        const NoteInfo& in = p.notes[size_t(k)];
                        const Note* note = in.note;
                        w->notes.push_back(note);
                        w->contexts.push_back(in.context);
                        w->keys.push_back(EvenLevels::cacheKey(p.contexts, size_t(k)));
                        w->fixed.push_back(k < win.measureFirst || k > win.measureLast);
                        w->windowMs.push_back(in.windowMs);
                        w->newSlur.push_back(in.newSlur);
                        const bool current = EvenLevels::isSet(note->evenLevel()) && note->evenContext() == in.context;
                        w->values[0].push_back(current ? note->evenLevel() : 0.0);
                        }
                  const Measure* m0 = w->notes[size_t(win.measureFirst - win.renderFirst)]->chord()->measure();
                  const Measure* m1 = w->notes[size_t(win.measureLast - win.renderFirst)]->chord()->measure();
                  w->text = QString("%1, bars %2-%3").arg(route.part->partName()).arg(m0 ? m0->no() + 1 : 0).arg(m1 ? m1->no() + 1 : 0);
                  jobs.push_back(makeJob(*w, 0, events));
                  if (_log)
                        _log(QString("even levels: measuring %1 (%2 notes, %3 rendered)").arg(w->text)
                             .arg(win.measureLast - win.measureFirst + 1).arg(w->notes.size()));
                  _works[w->id] = std::move(w);
                  }
            }
      return jobs;
      }

//---------------------------------------------------------
//   result
//---------------------------------------------------------

bool Session::result(const Result& r, Job* next)
      {
      if (_edited) {
            _held.push_back(r);
            return false;
            }
      return process(r, next);
      }

bool Session::process(const Result& r, Job* next)
      {
      auto it = _works.find(r.id);
      if (it == _works.end())
            return false;
      Work& w = *it->second;
      if (r.round != w.round)
            return false;
      const size_t n = w.notes.size();
      if (!r.ok || r.levels.size() != n) {
            if (_log)
                  _log(QString("even levels: %1, round %2 failed: %3").arg(w.text).arg(r.round).arg(r.error));
            _works.erase(it);
            return false;
            }
      const int round = r.round;
      w.levels[round] = r.levels;
      w.env[round] = r.env;
      w.stats[round] = EvenLevels::slurStats(w.fit[round].slur, r.levels);
      w.measured[round] = true;
      int nextRound = -1;
      if (round == 0 || (round == 1 && w.stats[1].residual > EvenLevels::FLOOR_DB)) {
            // the fit, divided by CC11's measured factor; run-in / run-out held
            const std::vector<double> du = EvenLevels::fitGlide(w.fit[round], w.env[round], w.levels[round], w.fixed);
            w.values[round + 1] = w.values[round];
            for (size_t j = 0; j < n && j < du.size(); ++j)
                  if (!w.fixed[j])
                        w.values[round + 1][j] = EvenLevels::clampDb(w.values[round][j] + du[j] / EvenLevels::CC11_FACTOR);
            nextRound = round + 1;
            }
      if (_log)
            _log(QString("even levels: %1, round %2: median slur range %3 dB, residual %4 dB").arg(w.text).arg(round)
                 .arg(w.stats[round].rangeMedian, 0, 'f', 2).arg(w.stats[round].residual, 0, 'f', 2));
      if (nextRound < 0) {
            finish(w);
            _works.erase(it);
            return false;
            }
      // the next round's events: the window's notes with its values (as they would play), then put back
      struct Was { Note* note; double db; QString context; };
      std::vector<Was> was;
      for (size_t j = 0; j < n; ++j) {
            Note* note = const_cast<Note*>(w.notes[j]);
            was.push_back({ note, note->evenLevel(), note->evenContext() });
            note->setEvenLevel(w.values[nextRound][j]);
            note->setEvenContext(w.contexts[j]);
            }
      EventMap events;
      render(&events, nullptr);
      for (const Was& x : was) {
            x.note->setEvenLevel(x.db);
            x.note->setEvenContext(x.context);
            }
      w.round = nextRound;
      *next = makeJob(w, nextRound, events);
      return true;
      }

//---------------------------------------------------------
//   finish
//    the round with the smallest median slur range: its values on the measured notes (0 dB where it is round 0 and
//    the note had none)
//---------------------------------------------------------

void Session::finish(Work& w)
      {
      int chosen = 0;
      for (int r = 1; r < 3; ++r)
            if (w.measured[r] && w.stats[r].rangeMedian < w.stats[chosen].rangeMedian - 1e-9)
                  chosen = r;
      int count = 0;
      for (size_t j = 0; j < w.notes.size(); ++j) {
            if (w.fixed[j])
                  continue;
            double db = EvenLevels::clampDb(w.values[chosen][j]);
            if (!EvenLevels::isSet(db))
                  db = 0.0;
            write(w.notes[j], db, w.contexts[j]);
            if (_cache)
                  _cache->put(w.keys[j], db);
            _keyOf[w.notes[j]] = w.keys[j];
            ++count;
            }
      _measured += count;
      if (_log)
            _log(QString("even levels: %1 done: round %2 kept (median slur range %3 -> %4 dB)").arg(w.text).arg(chosen)
                 .arg(w.stats[0].rangeMedian, 0, 'f', 2).arg(w.stats[chosen].rangeMedian, 0, 'f', 2));
      }

//---------------------------------------------------------
//   pending
//---------------------------------------------------------

std::vector<int> Session::workIds() const
      {
      std::vector<int> out;
      for (const auto& w : _works)
            out.push_back(w.first);
      return out;
      }

std::vector<double> Session::pendingOns() const
      {
      std::vector<double> out;
      for (const auto& w : _works)
            out.insert(out.end(), w.second->ons.begin(), w.second->ons.end());
      return out;
      }

QStringList Session::pendingText(double from, double to) const
      {
      QStringList out;
      for (const auto& w : _works)
            if (EvenLevels::mustWait(w.second->ons, from, to))
                  out << w.second->text;
      return out;
      }

double Session::pendingAudioSeconds(double from, double to) const
      {
      std::map<QString, double> by;
      for (const auto& w : _works)
            if (EvenLevels::mustWait(w.second->ons, from, to))
                  by[w.second->instance] += w.second->audioSeconds * (3 - w.second->round);
      double s = 0;
      for (const auto& b : by)
            s = std::max(s, b.second);
      return s;
      }

std::vector<QString> Session::pendingInstances(double from, double to) const
      {
      std::set<QString> by;
      for (const auto& w : _works)
            if (EvenLevels::mustWait(w.second->ons, from, to))
                  by.insert(w.second->instance);
      return std::vector<QString>(by.begin(), by.end());
      }

//---------------------------------------------------------
//   priority / next
//---------------------------------------------------------

double priority(const Job& job, double focus)
      {
      static const double BEFORE = 1e7;   // (after every window from the focus on: longer than any score)
      if (job.lastOn >= focus - 1e-9)
            return std::max(0.0, job.firstOn - focus);
      return BEFORE + (focus - job.lastOn);
      }

int next(const std::vector<Job>& queue, double focus)
      {
      int best = -1;
      double rank = 0;
      for (size_t i = 0; i < queue.size(); ++i) {
            const double r = priority(queue[i], focus);
            if (best < 0 || r < rank - 1e-12) {
                  best = int(i);
                  rank = r;
                  }
            }
      return best;
      }

}     // namespace EvenAuto
}     // namespace Ms
