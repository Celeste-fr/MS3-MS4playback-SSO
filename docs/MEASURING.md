# Measuring a patch: how a later agent answers a new question about the sound

This is the guide to measuring what a plug-in's patch does (how loud, when it is heard, how a legato transition moves,
how long it rings) with two tools in the repo:

- **kthost** (`tools/soundlibraries/kthost/`): an offline VST 3 host on the command line, built from MuseScore's own
  hosting code;
- **measure_patch.py** (`tools/soundlibraries/measure_patch.py`): plans a sweep, renders it on the Windows test VM
  through kthost, measures it and writes a JSON.

TOOLS.md lists every other tool; OPEN_ITEMS.md lists the open questions these tools are for. CLAUDE.md's rules apply.
In particular: say what was measured with what (the test synth on Linux, real Kontakt + SSO on the VM, the owner's
ears), never commit Spitfire / NI files or the owner's scores, and hold the VM lock only while rendering.

## The recipe

1. **Write the question as a grid.** For example: "when is a Violas Performance transition heard after an attack, by
   the attack's length?" becomes patch `Violas - Performance`, transition cells, `--before attack`,
   `--before-len 136,273,500,1000`, a few pitches and intervals, two takes.
2. **Plan it on Linux.** This writes kthost event files, `list.txt` and `plan.json` into a work folder:

   ```
   python3 tools/soundlibraries/measure_patch.py plan --patch "Violas - Performance" \
       --library "share/soundlibraries/Spitfire Symphony Orchestra.xml" --what transition \
       --pitches 50,67,79 --intervals=-2,2 --before attack,transition --before-len 136,273,500,1000 \
       --ioi 136,300 --takes 2 --out ~/MuseScore/ms3fork/private/<study>/work
   ```

   It prints how many cells, files and minutes of audio the plan makes. Keep a render under about an hour of audio:
   the VM renders about 5-10x faster than real time, and other sessions are waiting for it.
3. **Render and measure** with `measure_patch.py run <work>`. This one command:
   - waits for and takes the VM lock;
   - copies the VM MuseScore's own setup of the patch, read-only;
   - renders each file through kthost, under a watchdog with retries;
   - fetches the WAV, measures it into `work/res/<file>.json` and deletes the WAV, on the VM and here;
   - releases the lock between batches.

   It is resumable: run it again and it renders only the files that have no result yet.
4. **Sum up** with `measure_patch.py analyze <work> -o tools/soundlibraries/sso_<what>.json`, then
   `measure_patch.py report <work> [--png]`. Every cell gets its median, mean, SD, 95 % CI and the single takes.
5. **Check the numbers before using them:**
   - against an existing dataset, where one covers the same cells (see "Proof" below);
   - against take-to-take spread: is a difference between cells larger than the SD within a cell?
   - by ear, where the question is about what is heard. The owner decides; the clip maker is Missing tools #7.
6. **Feed the generator.** A generator (`gen_spitfire_sso.py`) reads the JSON; nothing in MuseScore reads it directly.
   - Record the method in the JSON's `_about` (the tool writes one) and in the commit message.
   - Readings of music or pictures go on a review page first (CLAUDE.md).

`measure_patch.py all …` runs all four steps in one go: the plan and the run options together, plus `-o`.

## Cells, order and repeatability

- **Note cells** (`--what level,onset,release`): one note after silence, for each combination of `--pitches` ×
  `--velocity` × `--cc1` × `--cc11` × `--lengths` (ms).
- **Transition cells** (`--what transition`): a figure ending on the measured note B (pitch + interval), for each
  combination of `--pitches` × `--intervals` × `--before` × `--before-len` × `--ioi` × `--speed` × `--b-early`. The
  kinds of `--before`:
  - `attack`: A attacked after silence, then B legato (A held `--overlap` ms into B). A long `--before-len` gives
    "after a held note".
  - `transition`: Z (pitch − interval), then A by transition, then B by transition: a run in one direction.
  - `staccato`: A short, then B attacked after `--staccato-gap` ms.
  - `turn`: as `transition` with Z on B's pitch, so B turns back (B A B). In Whence's alternating quarter-tone figures
    the f0 often hasn't reached A when B starts.
  - `staccato-attack`: Z a staccato, A attacked `--staccato-gap` ms after it, then B by transition: a slur's first
    transition after a staccato.
