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
2. Double-click **`Verify SSO playback in background.bat`** in its `bin` folder. With nothing
   dropped on it, it checks the test scores that come with MuseScore (`verifyplayback`: piano chords
   at pedal changes, strings with legato, staccato, pizzicato …). To check your own scores, drag
   scores, or a folder of scores, onto the `.bat`.
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
| `missing-note` | one note didn't sound although the other notes of its chord did (the staccato octaves of bars 59-62) |
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
checks may still flag the moment. The thresholds were set on one score, the owner's piano piece (four
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

## Testing here (Linux, no Kontakt)

- `tools/playbackverify/try_with_testsynth.sh <build dir> <install dir> [work dir]`: the whole
  thing headless with the MS Test Synth as the library (own HOME, a map, setups), once clean and
  once each with `MS_VERIFY_FAULT` = `pedal-drop:50`, `drop:17`, `truncate:7:120` (faults put into
  what reaches the plug-in, `Vst3Synth`), then `check_faults.py` compares each run's injected faults
  (`MS_VERIFY_FAULT_LOG`) with its report. Expected: clean passes; every detectable fault found;
  nothing else flagged.
- `tst_soundlibrary playbackVerify playbackVerifyDrift`: the analysis on synthetic audio.

## Optional: a GitHub runner on your PC

Not needed: the `.bat` does the same with one double-click. A runner only saves you starting it and
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
