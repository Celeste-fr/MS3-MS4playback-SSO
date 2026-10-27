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
#include "lagprobe.h"
#include "evenlevels.h"
#include "recentresults.h"
#include <QMutex>
#include <QRegularExpression>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <set>
#include <tuple>

namespace Ms {
//---------------------------------------------------------
//   EditLag (recentresults.h)
//---------------------------------------------------------

static std::atomic<int> editLagOn { -1 };

bool EditLag::on()
      {
      int v = editLagOn.load();
      if (v < 0) {
            v = qEnvironmentVariableIntValue("MS_EDIT_LAG_OFF") ? 0 : 1;
            editLagOn = v;
            }
      return v == 1;
      }

void EditLag::setOn(bool on)
      {
      editLagOn = on ? 1 : 0;
      }

//---------------------------------------------------------
//   untrackedInputs (recentresults.h)
//---------------------------------------------------------

QString untrackedInputs(const MasterScore* score)
      {
      uint tags = 0;
      if (score)
            for (auto i = score->metaTags().cbegin(); i != score->metaTags().cend(); ++i)
                  tags = tags * 31u + (qHash(i.key()) ^ (qHash(i.value()) * 7u));
      return QString("%1|%2|%3|%4|%5|%6|%7|%8|%9").arg(EvenLevels::generation()).arg(tags)
             .arg(quintptr(SoundLib::current().get())).arg(SoundLib::routesGeneration()).arg(Playback::generation())
             .arg(int(SoundLib::output())).arg(int(SoundLib::active())).arg(int(PartPlaybackModes::libraryDefault()))
             .arg(quintptr(SoundLib::dynamicsCalibration().get()));
      }

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

const Slur* phraseSlur(const Chord* chord)
      {
      return slurOf(chord);
      }

SoundLib::Articulation::ContextKind plainKind(const Note* note, const Ms4::Dynamics& dynamics, int tickOffset)
      {
      const Note* first = note->firstTiedNote();
      for (const Ms4::ArtRef& r : Ms4::noteArticulations(first, Ms4::chordArticulations(first->chord(), dynamics, tickOffset)))
            if (r.art == Ms4::Art::Staccato || r.art == Ms4::Art::Staccatissimo)
                  return SoundLib::Articulation::ContextKind::STACCATO;
      return slurOf(note->chord()) ? SoundLib::Articulation::ContextKind::PLAIN_SLUR : SoundLib::Articulation::ContextKind::SINGLE;
      }

double gain(const SoundLib::Articulation& a, const Chord* chord, const Ms4::Dynamics& dynamics, int tickOffset,
            SoundLib::Articulation::ContextKind kind)
      {
      using Kind = SoundLib::Articulation::ContextKind;
      const std::vector<const Chord*> chords = kind == Kind::STACCATO || kind == Kind::SINGLE ? std::vector<const Chord*> { chord }
                                                                                           : phrase(chord);
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
      return a.contextLevelAt(cc, median(iois), median(pitches), kind);
      }

static std::map<const Part*, Volumes> compute(const MasterScore* score, const SoundLib::Library& library, bool context, int sectionMode,
                                              const std::map<const Part*, double>& ranges, bool mics);

// (realRangeTops renders the score, and the renderer reads the volumes: inside it, the volumes without the real range)
static thread_local int rangePass = 0;

static bool rangeNeedsRender(const MasterScore* score)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library || !realRange(score) || library->dynamicsCC < 0 || library->dynamicsCC == CTRL_EXPRESSION)
            return false;
      bool any = false;
      // (of several libraries every one's parts: the map's measurements are its own patches', SoundLib::allRoutes)
      for (const SoundLib::Route& r : SoundLib::allRoutes(score))
            any = any || hasRealRange(r.instrument);
      return any;
      }

// the library the master trim measures: the first of the score's libraries whose map gives a crest (its calibration's),
// else the current one (several libraries: SSO's parts whatever the global library is; contextlevel.h › Gain stage)
static std::shared_ptr<const SoundLib::Library> trimLibrary(const MasterScore* score)
      {
      const std::vector<std::shared_ptr<const SoundLib::Library>> libs = SoundLib::scoreLibraries(score);
      for (const std::shared_ptr<const SoundLib::Library>& l : libs)
            if (std::isfinite(l->crestDb))
                  return l;
      return libs.empty() ? SoundLib::current() : libs.front();
      }

