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
//   The gain is up to about 30 dB (Celli pp, 2026-10-15 grid; 21 before), more than CC11 can add (CC11 rests at 127). So a score's largest gain
//   G (every slur of every part with such an articulation, with [slurs] performance on: a superset of the slurs that
//   play on it; a note on it outside a slur, by staff text "performance", louder than G plays at G) is shared out
//   (volumes()):
//     - the patches with the articulation (the Performance patches) all play G louder by their gain stage (below),
//     - every other patch of the library's parts (All techniques, and any other section) at 0 dB,
//   and each phrase plays gain - G by CC11 (<= 0). (Until 2026-10-15 every patch was S = max(0, G - 6) dB lower so the
//   loudest fit Live's +6 dB fader, TrackDelays::MAX_DB: the gain stage and the master trim below took S's place.) The
//   volumes go through TrackDelays::patchGain (the Kontakt slot's gain in MuseScore; MuseScore's Utility after the Kontakt
//   in Live); the CC11 goes through the renderer's per-note levels (libraryNoteLevels), so Live's clips and the plain set
//   carry it.
//
//   The section balance (playback setting [levels] sectionBalance; the owner, 2026-10-11: M, on in Recommended, off in
//   Library default): every patch of a section with a map level O (<Instrument sectionLevel>, dB against the rule's
//   flat level: SSO's five string sections, a real orchestra's balance at CC1 64 (Weinzierl et al. 2018's sound power +
//   10 log10 of SSO's section sizes) less SSO's measured one (sso_cc1_curves.json), centred; gen_spitfire_sso.py
//   SECTION_FADERS has the source) plays O dB louder, staccatos and Performance alike, by the same volumes. With the
//   rule a phrase plays gain + O, a staccato O: the rule with each section O apart (all the Performance tracks move
//   together: none is capped). A part's Performance patch carries T = min(G, S' + 6 - O), S' = max(0, the loudest
//   phrase's gain + O - 6) (its phrases' CC11: gain less T; G where it fits under +6, so the CC11 values are the rule's;
//   S' is what every patch was lower by until 2026-10-15, Volumes::shift, kept so that T and the CC11 values stay as they
//   were). Off: the rule alone.
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
//   context, by its phrase's gain, measured at the written dynamic against the section's held Long (since 2026-10-15; before,
//   against the staccatos, which were the Long's level there): each is the Long's level at the written CC1, which already holds SSO's own change with CC1
//   once; realDb takes that change away (- long) and puts the player's in ((F(steps) - F(0)) x span), so SSO's change is
//   counted once, not twice. With [levels] calibrated 2 (option B', 2026-10-15) a note on velocity is matched to the
//   Long as with 1 and plays D more (real players' difference, heldOffset; else its loud-end difference), so it follows
//   the Long's change too and takes
//   the same realDb. A short whose calibrated velocity is capped (127 and still under its level, or 1 and still
//   over it) gets the rest by CC11 (realExtra), no longer silently.
//   Headroom: CC11 only lowers, so a part's notes play realDb less the part's largest R (Volumes::range, every note's
//   realDb + realExtra over every CC1 value it meets, found by rendering the score once with the setting's CC11
//   unbounded: realRangeTops) and every patch of the part R louder by its gain stage (TrackDelays::patchGain: the
//   Kontakt slot hosted, MuseScore's Utility on the route set's and the plain set's Kontakt tracks in Live), with the
//   level rule's and the section balance's. The CC11 values go through the renderer's per-note levels
//   (libraryNoteLevels), so Live's clips and the plain set's CC11 lanes carry them.
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
//   C goes where the section level goes: the patches' gain stage (Volumes::patchDb), so with fixed values nothing else
//   changes, in Live too (MuseScore's Utility). Automated mics change C over time: the
//   volume carries the part's largest C over its notes (Volumes::micTop), each note plays its own C at its start less
//   that by CC11 (the renderer's per-note levels: Live's clips and the plain set carry it); a note held through a mic
//   change keeps its start's C.
//
//   Gain stage and master trim (the owner, 2026-10-15: "sure", then "yeah sure let's go with utility"; docs/
//   PLAYBACK_SETTINGS.md › Gain stage and master trim). The levels above (the rule's G or T, the section balance's O, the
//   real range's R, the mics' C: Volumes::patchDb) and the even levels' headroom (evenlevels.h) are each patch's own gain
//   after its Kontakt, uncapped: TrackDelays::gainStageDb. MuseScore plays it on the plug-in slot's output (a float factor,
//   Vst3Synth::setMix); Live's plain set and route set by Live's stock Utility right after the Kontakt, named "MuseScore
//   gain" (LiveSetWriter::UTILITY_NAME; Gain -inf .. +35 dB, past +35 the rest on the fader); the Kontakt track's fader
//   stays where it was (the part's Mixer, the track levels' headroom: TrackDelays::faderGain), the owner's to use. Every
//   level number is the one before; only where the gain is applied moved, and S' (every patch lower so the loudest fit
//   Live's +6 dB fader) is gone. Instead one master trim per score lowers every library patch's gain stage together, only as
//   much as the score's loudest moment needs not to clip (masterTrim):
//       peak(t) = 10 log10 sum_n 10^((g_n + e_n(t) + L_n(t) + crest) / 10)          trim = max(0, max_t peak(t))
//   over the library notes n sounding at t (note-on to note-off, as rendered: one render of the score with the untrimmed
//   gains; release tails left out), g_n its patch's gain (TrackDelays::patchGain without the trim: its gain stage and
//   track-level headroom; the part's Mixer and Live's faders are the owner's, not counted), e_n its route's CC11 in force
//   (20 log10 (CC11 / 127)), L_n its articulation's loudest 50 ms at its velocity or dynamics CC in force and its pitch (the
//   dynamics calibration's curve, DynamicsCurve: by velocity, by CC1, by the larger of both, as the curve is driven;
//   without its own curve: its patch's loudest level, else the calibration's), crest the map's <Dynamics crest> (a note's
//   sample peak over its loudest 50 ms: the largest measured, 14.515 dB over 392 SSO string notes, sso_crest.json). The
//   notes add as powers (the peak estimate of independent sources: a lower bound would be the loudest note alone, an upper
//   one the amplitudes added, peakAmplitudeDb, reported for comparison). The trim never boosts (a quiet score: 0). Without
//   a calibration or a crest (no levels to sum) the trim is the old S' (the loudest gain at +6 dB, fallbackDb), so nothing
//   plays louder than before. None of the level settings on (Library default): no gain stage, no trim.
//   MIDI out (no host, no Utility): the patch's gain goes on CC7 with the Mixer's (TrackDelays::midiVolumes; CC7 127 =
//   +4.15 dB on the host's law), every route lower together by what goes over (the old way, CC7 in the gain stage's place;
//   LIVE.md › What still differs).
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
// (utick = tick + tickOffset). Measured against the section's own held Long (2026-10-15 grid: gain = held - slurred,
// level_rule.py), so [levels] calibrated 2's D for the staccatos does not move it (until 2026-10-15 every slur gain took
// the section's Spiccato D, the gains having been fitted against the staccatos: that would now count it twice)
double gain(const SoundLib::Articulation& a, const Chord* chord, const Ms4::Dynamics& dynamics, int tickOffset);

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
// a part's mic faders for a patch (Mics above): Mic 1-5 and Mic Mix Distance, the part's settings (a flat lane's value,
// partcontrollers.h; -1: the patch's own), compensated by the patch volume, and their other automation lanes (a lane's
// value in force wins), by CC11 at each note
struct MicValues {
      std::array<double, 6> part { { -1, -1, -1, -1, -1, -1 } };
      std::array<Automation::Lane, 6> lanes;
      std::array<bool, 6> hasLane { { false, false, false, false, false, false } };
      bool automated() const;
      // the faders 0-127 at tick (-1: the patch's own)
      std::array<double, 6> at(int tick) const;
      };
