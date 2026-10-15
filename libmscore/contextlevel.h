//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __CONTEXTLEVEL_H__
#define __CONTEXTLEVEL_H__

//---------------------------------------------------------
//   The level rule (playback setting [levels] context; the owner, 2026-10-10: the same written dynamic sounds equally
//   loud in every string part and technique, judged in the music, not on isolated notes)
//
//   A note on an articulation with a measured context level (the SSO string sections' Performance Legato:
//   <Articulation contextLevel>, from tools/soundlibraries/performance_context/level_rule.py) plays its dynamics CC
//   at the written dynamic's value (Ms4::expressionLevel: no calibrated CC1 for it, CC1 is a timbre crossfade) and is
//   made louder by volume only: one gain per phrase (Articulation::contextLevelAt of the phrase's dynamic, median note
//   spacing and median pitch; a phrase is the slur, not a phrase mark, the note is in on its staff, else its chord
//   alone), so no level step inside a slur.
//
//   The gain is up to about 21 dB (Celli pp), more than CC11 can add (CC11 rests at 127). So a score's largest gain
//   G (every slur of every part with such an articulation, with [slurs] performance on: a superset of the slurs that
//   play on it; a note on it outside a slur, by staff text "performance", louder than G plays at G) is shared out
//   (volumes()):
//     - the patches with the articulation (the Performance patches) all play at the same volume P = G - S,
//     - every other patch of the library's parts (All techniques, and any other section) plays S = max(0, G - 6) dB
//       lower together (6: Live's track Volume top, TrackDelays::MAX_DB),
//   and each phrase plays gain - G by CC11 (<= 0). The volumes go through TrackDelays::patchGain (the Kontakt slot's
//   volume in MuseScore, LiveSetWriter::kontaktVolume / the plain set's Kontakt track Volume in Live); the CC11 goes
//   through the renderer's per-note levels (libraryNoteLevels), so Live's clips and the plain set carry it.
//
//   The section balance (playback setting [levels] sectionBalance; the owner, 2026-10-11: M, on in Recommended, off in
//   Library default): every patch of a section with a map level O (<Instrument sectionLevel>, dB against the rule's
//   flat level: SSO's five string sections, a real orchestra's balance at CC1 64 (Weinzierl et al. 2018's sound power +
//   10 log10 of SSO's section sizes) less SSO's measured one (sso_cc1_curves.json), centred; gen_spitfire_sso.py
//   SECTION_FADERS has the source) plays O dB louder, staccatos and Performance alike, by the same volumes. With the
//   rule a phrase plays gain + O, a staccato O, everything less S' = max(0, the loudest phrase's gain + O - 6): the rule
//   with each section O apart, less one common constant (all the Performance tracks move together: none is capped). A part's Performance patch carries T = min(G, S' + 6 - O)
//   (its phrases' CC11: gain less T; G where it fits under +6, so the CC11 values are the rule's). Off: the rule alone.
//   Which O (sectionBalanceMode, LibInstrument::sectionLevelFor; the owner, 2026-10-14: levels from real data): 1 the
//   map's sectionLevel (above), 2 (Recommended, the default) its sectionLevel2, the same method with the real side from
//   M. Clark, D. Luce, J. Audio Eng. Soc. 13(2), 1965: orchestral players' scales at mp as sound power
//   (section_balance_cl.py); a patch without sectionLevel2 plays its sectionLevel.
//
//   Real range (playback setting [levels] realRange; the owner, 2026-10-12, stage A of the dynamics plan, HANDOFF.md ›
//   Dynamics like a real player): a written dynamic is one gesture. In SSO CC1 (and a short's velocity) picks the
//   recording, mostly tone; CC11 and the patch's volume are loudness only. So each note of a patch with the map's
//   realSpan / longLevel (SSO's five string sections and the woodwinds' / brass' main patches the database has;
//   <Instrument>, the extras take the main's) plays, by CC11, the real player's change from mp less what the library's
//   held Long already changes:
//       realDb(x, p) = (F(steps(x)) - F(0)) * span(p) - long(x, p)
//   x the dynamics CC in force as written (Ms4::expressionLevel of the dynamic, so CC1 and CC11 move together: a
//   hairpin ramps both), steps(x) its distance from mp in markings (ppp -3 .. fff +4: the inverse of expressionLevel,
//   16 per marking, ff -> fff 15), span(p) one player's ff - pp sound power at the pitch (Weinzierl et al. 2018, TU
//   Berlin's single notes as softly / loudly as possible: the player's ppp and fff; smoothed: real_range.py), F the
//   level as a fraction of the span above that pp (realFraction). The Meyer rule (the owner, 2026-10-14, replacing rule
//   D: "this is clearly better"): written pp and ff are what players produce in normal playing, at the instrument's
//   fractions a / b of the span (<Instrument realPlay="a b">: a = (P_pp - X_lo) / (X_hi - X_lo), b likewise with P_ff;
//   J. Meyer, Acoustics and the Performance of Music, 2009, ch. 3: P rapid scales, X single-note extremes); p, mp, mf,
//   f in equal dB steps between pp and ff (Meyer's own method), ppp -> pp and ff -> fff one step each, linear in steps
//   between, so continuous in CC1. Without realPlay rule D (the owner, 2026-10-12): a = 1/7, b = 6/7, 7 equal steps of
//   span / 7 (instruments Meyer gives no extremes for). Clark & Luce spans ([levels] realRange 2, the owner,
//   2026-10-14: the section data where it's reliable, one source for every instrument; in Recommended and by default):
//   the player's change from mp is steps(x) x S / 5 instead, S the instrument's written pp -> ff as orchestral players
//   play it (<Instrument realSteps="S">: M. Clark, D. Luce, J. Audio Eng. Soc. 13(2), 1965, Table I: two players per
//   instrument, scales at pp, mf, ff; the Spheres dataset's whole sections agree within ~2 dB where they recorded 3+
//   dynamics, a cross-check only), ppp / fff one step more, whatever the single-note span; a patch without realSteps
//   plays the Meyer rule. long(x, p) SSO's All techniques Long at CC1 x less at CC1 64
//   (the sustain's power, sso_cc1_curves.json; linear in CC1 and in pitch between the measured points, the nearest end's
//   beyond). At mp (CC1 64, where the section balance M was set) realDb is 0: M stays as it is, on top.
//   Every technique of the section takes it: a note on velocity (a short) is calibrated to the Long at its dynamic with
//   [levels] calibrated 1, a Performance legato note to the Long by CC1 ([levels] calibrated 1 and 2) or, with [levels]
//   context, by its phrase's gain, measured in the music at the written dynamic against the staccatos (which are the
//   Long's level there): each is the Long's level at the written CC1, which already holds SSO's own change with CC1
//   once; realDb takes that change away (- long) and puts the player's in ((F(steps) - F(0)) x span), so SSO's change is
//   counted once, not twice. With [levels] calibrated 2 (option B, 2026-10-15) a note on velocity is matched to the
//   Long as with 1 and plays a constant D more (its loud-end difference), so it follows the Long's change too and takes
//   the same realDb. A short whose calibrated velocity is capped (127 and still under its level, or 1 and still
//   over it) gets the rest by CC11 (realExtra), no longer silently.
//   Headroom: CC11 only lowers, so a part's notes play realDb less the part's largest R (Volumes::range, every note's
//   realDb + realExtra over every CC1 value it meets, found by rendering the score once with the setting's CC11
//   unbounded: realRangeTops) and every patch of the part R louder by its volume (TrackDelays::patchGain: the Kontakt
//   slot hosted, the route set's and the plain set's Kontakt track Volume in Live), within the same S' as the level
//   rule and the section balance (everything lower together past Live's Volume top, +6 dB). The CC11 values go through
//   the renderer's per-note levels (libraryNoteLevels), so Live's clips and the plain set's CC11 lanes carry them.
//
//   Mics (playback setting [levels] micCompensation; the owner, 2026-10-15: changing a part's mics changes its tone and
//   sense of space, never the balance): every level above is measured at the patch's default mix (SSO: the Tree alone
//   at its full fader). A part whose mic Controllers (the plug-in parameters "Mic 1-5 level" and "Mic Mix Distance",
//   partcontrollers.h, or their automation lanes) give another mix plays C = -(the new mix's level against the default)
//   dB more, so the section stays at its balance:
//       level = 10 log10 sum_i 10^((micLevel_i + fader(v_i)) / 10)
//   micLevel_i the mic alone at its full fader against the default mix (<Instrument micLevels>), v_i its fader 0-127:
//   the part's value, else the patch's default (<Instrument micDefault>); fader(v) the measured law (<Mics fader>). With
//   Mic Mix Distance set the plug-in plays its mix whatever the mic values sent with it (measured: in one block, as a
//   part's values go, SSO's script overrides them, Outrigger and Leader included): Close, Tree, Ambient by its law
//   (<Mics mix>), Outrigger and Leader at their default. Mics add as powers (Kontakt sums their sample groups; measured: sso_mic_levels.json has the
//   prediction's error). A mix in which no mic sounds is left silent (C = 0). The measure: the sustain's power (rmsDb,
//   as the real range's longLevel), SSO's Long at CC1 64, median over three pitches.
//   C goes where the section level goes: the patches' volumes (Volumes::patchDb, within the same S'), so with fixed
//   values nothing else changes, in Live too (the Kontakt track's Volume). Automated mics change C over time: the
//   volume carries the part's largest C over its notes (Volumes::micTop), each note plays its own C at its start less
//   that by CC11 (the renderer's per-note levels: Live's clips and the plain set carry it); a note held through a mic
//   change keeps its start's C.
//---------------------------------------------------------

