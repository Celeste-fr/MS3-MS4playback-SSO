//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playbackaudit.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <memory>
#include <tuple>

#include "audio/midi/event.h"
#include "chord.h"
#include "dynamic.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "playbacksettings.h"
#include "repeatlist.h"
#include "score.h"
#include "segment.h"
#include "slur.h"
#include "staff.h"
#include "synthesizerstate.h"

namespace Ms {
namespace PlaybackAudit {

const char* kindName(Kind k)
      {
      switch (k) {
            case Kind::OVERLAP:    return "OVERLAP";
            case Kind::UNMEASURED: return "UNMEASURED";
            case Kind::EARLY_LONG: return "EARLY > NOTE";
            case Kind::TIMING:     return "TIMING";
            case Kind::LEVEL_STEP: return "LEVEL STEP";
            }
      return "";
      }

const char* severityName(Severity s)
      {
      switch (s) {
            case Severity::FAIL: return "fail";
            case Severity::WARN: return "warn";
            case Severity::INFO: return "info";
            }
      return "";
      }

// Rasch 1979 (Acustica 43, "Synchronization in performed ensemble music"): 30-50 ms between the players of an
// ensemble is normal playing; applied here to one line's neighbouring notes: a step beyond the upper end fails, between the
// two it is at the edge
static const double TIMING_WARN_MS = 30.0;
static const double TIMING_FAIL_MS = 50.0;

//---------------------------------------------------------
//   measuredFrom
//---------------------------------------------------------

static QStringList jsonKeys(const QString& file)
      {
      QFile f(file);
      if (!f.open(QIODevice::ReadOnly))
            return QStringList();
      return QJsonDocument::fromJson(f.readAll()).object().keys();
      }

Measured measuredFrom(const QString& folder)
      {
      Measured m;
      // (gen_spitfire_sso.py: ONSET_FITS and RACHM_LEVELS)
      const std::vector<std::tuple<const char*, const char*, bool>> files = {
            { "sso_long_onset_fit.json", "Long", false },
            { "sso_rachm_onset_fit.json", "Long (Rachm.)", false },
            { "sso_rachm_levels.json", "Long (Rachm.)", true },
            };
      for (const auto& f : files) {
            const QString path = folder + "/" + std::get<0>(f);
            if (!QFile::exists(path))
                  continue;
            m.files << std::get<0>(f);
            for (const QString& k : jsonKeys(path))
                  (std::get<2>(f) ? m.levelFits : m.onsetFits)[std::get<1>(f)].insert(k);
            }
      return m;
      }

//---------------------------------------------------------
//   render
//---------------------------------------------------------

void render(MasterScore* score, EventMap* events, std::vector<MidiRenderer::LibTrace>* trace)
      {
      score->setExpandRepeats(true);
      MidiRenderer midi(score);
      midi.setMinChunkSize(10);           // (Seq's)
      midi.setLibraryTrace(trace);
      SynthesizerState ss;
      MidiRenderer::Context ctx { ss };
      ctx.renderHarmony = true;
      for (MidiRenderer::Chunk chunk = midi.getChunkAt(0); chunk; chunk = midi.getChunkAt(chunk.utick2())) {
            EventMap part;
            midi.renderChunk(chunk, &part, ctx);
            events->insert(part.begin(), part.end());
            }
      }

//---------------------------------------------------------
//   Report
//---------------------------------------------------------

int Report::count(Kind k, Severity s) const
      {
      return int(std::count_if(findings.begin(), findings.end(), [&](const Finding& f) { return f.kind == k && f.severity == s; }));
      }

int Report::count(Kind k) const
      {
      return int(std::count_if(findings.begin(), findings.end(), [&](const Finding& f) { return f.kind == k; }));
      }

QString Report::text(const QString& title) const
      {
      QStringList out;
      if (!title.isEmpty())
            out << title;
      out << QString("%1 library notes; [legato] keepMs %2; timing: Rasch 1979, warn > %3 ms, fail > %4 ms; level steps: "
                     "values only (no sourced threshold)").arg(notes).arg(keepMs).arg(TIMING_WARN_MS).arg(TIMING_FAIL_MS);
      for (Kind k : { Kind::OVERLAP, Kind::UNMEASURED, Kind::EARLY_LONG, Kind::TIMING, Kind::LEVEL_STEP })
            out << QString("  %1: %2 fail, %3 warn, %4 info").arg(kindName(k), -12).arg(count(k, Severity::FAIL))
                   .arg(count(k, Severity::WARN)).arg(count(k, Severity::INFO));
      std::vector<const Finding*> sorted;
      for (const Finding& f : findings)
            sorted.push_back(&f);
      std::stable_sort(sorted.begin(), sorted.end(), [](const Finding* a, const Finding* b) {
            if (a->kind != b->kind)
                  return a->kind < b->kind;
            if (a->part != b->part)
                  return a->part < b->part;
            return a->seconds < b->seconds;
            });
      Kind last = Kind(-1);
      for (const Finding* f : sorted) {
            if (f->kind != last) {
                  out << "" << QString("== %1").arg(kindName(f->kind));
                  last = f->kind;
                  }
            if (f->bar > 0)
                  out << QString("%1 %2 bar %3 beat %4 pitch %5 %6: %7").arg(severityName(f->severity), f->part).arg(f->bar)
                         .arg(f->beat, 0, 'f', 2).arg(f->pitch).arg(f->technique, f->text);
            else
                  out << QString("%1 %2 %3: %4").arg(severityName(f->severity), f->part, f->technique, f->text);
            }
      return out.join("\n") + "\n";
      }

//---------------------------------------------------------
//   audit
//---------------------------------------------------------

namespace {

// a library note as played: one note-on and its note-off on a route
struct Played {
      int route;                    // port * 16 + channel
      int pitch;
      int on;                       // uticks
      int off { -1 };
      int velocity;
      int cc[128];                  // the route's controllers in force at the note-on (-1: none sent)
      const Note* note;
      const MidiRenderer::LibTrace* trace { nullptr };
      int written { -1 };           // utick
      };

struct Group {                      // a chord's notes attacked together on a line (track)
      int written;
      int writtenEnd;               // utick its longest tie chain ends
      std::vector<Played*> notes;
      };

QString techniqueOf(const Played& p)
      {
      if (!p.trace || !p.trace->choice)
            return "-";
      return QString("%1 / %2").arg(p.trace->patch->name, p.trace->choice.articulation->name);
      }

}     // namespace

Report audit(const Score* score, const EventMap& events, const std::vector<MidiRenderer::LibTrace>& trace,
             const Measured& measured)
      {
      Report report;
      report.keepMs = Playback::value("legato/keepMs", score);
      const double keepMs = report.keepMs;
      auto ms = [score](int utick) { return score->utick2utime(utick) * 1000.0; };

      std::map<const Note*, std::vector<const MidiRenderer::LibTrace*>> traces;
      for (const MidiRenderer::LibTrace& t : trace)
            traces[t.note].push_back(&t);

      auto finding = [&](Kind k, Severity s, const Played* p, const QString& text, double value) {
            Finding f { k, s, QString(), 0, 1, -1, QString(), value, text, 0 };
            if (p && p->note) {
                  const Note* n = p->note;
                  const Measure* m = n->chord()->measure();
                  f.part = n->part()->partName();
                  f.bar = m->no() + 1;
                  const int beatTicks = DIVISION * 4 / std::max(1, m->timesig().denominator());
                  f.beat = 1 + double(n->chord()->tick().ticks() - m->tick().ticks()) / beatTicks;
                  f.pitch = p->pitch;
                  f.technique = techniqueOf(*p);
                  f.seconds = p->written >= 0 ? ms(p->written) / 1000 : ms(p->on) / 1000;
                  }
            report.findings.push_back(f);
            };

      // the notes as played, route by route; a key struck while it still sounds on its route
      std::vector<std::unique_ptr<Played>> played;
      std::map<int, std::map<int, std::vector<Played*>>> sounding;      // route -> key -> notes on
      std::map<int, std::vector<int>> controllers;                      // route -> value per controller
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal())
                  continue;
            const int route = e.extPort() * 16 + e.extChannel();
            std::vector<int>& cc = controllers[route];
            if (cc.empty())
                  cc.assign(128, -1);
            if (e.type() == ME_CONTROLLER && e.controller() >= 0 && e.controller() < 128)
                  cc[size_t(e.controller())] = e.value();
            const bool on = e.type() == ME_NOTEON && e.velo() > 0;
            const bool off = (e.type() == ME_NOTEON && e.velo() == 0) || e.type() == ME_NOTEOFF;
            if (e.librarySwitch() || (!on && !off))
                  continue;
            std::vector<Played*>& keys = sounding[route][e.pitch()];
            if (off) {
                  auto it = std::find_if(keys.begin(), keys.end(), [&](const Played* p) { return e.note() && p->note == e.note(); });
                  if (it == keys.end() && !keys.empty())
                        it = keys.begin();
                  if (it != keys.end()) {
                        (*it)->off = te.first;
                        keys.erase(it);
                        }
                  continue;
                  }
            std::unique_ptr<Played> p(new Played);
            p->route = route;
            p->pitch = e.pitch();
            p->on = te.first;
            p->velocity = e.velo();
            std::copy(cc.begin(), cc.end(), p->cc);
            p->note = e.note();
            if (p->note) {
                  auto tr = traces.find(p->note);
                  if (tr != traces.end()) {
                        for (const MidiRenderer::LibTrace* t : tr->second)
                              if (!p->trace || std::abs(t->utick - p->on) < std::abs(p->trace->utick - p->on))
                                    p->trace = t;
                        p->written = p->trace->utick;
                        }
                  }
            ++report.notes;
            if (!keys.empty()) {
                  const Played* before = keys.front();
                  finding(Kind::OVERLAP, Severity::FAIL, p.get(),
                          QString("key struck again while it sounds on its route (%1-%2): on since %3 s, again at %4 s")
                          .arg(route / 16).arg(route % 16 + 1).arg(ms(before->on) / 1000, 0, 'f', 3).arg(ms(p->on) / 1000, 0, 'f', 3),
                          ms(p->on) - ms(before->on));
                  }
            keys.push_back(p.get());
            played.push_back(std::move(p));
            }

