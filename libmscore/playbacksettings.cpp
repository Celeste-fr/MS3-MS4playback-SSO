//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "playbacksettings.h"
#include "score.h"

#include <QCoreApplication>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
#include <mutex>
#include <cstring>
#include <string_view>
#include <unordered_map>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QTextStream>

namespace Ms {
namespace Playback {

static const double MAP = std::numeric_limits<double>::quiet_NaN();

// every adjustment this fork makes beyond MuseScore 4's playback that is a number (docs/PLAYBACK_SETTINGS.md
// has where each one acts and how it was measured). The automatic timing and level adjustments are off since
// 2026-10-06 (the owner: notes play as written, timing and levels are adjusted in Live); the settings no longer in
// use went on 2026-10-07 (the owner: remove settings not in use; their keys are ignored: REMOVED below)
static const std::vector<Definition> DEFINITIONS = {
      // [legato]
      // (keepMs chosen by a sweep, numbers-measured 2026-10-03: 0 / 20 / 40 / 60 / 80 / 120 ms on make_fastrun_scores.py's
      // scores, MuseScore 2bc46bc on the Windows VM, 3072 transitions each: median |arrival| 31 / 31 / 31 / 34 / 45 / 94
      // ms, 40 the fewest without an arrival (410 against 424-432); tools/playbackverify/choose_keep_ms.py)
      { "legato/keepMs", 40, 0, 1000, "ms",
        "a note before a held note started early on the same patch keeps at least this much of its length as played", true },
      // (fastShare / fastFullMs fitted, numbers-measured 2026-10-03: tools/playbackverify/fit_fast_share.py on 2580
      // transitions of 12 Performance patches in fast runs at 100-200 bpm, least squares over part x tempo medians;
      // docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "legato/fastShare", 50, 0, 100, "%",
        "after a very short note a transition arrives after this share of its measured delay (SSO is quicker in fast passages; "
        "times an unmeasured patch's bent transition's glide, tuning/bendAtArrival) ...", true },
      { "legato/fastFullMs", 380, 0, 4000, "ms", "... rising linearly to all of it after a note this long (0: always all of it)", true },
      // [heldNotes]
      { "heldNotes/early", MAP, 0, 200, "%",
        "a note that is no legato transition starts this share of its articulation's measured onset early (held notes and, since "
        "2026-10-08, every measured technique; default: the map's <Onset early>, SSO 100)", true },
      // (the owner, 2026-10-08: "make even early the new recommended")
      { "heldNotes/byPitch", 0, 0, 1, "on/off",
        "0: every note of an articulation by the median of its measured onsets (one shift: a run keeps its written "
        "spacing), slurred notes [slurs] quick swaps too; 1: each by its pitch's measured onset (attacks on the beat, a "
        "run's spacing uneven)", true },
      // [shorts]
      { "shorts/byMeantLength", 1, 0, 1, "on/off",
        "1: a short with a measured from= is chosen by how long the note is meant to sound (written length times the factors below); 0: by its written length", true },
      { "shorts/staccato", 50, 1, 100, "%",
        "a staccato note is meant to sound this share of its written length (MuseScore 4's; changed: used for staccato alone)", true },
      { "shorts/staccatissimo", 25, 1, 100, "%", "the same for staccatissimo", true },
      { "shorts/tenuto", 99, 1, 100, "%", "the same for tenuto", true },
      { "shorts/portato", 74.5, 1, 100, "%", "the same for portato (staccato and tenuto)", true },
      // (the owner, 2026-10-24: "why not", to try by ear; 1 by default and in Recommended since he heard the A/B clips the same
      // day: Violins 1 "B for the first set, same for the next set", Celli "B", Oboe "same"; the bassoon not heard. Real
      // players' staccato lasts about a fixed time, not a share: RESEARCH.md 2.5a, staccato_lengths.json, from his reviewed
      // readings)
      { "shorts/staccatoLength", 1, 0, 1, "on/off",
        "1: a library staccato (staccatissimo) note is meant to sound, and is held, no longer than real players sound one: "
        "the map's <Instrument staccatoLength> (staccatissimoLength) seconds, measured (TU Berlin Beethoven 8, Spheres K. 550: "
        "to -20 dB under the note's peak; winds ~0.095-0.16 s, strings ~0.19-0.22 s, staccatissimo 0.16 s from the literature); "
        "the shorter of that and the length above decides the short technique (from=) and the note-off; 0: as MuseScore 4 "
        "(staccato 50 %, staccatissimo 25 % of the written length)", true },
      // [slurs]
      // (the owner, 2026-10-08: slurred violins have no attack; SSO's violin Long peaks after ~1.06 s, Whence's slurred
      // notes last 136-273 ms; 2026-10-08, after comparing by ear: "let's make slurs quick = 2 the recommended preset")
      { "slurs/quick", 2, 0, 2, "",
        "a slurred note shorter than its held technique's measured peak (<Articulation peak>) plays: 0 the held technique; "
        "1 the patch's tenuto short (SSO: Short 1.0); 2 its espressivo long (SSO: Long (Rachm.))", true },
      // (the owner, 2026-10-08: Long (Rachm.) "sounds quieter than plain long"; measured on the Windows VM, perceived peaks at
      // mf held 100-273 ms: Violins 1 about 6 dB under Long, Violins 2 about 3; by pitch and length, not one number;
      // then, at 1: "not the correct volume. violin 1 in bar 5 now sounds really loud relative to the viola and cello":
      // re-measured in the passage (in-context levels, 0.6-2.1 dB); 2026-10-09, on build 37882502550: "1 is the right
      // balance, make it the recommended preset")
      { "slurs/quickLevel", 1, 0, 1, "on/off",
        "1: a note [slurs] quick swaps plays as loud as its held technique would at its pitch and written length (map "
        "quickLevel: the boost that matches Long in the passage, per register); 0: at the swapped technique's own level", true },
      // (the owner, 2026-10-10, after hand-building Whence's Live set R with the strings' slurs on the Performance patches:
      // Recommended plays them so. Reverses "No Performance patches" (2026-10-06) for Recommended only)
      { "slurs/performance", 1, 0, 1, "on/off",
        "1: slurred notes of the string sections whose Performance legato has measured attacks (SSO: Violins 1 and 2, Violas, "
        "Celli, Basses) play as under staff text \"performance\" (staff text \"non performance\" opts out), instead of "
        "[slurs] quick's swap; 0: on the All techniques patch", true },
      // (the owner, 2026-10-12, after trying it by hand in Whence's Velocity lane: "notes within a slur use fingered and two
      // slurs adjacent to each other will use bowed"; Spitfire's Performance legato, slow: 20-84 slurred (fingered), 85-127
      // bowed, support.spitfireaudio.com/en/articles/11815986)
      { "slurs/transitions", 1, 0, 1, "on/off",
        "1: on a Performance legato with listed techniques (SSO's string sections), Auto plays a slurred note's transition "
        "fingered and the first note of a slur that starts where the slur before ends (same patch, no rest, no accent) as a "
        "bowed transition from it, instead of a re-attack; 0: slurred notes bowed, each slur re-attacked", true },
      // (the owner, 2026-10-12: the first note of a phrase "smooth"; measured on Violins 1 - Performance 2026-10-10: smooth
      // velocity 1-9, no bite, -6 dB at ~165 ms; spiccato 10-116, a bite at 50-80 ms then a swell)
      { "slurs/smoothStart", 1, 0, 1, "on/off",
        "1: on a Performance legato with listed techniques (SSO's string sections), Auto starts a note after silence on its "
        "patch with the smooth attack (no bite) instead of spiccato; an accent or marcato still accented, a re-attack right "
        "after a note of the patch still spiccato; 0: spiccato", true },
      // (the owner, 2026-10-14: "sure let's hear them started early", to A/B by ear against the transitions on the beat, his
      // rule since 2026-10-07. Not the removed legato/early, whose key stays ignored: an old playback.ini can't turn this
      // on. Modes 2 / 3 (the coordinator, 2026-10-14, from the research note: a bowed legato note is heard ~23.5 ms after
      // its pitch change starts, Polfreman 2013, ISMIR, SD 22.8 ms). 1 by default and in Recommended since 2026-10-15: the
      // owner, after hearing Whence's fast quarter-tone slurs rendered through SSO, "I think 1 and 3 are both fine (I can
      // hear very little difference btwn them)"; 1 measured closest to the written pitches there (Violas bar 7 93 cents mean
      // error against 123 for 3 and 127 on the beat, Violins 1 bars 5-7 62 / 73 / 97: kthost on the test VM, pYIN))
      { "slurs/transitionsEarly", 1, 0, 3, "",
        "a Performance legato transition (SSO's string sections) is sent early: 0 on the beat; 1 by its modelled arrival (the "
        "patch's measured bendGlide, else legatoDelay by interval, quicker after a short note), the new pitch arrives on the beat; "
        "2 by the measured leave + 23.5 ms, the change is heard on the beat (Polfreman 2013); 3 the middle of the measured glide "
        "on the beat (2 / 3 without a measured glide: as 1); the note before keeps at least [legato] keepMs (and 15 ms) as played", true },
      // (the owner, 2026-10-17, "C" by ear on Whence's Violas bars 8-13 against Recommended: plain SSO Performance, every note
      // on the Performance patch at its written start, velocity 64, the whole part early by one measured constant per patch;
      // 1 in Recommended and by default since 2026-10-17: the owner chose it, and the all-notes constants, by ear on the five
      // string sections of Whence; 0 in Library default)
      { "slurs/plainPerformance", 1, 0, 1, "on/off",
        "1: every note of a part whose instrument has a Performance legato with measured attacks (SSO's string sections) plays "
        "on it (staff text \"non performance\" opts out; pizzicato, tremolo, harmonics, mutes ... keep their technique): at its "
        "written start, velocity 64, a staccato a spiccato attack ending 45 ms before the next note, no early start of its own; "
        "every event of the patch sent the map's plainShift early (one measured constant per patch, 0 where not measured); "
        "0: as the other settings choose", true },
      // (2026-10-21, branch plain-timing: the heard check of "Mixed techniques" (Kontakt 8 + SSO, build 457) found plainShift's
      // one constant per patch places the Performance attacks 21-74 ms early and its transitions 88-191 ms late, the sections
      // 160-230 ms apart on each beat; 0 in both presets until the owner has heard it)
      { "slurs/plainTiming", 0, 0, 1, "on/off",
        "with [slurs] plainPerformance, 1: each plain note starts early by its own model instead of the patch's one plainShift: "
        "an attack (after silence, a staccato's spiccato attack) by the patch's measured plain attack (map plainAttack, 15 dB "
        "under the peak), a transition by its modelled arrival ([slurs] transitionsEarly's mode, 1 where that is 0); a modifier "
        "the section has no sample for (SSO's Basses: con sord., espressivo) stays on the Performance patch; 0: the patch's "
        "events all go plainShift early", true },
      // (the owner approved, 2026-10-17: MuseScore takes over the Performance script's CC119 (its "Leg. Speed CC"), set from
      // the written spacing so that early starts can't move a transition into another speed band; 0 in both presets until
      // the owner has heard it after the null test. Per score: the hosted instances of a score whose value differs are
      // loaded again with their state, SoundLibraryHost)
      { "slurs/speedFromWritten", 0, 0, 1, "on/off",
        "1: on the Performance patches whose script picks the transition's speed variant by a controller it writes from the "
        "note-on spacing (SSO's string sections: CC119), that controller is MuseScore's: the script's own writing is off in "
        "the patch's state and each legato note-on gets the value from the WRITTEN spacing, so notes started early keep their "
        "band; 0: the script writes it from the spacing as played", true },
      // (2026-10-21, branch fast-microtones: the owner heard Whence's Violas bar 7, sixteenths of A3 / B3 -50 / A3 / G3 +50 on
      // Violas - Performance, "the pitch bending sounds weird". Measured (kthost + Kontakt 8 + SSO on the test VM, pYIN): the
      // bend glided as bendGlide says, measured on the bowed transition (arrival 130 ms), while plainPerformance plays the
      // fingered one (64), which reaches a step ~180 ms after its note-on, so each quarter tone's bend detuned the note still
      // sounding, against the step: the pitch hooked a quarter tone away first (B3 -50 heard at G#3 +50); 0 in both presets
      // until the owner had heard it; 1 by default and in Recommended since 2026-10-22, the owner on Whence's Violas alone:
      // "B is so much better it actually sounds right now"; MidiRenderer's microtoneSpelling, libraryPitchBends)
      // 3 (2026-10-22, branch microtone-next; measured on the Windows test VM): as 1, and each plain fingered transition on
      // such a patch early by its glide's middle less the patch's plainShift; 3 by default and in Recommended since
      // 2026-10-22, the owner on Whence's Celli and Violins 1 alone with a click: "celli and violins 1 both B"
      { "slurs/microtoneBends", 3, 0, 3, "",
        "a microtonal note reached by a legato transition on a patch that bends (SSO's Performance patches): 1 goes on the key "
        "on the side of the note before (B3 -50 after A3 on B-flat3 +50), so the key's step and the bend move the same way, "
        "and its bend glides with the transition that plays (a fingered one by the map's bendGlideFingered, measured apart "
        "from the bowed one); 2 the glide only; 0 as written, the bend timed by the bowed transition's bendGlide; 3 as 1, and "
        "each plain fingered transition there sent early so that its glide's middle comes on the beat (by bendGlideFingered, "
        "less the plainShift its patch already goes early: Violins 1 70 ms, Celli 61, Violas 0)", true },
      // (2026-10-29, branch bowed-join, OPEN_ITEMS 5: SSO's fingered legato in sixteenths at 110 bpm never reaches its keys,
      // its f0 the keys sent delayed and smoothed (Whence's export, Kontakt 8 + SSO, pYIN, bends taken out: median error
      // 13-15 c against the map's runLag); offline on three takes of builds 465 / 485 the quarter tones in tune 24-40 -> 38-53 %,
      // the 12-TET notes 51-58 -> 65-71 %; 0 in both presets until the owner has heard it; MidiRenderer::libraryPitchBends;
      // 2026-10-30, branch run-bends-fingered: no bowed correction, the Celli's fingered lag 90:90: docs/PLAYBACK_SETTINGS.md;
      // 2026-10-31: 1 in Recommended and by default, 0 in Library default: the owner, A/B page, off vs the fix, "the fix is obv better")
      { "slurs/runBends", 1, 0, 1, "on/off",
        "1: in a fast legato run (notes under 250 ms joined by transitions) on a patch that bends and whose map has runLag "
        "(SSO's Violins 1 / 2, Violas, Celli - Performance), the pitch bend also corrects the patch's own lagging pitch, so "
        "each note, 12-TET or microtonal, is heard at its pitch from its written start as far as the bend's range and "
        "smoothing allow; 0: the bend plays the note's tuning only (microtoneBends)", true },
      // (2026-10-20, branch sso-legato-fixes: measured on the Windows test VM, 0 in both presets until the owner has heard
      // it; SoundLib::fixedTransitions. Per score: the hosted instances of a score whose value differs are loaded again)
      { "slurs/fixedTransitions", 0, 0, 1, "on/off",
        "1: the Performance patches' random transition variation off (SSO's string sections: the modulators from Kontakt's "
        "random generator on their portamento and run transitions at 0 in the patch's state), so every play of a passage "
        "sounds the same, as loud as the library's takes on average; 0: as the library plays them, each play a little "
        "different", true },
      // (2026-10-22, branch slur-joins: a plain note touching a plain note of its patch at another pitch kept on that note's
      // copy, so SSO plays a transition, not an attack on another tuning copy; the owner heard Whence's Violas bars 12 / 16
      // both ways the same day: "violas A better for both", A the attack on its own copy: an unslurred note takes a new bow;
      // 0 everywhere, SoundLib::lanes' plainJoin)
      { "slurs/plainJoins", 0, 0, 1, "on/off",
        "1: an unslurred plain Performance note touching the note before at another tuning stays on that note's copy and "
        "plays as a transition from it; 0: it goes on the copy at its own tuning and starts with its own attack, as a new "
        "bow stroke", true },
      // (2026-10-26, branch celli-bowed: the owner on Whence's Celli bars 1-2, a slur's first note touching the slur before,
      // its Velocity lane "64 · fingered": "this is not supposed to be fingered. it's supposed to be bowed", and "slurs should
      // have smooth start when there's nothing preceding it". [slurs] transitions / smoothStart (2026-10-12) play them so;
      // plainPerformance (2026-10-17) sent every plain note at 64, so both were lost. One setting for both (he worried about
      // "too many rules"; widened the same day: every note after a rest smooth, every new bow bowed; first [slurs]
      // plainBowed, never in a Windows build: REMOVED). 1 by default and in Recommended,
      // 0 in Library default; performanceNote's plainAuto). 2 (branch bowed-early, 2026-10-27, for the owner to compare by
      // ear; 1 stays the default and Recommended's until he decides): as 1, each new bow stroke early by the map's bowedEarly
      // (the bowed transition's measured heard time less plainShift, sso_legato_glide_bowed.json; collectNote's bowedEarly)
      { "slurs/plainAuto", 1, 0, 2, "",
        "with [slurs] plainPerformance: 1 smooth after a rest, bowed on each new bow, fingered inside a slur, spiccato for "
        "staccatos: a plain note (no velocity or technique of its own) after silence on its patch plays the smooth attack "
        "(velocity 5), early by its measured onset less the patch's plainShift; a note touching the note before outside a "
        "slur going on the bowed transition (SSO: 93, Violas 100); inside a slur 64, SSO's fingered; staccatos, accents and a "
        "key struck again as before. 2: as 1, and each new bow stroke (the bowed transition) sent early by its patch's "
        "measured bowedEarly beyond plainShift (Violins 1 125, Violins 2 151, Violas 55, Celli 106, Basses 64 ms), capped so the "
        "note before keeps keepMs. 0: every such note at 64", true },
      // (2026-10-20, branch sso-legato-fixes: measured on the Windows test VM; 1 in Recommended and by default, 0 in
      // Library default, since 2026-10-28: the owner, A/B, "the last 3 notes sound slightly more in time";
      // MidiRenderer::libraryFastTransitions)
      { "slurs/fastTransitions", 1, 0, 1, "on/off",
        "1: a legato note started less than 15 ms after the note before on its patch, which SSO's legato ignores at velocity 64 "
        "or less (the audit's DROPPED), is sent at velocity 65, the lowest it plays, the same transition family (velocities "
        "20-64 only: a portamento has none above 64); 0: sent as chosen", true },
      // (2026-10-20, branch plain-accents: plainPerformance played every accent as a plain note, velocity 64 and no MS4
      // offset (private/bars9-12/ACCENT.md); four ways to compare by ear, 0 in both presets until the owner has heard them;
      // performanceNote, marcatoLevel's plainAccentDb). 2 in Recommended and by default since 2026-10-21 (branch accent-level;
      // the owner on hearing 0-3: "ig 2 is preferable, but is accented normally that loud? if yes then I'm fine with it"):
      // 2's accented attack at real players' level, +5.0 dB over the plain note (rendermidi.h PLAIN_ACCENT_DB, the map's
      // measured accentedLevel)
      { "slurs/plainAccents", 2, 0, 3, "",
        "[slurs] plainPerformance's notes with an accent, marcato or soft accent: 1 MuseScore 4's offset for its articulations "
        "(40 log10 of its velocity over a plain note's, as [levels] calibrated gives a technique on velocity: strings' accent "
        "+5.3 dB), by the dynamics CC along the patch's own curve, slurred notes too; 2 an accent or marcato that plays an "
        "attack (not a slur's transition) the Performance legato's accented attack (SSO: velocity 122) instead of velocity "
        "64, a note touching the one before re-attacked, by CC11 as loud as real players' accent (+5.0 dB over the plain "
        "note, where the library measured its accented attack); 3 both; 0: as a plain note", true },
      // [levels]
      // (the owner, 2026-10-08: "bring back the calibrated short velocity and include it in the recommended preset",
      // "MuseScore 4.7.5's articulation profiles for everything incl. marcato"; controller techniques keep the library's
      // balance: measured sensible, docs/PLAYBACK_SETTINGS.md › Calibrated levels; 2026-10-15, the owner, option B: "yes
      // this makes sense": matched + the recorded loud-end difference, 2, in Recommended and by default; option B', the
      // owner: "if the data clearly point to b', then b' it is": D from real players where measured)
      { "levels/calibrated", 2, 0, 2, "",
        "2: a technique on velocity (the map's <Dynamics velocity>: shorts, pizzicato ...), where measured, as loud as the "
        "part's held note at the same dynamic plus D, real players' difference where the map lists one (heldOffset: SSO's "
        "string staccatos -3.4 dB, pizzicato -5 .. -12 dB), else its level at velocity 127 less the held note's at 127 (both "
        "recorded loud), plus MuseScore 4's offset for its articulations (40 log10 of its velocity over a plain note's, marcato's too); "
        "the level rule's slur gains, measured against the held note, don't take D; "
        "1: as 2 without D; 1 and 2: a Performance legato note (on the dynamics CC) at the CC at which its patch sounds as "
        "loud as the part's held note; 0: at the dynamic's level, MuseScore 4's accent share on top, a marcato at the plain "
        "level, the Performance legato at the written CC", true },
      // (the owner, 2026-10-10: the same written dynamic sounds equally loud in every string part and technique, judged in
      // the music; by volume only, never by raising CC1 (a timbre crossfade); one level per slur; contextlevel.h)
      { "levels/context", 1, 0, 1, "on/off",
        "1: a slur on a Performance legato with a measured context level (map contextLevel: SSO's five string sections) plays "
        "CC1 at the written dynamic and one gain per slur (by its dynamic, median note spacing and median pitch) that makes it "
        "as loud as its section's held Long at the same dynamic (measured in MuseScore's own renders); past CC11's top the Performance patches' volume plays it, "
        "every other patch of the library's parts lower together (Live's Volume top, +6 dB); 0: [levels] calibrated's CC1", true },
      // (the owner, 2026-10-11: M, the real orchestra's balance at CC1 64 (Weinzierl et al. 2018's sound power, SSO's
      // section sizes) against SSO's measured one (sso_cc1_curves.json); on in Recommended, off in Library default;
      // contextlevel.h, gen_spitfire_sso.py SECTION_FADERS; 2026-10-14, the owner: "I prefer all the balance levels and the
      // dynamic levels loudness to be based on real data": 2, Clark & Luce balance, in Recommended and by default;
      // section_balance_cl.py)
      { "levels/sectionBalance", 2, 0, 2, "",
        "every patch of a section with a map section level (SSO's string sections, woodwinds and brass) plays that many dB "
        "louder, staccatos and Performance alike, by the patches' volumes: a real orchestra's balance at mp against SSO's "
        "measured levels (with [levels] context the Performance patches' volume carries the section's largest slur gain too; "
        "everything lower together so the loudest stays at Live's Volume top, +6 dB). 2: Clark & Luce balance (sectionLevel2: "
        "scales played at the markings as in orchestral music, Clark & Luce 1965, as sound power with TU Berlin's directivity, "
        "+ section size; agrees per family with the Spheres dataset's sections and Skaalevik 2019's orchestra measurements); "
        "1: TU Berlin's single notes at Meyer's mp (sectionLevel); 0: every section at the same level", true },
      // (the owner, 2026-10-12, stage A of the dynamics plan: each note as loud as a real player at its written dynamic,
      // the player's full range; rule D: TU Berlin's pp / ff are ppp / fff, 7 equal dB steps, relative to mp; 2026-10-13,
      // heard on the ladder: "this sounds great": on in Recommended and by default, off in Library default; contextlevel.h ›
      // Real range; 2026-10-14, the owner: "let's use the section data wherever it's reliable", one source for every
      // instrument: 2, Clark & Luce spans, in Recommended and by default)
      { "levels/realRange", 2, 0, 2, "",
        "each note of a patch with a map realSpan (SSO's string sections, woodwinds, brass) as loud as a real player at its "
        "written dynamic, from mp, less what the library's held note already changes with CC1, by CC11 (following CC1 "
        "through hairpins) and the patches' volumes (everything lower together past Live's Volume top, +6 dB); a short at "
        "velocity 127 still under its calibrated level gets the rest by CC11. 2: Clark & Luce spans: the markings in equal "
        "steps of the instrument's pp -> ff as orchestral players play them (map realSteps: Clark & Luce 1965, Table I), "
        "ppp / fff one step more (a patch without realSteps as 1); 1: the Meyer rule: ppp / fff the player's "
        "softest / loudest single note (Weinzierl et al. 2018), pp / ff the levels of normal playing (Meyer 2009; else "
        "ppp .. fff in 7 equal steps), p .. f equal steps between; 0: the library's own range", true },
      // (the owner, 2026-10-15: "mic changes then only affect tone and sense of space, never the balance": a part's mic
      // Controllers / lanes give another mix, its patches' volumes make up the measured level change; contextlevel.h › Mics,
      // sso_mic_levels.json; on in Recommended and by default, off in Library default)
      { "levels/micCompensation", 1, 0, 1, "on/off",
        "1: a part whose mic controllers (Mic 1-5 level, Mic Mix Distance: values or automation) give another mix than the "
        "patch's default plays as loud as the default mix (each mic's measured level, the faders' measured law, mics added as "
        "powers), by the patches' volumes (everything lower together past Live's Volume top, +6 dB); automated mics: the "
        "largest change by the volume, each note's own at its start by CC11; the mics then change tone and space, not the "
        "balance; 0: the mix's own level", true },
      // (the owner, 2026-10-20, "yes": a chord on a monophonic patch (SSO's Performance patches, voice lanes) as a real
      // section divided, the chord as loud as one section; an assumption from player counts, no divisi data; on in
      // Recommended and by default, off in Library default; SoundLib::divisiDb)
      // (branch slur-joins, 2026-10-22: Whence's Violas bars 12 / 16, a slur's F#4 into the G4 half note on one copy, the half's
      // lower CC11 sent with its note-on turned the F#4 down 6-7 dB over its last 100 ms; 0 in both presets until the owner
      // has heard it, proposed 1 in Recommended)
      // (2, branch bowed-join, 2026-10-28: the owner on the Hall Guard A/B page, hall.musicxml: "both have the problem of the
      // tail of the last note suddenly get louder as the next note enters"; the dynamics CC's step too, CC1 48 -> 96 with a
      // bowed note-on ~190 ms before it is heard; a fix of the per-route schedule: 2 by default and in Recommended, 0 in
      // Library default; libraryTransitionTails)
      { "levels/transitionLevels", 2, 0, 2, "",
        "1: where a legato transition's level (CC11: level rule, real range, track and note levels) differs from the note "
        "before's on its copy, the note before keeps its own level to the transition's glide leave, then CC11 moves to the "
        "new note's linear in dB, reaching it at the glide's arrival (SSO's measured bendGlide, else the legato delay); "
        "2: the same for every step a transition's note-on brings on its route, the dynamics CC (the written dynamic it "
        "starts on) and CC11 from whatever sets it, so what still sounds of the note before keeps its level until the "
        "transition leaves it; 0: the new values with the note-on, while SSO still plays the note before", true },
      // (branch attack-levels, 2026-10-22: Whence's Violas bar 12, the slur's first note, an attack after a staccato on the same
      // key, raised the staccato's release +3.4 dB before the beat with its level-rule CC11 sent with the note-on, 111 ms
      // early by plainShift; 2026-10-23, the owner, "B better": Whence's Violas bars 11-13 alone with a click, even levels on,
      // kthost renders, 2 against 0: 2 by default and in Recommended, 0 in Library default)
      { "levels/attackLevels", 2, 0, 2, "",
        "an attack (no legato transition) whose route still rings the note before (within that note's release) takes "
        "its CC11 level (level rule, real range, track and note levels, even levels) 1: at its note-on + its measured "
        "onset (plain Performance: the patch's plainAttack, else the articulation's attack onset); 2: at its written time "
        "(its early start undone, plain Performance's plainShift too), where its attack is meant to be heard; either only "
        "while that stays before the route's next note or written CC11 / dynamics; 0: the new level with the note-on, "
        "lifting the note before's release before the attack is heard", true },
      { "levels/divisi", 1, 0, 1, "on/off",
        "1: while n notes of a monophonic patch's part sound together (a chord on SSO's Performance patches, each note its "
        "own voice copy), each plays 10 log10(n) dB softer by CC11 (2: -3.0 dB, 3: -4.8 dB), the line's note too, as a "
        "section divided into n groups adds up to one section (an assumption from player counts, not measured divisi "
        "data); a note keeps the level of its own start; 0: every voice at the whole section's level", true },
      // (the owner, 2026-10-28: SSO's uneven keys are an artefact; every pitch at the real player's level, smoothed only
      // beyond the key-to-key scatter real players show; approved the approach: 1 in Recommended and by default, 0 in
      // Library default)
      { "levels/evenKeys", 1, 0, 1, "on/off",
        "1: with the real range on, a key of a wind or brass patch's Long whose level at mp lies more than 1.5 dB off a "
        "smooth curve over pitch (SSO's samples come in zones with jumps of up to ~9 dB; real players' keys scatter "
        "1.1 dB, TU Berlin database) plays the excess back by CC11 (the map's keyLevels, measured every semitone); the "
        "round robins can't be evened per note; 0: every key at the library's own level", true },
      // (the owner, 2026-10-12: SSO's Performance legato plays some slurred notes louder than others, by context; evened
      // out by a glide of CC11 measured in place, the blind listening test's choice; 2026-10-13, MuseScore's playback
      // verified on the VM (3.6 -> 0.60 dB), the owner: "why is this not in recommended": on in Recommended and by default,
      // off in Library default; it plays only corrections Tools > Even Out Levels... stored; evenlevels.h)
      { "levels/evenLevels", 1, 0, 1, "on/off",
        "1: each slurred note with an even-levels correction (measured in place by Tools > Even Out Levels...) plays it: CC11 "
        "gliding in dB between the notes' heard windows, every 5 ms, so a slur's notes sound equally loud; a correction "
        "whose note's context changed since (an edit nearby) is stale and not played; the largest correction of a patch "
        "by its volume (within Live's +6 dB); 0: as the library plays them", true },
      // (branch string-lowcut, 2026-10-30: SSO's new-bow samples on the string sections carry a low burst, 1-15 Hz and
      // 40-150 Hz, 150-250 ms after each new bow (bowed transition, smooth attack), heard as a kick in fast runs with a slur
      // per beat; the owner asked why it isn't fixed: 1 in Recommended and by default, 0 in Library default. The map's
      // <Instrument lowCut>, just under each section's lowest note: gen_spitfire_sso.py LOW_CUT; Vst3Synth::setLowCut)
      // (branch follow-cut, 2026-10-31: 2, the following cut, the owner approved by ear the same day: Violins 1 bars 5-6,
      // the per-beat burst at 150-225 Hz, "yes, much better now"; 2 in Recommended and by default, 1 the fixed cut,
      // 0 in Library default. libmscore/followcut.h)
      { "levels/lowCut", 2, 0, 2, "",
        "Low cut on string sections. 1: each patch with a low cut in the map (SSO's five string sections) plays through a "
        "4th-order high-pass just under the section's lowest note (Violins 150 Hz, Violas 100, Celli 50, Basses 25), "
        "taking out the low bow and body noise of the new-bow samples (a kick in fast runs) and the room rumble; the "
        "lowest note loses about 0.5 dB, a fourth higher 0.05 dB; 2: following, as 1 but the cut moves up with the notes, "
        "to 0.77 x the lowest note sounding or released within the last 2 s on the patch (smoothed over 100 ms, never "
        "under 1's), taking out the burst and thud between the fixed cut and the notes (Violins 1: the runs' burst at "
        "150-225 Hz -25 dB); MuseScore's playback and audio export, Live's plain set (MIDI out: none); 0: the "
        "library's own low end", true },
      // (2026-10-21, branch sso-script-fixes2: SSO's Performance patches play an extra diminuendo release on every held key at
      // a CC1 step down of 10 or more (read from the patch's script and group data; measured on the Windows test VM); it
      // changes the sound, so 0 in both presets until the owner decides; MidiRenderer's libraryHallGuard)
      { "levels/hallGuard", 0, 0, 1, "on/off",
        "1: on the patches that play an extra diminuendo release sample when the dynamics controller drops by their "
        "threshold or more in one step while a note is held (SSO's string sections' Performance patches: 10), such a step "
        "(a subito dynamic inside a slur, fp) is sent as smaller steps 1 ms apart ending at its time, so no release plays; "
        "0: sent in one step, as the notation gives it (the playback audit's HALL)", true },
      // [notes]
      { "notes/sameKeyEndsFirst", 1, 0, 1, "on/off",
        "1: a key struck again on the same patch while its last note still sounds ends that note just before (a sampler ends a key at its first note-off)", true },
      // (the owner, 2026-10-10: "the weird square wave-y noise problem" in Whence's Celli; measured: one round robin of
      // SSO's Celli - Performance C3 spiccato attack, tools/soundlibraries/sso_bad_samples.json)
      { "notes/avoidBadSamples", 1, 0, 1, "on/off",
        "1: an attack at a velocity whose recording at that key is faulty (the map's avoid: SSO's Celli - Performance C3) plays "
        "at the nearest clean velocity measured; a note's own velocity stays; 0: as chosen", true },
      // (2026-10-21, branch sso-script-fixes2, from reading SSO's scripts: the string sections' Performance patches and Tuba
      // Solo - Performance have "Reset on transport" saved off, so restartTransport() doesn't reset their round robins; it
      // changes which take plays: 1 in Recommended and by default, 0 in Library default since 2026-10-28 (the owner: "I don't see
      // a reason not to, since it literally changes nothing within one play"); SoundLib::transportReset. Per score: the
      // hosted instances of a score whose value differs are loaded again)
      { "notes/transportReset", 1, 0, 1, "on/off",
        "1: the patches whose round robins don't go back to the first take at a playback start (SSO's string sections' "
        "Performance patches, Tuba Solo - Performance: \"Reset on transport\" off) load with it on, so every play, export and "
        "verification pass from one place picks the same takes; 0: as the library has it (they reset only after 1.5 s "
        "without a note)", true },
      // [guitar] (a guitar library's <Guitar>: Ample Guitar M; guitarplayback.cpp. Choices of sound, so off by default and in
      // Library default until the owner decides by ear, 2026-10-20; the notation's techniques themselves (strings, capo,
      // harmonics, slides, hammer-ons, FX texts) are not settings)
      { "guitar/strums", 0, 0, 1, "on/off",
        "1: a chord with a strum line (a straight arpeggio, stretch at most 1) plays the guitar library's strummer: Detect mode, "
        "the chord's notes as chord keys, a full strum down (arrow up: low strings first) or up, muted when every note has a "
        "cross notehead, as loud as its notes would be by CC7 (the map's StrumLevel); 0: its notes, spread as MuseScore "
        "plays an arpeggio", true },
      { "guitar/softStrums", 0, 0, 2, "",
        "with guitar/strums, a strum at velocity <= the map's softStrum (Ample: 30, where a roll is darker than a quieter "
        "strum: measured): 1 its notes rolled rollGap ms apart (strum mode off, at their velocity); 2 a roll of the "
        "strummer's single-string keys (the map's rollKeys, strum mode on, the chord keys held) at the chord's velocity, "
        "spaced by the map's softRoll (measured: a roll at velocity 20 / 40 with 10 / 15 ms is as loud as the recorded "
        "strum turned down by CC7), no CC7 change (what still rings keeps its level); 0: the strummer turned down by CC7 "
        "(StrumLevel)", true },
      { "guitar/pinchHarmonics", 0, 0, 1, "on/off",
        "1: a note under staff text P.H. / pinch harm. plays a layered approximation (the natural harmonic at its partial, "
        "then the fretted note palm-muted and short: the map's pinch, measured closest by numbers); 0: the note as written", true },
      // [tuning]
      { "tuning/tolerance", MAP, 0, 50, "cents",
        "a note this close to a tuning copy's tuning plays on it (default: the map's <Tuning tolerance>, else half the smallest gap between two accidentals' values, 0.083: ScoreTuning::smallestAccidentalGap)", true },
      { "tuning/tail", MAP, 0, 10, "s", "a copy rings this long after its last note before it is retuned (default: the map's, else each "
        "note's measured release to 60 dB under: twice its release to 30 dB, as ISO 3382-1 extrapolates T30)", true },
      { "tuning/maxLanes", MAP, 1, 16, "copies", "at most this many copies of a patch for other tunings (default: the map's, else "
        "no maximum: as many as the music needs, with a warning when they need more memory than is free, 245 MB a copy)", true },
      { "tuning/waitForRelease", 1, 0, 1, "on/off",
        "1: a copy also waits for its notes' measured release (<Articulation release>) before it is retuned", true },
      { "tuning/pitchBend", 1, 0, 1, "on/off",
        "1: patches that bend cleanly (<Instrument bend>) are tuned by pitch bend; 0: by varispeed", true },
      { "tuning/bendAtArrival", 1, 0, 1, "on/off",
        "1: a legato transition's pitch bend glides with the transition's own pitch (the map's measured bendGlide; else "
        "from its arrival, note-on plus the measured legato delay), so the note before keeps its tuning while it sounds; "
        "0: at the note-on", true },
      // (the owner, 2026-10-15: "is there any way at all we can make SSO load even faster" … "Whence takes absolutely forever
      // to load all the patches because the microtonal stuff"; a patch's copies in one Kontakt, as the plain Live set's
      // multis: soundlibrary.h › hosted tuning; varispeed kept to compare by ear)
      { "tuning/hostedTuning", 1, 0, 1, "1 slots / 0 varispeed",
        "hosted Kontakt: 1 (slots): a patch's copies for other tunings play from one instance, a slot per tuning, each tuned "
        "by Kontakt's own Tune on its own MIDI channel, as in the Live set (one load per patch, ~85-90 MB less a copy); "
        "0 (varispeed): a Kontakt per copy, retuned by varispeed once silent (before 2026-10-15). Also \"slots\" / \"varispeed\"", true },
      // [live]
      // (measured in Live 12.4.6: 1 and 0 units keep every carrier before its note; 1 is the smallest the range allows:
      // LiveClips::EPSILON, docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "live/carrierEpsilon", 1, 1, 50, "clip units",
        "in Live clips a controller carrier note sits this far before the note it belongs to (1: ~0.13 ms at 120 bpm)", true },
      // [hosting] (global: the hosted plug-ins are the same for every score)
      { "hosting/maxVoices", 512, 0, 4096, "voices",
        "every Kontakt patch MuseScore sets up gets this voice limit (0: the patch's own; MS_KONTAKT_MAX_VOICES wins); also in the Live Set", false },
      // (measured: a patch's script keeps a parameter set after 1025 frames; 1025 / 22050 Hz, the lowest rate
      // offered, rounded up to 1 ms: Vst3Plugin::SETTLE_SECONDS, docs/PLAYBACK_SETTINGS.md › Measured by sweeps)
      { "hosting/settleSeconds", 0.047, 0, 10, "s",
        "a patch just loaded runs this long before its controllers are set (its script initialises)", false },
      // [soundlibrary] (global). hostProcesses: the plug-in's instances in helper processes (MuseScoreLibraryHost, Vst3Remote,
      // audio/vst3/vst3remote.h; the owner approved 2026-10-25): Kontakt does its setState work on its process's GUI thread
      // (check480: MuseScore's window froze 8-18 s per patch) and one CPU-bound thread per process (oop-load: 2-3 processes
      // 1.75-1.9x faster on the VM). 0 keeps them in MuseScore's process (as before); K up to K helpers; -1 (default)
      // automatic: a helper per physical core but one (at least 2 on 2+ cores with 4+ logical processors). Not a sound
      // setting: the helper plays the same calls in the same order (null-tested)
      { "soundlibrary/hostProcesses", -1, -1, 64, "processes",
        "hosted plug-in instances in up to this many helper processes, so that MuseScore's window stays free while they "
        "load, several load at once and a plug-in crash doesn't end MuseScore (0: in MuseScore's own process; -1: "
        "automatic, a helper per CPU core but one). Read when instances load", false },
      // renderAhead (branch render-ahead, the owner approved 2026-10-31): the owner's live playback turned into noise under
      // memory / CPU load while the export of the same score was clean; a callback over its budget is a gap the device
      // fills (the VM, 2026-10-24: callbacks over budget followed the recorded gaps). The producer thread renders up to
      // this much ahead, so a stall shorter than it is never heard. Not a sound setting: the same blocks, the same audio
      // (null-tested); changes made while playing are heard this much later; Play's start waits up to it. MS_RENDER_AHEAD
      // wins (mscore/seq.h › Render ahead)
      { "soundlibrary/renderAhead", 500, 0, 2000, "ms",
        "playback's audio is computed this far ahead of the audio device, so that a moment when other programs hold the "
        "CPU or memory is not heard (0: computed as the device asks for it, as before). Changes made while playing are "
        "heard this much later. Read when the audio device starts", false },
      };