- **The note after** (`--after transition`): a note C follows B by transition at B's written end, sent as early as B
  (a run: B + 2 the same way; a turn: back to A's pitch). Without it B ends at `--ioi` and its glide runs into a release;
  in music the next note's transition takes it over. `--after-len` is C's length.
- **Quarter tones.** `--intervals` may be fractional (`1.5` = three quarter tones). They are sent as `[slurs]
  microtoneBends` 1 sends them (rendermidi.cpp): the key on the side of the note before, and the pitch bend of the uncut
  fingered glide (the map's `bend=`, `bendGlideFingered=`, `bendSmoothing=`). `--microtone-bends 0` puts them on the lower
  key instead. A Python copy of the renderer's rule, not MuseScore's own events: check a new case against a dump.
- **Speed.** SSO's Performance legato writes CC119 from the spacing since the line's previous note-on as played
  (docs/SSO_SCRIPT_KNOWLEDGE.md). CC119 moves the transition layers' sample starts by 10-208 ms. So the spacing is a
  dimension of its own:
  - `--speed played` (the default): the script writes CC119 from the spacing it sees;
  - `--speed written`: as `[slurs] speedFromWritten` 1 plays. The state loads with the script's own writing off
    (`--script-value $vfm12=0`, from the map's `speedScript`), and CC119 is sent from the written spacing right before
    each legato note-on.
  - `--b-early ms` sends B's note-on that much before its written time, as MuseScore's early starts do. Under
    `played`, CC119 then follows the shortened spacing; under `written` it doesn't. Each cell records the CC119 that
    reaches its B (`cc119`).

  Cells of the two speed modes go into different files, because each mode needs its own kthost options. `--b-early`
  values that would put B closer than `--min-spacing` ms (36) to A are skipped. Each cell records the played spacing
  (`spacingMs`).
- **Order: ABBA over the whole plan.** Take 1 plays the cells in plan order, take 2 in reverse, take 3 forward again.
  A slow drift (a cache warming up, a round-robin cycle) therefore falls evenly on every cell.
- **Round robins.** SSO's string Performance patches don't reset their round robins on a transport start. They reset
  only after 1.5 s with no note-on. The default `--gap` of 1.6 s after each cell's last note-off makes every cell start
  from a reset counter. `rrNote` records B's position in the cell (0 for the first note). Every file starts with a
  warm-up note that isn't measured.
- **Random modulators.** The strings' Performance patches have random modulators on, so takes differ. That is the
  reason for `--takes ≥ 2`. To measure without that randomness, as `[slurs] fixedTransitions` 1 plays, add
  `--random-sources 0` to `--kthost-opts`.
- **Controllers.** CC1, CC11 and the articulation switch are sent `--pre` s before each cell. The switch comes from
  `--articulation` with `--library` (the map's `<Switch>`), or from `--switch cc:N:V` / `key:K[:V]` / `none`.

## What is measured

Rules below are the same ones the heard check and the map's onsets use, so new numbers are comparable with old ones.

- **`peak`**: the perceived peak (tools/playbackverify/loudness.py, loudness units ≈ dB) in the first `--level-window`
  ms (400) after the note-on. This is the accent study's measure.
- **`mean`**: the perceived mean over the middle of the note.
- **`loud50`**: the loudest 50 ms of power, dBFS, as the dynamics check measures it.
- **`onset15`**: the map's onset rule. Take the first envelope value after the last one under (peak − 15 dB) before the
  peak, minus 30 ms of latency (Vos & Rasch 1981; onset_detect.py). It reads a fast rise 15-30 ms early: the test synth's
  step reads −32 ms, a 20 ms ramp −17 ms.
- **`onset6`**: the same at −6 dB.
- **`onsetIso`**: the existing detector, `onset_detect.isolated_onset`.
- **`release40` / `release20`**: ms after the note-off until the power stays 40 / 20 dB under `loud50`.
- **`leave` / `mid` / `arrive`**: transition cells. The ms after B's note-on where the f0 (YIN, as `legato_glide/
  glide.py`) has gone 0.1 / 0.5 / 0.9 of the way from A's pitch to B's.
- **`heardRef` = leave + 23.5 ms: the perceptual reference for a transition.**
  - The owner, 2026-10-21, listened to each section alone with a click on Whence bars 5-14. Transitions whose f0
    arrives 130-200 ms late sounded on time to him.
  - So the ear places a legato transition near where the pitch *leaves* the note before. Polfreman 2013 gives leave +
    23.5 ms.
  - `arrive` is reported but is **not** the heard time. Don't fit timing models on arrival alone.
  - The 23.5 ms is Polfreman's figure, not measured on SSO: an assumption until a tap test exists (Missing tools #7).
- **`heardRefWritten` / `arriveWritten`**: the same against B's written time, with `--b-early` taken off.
- **`levelA`, `levelB`, `stepAB`**: transition cells. A's and B's perceived peaks, and the step between them.
- **`aOnset15`**: A's onset, when A is an attack.
- **`bOnset15` / `bMasked`**: B's onset after a staccato. `bMasked` is 1 when A's release stays within 15 dB of B's
  peak, which leaves no reading.
- **`analyze --reanalyze <kept WAVs> --at-level T --at-level-cells velocity=64,lengthMs=211`**: measures at a stated
  level. The perceived envelope depends on level, so every render is scaled by one gain that puts those cells' median
  peak at T units first. The accent study measured at the real players' 59.3 units this way. Run with `--keep-wavs`.

## Tone: faulty round robins, clicks, noise (`tone_check.py`)

`tools/soundlibraries/tone_check.py` (its header is the reference) measures every note's tone and noise and compares it
with the patch's own median. It is measure_patch's analysis step for tone (`--what …,tone`) and also reads any render
with its notes (a kthost event file, a `plan.json`, a verify `events.tsv`).

- **Play the round robins.** A faulty round robin shows only when the render plays every round robin: the same note
  repeated closer than the patch's round-robin reset (SSO's Performance patches reset after 1.5 s without a note-on;
  measure_patch's default `--gap` 1.6 s resets them on purpose, so every take plays round robin 1). For tone:

  ```
  python3 tools/soundlibraries/measure_patch.py all --patch "Celli - Performance" \
      --library "share/soundlibraries/Spitfire Symphony Orchestra.xml" --what level,tone \
      --pitches 45:51 --velocity 64,90,99,100,106,116 --lengths 500 --repeats 12 --repeat-gap 0.4 --takes 2 \
      --out ~/MuseScore/ms3fork/private/<study>/work
  ```

  This is sso_bad_samples.json's protocol (12 attacks, 0.5 s on, 0.4 s apart) as cells: the measures above on each
  cell's first note, tone on all twelve.
- **What it reports** (per note, against its group: the same key, cents, velocity, cell; over takes and repeats):
  - `narrow tone`: one harmonic (or, on an inharmonic sound, one 1/12-octave band) standing out from the group's median
    by ≥ 6 dB beyond its neighbours. A darker or louder recording is a step or an offset, not a peak, and doesn't count;
    neither does energy that was already ringing before the note-on (a reverb tail);
  - `dark` / `bright` (centroid ≥ 150 cents or tilt ≥ 2 dB/oct), `noisy` (the noise between the harmonics ≥ 6 dB up),
    `quiet` / `loud` (≥ 3 dB), `noisy floor` (a flat floor before the note ≥ 10 dB up);
  - `click at note-on` / `pop at note-off`: a linear-prediction model of the note's sustain leaves a residual spike
    (≥ 15 dB, and 6 dB over the group). SSO's isolated string attacks: −17 … +3 dB at the note-on, 1 … 10 dB at the
    note-off; a waveform cut at its crest: +24 dB;
  - the round robin: a fault at attacks 5 and 11 of 12 is "round robin 5 of 6";
  - key outliers: a key whose medians differ from the keys within ±2 semitones (level 4 dB, centroid 300 cents, tilt
    3 dB/oct, noise 8 dB): one recording for every round robin, or a sample-set seam.
- **Feeding the map.** `tone.json`'s `avoid` is a proposal in sso_bad_samples.json's schema: per key, neighbouring
  measured velocities whose attacks have a tone flag, played at the nearest clean velocity (below first); neighbouring
  keys merged. A person checks it before it goes into sso_bad_samples.json (gen_spitfire_sso.py reads it): the accented
  attack's velocities (117-127) may be another technique, as sso_bad_samples.json says.
- **In a passage of music** (a verify render's `events.tsv` with `--slot`), notes are grouped by key, velocity, joined
  (a legato transition) and the key they come from. Groups then mix lengths and contexts, so take the flags as leads
  and measure them again as isolated repeats. Example (Whence's Celli, verify render of 2026-10-20, slot 10): the C3
  transitions from D3 carry harmonic 9 (1177 Hz) +6.5 dB over its neighbours in all 11, those from B2 −3 dB in all 12:
  a property of that transition, not a round robin; the same frequency as sso_bad_samples.json's fault (1184 Hz), not
  checked by ear.
- **Checked on**: synthetic faults (`test_tone_check.py`: the narrow round robin of sso_bad_samples.json rebuilt, with
  its `avoid` re-found exactly; dark, noisy, click, pop; a dark key) and 120 clean SSO attacks (kthost, five string
  Performance patches, v64 / 122, private plain-accents renders; none flagged). There is no kept render of the
  original fault (its renders and script are lost): re-finding it in real SSO needs a VM run of the command above.

## Patch facts

### Grand Piano (2026-10-30, VM kthost, offline, 44.1 kHz; analysed on Linux; private `piano-analysis/summary.json`)

- **Round robins**: at least two per key and velocity (attacks 1 and 4 of 4 often the same recording, r 0.99); their
  level differs by up to ~4 dB, their spectra differ (tone_check's `dark` / `bright` / `quiet` / `loud` flags are this).
- **tone_check on a piano**: its `pop at note-off` flags (147 of 1056 notes, almost all attacks 1 and 4) are its
  sustain model on a decaying note: the energy above 3 kHz 30 ms after the note-off against before is the same for every
  attack (median −1.7 dB, max 6.4). Its harmonic grid misses a piano's stretched partials above ~the 15th, so a
  `narrow tone` there needs a look at the real partial. Key 108's `click at note-on on every note` (25 dB) is the hammer
  over a top key's short sustain. Lead: keys 40-41, one round robin, a partial near 2.0 / 2.1 kHz +16-19 dB at v112
  (OPEN_ITEMS 38).
- **Key levels** (4 repeats at v48 / 80 / 112, repeat SD 0.5 dB): two-key sample zones ±3-5 dB, key SD 1.8-2.2 dB
  (OPEN_ITEMS 38 lists them).
- **Crest** (peak − loudest 50 ms, as `sso_crest.json`; 15 keys x v48 / 80 / 112 / 127 x 2 takes): max 13.45, median
  8.8 dB; loudest peak −6.6 dBFS (v127, at the setup's defaults). Over the shipped calibration (key 62 alone): up to 18.7 dB.
- **Sustain pedal (CC64)**: keys 43 + 60 at v80 released after 0.5 s: pedal up, −69 dBFS 1 s after the key (notes'
  loudest 50 ms −23.4); pedal down, still −48.7 dBFS 5.5 s after; CC64 up then cuts it (−78 at +0.5 s, −88 at +1 s).
  A lift and press 1 ms apart damps notes held by the pedal as a pedal up does (−42 → −57 at +0.25 s → −66 at +0.5 s;
  without it −42.5): the press doesn't catch them. Each lift plays one pedal-up noise, loudest 50 ms −60 dBFS, peak
  −52 (pedal down's −68): ~36 dB under a v80 note; no click, no glitch; a press and lift 1 ms apart the same. Both takes
  identical to 0.1 dB.

## kthost

`kthost <plugin.vst3> <state|-> <events.txt> <out.wav> [options]` (`--help` lists them all). The source is
`tools/soundlibraries/kthost/kthost.cpp`; its header is the reference.

It renders as MuseScore's export renders a route:
- `Vst3Plugin::load`, then `setState`, then `settle`;
- flat-lane parameters (`--param`);
- the probe (`--wait`);
- offline mode, the transport restarted, and the events delivered sample-exactly (the render is split at each event's
  frame);
- a 32-bit float WAV with the gain stage (`--gain`).

It can also prepare the state as MuseScore does:
- `--max-voices 512`;
- `--tune` and `--slots` (the tuning copies as one instance's slots, slot i on MIDI channel i + 1);
- `--random-sources`;
- `--script-value`;
- `--prepared <dir>`, which keeps the prepared states.

Event file lines are `seconds kind a b [channel]`, the format of dumpEvents' `<route> museScore.txt`. The kinds are
`on`, `off`, `cc`, `pb`, `at`, `param <id|title> <value>`, `tr` (transport restart) and `vs <cents>` (varispeed).
Exit codes: 0 rendered, 2 usage, 3 plug-in, 4 state, 5 events, 6 WAV. `--report` writes a JSON with the timings, the
probe, and each pass's peak.

### Against the restored binary (C:\claude\kt\kthost.exe)

The old binary's source was lost on 2026-10-10. Only a restored binary is left: MD5 `ea7e692e…`, the options found in
its strings. The new kthost takes the same positional arguments and `--tail` / `--wait` / `--probe-pitch` / `--block` /
`--realtime` / `--rtfast` / `--param` / `--list-params`, so the existing `run.cmd` scripts can switch by changing the
exe's path.

Measured against it: see "Proof" below.

Differences:
- **The transport is restarted** before the render, as at every MuseScore export (`--no-transport-restart` turns it
  off). SSO's All-techniques patches reset their round robins on it.
- **The probe:** the probe pitch (60 by default) at velocity 64, until −60 dBFS or `--wait` s of wall time. Then it is
  released and rung out (0.25 s under −100 dBFS, at most 4 s), then all notes off. The old binary's probe details
  are unknown.
- **Not implemented:** `--offsets`, `--param-late`, `--param-first-block`, `--warm`, `--warm-block`, `--live-first`,
  `--from`, `--to`, `--editor`. None is used by a committed script. kthost2's realtime editor mode isn't covered either:
  screenshots still need the old `C:\claude\kp\kthost2.exe`.
- **New:** event channels, `param` / `tr` / `vs` events, the state edits, `--gain`, `--repeat`, `--report`,
  `--save-state`, exit codes.
- **Not covered:** several instances in one process. A Kontakt multi's slots share one output, so a per-slot gain
  can't be applied offline; `--gain` is per instance, as MuseScore applies it to hosted slots. For several instances,
  render each and add the WAVs.

### Building kthost

- **Windows CI.** With MuseScore: `BUILD_VST3` adds `tools/soundlibraries/kthost`, and on Windows it is installed into
  `bin/`. The Windows CI artifact (`MuseScore-soundlibrary-win64-*`) therefore carries `kthost.exe` next to
  `MuseScore3Evo.exe`, with the Qt DLLs it needs.
- **Linux.** `ninja kthost` in a build dir gives `build.rel/tools/soundlibraries/kthost/kthost`. It runs with the test
  synth (`mtest/libmscore/soundlibrary/mstestsynth.vst3`).
- **On its own with msvc-wine** (for the VM without a CI run):

  ```
  # Qt 5.15.2 for MSVC, qtbase only (aqtinstall; ~230 MB, keep it on /tmp)
  python3 -m venv /tmp/qt/venv && /tmp/qt/venv/bin/pip install aqtinstall
  /tmp/qt/venv/bin/aqt install-qt windows desktop 5.15.2 win64_msvc2019_64 --archives qtbase -O /tmp/qt/Qt
  PATH=~/msvc/vs17/bin/x64:$PATH cmake -G Ninja -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_PREFIX_PATH=/tmp/qt/Qt/5.15.2/msvc2019_64 \
      -S tools/soundlibraries/kthost -B /tmp/kthost-win
  PATH=~/msvc/vs17/bin/x64:$PATH ninja -C /tmp/kthost-win kthost
  scp /tmp/kthost-win/kthost.exe /tmp/qt/Qt/5.15.2/msvc2019_64/bin/Qt5Core.dll <vm>:C:/claude/ps/kt/
  ```

  `measure_patch.py run` uses `C:\claude\ps\kt\kthost.exe` by default (`--kthost` for another).

## The VM

- **Address and key.** The address and key are in CLAUDE.md's VM notes (`measure_patch.py --vm`, `--key`). The SSO
  samples stream from the owner's PC over `\\VOLENS\VMShare`. The runner maps it from `C:\claude\share.txt` when it
  isn't mapped already, and removes only a mapping it made itself.
- **Lock protocol.** Wait while `C:\claude\quiet.lock` exists. Write it with your tag (`--lock-tag`), plus the local
  `private/VM_PAUSE`. Remove only your own, after each batch of `--batch` files, so others get turns.
- **One heavy job at a time.** Never two renders at once.
- **Setups.** The patch's setup comes from the VM MuseScore's setups folder (`%LOCALAPPDATA%\MuseScore\MuseScore3Evo\
  soundlibraries\<library>\<patch>.vst3state`). It is copied into the work folder and never written. `--state` uploads
  another one.
- **Work folder and disk.** Work folders are `C:\claude\ps\<name>`, deleted at the end (`--keep-vm` keeps them).
  WAVs are deleted once measured (`--keep-wavs` keeps the local copies). Disk on the dev machine is tight: keep
  `--keep-wavs` for small plans only.

## Tests

- `python3 tools/soundlibraries/test_measure_patch.py` uses a synthetic instrument with known onsets, levels, releases
  and glides. It covers the ABBA order, every cell measured, onset, level by velocity, release, leave / mid / arrive,
  heardRef, masked staccato, the map's switch and the report.
- With `MS_KTHOST=<kthost> MS_TESTSYNTH=<mstestsynth.vst3>` the same file also runs plan → kthost (`run --local`) →
  analyze on the Linux test synth.
- `tools/soundlibraries/kthost/null_compare.py a.wav b.wav` gives the lag, residual and correlation of two renders.
  It is used for the null test against the restored binary.

## Proof (2026-10-21, Windows test VM, Kontakt 8 + SSO)

- **New kthost against the restored binary.** SSO Violas All techniques Long, 7 notes over 20 s, identical events and
  options (`--tail 3 --wait 60 --probe-pitch 61`):
  - each binary against itself: −144 / −141 dB;
  - new against old: lag 0 samples, level +0.003 dB, correlation 0.99999, residual −47.5 dB;
  - every note-on is sample-identical. The differences start a few dozen to ~500 samples after each note-off (the
    releases); the worst second is −29 dB under the signal. The old binary's note-off handling is unknown (no source).
  - So: onsets and levels carry over from the old renders; releases measured with the old binary may differ slightly.
- **The accent study re-measured** through `measure_patch.py` and the new kthost. Violas - Performance, pitches
  48:88:4, velocity 64 / 122, 100 / 211 / 450 ms, CC1 64, CC11 86, 2 takes, at the study's level (59.3 units: gain
  +21.76 dB here, 21.48 there). SSO's 122 − 64 perceived difference against `sso_accent_attack.json`, 33 cells:
  mean |diff| 0.04 dB, max 0.12 dB, r 1.000.
- **First new measurement:** transitions of the five string sections' Performance patches by the note before, as plain
  Performance plays them (velocity 64). The data is `tools/soundlibraries/sso_transition_context.json`: 192 cells per
  section, 2 takes.
  - **Leave depends on the note before's length**, after an attack and after a transition alike. After 136 ms the
    pitch leaves 44-146 ms after B's note-on (Basses 44, Violas 65, Violins 1 90, Violins 2 94, Celli 146). After 500
    ms it leaves 70-188 ms after.
  - The map's `bendGlide` model says −8 / −9 ms at 136 ms for every section. It was measured on runs at velocities
    112 / 93 and is the model plainTiming times transitions by.
  - heardRef (leave + 23.5) after an attack, 136 → 1000 ms before:
    - Violins 1: 114 → 215;
    - Violins 2: 117 → 150;
    - Violas: 88 → 175;
    - Celli: 170 → 167 (flat);
    - Basses: 67 → 108.
  - Celli against Basses at the same lengths differ by ~60-100 ms, the sign of item 3's residue.
  - **Speed played against written** (no early start, so the same CC119 by two routes): heardRef within ±10 ms in 74
    of 80 groups, at most 21 ms. The `--speed written` path reproduces the script's own writing.
  - **Take spread is small**: the median take SD of heardRef is 1-5 ms per cell.
  - Not measured yet: an early start (`--b-early`), where played and written CC119 part. That is the Whence Violas
    bars 9 / 13 case: the next run.
