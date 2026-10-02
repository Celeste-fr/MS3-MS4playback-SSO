# Playback adjustments: inventory and configuration

Every way this fork's playback differs from plain MuseScore 4 behaviour (beyond the MS4 note model itself), where
it lives, and how to change it. The owner (2026-10-01): "make all the playback adjustments we made fully
configurable … and editable in an ini file".

## Configuration: three layers

1. **Built-in defaults** (`libmscore/playbacksettings.cpp`, `DEFINITIONS`). A few take the sound library map's
   value as their default (`<Legato early>`, `<Onset early>`, `<Tuning tolerance tail maxLanes>`).
2. **playback.ini**, one file for every score, in MuseScore's data folder:
   - Windows: `%LOCALAPPDATA%\MuseScore\MuseScore3Evo\playback.ini`
   - Linux: `~/.local/share/MuseScore/MuseScore3Evo/playback.ini`
   - macOS: `~/Library/Application Support/MuseScore/MuseScore3Evo/playback.ini`

   Written at start when missing, with every key, its default and a comment (unit, range); never overwritten.
   QSettings IniFormat, comments start with `;`. An empty value (`key=`) means the default. Unknown keys, values
   that aren't numbers and values out of range are logged (`playback.ini: …`) and shown under the settings table;
   out-of-range values are clamped. **Edit › Reload Playback Settings** (or *Reload playback.ini* in the dialog)
   reads it again and renders every score again; no restart.
3. **The score** (saved in it, undoable): metaTag `playbackSettings` (`legato/overlapTicks=40;pedal/upAfterMs=60`),
   plus the three older per-score metaTags that stay where they were: `soundLibraryLegatoEarly` (legato/early),
   `soundLibraryOnsetEarly` (heldNotes/early), `soundLibraryLanes` (tuning tolerance / tail / maxLanes). A score
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
`hosting/settleSeconds`, `hosting/mixSmoothingMs`, and the varispeed side of `legato/glideMs` (varispeed can't
reach Live; pitch-bend glides are rendered and do).

## Inventory

Ini key = `[section] key`. "Map" = per-patch data in `share/soundlibraries/Spitfire Symphony Orchestra.xml`, made by
`tools/soundlibraries/gen_spitfire_sso.py` from the measurements (its comments say how each was measured).