const std::vector<Definition>& definitions()
      {
      return DEFINITIONS;
      }

bool Definition::mapDefault() const
      {
      return std::isnan(value);
      }

// (the lookups below ran 100 string compares, and a QString made of the id, per read; an index of the same ids, the first
// of a duplicate winning as the loop did)
static const std::unordered_map<std::string_view, const Definition*>& definitionIndex()
      {
      static const std::unordered_map<std::string_view, const Definition*> index = [] {
            std::unordered_map<std::string_view, const Definition*> m;
            for (const Definition& d : DEFINITIONS)
                  m.emplace(std::string_view(d.id), &d);
            return m;
            }();
      return index;
      }

static const Definition* definitionOf(const char* id)
      {
      const auto& index = definitionIndex();
      auto it = index.find(std::string_view(id));
      return it == index.end() ? nullptr : it->second;
      }

const Definition* definition(const QString& id)
      {
      const QByteArray utf8 = id.toUtf8();
      if (utf8.contains('\0'))           // (QString == const char* stops at the first NUL of the id: as before, never equal)
            return nullptr;
      return definitionOf(utf8.constData());
      }

QString sourceName(Source s)
      {
      switch (s) {
            case Source::DEFAULT: return QCoreApplication::translate("playbackSettings", QT_TRANSLATE_NOOP("playbackSettings", "default"));
            case Source::MAP:     return QCoreApplication::translate("playbackSettings", QT_TRANSLATE_NOOP("playbackSettings", "library"));
            case Source::INI:     return QCoreApplication::translate("playbackSettings", QT_TRANSLATE_NOOP("playbackSettings", "playback.ini"));
            case Source::SCORE:   return QCoreApplication::translate("playbackSettings", QT_TRANSLATE_NOOP("playbackSettings", "score"));
            }
      return QString();
      }

