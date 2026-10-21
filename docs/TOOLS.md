# Tools: every measurement, verification and generation tool

The inventory of 2026-10-21, for an agent picking up the work later. It says what each tool answers, how to run it,
what it feeds and its known limits. **Open problems are in `docs/OPEN_ITEMS.md`; tools still to build are in its
"Missing tools" section.** CLAUDE.md stays the guide to the architecture; this file is only about tools.

Conventions:

- **Machine**: *Linux* = the dev machine (no Kontakt, no sound you can hear); *VM* = the Windows test VM (Kontakt 8 +
  SSO, a Live 12.4.6 trial, an Ample Guitar M demo; reached over ssh with the key in CLAUDE.md's VM notes). Results from
  a stand-in (test synth, sfizz) are not results with the library: say which.
- **kthost 3-step**: many VM measurements are (1) `events` on Linux, writing kthost event files (`seconds on|off|cc|pb …`
  lines, the format of dumpEvents' `<route> museScore.txt`) and a `list.txt`; (2) renders on the VM:
  `C:\claude\kt\kthost.exe "<Common Files>\VST3\Kontakt 8.vst3" <setup>.vst3state <events>.txt <out>.wav [opts]`, the
  setups copied from MuseScore's setups folder; (3) `analyze` on Linux (or the VM's Python 3.12), then `report`/`summary`.
- **Wrappers**: Linux builds and tests run through a wrapper that bind-mounts the worktree over the build dir's
  configured path (CLAUDE.md › Building and testing). Current form: an overlay folder per branch,
  `~/MuseScore/ms3fork/ovl-<branch>/<xx>.sh '<command>'` (flock on `private/BUILD_LOCK`, overlayfs build.rel over
  `build-integrate`); older form `ninja-<name>.sh` / `run-<name>.sh`; builds one at a time through `build-queue.sh
  <wrapper> ninja -j6 <target>`. Below, `WRAP` stands for that wrapper.
- **VM etiquette**: one VM-heavy job at a time; scripts take `C:\claude\quiet.lock` and a local `private/VM_PAUSE`
  while they run and remove them after; never touch the owner's setups or `AGM_info.xml`.
- **Private, not in repo**: a path under `~/MuseScore/ms3fork/private/` (the owner's renders, scores, plug-in dumps).
  Those scripts are one-offs with hard-coded paths; the ones worth turning into repo tools are listed under
  "Missing tools" in OPEN_ITEMS.md.
- "Last" = last commit touching the file (`git log -1`), or the file date for files outside the repo.

Tool count: about 200 entries below (≈ 75 committed Python/shell tools in `tools/soundlibraries`, ≈ 45 in
`tools/playbackverify` / `tools/live` / `tools/lagprobe` / generators, ≈ 20 in-app command-line jobs, ≈ 15 env-driven
mtest tools, ≈ 50 private and VM tools).

---

## 1. Regression: "did this change move anything?"

| Tool | What it answers | Command (machine) | Outputs / feeds | Last | Limits |
|---|---|---|---|---|---|
| `tools/run_fork_tests.sh <build dir> [--no-build]` | every fork test passes | Linux, through `WRAP` | summary table, exit 1 on a failure | 10-18 | leaves out `tools/live/test/test_timing.js` and `test_load.js` |
| `tools/check_windows_names.sh [base]` | added C++ uses a windows.h macro name (`near`, `ERROR`, bare `Chord` in mtest/audio …) | Linux | exit 1 if found | 10-20 | run before any push meant for a Windows build |
| **`tst_liveequivalence dumpEvents`** | every event MuseScore sends per route, as playback renders it | Linux: `WRAP 'cd mtest/libmscore/liveequivalence && MS_DUMP_SCORE=<score> MS_DUMP_MAP=<map.xml> MS_DUMP_OUT=<dir> [MS_DUMP_CHUNKED=1] [MS_DUMP_CALIBRATION=<dynamics.json>] [MS_DUMP_USER_CALIBRATION=…] [MS_DUMP_SETTINGS="id=v;…"] [MS_DUMP_LIVE=1] [MS_DUMP_TIMING=<file>] QT_QPA_PLATFORM=offscreen ./tst_liveequivalence dumpEvents'` | `<route> museScore.txt` (kthost format), `routemap.txt`, `notes.txt` (pitch, cents, slur), `trace.txt` (each note's choice / articulation / velocity / glide), `gains.txt`, `headroom.txt`, `volumes.txt`, `tunings.txt`, `instances.txt`, `bars.txt`; with `MS_DUMP_LIVE` also `<route> live.txt`, `kontakts.txt`, `routes.txt`, `multis.txt` | 10-21 | the standard before/after proof for "sound-identical"; compare two dumps by hand or with the private `cmpdump.py` |
| **`tst_playbackaudit auditScore`** (the playback audit) | the rendered library events against the map's own model (not the sound) | Linux: `WRAP 'cd mtest/libmscore/playbackaudit && MS_AUDIT_SCORE=<score> MS_AUDIT_OUT=<txt> [MS_AUDIT_SETTINGS=recommended\|library\|<metaTag>] [MS_AUDIT_MAP=<map>] [MS_AUDIT_DUMP=<dir>] QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore'` | counts and lines per kind (table below); `MS_AUDIT_DUMP` writes the heard check's inputs | 10-21 | judges the model, not the sound: a wrong model passes. With plainPerformance on, the strings' audit is nearly vacuous. Run on Whence before any build goes to the owner |
| `tst_liveequivalence editLagIdentical` | the edit-lag speedups change nothing | `MS_EDITLAG_SCORE` / `_SETTINGS` / `_TIMES=1` | pass/fail, timings | 10-17 | |
| `tst_soundlibrary evenTrimTrace` | two builds give the same even-levels / trim trace | `MS_TRIM_SCORE`, `MS_TRIM_MAP`, `MS_TRIM_OUT`, `_SETTINGS`, `_CALIBRATION` | `trace.txt`, `times.txt` | | |
| `tools/playbackverify/compare_renders.py <dirA> <dirB> <part> <notes.txt\|-> [--json]` | note by note (f0, level, onset, spectrum) and whole-part null of two hosted renders | Linux | report / json | 10-16 | crashes (ZeroDivisionError, line 124) on a silent note's spectral centroid in "-" mode |
| `tools/playbackverify/part_variants.py <unzipped mscz> <outdir>` | Whence variants: one part on the library, hostedTuning 0/1 | Linux | `.mscz` variants | 10-16 | Whence-specific (hard-coded parts) |
| `tools/playbackverify/compare_sweeps.py`, `compare_octave_sweeps.py <dirA> <dirB> [--md]` | build against build on the old sweep scores | Linux | tables | 10-01/02 | the sweeps predate Performance patches |
| `~/MuseScore/ms3fork/ab/roundtrip2.py [--mtest N] [--list f] [scores…]` | fork → 3.6.2 → fork unchanged (XML, PNG pages, MIDI) | Linux, xvfb | `roundtrip2/summary.tsv`, `<name>.diff` | 09-26 | **paths stale**: calls `install-ms4/` and `ms37-ms4/`, which no longer exist; re-point before use. Not in the repo |
| `ab/trace/regress.sh [name…]` | MS4 note model: fork's FluidSynth calls against MS4 4.7.5's, sample-exact (gdb dprintf) | Linux | `regress.txt`, `<name>.diff` | 09-25 | same stale paths; `MS4_STRICT=1` |
| `ab/render.sh`, `compare.py`, `nulltest.py`, `audible.py`, `ltas.py`, `bandtime.py`, `dewarp.py`, `dry_ms4.py`, `run_ab.sh`, `run_demos.sh` | MS4 vs fork audio: level, 48-band timbre, onsets, nulls | Linux | `results-*.txt` | 09-24/25 | < 1.5 dB loudness and < 2 dB timbre ≈ identical; stale paths |
| `ab/trace/*` helpers (`events.py`, `diffnotes.py`, `ccstate.py`, `chandiff.py`, `progcheck.py`, `chordcmp.py`, `sameinstant.py`, `drift.py`, …), `ab/corpus.py`, `techgen.py`, `corpus/mk*.py`, score cutters | MS4 regression corpus and diffing | Linux | | 09-24/25 | |
| `tst_midi` (97), `eventsMs4` | MS3 render against 3.6 references; MS4 against `-ms4-ref.txt` | Linux | | 10-04 | regenerate refs only for an intended note-model change |

**Audit kinds** (`libmscore/playbackaudit.*`, VERIFY.md › Playback audit):

| Kind | Severity | Rule |
|---|---|---|
| OVERLAP | fail | a note sounds > `keepMs` into the next of its line on one route, or a key is struck while still sounding |
| UNMEASURED | info | a swapped / early note whose onset isn't from an in-context fit, or a technique without onset |
| CAPPED | info | an early start cut (keepMs, pass start, chunk start, grace, arpeggio, transition) |
| ARRIVAL | fail > 50 / warn 30-50 ms | (sent + onset) − written against the line's note before (Rasch 1979); transitions by `glideArriveMs`, plain notes by `plainShift` / `plainAttack` |
| LEVEL STEP | info | level step under one slur at one dynamic; 0.69 dB proposed (Jesteadt et al. 1977), owner to approve |
| DROPPED | fail | a legato transition < 15 ms after the legato note-on before on its route at velocity ≤ 64 |
| LEAP | warn | a transition beyond 1200 cents |
| CHORD | fail | on a `mono="1"` patch, a note-on < 15 ms after one still sounding on its route |

---

## 2. Timing: "when is each note heard?"

### 2a. In the real sound (VM)

**Heard check** (`tools/playbackverify/heard_check.sh`, `heard_check.py`, `heard_export_vm.cmd`, `heard_check_vm.cmd`;
VERIFY.md › Heard check; last 10-18). Each library note's heard time in Kontakt + SSO against its written time and the
audit's prediction.

```
HC_RUN=<WRAP> HC_VM=user@<vm address> tools/playbackverify/heard_check.sh <score> [recommended|library|<metaTag>] [partA,partB]
```

- Env: `HC_RENDER` export (default: `--verify-playback --verify-wav` with `MS_VERIFY_ROUTES=1`, as MuseScore exports:
  slots, gain stage, trim, lanes) or kthost (bare patches, offset 1.0 s); `HC_SPLIT` parts (default, one run per part) /
  whole; `HC_EXE` the VM's MuseScore (≥ build 443; **set it to the build under test**); `HC_VM` (**set it**: the
  script's default address is stale), `HC_KEY`, `HC_VMDIR`, `HC_WORK` (default `~/.cache/heardcheck/<run>`),
  `HC_KEEP=1`, `HC_RUN` or `HC_BUILD`.
- Steps: audit dump with `MS_AUDIT_DUMP` → events → VM render → VM folder deleted → `heard_check.py analyse <dump>
  <wavs> [--export] --report --json [--parts]` → `report.txt`, `report.json`; exit 1 on FAIL, 2 if nothing measured.
- Measurement: attacks by the perceived envelope, −15 dB under the peak, −30 ms latency (Vos & Rasch 1981; −6 dB
  reported as `heardAltMs`, not judged); a note within 15 dB of the one before is "masked" (its tail extrapolated);
  transitions by pYIN (hop 128): arrived after 3 frames within 30 cents, searched to 0.7 s.
- Flags: ARRIVAL fail > 50 / warn 30-50 ms; RHYTHM (3-IOI windows, max share difference) fail > 0.10 / warn > 0.05
  (approved 2026-10-16); LEVEL warn > 0.69 dB under one slur (proposed); UNHEARD (never within 30 cents); DRIFT
  (measured − predicted per patch × situation, |median| > 20 ms).
- Scores: Whence (owner's, never in the repo), `share/verifyplayback/Heard passages.musicxml`, `Mixed
  techniques.musicxml`. Run time (quiet VM): export Whence ≈ 5 min, passages ≈ 7 min, Mixed ≈ 5 min; kthost slower
  (Basses routes +60 s each: the probe never passes −60 dBFS); analysis 1.5-4 min.
- Limits: **one run = one take** (Performance patches differ take to take, as much as the two renderers differ: run
  twice for anything that matters); masked readings of a re-attacked held note are unreliable (Celli bar 11 +539 ms);
  a hard step onset reads ≈ 24 ms early at −15 dB; 30 cents vs the model's 9/10 of the interval gives a small
  negative drift by definition; chords on one route are not measured; kthost reads Performance attacks 30-60 ms later
  than export (trust export); slow techniques (sul tasto, flautando, harmonics) read −580 … +870 ms (no rule for
  them); split runs compare levels only within a route; from 7f51a91979 to 74155f2b0c every export run measured 0 of 0.
- Tests: `tools/playbackverify/test_heard_check.py` (synthetic sawtooths, needs librosa, run without `-I`).
- Feeds: the go/no-go before any build goes to the owner; `plainAttack` (`sso_plain_performance_shift.json`
  `attackMs`); every timing finding since 2026-10-16.

**Private analysis of heard-check reports** (private, not in repo):

| Script | What | Command |
|---|---|---|
| `private/plain-timing/scripts/spread.py` | beat spread across sections (max − min heard − written per written time), per-kind medians, at −15 or −6 dB | `spread.py <report.json> [heardMs\|heardAltMs]` |
| `private/plain-timing/scripts/tdrift.py` | Performance transitions, heard − predicted, by section and tempo | `tdrift.py <report.json>` |
| `private/mixed-techniques/scripts/mixed2.py`, `mixed_analyse.py` | per technique switch: heard − written, All techniques against Performance attacks / transitions, levels | `mixed2.py <report.json> [out]` |
| `private/mixed-techniques/scripts/mixroutes.py` | adds the export's route WAVs up on score time (start lead out) | `mixroutes.py <hc work> <out.wav> [parts]` |
| `private/solo-clicks/scripts/soloclips.py` | per-section solo A/B clips with a click on every written beat (from `bars.txt`), count-in, MP3 | paths hard-coded |
| `private/*/scripts/clips.py` (plain-timing, mixed-techniques, bars9-12, accent-level) | A/B clips with a shared gain and pre-roll, notes JSON | one per study |
| `private/plain-timing/cmp.py <dumpA> <dumpB>` | patch / articulation changes between two `predicted.tsv` | |
| `private/*/hc.sh`, `batch*.sh` | heard-check wrappers with the VM locks and a MemAvailable gate | hard-coded VM address |

### 2b. Map onsets and legato timing (VM measurement jobs, read on Linux)

| Tool | Measures | Command | Feeds | Last | Limits |
|---|---|---|---|---|---|
| `MuseScore3Evo --extract-library <lib> --check-timing [--all-sounds]` (VM) | when each articulation speaks, its length and ring, legato transitions, at pp/mf/ff (5 ms windows) | in-app, supervised, resumable | `background timing check.log`, check zip → `timing_from_check.py <zip…> [-o sso_articulation_timing.json]` | 10-01 | → map `release`, values |
| `--extract-library <lib> --check-rest [--rest-parts range,repeats,controls,legato,onset,shorts,legatolengths,legatopitches]` (VM) | range per semitone, round robins (8), every control, legato grid (9 velocities × 14 intervals); onsets, shorts, legato lengths/pitches when named | env `MS_EXTRACT_LEGATO_STARTS`, `_INTERVALS`, `MS_EXTRACT_HANG_MINUTES`, `MS_EXTRACT_REDO` | `rest_from_check.py <zip…> [-d dir]` → `sso_sound_range`, `sso_sound_repeats`, `sso_sound_controls`, `sso_legato_grid`; `onset_from_check.py` → `sso_sound_onset`, `sso_short_lengths`, `sso_legato_lengths` | 10-01/03 | → `onset`, `peak`, `from`, `legatoDelay`; single notes (round robins alternate) |
| `release_long_tail.py <patch> <out> [--tail 25]` (VM kthost) + `releases_from_long_tail.py <json…>` | releases with a 25 s tail | | `sso_sound_range` row 9 (`mfReleaseMs`) | 10-03 | SyntaxWarning, harmless |
| `onset_detect.py loudness\|harmonics <meta> <wavA> <wavB> <out>`, `test_onset_detect.py [--report]` | onset detectors and their accuracy | either | `onset_detect_accuracy.json` | 10-09 | no reading while the note two back rings within 15 dB |
| `onset_fit_from_split.py --isolated\|--split\|--sound "Long (Rachm.)" [--context]\|--smooth 1 --wavs <dir>` | Long / Long (Rachm.) onset fits so sections arrive together | Linux on VM kthost renders | `sso_long_onset_fit`, `sso_rachm_onset_fit` → `onset` | 10-09 | Violas / Celli in-context fits unused (`RACHM_UNUSED`) |
| `make_rachm_holdout.py <out> [instr…]`, `rachm_holdout/` | held-out scores for the Rachm. swap | Linux → 3.6.2 → VM | `sso_rachm_holdout.json` (no consumer) | 10-09 | |
| `legato_pair_scan.py events\|analyse` (kthost) | transition arrival per start pitch × interval | | `sso_legato_pairs.json` (Violas; unused) | 10-07 | the isolated table doesn't predict real runs |
| `octave_measure/` (`mkvariant.ps1`, `job-oct.cmd`, `analyze.py`, `fam.py`) | ±12 slur timing at many start pitches | VM + Linux | `octave_measure.json` → `octaveUp` / `octaveDown` | 10-02 | **not re-runnable**: hard-coded `/tmp/oct` and another worktree's path |
| `legato_glide/gen.py <work>`, `glide.py <work> [out]`, `fit.py <work> [out]`, `f0.py` | each transition's leave / arrive (YIN); Kontakt's one-pole bend smoothing | VM kthost + Linux | `sso_legato_glide` → `bendGlide`; `sso_bend_smoothing` → `bendSmoothing` | 10-11/15 | five string sections' Performance patches only |
| `legato_switch/probe.py events\|analyse`, `run.cmd`, `conv.py`, `jumps.py` | what makes Performance legato arrivals jump | VM + Linux | `sso_legato_switch.json` (no consumer) | 10-16 | take A, Violins 1/2, stopped early |
| `tools/playbackverify/make_sweep_scores.py`, `fix_sweep_ids.py`, `analyze_sweep.py <notes.json> <wav>`, `octave_synth_check.py` | legato / held / short onset per instrument at q=60; method check with known answers | Linux → 3.6.2 → VM `--verify-playback --verify-wav` → Linux | legato grid, onset tables | 10-01 | |
| `make_fastrun_scores.py`, `fix_fastrun_ids.py`, `analyze_fastruns.py`, `choose_keep_ms.py`, `fit_fast_share.py` | fast slurs on the beat; `keepMs` = 40 | same | `keep_ms_choice.json`; `fast_share_fit.json` (historical: setting removed 10-07) | 10-02/07 | |
| `qt_bends/abl.py` (branch `qt-bends` 67f90e1a06, not in this tree) + heard check | plainShift per patch | | `sso_plain_performance_shift.json` → `plainShift` | 10-21 | not merged |
| `private/plain-shift/tools/abl_plain.py`, `latency.py` (private, not in repo) | Violas ablation: variants through kthost judged by the heard check | | | 10-18 | |
| `--extract-pitch-bend` (in-app, ~25 s a patch) | bend range / cleanliness per patch | VM | `measurements_from_extract.py <extract…> [-o sso_patch_measurements.json]` → `bend` | 09-29 | |

---

## 3. Level and balance: "how loud is each note, part and marking?"

### 3a. In-app

| Job | Measures | Command | Outputs | Limits |
|---|---|---|---|---|
| `--extract-library <lib> --check-dynamics [--all-sounds]` (VM) | velocity / CC1 → perceived level per patch, every sound with `--all-sounds` | in-app (~1 min for Violas) | working `dynamics.json`; `dynamics_from_check.py <zip…> [-o sso_sound_dynamics.json]` | one note per point: round robins alternate (Celli's Long came out flat) |
| `--even-levels <score> [--even-levels-out <dir>] [--even-levels-parts …] [--even-levels-bars a-b] [--even-levels-ticks a-b] [--even-levels-play] [--verify-library …]` (VM) | slurred Performance passages through the plug-in, per-note CC11 correction | env `MS_EVEN_LEVELS_WAV` keeps renders | `result.json`, `<name> even.mscz` | the GUI's background measurer does the same near edits (`libmscore/evenauto.h`) |

### 3b. Committed sweeps and data tools (tools/soundlibraries)

| Tool | Measures / computes | Command | Feeds (map attribute) | Last | Limits |
|---|---|---|---|---|---|
| `calibration_from_sound_dynamics.py [-d] [-r] [-s] [-m] [-o]` | the shipped `<library>.dynamics.json` | Linux | read at runtime (`SoundLib::readCalibration`) | 10-12 | |
| `string_dynamics_sweep.py events\|analyze\|curves` (kthost) | five sections' Long and velocity techniques, each point 4× | | `sso_string_dynamics` → calibration | 10-12 | |
| `rachm_register_sweep.py`, `rachm_quick_levels.py [--check]` | Long (Rachm.) along CC1 at several pitches; quickLevel | kthost / Linux | `sso_rachm_register_curves` (calibration `"pitches"`), `sso_rachm_levels` → `quickLevel` | 10-09 | input `sso_rachm_context_match.json` has no committed producer |
| `sso_cc1_sweep.py events\|analyze\|merge` (kthost) | Long level against CC1 per pitch (strings, 23 winds, 15 gap patches) | needs pyloudnorm, mosqito | `sso_cc1_curves`, `sso_winds_cc1_curves`, `sso_gap_cc1_curves` → `longLevel` via real_range, balance | 10-14 | |
| `section_balance_winds.py <TU csv> …` | section balance M (TU Berlin sound power) | Linux, `-I` | `sso_winds_section_balance` → `sectionLevel` | 10-14 | TU CSV not in repo |
| `clark_luce_table2.py`, `section_balance_cl.py <14 inputs>` | Clark & Luce balance | Linux | `sso_cl_section_balance` → `sectionLevel2`, decaying `realSteps` | 10-15 | gap patches a family member's level (assumption); harp ±4, timpani ±3 dB; log in BALANCE_LOG.md |
| `tu_directivity.py singletones\|recordings`, `tu_transient.py compare\|levels` | TU Berlin directivity per dynamic; sound power of decaying tones | Linux, HTTP range reads | `tu_directivity`, `tu_decaying` → section_balance_cl | 10-15 | |
| `strings_ensemble.py events\|analyze` | what Strings Ensemble mixes per key | kthost | `sso_strings_ensemble` → +11.93 dB | 10-15 | |
| `spheres_timpani.py <Spheres dir> …` | timpani at written markings (Spheres, Romeo and Juliet) | Linux | `spheres_timpani` → timpani sectionLevel2 / realSteps | 10-15 | audio local only |
| `real_range.py [--rule-d] …`, `test_real_range.py` | player's dynamic range against SSO's | Linux | `sso_real_range` → `realSpan`, `longLevel`, `realPlay`, `realSteps` | 10-15 | family spans for bass clarinet / contrabassoon / bass trombones (assumption) |
| `technique_offsets.py [spheres_k550_staccato.json] [out]` | D for `calibrated` 2 | Linux | `technique_offsets` → `heldOffset` | 10-15 | one pooled −3.4 dB; Violins 2 / Violas not measured; the Spheres input has no committed producer |
| `mic_levels.py events\|analyze\|extra` | each mic alone, fader law, Mic Mix Distance | kthost | `sso_mic_levels` → `micLevels`, `micDefault`, `<Mics>` | 10-15 | timpani Close empty offline; extras take their main's |
| `crest_sweep.py events\|analyze\|summary` | loudest 50 ms over calibration | kthost | `sso_crest` → `<Dynamics crest>` 14.5 dB | 10-15 | strings only |
| `performance_context/grid_score.py`, `grid_ana.py cells\|groups\|parts`, `grid_kthost.py`, `level_rule.py`, `perceived.py`, `run.cmd` | the level rule's grid as MuseScore plays it | VM `--verify-playback --verify-wav` or kthost + Linux; env `GRID_METRIC` (default P0) | `sso_performance_context` → `contextLevel*` | 10-17 | `level_rule.py` hard-codes metric P |
| `derived_numbers.py marcato\|levels\|sensitivity` | constants quoted in code (±35.5 dB …) | Linux | code constants | 10-08 | |
| `tools/playbackverify/make_levelrun_scores.py`, `analyze_levelruns.py`, `make_dynamics_ladder.py` | legato vs attack level; ppp…fff ladder per section | Linux → VM | | 10-02/12 | |
| `tools/playbackverify/even_need.sh <score> <takes> [parts]`, `even_need.py` | does Even levels still matter in the real sound (fast slurs, 2 conditions × N kthost takes) | Linux + VM; env `HC_RUN`, `HC_VM`, `HC_KEY`, `EN_WORK`, `EN_SETTINGS` | `even_need.json` | 10-16 | |
| `tools/playbackverify/loudness.py` | `perceived_envelope`, `power_envelope` (port of `articulationcheck.cpp`) | module | used by heard check, even_need, analyzers | 10-01 | |

**Even-levels research tools** (all kthost 3-step, 10-12/13; their results set constants in `evenlevels.h` /
`evenauto.h`, not map attributes): `legato_level_scan.py`, `legato_levels_from_scan.py` (setting removed),
`transition_level_scan.py`, `transition_levels_report.py`, `even_levels_test.py` (needs a dump with
`MS_DUMP_SETTINGS`), `even_levels_check.py` (checks MuseScore's own even-levels playback), `perturbation_scan.py`
(effect ≈ 2 notes, Basses ≈ 4), `context_reuse_scan.py`, `superposition_scan.py` (r 0.65), `layer_isolation.py`
(Kontakt mixes groups linearly; on scratch setups only), `swell_vs_length.py`, `slow_slur_test.py` (→ `SLOW_IOI_S`),
`tempo_line_test.py`, `even_window_check.py` (→ `EvenLevels::nodeTime`), `even_fast_checks.py`, `even_gap_check.py`
(→ `GAP_S`, `earlyStopDb`).

### 3c. Private level tools (private, not in repo)

| Script | What |
|---|---|
| `private/accent-level/scripts/mkgrid.py`, `mkfull.py`, `mkfullcb.py`, `mkiso.py`, `mklvl.py`, `mkexp.py` + `anagrid.py`, `anafull.py`, `anafull2.py`, `anaiso.py`, `anaexp.py` | attack against attack level grids by pitch, length, level (ABBA); `anafull2` normalises to a target loudness. Produced `sso_accent_attack.json` (→ `accentedLevel`) |
| `private/plain-accents/scripts/mkatt.py`, `anaatt.py`, `notemeasure.py`, `clipmeasure.py`; `accent-level/scripts/ctxmeasure.py` | accented attack vs plain; in-context note levels on per-route renders |
| `private/accent-level/scripts/mkclipev.py <dump> <out> <tag>` | dump → kthost event files for every string route + `list.txt` + `gains.tsv` |
| `private/accent-level/scripts/kt.ps1 -run <dir>` (VM) | kthost over `list.txt` with the VM's setups, read only, 300 s watchdog |
| `private/accent-spheres/scripts/rzip.py`, `get.py`, `fetch_stems.sh`, `stl.py`, `look*.py`, `xc.py`, `events.py`, `measure.py`, `stats.py` | single stems from the Spheres Zenodo zip by HTTP range; accent measurement, two-level bootstrap (sf at f +5.0 dB, 90 % [+3.8, +6.0]) |
| `private/bars9-12/scripts/accent_levels.py`, `score_arts.py` | per-note perceived peak from a verify folder + dump; each staff's articulations by written time |
| `private/sso-legato-fixes/tools/mkst.py <src.vst3state> <out> <prefix>`, `mkev.py`, `ana.py` | measurement-only state copies (random sources / CC102 ramp off); legato grid events; per-note level and swell |
| `private/cc119-null/null.py <r dir>`, `run.cmd`, `mkst.sh` | null residuals between kthost renders (whole, worst 100 ms, peak); `$vfm12` state variants through `scriptValuesSet` |

---

## 4. Tone, noise, dropped / cut notes

| Tool | Answers | Command | Outputs | Limits |
|---|---|---|---|---|
| **`--verify-playback <score\|folder\|default> [scores…] [--verify-out <dir>] [--verify-library <lib>] [--verify-audio <wav>] [--verify-wav] [--verify-shareable]`** (VM; `--verify-audio` also Linux) | renders as File › Export › Audio and checks the audio against the notes sent: missing attack / note, cut short, silence, clipping, drift | env `MS_VERIFY_ROUTES=1` (every route alone, with `--verify-wav`), `MS_VERIFY_LOAD_WAIT` (s, default 300), `MS_VERIFY_FAULT` (`pedal-drop:<ms>`, `drop:<n>`, `truncate:<n>:<ms>`), `MS_VERIFY_FAULT_LOG` | `Playback verify <date>` folder/zip: `summary.txt` (with the render preflight), `report.json`, `strikes.tsv`, `notes.tsv`, `events.tsv`, `clips/`; one part's instances at a time | can't judge sample choice or balance; thresholds set on one piano score; **false positives on Performance legato** (offset alias, short legato notes, 12-TET bins for quarter tones: OPEN_ITEMS.md) |
| `tools/playbackverify/read_verify_report.py <zip\|dir> [--near s] [--part] [--json]` | reads a verify report | Linux, stdlib | verdict | |
| `make_verify_scores.py [folder]` | default `share/verifyplayback/` scores | Linux | | |
| `try_with_testsynth.sh <build> <install> [work] [scores…]`, `check_faults.py --setup\|<out>` | does the verifier find injected faults and nothing else | Linux, test synth | found / missed / unexplained | |
| `private/bars9-12/scripts/offset_curve.py <wav> <notes.tsv> [gain]`, `legato_level.py` (private) | replicas of the verifier's `bestOffset` curve and legato held level (12-TET vs tuned bins) | Linux | | needed to fix the detector |
| `layer_isolation.py` | band powers of the mix and of each sample-group family | kthost | `sso_layer_isolation.json` | nearest thing to a tone tool |
| `tools/soundlibraries/sfizz-test/` (`make_uacc_sfz.py`, `make_setup.py`, `fingerprint.py`) | which UACC articulation each note played, on sfizz | Linux | 12/12 on 2026-09-25 | stand-in only |
| `sso_bad_samples.json` (→ `avoid`) | faulty round robins | made by a scratch kthost script, no committed producer | | |

There is no committed tool for timbre / noise of a patch.

---

## 5. Live equivalence: "does Live play what MuseScore plays?"

| Tool | Answers | Command | Outputs | Limits |
|---|---|---|---|---|
| **`--live-equivalence <folder> <score> [--live-equivalence-wav] [--live-equivalence-strict]`** (VM, real SSO) | MuseScore's own render against the set's clips as the device plays them, through the set's embedded states and Live's mixer (modelled, not Live itself) | env `MS_LIVE_EQUIVALENCE_FAULT=no-bend,no-mixer,no-params,no-controllers` | `report.txt`, `report.json`, `preflight.txt` | strict: corr ≥ 0.999, residual ≤ −30 dB, each note ≤ 0.5 dB / 1 ms; Kontakt: median ≤ 1 dB, each ≤ 3 dB, onsets ≤ 5 ms. Not real Live |
| `tst_liveequivalence` (65+ tests) | the same on Linux with the test synth | `WRAP 'cd mtest/libmscore/liveequivalence && QT_QPA_PLATFORM=offscreen ./tst_liveequivalence'` | | stand-in |
| `--create-live-set <out.als> <score>` | writes the plain set (records what it wrote since 2ef9dd9dfb) | VM or Linux | `<out.als>`, `<out.als>.txt` | |
| `--update-live-set <set.als> <score>` | edits a closed plain set in place | | `Backup/<name> [date].als`, `<set>.update.txt` | untried in real Live |
| `--live-set-readback <file.als>` | loads each plug-in state of a set, reads the panel parameters back | VM | `<file.als> readback.txt` | |
| `tst_liveequivalence utilitySet` | writes a "MuseScore gain" Utility into a set | `MS_UTILITY_IN`, `MS_UTILITY_OUT`, `MS_UTILITY_DB="<track>=<dB>,…"` | `.als` | |
| `tst_liveequivalence plainSetLinked` (tool part) | writes plain / linked sets to open in Live | `MS_PLAIN_SCORE`, `MS_PLAIN_MAP`, `MS_PLAIN_OUT`, `MS_PLAIN_SETTINGS`, `MS_PLAIN_DEVICE`, `MS_PLAIN_DEVICE_AT`, `MS_PLAIN_SETUPS`; also `MS_PLAIN_SET_OUT`, `MS_PLAIN_LINKED_SET_OUT`, `MS_LIVESET_OUT` | `.als` + reports | |
| `tools/live/make_device.py` | builds `MuseScoreLink.maxpat` / `MuseScore Link.amxd` from `MuseScoreLink.js` | Linux | device | re-run after any `.js` change |
| `tools/live/MuseScoreEnvelopes/` | Control Surface script (UDP 9005) writing / reading Session-clip envelopes | installed in Live | `MuseScoreEnvelopes.log` | Session clips only; `create_event` ignores curves |
| `tools/live/test/` (`test_*.js`, `fakelive.js`, `fake_live_server.js`, `fake_envelopes_server.py`, `test_envelopes.py`, `measure_param_ramp.js`, `compare_als_skeleton.py`) | device logic against stand-in Lives; a written set's element tree against sets Live saved | `node tools/live/test/test_X.js` | | `compare_als_skeleton` skips without the owner's reference sets |
| `tools/live/research/` (`MuseScoreAuto/`, `edges.py <wav> [bpm]`, `storage/`) | prototypes: Arrangement automation, stepped automation timing in Live's export, M4L store persistence | | | not shipped |
| VM `C:\claude\lc1012\seq.ps1`, `watch.ps1`; `ug\seqrun.ps1`; `psave\*.ps1`; `uh\` (VM, not in repo) | drive Live 12 on the VM desktop (open a set, play, screenshot, save profiling, Update Live Set GUI check) | VM scheduled task | screenshots, logs | one-offs, owner's Live projects |
| `private/liveset-readback/`, `ar-tools/`, `ce-tools/`, `vl-tools/` (private, not in repo) | Live automation / clip-envelope / velocity-lane research (OSC probes, `.als` sets) | | | `vl-tools` holds Live sets: never commit |

---

## 6. Map generation and library extraction

| Tool | Makes | Command | Inputs | Last | Limits |
|---|---|---|---|---|---|
| **`tools/soundlibraries/gen_spitfire_sso.py <reaticulate>/userbanks/Spitfire/Spitfire-Symphony_Orchestra.reabank "share/soundlibraries/Spitfire Symphony Orchestra.xml"`** | the SSO map (never hand-edit) | Linux | its tables (`I`, `T`, `SPITFIRE_ADD/DROP/RENAME`, `DRUMS`, `KEYSWITCHED`, `LENGTHS`, `EXPECT`, `MONO`, `KEY_SHIFT`, `ROLL_ON_HIGH_VELOCITY`, `RACHM_UNUSED`, corrections) and ≈ 30 `sso_*.json` (table below) | 10-21 | diff the XML after; needs a reaticulate clone |
| **`tools/soundlibraries/gen_ample.py <amphost info dump> [out]`**, `test_gen_ample.py` | `Ample Guitar M.xml` | Linux; dump private: `private/ample-map/agm-info.txt` | tables `GUITAR`, `STRUM_LEVELS`, `GUITAR_FX`, `SAMPLE_SETS`, `KEYSWITCHES`, `state_settings`, `VELOCITY_LAYERS`, `CONTROL_DEFAULTS`, `INSTRUMENT_IDS` | 10-20 | AGM 4.1.0 only; ranges not seen in the GUI assumed; Master Tune left out |
| `check_spitfire_expressionmaps.py <dir> [map]` | CC32 names against Spitfire's expression maps | Linux | Spitfire's files (not in repo) | 09-25 | prints only |
| `--extract-library <lib> [--extract-patches all\|mapped\|<file>] [--extract-pitch-bend] [--extract-controllers] [--extract-plan <file>]` (VM) | plug-in data per patch: parameters, script values, controls, whether it sounds; supervised (`superviseExtract`, hang `MS_EXTRACT_HANG_MINUTES`), resumable | in-app; ≈ 1 h for 700 patches | check zip | | test knobs `MS_EXTRACT_TEST_CRASH/_HANG/_CRASH_STEP/_BROKEN` |
| `read_plugin_data.py`, `controllers_from_extract.py`, `controller_defaults_from_extract.py <extract> <map> <out> [--source]`, `measurements_from_extract.py`, `links_plan.py` | readers of extracts | Linux | `sso_controller_defaults` → `patchDefault`; `sso_patch_measurements` → `bend` | 09-27 … 10-15 | |
| `--scan-keys <lib> [--extract-patches <file>]` (VM) | keys of patches whose keys aren't known, by listening (≈ 2.6 min a patch) | in-app | `background extract.log` | | |
| `--window-pictures <lib>` (VM) | pictures of the percussion patches' Kickstart windows, each drum icon clicked | in-app | pictures → OCR → `sso_percussion_hits.json` (→ `<Drum>`) | | look at the pictures before changing the map (review-page rule) |
| `read_check_names.py`, `read_loaded_patches.py` | OCR of Check-articulations pictures | Linux (tesseract) | prints | 09-28 | OCR misreads |
| `extract_library_files.py ["D:\…"] [--report …]`, `nki_articulation_details.py`, `test_extract_library_files.py` (`KONTAKT_NKI`) | the library's files without Kontakt (decrypts nothing) | owner's PC / CI exe | `library.json` → `sso_nki_*` | 10-01 | |
| `make_setups.py`, `test_make_setups.py` (`SSO_NKI`, `SSO_SETUP`, `SSO_EMPTY`) | `.vst3state` setups from `.nki` | Windows | | 10-01 | superseded in-app by `kontaktsetup.*` |
| `tst_liveequivalence scriptValuesSet` | a setup state with script values set, for kthost | `MS_SCRIPT_VALUES_IN`, `_OUT`, `MS_SCRIPT_VALUES="$name=v;…"` | `.vst3state` | 10-17 | |
| `tools/ms4playback/gen_tables.py <MS4 source>` | `libmscore/ms4tables.h` | Linux | MS4 source | 09-25 | |
| `tools/tuning/gen_tuning_tables.py <table.js> <tuning.qml> > libmscore/tuningtables.h` | tuning tables | Linux | | 09-26 | |
| `tools/playability/gen_winds.py [winds.js]`, `gen_scord_tests.py` | playability tables / test score | Linux | | 09-28 | |
| `tools/playbackverify/make_heard_passages.py`, `make_mixed_techniques.py` | the heard check's standard scores | Linux | | 10-16 / 10-21 | |

**Which json feeds which map attribute** (gen_spitfire_sso.py reads them): `sso_controller_defaults` → `patchDefault`;
`sso_smooth_onset` → `attackOnset`; `sso_accent_attack` → `accentedLevel`; `sso_bad_samples` → `avoid`;
`sso_patch_controls` → `<Controller>`s; `sso_percussion_hits` → `<Drum>`; `sso_nki_files.txt` / `_keys` /
`_articulations` / `_groups` → patches, `pitch`, `scan`; `sso_articulation_timing` → `release`; `sso_legato_grid*` →
`legatoDelay`; `octave_measure.json` → `octaveUp/Down`; `sso_patch_measurements` → `bend`; `sso_legato_glide` →
`bendGlide`; `sso_bend_smoothing` → `bendSmoothing`; `sso_plain_performance_shift` → `plainShift`, `plainAttack`;
`sso_performance_layers` → portamento velocity, `speedCC`, `speedScript`; `sso_sound_range` → onset fit, release;
`sso_sound_onset` → `onset`, `peak`; `sso_long_onset_fit`, `sso_rachm_onset_fit` → `onset`; `sso_short_lengths` →
`from`; `sso_rachm_levels` → `quickLevel`; `sso_performance_context` → `contextLevel*`; `sso_winds_section_balance` →
`sectionLevel`; `sso_cl_section_balance` → `sectionLevel2`; `sso_real_range` → `realSpan`, `longLevel`, `realPlay`,
`realSteps`; `sso_mic_levels` → `micLevels`, `micDefault`, `<Mics>`; `technique_offsets` → `heldOffset`; `sso_crest` →
`<Dynamics crest>`.

**jsons without a committed producer** (made by one-off scripts; most record their method in an "about" key):
`sso_accent_attack`, `sso_bad_samples`, `sso_smooth_onset`, `sso_plain_performance_shift`, `sso_performance_layers`,
`sso_percussion_levels`, `sso_rachm_cc1_sweep`, `sso_rachm_context_match`, `sso_slur_swell`, `spheres_k550_staccato`,
`sso_patch_controls`, `sso_nki_*`, `sso_percussion_hits`, `sso_drum_keys_sounding`.

---

## 7. Hosting, loading and UI speed

| Tool | Answers | Command | Outputs | Limits |
|---|---|---|---|---|
| `--measure-load-times <lib> [scores…] [--extract-patches <f>] [--measure-probe <patch>] [--measure-threads 2,4] [--measure-no-probe]` (VM) | instance load times: alone, as at score open, on threads, reuse, script-value probe, a parameter on a multi | in-app, normal priority | `<lib> load times <date>.txt` | |
| **kthost** (VM `C:\claude\kt\kthost.exe`; `kthost2.exe` in `C:\claude\kp`: real time, editor open) | a minimal VST 3 host: Kontakt 8 + a `.vst3state`, renders an event file to a WAV (offline by default) | `kthost <plugin> <state> <events> <out.wav> [--tail s] [--wait s] [--probe-pitch n] [--param …] [--offsets] [--warm] [--realtime] [--from] [--to] [--list-params] [--editor] …` | WAV | **no source anywhere** (only a restored binary, MD5 `ea7e692e…`, and a disassembly): OPEN_ITEMS › Missing tools. No tuning slots, gain stage, trim or lane parameters; fresh-process renders null at ≈ −93 dB |
| `tools/soundlibraries/ktload/` (`ktload.cpp`) | Kontakt state load times and memory | VM, built with msvc-wine: `ktload <plugin> <out> [--pump s] [--save-after s] [--no-process] <state>[=<save>]…` | log | |
| `private/sso-load-profile-work/ktprof.cpp`, `kttrace.cpp` (MinHook tracer), `launchw.cpp`, `plateau.py`, `ksp.py`, `mainprof.py`, `resolve.py`, `pc-kit.zip` (private) | load phase marks, ntdll / registry trace, thread sampler, owner-PC kit | VM / owner PC | | kttrace pulls in minhook |
| `tools/lagprobe/` (`vm/setup.cmd`, `setup2.cmd`, `go.cmd <tag> [phases] [reps] [modes]`, `probe.cmd`, `monitor.ps1`, `stop.ps1`; `linux/mkenv.py`, `run-linux.sh`; `read_lag_log.py <log> [--limit-ms 100]`, `compare_blocks.py`) | UI response to edits with SSO loaded | env `MS_LAG_PROBE=<log>`, `_REPS`, `_GAP_MS`, `_PHASES`, `_MODES`, `_OPEN_S`, `_KEEP` | jsonl → reports | VM shares CPU with builds: interleave blocks only; a crashed probe leaves the playback mode at MS4 |
| Env knobs for hosting | | `MS_SOUNDLIBRARY_LOAD_THREADS`, `MS_PREPARED_OFF`, `MS_KONTAKT_MAX_VOICES`, `MS_PLUGIN_SETTINGS_DIR`, `MS_BACKGROUND_LOAD_WAIT`, `MS_EDIT_LAG_OFF`, `MS_LIVE_LOG`, `MS_LIVE_CONFLICT_AUTOSHOT`, `MS4_STRICT`, `MS4_DEBUG_*` | | |
| Test synth (`mstestsynth.vst3`) | stand-in plug-in for Linux tests | env `MSTESTSYNTH_INIT_MS`, `_SETSTATE_MS`, `_STREAM_MS`, `_STREAM_MB`, `_PARAMS=<file>`, `_TRANSPORT_LOG=<file>` | | |

---

## 8. Ample guitars (private tools, not in repo unless marked)

| Tool | What | Command / machine |
|---|---|---|
| `private/ample-full/meas2/scripts/amphost2.cpp`, `amphost3.cpp` (VM exe `C:\claude\ample\amphost.exe`) | `info` dump (what `gen_ample.py` reads); `render <events> <out.wav>` with `--pre --tail --rt --offline --block --repeat --state --savestate --set id=norm --gui --midiout` | built with msvc-wine against `thirdparty/vst3sdk`, run on the VM |
| `private/ample-full/amphostgui.cpp` | adds an `editor` mode (screenshots) | VM |
| `private/ample-check455/fix4/tools/amphost4.cpp`, `amphost5.cpp` | background-load timing (`--wait --nopump --fastpre --probe`); several instances in one process replaying a verify `events.tsv` | VM; amphost5's CMakeLists points at a deleted worktree |
| `private/ample-state/tools/astate.cpp` + `st.py xml\|diff\|set`, `step.sh`, `ana.py`, `mid.py`, … ; `ample-full/meas2/scripts/mkstate.py` | GUI host driven by a command file (state save/load, clicks, shots); AGM state reader / differ / editor; state writer | VM desktop via scheduled task |
| `private/ample-full/meas{,2,3}/scripts/gen*.py`, `an*.py`; `private/mv/scripts/` | keyswitch, strum, CC7 law, roll, pinch measurements; M-Master Vol vs CC7 (bit-identical, gain 20·log10(2·CC/127)) | VM + Linux |
| `private/ample-check453/scripts/` (`mk453.py`, `patch453.py`, `ana453.py`, `a_*.py`, `clips453.py`, `vm/wd.ps1`), `ample-check455/scripts/` (`a_als.py`, `a_null_all.py`, …), `ample-check457/scripts/` (`lanes.py`, `editvol.py`, `stackscan.py`), `ample-wincheck/scripts/` | the Windows check harness: test scores, settings into the .mscz, hashes, .als envelopes, crash-dump stacks; 3-minute watchdog | Linux + VM; `python3 -P -E` |
| In-app: `MS_VERIFY_ROUTES=1 MuseScore3Evo --verify-library "Ample Guitar M" --verify-playback <score> --verify-out <dir> --verify-wav` | Ample renders per route | VM |

---

## 9. Other VM folders (`C:\claude`, list only)

`kt` (kthost), `kp` (kthost2, editor shots), `bs`, `lr`, `k` (kthost runs, `krun.cmd`), `evenlv`, `sst`, `sbright`,
`swell`, `bvalid`, `vialsso` (kthost batches: `list.txt` + `run.cmd`, `lock.ps1` / `unlock.ps1` quiet-lock helpers),
`ctldef` (controller defaults run), `loadprof` (load profile), `longcal` (calibration verify), `pk`, `psave` (Live save
profiling), `lc1012`, `ug`, `uh` (Live GUI drivers), `gui444`, `playability-5e2d7b7` (GUI drivers), `ample`,
`amplefull`, `amp2`, `amp3`, `astate` (Ample), `b454` … `b457`, `bpt` (installed builds of runs #454-#458),
`MuseScore-soundlibrary-win64-16d3dbd` (an install), `heardcheck`, `plaintiming`, `soloclicks`, `out` (empty work
dirs), `verify.cmd` / `task-verify.cmd` (an old verify wrapper pointing at build d9936a3, gone), `quiet.lock`.
