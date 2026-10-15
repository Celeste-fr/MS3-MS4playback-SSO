//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "contextlevel.h"
#include "chord.h"
#include "ms4playback.h"
#include "note.h"
#include "part.h"
#include "playbacksettings.h"
#include "score.h"
#include "segment.h"
#include "slur.h"
#include "soundlibrary.h"
#include "staff.h"
#include "tempo.h"
#include "trackdelays.h"
#include "partplayback.h"
#include "rendermidi.h"
#include "repeatlist.h"
#include "synthesizerstate.h"
#include "undo.h"
#include <QMutex>
#include <algorithm>
#include <cmath>
#include <set>

namespace Ms {
namespace ContextLevel {

bool on(const Score* score)
      {
      return Playback::on("levels/context", score);
      }

bool sectionBalance(const Score* score)
      {
      return Playback::on("levels/sectionBalance", score);
      }

bool realRange(const Score* score)
      {
      return Playback::on("levels/realRange", score);
      }

bool clarkLuceSpans(const Score* score)
      {
      return Playback::value("levels/realRange", score) >= 1.5;
      }

bool hasRealRange(const SoundLib::LibInstrument* patch)
      {
      return patch && !patch->realSpan.empty() && !patch->longLevel.empty();
      }

double markingSteps(int x)
      {
      // (Ms4::expressionLevel: ppp 16, pp 32 … ff 112 in steps of 16, fff 127)
      if (x <= 16)
            return -3.0;
      if (x <= 112)
            return (x - 64) / 16.0;
      if (x < 127)
            return 3.0 + (x - 112) / 15.0;
      return 4.0;
      }

// linear between the nearest keys, the nearest end's beyond
template <typename V, typename F>
static double between(const std::map<int, V>& m, double x, F value)
      {
      if (m.empty())
            return 0.0;
      auto hi = m.lower_bound(int(std::ceil(x)));
      if (hi == m.end())
            return value(std::prev(m.end())->second);
      if (hi == m.begin() || hi->first == x)
            return value(hi->second);
      auto lo = std::prev(hi);
      const double t = (x - lo->first) / double(hi->first - lo->first);
      return value(lo->second) + t * (value(hi->second) - value(lo->second));
      }

double realFraction(double steps, double pp, double ff)
      {
      // ppp 0 -> pp: one step; pp -> ff: 5 equal steps (p, mp, mf, f); ff -> fff 1: one step
      if (steps <= -2.0)
            return std::max(0.0, (steps + 3.0) * pp);
      if (steps <= 3.0)
            return pp + (steps + 2.0) * (ff - pp) / 5.0;
      return std::min(1.0, ff + (steps - 3.0) * (1.0 - ff));
      }

double realRangeDb(const SoundLib::LibInstrument& patch, int x, int pitch, bool spans)
      {
      if (!hasRealRange(&patch))
            return 0.0;
      const double cc = std::max(0, std::min(127, x));
      const double sso = between(patch.longLevel, pitch, [cc](const std::map<int, double>& byCc) {
            return between(byCc, cc, [](double v) { return v; });
            });
      // Clark & Luce spans: equal steps of S / 5 from mp, ppp / fff one step more (markingSteps -3 .. +4)
      if (spans && patch.realStepsDb > 0.0)
            return markingSteps(x) * patch.realStepsDb / 5.0 - sso;
      const double span = between(patch.realSpan, pitch, [](double v) { return v; });
      const double player = realFraction(markingSteps(x), patch.realPlayPp, patch.realPlayFf)
                            - realFraction(0.0, patch.realPlayPp, patch.realPlayFf);
      return player * span - sso;
      }

bool has(const SoundLib::Articulation* a)
      {
      return a && !a->contextLevels.empty();
      }

bool has(const SoundLib::LibInstrument* patch)
      {
      if (!patch)
            return false;
      for (const SoundLib::Articulation& a : patch->articulations)
            if (has(&a))
                  return true;
      return false;
      }

// the slur (not a phrase mark) the chord is in on its staff and voice; nullptr: none
static const Slur* slurOf(const Chord* chord)
      {
      const int t = chord->tick().ticks();
      const Slur* found = nullptr;
      for (const auto& iv : chord->score()->spannerMap().findOverlapping(t, t)) {
            const Spanner* sp = iv.value;
            if (!sp->isSlur() || toSlur(sp)->phraseMark() || sp->track() != chord->track())
                  continue;
            if (sp->tick().ticks() <= t && sp->tick2().ticks() >= t
                && (!found || sp->tick() < found->tick()))
                  found = toSlur(sp);
            }
      return found;
      }

static double median(std::vector<double> v)
      {
      if (v.empty())
            return 0.0;
      std::sort(v.begin(), v.end());
      const size_t n = v.size();
      return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
      }

// the phrase's chords (its slur's on the slur's track, or the chord alone)
static std::vector<const Chord*> phrase(const Chord* chord)
      {
      const Slur* s = slurOf(chord);
      if (!s)
            return { chord };
      std::vector<const Chord*> chords;
      const int track = s->track();
      for (const Segment* seg = chord->score()->tick2segment(s->tick(), true, SegmentType::ChordRest);
           seg && seg->tick() <= s->tick2(); seg = seg->next1(SegmentType::ChordRest)) {
            const Element* e = seg->element(track);
            if (e && e->isChord() && (chords.empty() || chords.back() != e))
                  chords.push_back(toChord(e));
            }
      if (chords.empty())
            chords.push_back(chord);
      return chords;
      }

double gain(const SoundLib::Articulation& a, const Chord* chord, const Ms4::Dynamics& dynamics, int tickOffset)
      {
      const std::vector<const Chord*> chords = phrase(chord);
      const TempoMap* tempo = chord->score()->masterScore()->tempomap();
      std::vector<double> pitches, iois;
      for (size_t i = 0; i < chords.size(); ++i) {
            for (const Note* n : chords[i]->notes())
                  pitches.push_back(n->ppitch());
            if (i > 0)
                  iois.push_back(1000.0 * tempo->writtenTime(chords[i - 1]->tick().ticks(), chords[i]->tick().ticks()));
            }
      // (one chord: its length as its spacing)
      if (iois.empty())
            iois.push_back(1000.0 * tempo->writtenTime(chords.front()->tick().ticks(),
                                                        chords.front()->tick().ticks() + chords.front()->actualTicks().ticks()));
      const int cc = Ms4::expressionLevel(dynamics.levelAt(chords.front()->track(), chords.front()->tick().ticks() + tickOffset));
      return a.contextLevelAt(cc, median(iois), median(pitches));
      }

static std::map<const Part*, Volumes> compute(const MasterScore* score, const SoundLib::Library& library, bool context, bool section,
                                              const std::map<const Part*, double>& ranges);

// (realRangeTops renders the score, and the renderer reads the volumes: inside it, the volumes without the real range)
static thread_local int rangePass = 0;

std::map<const Part*, double> realRangeTops(const MasterScore* score)
      {
      std::map<const Part*, double> tops;
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library || !realRange(score) || library->dynamicsCC < 0 || library->dynamicsCC == CTRL_EXPRESSION)
            return tops;
      bool any = false;
      for (const SoundLib::Route& r : SoundLib::routes(score, *library))
            any = any || hasRealRange(r.instrument);
      if (!any)
            return tops;
      ++rangePass;
      {
            MasterScore* ms = const_cast<MasterScore*>(score);
            MidiRenderer midi(ms);
            midi.setMinChunkSize(10);
            midi.setRealRangeTops(&tops);
            SynthesizerState ss;
            MidiRenderer::Context ctx { ss };
            ctx.metronome = false;
            for (MidiRenderer::Chunk chunk = midi.getChunkAt(0); chunk; chunk = midi.getChunkAt(chunk.utick2())) {
                  EventMap events;
                  midi.renderChunk(chunk, &events, ctx);
                  }
      }
      --rangePass;
      return tops;
      }

// (read often: TrackDelays::read, the host's mixer every 50 ms; kept per score until an edit, an undo, another library,
// other routes or settings)
std::map<const Part*, Volumes> volumes(const MasterScore* score)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library)
            return std::map<const Part*, Volumes>();
      const bool context = on(score) && Playback::on("slurs/performance", score);
      const bool section = sectionBalance(score);
      const bool real = realRange(score) && !rangePass;
      if (!context && !section && !real)
            return std::map<const Part*, Volumes>();
      const UndoStack* undo = score->undoStack();
      const QString key = QString("%1|%2|%3|%4|%5|%6|%7").arg(quintptr(library.get())).arg(SoundLib::routesGeneration())
                          .arg(Playback::generation()).arg(undo ? undo->getCurIdx() : -1).arg(undo ? undo->state() : -1)
                          .arg(score->metaTag(Playback::metaTag)).arg(score->metaTag(PartPlaybackModes::metaTag))
                          + QString("|%1|%2|%3%4").arg(score->parts().isEmpty() ? 0 : quintptr(score->parts().first())).arg(score->nstaves())
                          .arg(int(context)).arg(int(section)) + QString("|%1").arg(int(real));
      static QMutex mutex;
      static std::map<const MasterScore*, std::pair<QString, std::map<const Part*, Volumes>>> cache;
      {
            QMutexLocker lock(&mutex);
            auto c = cache.find(score);
            if (c != cache.end() && c->second.first == key && undo && !undo->active())
                  return c->second.second;
      }
      // (the real range's headroom renders the score, which reads the volumes again: not under the lock)
      const std::map<const Part*, double> ranges = real ? realRangeTops(score) : std::map<const Part*, double>();
      if (!context && !section && ranges.empty())
            return std::map<const Part*, Volumes>();
      QMutexLocker lock(&mutex);
      if (cache.size() > 16)
            cache.clear();
      std::map<const Part*, Volumes> out = compute(score, *library, context, section, ranges);
      cache[score] = { key, out };
      return out;
      }