//---------------------------------------------------------
//   the ini, read once per (re)load into an immutable snapshot (the renderer reads it on its own thread)
//---------------------------------------------------------

static const char* const TABLES[] = { "legato.delay", "heldNotes.onset", "shorts.from" };

struct Ini {
      std::map<QString, double> values;                       // "section/key" -> value
      std::map<QString, std::map<QString, QString>> tables;   // table -> patch (or "patch|articulation") -> text
      QStringList warnings;
      };

static std::mutex iniMutex;
static std::shared_ptr<const Ini> currentIni = std::make_shared<Ini>();
static QString currentPath;
static std::atomic<int> currentGeneration { 0 };

static std::shared_ptr<const Ini> ini()
      {
      std::lock_guard<std::mutex> lock(iniMutex);
      return currentIni;
      }

static void install(std::shared_ptr<Ini> i)
      {
      for (const QString& w : i->warnings)
            qWarning("playback.ini: %s", qPrintable(w));
      std::lock_guard<std::mutex> lock(iniMutex);
      currentIni = i;
      ++currentGeneration;
      }

static bool isTable(const QString& group)
      {
      for (const char* t : TABLES)
            if (group == t)
                  return true;
      return false;
      }

// settings no longer in use: an older playback.ini or score that has them opens as before, the key ignored without a
// warning (the owner, 2026-10-07: no wall of text). 2026-10-02: the fast-note ramp (legato/ramp*); 2026-10-03: varispeed's
// glide (a step within a cent), the nominal short (measured from=), the Mixer's glide, a fixed automation step (one MIDI
// step at its tick); 2026-10-07 (the owner: remove settings not in use; each was off or at a value that changed nothing):
// the rest
static const char* const REMOVED[] = {
      "legato/rampFromMs", "legato/rampToMs", "legato/rampMaxShare", "legato/glideMs", "shorts/nominalShare",
      "hosting/mixSmoothingMs", "automation/stepTicks",
      "legato/overlapTicks", "legato/slurEndOverlap", "legato/phraseGapMs", "legato/early", "legato/velocity",
      "legato/fastTechnique", "legato/fastFirsts", "legato/fastBelowShare", "legato/levelBalance", "legato/levelMaxDb",
      "legato/levelHeadroomDb", "shorts/calibratedVelocity", "pedal/upAfterMs", "pedal/downAfterMs", "pedal/upMaxShare",
      "pedal/downMaxShare", "dynamics/evenSteps", "tuning/oneInstance", "tracks/mapDelays",
      "slurs/plainBowed",     // (2026-10-26, branch celli-bowed only: became slurs/plainAuto the same day)
      };

