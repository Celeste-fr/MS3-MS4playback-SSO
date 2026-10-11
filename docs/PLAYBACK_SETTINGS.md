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
transition's glide (`[tuning] bendAtArrival`). The measurements stay in the repository (`sso_legato_levels.json`,
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
   out-of-range values are clamped. **Edit › Reload Playback Settings** (or *Reload playback.ini* in the dialog)
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
Playback adjustments writes the preset's keys into the file as a text edit (comments and the other values stay; a
missing key goes under its section's header, a missing section at the end; a missing file starts from the template)
and then reads it as Reload Playback Settings does, playback going on. The box says "this score overrides: ..." when
the open score has its own value of a preset key (not cleared). "(Custom)" shows when the ini matches none (not
selectable).

| Preset | heldNotes/early | heldNotes/byPitch | levels/calibrated | slurs/quick, slurs/quickLevel | slurs/performance | levels/context | levels/sectionBalance |
|---|---|---|---|---|---|---|---|
| Recommended | 100 (held notes and, since 2026-10-08, every technique with measured onsets: shorts on the beat as the Longs are) | 0: every note of an articulation early by the median of its measured onsets (a run as written; a note's attack off by its pitch's distance from the median: SSO's section strings -78 .. +62 ms; since 2026-10-09 slurred notes `[slurs] quick` swaps too) | 1: techniques on velocity as loud as the held note, plus MS4's offset (below, Calibrated levels; 2026-10-08) | 2, 1: short slurred notes on the espressivo long (Long (Rachm.)) as loud as the held Long (the owner, 2026-10-08: "let's make slurs quick = 2 the recommended preset"; quickLevel re-measured in the passage, 2026-10-09: "1 is the right balance, make it the recommended preset") | 1: the five string sections' slurred notes on their Performance legato (below, Performance legato; 2026-10-10) | 1: those slurs by the level rule (below, Level rule; 2026-10-10) | 1: each string section at its level, A′'s balance (below, Section balance; 2026-10-11) |
| Library default | 0 (notes play as written, no timing adjustment of the fork; byPitch left as it is) | | 0: the library's own balance | 0: the held technique (quickLevel left as it is) | 0: on the All techniques patch | 0 | 0 |

`byPitch=1` (by hand, either layer) starts each note early by its own pitch's onset instead: attacks on the beat, a
run's spacing following the onsets (Recommended until 2026-10-08); the Preset box then shows "(Custom)".

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
With `[levels] calibrated` they play as loud as the part's Long (below).

## Calibrated levels

The owner, 2026-10-08, on staccatos sounding too quiet: "bring back the calibrated short velocity and include it in the
recommended preset. adjust all techniques according to the measured value, taking into account that some techniques
are meant to be louder", then "use MuseScore 4.7.5's articulation profiles for everything incl. marcato, with the
requirement that whatever notation that caused the technique can change the dynamics in the inspector".

`[levels] calibrated` (1 by default and in Recommended, 0 in Library default; per score through `playbackSettings`):
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
The exception (2026-10-10, with `[slurs] performance`): a Performance legato note (an articulation with `attacks`) plays,
on CC1, the value at which its patch sounds as loud as the part's Long at the CC1 in force
(`SoundLib::calibratedController`, by ear where both are measured, the note's pitch's curve where measured; a LibLevel
on the dynamics CC from right before its note-on, `rendermidi.cpp` `performanceLevel`): its notes are as loud as the
All techniques notes around them. SSO's shipped curves (perceived, dB, Long less Performance at CC1 16 / 64 / 127):
Violins 1 9.6 / 2.7 / -2.6, Violins 2 9.9 / 2.4 / -6.7, Violas 16.1 / 3.8 / -2.4, Celli 28.1 / 7.8 / -3.8, Basses
5.8 / 3.1 / -1.1: louder at soft dynamics, softer at the loudest. An Inspector level on top: louder along its own curve
from the calibrated value, softer by CC11 (a plain volume on these patches, measured 2026-10-02: HANDOFF.md › Legato level balance). Its velocity (the
attack or transition) is not changed. Live gets the same values: they are CC1 events, so the clips and the plain set's
CC1 lanes carry them (`tst_liveequivalence::livePerformanceLevel`).

The measurements: the user's `dynamics.json` (Check articulations › Dynamics) when there is one, else the shipped
`share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json` (`SoundLibraryHost::loadCalibration`), made from the
owner's checks in `tools/soundlibraries/sso_sound_dynamics.json` by
`tools/soundlibraries/calibration_from_sound_dynamics.py` (105 patches, 492 curves; only what the map plays).

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
- The gain reaches 21 dB (Celli pp, high), more than CC11 can add. The score's largest gain G over every slur of those
  parts is shared out (`ContextLevel::volumes`): the Performance patches play at volume G - S, every other patch of the
  library's parts S = max(0, G - 6) dB lower together (6: Live's Volume top; the owner, 2026-10-10: Performance levels
  move together, by lowering the rest), and each slur plays gain - G by CC11. The volumes go through
  `TrackDelays::patchGain` (the Kontakt slot's volume in MuseScore; `LiveSetWriter::kontaktVolume` / the plain set's
  Kontakt track Volume in Live), the CC11 through the per-note levels (`libraryNoteLevels`): Live's clips and the plain
  set's CC11 lanes carry it. They are not kept in the `trackDelays` metaTag; the Mixer's own track levels add.
