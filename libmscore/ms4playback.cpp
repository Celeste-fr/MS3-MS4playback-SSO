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
#include <QRegularExpression>

#include "arpeggio.h"
#include "articulation.h"
#include "chord.h"
#include "chordline.h"
#include "dynamic.h"
#include "glissando.h"
#include "hairpin.h"
#include "instrument.h"
#include "measure.h"
#include "note.h"
#include "part.h"
#include "repeatlist.h"
#include "score.h"
#include "segment.h"
#include "slur.h"
#include "staff.h"
#include "stafftextbase.h"
#include "sym.h"
#include "tremolo.h"
#include "trill.h"
#include "key.h"
#include "pitchspelling.h"

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
            case Art::ContinuousGlissando:
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
      s.techniques = e && e->nArts > 0;
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

// ChannelMap::resolveChannelForEvent: a channel per voice, except that without technique
// mappings a note with articulations but no Standard goes to the first channel (voice 1's)
int Sounds::layerFor(const std::vector<Art>& noteArts, int voice) const
      {
      if (noteArts.empty() || std::find(noteArts.begin(), noteArts.end(), Art::Standard) != noteArts.end())
            return voice;
      return techniques ? voice : 0;
      }

//---------------------------------------------------------
//   ornaments
//---------------------------------------------------------

static const float QUAVER = 240, SEMI = 120, DEMI = 60;

const OrnamentRule* ornamentRule(Art a)
      {
      static const std::map<Art, OrnamentRule> rules {
            { Art::Trill,                  { {}, true, { 0, 1 }, {}, QUAVER / 10.f, DEMI, SEMI } },
            { Art::TrillBaroque,           { {}, true, { 1, 0 }, { -1, 0 }, QUAVER / 10.f, DEMI, SEMI } },
            { Art::LinePrall,              { { 2, 2, 2 }, true, { 1, 0 }, { 1, 0 }, DEMI, DEMI, DEMI } },
            { Art::UpPrall,                { { -1, 0 }, true, { 1, 0 }, { 1, 0 }, DEMI, DEMI, DEMI } },
            { Art::UpMordent,              { { -1, 0 }, true, { 1, 0 }, { -1, 0 }, SEMI, SEMI, SEMI } },
            { Art::UpperMordent,           { { 0, 1 }, false, { 0 }, {}, DEMI / 2.f, DEMI, SEMI } },
            { Art::LowerMordent,           { { 0, -1 }, false, { 0 }, {}, DEMI / 2.f, DEMI, SEMI } },
            { Art::UpperMordentBaroque,    { { 1, 0, 1 }, false, { 0 }, {}, DEMI / 2.f, DEMI, SEMI } },
            { Art::MordentWithUpperPrefix, { { 1, 1, 1, 0 }, true, { 1, 0 }, {}, SEMI, SEMI, SEMI } },
            { Art::DownMordent,            { { 1, 1, 1, 0 }, true, { 1, 0 }, { -1, 0 }, SEMI, SEMI, SEMI } },
            { Art::PrallUp,                { { 1, 0 }, true, { 1, 0 }, { -1, 0 }, SEMI, SEMI, SEMI } },
            { Art::PrallDown,              { { 1, 0 }, true, { 1, 0 }, { -1, 0, 0, 0 }, SEMI, SEMI, SEMI } },
            { Art::Turn,                   { { 1, 0, -1 }, false, { 0 }, {}, DEMI / 2.f, DEMI, SEMI } },
            { Art::InvertedTurn,           { { -1, 0, 1 }, false, { 0 }, {}, DEMI / 2.f, DEMI, SEMI } },
            { Art::Tremblement,            { { 1, 0 }, false, { 1, 0 }, {}, SEMI, SEMI, SEMI } },
            { Art::PrallMordent,           { {}, false, { 1, 0, -1, 0 }, {}, SEMI, SEMI, SEMI } },
            };
      auto it = rules.find(a);
      return it == rules.end() ? nullptr : &it->second;
      }