static bool removed(const QString& id)
      {
      for (const char* r : REMOVED)
            if (id == r)
                  return true;
      return false;
      }

// one key's text into the snapshot (warns of unknown keys and bad values)
static void take(Ini& i, const QString& group, const QString& key, const QString& text)
      {
      if (isTable(group)) {
            i.tables[group][key] = text.trimmed();
            return;
            }
      const QString id = group + "/" + key;
      const Definition* d = definition(id);
      if (!d) {
            if (removed(id))
                  return;     // (no longer in use: ignored silently)
            i.warnings << QString("unknown key %1 (ignored)").arg(id);
            return;
            }
      const QString t = text.trimmed();
      if (t.isEmpty())
            return;                 // (empty: the default)
      bool ok = false;
      double v = t.toDouble(&ok);
      if (!ok) {
            if (t.compare("on", Qt::CaseInsensitive) == 0 || t.compare("true", Qt::CaseInsensitive) == 0)
                  v = 1, ok = true;
            else if (t.compare("off", Qt::CaseInsensitive) == 0 || t.compare("false", Qt::CaseInsensitive) == 0)
                  v = 0, ok = true;
            else if (id == "tuning/hostedTuning" && t.compare("slots", Qt::CaseInsensitive) == 0)
                  v = 1, ok = true;
            else if (id == "tuning/hostedTuning" && t.compare("varispeed", Qt::CaseInsensitive) == 0)
                  v = 0, ok = true;
            }
      if (!ok) {
            i.warnings << QString("%1: \"%2\" is not a number (ignored)").arg(id, t);
            return;
            }
      if (v < d->min || v > d->max) {
            i.warnings << QString("%1: %2 is outside %3 ... %4 (clamped)").arg(id).arg(v).arg(d->min).arg(d->max);
            v = qBound(d->min, v, d->max);
            }
      i.values[id] = v;
      }