static std::map<const Part*, Volumes> compute(const MasterScore* score, const SoundLib::Library& library, bool context, bool section,
                                              const std::map<const Part*, double>& ranges)
      {
      std::map<const Part*, Volumes> out;
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, library);
      std::map<const Part*, std::vector<const SoundLib::LibInstrument*>> patches;
      for (const SoundLib::Route& r : routes)
            if (r.lane == 0)
                  patches[r.part].push_back(r.instrument);
      // (no section level in the score's patches: the section balance changes nothing)
      bool levels = false;
      for (const auto& pp : patches)
            for (const SoundLib::LibInstrument* p : pp.second)
                  levels = levels || p->sectionLevelDb != 0.0;
      section = section && levels;
      const bool real = !ranges.empty();
      if (!context && !section && !real)
            return out;
      // the rule ([levels] context): the largest slur gain of each part with the articulation, and of the score (G)
      std::map<const Part*, double> partTop;
      std::set<const Part*> ruled;          // (the parts with the articulation)
      double top = -1e9;
      for (const auto& pp : patches) {
            if (!context)
                  break;
            const SoundLib::Articulation* a = nullptr;
            for (const SoundLib::LibInstrument* p : pp.second)
                  for (const SoundLib::Articulation& x : p->articulations)
                        if (!a && has(&x))
                              a = &x;
            if (!a)
                  continue;
            ruled.insert(pp.first);
            Part* part = const_cast<Part*>(pp.first);
            Ms4::Dynamics dynamics;
            dynamics.build(const_cast<MasterScore*>(score), part);
            // every slur's phrase ([slurs] performance plays slurs on it: a superset of what plays on it). A note on
            // it outside slurs (staff text "performance") is not counted: louder than G it plays at G (the renderer)
            std::set<const Slur*> done;
            for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                  for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                        const Element* e = seg->element(track);
                        if (!e || !e->isChord())
                              continue;
                        const Slur* s = slurOf(toChord(e));
                        if (!s || !done.insert(s).second)
                              continue;
                        const double g = gain(*a, toChord(e), dynamics, 0);
                        top = std::max(top, g);
                        auto t = partTop.find(pp.first);
                        if (t == partTop.end())
                              partTop[pp.first] = g;
                        else
                              t->second = std::max(t->second, g);
                        }
                  }
            }
      if (!section && !real) {
            // the rule alone: the score's largest gain G on every Performance patch, every other patch -S
            if (partTop.empty())
                  return out;
            const double s = std::max(0.0, top - TrackDelays::MAX_DB);
            for (const auto& pp : patches) {
                  Volumes v;
                  v.top = top;
                  for (const SoundLib::LibInstrument* p : pp.second)
                        v.patchDb[p->name] = has(p) ? top - s : -s;
                  out[pp.first] = v;
                  }
            return out;
            }
      // [levels] sectionBalance: every patch its section's level O on top; with the rule, each phrase plays gain + O,
      // everything lower together by S' = max(0, the loudest phrase's gain + O - MAX_DB). A Performance patch of a part
      // with the articulation plays at T + O - S', its phrases' CC11 their gain less T, T = min(G, S' + MAX_DB - O):
      // the score's G as with the rule alone (the same CC11 values as the rule's), lower only where G would put the patch over
      // MAX_DB (T is at least the part's own largest gain, so no CC11 goes over 0 dB). Without slurs anywhere (staff
      // text "performance" alone) T = S' + MAX_DB - O.
      // [levels] realRange: every patch of a part also its R louder (the part's largest realDb: its notes' CC11 play
      // realDb - R), within the same S' (T = min(G, S' + MAX_DB - O - R)). Section balance off: O = 0
      auto levelOf = [section](const SoundLib::LibInstrument* p) { return section ? p->sectionLevelDb : 0.0; };
      auto rangeOf = [&ranges](const Part* part) {
            auto r = ranges.find(part);
            return r == ranges.end() ? 0.0 : r->second;
            };
      double loudest = 0.0;
      for (const auto& pp : patches) {
            auto t = partTop.find(pp.first);
            for (const SoundLib::LibInstrument* p : pp.second)
                  loudest = std::max(loudest, levelOf(p) + rangeOf(pp.first) + (t != partTop.end() && has(p) ? t->second : 0.0));
            }
      const double s = std::max(0.0, loudest - TrackDelays::MAX_DB);
      for (const auto& pp : patches) {
            Volumes v;
            v.range = rangeOf(pp.first);
            const bool rule = ruled.count(pp.first);
            for (const SoundLib::LibInstrument* p : pp.second)
                  if (rule && has(p))
                        v.top = std::min(partTop.empty() ? 1e9 : top, s + TrackDelays::MAX_DB - levelOf(p) - v.range);
            for (const SoundLib::LibInstrument* p : pp.second)
                  v.patchDb[p->name] = levelOf(p) + v.range + (rule && has(p) ? v.top : 0.0) - s;
            out[pp.first] = v;
            }
      return out;
      }

}     // namespace ContextLevel
}     // namespace Ms
