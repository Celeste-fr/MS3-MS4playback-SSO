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
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <tuple>

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

int sectionBalanceMode(const Score* score)
      {
      return qBound(0, int(Playback::value("levels/sectionBalance", score) + 0.5), 2);
      }

bool realRange(const Score* score)
      {
      return Playback::on("levels/realRange", score);
      }

bool clarkLuceSpans(const Score* score)
      {
      return Playback::value("levels/realRange", score) >= 1.5;
      }

//---------------------------------------------------------
//   Mics ([levels] micCompensation, contextlevel.h › Mics)
//---------------------------------------------------------

bool micCompensation(const Score* score)
      {
      return Playback::on("levels/micCompensation", score);
      }

// which of Mic 1-5, Mic Mix Distance a controller sets (-1: none)
static int micIndex(const SoundLib::Controller& c)
      {
      if (c.param == "Mic Mix Distance")
            return 5;
      static const QRegularExpression re("^Mic ([1-5]) level$");
      const QRegularExpressionMatch m = re.match(c.param);
      return m.hasMatch() ? m.captured(1).toInt() - 1 : -1;
      }

bool MicValues::automated() const
      {
      return std::any_of(hasLane.begin(), hasLane.end(), [](bool b) { return b; });
      }

std::array<double, 6> MicValues::at(int tick) const
      {
      std::array<double, 6> f;
      for (size_t i = 0; i < 6; ++i) {
            const double l = hasLane[i] ? lanes[i].valueAt(tick) : -1.0;
            f[i] = l >= 0.0 ? 127.0 * std::min(1.0, l) : part[i];
            }
      return f;
      }

MicValues micValues(const Part* part, const SoundLib::LibInstrument& patch,
                    const std::map<const Part*, Automation::PartLanes>& lanes)
      {
      MicValues v;
      const Automation::PartLanes own = Automation::lanes(part, lanes);
      for (const SoundLib::Controller& c : patch.allControllers) {
            const int i = micIndex(c);
            if (i < 0)
                  continue;
            v.part[size_t(i)] = PartControllers::value(part, c, lanes);
            if (PartControllers::flatLane(own, c))
                  continue;         // (a setting: the patch volume)
            for (const Automation::Lane& l : own)
                  if (l.target == c.id && !l.points.empty()) {
                        v.lanes[size_t(i)] = l;
                        v.hasLane[size_t(i)] = true;
                        }
            }
      return v;
      }

double micFaderDb(const SoundLib::Library& library, double fader)
      {
      const std::map<int, double>& law = library.micFader;
      if (fader <= 0.0 || law.empty())
            return -INFINITY;
      if (fader >= 127.0)
            return 0.0;
      auto hi = law.lower_bound(int(std::ceil(fader)));
      if (hi == law.begin()) {
            // (below the first measured value: linear in amplitude down to silence at 0)
            return hi->second + 20.0 * std::log10(fader / hi->first);
            }
      // (127 is 0 dB whether listed or not)
      const std::pair<int, double> b = hi == law.end() ? std::make_pair(127, 0.0) : std::make_pair(hi->first, hi->second);
      const std::pair<int, double> a(std::prev(hi)->first, std::prev(hi)->second);
      if (b.first == a.first)
            return a.second;
      return a.second + (b.second - a.second) * (fader - a.first) / (b.first - a.first);
      }

// what Mic Mix Distance m sets Close, Tree, Ambient to, dB against their full faders (linear in amplitude between the
// measured values); false: no law
static bool micMixDb(const SoundLib::Library& library, double m, std::array<double, 3>& out)
      {
      const auto& law = library.micMix;
      if (law.empty())
            return false;
      auto hi = law.lower_bound(int(std::ceil(m)));
      if (hi == law.end())
            hi = std::prev(law.end());
      if (hi == law.begin() || hi->first == m) {
            out = hi->second;
            return true;
            }
      const auto lo = std::prev(hi);
      const double t = (m - lo->first) / double(hi->first - lo->first);
      for (size_t i = 0; i < 3; ++i) {
            const double a = std::isinf(lo->second[i]) ? 0.0 : std::pow(10.0, lo->second[i] / 20.0);
            const double b = std::isinf(hi->second[i]) ? 0.0 : std::pow(10.0, hi->second[i] / 20.0);
            const double g = a + (b - a) * t;
            out[i] = g > 0.0 ? 20.0 * std::log10(g) : -INFINITY;
            }
      return true;
      }