static QString number(double v)
      {
      return QString::number(v, 'g', 10);
      }

QString iniTemplate()
      {
      QString out;
      QTextStream s(&out);
      s << "; MuseScore 3.7 fork: playback adjustments (docs/PLAYBACK_SETTINGS.md has what each one does and why).\n"
           "; Every key is written with its built-in default; change a value to override it for every score.\n"
           "; A key left empty (key=) uses the default; a score can override any of these in\n"
           "; Mixer > Advanced Options... > Playback adjustments. Reload playback.ini there\n"
           "; reads this file again, no restart. MuseScore writes this file only when it is missing.\n"
           "; Presets (Recommended, Library default): the Preset box there; they set only the keys listed in docs/PLAYBACK_SETTINGS.md > Presets.\n"
           "; Lines starting with ';' are comments. On/off settings: 1 or 0.\n";
      QString section;
      for (const Definition& d : DEFINITIONS) {
            const QString id = d.id;
            const QString group = id.section('/', 0, 0);
            const QString key = id.section('/', 1);
            if (group != section) {
                  section = group;
                  s << "\n[" << group << "]\n";
                  }
            s << "; " << d.comment << " (" << d.unit << "; " << number(d.min) << " ... " << number(d.max)
              << (d.perScore ? "" : "; global, not per score") << ")\n";
            if (d.mapDefault())
                  s << key << "=\n";
            else
                  s << key << "=" << number(d.value) << "\n";
            }
      s << "\n; Per-patch tables (the sound library map's measured data), by patch name as in the map, or\n"
           "; \"patch|articulation\". A value is an offset in ms (+25, -30) added to the map's, or a whole\n"
           "; table that replaces it: legato delays by interval (-12:240 -7:280 +2:220 ... +12:440), onsets\n"
           "; by MIDI pitch (55:60 72:40 ...), or one number for all.\n"
           "\n; a legato transition's delay (ms; where the map has no bendGlide, a bent transition's glide starts when it arrives:\n"
           "; tuning/bendAtArrival)\n"
           "[legato.delay]\n"
           "; Violins 2 - Performance=+25\n"
           "\n; a note's onset (ms; it starts that much early, times heldNotes/early)\n"
           "[heldNotes.onset]\n"
           "; Violins 1|Long Flautando=-50\n"
           "\n; a short's from= (s: the meant sounding length from which it is chosen; an offset in s, or a number)\n"
           "[shorts.from]\n"
           "; Violas|Short 0.5=0.55\n";
      return out;
      }