| Adjustment | Default | Where it acts | Configure |
|---|---|---|---|
| Legato overlap: a slurred note lasts into the next | 30 ticks (DIVISION/16) | `rendermidi.cpp` `libOverlap` | `[legato] overlapTicks` |
| A slur's last note doesn't overlap the next note | on (MS4 overlaps) | `libOverlap`, `slurGoesOn` | `[legato] slurEndOverlap` (1: MS4's) |
| Phrase marks: a slur that plays no legato | per slur | `slur.h` Phrase marks, metaTag `phraseMarks` | per slur (Alt+S, Inspector) |
| Legato transitions start early by the patch's measured delay | 100 % (map `<Legato early>`) | `collect` / `legatoTransition` | `[legato] early`; score: old metaTag |
| Legato delay per patch and interval | map `legatoDelay` (5-pitch grid; octaves by template fit) | `Articulation::legatoDelayAt` | `[legato.delay]` table |
| Oboe Solo +60, Violins 2 +25 ms legato corrections | map (generator `SWEEP_LEGATO_CORRECTION`) | map data | `[legato.delay]` offset |
| Fast-note ramp: what an early start may take from the note before | none to 125 ms, linear to 50 % at 250 ms | `libRampShare` (transitions and held notes) | `[legato] rampFromMs`, `rampToMs`, `rampMaxShare` |
| Legato glide of a tuning copy (pitch bend steps; varispeed glide) | 30 ms | `libraryPitchBends`, `Vst3Synth::play` | `[legato] glideMs` |
| Held notes start early by their measured onset | 100 % (map `<Onset early>`) | `collect` / `onsetEarliest`, `finishLibraryEvents` | `[heldNotes] early`; score: old metaTag |
| Onset per patch and pitch (-15 dB perceived; swells per semitone) | map `onset` | `Articulation::onsetAt` | `[heldNotes.onset]` table; definition: generator |
| Shorts chosen by meant sounding length | on | `SoundLib::want`, `choose` | `[shorts] byMeantLength` |
| Meant length factors | staccato 50, staccatissimo 25, tenuto 99, portato 74.5 % (MS4's) | `want` (`Want::soundSeconds`) | `[shorts] staccato` … `portato` |
| Measured short thresholds | map `from` | `choose` | `[shorts.from]` table |
| Nominal short rule (no from=) | 90 % of `length` | `choose` | `[shorts] nominalShare` |
| Short notes' velocity balance per family, calibration, Recommended | dynamics.json | `calibratedVelocity` | Advanced Options (balance rows), dynamics.json; metaTag `soundLibraryShortBalance` |
| Even dynamic steps | off | `evenStepsEnabled` | `[dynamics] evenSteps` (or `MS_EVEN_DYNAMIC_STEPS`); mode: metaTag `soundLibraryEvenSteps` |
| Pedal changes after their chord (legato pedalling) | up 40 ms, down 90 ms after; at most 25 % of the next pedal / 50 % of its own | `renderSpanners` (pedal) | `[pedal] upAfterMs`, `downAfterMs`, `upMaxShare`, `downMaxShare` |
| A key struck again ends its sounding note just before | on | `finishLibraryEvents` | `[notes] sameKeyEndsFirst` |
| Tuning copies: tolerance, tail, max copies | map: 0.5 cents, 1.5 s, 4 | `SoundLib::lanes` | `[tuning] tolerance`, `tail`, `maxLanes`; score: `soundLibraryLanes` |
| A copy waits for its notes' measured release | on (map `release`) | `SoundLib::lanes` | `[tuning] waitForRelease` |
| Pitch bend instead of varispeed where the patch bends | on (map `bend`) | `updateState` (`libBend`) | `[tuning] pitchBend` |
| Automation ramps sent every … | 30 ticks | `renderMs4Dynamics` (lanes) | `[automation] stepTicks` |
| Live clips: controller carrier spacing | 2 units (EPSILON) | `LiveClips::clipNotes` | `[live] carrierEpsilon` |
| Kontakt voices per patch | 512 | `SoundLibraryHost::kontaktMaxVoices` (also the Live Set) | `[hosting] maxVoices` (`MS_KONTAKT_MAX_VOICES` wins) |
| Settle after setState before controllers | 1 s | `Vst3Plugin::settle` | `[hosting] settleSeconds` |
| Mixer gain glide on library slots | 5 ms | `Vst3Synth::process` | `[hosting] mixSmoothingMs` |
| MS3 hairpin with its own velocity change plays as in 3.6 | on | `ms3HairpinVelocity()` | Preferences (Advanced) `application/playback/ms3HairpinVelocityChange`; `MS4_STRICT` |
| Library dynamics CC ahead of the notes at its tick; shorts' velocity on the CC1 scale | always | `renderMs4Dynamics`, `libVelocity` | map `<Dynamics cc velocity>` |
| Live controls (Controllers window live, LiveOverrides) | always | `PartControllers::liveChanges` | not a number: no setting |
| Chunks don't end before a slurred or library note (live playback) | always | `libSlurAcross`, `libNoteAfter` | fixed (correctness: an early start can't cross a chunk) |
| Background loading pauses | 0 ms | `SoundLibraryHost` `INPUT_PAUSE_MS`, `PRELOAD_GAP_MS` | fixed (the owner chose 0) |

Not settings: the measurement definitions behind the map data (the -15 dB onset threshold, the swell rule, the
legato grid's 50 % crossing, octaves by template fit, the release as the longest over the range) are applied when
the map is generated; change them in `gen_spitfire_sso.py` (its comments hold the numbers), or override the result
per patch in playback.ini.

Tests: `tst_soundlibrary::playbackSettingsIni` (generation, parsing, unknown keys, clamping, reload) and
`playbackSettingsLayers` (default / ini / score for rendered effects); `tst_liveequivalence::playbackSettingsWidget` (the table, per-score edits).