double micLevelDb(const SoundLib::LibInstrument& patch, const SoundLib::Library& library, const std::array<double, 6>& faders)
      {
      if (!patch.hasMicLevels || library.micFader.empty())
            return NAN;
      std::array<double, 3> mix;
      const bool mixed = faders[5] >= 0.0 && micMixDb(library, faders[5], mix);
      double power = 0.0;
      for (size_t i = 0; i < 5; ++i) {
            if (std::isnan(patch.micLevelDb[i]))
                  continue;
            // (with Mic Mix Distance the mic values sent with it are not played: measured, sso_mic_levels.json)
            double g;
            if (mixed)
                  g = i < 3 ? mix[i] : micFaderDb(library, patch.micDefault[i]);
            else if (faders[i] >= 0.0)
                  g = micFaderDb(library, faders[i]);
            else
                  g = micFaderDb(library, patch.micDefault[i]);
            if (!std::isinf(g))
                  power += std::pow(10.0, (patch.micLevelDb[i] + g) / 10.0);
            }
      return power > 0.0 ? 10.0 * std::log10(power) : NAN;
      }

double micOffsetDb(const SoundLib::LibInstrument& patch, const SoundLib::Library& library, const std::array<double, 6>& faders)
      {
      const double l = micLevelDb(patch, library, faders);
      return std::isnan(l) ? 0.0 : -l;
      }