float OrnamentRule::subNoteTicks(double bps) const
      {
      static const double PRESTISSIMO_BPS = 3.33;       // RealRound(200 / 60, 2)
      static const double MODERATO_BPS = 1.8;           // RealRound(108 / 60, 2)
      if (bps >= PRESTISSIMO_BPS)
            return highTempoTicks;
      if (bps >= MODERATO_BPS)
            return mediumTempoTicks;
      return lowTempoTicks;
      }

// semitones from the note to its diatonic neighbour (dir +1 above, -1 below): the neighbour takes
// an accidental written earlier in the bar on its line, else the key's (chromaticPitchSteps)
int neighbourSemitones(const Note* note, int dir)
      {
      static const int NATURAL_PC[7] = { 0, 2, 4, 5, 7, 9, 11 };    // C D E F G A B
      const int tpc = note->tpc();
      const int L = tpc2step(tpc);
      const int a = int(tpc2alter(tpc));
      const int L2 = (L + dir + 7) % 7;
      const int line2 = note->line() - dir;

      int a2;
      bool found = false;
      const Chord* chord = note->chord();
      const Measure* m = chord->measure();
      const int staffIdx = chord->staffIdx();
      int barAlter = 0;
      for (Segment* seg = m->first(SegmentType::ChordRest); seg && seg->tick() < chord->tick(); seg = seg->next(SegmentType::ChordRest)) {
            for (int tr = staffIdx * VOICES; tr < (staffIdx + 1) * VOICES; ++tr) {
                  Element* e = seg->element(tr);
                  if (!e || !e->isChord())
                        continue;
                  for (Note* n : toChord(e)->notes()) {
                        if (n->accidental() && n->line() == line2) {
                              barAlter = int(tpc2alter(n->tpc()));
                              found = true;
                              }
                        }
                  }
            }
      if (found)
            a2 = barAlter;
      else {
            const int key = int(note->staff()->key(chord->tick()));
            static const int SHARPS[7] = { 3, 0, 4, 1, 5, 2, 6 };     // F C G D A E B
            static const int FLATS[7] = { 6, 2, 5, 1, 4, 0, 3 };      // B E A D G C F
            a2 = 0;
            for (int k = 0; k < key && k < 7; ++k)
                  if (SHARPS[k] == L2) a2 = 1;
            for (int k = 0; k < -key && k < 7; ++k)
                  if (FLATS[k] == L2) a2 = -1;
            }
      const int d = dir > 0 ? (NATURAL_PC[L2] - NATURAL_PC[L] + 12) % 12 : (NATURAL_PC[L] - NATURAL_PC[L2] + 12) % 12;
      return std::abs(d + dir * (a2 - a));
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

static const Pattern* patternOf(Family fam, const ArtRef& a)
      {
      const Pattern* p = pattern(fam, a.art);
      if (!p && a.fallback)
            p = pattern(fam, Art::Standard);
      return p;
      }

std::vector<Art> iterationOrder(const std::vector<ArtRef>& arts, int fam)
      {
      std::unordered_map<Art, int> map;
      auto enters = [&](const ArtRef& a) { return fam < 0 || patternOf(Family(fam), a) != nullptr; };
      // the chord's map, the principal's grace type out
      for (const ArtRef& a : arts)
            if (a.phase == 0 && enters(a))
                  map.emplace(a.art, 0);
      for (const ArtRef& a : arts)
            if (a.phase == 0 && a.erased)
                  map.erase(a.art);
      // the note's own (a copy of the chord's, then the note's parsers), Standard if nothing
      bool note = false;
      for (const ArtRef& a : arts)
            note |= a.phase >= 1;
      if (note || fam >= 0) {
            for (const ArtRef& a : arts)
                  if (a.phase == 1 && enters(a))
                        map.emplace(a.art, 0);
            if (map.empty())
                  map.emplace(Art::Standard, 0);
            // renderNormalTie: the tied notes' articulations, then Standard out where there are others
            for (const ArtRef& a : arts)
                  if (a.phase == 2 && enters(a))
                        map.emplace(a.art, 0);
            if (map.size() > 1)
                  map.erase(Art::Standard);
            }
      std::vector<Art> order;
      for (const auto& p : map)
            order.push_back(p.first);
      return order;
      }

NoteResult note(Family fam, const std::vector<ArtRef>& arts, int D, bool snd)
      {
      struct P { Art art; const Pattern* p; };
      std::vector<P> pats;
      for (Art art : iterationOrder(arts, int(fam))) {
            const Pattern* p = nullptr;
            for (const ArtRef& a : arts)
                  if (a.art == art && (p = patternOf(fam, a)))
                        break;
            if (!p && art == Art::Standard)
                  p = pattern(fam, Art::Standard);
            if (p)
                  pats.push_back({ art, p });
            }
      if (pats.empty())
            pats.push_back({ Art::Standard, pattern(fam, Art::Standard) });

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

      // pitch curve (ArticulationMap: averaged over the articulations that bend, as their range;
      // NoteEvent::calculatePitchCurve scales it by the range unless that is 0 or a semitone)
      {
            auto rangeOf = [&](Art art) {
                  for (const ArtRef& a : arts)
                        if (a.art == art)
                              return a.pitchRange;
                  return 0;
                  };
            int sum[11] = {};
            int count = 0;
            int rangeSum = 0;
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
                  rangeSum += rangeOf(q.art);
                  for (int k = 0; k < 11; ++k)
                        sum[k] += pc[k];
                  }
            for (const P& q : pats)
                  r.bend |= isBendType(q.art);
            const int range = count > 0 ? rangeSum / count : rangeOf(pats[0].art);
            if (count > 0)
                  for (int k = 0; k < 11; ++k)
                        r.pitchCurve[k] = sum[k] / count;
            else if (const int* pc = pitchPattern(fam, pats[0].art))
                  std::copy(pc, pc + 11, r.pitchCurve);
            if (range != 0 && range != 50) {                  // PITCH_LEVEL_STEP
                  const float ratio = range / 50.0f;
                  const float unit = 50.0f / 100.0f;          // PITCH_LEVEL_STEP / ONE_PERCENT
                  for (int k = 0; k < 11; ++k)
                        r.pitchCurve[k] = int(std::round(r.pitchCurve[k] * ratio * unit));
                  }
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

// PlaybackContext::applyDynamic: a dynamic or hairpin read from a MuseScore 3 score applies to
// its own voice if it is in voice 2-4 (EngravingCompat::migrateDynamicPosOnVocalStaves), else to
// the whole instrument; at a tick the more specific assignment wins
void Dynamics::apply(const Element* e, int tick, int level)
      {
      // VoiceAssignment: voices 2-4 CURRENT_VOICE_ONLY (EngravingCompat); voice 1 from the
      // MuseScore 3 range (Read206::readDynamicRange): "staff" ALL_VOICE_IN_STAFF, else
      // ALL_VOICE_IN_INSTRUMENT; the priority is the enum's value
      const bool ownVoice = e->voice() != 0;
      Dynamic::Range range = Dynamic::Range::PART;
      if (e->isDynamic())
            range = toDynamic(e)->dynRange();
      else if (e->isHairpin())
            range = toHairpin(e)->dynRange();
      const bool ownStaff = !ownVoice && range == Dynamic::Range::STAFF;
      const int priority = ownVoice ? 2 : ownStaff ? 1 : 0;
      for (int track = _strack; track < _etrack; ++track) {
            if (ownVoice && track != e->track())
                  continue;
            if (ownStaff && track / VOICES != e->staffIdx())
                  continue;
            auto r = _byTrack[track].emplace(tick, Info { level, priority });
            if (!r.second && r.first->second.priority <= priority)
                  r.first->second = { level, priority };
            }
      }

int Dynamics::appliable(int track, int tick) const
      {
      auto t = _byTrack.find(track);
      if (t == _byTrack.end())
            return NATURAL;
      auto it = t->second.upper_bound(tick);
      if (it == t->second.begin())
            return NATURAL;
      return std::prev(it)->second.level;
      }

int Dynamics::nominal(int track, int tick) const
      {
      auto t = _byTrack.find(track);
      if (t == _byTrack.end())
            return NATURAL;
      auto it = t->second.find(tick);
      return it == t->second.end() ? NATURAL : it->second.level;
      }

// FluidSequencer sends every track's changes as CC11 to the instrument's channels; where tracks
// change at the same time the highest level is taken
void Dynamics::mergeTracks()
      {
      _merged.clear();
      for (const auto& t : _byTrack) {
            for (const auto& l : t.second) {
                  auto r = _merged.emplace(l.first, l.second.level);
                  if (!r.second)
                        r.first->second = std::max(r.first->second, l.second.level);
                  }
            }
      }

// the dynamic a text dynamic ("other-dynamics") spells: SMuFL dynamic glyphs (as old scores
// have them) and <sym> names read as the dynamic letters, the first dynamic in the text (the
// longest one where several start at the same place) — MuseScore 4 plays such a marking
static QString tagFromText(const QString& text)
      {
      static const char* const GLYPHS[] = { "dynamicPiano", "dynamicMezzo", "dynamicForte", "dynamicRinforzando",
                                            "dynamicSforzando", "dynamicZ", "dynamicNiente" };      // U+E520 …
      QString t;
      for (QChar c : text) {
            const int u = c.unicode();
            if (u >= 0xE520 && u <= 0xE526)
                  t += QString("<sym>%1</sym>").arg(GLYPHS[u - 0xE520]);
            else
                  t += c;
            }
      t.remove(QRegularExpression("<(?!/?sym>)[^>]*>"));         // formatting tags, keep <sym>
      int bestIndex = -1;
      int bestLength = 0;
      QString best;
      for (const Dyn& d : dynList) {
            const QString dt = QString::fromUtf8(d.text);
            if (dt.isEmpty())
                  continue;
            const int index = t.indexOf(dt);
            if (index < 0)
                  continue;
            if (bestIndex < 0 || index < bestIndex || (index == bestIndex && dt.length() > bestLength)) {
                  bestIndex = index;
                  bestLength = dt.length();
                  best = QString::fromUtf8(d.tag);
                  }
            }
      return best;
      }

// PlaybackContext::updateDynamicMap, at the dynamic's place in the unrolled score (tick + offset)
void Dynamics::addDynamic(Score*, Dynamic* dynamic, int offset)
      {
      Segment* segment = dynamic->segment();
      if (!segment)
            return;
      const int scoreTick = segment->tick().ticks();
      const int tick = scoreTick + offset;
      QString type = dynamic->dynamicTypeName();
      if (type == "other-dynamics")
            type = tagFromText(dynamic->xmlText());

      // AnnotationsMetaParser: these make the chords of their staff Subito
      static const QSet<QString> SUBITO { "s", "sf", "sff", "sfff", "sfz", "sffz", "sfffz", "sfp", "sfpp" };
      if (SUBITO.contains(type))
            _subito.insert({ dynamic->staffIdx(), scoreTick });

      int level = ordinaryLevel(type);
      if (level >= 0) {
            apply(dynamic, tick, level);
            return;
            }
      level = singleNoteLevel(type);
      if (level >= 0) {
            const int prev = appliable(dynamic->track(), tick);
            apply(dynamic, tick, level);
            if (Segment* next = segment->next())
                  apply(dynamic, next->tick().ticks() + offset, prev);
            return;
            }
      int from, to;
      if (compound(type, from, to)) {
            const int length = dynamic->velocityChangeLength().ticks();
            for (const auto& p : easingValueCurve(length, 6, to - from, ChangeMethod::NORMAL))
                  apply(dynamic, tick + p.first, from + p.second);
            }
      }

// the dynamic marking at a segment in the hairpin's track, if any
static Dynamic* dynamicAt(Segment* segment, int track)
      {
      if (!segment)
            return nullptr;
      Element* e = segment->findAnnotation(ElementType::DYNAMIC, track, track);
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

// Not MuseScore 4: a MuseScore 3 hairpin with a velocity change of its own and no end dynamic
// MS4 takes (MS4 ignores the change and goes one step): the dynamic nearest to the velocity
// MuseScore 3 reached, from the one nearest the level in force (MS3's velocities), and at least
// MS4's one step. Scores written for MuseScore 3 set these to be heard (p < with +63: to ff).
static int ms3VelocityChangeLevel(int levelFrom, int oneStep, bool crescendo, int veloChange)
      {
      static const std::pair<const char*, int> MS3 [] {
            { "pppppp", 1 }, { "ppppp", 5 }, { "pppp", 10 }, { "ppp", 16 }, { "pp", 33 }, { "p", 49 },
            { "mp", 64 }, { "mf", 80 }, { "f", 96 }, { "ff", 112 }, { "fff", 126 }, { "ffff", 127 },
            };
      auto nearest = [](auto key) {
            int best = 0;
            for (int i = 1; i < int(sizeof(MS3) / sizeof(MS3[0])); ++i)
                  if (std::abs(key(i)) < std::abs(key(best)))
                        best = i;
            return best;
            };
      const int from = nearest([&](int i) { return levelOf(MS3[i].first, true) - levelFrom; });
      const int velocity = qBound(1, MS3[from].second + (crescendo ? veloChange : -veloChange), 127);
      const int to = nearest([&](int i) { return MS3[i].second - velocity; });
      const int level = levelOf(MS3[to].first, true);
      return crescendo ? std::max(level, oneStep) : std::min(level, oneStep);
      }

// PlaybackContext::handleHairpin: the hairpin's whole length at its place in this pass of the
// unrolled score, even where it reaches past the pass (it then runs on into what plays next)
void Dynamics::addHairpin(Score* score, Hairpin* hairpin, int offset)
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
      const int track = hairpin->track();
      const int levelFrom = appliable(track, spannerFrom + offset);

      Dynamic* endDynamic = dynamicAt(score->tick2segment(Fraction::fromTicks(spannerTo), true, SegmentType::ChordRest), hairpin->track());
      const int nominalLevelTo = endDynamic ? levelOf(endDynamic->dynamicTypeName(), true) : NATURAL;
      const bool hasNominalLevelTo = nominalLevelTo != NATURAL;
      const bool isCrescendo = hairpin->isCrescendo();
      const bool useNominalLevelTo = hasNominalLevelTo && (isCrescendo ? nominalLevelTo > levelFrom : nominalLevelTo < levelFrom);
      int levelTo = useNominalLevelTo ? nominalLevelTo : levelFrom + (isCrescendo ? STEP : -STEP);
      if (!useNominalLevelTo && hairpin->veloChange() != 0 && MScore::ms3HairpinVelocity && !qEnvironmentVariableIsSet("MS4_STRICT"))
            levelTo = ms3VelocityChangeLevel(levelFrom, levelTo, isCrescendo, std::abs(hairpin->veloChange()));

      const int levelAtEnd = nominal(track, spannerTo + offset);
      const bool hasDynamicAtEndTick = levelAtEnd != NATURAL;
      if (hasDynamicAtEndTick && levelAtEnd != levelTo)
            spannerTo -= 1;                                   // Fraction::eps()

      const int durationTicks = spannerTo - spannerFrom;
      if (durationTicks <= 0)
            return;

      const int steps = std::max(durationTicks / (DIVISION / 4), 24);
      for (const auto& p : easingValueCurve(durationTicks, steps, levelTo - levelFrom, hairpin->veloChangeMethod()))
            apply(hairpin, spannerFrom + p.first + offset, levelFrom + p.second);

      if (hasNominalLevelTo && !useNominalLevelTo && !hasDynamicAtEndTick)
            apply(hairpin, spannerTo + offset, nominalLevelTo);
      }

// PlaybackContext::update: the part's dynamic markings in score order, then its hairpins
// CompatUtils::replaceStaffTextWithPlayTechniqueAnnotation: a MuseScore 3 staff text that names a
// technique exactly (or switches to a channel so named) is a playing technique from there on, for the
// whole part (PlaybackContext::updatePlayTechMap; articulationFromPlayTechType)
void Dynamics::addTechnique(const StaffTextBase* text, int utick)
      {
      static const std::map<QString, Art> TECHNIQUES {
            { "natural", Art::COUNT }, { "normal", Art::COUNT }, { "arco", Art::COUNT },
            { "open", Art::Open }, { "mute", Art::Mute }, { "distortion", Art::Distortion },
            { "overdriven", Art::Overdrive }, { "harmonics", Art::Harmonic }, { "jazz", Art::JazzTone },
            { "pizzicato", Art::Pizzicato }, { "tremolo", Art::Tremolo64th },
            };
      auto it = TECHNIQUES.find(text->plainText().toLower());
      if (it == TECHNIQUES.end()) {
            const QString channel = text->channelName(0).toLower();
            if (!channel.isEmpty())
                  it = TECHNIQUES.find(channel);
            }
      if (it != TECHNIQUES.end())
            _techniques[utick] = it->second;
      }

Art Dynamics::techniqueAt(int utick) const
      {
      auto it = _techniques.upper_bound(utick);
      if (it == _techniques.begin())
            return Art::COUNT;
      return std::prev(it)->second;
      }

int Dynamics::spannerStop(const Spanner* sp) const
      {
      auto it = _clippedStop.find(sp);
      return it != _clippedStop.end() ? it->second : sp->tick2().ticks();
      }

void Dynamics::build(Score* score, Part* part)
      {
      _byTrack.clear();
      _subito.clear();
      _techniques.clear();
      const int strack = part->startTrack();
      const int etrack = part->endTrack();
      _strack = strack;
      _etrack = etrack;

      // SpannerMap::collectIntervals: per part and spanner type, in the map's order, a spanner
      // starting before the previous one of its type has stopped cuts that one off a tick before
      // (unless they are linked); playback looks spanners up in these collision-free intervals
      _clippedStop.clear();
      std::map<ElementType, Spanner*> lastOfType;
      for (const auto& p : score->spanner()) {
            Spanner* sp = p.second;
            if (sp->part() != part)
                  continue;
            const int start = sp->tick().ticks();
            auto it = lastOfType.find(sp->type());
            if (it != lastOfType.end()) {
                  Spanner* last = it->second;
                  if (spannerStop(last) >= start && !last->isLinked(sp))
                        _clippedStop[last] = start - 1;
                  }
            lastOfType[sp->type()] = sp;
            }

      // PlaybackContext::update: pass by pass through the unrolled score (repeats, jumps), the
      // dynamic markings of each pass's measures, then the hairpins overlapping the pass; levels
      // are kept at unrolled ticks, so after a jump the level last played is in force
      for (const RepeatSegment* rs : score->repeatList()) {
            const int offset = rs->utick - rs->tick;
            const Measure* last = rs->lastMeasure();
            for (const Measure* m = rs->firstMeasure(); m; m = m->nextMeasure()) {
                  for (Segment* s = m->first(); s; s = s->next()) {
                        for (Element* e : s->annotations()) {
                              if (e->isDynamic() && e->track() >= strack && e->track() < etrack)
                                    addDynamic(score, toDynamic(e), offset);
                              else if (e->isStaffTextBase() && e->part() == part)
                                    addTechnique(toStaffTextBase(e), s->tick().ticks() + offset);
                              }
                        }
                  if (m == last)
                        break;
                  }
            std::vector<Hairpin*> hairpins;
            for (const auto& iv : score->spannerMap().findOverlapping(rs->tick + 1, rs->tick + rs->len() - 1)) {
                  Spanner* sp = iv.value;
                  if (sp->isHairpin() && sp->track() >= strack && sp->track() < etrack) {
                        Staff* st = sp->staff();
                        if (st && !st->primaryStaff())
                              continue;                             // linked staves
                        hairpins.push_back(toHairpin(sp));
                        }
                  }
            for (Hairpin* h : hairpins)
                  addHairpin(score, h, offset);
            }

      for (int track = strack; track < etrack; ++track)
            _byTrack[track].emplace(0, Info { NATURAL, 0 });
      mergeTracks();
      if (qEnvironmentVariableIsSet("MS4_DEBUG_DYNAMICS")) {
            for (const auto& t : _byTrack)
                  for (const auto& l : t.second)
                        qDebug("MS4DYN part %s track %d tick %d level %d", qPrintable(part->partName()), t.first, l.first, l.second.level);
            }
      }

//---------------------------------------------------------
//   articulations
//---------------------------------------------------------

// SymbolsMetaParser: the symbols MS3 can show
static bool ornamentType(const QString& s, MScore::OrnamentStyle style, Art& art)
      {
      const bool baroque = style == MScore::OrnamentStyle::BAROQUE;
      if (s == "ornamentUpPrall") art = Art::UpPrall;
      else if (s == "ornamentPrallDown") art = Art::PrallDown;
      else if (s == "ornamentPrallUp") art = Art::PrallUp;
      else if (s == "ornamentLinePrall") art = Art::LinePrall;
      else if (s == "ornamentPrallMordent") art = Art::PrallMordent;
      else if (s == "ornamentUpMordent") art = Art::UpMordent;
      else if (s == "ornamentMordent" || s == "ornamentPinceCouperin") art = Art::LowerMordent;
      else if (s == "ornamentDownMordent") art = Art::DownMordent;
      else if (s == "ornamentTurn" || s == "ornamentTurnUp" || s == "ornamentHaydn" || s == "brassJazzTurn") art = Art::Turn;
      else if (s == "ornamentTurnInverted" || s == "ornamentTurnUpS" || s == "ornamentTurnSlash") art = Art::InvertedTurn;
      else if (s == "ornamentTrill" || s == "ornamentShake3" || s == "ornamentShakeMuffat1") art = baroque ? Art::TrillBaroque : Art::Trill;
      else if (s == "ornamentShortTrill") art = baroque ? Art::UpperMordentBaroque : Art::UpperMordent;
      else if (s == "ornamentTremblement" || s == "ornamentTremblementCouperin") art = Art::Tremblement;
      else return false;
      return true;
      }

static void symbolTypes(const Articulation* a, std::vector<ArtRef>& out)
      {
      QString s = Sym::id2name(a->symId());
      {
            Art orn;
            if (ornamentType(s, a->ornamentStyle(), orn)) {
                  out.push_back({ orn, true });
                  return;
                  }
      }
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

std::vector<ArtRef> chordArticulations(const Chord* chord, const Dynamics& dynamics, int tickOffset)
      {
      std::vector<ArtRef> arts;
      Score* score = chord->score();
      const int tick = chord->tick().ticks();
      const int staffIdx = chord->staffIdx();

      // spanners over any of the chord's time (ChordArticulationsParser::parseSpanners: all voices
      // of the staff, so a slur starting inside a longer note of another voice counts): a slur
      // from its first to its last chord, lines from their start up to their end
      const int chordEnd = tick + std::max(1, chord->actualTicks().ticks());
      bool legato = false;
      for (const auto& iv : score->spannerMap().findOverlapping(tick, chordEnd - 1)) {
            Spanner* sp = iv.value;
            if (sp->staffIdx() != staffIdx && !(sp->isPedal() && sp->part() == chord->part()))
                  continue;
            const int from = sp->tick().ticks();
            const int to = dynamics.spannerStop(sp);
            if (from >= chordEnd)
                  continue;
            if (sp->isSlur()) {
                  if (!legato && tick <= to) {
                        arts.push_back({ Art::Legato, false });
                        legato = true;
                        }
                  continue;
                  }
            if (tick >= to)
                  continue;
            switch (sp->type()) {
                  case ElementType::PEDAL:
                  case ElementType::LET_RING:  arts.push_back({ Art::Pedal, false }); break;
                  case ElementType::PALM_MUTE: arts.push_back({ Art::PalmMute, false }); break;
                  case ElementType::VIBRATO:   arts.push_back({ Art::Vibrato, false }); break;
                  case ElementType::TRILL:
                        switch (toTrill(sp)->trillType()) {
                              case Trill::Type::TRILL_LINE:      arts.push_back({ Art::Trill, false }); break;
                              case Trill::Type::UPPRALL_LINE:    arts.push_back({ Art::UpPrall, false }); break;
                              case Trill::Type::DOWNPRALL_LINE:  arts.push_back({ Art::PrallDown, false }); break;
                              case Trill::Type::PRALLPRALL_LINE: arts.push_back({ Art::LinePrall, false }); break;
                              }
                        break;
                  default: break;
                  }
            }
      // sf-type dynamic on the chord (AnnotationsMetaParser: Subito); grace chords have no
      // segment of their own and get none
      if (!chord->isGrace() && dynamics.subitoAt(tick, staffIdx))
            arts.push_back({ Art::Subito, false });
      // tremolo on one chord or between two (TremoloSingleMetaParser / TremoloTwoMetaParser)
      if (Tremolo* t = chord->tremolo()) {
            switch (t->tremoloType()) {
                  case TremoloType::R8:
                  case TremoloType::C8:  arts.push_back({ Art::Tremolo8th, false }); break;
                  case TremoloType::R16:
                  case TremoloType::C16: arts.push_back({ Art::Tremolo16th, false }); break;
                  case TremoloType::R32:
                  case TremoloType::C32: arts.push_back({ Art::Tremolo32nd, false }); break;
                  case TremoloType::R64:
                  case TremoloType::C64: arts.push_back({ Art::Tremolo64th, false }); break;
                  case TremoloType::BUZZ_ROLL: arts.push_back({ Art::TremoloBuzz, false }); break;
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
            symbolTypes(a, arts);
      // playing technique in force (NoteArticulationsParser::parsePlayingTechnique)
      // (only a switch away from the instrument's first channel: a trumpet's default channel is
      // called "open", but MS4 sees a technique only where a text asks for one)
      {
            const Art art = dynamics.techniqueAt(tick + tickOffset);
            if (art != Art::COUNT)
                  arts.push_back({ art, false, 1 });          // NoteArticulationsParser::parsePlayingTechnique
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
      // (NoteArticulationsParser::doParse: technique, ghost note, notehead, symbols, laissez vibrer,
      // spanners)
      // a MuseScore 3 ghost note reaches MS4 with an x notehead, so it plays as one (CrossNote,
      // parseNoteHead's notehead symbol), not as a GhostNote
      if (note->ghost() && head == Art::COUNT)
            head = Art::CrossNote;
      if (head != Art::COUNT)
            arts.push_back({ head, false, 1 });
      // a portamento glissando from the note: a continuous bend over it to the next note's pitch
      // (SpannersMetaParser: ContinuousGlissando, its range the interval)
      for (Spanner* sp : note->spannerFor()) {
            if (!sp->isGlissando() || !toGlissando(sp)->playGlissando())
                  continue;
            Glissando* g = toGlissando(sp);
            const Note* end = sp->endElement() && sp->endElement()->isNote() ? toNote(sp->endElement()) : nullptr;
            if (g->glissandoStyle() == GlissandoStyle::PORTAMENTO && end) {
                  ArtRef a { Art::ContinuousGlissando, false, 1 };
                  a.pitchRange = (end->ppitch() - note->ppitch()) * 50;
                  arts.push_back(a);
                  }
            break;
            }
      return arts;
      }

} // namespace Ms4
} // namespace Ms