      // the lines: each track's chords as attacked (grace notes and notes without a choice left out)
      std::map<int, std::map<int, Group>> lines;          // track -> written utick -> group
      for (const auto& up : played) {
            Played* p = up.get();
            if (!p->trace || !p->note || p->note->chord()->isGrace())
                  continue;
            Group& g = lines[p->note->track()][p->written];
            const Note* last = p->note->lastTiedNote();
            const int end = p->written + (last->chord()->tick() + last->chord()->actualTicks() - p->note->chord()->tick()).ticks();
            if (g.notes.empty())
                  g.written = p->written, g.writtenEnd = end;
            g.writtenEnd = std::max(g.writtenEnd, end);
            g.notes.push_back(p);
            }

      // (a) a note sounding into the next attack of its line on its route, more than keepMs
      for (auto& line : lines) {
            std::vector<Group*> gs;
            for (auto& g : line.second)
                  gs.push_back(&g.second);
            for (size_t i = 0; i < gs.size(); ++i) {
                  for (Played* p : gs[i]->notes) {
                        const Note* last = p->note->lastTiedNote();
                        const int end = p->written + (last->chord()->tick() + last->chord()->actualTicks() - p->note->chord()->tick()).ticks();
                        auto next = std::find_if(gs.begin() + long(i) + 1, gs.end(), [&](const Group* g) { return g->written >= end; });
                        if (next == gs.end() || p->off < 0)
                              continue;
                        for (Played* q : (*next)->notes) {
                              if (q->route != p->route)
                                    continue;
                              const double overlap = ms(p->off) - ms(q->on);
                              if (overlap > keepMs + 0.5)
                                    finding(Kind::OVERLAP, Severity::FAIL, p,
                                            QString("sounds %1 ms into the next note of its line (pitch %2, on at %3 s), [legato] keepMs %4")
                                            .arg(overlap, 0, 'f', 1).arg(q->pitch).arg(ms(q->on) / 1000, 0, 'f', 3).arg(keepMs), overlap);
                              }
                        }
                  }
            }