bool hasRealRange(const SoundLib::LibInstrument* patch)
      {
      // (a patch with no database instrument has realSteps only, its family's: realRange 2 plays it, realRangeDb gives 1 none)
      return patch && !patch->longLevel.empty() && (!patch->realSpan.empty() || patch->realStepsDb > 0.0);
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

double longLevelDb(const SoundLib::LibInstrument& patch, int x, int pitch)
      {
      const double cc = std::max(0, std::min(127, x));
      return between(patch.longLevel, pitch, [cc](const std::map<int, double>& byCc) {
            return between(byCc, cc, [](double v) { return v; });
            });
      }

double realRangeDb(const SoundLib::LibInstrument& patch, int x, int pitch, bool spans)
      {
      if (!hasRealRange(&patch))
            return 0.0;
      const double sso = longLevelDb(patch, x, pitch);
      // Clark & Luce spans: equal steps of S / 5 from mp, ppp / fff one step more (markingSteps -3 .. +4)
      if (spans && patch.realStepsDb > 0.0)
            return markingSteps(x) * patch.realStepsDb / 5.0 - sso;
      if (patch.realSpan.empty())
            return 0.0;
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

static std::map<const Part*, Volumes> compute(const MasterScore* score, const SoundLib::Library& library, bool context, int sectionMode,
                                              const std::map<const Part*, double>& ranges, bool mics);

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
      const int sectionMode = sectionBalanceMode(score);
      const bool section = sectionMode > 0;
      const bool real = realRange(score) && !rangePass;
      const bool mics = micCompensation(score) && !library->micFader.empty();
      if (!context && !section && !real && !mics)
            return std::map<const Part*, Volumes>();
      const UndoStack* undo = score->undoStack();
      const QString key = QString("%1|%2|%3|%4|%5|%6|%7").arg(quintptr(library.get())).arg(SoundLib::routesGeneration())
                          .arg(Playback::generation()).arg(undo ? undo->getCurIdx() : -1).arg(undo ? undo->state() : -1)
                          .arg(score->metaTag(Playback::metaTag)).arg(score->metaTag(PartPlaybackModes::metaTag))
                          + QString("|%1|%2|%3%4").arg(score->parts().isEmpty() ? 0 : quintptr(score->parts().first())).arg(score->nstaves())
                          .arg(int(context)).arg(sectionMode) + QString("|%1|%2").arg(int(real)).arg(int(mics))
                          + (mics ? "|" + score->metaTag(Automation::metaTag) : QString());
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
      if (!context && !section && ranges.empty() && !mics)
            return std::map<const Part*, Volumes>();
      QMutexLocker lock(&mutex);
      if (cache.size() > 16)
            cache.clear();
      std::map<const Part*, Volumes> out = compute(score, *library, context, sectionMode, ranges, mics);
      cache[score] = { key, out };
      return out;
      }

// [levels] micCompensation: each part's patches' C (contextlevel.h › Mics), the largest over the part's notes when its mics
// are automated; only those not 0
static std::map<const Part*, std::map<const SoundLib::LibInstrument*, double>> micTops(
      const MasterScore* score, const SoundLib::Library& library,
      const std::map<const Part*, std::vector<const SoundLib::LibInstrument*>>& patches)
      {
      std::map<const Part*, std::map<const SoundLib::LibInstrument*, double>> out;
      const std::map<const Part*, Automation::PartLanes> lanes = Automation::read(score);
      for (const auto& pp : patches) {
            std::set<int> ticks;          // (the part's note starts, when automated)
            for (const SoundLib::LibInstrument* p : pp.second) {
                  if (!p->hasMicLevels)
                        continue;
                  const MicValues v = micValues(pp.first, *p, lanes);
                  double top = micOffsetDb(*p, library, v.at(0));
                  if (v.automated()) {
                        if (ticks.empty()) {
                              for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest))
                                    for (int track = pp.first->startTrack(); track < pp.first->endTrack(); ++track) {
                                          const Element* e = seg->element(track);
                                          if (e && e->isChord())
                                                ticks.insert(seg->tick().ticks());
                                          }
                              }
                        if (!ticks.empty()) {
                              top = -1e9;
                              for (int t : ticks)
                                    top = std::max(top, micOffsetDb(*p, library, v.at(t)));
                              }
                        }
                  if (top != 0.0)
                        out[pp.first][p] = top;
                  }
            }
      return out;
      }