//---------------------------------------------------------
//   RenderJob
//    a whole-score render of realRangeTops or the master trim one chunk at a time (warmStep: the edit lag), the very
//    loop each runs at once: one renderer, the chunks in order, the same context. A sync caller of the same inputs (key
//    and untrackedInputs) finishes it instead of starting over; other inputs drop it. The main thread's only
//---------------------------------------------------------

namespace {
struct RenderJob {
      const MasterScore* score { nullptr };
      QString key;
      QString untracked;
      bool trim { false };                    // the master trim's render (else realRangeTops')
      std::unique_ptr<MidiRenderer> midi;
      MidiRenderer::Chunk next;
      bool started { false };
      bool done { false };
      std::map<const Part*, double> tops;     // realRangeTops'
      std::map<const Part*, TrackDelays::Delays> delays;  // the trim's
      EventMap events;
      std::vector<MidiRenderer::LibTrace> trace;
      };
std::unique_ptr<RenderJob> rangeJob;
std::unique_ptr<RenderJob> trimJob;
}

static void step(RenderJob& j)
      {
      if (!j.started) {
            j.midi.reset(new MidiRenderer(const_cast<MasterScore*>(j.score)));
            j.midi->setMinChunkSize(10);
            if (j.trim)
                  j.midi->setLibraryTrace(&j.trace);
            else
                  j.midi->setRealRangeTops(&j.tops);
            j.next = j.midi->getChunkAt(0);
            j.started = true;
            }
      if (j.next) {
            SynthesizerState ss;
            MidiRenderer::Context ctx { ss };
            ctx.metronome = false;
            EventMap part;
            j.midi->renderChunk(j.next, &part, ctx);
            if (j.trim)
                  j.events.insert(part.begin(), part.end());
            j.next = j.midi->getChunkAt(j.next.utick2());
            }
      j.done = !j.next;
      }

static void rangeStep(RenderJob& j)
      {
      ++rangePass;
      {
            LagProbe::Scope probe("realRangeTops render (step)");
            step(j);
      }
      --rangePass;
      }

