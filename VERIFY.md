# Playback verification

Checks, without anyone listening, that this MuseScore plays scores right through the sound library
(Spitfire Symphony Orchestra in Kontakt Player): each score is rendered offline exactly as
*File › Export › Audio* renders it, rendered again with MuseScore's built-in synth (the
expectation), and the audio is compared with the notes MuseScore sent. The report says which
chords or notes didn't sound, which were cut short, where it went silent or clipped, and what else
happened at each of those moments (pedal, articulation switch, dynamics, how busy the patch was),
so the cause shows from the report alone.

Code: `mscore/playbackverify.*` (the command, rendering, report), `audio/vst3/playbackverify.*`
(the analysis; its header lists every check and threshold), `main/Verify SSO playback in
background.bat.in`, `tools/playbackverify/`.

## For the owner: one run

1. Get the MuseScore to check (a run of *Test: Sound library on Windows*, as for any test build),
   unzip it where you like.
2. Start the check from a command prompt in its `bin` folder:
   `MuseScore3Evo.exe --verify-library "Spitfire Symphony Orchestra" --verify-playback default`
   (`default`: the test scores that come with MuseScore, `verifyplayback`: piano chords at pedal changes,
   strings with legato, staccato, pizzicato …; or give score files / folders instead). The double-click
   `Verify SSO playback in background.bat` is no longer in the builds (2026-10-02); a build configured with
   `-DMSCORE_INSTALL_MEASUREMENT_SCRIPTS=ON` still has it (`main/Verify SSO playback in background.bat.in`).
3. Keep working: it has no window, runs at below-normal priority, uses a copy of your setups
   (`Documents\MuseScore Sound Library Check\background verify setups`) and changes nothing of your
   MuseScore. Progress: `Documents\MuseScore Sound Library Check\background playback verify.log`.
   Each score is rendered through Kontakt once mixed and once per library part alone (plus the
   built-in synth, which is quick), after loading its patches; how long that takes with Kontakt
   isn't measured yet (here, with a test plug-in, a 13-minute piano score took about a minute).
4. When it's done that folder opens: hand back the newest **`Playback verify <date>.zip`**.

What the zip holds: `summary.txt` (read it yourself: PASS or FAIL and each finding), `report.json`,
lists of every note and strike with their measurements, the events MuseScore sent, and `clips/`:
2.5 s of the rendering (and of the built-in synth) around each finding, to listen to. The clips and
lists are your music: keep the zip private. For a report to post publicly, run it from a command
prompt with `--verify-shareable` (only the summary and the findings, no audio, no note lists):

    MuseScore3Evo.exe --verify-playback "D:\My scores" --verify-library "Spitfire Symphony Orchestra" --verify-shareable

Only one verification runs at a time. To stop one: Task Manager, end `MuseScore3Evo.exe` (the one
without a window).

## What it checks, and what it can't

Per library part (a score with several parts: each part rendered alone too, so a finding names its part):

| Finding | Meaning |
|---|---|
| `missing-attack` | a chord (the notes starting together) has no attack where the built-in synth has one, and one of its notes is under its usual level (SSO's Grand Piano dropping chords at pedal changes) |
| `missing-note` | one note didn't sound although the other notes of its chord did (the staccato octaves of bars 59-62); a note the library played with a legato articulation (SSO's Performance "Legato": no attack, slow build-up) is judged on its level held at 40 and 70 % of it, against that patch's other legato notes |
| `cut-short` | a held note (long, legato, tremolo, trill articulation) fell silent before 70 % of its length where the built-in synth still sounds |
| `silence` | nothing (under -70 dB) for 0.25 s or more while held notes should sound |
| `clipping` | samples over full scale in the mixed rendering |
| `drift` | the audio's timing against the notes moves by more than 40 ms along the score |