static std::map<const Part*, Volumes> compute(const MasterScore* score, const SoundLib::Library& library, bool context, int sectionMode,
                                              const std::map<const Part*, double>& ranges, bool mics)
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
                  levels = levels || p->sectionLevelFor(sectionMode) != 0.0;
      const bool section = sectionMode > 0 && levels;
      const bool real = !ranges.empty();
      const std::map<const Part*, std::map<const SoundLib::LibInstrument*, double>> micC
            = mics ? micTops(score, library, patches) : std::map<const Part*, std::map<const SoundLib::LibInstrument*, double>>();
      const bool mic = !micC.empty();
      if (!context && !section && !real && !mic)
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
      if (!section && !real && !mic) {
            // the rule alone: the score's largest gain G on every Performance patch, every other patch -S
            if (partTop.empty())
                  return out;
            // (the gain stage, contextlevel.h › Gain stage: no -S; S kept for the old volumes, TrackDelays::legacyPatchGain)
            const double s = std::max(0.0, top - TrackDelays::MAX_DB);
            for (const auto& pp : patches) {
                  Volumes v;
                  v.top = top;
                  v.shift = s;
                  for (const SoundLib::LibInstrument* p : pp.second)
                        v.patchDb[p->name] = has(p) ? top : 0.0;
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
      // [levels] micCompensation: each patch's C too, as its section level
      auto micOf = [&micC](const Part* part, const SoundLib::LibInstrument* p) {
            auto m = micC.find(part);
            if (m == micC.end())
                  return 0.0;
            auto c = m->second.find(p);
            return c == m->second.end() ? 0.0 : c->second;
            };
      auto levelOf = [section, sectionMode](const SoundLib::LibInstrument* p) { return section ? p->sectionLevelFor(sectionMode) : 0.0; };
      auto rangeOf = [&ranges](const Part* part) {
            auto r = ranges.find(part);
            return r == ranges.end() ? 0.0 : r->second;
            };
      double loudest = 0.0;
      for (const auto& pp : patches) {
            auto t = partTop.find(pp.first);
            for (const SoundLib::LibInstrument* p : pp.second)
                  loudest = std::max(loudest, levelOf(p) + micOf(pp.first, p) + rangeOf(pp.first) + (t != partTop.end() && has(p) ? t->second : 0.0));
            }
      const double s = std::max(0.0, loudest - TrackDelays::MAX_DB);
      for (const auto& pp : patches) {
            Volumes v;
            v.range = rangeOf(pp.first);
            v.shift = s;
            const bool rule = ruled.count(pp.first);
            for (const SoundLib::LibInstrument* p : pp.second)
                  if (rule && has(p))
                        v.top = std::min(partTop.empty() ? 1e9 : top, s + TrackDelays::MAX_DB - levelOf(p) - micOf(pp.first, p) - v.range);
            for (const SoundLib::LibInstrument* p : pp.second) {
                  v.patchDb[p->name] = levelOf(p) + micOf(pp.first, p) + v.range + (rule && has(p) ? v.top : 0.0);
                  if (micOf(pp.first, p) != 0.0)
                        v.micTop[p->name] = micOf(pp.first, p);
                  }
            out[pp.first] = v;
            }
      return out;
      }

//---------------------------------------------------------
//   masterTrim (contextlevel.h › Gain stage and master trim)
//---------------------------------------------------------

// (computeMasterTrim renders the score and reads the gains: inside it, no trim)
static thread_local int trimPass = 0;

namespace {
struct TrimGuard {
      TrimGuard() { ++trimPass; }
      ~TrimGuard() { --trimPass; }
      };
}

static double dbOf(double gain)
      {
      return gain > 0.0 ? 20.0 * std::log10(gain) : -200.0;
      }

static MasterTrim computeTrim(const MasterScore* score, const SoundLib::Library& library,
                              const std::map<const Part*, TrackDelays::Delays>& delays)
      {
      MasterTrim t;
      bool any = false;             // (a level setting gives a patch a gain: else nothing to trim, Library default)
      for (const auto& d : delays)
            any = any || !d.second.rule.empty() || !d.second.even.empty();
      if (!any || library.dynamicsCC == CTRL_EXPRESSION)
            return t;
      const std::vector<SoundLib::Route> routes = SoundLib::routes(score, library, SoundLib::output() == SoundLib::Output::MIDI);
      std::map<int, const SoundLib::Route*> byRoute;
      std::map<int, double> gainDb;                   // route -> its patch's gain (untrimmed), dB
      double loudest = -200.0;
      for (const SoundLib::Route& r : routes) {
            const int key = r.port * 16 + r.channel;
            byRoute[key] = &r;
            gainDb[key] = dbOf(TrackDelays::patchGain(library, TrackDelays::of(r.part, delays), r.instrument->name));
            loudest = std::max(loudest, gainDb[key]);
            }
      t.fallbackDb = std::max(0.0, loudest - TrackDelays::MAX_DB);
      const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::dynamicsCalibration();
      if (!cal || cal->patches().empty() || !std::isfinite(library.crestDb)) {
            t.db = t.fallbackDb;
            return t;
            }
      // the loudest level of a patch's curves, and of the calibration (a note whose own curve isn't measured)
      std::map<QString, double> patchTop;
      double calTop = -200.0;
      for (const auto& p : cal->patches()) {
            double top = -200.0;
            for (const auto& c : p.second) {
                  for (const auto& x : c.second.points)
                        top = std::max(top, x.second);
                  for (const auto& pc : c.second.pitches)
                        for (const auto& x : pc.second.points)
                              top = std::max(top, x.second);
                  }
            patchTop[p.first] = top;
            calTop = std::max(calTop, top);
            }

      // the score rendered once, each library note's choice traced (its articulation: the calibration's curve)
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      {
            MidiRenderer midi(const_cast<MasterScore*>(score));
            midi.setMinChunkSize(10);
            midi.setLibraryTrace(&trace);
            SynthesizerState ss;
            MidiRenderer::Context ctx { ss };
            ctx.metronome = false;
            for (MidiRenderer::Chunk chunk = midi.getChunkAt(0); chunk; chunk = midi.getChunkAt(chunk.utick2())) {
                  EventMap part;
                  midi.renderChunk(chunk, &part, ctx);
                  events.insert(part.begin(), part.end());
                  }
      }
      std::map<const Note*, const MidiRenderer::LibTrace*> traceOf;
      for (const MidiRenderer::LibTrace& x : trace)
            traceOf.emplace(x.note, &x);

      struct Curve {
            SoundLib::DynamicsCurve curve;
            bool has { false };
            double flat { -200.0 };       // without a curve: this level
            };
      std::map<std::tuple<QString, int, int>, std::shared_ptr<const Curve>> curves;     // (patch, value, pitch)
      auto curveOf = [&](const QString& patch, int value, int pitch) {
            auto& c = curves[std::make_tuple(patch, value, pitch)];
            if (!c) {
                  auto n = std::make_shared<Curve>();
                  const SoundLib::DynamicsCurve* dc = value >= 0 ? cal->curve(patch, value) : nullptr;
                  if (dc && !dc->points.empty()) {
                        n->curve = dc->atPitch(pitch);
                        n->has = !n->curve.points.empty();
                        }
                  if (!n->has) {
                        auto pt = patchTop.find(patch);
                        n->flat = pt != patchTop.end() ? pt->second : calTop;
                        }
                  c = n;
                  }
            return c;
            };
      struct Sounding {
            std::shared_ptr<const Curve> curve;
            int velocity { 0 };
            };
      struct RouteState {
            int cc11 { 127 };
            int dyn { 64 };
            std::map<int, Sounding> notes;          // pitch ->
            };
      std::map<int, RouteState> state;
      for (const auto& r : byRoute)
            state[r.first].cc11 = qBound(0, library.expressionValue, 127);
      const int dynamicsCC = library.dynamicsCC;
      double maxPower = 0.0;
      for (auto it = events.begin(); it != events.end();) {
            const int tick = it->first;
            bool changed = false;
            for (; it != events.end() && it->first == tick; ++it) {
                  const NPlayEvent& e = it->second;
                  if (!e.isExternal())
                        continue;
                  const int route = e.extPort() * 16 + e.extChannel();
                  auto br = byRoute.find(route);
                  if (br == byRoute.end())
                        continue;
                  RouteState& rs = state[route];
                  if (e.type() == ME_CONTROLLER) {
                        if (e.controller() == CTRL_EXPRESSION)
                              rs.cc11 = e.value();
                        else if (e.controller() == dynamicsCC)
                              rs.dyn = e.value();
                        changed = true;
                        }
                  else if (e.type() == ME_NOTEOFF || (e.type() == ME_NOTEON && e.velo() == 0))
                        rs.notes.erase(e.pitch());
                  else if (e.type() == ME_NOTEON && !e.librarySwitch()) {
                        auto tr = e.note() ? traceOf.find(e.note()) : traceOf.end();
                        const SoundLib::LibInstrument* patch = br->second->instrument;
                        int value = -1;
                        if (tr != traceOf.end() && tr->second->choice.articulation) {
                              value = tr->second->choice.articulation->value;
                              if (tr->second->patch)
                                    patch = tr->second->patch;
                              }
                        Sounding s;
                        s.curve = curveOf(patch ? patch->name : QString(), value, e.pitch());
                        s.velocity = e.velo();
                        rs.notes[e.pitch()] = s;
                        ++t.notes;
                        if (!s.curve->has)
                              ++t.uncalibrated;
                        changed = true;
                        }
                  }
            if (!changed)
                  continue;
            double power = 0.0, amplitude = 0.0;
            int sounding = 0;
            for (const auto& rs : state) {
                  if (rs.second.notes.empty() || rs.second.cc11 <= 0)
                        continue;
                  const double g = gainDb[rs.first] + dbOf(rs.second.cc11 / 127.0) + library.crestDb;
                  for (const auto& n : rs.second.notes) {
                        const Curve& c = *n.second.curve;
                        double level = c.flat;
                        if (c.has) {
                              const QString& by = c.curve.drivenBy;
                              const int x = by == "velocity" ? n.second.velocity
                                            : (by == "controller" ? rs.second.dyn
                                               : (by == "both" ? std::max(n.second.velocity, rs.second.dyn) : 127));
                              level = c.curve.at(qBound(1, x, 127));
                              }
                        power += std::pow(10.0, (level + g) / 10.0);
                        amplitude += std::pow(10.0, (level + g) / 20.0);
                        ++sounding;
                        }
                  }
            if (power > maxPower) {
                  maxPower = power;
                  t.peakDb = 10.0 * std::log10(power);
                  t.peakAmplitudeDb = 20.0 * std::log10(amplitude);
                  t.atSeconds = score->utick2utime(tick);
                  t.sounding = sounding;
                  }
            }
      t.measured = true;
      t.db = std::max(0.0, t.peakDb);
      return t;
      }

MasterTrim computeMasterTrim(const MasterScore* score)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library)
            return MasterTrim();
      TrimGuard guard;
      return computeTrim(score, *library, TrackDelays::read(score));
      }