      auto fits = [](const std::map<QString, std::set<QString>>& m, const QString& a, const QString& i) {
            auto it = m.find(a);
            return it != m.end() && it->second.count(i);
            };
      auto onsetFitted = [&](const Played* p) {
            return fits(measured.onsetFits, p->trace->choice.articulation->name, p->trace->patch->name);
            };

      // (b) swapped or early without an in-context measurement, per instrument; (c) early by more than the note
      struct Missing { int notes = 0; double maxEarly = 0; std::set<int> bars; QString what; };
      std::map<std::pair<QString, QString>, Missing> missing;     // (instrument, articulation)
      for (const auto& up : played) {
            const Played* p = up.get();
            if (!p->trace || !p->note || p->written < 0)
                  continue;
            const double early = ms(p->written) - ms(p->on);
            const SoundLib::Choice& c = p->trace->choice;
            const QString inst = p->trace->patch->name;
            const QString art = c.articulation->name;
            const double noteMs = SoundLib::noteSeconds(p->note) * 1000;
            if (early > 0.5 && noteMs > 0 && early > noteMs)
                  finding(Kind::EARLY_LONG, Severity::WARN, p, QString("starts %1 ms early, its written length %2 ms")
                          .arg(early, 0, 'f', 1).arg(noteMs, 0, 'f', 1), early);
            if (!c.swapped && early <= 0.5)
                  continue;
            QStringList what;
            if (early > 0.5 && !fits(measured.onsetFits, art, inst))
                  what << "onset not fitted in context";
            if (c.swapped && (!fits(measured.levelFits, art, inst) || c.articulation->quickLevels.empty()))
                  what << (c.articulation->quickLevels.empty() ? "level: no quickLevel in the map" : "level not measured in context");
            if (what.isEmpty())
                  continue;
            Missing& mm = missing[{ inst, art }];
            ++mm.notes;
            mm.maxEarly = std::max(mm.maxEarly, early);
            mm.bars.insert(p->note->chord()->measure()->no() + 1);
            mm.what = (c.swapped ? QString("swapped ([slurs] quick); ") : QString()) + what.join(", ");
            }
      for (const auto& m : missing) {
            QStringList bars;
            for (int b : m.second.bars)
                  bars << QString::number(b);
            Finding f { Kind::UNMEASURED, Severity::INFO, m.first.first, 0, 1, -1, m.first.second, double(m.second.notes),
                        QString("%1 notes, %2; earliest start %3 ms; bars %4").arg(m.second.notes).arg(m.second.what)
                        .arg(m.second.maxEarly, 0, 'f', 1).arg(bars.join(" ")), 0 };
            report.findings.push_back(f);
            }

