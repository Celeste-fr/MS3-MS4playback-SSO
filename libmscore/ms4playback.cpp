//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Ms4: MuseScore 4.7.5's note model (see ms4playback.h).
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 3.
//=============================================================================

#include "ms4playback.h"

#include <cmath>

#include "arpeggio.h"
#include "articulation.h"
#include "chord.h"
#include "chordline.h"
#include "dynamic.h"
#include "hairpin.h"
#include "instrument.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "score.h"
#include "segment.h"
#include "slur.h"
#include "staff.h"
#include "sym.h"
#include "tremolo.h"

namespace Ms {
namespace Ms4 {

//---------------------------------------------------------
//   tables
//---------------------------------------------------------

static const Pattern* pattern(Family f, Art a)
      {
      static const Pattern* table[5][int(Art::COUNT)] = {};
      static bool init = false;
      if (!init) {
            init = true;
            for (const ProfileEntry& e : PROFILE)
                  table[int(e.family)][int(e.art)] = &e.p;
            }
      return table[int(f)][int(a)];
      }

static const int* pitchPattern(Family f, Art a)
      {
      for (const PitchEntry& e : PITCH)
            if (e.family == f && e.art == a)
                  return e.curve;
      return nullptr;
      }

// FluidSequencer's BEND_SUPPORTED_TYPES (those MS3 can show)
static bool isBendType(Art a)
      {
      switch (a) {
            case Art::BrassBend:
            case Art::Fall:
            case Art::QuickFall:
            case Art::Doit:
            case Art::Plop:
            case Art::Scoop:
                  return true;
            default:
                  return false;
            }
      }

int pitchBendLevel(int pitchLevel)
      {
      static const int SEMITONE_STEP = 4096 / 12;
      const float steps = pitchLevel / 50.0f;                 // PITCH_LEVEL_STEP
      const int offset = int(steps * SEMITONE_STEP);
      return qBound(0, 8192 + offset, 16383);
      }

Family family(const Instrument* instrument)
      {
      static QHash<QString, Family> map;
      if (map.isEmpty()) {
            for (const FamilyEntry& e : FAMILY)
                  map.insert(QString::fromLatin1(e.id), e.family);
            }
      return map.value(instrument->instrumentId(), Family::Keyboards);
      }

//---------------------------------------------------------
//   sounds
//---------------------------------------------------------

static const SoundEntry* soundEntry(const Instrument* instrument)
      {
      static QHash<QString, const SoundEntry*> byId;
      static QHash<QString, QString> templateOf;
      if (byId.isEmpty()) {
            for (const SoundEntry& e : SOUNDS)
                  byId.insert(QString::fromLatin1(e.id), &e);
            for (const TemplateEntry& e : TEMPLATE_OF)
                  templateOf.insert(QString::fromLatin1(e.musicXmlId), QString::fromLatin1(e.templateId));
            }
      QString id = instrument->getId();
      if (id.isEmpty() || !byId.contains(id))
            id = templateOf.value(instrument->instrumentId());
      return byId.value(id, nullptr);
      }

// the MuseScore 3 channel name a technique's sound would naturally live on
static const char* channelNameOf(Art a)
      {
      switch (a) {
            case Art::Pizzicato:
            case Art::SnapPizzicato: return "pizzicato";
            case Art::Mute:
            case Art::PalmMute:      return "mute";
            case Art::Tremolo8th:
            case Art::Tremolo16th:
            case Art::Tremolo32nd:
            case Art::Tremolo64th:   return "tremolo";
            case Art::Harmonic:      return "harmonics";
            case Art::JazzTone:      return "jazz";
            case Art::Distortion:    return "distortion";
            case Art::Overdrive:     return "overdriven";
            default:                 return "";
            }
      }

Sounds sounds(const Instrument* instrument)
      {
      Sounds s;
      const SoundEntry* e = soundEntry(instrument);
      const int n = instrument->channel().size();
      const int stdBank = e ? e->bank : 0;
      const int stdProgram = e ? e->program : 0;             // MS4: Program(0, 0) when unmapped
      for (int i = 0; i < n; ++i)
            s.channelSlots.push_back({ i, stdBank, stdProgram });
      if (!e || n < 2)
            return s;

      // the distinct technique presets, in the mapping's order
      std::vector<std::pair<int, int>> presets;
      for (int k = 0; k < e->nArts; ++k) {
            std::pair<int, int> p(e->arts[k].bank, e->arts[k].program);
            if (p != std::make_pair(stdBank, stdProgram) && std::find(presets.begin(), presets.end(), p) == presets.end())
                  presets.push_back(p);
            }
      std::vector<int> slotOfPreset(presets.size(), -1);
      std::vector<bool> used(n, false);
      used[0] = true;
      // a channel whose name fits one of the preset's techniques first …
      for (size_t pi = 0; pi < presets.size(); ++pi) {
            for (int k = 0; k < e->nArts && slotOfPreset[pi] < 0; ++k) {
                  if (std::make_pair(e->arts[k].bank, e->arts[k].program) != presets[pi])
                        continue;
                  const QString want = QString::fromLatin1(channelNameOf(e->arts[k].art));
                  for (int i = 1; i < n && !want.isEmpty(); ++i) {
                        if (!used[i] && instrument->channel(i)->name() == want) {
                              used[i] = true;
                              slotOfPreset[pi] = i;
                              break;
                              }
                        }
                  }
            }
      // … then any channel still free
      for (size_t pi = 0; pi < presets.size(); ++pi) {
            for (int i = 1; i < n && slotOfPreset[pi] < 0; ++i) {
                  if (!used[i]) {
                        used[i] = true;
                        slotOfPreset[pi] = i;
                        }
                  }
            if (slotOfPreset[pi] >= 0) {
                  s.channelSlots[slotOfPreset[pi]].bank = presets[pi].first;
                  s.channelSlots[slotOfPreset[pi]].program = presets[pi].second;
                  }
            }
      for (int k = 0; k < e->nArts; ++k) {
            std::pair<int, int> p(e->arts[k].bank, e->arts[k].program);
            if (p == std::make_pair(stdBank, stdProgram)) {
                  s.artSlot.push_back({ e->arts[k].art, 0 });
                  continue;
                  }
            auto it = std::find(presets.begin(), presets.end(), p);
            const int slot = slotOfPreset[it - presets.begin()];
            if (slot >= 0)
                  s.artSlot.push_back({ e->arts[k].art, slot });
            }
      return s;
      }

// ChannelMap::resolveChannelForEvent: Standard (or nothing) -> the standard preset; otherwise the
// first of the note's articulations (enum order) that has a preset of its own
int Sounds::slotFor(const std::vector<Art>& noteArts) const
      {
      if (noteArts.empty() || std::find(noteArts.begin(), noteArts.end(), Art::Standard) != noteArts.end())
            return 0;
      for (Art a : noteArts)
            for (const auto& as : artSlot)
                  if (as.first == a)
                        return as.second;
      return 0;
      }

//---------------------------------------------------------
//   curve helpers
//    curves are 11 points at 0, 1000 … 10000
//---------------------------------------------------------

struct Peak { int pos; int val; };

// ValuesCurve::amplitudeValuePoint: the first point with the largest |value|
static Peak peak(const int* curve)
      {
      int bi = 0;
      for (int i = 1; i < 11; ++i)
            if (std::abs(curve[i]) > std::abs(curve[bi]))
                  bi = i;
      return { bi * 1000, curve[bi] };
      }

//---------------------------------------------------------
//   expressionLevel
//    FluidSequencer::expressionLevel
//---------------------------------------------------------

int expressionLevel(int level)
      {
      static const int MIN_LEVEL = 3250;      // ppp
      static const int MAX_LEVEL = 6750;      // fff
      if (level <= MIN_LEVEL)
            return 16;
      if (level >= MAX_LEVEL)
            return 127;
      float steps = (level - MIN_LEVEL) / float(STEP);
      if (level == NATURAL)
            steps -= 0.5f;
      int r = int(std::round(16 + steps * 16));
      return qBound(16, r, 127);
      }

//---------------------------------------------------------
//   note
//    ArticulationMap::preCalculateAverageData, NoteEvent::calculateExpressionCurve,
//    FluidSequencer::noteVelocity
//---------------------------------------------------------

NoteResult note(Family fam, const std::vector<ArtRef>& arts, int D, bool snd)
      {
      struct P { Art art; const Pattern* p; };
      std::vector<P> pats;
      for (const ArtRef& a : arts) {
            bool seen = false;
            for (const P& q : pats)
                  if (q.art == a.art)
                        seen = true;
            if (seen)
                  continue;
            const Pattern* p = pattern(fam, a.art);
            if (!p && a.fallback)
                  p = pattern(fam, Art::Standard);
            if (p)
                  pats.push_back({ a.art, p });
            }
      if (pats.empty())                                     // NoteArticulationsParser: nothing -> Standard
            pats.push_back({ Art::Standard, pattern(fam, Art::Standard) });
      std::sort(pats.begin(), pats.end(), [](const P& a, const P& b) { return a.art < b.art; });

      // average (ArticulationMap::preCalculateAverageData)
      int dur, ts, maxAmp;
      int curve[11];
      if (pats.size() == 1) {
            const Pattern* p = pats[0].p;
            dur = p->dur;
            ts = p->ts;
            maxAmp = peak(p->curve).val;
            std::copy(p->curve, p->curve + 11, curve);
            }
      else {
            const int n = int(pats.size());
            long sumDur = 0, sumTs = 0, sumAmp = 0;
            int tsCount = 0, dynCount = 0;
            long sumCurve[11] = {};
            for (const P& q : pats) {
                  const int amp = peak(q.p->curve).val;
                  sumDur += q.p->dur;
                  sumTs += q.p->ts;
                  sumAmp += amp;                              // summed for every articulation …
                  if (q.p->ts != 0)
                        ++tsCount;
                  if (amp != NATURAL) {                       // … but divided by the "meaningful" ones only
                        ++dynCount;
                        for (int k = 0; k < 11; ++k)
                              sumCurve[k] += q.p->curve[k];
                        }
                  }
            dur = int(sumDur / n);
            ts = tsCount ? int(sumTs / tsCount) : 0;
            if (dynCount > 0) {
                  maxAmp = int(sumAmp / dynCount);
                  for (int k = 0; k < 11; ++k)
                        curve[k] = int(sumCurve[k] / dynCount);
                  }
            else {
                  maxAmp = peak(pats[0].p->curve).val;
                  std::copy(pats[0].p->curve, pats[0].p->curve + 11, curve);
                  }
            }

      // expression curve for the dynamic level D (NoteEvent::calculateExpressionCurve)
      const float factor = (maxAmp - NATURAL) / float(STEP);
      const int diff = int(std::max(0, STEP) * factor);
      const int actual = D + diff;
      if (actual != maxAmp) {
            const float ratio = actual / float(maxAmp);
            for (int k = 0; k < 11; ++k)
                  curve[k] = int(std::round(curve[k] * ratio));
            }

      NoteResult r;
      r.dur = dur;
      r.ts = ts;

      // pitch curve (ArticulationMap: averaged over the articulations that bend)
      {
            int sum[11] = {};
            int count = 0;
            for (const P& q : pats) {
                  const int* pc = pitchPattern(fam, q.art);
                  if (!pc)
                        continue;
                  bool meaningful = false;
                  for (int k = 0; k < 11; ++k)
                        meaningful |= pc[k] != 0;
                  if (!meaningful)
                        continue;
                  ++count;
                  for (int k = 0; k < 11; ++k)
                        sum[k] += pc[k];
                  }
            for (const P& q : pats)
                  r.bend |= isBendType(q.art);
            if (count > 0)
                  for (int k = 0; k < 11; ++k)
                        r.pitchCurve[k] = sum[k] / count;
            }
      const Peak pk = peak(curve);
      if (snd) {
            // velocityFraction, as a 16-bit velocity scaled down to 7 bits
            float f = 1.0f;
            if (pk.pos != 0)
                  f = (std::log10(pk.val / float(pk.pos)) + 1.0f) / 2.0f;
            const int v16 = qBound(0, int(std::round(f * 65535.0f)), 65535);
            r.velocity = v16 >> 9;
            }
      else
            r.velocity = expressionLevel(pk.val);
      for (const P& q : pats)
            r.arts.push_back(q.art);
      return r;
      }

//---------------------------------------------------------
//   dynamic levels (mpetypes.h DynamicType, expressionutils.h), by MS3's dynamic tag.
//    By tag, not by Dynamic::Type: MuseScore 3.7's enum lacks "pf" that its dynList has, so
//    every type after "fp" is off by one there (an sfz reads as SFF).
//---------------------------------------------------------

static int ordinaryLevel(const QString& tag)
      {
      static const QHash<QString, int> levels {
            { "n", 0 }, { "pppppp", 1750 }, { "ppppp", 2250 }, { "pppp", 2750 }, { "ppp", 3250 }, { "pp", 3750 },
            { "p", 4250 }, { "mp", 4750 }, { "mf", 5250 }, { "f", 5750 }, { "ff", 6250 }, { "fff", 6750 },
            { "ffff", 7250 }, { "fffff", 7750 }, { "ffffff", 8250 } };
      return levels.value(tag, -1);
      }

static int singleNoteLevel(const QString& tag)
      {
      static const QHash<QString, int> levels {
            { "sf", 5750 }, { "sfz", 5750 }, { "sff", 6250 }, { "sffz", 6250 }, { "sfff", 6750 }, { "sfffz", 6750 },
            { "rfz", 5750 }, { "rf", 5750 } };
      return levels.value(tag, -1);
      }

static bool compound(const QString& tag, int& from, int& to)
      {
      if (tag == "fp" || tag == "sfp") { from = 5750; to = 4250; return true; }
      if (tag == "pf")                 { from = 4250; to = 5750; return true; }
      if (tag == "sfpp")               { from = 5750; to = 3750; return true; }
      return false;
      }

//---------------------------------------------------------
//   easingValueCurve
//    TConv::easingValueCurve (int version): tick offset -> value offset
//---------------------------------------------------------

static float easingFactor(float x, ChangeMethod method)
      {
      switch (method) {
            case ChangeMethod::NORMAL:      return x;
            case ChangeMethod::EASE_IN:     return 1 - std::sqrt(1 - std::pow(x, 2));
            case ChangeMethod::EASE_OUT:    return std::sqrt(1 - std::pow(x - 1, 2));
            case ChangeMethod::EASE_IN_OUT:
                  if (x < 0.5f)
                        return (1.f - std::sqrt(1 - std::pow(2 * x, 2))) / 2;
                  return (std::sqrt(1.f - std::pow(-2 * x + 2, 2)) + 1) / 2;
            case ChangeMethod::EXPONENTIAL:
                  if (qFuzzyCompare(x, 1.f))
                        return x;
                  return 1.f - std::pow(2, -10 * x);
            }
      return 1.f;
      }

static std::map<int, int> easingValueCurve(int ticksDuration, int stepsCount, int amplitude, ChangeMethod method)
      {
      std::map<int, int> result;
      if (stepsCount <= 0)
            return result;
      const float durationStep = float(ticksDuration) / float(stepsCount);
      for (int i = 0; i <= stepsCount; ++i)
            result.emplace(int(i * durationStep), int(easingFactor(i / float(stepsCount), method) * amplitude));
      return result;
      }

//---------------------------------------------------------
//   Dynamics
//---------------------------------------------------------

int Dynamics::appliable(int tick) const
      {
      auto it = _levels.upper_bound(tick);
      if (it == _levels.begin())
            return NATURAL;
      return std::prev(it)->second;
      }

int Dynamics::nominal(int tick) const
      {
      auto it = _levels.find(tick);
      return it == _levels.end() ? NATURAL : it->second;
      }

// PlaybackContext::updateDynamicMap
void Dynamics::addDynamic(Score*, Dynamic* dynamic)
      {
      Segment* segment = dynamic->segment();
      if (!segment)
            return;
      const int tick = segment->tick().ticks();
      const QString type = dynamic->dynamicTypeName();

      int level = ordinaryLevel(type);
      if (level >= 0) {
            apply(tick, level);
            return;
            }
      level = singleNoteLevel(type);
      if (level >= 0) {
            const int prev = appliable(tick);
            apply(tick, level);
            _subito[tick] = true;
            if (Segment* next = segment->next())
                  apply(next->tick().ticks(), prev);
            return;
            }
      int from, to;
      if (compound(type, from, to)) {
            _subito[tick] = type.startsWith("s");           // sfp, sfpp are sf-type too
            const int length = dynamic->velocityChangeLength().ticks();
            for (const auto& p : easingValueCurve(length, 6, to - from, ChangeMethod::NORMAL))
                  apply(tick + p.first, from + p.second);
            }
      }

// the dynamic marking at a segment of the hairpin's staff, if any
static Dynamic* dynamicAt(Segment* segment, int track)
      {
      if (!segment)
            return nullptr;
      const int strack = track - track % VOICES;
      Element* e = segment->findAnnotation(ElementType::DYNAMIC, strack, strack + VOICES - 1);
      return e ? toDynamic(e) : nullptr;
      }

static int levelOf(const QString& type, bool atEnd)
      {
      int l = ordinaryLevel(type);
      if (l >= 0)
            return l;
      l = singleNoteLevel(type);
      if (l >= 0)
            return l;
      int from, to;
      if (compound(type, from, to))
            return atEnd ? from : to;
      return NATURAL;
      }

// PlaybackContext::handleHairpin
void Dynamics::addHairpin(Score* score, Hairpin* hairpin)
      {
      int spannerFrom = hairpin->tick().ticks();
      int spannerTo = spannerFrom + std::abs(hairpin->ticks().ticks());

      // a hairpin starting with a compound dynamic starts once the transition is over
      Dynamic* startDynamic = dynamicAt(score->tick2segment(hairpin->tick(), true, SegmentType::ChordRest), hairpin->track());
      if (startDynamic) {
            int from, to;
            if (compound(startDynamic->dynamicTypeName(), from, to))
                  spannerFrom += startDynamic->velocityChangeLength().ticks();
            }

      // MS3 hairpins carry no start dynamic of their own: the level in force
      const int levelFrom = appliable(spannerFrom);

      Dynamic* endDynamic = dynamicAt(score->tick2segment(Fraction::fromTicks(spannerTo), true, SegmentType::ChordRest), hairpin->track());
      const int nominalLevelTo = endDynamic ? levelOf(endDynamic->dynamicTypeName(), true) : NATURAL;
      const bool hasNominalLevelTo = nominalLevelTo != NATURAL;
      const bool isCrescendo = hairpin->isCrescendo();
      const bool useNominalLevelTo = hasNominalLevelTo && (isCrescendo ? nominalLevelTo > levelFrom : nominalLevelTo < levelFrom);
      const int levelTo = useNominalLevelTo ? nominalLevelTo : levelFrom + (isCrescendo ? STEP : -STEP);

      const int levelAtEnd = nominal(spannerTo);
      const bool hasDynamicAtEndTick = levelAtEnd != NATURAL;
      if (hasDynamicAtEndTick && levelAtEnd != levelTo)
            spannerTo -= 1;                                   // Fraction::eps()

      const int durationTicks = spannerTo - spannerFrom;
      if (durationTicks <= 0)
            return;

      const int steps = std::max(durationTicks / (DIVISION / 4), 24);
      for (const auto& p : easingValueCurve(durationTicks, steps, levelTo - levelFrom, hairpin->veloChangeMethod()))
            apply(spannerFrom + p.first, levelFrom + p.second);

      if (hasNominalLevelTo && !useNominalLevelTo && !hasDynamicAtEndTick)
            apply(spannerTo, nominalLevelTo);
      }

// PlaybackContext::update: the part's dynamic markings in score order, then its hairpins
void Dynamics::build(Score* score, Part* part)
      {
      _levels.clear();
      _subito.clear();
      const int strack = part->startTrack();
      const int etrack = part->endTrack();

      for (Segment* s = score->firstSegment(SegmentType::All); s; s = s->next1()) {
            for (Element* e : s->annotations()) {
                  if (e->isDynamic() && e->track() >= strack && e->track() < etrack)
                        addDynamic(score, toDynamic(e));
                  }
            }

      std::vector<Hairpin*> hairpins;
      for (const auto& p : score->spanner()) {
            Spanner* sp = p.second;
            if (sp->isHairpin() && sp->track() >= strack && sp->track() < etrack) {
                  Staff* st = sp->staff();
                  if (st && !st->primaryStaff())
                        continue;                             // linked staves
                  hairpins.push_back(toHairpin(sp));
                  }
            }
      for (Hairpin* h : hairpins)
            addHairpin(score, h);

      if (!_levels.count(0))
            _levels.emplace(0, NATURAL);
      if (qEnvironmentVariableIsSet("MS4_DEBUG_DYNAMICS")) {
            for (const auto& l : _levels)
                  qDebug("MS4DYN part %s tick %d level %d", qPrintable(part->partName()), l.first, l.second);
            }
      }

//---------------------------------------------------------
//   articulations
//---------------------------------------------------------

// SymbolsMetaParser: the symbols MS3 can show
static void symbolTypes(SymId id, std::vector<ArtRef>& out)
      {
      QString s = Sym::id2name(id);
      if (s.endsWith("Above"))
            s.chop(5);
      else if (s.endsWith("Below"))
            s.chop(5);
      auto add = [&out](std::initializer_list<Art> l) { for (Art a : l) out.push_back({ a, true }); };
      if (s == "articAccent") add({ Art::Accent });
      else if (s == "articAccentStaccato") add({ Art::Accent, Art::Staccato });
      else if (s == "articMarcato") add({ Art::Marcato });
      else if (s == "articMarcatoStaccato") add({ Art::Marcato, Art::Staccato });
      else if (s == "articMarcatoTenuto") add({ Art::Marcato, Art::Tenuto });
      else if (s == "articSoftAccent") add({ Art::SoftAccent });
      else if (s == "articSoftAccentStaccato") add({ Art::SoftAccent, Art::Staccato });
      else if (s == "articSoftAccentTenuto") add({ Art::SoftAccent, Art::Tenuto });
      else if (s == "articSoftAccentTenutoStaccato") add({ Art::SoftAccent, Art::Tenuto, Art::Staccato });
      else if (s == "articStaccatissimo" || s == "articStaccatissimoStroke" || s == "articStaccatissimoWedge") add({ Art::Staccatissimo });
      else if (s == "articStaccato") add({ Art::Staccato });
      else if (s == "articTenuto") add({ Art::Tenuto });
      else if (s == "articTenutoStaccato") add({ Art::Tenuto, Art::Staccato });
      else if (s == "articTenutoAccent") add({ Art::Tenuto, Art::Accent });
      else if (s == "stringsHalfHarmonic" || s == "stringsHarmonic") add({ Art::Harmonic });
      else if (s == "stringsMuteOn" || s == "brassMuteHalfClosed" || s == "brassMuteClosed"
               || s == "brassHarmonMuteStemHalfRight" || s == "brassHarmonMuteStemHalfLeft" || s == "brassHarmonMuteClosed") add({ Art::Mute });
      else if (s == "brassHarmonMuteStemOpen" || s == "brassMuteOpen" || s == "stringsMuteOff") add({ Art::Open });
      else if (s == "pluckedLeftHandPizzicato") add({ Art::Pizzicato });
      else if (s == "pluckedSnapPizzicato") add({ Art::SnapPizzicato });
      else if (s == "stringsUpBow") add({ Art::UpBow });
      else if (s == "stringsDownBow") add({ Art::DownBow });
      else if (s == "stringsJete") add({ Art::Jete });
      else if (s == "brassBend") add({ Art::BrassBend });
      }

// playing technique in force: MuseScore 3 switches the channel (staff text), MS4 converts that
// switch into a playing technique annotation (CompatUtils::replaceStaffTextWithPlayTechniqueAnnotation)
static bool techniqueOfChannel(const QString& name, Art& art)
      {
      if (name == "pizzicato") { art = Art::Pizzicato; return true; }
      if (name == "tremolo") { art = Art::Tremolo64th; return true; }
      if (name == "mute") { art = Art::Mute; return true; }
      if (name == "open") { art = Art::Open; return true; }
      if (name == "harmonics") { art = Art::Harmonic; return true; }
      if (name == "jazz") { art = Art::JazzTone; return true; }
      if (name == "distortion") { art = Art::Distortion; return true; }
      if (name == "overdriven") { art = Art::Overdrive; return true; }
      return false;
      }

std::vector<ArtRef> chordArticulations(const Chord* chord, const Dynamics& dynamics)
      {
      std::vector<ArtRef> arts;
      Score* score = chord->score();
      const int tick = chord->tick().ticks();
      const int staffIdx = chord->staffIdx();

      // spanners over the chord (SpannersMetaParser); a slur from its first to its last chord,
      // lines from their start up to their end
      bool legato = false;
      for (const auto& iv : score->spannerMap().findOverlapping(tick, tick)) {
            Spanner* sp = iv.value;
            if (sp->staffIdx() != staffIdx && !(sp->isPedal() && sp->part() == chord->part()))
                  continue;
            const int from = sp->tick().ticks();
            const int to = sp->tick2().ticks();
            if (sp->isSlur()) {
                  if (!legato && from <= tick && tick <= to) {
                        arts.push_back({ Art::Legato, false });
                        legato = true;
                        }
                  continue;
                  }
            if (tick < from || tick >= to)
                  continue;
            switch (sp->type()) {
                  case ElementType::PEDAL:
                  case ElementType::LET_RING:  arts.push_back({ Art::Pedal, false }); break;
                  case ElementType::PALM_MUTE: arts.push_back({ Art::PalmMute, false }); break;
                  case ElementType::VIBRATO:   arts.push_back({ Art::Vibrato, false }); break;
                  default: break;
                  }
            }
      // sf-type dynamic on the chord (AnnotationsMetaParser: Subito)
      if (dynamics.subitoAt(tick))
            arts.push_back({ Art::Subito, false });
      // tremolo on one chord (TremoloMetaParser)
      if (Tremolo* t = chord->tremolo()) {
            if (!t->twoNotes()) {
                  switch (t->tremoloType()) {
                        case TremoloType::R8:  arts.push_back({ Art::Tremolo8th, false }); break;
                        case TremoloType::R16: arts.push_back({ Art::Tremolo16th, false }); break;
                        case TremoloType::R32: arts.push_back({ Art::Tremolo32nd, false }); break;
                        case TremoloType::R64: arts.push_back({ Art::Tremolo64th, false }); break;
                        case TremoloType::BUZZ_ROLL: arts.push_back({ Art::TremoloBuzz, false }); break;
                        default: break;
                        }
                  }
            }
      // grace notes (GraceNotesMetaParser): their type is an articulation of the principal chord
      for (const Chord* g : chord->graceNotes()) {
            switch (g->noteType()) {
                  case NoteType::ACCIACCATURA: arts.push_back({ Art::Acciaccatura, false }); break;
                  case NoteType::APPOGGIATURA:
                  case NoteType::GRACE4:
                  case NoteType::GRACE16:
                  case NoteType::GRACE32:      arts.push_back({ Art::PreAppoggiatura, false }); break;
                  case NoteType::GRACE8_AFTER:
                  case NoteType::GRACE16_AFTER:
                  case NoteType::GRACE32_AFTER: arts.push_back({ Art::PostAppoggiatura, false }); break;
                  default: break;
                  }
            }
      // arpeggio (ArpeggioMetaParser)
      if (Arpeggio* a = chord->arpeggio()) {
            if (a->playArpeggio()) {
                  switch (a->arpeggioType()) {
                        case ArpeggioType::NORMAL:        arts.push_back({ Art::Arpeggio, false }); break;
                        case ArpeggioType::UP:            arts.push_back({ Art::ArpeggioUp, false }); break;
                        case ArpeggioType::DOWN:          arts.push_back({ Art::ArpeggioDown, false }); break;
                        case ArpeggioType::UP_STRAIGHT:   arts.push_back({ Art::ArpeggioStraightUp, false }); break;
                        case ArpeggioType::DOWN_STRAIGHT: arts.push_back({ Art::ArpeggioStraightDown, false }); break;
                        default: break;
                        }
                  }
            }
      // chord line (ChordLineMetaParser)
      for (Element* e : chord->el()) {
            if (!e->isChordLine())
                  continue;
            switch (toChordLine(e)->chordLineType()) {
                  case ChordLineType::FALL:  arts.push_back({ Art::Fall, false }); break;
                  case ChordLineType::DOIT:  arts.push_back({ Art::Doit, false }); break;
                  case ChordLineType::PLOP:  arts.push_back({ Art::Plop, false }); break;
                  case ChordLineType::SCOOP: arts.push_back({ Art::Scoop, false }); break;
                  default: break;
                  }
            }
      // symbols (SymbolsMetaParser)
      for (Articulation* a : chord->articulations())
            symbolTypes(a->symId(), arts);
      // playing technique in force (NoteArticulationsParser::parsePlayingTechnique)
      // (only a switch away from the instrument's first channel: a trumpet's default channel is
      // called "open", but MS4 sees a technique only where a text asks for one)
      const Note* up = chord->upNote();
      if (up && up->subchannel() > 0) {
            const Instrument* instr = chord->part()->instrument(chord->tick());
            const Channel* ch = instr->channel(up->subchannel());
            Art art;
            if (ch && techniqueOfChannel(ch->name(), art))
                  arts.push_back({ art, false });
            }
      return arts;
      }

// NoteArticulationsParser: notehead and ghost note
std::vector<ArtRef> noteArticulations(const Note* note, const std::vector<ArtRef>& chordArts)
      {
      std::vector<ArtRef> arts = chordArts;
      Art head = Art::COUNT;
      switch (note->headGroup()) {
            case NoteHead::Group::HEAD_CROSS: head = Art::CrossNote; break;
            case NoteHead::Group::HEAD_CIRCLED_LARGE:
            case NoteHead::Group::HEAD_CIRCLED: head = Art::CircleNote; break;
            case NoteHead::Group::HEAD_XCIRCLE: head = Art::CircleCrossNote; break;
            case NoteHead::Group::HEAD_TRIANGLE_DOWN: head = Art::TriangleDownNote; break;
            case NoteHead::Group::HEAD_TRIANGLE_UP: head = Art::TriangleUpNote; break;
            case NoteHead::Group::HEAD_DIAMOND:
            case NoteHead::Group::HEAD_DIAMOND_OLD: head = Art::DiamondNote; break;
            case NoteHead::Group::HEAD_PLUS: head = Art::PlusNote; break;
            case NoteHead::Group::HEAD_SLASH: head = Art::SlashNote; break;
            case NoteHead::Group::HEAD_SLASHED1: head = Art::SlashedForwardsNote; break;
            case NoteHead::Group::HEAD_SLASHED2: head = Art::SlashedBackwardsNote; break;
            case NoteHead::Group::HEAD_DO: head = Art::TriangleUpNote; break;
            case NoteHead::Group::HEAD_RE: head = Art::MoonNote; break;
            case NoteHead::Group::HEAD_FA: head = Art::TriangleRightNote; break;
            case NoteHead::Group::HEAD_LA: head = Art::SquareNote; break;
            case NoteHead::Group::HEAD_TI: head = Art::TriangleRoundDownNote; break;
            default: break;
            }
      if (head != Art::COUNT)
            arts.push_back({ head, false });
      if (note->ghost())
            arts.push_back({ Art::GhostNote, false });
      return arts;
      }

} // namespace Ms4
} // namespace Ms