Each finding has the measure and beat, the part, the pitches, the time in the score and in the audio,
the articulation the library played, and the events on that part's slot around it: `CC64=0 (pedal
up) at -1 ms`, `articulation switch CC 32=42`, dynamics (CC1/CC11), the same key released just
before, an earlier note of the same key still sounding, and how many notes the patch started in the
2 s before. Per part, `summary.txt` also counts those against all strikes ("pedal up 28/29 vs
220/2662" says it's the pedal).

It can't tell: whether the right articulation or sample was chosen (a wrong but sounding sample
passes), balance and dynamics by ear, or anything the built-in synth doesn't sound either (a note
struck again while it rings under the pedal is not flagged: `unclear` in the counts). A missing note
whose partials another note of its chord fills (C6 over C5) is often not found; the chord's other
checks may still flag the moment. Levels are compared per patch (a part's Performance legato patch
is 15-20 dB quieter against the built-in synth than its solo patch: the first Kontakt run, 2026-09-29,
flagged 9 legato notes the owner heard). The thresholds were set on one score, the owner's piano piece (four
exports, known bugs); strings, winds and brass through Kontakt are untried.

## For agents: reading a report

    tools/playbackverify/read_verify_report.py "<zip or folder>"          # verdict, counts, each finding
    tools/playbackverify/read_verify_report.py "<zip>" --near 136.24      # notes, strikes, events around 136.24 s
    tools/playbackverify/read_verify_report.py "<zip>" --json             # report.json

- `report.json`: `scores[].parts[].findings[]` (`kind`, `time` (score seconds), `audioTime`,
  `measure`, `beat`, `pitchNames`, `text`, `context[]`, `notesBefore2s`, `clip`), `atStrikes` (per
  kind of event: flagged strikes with it, all strikes with it), `offset`, `offsetPerWindow`,
  `weakStrikes`, `unclear`, `weakButSounding`; per score `clipping[]`, `peakDb`.
- `<score> [<part>] strikes.tsv`: every strike's `broad` (attack flux / the median strike's) and
  `pitchLocal`, the reference's, `weak`, `flagged`, the event tags.
- `<score> [<part>] notes.tsv`: every note's `riseDb`, `afterDb`, `deficitDb` (its level against the
  built-in synth's, against its register's usual difference) and the reference's values, cut-short
  measures, `flagged`. To try other thresholds, filter these; the defaults are in
  `PlaybackVerify::Settings`.
- `<score> events.tsv`: what MuseScore sent to the plug-ins (time, tick, slot, type, data).
- The owner's own audio export can be checked here against the score without Kontakt:
  `mscore --verify-playback score.mscx --verify-audio export.wav --verify-library "Spitfire Symphony
  Orchestra" --verify-out <folder>` (the events come from this build: say so if the export came from
  an older one).

## With the real library (agents, since 2026-09-29)