      // (d) a chord's start offset (its earliest note's played - written) against the note before it in its line,
      // joined without a rest: the step between the two shortens or lengthens the written interval by that much
      for (auto& line : lines) {
            std::vector<Group*> gs;
            for (auto& g : line.second)
                  gs.push_back(&g.second);
            auto offset = [&](const Group* g) {
                  double o = 1e9;
                  for (const Played* p : g->notes)
                        o = std::min(o, ms(p->on) - ms(p->written));
                  return o;
                  };
            for (size_t i = 1; i < gs.size(); ++i) {
                  if (gs[i - 1]->writtenEnd != gs[i]->written)
                        continue;
                  const double o = offset(gs[i]);
                  const double before = offset(gs[i - 1]);
                  const double d = o - before;
                  if (std::abs(d) <= TIMING_WARN_MS)
                        continue;
                  const Played* p = *std::min_element(gs[i]->notes.begin(), gs[i]->notes.end(),
                                                      [](const Played* a, const Played* b) { return a->on < b->on; });
                  const Played* q = *std::min_element(gs[i - 1]->notes.begin(), gs[i - 1]->notes.end(),
                                                      [](const Played* a, const Played* b) { return a->on < b->on; });
                  finding(Kind::TIMING, std::abs(d) > TIMING_FAIL_MS ? Severity::FAIL : Severity::WARN, p,
                          QString("%1 ms against the note before (starts %2 ms off its written time; the note before, "
                                  "pitch %3 %4, %5 ms)").arg(d, 0, 'f', 1).arg(o, 0, 'f', 1).arg(q->pitch)
                          .arg(techniqueOf(*q)).arg(before, 0, 'f', 1)
                          + QString("; onsets fitted in context: %1").arg(onsetFitted(p) ? (onsetFitted(q) ? "both" : "this only")
                                                                          : (onsetFitted(q) ? "the note before only" : "neither")), d);
                  }
            }