MicValues micValues(const Part* part, const SoundLib::LibInstrument& patch,
                    const std::map<const Part*, Automation::PartLanes>& lanes);
// a fader value's level against its full fader (<Mics fader>; -inf at 0)
double micFaderDb(const SoundLib::Library& library, double fader);
// the patch's level against its default mix with these faders (Mic 1-5, Mic Mix Distance; -1: the patch's own); NAN: no
// mic sounds, or no data (the patch without micLevels, the library without <Mics>)
double micLevelDb(const SoundLib::LibInstrument& patch, const SoundLib::Library& library, const std::array<double, 6>& faders);
// the compensation C (-micLevelDb; 0 where that is NAN)
double micOffsetDb(const SoundLib::LibInstrument& patch, const SoundLib::Library& library, const std::array<double, 6>& faders);

struct Volumes {
      // patch -> its gain stage in dB (Gain stage below: the rule's G or 0; section balance: O (+ the part's T); real range:
      // + R; mics: + C), never lowered to fit Live's fader (until 2026-10-15 this was less S')
      std::map<QString, double> patchDb;
      std::map<QString, double> micTop;     // [levels] micCompensation: patch -> its largest C over the part's notes (in patchDb)
      double top { 0.0 };                   // G, T (section balance: min(G, S' + MAX_DB - O - R))
      double range { 0.0 };                 // R: the real range's headroom (its notes' CC11 play realDb less it)
      // S' (the rule alone: S): what every patch was lowered by until 2026-10-15 so the loudest fit Live's +6 dB fader. Still
      // what T is computed with (so the CC11 values are the same), and the old volumes' (TrackDelays::legacyPatchGain: a set
      // written before the gain stage read back) and the trim's fallback
      double shift { 0.0 };
      };
// the real range's largest realDb (+ realExtra) per part over the score (MidiRenderer::setRealRangeTops), the score
// rendered once; empty: the setting off or no part with the data
std::map<const Part*, double> realRangeTops(const MasterScore* score);
// per library part (none: both settings off, no library, or the rule alone and no phrase on such an articulation)
std::map<const Part*, Volumes> volumes(const MasterScore* score);

// the master trim (Gain stage and master trim above)
struct MasterTrim {
      double db { 0.0 };                    // every library patch's gain stage this much lower (>= 0)
      bool measured { false };              // from the calibration and the crest; false: the fallback (or no trim needed)
      double peakDb { -200.0 };             // measured: the loudest moment's peak estimate, dBFS, untrimmed (powers added)
      double peakAmplitudeDb { -200.0 };    // the same moment's in-phase bound (amplitudes added), for comparison
      double atSeconds { 0.0 };             // where it is
      int sounding { 0 };                   // notes sounding there
      double fallbackDb { 0.0 };            // the old way: the loudest patch's gain over +6 dB (MAX_DB), >= 0
      int notes { 0 };                      // library notes counted
      int uncalibrated { 0 };               // of them, without a curve of their own (the calibration's loudest level used)
      };
// the score's master trim: 0 when none of the level settings above gives a patch a gain (Library default: nothing lowered);
// cached per score until an edit, an undo, another library, calibration, other routes or settings (renders the score once)
MasterTrim masterTrim(const MasterScore* score);
// the estimate itself on the score's untrimmed gains (masterTrim's work; for tests and the audit)
MasterTrim computeMasterTrim(const MasterScore* score);

}     // namespace ContextLevel
}     // namespace Ms
#endif
