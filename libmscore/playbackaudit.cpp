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

#include <QDir>
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
#include "tuning.h"

namespace Ms {
namespace PlaybackAudit {

const char* kindName(Kind k)
      {
      switch (k) {
            case Kind::OVERLAP:    return "OVERLAP";
            case Kind::UNMEASURED: return "UNMEASURED";
            case Kind::CAPPED:     return "CAPPED";
            case Kind::ARRIVAL:    return "ARRIVAL";
            case Kind::LEVEL_STEP: return "LEVEL STEP";
            case Kind::DROPPED:    return "DROPPED";
            case Kind::LEAP:       return "LEAP";
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
// ensemble is normal playing; applied here to the predicted arrivals of one line's neighbouring notes: a step beyond
// the upper end fails, between the two it is at the edge
static const double TIMING_WARN_MS = 30.0;
static const double TIMING_FAIL_MS = 50.0;

// SSO's legato engine (read privately, behaviour only): a legato note-on less than 15 ms after the previous legato
// note-on on its channel, at velocity 64 or lower, isn't played; a step beyond an octave gets no transition
static const double DROP_GAP_MS = MidiRenderer::TRANSITION_MIN_GAP_MS;
static const int DROP_MAX_VELOCITY = 64;
static const double LEAP_MAX_CENTS = 1200.0;

// Jesteadt, Wier & Green 1977 (JASA 61, 169-177), as Praat's manual (phonToDifferenceLimens) quotes it:
// dI / I = 0.463 (I / I0)^-0.072; in dB: 10 log10(1 + dI / I)
double differenceLimenDb(double sensationLevel)
      {
      return 10.0 * std::log10(1.0 + 0.463 * std::pow(10.0, -0.072 * sensationLevel / 10.0));
      }

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
            events->startLeadMs = part.startLeadMs;               // (the start lead: the same in every chunk)
            events->startEarlyMs.insert(part.startEarlyMs.begin(), part.startEarlyMs.end());
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
      out << QString("%1 library notes; [legato] keepMs %2; predicted arrival (the map's onsets, not the real sound): "
                     "Rasch 1979, warn > %3 ms, fail > %4 ms; level steps: values, proposed threshold (not enforced, owner to "
                     "approve) the difference limen at 60 dB SL, Jesteadt, Wier & Green 1977: %5 dB, marked \"> DL\"")
                     .arg(notes).arg(keepMs).arg(TIMING_WARN_MS).arg(TIMING_FAIL_MS).arg(differenceLimenDb(), 0, 'f', 2);
      for (Kind k : { Kind::OVERLAP, Kind::UNMEASURED, Kind::CAPPED, Kind::ARRIVAL, Kind::LEVEL_STEP, Kind::DROPPED, Kind::LEAP })
            out << QString("  %1: %2 fail, %3 warn, %4 info").arg(kindName(k), -12).arg(count(k, Severity::FAIL))
                   .arg(count(k, Severity::WARN)).arg(count(k, Severity::INFO));
      if (!transitionShiftsMs.empty()) {
            std::vector<double> v = transitionShiftsMs;
            std::sort(v.begin(), v.end());
            const double median = v.size() % 2 ? v[v.size() / 2] : (v[v.size() / 2 - 1] + v[v.size() / 2]) / 2;
            out << QString("  [slurs] transitionsEarly %6: %1 transitions started early, median %2 ms (%3 - %4), %5 capped")
                   .arg(v.size()).arg(median, 0, 'f', 1).arg(v.front(), 0, 'f', 1).arg(v.back(), 0, 'f', 1).arg(transitionsCapped)
                   .arg(transitionsEarlyMode);
            if (!transitionsFallback.empty()) {
                  QStringList names;
                  for (const QString& n : transitionsFallback)
                        names << n;
                  out << QString("    no measured glide (mode 2 / 3 fell back to the arrival): %1").arg(names.join(", "));
                  }
            }
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
      bool sub { false };           // an ornament's (or glissando's) later sub-note: never started early
      bool hasOnset { false };      // its technique has an onset in the map
      double onset { 0 };           // ms, the map's for this note, as the renderer takes it
      double arrival { 0 };         // ms, predicted: sent + onset - written (a transition: + its arrival instead)
      bool transition { false };    // played as a legato transition with a known arrival (LibTrace::glideArriveMs)
      bool hasArrival { false };    // arrival is known: an onset, or a transition's arrival
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
      // a note's note-on, ms on the score's time: without the start lead everything plays later by
      // (MidiRenderer::libraryDelayLead; a first note started its whole onset early comes out before its written time)
      auto sent = [&](const auto* p) { return ms(p->on) - events.startLeadMs; };

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

      // the onset the renderer takes (collectMeasureEventsMs4: a held note's early start): each note's by its pitch for a
      // swap or [heldNotes] byPitch, else the technique's median; playback.ini's [heldNotes.onset] applied. The chord
      // starts early by its latest note's, x <Onset early> %
      std::shared_ptr<const SoundLib::Library> lib = SoundLib::current();
      const bool byPitchAll = Playback::on("heldNotes/byPitch", score);
      const int onsetEarlyPct = lib ? SoundLib::onsetEarly(score, *lib) : 0;
      const bool transitionsEarly = Playback::value("slurs/transitionsEarly", score) >= 0.5;
      report.transitionsEarlyMode = int(std::lround(Playback::value("slurs/transitionsEarly", score)));
      auto onsetOf = [&](const Played* p, int pitch) {
            const SoundLib::Choice& c = p->trace->choice;
            const SoundLib::Articulation* a = c.articulation;
            return Playback::adjust("heldNotes.onset", p->trace->patch->name, a->name, pitch,
                                    a->startOnset(p->trace->attack, pitch, byPitchAll));   // (a smooth attack: its own)
            };
      std::map<const Note*, std::vector<int>> writtenOf;         // a note's rendered starts (sub-notes after the first)
      for (const auto& up : played)
            if (up->trace && up->note && up->written >= 0)
                  writtenOf[up->note].push_back(up->written);
      for (const auto& up : played) {
            Played* p = up.get();
            if (!p->trace || !p->note || p->written < 0)
                  continue;
            const std::vector<int>& w = writtenOf[p->note];
            const int len = p->note->chord()->actualTicks().ticks();
            p->sub = std::any_of(w.begin(), w.end(), [&](int x) { return x < p->written && p->written - x < len; });
            const SoundLib::Articulation* a = p->trace->choice.articulation;
            p->hasOnset = a->onsetMs > 0 || !a->onsets.empty() || p->trace->attack;
            if (p->hasOnset) {
                  p->onset = onsetOf(p, p->pitch);
                  p->arrival = sent(p) + p->onset - ms(p->written);
                  p->hasArrival = true;
                  }
            // a legato transition isn't an attack: it arrives when the transition reaches the new pitch, as the renderer
            // models it for the note's bend (LibTrace::glideArriveMs: the patch's measured bendGlide by the note before's
            // length, else its legatoDelay by interval, an octave by its start pitch, quicker after a short note:
            // libFastDelay; playback.ini [legato.delay]); unknown (no delay in the map): no arrival
            if (p->trace->from && p->trace->glideArriveMs >= 0) {
                  p->transition = true;
                  p->hasArrival = true;
                  p->arrival = sent(p) + p->trace->glideArriveMs - ms(p->written);
                  }
            else if (p->trace->from)
                  p->hasArrival = false;
            }

      // (b) swapped or early without an in-context measurement, or no onset at all, per instrument; (c) capped
      struct Missing { int notes = 0; double maxEarly = 0; std::set<int> bars; QString what; };
      std::map<std::pair<QString, QString>, Missing> missing;     // (instrument, articulation)
      for (const auto& up : played) {
            const Played* p = up.get();
            if (!p->trace || !p->note || p->written < 0)
                  continue;
            const double early = ms(p->written) - sent(p);
            const SoundLib::Choice& c = p->trace->choice;
            const QString inst = p->trace->patch->name;
            const QString art = c.articulation->name;
            // ([slurs] transitionsEarly: a transition is meant its modelled arrival early, not its onset: capped when the
            // note before keeps keepMs (or SSO's 15 ms) as played, TRANSITION_MIN_GAP_MS; its shift is listed in the report)
            if (transitionsEarly && p->transition && !p->sub && p->trace->earlyMeantMs >= 0) {
                  const double meant = p->trace->earlyMeantMs;
                  if (p->trace->earlyFallback)
                        report.transitionsFallback.insert(p->trace->patch->name);
                  const double cut = meant - std::max(0.0, early);
                  const double tickMs = p->written > 0 ? ms(p->written) - ms(p->written - 1) : ms(1) - ms(0);
                  report.transitionShiftsMs.push_back(std::max(0.0, early));
                  if (cut > tickMs) {
                        ++report.transitionsCapped;
                        finding(Kind::CAPPED, Severity::INFO, p, QString("transition started %1 ms early, meant %2 ([slurs] transitionsEarly): "
                                                                         "cut by %3 ms, the note before on its patch keeps [legato] keepMs")
                                .arg(std::max(0.0, early), 0, 'f', 1).arg(meant, 0, 'f', 1).arg(cut, 0, 'f', 1), cut);
                        }
                  }
            else if (p->hasOnset && !p->sub && !p->note->tieBack() && onsetEarlyPct > 0) {
                  double chordOnset = 0;
                  for (const Note* n : p->note->chord()->notes())
                        if (n->play())
                              chordOnset = std::max(chordOnset, onsetOf(p, n->ppitch()));
                  const double meant = chordOnset * onsetEarlyPct / 100.0;
                  const double cut = meant - std::max(0.0, early);
                  // the renderer sends at whole ticks (utime2utick): a cut within one tick there is rounding
                  const double tickMs = p->written > 0 ? ms(p->written) - ms(p->written - 1) : ms(1) - ms(0);
                  if (cut > tickMs) {
                        // why: the note before on its route (patch) keeps keepMs, the pass starts here, or else
                        QString why = "other (a chunk start, grace notes, an arpeggio or a legato transition)";
                        const Played* before = nullptr;
                        for (const auto& uq : played)
                              if (uq->route == p->route && uq->on < p->on && (!before || uq->on > before->on))
                                    before = uq.get();
                        const RepeatList& repeats = score->repeatList();
                        auto rs = repeats.findRepeatSegmentFromUTick(p->written);
                        if (before && ms(before->on) + keepMs >= ms(p->on) - 0.5)
                              why = QString("the note before on its patch (pitch %1, %2 ms long as written) keeps [legato] keepMs")
                                    .arg(before->pitch).arg(before->note ? SoundLib::noteSeconds(before->note) * 1000 : 0.0, 0, 'f', 1);
                        else if (p->written == 0 || (rs != repeats.end() && (*rs)->utick == p->written))
                              why = "the pass starts here (time 0 or a repeat)";
                        finding(Kind::CAPPED, Severity::INFO, p, QString("early start cut by %1 ms (meant %2, started %3 ms early): %4")
                                .arg(cut, 0, 'f', 1).arg(meant, 0, 'f', 1).arg(std::max(0.0, early), 0, 'f', 1).arg(why), cut);
                        }
                  }
            if (!c.swapped && early <= 0.5 && p->hasOnset)
                  continue;
            if (transitionsEarly && p->transition && !c.swapped)
                  continue;         // (early by the transition's modelled arrival, not by an onset)
            QStringList what;
            if (!p->hasOnset)
                  what << "no onset in the map (arrival unknown)";
            else if (early > 0.5 && !fits(measured.onsetFits, art, inst))
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

      // (d) predicted arrival: a chord's earliest arriving note (with an onset in the map, or a legato transition with
      // its modelled arrival) against the note before it in its line, joined without a rest
      auto how = [&](const Played* p) {
            return p->transition ? QString("sent %1, transition arrives %2 after its note-on")
                                   .arg(sent(p) - ms(p->written), 0, 'f', 1).arg(p->trace->glideArriveMs, 0, 'f', 1)
                                 : QString("sent %1, onset %2").arg(sent(p) - ms(p->written), 0, 'f', 1).arg(p->onset, 0, 'f', 1);
            };
      for (auto& line : lines) {
            std::vector<Group*> gs;
            for (auto& g : line.second)
                  gs.push_back(&g.second);
            auto first = [](const Group* g) -> const Played* {
                  const Played* f = nullptr;
                  for (const Played* p : g->notes)
                        if (p->hasArrival && (!f || p->arrival < f->arrival))
                              f = p;
                  return f;
                  };
            for (size_t i = 1; i < gs.size(); ++i) {
                  if (gs[i - 1]->writtenEnd != gs[i]->written)
                        continue;
                  const Played* p = first(gs[i]);
                  const Played* q = first(gs[i - 1]);
                  if (!p || !q)
                        continue;
                  const double d = p->arrival - q->arrival;
                  if (std::abs(d) <= TIMING_WARN_MS)
                        continue;
                  finding(Kind::ARRIVAL, std::abs(d) > TIMING_FAIL_MS ? Severity::FAIL : Severity::WARN, p,
                          QString("%1 ms against the note before (arrives %2 ms off its written time: %3; the note "
                                  "before, pitch %4 %5, %6 ms: %7)").arg(d, 0, 'f', 1).arg(p->arrival, 0, 'f', 1)
                          .arg(how(p)).arg(q->pitch).arg(techniqueOf(*q)).arg(q->arrival, 0, 'f', 1).arg(how(q))
                          + QString("; onsets fitted in context: %1").arg(onsetFitted(p) ? (onsetFitted(q) ? "both" : "this only")
                                                                          : (onsetFitted(q) ? "the note before only" : "neither")), d);
                  }
            }

      // (e) the level of neighbouring notes under one slur at an unchanged written dynamic
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
                  finding(Kind::LEVEL_STEP, Severity::INFO, b, QString("%1 dB%8 after the note before (pitch %2, %3: %4 dB; this %5: %6 dB)%7")
                          .arg(step, 0, 'f', 1).arg(a->pitch).arg(techniqueOf(*a)).arg(la, 0, 'f', 1).arg(howB).arg(lb, 0, 'f', 1)
                          .arg(howA.isEmpty() ? QString() : QString(" [before: %1]").arg(howA))
                          .arg(std::abs(step) > differenceLimenDb() ? " (> DL)" : ""), step);
                  }
            }
      // (f) legato transitions SSO doesn't play: dropped (a note-on within 15 ms of the legato note-on before on its route,
      // velocity <= 64) and leaps (beyond an octave: a fresh attack)
      ScoreTuning tuning(score);
      std::map<int, const Played*> lastOn;                // route -> the previous note-on
      for (const auto& up : played) {
            const Played* p = up.get();
            const Played* before = lastOn[p->route];
            lastOn[p->route] = p;
            if (!p->trace || !p->note || !p->trace->from || !p->trace->choice || !p->trace->choice.articulation->playsTransitions())
                  continue;
            if (before && before->trace && before->trace->choice && before->trace->choice.articulation->playsTransitions()
                && before->note && before->note->chord() != p->note->chord()) {
                  const double gap = ms(p->on) - ms(before->on);
                  if (gap < DROP_GAP_MS && p->velocity <= DROP_MAX_VELOCITY)
                        finding(Kind::DROPPED, Severity::FAIL, p, QString("velocity %1, %2 ms after the note-on before on its route "
                                "(pitch %3): SSO's legato drops a note under %4 ms at velocity %5 or lower")
                                .arg(p->velocity).arg(gap, 0, 'f', 1).arg(before->pitch).arg(DROP_GAP_MS).arg(DROP_MAX_VELOCITY), gap);
                  }
            const Note* from = p->trace->from;
            const double cents = 100.0 * (p->note->ppitch() - from->ppitch()) + tuning.cents(p->note) - tuning.cents(from);
            if (std::abs(cents) > LEAP_MAX_CENTS + 0.01)
                  finding(Kind::LEAP, Severity::WARN, p, QString("transition from pitch %1: %2 semitones (%3 cents) beyond an octave, SSO "
                          "attacks afresh (velocity %4); its Performance technique, legato delay and level calibration don't apply")
                          .arg(from->ppitch()).arg(cents / 100.0, 0, 'f', 2).arg(cents, 0, 'f', 0).arg(p->velocity), cents);
            }

      // (g) the rows for the heard check: every note-on with a choice, its prediction and its place in its line and slur
      std::vector<const Slur*> slurs;
      for (const auto& sp : score->spanner())
            if (sp.second->isSlur() && !toSlur(sp.second)->phraseMark())
                  slurs.push_back(toSlur(sp.second));
      std::sort(slurs.begin(), slurs.end(), [](const Slur* a, const Slur* b) {
            return a->tick() != b->tick() ? a->tick() < b->tick() : a->track() < b->track();
            });
      std::map<const Played*, const Played*> before;            // the top note of the chord before in its line, joined
      std::map<const Played*, bool> slurredBefore, dynamicBefore;
      for (auto& line : lines) {
            std::vector<Group*> gs;
            for (auto& g : line.second)
                  gs.push_back(&g.second);
            for (size_t i = 1; i < gs.size(); ++i) {
                  if (gs[i - 1]->writtenEnd != gs[i]->written)
                        continue;
                  const Played* a = *std::max_element(gs[i - 1]->notes.begin(), gs[i - 1]->notes.end(),
                                                      [](const Played* x, const Played* y) { return x->pitch < y->pitch; });
                  for (const Played* b : gs[i]->notes) {
                        before[b] = a;
                        slurredBefore[b] = slurBoth(a->note->lastTiedNote(), b->note);
                        dynamicBefore[b] = dynamicChanges(a->note->lastTiedNote(), b->note);
                        }
                  }
            }
      for (const auto& up : played) {
            const Played* p = up.get();
            if (!p->trace || !p->note || !p->trace->choice || p->written < 0)
                  continue;
            const Note* n = p->note;
            NoteRow r;
            r.part = n->part()->partName();
            r.track = n->track();
            const Measure* m = n->chord()->measure();
            r.bar = m->no() + 1;
            const int beatTicks = DIVISION * 4 / std::max(1, m->timesig().denominator());
            r.beat = 1 + double(n->chord()->tick().ticks() - m->tick().ticks()) / beatTicks;
            r.written = ms(p->written) / 1000;
            const Note* last = n->lastTiedNote();
            r.writtenEnd = ms(p->written + (last->chord()->tick() + last->chord()->actualTicks() - n->chord()->tick()).ticks()) / 1000;
            r.pitch = p->pitch;
            r.cents = tuning.cents(n);
            r.route = p->route;
            r.patch = p->trace->patch->name;
            r.articulation = p->trace->choice.articulation->name;
            r.techniques = p->trace->choice.articulation->techniques.join(",");
            r.velocity = p->velocity;
            r.performance = int(p->trace->performance);
            if (p->trace->from) {
                  r.transition = true;
                  r.fromPitch = p->trace->from->ppitch();
                  r.fromWritten = score->utick2utime(p->trace->from->chord()->tick().ticks());
                  }
            r.sentMs = sent(p) - ms(p->written);
            r.onsetMs = p->hasOnset ? p->onset : -1.0;
            r.glideArriveMs = p->trace->glideArriveMs;
            r.hasArrival = p->hasArrival;
            r.arrivalMs = p->arrival;
            r.sub = p->sub;
            r.grace = n->chord()->isGrace();
            const int tick = n->chord()->tick().ticks();
            for (size_t i = 0; i < slurs.size(); ++i)
                  if (slurs[i]->track() == n->track() && slurs[i]->tick().ticks() <= tick && tick <= slurs[i]->tick2().ticks())
                        r.slur = int(i);          // (sorted by start: the latest one that covers it)
            r.joinedBefore = before.count(p);
            r.slurredBefore = r.joinedBefore && slurredBefore[p];
            r.dynamicBefore = r.joinedBefore && dynamicBefore[p];
            report.rows.push_back(r);
            }
      return report;
      }

//---------------------------------------------------------
//   Report::rowsText
//---------------------------------------------------------

QString Report::rowsText() const
      {
      QStringList out;
      out << "part\ttrack\tbar\tbeat\twritten\twrittenEnd\tpitch\tcents\troute\tpatch\tarticulation\ttechniques\tvelocity\t"
             "performance\ttransition\tfromWritten\tfromPitch\tsentMs\tonsetMs\tglideArriveMs\thasArrival\tarrivalMs\tsub\tgrace\t"
             "slur\tjoinedBefore\tslurredBefore\tdynamicBefore";
      for (const NoteRow& r : rows)
            out << QStringList({ r.part, QString::number(r.track), QString::number(r.bar), QString::number(r.beat, 'f', 4),
                                 QString::number(r.written, 'f', 6), QString::number(r.writtenEnd, 'f', 6), QString::number(r.pitch),
                                 QString::number(r.cents, 'f', 2), QString("%1-%2").arg(r.route / 16).arg(r.route % 16 + 1), r.patch,
                                 r.articulation, r.techniques, QString::number(r.velocity), QString::number(r.performance),
                                 QString::number(int(r.transition)), QString::number(r.fromWritten, 'f', 6), QString::number(r.fromPitch),
                                 QString::number(r.sentMs, 'f', 2), QString::number(r.onsetMs, 'f', 2), QString::number(r.glideArriveMs, 'f', 2),
                                 QString::number(int(r.hasArrival)), QString::number(r.arrivalMs, 'f', 2), QString::number(int(r.sub)),
                                 QString::number(int(r.grace)), QString::number(r.slur), QString::number(int(r.joinedBefore)),
                                 QString::number(int(r.slurredBefore)), QString::number(int(r.dynamicBefore)) }).join("\t");
      return out.join("\n") + "\n";
      }

//---------------------------------------------------------
//   writeHeardInputs
//---------------------------------------------------------

bool writeHeardInputs(const Score* score, const EventMap& events, const Report& report, const QString& folder)
      {
      QDir().mkpath(folder);
      auto write = [&folder](const QString& name, const QString& text) {
            QFile f(folder + "/" + name);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                  return false;
            f.write(text.toUtf8());
            return true;
            };
      bool ok = true;
      std::map<QString, QStringList> ev;
      for (const auto& te : events) {
            const NPlayEvent& e = te.second;
            if (!e.isExternal())
                  continue;
            const QString key = QString("%1-%2").arg(e.extPort()).arg(e.extChannel() + 1);
            const double t = score->utick2utime(te.first);
            if (e.type() == ME_NOTEON)
                  ev[key] << QString("%1 %2 %3 %4").arg(t, 0, 'f', 6).arg(e.velo() > 0 ? "on" : "off").arg(e.pitch()).arg(e.velo());
            else if (e.type() == ME_NOTEOFF)
                  ev[key] << QString("%1 off %2 0").arg(t, 0, 'f', 6).arg(e.pitch());
            else if (e.type() == ME_CONTROLLER)
                  ev[key] << QString("%1 cc %2 %3").arg(t, 0, 'f', 6).arg(e.controller()).arg(e.value());
            else if (e.type() == ME_PITCHBEND)
                  ev[key] << QString("%1 pb %2 0").arg(t, 0, 'f', 6).arg(e.dataA() | (e.dataB() << 7));
            }
      for (const auto& m : ev)
            ok = write(m.first + " museScore.txt", m.second.join("\n") + "\n") && ok;
      QStringList rm;
      if (std::shared_ptr<const SoundLib::Library> lib = SoundLib::current())
            for (const SoundLib::Route& r : SoundLib::routes(score, *lib))
                  rm << QString("%1-%2|%3|%4|%5|%6|%7").arg(r.port).arg(r.channel + 1).arg(r.part ? r.part->partName() : QString(),
                           r.instrument ? r.instrument->name : QString()).arg(r.lane).arg(r.cents, 0, 'f', 2)
                           .arg(r.instrument ? r.instrument->bendCents : 0.0, 0, 'f', 1);
      ok = write("routemap.txt", rm.join("\n") + "\n") && ok;
      ok = write("predicted.tsv", report.rowsText()) && ok;
      ok = write("lead.txt", QString::number(events.startLeadMs, 'f', 3) + "\n") && ok;
      QStringList bars;
      for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure())
            bars << QString::number(score->utick2utime(m->tick().ticks()), 'f', 6);
      ok = write("bars.txt", bars.join("\n") + "\n") && ok;
      return ok;
      }

}     // namespace PlaybackAudit
}     // namespace Ms