MasterTrim masterTrim(const MasterScore* score)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      // (none inside the real range's render either: it needs no gains, and its volumes lack R)
      if (!score || !library || trimPass || rangePass)
            return MasterTrim();
      std::map<const Part*, TrackDelays::Delays> delays;
      {
            TrimGuard guard;
            delays = TrackDelays::read(score);
      }
      // (the key: what the render and the gains depend on; the gains themselves, as even levels change without an undo step)
      const UndoStack* undo = score->undoStack();
      QString key = QString("%1|%2|%3|%4|%5|%6|%7").arg(quintptr(library.get())).arg(SoundLib::routesGeneration())
                    .arg(Playback::generation()).arg(undo ? undo->getCurIdx() : -1).arg(undo ? undo->state() : -1)
                    .arg(quintptr(SoundLib::dynamicsCalibration().get())).arg(int(SoundLib::output()))
                    + "|" + score->metaTag(Playback::metaTag) + "|" + score->metaTag(PartPlaybackModes::metaTag)
                    + QString("|%1|%2").arg(score->parts().isEmpty() ? 0 : quintptr(score->parts().first())).arg(score->nstaves());
      bool gains = false;
      for (const auto& pd : delays) {
            const TrackDelays::Delays& d = pd.second;
            gains = gains || !d.rule.empty() || !d.even.empty();
            key += QString("|%1").arg(quintptr(pd.first));
            for (const std::map<QString, double>* m : { &d.rule, &d.even, &d.levels })
                  for (const auto& v : *m)
                        key += QString(",%1=%2").arg(v.first).arg(v.second, 0, 'g', 12);
            }
      if (!gains)
            return MasterTrim();
      static QMutex mutex;
      static std::map<const MasterScore*, std::pair<QString, MasterTrim>> cache;
      {
            QMutexLocker lock(&mutex);
            auto c = cache.find(score);
            if (c != cache.end() && c->second.first == key && undo && !undo->active())
                  return c->second.second;
      }
      MasterTrim t;
      {
            TrimGuard guard;
            t = computeTrim(score, *library, delays);
      }
      QMutexLocker lock(&mutex);
      if (cache.size() > 16)
            cache.clear();
      cache[score] = { key, t };
      return t;
      }

}     // namespace ContextLevel
}     // namespace Ms