//---------------------------------------------------------
//   presets
//    Only the held notes' early start (heldNotes/early, heldNotes/byPitch), the calibrated levels (levels/calibrated,
//    levels/context, levels/sectionBalance, levels/realRange) and
//    the slurred notes' technique (slurs/quick, slurs/quickLevel, slurs/performance, slurs/transitions, slurs/smoothStart) differ between them;
//    slurs/transitionsEarly (1 in Recommended since 2026-10-15, 0 in Library default); slurs/plainPerformance (1 in Recommended and by default, 0 in Library default,
//    since 2026-10-17: the owner chose it by ear on the five string sections); slurs/plainTiming (0 in both since 2026-10-21
//    until the owner has heard it); slurs/speedFromWritten (0 in both since
//    2026-10-17: the owner flips it after the null test and listening); slurs/fixedTransitions (0 in
//    both since 2026-10-20; the owner, 2026-10-28: "keep A"); slurs/fastTransitions (0 in both 2026-10-20, 1 in Recommended and by default,
//    0 in Library default, since 2026-10-28: the owner's A/B); levels/hallGuard (0 in both since 2026-10-21 until the owner decides); notes/transportReset (0 in both from
//    2026-10-21, 1 in Recommended and by default, 0 in Library default, since 2026-10-28: the owner's choice); slurs/microtoneBends (0 in both 2026-10-21, 1 in Recommended since 2026-10-22: the owner heard it); slurs/runBends (0 in both 2026-10-29; 1 in Recommended and by default, 0 in Library default, since 2026-10-31: the owner, A/B page, "the fix is obv better"); slurs/plainAuto (1 in Recommended and by default, 0 in Library default, since 2026-10-26: the owner, "supposed to be bowed", "smooth start"); slurs/plainAccents (0 in both from 2026-10-20 until the owner
//    had compared its values by ear; 2 in Recommended and by default, 0 in Library default, since 2026-10-21: his choice,
//    at real players' level); levels/divisi (1 in Recommended and by default, 0 in Library default, since
//    2026-10-20: the owner approved the rule); levels/transitionLevels (0 in both since 2026-10-22, branch slur-joins, until the
//    owner has heard it; 2 in Recommended and by default, 0 in Library default, since 2026-10-28: a fix, the note before's tail
//    lifted by the next transition's step, branch bowed-join); levels/attackLevels (0 in both on 2026-10-22, branch attack-levels; 2 in Recommended and by default, 0 in Library
//    default, since 2026-10-23: the owner heard it, "B better"); shorts/staccatoLength (0 in both on 2026-10-24, branch
//    staccato-length; 1 in Recommended and by default, 0 in Library default, since the same day: the owner heard the clips, "B");
//    levels/evenKeys (1 in Recommended and by default, 0 in Library default, since 2026-10-28: the owner approved the approach); levels/lowCut (1 in Recommended and by default, 0 in Library default, since 2026-10-30: the owner, the strings' new-bow kick; 2, following, since 2026-10-31: the owner heard it, "yes, much better now"). The other settings are not in the table because: legato/keepMs acts
//    only with early starts (so it follows heldNotes/early); the shorts' lengths are MuseScore 4's note model (not a
//    timing adjustment of this fork); tuning and hosting are mechanics (how microtones and plug-ins work), not a
//    choice of sound.
//---------------------------------------------------------