Agents can reach a Windows VM of the owner's with Kontakt 8 and SSO (access in the agents' own notes, not
here). What was learned there about checking playback:
- The render must be what the owner hears. Two of MuseScore's renders of the same score differed: the first
  after loading (the patch's KSP script not yet initialised, the score's Controllers… lost when it did) and
  every later one (the mics on, several voices per note, 31-40 notes dropped at the patch's 256-voice limit).
  An export right after opening a score, and this tool's render, were the first kind and missed the bug; the
  owner exports after playing. Both are one kind now (`Vst3Plugin::settle`, docs/HISTORY.md › A patch's own script, and its voice limit).
  When a finding depends on what happened before, render twice (two exports in a row, or a play then an
  export) and compare.
- In the GUI, a play and an export can be driven without a person: a QML plug-in (enabled in
  `plugins.xml`) that calls `cmd("play")` and `writeScore(curScore, path, "wav")`, started over OSC
  (`io/osc/useRemoteControl`, `/plugin/<name>/<method>`); the session must be the console's (a
  disconnected remote session has no audio device: play does nothing).
- `--verify-audio` here on Linux checks such an export against the score's events.
- Kontakt's state can be diffed before and after a change made in its window (`Save Kontakt's state for
  diagnosis`, or a test host's getState): that is how Max voices was found.

## Playback audit (agents, Linux; before any build goes to the owner)

A whole-score check of the sound library events as playback renders them (10-measure chunks, as Seq; no plug-in,
no audio), for the faults the owner should never have to point out (2026-10-09). Code: `libmscore/playbackaudit.*`
(its header lists each check and its source), test `tst_playbackaudit` (fixtures for every check, and an env-driven
run on any score). Checks:

- **OVERLAP** (fail): on one route, a note still sounding more than `[legato] keepMs` into the next note of its line,
  or a key struck while it still sounds there.
- **UNMEASURED** (report, per instrument): swapped (`[slurs] quick`) or early notes whose onset (and swapped level) is
  not from an in-context fit. The map doesn't record where an onset comes from, so the fit files' instrument lists
  say it (`sso_long_onset_fit.json`, `sso_rachm_onset_fit.json`, `sso_rachm_levels.json`, as `gen_spitfire_sso.py`
  reads them).
- **EARLY > NOTE** (warning): a note started earlier than its own written length.
- **TIMING**: a chord's start offset against the note before it in its line (joined without a rest): a step of
  more than 50 ms fails, 30-50 ms warns (Rasch 1979's ensemble asynchrony, applied within a line). The offsets are
  note-ons, early by design by each technique's onset: a step shows where the note's arrival rests on the onsets being
  right, so each finding says whether both onsets are fitted in context.
- **LEVEL STEP** (values only): under one slur at an unchanged written dynamic, the change of level between
  neighbouring notes in dB (the shipped dynamics calibration). No threshold until the owner sets one.

Run it on the standard score (Recommended), through the worktree's wrappers:

```sh
./run-<name>.sh 'cd mtest/libmscore/playbackaudit && MS_AUDIT_SCORE="$HOME/MuseScore/ms3fork/test-scores/Whence 12-TET.mscz" \
  MS_AUDIT_OUT=/tmp/audit-whence.txt QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore'
```

`MS_AUDIT_SETTINGS`: a preset id (`recommended`, the default; `library`) or a `playbackSettings` metaTag
(`heldNotes/early=0;slurs/quick=0`). `MS_AUDIT_MAP`: another map (default the shipped SSO map and its
`.dynamics.json`). The test fails on any OVERLAP; read the report's TIMING fails and EARLY > NOTE before handing a
build over, and say in the hand-over which remain and why. Keep reports out of the repository (the owner's score).

## Testing here (Linux, no Kontakt)

- `tools/playbackverify/try_with_testsynth.sh <build dir> <install dir> [work dir]`: the whole
  thing headless with the MS Test Synth as the library (own HOME, a map, setups), once clean and
  once each with `MS_VERIFY_FAULT` = `pedal-drop:50`, `drop:17`, `truncate:7:120` (faults put into
  what reaches the plug-in, `Vst3Synth`), then `check_faults.py` compares each run's injected faults
  (`MS_VERIFY_FAULT_LOG`) with its report. Expected: clean passes; every detectable fault found;
  nothing else flagged.
- `tst_soundlibrary playbackVerify playbackVerifyDrift`: the analysis on synthetic audio.

## Optional: a GitHub runner on your PC

Not needed: the command above does the same. A runner only saves you starting it and
sending the zip: an agent starts the check and downloads the report itself
(`.github/workflows/verify_playback_owner_pc.yml`).

**What it means:** a self-hosted runner lets **whoever can push to this repository run code on your
PC**, under the Windows account the runner runs as: a workflow is code. The workflow here is started
by hand only (never by a push or a pull request, so nothing from forks reaches your PC), waits for
**your approval on every run**, doesn't check out the repository, only runs a MuseScore built by
GitHub from the repository, and uploads a report of your own scores without audio or note lists.
But anyone who can push could change the workflow, so the approval is what protects you: approve
only runs you expect.

If you want it anyway:
1. GitHub, the repository › *Settings › Environments › New environment* `owner-pc` › *Required
   reviewers*: yourself. (Every run then waits for you under *Actions*: *Review deployments ›
   Approve*.)
2. *Settings › Actions › Runners › New self-hosted runner* › Windows: follow the steps into a folder
   of its own (e.g. `C:\actions-runner`); when `config.cmd` asks, add the label **`sso`**, and answer
   **N** to running it as a service.
3. Start it only when you want checks: double-click `run.cmd` in that folder (a console window: close
   it and the runner is off). Not a service, not at startup.
4. To remove it: `config.cmd remove` in that folder, then delete the folder and the environment.

A separate Windows account for the runner limits what a run can reach, but Kontakt and SSO must then
work in that account too (Native Access activation, the library's location, MuseScore's settings and
setups there), which is set up by hand once.

## The Ableton Live route (assessed, not built)

The idea: use the Live integration (branch `live-integration`: MIDI clock/SPP sync out, "Play through
Live", `.als` import) to verify. What it would and wouldn't catch:

- Live would host Kontakt instead of MuseScore. The bugs so far were in MuseScore's own hosting and
  rendering: the order of CC64 and the chord (the dropped chords), the events for a pedal ending on a
  chord or at a repeat, the offline export path (`Vst3Synth`, `SoundLibraryExport`), setups made from
  the `.nki`. With Live in between, those paths aren't used, so it would not catch them; it would
  test Live's hosting and the MIDI MuseScore sends out, a different path from the export.
- MIDI into Live is real time, through a virtual cable: jitter and dropped messages of the cable
  itself would be mixed with MuseScore's.
- Live can't be driven without a person (no headless mode or command-line render; Live's own API is
  Max for Live / remote scripts inside a running Live), so it can't run in the background or on a
  runner.
- What it could add: a second opinion on a sound (the same MIDI in another host), and recording the
  MIDI MuseScore sends for comparison. The events are already in the report (`events.tsv`), so that
  adds little.

So the verification hosts Kontakt in MuseScore itself, the path the owner's exports and playback use.