#include <array>
#include <map>
#include <QString>
#include "automation.h"
#include "partcontrollers.h"

namespace Ms {

class Chord;
class MasterScore;
class Part;
class Score;
namespace Ms4 { class Dynamics; }
namespace SoundLib { struct Articulation; struct LibInstrument; class Library; }

namespace ContextLevel {

// whether the setting is on for the score
bool on(const Score* score);
// whether [levels] sectionBalance is on for the score
bool sectionBalance(const Score* score);
// which: 0 none, 1 sectionLevel (TU Berlin single notes, Meyer mp), 2 sectionLevel2 (Clark & Luce at playing levels)
int sectionBalanceMode(const Score* score);
// whether the articulation has a measured context level
bool has(const SoundLib::Articulation* a);
// whether a patch has an articulation with one (a Performance patch)
bool has(const SoundLib::LibInstrument* patch);

// the gain in dB of the phrase the chord is in (above), its dynamic from dynamics at the phrase's first chord
// (utick = tick + tickOffset)
double gain(const SoundLib::Articulation& a, const Chord* chord, const Ms4::Dynamics& dynamics, int tickOffset);
// the gains were fitted against the section's staccatos (Spiccato) at [levels] calibrated 1's level (level_rule.py:
// gain = st - pfd + 1.3, the owner's 1.3 dB of slurs over the staccatos around them). With calibrated 2 the staccatos
// play D louder (SoundLib::loudEndDb of the section's Spiccato against its Long, the patch's one curve), so every slur
// gain of the section takes D too: the relation stays 1.3 dB, G and S follow. 0: not 2, no calibration, not measured
double staccatoShiftDb(const std::vector<const SoundLib::LibInstrument*>& patches, const Score* score);

// whether [levels] realRange is on for the score (1 or 2)
bool realRange(const Score* score);
// whether it plays Clark & Luce spans (2) for the score
bool clarkLuceSpans(const Score* score);
// whether a patch has the real range's data (longLevel, and realSpan or realSteps: a patch with no database instrument
// has its family's realSteps only, an assumption; realRangeDb without spans gives it none)
bool hasRealRange(const SoundLib::LibInstrument* patch);
// a written dynamics CC value's distance from mp in markings (ppp -3 … fff +4)
double markingSteps(int cc);
// the player's level at a marking step (markingSteps) as a fraction of realSpan above the single-note pp: ppp 0, pp the
// patch's realPlayPp, p .. f in 5 equal steps to ff's realPlayFf, fff 1; linear between (rule D: 1/7, 6/7: (steps + 3) / 7)
double realFraction(double steps, double pp, double ff);
// realDb (above) of a note at the pitch on the patch with the dynamics CC x in force as written; spans: Clark & Luce
// spans (realRange 2) where the patch has realSteps, the player's level steps(x) x S / 5 instead of the Meyer rule's
double realRangeDb(const SoundLib::LibInstrument& patch, int x, int pitch, bool spans = false);
// long(x, pitch) of realDb: SSO's All techniques Long at the dynamics CC x less at 64, dB (the map's longLevel; 0 without it)
double longLevelDb(const SoundLib::LibInstrument& patch, int x, int pitch);

// whether [levels] micCompensation is on for the score
bool micCompensation(const Score* score);
// a part's mic faders for a patch (Mics above): Mic 1-5 and Mic Mix Distance, the part's Controllers values (-1: the
// patch's own) and their automation lanes (a lane's value in force wins)
struct MicValues {
      std::array<int, 6> part { { -1, -1, -1, -1, -1, -1 } };
      std::array<Automation::Lane, 6> lanes;
      std::array<bool, 6> hasLane { { false, false, false, false, false, false } };
      bool automated() const;
      // the faders 0-127 at tick (-1: the patch's own)
      std::array<double, 6> at(int tick) const;
      };
MicValues micValues(const Part* part, const SoundLib::LibInstrument& patch,
                    const std::map<const Part*, PartControllers::Values>& values,
                    const std::map<const Part*, Automation::PartLanes>& lanes);
// a fader value's level against its full fader (<Mics fader>; -inf at 0)
double micFaderDb(const SoundLib::Library& library, double fader);
// the patch's level against its default mix with these faders (Mic 1-5, Mic Mix Distance; -1: the patch's own); NAN: no
// mic sounds, or no data (the patch without micLevels, the library without <Mics>)
double micLevelDb(const SoundLib::LibInstrument& patch, const SoundLib::Library& library, const std::array<double, 6>& faders);
// the compensation C (-micLevelDb; 0 where that is NAN)
double micOffsetDb(const SoundLib::LibInstrument& patch, const SoundLib::Library& library, const std::array<double, 6>& faders);

struct Volumes {
      std::map<QString, double> patchDb;    // patch -> its volume in dB (P or -S; section balance: O (+ the part's G) - S'; real range: + R)
      std::map<QString, double> micTop;     // [levels] micCompensation: patch -> its largest C over the part's notes (in patchDb)
      double top { 0.0 };                   // G (section balance: min(G, S' + MAX_DB - O - R))
      double range { 0.0 };                 // R: the real range's headroom (its notes' CC11 play realDb less it)
      };
// the real range's largest realDb (+ realExtra) per part over the score (MidiRenderer::setRealRangeTops), the score
// rendered once; empty: the setting off or no part with the data
std::map<const Part*, double> realRangeTops(const MasterScore* score);
// per library part (none: both settings off, no library, or the rule alone and no phrase on such an articulation)
std::map<const Part*, Volumes> volumes(const MasterScore* score);

}     // namespace ContextLevel
}     // namespace Ms
#endif