      // (e) the level of neighbouring notes under one slur at an unchanged written dynamic
      std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
      std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
      const int dynCC = lib ? lib->dynamicsCC : 1;
      auto law = [](int x) { return x > 0 ? 40.0 * std::log10(x / 127.0) : -200.0; };
      auto curveDb = [](const SoundLib::DynamicsCurve& c, int x) { return c.byEar() ? c.perceivedAt(x) : c.at(x); };
      // the note's level, dB, and how it was found
      auto level = [&](const Played* p, QString* how) {
            const SoundLib::Choice& c = p->trace->choice;
            const SoundLib::DynamicsCurve* own = cal ? cal->curve(p->trace->patch->name, c.articulation->value) : nullptr;
            const int x = p->cc[dynCC >= 0 && dynCC < 128 ? dynCC : 1];
            double db;
            const bool byVelocity = (own && own->drivenBy == "velocity") || (!own && dynCC < 0) || x < 0;
            if (own && own->points.size() >= 2 && own->drivenBy != "neither") {
                  db = curveDb(*own, byVelocity ? p->velocity : x);
                  *how = QString("%1 %2 by %3").arg(byVelocity ? "velocity" : "CC" + QString::number(dynCC))
                         .arg(byVelocity ? p->velocity : x).arg(own->byEar() ? "ear" : "50 ms level");
                  }
            else {
                  db = law(byVelocity ? p->velocity : x);
                  *how = QString("%1 %2 by the law").arg(byVelocity ? "velocity" : "CC" + QString::number(dynCC))
                         .arg(byVelocity ? p->velocity : x);
                  }
            const int e = p->cc[CTRL_EXPRESSION];
            if (e >= 0 && e < 127 && dynCC != CTRL_EXPRESSION) {
                  const SoundLib::DynamicsCurve* held = cal ? SoundLib::heldCurve(*cal, { p->trace->patch }) : nullptr;
                  SoundLib::DynamicsCurve ex;
                  if (held)
                        ex.points = held->expressionPerceived.size() >= 2 ? held->expressionPerceived : held->expression;
                  db += ex.points.size() >= 2 ? ex.at(e) - ex.at(127) : law(e);
                  *how += QString(", CC11 %1").arg(e);
                  }
            return db;
            };
      auto slurBoth = [&](const Note* a, const Note* b) {
            const int ta = a->chord()->tick().ticks();
            const int tb = b->chord()->tick().ticks();
            if (tb <= ta)
                  return false;
            for (const auto& iv : const_cast<Score*>(score)->spannerMap().findOverlapping(ta, tb)) {
                  const Spanner* sp = iv.value;
                  if (sp->isSlur() && !toSlur(sp)->phraseMark() && sp->track() == a->track()
                      && sp->tick().ticks() <= ta && sp->tick2().ticks() >= tb)
                        return true;
                  }
            return false;
            };
      auto dynamicChanges = [&](const Note* a, const Note* b) {
            const int ta = a->chord()->tick().ticks();
            const int tb = b->chord()->tick().ticks();
            const Part* part = a->part();
            for (const auto& iv : const_cast<Score*>(score)->spannerMap().findOverlapping(ta, tb)) {
                  const Spanner* sp = iv.value;
                  if (sp->isHairpin() && sp->staff() && sp->staff()->part() == part && sp->tick2().ticks() > ta && sp->tick().ticks() < tb)
                        return true;
                  }
            for (const Segment* s = b->chord()->segment(); s && s->tick().ticks() > ta; s = s->prev1(SegmentType::ChordRest)) {
                  for (const Element* e : s->annotations())
                        if (e->isDynamic() && e->part() == part)
                              return true;
                  }
            return false;
            };
      for (auto& line : lines) {
            std::vector<Group*> gs;
            for (auto& g : line.second)
                  gs.push_back(&g.second);
            for (size_t i = 1; i < gs.size(); ++i) {
                  if (gs[i - 1]->writtenEnd != gs[i]->written)
                        continue;
                  // the top notes of each
                  auto top = [](const Group* g) {
                        return *std::max_element(g->notes.begin(), g->notes.end(), [](const Played* a, const Played* b) { return a->pitch < b->pitch; });
                        };
                  const Played* a = top(gs[i - 1]);
                  const Played* b = top(gs[i]);
                  if (!slurBoth(a->note->lastTiedNote(), b->note) || dynamicChanges(a->note->lastTiedNote(), b->note))
                        continue;
                  QString howA, howB;
                  const double la = level(a, &howA);
                  const double lb = level(b, &howB);
                  const double step = lb - la;
                  if (std::abs(step) < 0.05)
                        continue;
                  finding(Kind::LEVEL_STEP, Severity::INFO, b, QString("%1 dB after the note before (pitch %2, %3: %4 dB; this %5: %6 dB)%7")
                          .arg(step, 0, 'f', 1).arg(a->pitch).arg(techniqueOf(*a)).arg(la, 0, 'f', 1).arg(howB).arg(lb, 0, 'f', 1)
                          .arg(howA.isEmpty() ? QString() : QString(" [before: %1]").arg(howA)), step);
                  }
            }
      return report;
      }

}     // namespace PlaybackAudit
}     // namespace Ms