std::map<const Part*, double> realRangeTops(const MasterScore* score)
      {
      std::map<const Part*, double> tops;
      if (!rangeNeedsRender(score))
            return tops;
      ++rangePass;
      {
            LagProbe::Scope probe("realRangeTops render");
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

// realRangeTops for volumes(key): the warm-up's job when it is for the same inputs (finished here), else at once
static std::map<const Part*, double> rangeTopsFor(const MasterScore* score, const QString& key, const QString& untracked)
      {
      if (EditLag::on() && rangeJob && rangeJob->score == score && rangeJob->key == key && rangeJob->untracked == untracked) {
            std::unique_ptr<RenderJob> j = std::move(rangeJob);
            while (!j->done)
                  rangeStep(*j);
            return j->tops;
            }
      return realRangeTops(score);
      }

// what volumes() computes for the score now (and its cache key); any false: nothing (empty)
namespace {
struct VolumesCall {
      bool context { false };
      int sectionMode { 0 };
      bool section { false };
      bool real { false };
      bool mics { false };
      bool any { false };
      QString key;
      };
}

static VolumesCall volumesCall(const MasterScore* score, const SoundLib::Library& library)
      {
      VolumesCall v;
      v.context = on(score) && Playback::on("slurs/performance", score);
      v.sectionMode = sectionBalanceMode(score);
      v.section = v.sectionMode > 0;
      v.real = realRange(score) && !rangePass;
      bool micLaws = !library.micFader.empty();       // (of several libraries: any one's)
      if (!micLaws)
            for (const std::shared_ptr<const SoundLib::Library>& l : SoundLib::scoreLibraries(score))
                  micLaws = micLaws || !l->micFader.empty();
      v.mics = micCompensation(score) && micLaws;
      v.any = v.context || v.section || v.real || v.mics;
      if (!v.any)
            return v;
      const UndoStack* undo = score->undoStack();
      v.key = QString("%1|%2|%3|%4|%5|%6|%7").arg(quintptr(&library)).arg(SoundLib::routesGeneration())
              .arg(Playback::generation()).arg(undo ? undo->getCurIdx() : -1).arg(undo ? undo->state() : -1)
              .arg(score->metaTag(Playback::metaTag)).arg(score->metaTag(PartPlaybackModes::metaTag))
              + QString("|%1|%2|%3%4").arg(score->parts().isEmpty() ? 0 : quintptr(score->parts().first())).arg(score->nstaves())
              .arg(int(v.context)).arg(v.sectionMode) + QString("|%1|%2").arg(int(v.real)).arg(int(v.mics))
              + (v.mics ? "|" + score->metaTag(Automation::metaTag) : QString());
      return v;
      }

static QMutex volumesMutex;
// (the last few states per score: an undo or redo back to one is a hit; the key holds the undo state, so an entry is
// only ever the same inputs' result)
static std::map<const MasterScore*, RecentResults<std::map<const Part*, Volumes>>> volumesCache;

// (read often: TrackDelays::read, the host's mixer every 50 ms; kept per score until an edit, an undo, another library,
// other routes or settings)
std::map<const Part*, Volumes> volumes(const MasterScore* score)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!score || !library)
            return std::map<const Part*, Volumes>();
      const VolumesCall vc = volumesCall(score, *library);
      if (!vc.any)
            return std::map<const Part*, Volumes>();
      const UndoStack* undo = score->undoStack();
      const QString& key = vc.key;
      {
            QMutexLocker lock(&volumesMutex);
            auto c = volumesCache.find(score);
            const std::map<const Part*, Volumes>* hit = c != volumesCache.end() && undo && !undo->active() ? c->second.find(key, untrackedInputs(score)) : nullptr;
            if (hit)
                  return *hit;
      }
      const QString untracked = untrackedInputs(score);         // (as they were when computed)
      // (the real range's headroom renders the score, which reads the volumes again: not under the lock)
      const std::map<const Part*, double> ranges = vc.real ? rangeTopsFor(score, key, untracked) : std::map<const Part*, double>();
      if (!vc.context && !vc.section && ranges.empty() && !vc.mics)
            return std::map<const Part*, Volumes>();
      QMutexLocker lock(&volumesMutex);
      if (volumesCache.size() > 16)
            volumesCache.clear();
      LagProbe::Scope probe("ContextLevel::volumes compute");
      std::map<const Part*, Volumes> out = compute(score, *library, vc.context, vc.sectionMode, ranges, vc.mics);
      volumesCache[score].put(key, untracked, out);
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
                  const SoundLib::Library& lib = p->library ? *p->library : library;    // (the patch's own map's laws)
                  double top = micOffsetDb(*p, lib, v.at(0));
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
                                    top = std::max(top, micOffsetDb(*p, lib, v.at(t)));
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
      // (several libraries: every one's parts, each patch by its own map's measurements; a map without them, Vial's, none)
      const std::vector<SoundLib::Route> routes = SoundLib::severalLibraries(score) ? SoundLib::allRoutes(score)
                                                                                     : SoundLib::routes(score, library);
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
            const bool plain = Playback::on("slurs/plainPerformance", score);
            for (const Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                  for (int track = part->startTrack(); track < part->endTrack(); ++track) {
                        const Element* e = seg->element(track);
                        if (!e || !e->isChord())
                              continue;
                        const Slur* s = slurOf(toChord(e));
                        // ([slurs] plainPerformance: every chord plays on it, a staccato or a note outside a slur alone,
                        // by its own grid; a superset again: "non performance" and techniques it doesn't play counted)
                        SoundLib::Articulation::ContextKind kind = SoundLib::Articulation::ContextKind::SLUR;
                        if (plain && !toChord(e)->notes().empty()) {
                              kind = plainKind(toChord(e)->notes().front(), dynamics, 0);
                              if (kind != SoundLib::Articulation::ContextKind::PLAIN_SLUR)
                                    s = nullptr;
                              }
                        if (s ? !done.insert(s).second : !plain)
                              continue;
                        const double g = gain(*a, toChord(e), dynamics, 0, kind);
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

// (computeTrim renders the score: false when it returns before, nothing to render)
static bool trimNeedsRender(const SoundLib::Library& library, const std::map<const Part*, TrackDelays::Delays>& delays)
      {
      bool any = false;
      for (const auto& d : delays)
            any = any || !d.second.rule.empty() || !d.second.even.empty();
      if (!any || library.dynamicsCC == CTRL_EXPRESSION)
            return false;
      const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::calibrationFor(&library);
      return cal && !cal->patches().empty() && std::isfinite(library.crestDb);
      }

// rendered, renderedTrace: the score's render as below, made before (RenderJob, the warm-up)
static MasterTrim computeTrim(const MasterScore* score, const SoundLib::Library& library,
                              const std::map<const Part*, TrackDelays::Delays>& delays, const EventMap* rendered = nullptr,
                              const std::vector<MidiRenderer::LibTrace>* renderedTrace = nullptr)
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
      const std::shared_ptr<const SoundLib::DynamicsCalibration> cal = SoundLib::calibrationFor(&library);
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
      EventMap ownEvents;
      std::vector<MidiRenderer::LibTrace> ownTrace;
      const EventMap& events = rendered ? *rendered : ownEvents;
      const std::vector<MidiRenderer::LibTrace>& trace = renderedTrace ? *renderedTrace : ownTrace;
      if (!rendered) {
            EventMap& events = ownEvents;
            std::vector<MidiRenderer::LibTrace>& trace = ownTrace;
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
      std::shared_ptr<const SoundLib::Library> library = trimLibrary(score);
      if (!score || !library)
            return MasterTrim();
      TrimGuard guard;
      return computeTrim(score, *library, TrackDelays::read(score));
      }

// masterTrim's inputs now: the gains (TrackDelays, untrimmed) and the cache key; false: no gains, no trim
static bool trimCall(const MasterScore* score, const SoundLib::Library& library, std::map<const Part*, TrackDelays::Delays>* delays,
                     QString* key)
      {
      {
            TrimGuard guard;
            *delays = TrackDelays::read(score);
      }
      // (the key: what the render and the gains depend on; the gains themselves, as even levels change without an undo step)
      const UndoStack* undo = score->undoStack();
      *key = QString("%1|%2|%3|%4|%5|%6|%7").arg(quintptr(&library)).arg(SoundLib::routesGeneration())
             .arg(Playback::generation()).arg(undo ? undo->getCurIdx() : -1).arg(undo ? undo->state() : -1)
             .arg(quintptr(SoundLib::dynamicsCalibration().get())).arg(int(SoundLib::output()))
             + "|" + score->metaTag(Playback::metaTag) + "|" + score->metaTag(PartPlaybackModes::metaTag)
             + QString("|%1|%2").arg(score->parts().isEmpty() ? 0 : quintptr(score->parts().first())).arg(score->nstaves());
      bool gains = false;
      for (const auto& pd : *delays) {
            const TrackDelays::Delays& d = pd.second;
            gains = gains || !d.rule.empty() || !d.even.empty();
            *key += QString("|%1").arg(quintptr(pd.first));
            for (const std::map<QString, double>* m : { &d.rule, &d.even, &d.levels })
                  for (const auto& v : *m)
                        *key += QString(",%1=%2").arg(v.first).arg(v.second, 0, 'g', 12);
            }
      return gains;
      }

static QMutex trimMutex;
static std::map<const MasterScore*, RecentResults<MasterTrim>> trimCache;       // (as volumes': the last few states)

static void trimStep(RenderJob& j)
      {
      LagProbe::Scope probe("masterTrim render (step)");
      TrimGuard guard;
      step(j);
      }

MasterTrim masterTrim(const MasterScore* score)
      {
      std::shared_ptr<const SoundLib::Library> library = score ? trimLibrary(score) : nullptr;
      // (none inside the real range's render either: it needs no gains, and its volumes lack R)
      if (!score || !library || trimPass || rangePass)
            return MasterTrim();
      std::map<const Part*, TrackDelays::Delays> delays;
      QString key;
      if (!trimCall(score, *library, &delays, &key))
            return MasterTrim();
      const UndoStack* undo = score->undoStack();
      {
            QMutexLocker lock(&trimMutex);
            auto c = trimCache.find(score);
            const MasterTrim* hit = c != trimCache.end() && undo && !undo->active() ? c->second.find(key, untrackedInputs(score)) : nullptr;
            if (hit)
                  return *hit;
      }
      const QString untracked = untrackedInputs(score);         // (as they were when computed)
      MasterTrim t;
      if (EditLag::on() && trimJob && trimJob->score == score && trimJob->key == key && trimJob->untracked == untracked) {
            // (the warm-up's render of these very inputs: finished here)
            std::unique_ptr<RenderJob> j = std::move(trimJob);
            while (!j->done)
                  trimStep(*j);
            TrimGuard guard;
            t = computeTrim(score, *library, delays, &j->events, &j->trace);
            }
      else {
            LagProbe::Scope probe("masterTrim render");
            TrimGuard guard;
            t = computeTrim(score, *library, delays);
            }
      QMutexLocker lock(&trimMutex);
      if (trimCache.size() > 16)
            trimCache.clear();
      trimCache[score].put(key, untracked, t);
      return t;
      }

//---------------------------------------------------------
//   warmStep / dropWarmup (contextlevel.h)
//---------------------------------------------------------

bool warmStep(const MasterScore* score)
      {
      std::shared_ptr<const SoundLib::Library> library = SoundLib::current();
      if (!EditLag::on() || !score || !library || trimPass || rangePass)
            return true;
      const UndoStack* undo = score->undoStack();
      if (!undo || undo->active())
            return true;                  // (inside a command: nothing kept now)
      const QString untracked = untrackedInputs(score);
      // the volumes (their real range's render)
      const VolumesCall vc = volumesCall(score, *library);
      static QString volumesTried;      // (volumes() asked for these inputs: a result it doesn't keep, none to keep, is warm too)
      if (vc.any && volumesTried != vc.key + "#" + untracked) {
            bool warm = false;
            {
                  QMutexLocker lock(&volumesMutex);
                  auto c = volumesCache.find(score);
                  warm = c != volumesCache.end() && c->second.find(vc.key, untracked);
            }
            if (!warm) {
                  if (vc.real && rangeNeedsRender(score)) {
                        if (!rangeJob || rangeJob->score != score || rangeJob->key != vc.key || rangeJob->untracked != untracked) {
                              rangeJob.reset(new RenderJob);
                              rangeJob->score = score;
                              rangeJob->key = vc.key;
                              rangeJob->untracked = untracked;
                              }
                        if (!rangeJob->done) {
                              rangeStep(*rangeJob);
                              return false;
                              }
                        }
                  volumes(score);         // (with the job's tops: their compute only)
                  volumesTried = vc.key + "#" + untracked;
                  return false;
                  }
            }
      // the master trim (its render)
      std::map<const Part*, TrackDelays::Delays> delays;
      QString key;
      const std::shared_ptr<const SoundLib::Library> trimLib = trimLibrary(score);
      if (!trimCall(score, *trimLib, &delays, &key))
            return true;
      {
            QMutexLocker lock(&trimMutex);
            auto c = trimCache.find(score);
            if (c != trimCache.end() && c->second.find(key, untracked))
                  return true;
      }
      if (trimNeedsRender(*trimLib, delays)) {
            if (!trimJob || trimJob->score != score || trimJob->key != key || trimJob->untracked != untracked) {
                  trimJob.reset(new RenderJob);
                  trimJob->score = score;
                  trimJob->key = key;
                  trimJob->untracked = untracked;
                  trimJob->trim = true;
                  }
            if (!trimJob->done) {
                  trimStep(*trimJob);
                  return false;
                  }
            }
      masterTrim(score);            // (with the job's render: the estimate only)
      return false;
      }

void dropWarmup()
      {
      rangeJob.reset();
      trimJob.reset();
      }

}     // namespace ContextLevel
}     // namespace Ms