- An Inspector level on top: louder along the patch's own curve from the written value (CC1), softer by CC11.

The gains (`tools/soundlibraries/performance_context/level_rule.py`, from the measured cells of
`tools/soundlibraries/sso_performance_context.json`, the real Kontakt + SSO on the Windows VM, 2026-10-10; metric L:
loudest 50 ms RMS per note, median per figure, two takes):

1. The staccatos' (Spiccato at the calibrated velocity) level in the music, least squares over all five sections:
   st = T(d) + a_s + b_s (p - p_mid) / 12 + c_s log2(IOI / 136 ms). T(32 / 64 / 96 / 112) = -40.2 / -34.9 / -32.3 /
   -29.9 dB; RMS residual 2.37 dB. The fitted level, not each cell: the cells' take-to-take noise is 0.9-1.3 dB, and
   two cells break the rule themselves (Celli pp staccatos 4.9 dB over the fit, Basses pp 4.8 under: calibration
   points to check, not followed).
2. gain = st - pfd + 1.3 dB: pfd the Performance figure's measured level at CC1 = d; 1.3 dB the Violas' relation the
   owner approved (slurs 1.3 dB louder than the staccatos around them, HANDOFF.md).

`gen_spitfire_sso.py` writes them into the map (`contextLevel`, the grid: d 32 / 64 / 96 / 112, IOI 100 / 136 / 200 /
300 / 500 ms, three pitches per section). Gains at IOI 136 ms, low / mid / high register, d 32 / 64 / 96 / 112:

| Section | 32 | 64 | 96 | 112 |
|---|---|---|---|---|
| Violins 1 | 10.1 / 13.6 / 16.3 | 6.7 / 10.0 / 12.9 | 1.4 / 6.8 / 9.0 | 1.9 / 6.2 / 7.5 |
| Violins 2 | 8.8 / 14.1 / 17.0 | 7.3 / 11.5 / 14.5 | 3.8 / 7.1 / 9.7 | 3.9 / 7.2 / 9.5 |
| Violas | 10.2 / 8.0 / 16.0 | 9.1 / 8.3 / 15.5 | 2.8 / 4.4 / 10.7 | 3.2 / 3.8 / 10.1 |
| Celli | 16.3 / 20.9 / 20.4 | 11.6 / 15.4 / 14.2 | 6.1 / 9.8 / 9.0 | 7.0 / 10.0 / 8.7 |
| Basses | 4.6 / 0.7 / 3.7 | 2.6 / -1.9 / 1.0 | 0.4 / -1.1 / 1.2 | 1.5 / -0.8 / 1.2 |

Test: `tst_soundlibrary::levelContext` (the grid, the layers, CC1 at the written value, one CC11 per slur, the volumes),
`tst_liveequivalence::liveContextLevel` (Live's clips, the plain set's lanes and Kontakt volumes).

### Section balance

The owner, 2026-10-11: A′ (the rule on the owner's Live set R: R's All techniques faders, the Performance tracks at the
rule's gain plus their section's fader) approved and preferred over the owner's own balance. `[levels] sectionBalance`
(1 by default and in Recommended, 0 in Library default; per score through `playbackSettings`) plays it:

- Each string section has a level O (the map's `<Instrument sectionLevel>`, dB; extras take the main patch's):
  `gen_spitfire_sso.py` `SECTION_FADERS`, R's faders by ear (the owner's Whence R set, 2026-10-11: Violins 1 -10.8,
  Violins 2 -10.2, Violas -7.3, Celli -8.0, Basses -7.3 dB) plus the rule's S on Whence 12-TET (9.293): O = -1.507 /
  -0.907 / +1.993 / +1.293 / +1.993. Only differences between sections count: the common part is taken out below.
- Every patch of the section plays O louder by its volume (`TrackDelays::patchGain`, so the Kontakt slot, the route
  set's and the plain set's Kontakt track Volume alike). With `[levels] context` a phrase plays gain + O, a staccato O,
  everything lower together by S' = max(0, max over phrases of gain + O - 6). A part's Performance patch plays at
  T + O - S', T = min(G, S' + 6 - O), its phrases' CC11 gain - T: the rule's CC11 values where G fits under +6
  (`ContextLevel::volumes`).
- So MuseScore plays A′ less one constant, S - S'. Whence 12-TET, Recommended: see HANDOFF.md › Section balance.
- Off, or no patch of the score's library parts with a level: the rule alone, as above.

Test: `tst_soundlibrary::levelSectionBalance` (the levels, the volumes, each note's level, the layers, a map without
levels), `tst_liveequivalence::liveSectionBalance` (Live's clips, the route set's and the plain set's Kontakt volumes).

## Inventory

Ini key = `[section] key`. "Map" = per-patch data in `share/soundlibraries/Spitfire Symphony Orchestra.xml`, made by
`tools/soundlibraries/gen_spitfire_sso.py` from the measurements (its comments say how each was measured).

| Adjustment | Default | Where it acts | Configure |
|---|---|---|---|
| Phrase marks: a slur that plays no legato | per slur | `slur.h` Phrase marks, metaTag `phraseMarks` | per slur (Alt+S, Inspector) |
| Level (marcato only until 2026-10-08): an articulation sign, or a staff text that changes the technique, louder or softer than the technique plays (above, Calibrated levels). A note on velocity: the velocity at which its measured curve (dynamics.json) is that many dB away, else SoundFont 2's law 40 log10(v/127); a note on the dynamics CC (SSO's strings' Marcato Attack): softer by CC11 (the held note's expression curve, else the law), louder by CC1 (its own curve, else the law; up to 127), on its route from right before its note-on to right before the route's next note-on; built-in synthesizer (MS3 and MS4 modes): the velocity by the law | "Library default" (0 dB: no change) | `articulation.h` MarcatoLevel, `rendermidi.cpp` (`levelOf`, `marcatoLevel`, `libraryNoteLevels`, `playNote`), `SoundLib::TextState::db`, metaTag `marcatoLevels` | per sign: Inspector › Articulation › *Level*; per text: Inspector › Staff text › *Level* (−35.5 … +35.5 dB, 0.5 steps; several selected: all; the range: above). Until 2026-10-08 −18.5 … +12.5, what SSO's marcatos span (`derived_numbers.py marcato`) |
| Calibrated levels: a technique on velocity as loud as the part's held note at the same dynamic, plus MuseScore 4's offset for its articulations (marcato's too); a Performance legato note by CC1 as loud as the held note (2026-10-10) | on (Recommended; Library default: off) | `rendermidi.cpp` `libVelocity`, `SoundLib::calibratedVelocity`; data: dynamics.json, else the shipped `Spitfire Symphony Orchestra.dynamics.json` | `[levels] calibrated`; score: `playbackSettings` |
| Track delay: a library part (and each of its patches and techniques, added to the part's) plays this many ms later, negative earlier, as Live's Track Delay; every library event of the track moved in time, the tempo followed; with a negative delay everything else (other parts, the metronome) plays later by the earliest one (`libraryDelayLead`), so the track is early from its first note (2026-10-07; before: clamped at the start); a note and its switch by the technique's sum, and so do the controllers and bends right before it at its tick (its track level's CC11, a dynamic; 2026-10-08), other controllers by the patch's | 0 ms (none) | `trackdelays.h`, `rendermidi.cpp` `libraryTrackDelays`, metaTag `trackDelays` | per part: Mixer › *Track delay*, *Tracks…* (−1000 … 1000 ms: a tutorial's figure for Live, its manual gives none); the plain Live set writes it as TrackDelay and reads it back |
| Track level: a library part's patch and technique each play this many dB softer or louder (added up), by CC11 (expression) from right before each of the technique's notes, the value in force again before the next note without one; adds to a marcato's level (2026-10-07). Louder (2026-10-08): the patch's loudest level is its headroom, played by its volume (hosted: the slot's; Live: the Kontakt track's Volume), its notes' CC11 that much lower | 0 dB (none) | `trackdelays.h` (`headroomDb`, `noteDb`, `patchGain`), `rendermidi.cpp` `trackLevel` / `libraryNoteLevels`, `soundlibraryhost.cpp` `applyMixer`, `LiveSetWriter::kontaktVolume`, metaTag `trackDelays` (`levels`) | per part: Mixer › *Tracks…* › Level (−42.08 … +6 dB: 1 of 127 is −42.08 dB; +6 dB is Live's track Volume top, the owner's choice 2026-10-08); in Live's clips and the plain set's CC11 lane alike |
| Legato transition velocity: a legato transition plays at the map's velocity, whatever the note's (Spitfire's Performance legato picks the transition by velocity: fast 85-127 "with accent", slow 85-127 bowed; Spitfire support article 11815986). Measured 2026-10-07 (Violas - Performance, Whence's violas bars 3-7, 80 slurred sixteenths at 110 bpm, three passes, kthost on the test VM): arrival - written SD 49 ms at velocity 64, 41 ms at 100 (37-38 against 45-46 on the notes heard in every render); 159 of 180 transitions detected against 133; median arrival 68 ms against 88; median note peak 1.5 dB lower. Per-note shifts made it no tighter (HANDOFF.md). Same passage on every family (moved by octaves near each patch's test note), SD at 64 / at 100 (ms): Violas 54 / 39, Violins 1 53 / 65, Celli 53 / 70, Basses 39 / 65, Flute 35 / 65, Trumpet 26 / 35; Oboe 29, Clarinet 31, Bassoon 98, Horn 29, Tenor Trombone 31, Tuba 55 render the same at both. So only Violas - Performance gets 100; the others keep the note's own (Violins 1 at 30 / 50 / 84: 53 / 50 / 53). Only under staff text "performance" (SSO) | map `legatoVelocity` (SSO: Violas - Performance 100; other maps: the note's own) | `collect` (`libTransitionFrom`) | map only (`[legato] velocity` removed 2026-10-07) |
| Legato delay per patch and interval (times a bent transition's glide: `[tuning] bendAtArrival`) | map `legatoDelay` (5-pitch grid; octaves by template fit) | `Articulation::legatoDelayAt` | `[legato.delay]` table |
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
| Slurred notes shorter than their held technique's measured peak (map `peak`, the median over its semitones of the onset check's mf perceived peak: SSO violins' Long ~1.06 s, violas' 0.77 s) play a quicker technique of the same patch with the note's modifiers: 1 the tenuto short (Short 1.0, its `from` not applied: the slur's note-off ends it), 2 the espressivo long (Long (Rachm.)); none fits: the held one; early by the swapped technique's own onset, by `[heldNotes] byPitch` as every note (`Choice::swapped`; until 2026-10-09 always at each pitch: the owner then heard Whence's first bars uneven, its Violas / Celli per-pitch in-context fit spreading the sent times 86-109 ms; byPitch 1 keeps that: Long (Rachm.)'s per-pitch fit, `sso_rachm_onset_fit.json`, Windows VM 2026-10-08: on Whence's violin passage arrivals SD 10.8 / 11.4 ms for Violins 1 / 2 against 22.0 / 22.6 on its median and 21.6 / 26.5 on Long's; the owner, 2026-10-08: "the timing is still not as good as plain long"). The owner, 2026-10-08: slurred violins had no attack (Whence's slurred notes 136-273 ms); 2026-10-08, after comparing by ear: "let's make slurs quick = 2 the recommended preset" | 2 (Recommended; Library default 0) | `SoundLib::choose` (`chooseSlurred`), `want` (`Want::slurQuick`) | `[slurs] quick`; score: `playbackSettings` |
| A note `[slurs] quick` swaps plays as loud as its held technique would: Long less Long (Rachm.) at the note's pitch and written length (map `quickLevel` on the swapped articulation: `sso_rachm_levels.json`, re-measured on the Windows VM 2026-10-08 in the passage itself, standard test score's slurred groups, perceived loudness per group against plain Long: the dB that, through the shipped dynamics curve at the note's pitch (per-pitch curves, above), raises the passage's CC1 64 to the CC1 at which Rachm. matches Long, per register: `sso_rachm_context_match.json` → `rachm_quick_levels.py`; match CC1 Violins 1 75 / 82 / 69 (pitches to 75, to 81, above), Violins 2 76 / 67 / 64, Violas 72 / 89 (to 58, above) and Celli 91 / 105 / 98 / 92 (to 49, 52, 56, above) measured 2026-10-09 but not used: the owner, 2026-10-09, the in-context Violas / Celli fits (these levels and their in-context onsets in `sso_rachm_onset_fit.json`) made Whence bars 1-2 Celli ~8 dB louder and pulsing, so their Long (Rachm.) is back to 25a3dcbbeb's (the timing check's onsets, no quickLevel: `RACHM_UNUSED` in `gen_spitfire_sso.py`; the data stay in the files); the same at every length; log-linear in length, linear in pitch, the nearest end's beyond. Not modelled: length. In the standard test score the violas' upper register at CC1 89 reads +1.2 dB on 16ths and -1.3 on 8ths against Long; the held-out check (branch rachm-holdout, `sso_rachm_holdout.json`) -3.7 for 59-90, about -6 at 4-8 32nds), added to the note's level (CC11 down along the held expression curve, CC1 up along the articulation's dynamics curve, as the Inspector's levels: so in Live's clips and the plain set too). The owner, 2026-10-08: Long (Rachm.) "sounds quieter than plain long"; measured, Violins 1 about 6 dB under Long at 100-136 ms, Violins 2 about 3; at 1 the owner, 2026-10-08: "not the correct volume. violin 1 in bar 5 now sounds really loud relative to the viola and cello" ; that file held isolated notes' differences (5-11 dB), which the shallow dynamics curve turned into CC1 105-127, about 5 dB over Long in the passage (VM). Values replaced by the in-context ones; the owner, 2026-10-09, on build 37882502550: "1 is the right balance, make it the recommended preset" | 1 (on; Recommended 1) | `rendermidi.cpp` (`quickDb`, `marcatoLevel`), `Articulation::quickLevelAt` | `[slurs] quickLevel`; score: `playbackSettings` |
| Slurred notes of the five string sections play on their Performance legato (as under staff text "performance"; "non performance" opts out), not `[slurs] quick`'s swap; with `[levels] calibrated` as loud as Long (above, Performance legato in Recommended; the owner, 2026-10-10) | 1 (on; Recommended 1, Library default 0) | `want` (`Want::slurPerformance`), `choose` (`choosePerformance`); `rendermidi.cpp` `performanceLevel` | `[slurs] performance`; score: `playbackSettings` |
| Level rule: a slur on a string section's Performance legato plays CC1 at the written dynamic and one gain per slur (by its dynamic, median spacing and pitch) by CC11 and the patches' volumes, the rest of the part's patches lower together (above, Level rule; the owner, 2026-10-10) | 1 (on; Recommended 1, Library default 0) | `contextlevel.cpp` (`ContextLevel::gain`, `volumes`), `rendermidi.cpp` `contextDb`, `TrackDelays::read` / `patchGain`; data: map `contextLevel` from `level_rule.py` | `[levels] context`; score: `playbackSettings` |
| Section balance: every patch of a string section its map `sectionLevel` louder by its volume, with the level rule each phrase at gain + its section's level, all lower together so the loudest stays at +6 dB (above, Section balance; the owner, 2026-10-11: A′) | 1 (on; Recommended 1, Library default 0) | `contextlevel.cpp` (`ContextLevel::volumes`), `TrackDelays::patchGain`; data: map `sectionLevel` from `gen_spitfire_sso.py` `SECTION_FADERS` | `[levels] sectionBalance`; score: `playbackSettings` |
| Pedal changes after their chord (legato pedalling) | up one tick after, down the tick after (since 2026-10-06; the pianist's timing, `[pedal]`, removed 2026-10-07) | `renderSpanners` (pedal) | fixed |
| A key struck again ends its sounding note just before | on | `finishLibraryEvents` | `[notes] sameKeyEndsFirst` |
| Tuning copies: tolerance, tail, max copies. Computed when the map leaves them out (SSO's does since 2026-10-03). Tolerance: half the smallest gap between two distinct accidental values (`ScoreTuning::smallestAccidentalGap`: 16.5, MuseScore 3.6's 23-limit comma, against 16.667, a twelfth of a tone: 0.167, so 0.083 cents). Tail: each note's ring to 60 dB under (ISO 3382-1's reverberation time), twice its measured release to 30 dB under (`<Articulation release>`; T30 extrapolated as ISO 3382-1 does); unmeasured: its patch's longest, else the part's (`SoundLib::laneRing`). Max copies: 1 + the free memory (read once a run: Windows available physical memory, Linux MemAvailable) / (245 MB a copy, the owner's 1031 → 1276 MB × the parts that need copies: whose notes play at more than one tuning, `SoundLib::partsNeedingCopies`; the owner, 2026-10-04: an even share between those parts only) (`SoundLib::memoryMaxLanes`). Live and MIDI out (no varispeed, 2026-10-10): a patch without `bend=` gets a fixed copy per tuning, max copies not applied (`SoundLib::lanes` fixedTuning; LIVE.md › Microtones) | 0.083 cents; 2 × release; by memory | `SoundLib::lanes`, `libraryLaneSettings` | `[tuning] tolerance`, `tail`, `maxLanes`; score: `soundLibraryLanes` |
| A copy waits for its notes' measured release | on (map `release`) | `SoundLib::lanes` | `[tuning] waitForRelease` |
| Pitch bend instead of varispeed where the patch bends | on (map `bend`) | `updateState` (`libBend`) | `[tuning] pitchBend` |
| A legato transition's pitch bend glides when the transition arrives (note-on + the full measured legato delay for the interval / start pitch), one cent a tick from there, at the latest by the lane's next note-on; fresh attacks and slur starts bend at the note-on. The bend is the channel's: at the note-on it retuned the note before while it still sounded (SSO, Whence on 49b00a0, 2026-10-02: violas 65-83 % retuned 10-30 ms after the note-on, the new note heard ~90-130 ms after it) | on | `libraryPitchBends` (`libGlideDelayMs`) | `[tuning] bendAtArrival` (0: at the note-on) |
| Automation ramps: a value at each tick where the ramp reaches another step: a MIDI controller's 1/127 (every CC value it passes, at its tick), a plug-in parameter's 1e-4 (the lane's own precision: its points are kept to 1e-4); the owner's criterion, 2026-10-03. Live: the lanes Create Live Set / the device store are packed (`packLane`) to within one MIDI step (1/127) and one tick of MuseScore's staircase; Live's clip envelopes and a set's curves as straight pieces within one MIDI step of the curve (`Automation::flattenCurve`) | computed (no setting; `[automation] stepTicks` retired) | `Automation::Lane::events`, `renderMs4Dynamics` (lanes), `envelopeEvents`, `LiveSet::curve` | — |
| Live clips: controller carrier spacing (measured in Live 12.4.6: below) | 1 unit (EPSILON) | `LiveClips::clipNotes` | `[live] carrierEpsilon` |
| Kontakt voices per patch | 512 | `SoundLibraryHost::kontaktMaxVoices` (also the Live Set) | `[hosting] maxVoices` (`MS_KONTAKT_MAX_VOICES` wins) |
| Settle after setState before controllers (measured: below) | 0.047 s | `Vst3Plugin::settle` | `[hosting] settleSeconds` |
| Mixer volume, pan and mute on library slots apply from the next audio block, no glide (the owner, 2026-10-03: no mixer smoothing; the 5 ms glide had no source, `[hosting] mixSmoothingMs` is retired) | at once | `Vst3Synth::process` | — |
| MS3 hairpin with its own velocity change plays as in 3.6 | on | `ms3HairpinVelocity()` | Preferences (Advanced) `application/playback/ms3HairpinVelocityChange`; `MS4_STRICT` |
| Library dynamics CC ahead of the notes at its tick; shorts' velocity on the CC1 scale (calibrated levels off, or not measured) | always | `renderMs4Dynamics`, `libVelocity` | map `<Dynamics cc velocity>` |
| Live controls (Controllers window live, LiveOverrides) | always | `PartControllers::liveChanges` | not a number: no setting |
| Chunks don't end before a slurred or library note (live playback) | always | `libSlurAcross`, `libNoteAfter` | fixed (correctness: an early start can't cross a chunk) |
| Background loading pauses | 0 ms | `SoundLibraryHost` `INPUT_PAUSE_MS`, `PRELOAD_GAP_MS` | fixed (the owner chose 0) |

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