const std::vector<Preset>& presets()
      {
      static const std::vector<Preset> P = {
            // (the owner, 2026-10-07: Library default "is very late, but at least it sounds consistent"; 2026-10-08: "make
            // even early the new recommended": early, each patch by its median onset)
            // (2026-10-08: "let's make slurs quick = 2 the recommended preset"; 2026-10-09, quickLevel re-measured in context:
            // "1 is the right balance, make it the recommended preset"; 2026-10-10: the strings' slurs on Performance, and the
            // level rule: levels/context; 2026-10-11: the section balance M, levels/sectionBalance; 2026-10-12: fingered within a slur,
            // bowed between slurs, slurs/transitions; a phrase's first note smooth, slurs/smoothStart; 2026-10-13: the real
            // player's range, levels/realRange; 2026-10-14: slurs/transitionsEarly 0 in both until the owner approves it;
            // levels/realRange 2 and levels/sectionBalance 2: the balance and the markings' levels from real data; 2026-10-15:
            // levels/micCompensation, the mics never change the balance; levels/calibrated 2, the technique balance (option
            // B'); slurs/transitionsEarly 1, the transitions sent by their modelled arrival: the owner by ear on Whence's fast
            // quarter tones)
            { "recommended", QT_TRANSLATE_NOOP("playbackSettings", "Recommended"), { { "heldNotes/early", 100 }, { "heldNotes/byPitch", 0 }, { "levels/calibrated", 2 },
                                              { "slurs/quick", 2 }, { "slurs/quickLevel", 1 }, { "slurs/performance", 1 }, { "slurs/transitions", 1 }, { "slurs/smoothStart", 1 }, { "slurs/transitionsEarly", 1 },
                                              { "slurs/plainPerformance", 1 }, { "slurs/plainTiming", 0 }, { "slurs/speedFromWritten", 0 }, { "slurs/fixedTransitions", 0 }, { "slurs/fastTransitions", 1 }, { "slurs/microtoneBends", 3 }, { "slurs/runBends", 1 }, { "notes/transportReset", 1 }, { "levels/hallGuard", 0 }, { "slurs/plainAccents", 2 }, { "slurs/plainAuto", 1 }, { "levels/context", 1 }, { "levels/sectionBalance", 2 }, { "levels/realRange", 2 },
                                              { "levels/evenLevels", 1 }, { "levels/micCompensation", 1 }, { "levels/divisi", 1 }, { "levels/transitionLevels", 2 }, { "levels/attackLevels", 2 }, { "levels/evenKeys", 1 }, { "levels/lowCut", 2 }, { "shorts/staccatoLength", 1 } } },
            { "library", QT_TRANSLATE_NOOP("playbackSettings", "Library default"), { { "heldNotes/early", 0 }, { "levels/calibrated", 0 }, { "slurs/quick", 0 },
                                              { "slurs/performance", 0 }, { "slurs/transitions", 0 }, { "slurs/smoothStart", 0 }, { "slurs/transitionsEarly", 0 }, { "slurs/plainPerformance", 0 }, { "slurs/plainTiming", 0 }, { "slurs/speedFromWritten", 0 }, { "slurs/fixedTransitions", 0 }, { "slurs/fastTransitions", 0 }, { "slurs/microtoneBends", 0 }, { "slurs/runBends", 0 }, { "notes/transportReset", 0 }, { "levels/hallGuard", 0 }, { "slurs/plainAccents", 0 }, { "slurs/plainAuto", 0 }, { "levels/context", 0 },
                                              { "levels/sectionBalance", 0 }, { "levels/realRange", 0 },
                                              { "levels/evenLevels", 0 }, { "levels/micCompensation", 0 }, { "levels/divisi", 0 }, { "levels/transitionLevels", 0 }, { "levels/attackLevels", 0 }, { "levels/evenKeys", 0 }, { "levels/lowCut", 0 }, { "shorts/staccatoLength", 0 },
                                              { "guitar/strums", 0 }, { "guitar/softStrums", 0 }, { "guitar/pinchHarmonics", 0 } } },
            };
      return P;
      }

QString applyPresetToText(const QString& text, const Preset& preset)
      {
      const QString eol = text.contains("\r\n") ? "\r\n" : "\n";
      QStringList lines = text.split('\n');
      for (QString& l : lines)
            if (l.endsWith('\r'))
                  l.chop(1);
      if (!lines.isEmpty() && lines.last().isEmpty())
            lines.removeLast();
      for (const auto& kv : preset.values) {
            const QString id = kv.first;
            const QString group = id.section('/', 0, 0);
            const QString key = id.section('/', 1);
            const QString line = key + "=" + number(kv.second);
            int sectionLine = -1;
            int keyLine = -1;
            QString cur;
            for (int i = 0; i < lines.size(); ++i) {
                  const QString t = lines[i].trimmed();
                  if (t.startsWith(';') || t.startsWith('#') || t.isEmpty())
                        continue;
                  if (t.startsWith('[') && t.endsWith(']')) {
                        cur = t.mid(1, t.size() - 2).trimmed();
                        if (cur == group && sectionLine < 0)
                              sectionLine = i;
                        continue;
                        }
                  if (cur == group && t.section('=', 0, 0).trimmed() == key) {
                        keyLine = i;
                        break;
                        }
                  }
            if (keyLine >= 0)
                  lines[keyLine] = line;
            else if (sectionLine >= 0)
                  lines.insert(sectionLine + 1, line);
            else {
                  if (!lines.isEmpty() && !lines.last().trimmed().isEmpty())
                        lines << QString();
                  lines << "[" + group + "]" << line;
                  }
            }
      return lines.join(eol) + eol;
      }

bool applyPreset(const QString& id, const QString& pathArg)
      {
      const Preset* preset = nullptr;
      for (const Preset& p : presets())
            if (id == p.id)
                  preset = &p;
      const QString path = pathArg.isEmpty() ? iniPath() : pathArg;
      if (!preset || path.isEmpty())
            return false;
      // every setting rewritten (the owner, 2026-10-29): the preset's values, every other key at its default, from the
      // template; the old file kept as <path>.bak (an older .bak replaced)
      QDir().mkpath(QFileInfo(path).absolutePath());
      if (QFileInfo::exists(path)) {
            QFile::remove(path + ".bak");
            if (!QFile::copy(path, path + ".bak"))
                  return false;
            }
      const QString text = iniTemplate();
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
      f.write(applyPresetToText(text, *preset).toUtf8());
      return f.error() == QFile::NoError;
      }

static double presetValue(const char* presetId, const char* id)
      {
      for (const Preset& p : presets())
            if (QString(p.id) == presetId)
                  for (const auto& kv : p.values)
                        if (QString(kv.first) == id)
                              return kv.second;
      return NAN;
      }

QString detectPreset(const std::map<QString, double>& iniValues)
      {
      for (const Preset& p : presets()) {
            bool all = true;
            for (const auto& kv : p.values) {
                  const auto it = iniValues.find(kv.first);
                  // (a key left out is its default; the map's value is the Recommended preset's)
                  const Definition* d = definition(kv.first);
                  const double left = d && d->mapDefault() ? presetValue("recommended", kv.first) : d ? d->value : NAN;
                  const double v = it != iniValues.end() ? it->second : left;
                  const bool match = std::fabs(v - kv.second) < 1e-6;
                  all = all && match;
                  }
            if (all)
                  return p.id;
            }
      return QString();
      }

QString currentPreset()
      {
      return detectPreset(ini()->values);
      }

static std::shared_ptr<Ini> read(const QString& path)
      {
      auto i = std::make_shared<Ini>();
      if (path.isEmpty() || !QFileInfo::exists(path))
            return i;
      QSettings st(path, QSettings::IniFormat);
      for (const QString& group : st.childGroups()) {
            st.beginGroup(group);
            for (const QString& key : st.childKeys()) {
                  const QVariant v = st.value(key);
                  // (a value with commas reads as a list: put it back together)
                  const QString text = v.type() == QVariant::StringList ? v.toStringList().join(",") : v.toString();
                  take(*i, group, key, text);
                  }
            st.endGroup();
            }
      for (const QString& key : st.childKeys())               // (keys outside any section)
            i->warnings << QString("unknown key %1 (ignored: no section)").arg(key);
      return i;
      }

void setIniPath(const QString& path)
      {
      {
            std::lock_guard<std::mutex> lock(iniMutex);
            currentPath = path;
      }
      if (!path.isEmpty() && !QFileInfo::exists(path)) {
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
                  f.write(iniTemplate().toUtf8());
                  f.close();
                  }
            }
      install(read(path));
      }

QString iniPath()
      {
      std::lock_guard<std::mutex> lock(iniMutex);
      return currentPath;
      }

void reload()
      {
      install(read(iniPath()));
      }

int generation()
      {
      return currentGeneration;
      }

QStringList warnings()
      {
      return ini()->warnings;
      }

