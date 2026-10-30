# Playback adjustments: inventory and configuration

Every way this fork's playback differs from plain MuseScore 4 behaviour (beyond the MS4 note model itself), where
it lives, and how to change it. The owner (2026-10-01): "make all the playback adjustments we made fully
configurable … and editable in an ini file".

**Off by default since 2026-10-06** (the owner: "I'm done with automatic adjustments altogether … just make musescore
export everything in the correct techniques and configured so that it's easiest for me to adjust in ableton"): every
early start (legato transitions, held notes' onsets, fast-run firsts), the phrase gap, pedal timing and the calibrated
short velocities. Notes play as written; the technique is still chosen from the notation (shorts by their meant
length) and quarter tones still bend. Held notes' onsets came back on 2026-10-07 (`<Onset early>` 100).

**Removed 2026-10-07** (the owner: "remove all settings not currently being used"): the settings that were off or at
a value that changed nothing, with the code they switched: `[legato] overlapTicks`, `slurEndOverlap`, `phraseGapMs`,
`early` (map `<Legato early>`), `velocity` (the map's `legatoVelocity` stays, applied as it is), `fastTechnique`,
`fastFirsts`, `fastBelowShare`, `levelBalance`, `levelMaxDb`, `levelHeadroomDb` (map `legatoLevel` /
`legatoLevelLong`); `[shorts] calibratedVelocity` (and the short notes' balance: Advanced Options' balance rows,
metaTag `soundLibraryShortBalance`, Recommended); `[pedal] upAfterMs`, `downAfterMs`, `upMaxShare`, `downMaxShare` (a
pedal change goes up one tick after its chord, down the tick after); `[dynamics] evenSteps` (metaTag
`soundLibraryEvenSteps`, `MS_EVEN_DYNAMIC_STEPS`); `[tuning] oneInstance`; `[tracks] mapDelays` (map
`<Instrument trackDelay>`). Default playback is unchanged (the renderer's events for Whence, before and after,
identical). An older playback.ini or score with these keys (or the metaTags `soundLibraryLegatoEarly`,
`soundLibraryShortBalance`, `soundLibraryEvenSteps`) opens as before: the keys are ignored without a warning; an older
map's `<Legato early>` and `legatoLevel` / `legatoLevelLong` are ignored too. Legato transitions play on their beat;
their measured delays (`legatoDelay`, octaves, `[legato.delay]`, `fastShare` / `fastFullMs`) now time only a bent
transition's glide on a patch without a measured `bendGlide` (`[tuning] bendAtArrival`). The measurements stay in the repository (`sso_legato_levels.json`,
the sweeps below, HANDOFF.md).

## Configuration: three layers

1. **Built-in defaults** (`libmscore/playbacksettings.cpp`, `DEFINITIONS`). A few take the sound library map's
   value as their default (`<Onset early>`, `<Tuning tolerance tail maxLanes>`).
2. **playback.ini**, one file for every score, in MuseScore's data folder:
   - Windows: `%LOCALAPPDATA%\MuseScore\MuseScore3Evo\playback.ini`
   - Linux: `~/.local/share/MuseScore/MuseScore3Evo/playback.ini`
   - macOS: `~/Library/Application Support/MuseScore/MuseScore3Evo/playback.ini`

   Written at start when missing, with every key, its default and a comment (unit, range); never overwritten.
   QSettings IniFormat, comments start with `;`. An empty value (`key=`) means the default. Unknown keys, values
   that aren't numbers and values out of range are logged (`playback.ini: …`) and shown under the settings table;
   out-of-range values are clamped. **Reload playback.ini** (Mixer › Advanced Options… › Playback adjustments; the action "Reload Playback Settings" can get a shortcut in Preferences › Shortcuts)
   reads it again and renders every score again; no restart.
3. **The score** (saved in it, undoable): metaTag `playbackSettings` (`legato/keepMs=60;shorts/staccato=40`),
   plus the two older per-score metaTags that stay where they were: `soundLibraryOnsetEarly` (heldNotes/early),
   `soundLibraryLanes` (tuning tolerance / tail / maxLanes). A score
   without overrides has none of them, so its file is unchanged; MuseScore 3.6 keeps metaTags (round trip).

**UI**: *Mixer › Advanced Options… › Playback adjustments*: every setting, grouped as in the ini, with its
effective value for the score and where it comes from (default, library, playback.ini, score; bold: this score's).
Editing a value sets it for the score; *Reset to ini/default* takes the score's value away (a whole group when a
group is selected); *Open playback.ini*; *Reload playback.ini*. Hosting settings are global (the plug-in instances
serve every score): playback.ini only.

**Per-patch tables** (the map's measured data) are overridden in playback.ini only, by the patch's name in the map,
or `patch|articulation`; a value is an offset (`+25`, `-30`, in the table's unit) added to the map's, or a whole
table that replaces it (`-12:240 -7:280 … +12:440`; one number for all):
- `[legato.delay]` legato transition delay, ms by interval (`Violins 2 - Performance=+25`)
- `[heldNotes.onset]` held-note onset, ms by MIDI pitch (`Violins 1|Long Flautando=-50`)
- `[shorts.from]` a short's from=, seconds (`Violas|Short 0.5=0.55`)

**Live**: every setting that acts in the renderer reaches Live's clips identically (LiveClipsLink renders with the
same `MidiRenderer`). `hosting/maxVoices` reaches the Live Set (its Kontakt states come from
`SoundLibraryHost::setupState`). Host-only (MuseScore's own plug-in hosting; LIVE.md › What still differs):
`hosting/settleSeconds` and varispeed's glide (varispeed can't reach Live; pitch-bend
glides, one cent a tick, are rendered and do).

## Presets

Two presets switch `playback.ini` (the owner, 2026-10-07: "library default, and recommended"; then, of Library
default: "very late, but at least it sounds consistent": an Even early preset; 2026-10-08: "make even early the new
recommended", so Recommended is it and the per-pitch shift is `byPitch=1` only). The Preset box in Mixer > Advanced Options... >
Playback adjustments rewrites the whole file (the owner, 2026-10-29): the fresh template (every setting at its default, as a missing file
gets it) with the preset's values; nothing of the old file stays, comments included, and it is kept as `playback.ini.bak`
(an older .bak replaced); the scores' own values are untouched. Then it reads it as Reload Playback Settings does, playback going on. The box says "this score overrides: ..." when
the open score has its own value of a preset key (not cleared). "(Custom)" shows when the ini matches none (not
selectable).

| Preset | heldNotes/early | heldNotes/byPitch | levels/calibrated | slurs/quick, slurs/quickLevel | slurs/performance | slurs/transitions | slurs/smoothStart | levels/context | levels/sectionBalance | levels/realRange | levels/evenLevels | levels/micCompensation | levels/divisi | levels/evenKeys |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Recommended | 100 (held notes and, since 2026-10-08, every technique with measured onsets: shorts on the beat as the Longs are) | 0: every note of an articulation early by the median of its measured onsets (a run as written; a note's attack off by its pitch's distance from the median: SSO's section strings -78 .. +62 ms; since 2026-10-09 slurred notes `[slurs] quick` swaps too) | 2: techniques on velocity matched to the held note plus D, real players' difference (staccatos -4.2 dB, pizzicato Meyer's), MS4's offset on top (below, Calibrated levels; the owner, 2026-10-15, option B'; 1, the match alone, Recommended 2026-10-08 to 10-15, by hand: "(Custom)") | 2, 1: short slurred notes on the espressivo long (Long (Rachm.)) as loud as the held Long (the owner, 2026-10-08: "let's make slurs quick = 2 the recommended preset"; quickLevel re-measured in the passage, 2026-10-09: "1 is the right balance, make it the recommended preset") | 1: the five string sections' slurred notes on their Performance legato (below, Performance legato; 2026-10-10) | 1: Auto fingered within a slur, bowed from a slur into the next (the owner, 2026-10-12) | 1: a phrase's first note smooth (the owner, 2026-10-12) | 1: those slurs by the level rule (below, Level rule; 2026-10-10) | 2: each section at its level from Clark & Luce's orchestral scales, `sectionLevel2` (below, Section balance; the owner, 2026-10-14: real data; 1, the TU Berlin M, by hand: "(Custom)") | 2: each note at the real player's level for its written dynamic, the markings in equal steps of the instrument's pp -> ff as orchestral players play it (below, Real range › Clark & Luce spans; the owner, 2026-10-13 on, 2026-10-14 the spans; 1, the Meyer rule, by hand: "(Custom)") | 1: slurred passages evened out by measurement (below, Even levels; the owner, 2026-10-13) | 1: a part's mics change tone and space, not its level (below, Mic compensation; the owner, 2026-10-15) | 1: a chord on a monophonic patch adds up to one section (below, Divisi level; the owner, 2026-10-20) | 1: a wind or brass key off the smooth curve by more than real players' scatter evened (below, Even keys; the owner, 2026-10-28) |
| Library default | 0 (notes play as written, no timing adjustment of the fork; byPitch left as it is) | | 0: the library's own balance | 0: the held technique (quickLevel left as it is) | 0: on the All techniques patch | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

`byPitch=1` (by hand, either layer) starts each note early by its own pitch's onset instead: attacks on the beat, a
run's spacing following the onsets (Recommended until 2026-10-08); the Preset box then shows "(Custom)".

`levels/calibrated` 1 (as loud as the held note) is in neither preset since 2026-10-15 (Recommended plays 2, the
match plus D, real players' difference: option B'): set by hand it shows "(Custom)".

`levels/realRange` 1 (the Meyer rule) is in neither preset since 2026-10-14 (Recommended plays 2, Clark & Luce
spans): set by hand it shows "(Custom)".

`slurs/plainPerformance` is 1 in Recommended and 0 in Library default (2026-10-17, the owner: chosen by ear on Whence's
five string sections, with the all-notes constants; below, Plain Performance): set by hand it shows "(Custom)".

`slurs/plainAccents` is 2 in Recommended and by default, 0 in Library default (2026-10-21: the owner chose 2, the accented
attack, at real players' level, heard 2026-10-21 on Whence bars 7-9: "fine"; below, Plain accents); 1 or 3 by hand shows "(Custom)".

`slurs/plainTiming` is 0 in both presets (2026-10-21: plain notes early each by its own model instead of the patch's one shift; the owner heard each section alone with a click the same day: 0 better for Violins 1, Celli, Basses, no difference for Violins 2, Violas undecided: stays 0; below, Plain timing): set by hand to 1 it shows "(Custom)".

`notes/transportReset` and `levels/hallGuard` are 0 in both presets (2026-10-21, from reading SSO's scripts, measured on the VM; proposed 1 in Recommended, the owner decides; below, Transport reset and Hall guard): set by hand to 1 they show "(Custom)".

`slurs/microtoneBends` is 3 in Recommended and by default, 0 in Library default (2026-10-22: the owner, Whence's Violas
alone with a click: "B is so much better it actually sounds right now" (1), then Celli and Violins 1 alone: "both B" (3);
below, Microtone bends); 1 or 2 by hand shows "(Custom)".

`slurs/speedFromWritten` is 0 in both presets (2026-10-17: the owner approved MuseScore taking over the Performance
patches' CC119 and flips Recommended after the null test and listening; below, Speed from written): set by hand to 1 it
shows "(Custom)".

`slurs/transitionsEarly` is 1 in Recommended and 0 in Library default since 2026-10-15 (the owner, 2026-10-15, after hearing Whence's fast quarter-tone slurs through SSO: "I think 1 and 3 are both fine (I can hear very little difference btwn them)"; 1 measured closest to the written pitches there: Violas bar 7 93 cents mean error against 123 for 3 and 127 on the beat, Violins 1 bars 5-7 62 / 73 / 97, kthost + SSO on the test VM, pYIN; below, Transitions
early); 2 or 3 by hand shows "(Custom)".

Only those keys differ. The others are left as they are because: `legato/keepMs` acts only with early starts; the shorts'
lengths are MuseScore 4's note model; tuning and hosting are mechanics. A key missing from the ini counts as its
default; for `heldNotes/early` that is the map's value, taken as Recommended's 100 (SSO's).
Advanced Options shows each of these once, in Playback adjustments (its older rows "Held notes early by" and "Copies
for other tunings" were the same metaTags and went 2026-10-08). The table is
`Playback::presets()` (`libmscore/playbacksettings.cpp`).

## Performance legato in Recommended

The owner, 2026-10-10, after hand-building *Whence*'s Live set R (the string sections' slurs on their Performance
patches, by ear): Recommended plays them so. This reverses "No Performance patches" (2026-10-06, HANDOFF.md) for
Recommended only; Library default keeps the All techniques patches. `[slurs] performance` (1 by default and in
Recommended, 0 in Library default): a slurred note (MS4's legato articulation: every note of a slur, its last one too)
plays as under staff text "performance" (`Want::slurPerformance`, `choose()`) when its plain choice has no modifier
(a muted or sul pont. slur keeps its own technique) and "performance" then gives a legato articulation with measured
`attacks` (SSO: the five string sections' Performance legato; the solo strings' and the winds' have none and keep
`[slurs] quick`). It replaces `[slurs] quick`'s Long (Rachm.) swap for those notes. Unchanged: a staccato before a slur
is not slurred and stays on All techniques (the owner, 2026-10-10), so the slur's first note starts from silence on the
Performance patch (an attack, `performanceNote`'s Auto). Staff text "non performance" (or "senza", "no") opts the part
out from there until a "performance" text (`TextState::performanceOff`; "ord." does not end the opt-out). Each note's
Performance technique, velocity and join as before (performancetechnique.h; the map's `attacks`, `transitions`,
`reattack`, `reattackGap`, `overlap`, `legatoGap`, among them the Violas' transitions at velocity 100, approved with this).
With `[levels] calibrated` 1 or 2 they play as loud as the part's Long (below).

## Calibrated levels

The owner, 2026-10-08, on staccatos sounding too quiet: "bring back the calibrated short velocity and include it in the
recommended preset. adjust all techniques according to the measured value, taking into account that some techniques
are meant to be louder", then "use MuseScore 4.7.5's articulation profiles for everything incl. marcato, with the
requirement that whatever notation that caused the technique can change the dynamics in the inspector".

`[levels] calibrated` (**2 by default and in Recommended since 2026-10-15**, 0 in Library default, 1 by hand: "(Custom)";
per score through `playbackSettings`).

**2: matched + D, D from real players** (option B'; the owner, 2026-10-15: "huh, I'd always thought of staccatos as
louder than plain. but if the data clearly point to b', then b' it is"; option B the same day before it). A note on a
velocity technique first gets 1's velocity (as loud as the part's Long at the same marking, along the measured perceived
curves, at the note's pitch where curves have pitches), then plays **D** dB more along its own curve (negative: softer),
then MuseScore 4.7.5's offset for its articulations and the Inspector's levels, as with 1 (one inverse of its curve:
`SoundLib::calibratedVelocity` with D + the offset). **D** (`SoundLib::techniqueOffsetDb`): real players' difference
against the held note where the map lists one (`<Articulation heldOffset>`, one value or `cc:dB` pairs by the written
dynamic's CC, linear between, the ends' beyond), written by `gen_spitfire_sso.py` from
`tools/soundlibraries/technique_offsets.json` (`technique_offsets.py`, which states sources, n, intervals and
assumptions); else (option B's) the technique's level at velocity 127 less the Long's at CC1 127 in the same calibration
(`SoundLib::loudEndDb`: perceived where both curves have it, the loudest 50 ms else; per pitch), an assumption: SSO's own
balance, no real-player data. Past 127 the rest goes by the capped-short top-up (CC11 / the patch headroom: `realRange`'s
realExtra, so only with realRange on); under the curve's lowest measured point the note plays that point's velocity and
the rest by CC11 (`SoundLib::calibratedVelocityFloored`, not extrapolated). Without either curve: as 0.

D, the five string sections (dB; the map's `heldOffset`; `tst_soundlibrary::levelTechniqueBalance` prints them):

| Technique | Violins 1 | Violins 2 | Violas | Celli | Basses | Source, uncertainty |
|---|---|---|---|---|---|---|
| Spiccato, Short 0.5 | -3.4 | -3.4 | -3.4 | -3.4 | -3.4 | Spheres K. 550, staccato - held at f, perceived (STL), pooled over Violins 1, Celli, Basses; 90 % [-5.7, -0.9] |
| Short 1.0 | +0.3 | +0.3 | +0.3 | +0.3 | +0.3 | Spheres K. 550, quarters - wholes at p (Celli, Basses), pooled; 90 % [-2.2, +2.3] |
| Pizzicato | -7 (pp) .. -9 (ff) | -7 .. -9 | -9 .. -5 | -12 .. -8 | -6 .. -7 | Meyer 2009 ch. 3, pizz - arco, sound power (not perceived: several dB) |
| Pizzicato Bartók | SSO's (+9.4) | SSO's (+6.5) | SSO's (+4.9) | SSO's (+4.4) | 0 | no real data (SSO's: an assumption); Basses below |
| Col Legno | SSO's (-15.8) | SSO's (-19.1) | SSO's (-23.3) | SSO's (-12.1) | 0 | as Bartók |
| Short CS, Short Harmonics, Short Brushed | SSO's | SSO's | SSO's | SSO's | SSO's | no real data (the mute / harmonics against an open held note) |

- **Bowed shorts** (`spheres_k550_staccato.json`: the Spheres dataset, Colibri Ensemble, Mozart K. 550, each section's
  own stem; per note the STL maximum). The readings are the owner's, confirmed or corrected 2026-10-15 (review page
  https://claude.ai/artifact/8FoTWaNAb3Ekf2zFEDqWg8): bar 28 is a double stop in both violin sections and bar 29 one in
  Violins 2, all left out (a double stop as the held reference biases the difference), so **Violins 2 has no single held
  note at f** in bars 28-33 (bars 34-37 are sf) and is not measured; Violins 1 is bars 30-33's 13 dotted quarters per
  play against bar 29's single dotted half; Celli / Basses bars 191-197 (all staccato: bar 191 note 2's missing dot a
  typo) against bar 198, minus the notes whose level dip is no deeper than the held note's (1 cello, 4 bass notes).
  Per section: Violins 1 -0.4 [-1.4, +0.6] (26 staccatos / 2 plays of one held note), Celli -6.7 [-7.1, -5.9] (55 / 1),
  Basses -3.1 [-3.6, -2.2] (51 / 1). **Pooled -3.4, 90 % [-5.7, -0.9]**: the mean of the sections' differences, its
  interval by a two-level bootstrap (sections resampled, then each section's notes; 20000 draws). Chosen over
  per-section values by data quality: one held note per section (so the per-section intervals hold only the staccatos'
  spread), and the sections differ by 3.1 dB (SD), about what one held note varied by in the first readings (2.6-4.2 dB
  SD): one held note each can't tell a section's own difference from its held note's. Violins 2 and Violas (not
  measured) take it: an assumption. The same D at every marking: the constant-offset assumption, measured at f only,
  untested at p. Short 0.5 is a spiccato-length bowed short: the staccato value. Short 1.0 (a full-length bowed short)
  takes K. 550's unaccented quarters (bars 1-9) against whole notes (bars 10-15) at p, Celli and Basses, pooled +0.3
  [-2.2, +2.3] (an assumption that it plays as those quarters; Violins and Violas take it).
  The first readings (bar 28 taken as Violins 1's held note, Violins 2 measured against a double stop) gave -4.2
  [-5.8, -2.5] over four sections, which the map played until the corrected readings arrived (`technique_offsets.py`
  `STACCATO_PLACEHOLDER`, now None: the map plays the pooled value, `technique_offsets.json` › `staccato` › `use`, every
  bowed short referring to it as `@staccato`).
- **Pizzicato**: Meyer 2009 (*Acoustics and the Performance of Music*, ch. 3), pizzicato against arco at pp and ff, by
  the written dynamic's CC (pp 32, ff 112, linear between; ppp / fff the ends'): the per-marking reading of the source
  rather than one mean. Sound power, not the perceived metric: an uncertainty of several dB. Violins 2 the violin's.
  Checks against other data (2026-10-28; not used for the values; `technique_offsets.json` › `pizzicato` ›
  `corroboration`):
  - The University of Iowa Musical Instrument Samples (one solo player per session, anechoic, 5 ft): 8 sessions
    (violin, viola, cello, bass, pre-2011 and 2012), 6 with fixed gain confirmed by the gap noise. Each pizzicato note
    is paired with the arco note of the same pitch and marking. Median over the sessions (range): perceived peak pp
    -1.7 (-8.6 .. +5.6), mf +0.9 (-8.3 .. +4.3), ff +0.1 (-2.1 .. +5.9); by energy (BS.1770 momentary) pp -2.4
    (-8.5 .. -0.5), mf -1.4 (-10.2 .. +0.3), ff -1.4 (-6.6 .. +3.8). The players differ by 10-14 dB; only the
    pre-2011 cellist lies near Meyer. Iowa does not corroborate Meyer.
  - The Spheres dataset's ScalesAndSolos (whole sections in a studio room, their spot mics; the main mic within
    1.5 dB): each section's pizzicato scale against its quiet and loud legato scales of the same session, perceived
    peak: Violins 1 -10.3 / -16.5, Violas -3.6 / -8.5, Celli -8.8 / -13.9, Basses -2.6 / -7.7 dB (against quiet /
    loud). The pizzicato's marking isn't stated; if it was at least the quiet scale's, pizz - arco is at most the
    first figure: Meyer's order, against Iowa.
  - An earlier note here read "pp -7.3, mf -6.3, ff -4.0 (ISO 532-1, 5 sessions), Meyer holds within +-3-4 dB". It
    can't be reproduced: that run's sessions and script were not kept.
- **Basses' Bartók and col legno**: SSO's own D disagrees by ~10 dB between the perceived and the 50 ms metric (the
  2026-10-15 validation), so D = 0: SSO's balance at the matched level (an assumption).

**Why B was replaced** (the validation, 2026-10-15): B took D from SSO's recordings at the loudest layers. Against
real players that has the wrong sign for the bowed shorts (B: Spiccato +1.6 .. +9.2 dB over the Long; K. 550's sections:
-3.0 .. -6.0 dB) and is too high for the pizzicato (B: -0.9 .. +4.1; Meyer: -5 .. -12).

**Why the first 2 (A, the same day) failed**: it played the written dynamic's velocity on the technique's own curve, so
SSO's mapping between the layers decided the balance: Whence's staccatos came out 13-21 dB under the Long (Violins 1 107 ->
64 at mp), far from any player's difference. B and B' keep the match at every marking; B took from the recordings only
their relation at the loud end, B' takes D from real players.

**The level rule doesn't follow** (`[levels] context`, below; since 2026-10-15, the final B'). B and the first B'
moved every slur gain of a section by its Spiccato's D (`ContextLevel::staccatoShiftDb`), because the 2026-10-10 gains
were fitted against the staccatos at 1's level (gain = st - pfd + 1.3, the slurs 1.3 dB over the staccatos around them).
The level rule's grid was re-measured the same day against each section's **held** Long directly (gain = held -
slurred), and the held Long is what D is measured against too: shifting the gains by D would count it twice, so the
shift was removed. The slur gains stand as measured; the staccatos play D against the held note, the slurs as loud as
it (`tst_soundlibrary::levelContext`: G, S and every slur's CC11 the same under 1 and 2, the staccato D softer).

Unchanged by 2: techniques on the dynamics CC; the Performance legato outside the level rule (`calibratedController`,
`performanceLevel`, as with 1); `[slurs] quickLevel` and Long (Rachm.)'s quickDb. With `[levels] realRange` a velocity
technique takes the Long's realDb at its marking (it is tied to the Long at every marking). Live gets the same: the
velocities and CC11 are the render's (`tst_liveequivalence::liveMarcatoLevel` "technique balance"). Untried in Kontakt /
SSO (HANDOFF.md › Technique balance).

What B' changes in *Whence* (the final values, branch `bprime-final`, 2026-10-15; Recommended, `dumpEvents` chunked, the
shipped calibration; against integrate-1012, calibrated 1 and the 2026-10-10 grid): the 109 velocity notes are all
Spiccato (no pizzicato in Whence), each D softer: Violins 1 -7 .. -12 velocities (21 notes, median 107 -> 95), Violins 2
-5 .. -12 (45, 105 -> 93), Violas -5 .. -7 (41, 111 -> 106), Celli -15 (1, 127 -> 112: no longer capped), Basses -8
(1, 100 -> 92). The slurs take the re-measured gains as measured, no shift: G 15.29 -> 16.32 dB, S 9.29 -> 10.32 before the gain stage (the
first B', with the shift and the placeholder -4.2: G 11.09, S 5.09). Audit: OVERLAP 0, UNMEASURED 10 info, CAPPED 1
info, ARRIVAL 0 fail / 2 warn, LEVEL STEP 0, DROPPED 0, LEAP 0. Untried in Kontakt / SSO and not heard.

What option B changed in *Whence* (Recommended, `dumpEvents` chunked, the shipped map and calibration, 1 against 2, 2026-10-15):
109 of the 454 library notes are on velocity, all Spiccato (the strings' staccatos); 108 change, each louder by its
section's D: Violins 1 +7 .. +8 velocities (21 notes, D +2.9 dB), Violins 2 +9 .. +14 (45, +6.0), Violas +6 .. +11 (41,
+6.0), Basses +17 (1, +9.2); the Celli's one is at 127 under both (capped: its rest by CC11, the Celli's real-range top
0.10 -> 1.75 dB; Violas' 0 -> 2.47, Violins 2's 0 -> 0.11). The slurs follow (the level rule): the score's G 15.29 ->
16.94 dB, every patch's volume 1.65-3.3 dB lower together (S', before the gain stage and master trim replaced it the same day), and each slur against its section's All techniques patch
+2.84 (Violins 1), +5.88 (Violas), +1.57 dB (Celli), the D of its Spiccato within CC11's rounding: the slurs stay 1.3 dB
over the staccatos. The audit unchanged (OVERLAP 0, UNMEASURED 10, CAPPED 288, ARRIVAL 33 / 6, the rest 0).

**1: as loud as the held note** (2026-10-08 to 2026-10-15 the default; 2 is this plus D):
a library note whose technique is on velocity (the map's `<Dynamics velocity>`: SSO's shorts, staccatissimo,
spiccato, marcato, tenuto, pizzicato, Bartók, col legno) plays at the velocity at which its measured curve is as loud
as the part's plain held note ("long": SSO's Long) at the same dynamic's CC1 (`SoundLib::calibratedVelocity`), plus
MuseScore 4.7.5's offset for its articulations: 40 log10(MS4's velocity / a plain note's at that dynamic) dB
(SoundFont 2's velocity law; MS4's articulation profiles: a marcato at mf plays at 103 against a plain note's 80: +4.4 dB),
then the Inspector's levels (below). "As loud" is by ear: both curves' perceived loudness
(`ArticulationCheck::perceivedLoudnessDb`: ERB-band specific loudness, short-term loudness with 22 / 50 ms attack / release,
its peak; since 2026-10-08, after the first measurements on the VM showed the loudest 50 ms RMS misjudging a slow swell
against a short: Flute Marcato on +4.2 dB by RMS, +8.6 by ear at mf), the loudest 50 ms RMS where either curve has no
perceived points; the Inspector's dB on velocity are by ear too (`DynamicsCurve::louder`). Where either curve is missing, or with 0: as before (the dynamic's level on CC1's
scale, an accent's share included; a marcato at the plain level). Techniques on the dynamics CC (legato, tremolo,
trills, swells, marcato attack ...) keep the library's balance: they follow CC1 as the held note does, and their
measured differences from it are the library's design (louder tremolo, softer flautando), not a calibration error.
The exception (2026-10-10, with `[slurs] performance`; with 1 and 2): a Performance legato note (an articulation with `attacks`) plays,
on CC1, the value at which its patch sounds as loud as the part's Long at the CC1 in force
(`SoundLib::calibratedController`, by ear where both are measured, the note's pitch's curve where measured; a LibLevel
on the dynamics CC from right before its note-on, `rendermidi.cpp` `performanceLevel`): its notes are as loud as the
All techniques notes around them. SSO's shipped curves (perceived, dB, Long less Performance at CC1 16 / 64 / 127;
since 2026-10-12 with the Longs measured again, below): Violins 1 10.2 / 3.4 / -2.0, Violins 2 8.3 / 1.8 / -6.7, Violas
16.5 / 4.5 / -2.3, Celli 30.2 / 9.1 / -2.8, Basses 4.1 / 1.9 / -0.8 (before: 9.6 / 2.7 / -2.6, 9.9 / 2.4 / -6.7,
16.1 / 3.8 / -2.4, 28.1 / 7.8 / -3.8, 5.8 / 3.1 / -1.1): louder at soft dynamics, softer at the loudest. An Inspector level on top: louder along its own curve
from the calibrated value, softer by CC11 (a plain volume on these patches, measured 2026-10-02: HANDOFF.md › Legato level balance). Its velocity (the
attack or transition) is not changed. Live gets the same values: they are CC1 events, so the clips and the plain set's
CC1 lanes carry them (`tst_liveequivalence::livePerformanceLevel`).

The measurements: each patch the user's `dynamics.json` (Check articulations › Dynamics, the patches checked so far)
measured from it, every other patch from the shipped `share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json`
(`SoundLib::readCalibration`; until 2026-10-11 a user's file replaced the shipped one whole), made from the
owner's checks in `tools/soundlibraries/sso_sound_dynamics.json` by
`tools/soundlibraries/calibration_from_sound_dynamics.py` (105 patches, 492 curves; only what the map plays).
**The five string sections' Long** (All techniques, value 1) comes instead from `sso_string_dynamics.json`
(`string_dynamics_sweep.py`, 2026-10-12, Windows VM, Kontakt 8 + SSO through kthost): the check plays each point once,
and SSO's string Longs alternate round robins up to ~4 dB apart at soft CC1, so Celli's came out "neither" and not
monotonic (perceived 44.1, 51.0, 52.5, 49.5, 52.8, 52.2, 55.1, 53.3 at CC1 16 … 127): its velocity techniques were
matched to a noisy reference. Measured again at the check's pitch with its measures and note lengths, each point 4 times
(the median; CC1 1.0 s before the note-on): Celli 46.2, 49.0, 50.6, 50.8, 51.8, 53.2, 54.1, 54.4 (a range of 8.1 dB:
the Celli's Long is that flat; sso_cc1_curves.json's loudest 50 ms over 17 pitches 7.3 dB), Violins 1 40.7 … 55.4,
Violins 2 39.5 … 53.6, Violas 48.9 … 59.0, Basses 26.4 … 53.7, each rising by more than its repeats' spread. Their
velocity techniques were measured in the same runs: each within about 1 dB of the owner's (median over the points), so
those stay the owner's (the same scale). The Celli shorts still play at velocity ~95-127 at every dynamic: the Long they
match is only 8 dB from ppp to fff, their own perceived range 70 dB.

**Per-pitch curves** (2026-10-09, the owner: Long (Rachm.) gets per-register loudness curves so the notes `[slurs] quick`
swaps can match Long): a curve may carry `"pitches": {"<pitch>": {"curve": [[cc, dB]...], "perceived": [...]}}` beside
its one `curve` / `perceived` (files without it work as before). `DynamicsCurve::atPitch(p)`: at each CC1 (the union of
both neighbours' points) linear in dB between the two nearest measured pitches, the nearest one's beyond the ends; no
pitches (or pitch -1): the one curve. Chosen over nearest-only by a leave-one-out check on the measured pitches (RMS
error 0.79 dB against 0.99, max 3.3 against 4.2, 833 points). `calibratedVelocity`, the Inspector's levels and
quickLevel all read the note's pitch's curve. Raising a CC1 (`DynamicsCurve::raise(x, db)`) takes the lowest CC1 ≥ x
whose level reaches level(x) + db, so a dip in the curve (Violas Long (Rachm.) at pitch 69: CC1 68-76 quieter than 64)
is crossed, not stopped at; never reached: 127. On a rising curve it is the old inverse. Shipped: SSO's Violins 1,
Violins 2, Violas and Celli Long (Rachm.) (`tools/soundlibraries/sso_rachm_register_curves.json`, measured on the
Windows VM with the real library: CC1 16..124 step 4 and 127 at 8 / 6 / 8 / 7 pitches by rule: the old curves'
band centres, the lowest and highest Rachm. key, the earlier sweep's points; one round robin each, repeats within
0.001 dB; `rachm_register_sweep.py`); Basses have no Long (Rachm.).

**Levels in the Inspector** (articulation.h MarcatoLevel): every articulation sign (Inspector › Articulation ›
*Level*) and every staff text that changes the technique (Inspector › Staff text › *Level*: pizz., col legno, sul
tasto ..., from the text to the next one that changes the technique) has a level in dB; a chord's articulations' and
its text's add up. −35.5 … +35.5 dB, 0.5 steps: what SSO's played techniques differ from their plain held note at
the same dynamic, −35.1 (Violas Col Legno at 32) … +25.1 dB (Horns a2 Rip at 32), 1308 pairs, median −2.6
(`tools/soundlibraries/derived_numbers.py levels`), widened to the larger side both ways and rounded outward to 0.5:
a level can bring any technique to the held note's loudness. Kept in the metaTag `marcatoLevels` (its name from when
only marcatos had one; a text's entry `{"tick","track","text","db"}`).

## Level rule

The owner, 2026-10-10: "the same written dynamic sounds equally loud in every string part and technique, judged in the
music", by volume only (never by raising CC1, a timbre crossfade), one level per slur. `[levels] context` (1 by default
and in Recommended, 0 in Library default; per score through `playbackSettings`; acts with `[slurs] performance`):

- A note on an articulation with a measured context level (the map's `<Articulation contextLevel="cc/ms/pitch:dB ...">`:
  SSO's five string sections' Performance Legato) plays CC1 at the written dynamic's value (`Ms4::expressionLevel`), not
  `[levels] calibrated`'s value, and gets its slur's gain: `Articulation::contextLevelAt` (linear in CC1 and pitch, in
  log ms of spacing, the nearest end's beyond the grid) of the slur's first dynamic, its median note spacing (IOI) and
  median pitch (`libmscore/contextlevel.h`). One gain per slur: no level step inside it. A note outside a slur (staff
  text "performance") is a phrase of one.
- The gain reaches 29.6 dB (Celli pp, 136 ms, mid register; 21 before the 2026-10-15 re-measure), more than CC11 can add. The score's largest gain G over every slur of those
  parts is shared out (`ContextLevel::volumes`): the Performance patches play G louder by their **gain stage**, every
  other patch of the library's parts at 0 dB (the owner, 2026-10-10: Performance levels move together, one shared gain,
  not equal levels; section balance's per-section offsets go on top, 2026-10-12), and each slur plays gain - G by CC11.
  Until 2026-10-15 every patch was S = max(0, G - 6) dB lower so the loudest fit Live's +6 dB fader; the gain stage and
  the master trim took its place (below, Gain stage and master trim: the same levels, one trim per score against
  clipping). The gains go through `TrackDelays::patchGain` (the Kontakt slot's gain in MuseScore; MuseScore's Utility
  after the Kontakt in Live, `LiveSetWriter::kontaktGains`), the CC11 through the per-note levels (`libraryNoteLevels`):
  Live's clips and the plain set's CC11 lanes carry it. They are not kept in the `trackDelays` metaTag; the Mixer's own
  track levels add.
- An Inspector level on top: louder along the patch's own curve from the written value (CC1), softer by CC11.
- `[levels] calibrated` 2's D doesn't move the gains: they are measured against the held Long, as D is (until
  2026-10-15 every slur gain took its section's Spiccato D, `ContextLevel::staccatoShiftDb`, removed: it would count D
  twice; Calibrated levels › 2).

The gains (`tools/soundlibraries/performance_context/level_rule.py`, from the measured cells of
`tools/soundlibraries/sso_performance_context.json`). **Re-measured 2026-10-15** with today's Recommended slurs
(`[slurs] transitions`, `smoothStart`, `transitionsEarly` 1), which the 2026-10-10 grid predated: the VM check of build
435 had the held notes balanced as the model says but the slurs not (violas' slurs 2.7 dB under, celli's 3-5 over).

- Renders: the real Kontakt 8 + SSO on the Windows VM, MuseScore3Evo.exe (build 435) `--verify-playback --verify-wav`
  of `performance_context/grid_score.py`'s scores (the repository's String Orchestra template; each part rendered alone
  through the hosted plug-ins, so the events are MuseScore's own): per section, dynamic d (pp mp f ff = CC1 32 64 96
  112) and grid pitch p, four held half notes (All techniques Long) and, at each IOI 100 / 136 / 200 / 300 / 500 /
  1000 ms (1000 new: slow slurs fell on the 500 ms column before), two touching slurs of four sixteenths (Performance
  Legato: smooth first note, fingered within, bowed into the second, transitions sent by their modelled arrival).
  Takes A and B (B in reverse order); take to take sd 0.36 dB per figure.
- The map's `contextLevel` zeroed for these renders (the VM's copy only): CC1 = the written dynamic, no gain. What both
  techniques of a section play alike cancels in held - slurred and was off (`sectionBalance`, `realRange`,
  `micCompensation`); it stacks on top in MuseScore.
- **Even levels off**: the gain sets a slur's overall level (its median); even levels then glides the notes inside the
  slur to that median (CC11), so the two stack without counting anything twice. With even levels on the grid would
  depend on even values measured at the old gains.
- Metric P, `ArticulationCheck::perceivedLoudnessDb` (`perceived.py`: the calibration curves' measure, the one the
  check used), per note from its note-on as sent to the next note-on (or its end + 0.3 s), median per figure; L (the
  2026-10-10 grid's loudest 50 ms RMS) is kept beside it, 1-3 dB smaller gains.
- **gain = held - slurred** (the held Long at the written CC1 is what every other technique of the section is
  calibrated to). Before: gain = staccatos' fitted level - slurs + 1.3 dB (the Violas' relation against the staccatos
  around them); the fit's common T(d) and section offsets misplaced each section's slurs against its own held notes.

Gains at IOI 136 ms, low / mid / high register, d 32 / 64 / 96 / 112 (before, 2026-10-10, in brackets at d 64):

| Section | 32 | 64 | 96 | 112 |
|---|---|---|---|---|
| Violins 1 | 19.3 / 18.9 / 17.7 | 12.8 / 11.1 / 11.1 (6.7 / 10.0 / 12.9) | 9.4 / 7.1 / 7.9 | 7.1 / 4.8 / 5.6 |
| Violins 2 | 15.8 / 14.6 / 17.3 | 10.9 / 8.9 / 10.5 (7.3 / 11.5 / 14.5) | 5.9 / 4.5 / 5.9 | 4.2 / 3.4 / 3.9 |
| Violas | 19.2 / 20.0 / 22.3 | 14.5 / 16.1 / 17.9 (9.1 / 8.3 / 15.5) | 9.2 / 9.4 / 11.7 | 7.3 / 7.3 / 9.6 |
| Celli | 24.9 / 29.6 / 28.9 | 14.0 / 16.4 / 15.5 (11.6 / 15.4 / 14.2) | 8.4 / 9.1 / 8.5 | 6.5 / 6.0 / 5.5 |
| Basses | 11.7 / 6.1 / 7.8 | 5.4 / -0.2 / 0.8 (2.6 / -1.9 / 1.0) | -0.4 / -1.8 / -1.0 | 0.9 / 0.1 / 1.7 |

The gain falls with the IOI (at 1000 ms 4-7 dB under 136 ms: SSO's Performance notes swell when held longer), and
with the dynamic (the Performance patch's CC1 curve is steeper than the Long's). `gen_spitfire_sso.py` writes the grid
into the map (`contextLevel`, d 32 / 64 / 96 / 112, IOI 100 / 136 / 200 / 300 / 500 / 1000 ms, three pitches).

Validation (same VM, build 435 with the new map, full Recommended; `grid_ana.py groups` / `parts`): the calibration
score (`grid_score.py`'s calib.mscx: 8 held halves, 16 slurred quarters at q = 60, mp then f) slurred - held per
section at mp Violins 1 / 2, Violas, Celli, Basses -0.6 / -0.1 / +0.5 / +1.3 / -1.4 dB (before -1.4 / +5.0 / -2.0 /
+2.7 / +2.3), at f 0.0 / -0.8 / 0.0 / -0.7 / +1.0 (before -1.8 / +3.4 / -3.1 / +2.0 / +1.7); the slurs' section
differences now follow the held notes' (mp: slurs Va - V1 -4.3, Vc - Va +8.0 against held -5.5 / +7.3; before -6.1 /
+12.1). *Whence* (quarter tones), the Performance notes: Va - V1 -1.7, Vc - Va +7.4 dB (before -8.1 / +13.3).

Test: `tst_soundlibrary::levelContext` (the grid, the layers, CC1 at the written value, one CC11 per slur, the volumes),
`tst_liveequivalence::liveContextLevel` (Live's clips, the plain set's lanes and Kontakt volumes).

#### Under `[slurs] plainPerformance` (2026-10-17, branch plain-levels)

With `[slurs] plainPerformance` (1 in Recommended and by default since 2026-10-17) every bowed note of the five sections
plays the plain Performance legato at velocity 64, staccatos (a spiccato attack each) and notes outside a slur
included, which the grid above never measured (it had slurs only, on `[slurs] performance`'s own legato). So the grid
was measured again as Recommended plays it today, per kind of note, and a plain note takes its kind's gains:

- **Kinds** (`ContextLevel::plainKind`): a staccato or staccatissimo as written (the tie chain's first note's; also
  inside a slur: it plays a spiccato attack) → the map's `contextLevelStaccato`; a note in a slur → `contextLevelPlain`;
  any other → `contextLevelSingle` (a kind the map lacks: `contextLevel`'s). A staccato or single note is a phrase of
  one, its length as its spacing (as the grid's figures). A staccato then also plays `[levels] calibrated` 2's **D** of
  the technique it would play otherwise (`Choice::plainFrom`: the Spiccato / Short 0.5, D -3.4 dB), so staccatos sit D
  under the held notes as a velocity technique's do. With plainPerformance on, the score's largest gain G counts every
  chord of those parts (the plain kinds too; a superset again).
- **Renders**: the Windows VM, build 16d3dbd (integrate-1017, run
  https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/39034459356), `--verify-playback --verify-wav` of
  `grid_score.py`'s scores, the map's `contextLevel*` zeroed in the VM's copy: per section, dynamic and grid pitch, the
  held reference (four half notes under staff text "non performance", the All techniques Long) and, at each IOI 100 /
  136 / 200 / 300 / 500 / 1000 ms, three figures of eight sixteenths on the plain Performance legato: two touching slurs
  of four, every note staccato, no slur. Takes A and B (B in reverse order), one score per take and dynamic (each part's
  WAV of a 39-minute take didn't fit the VM's disk); take to take sd 0.19 dB per figure. `grid_ana.py cells` →
  `sso_performance_context.json` › `plain`; `level_rule.py` → its `rule`: gain = held - figure (a staccato + D: the
  renders played D already, the zeroed gain plus D). The 2026-10-15 slur grid stays (top level) for
  `[slurs] performance` without plainPerformance.
- Gains at IOI 136 ms, mid register, slur / staccato / single (dB):

| Section | 32 | 64 | 96 | 112 |
|---|---|---|---|---|
| Violins 1 | 18.8 / 21.1 / 17.2 | 11.2 / 13.4 / 10.8 | 7 / 9.1 / 5.9 | 5.1 / 7 / 4.2 |
| Violins 2 | 13.8 / 19.8 / 11.7 | 8 / 12.2 / 6.9 | 4.2 / 7.7 / 3.6 | 2.8 / 5.5 / 2.4 |
| Violas | 19.8 / 27.5 / 16.8 | 16.4 / 21.8 / 13.4 | 9.4 / 14.2 / 8.9 | 7.2 / 10.9 / 6.7 |
| Celli | 28.7 / 30.3 / 23.8 | 16.7 / 18.1 / 13 | 10 / 10 / 7 | 7.3 / 7.8 / 5.1 |
| Basses | 9 / 9.5 / -1.9 | 3.2 / 3.6 / -2.7 | 0.4 / 0.6 / -1.8 | 2.4 / 2.2 / 0.7 |

  The plain slurs' gains lie within about 2 dB of the 2026-10-15 slurs'; at 136 ms the staccatos need 3-11 dB more than
  the slurs (velocity 64's spiccato attack is quiet at a low CC1), notes outside a slur 0-5 dB less (the Basses' at pp
  11 dB less: SSO plays fast unslurred notes louder).
- **Validation** (same build, full Recommended, the new map in the VM's copy; before: the branch's map without the plain
  grids, every plain note on the slur grid). Calibration score (`grid_score.py`'s calib.mscx: 8 held halves, then 8
  slurred / staccato / single quarters at q = 60), figure - held per section Violins 1 / 2, Violas, Celli, Basses
  (`grid_ana.py groups`; target: slurs and single notes 0, staccatos D = -3.4):
  - mp: slur +0.3 / -0.6 / +0.1 / +1.0 / -1.1 (before -0.7 / +0.1 / +0.5 / +0.9 / -0.4); staccato -3.4 / -3.2 / -0.5 /
    -1.1 / -2.6 (before -2.9 / -3.4 / -6.6 / -1.9 / -5.3); single -0.9 / +0.3 / +0.3 / +2.0 / -1.1 (before +0.4 / +0.8 /
    +0.7 / +2.1 / -0.1).
  - f: slur +0.8 / -0.3 / -0.1 / -0.8 / -0.1 (before +0.5 / -0.3 / 0.0 / -0.7 / +0.4); staccato -1.9 / -4.3 / -2.7 /
    -2.7 / -3.7 (before -4.2 / -2.2 / -2.8 / -1.7 / -4.5); single +0.8 / -0.4 / +0.3 / +0.3 / -0.1 (before +1.3 / +0.6 /
    +1.0 / +0.2 / +0.5).
  - RMS error over both markings and five sections: staccatos 1.4 dB (before 1.5), single notes 0.8 (0.9), slurs 0.6
    (0.5). The staccatos' errors are left over from the grid's figure against the calibration's (the Violas at mp +3.0,
    Celli +2.3), not from the gains' arithmetic (the events are as designed: `tst_liveequivalence` dumps).
  *Whence* (each part rendered alone; per kind from the score: staccato - slurred, target D = -3.4): Violins 1 -1.8
  (before -4.4), Violas -1.8 (before -6.0); Violins 2 (staccatos only) 1.9 dB louder than before; the sections'
  Performance medians Va - V1 -1.2, Vc - Va +8.3 dB (before -0.9 / +8.2). The dump (Linux) changes only CC11 on the
  Performance routes (and the patches' gain stage).
- **Metric P0** (2026-10-17, branch stac-levels; the cause of the staccatos' ±3 dB above). P
  (`ArticulationCheck::perceivedLoudnessDb`) has a threshold term (`LOUDNESS_A`) that sits near the levels of SSO's quiet
  notes, so P grows 1.1-1.4 dB per dB of gain there (calib.mscx, P at +5 vs -5 dB: held notes 1.07-1.17, the zeroed
  grid's staccatos up to 1.41 on the Violas at mp, 17 dB under the held note). held - figure in P was taken as a gain in
  dB, so the quietest figures got too much: on calib (kthost renders, current map vs the map's grids zeroed) the gain the
  staccatos played moved them 3.8 dB (Violas mp), 2.2 (Celli mp) more in P than the map's gain. Ruled out by measurement:
  round robins (two passes in one Kontakt, every figure the same to 0.1 dB), the window against the staccato's length
  (a staccato's length is its written one, as the grid's: `ContextLevel::gain`), even levels (no CC11 changes on calib),
  CC11's law (the dump's levels against the renders: median 0.5 dB, the rest is P's slope). The plain grid is now
  measured in **P0**, P without the threshold (`perceived.py` a = 0: specific loudness E^0.3, 0.996 dB per dB at any
  level): `grid_ana.py` writes P, L and P0 per cell, `level_rule.py`'s plain rule reads P0 (the 2026-10-15 slur grid,
  `[slurs] performance` without plainPerformance, keeps P). Renders: the grid's eight scores dumped on Linux (build
  98572aa, map's `contextLevel*` zeroed, the grid's settings) and rendered per route through kthost
  (`performance_context/grid_kthost.py`, `run.cmd`; against the verify renders: kthost - verify P per cell median
  -0.03 dB, sd 0.12-0.34 per section and dynamic, the take-to-take sd 0.19). The map's plain gains moved by median
  -0.9 (slur), -1.7 (staccato), -0.7 (single) dB, most at pp and short IOIs (up to -9.6).
  - Validation, calib.mscx (kthost, Recommended, figure - held, Violins 1 / 2, Violas, Celli, Basses; target slurs and
    single notes 0, staccatos -3.4), P (P0): mp staccato -4.7 / -4.4 / -2.8 / -4.1 / -3.8 (-4.1 / -3.8 / -2.2 / -3.6 /
    -3.0), f staccato -3.1 / -4.5 / -3.5 / -3.3 / -3.7 (-2.7 / -4.1 / -3.1 / -3.0 / -3.2); mp slur -0.6 / -0.7 / -0.8 /
    -0.9 / -1.2, f slur +0.4 / -0.4 / -0.9 / -1.0 / +0.4. RMS error, P: staccatos 0.72 dB (before 1.36), single notes
    0.66 (0.78), slurs 0.78 (0.63); P0: staccatos 0.58 (1.53), single 0.51 (0.67), slurs 0.66 (0.53); largest staccato
    error 1.3 dB (before 2.9). The slurs and single notes now sit 0.5-1 dB under the held notes at mp in all five
    sections alike (calib against the grid's own figure; not explained).
  - *Whence* (kthost, the part's routes mixed; staccato - slurred, target -3.4), P: Violins 1 -2.3 (before -1.6),
    Violas -3.0 (-2.0): mean error 0.8 dB (1.6); P0: -2.0 (-1.4), -2.6 (-1.7). The dump changes only CC11 on the
    Performance routes and the patches' gain stage; the playback audit's findings are unchanged.
- The first VM check counted D twice (the grid's renders had it in place; calib's staccatos 3.4 dB over the held notes
  at mp, Whence's 1.2 over the slurs): `level_rule.py` adds D to the staccato cells since.
- Tests: `tst_soundlibrary::levelContextPlain` (the kinds' grids, the fallback, `plainKind`, the top, CC11 per kind).

### Section balance

**Since 2026-10-14 Recommended and the default play `[levels] sectionBalance` 2, the Clark & Luce balance** (the owner,
2026-10-14: "honestly I don't trust my ears, I prefer all the balance levels and the dynamic levels loudness to be based
on real data"; the same source as `[levels] realRange` 2's spans). 0 off (Library default), 1 the map's `sectionLevel`
(TU Berlin single notes at Meyer's mp, below), 2 the map's `sectionLevel2` (a patch without it plays its
`sectionLevel`). Both are applied the same way (How it plays, below); only the real side differs.

Where `sectionLevel2` comes from (`tools/soundlibraries/section_balance_cl.py` with `clark_luce_levels.json` and
`clark_luce_table2.json` -> `sso_cl_section_balance.json` -> `gen_spitfire_sso.py` `SECTION_LEVEL2` -> the map):

- Real side: M. Clark, D. Luce, "Intensities of orchestral instrument scales played at prescribed dynamic markings",
  J. Audio Eng. Soc. 13(2), 1965, Fig. 2: each instrument's scales played pp / mf / ff as in orchestral music, dB SPL at
  10 m in its loudest direction (13 instruments, two players each; readings in `clark_luce_levels.json`). To sound
  power: Lw = Lp + 20 log10(10 m) + 11 - DI_max (DI_max: the TU Berlin database's directivity index in that loudest
  direction, at ff: violin 2.62 … horn 9.69 dB). mp = pp + 2/3 (mf - pp). A patch = one player + 10 log10 N (strings
  16 / 14 / 12 / 10 / 8, solo 1, a2 2, a6 6).
- The French horn (since 2026-10-15, docs/BALANCE_LOG.md › Iteration 1): Fig. 2's horn isn't the mean of its Table II
  scales (every other instrument's is, within 0.05 .. 1.05 dB) but follows player 2, whose scales lie 20 .. 40 dB under
  player 1's; the horn takes player 1's pp and mf (`clark_luce_table2.py`, Table II read from the scan, review page
  https://claude.ai/artifact/5W2xDamjK9vLRcJ4S6cFC2, owner to check). One player: about ±4 dB.
- Each player counted once (since 2026-10-15, docs/BALANCE_LOG.md › Iteration 3): Fig. 2 weighs every scale alike, so
  a player who played both styles (with and without vibrato) counts twice; Fig. 2 + `clark_luce_table2.json`'s
  `playerWeight` (the players' mean less the scales' mean, from Table II): the tuba +1.4 dB at mp, the rest -0.7 .. +0.8.
- SSO side and M: as `sectionLevel` (below): SSO's Long at CC1 64 over the patch's common range, D = SSO - real,
  M = -(D - the strings' mean D) (strings centred, winds on their scale).
- No Clark & Luce instrument (since 2026-10-15, docs/BALANCE_LOG.md › Iteration 5): Bass Clarinet, Contrabassoon, Bass
  Trombone Solo / a2 take their sibling's (clarinet's, bassoon's, tenor trombone's) Clark & Luce level plus the TU Berlin
  database's difference between the two at mp over the semitones both played (-4.46, +1.71 dB for the contrabassoon and
  the bass trombones): one source's ratio, not an assumption; the bass trombones' is corroborated by Iowa MIS (+1.1 pp /
  +1.0 mf / +2.0 ff) and ISO 23591 (+1). The Bass Clarinet's (Iteration 13, 2026-10-15) is the median of four independent
  sources, -0.35 dB (ISO 23591 Annex A -1, O'Brien 2013 0, TU Berlin's ff +0.5, Iowa MIS mf -0.7; spread -1.0 .. +0.5,
  uncertainty about ±2 dB), in place of TU's mp +4.88 (one player's loud pp, ±5 dB).
  Before: their family member's change from `sectionLevel` to `sectionLevel2` (an assumption).
- The harp (since 2026-10-15, Iteration 6): TU Berlin's sound power (no Clark & Luce figure) on Clark & Luce's scale:
  TU's measure for a decaying tone (the fast-weighted pressure squared averaged over the recording: `tu_transient.py`
  reproduces the CSV's harp values within 0.005 dB) as an energy, SSO's harp (`sso_percussion_levels.json`, velocity 64)
  over the same span after the note-on; TU's mp by rule D; to Clark & Luce's scale by the median (M2 + sectionLevel's D)
  over the nine solo winds with their own Clark & Luce instrument (RMS 3.3 dB): -9.81 dB since iteration 8's directivity (-10.41 before), about ±4 dB. The timpani
  measured that way (+1.65) were not applied (TU's D2 series name no markings: mp's step unknown by about 7 dB).
- The timpani (since 2026-10-15, Iterations 10 and 14): the Spheres dataset's Romeo and Juliet (1880 version), every part
  recorded alone, markings read from the 1881 Bote & G. Bock full score (review page
  https://claude.ai/artifact/R2bYpawLeH8MU2585HRoXj, checked by the owner: four readings corrected, Iteration 14):
  the timpani's energy per hit at f (11 hits, Allegro giusto) and p (145 hits, the coda: a sixteenth triplet and an eighth
  on every beat), position-corrected by the seats' sweeps, put on Clark & Luce's scale by the eight string lines at mf in
  the same coda (their Clark & Luce power less their level; RMS 2.3 dB): 98.6 / 72.4 dB re 1 pW s, mp by a third of
  p -> f; SSO's timpani energy at velocity 64 over keys 46 .. 62 (`sso_percussion_levels.json`): **-6.92**, about ±3 dB
  (scale RMS 2.3, main microphone L only: Zenodo stopped answering; L + C without the celli moved the levels by under
  0.5). One recording, one player; TU Berlin's D2 series (no markings) rises 20.9 .. 24.7 dB over the same three steps
  against the Spheres' 26.3 (`spheres_timpani.py`, `spheres_timpani.json`; a velocity patch: realRange below by CC11).
- The Grand Piano (since 2026-10-15, Iteration 11): Meyer 2009 § 3.4.2 (two-voice scales, lid open, no pedal: pp 88, ff
  104 dB), mp at 2 of Meyer's 5 steps, moved to Clark & Luce's scale by the median offset over the instruments both
  measured (-3.92 dB, RMS 2.50 against 3.87 with no offset); SSO's Grand Piano (Direct) swept on the VM every 6 semitones
  21 .. 105: **-1.11**, about ±3 dB. Celeste, glockenspiel, xylophone, marimba, vibraphone, crotales, tubular and desk
  bells (Iteration 12): no source of their level in an orchestra; the University of Iowa's recordings give only their
  pp / ff extremes (18 .. 39 dB, spread 4 .. 10 dB by pitch), not markings: they play SSO's own level and span.
- The percussion section (Iteration 7): SSO's six-player group at mf against the string family lies within
  Skålevik 2019's -8 .. -2 dB by energy (-3.6 / -2.0, peak +1.8): no offset.
- **In context** (since 2026-10-24, docs/BALANCE_LOG.md › Iteration 15; the owner, after checking 40 of the score
  readings on the spot-check page, all right: "apply the full measured balance"): the flutes **+3.96**, oboes **-4.17**,
  horns **-8.29** and trumpets **-4.59 dB** on top of the Clark & Luce level, every patch of the instrument (solo, a2, a6,
  the Motif horns a4 and trumpets a3). The shift is how much louder (+) or softer (-) one player plays against the strings
  at the same written marking in real music than the map implies, the pooled value of two anechoic orchestra recordings:
  Aalto's (Beethoven 7, Bruckner 8, Mahler 1; one gain for every part) and TU Berlin's Beethoven 8 (calibrated; period
  instruments) (docs/RESEARCH.md › 1.5a; `anechoic_balance.json` -> `anechoic_shift.py` -> `sso_anechoic_shift.json` ->
  `gen_spitfire_sso.py` `ANECHOIC_SHIFT`). About ±3 dB each. Applied where both recordings measured the instrument, agree
  in sign and each 90 % interval excludes 0; the clarinets (the datasets +2.7 / -0.8), bassoons (TU's interval includes 0),
  trombones and tuba (Aalto only) and the strings (the centre) keep their Clark & Luce level. Piccolo, Alto Flute, Bass
  Flute take the flute's shift and Cor Anglais the oboe's: **an assumption** ("plays like its family"; neither recording
  has them). The harp's and the Grand Piano's offsets to this scale were left as they are (the harp's median over the
  solo winds would move -3.4 dB with the shifted winds, the piano's -0.1: BALANCE_LOG.md › Iteration 15).
- Corroboration (one player against a string player, woodwind / brass, dB): Clark & Luce + directivity +2.6 .. +4.0 /
  +11.3 .. +11.9 (pp .. mf; at ff -1.9 / +8.3); the Spheres dataset's whole sections +1.4 / +14.3 (its claps showed no
  gain change between sessions: +2.1 / +1.9 / +3.0 dB L / R / C, SD ~3.8, so the sessions stand as measured); Skålevik
  2019 (JASA 146, 2802, orchestra dosimetry fit; readings by eye ±0.5 dB, owner to review) +3.2 / +4.2 in average
  sessions, +4.2 / +12.2 in forte passages; `sectionLevel` (TU single notes at Meyer's mp) +11.9 / +18.9, the outlier.
- Uncertainties: two players per instrument, who disagree by up to 10.0 (violin), 11.5 (horn) dB over the range (Clark
  & Luce Table III); absolute levels differ by up to 9.7 dB (horn) between them; directivity by marking since 2026-10-15 (TU's recordings, pp and ff: the brass 2-6.5 dB less directional at pp; docs/BALANCE_LOG.md › Iteration 8; before: ff only (brass
  at pp may be 3-4 dB more directional); instrument families stand for each patch's instrument.

| Patch | sectionLevel2 (dB, 2) | sectionLevel (dB, 1) | real Lw at mp (dB re 1 pW) |
|---|---|---|---|
| Violins 1 | -0.03 | -2.60 | 90.0 |
| Violins 2 | +1.12 | -1.45 | 89.4 |
| Violas | -7.08 | -3.08 | 86.1 |
| Celli | +2.56 | +1.89 | 92.7 |
| Basses | +3.43 | +5.24 | 90.9 |
| Flute Solo | +1.96 (was -2.00; in context +3.96) | +7.39 | 81.9 |
| Flutes a2 | -0.02 (was -3.98; in context +3.96) | +5.41 | 84.9 |
| Oboe Solo | -10.30 (was -6.13; in context -4.17) | +0.16 | 85.2 |
| Oboes a2 | -13.12 (was -8.95; in context -4.17) | -2.65 | 88.2 |
| Cor Anglais | -8.44 (was -4.27; in context -4.17, an assumption) | +3.98 | 81.4 |
| Clarinet Solo | -2.99 | +3.23 | 82.0 |
| Clarinets a2 | -4.66 | +1.56 | 85.1 |
| Bass Clarinet (median of 4 sources) | -5.14 | +0.92 | 86.9 |
| Bassoon Solo | -15.04 | -2.80 | 81.2 |
| Bassoons a2 | -2.63 | +9.61 | 84.2 |
| Contrabassoon (TU ratio) | -14.43 | -3.00 | 76.7 |
| Horn Solo | -7.83 (was +0.46; in context -8.29) | +3.86 | 94.8 |
| Horns a2 | -1.62 (was +6.67; in context -8.29) | +10.07 | 97.8 |
| Horns a6 | +2.18 (was +10.47; in context -8.29) | +13.87 | 102.5 |
| Trumpet Solo | +4.63 (was +9.22; in context -4.59) | +9.00 | 96.6 |
| Trumpets a2 | +9.20 (was +13.79; in context -4.59) | +13.57 | 99.6 |
| Trumpets a6 | +15.85 (was +20.44; in context -4.59) | +19.65 | 104.4 |
| Tenor Trombone Solo | -0.20 | +4.82 | 89.8 |
| Tenor Trombones a2 | +6.17 | +11.19 | 92.8 |
| Trombones a6 | +15.89 | +19.96 | 97.5 |
| Bass Trombone Solo (TU ratio) | +2.96 | +6.38 | 91.5 |
| Bass Trombones a2 (TU ratio) | +6.69 | +10.12 | 94.5 |
| Tuba Solo | -0.97 | +8.72 | 95.1 |
| Solo Violin 1 | -0.05 | - | 77.9 |
| Solo Violin 2 | -0.23 | - | 77.9 |
| Solo Viola | -3.92 | - | 75.3 |
| Solo Cello | +3.09 | - | 82.7 |
| Motif Horns a4 | +4.75 (was +13.04; in context -8.29) | - | 100.8 |
| Motif Trumpets a3 | +9.12 (was +13.71; in context -4.59) | - | 101.4 |
| Motif Trombones a5 | +8.17 | - | 96.8 |
| Piccolo (flute) | -0.24 (was -4.20; in context +3.96, an assumption) | - | 81.9 |
| Alto Flute (flute) | +6.07 (was +2.11; in context +3.96, an assumption) | - | 81.9 |
| Bass Flute (flute) | +1.49 (was -2.47; in context +3.96, an assumption) | - | 81.9 |
| Contrabass Clarinet (clarinet) | -0.21 | - | 82.0 |
| Contrabass Trombone (trombone) | -1.93 | - | 89.8 |
| Cimbasso Solo (trombone) | +4.77 | - | 89.8 |
| Cimbassi a2 (trombone) | +14.34 | - | 92.8 |
| Contrabass Tuba (tuba) | -2.68 | - | 95.1 |
| Harp | -9.81 | - | - |
| Strings Ensemble (the sections it mixes) | +11.93 | - | - |
| Timpani (Spheres) | -6.92 | - | 81.1 (energy per hit, dB re 1 pW s) |
| Grand Piano (Meyer) | -1.11 | - | 87.5 |

The 15 rows from Solo Violin 1 to Contrabass Tuba (since 2026-10-15, docs/BALANCE_LOG.md › Iterations 2 and 4) had no
section level and played SSO's own level; since Iteration 4 their SSO side is their own sweep, as every other patch's
(until then the dynamics check at one test pitch + 1.50 dB, ±2.3 dB). In brackets: the family member whose real level
and span the patch takes (an assumption); "TU ratio": the sibling's Clark & Luce level plus TU Berlin's difference
(Iteration 5). The harp: Iteration 6 (above). Strings Ensemble (since 2026-10-15, docs/BALANCE_LOG.md › Iteration 9):
its Long is the five sections' own samples mixed, every section that has the note (Basses 24 .. 50, Celli 36 .. 80,
Violas 48 .. 90, Violins 1 / 2 56 .. 96, faded in and out over 4 .. 10 semitones at their ends; each at -13.3 dB against
its own patch, the basses at -5.7), found by cross-correlation with the sections' recordings and a harmonic-by-harmonic
fit (`strings_ensemble.py` -> `sso_strings_ensemble.json`); per key the target is the balanced sections it plays (their
Long + `sectionLevel2` + how far faded), +11.93 the mean over keys 24 .. 96 (SD 2.0 across keys: the basses-only
bottom +7.3 .. +10.4, so with one offset it plays 2 .. 4 dB over the balanced sections there, the middle +13 .. +15; the
sections' own uncertainty carries over). Before: SSO's own level, about 12 dB under the balanced sections. The celeste and
mallets have none (Iteration 12: no source), nor the other percussion (Iteration 7: SSO's group already inside
Skålevik's range). Timpani: Iterations 10 and 14; Grand Piano: Iteration 11 (above).

Biggest moves from `sectionLevel`: Bassoon Solo -12.5 dB (TU's single notes put it far louder than orchestral players
play it), Horn Solo -7.1, Violas -3.7; the trumpets barely move.

#### The TU Berlin balance (`sectionLevel`, sectionBalance 1)

The owner, 2026-10-11: **M**, on in Recommended (and by default) until 2026-10-14, off in Library default; per score
through `playbackSettings`. `[levels] sectionBalance` plays each section M dB apart from the flat level the level rule
plays (every section's written dynamic equally loud). Since 2026-10-14 (the owner, "sure") one rule covers every
section: mp where `[levels] realRange` plays it.

| Section | Violins 1 | Violins 2 | Violas | Celli | Basses |
|---|---|---|---|---|---|
| M (dB, map `sectionLevel`), since 2026-10-14 | -2.60 | -1.45 | -3.08 | +1.89 | +5.24 |
| M of 2026-10-11 (mp at 0.4 of pp -> ff) | -3.81 | -2.66 | -2.12 | +2.77 | +5.81 |

Where M comes from (`tools/soundlibraries/section_balance_winds.py` -> `sso_winds_section_balance.json` ->
`gen_spitfire_sso.py` `SECTION_FADERS` / `SECTION_FADERS_WINDS` -> the map): the balance at the CC1 Whence plays (64).

- Real side: Weinzierl, Lepa, Schultz, Detzner, von Coler & Behler 2018, "Sound power and timbre as cues for the dynamic
  strength of orchestral instruments", JASA 144(3) (TU Berlin database v3: single tones' sound power per MIDI pitch,
  linear in dB between measured pitches). Total sound power, no directivity (beyond the critical distance the
  reverberant level follows the power). A section = one player + 10 log10 N, N = 16 / 14 / 12 / 10 / 8 (SSO's section
  sizes, Spitfire's product page); winds solo 1, a2 2, a6 6.
- mp: pp + f (ff - pp) at each pitch, f = a + 2 (b - a) / 5 by the Meyer rule (Real range, below: the strings 0.54-0.59,
  the winds 0.50-0.63; the bass clarinet's and bass trombones' their sibling's fractions, an assumption), 3 / 7 by rule
  D (Cor Anglais, Contrabassoon). Until 2026-10-14 the strings' mp sat at f = 0.4 (CC1 64 is level 4750 on MuseScore's
  pp 3750 .. ff 6250, that scale taken as linear in dB).
- SSO side: measured on the Windows test VM (real Kontakt + SSO), All techniques Long at CC1 = velocity 64, the loudest
  50 ms of the note (`loudDb`), median of 4: `sso_cc1_curves.json` (Violins, Violas every 3 semitones, Celli and Basses
  every semitone since 2026-10-14) and, since 2026-10-28, `sso_winds_cc1_semitone.json` (every semitone; the 3-semitone
  `sso_winds_cc1_curves.json` gave the values before), linear in dB between measured pitches. The gap patches
  (`sectionLevel2` only) likewise `sso_gap_cc1_semitone.json` (Solo Violin 2, which it lacks, from `sso_gap_cc1_curves.json`).
  Effect of the every-semitone wind and brass sweeps (2026-10-28, docs/BALANCE_LOG.md): 37 of 47 `sectionLevel2` and 23
  of 28 `sectionLevel` values change, at most 0.56 dB (Contrabass Clarinet / Contrabass Tuba `sectionLevel2` +0.56, Oboe
  Solo `sectionLevel` +0.53), median 0.11 dB, inside the +-2 dB uncertainty; the five strings and the non-wind patches
  are unchanged. The old files, passed to the two scripts, reproduce the earlier numbers exactly.
- Per patch: the mean over every semitone of the Long's range that the database also measured of (SSO - real); M =
  minus that, centred over the five string sections (they add up to 0); the winds on the strings' scale, not re-centred,
  so each plays M louder beside the strings. Variants not used (2026-10-11, mp 0.4): by the sustain's RMS -3.69 / -3.00
  / -1.47 / +2.60 / +5.56, by LUFS -4.63 / -3.66 / -2.46 / +2.99 / +7.75. The 2026-10-11 method on today's curves gives
  -3.83 / -2.68 / -2.15 / +2.71 / +5.95; Meyer's mp moved the strings by up to 1.21 dB (Violins) and every wind 4.39 dB
  lower than on the 0.4 scale.
- Listening (the owner, 2026-10-11, ABX, blind, the 2026-10-11 M): the basses' 1.6-3.6 dB differences between candidate
  balances heard soloed (14/14), not in the full mix (5/8). The owner chose M for its model, not for a heard difference;
  the move to Meyer's mp (2026-10-14) was chosen for consistency, not heard.

**Woodwinds and brass** (the owner, 2026-10-14: from the solo data "until we can find better non-solo data"):

| Patch | M (dB) | Patch | M (dB) | Patch | M (dB) |
|---|---|---|---|---|---|
| Flute Solo | +7.39 | Bassoon Solo | -2.80 | Trumpets a2 | +13.57 |
| Flutes a2 | +5.41 | Bassoons a2 | +9.61 | Trumpets a6 | +19.65 |
| Oboe Solo | +0.16 | Contrabassoon (rule D) | -3.00 | Tenor Trombone Solo | +4.82 |
| Oboes a2 | -2.65 | Horn Solo | +3.86 | Tenor Trombones a2 | +11.19 |
| Cor Anglais (rule D) | +3.98 | Horns a2 | +10.07 | Trombones a6 | +19.96 |
| Clarinet Solo | +3.23 | Horns a6 | +13.87 | Bass Trombone Solo | +6.38 |
| Clarinets a2 | +1.56 | Trumpet Solo | +9.00 | Bass Trombones a2 | +10.12 |
| Bass Clarinet | +0.92 | | | Tuba Solo | +8.72 |

Large values (Bassoons a2 12.5 dB over Bassoon Solo, Trumpets a6 +19.7) are SSO's own levels: at CC1 64 Bassoons a2
measures ~9 dB under Bassoon Solo and Trumpets a6 no louder than Trumpet Solo, against a real a2 / a6 3.0 / 7.8 dB over
one player. Solo data: a real section's blend and its players' spread are not in it (HANDOFF.md › Section balance ›
Winds).

How it plays (`ContextLevel::volumes`):

- Each section (strings; since 2026-10-14 woodwinds and brass) has its level O (the map's `<Instrument sectionLevel>` on
  the main patch; extras take it). Every patch of the section plays O louder by its gain stage (`TrackDelays::patchGain`:
  the Kontakt slot hosted, MuseScore's Utility on the route set's and the plain set's Kontakt tracks in Live). Staccatos
  and Performance alike.
- With `[levels] context` a phrase plays gain + O, a staccato O, none capped, all the Performance tracks moving together
  (the owner, 2026-10-10). A part's Performance patch plays at T + O, T = min(G, S' + 6 - O), S' = max(0, max over
  phrases of gain + O - 6), its phrases' CC11 gain - T (the rule's own CC11 values where G fits under +6; T is at least
  the part's own loudest gain, so no CC11 goes over 127). Until 2026-10-15 everything was S' lower too, the loudest track
  at Live's Volume top (+6 dB); now the master trim (below) lowers everything only as far as the score's loudest moment
  needs. S' is kept (`Volumes::shift`) so T and the CC11 values stay what they were.
- So each note plays the rule's level plus its section's O, less one common constant (the master trim).
- Off, or no patch of the score's library parts with a level: the rule alone, as above.

Whence (quarter tones), Recommended (dumpEvents on the Linux dev machine, no Kontakt): the Celli - Performance stays
the top (+6), so every note plays O - O(Celli) against the setting off: the sections are M apart, the mix 1.89 dB
quieter (2.77 with the 2026-10-11 M; HANDOFF.md › Section balance). Against the 2026-10-11 M the volumes move by the M
change + 0.88: Violins 1 / 2 +2.09, Violas -0.08, Celli 0, Basses +0.31 dB. A full orchestra with a6 brass (a2
woodwinds, a6 horns / trumpets / trombones, tuba, five string sections, fff then mp; dumpEvents): Trombones a6 (O 20.07
+ R 5.39) set the top, everything 11.47 dB lower than before the winds' M (each patch also by its own M change: the
strings 10.26-12.43 dB lower; HANDOFF.md › Section balance › Winds).

Test: `tst_soundlibrary::levelSectionBalance` (the levels, the volumes, each note's level, the layers, a map without
levels, the shipped map's M and sectionLevel2 for all 28 patches, sectionLevel2 by mode with extras and fallback, a flute and Trumpets a6 on it under 1 and 2), `tst_liveequivalence::liveSectionBalance` (Live's clips, the route set's and the plain set's Kontakt volumes).

### Real range

Stage A of the dynamics plan (HANDOFF.md › Dynamics like a real player; the owner, 2026-10-12): a written dynamic is one
gesture; in SSO, CC1 (and a short's velocity) mostly picks the recording (tone), CC11 and the patch's volume are
loudness only. `[levels] realRange` (**2 in Recommended and by default, 0 in Library default**: the owner, 2026-10-13, heard on the ladder, "this sounds great";
2026-10-14 the section data where it's reliable, one source for every instrument: 2, Clark & Luce spans, below; 1 the
Meyer rule, by hand; per score through `playbackSettings`) makes each note of the five string sections (and, since 2026-10-14, the woodwinds' and
brass' main patches the database has) as loud as a real player at its written dynamic, the player's full range, by CC11
and the patches' volumes; CC1 stays MuseScore's (the written dynamic).

- **Clark & Luce spans** (`realRange` 2, Recommended and the default since 2026-10-14; the owner: "let's use the section
  data wherever it's reliable", then one source for every instrument, not mixed): each written marking is one equal step
  of S / 5 from mp, S the instrument's written pp -> ff as orchestral players play it, whatever its single-note span:
  pp = mp - 2S/5 … ff = mp + 3S/5, ppp / fff one step more (-3S/5, +4S/5), linear in CC1 between (continuous through
  hairpins); less SSO's Long's own change with CC1 as below. S from M. Clark, D. Luce, "Intensities of Orchestral
  Instrument Scales Played at Prescribed Dynamic Markings", *J. Audio Eng. Soc.* 13(2), 1965, 151-157, Table I
  "Dynamic Range" (printed numbers: two players per instrument, chromatic scales at pp, mf and ff as they would play those
  markings in orchestral music, anechoic, the average ff scale less the average pp scale). Meyer's in-play pp / ff (the
  Meyer rule, below) are the softest / loudest *possible* during fast scales, limits, not the markings; Clark & Luce
  measured the markings. Map `<Instrument realSteps="S">` (`real_range.py` `SPANS` → `sso_real_range.json`
  `sectionSpan` → `gen_spitfire_sso.py`); a patch without it plays the Meyer rule.

  | Instrument (SSO patches) | S, dB | Spheres cross-check (whole section, 3+ dynamics) |
  |---|---|---|
  | violin (Violins 1, 2, Solo Violin 1, 2) | 13.8 | Violin 2 13.7 |
  | viola (Violas, Solo Viola) | 15.9 | |
  | cello (Celli, Solo Cello) | 14.1 | Cello 15.8 |
  | double bass (Basses) | 14.1 | |
  | flute (Flute Solo, Flutes a2; Piccolo, Alto Flute, Bass Flute: family) | 7.5 | |
  | oboe (Oboe Solo, Oboes a2) | 7.4 | |
  | English horn (Cor Anglais) | 5.2 | |
  | clarinet (Clarinet Solo, Clarinets a2; Bass Clarinet, Contrabass Clarinet: family) | 8.3 | |
  | bassoon (Bassoon Solo, Bassoons a2; Contrabassoon: family) | 10.2 | |
  | horn (Horn Solo, Horns a2, a6, Motif Horns a4) | 17.7 | |
  | trumpet (Trumpet Solo, Trumpets a2, a6, Motif Trumpets a3) | 8.8 | Trumpet 10.6 |
  | trombone (Tenor Trombone Solo, a2, Trombones a6, Motif Trombones a5; Bass Trombone Solo, a2, Contrabass Trombone, Cimbasso Solo, Cimbassi a2: family) | 17.0 | Trombone 14.3 |
  | tuba (Tuba Solo; Contrabass Tuba: family) | 12.5 | |
  | timpani (Timpani; the Spheres dataset's Romeo and Juliet, f - p 26.3 dB over 3 steps; velocity patch: by CC11, SSO's own velocity curve the longLevel per written pitch) | 43.8 | (is the source) |

  *Family* is an assumption, not a measurement ("plays like its family"). The cross-check: the Spheres dataset (Colibrì
  Ensemble, Pescara; arXiv 2511.21247, Zenodo 17347681, CC BY-SA 4.0), each section of a chamber orchestra recorded alone
  playing chromatic scales in a studio; a section's loudest less its quietest full scale (median note level at the main
  pair), only where its recording has 3 or more section scales; the dynamics are not labelled in the dataset (two-scale
  sections' spans may not reach pp / ff and are not used). Where it has them it agrees with Clark & Luce within ~2 dB:
  independent support, not used in the map (one source for every instrument). Since 2026-10-15 (docs/BALANCE_LOG.md ›
  Iteration 4) the 15 patches without a section level before also have a real range: their Long swept the same way
  (`sso_gap_cc1_curves.json`); the solo strings and the Motif patches with their database instrument's realSpan (both
  rules), the eight family patches (Piccolo, Alto / Bass Flute, Contrabass Clarinet, Contrabass Trombone, Cimbasso Solo /
  a2, Contrabass Tuba) with realSteps only (realRange 2; realRange 1 leaves them as SSO plays them). Checked in Kontakt
  (all 43 patches with a real range, each marking at its test pitch as MuseScore sends it): the steps from mp within
  0.05 dB RMS (median) of steps x S / 5, every patch in order except Cor Anglais, whose round robins (4.5 dB apart at pp)
  exceed its 1.04 dB step. Strings Ensemble (its sections don't share a span: violas 15.9 against 13.8 .. 14.1 dB), the
  harp, keyboards and percussion other than the timpani have none.

  At a middle pitch, realDb (dB from mp, the CC11 before the headroom; ppp / pp / p / mp / mf / f / ff / fff;
  `sso_real_range.json` `realDbMid`), Clark & Luce spans against the Meyer rule (rule D for Cor Anglais):

  | Patch | pitch | S | realDb, Clark & Luce spans | realDb, Meyer rule |
  |---|---|---|---|---|
  | Violins 1 | 73 | 13.8 | -2.5 / -2.4 / -1.7 / 0 / +1.1 / +2.3 / +4.0 / +6.5 | -12.2 / -2.9 / -1.9 / 0 / +1.3 / +2.8 / +4.8 / +8.3 |
  | Violas | 75 | 15.9 | -3.0 / -2.7 / -1.8 / 0 / +1.3 / +2.9 / +5.0 / +8.0 | -7.2 / -2.0 / -1.4 / 0 / +1.0 / +2.2 / +4.1 / +7.0 |
  | Celli | 64 | 14.1 | -2.9 / -2.7 / -1.8 / 0 / +1.1 / +2.4 / +4.2 / +6.7 | -6.8 / -2.5 / -1.7 / 0 / +1.0 / +2.2 / +3.9 / +4.9 |
  | Basses | 52 | 14.1 | +1.1 / -1.0 / -1.6 / 0 / +2.9 / +1.7 / +0.4 / +1.9 | -3.0 / +0.3 / -0.9 / 0 / +2.2 / +0.4 / -1.6 / -0.4 |
  | Flute Solo | 77 | 7.5 | +7.4 / +2.2 / -0.2 / 0 / +2.7 / +5.0 / +4.1 / +4.3 | +8.3 / +4.3 / +0.8 / 0 / +1.6 / +2.9 / +0.9 / +0.7 |
  | Oboe Solo | 74 | 7.4 | +6.7 / +1.6 / -0.5 / 0 / +1.1 / -0.3 / -0.9 / -0.4 | +3.8 / +2.6 / 0 / 0 / +0.6 / -1.3 / -2.4 / -0.1 |
  | Cor Anglais | 67 | 5.2 | +3.4 / +2.1 / +1.6 / 0 / +2.6 / +3.2 / +2.3 / +1.3 | (rule D) +1.5 / +0.8 / +0.9 / 0 / +3.2 / +4.5 / +4.2 / +3.9 |
  | Clarinet Solo | 72 | 8.3 | +2.6 / +1.6 / -0.3 / 0 / +1.9 / +0.7 / 0 / +0.7 | -10.6 / -0.3 / -1.2 / 0 / +2.9 / +2.6 / +2.8 / +7.7 |
  | Bassoon Solo | 55 | 10.2 | +4.2 / +1.1 / +0.2 / 0 / +4.0 / +6.5 / +6.8 / +6.7 | +3.9 / +2.6 / +1.0 / 0 / +3.3 / +5.0 / +4.6 / +5.0 |
  | Horn Solo | 58 | 17.7 | +1.1 / -1.9 / -1.4 / 0 / +2.5 / +4.5 / +3.3 / +5.5 | -1.1 / +1.5 / +0.3 / 0 / +0.8 / +1.0 / -1.8 / +1.2 |
  | Trumpet Solo | 72 | 8.8 | +5.3 / +1.4 / +2.0 / 0 / -3.5 / -1.4 / +0.4 / -1.6 | -1.0 / +0.9 / +1.7 / 0 / -3.2 / -0.8 / +1.2 / +2.2 |
  | Tenor Trombone Solo | 54 | 17.0 | +8.7 / +4.9 / +3.8 / 0 / +1.8 / +1.5 / +3.7 / +6.9 | +0.8 / +6.5 / +4.6 / 0 / +1.0 / -0.2 / +1.2 / +7.4 |
  | Tuba Solo | 45 | 12.5 | +7.5 / +0.8 / -1.1 / 0 / +3.6 / +5.5 / +4.8 / +6.0 | +0.1 / +1.7 / -0.7 / 0 / +3.1 / +4.6 / +3.4 / +4.9 |

  The big changes: ppp no longer at the single-note extreme (violins -12.2 → -2.5 dB, clarinet -10.6 → +2.6: Clark &
  Luce's ppp is one marking step under pp), the horn's and trombones' wider ranges (horn ff -1.8 → +3.3), the flute's
  (+0.9 → +4.1 at ff). realDb is the player's change less SSO's own, so where SSO's Long already changes more than the
  player (the flute's, the oboe's soft end) it runs against the marking.
- **The Meyer rule** (`realRange` 1; the owner, 2026-10-14, replacing rule D: "this is clearly better"; Recommended until Clark & Luce spans the same day): TU Berlin's pp and ff ("as
  softly / loudly as possible", single notes) stay the player's ppp and fff. Written pp and ff are what players produce in
  normal playing: J. Meyer, *Acoustics and the Performance of Music* (Springer 2009), chapter 3, gives per instrument
  the sound power of single-note extremes X (the softest pp, the loudest ff) and of rapid two-octave scales P (pp, ff,
  every note sounded). Per instrument a = (P_pp - X_lo) / (X_hi - X_lo), b = (P_ff - X_lo) / (X_hi - X_lo) place pp and
  ff inside each pitch's TU Berlin span (its per-pitch shape kept): pp(p) = tuPp(p) + a span(p), ff(p) = tuPp(p) +
  b span(p). p, mp, mf, f in equal dB steps between pp and ff (Meyer's own method for his forte), so 5 steps of
  (b - a) span / 5; ppp → pp and ff → fff one step each. Relative to mp (CC1 64, where the section balance M was set; M
  and the level rule unchanged). A section's range in dB is one player's (section size doesn't change it).

  | Instrument (SSO patches) | Meyer § | X (single notes) | P (scales) | a | b |
  |---|---|---|---|---|---|
  | violin (Violins 1, 2) | 3.3.1.2 | 58-99 | 74-94 | 0.390 | 0.878 |
  | viola (Violas) | 3.3.2.2 | 63-95 (63 on D / A, 67 low) | 73-91 | 0.313 | 0.875 |
  | cello (Celli) | 3.3.3.2 | 63-98 | 74-96 | 0.314 | 0.943 |
  | double bass (Basses) | 3.3.4.2 | 66-100 | 79-96 | 0.382 | 0.882 |
  | flute (Flute Solo, Flutes a2) | 3.2.1.2 | 67-100 | 82-94 | 0.455 | 0.818 |
  | oboe (Oboe Solo, Oboes a2) | 3.2.2.2 | 70-103 | 83-95 | 0.394 | 0.758 |
  | clarinet (Clarinet Solo, Clarinets a2; Bass Clarinet: assumed) | 3.2.3.2 | 57-106 (57 fifth octave, 65 typical) | 77-97 | 0.408 | 0.816 |
  | bassoon (Bassoon Solo, Bassoons a2) | 3.2.4.2 | 72-102 | 81-96 | 0.300 | 0.800 |
  | horn (Horn Solo, Horns a2, a6) | 3.1.1.2 | 65-117 | 86-107 | 0.404 | 0.808 |
  | trumpet (Trumpet Solo, Trumpets a2, a6) | 3.1.2.2 | 78-111 | 89-104 ("sound pressure", read as power) | 0.333 | 0.788 |
  | tenor trombone (Tenor Trombone Solo, a2, Trombones a6; Bass Trombone Solo, a2: assumed) | 3.1.3.2 | 73-113 | 89-105 | 0.400 | 0.800 |
  | tuba (Tuba Solo) | 3.1.4.2 | 77-112 | 93-108 | 0.457 | 0.886 |

- **Siblings' fractions, an assumption** (the owner, 2026-10-14, "sure": "plays like its family"; not a measurement):
  Meyer gives the bass clarinet (§3.2.3.4: single notes only) and the bass trombone (§3.1.3.4: no levels) nothing to
  place pp / ff by, so Bass Clarinet takes the clarinet's a / b and Bass Trombone Solo / a2 the tenor trombone's
  (`real_range.py` `SIBLING`; their entries in `sso_real_range.json` carry `meyerSibling`). Effect at the middle pitch
  (realDb pp / mp / ff, rule D -> sibling's): Bass Clarinet (55) -6.2 / 0 / +2.2 -> -3.4 / 0 / -2.0, Bass Trombone Solo
  (47) +1.3 / 0 / +10.0 -> +5.5 / 0 / +3.7, Bass Trombones a2 (47) -4.1 / 0 / +1.9 -> +0.1 / 0 / -4.4.
- **Rule D** (the owner, 2026-10-12; now only Cor Anglais (§3.2.2.4 limits only) and Contrabassoon (§3.2.4.6 in-play
  only), which the owner was not asked about): the markings in 7 equal dB steps of span / 7, i.e. a = 1/7, b = 6/7.
  `real_range.py --rule-d` writes every instrument by it (comparison).
- **Each note** plays by CC11 `realDb(x, p) = (F(steps(x)) - F(0)) × span(p) - long(x, p)`: x the dynamics CC in force as
  written (so CC1 and CC11 move together: a hairpin ramps both), steps(x) its distance from mp in markings (ppp -3, pp -2 …
  ff +3, fff +4; between them linear in CC1, the inverse of `Ms4::expressionLevel`), F the level as a fraction of the span
  above the single-note pp (ppp 0, pp a, … ff b, fff 1, linear in steps: `ContextLevel::realFraction`, continuous in CC1
  through hairpins), span(p) one player's ff - pp at the pitch, long(x, p) SSO's All techniques Long at CC1 x less at 64
  (`ContextLevel::realRangeDb`).
- **Every technique**: with [levels] calibrated 1 a short is calibrated to the Long at its dynamic, a Performance legato
  note to the Long by CC1 ([levels] calibrated 1 and 2) or, with [levels] context, by its slur's gain measured in the music
  at the written dynamic against the staccatos (which are the Long's level there). Each is the Long's level at the written
  CC1, which already holds SSO's own change with CC1 once; realDb takes it away (- long) and puts the player's in, so it is
  counted once.
- **Velocity techniques under [levels] calibrated 2** (2026-10-15, option B): a short, pizzicato, col legno, spiccato ...
  is matched to the Long at its marking and plays a constant D more (Calibrated levels › 2), so it follows the Long's change
  and takes the same realDb as the Long. (Under the first 2, A, the technique played its own velocity layers; taking its own
  change from mp away instead would have needed up to +24 dB at pp, Celli pizzicato: measured, not done.) The realDb the
  strings' velocity techniques get (Clark & Luce spans, at the patch's test pitch, pp / mp / ff): Violins 1 -2.55 / 0 / +3.97
  dB, Violins 2 -3.28 / 0 / +4.56, Violas -3.44 / 0 / +5.63, Celli -4.91 / 0 / +4.49, Basses -1.76 / 0 / +0.96.
- **Capped shorts**: a short whose calibrated velocity is 127 and still under its level (or 1 and still over) gets the
  rest by CC11 (`realExtra`, rendermidi.cpp `realRangeLevel`); before, it stayed under silently. Not with the note's own
  velocity (the Velocity lane).
- **Headroom**: CC11 only lowers. A part's notes play realDb (+ realExtra) less the part's largest R, found by rendering
  the score once (`ContextLevel::realRangeTops`, every CC1 value each note meets), and every patch of the part plays R
  louder by its gain stage (`TrackDelays::patchGain`), with the level rule's and the section balance's, less the master
  trim (below).
- **Live**: the CC11 values are the renderer's (`libraryNoteLevels`), so Live's clips and the plain set's CC11 lanes carry
  them; the gains reach MuseScore's Utility on the route set's and the plain set's Kontakt tracks
  (`tst_liveequivalence::liveRealRange`).

The data (`tools/soundlibraries/real_range.py` → `sso_real_range.json` → `gen_spitfire_sso.py` → the map's
`<Instrument realSpan longLevel realPlay>` on the main patches, their extras take them; `realPlay="a b"` only where the
Meyer rule applies; each entry of the JSON has both rules' levels at its middle pitch, `marksMid` and `realDbMid`):

- Real side: Weinzierl et al. 2018 (TU Berlin database v3, `3_Audio_Features.csv`, "SoundPower in dB" of single tones,
  modern instruments, `_et_` files, pp and ff; violin for Violins 1 and 2). The span ff - pp per semitone is smoothed (single
  takes jump by several dB a semitone): local linear least squares over ±k semitones, k = 5 chosen by leave-one-out
  cross-validation pooled over the four instruments (RMS 2.39 dB; k 1 … 12: 2.69 … 2.49). Beyond the database's range: the
  nearest end's.
- SSO side: `sso_cc1_curves.json` (Windows VM, Kontakt 8 + SSO, 2026-10-11), rmsDb (the sustain's mean power 1.5-3.0 s after
  the note-on: like for like with sound power), each measured pitch less its CC1 64, unsmoothed (repeat spread ~1 dB);
  linear in CC1 and pitch between the points.

At a middle pitch (dB from mp; the player's level and realDb, the CC11 before the headroom, at ppp / pp / p / mp / mf / f /
ff / fff; from `sso_real_range.json`):

| Patch | pitch | span | player ppp / pp / ff / fff, Meyer (rule D) | realDb Meyer | realDb rule D |
|---|---|---|---|---|---|
| Violins 1 | 73 | 30.8 | -18.0 / -6.0 / +9.0 / +12.8 (-13.2 / -8.8 / +13.2 / +17.6) | -12.2 / -2.9 / -1.9 / 0 / +1.3 / +2.8 / +4.8 / +8.3 | -7.4 / -5.7 / -3.3 / 0 / +2.7 / +5.6 / +9.0 / +13.1 |
| Violins 2 | 73 | 30.8 | as Violins 1 | -12.0 / -2.8 / -2.0 / 0 / +1.5 / +3.1 / +5.2 / +8.7 | -7.2 / -5.6 / -3.4 / 0 / +2.9 / +5.9 / +9.4 / +13.5 |
| Violas | 75 | 25.4 | -13.7 / -5.7 / +8.6 / +11.7 (-10.9 / -7.2 / +10.9 / +14.5) | -7.2 / -2.0 / -1.4 / 0 / +1.0 / +2.2 / +4.1 / +7.0 | -4.4 / -3.6 / -2.2 / 0 / +1.8 / +3.8 / +6.4 / +9.8 |
| Celli | 64 | 21.8 | -12.3 / -5.5 / +8.2 / +9.4 (-9.3 / -6.2 / +9.3 / +12.4) | -6.8 / -2.5 / -1.7 / 0 / +1.0 / +2.2 / +3.9 / +4.9 | -3.8 / -3.3 / -2.1 / 0 / +1.4 / +2.9 / +5.0 / +7.9 |
| Basses | 52 | 21.5 | -12.5 / -4.3 / +6.5 / +9.0 (-9.2 / -6.2 / +9.2 / +12.3) | -3.0 / +0.3 / -0.9 / 0 / +2.2 / +0.4 / -1.6 / -0.4 | +0.3 / -1.5 / -1.9 / 0 / +3.2 / +2.2 / +1.2 / +3.0 |
| Flute Solo | 77 | 6.0 | -3.6 / -0.9 / +1.3 / +2.4 (-2.5 / -1.7 / +2.5 / +3.4) | +8.3 / +4.3 / +0.8 / 0 / +1.6 / +2.9 / +0.9 / +0.7 | +9.3 / +3.5 / +0.4 / 0 / +2.0 / +3.7 / +2.1 / +1.7 |
| Oboe Solo | 74 | 13.6 | -7.3 / -2.0 / +3.0 / +6.2 (-5.8 / -3.9 / +5.8 / +7.8) | +3.8 / +2.6 / 0.0 / 0 / +0.6 / -1.3 / -2.4 / -0.1 | +5.3 / +0.7 / -0.9 / 0 / +1.5 / +0.6 / +0.4 / +1.4 |
| Clarinet Solo | 72 | 31.8 | -18.2 / -5.2 / +7.8 / +13.6 (-13.6 / -9.1 / +13.6 / +18.2) | -10.6 / -0.3 / -1.2 / 0 / +2.9 / +2.6 / +2.8 / +7.7 | -6.0 / -4.2 / -3.2 / 0 / +4.8 / +6.5 / +8.7 / +12.2 |
| Bassoon Solo | 55 | 12.8 | -6.4 / -2.6 / +3.9 / +6.4 (-5.5 / -3.7 / +5.5 / +7.3) | +3.9 / +2.6 / +1.0 / 0 / +3.3 / +5.0 / +4.6 / +5.0 | +4.8 / +1.6 / +0.4 / 0 / +3.8 / +6.1 / +6.2 / +5.9 |
| Horn Solo | 58 | 22.6 | -12.8 / -3.7 / +5.5 / +9.8 (-9.7 / -6.5 / +9.7 / +12.9) | -1.1 / +1.5 / +0.3 / 0 / +0.8 / +1.0 / -1.8 / +1.2 | +2.0 / -1.3 / -1.1 / 0 / +2.2 / +3.8 / +2.4 / +4.3 |
| Trumpet Solo | 72 | 22.5 | -11.6 / -4.1 / +6.1 / +10.9 (-9.6 / -6.4 / +9.6 / +12.8) | -1.0 / +0.9 / +1.7 / 0 / -3.2 / -0.8 / +1.2 / +2.2 | +0.9 / -1.5 / +0.5 / 0 / -2.0 / +1.5 / +4.7 / +4.2 |

realDb is the player's change less SSO's own (its Long changes with CC1 by up to ~10-19 dB on the winds and basses), so
where SSO's Long already spans more than the player, realDb runs against the marking (the flute's ppp +8.3: SSO's Flute
Long at F5, CC1 16, is 11.9 dB under its 64, the real flute's ppp only 3.6). The ppp -> pp and ff -> fff steps are larger than the
steps between under the Meyer rule (a violin's ppp is 12 dB under its pp, its p .. f 3 dB apart).

Test: `tst_soundlibrary::levelRealRange` (realDb by both rules and by Clark & Luce spans by hand, realRange 2 without
realSteps playing the Meyer rule, each shipped patch's realSteps, CC11 at ppp / mp / fff, a hairpin's CC11
following its CC1, R as volume, a capped short, the layers, the presets without it, the shipped map's patches and
fractions), `tst_liveequivalence::liveRealRange` (the Meyer rule and Clark & Luce spans in Live's clips and both sets), `test_real_range.py`.

### Even levels

SSO's Performance legato plays some slurred notes louder or quieter than others, deterministically and by context
(Whence 12-TET bar 2, Celli: notes 2-3 ~3 dB over 1 and 4, the same within 0.2 dB pass to pass); a table per transition
can't predict it (`sso_transition_levels.json`: r 0.45-0.63). The owner (2026-10-12) wants it evened out, and in a blind
listening test (real SSO, loudness-matched; `even_levels_test.py`, `sso_even_levels_test.json`) chose the **glide**: CC11
linear in dB between the middles of the notes' heard windows, a message every 5 ms (Ramp also liked, Step not chosen,
uncorrected marked wrong). `[levels] evenLevels` (**1 in Recommended and by default, 0 in Library default**: the owner, 2026-10-13, after the VM check;
per score through `playbackSettings`) plays the corrections measured for the score; libmscore/evenlevels.h has
the design.

- **Measured in place, per score**: *Tools › Even Out Levels…* (the selection's parts and bars, else every library part;
  a MuseScore of its own measures a copy, the result one undo step) or `MuseScore3Evo --even-levels <score>
  [--even-levels-parts 4] [--even-levels-bars 1-8] [--even-levels-play] [--even-levels-out <folder>]` (mscore/evenlevelsjob.h).
  Needs the library's plug-in (Kontakt + SSO). Every passage of three or more joined notes on a Performance legato patch is
  rendered as playback renders it, through the hosted plug-ins, three passes; each note's level is the perceived envelope's
  mean over 136.4 ms from its window start (a transition: the map's `legatoDelay` for its interval; an attack: its onset),
  the median of the passes, for the statistics and the logs; the glide's values per note are fitted (least squares, ridge
  0.5) so that the glide, smoothed as the envelope (one pole, 35 ms) and 12 ms late (CC11's measured lag), flattens the
  measured envelope (the median of the passes) to each slur's median of it over the slur's span (a slur: from an attack
  or a bowed transition; from the passage's second note-on to its last), divided by CC11's measured factor 1.28. Rendered
  and measured again with them, fitted once more (scaled by the measured / predicted change of the envelope in round 1:
  the test's first round overshot ~25 %), rendered and measured again; per passage the round whose envelope lies closest
  to its slurs' medians (rms) is kept, none if uncorrected was closest. `result.json` lists every note's levels.
- **The glide's nodes** (the owner, 2026-10-14, "do 1"): each note's in the middle of its own span, halfway from its
  note-on to the next joined note-on (`EvenLevels::nodeTime`; a passage's last note: halfway to its note-off). Until
  then they were the middles of the heard windows, 270-310 ms after the note-on at 180 ms notes, inside the next note:
  the glide ramped up through each slur's first note, which the owner heard swell. With the nodes in their own notes (S,
  `even_180_check.py`, `sso_even_180_check.json`, real SSO on the VM) the owner heard 250 ms slurs fine in 4 of 4 blind
  pairs where the old nodes were wrong in 2 of 2; at 180 ms both swell on the figure's note 3, SSO's own E3 -> Eb3
  transition (`sso_note3_check.json`), which no value per note reaches. Values measured with the old nodes are stale
  (the context's hash changed) and measured again.
- **Under `[slurs] plainPerformance`** (2026-10-17): every plain transition is a fingered one (velocity 64), so the
  "bowed transition starts a slur" rule no longer saw the score's slurs and a passage of several slurs was fitted to one
  median, flattening the level rule's step between them. A plain note now starts a slur of its own where its phrase
  (`ContextLevel::phraseSlur`: the written slur, or the note alone outside one) differs from the joined note before
  (`tst_performancetechnique::evenLevelsPlain`). Even levels plays measured values only: a score without them (*Whence*
  has none: no `evenLevels` metaTag) plays no glide until the job or the background measurer has measured it, which
  needs the plug-in (Kontakt + SSO): a Linux dump never shows glides. Checked on the VM (build 16d3dbd, the new map):
  `--even-levels` on *Whence* measured 16 plain passages (4 slow ones skipped), the envelope off its slurs' medians
  1.03 dB uncorrected, 0.49 after round 2.
- **Slow slurs are skipped** (the owner's blind tests of real SSO: 2026-10-13, slow slurred passages sound the same
  uncorrected and corrected at 500 ms and 1 s; the tempo-line test, 350 and 500 ms, figure and line: no difference,
  neither wrong; the limit lowered from 500 to 350 ms by the owner, 2026-10-14, once the mid-note correction was fine at
  250 ms; HANDOFF.md > Slow slurs need no correction): a passage whose every inter-onset interval between consecutive
  joined notes (seconds at the score's tempo, as rendered) is >= 350 ms (`EvenLevels::SLOW_IOI_S`, `slowPassage`) is
  not measured by the job (its log counts them) nor in the background: no render, no Work, no value, no wait for it,
  and the notes play as SSO plays them. One interval under 350 ms (349 ms counts) measures the whole passage in the
  job, the notes around the edit in the background. Values already on a
  slow passage stay and play (harmless). `tst_soundlibrary::evenAutoSlow`.
- **Kept on the note** (Inspector › Note › Even level, editable, with its state): the glide's value at its node (dB) and
  what it was measured against: `"<patch>|<hash>"` of its rendered context (patch, pitch and tuning,
  velocity, joined or not, length, the joined neighbours' pitches, velocities and timing, the dynamics CC in force; the
  tempo through the times). Each render compares: a changed context is **stale**: not played, shown, until measured
  again. Saved in the metaTag `evenLevels` (left out when no note has one; MuseScore 3.6 keeps it).
- **Played** (`MidiRenderer::libraryEvenLevels`, after the per-note levels and Live timing overrides, before track delays):
  a run of joined notes with current values glides on top of the route's CC11 schedule (the level rule, section balance,
  realRange, track levels: dB add up), a message per 5 ms where the value changes, the CC11 in force again at the next
  note-on. **Headroom**: every note on a patch plays its largest value lower by CC11 (`TrackDelays::evenDb`), so no
  correction is cut at CC11 127; the patch's gain stage adds it back (`patchGain`, uncapped since 2026-10-15), the master
  trim (below) keeping the score from clipping. Until 2026-10-15 the volume added it only as far as it fit under Live's
  +6 dB and the rest lowered every library patch alike (`Delays::evenAll`, now only the old volumes', `legacyPatchGain`;
  2026-10-13: Celli - Performance already at +6 dB by the level rule, the first VM run cut every boost at 127).
- **Live**: the same CC11 events reach Live's clips and the plain set's CC11 lane; the headroom MuseScore's Utility on the
  Kontakt tracks (`tst_liveequivalence::liveEvenLevels`).
- **Measured in the background near edits** (branch `even-auto`, 2026-10-13; libmscore/evenauto.h, mscore/evenautomeasurer.h;
  no setting of its own: on whenever `[levels] evenLevels` plays for the score and the library plays through its
  plug-in). The owner (2026-10-13): a note plays its **measured** correction or playback waits for it, never a predicted
  one (a model of the swell failed: r 0.65 against 0.97 measured in place, `superposition_scan.py`). After an edit (once
  edits pause, 0.6 s) and when a score is shown, MuseScore renders the score once and finds the passage notes without a
  value or with a stale one. Each such note's value is looked up first in the user's cache (`<setups folder>/even
  cache.json`: every value by the contexts of notes j-3 … j+3), so an **undo or redo restores values without rendering**;
  the rest are measured in windows: the note +-3 measured, 2 notes before (the Basses 4: `sso_perturbation_scan.json`) and
  2 after rendered with their values held, from 0.5 s before the first note-on, the whole passage when that reaches both
  ends (`sso_even_fast_checks.json`: such a window matches the whole passage within the noise). The full method (the
  owner's choice): 3 passes, round 1 = the fit / 1.28 (CC11's measured factor), round 2 the same again (always, since
  2026-10-14: S's two rounds; before, only when the window residual stayed above 1 dB), the round whose envelope lies
  closest to its slurs' medians kept; the window's last note's node halfway to the passage's next note-on (as played);
  a note that needs none gets 0 dB
  (measured, current, never measured again). Each pass is a playback (the transport restarted, as the job's passes).
  **Gap** (the owner, 2026-10-13: the 3 s silence after each pass "may shrink to the length the VM shows doesn't change
  results"): 1 s after the last note-off for Violins 1, Violas and Celli - Performance, 1.5 s for Basses - Performance,
  the job's 3 s for every patch not measured (`EvenLevels::gapFor`; `sso_even_gap_check.json`, `even_gap_check.py`: the
  measurer's windows of Whence on the VM, 6 gaps, 2-6 kthost processes each; from 1 s (Basses 1.5) each note's level and
  the fitted glide match 3 s's within SSO's own spread, 0.25 / 0.5 s do not; the release still sounds there, -40 dB
  re the notes ~2.4-3 s after the last note-off, but the next window's notes start 0.5 s + 2 run-in notes later).
  **Early stop**: 2 passes, the 3rd only when they differ on a measured note by more than the patch's threshold (the 95th
  percentile of SSO's |pass 1 - pass 2|: Violins 1 0.67, Violas 0.74, Celli 1.26, Basses 1.49 dB; `earlyStopFor`, 0 = always
  3 for patches not measured): about 70 % of renders stop at 2 (23-25 % of the passes saved); each render's fit against
  the other renders' stays within always-3's 95 % interval (RMS error +0.01-0.02 dB). Blocks of 4096 frames (12-16 %
  faster on the VM, levels the same). Rendering: plug-in instances of MuseScore's own (one per part and patch measured,
  loaded on the main thread when first needed, the same setup and controllers as playback's), each on a worker thread,
  offline; parts in parallel (two kthost processes at once on the VM: each nearly as fast as alone); paused while MuseScore
  plays (an edit made while playing is measured after it stops; playback is never stopped). **Order**: each instance
  renders the windows from the play position on first (earliest first; when Play waits, from where it starts), then those
  before it, nearest first (`EvenAuto::priority`); the rest of the score goes on in the background. Values go on the notes
  **without an undo step** (the score is marked modified until saved). **Play** (the owner, 2026-10-13: "Wait for the
  measurement", also when a score is opened without values): when a note being measured, rendering or still queued (at
  opening every passage is queued), has its note-on in what is about to play (from the transport's start, the pre-roll of
  early notes included, to the loop's end, else the score's), playback waits with a countdown in the status bar ("playing
  in ~n s": the windows still to render there, the largest instance's share, at the speed measured so far, else the VM's
  8.9x audio; "loading the plug-in" first while one it needs loads), then plays; Stop cancels; otherwise it plays at once.
  **Time per edit** (VM, kthost + Kontakt 8 + SSO, samples streamed over the network): a window is ~2 s of notes per pass
  at Whence's sixteenths (+ the gap, nearly free: render time ~0.2 s per second of notes for Violas / Celli, 0.1 Basses,
  0.3 Violins 1, ~0 per second of silence), so ~0.4 s per pass, ~2.3 passes, 2-3 rounds: **~1.5-2.5 s of rendering per
  edited part** (before: ~2.4-3.6 s; the 3 s gap itself cost little, the early stop and the blocks save the time), plus
  the plug-in's load at the first edit. Passages with a note played twice (repeats, ornaments) are left out, as by the
  job: they play uncorrected. *Tools › Even Out Levels…* stays (a whole-score measurement as one undo step; its 3 passes
  and 3 s gap unchanged). Tested on Linux with the test synth (and headless MuseScore: Play waited for the measurement,
  then played); untried in MuseScore with Kontakt and SSO.

Tests: `tst_performancetechnique::evenLevels` (contexts, stale and current, undo, the metaTag round trip, the fit's maths),
`tst_soundlibrary::evenLevelsGlide` (the CC11 glide's shape, headroom, a track level, layers, a stale note, presets),
`tst_soundlibrary::evenAutoWindows` (windows, run-in, cache key and file, the wait decision), `evenAutoSession` (idle,
first measurement, an edit's windows, undo / redo from the cache, results after an edit), `evenAutoTestSynth` (end to end
on the test synth), `tst_liveequivalence::liveEvenLevels`.

### Mic compensation

Every level above (section balance, real range, calibrated levels, the level rule) was measured at each patch's
**default mix**: in SSO the Tree mic alone at its full fader. A part's mics can be changed by their automation lanes
(Mic 1-5: Close, Tree, Ambient, Outrigger, Leader; Mic Mix Distance, which sets Close / Tree / Ambient together; until
2026-10-15 also the Controllers window, whose values old scores now read as flat lanes), and that changes how loud the
patch is. `[levels] micCompensation` (the owner, 2026-10-15: "mic
changes then only affect tone and sense of space, never the balance"; **1 in Recommended and by default, 0 in Library
default**) makes the change up, so the section stays at its balance whatever mics it plays:

- **The level of a mix** = 10 log10 of the sum over the mics of 10^((micLevel + fader(v)) / 10): micLevel each mic
  alone at its full fader against the default mix (map `<Instrument micLevels>`), v its fader 0-127: the part's setting (a flat lane: one value
  from the score's start),
  else the patch's default (map `micDefault`, SSO: Tree 127, the others 0). **With Mic Mix Distance set** the plug-in
  plays its mix whatever mic values go with it (measured: sent in one block, as MuseScore sends a part's values, SSO's
  script overrides every mic, Outrigger and Leader included): Close / Tree / Ambient by its law (map `<Mics mix>`),
  Outrigger and Leader at their default. fader(v) the measured law (map
  `<Mics fader>`: linear in dB between the measured values, in amplitude below the first). The compensation C is minus
  that level; a mix in which no mic sounds stays silent (C 0).
- **Played** where the section level is: the patches' gain stage (`ContextLevel::volumes`, `TrackDelays::patchGain`: the
  hosted slot, MuseScore's Utility on the route set's and the plain set's Kontakt tracks), less the master trim. Fixed
  values (flat lanes, as the Controllers values were: a migrated score plays the same volumes) change nothing
  else, in Live too. **Automated mics** (any other lane): the volume
  carries the part's largest C over its note starts (`Volumes::micTop`), each note its own C at its start less that, by
  CC11 (`trackLevel` in rendermidi.cpp: the per-note levels, so Live's clips and the plain set's CC11 lane carry it). A
  note held through a mic change keeps its start's C (Kontakt changes the mic at once, the note's level stays).
- **Measured** (2026-10-15, Windows test VM, kthost: Kontakt 8.13.1 + SSO, offline; `tools/soundlibraries/mic_levels.py`
  → `sso_mic_levels.json` → `gen_spitfire_sso.py`): SSO's All techniques Long at CC1 64, three pitches (the quartiles of
  the CC1 sweep's), 4 takes each, the sustain's power (rmsDb, the real range's measure; mics add as powers), each
  configuration against the patch's default render, the median over the pitches. Results (33 patches, `sso_mic_levels.json`): the default mix is the Tree alone at 127 in every patch (the Tree off,
  nothing sounds). Each mic alone at full against it: Close -11.47 (Tuba Solo) … +4.14 dB (Flute Solo), median -2.1;
  Ambient -7.36 (Basses - Performance) … +3.29 (Tuba Solo), median -1.27; Outrigger -6.96 (Bassoons a2) … +2.72
  (Trombones a6), median -4.67; Leader (strings) -18.51 … -2.58 (Celli), median -13.04. The fader law, the same for four
  mics on three patches to 0.00 dB: 120 -1.16, 112 -2.54, 96 -5.55, 80 -8.96, 64 -12.88, 48 -17.49, 32 -23.11, 16 -30.27
  dB. Mic Mix Distance: 0 the Ambient alone at full, 64 the Tree, 127 the Close; between, a crossfade fitted to five
  patches' totals (in-sample within 0.65 dB; each patch predicted from the other four within 2.05 dB, Flute Solo at 96).
  The power sum against five mixed renders of explicit faders: -0.06, +0.08, -0.05, +0.94 (Horn Solo: Close, Ambient
  127, Outrigger 64), +1.39 dB (Basses, all five mics): mics of one recording are partly coherent, so a sum of
  several close mics comes out up to ~1.4 dB louder in the model than measured. A take-to-take spread (SSO's two round
  robins) of up to 5 dB (Tuba Solo) is why each value is a median of 4 takes at 3 pitches.
- **Which patches**: the 28 with a section level and the five string sections' Performance patches, measured; since
  2026-10-15 (balance-refine, `mic_levels.py extra`, same method) also the 15 patches swept for the balance (solo
  strings, Motif brass, piccolo, alto / bass flute, contrabass clarinet / trombone / tuba, cimbassi), the harp and the
  timpani (decaying notes by their energy 0 .. 6 s after the note-on; the timpani's Close mic rendered empty offline,
  about -92 dB rms in every take: no level, so not compensated), and Strings Ensemble (Iteration 9: Close -4.07, Tree 0,
  Ambient -5.28, Outrigger -5.81 dB; at MIDI 42 / 60 / 78); every patch with a section level is covered. The
  percussion kits have no mic Controllers, so their mics never change. Any other `with` patch takes its main patch's
  values (`soundlibrary.cpp`: an assumption, the same recording's mics); other patches none (no compensation).
- **Not covered / untested**: a mic change by staff text (mic Controllers have none). With Mic Mix Distance set, a mic
  value changed alone later (a mic lane's point) reaches Kontakt in a block of its
  own and may play, where the model keeps Mic Mix Distance's mix until playback starts again (only values sent together
  were measured).

Tests: `tst_soundlibrary::levelMicCompensation` (the map's attributes, the laws, the power sum, Mic Mix Distance and a
set mic, silence, a migrated Controllers value (a flat lane) on the gain stage, a lane by CC11, layers, presets),
`tst_liveequivalence::liveMicCompensation` (a fixed Close and a Tree lane: MuseScore's gain = the route set's and the
plain set's Kontakt gain, Live's clips' CC11 = MuseScore's, the plain set's CC11 lane).

## Gain stage and master trim

The owner, 2026-10-15 ("sure", then "yeah sure let's go with utility"): stop solving "one quiet patch needs more than
+6 dB" by lowering every other patch by S' = max(0, loudest - 6). The level rule's gain, the section balance, the real
range's tops, the mic compensation and the even levels are each patch's own **gain stage** after its Kontakt; one
**master trim** per score keeps the loudest moment from clipping. Every level number is the one before; only where the
gain is applied moved (`libmscore/contextlevel.h` › Gain stage and master trim).

- **The gain stage** (`TrackDelays::gainStageDb`): the patch's volume from the levels (`ContextLevel::Volumes::patchDb`:
  G or T, O, R, C; no longer less S') plus the even levels' headroom (`evenDb`), less the score's trim; uncapped. What
  stays on the fader with the Mixer's: the track levels' headroom (`faderGain`, 0 … +6 dB, as before). `patchGain` =
  both.
  - **MuseScore hosted**: the plug-in slot's output gain (`Vst3Synth::setMix`, a float factor, no top).
  - **Live** (plain set and route set): Live's stock **Utility** right after the Kontakt on each Kontakt track that needs
    a gain, named **MuseScore gain** (`LiveSetWriter::UTILITY_NAME`, `utilityDevice`; its Info text says what it holds).
    Its Gain is a linear factor 0 (-inf) … 56.2341309 (+35 dB): the range Live 12.4 saves (a factory set saved by Live
    12.4d1: `<Gain>` Manual 1 = 0 dB, MidiControllerRange 0 … 56.2341309; `<LegacyGain>` -35 … 35 dB, LegacyMode false).
    Above +35 dB the rest goes on the fader (`kontaktGains`), within its +6 dB. The fader stays at its old place (the
    part's Mixer and the track levels' headroom), the owner's to use. The set stays Live 12.2's format.
  - **MIDI out** (no host, no Utility): the gain goes on CC7 with the Mixer's (`TrackDelays::midiVolumes`, on the host's
    law: gain = (CC7 / 100)², so CC7 127 = +4.15 dB); where a route would go over, every route lower together by the
    largest excess (the old way, CC7 in the gain stage's place). Until 2026-10-15 MIDI out sent the Mixer's CC7 alone
    and none of the level volumes. How a receiver turns CC7 into gain is its own (LIVE.md › What still differs).
- **The master trim** (`ContextLevel::masterTrim`; cached per score until an edit, undo, settings, routes, library or
  calibration change; one render of the score with the untrimmed gains):

  ```
  peak(t) = 10 log10 Σ_n 10^((g_n + e_n(t) + L_n(t) + crest) / 10)          trim = max(0, max_t peak(t))
  No margin under 0 dBFS beyond the crest (the owner, 2026-10-16: "keep it as it is, we have other ways to make extra room anyways").
  ```

  over the library notes n sounding at t (note-on to note-off as rendered; release tails left out): g_n its patch's
  untrimmed gain (`patchGain`, dB; the Mixer and Live's faders are the owner's, not counted), e_n its route's CC11 in
  force (20 log10 (CC11 / 127)), L_n its articulation's loudest 50 ms (the dynamics calibration's curve, dB as
  `dynamics.json` has it: at the note's velocity, its route's dynamics CC in force, or the larger of both, as the curve is
  driven; at its pitch where measured; a note without its own curve: its patch's loudest curve level, else the
  calibration's loudest), crest the map's `<Dynamics crest>`. Powers add (independent sources); the amplitudes added
  (`peakAmplitudeDb`, the in-phase bound) are reported beside it. The trim never boosts. Without a calibration or a crest
  the trim is the old S' (`fallbackDb`: the loudest gain at +6 dB), so nothing plays louder than before. None of the
  level settings on (Library default): no gain stage, no trim.
- **The crest** (`tools/soundlibraries/crest_sweep.py` → `sso_crest.json` → `gen_spitfire_sso.py` → `<Dynamics
  crest="14.515">`): a note's sample peak over its loudest 50 ms, measured 2026-10-12 on the Windows test VM (real
  Kontakt 8 + SSO, kthost, offline, 44.1 kHz; branch utility-headroom's measurement): the five string sections' All
  techniques Long and played velocity techniques and their Performance Legato, velocity = CC1 = 32 / 64 / 96 / 127, two
  repeats, 392 notes: median 8.97 dB, largest 14.515 dB (Basses Pizzicato Bartok), the one used. Against the
  calibration's level instead of the note's own (round robins differ), the same notes' peaks lie up to 20.2 dB over it
  (median 9.1; the largest a pp Violins 1 pizzicato): the trim can be ~6 dB short on a single soft note's odd take, never
  near the loud moments that set it. Woodwinds, brass and percussion not measured (an assumption: the strings' crest).
- **Read back** (livetracks.h): MuseScore's Utility is known by its name; its Gain changed in Live is reported, not
  imported (Update Live Set writes MuseScore's again). A Kontakt track without it (a set written before 2026-10-15) is
  read with its old fader (`TrackDelays::legacyPatchGain`, the volume as it was then), so only the owner's change of it
  counts. **Update Live Set** (livesetupdate.h): the Utility's Gain and Info text replaced where they differ; a Kontakt
  track that needs one and has none gets it inserted right after the Kontakt (new pointee ids, its list Id after the
  others'), and a fader still at its old volume moves to the Mixer's (else it is the owner's: kept, reported).

Tests: `tst_soundlibrary::gainStage` (one patch at +29.6 dB, every other at 0 dB, only the trim moves them; the trim from
the calibration and the crest, never a boost; the fallback; Library default; MIDI out's CC7), the level tests' volumes
(`levelContext`, `levelSectionBalance`, `levelRealRange`, `levelMicCompensation`, `evenLevels`: their old values less
`Volumes::shift`), `tst_liveequivalence::liveGainStage` (fader × Utility = MuseScore's slot gain; read back; Update Live
Set unchanged, changed and from a set written before the Utility) and the live tests' route-set checks.

## Transitions early

The owner's rule since 2026-10-07 sends a legato transition on its beat; SSO's Performance legato then reaches the new
pitch 100-200 ms later (HANDOFF.md › The audit's transitions). On 2026-10-14 he asked to hear them started early:
`[slurs] transitionsEarly` (since 2026-10-15 1 by default and in Recommended, 0 in Library default: the owner, 2026-10-15, after hearing Whence's fast quarter-tone slurs through SSO: "I think 1 and 3 are both fine (I can hear very little difference btwn them)"; 1 measured closest to the written pitches there: Violas bar 7 93 cents mean error against 123 for 3 and 127 on the beat, Violins 1 bars 5-7 62 / 73 / 97, kthost + SSO on the test VM, pYIN):

| Value | Sent early by | On the beat |
|---|---|---|
| 0 | nothing | the note-on (the rule since 2026-10-07) |
| 1 | the modelled arrival: `bendGlide`'s arrive by the note before's length, else `legatoDelay` by interval, quicker after a short note (`libFastDelay`) | the new pitch's arrival (f0 nine tenths of the way: `sso_legato_glide.json`) |
| 2 | `bendGlide`'s leave + 23.5 ms | the change as heard: a legato note on a bowed string is perceived 23.5 ms (SD 22.8) after its pitch change starts (Polfreman 2013, "Comparing onset detection and perceptual attack time", ISMIR 2013, a modelled bowed string) |
| 3 | the middle of `bendGlide`'s leave and arrive | the glide's middle |

2 and 3 need a measured glide (`bendGlide`: SSO's five string sections' Performance legato); elsewhere they act as 1 and
the audit names the patches. Every mode keeps the note before on its patch at least `[legato] keepMs` (40) as played,
and never less than 15 ms (`MidiRenderer::TRANSITION_MIN_GAP_MS`: SSO's legato engine doesn't play a note-on closer
to the legato note-on before, at velocity 64 or lower: the audit's DROPPED); a capped transition starts that late. The
note before still ends `overlap` ms after the moved note-on (`libJoins`), so it stays a transition; the switch and the
chord's controllers move with it (`libShifts`); its bend's glide is timed from the note-on as played. Not across a
repeat's start (and, since chunk-equal 2026-10-25, across a 10-measure chunk as in the whole render); Play from a
tick pre-rolls to the moved note-on (`PlayStart`). Live: the plain set's clips hold the written notes and LiveTiming
puts the moved note-on back (Track Delay + MuseScore Link's per-note delays; `tst_liveequivalence::plainWritten`);
*Live plays the score*'s clips are rendered as played, so they carry it. The audit (`tst_playbackaudit`, `MS_AUDIT_SETTINGS=slurs/transitionsEarly=<n>`)
judges each transition against its mode's shift for CAPPED and prints the shifts (median, range, capped); ARRIVAL still
compares the modelled arrival, so in modes 2 / 3 a transition shows `arrive - shift` late by design.

## Plain Performance

The owner, 2026-10-17, by ear on Whence's Violas bars 8-13 ("C"): plain SSO Performance (abl.py's PLAINb, branch `qt-bends`,
`tools/soundlibraries/qt_bends/abl.py`) sounded better than Recommended, which plays the staccatos on All techniques and
starts each note early by its own onset or modelled arrival (they drift apart). `[slurs] plainPerformance` (1 by default
and in Recommended, 0 in Library default; the owner, 2026-10-17, after hearing the five string sections of Whence):

- **Which notes**: every note of a part whose instrument has a Performance legato with measured `attacks` (SSO's five
  string sections; `SoundLib::plainPerformanceArticulation`: a legato articulation under modifier "performance" alone,
  so not the Sul G / Sul C patches, the solo strings' or the winds') plays on it (`Want::plainPerformance`,
  `choosePlain`, `Choice::plain`), staccatos and unslurred notes too. A note whose technique the Performance legato
  doesn't play keeps its choice: pizzicato, col legno, tremolo, trills, falls, rips (a base outside long / legato /
  marcato / tenuto / the shorts) and any modifier (harmonics, mutes, sul pont. / tasto, flautando, espressivo …). Staff
  text "non performance" opts the part out until "performance", as for `[slurs] performance`.
- **Each note** (`performanceNote` in rendermidi.cpp): velocity 64 (`MidiRenderer::PLAIN_VELOCITY`; a note's own velocity
  or an attack / transition chosen in the Inspector still its own); at its written start; a staccato (or staccatissimo)
  as MuseScore 4's note model plays it (today's note-off) and a spiccato attack: the note before it on the patch, and the
  staccato itself before the next note, end at least 45 ms before the next note-on (`PLAIN_STACCATO_GAP_MS`: SSO joins a
  note-on less than 40 ms after the note-off before as a transition, the map's `legatoGap` 39 ms measured on Violins 1 -
  Performance; 45 is abl.py's margin); a slur's transitions as before (the note before overlapping by the map's
  `overlap`); every other note its written length (100 %), so a note right after another of the patch touches it and SSO
  plays a transition (its bend glides as a transition's: `LibTrace::from`). The score's CC1 (dynamics) unchanged.
- **Plain Auto**, `[slurs] plainAuto` (branch `celli-bowed`, 2026-10-26; 1 by default and in Recommended, 0 in Library
  default; first `[slurs] plainBowed` the same day, never in a Windows build: its key is ignored). In one sentence:
  *plain Performance: smooth after a rest, bowed on each new bow, fingered inside a slur, spiccato for staccatos.* For a
  plain note on Auto (no velocity of its own, no attack or transition chosen in the Inspector):
  - **after silence on its patch** (no note of the patch touching it, not the same key struck again), slurred or not: the
    **smooth** attack (5) instead of 64. The owner: "slurs should have smooth start when there's nothing preceding it", then
    widened to every note after a rest. The smooth attack is heard later after its note-on (the map's `attackOnset`,
    `sso_smooth_onset.json`) than the patch's `plainShift`, which already moves the whole patch early, so the note starts
    early by the difference (times `[heldNotes] early` %): Violins 1 34 ms (100.2 - 65.8), Violins 2 25 (91.1 - 66), Violas
    125 (236.2 - 111), Celli 92 (172.2 - 80.2), Basses 44 (103.1 - 59.1); capped as any early attack (the note before on its
    patch keeps `[legato] keepMs`; at the score's start the start lead), as microtoneBends 3's mid-glide start is. Live's
    clips hold it as written and the plain set plays it early (written clips' timing, `tst_liveequivalence::plainAutoLive`);
    the audit takes its arrival from the smooth onset. With `[slurs] plainTiming` it starts early by its own onset instead;
  - **each new bow stroke**, a note touching the note before on its patch that is not inside the same slur (a slur's first
    note right after the slur before or after an unslurred note, an unslurred note touching the one before: détaché, the
    note after a slur): the **bowed** transition (93; Violas' `legatoVelocity` 100) instead of 64, SSO's fingered one (64 is
    in its fingered band 20-84). The owner, Whence's Celli bars 1-2, the Velocity lane's "64 · fingered" on a slur's first
    note: "it's supposed to be bowed". Its bend glides as the bowed transition's (`bendGlide`); `[slurs] microtoneBends` 3
    doesn't move it early (that is for fingered ones); even levels take it as a slur start (`newSlur`); a slur after a slur
    overlaps the note before by the map's `overlap` as `[slurs] transitions` plays it, a détaché note touches it. It goes
    on the note before's copy also at another tuning (`SoundLib::lanes`' `plainJoin`, as `[slurs] plainJoins` 1 does;
    branch `bowed-join`, 2026-10-28): before, a touching note outside a slur at another quarter-tone tuning went on its
    tuning's copy and was sent bowed there with no note to go on from, a fresh attack at 93 (the owner, Whence's Violins
    1 bar 16, F#5 into the quarter-sharp G5: "the transition between the last note and the penultimate note is wrong";
    Whence, Recommended: that note, the Basses' bars 10 / 14 and the Violas' bars 12 / 16 join, no other note changes);
  - **inside a slur**: 64, SSO's fingered transition, as before;
  - unchanged: a staccato (and a note after a staccato) a spiccato attack at 64, an accent as `[slurs] plainAccents` plays
    it, the same key struck again right after itself 64 (an attack).
  0: every such note at 64, as from 2026-10-17. Whence (quarter tones), Recommended, 1 against 0 (dumpEvents chunked): 102 of
  454 library notes change, nothing else in the trace: 10 smooth (Violins 1 2, Violins 2 1, Violas 2, Celli 3, Basses 2),
  92 bowed (Celli 45, Violas 23 at 100, Violins 1 14, Basses 10; 63 of them a slur after a slur, the rest détaché or after a
  slur). The bowed ones no longer take microtoneBends 3's fingered early start (Celli 61, Violins 1 69, Basses 19 ms later
  than as fingered transitions; the Violas' was 0): the audit, whose plain model gives every plain note the patch's
  plainShift, then finds 96 ARRIVAL fails instead of 12 (neighbours differing by that amount); not checked against the
  sound. Not heard.
  - **2** (branch `bowed-early`, 2026-10-27; for the owner to compare with 1 by ear, 1 staying the default and
    Recommended's): as 1, and each new bow stroke on the bowed transition (a slur after a slur, détaché, the note after a
    slur: `PerformanceNote::plainBowed`) is sent early by its patch's **`<Instrument bowedEarly>`** beyond the plainShift
    the whole patch already goes early: the bowed transition's measured heard time (f0 leave + 23.5 ms, Polfreman 2013)
    after its note-on less plainShift, median without the octave rows (YIN's octave confusion): Violins 1 125, Violins 2
    151, Violas 55, Celli 106, Basses 64 ms (IQR 60-120 ms; kthost build 485 + Kontakt 8 + SSO on the VM, 192 transitions
    a section, CC1 64, 150 / 600 ms notes before: `tools/soundlibraries/sso_legato_glide_bowed.json` `proposalNoOctavesMs`,
    `legato_glide/bowed.py` -> `gen_spitfire_sso.py` BOWED_EARLY). By microtoneBends 3's `midEarly` mechanism: the switch,
    controllers, join and bend move with the note-on, capped so that the note before on its patch keeps `[legato] keepMs`
    (at least 15 ms), the start lead at the score's start; Live's written clips hold it as written and the plain set plays
    it early (`tst_liveequivalence::plainAutoLive`). The audit now models a bowed new bow under 1 and 2 as heard
    plainShift + bowedEarly after its note-on (`LibTrace::plainBowed`; before, plainShift alone).
    Whence (quarter tones), Recommended, 2 against 1 (dumpEvents chunked, and as Live's clips: the same): all 92
    bowed notes move, none capped, by the map's amount to the tick (Violins 1 14 at 125.0 ms, Violas 23 at 54.5, Celli 45
    at 105.7, Basses 10 at 64.8, Violins 2 none: it has no new bow under plainAuto), nothing else. Audit ARRIVAL with the
    bowed model: 1 148 fails (Violas 45, Celli 64, Violins 1 36, Basses 3), 2 96 (Celli 60, Violins 1 36): what is left are
    microtoneBends 3's midEarly fingered transitions next to them, which the audit's plain model still takes as heard at
    plainShift alone. Not heard.
- **Joins across tunings**, `[slurs] plainJoins` (branch `slur-joins`, 2026-10-22; **0 everywhere**: the owner heard both
  the same day, Whence's Violas bars 11-13 / 15-17 alone with a click: "violas A better for both", A being the attack on
  the note's own copy; the half note is unslurred, so a new bow stroke, which the attack plays and a transition doesn't;
  first made a bug fix on by default, then this setting): with 1 such a touching note stays on the note
  before's copy of the patch (`SoundLib::lanes`, `Item::attacks`, the lane's `lastPlain` / `lastAttacks`), as a slurred
  note does, so its transition and bend glide happen; before, a note at another tuning went on the copy already at its
  tuning and was attacked after silence there while the note before released on its own copy. Not for attacks (a
  staccato or the note after one, an accented re-attack under `[slurs] plainAccents` 2 / 3, a join of its own wider than
  `legatoGap`) or the same key struck again: their copies by tuning as before. The owner, 2026-10-22, Whence's Violas
  (build #464): "bar 12 and 16 slur first note sounds disjointed from the note before": there a slur's last F#4 into
  the quarter-sharp G4 half note. Whence, Recommended: 7 such split joins (Violas bars 12 / 16, Violins 1 bar 16,
  Basses bars 10, 11, 14, 15) -> 0; every note's time, key and velocity unchanged. Tests
  `tst_soundlibrary::plainJoinLanes`, `tst_liveequivalence::plainJoinsLive` (the plain set's copy tracks and the route
  set's clips follow).
- **Timing**: no early start of its own (no onset, `heldNotes/early`, `transitionsEarly`, smooth attack's onset); instead
  every event of the patch (note-ons, note-offs, CCs, bends) goes one constant early: the map's `<Instrument plainShift>`
  (ms), the median heard - written latency of the plain notes measured in context
  (`tools/soundlibraries/sso_plain_performance_shift.json` → `gen_spitfire_sso.py`). Measured over all timed notes on
  Performance at written time ("Heard passages.musicxml", 12-TET, PLAINb, velocity 64, score CC1, 2 passes, Kontakt 8 +
  SSO in kthost on the test VM, 2026-10-17; the owner chose these all-notes medians over the attacks-only ones by ear):
  Violins 1 65.8 ms (n 111), Violins 2 66.0 (116), Violas 111.0 (104), Celli 80.2 (88), Basses 59.1 (109). A Performance
  patch not listed: 0 (as written), and the playback audit lists it (UNMEASURED, and the report's "plainPerformance"
  line); none of the five is unmeasured now. Played as a track delay of the patch (`TrackDelays::Delays::plain`, `plainMs`; never in the
  `trackDelays` metaTag): everything else plays that much later (`libraryDelayLead`), so the patch is early from its
  first note; Play from a tick pre-rolls (`PlayStart`). The start lead (`libStartLead`) still serves the other parts'
  early notes.
- **Levels**: unchanged; the level settings (calibrated, context, realRange, sectionBalance, evenLevels, the gain stage)
  act on these notes as on any note of the Performance legato.
- **Live**: the plain set's clips hold the written notes; the Performance patch's Kontakt track carries the shift as its
  Track Delay (-plainShift, with the patch's own delay), recorded as that track's lookahead so that reading the set back
  takes it off (`PlainLiveSet`, `LiveTracks`); the MuseScore Link device delays what the joins move (`tst_liveequivalence::
  plainPerformanceLive`: every note-on / note-off and the patch's CC1 as MuseScore plays them). *Live plays the score*'s
  clips and the route set have no Track Delay (LIVE.md › What still differs).
- **Audit**: a plain note's predicted arrival is sent + plainShift - written (`Report::plainShifts`).

## Plain timing

`[slurs] plainTiming` (branch plain-timing, 2026-10-21; 0 by default and in both presets: rejected by ear, the owner, 2026-10-27, on the A/B page (Whence Violins 1 bars 5-8, build 485): "I feel like A's timing is better in general ... I would prefer A for all the note timings" (the last note, a re-attack, sounded late in B though measured on the beat at -15 dB: his ear places these attacks later); per
score). Why: the heard check of `share/verifyplayback/Mixed techniques.musicxml` (Kontakt 8 + SSO on the Windows VM, build 457,
Recommended, MuseScore's own export; HANDOFF.md › Mixed techniques) found that plainPerformance's one `plainShift` per patch
(the all-notes median) can't place both kinds of Performance note: the attacks came 21-74 ms early, the transitions 88-191 ms
late (SSO's transitions take 170-440 ms by interval and note length), so on each beat the sections spread over 160-230 ms and
a pizzicato or tremolo on All techniques (on time by its own onset) sat between them. With plainTiming 1 and
plainPerformance 1:

- **No plainShift track delay** (`TrackDelays::Delays::plain` stays empty): the Performance patch's events at their own times.
- **An attack** (a plain note that is no transition: after silence on its patch, a staccato's spiccato attack, the note after a
  staccato, an accented re-attack under plainAccents) starts early by the patch's measured plain attack, map `<Instrument
  plainAttack>` (ms, times `heldNotes/early` %): the median heard - sent of plain attacks at velocity 64 in context
  (`sso_plain_performance_shift.json` › `attackMs`; Mixed techniques' 39 attacks per section, build 457: Violins 1 19.5,
  Violins 2 29.2, Violas 37.1, Celli 38.7, Basses 37.5 ms). An attack chosen in the Inspector with its own measured onset
  (smooth) by that one. The accented attack's own onset is unmeasured: it takes plainAttack (an assumption).
- **A transition** (slurred, or a plain note touching the one before) starts early by its modelled arrival, as
  `[slurs] transitionsEarly` sends a Performance transition: its mode, 1 (the arrival on the beat) where transitionsEarly is 0;
  capped so that the note before keeps `[legato] keepMs` as played (and SSO's 15 ms); the joins, switch, controllers and bend
  follow the moved note-on (`libJoins`, `libShifts`).
- **A staccato** keeps MuseScore 4's note-off (no extra length for the onset its All techniques technique would have had): its
  note-on to note-off is MS4's length plus its early start.
- **Basses' con sord. / espressivo** (a mapping fix, part of this setting because it changes the sound): SSO's Basses have no
  con sord. or espressivo (Rachm.) sample, so their choice is the plain Long; without plainTiming `choosePlain` refused the
  note for the modifier it asked for, so it left the Performance patch for All techniques' Long (re-attacked, its own onset).
  With plainTiming only the chosen articulation's own modifiers keep a note off the plain Performance legato (`Want::plainTiming`,
  `choosePlain`): a modifier the section has a sample for (harmonics, sul pont., the violins' con sord. …) keeps its technique.
- **Onset rule** (for `plainAttack` and the audit's predictions): the time the note's perceived envelope reaches 15 dB under its
  peak (less the analysis's 30 ms), the rule the map's other onsets use, so attacks on Performance and All techniques are timed
  alike. Sources: Vos & Rasch 1981 (*Perception & Psychophysics* 29(4) 323-335: a relative threshold, about -15 dB under the
  peak at normal listening levels, -7 dB only about 20 dB above threshold, about 1 dB per 7 dB of level); Gordon 1987 (*JASA*
  82(1) 88-105: the best model of perceptual attack time is set by the rise's slope, which on cosine rises of 100-300 ms comes
  out at about -18 to -22 dB); Villing 2010 (PhD thesis, Maynooth: 20-160 ms attacks perceived only 18 ms apart, which every
  threshold over-predicts, -6 dB most). -6 dB, which matched the owner's ear on Whence's smooth first notes (VERIFY.md › Heard
  check), has no support at normal levels, so it doesn't set the attack targets (CLAUDE.md: real data, no listening test). The
  evidence is synthetic tones with rises up to ~160 ms; nothing covers real section strings with 200-300 ms rises, and listeners
  scatter 20-60 ms on slow attacks.
- **Audit**: a plain attack arrives sent + plainAttack, a transition sent + its modelled arrival (as under transitionsEarly);
  CAPPED for both; a patch without plainAttack is UNMEASURED (its attacks as written).
- **Live**: the plain set's clips hold the written notes; the moved note-ons come back as for transitionsEarly (the technique
  track's Track Delay its lookahead, the MuseScore Link device's per-note delays); the Kontakt track has no plainShift Track
  Delay (`tst_liveequivalence::plainTimingLive`). *Live plays the score*'s clips are rendered as played.
- Tests: `tst_soundlibrary::plainTiming`, `tst_liveequivalence::plainTimingLive`.
- **Measured** (heard check, Kontakt 8 + SSO on the Windows VM, build 458, MuseScore's export, Recommended with plainTiming
  0 / 1, one take each; heard - written medians, -15 dB rule): *Mixed techniques*: Performance attacks -74 .. -21 ms -> -7 .. +3;
  Performance transitions +86 .. +203 -> Violins 1 +1, Violins 2 -17, Violas +4, Celli +42, Basses -42 (10-90 % about +-100 ms);
  All techniques' quick techniques -16 .. +2 (unchanged); a quick technique against another section's Performance attack on the
  same beat +32 -> -10 ms, against its transition -150 -> +9; notes within 30 ms of their beat's median 58 -> 67 % (without
  sul tasto / flautando / harmonics). *Whence*: attacks -62 .. -9 -> -4 .. +27 (Violins 2's +27 from masked readings; its clean
  ones 14-20 ms after the note-on), transitions -3 .. +182 -> -52 .. +18 (Basses +182 -> -24, Celli +134 -> -52); ARRIVAL
  35 fail / 26 warn -> 36 / 19, RHYTHM slurs 1 / 0 -> 3 / 1 (the Violas' 273 / 136 / 136 slurs, bars 9 and 13: the second note's
  transition after a held first note arrives ~100 ms sooner than modelled; masked onsets), runs 9 / 6 -> 7 / 3. Open: the
  transition model is off by 40-50 ms on Celli and Basses, with opposite signs in the two scores (no constant fixes it).

## Speed from written

The owner approved, 2026-10-17: MuseScore takes over the CC119 of SSO's string-section Performance patches. Their script
("Leg. Speed CC", script value `$vfm12`, 119 in every one of them) writes CC119 = 126 - floor(Δt × 127 / 1000) (0-126; Δt
= ms since the voice's previous note-on) at each note-on it plays legato, and the transition groups pick their speed
variant by it (RUNS 114-127 ⇔ Δt ≤ 102 ms, RUNS RT 95-113, FAST LEGATO 0-94 on 1-3 semitone steps; whatever the notes'
velocity picks). A note sent early (`transitionsEarly`, an onset) changes Δt and can move a transition into another band.
`[slurs] speedFromWritten` (0 by default and in both presets; per score):

- **The state**: a patch with the map's `speedCC` / `speedScript` (`<Instrument speedCC="119" speedScript="$vfm12">`,
  `tools/soundlibraries/sso_performance_layers.json`, the five sections) loads with `speedScript` = 0, so the script writes
  no CC119 (`SoundLib::playbackScriptValues`, `SoundLibraryHost::setupState` / `setupStateWith`, made once: prepared). The
  setup file keeps the library's value (a first load's resave puts it back); an instance whose score's value differs is
  loaded again (`Slot::values`). Create Live Set embeds the same state.
- **The value**: each legato note-on (a transition from the note before on the patch: slurred, joined, or a plain note
  touching it; `performanceNote`'s `from`) gets CC119 = the same formula with Δ = the WRITTEN time from the note it follows
  (the first of that note's tie chain) to it (`SoundLib::speedValue`, `MidiRenderer::libSpeed`), right before its note-on
  as played (`librarySpeeds`; it moves with the note: early starts, track delays, timing overrides). An attack gets none,
  as the script writes none at a note-on it doesn't play legato (the value before stays). One value a transition, at the
  transition: the sounding interval sample has CC119 modulators, so a value sent earlier would reshape it.
- **Live**: a carrier on key 113 (*Live plays the score*, the device's protocol 11); in the plain set the technique track's
  clip envelope (LIVE.md › What still differs). Live sends a clip envelope's value only where it changes, so each technique
  track's first value is a step (from 0, or from 1 when that value is 0): an envelope holding 0 throughout, as the chord
  voices' copies of Whence's Celli (every transition at speed 0), sent nothing, and the slots played their transitions at
  another speed (+5 .. +8 dB for ~0.4 s at each chord change in real Live; branch `live-celli`, OPEN_ITEMS 11d;
  `tst_liveequivalence::speedSentLive`).
- **Null test** (plainPerformance 1, where written = played): see HANDOFF.md › Speed from written.

## Fixed transitions

2026-10-20 (branch `sso-legato-fixes`; measured, not yet heard by the owner): SSO's five string-section Performance patches
carry 100-130 external modulators whose source is Kontakt's random generator (source type 10, read from the patches' group
data): 90 on the PORTAMENTO FAST / RUNS / RUNS RT groups' flex envelope level 1 (intensity 1), 10-40 on sustain groups'
pitch (0.02-0.04). Each voice draws a new value, so two plays of the same passage differ (two kthost renders of Whence:
-7..-9 dB as a null on the upper strings and Celli, -21 dB on Basses). The script has no setting for them: its "random
detune" switch acts on `LFO_RAND` modulators, which these patches don't have, and is read back from the groups at load.
`[slurs] fixedTransitions` (0 by default and in both presets; per score):

- **The state**: a patch with a speed script (`<Instrument speedScript>`: the five sections' Performance patches) loads
  with those modulators' intensity at 0 (`SoundLib::playbackScriptValues`' pseudo value `@randomSources`,
  `SoundLibraryHost::setupStateWith`, `KontaktSetup::withRandomSources`; made once: prepared). The setup file keeps the
  library's intensities (the first load's resave puts them back); an instance whose score's value differs is loaded again
  (`Slot::values`). Create Live Set embeds the same state, so the plain set plays it too (LIVE.md › Live against
  MuseScore: Update Live Set and *Live plays the score* don't).
- **Measured** (Windows test VM, kthost, Kontakt 8 + SSO; Whence lane 0 as plainPerformance plays it; private RESULT of
  sso-legato-fixes): two renders null at -73.5 dB (Violas), -70.4 (Violins 1), -96.6 (Celli); Basses -85.8 with the 90
  envelope modulators alone. Each note's perceived level against the mean of two shipped takes: median +0.00 / -0.06 /
  +0.14 dB, SD 0.30 / 0.22 / 0.50 (the shipped takes differ from each other by SD 0.32 / 0.25 / 0.52); third-octave spectra
  within 0.07-0.30 dB on average; the swell inside slurs unchanged (SD 1.07 against 1.11 dB, Violas). So it plays an
  average take, the same each time. Not deterministic: bowed transitions (velocity 85+) at 120-200 ms note spacing (the
  Across Strings sustain groups vary from take to take with no random modulator; not a modulator, perhaps Kontakt's group
  start options).

## Fast transitions

2026-10-20 (branch `sso-legato-fixes`; measured; the owner, 2026-10-28, A/B on fast-legato.musicxml as Violins 1, build 485: "B is maybe ever so slightly better? the last 3 notes sound slightly more in time"): SSO's legato ignores a note-on that comes less
than 15 ms after the note-on before at velocity 64 or less (its chord guard, read from the patch's script; the audit's
DROPPED, a fail). Measured on the Windows test VM (kthost, Violas / Violins 1 - Performance, a whole step after a held note,
deterministic copies): at 64 the step 10 or 14 ms after is silent, 16 ms after it plays; at 65, 70 and 84 10 ms after it
plays as the 64 sent 20 ms after (third-octave spectrum within 0.3-0.5 dB on Violas, 0.6-1.0 dB on Violins 1, the same
arrival 210 / 160 ms: FING + PORTAMENTO FAST), at 85 another family (DETACHED: 3.7-4.7 dB, arrival 60-90 ms).
`[slurs] fastTransitions` (1 by default and in Recommended, 0 in Library default, since 2026-10-28; per score): such a note-on (a route whose patch has a legato
articulation, a note of another chord than the note-on before on the route, velocity 20-64) is sent at 65
(`MidiRenderer::libraryFastTransitions`, after the track delays; `FAST_TRANSITION_VELOCITY`). Velocities 1-19 (portamento)
have no velocity of their family above 64 and stay; the audit still lists them. MuseScore's own early starts never make
one (`TRANSITION_MIN_GAP_MS` caps them); written notes closer than 15 ms do (64th notes at 300 bpm, grace notes).
Live: the velocity is in the clips; the plain set's written clips decide by the written spacing.
`tst_playbackaudit::droppedFindings`: fast-legato.musicxml's three DROPPED gone, those notes at 65.

## Transport reset

2026-10-21 (branch `sso-script-fixes2`, from reading SSO's patch scripts; measured, not yet heard by the owner): SSO's
round robins count per key, +1 at each note-on, and go back to the first take after 1.5 s without a note-on on the
Performance patches, and at a transport start where the script's "Reset on transport" is on (read from the patch's
script). It is on in nearly all the library's patches, which is what `restartTransport()` relies on, but saved **off** in
the five string sections' Performance patches and Tuba Solo - Performance (and Oboe Principal - Total Performance, not a
map instrument). So a play, export or verification pass that starts less than 1.5 s after the last note-on of the one
before picks other takes. `[notes] transportReset` (1 in Recommended and by default, 0 in Library default since 2026-10-28 (the owner: "I don't see a reason not to, since it literally changes nothing within one play"); per score):

- **The state**: a patch with `<Instrument transportResetScript>` (`$xqdzx` on those six) loads with it at 1
  (`SoundLib::playbackScriptValues`, `SoundLibraryHost::setupStateWith`; made once: prepared). The setup file keeps the
  library's 0 (`resave` puts back every key `SoundLib::playbackScriptKeys` lists: this one and speedFromWritten's); an
  instance whose score's value differs is loaded again. Create Live Set embeds the same state (LIVE.md: Update Live Set
  and *Live plays the score* don't).
- **Measured** (Windows test VM, kthost, Kontakt 8 + SSO; Violas / Celli - Performance; a 9 s phrase: an attack and a
  slur, ten same-key spiccato attacks, another slur; three passes in one process, each after a transport restart, 0.5 s
  apart; nulls of pass 2 against pass 1, dB; private `sso-script-fixes2/rr_results.json`):

  | | Violas | Celli |
  |---|---|---|
  | as shipped | -3.1 | -5.1 |
  | `fixedTransitions` alone | -3.2 | -6.0 |
  | `transportReset` alone | -16.9 | -13.0 |
  | both | -37.2 (from 3 s: -95.4) | -23.5 (from 3 s: -99.3) |
  | both, pass 3 against pass 2 | -94.8 | -100.2 |
  | `fixedTransitions`, passes 2.5 s apart (the library's own 1.5 s reset) | -95.3 | -63.5 |

  With both settings every pass after the first repeats at the floor; pass 1 differs only in its first 3 s, where the
  passes after it start under the previous pass's release tail (kthost doesn't silence it; MuseScore's renders start from
  silence). Without the reset, the takes differ even with the random modulators fixed (-3 .. -6 dB): the round robins,
  not the modulators, decide most of it here (same-key repeated notes). Offline the 1.5 s idle reset counts processed
  audio, so renders with 1.5 s or more between passes reset by themselves.
- **Decided** (2026-10-28): 1 in Recommended, after the owner kept SSO's random modulators (`fixedTransitions` 0, "keep
  A"): every play from one place starts on the same takes, as the library's other patches do already; within a play
  nothing changes. Live: the plain set's Kontakt state carries it.

## Hall guard

2026-10-21 (branch `sso-script-fixes2`; measured, not yet heard by the owner): SSO's Hall trigger (read from the patch's
script and group data): on a patch with an "Rls diminuendo" group (the five string sections' Performance patches), a
Dynamics (CC1) step down of 10 or more in one change while a key is held plays that group on every held key, at the level
before the drop, once until CC1 rises again. Map `<Instrument hallDrop="10">`; the playback audit's HALL.

- **Measured** (Windows test VM, kthost, Violas / Celli - Performance, one held note, CC1 64 -> 50 at 1.5 s, random
  modulators fixed; private `hall_results.json`): one step, or two values (57, 50) at the same sample, adds a layer 14
  dB (Violas) / 25 dB (Celli) under the note in the following 0.6 s, still -33 / -31 dB a second later; two steps of 7
  1, 5 or 20 ms apart don't (they differ from each other by -34 .. -50 dB, from the slow ramp alike: the CC1 path only).
  The note's level is unchanged (+0.05 .. +0.12 dB): it is a change of sound, a release under the held note.
- **MuseScore makes such steps** (the audit on `mtest/libmscore/playbackaudit/hall.musicxml`, Recommended, the shipped
  map): a subito dynamic inside a slur (the Performance legato's overlap still holds the note before: CC1 96 -> 48), a
  dynamic on a slurred note after a diminuendo (80 -> 48 at the bar line), fp (80 -> 64). Not a hairpin (a ramp of small
  steps) nor a dynamic on a detached note (no key held). Whence: none. Even levels' CC11 is another controller.
- `[levels] hallGuard` (0 by default and in both presets; per score): on such a route, such a step while a key is held is
  sent as steps of `hallDrop` - 1, 1 ms (at least a tick) apart, ending at the step's own time (`MidiRenderer::
  libraryHallGuard`, after the track delays; starting there when an earlier CC1 event leaves no room). Values at one tick
  count as one change (as measured). Live: the clips and the plain set carry the steps (rendered events).
- **Proposed**: 1 in Recommended (the release is an artefact of a written subito, not a played diminuendo); the owner's
  call, by ear: `hall.musicxml` bars 1 and 4 with 0 and 1.

## Tightness (measured, nothing changed)

2026-10-21 (branch `sso-script-fixes2`; Windows test VM, kthost, Kontakt 8 + SSO, Violins 1 and Celli - All techniques,
CC1 64, velocity 80, 3 pitches x 4 round robins, Tightness 0 / 0.5039 (the default) / 1 set as the plug-in parameter;
private `tight_results.json`): the panel's Tightness moves the start of the shorts' samples, the peak level unchanged
(within 0.03 dB). Shift against the default, ms, by cross-correlation (the perceived onset, 15 dB under the peak, moves
the same way, 7-15 ms):

| | Spiccato | Pizzicato | Col Legno | Short CS | Short Brushed CS |
|---|---|---|---|---|---|
| Violins 1, Tightness 0 | +10.6 later | +10.6 | +10.6 | +20.0 (2-21 by pitch) | 0 |
| Violins 1, Tightness 1 | -10.6 earlier | -10.6 | -10.6 | -21.2 (10-21) | 0 |
| Celli, Tightness 0 | +10.0 (0.4-10.6) | +15.0 | +15.0 | 0 | 0 |
| Celli, Tightness 1 | -10.0 | -15.0 | -15.0 | 0 | 0 |

So Tightness 1 is "tighter" (the reading of the script's direction in docs/SSO_SCRIPT_KNOWLEDGE.md was the wrong way
round: the slider up skips more). Against the map's onsets (measured at the default, minus the 30 ms latency rule): Violins 1
Spiccato 15 -> about 8 at Tightness 1, Pizzicato 10 and Col Legno 10 -> 0 (the rule floors at 0). Under Recommended the
shorts already start early by their onset, so a Tightness setting would change their attack (less of the bow / finger
noise before it), not their timing; it would tighten Library default and notes played as written by 10-20 ms. Offering
it means re-measuring `onset=` at the chosen value. Not built: the owner's call.

## Microtone bends

2026-10-21 (branch `fast-microtones`; measured with the real library, not yet heard by the owner). The owner heard Whence's
Violas alone (build #458's export, Recommended): "the 3rd measure pitch bending sounds weird", bar 7: slurred or touching
sixteenths at 110 bpm (136 ms), A3, B3 -50, A3, G3 +50, on Violas - Performance (plain, velocity 64: SSO's fingered
transition). Measured (the Windows test VM, kthost + Kontakt 8 + SSO with MuseScore's setups, MuseScore's own events of
the route, pYIN f0; a note "in tune" within 30 cents, its heard time its written place + 111 ms as the export plays
it): the quarter-tone notes were in tune 8-10 % of their time, median error 85 cents, 11 of 27 reached at all. Two causes:

- **The bend's timing** is `bendGlide`, measured on the bowed transition (velocity 93; the Violas 100): it leaves at the
  note-on and arrives 130 ms later. Plain Performance (and `[slurs] transitions`' slurs) play the fingered one, which leaves
  later and reaches the new key 130-200 ms after its note-on (velocity 64, slurred runs of steps at 136 ms, isolated:
  Violas 65 / 158 ms, Violins 1 82 / 189, Violins 2 75 / 201, Celli 100 / 182, Basses 25 / 132). So the bend retuned the
  note still sounding.
- **Key and bend against each other**: B3 -50 after A3 is sent on B3 (a whole step up) bent down, G3 +50 on G3 bent up:
  with the bend early the pitch first went a quarter tone the wrong way (B3 -50 heard near A-flat3 +45, G3 +50 near A3
  +55 for ~100-150 ms), then slid through: the hook the owner heard. Without bends the fingered whole step down in this
  figure often doesn't sound within 250 ms at all (SSO's own transition: Whence 12-TET plays it so too).

`[slurs] microtoneBends` (1 by default and in Recommended since 2026-10-22, 0 in Library default; per score):

- **1**: a microtonal note on a patch that bends (`<Instrument bend>`), reached by a legato transition or going on by one,
  is sent on the key on the side of the note before, so that the key's step and the bend move the same way: B3 -50 after
  A3 on B-flat3 +50, G3 +50 after A3 on A-flat3 -50 (each candidate key, the written one and the two either side of the
  sounding pitch, whose bend fits the range and whose step from the note before and to the note after isn't 0, scores a
  point per transition where key and bend move the same way, the written key on a tie: `microtoneSpelling` in
  rendermidi.cpp); and the fingered transition's bend glides by the map's `<Articulation bendGlideFingered>` (the format
  of `bendGlide`; `tools/soundlibraries/sso_legato_glide_fingered.json`: `legato_glide/gen.py --velocity=64`,
  `glide.py --summarise --moving`), uncut: it runs to its arrival past the next note-on, up to the next transition's leave
  (the next note's own sound comes as late), the next note-on getting the bend in force there (`libraryPitchBends`).
  The pitch sounding once a glide is done is the same as written.
- **2**: the glide only (the written keys).
- **0**: as before (`bendGlide`, cut at the next note-on).
- **3** (2026-10-22, branch `microtone-next`; by hand, "(Custom)", until the owner has heard it): as 1, and each plain
  fingered transition on such a patch is sent early by its glide's middle less the patch's plainShift (below, The notes
  between the quarter tones).

Measured on Whence in context (the same method, each variant two takes; quarter-tone notes with the next note-on within
250 ms; share of their heard time in tune, median error; the 12-TET notes' share after the slash):

| Route, bars | 0 (as now) | spelled only | glide only | 1 (spelled + fingered glide) |
|---|---|---|---|---|
| Violas - Performance, 5-8 | 8-10 %, 85 c / 46 % | 15-17 % | 17-21 % | 35-40 % / 49-52 % |
| Violins 1 - Performance, 5-8 | 10-11 % / 17-19 % | | | 31-32 % / 30-31 % |
| Celli - Performance, 1-8 | 5-7 % / 35-36 % | | | 19 % / 38-39 % |

(1 from renders with the map's glide or the nearest scanned leave / arrival: Violas 70-90 / 180-200, Violins 1 90 / 200,
Celli 100 / 200; the best leave found for the Violas, 90 ms, 43-44 %.)

In MuseScore's own export (the fast-microtones build, run 462, against build #458; the heard check's renders of Whence with
`MS_VERIFY_ROUTES=1`, Recommended; the part's routes added up; fast notes alone in their part, judged over [written start,
next written start]; two takes of 0 and 1, one of 2; the hook: the share of a quarter tone's first 136 ms heard beyond the
note before, away from its pitch, by more than 30 c):

| Part, bars | quarter tones in tune, 0 / 1 / 2 | 12-TET notes in tune, 0 / 1 / 2 | the hook, 0 / 1 / 2 |
|---|---|---|---|
| Violas, 5-14 | 15-16 / 27 / 26 % | 41-43 / 47-49 / 49 % | 10-12 / 0 / 17 % |
| Violas, bar 7 | 2-10 / 16 / 12 % | 12-17 / 43-46 / 52 % | 40-44 / 0 / 49 % |
| Violins 1, 5-14 | 11-13 / 28-29 / 18 % | 39-40 / 29-30 / 33 % | 17 / 0-1 / 6 % |
| Celli, 1-14 | 5-6 / 14 / 15 % | 28-29 / 19-21 / 22 % | 15-16 / 1-2 / 2 % |

1 takes the hook away everywhere (the spelling: 2 keeps it in the Violas) and doubles to triples the quarter tones' time in
tune; the Violins' and Celli's 12-TET notes after a quarter tone lose some (the glide of the quarter tone runs on into them),
so over all their fast notes the Violins 1 gain 25 -> 29 %, the Celli none (17 -> 17-18 %); the Violas 28-29 -> 37-38 %.

The pitch still glides through each sixteenth: SSO's fingered transition takes longer than the note, and no bend makes it
settle within tens of ms. A real violist's quarter tone is a finger placed there, in tune from the note's start; SSO's
bowed transition (velocity 85+) is faster (Violas mid-way 59 ms against 102; with today's bends 30-35 % in tune) but is a
bow change, an articulation the owner chose against for slurs: not offered here. Detached attacks on tuned slots were
measured too (every note an attack on its tuning's copy): 25 % (the attacks' latency and the release tails before them),
and they lose the legato: not offered.

Live: the keys and bends are the renderer's events, so the plain set's technique clips (written and played), its bend lane,
the route set's clips and MIDI out carry them (`tst_liveequivalence::microtoneBendsLive`). Untried in real Live and with
Kontakt in MuseScore's GUI.

### The notes between the quarter tones (value 3)

Build #464 (1 in Recommended) on the VM: the 12-TET notes next to Whence's quarter tones were in tune less often than in
#458 (Violins 1 39-40 -> 30-31 %, Celli 28-29 -> 20-22 %). Measured (the Windows test VM, kthost + Kontakt 8 + SSO with
MuseScore's setups, the route events of MuseScore's dump, pYIN, the measure above; two takes each):

- **Not the glide running on.** Variants of the bends alone (the quarter tone's glide cut at the next note-on; the glide
  back to 12-TET earlier, -8/100, 0/60, 30/120 or 50/150 ms leave / arrival; both earlier; a bend leading SSO's modelled
  transition) trade the quarter tones against the 12-TET notes about one for one: Violins 1 bars 5-14 17 % / 27 % (quarter
  tones / 12-TET, as #464) -> 1-3 % / 49-58 % with an early return, 16 % / 29-31 % with the cut; Celli bars 1-14 11 % / 20 %
  -> 0-2 % / 30-36 %, 7 % / 23 %.
- **The time.** Violins 1's and the Celli's fingered transitions reach their pitch 100-110 ms after the written start, the
  Violas' 40 ms: plainShift (all notes' median: 65.5, 80.3, 111 ms) sends them less early than their fingered glide's middle
  (`bendGlideFingered` after a 136 ms note: 135.5, 141, 111.5 ms). With 1's bends the pitch makes the whole swing, late; #458's
  12-TET notes read better because its quarter tones never got there (the pitch stayed near the middle). The same #464
  renders judged with the windows later, as if sent earlier: Violins 1 best 50-70 ms, Celli 70-80, Violas 0.

**3** sends each plain fingered transition on a patch that bends early by (its glide's middle - the patch's plainShift), at
most the middle after the shortest measured note (136 ms: one shift per part; the slower glides' middles were not checked
against the sound): Violins 1 70 ms, Violins 2 72.5, Violas 0.5, Celli 60.7, Basses 19.8. By `[slurs] transitionsEarly`'s
mechanism (`midEarly` in rendermidi.cpp: switch, controllers, bends and joins move with the note-on; capped so that the note
before keeps `[legato] keepMs`); attacks keep plainShift. Keys and bends as 1. The audit flags the first transition of each
such slur (12 ARRIVAL fails on Whence): its model is the arrival, now about 65 ms before the beat, against the attack before.

In MuseScore's own export (the heard check's renders, `MS_VERIFY_ROUTES=1`, Whence, Recommended; A build #464 = 1, B build
#465 = 3; two takes each; the quarter tones read at their sounding pitch, see below):

| Part, bars | quarter tones in tune, A / B | reached, A / B | 12-TET in tune, A / B | #458's 12-TET |
|---|---|---|---|---|
| Violas, 5-14 | 35-37 / 37-41 % | 27-29 / 27-29 of 32 | 45-47 / 49 % | 41-43 % |
| Violas, bar 7 | 27-30 / 31-32 % | 8 / 8 of 8 | 42-43 / 43-45 % | 12-17 % |
| Violins 1, 5-14 | 23-24 / 31-34 % | 22-24 / 21-24 of 29 | 30 / 51-52 % | 39-40 % |
| Celli, 1-14 | 11-13 / 24 % | 32-33 / 33-35 of 59 | 20-22 / 56-58 % | 28-29 % |

The wrong-way hook (share of a quarter tone's first 136 ms beyond the note before, away from it): Violas 5-7 / 1 %, bar 7
0 / 0, Violins 1 4 / 4, Celli 1 / 1. The heard check's counts (arrival reading) A / B: ARRIVAL 33-36 / 34-36 fail, RHYTHM
11 / 10-12 fail; in the leave reading the same totals (ARRIVAL 111-112 / 110-118, RHYTHM 69-70 / 72) and more Celli RHYTHM
(18-23 / 27-30): in these gliding runs the transition readings sit at their bound, 50 ms before the note-on (Celli median
-129 ms in A, -190 in B), and move with it, so they don't judge this.

The quarter tones' reading: predicted.tsv (the audit's dump, which the heard check and the in-tune measure read) gave a row
the key it is sent on as `pitch` but the note's own cents, so with 1 a respelled note's pitch + cents was a semitone off (50
of Whence's 454 rows; `fromPitch` the note's own key). Since 2026-10-22 the cents are from the key sent and `fromPitch` the
key the note before is sent on (playbackaudit.cpp, `keyCents`). #464's quarter tones read so: Violas 5-14 37 % (reported 27),
bar 7 29 % (15), Violins 1 24 % (28), Celli 13 % (15); its 12-TET figures are unchanged.

## Run bends

2026-10-29 (branch `bowed-join`, OPEN_ITEMS 5); **`[slurs] runBends` 0 by default and in both presets** until the owner has
heard it, **for the owner to hear** (A/B clips). Predicted offline, then rendered on the VM (kthost, 2026-10-29: Measured below).

**Why the fast quarter tones never get there.** Build 491's heard check of Whence (export, Recommended): of 197 quarter-tone
notes, 98 "pitch not reached" (Celli 52, Violas 26, Violins 1 20). In the sixteenth runs (136 ms) the 12-TET notes miss as
well: Violas bar 3 E-flat3 after D3 -50 stays 40-70 c flat, after E3 +50 90-110 c sharp; bar 7 B3 -50 (sent on B-flat3 +50)
comes no closer than 80-100 c, G3 +50 30-50 c. Taking the heard bend out of the f0 (the dump's bends through the patch's
bendSmoothing; build 485's export, which kept its renders: `legato_glide/runlag.py`) shows why: **SSO's own pitch never
reaches its keys in these runs**. A fingered transition there leaves ~65 ms after its note-on and would arrive 160-200 ms
after it, but the next note-on comes at 136 ms and SSO turns towards the next key from wherever it is: the f0 is the keys
sent, delayed and smoothed. Fitted (`runlag.py fit`, three takes, builds 465 / 485, frames of notes whose neighbours start
within 250 ms): Violas D 60 ms / tau 80 ms, Violins 1 70 / 70, Celli 130 / 60, median error 12.6-15.1 cents (the keys as
sent: 82-93 c); the takes agree within 10 ms. The bends did what they were designed to (the quarter tone's offset, glided
with SSO's transition), on top of a pitch that was itself 30-100 c away. Not the bend arriving late, not Kontakt's
smoothing alone: in Violas bar 7 SSO's own pitch swings about 56.1 .. 57.8 for keys 56 .. 58.

**What 1 does.** In a fast run (notes on the lane, each a legato transition from the one before, written IOI < 250 ms,
touching) on a patch whose map has `<Articulation runLag="fingered:D:tau bowed:D:tau">` (`sso_run_lag.json` ->
`gen_spitfire_sso.py`: Violins 1, Violas, Celli - Performance; Violins 2 and Basses have no measured runs: none), the bend
also corrects the patch's own pitch: from each transition's note-on to the next (the run's last note: its written end +
0.3 s), every 10 ms, the value whose heard bend (bendSmoothing's pole) best meets, over the next 30 ms, the written pitch
from its heard start (written + plainShift) less the modelled f0 (least squares, clipped to the bend range ±99.6 c). 12-TET
notes and quarter tones alike. Keys, note-ons, velocities and every other event as without it (`tst_soundlibrary::runBends`);
outside the runs the microtoneBends path, its value in force sent again where a run ends. MidiRenderer::libraryPitchBends
(`runBendPath`). **A bowed transition** (plainAuto's new bow strokes, velocity 93 / Violas 100) takes the map's `bowed` lag;
**SSO's map has none since 2026-10-30** (below, No bowed correction): its own window gets no lag correction (the model takes
its key at its heard start, so the bend there aims at its tuning alone), the notes around it keep theirs. Until 2026-10-30
the map had an assumed bowed lag (the fingered tau with D plus the isolated leaves' difference: Violas 108, Violins 1 164,
Celli 145 ms), and a map without one fell back to the fingered lag. Any other transition (portamento) takes the fingered lag.

**Predicted** (`legato_glide/runlag_sim.py`: SSO's pitch as measured, only the bends replaced, judged as hcana.py: in tune
within 30 c over [written start, next written start], fast notes; three takes: build 485, build 465 b1 / b2; Recommended of
then: no plainAuto, every transition fingered):

| Part, bars | quarter tones in tune, 0 -> 1 | median error | 12-TET in tune, 0 -> 1 | median error |
|---|---|---|---|---|
| Violas, 1-14 (48 QT / 47) | 32.5-40.2 -> 49.3-52.8 % | 38-40 -> 24-30 c | 49.7-53.3 -> 67.1-68.4 % | 29 -> 14-15 c |
| Violas, bar 7 | 26.1-32.0 -> 55.2-59.0 % | 39-42 -> 24-26 c | 40.3-45.0 -> 61.8-64.0 % | 32-35 -> 17-18 c |
| Violins 1, 5-14 (29 / 28) | 30.9-34.1 -> 37.7-40.5 % | 36-39 -> 32-33 c | 51.0-51.5 -> 70.5-71.1 % | 25-26 -> 12-13 c |
| Celli, 1-14 (59 / 60) | 23.6-25.1 -> 41.4-42.1 % | 49-54 -> 34-36 c | 56.0-57.5 -> 64.8-66.0 % | 25-26 -> 17-18 c |

All the parts' legato notes in bars 1-20 (slow ones too, window to 0.6 s): none worse (Violas QT 48 -> 61 %, 12-TET 63 ->
73 %). With SSO's measured pitch instead of the model (the bound for this bend): Violas QT 44.6 %, Celli 51.7 %, Violins 1
24.7 % (Violins 1's quarter tones are limited by the range and the smoothing, not the model). Horizons tried 20 / 30 / 40 /
60 / 80 ms, steps 2 / 5 / 10 / 20 ms: 30 / 10 the best compromise (40 favours Violins 1's quarter tones, 20 the Celli's).
Limits: a quarter tone a semitone key step away needs up to 150 c while SSO's pitch is still on the note before, beyond
the range; the correction moves the pitch faster than SSO's own legato (a fast finger, not a slide), which is what a real
player's sixteenths do. Today's Recommended (plainAuto: each beat's first note bowed) is predicted only for its fingered
notes; the VM render below checks the rest.

**To confirm** (kthost, the Linux build's events, no Windows build needed; one VM job, two takes each):
`HC_RENDER=kthost HC_KEEP=1 HC_BUILD=<build dir> HC_WORK=<dir>/A tools/playbackverify/heard_check.sh --takes 2
Whence.mscz "<Recommended's string>;slurs/runBends=0" "Violins 1,Violas,Violoncellos"`, the same with `runBends=1` in
`<dir>/B`; compare report.json's quarter-tone "pitch not reached" and the in-tune share; then refit `runLag`'s bowed values
with `runlag.py fit` on A (HANDOFF.md / OPEN_ITEMS 5 have the exact command).

**Measured** (2026-10-29: kthost + Kontakt 8 + SSO on the Windows test VM, the Linux build.rel's events, Whence, build
491's Recommended (plainAuto 1: each beat's first note bowed) with runBends 0 (A) / 1 (B), Violins 1, Violas, Celli, two
takes each; judged as the prediction: pYIN of the part's routes added up, fast notes, in tune within 30 c over [written,
next written]; private `runbends-ab/`, `ana/kthcana.py`). In tune, A -> B (take 1 / take 2):

| Part, bars | quarter tones in tune | median error | 12-TET in tune | median error |
|---|---|---|---|---|
| Violas, 1-14 (48 / 48) | 43.0 / 40.5 -> 51.8 / 51.5 % | 34 -> 24-26 c | 45.8 / 43.9 -> 56.0 / 54.5 % | 39 -> 24-25 c |
| Violas, bar 7 (8 / 8) | 28.6 -> 48.7 % (mean of takes) | | 43.0 -> 59.6 % | |
| Violins 1, 5-14 (29 / 28) | 31.2 / 30.0 -> 60.1 / 60.6 % | 41-44 -> 21-24 c | 37.1 / 38.1 -> 49.5 / 49.1 % | 36-41 -> 20-21 c |
| Celli, 1-14 (59 / 60) | 17.2 / 14.1 -> 50.6 / 45.8 % | 51-56 -> 26-31 c | 41.7 / 41.8 -> **33.8 / 34.6 %** | 31-36 -> 39-41 c |

By transition (mean of the takes): fingered (64) quarter tones Violas 42 -> 52, Violins 1 31 -> 60, Celli 16 -> 48 %;
fingered 12-TET Violas 55 -> 62, Violins 1 48 -> 65, **Celli 56 -> 48**; bowed (93 / Violas 100, all 12-TET) Violas 31 ->
45, Violins 1 23 -> 31, **Celli 25 -> 18 %**. So runBends does what was predicted for the quarter tones (every part at or
above the prediction; Violins 1 far above: 38-41 predicted, 60 measured) and for the Violas' and Violins 1's 12-TET notes
(less than predicted: 55 / 49 % against 67-71, A's bowed beat notes, not in the prediction, pull them down); **the Celli's
12-TET notes got worse** (-7 to -8 points; 5 notes lose more than 0.3 of their share, all bowed C3s on beats of bars 1 and
5). The heard check's own findings, A -> B: ARRIVAL fail 28 / 25 -> 25 / 24, warn 14 / 12 -> 9 / 7; RHYTHM runs fail 8 / 9 ->
7 / 7, warn 1 / 1 -> 5 / 3; LEVEL fail 0 -> 0, warn 4 / 1 -> 3 / 1; "pitch not reached" (its 0.7 s arrival rule) quarter
tones Violins 1 20 / 17 -> 15 / 15, Violas 26 / 29 -> 30 / 31, Celli 50 / 47 -> 45 / 44; the not-reached notes'
`closestCents` median 50-80 c either way (that rule barely moves: a note in tune for part of its 136 ms is still "not
reached" if never 3 frames in a row at its own pitch before the window ends).

**Refit on A** (`runlag.py fit`, bars 1-14, the main route of each part; then `ana/runlag2.py`, fingered and bowed each their
own D / tau): one lag for all fast notes Violas 50-60 / 70-80 ms, Violins 1 50-60 / 70-80, **Celli 90 / 90** (the map's
fingered Celli 130 / 60); split, fingered Violas 60-70 / 70, Violins 1 70 / 70, Celli 120 / 60-70 (the map's within 10 ms:
confirmed) and **bowed D 30-50 ms, tau 70-150 ms** (Violas 30 / 100, 40 / 70; Violins 1 30 / 150 both takes; Celli 40 / 150,
50 / 150) against the assumed 108 / 80, 164 / 70, 145 / 60: a bowed new bow starts moving at once, then converges slowly.
Not a clean fit: tau sits at the grid's edge, and with a wider grid it runs on to 400 ms (D 20), which also spoils the
fingered fit; only 14-29 bowed notes a part. So A's data rules the assumption out (D <= 50 ms in every fit) but does not pin
a value. **Proposal** (not in the map): bowed `40:150` for all three (the bounded fit), then a kthost B' with it; the Celli's
12-TET loss is the bowed beat notes corrected for a lag that isn't there. The owner by ear first or B' first: his call.

**B' for the Celli** (2026-10-30: kthost + Kontakt 8 + SSO on the VM, as B but the Celli's bowed `runLag` 40:150 from a temp
map, the Celli only, two takes; plus `clipA`, a third take of A; private `runbends-ab/B2`, `ana/kthcana.py`, `ana/bytrans.py`,
`ana/signed.py`). Celli 1-14 in tune, A (take 1 / take 2 / clipA) -> B (145:60) -> B' (40:150):

| | A | B | B' |
|---|---|---|---|
| quarter tones (59, all fingered) | 17.2 / 14.1 / 15.8 % | 50.6 / 45.8 % | 53.2 / 58.1 % |
| 12-TET (60) | 41.7 / 41.8 / 43.2 % | 33.8 / 34.6 % | **31.1 / 29.7 %** |
| fingered 12-TET (30) | 54.3 / 57.7 / 56.3 % | 46.1 / 49.3 % | 46.6 / 45.0 % |
| bowed 12-TET (29, the beat notes) | 26.9 / 23.8 / 27.9 % | 18.8 / 17.4 % | **12.9 / 11.6 %** |

**40:150 does not fix the regression: it makes the bowed beat notes worse** (the quarter tones gain a few points). The median
signed error of the bowed notes (+ = still towards the note before) in the first / second half of their 136 ms: A +54 / -36 c,
B +39 / -51, B' +47..+59 / -74: in its second half SSO's bowed new bow is already past the written pitch, moving on, so a
correction that assumes it still lags pushes it further, and the longer tau assumes more lag. The bowed notes want no lag
correction or another model, not other D / tau of this one; a map without `bowed` falls back to the fingered lag
(`rendermidi.cpp`), so "no correction for bowed" needs code. The fingered 12-TET loss (56 -> 46-49 %) doesn't move with the
bowed value: not explained yet. **No map change**; runBends stays 0 everywhere. A/B page for the owner (Violins 1, Violas:
A / B; Celli: A / B'): see OPEN_ITEMS 5.

**No bowed correction, the Celli's fingered 90:90, a slew cap** (2026-10-30, branch `run-bends-fingered`; analysed on Linux from the
kept f0 of the A / B / B' takes above, no new render; runBends stays 0 everywhere).

- **Bowed**: `sso_run_lag.json` has no `bowed` any more, and a transition without a lag of its kind is modelled at its key
  from its heard start (written + plainShift). Simply leaving its span out (note-on to the next note-on) would not do: every
  note-on comes 80-140 ms before its written start, so that span is mostly the note before's tail, and dropping it cost the
  quarter tones before the beat notes (replica, Celli QT 46 -> 32 %). `tst_soundlibrary::runBends`, run-bends-bowed.musicxml:
  the bowed A4 at its tuning within 8 c with `runLag="fingered:60:80"`, corrected with a bowed lag; the fingered note after it
  still corrected; the run before it unchanged.
- **The replica**: `runlag_sim.py` with `RUNLAG="fingered:D:tau [bowed:D:tau]"` (and `OLD=<runBends 0 take>` when the base is a
  runBends take) runs the C++'s own controller (runs, segments, model, 1 ms grid, 10 ms least-squares sends over 30 ms, the
  range); against B's dump it gives the same heard bend within 0.4 c rms (Celli; Violas 0.25).
- **Why the Celli's fingered 12-TET notes got worse (56 -> 46-49 %)**: not the horizon, the step or Kontakt's smoothing. A's
  own pitch plus B's actual bends would have put them at 64 %. But SSO's own pitch (f0 less the heard bend) is not the same
  under B's bends, although every other event is identical (the dumps compared without pitch bend): A against B 14-16 c a
  frame, A's two takes 9-10 c, and no model of the heard bend explains the difference (one pole 50-150 ms, a delay 0-40 ms,
  a gain 0.8-1.3, each voice's modulator starting at its note-on: 12-13 c at best; a free impulse response leaves 60 % of
  it). In those notes (the reversals between two quarter tones, 46.5 -> 48 -> 49.5 on keys 47 -> 48 -> 49) SSO's pitch under
  the correction gets 15-20 c further in the second half than under A: median signed error by quarter of the note (+ =
  towards the note before) A +76 / +19 / +2 / -22 c, B +76 / -1 / -34 / -34. The correction overshoots: the fingered model
  130:60 lags more than SSO does once the bend pushes it. It is not 12-TET against quarter-tone steps as such (each is one key
  here). The lag fit can't tell D from tau: its error is flat along the ridge (A, one lag for all fast notes: 90 / 90; B take
  1 90 / 90, take 2 130 / 50, the same error within 0.5 c), and on that ridge **90:90** is the point that doesn't overshoot.
  The Violas' and Violins 1's alternatives (60:70, 50:80; 70:50, 55:75) gain nothing consistent: unchanged.

- **Saturation** (the coordinator's finding in clipB's dump, Violins 1 bars 5-8: most values at the range's ends, swings of
  150-200 c within 10 ms around each bowed note): the least-squares send kicks Kontakt's 72 ms pole between ±99 c because what
  it wants is beyond the range (a quarter tone a key step away needs up to 150 c while SSO's pitch is still on the note
  before). **Since 2026-10-30 no send goes more than 80 c from the one before** (`RUN_SLEW_C`; at a run's start from the old
  path's value, at its end back to it in such steps): Whence bars 1-14 (replica, B's base), steps over 100 c Celli 117 -> 1,
  Violas 107 -> 0, Violins 1 54 -> 2; sends at the range's ends Celli 53 -> 45 %, Violas 19 -> 17, Violins 1 25 -> 23
  (Violins 1 bars 5-8, all its pitch-bend values: 58 % in B as rendered, 53 without the bowed lag, 48 with the cap). The
  pinning that stays is the range itself: limiting it costs correction (a ridge on the send, replica `LAM`: 1 halves it,
  20-34 / 11 / 15 %, the 12-TET notes 4-8 points lower; 2: 8-20 %, 6-12 points lower; a cap on the send's distance from the
  heard bend, `KICK` 40-60: 0-3 %, Violins 1's quarter tones 58 -> 14-42 %). Not in the code: the owner's or a VM run's call.

Predicted by the replica with all of it (bowed, Celli 90:90, the slew cap; Whence bars 1-14, Violins 1 5-14, fast notes in
tune, take 1 / take 2), from B's measured pitch (the closer base: the new bends differ less from B's than from A's) and, in
brackets, from A's:

| Part | | A measured | B measured | C predicted (this branch) |
|---|---|---|---|---|
| Celli | quarter tones (59) | 17.2 / 14.1 % | 50.6 / 45.8 % | 48.2 / 45.1 % (43.4 / 41.7) |
| | 12-TET (60) | 41.7 / 41.8 % | 33.8 / 34.6 % | **52.9 / 54.4 %** (58.0 / 60.6) |
| | fingered 12-TET (30) | 54.3 / 57.7 % | 46.1 / 49.3 % | 59.1 / 63.7 % (68.1 / 70.3) |
| | bowed 12-TET (29) | 26.9 / 23.8 % | 18.8 / 17.4 % | 44.9 / 43.2 % (46.1 / 49.3) |
| Violas | quarter tones (48) | 43.0 / 40.5 % | 51.8 / 51.5 % | 45.5 / 44.4 % (40.0 / 39.4) |
| | 12-TET (48) | 45.8 / 43.9 % | 56.0 / 54.5 % | 65.5 / 64.5 % (68.0 / 66.5) |
| | bowed 12-TET (21) | 30.1 / 31.0 % | 43.8 / 45.9 % | 66.8 / 70.1 % (69.5 / 68.1) |
| Violins 1 | quarter tones (29) | 31.2 / 30.0 % | 60.1 / 60.6 % | 52.2 / 53.1 % (35.1 / 34.3) |
| | 12-TET (28) | 37.1 / 38.1 % | 49.5 / 49.1 % | 58.3 / 57.2 % (63.7 / 63.9) |
| | bowed 12-TET (13) | 22.4 / 23.5 % | 31.0 / 30.8 % | 44.1 / 43.7 % (52.2 / 51.7) |

So: every part's 12-TET notes above A and B, the Celli's back above A (the bowed beat notes too: the bend there settles
on the tuning quicker than the old path's glide); the quarter tones well above A, below B by 2-8 points (Violins 1 60 -> 52-53,
Violas 52 -> 44-46: the slew cap's price, 2-6 points of it). A's base over-predicts the 12-TET notes and under-predicts the
quarter tones (as it did for B: the same non-additivity).

**To confirm** (kthost, the branch's Linux build, no Windows build; one VM job, two takes, ~10 min of VM, lock taken by
heard_check.sh): `nohup bash ~/MuseScore/ms3fork/private/queued/runbends_c.sh >
~/MuseScore/ms3fork/private/queued/logs/runbends_c.log 2>&1 &` (the branch's tree and its own build dir `build-rb`, no sudo: `HC_RENDER=kthost HC_KEEP=1
HC_BUILD=~/MuseScore/ms3fork/build-rb HC_WORK=runbends-ab/C tools/playbackverify/heard_check.sh --takes 2 Whence.mscz
"<rec491>;slurs/runBends=1" "Violins 1,Violas,Violoncellos"`, as slot1030's job b); then `ana/kthcana.py` on C's takes against
A and B, and `ana/bytrans.py` for the Celli by kind. A is unchanged by this branch (runBends 0).

**Measured: the fix and a far-side key** (2026-10-30: kthost + Kontakt 8 + SSO on the Windows test VM, two takes each,
analysed on Linux; private `runbends-ab/`, `ana/smear_report.md` › 7). **Crep** = this branch's bends (by its replica, on
B's events); **Cfar** = Crep + each descending fast quarter tone on the key on the far side (48 -> 46.5 on key 46 bent +50
instead of key 47 bent -50; 19 Violas, 14 Violins 1, 30 Celli notes), predicted +3.5 points. In tune within 30 c, fast notes,
take 1 / take 2:

| Part | | A | B | Crep (this branch) | Cfar |
|---|---|---|---|---|---|
| Violas 1-14 | quarter tones (48) | 43.0 / 40.5 % | 51.8 / 51.5 % | 46.8 / 51.3 % | 31.3 / 32.4 % |
| | 12-TET (48) | 45.8 / 43.9 % | 56.0 / 54.5 % | **69.5 / 67.7 %** | 58.1 / 56.2 % |
| Violins 1 5-14 | quarter tones (29) | 31.2 / 30.0 % | 60.1 / 60.6 % | 49.8 / 50.6 % | 51.9 / 54.0 % |
| | 12-TET (28) | 37.1 / 38.1 % | 49.5 / 49.1 % | **55.8 / 56.7 %** | 42.6 / 43.6 % |
| Celli 1-14 | quarter tones (59) | 17.2 / 14.1 % | 50.6 / 45.8 % | 47.6 / 46.4 % | 39.0 / 35.4 % |
| | 12-TET (60) | 41.7 / 41.8 % | 33.8 / 34.6 % | **48.0 / 50.3 %** | 46.4 / 43.3 % |

**This branch's prediction held**: mean of the six 53.4 % measured, 53.4 predicted, each within 4.5 points of its prediction
(take spread 1-4.5): the 12-TET notes 7-15 points above B in every part (the Celli back above A), the quarter tones 2-10 below
B (the slew cap). **The far-side key failed** (44.5 % against 56.9 predicted): on the far key SSO plays a whole-tone fingered
transition, which barely glides inside 136 ms (the Violas' respelled notes 44-49 -> 4 % in tune, still 113 c towards the note
before in their second half), and the 12-TET note after loses 1-13 points. The pre-registered rule (in both takes quarter
tones >= +5 and 12-TET >= -3 against Crep) fails in every section (Violas -16 / -19 and -11; Violins 1 +2 / +3 and -13; Celli
-9 / -11 and -2 / -7): **dropped, no spelling change**. Low band (40-150 Hz, the 100 ms after each note-on): the respelled
notes 0.7-1.6 dB higher in Cfar, within the takes' spread; no new loudest burst. Every value stays below the assumed
real-player band (in30 65-90 %) except Crep's Violas 12-TET at its bottom edge: no risk of a "MIDI" over-correction.

**Confirmed with the branch's own render** (2026-10-30, `runbends_c.sh`: build-rb's events, kthost + Kontakt 8 + SSO on the
Windows test VM, two takes, analysed on Linux; private `runbends-ab/C`): take 1 / take 2, Violas quarter tones 47.3 / 49.6 %,
12-TET 69.3 / 69.1 %; Violins 1 50.1 / 50.0 %, 56.3 / 57.2 %; Celli 45.0 / 47.2 %, 48.2 / 48.3 % (bowed 12-TET 39.2 / 37.0,
fingered 12-TET 55.2 / 57.5, below the replica's 44-45 / 59-64). Mean of the six 53.1 % against Crep's 53.4: the C++ path
plays what the replica predicted, within the takes' spread.

## Plain accents

2026-10-20 (branch `plain-accents`); **2 in Recommended and by default since 2026-10-21** (branch `accent-level`; the owner,
on hearing 0-3: "ig 2 is preferable, but is accented normally that loud? if yes then I'm fine with it": 2's tone at real
players' level, below, Level of the accented attack). Under `[slurs] plainPerformance` an accented note played
exactly as a plain one: velocity 64 (a spiccato attack) and no level of its own, because `performanceNote` sets 64 before it
reads the accent and the plain choice's base "long" is no velocity technique, so MS4.7.5's articulation offset (which
`[levels] calibrated` gives a technique on velocity, `libVelocity`) was never added (private/bars9-12/ACCENT.md: Whence bar
8's staccato + accent 1.6 dB under the slur before it in SSO, +3 to +5 dB over it in the built-in synth).
`[slurs] plainAccents` (2 by default and in Recommended, 0 in Library default; per score), for a plain Performance note
(`Choice::plain`):

- **1, the level**: a note with an accent, marcato or soft accent (the articulations MS4.7.5's tables give a louder curve:
  their curves peak at 7000 against 5000 for strings, 6000 for the other families) plays MS4's offset for all its
  articulations, 40 log10(share) (`Ms4::NoteResult::share`: its velocity over a plain note's at the same dynamic), the same
  number calibrated levels add on velocity techniques: strings' accent, staccato + accent, marcato or soft accent +5.3 dB.
  It is the note's own level (`marcatoLevel`'s `plainAccentDb`, added to the MarcatoLevel / text level): louder by the
  dynamics CC along the patch's own measured curve (`DynamicsCurve::raise`; SoundFont 2's law without one), on top of the
  level rule's CC11, as a positive MarcatoLevel plays; slurred notes too (the route's CC1 back after it).
- **2, the attack**: an accent or marcato on an Auto attack without a velocity of its own that plays an attack (a staccato,
  a note after silence, a note touching the one before outside a slur) takes the Performance legato's `accented` attack
  (SSO: velocity 122, band 117-127) instead of 64, as `[slurs] performance` gives it; a note touching the one before on the
  patch is re-attacked (the note before ends the map's `reattackGap`, 54 ms, before it) instead of SSO's transition from
  it. A slur's transition stays a transition at 64 (as under `[slurs] performance`), and so does a soft accent.
  **At real players' level** (2026-10-21): the note's CC11 moves by D = `PLAIN_ACCENT_DB` (+5.0 dB) - the map's
  `accentedLevel` at its pitch and length, so it plays +5.0 dB over the spiccato attack it replaces (below).
- **3**: both (the attack at +5.0 dB, then 1's level on top).
- **Measured** (Windows test VM, kthost, Kontakt 8 + SSO, the VM MuseScore's own setups of the five Performance patches,
  2026-10-20; private/plain-accents): the accented attack (122) against the spiccato attack (64) after silence, CC1 64,
  CC11 as Whence bar 8's staccatos, 150 ms notes, 3 pitches x 4 rounds (the rounds within 0.5): perceived peak (the
  fork's perceived envelope, loudness-level units) +6.4 dB Violins 1 (+4.2 .. +9.2 by pitch), +7.8 Violins 2 (+1.8 ..
  +8.6), +8.5 Violas (+3.5 .. +8.9), +5.3 Celli (+4.9 .. +6.4), +5.4 Basses (+4.3 .. +8.0); the loudest 50 ms +0.8 ..
  +9.6 dB. So 2 alone is about as loud as 1 (+5.3) and 3 adds both, about +11 dB: for the owner to judge by ear.
  The accented attack's timing (heard against the plainShift measured on velocity 64) is not measured: the audit treats
  it as every plain note.
- **Live**: the level is the note's CC1 / CC11 in the clips and the plain set's lanes, the attack its velocity
  (`tst_liveequivalence::plainAccentsLive`; rows "real level": the accented notes' CC11 in MuseScore, the clips and the
  plain set alike).
- `tst_soundlibrary::plainAccents`.

### Level of the accented attack (2026-10-21, branch accent-level)

- **The target, real players**: an sf at f plays **+5.0 dB** over the unaccented notes around it in the same part (the
  five sections' mean, 90 % [+3.8, +6.0]; the sections +4.1 .. +5.5, within one instance's noise, so one pooled value),
  the Spheres dataset's Mozart K. 550 (Colibri Ensemble, arXiv 2511.21247, CC BY-SA 4.0), each section's close stem, the
  perceived peak in the 0.4 s after the onset (the fork's perceived envelope), 34 instances; readings approved by the owner
  2026-10-21. `MidiRenderer::PLAIN_ACCENT_DB` (rendermidi.h). **An assumption**: K. 550's strings have only sf, no ">" or
  marcato; in Classical notation sf is the stronger sign, so a ">" may play softer. MS4.7.5's +5.3 dB lies inside the
  interval.
- **SSO's side** (`<Articulation accentedLevel="pitch/ms:dB …">`, generated from `tools/soundlibraries/sso_accent_attack.json`
  by `gen_spitfire_sso.py`; `Articulation::accentedLevelAt`, linear in pitch, log-linear in ms, the nearest end's beyond,
  as quickLevel): the accented attack (122) minus the spiccato attack (64), the same perceived peak in 0.4 s, after silence,
  CC1 64, every semitone of each section's range x 100 / 150 / 211 / 300 / 450 ms, 2 rounds (within 0.4 dB), kthost +
  Kontakt 8 + SSO on the test VM, **at the real players' level**: each section's render scaled so that its 64 peaks at
  59.3 loudness units (median at 211 ms), the median of the unaccented neighbours at f in the Spheres study (the perceived
  envelope is level-dependent: at kthost's unity level, 15-35 units, the differences came out 0.2-2 dB larger, up to 4.7
  at single keys; `sso_accent_attack.json` keeps each section's gain). It depends strongly on **the note's length**: the
  64 swells while held, the 122 is loud at once, so the difference falls from +5.6 .. +8.0 dB at 100 ms (the sections' medians) to +1.2 .. +6.1 at 450 ms
  (450 = 700 = 1000 ms within 0.2 dB: the window then sees no note-off), and on **the key** (sample zones, mostly in pairs
  of semitones: Violins 2 at 211 ms +1.6 .. +12.3 dB). The renderer takes the note's length as played (written x the
  length factor the plain note gets: 100 % for a non-staccato, the note model's for a staccato; a join to the next note
  may shorten it a little more).
- **D = 5.0 - accentedLevel**, the median over each section's keys (range): Violins 1 -2.6 (-6.8 .. +2.5) at 100 ms,
  +1.2 (-2.4 .. +4.6) at 211, +3.2 (+0.1 .. +5.9) at 450; Violins 2 -3.0 / +0.6 / +2.9; Violas -1.3 / +2.2 / +3.8;
  Celli -0.6 / +1.1 / +2.5; Basses -2.4 / -0.9 / -1.1. A positive D (the accented attack less than +5 dB over the
  spiccato, mostly held notes) raises CC11 only as far as 127 (`libraryNoteLevels` caps it; not by CC1, which would also
  change the tone): what is left is not played.
- **Checked on the VM** (kthost, real SSO, 2026-10-21): in isolation (25 keys x lengths, CC11 as the renderer sets it,
  at the real players' level) 122 - 64 = **+5.07 dB** median (+3.9 .. +5.3; the two lowest: Violas Eb4 capped at CC11
  127, Basses C2 at 205 ms); at kthost's unity level the same notes give +6.0 (the level dependence above). In Whence
  (bars 8-9, each route alone at its patchGain, the change from value 0, both renders scaled so that 0's note peaks at
  59.3 units): Violins 1 C6 +5.9, Violins 2
  C6 x5 +3.3 .. +5.3 (median +4.3), Violas Eb4 +4.5, Celli C4 +4.9, Basses C2 +3.3 (the held C2 before it releases into
  the window).
- **Why the isolated figure (150 ms) and Whence differed** (ACCENT-FIX.md: Violas -0.6 dB in Whence against +8.5
  isolated): the length. Whence's staccato + accents play 205-211 ms; the same Violas key (63) after silence gives +3.5
  dB at 150 ms and -1.1 at 211 (CC11 35). Not the CC11 jump at the note-on (127 -> the note's value at the note-on, 30 ms
  before, or steady: within 0.2 dB), not the instance's first note (a fresh instance plays the same), the CC11 value
  little (86 against 35: +0.3 .. +0.5 dB at 150 ms, 1.3 at 211); Violas' 63 itself is one of the keys where the accented
  attack is weakest. Celli (60: +6.4 at 150, +1.7 at 211) and Basses alike; Violins 1's 84 at 211 ms is still +7.5, as
  Whence measured.
- **Uncertainty**: the real side ±1.1 dB (90 %) plus the sf-for-">" assumption; SSO's side: the rounds within 0.4 dB, the
  listening level (the perceived measure's level dependence, about 1 dB between 35 and 59 units; the owner's monitoring
  level is unknown), the note's length as estimated (10 % off: about 0.5-1 dB on the steepest keys); for notes from 0.45 s
  on the measure is the first 0.4 s, as the real side's.

## Chords on monophonic patches (voice lanes)

2026-10-20 (branch `chord-voices`; a correctness fix, the owner: "of course it should play every note"; not a setting, in
every preset). SSO's Performance patches play ONE legato line per Kontakt slot (read from the patch's script, Violas -
Performance: a note-on while a note is held is a legato transition that ends it; one less than 15 ms after the note-on
before at velocity 64 or less is dropped). So two notes of a chord on one slot lose one: Whence's Celli, four three-note
chords, one note of each silent (private/bars9-12, VM run 2 of 2026-10-20). The map marks such a patch
`<Instrument mono="1">` (`gen_spitfire_sso.py` `MONO`: every "... - Performance" / "... - Total Performance" patch, 42;
the others share the Violas script, an assumption checked on Celli).

- **Voices** (`SoundLib::lanes`, `Lanes::voice`, `Route::voice`): a mono patch's notes, in the order they start, those
  starting together from the lowest pitch up, each take the lowest voice free then (its last note ended, as written,
  by this note's start; a grace note its chord's lowest note's voice). Voice 0 is the line (a lone note, a chord's
  lowest note); a chord after a chord continues voice by voice in pitch order, so each voice is a legato line of its
  own. A part without overlapping notes has all its notes in voice 0: its lanes exactly as before.
- **Copies**: each voice gets its own tuning lanes (as before, within the voice), voice 0's first. On a bending patch
  a note of a chord (or right after one) played plain (`[slurs] plainPerformance`), touching its voice's note before,
  stays on that note's copy and glides there, as a slurred note does (SSO plays it as a transition, which ends the
  old note without its release); a lone note after a lone note keeps the lanes it had. Every copy is a
  route like a tuning copy: hosted, a slot of the patch's instance on its own MIDI channel (hosted slots; the gain
  stage, Mixer and parameters the instance's, once), the command-line renders' instances the same; Live's plain set a
  slot of the patch's multi and a MIDI track named "<part> (voice n)" (with its tuning when not 0 cents); the route set
  and *Live plays the score* a track / clip per copy; MIDI out a channel. A library without varispeed: one untuned copy
  per voice.
- **Transitions**: where a chord comes before or is the note's own, a note goes on legato (transition, join, bend
  glide) only from the note before on its own copy (`otherLane` in rendermidi.cpp); a chord's other notes attack. A
  lone note after a lone note as ever.
- Level rule, section balance, real range, master trim, even levels: by patch (a voice copy is its patch); even levels
  measure a voice copy's passages on its own route.
- **Audit**: CHORD (fail), a note-on less than 15 ms after the note-on before on its route while that one still sounds,
  on a mono patch.
- Whence (Recommended, `dumpEvents` before / after, lanes pinned at max=8): only Celli - Performance changes, from 19.67 s
  to 34.94 s (bars 9-16, the divisi passage: five two-note then four three-note chords), and one copy more (4 instead of
  3: a slot more in its instance). Every other route, every note's time, the gains, volumes and the trim are identical. The three
  voices now play three legato lines, a copy each (before: three chord notes shared a copy with another chord note at the
  same instant, one dropped; other chord notes "joined" a note on another copy). The audit: CHORD 0 (the same rule
  on the old routing: 3), every other check unchanged (all 0).
- Level: with `[levels] divisi` (1 by default and in Recommended since 2026-10-20) each voice of a chord plays
  10 log10(n) dB softer, so the chord adds up to one section (below, Divisi level); with it off each voice copy plays at
  its patch's levels, a three-note divisi chord three full sections.
- Cost: a slot per extra voice (Kontakt compiles its script per slot, ~1.3-1.7 s at load; ~220 MB).
- Tests: `tst_soundlibrary::chordVoices`, `tst_liveequivalence::chordVoicesLive` / `chordVoicesHosted`,
  `tst_playbackaudit::chordFindings`. Untried with Kontakt / SSO and in Live.
- A unison is one note (2026-10-28, branch `bowed-join`; a fix, no setting): two notes of one pitch and tuning (within
  1 cent) starting together on a mono patch (two staff voices meeting, a doubled chord note) are ONE note:
  `SoundLib::Lanes::unison` maps the duplicate (the shorter, else the later one) to the kept note, whose voice, lane
  and tuning it takes; it counts in no divisi n and `MidiRenderer::libUnison` sends nothing for it. Before, the two
  notes sat on two voice copies of one key: `notes/sameKeyEndsFirst` (keyed by channel and key, not by copy) cut one
  copy's note to zero length, or, its starts apart, moved a note-off before the later note-on, which then hung; the
  other copy played 3 dB under a lone note (divisi n = 2) while the same sample on two copies in phase adds +3 dB.
  `sameKeyEndsFirst` now keys by lane too, so one copy's note-off never ends another copy's note. A quarter tone
  apart is no unison. Test: `tst_soundlibrary::monoUnison` (`unison.musicxml`).

## Divisi level

2026-10-20 (branch `divisi-level`; the owner, 2026-10-20, "yes" to playing it as a real orchestra does). `[levels] divisi` (1 by default
and in Recommended, 0 in Library default; per score). With voice lanes (above) every chord note on a monophonic patch
plays the whole section's patch, so a three-note divisi chord sounded three full sections. A real section divided into
n groups plays each note with 1 / n of its players; n equal groups playing independently (incoherent) add up as
powers, so each group plays **10 log10(n) dB under the whole section** and the chord adds up to one section's
loudness: n = 2 → -3.01 dB, 3 → -4.77 dB, 4 → -6.02 dB.

**An assumption from player counts, not measured divisi data** (CLAUDE.md › levels come from real data: we have
no measurement of a divided section's level against the whole; the rule takes the players as equal and their sum as
powers). It
ignores that a divided section's players may play a little louder (a gap without data).

- **n per note**: the voices of the note's part on that patch sounding at the note's start as written, the note itself
  and the notes starting with it included (`SoundLib::Lanes::sounding`, computed with the voices in `SoundLib::lanes`;
  a grace note its chord's count). Voice 0 (the line) is lowered too while it is part of a chord: every voice of a
  chord plays the same share. A note after the chord (n = 1) plays at the whole section's level again.
- **A note held under a chord change keeps the level of its own start** (no CC11 move inside a note). Why: the level is
  set per note-on through the per-note level schedule, which changes only at note-ons; a CC11 glide inside a held note
  would be a level change the listener hears on that note (SSO's CC11 is a plain volume), and a new mechanism. The
  cost: while a voice enters under a held one, that moment is up to +1.8 dB over one section (held at 1 + the newcomer
  at 1/2, two voices), until the held note's next note-on. Whence's divisi (Celli, bars 9-16) are block chords (all
  notes start together, a bar each), so there it is exact.
- **How**: by CC11 per note on its route, through the per-note level schedule (`MidiRenderer::libraryNoteLevels`,
  `libLevels` volumeDb: dB add up with the level rule, real range, track levels, mic compensation, even levels'
  headroom), so it reaches Live's clips, the plain set's CC11 lanes, the route set and MIDI out as every other note
  level does (`divisiLevel` in rendermidi.cpp, `MidiRenderer::libDivisi`). A library whose dynamics are CC11 has no plain
  volume for it: not applied there (SSO's are CC1).
- **Master trim / gain stage**: the trim renders the score and reads each route's CC11 (`ContextLevel::masterTrim`), so it
  sees the lower levels (a chord that was the score's loudest moment now trims less). The gain stage is per patch and
  unchanged.
- **Even levels**: they measure slurred passages faster than 350 ms per note (`EvenLevels::SLOW_IOI_S`); a fast slurred
  line that enters a chord would see the divisi step in its envelope and the fit would flatten it (an open item; none in
  Whence: its divisi are a bar per chord).
- Whence (Recommended, `dumpEvents` chunked and `MS_DUMP_LIVE`, lanes pinned at max=8, divisi 0 against 1, same
  build): only Celli - Performance's copies 1-3 change (the divisi's three voices; copy 0 and every other route
  identical), and only their CC11: the 20 notes of the four two-note chords (17.45-26.18 s) -2.90 .. -3.02 dB, of the
  four three-note chords (26.18-34.91 s) -4.72 .. -4.83 dB (CC11 73-76 -> 52-54 / 42-44: the rounding of a 7-bit value).
  Note times, velocities, notes.txt, trace, volumes, headroom, tunings identical; the trim stays 0 dB (its loudest moment
  moves from 33.41 s in the Celli's chords, -5.62 dBFS, to 15.41 s, -5.81); the audit unchanged, every check 0. divisi 0
  is identical to the chord-voices branch's own dump (headroom.txt's rows in another order).
- Tests: `tst_soundlibrary::divisiLevel` (two- and three-note chords -3.01 / -4.77 dB against a lone note, back to 0
  after, a held note keeping its start's level, hosted / Live's clips / MIDI out, off, not mono),
  `tst_liveequivalence::divisiLive` (Live's clips carry MuseScore's CC11 route for route, the plain set's copies' CC11
  lanes the chord values; off: none), `tst_soundlibrary::playbackPresets`. Untried with Kontakt / SSO and in Live; not
  heard.

## Transition levels

`[levels] transitionLevels` (branch `slur-joins`, 2026-10-22; **2 by default and in Recommended since 2026-10-28** (branch
`bowed-join`, a fix: below, 2), 0 in Library default; 1 never heard by the owner; per score). Every per-note level by CC11 (the level rule's gains, real range, track and note levels,
divisi) goes on its route with the note's note-on (`libraryNoteLevels`), and so "what still rings of the note before takes the
note's level". For an attack that is its release; for a legato transition it is the note before itself: SSO's transition
leaves the old pitch 25-140 ms after the note-on and reaches the new one 130-300 ms after it (`bendGlide`). With 1:

- where a note-on is a legato transition (`libGlide`: a slurred note, or a plain note touching the one before under
  `[slurs] plainPerformance`) whose CC11 level differs from the note before's on its copy, the note before keeps its
  level until the glide's leave, then CC11 moves to the new note's value linear in dB, a value every 5 ms
  (`EvenLevels::STEP_MS`), reaching it at the glide's arrival, at the latest at the route's next note-on;
- the leave and arrival are the ones the bend uses (`bendGlide` by the note before's length, `bendGlideFingered` under
  `[slurs] microtoneBends`; else the legato delay at once);
- only CC11 levels (a level on another controller switches at the note-on as before); a CC11 the score writes between the
  note-on and the arrival (a hairpin's step) is mapped by the new note's level.

Why: Whence's Violas bars 12 / 16 (after the lane fix above, Plain Performance › Joins across tunings): the slur's F#4
plays the level rule's slur gain (CC11 110), the G4 half note after it a single note's (47). Sent with the half's note-on
(111 ms before the beat, the patch's plainShift), the 47 turned the F#4 down 6-7 loudness units over its last 100 ms
(MuseScore's export of #466 on the VM, Kontakt 8 + SSO, the perceived envelope of the Violas alone: 49.5 at -130 ms,
43.0 at -30 ms, 42 at the beat), a diminuendo nobody wrote. Whence, Recommended: 38 transitions have a CC11 step at their
note-on (the realRange steps of 1-4 values at bar lines, Celli bar 13 54 -> 42, the Violas' 110 -> 47 and Violins 1's
64 -> 35 at bar 16). Measured (kthost + Kontakt 8 + SSO on the VM, the dump's Violas routes, Recommended with 0 / 1, the events at written time: no plainShift): with 0 the perceived envelope falls from 39.6 at the half's note-on to 31-32 by 90 ms after it, the F#4 still sounding; with 1 it stays 39.4-40.2 to 90 ms, then follows the glide down to 33 by 210 ms (both joins alike). Not yet in a Windows build. Live: the ramp is CC11 events like any level, so the
route set's carrier notes and the plain set's CC11 lane carry it (`tst_liveequivalence::transitionLevelsLive`). Tests
`tst_soundlibrary::transitionLevels`.

**2: every step a transition brings** (branch `bowed-join`, 2026-10-28; `libraryTransitionTails` in rendermidi.cpp, run after
the levels and even levels, before the track delays). The owner on the Hall Guard A/B page (hall.musicxml, build 485, both
A and B): "both have the problem of the tail of the last note suddenly get louder as the next note enters". 1 moves only
CC11 levels; the dynamics CC (SSO: CC1) that a transition starts on goes with its note-on too (the written dynamic, moved
with the note-on by its early start), and so does any CC11 step at the note-on (real range following the new dynamic,
level rule, divisi, even levels' switch). In hall.musicxml under today's Recommended (plainAuto: each unslurred touching
note a bowed transition, 93, sent 65.8 ms before its beat by plainShift; heard ~191 ms after its note-on, plainShift +
bowedEarly; bendGlide leaves the note before 124 ms after it): bar 2 and bar 5 (p into f) CC1 48 -> 96 and CC11 54 / 56 ->
82 with the note-on, on Violins 1 - Performance's measured curves +8.5 dB (CC1, `dynamics.json` 48: -44.4, 96: -35.9) and
+3.8 dB (CC11), about +12 dB on the p note's end and tail for the ~124-190 ms before the f note is heard; bar 3's first
note CC11 73 -> 87 (+1.6 dB); bar 1 beat 3 and bar 5 beat 3 (into p) the same steps down. With 2, at each legato
transition (`libGlideFrom`, its glide `libGlide`) whose note-on steps the dynamics CC or CC11 on its route (the value at
the note-on's tick against the one before it), that step alone is held back: the controller keeps the note before's value
to the glide's leave, then the step comes in linearly (CC11 linear in dB, the dynamics CC in value), a value every 5 ms
where it changes, complete at the glide's arrival (at the latest at the route's next note-on, where what is left of it
goes and the next transition holds it in turn). What moves after the note-on (a hairpin's steps, even levels' glide)
moves as before, the step's remainder on top; from the arrival everything as rendered before. Both directions: a drop at
a subito p inside a slur too (the note before keeps its level while it sounds; as a ramp of 1-2 values every 5 ms it no
longer fires SSO's Hall trigger: hall.musicxml's audit HALL 3 -> 1, the fp of bar 4 on its held note left, which is what
`[levels] hallGuard` is about). Attacks stay `[levels] attackLevels`' (no transition, no glide).
hall.musicxml (Linux dump, Recommended, 0 against 2): 8 of 12 note-ons change their CC1 / CC11 schedule, no note-on, note-off
or other event changes; bar 2: CC1 48 to 124 ms after the note-on, then 50 … 95 by 263 ms, CC11 54 -> 82 alongside.
Whence (quarter tones, Recommended, the audit's dump, 0 against 2): 47 of 454 note-ons change (Celli 21, Violas 13, Basses
10, Violins 1 3), notes unchanged; audit counts unchanged except LEVEL STEP info 12 -> 26 (values read at the note-ons, now
the note before's: the steps it lists are each 0.5-1.6 dB, held over the glide). Not measured on the VM; not heard. Live:
CC1 and CC11 events like any level (`tst_liveequivalence::transitionTailsLive`: the route set's CC1 carrier notes and the
plain set's CC1 lane). Tests `tst_soundlibrary::transitionTails` (tail-levels.musicxml).

## Attack levels

`[levels] attackLevels` (branch `attack-levels`, 2026-10-22; **2 by default and in Recommended, 0 in Library default since
2026-10-23**: the owner, "B better", Whence's Violas bars 11-13 alone with a click, even levels on, kthost renders, 2 against
0; per score). The attack counterpart of Transition levels above. Every per-note level by CC11 goes with the note's
note-on (`libraryNoteLevels`), and before an attack is heard that only lifts or lowers what still rings of the note
before. With 1 or 2:

- where a note-on is an attack (no legato transition: not in `libGlideFrom`) with a measured onset (`libAttackOnset`: a plain
  Performance note its patch's `<Instrument plainAttack>`, the Violas' 37.1 ms, or an attack with its own onset (smooth) by
  that one; any other library note its articulation's onset, the chord's latest, as its early start takes it),
- and the note before on its route (copy) still sounds or ended less than that note's release ago (`libReleaseMs`: the map's
  `<Articulation release>`, measured to 30 dB under, the Violas' Performance Legato 2620 ms; else its patch's longest),
- its CC11 level (level rule, real range, track and note levels, divisi; with even levels, the glide's start too:
  `libAttackLevelAt`, and the run before ends there) goes, instead of with the note-on,
  - **1**: at the note-on + that onset (the measured onset needed);
  - **2**: at the note's **written time**, where its attack is meant to be heard: the note-on's own early start undone
    (onset early: the written start in its pass less the note-on) plus, for a plain Performance note, its patch's
    `plainShift` (`libAttackShiftMs`: the track delay that moves the patch's events early after this pass; not with
    `[slurs] plainTiming`, whose notes start early each by their own model). The Violas: 111 ms after the note-on. The
    attack's first part (SSO's Violas sound from 37 ms after the note-on, 74 ms before the beat) then plays at the note
    before's level, growing into its own at the beat; **rises only**: a drop goes with the note-on, as 0 plays it
    (check471, 2026-10-23, build #471 on the VM, MuseScore's export: held to the beat after an accent at velocity 122, a
    drop let the new attack's start play at the accent's level, Whence's Violas bar 8 beat 4.5 +4.7 dB and Celli bar 9 beat
    1 +5.9 dB over #470 in the 100 ms before the beat; the owner had heard #470 there: "everything else in Whence sounds
    perfect");
- only if that moment stays before the route's next note-on / note-off and before any CC11 or dynamics CC the score writes on
  the route (those are mapped by the level in force), inside the render chunk; CC11 levels only (a level on another
  controller, a marcato's CC1, switches at the note-on). (Live playback's chunks render inside a window since chunk-equal,
  2026-10-25: the note before is in the window, as in the whole render.)

Why: Whence's Violas bar 12 (the owner, #470: "when the slur starts. the volume just like jumps in a disjointed way";
`private/violas-slurstart`): the slur's first note is a plain attack after a staccato G4+50 on the same key and copy, 116 ms
gap; its CC11 step (the level rule's slur gain, 86 -> 110) went with its note-on, which plainShift sends 111 ms before the
beat, and raised the staccato's release +3.4 dB before the beat (+6.6 with even levels), a level change with no note under
it. The levels themselves are unchanged; only their timing moves. Measured (kthost of build #470 + Kontakt 8 + SSO on
the VM, the Linux dump's Violas routes, Recommended with 0 / 1; the perceived envelope's change over the 100 ms before the
slur's beat / over the attack, -120 -> -20 / -20 -> +60 ms, the beat = note-on + plainShift): bar 12 +3.7 / -0.5 with 0,
+3.6 / -0.4 with 1; bar 16 +0.9 / +4.9 and +0.6 / +5.3; with even levels (violas-slurstart's measured values) bar 12
+8.0 / -1.4 and +7.9 / -1.2. So 1 takes away only the first 37 ms of lifted tail (about 1 dB at -100 .. -60 ms); the rest
of the rise before the beat is the attack itself, which SSO sounds from its onset, 74 ms before the beat, at the new level.
The level at the written time (2; measured as a map copy with plainAttack 111 before 2 existed) moved it:
bar 12 +1.2 / +1.7 (even levels +1.3 / +4.4), bar 16 -1.6 / +7.4, the other slur starts' rise before the beat 1.8-2.4 dB
smaller and their attack's 1.7-2.2 larger. The owner heard 0 against it (the clips' A / C, even levels on, shown to him as A / B):
"B better" (2026-10-23). Whence, Recommended, 0 against 2 before rises only (the Linux dump): 35 attacks' CC11 levels move, each by its
patch's plainShift (no other early start there): Violas 18 (+111.4 ms; bars 8-16, every slur start and the staccato runs'
first notes), Violins 2 8 (+65.9; bars 7, 8, 13-16), Violins 1 5 (+65.9; bars 13-16), Celli 2 (+80.7; bars 9, 16), Basses
2 (+59.1; bars 8, 9); nothing else changes (notes, switches, bends; a value equal to the one in force isn't sent again).
The audit (it reads each note's level at its note-on) then lists 12 LEVEL STEP infos: the second note of each slur, Violas bars 9-16
and Violins 1 bars 13-16, read against the first note's level still the note before's. Live: the moved level is
CC11 events like any level, so the route set's carrier notes and the plain set's CC11 lane carry it
(`tst_liveequivalence::attackLevelsLive`: 1 and 2; the plain set's lane at written times, the Kontakt's Track Delay playing
plainShift). Build #471 (2 moving drops too; VM, private `check471/RESULT.md`): bar 12's rise before the beat +3.4 -> +1.1 dB (even
levels +6.6 -> +1.1), the step's size unchanged, Violins 1 bars 13-16 3.0-3.8 dB less before the beat, the two drops above
worse, timing and tuning identical. Rises only (Linux dumps, Whence, Recommended, 0 against 2): 19 CC11 levels move (Violas
11, bars 9-16; Violins 1 4, bars 13-16; Violins 2 2, bars 7 and 16; Celli bar 16; Basses bar 9), the drops at Violas bar 8
and Celli bar 9 as with 0; the audit the same (LEVEL STEP 12 info). Tests `tst_soundlibrary::attackLevels` (0 / 1 / 2, 2's drop with its note-on), `playbackPresets`; `Playback::setIniValuesForTest` leaves it
0 unless a test sets it, so the older tests keep their expectations.

## Staccato length

`[shorts] staccatoLength` (branch `staccato-length`, 2026-10-24; the owner: "why not", to try by ear; **1 by default and in
Recommended, 0 in Library default, since he heard the A/B clips the same day** (https://claude.ai/artifact/DvMdsMtTigjhLzztAjnTSz:
each section alone with a click, `share/verifyplayback/Staccato lengths.musicxml`, Recommended with 0 (A) or 1 (B), MuseScore's
export of real SSO on the VM, build #476): Violins 1 "B for the first set, same for the next set", Celli "B", Oboe "same", Bassoon "same"; the first set is bars 1-2's quarters at 60 bpm, the next bars 3-7's
slow eighths and the 120 bpm quarters and eighths; a test's ini leaves it 0 unless it sets it, `Playback::setIniValuesForTest`); the readings it leans on: his review of 2026-10-24,
https://claude.ai/artifact/GNwFDt1PcNHJ1UWoWugvDc, R01-R05, R12-R17, R19-R24 right, R06-R11 and R18 "can't tell": only the
measurements resting on confirmed readings set the values).

**Why.** MuseScore 4 holds a staccato for 50 % of its written length (staccatissimo 25 %) at every tempo, and the library
chooses a short by that meant length (`[shorts] byMeantLength`, `from`). Real players sound a staccato for about a fixed time
instead (RESEARCH.md 2.5a, `tools/soundlibraries/staccato_lengths.json`): winds about 0.10-0.16 s, strings about 0.15-0.22 s
to -20 dB under the note's peak, whether the note is written 0.2 or 0.36 s long. So a slow staccato plays much longer in
MuseScore than with players: a Violins 1 staccato quarter at 60 bpm is meant to sound 0.5 s and chooses Short 0.5.

**1.** A library note with a staccato (staccatissimo; not a portato) on a patch with `<Instrument staccatoLength>`
(`staccatissimoLength`):
- is **meant** to sound the shorter of the written length times the factor and that time: the short technique is chosen
  against `from` by it (a slow staccato on Spiccato, not Short 0.5);
- is **held** no longer than that time from its written start (the note-off: `config.ms4Dur` capped; an early start by the
  onset leaves the note-off where it is, so it sounds about that long; a plain Performance staccato's spiccato attack
  alike, plainShift moving the whole patch).
A note already shorter (a fast staccato) is unchanged. Both of MuseScore's renders take it, so Live's clips and the plain set
hold the same note-off (`tst_liveequivalence::staccatoLengthLive`).

**Values** (map, generated: `gen_spitfire_sso.py` `STACCATO_RULES` from `staccato_lengths.json`, seconds):

| family | staccato | staccatissimo | source |
|---|---|---|---|
| Violins 1 / 2, solo violins, Strings Ensemble | 0.19 | 0.16 | mean of TU Beethoven 8 / IV Violin I (≥ 0.155) and Spheres K. 550 Violins 1 (0.22) |
| Violas, Solo Viola | 0.19 | 0.16 | the violins' (assumption: not measured) |
| Celli, Basses, Solo Cello | 0.22 | 0.16 | the larger violin value (assumption: their staccato eighths never part by 20 dB; low strings ring longer) |
| Oboe, cor anglais | 0.115 | 0.115 | TU Beethoven 8 / II staccato sixteenths, bars 1-2 at pp (confirmed readings): one oboist, 15 notes, 90 % [0.115, 0.125] |
| Clarinets (bass, contrabass) | 0.105 | 0.105 | the same: 2 players, 30 notes, [0.090, 0.110] |
| Bassoons, contrabassoon | 0.095 | 0.095 | the same: 2 players, 18 notes, [0.080, 0.105] |
| Horns | 0.16 | 0.16 | the same: 2 players, 26 notes, [0.155, 0.160] |
| Flutes, piccolo | 0.115 | 0.115 | the four winds pooled, 89 notes, [0.100, 0.155] (assumption: no flute measured) |
| Trumpets, trombones, cimbassi, tubas | 0.16 | 0.16 | the horn's (assumption: no other brass measured) |

Staccatissimo: 0.16 s (Hähnel & Berndt 2010, 161 ms over strings, winds and brass; no staccatissimo in the recordings: the one
value from the literature, an assumption), never longer than the staccato. The winds' values are measured at pp only (in the
unconfirmed bars 18-22, cresc. to f, 10-30 ms longer); Hähnel & Berndt's staccato, 173 ms (to a 66 % loudness drop), is a
cross-check only. Percussion, harp, keyboards: none (as MuseScore 4).

**Uncertainty.** One performance per passage, players ±30-50 ms; the strings' -20 dB includes the free string's ring; SSO's
own shorts sound about twice as long as real notes at -10 dB perceived (with the hall; the Close mic not measured), so the
note-off caps what MuseScore sends, not what the sample rings. Not done: a note before a rest (proposal b), the 45 ms spiccato
clearance (d).

Tests: `tst_soundlibrary::staccatoLength` (the map attributes and the shipped values, choice and note-off with 0 and 1, a
portato / unmarked / short staccato unchanged, the default 1, Recommended 1, Library default 0), `tst_liveequivalence::staccatoLengthLive`.

## Clicked and entered notes

The owner (2026-10-15): "send a clicked note on a library part to its SSO patch, with its technique and the same patch
volume and levels playback uses, so a click sounds like that note does in playback, in tone and level"; "the clicked
note should match the global playback library". A note clicked, selected with the keyboard, entered or typed (and a
chord added with *play chord*) goes through `MuseScore::play` → `Seq::playNotes` (`libmscore/notepreview.h`). The part's
effective mode (its own, `partplayback.h`, else the global one) decides, read at every click:

| Effective mode | The click |
|---|---|
| MS3 | MuseScore 3.6's click, unchanged: velocity 80, the single-note-dynamics controller at 80 |
| MS4 (and a part set to the library that the library lacks) | the note's MS4 note-on (velocity by its dynamic, MS4's channel slot) with CC11 and the preset in force on that channel |
| Library | the note's library note-on on its route (patch, varispeed lane or bend) with the switch, CC1, CC11 and every controller and bend in force there |

So every adjustment in this file that playback applies to the note applies to its click: the technique, calibrated
velocity, the level rule, section balance, real range, mic compensation, even levels, track levels, the Inspector's
levels; the patch volumes are the slots' (hosted: `SoundLibraryHost::applyMixer` keeps them every 50 ms, also while
stopped). No early start: the note-on goes at once, ending after the click duration as before. The events come from
rendering the note's 10-measure playback chunk on a renderer of the click's own (kept until an edit, a metaTag, the
library or the mode changes). Not changed: while playing (a click plays as before), while *Live plays the score*, a clip
tab playing through its Live track, the piano roll's and drum roll's keyboards and the drum palette (`seq->startNote`).
Hosted, a route whose patch isn't loaded yet (a technique added since the score's last load) stays silent and the
host loads the score's patches in the background; the next click plays it.

## Several libraries (engines)

Not a setting: the owner, 2026-10-26, "natively integrate Vial Instrumental as another playback engine, like we have now
for ms3, ms4 and sso" (branch `vial-engine`; share/soundlibraries/README.md › Several libraries). A part may play
another library than the global one (metaTag `partPlayback` "library:<engine>"). Every adjustment in this file is
read from the note's own library's map: a Vial part gets none of SSO's measured layers but its levels (no calibration,
onsets, level rule, mic compensation, even levels: Vial's map has none; section balance and real range: › Vial levels),
an SSO part keeps all of them
when Vial is the global library (its calibration read as the global one's would be: `SoundLib::calibrationFor`). The
master trim (› Gain stage and master trim) is measured on the score's first library with a crest and lowers every
library patch, Vial's too (one master level). Even levels' measurement, the audit and the verification: the global
library's parts only. A score that plays one library renders byte for byte as before (Whence: `dumpEvents` identical,
2026-10-26).

## Even keys

2026-10-28 (branch `bowed-join`; the owner, 2026-10-28: SSO's uneven keys are an artefact, every pitch at the real player's
level, smoothed only beyond the scatter real players show; he approved the approach). `[levels] evenKeys` (1 by default and
in Recommended, 0 in Library default; per score). **Why**: the woodwinds' and brass' Long swept every semitone
(`sso_winds_cc1_semitone.json`, `sso_gap_cc1_semitone.json`: VM, Kontakt 8 + SSO, kthost, 8 CC1 points 16-127, 4 takes)
plays in zones, mostly two semitones a sample, whose levels jump up to ~10 dB from zone to zone: Oboe Solo 69-70 and 75-76
4-7 dB over the line through their neighbours, 71-73 and 79-80 3-7 dB under; the 3-semitone sweep (`sso_winds_cc1_curves.json`)
fell between them, so a real export of the winds A/B (`winds_ab.mscx`) played Oboe 72 5-8 dB under what MuseScore and the
Vial match predicted. The semitone sweep agrees with the 3-semitone one at every shared pitch (median 0.00 dB, largest 0.28
in loudDb; some patches swept on their `- Long` patch, the same samples), except Solo Violin 2, which the sweep played on Solo
Violin 1's patch (left out: its 3-semitone curve stays).

**Method** (`tools/soundlibraries/even_keys.py` -> `sso_even_keys.json` -> `gen_spitfire_sso.py` -> `<Articulation
keyLevels="pitch:dB …">` on each patch's Long, UACC 1): per patch a key's deviation d = its Long's loudDb at CC1 64 (the
median of 4 takes = SSO's two round robins' mean) less the Theil-Sen line (median slope, robust to a zone of outliers)
through the 8 nearest other keys, at the key. **Threshold T = 1.5 dB**: the 80th percentile of |d| by the same estimator over
the TU Berlin database's ff tones (Weinzierl et al. 2018; the twelve wind and brass instruments of `real_range.py` WINDS,
478 keys: robust SD 1.08, rms 1.33, p80 1.47, p90 2.02 dB); one take a pitch, so take noise is in it: an upper bound of real
key-to-key structure. Not the pp tones ("as softly as possible": clarinet 6.7 dB rms, near the player's limit). The key
plays c = -sign(d) max(0, |d| - T) dB more: only the part beyond real players' scatter is taken away (a key 6.6 dB under
the line plays 1.5 under it; one 1 dB off stays). 34 patches, 4-22 keys each; rms deviation 1.0-3.8 dB before, 0.8-1.3
after. Largest: Contrabass Clarinet 47-48 +8.4 / +8.9 (10 dB under their neighbours at mp, not at ppp), Oboe Solo (real shape,
below) 76 -5.9, 69 -5.6, 77 -4.9, 73 +3.8, Bassoons a2 55-56 / 67-68 +4.5 .. +5.5, Trumpets a2 79-80 -4.8 / -5.0,
Contrabassoon 49 -4.7, Clarinets a2 74 -4.5, Cor Anglais 81-82 -4.4, Bassoon Solo 73 -4.2, Flute Solo 69-70 -3.6 / -4.2,
Horn Solo 66 -4.1; the a6 sections and Trumpet / Trombone Solo nearly even (4-7 keys, at most 1.6 dB).

**Played with `[levels] realRange` only** (Recommended's 2), as part of its level: `realDb + c` by CC11, inside the
part's R (`LibLevel::keyDb`, rendermidi.cpp: a raised key gets headroom, the patch's gain stage plays it), so Live's clips,
the plain set and MIDI out carry it as every realRange level. Under realRange a note plays SSO's Long at CC1 64 at its key
plus the player's change from mp (longLevel takes SSO's own change away, **every semitone since 2026-10-28**: `real_range.py`
reads the semitone sweeps, a comma joining files; before, between the 3-semitone points longLevel was interpolated, up to
2 dB off at the 90th percentile and 6-13 dB at single points), so the key's mp level is all that is left uneven: c comes
from CC1 64 alone (each CC1 point's deviations are in `sso_even_keys.json` › byCc, for the record). Only the Long (the swept
articulation); other techniques play SSO's own keys.

**Not corrected / uncertainty**: the round robins alternate note by note (|A - B| at mp per key: median 0-2.2 dB per
patch, up to 9 dB at single keys; `rr` in the file), no per-key curve can even them: the mean is the target. A short note
before a slow attack (SSO's Long onsets 70-140 ms on the Oboe's 69-76) reaches less than loudDb says; the zones are the same
by loud1sDb (first second) and rmsDb (the sustain) within 0.3-1.2 dB rms. **Real players corroborate part of the Oboe's
pattern**: SSO's deviations correlate with TU Berlin's oboe at ff (r +0.73 Oboe Solo, +0.62 Oboes a2; one player, one take),
whose 72 is 5.4 dB under the line (pp 3.0) and 75-76 +3; SSO's swings are about twice as large. **So the two oboes take the
real oboe's key shape** (the owner, 2026-10-28: two independent sources agree in direction): their target is the line plus
TU's own deviation at ff at each key (`even_keys.py` REAL_SHAPE; each patch's `target` in `sso_even_keys.json`), not the
line within T; keys TU lacks the line within T. Uncertainty: TU is one player, one take a pitch, its deviations carry take
noise (real players' scatter at ff 1.1 dB robust SD). Oboe Solo 69-77: -5.55 -2.9 -0.27 +1.29 +3.75 +3.07 -3.35 -5.88 -4.89
(capped at T they were -4.9 -3.81 +1.36 +5.15 +3.57 +1.48 -4.81 -7.09 -5.22): 72 now plays 5.4 under the line as TU's
oboe, 76 2.7 over it. Every other patch (correlation -0.5 .. +0.46) keeps the line within T.

**Vial** (› Vial levels): its presets are even over pitch (within 0.1 dB at CC1 64), so its `longLevel` is matched to SSO's
keys as SSO plays them (every semitone) and its Long carries the SSO Long's `keyLevels`: with evenKeys on both play the
evened curve, off both play SSO's keys.Vial's eight wind presets swept every semitone too (`vial_levels.py sweep`, Linux kthost, 2026-10-28; their old
3-semitone points again within 0.06 dB; Theil-Sen deviation at mp rms 0.02-0.19 dB), refitted (`fit`): the offsets
(mean SSO - Vial at mp over every semitone) moved by -0.44 .. +0.26 dB: Flute -2.34 -> -2.15, Oboe +5.33 -> +4.89, Clarinet
-1.48 -> -1.37, Bassoon +9.82 -> +9.59, Horn +7.53 -> +7.79, Trumpet -0.21 -> -0.40, Tenor Trombone +3.34 -> +3.13, Tuba
+9.54 -> +9.37 (the strings unchanged).

Test `tst_soundlibrary::evenKeys` (parsing, realRange 1 with and without, realRange 0, a raised key in R, the shipped maps).

## Vial levels

Not a setting of its own (branch `vial-levels`, 2026-10-27; the owner, 2026-10-26/27: a Vial part as loud as the SSO part
at the same written dynamic, "measured, not by ear"): the Vial maps' 13 instruments carry the SSO instrument's
`realSteps` / `realSpan` / `realPlay` (the same players' data), their own `longLevel` and SSO's `sectionLevel` /
`sectionLevel2` moved by a measured offset, so `[levels] realRange` and `sectionBalance` (Recommended 2 / 2, Library
default 0 / 0: Vial then plays CC11 127 and no gain, as before) play a Vial Long at SSO's Long's level at every marking.
Made by `tools/soundlibraries/vial_levels.py` (its header is the method): `sweep` renders each preset by kthost
(Linux, Vial c29c49e) with `sso_cc1_sweep.py`'s events at SSO's swept pitches (the winds every semitone since 2026-10-28),
CC1 16 .. 127 (`vial_cc1_curves.json`, one take a point: Vial has no round robins); `fit` -> `vial_levels.json` -> `gen_vial.py` (`OFFSET_MEASURE`); `check` renders every preset
at ppp .. fff at three pitches as MuseScore sends it under Recommended from the generated map
(`vial_levels_check.json`). **Matched on loud1sDb since 2026-10-29** (the loudest 50 ms of a note's first second; the owner, 2026-10-29: "Most
notes in real music are shorter than a second, and the start of a note largely sets how loud it seems ... It fits better
overall (all 13 instruments matched on average; under loudDb up to 2 dB off: Basses 2.1, trumpet 1.9). It fixes the
trumpet completely." - "sure"). Cause (debug of 2026-10-29, played notes rendered through MuseScore): loudDb takes the
loudest 50 ms of a 3 s held note, where SSO's oboe and trumpet still swell after 1.5 s and Vial's presets hold their level,
so a match on the peak made Vial too loud (trumpet +0.9 .. +1.3 dB, oboe +1.1 .. +1.4) at the lengths music plays. Offsets
O (sectionLevel2, old -> new): Violins 1 -0.03 -> 0.36, Violins 2 -0.60 -> -0.69, Violas -4.05 -> -3.72, Celli 3.38 ->
3.59, Basses 0.31 -> 1.01, Flute -0.19 -> -0.54, Oboe -5.41 -> -6.56, Clarinet -4.36 -> -4.45, Bassoon -5.45 -> -6.21,
Horn -0.04 -> -0.63, Trumpet 4.23 -> 2.77, Tenor Trombone 2.93 -> 2.28, Tuba 8.40 -> 8.39. **Cost: on long held notes
SSO's oboe swells, so Vial's oboe ends up quieter late in a long note** (its ~+2 dB residual in the debug was SSO's round
robins). The test reads loud1sDb (the shipped maps within 0.5 dB); the earlier check on loudDb (largest error 0.53 dB,
Flute, strings <= 0.4) was not rerun with the VM-free renderer. **Uncertainty: other loudness
measures judge the winds louder on Vial**: by the sustain's power +1.8 .. +4.1 dB, LUFS +1.8 .. +3.9, phon +2.1 .. +7.1
(the presets hold their level, SSO's solo samples peak and decay); the strings within +-1.2 dB (phon -1.8 .. -0.5). Test
`tst_liveequivalence::vialLevels`. Since 2026-10-28 the wind presets' Long also carries the SSO Long's `keyLevels`
([levels] evenKeys, › Even keys): longLevel matched to SSO's keys as SSO plays them, every semitone, then both evened alike.

## Inventory

Not in the table, because they are corrections and not adjustments (no setting, the same in every preset): a percussion
patch's `maxVelocity` (the 13 metal patches whose Kickstart option "Roll on high vel." is ticked are sent velocity 126 at
most: 127 would play a roll the score did not ask for), a patch's `avoidCC` (SSO's Trumpet / Tenor Trombone Solo - Total Performance never get CC112 / CC113, which their helper script would take as the note's velocity: `MidiRenderer::libraryAvoidCC`) and the transport restart (the hosted plug-ins see a transport
start with every playback, export and verification pass, so SSO's round robins reset as they do in Live).

Ini key = `[section] key`. "Map" = per-patch data in `share/soundlibraries/Spitfire Symphony Orchestra.xml`, made by
`tools/soundlibraries/gen_spitfire_sso.py` from the measurements (its comments say how each was measured).

| Adjustment | Default | Where it acts | Configure |
|---|---|---|---|
| Phrase marks: a slur that plays no legato | per slur | `slur.h` Phrase marks, metaTag `phraseMarks` | per slur (Alt+S, Inspector) |
| Level (marcato only until 2026-10-08): an articulation sign, or a staff text that changes the technique, louder or softer than the technique plays (above, Calibrated levels). A note on velocity: the velocity at which its measured curve (dynamics.json) is that many dB away, else SoundFont 2's law 40 log10(v/127); a note on the dynamics CC (SSO's strings' Marcato Attack): softer by CC11 (the held note's expression curve, else the law), louder by CC1 (its own curve, else the law; up to 127), on its route from right before its note-on to right before the route's next note-on; built-in synthesizer (MS3 and MS4 modes): the velocity by the law | "Library default" (0 dB: no change) | `articulation.h` MarcatoLevel, `rendermidi.cpp` (`levelOf`, `marcatoLevel`, `libraryNoteLevels`, `playNote`), `SoundLib::TextState::db`, metaTag `marcatoLevels` | per sign: Inspector › Articulation › *Level*; per text: Inspector › Staff text › *Level* (−35.5 … +35.5 dB, 0.5 steps; several selected: all; the range: above). Until 2026-10-08 −18.5 … +12.5, what SSO's marcatos span (`derived_numbers.py marcato`) |
| Calibrated levels: 2 (2026-10-15, option B'): a technique on velocity as loud as the part's held note at the same dynamic plus D (real players' difference, map `heldOffset` from `technique_offsets.json`; else its level at velocity 127 less the held note's at 127), plus MuseScore 4's offset for its articulations (marcato's too); with the level rule every slur gain + its section's Spiccato D; 1: without D; 1 and 2: a Performance legato note by CC1 as loud as the held note (2026-10-10) | 2 (Recommended; Library default: 0) | `rendermidi.cpp` `libVelocity`, `SoundLib::calibratedVelocity`; data: dynamics.json per patch, else the shipped `Spitfire Symphony Orchestra.dynamics.json` | `[levels] calibrated`; score: `playbackSettings` |
| Track delay: a library part (and each of its patches and techniques, added to the part's) plays this many ms later, negative earlier, as Live's Track Delay; every library event of the track moved in time, the tempo followed; with a negative delay everything else (other parts, the metronome) plays later by the earliest one (`libraryDelayLead`), so the track is early from its first note (2026-10-07; before: clamped at the start); likewise a note whose own early start (onset) the score's start would cut: everything later by the most cut, the start lead (not a setting; 2026-10-14); a note and its switch by the technique's sum, and so do the controllers and bends right before it at its tick (its track level's CC11, a dynamic; 2026-10-08), other controllers by the patch's | 0 ms (none) | `trackdelays.h`, `rendermidi.cpp` `libraryTrackDelays`, metaTag `trackDelays` | per part: Mixer › *Track delay*, *Tracks…* (−1000 … 1000 ms: a tutorial's figure for Live, its manual gives none); the plain Live set writes it as TrackDelay and reads it back |
| Track level: a library part's patch and technique each play this many dB softer or louder (added up), by CC11 (expression) from right before each of the technique's notes, the value in force again before the next note without one; adds to a marcato's level (2026-10-07). Louder (2026-10-08): the patch's loudest level is its headroom, played by its volume (hosted: the slot's; Live: the Kontakt track's Volume), its notes' CC11 that much lower | 0 dB (none) | `trackdelays.h` (`headroomDb`, `noteDb`, `patchGain`), `rendermidi.cpp` `trackLevel` / `libraryNoteLevels`, `soundlibraryhost.cpp` `applyMixer`, `LiveSetWriter::kontaktVolume`, metaTag `trackDelays` (`levels`) | per part: Mixer › *Tracks…* › Level (−42.08 … +6 dB: 1 of 127 is −42.08 dB; +6 dB is Live's track Volume top, the owner's choice 2026-10-08); in Live's clips and the plain set's CC11 lane alike |
| Legato transition velocity: a legato transition plays at the map's velocity, whatever the note's (Spitfire's Performance legato picks the transition by velocity: fast 85-127 "with accent", slow 85-127 bowed; Spitfire support article 11815986). Measured 2026-10-07 (Violas - Performance, Whence's violas bars 3-7, 80 slurred sixteenths at 110 bpm, three passes, kthost on the test VM): arrival - written SD 49 ms at velocity 64, 41 ms at 100 (37-38 against 45-46 on the notes heard in every render); 159 of 180 transitions detected against 133; median arrival 68 ms against 88; median note peak 1.5 dB lower. Per-note shifts made it no tighter (HANDOFF.md). Same passage on every family (moved by octaves near each patch's test note), SD at 64 / at 100 (ms): Violas 54 / 39, Violins 1 53 / 65, Celli 53 / 70, Basses 39 / 65, Flute 35 / 65, Trumpet 26 / 35; Oboe 29, Clarinet 31, Bassoon 98, Horn 29, Tenor Trombone 31, Tuba 55 render the same at both. So only Violas - Performance gets 100; the others keep the note's own (Violins 1 at 30 / 50 / 84: 53 / 50 / 53). Only under staff text "performance" (SSO) | map `legatoVelocity` (SSO: Violas - Performance 100; other maps: the note's own) | `collect` (`libTransitionFrom`) | map only (`[legato] velocity` removed 2026-10-07) |
| Legato delay per patch and interval (times an unmeasured patch's bent transition's glide: `[tuning] bendAtArrival`) | map `legatoDelay` (5-pitch grid; octaves by template fit) | `Articulation::legatoDelayAt` | `[legato.delay]` table |
| Octave slurs (±12) by start pitch (16 patches, measured at ~16-30 starts each; 2026-10-02, option C) | map `octaveUp` / `octaveDown` (start MIDI pitch:ms; failed fits left out); unmeasured start: the nearest measured one, a tie (one-semitone gap) to the side whose run of like values (±50 ms, adjacent) is shorter, else the lower; unknown start: the table's median; patches without them: `legatoDelay`'s ±12 | `SoundLib::octaveDelayAt` via `legatoDelayAt(interval, fromPitch)` | `[legato.delay]` offset (adds) or table (its ±12 replaces) |
| Oboe Solo +60 (not its +12 per-start octave values), Violins 2 +25 ms legato corrections; Violins 2 octave slurs −30 / −30 more (+12 / −12; the octave sweep of 5698181 heard both 30 ms early) | map (generator `SWEEP_LEGATO_CORRECTION`, `OCTAVE_NO_SWEEP_CORRECTION`, `OCTAVE_SWEEP_CORRECTION`) | map data | `[legato.delay]` offset |
| A held note started early by its onset leaves the note before on its patch at least this much of its length as played (it may have started early itself) | 40 ms (chosen by a sweep: below) | `onsetEarliest` (`libPlayedOn`) | `[legato] keepMs` |
| A transition after a short note arrives sooner, by a share of the measured delay: 50 % after a very short note, rising linearly to all of it after a 380 ms note (fitted: below); times the bent transition's glide | 50 %, 380 ms | `libFastDelay` (`libGlideDelayMs`) | `[legato] fastShare`, `fastFullMs` (0: always all) |
| Legato glide of a tuning copy. Pitch bend: one cent a tick, the bend value nearest each step (a glide of d cents: ceil(d) ticks; a quarter tone ~52 ms at 120 bpm): the owner's criterion (2026-10-03) "as short as possible without audible steps: each step at most one cent" on the renderer's tick grid; no setting. Varispeed: the same criterion per output frame (it changes its speed every frame): a glide of d cents takes ceil(d × the larger ratio / the smaller) frames, each within a cent (a quarter tone 51 frames, ~1.2 ms at 44.1 kHz; `Vst3Plugin::GLIDE_CENT_STEP`); `[legato] glideMs` (30 ms, no source) retired 2026-10-03 | computed | `libraryPitchBends`, `Vst3Plugin::setPitch` | — |
| Held notes, and since 2026-10-08 every technique with measured onsets (shorts, marcato, tremolos, trills, falls; not rips, effects, harp, keyboards, percussion; the owner: "is there a reason not to make this the recommended?" after slurred shorts stayed on the beat while Longs moved), start early by their measured onset. Medians across patches (latency removed): pizzicato / Bartók / col legno 3, staccato / spiccato 8, Short CS 26, Marcato / Tenuto 33, Short 0.5 61, Short 1.0 113, Long 86 ms | 100 % (map `<Onset early>`; 0 from 2026-10-06 to 2026-10-07; the owner: plain Long for every slurred note, lined up between sections; 0 plays them as written, to compare) | `collect` / `onsetEarliest`, `finishLibraryEvents` | `[heldNotes] early`; score: old metaTag |
| Onset per patch and pitch (-15 dB perceived; swells per semitone) | map `onset` | `Articulation::onsetAt` | `[heldNotes.onset]` table; definition: generator |
| Notes early by their articulation's median onset, or each by its pitch's onset (2026-10-07: even runs; the default since 2026-10-08; notes `[slurs] quick` swaps too since 2026-10-09) | 0 (median) | `collect` (`libOnsetByPitch`), `Articulation::onsetMedian` | `[heldNotes] byPitch`; score: `playbackSettings` |
| Shorts chosen by meant sounding length | on | `SoundLib::want`, `choose` | `[shorts] byMeantLength` |
| Meant length factors | staccato 50, staccatissimo 25, tenuto 99, portato 74.5 % (MS4's) | `want` (`Want::soundSeconds`) | `[shorts] staccato` … `portato` |
| Measured short thresholds | map `from` | `choose` | `[shorts.from]` table |
| Staccato length, `[shorts] staccatoLength` (branch staccato-length, 2026-10-24; the owner heard the clips: Violins 1 "B for the first set, same for the next set", Celli "B", Oboe "same", Bassoon "same"): a library staccato (staccatissimo) is meant to sound and held no longer than real players sound one, the map's `staccatoLength` / `staccatissimoLength` by family (above, Staccato length) | 1 (Recommended 1, Library default 0) | `SoundLib::want` (`staccatoKind`), `chooseBases`, `rendermidi.cpp` (`config.ms4Dur`) | `[shorts] staccatoLength`; score: `playbackSettings` |
| (In Recommended only under staff text "non performance": `[slurs] performance` takes the strings' slurs; a Whence dump, 2026-10-12, has no Long (Rachm.).) Slurred notes shorter than their held technique's measured peak (map `peak`, the median over its semitones of the onset check's mf perceived peak: SSO violins' Long ~1.06 s, violas' 0.77 s) play a quicker technique of the same patch with the note's modifiers: 1 the tenuto short (Short 1.0, its `from` not applied: the slur's note-off ends it), 2 the espressivo long (Long (Rachm.)); none fits: the held one; early by the swapped technique's own onset, by `[heldNotes] byPitch` as every note (`Choice::swapped`; until 2026-10-09 always at each pitch: the owner then heard Whence's first bars uneven, its Violas / Celli per-pitch in-context fit spreading the sent times 86-109 ms; byPitch 1 keeps that: Long (Rachm.)'s per-pitch fit, `sso_rachm_onset_fit.json`, Windows VM 2026-10-08: on Whence's violin passage arrivals SD 10.8 / 11.4 ms for Violins 1 / 2 against 22.0 / 22.6 on its median and 21.6 / 26.5 on Long's; the owner, 2026-10-08: "the timing is still not as good as plain long"). The owner, 2026-10-08: slurred violins had no attack (Whence's slurred notes 136-273 ms); 2026-10-08, after comparing by ear: "let's make slurs quick = 2 the recommended preset" | 2 (Recommended; Library default 0) | `SoundLib::choose` (`chooseSlurred`), `want` (`Want::slurQuick`) | `[slurs] quick`; score: `playbackSettings` |
| A note `[slurs] quick` swaps plays as loud as its held technique would: Long less Long (Rachm.) at the note's pitch and written length (map `quickLevel` on the swapped articulation: `sso_rachm_levels.json`, re-measured on the Windows VM 2026-10-08 in the passage itself, standard test score's slurred groups, perceived loudness per group against plain Long: the dB that, through the shipped dynamics curve at the note's pitch (per-pitch curves, above), raises the passage's CC1 64 to the CC1 at which Rachm. matches Long, per register: `sso_rachm_context_match.json` → `rachm_quick_levels.py`; match CC1 Violins 1 75 / 82 / 69 (pitches to 75, to 81, above), Violins 2 76 / 67 / 64, Violas 72 / 89 (to 58, above) and Celli 91 / 105 / 98 / 92 (to 49, 52, 56, above) measured 2026-10-09 but not used: the owner, 2026-10-09, the in-context Violas / Celli fits (these levels and their in-context onsets in `sso_rachm_onset_fit.json`) made Whence bars 1-2 Celli ~8 dB louder and pulsing, so their Long (Rachm.) is back to 25a3dcbbeb's (the timing check's onsets, no quickLevel: `RACHM_UNUSED` in `gen_spitfire_sso.py`; the data stay in the files); the same at every length; log-linear in length, linear in pitch, the nearest end's beyond. Not modelled: length. In the standard test score the violas' upper register at CC1 89 reads +1.2 dB on 16ths and -1.3 on 8ths against Long; the held-out check (branch rachm-holdout, `sso_rachm_holdout.json`) -3.7 for 59-90, about -6 at 4-8 32nds), added to the note's level (CC11 down along the held expression curve, CC1 up along the articulation's dynamics curve, as the Inspector's levels: so in Live's clips and the plain set too). The owner, 2026-10-08: Long (Rachm.) "sounds quieter than plain long"; measured, Violins 1 about 6 dB under Long at 100-136 ms, Violins 2 about 3; at 1 the owner, 2026-10-08: "not the correct volume. violin 1 in bar 5 now sounds really loud relative to the viola and cello" ; that file held isolated notes' differences (5-11 dB), which the shallow dynamics curve turned into CC1 105-127, about 5 dB over Long in the passage (VM). Values replaced by the in-context ones; the owner, 2026-10-09, on build 37882502550: "1 is the right balance, make it the recommended preset" | 1 (on; Recommended 1) | `rendermidi.cpp` (`quickDb`, `marcatoLevel`), `Articulation::quickLevelAt` | `[slurs] quickLevel`; score: `playbackSettings` |
| Slurred notes of the five string sections play on their Performance legato (as under staff text "performance"; "non performance" opts out), not `[slurs] quick`'s swap; with `[levels] calibrated` as loud as Long (above, Performance legato in Recommended; the owner, 2026-10-10) | 1 (on; Recommended 1, Library default 0) | `want` (`Want::slurPerformance`), `choose` (`choosePerformance`); `rendermidi.cpp` `performanceLevel` | `[slurs] performance`; score: `playbackSettings` |
| Auto's transitions on a Performance legato with listed techniques (SSO's five string sections; Spitfire's slow transitions by velocity: 20-84 slurred / fingered, 85-127 bowed, support.spitfireaudio.com/en/articles/11815986): a slurred note fingered (map velocity 69); the first note of a slur that starts where the slur before ends (same patch, touching, no accent or marcato) a bowed transition from it (93; Violas' map `legatoVelocity` 100), the note before overlapping it by the map's `overlap`, instead of a re-attack (the note before cut `reattackGap` ms early). A glissando line still portamento; the Inspector's attack / transition and the Join lane override. 0: slurred notes bowed, each slur re-attacked. The owner, 2026-10-12, after setting it by hand in Whence's Velocity lane: "notes within a slur use fingered and two slurs adjacent to each other will use bowed". Not measured: the Violas' transitions at 69 (their 100 tightened a run's arrival SD 54 → 39 ms, 2026-10-07) | 1 (on; Recommended 1, Library default 0) | `rendermidi.cpp` `performanceNote` (`slurAt`) | `[slurs] transitions`; score: `playbackSettings` |
| Auto's attack after silence on a Performance legato with listed techniques (SSO's five string sections): smooth (map velocity 5; measured on Violins 1 - Performance 2026-10-10: velocity 1-9, no bite, -6 dB at ~165 ms) instead of spiccato (112: a bite at 50-80 ms, then a swell). An accent or marcato stays accented (122), a re-attack right after a note of the patch spiccato (106); the Inspector's attack and the Velocity lane override. The owner, 2026-10-12: "smooth" for a phrase's first note (after setting it by hand in Whence). A smooth note starts early by the smooth attack's own measured onset (map `attackOnset`, `sso_smooth_onset.json`, 2026-10-12, kthost on the test VM, CC1 64: median Violins 1 100.2, Violins 2 91.1, Violas 236.2, Celli 172.2, Basses 103.1 ms; by pitch with `[heldNotes] byPitch` 1), not the articulation's onset (measured at velocity 80), within the same cap (the note before on its patch keeps keepMs; chunks); Live's written clips get it through their timing. 0: spiccato | 1 (on; Recommended 1, Library default 0) | `rendermidi.cpp` `performanceNote` | `[slurs] smoothStart`; score: `playbackSettings` |
| Transitions early, `[slurs] transitionsEarly` (the owner, 2026-10-14: "sure let's hear them started early"; 2026-10-15: 1, by ear and measured): a Performance legato transition (SSO's five string sections) is sent early: 1 by its modelled arrival (the measured `bendGlide` by the note before's length, else `legatoDelay` by interval with `libFastDelay`: the audit's model), the new pitch arrives on the beat; 2 by the measured leave + 23.5 ms, the change heard on the beat (Polfreman 2013, ISMIR: a legato note on a modelled bowed string is perceived 23.5 ms after its pitch change starts, SD 22.8); 3 the middle of the measured glide on the beat; 2 / 3 without a measured glide as 1 (the audit lists those patches). Capped in every mode: the note before on its patch keeps `[legato] keepMs` as played and never less than 15 ms (SSO doesn't play a legato note-on closer to the one before: the audit's DROPPED); the note before still ends `overlap` after the moved note-on (a transition, not a re-attack); the bend's glide moves with the note-on; Live: the written clips + Track Delay + MuseScore Link's delays play the moved note-ons (LiveTiming), *Live plays the score*'s clips carry them (below, Transitions early) | **1 (Recommended 1, Library default 0; since 2026-10-15)** | `rendermidi.cpp` `collect` (`libTransitionsEarly`, `onsetEarliest`, `libShifts`, `libJoins`), `MidiRenderer::TRANSITION_MIN_GAP_MS`, `HEARD_AFTER_LEAVE_MS`; audit: `Report::transitionShiftsMs` | `[slurs] transitionsEarly` 0-3; score: `playbackSettings` |
| Plain Performance, `[slurs] plainPerformance` (the owner, 2026-10-17, "C" by ear on Whence's Violas bars 8-13): every bowed note of a string section with a Performance legato (attacks listed) on it, staccatos too, velocity 64, at its written start (no early start of its own), a staccato a spiccato attack (its neighbours on the patch ending 45 ms before the next note-on), slurred transitions overlapping as before, other notes their written length (a touching one on the note before's copy, also at another tuning: SSO's transition; branch slur-joins, a fix); pizzicato, tremolo, trills, harmonics, mutes … keep their technique; "non performance" opts out; every event of the patch the map's `plainShift` early (Violins 1 65.8, Violins 2 66.0, Violas 111.0, Celli 80.2, Basses 59.1 ms, measured; another patch 0, the audit lists it), everything else later by it; Live: the Kontakt track's Track Delay (above, Plain Performance) | 1 (Recommended 1, Library default 0; the owner, 2026-10-17) | `soundlibrary.cpp` (`choosePlain`, `plainPerformanceArticulation`, `plainPerformancePatches`), `rendermidi.cpp` (`performanceNote`, `collect`, `PLAIN_VELOCITY`, `PLAIN_STACCATO_GAP_MS`, `libraryTrackDelays`), `TrackDelays::Delays::plain` / `plainMs`, `plainliveset.cpp`; data: map `plainShift` from `sso_plain_performance_shift.json` | `[slurs] plainPerformance`; score: `playbackSettings` |
| Plain timing, `[slurs] plainTiming` (branch plain-timing, 2026-10-21; the heard check of Mixed techniques: plainShift's one constant places the Performance attacks 21-74 ms early and its transitions 88-191 ms late): with plainPerformance each plain note early by its own model instead of the patch's plainShift: an attack (after silence, a staccato's spiccato attack) by the map's `plainAttack` (Violins 1 19.5, Violins 2 29.2, Violas 37.1, Celli 38.7, Basses 37.5 ms, measured in context, 15 dB under the peak), times heldNotes/early %; a transition by its modelled arrival (transitionsEarly's mode, 1 where that is 0), capped by keepMs; no plainShift track delay; a modifier the section has no sample for (SSO's Basses: con sord., espressivo) stays on the Performance patch; Live: the technique tracks' Track Delay and the MuseScore Link device's per-note delays (below, Plain timing) | 0 (Recommended 0, Library default 0; the owner preferred plainShift's timing, 2026-10-27) | `rendermidi.cpp` (`libPlainTiming`, the early starts in `collect`), `soundlibrary.cpp` (`choosePlain`, `Want::plainTiming`), `trackdelays.cpp`, `playbackaudit.cpp`; data: map `plainAttack` from `sso_plain_performance_shift.json` › attackMs | `[slurs] plainTiming`; score: `playbackSettings` |
| Guitar strums, `[guitar] strums` (branch ample-phase2, 2026-10-20; the owner decides by ear): on a guitar library (map `<Guitar>`: Ample Guitar M) a chord with a strum line (straight arpeggio, stretch <= 1) plays the strummer: CC7 so the strum is as loud as its notes at their velocity (the map's `StrumLevel`, measured: meas3, on the CC7 in force; restored before the next non-strum note), Detect mode, strum mode on, the chord's notes as chord keys, the full strum key (arrow up = low strings first = Ample's down strum; muted keys when every note has a cross notehead) at 110, then the keys and strum mode off; 0: the notes, spread as MuseScore plays an arpeggio | 0 (Recommended: not set, Library default 0) | `guitarplayback.cpp` (`MidiRenderer::libraryGuitar`), `soundlibrary.cpp` (`readGuitar`, `Guitar::strumLevel`); map: `gen_ample.py` GUITAR / STRUM_LEVELS | `[guitar] strums`; score: `playbackSettings` |
| Soft strums, `[guitar] softStrums` (2026-10-20; value 2 the same day, ample-fixes2; the owner compares by ear): with strums on, a strum at velocity <= the map's `softStrum` (Ample 30: where a roll is darker than a CC7-lowered strum, meas3) plays **0** the strummer turned down by CC7 (StrumLevel: CC7 is the channel's gain, so whatever still rings is turned down with it until the CC7 in force comes back before the next note); **1** its notes rolled `rollGap` ms (15) apart in strum order, strum mode off, at their velocity; **2** a roll of the strummer's single-string keys (map `rollKeys`, Ample D3-B3 = strings 6..1, string 6 first for a down strum) with strum mode on, Detect and the chord keys held as for a strum, at the chord's velocity held within the map's `softRoll` pairs (Ample `20:10 40:15`: meas3, a six-string key roll at velocity 20 10 ms apart / 40 15 ms apart as loud as the recorded strum turned down by StrumLevel's CC7, within 0.2 dB in the Strum set; below 20 the roll plays at 20 as value 0's strum plays at v20's CC7), its keys spaced linearly between the pairs (an assumption: 12.5 ms at v30), no CC7 of its own (a lowered one is restored before it); a muted strum stays the strummer. Live: the roll is notes (clips and plain set carry it, `tst_liveequivalence::liveGuitarSoftRoll`); value 0's CC7 is not carried (LIVE.md › What still differs) | 0 (Recommended: not set, Library default 0) | `guitarplayback.cpp`, `soundlibrary.cpp` (`Guitar::softRollVelocity` / `softRollGapMs`); map: `gen_ample.py` GUITAR | `[guitar] softStrums` 0-2; score: `playbackSettings` |
| Pinch harmonics, `[guitar] pinchHarmonics` (2026-10-20; off: Ample has no pinch sound, the owner decides by ear): a chord under staff text P.H. / pinch harm.: the natural harmonic keyswitch and the note at its partial (a diamond note's pitch, else fretted + 19), 2 ms later palm mute and the fretted note at velocity 60 for 45 ms (map `pinch`: meas3's closest by numbers), the next note's keyswitch again | 0 (Recommended: not set, Library default 0) | `guitarplayback.cpp` | `[guitar] pinchHarmonics`; score: `playbackSettings` |
| Speed from written, `[slurs] speedFromWritten` (the owner approved, 2026-10-17): on SSO's five string-section Performance patches CC119 (the transition's speed variant: RUNS / RUNS RT / FAST LEGATO) is MuseScore's: the patch's state has the script's own writing off (`$vfm12` 0, map `speedCC` / `speedScript`), each legato note-on gets 126 - floor(written ms from the note before x 127 / 1000) right before it, so early starts keep the written band; attacks none (above, Speed from written) | 0 (Recommended 0, Library default 0) | `soundlibrary.cpp` (`speedFromWritten`, `speedValue`, `playbackScriptValues`), `rendermidi.cpp` (`libSpeed`, `librarySpeeds`), `mscore/soundlibraryhost.cpp` (`setupStateWith`, `resave`, `Slot::values`), `liveclips.cpp` (`EXTRA_CARRIERS`), `plainliveset.cpp` (`speedEnvelope`); data: `sso_performance_layers.json` | `[slurs] speedFromWritten`; score: `playbackSettings` |
| Fixed transitions, `[slurs] fixedTransitions` (2026-10-20, not yet heard by the owner): on SSO's five string sections' Performance patches the modulators from Kontakt's random generator (source type 10: the transitions' envelope level, the sustains' pitch) at intensity 0 in the state loaded, so every play of a passage sounds the same, an average take (above, Fixed transitions) | 0 (Recommended 0, Library default 0) | `soundlibrary.cpp` (`fixedTransitions`, `playbackScriptValues`), `mscore/soundlibraryhost.cpp` (`setupStateWith`, `resave`), `audio/vst3/kontaktsetup.cpp` (`randomSources`, `withRandomSources`) | `[slurs] fixedTransitions`; score: `playbackSettings` |
| Joins across tunings, `[slurs] plainJoins` (2026-10-22; the owner: "violas A better for both", the attack): 1 an unslurred plain Performance note touching the note before at another tuning stays on that note's copy and plays as a transition; 0 it goes on its own tuning's copy with its own attack (above, Plain Performance › Joins across tunings) | 0 (Recommended 0, Library default 0) | `soundlibrary.cpp` (`SoundLib::lanes`, `plainJoin`) | `[slurs] plainJoins`; score: `playbackSettings` |
| Plain Auto, `[slurs] plainAuto` (branch `celli-bowed`, 2026-10-26; the owner: "it's supposed to be bowed", "slurs should have smooth start when there's nothing preceding it", widened the same day): with plainPerformance, 1 "smooth after a rest, bowed on each new bow, fingered inside a slur, spiccato for staccatos": a plain note on Auto after silence on its patch the smooth attack (5), early by its smooth onset less the patch's plainShift (Violins 1 34, Violins 2 25, Violas 125, Celli 92, Basses 44 ms); a note touching the note before outside a slur going on the bowed transition (93; Violas 100); inside a slur 64; staccatos, accents, a key struck again as before; 2 (branch `bowed-early`, 2026-10-27, to compare by ear) as 1, each new bow stroke early by the map's `bowedEarly` (the bowed transition's measured heard time less plainShift: Violins 1 125, Violins 2 151, Violas 55, Celli 106, Basses 64 ms), capped as midEarly; 0 all 64, as from 2026-10-17 (above, Plain Performance › Plain Auto) | 1 (Recommended 1, Library default 0) | `rendermidi.cpp` (`performanceNote`'s `plainAuto`, `PerformanceNote::smoothStart`; collectNote's early start) | `[slurs] plainAuto`; score: `playbackSettings` |
| Fast transitions, `[slurs] fastTransitions` (2026-10-20; the owner's choice 2026-10-28): a legato note-on less than 15 ms after the note-on before on its route at velocity 20-64, which SSO ignores (the audit's DROPPED), sent at 65, the lowest that plays, the same transition family (above, Fast transitions) | 1 (Recommended 1, Library default 0) | `rendermidi.cpp` (`libraryFastTransitions`) | `[slurs] fastTransitions`; score: `playbackSettings` |
| Microtone bends, `[slurs] microtoneBends` (2026-10-21; 3 since 2026-10-22, the owner by ear): 1 a microtonal note on a bending patch joined by a legato transition on the key on the side of the note before (key and bend the same way) and a fingered transition's bend by `bendGlideFingered`, uncut past the next note-on; 2 the glide only; 3 as 1 and each plain fingered transition there early by its glide's middle less plainShift (2026-10-22, by hand) (above, Microtone bends) | 3 (Recommended 3, Library default 0) | `rendermidi.cpp` (`microtoneSpelling`, `libraryPitchBends`), `soundlibrary.cpp` (`bendGlideAt`) | `[slurs] microtoneBends`; score: `playbackSettings` |
| Run bends, `[slurs] runBends` (2026-10-29, OPEN_ITEMS 5): 1 in a fast legato run on a patch with runLag (SSO Violins 1, Violas, Celli - Performance) the bend also corrects the patch's own lagging pitch, every 10 ms, so each note is heard at its written pitch from its written start as far as range and smoothing allow; a bowed transition without a bowed lag (SSO's map since 2026-10-30) not corrected in its own window (above, Run bends) | 0 (Recommended 0, Library default 0) | `rendermidi.cpp` (`libraryPitchBends`' `runBendPath`), `soundlibrary.cpp` (`runLag`) | `[slurs] runBends`; score: `playbackSettings` |
| Plain accents, `[slurs] plainAccents` (2026-10-20; 2 since 2026-10-21, the owner's choice at real players' level): under plainPerformance an accent, marcato or soft accent plays 1 MS4.7.5's offset for its articulations (40 log10 share, strings +5.3 dB) by the dynamics CC along the patch's curve, slurred notes too; 2 an accent or marcato on an attack the Performance legato's accented attack (SSO 122) instead of 64, a touching note re-attacked (reattackGap), by CC11 at real players' +5.0 dB over the spiccato attack (the map's `accentedLevel` by pitch and length, `PLAIN_ACCENT_DB`); 3 both (above, Plain accents) | 2 (Recommended 2, Library default 0) | `rendermidi.cpp` (`performanceNote`'s `accentDb`, `marcatoLevel`'s `plainAccentDb`), map `accentedLevel` | `[slurs] plainAccents`; score: `playbackSettings` |
| Level rule: a slur on a string section's Performance legato plays CC1 at the written dynamic and one gain per slur (by its dynamic, median spacing and pitch) by CC11 and the patches' gain stage, the rest of the part's patches at 0 dB (above, Level rule; the owner, 2026-10-10; the gain stage 2026-10-15); with `[slurs] plainPerformance` a plain note takes its kind's grid (slur / staccato + calibrated 2's D / single note; 2026-10-17) | 1 (on; Recommended 1, Library default 0) | `contextlevel.cpp` (`ContextLevel::gain`, `plainKind`, `volumes`), `rendermidi.cpp` `contextDb`, `TrackDelays::read` / `patchGain`; data: map `contextLevel`, `contextLevelPlain` / `Staccato` / `Single` from `level_rule.py` | `[levels] context`; score: `playbackSettings` |
| Section balance: every patch of a section its map `sectionLevel2` (2, Clark & Luce) or `sectionLevel` (1, TU Berlin) louder by its volume, with the level rule each phrase at gain + its section's level, by the patches' gain stage less the score's master trim (above, Section balance, Gain stage and master trim; the owner, 2026-10-11: M, Weinzierl et al. 2018's sound power against SSO's measured `sso_cc1_curves.json`) | 2 (Clark & Luce; Recommended 2, Library default 0; 1 TU Berlin) | `contextlevel.cpp` (`ContextLevel::volumes`, `sectionBalanceMode`), `LibInstrument::sectionLevelFor`, `TrackDelays::patchGain`; data: map `sectionLevel2` / `sectionLevel` from `gen_spitfire_sso.py` `SECTION_LEVEL2` / `SECTION_FADERS` | `[levels] sectionBalance`; score: `playbackSettings` |
| Gain stage and master trim: the levels above are each patch's own gain after its Kontakt (hosted: the slot's output; Live: MuseScore's Utility, "MuseScore gain"; MIDI out: CC7), uncapped, less one master trim per score that keeps its loudest moment (calibrated levels + the map's crest, powers added) from clipping; the fader keeps the Mixer and the track levels (above, Gain stage and master trim; the owner, 2026-10-15) | with any level setting on (none in Library default); no setting | `contextlevel.cpp` (`masterTrim`), `TrackDelays::gainStageDb` / `faderGain` / `patchGain` / `midiVolumes`, `LiveSetWriter::kontaktGains` / `utilityDevice`, `livesetupdate.cpp`, `liveset.cpp`; data: map `<Dynamics crest>` from `sso_crest.json` | none (follows the level settings) |
| Even levels: each slurred note on a Performance legato patch with a correction measured in place (Tools › Even Out Levels…, `--even-levels`) plays it by CC11 gliding in dB between the notes' nodes (halfway from each note-on to the next, since 2026-10-14), every 5 ms; a correction whose context changed is stale and not played; each patch's largest by its volume (above, Even levels; the owner, 2026-10-12, the blind test's Glide) | 1 (Recommended, the default; 0 in Library default; the owner, 2026-10-13) | `evenlevels.cpp`, `rendermidi.cpp` `libraryEvenLevels` / `trackLevel`, `TrackDelays::read` / `evenDb` / `patchGain`, `mscore/evenlevelsjob.cpp`; data: the score's metaTag `evenLevels` | `[levels] evenLevels`; score: `playbackSettings` |
| Divisi level, `[levels] divisi` (the owner, 2026-10-20): while n voices of a monophonic patch's part (voice lanes) sound together, each note plays 10 log10(n) dB softer by CC11 (2: -3.0, 3: -4.8 dB), the line's too, so the chord adds up to one section; a note keeps its start's level; an assumption from player counts, no divisi data (above, Divisi level) | 1 (Recommended, the default; 0 in Library default) | `soundlibrary.cpp` (`lanes`: `Lanes::sounding`, `divisiDb`), `rendermidi.cpp` (`divisiLevel`, `libDivisi`, `libraryNoteLevels`) | `[levels] divisi`; score: `playbackSettings` |
| Even keys, `[levels] evenKeys` (the owner, 2026-10-28): with the real range, a wind or brass Long's key whose level at mp lies more than T = 1.5 dB (real players' key-to-key scatter, TU Berlin) off a smooth curve over pitch plays the excess back by CC11 (map `<Articulation keyLevels>`, the every-semitone sweep; above, Even keys) | 1 (Recommended, the default; 0 in Library default) | `soundlibrary.cpp` (`keyLevels`), `rendermidi.cpp` (`realRangeLevel`: `LibLevel::keyDb`; `libraryNoteLevels`) | `[levels] evenKeys`; score: `playbackSettings` |
| Transition levels, `[levels] transitionLevels` (branch slur-joins, 2026-10-22; 2 branch bowed-join, 2026-10-28, a fix: the owner, hall.musicxml, "the tail of the last note suddenly get louder as the next note enters"): 1 a legato transition whose CC11 level differs from the note before's on its copy keeps the note before's level to the glide's leave, then moves linear in dB to the new one by the glide's arrival (bendGlide), instead of with the note-on while SSO still plays the note before; 2 the same for every step the transition's note-on brings on its route, the dynamics CC (CC1) and CC11 from whatever sets it (above, Transition levels) | 2 (Recommended 2, Library default 0) | `rendermidi.cpp` (1: `libraryNoteLevels`' `transitionSwitch`; 2: `libraryTransitionTails`; `libGlide`) | `[levels] transitionLevels`; score: `playbackSettings` |
| Attack levels, `[levels] attackLevels` (branch attack-levels, 2026-10-22; the owner 2026-10-23, "B better": 2): an attack whose route still rings the note before (within that note's `release`) takes its CC11 level (and even levels' glide start) 1: at its note-on + its measured onset (`plainAttack`, else the articulation's); 2: at its written time (its early start undone, plain Performance's `plainShift` too), when that stays before the route's next note or written CC11 / dynamics, instead of with the note-on (above, Attack levels) | 2 (Recommended 2, Library default 0) | `rendermidi.cpp` (`libraryNoteLevels`: `attackSwitch`; `libraryEvenLevels`: `libAttackLevelAt`) | `[levels] attackLevels`; score: `playbackSettings` |
| Mic compensation: a part whose mic controllers (Mic 1-5, Mic Mix Distance; values or lanes) give another mix than the default plays as loud as the default mix, by its patches' volumes (lanes: the largest by the volume, each note's own at its start by CC11); measured mic levels and laws, mics as powers (above, Mic compensation; the owner, 2026-10-15) | 1 (Recommended, the default; 0 in Library default) | `contextlevel.cpp` (`micLevelDb`, `micValues`, `micTops`, `volumes`), `rendermidi.cpp` `trackLevel`, `TrackDelays::patchGain`; data: map `<Mics>`, `micLevels` / `micDefault` from `mic_levels.py` | `[levels] micCompensation`; score: `playbackSettings` |
| Real range: each note of the five string sections and the main woodwind / brass patches as loud as a real player at its written dynamic (the Meyer rule, 2026-10-14: TU Berlin's single-note pp / ff are ppp / fff, written pp / ff at Meyer's in-play levels, p .. f equal steps between, from mp; rule D, 7 equal steps, where Meyer has no figures), less what SSO's Long already changes with CC1, by CC11 (following CC1 through hairpins) and the patches' volumes; a capped short's rest by CC11 (above, Real range; the owner, 2026-10-12, stage A) | 2, Clark & Luce spans (Recommended, the default since 2026-10-14; 1 the Meyer rule; 0 in Library default; the owner, 2026-10-13 / 14) | `contextlevel.cpp` (`realRangeDb`, `clarkLuceSpans`, `realRangeTops`, `volumes`), `rendermidi.cpp` `realRangeLevel` / `libraryNoteLevels`, `TrackDelays::patchGain`; data: map `realSpan` / `longLevel` / `realPlay` / `realSteps` from `real_range.py` | `[levels] realRange`; score: `playbackSettings` |
| Pedal changes after their chord (legato pedalling) | up one tick after, down the tick after (since 2026-10-06; the pianist's timing, `[pedal]`, removed 2026-10-07) | `renderSpanners` (pedal) | fixed |
| A key struck again ends its sounding note just before | on | `finishLibraryEvents` | `[notes] sameKeyEndsFirst` |
| Transport reset, `[notes] transportReset` (2026-10-21; the owner's choice 2026-10-28): the patches whose round robins don't go back to the first take at a transport start (SSO's string sections' Performance patches, Tuba Solo - Performance: "Reset on transport" off) load with it on, so every pass from one place picks the same takes (above, Transport reset) | 1 (Recommended 1, Library default 0) | `soundlibrary.cpp` (`transportReset`, `playbackScriptValues`, `playbackScriptKeys`), `mscore/soundlibraryhost.cpp` (`setupStateWith`, `resave`) | `[notes] transportReset`; score: `playbackSettings` |
| Hall guard, `[levels] hallGuard` (2026-10-21, not yet heard by the owner): a CC1 step down of 10 or more while a note is held on a patch with a diminuendo-release trigger (SSO's string sections' Performance patches) is sent as steps of 9, 1 ms apart, so no extra release plays (above, Hall guard) | 0 (Recommended 0, Library default 0) | `rendermidi.cpp` (`libraryHallGuard`), map `hallDrop` | `[levels] hallGuard`; score: `playbackSettings` |
| An attack at a velocity whose recording at that key is faulty plays at the nearest clean velocity measured (map `avoid`, `tools/soundlibraries/sso_bad_samples.json`: SSO's Celli - Performance B2-C3 at 100-116, the spiccato attack's top layer and the re-attack 106, plays its 5th round robin of 6 with a narrow tone near 1184 Hz, +9 .. +11 dB over the other attacks there; 99 and below clean; kthost on the Windows VM, 2026-10-10). Not legato transitions, not a note's own velocity (the Velocity lane). The owner, 2026-10-10: "the weird square wave-y noise problem" in Whence's Celli under the level rule (its CC1 64 makes the recording stand out: at CC1 89 it is there too, +3 dB) | on | `rendermidi.cpp` (`libAvoidBad`), `Articulation::avoidVelocity` | `[notes] avoidBadSamples`; score: `playbackSettings` |
| Tuning copies: tolerance, tail, max copies. Computed when the map leaves them out (SSO's does since 2026-10-03). Tolerance: half the smallest gap between two distinct accidental values (`ScoreTuning::smallestAccidentalGap`: 16.5, MuseScore 3.6's 23-limit comma, against 16.667, a twelfth of a tone: 0.167, so 0.083 cents). Tail: each note's ring to 60 dB under (ISO 3382-1's reverberation time), twice its measured release to 30 dB under (`<Articulation release>`; T30 extrapolated as ISO 3382-1 does); unmeasured: its patch's longest, else the part's (`SoundLib::laneRing`). Max copies: none unless the map, playback.ini or the score sets one (the owner, 2026-10-21, "2": always load every copy the music needs and warn when memory is low; it replaced his rule of 2026-10-03 that 1 + the free memory / (245 MB × the parts needing copies) caps them, which made a render depend on the machine's memory: the Hyper-V test VM, 1.9 GB free of 4.9 visible of 16 when idle, gave one score 1, 2 or 4 copies between runs, branch voice-determinism). **The warning**: the copies (`SoundLib::routeCopies`: routes beyond one per part and patch, tuning and voice copies) at 245 MB each (`LANE_COPY_BYTES`, the owner's 1031 → 1276 MB) against the memory free now (`SoundLib::freeMemoryBytes`, read at each check: Windows available physical memory plus installed memory not yet visible beyond 2 GB, `memoryForCopies`; Linux MemAvailable; `MS_FREE_MEMORY_MB` pins it), `SoundLib::copiesMemory`; short: hosted, a non-modal message box and a line in load times.log, once per score open and per change of its copies (an edit adding copies: the added ones against what is free then); the command-line renders (`SoundLibraryExport::loadInstances`) a WARNING line on stderr and in load times.log and the render preflight's `copies` entry (`RenderPreflight::setCopies`; the verify report's preflight). Never fewer copies, never a wait. Live and MIDI out (no varispeed, 2026-10-10): a patch without `bend=` gets a fixed copy per tuning, max copies not applied (`SoundLib::lanes` fixedTuning; LIVE.md › Microtones) | 0.083 cents; 2 × release; no maximum | `SoundLib::lanes`, `libraryLaneSettings` | `[tuning] tolerance`, `tail`, `maxLanes`; score: `soundLibraryLanes` |
| A copy waits for its notes' measured release | on (map `release`) | `SoundLib::lanes` | `[tuning] waitForRelease` |
| Pitch bend instead of varispeed where the patch bends | on (map `bend`) | `updateState` (`libBend`) | `[tuning] pitchBend` |
| Hosted tuning (the owner, 2026-10-15: "Whence takes absolutely forever to load all the patches because the microtonal stuff"): 1, slots: when MuseScore hosts a Kontakt library whose plug-in ignores a note's tuning (`<Files registry>`, `<Tuning method="varispeed">`), a patch that doesn't bend gets Live's copies, one per tuning, never retuned (`SoundLib::lanes` fixedTuning, as Live and MIDI out), and a patch's copies of one part play from ONE Kontakt, a slot per copy, each tuned by its program's Tune and listening to its own MIDI channel (`KontaktSetup::withSlots`, the plain Live set's multis: measured identical to a Kontakt per copy, -111 dB, ~85-90 MB less each; Tune within 2 cents); a bending copy (Performance) shares it untuned, its bends on its own channel. A plug-in parameter the part sets (SSO's Vibrato, mics) is set on the multi and reaches every slot (measured: the load-times run's phase 6, 2026-10-15). A copy keeps a Kontakt of its own only when a bending copy has a note beyond its bend range (varispeed plays that). The Mixer of a patch's copies is one (one instance, one output; they are one part's at one patch volume). 0, varispeed: a Kontakt per copy, retuned by varispeed once silent, as before 2026-10-15. Also the words `slots` / `varispeed` in playback.ini. Not in a preset. Kept by the owner, 2026-10-16 ("yeah seems good"), knowing slots differ slightly from varispeed (pitch 1.2-2.9 cents, level within 0.82 dB, 1/3-octave spectrum within 1.45 dB, onsets within 10 ms, HANDOFF.md › Tuning slots against varispeed) and match the plain Live set (Kontakt Tune) | 1 (slots) | `SoundLib::hostedSlots`, `SoundLib::instances`, `SoundLibraryHost::syncSome` / `tunedState` / `loadInstanceSetup`, `Vst3Synth::setLink`, `SoundLibraryExport::loadInstances` | `[tuning] hostedTuning`; score: `playbackSettings` |
| A legato transition's pitch bend glides with the transition's own pitch: from when SSO's f0 leaves the note before (a tenth of the way) to when it reaches the new note (nine tenths), ms after the note-on by the note before's length (map `bendGlide`: the five string sections' Performance patches, slurred runs of steps at IOI 136 / 300 / 600 ms pitch-tracked, Kontakt 8 + SSO in kthost, 2026-10-11: leave -9 … 140, arrive 100 … 306; `sso_legato_glide.json`, `tools/soundlibraries/legato_glide/`), not before the note-on; then held to the next note-on. The path is sent as is, a value a tick (a glide of 100-300 ms: under half a cent a tick). Kontakt + SSO (Violins 1 / Violas - Performance, slurred semitone runs with a quarter tone, 2026-10-11): from the patch's own arrival to the next transition the bend within 10 cents of the note 0.81-0.91 of the time at 300 ms a note, 0.91-0.95 at 600 (the glide before: 0.14-0.15, 0.83-0.85), the note before untouched until the transition leaves it. The patch smooths a bend as one pole (71-80 ms, Basses 29: `sso_bend_smoothing.json`, map `bendSmoothing`): since 2026-10-11 (branch `legato-bend-settle`) the glide is sent so that the bend as heard reaches the tuning at the arrival (the note before's tuning to the leave, one value (target - heard e) / (1 - e), e = exp(-(arrive - leave) / tau), to the arrival or an earlier note-on on the lane, then the tuning); sent as the path itself it was still 35-43 % of the step short at the next note-on in 136 ms sixteenths (the pole's inverse of the straight path, 1 + tau / glide times the step, overshot by ~15 cents in context). Whence (Recommended, dump, the pole modelled): transitions into a quarter tone heard within 5 cents before the next note-on 5 of 140 before, 139 after (median 80 ms after the written time; the bend clamped at the range once). Unmeasured patches: from the arrival (note-on + the legato delay, `[legato.delay]`, fastShare), one cent a tick, at the latest by the lane's next note-on. Before 2026-10-11 every patch glided so from the arrival: on Whence 141 of 147 slurred quarter tones were in tune under half their length. Whence (quarter tones, Recommended), real renders: the 40 quarter-tone notes the patch settles on in tune 0.15 of that time before, 0.54 after (Violins 1 0.06 → 0.66, Celli 0.00 → 0.29); 69 of 147 never settle (136 ms sixteenths) | on | `libraryPitchBends` (`libGlide`) | `[tuning] bendAtArrival` (0: a cent a tick from the note-on) |
| Automation ramps: a value at each tick where the ramp reaches another step: a MIDI controller's 1/127 (every CC value it passes, at its tick), a plug-in parameter's 1e-4 (the lane's own precision: its points are kept to 1e-4); the owner's criterion, 2026-10-03. Live: the lanes Create Live Set / the device store are packed (`packLane`) to within one MIDI step (1/127) and one tick of MuseScore's staircase; Live's clip envelopes and a set's curves as straight pieces within one MIDI step of the curve (`Automation::flattenCurve`) | computed (no setting; `[automation] stepTicks` retired) | `Automation::Lane::events`, `renderMs4Dynamics` (lanes), `envelopeEvents`, `LiveSet::curve` | — |
| Live clips: controller carrier spacing (measured in Live 12.4.6: below) | 1 unit (EPSILON) | `LiveClips::clipNotes` | `[live] carrierEpsilon` |
| Kontakt voices per patch | 512 | `SoundLibraryHost::kontaktMaxVoices` (also the Live Set) | `[hosting] maxVoices` (`MS_KONTAKT_MAX_VOICES` wins) |
| Settle after setState before controllers (measured: below) | 0.047 s | `Vst3Plugin::settle` | `[hosting] settleSeconds` |
| Helper processes for the plug-in's instances (MuseScoreLibraryHost: the window free while they load, several loading at once, a plug-in crash ends only its helper; 0: in MuseScore's process, as before; K: up to K; -1: automatic, a helper per physical core but one, at least 2 on 2+ cores with 4+ logical processors). Not a sound setting: the helper plays the same calls in the same order, sample-identical (tst_soundlibrary::remoteNull; VM: HANDOFF.md › Out-of-process host) | -1 | `Vst3Remote` (audio/vst3/vst3remote.h) | `[soundlibrary] hostProcesses` (`MS_VST3_HOST_PROCESSES` wins) |
| Mixer volume, pan and mute on library slots apply from the next audio block, no glide (the owner, 2026-10-03: no mixer smoothing; the 5 ms glide had no source, `[hosting] mixSmoothingMs` is retired) | at once | `Vst3Synth::process` | — |
| MS3 hairpin with its own velocity change plays as in 3.6 | on | `ms3HairpinVelocity()` | Preferences (Advanced) `application/playback/ms3HairpinVelocityChange`; `MS4_STRICT` |
| Library dynamics CC ahead of the notes at its tick; shorts' velocity on the CC1 scale (calibrated levels off, or not measured) | always | `renderMs4Dynamics`, `libVelocity` | map `<Dynamics cc velocity>` |
| A lane edited while playing: heard at once (flat lanes' parameters on the instances, the rest rendered again) | always | `SoundLibraryHost::applyPartSettings`, `Seq::renderAgainPlaying` | not a number: no setting |
| Chunks don't end before a slurred or library note (live playback) | always | `libSlurAcross`, `libNoteAfter` | fixed (correctness: an early start can't cross a chunk) |
| Chunks rendered inside a window around them (live playback plays the whole render's events) | always | `MidiRenderer::renderChunk`, `libWindow`, `libCut`, `libSnapshotAt` (2026-10-25, chunk-equal; `MS_CHUNK_WINDOWS_OFF=1` to compare) | fixed (correctness: CLAUDE.md › Chunks) |
| Background loading pauses | 0 ms | `SoundLibraryHost` `INPUT_PAUSE_MS`, `PRELOAD_GAP_MS` | fixed (the owner chose 0) |
| Prepared states kept (a patch's voice limit and its copies' tunings made once, the very bytes, in memory and in `<setups folder>/prepared`) | always | `SoundLibraryHost` `prepared` (`MS_PREPARED_OFF`: made at each load, to compare) | not a number: load time only, the plug-in gets the same state (2026-10-16, load profile) |
| mimalloc for the C runtime heap during a plug-in's setState (Windows: kheap.dll, loaded first thing in `main()`, scoped: new blocks from mimalloc only on the thread inside `Vst3Plugin::setState`, every other thread the system allocator) | always (Windows) | `ProcessAllocator::start` / `LoadScope` (`audio/vst3/processallocator.h`, `tools/soundlibraries/kheap`; `MS_KHEAP_OFF=1`: the system allocator, to compare; without kheap.dll: the system allocator; load times.log says which) | not a number: load time only, Kontakt computes the same bytes (renders null at the floor of two passes; 2026-10-23, sso-load: Whence -15 % on the owner's PC, memory +17 %, whole process; 2026-10-24, kheap-realtime: scoped, the whole-process mode crackled the owner's live playback) |

**Fast slurs (2026-10-02; history: legato transitions play on their beat since 2026-10-06, the early start removed
2026-10-07, the owner: "I want fast slurs to not sound late"; replaces the fast-note ramp of
2026-09-30, whose keys `rampFromMs`, `rampToMs`, `rampMaxShare` are now ignored):** SSO's
Performance patches sound a slurred sixteenth's pitch 100-170 ms after its note-on (strings; woodwinds and brass
60-130) at 100-200 bpm, longer than the note itself; the ramp left them 90-125 ms late (median). Now every note of a
fast slurred run starts early by about that much (the cascade: each note before keeps its played length, only a run's
first note gives up time), and a slur's first note inside a run as well. Measured on the Windows VM (13 Performance
patches, 4- and 8-note slurs of sixteenths at 100/130/160/200 bpm, rendered offline through Kontakt with the
renderer's events; tools/playbackverify/make_fastrun_scores.py, analyze_fastruns.py): transitions' median arrival
+125 / +89 / +109 ms (strings / woodwinds / brass) before, about 0-20 after; level spread over slur positions
unchanged (1.3-2.2 dB). Numbers per setting: HANDOFF.md › Legato and onset timing.

## Measured by sweeps (numbers-measured, 2026-10-03)

The owner's rule (2026-10-03): no number without a source; a sweep with a rule stated before the data chooses each
value. Windows VM: Kontakt 8 + SSO through kthost (an offline VST 3 host: each patch's MuseScore setup, the rest
check's), MuseScore 2bc46bc's `--verify-playback`, Live 12.4.6 (trial). Tools in the repository unless said.

- **`[legato] overlapTicks` 30 → 0** (removed 2026-10-07 at 0). Rule: the smallest overlap with a legato transition in every tested case.
  Slurred pairs on all 43 Performance patches, intervals ±1 ±2 ±5 ±12 from the middle of the legato range, the note
  before held 0.8 s or a sixteenth at 110 (136 ms), the note before ending −300 (a gap) … +250 ms after the next
  note-on; transition or fresh attack told by the new note's own harmonics over its first 400 ms against the same
  pair's references (+250 ms: legato; −300 ms: attack). Result: transition in 336 of 336 cases at every ending from
  −20 ms on (−20, −10, 0, +1, +5, +30), in 80 of 336 at −40 ms, none from −60 ms (offline, events in blocks of their
  own as MuseScore sends them; the same in 512-frame blocks with each event at its sample offset, as a realtime host
  and Live deliver them: 106 of 106 from −20 ms on, 24 of 106 at −40 ms, 14 patches). The unit that matters is time (SSO joins notes up to 20 ms apart); 0 ticks
  is the smallest value, tempo-independent, with a 20 ms margin. Note: a slur's last note ending 1 % early
  (`slurEndOverlap` 0) is therefore also played as a transition by SSO whenever that 1 % is 20 ms or less, i.e. for
  any note up to 2 s: the phrase break needs a gap of 40 ms or more (open, for the owner).
- **`[hosting] settleSeconds` 1 → 0.047 s.** Rule: the smallest settle after which every repeat keeps a parameter
  set then (Grand Piano: the four mic levels; Violins 1 / Horn Solo - Performance, Timpani: three). kthost processed
  silence in blocks of at most 1024 frames (as `Vst3Plugin::settle`), then set the parameters, then played four notes:
  lost after 23 ms (1014 frames), kept from 24 ms (1058) at 44.1 kHz, offline and realtime alike, 3-5 repeats each,
  every repeat the same; in blocks of 256 kept from the second block (441 frames), in one block of 4096 from 1103
  frames: Kontakt runs the script in its first sub-block of up to 1024 frames. N: every repeat gave the same result,
  so a few show it. Kept at every rate MuseScore offers: 1025 frames at 22050 Hz = 46.5 ms → 47 ms.
- **`[live] carrierEpsilon` 2 → 1 unit.** Rule: the smallest spacing (the setting's range starts at 1) at which Live
  keeps every carrier before its note. A clip of 160 quarters alternating Spiccato / Tremolo (each needs its CC32
  carrier first) written through the MuseScore Link hub on a Kontakt + SSO Violins 1 track and frozen: with the
  carrier 1 unit before the note (120 and 240 bpm) and 0 units (240 bpm) every note rendered exactly as with 96 units
  (0.00 dB difference in every note's envelope); with the carrier 96 units after the note 157 of 160 differed.
- **Live link timings** (the owner's rule 7B: just above Live's longest silence in its busiest normal moments). A
  listener in MuseScore's place logged the hub's datagrams while Live froze a 160-note Kontakt track (3×), duplicated
  Kontakt tracks (5×), saved and loaded sets: hellos (every 2 s) at most 6.0 s apart → `HELLO_TIMEOUT_MS` 6000 → 8000
  (+ one hello period); the device's 1 s Task stalled as long, and at 5 s other copies took over (three hubs said hello
  at once) → `HUB_STALE_MS` 5000 → 7000 and `AUDIBLE_STALE_MS` 4000 → 7000 (+ one beat / heartbeat, 1 s); replies
  outside freezes at most 3.79 s (median 46 ms) → `CONFIRM_MS` 3000 → 3800 (during a freeze Live answers nothing until
  it ends). With extra load (4 heavy probes a second) hellos reached 8.6 s. Not derived: `MAX_TRIES`, debounces (no
  datagram was lost: 0 of 6898).
- **`[legato] fastShare` / `fastFullMs` 65 % / 800 → 50 % / 380 ms** (refit: the 2026-10-02 fit's data and script
  were gone). Rule (stated in `tools/playbackverify/fit_fast_share.py` before fitting): least squares between each
  part × tempo median of the measured arrivals and of the predicted delays (map `legatoDelay` × the share after the
  note before's length), grid 30-100 % × 100-1200 ms. Data: `make_fastrun_scores.py`'s three scores with legato early 0
  (every note-on on its written time, so the arrival is SSO's own delay), MuseScore 2bc46bc `--verify-playback`,
  `analyze_fastruns.py`: 2580 transitions of 12 Performance patches at 100 / 130 / 160 / 200 bpm
  (`tools/playbackverify/fast_share_fit.json`: the 48 groups). rms group error 20.1 ms (65 % / 800: 22.4); the
  minimum is flat (within 5 %: 39-60 %, 260-1060 ms): sixteenths only pin the share near their lengths (75-150 ms).
- **`[legato] keepMs` 40 → 40 ms (now chosen).** Rule (stated before the renders): the value of 0 / 20 / 40 / 60 /
  80 / 120 ms whose median |arrival − written time| of the slurred transitions (not slur firsts) over the 12 parts and
  4 tempi is the smallest, among those with the fewest transitions without an arrival; within 1 ms the larger.
  `make_fastrun_scores.py`'s three scores with each value (and the refit fastShare 50 % / fastFullMs 380) through
  MuseScore 2bc46bc `--verify-playback`, `analyze_fastruns.py`, `tools/playbackverify/choose_keep_ms.py`
  (`keep_ms_choice.json`): 3072 transitions each; median |arrival| 31 / 31 / 31 / 34 / 45 / 94 ms, 90 % 104 / 104 /
  104 / 109 / 129 / 159; no arrival found 431 / 430 / 410 / 424 / 432 / 432 (mostly the analysis, alike for every
  value). 40 wins on the fewest without an arrival and on the tie-break; 0-40 hear the same, 60 and more get late.
- **`[shorts] nominalShare` (90 %) removed.** Every SSO short with a `length` (11) has a measured `from`, so the rule
  applied to nothing; a short without `from` is no longer skipped by length.
- **Varispeed glide (`[legato] glideMs` 30 ms) removed**: computed by the owner's criterion, each output frame's step
  within a cent (above).
- **`ArticulationCheck::TAIL_SECONDS` 6 → 19 s; the timpani's and bells' releases in the map.** Rule: the smallest
  whole second longer than the longest release measured with a 25 s tail. The 98 sounds whose release had reached
  the 6 s tail, again (`release_long_tail.py`, every pitch of the pitched ones): longest 18.9 s (Cymbal Hi FX Bow);
  `releases_from_long_tail.py` → `sso_sound_range.json` (277 values) → the generator: Timpani Swell mf 6070 → 7575,
  Swell f 5730 → 7550, Roll 4720 → 4715, Roll Muted 1835 → 1975; Glockenspiel Roll 5325 → 12875; Tubular Bells 6110 →
  9845 ms.
- **`[pedal]` 40 / 90 ms, 25 / 50 %: off from 2026-10-06, removed 2026-10-07.** Rule: the smallest times with no chord losing a note. The VM can't
  make SSO's Grand Piano drop a chord at any timing: kthost, 60 four-note chord changes each, up −1 / down 0 ms (MS4's,
  the owner's failing case) … up 60 / down 160 ms, offline and realtime: 0 notes more than 6 dB under the reference;
  MuseScore's export of `Piano pedal chords` (102 strikes) with up 0 … 60 ms: 0 missing attacks. With nothing failing,
  the rule would pick the earliest times, where the owner's export lost 1 chord in 8; so the values stay until the
  owner's PC can be measured. The shares only matter for pedals shorter than 160 / 180 ms; any up share under the down
  share keeps the order.
- **3C sensitivity** (thresholds inside the measurements, halved / doubled where the data allows): a short's
  sounding length at 6 / 15 / 20 dB instead of 10 moves its `from` by −0.10 / +0.12 / +0.24 s (median of 11; up to
  0.31 s): it matters, needs a source; the legato delay at the 10 % / 90 % crossing instead of 50 % moves a patch's
  median by −120 / +580 ms (43 patches): it matters (the 50 % one is what the sweeps confirmed by ear-matched arrival);
  a release to 15 / 60 dB instead of 30: Timpani Swell mf 4655 / 13305 against 7575 ms (it matters; 30 dB is ISO
  3382-1's T30 range).
  **The owner's decisions (2026-10-04)**: the 10 dB short length is approved as the owner's rule; the supporting
  analogy is ISO 3382-1's early decay time, which is read over the first 10 dB of a decay (an analogy only: no
  standard defines a note's sounding length). The 50 % legato arrival is approved as the owner's rule (the neutral
  midpoint; the sweeps' heard arrival matched it). The tuning tail's 60 dB as twice the 30 dB release (T30, ISO
  3382-1) is approved without checking the standard's text.

Not settings: the measurement definitions behind the map data (the -15 dB onset threshold, the swell rule, the
legato grid's 50 % crossing, octaves by template fit, the release as the longest over the range) are applied when
the map is generated; change them in `gen_spitfire_sso.py` (its comments hold the numbers), or override the result
per patch in playback.ini.

Tests: `tst_soundlibrary::playbackSettingsIni` (generation, parsing, unknown keys, clamping, reload) and
`playbackSettingsLayers` (default / ini / score for rendered effects; removed keys ignored); `tst_liveequivalence::playbackSettingsWidget` (the table, per-score edits). Octaves by start pitch: `tst_soundlibrary::legatoOctaveByStartPitch`, `tst_liveequivalence::liveClipsLegatoOctave` / `liveEquivalenceOctave` (Live identical). Bend at arrival: `tst_soundlibrary::tuningBendAtArrival` (transition, fresh attack, clamp, layers), `tst_liveequivalence::liveClipsBend` (row "at arrival": Live identical). Marcato level: `tst_marcatolevel` (metaTag, undo, copy / paste, parts, the law, velocity and controller paths, default = main's events), `tst_liveequivalence::liveMarcatoLevel` (clips and audio identical).