void setIniValuesForTest(const std::map<QString, QString>& values)
      {
      auto i = std::make_shared<Ini>();
      for (const auto& v : values)
            take(*i, v.first.section('/', 0, 0), v.first.section('/', 1), v.second);
      // (slurs/plainPerformance is 1 by default since 2026-10-17; the tests written before it expect the parts' own
      // choices, so a test's ini leaves it off unless it says otherwise: the built-in default is checked on its own)
      if (!values.count("slurs/plainPerformance"))
            take(*i, "slurs", "plainPerformance", "0");
      // (slurs/plainAccents likewise, 2 by default since 2026-10-21)
      if (!values.count("slurs/plainAccents"))
            take(*i, "slurs", "plainAccents", "0");
      // (slurs/plainAuto likewise, 1 by default since 2026-10-26)
      if (!values.count("slurs/plainAuto"))
            take(*i, "slurs", "plainAuto", "0");
      // (slurs/microtoneBends likewise, 1 by default since 2026-10-22)
      if (!values.count("slurs/microtoneBends"))
            take(*i, "slurs", "microtoneBends", "0");
      // (levels/attackLevels likewise, 2 by default since 2026-10-23)
      if (!values.count("levels/attackLevels"))
            take(*i, "levels", "attackLevels", "0");
      // (levels/transitionLevels likewise, 2 by default since 2026-10-28)
      if (!values.count("levels/transitionLevels"))
            take(*i, "levels", "transitionLevels", "0");
      // (shorts/staccatoLength likewise, 1 by default since 2026-10-24)
      if (!values.count("shorts/staccatoLength"))
            take(*i, "shorts", "staccatoLength", "0");
      // (slurs/runBends likewise, 1 by default since 2026-10-31)
      if (!values.count("slurs/runBends"))
            take(*i, "slurs", "runBends", "0");
      // (levels/lowCut likewise: 2, following, by default since 2026-10-31; the tests before it expect the fixed cut, 1)
      if (!values.count("levels/lowCut"))
            take(*i, "levels", "lowCut", "1");
      install(i);
      }

bool iniHas(const char* id)
      {
      return ini()->values.count(id) > 0;
      }

double iniValue(const char* id)
      {
      auto i = ini();
      auto it = i->values.find(id);
      return it == i->values.end() ? MAP : it->second;
      }

//---------------------------------------------------------
//   the score layer
//---------------------------------------------------------

const char* metaTag = "playbackSettings";

// (parsed from the score's tag, a pure function of its text: kept per thread for the last text asked, as every read of
// a setting parsed the whole tag again, and a render reads thousands)
static std::shared_ptr<const std::map<QString, double>> scoreValuesShared(const Score* score)
      {
      static const std::shared_ptr<const std::map<QString, double>> none = std::make_shared<const std::map<QString, double>>();
      if (!score || !score->masterScore())
            return none;
      static const QString key = QString::fromLatin1(metaTag);
      const QString tag = score->masterScore()->metaTag(key);
      thread_local QString lastTag;
      thread_local std::shared_ptr<const std::map<QString, double>> last;
      if (last && tag == lastTag)
            return last;
      std::map<QString, double> out;
      for (const QString& item : tag.split(';', QString::SkipEmptyParts)) {
            const QString id = item.section('=', 0, 0).trimmed();
            bool ok = false;
            const double v = item.section('=', 1).trimmed().toDouble(&ok);
            const Definition* d = definition(id);
            if (ok && d)
                  out[id] = qBound(d->min, v, d->max);
            }
      lastTag = tag;
      last = std::make_shared<const std::map<QString, double>>(std::move(out));
      return last;
      }

std::map<QString, double> scoreValues(const Score* score)
      {
      return *scoreValuesShared(score);
      }

QString writeScoreValues(const std::map<QString, double>& values)
      {
      QStringList items;
      for (const Definition& d : DEFINITIONS) {          // (in the definitions' order: stable text)
            auto it = values.find(d.id);
            if (it != values.end())
                  items << QString("%1=%2").arg(d.id, number(it->second));
            }
      return items.join(';');
      }

// the older per-score metaTags (their own UI rows: Mixer > Advanced Options...)
bool hasOwnMetaTag(const char* id)
      {
      return !strcmp(id, "heldNotes/early") || (!strncmp(id, "tuning/", 7) && strcmp(id, "tuning/waitForRelease")
             && strcmp(id, "tuning/pitchBend") && strcmp(id, "tuning/bendAtArrival") && strcmp(id, "tuning/hostedTuning"));
      }

static bool ownMetaTagValue(const char* id, const Score* score, double* v)
      {
      if (!score || !score->masterScore() || !hasOwnMetaTag(id))
            return false;
      const MasterScore* ms = score->masterScore();
      const QString s = id;
      bool ok = false;
      if (s == "heldNotes/early") {
            const QString tag = ms->metaTag("soundLibraryOnsetEarly");     // (soundLibraryLegatoEarly: ignored since 2026-10-07)
            const int x = tag.trimmed().toInt(&ok);
            if (ok && x >= 0) {
                  *v = std::min(x, 200);
                  return true;
                  }
            return false;
            }
      const QString key = s == "tuning/tolerance" ? "tolerance" : s == "tuning/tail" ? "tail" : "max";
      for (const QString& item : ms->metaTag("soundLibraryLanes").split(' ', QString::SkipEmptyParts)) {
            if (item.section('=', 0, 0) != key)
                  continue;
            const double x = item.section('=', 1).toDouble(&ok);
            if (ok && x >= (key == "max" ? 1.0 : 0.0)) {
                  *v = x;
                  return true;
                  }
            }
      return false;
      }

//---------------------------------------------------------
//   value
//---------------------------------------------------------

Source source(const char* id, const Score* score, double mapValue)
      {
      const Definition* d = definitionOf(id);
      if (!d)
            return Source::DEFAULT;
      double v;
      if (d->perScore && (ownMetaTagValue(id, score, &v) || scoreValuesShared(score)->count(QString::fromUtf8(id))))
            return Source::SCORE;
      // (the generated file holds every default: a value equal to the built-in one counts as the default)
      if (iniHas(id) && (d->mapDefault() || iniValue(id) != d->value))
            return Source::INI;
      return d->mapDefault() && mapValue >= 0 ? Source::MAP : Source::DEFAULT;
      }

double value(const char* id, const Score* score, double mapValue)
      {
      const Definition* d = definitionOf(id);
      Q_ASSERT(d);
      if (!d)
            return 0;
      if (d->perScore && score) {
            double v;
            if (ownMetaTagValue(id, score, &v))
                  return v;
            const auto sv = scoreValuesShared(score);
            if (!sv->empty()) {
                  auto it = sv->find(QString::fromUtf8(id));
                  if (it != sv->end())
                        return it->second;
                  }
            }
      if (iniHas(id))
            return iniValue(id);
      if (d->mapDefault())
            return mapValue >= 0 ? mapValue : 0;
      return d->value;
      }

bool on(const char* id, const Score* score)
      {
      return value(id, score) >= 0.5;
      }

//---------------------------------------------------------
//   per-patch tables
//---------------------------------------------------------

QString patchEntry(const char* table, const QString& patch, const QString& articulation)
      {
      auto i = ini();
      auto t = i->tables.find(table);
      if (t == i->tables.end())
            return QString();
      if (!articulation.isEmpty()) {
            auto e = t->second.find(patch + "|" + articulation);
            if (e != t->second.end())
                  return e->second;
            }
      auto e = t->second.find(patch);
      return e == t->second.end() ? QString() : e->second;
      }

double adjust(const char* table, const QString& patch, const QString& articulation, int key, double mapValue)
      {
      const QString text = patchEntry(table, patch, articulation);
      if (text.isEmpty())
            return mapValue;
      bool ok = false;
      if ((text.startsWith('+') || text.startsWith('-')) && !text.contains(':')) {
            const double d = text.toDouble(&ok);
            return ok ? std::max(0.0, mapValue + d) : mapValue;
            }
      if (!text.contains(':')) {
            const double v = text.toDouble(&ok);
            return ok ? v : mapValue;
            }
      // a table: linear between its keys, the nearest end's beyond (as the map's)
      std::vector<std::pair<int, double>> pts;
      for (const QString& pair : text.split(' ', QString::SkipEmptyParts)) {
            bool ok1 = false, ok2 = false;
            const int k = pair.section(':', 0, 0).toInt(&ok1);
            const double v = pair.section(':', 1).toDouble(&ok2);
            if (ok1 && ok2)
                  pts.push_back({ k, v });
            }
      if (pts.empty())
            return mapValue;
      std::sort(pts.begin(), pts.end());
      if (key <= pts.front().first)
            return pts.front().second;
      if (key >= pts.back().first)
            return pts.back().second;
      for (size_t n = 1; n < pts.size(); ++n) {
            if (key > pts[n].first)
                  continue;
            const auto& a = pts[n - 1];
            const auto& b = pts[n];
            return b.first == a.first ? b.second : a.second + (b.second - a.second) * (key - a.first) / double(b.first - a.first);
            }
      return pts.back().second;
      }

QStringList presetKeysOverriddenBy(const Score* score)
      {
      QStringList out;
      if (!score)
            return out;
      const std::map<QString, double> own = scoreValues(score);
      for (const Preset& p : presets())
            for (const auto& kv : p.values) {
                  const QString id = kv.first;
                  double v = 0;
                  if ((own.count(id) || ownMetaTagValue(kv.first, score, &v)) && !out.contains(id))
                        out << id;
                  }
      return out;
      }

} // namespace Playback
} // namespace Ms
