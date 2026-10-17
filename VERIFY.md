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
   Each score is rendered through Kontakt once per library part alone (plus the built-in synth,
   which is quick), each part with only its own patches loaded (the part before's released first),
   and the mix is those renders added up: every part's patches at once needed more memory than the
   Windows VM's 16 GB on the passages' score (2026-10-16: 18 GB committed, the run crashed late). A
   one-part score is rendered once. How long that takes with Kontakt isn't measured yet (here, with a
   test plug-in, a 13-minute piano score took about a minute). The WAVs (`--verify-wav`) have a valid
   header from the start, brought up to date every second of audio: a run that stops early leaves
   playable files.
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
- **Preflight** (since 2026-10-11; `libmscore/renderpreflight.h`): what the render played with, at the top of
  `summary.txt`, in `report.json` `preflight` and printed first by `read_verify_report.py`: the shipped
  calibration (`share/soundlibraries/<library>.dynamics.json`) and the user's (`<setups folder>/dynamics.json`),
  each path + SHA-256 (16 hex) + time, **per patch which file its curves came from** (`calibration.patches`),
  the map's patches without a curve; the setups folder (here the job's copy, `background verify setups`) and
  what the copy sync did; `playback.ini` path + SHA-256 + the preset its values match. Every `WARNING` line
  matters: a patch on the user's file, a copy file replaced / removed / kept different, ini warnings. **Read it
  before trusting a render's levels** (2026-10-11: a partial `dynamics.json` left in the job's copy played
  Whence's staccatos uncalibrated, unnoticed until the owner heard it). A report without `preflight` came from an
  older build: which calibration it used is unknown.
- The job's setups copy (`RenderPreflight::syncJobCopy`, VERIFY): at each start the calibration is made the main
  folder's (a copy's `dynamics.json` replaced, or removed when the main folder has none), other files the main
  folder has newer replace the copy's, missing ones are copied (with their time); made setups are kept (each is
  checked against its `.nki` at its load via `made setups.json`: made again, or Kontakt's own state taken from the
  main folder). The measuring jobs (`--extract-library`, `--check-*`, key scan, pictures, load times) keep their
  copy's own results (JOB: only missing files copied) and log each difference as a `WARNING` in their log's
  preflight; `--live-equivalence` writes `preflight.txt` in its folder, `--create-live-set` adds it to `<set>.txt`;
  the working MuseScore appends it to `render preflight.log` in its setups folder at each calibration load and
  Reload Playback Settings.
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
  after loading (the patch's KSP script not yet initialised, the score's controller settings lost when it did) and
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
run on any score). It checks the renderer against the map's own onsets and the shipped calibration, not the real
sound: whether the onsets are right is the Windows VM's job (above). Checks:

- **OVERLAP** (fail): on one route, a note still sounding more than `[legato] keepMs` into the next note of its line,
  or a key struck while it still sounds there.
- **UNMEASURED** (report, per instrument): swapped (`[slurs] quick`) or early notes whose onset (and swapped level) is
  not from an in-context fit, and notes whose technique has no onset in the map at all (arrival unknown). The map doesn't record where an onset comes from, so the fit files' instrument lists
  say it (`sso_long_onset_fit.json`, `sso_rachm_onset_fit.json`, `sso_rachm_levels.json`, as `gen_spitfire_sso.py`
  reads them).
- **CAPPED** (report, the cut in ms): a note started less early than its onset says (x `<Onset early>` %): cut where
  the note before on its patch keeps `[legato] keepMs` (that note started earlier than its own length), at the start
  of the pass, or else (chunk start, grace notes, arpeggio, legato transition). It arrives late by the cut. A cut
  within one tick is the renderer's rounding to ticks and not reported.
- **ARRIVAL**: predicted arrival = sent time + the onset the map gives the note (as the renderer takes it: by pitch for a
  swap or `[heldNotes] byPitch`, else the median; playback.ini's `[heldNotes.onset]` applied) - written time; a chord's
  earliest against the note before in its line (joined without a rest): more than 50 ms fails, 30-50 ms warns (Rasch
  1979's ensemble asynchrony, applied within a line). With the full early start it is ~0 by construction; what shows
  is CAPPED notes and `<Onset early>` below 100. A legato transition (the renderer's `LibTrace::from`) is no attack:
  it arrives at sent + the transition's arrival the renderer models for its bend (`LibTrace::glideArriveMs`: the
  patch's measured bendGlide by the note before's length, else `legatoDelay` by interval, quicker after a short note)
  - written (2026-10-14; before, its articulation's onset); no delay in the map, no arrival. That compares a pitch
  arrival with an attack's loudness onset. Sent times are on the score's time: the start lead
  (`EventMap::startLeadMs`) taken off.
- **LEVEL STEP** (values): under one slur at an unchanged written dynamic, the change of level between neighbouring
  notes in dB (the shipped dynamics calibration). Proposed threshold, not enforced, owner to approve: the intensity
  difference limen of Jesteadt, Wier & Green 1977 (JASA 61, 169-177; dI/I = 0.463 (I/I0)^-0.072 as Praat's manual
  quotes it), 0.69 dB at 60 dB sensation level (0.93 at 40, 0.50 at 80); steps above it are marked "> DL".
- **DROPPED** (fail): a legato transition (the renderer's `LibTrace::from`) on an articulation that plays transitions,
  sent less than 15 ms after the previous legato note-on on its route, at velocity 64 or lower. SSO's legato engine
  doesn't play such a note at all (behaviour from reading its script privately, not quoted). Notes of one chord are not
  compared. Part, bar, beat, pitch, velocity, gap in ms.
- **LEAP** (warn): a legato transition whose interval from the note before exceeds an octave (12 semitones; with
  microtones 1200 cents by `ScoreTuning`). SSO gives it no transition: it releases the note before and attacks afresh,
  so the note's Performance technique, `legatoDelay` timing and level calibration don't apply. Same fields plus the
  interval.

Run it on the standard score, *Whence* (quarter tones, `Whence.mscz`, since 2026-10-11; Recommended), through the
worktree's wrappers:

```sh
./run-<name>.sh 'cd mtest/libmscore/playbackaudit && MS_AUDIT_SCORE="$HOME/MuseScore/ms3fork/test-scores/Whence.mscz" \
  MS_AUDIT_OUT=/tmp/audit-whence.txt QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore'
```

`MS_AUDIT_SETTINGS`: a preset id (`recommended`, the default; `library`) or a `playbackSettings` metaTag
(`heldNotes/early=0;slurs/quick=0`). `MS_AUDIT_MAP`: another map (default the shipped SSO map and its
`.dynamics.json`). The test fails on any OVERLAP; read the report's ARRIVAL fails and CAPPED notes before handing a
build over, and say in the hand-over which remain and why. Keep reports out of the repository (the owner's score).

## Heard check (agents; Linux + the Windows VM; before any build goes to the owner)

The audit above checks the renderer against its own model, so a wrong model passes it: on 2026-10-16 it reported
ARRIVAL 0 fail while Whence's Violas slurs (an eighth + two sixteenths, written 273 / 136 / 136 ms) sounded about
200 / 210 / 105 ms, heard as a triplet by the owner. The heard check measures the **real sound**: every library route
rendered with Kontakt + SSO on the VM, each note's heard time measured, judged against the written times and against
the audit's predictions. Code: `tools/playbackverify/heard_check.py` (its header holds every rule and source),
`heard_check.sh` (end to end), `heard_export_vm.cmd` / `heard_check_vm.cmd` (the VM side of each renderer), the inputs from `PlaybackAudit::writeHeardInputs`
(`tst_playbackaudit auditScore` with `MS_AUDIT_DUMP=<folder>`), tests `test_heard_check.py` (synthetic audio).

```sh
HC_RUN=~/MuseScore/ms3fork/run-<name>.sh tools/playbackverify/heard_check.sh ~/MuseScore/ms3fork/test-scores/Whence.mscz [preset] [parts]
```

The preset is `recommended` (the default), `library`, or a `playbackSettings` metaTag (`slurs/transitionsEarly=0`);
parts a comma list (default all). `HC_RUN`: a wrapper running a command in the build dir (else `HC_BUILD`, the build
dir). It writes the dump, the renders and `report.txt` / `report.json` to `~/.cache/heardcheck/<run>/` (`HC_WORK`; the
renders deleted after the analysis unless `HC_KEEP=1`), uses `C:\claude\<HC_VMDIR, default heardcheck>\<run>` on the VM (one share login,
deleted after) and exits 1 on any FAIL. Keep the reports of the owner's scores out of the repository.

- **Render**, two renderers, each route alone (the analysis needs one route's sound):
  - `HC_RENDER=export` (**the default**, 2026-10-16): MuseScore's own export, `--verify-playback --verify-wav` with
    `MS_VERIFY_ROUTES=1` (build 443 on, https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38997724166;
    `HC_EXE` the VM's MuseScore3Evo.exe): every library route rendered alone as File › Export audio renders it, so with
    what the owner hears: the tuning slots (Kontakt's Tune: quarter tones on the non-bending patches too), the gain stage
    and master trim, the export mix, the flat lanes' plug-in parameters (Whence's Violins 1 vibrato 0), the transport
    restart. Its copy of the score carries the dump's settings as its playbackSettings metaTag (`heard_check.py score`: the
    preset's values with the score's own over them); a MusicXML score is imported by the VM's MuseScore first and the
    dump made from that same copy. `HC_SPLIT=parts` (the default): one MuseScore run per library part, the others on
    MS4's sounds (partPlayback metaTag): each run loads only its part's instances, and a crash loses one part (one run of
    the whole passages crashed with 0xC0000409 after 24 of 34 routes, the `--verify-wav` crash `C:\claude\crashfix` is
    on; per part, none); `whole` one run. Alignment: the WAV's sample 0 is score time 0 and the events are placed
    sample-exactly (the renderer splits its blocks at each event), so the offset is **0**: export against kthost on 5
    untuned All techniques routes of Whence (identical events) null at -46 … -61 dB at lag **0 samples** after a gain
    (the gain stage: Violins 2 +1.12, Violas -7.08, Celli +2.56, Basses +3.44 dB), the first sound 0.03-1.9 ms after the
    first note-on in both. (The -16 … +34 ms "offsets" fitted before were the onset readings, not the timeline.) The
    analysis takes each note's note-on from the export's own `events.tsv`, paired with the dump's by key and time
    (`analyse --export`; reported: Whence 454 / 454 within 0.05 ms, the passages 600 / 600): a part run has its own
    start lead (the passages' parts 133-216 ms less than the score's), taken out per run; a note the dump put on another
    lane is re-routed (once in 13 dumps of Whence the dump gave Violas - Performance 2 lanes, the export 3: the renderer
    isn't always deterministic there; not investigated).
  - `HC_RENDER=kthost`: each route through kthost (one Kontakt instance with MuseScore's setup of the route's patch from
    the VM's setups folder), the dump's events + 1.0 s. It lacks the tuning slots (a non-bending patch's quarter tones
    play in 12-TET), the gain stage, master trim and mix (the passages' Performance routes ~30-50 dB quieter in
    perceived level than exported) and the flat lanes' parameters; its probe note waits 60 s on every Basses route.
  Both: MuseScore's events as playback renders them (10-measure chunks), the start lead included.
- **Heard time**: an attack (staccato, held note, a slur's first note) by its perceived envelope (`loudness.py`): the
  -15 dB time under its peak less 30 ms, the rule the map's onsets were measured by (so a step onset reads ~24 ms
  early, a swell near its -15 dB time); the peak over the note's own span, a slur's first note over its slur. A note
  before on the route still within 15 dB: "masked", its tail (extrapolated in dB) taken off in loudness first. Beside
  it a -6 dB reading (`heardAltMs`, and the slurs' RHYTHM counts with it in one line, not judged): on Whence's Violas
  slurs the owner's ear (~200 / 210 / 105 ms) matches -6 dB on the smooth first notes, not -15; which rule is right for
  slow attacks is open (owner). A legato transition by pYIN over its whole route: the note before's pitch heard, then
  left, then the first of 3 frames within 30 cents of its pitch (quarter-tone cents included) before the next note-on
  on the route (the bendGlide's model arrival is 9/10 of the way: for a semitone 30 cents is 7/10). Not reached:
  **UNHEARD** (warn: a skipped note, a microtone not reached, or pYIN lost it; listen), with the f0 closest to the
  target between its note-on and the next (`closestCents`).
- **ARRIVAL** (fail > 50 ms, warn > 30): heard - written against the note before in its line (Rasch 1979, as the
  audit's ARRIVAL).
- **RHYTHM** (fail > 0.10, warn > 0.05; approved by the owner 2026-10-16 ("sure, go with your rec")): per slur (its notes and the next onset) and per
  run (a line's notes joined without a rest), windows of 3 inter-onset intervals: the largest difference between a
  heard IOI's share of the window and the written one's. 0.10 is derived, not published: Desain & Honing 2003
  (Perception 32, 341-365) category means for 2:1 scatter with SD ~47 ms in a 1 s bar (as tabled in Sadakata, Desain &
  Honing 2006, Music Perception 23(3)): 2 SD ~0.094; players' largest SD there ~37 ms (2 SD 0.074) stays under it.
  0.05: audible, not categorical (Friberg & Sundberg's JND, ~10 ms below ~240 ms, ~5 % above, their 1993 ASA abstract).
  The Violas' 200 / 210 / 105 for 273 / 136 / 136 is 0.158.
- **LEVEL** (warn): under one slur at an unchanged written dynamic, the change of the notes' loudest perceived level
  above the audit's proposed 0.69 dB. The renders play at the setups' own volumes (no gain stage): steps within a route
  only.
- **Model drift**: measured - the audit's predicted heard time (`arrivalMs`: sent + onset, or sent + glideArriveMs) per
  patch x situation (first after silence, attack joined, staccato, transition after attack, transition after
  transition), median and 10th-90th percentile; |median| > 20 ms marked DRIFT. Where it drifts, the map's numbers (or
  the renderer's model) are wrong for that situation, and the audit can't see it.

Time (2026-10-16, VM, uncontended), end to end: **export** Whence 5.1 min (VM 258 s, 5 part runs), the passages 7.1 min
(VM 334 s); **kthost** Whence 5.8 min (VM 295 s), the passages 17.5 min (VM 850 s). Before that: ~10-25 s of kthost per route (+60 s per Basses route: kthost's probe note never passes
-60 dBFS at their setups' levels, so it waits its 60 s; their renders sound), analysis ~1.5 min (Whence) to ~4 min
(the passages; pYIN on every Performance route). Whence 7.6 min end to end, the passages ~20 min. Performance renders
differ take to take (HANDOFF.md): one run is one take; for a finding that matters, run twice.

First results (integrate-1012 41cdbbb311, Recommended; numbers: commit message of the heard-check branch): Whence's
Violas bars 9-11 reproduced (each slur's 2nd note heard 27-60 ms early, its smooth first note 90-160 ms before its
beat by the -15 dB rule, +6 and +77 by -6 dB, one unreadable); Whence's fast quarter-tone sixteenths (Violas bar 8,
...) mostly UNHEARD: f0 a slow wave that never settles on the quarter tones; model drift on every Performance patch's
transitions and first notes, none on the staccatos (within 20 ms). Masked readings of a held Long re-attacked on its
route are unreliable (Celli bar 11, +539 ms): check those by ear before acting on them.

**The two renderers side by side** (2026-10-16, integrate-1012 8cfc8f56b1 + this branch, Recommended, build 443, one
take each; kthost | export, fail / warn):

| | Whence kthost | Whence export | passages kthost | passages export |
|---|---|---|---|---|
| measured notes | 233 / 454 | 225 / 454 | 439 / 600 | 443 / 600 |
| ARRIVAL | 29 / 32 | 33 / 30 | 162 / 48 | 164 / 50 |
| RHYTHM slurs | 4 / 2 | 1 / 1 | 29 / 9 | 27 / 13 |
| RHYTHM runs | 10 / 9 | 9 / 11 | 29 / 12 | 26 / 19 |
| LEVEL (warn) | 25 | 16 | 62 | 65 |
| UNHEARD (warn) | 126 | 132 | 90 | 89 |

(Whence's whole-score export run, a second take: ARRIVAL 41 / 27, RHYTHM slurs 4 / 1, runs 11 / 12: the Performance
patches' take-to-take spread is as large as the renderers' difference.) Model drift agrees on the All techniques
staccatos (within 3 ms) and the transitions; it differs where the export plays what kthost doesn't:
- **Performance attacks read 30-60 ms earlier** in the export (passages, first after silence / attack joined: Celli
  -8 / +33 -> -58 / -52, Violins 2 -5 / +31 -> -55 / -31, Basses +51 / +155 -> +4 / +15 ms): the level. The kthost renders
  scaled to the export's levels read the same as the export (Celli -66 / -61, Violins 2 -59 / -42, Basses -11 / +5): the
  perceived envelope is level-dependent, so the export's readings (the real levels) are the ones to trust.
- **Whence's Violins 1 Longs on the tuned copies** (route +50 cents, bars 12 and 16): kthost -10 / -2 ms, the export +67 /
  +225 ms, a slow swell ~10 dB under kthost's attack with identical events; the likely cause is the score's vibrato 0
  (a flat lane, set on the instance; kthost plays the patch's default), not proven.
- **Quarter-tone transitions are tracked** in both (the Violas' and Celli's are on bending Performance patches, so
  kthost played them by pitch bend too): Violas bar 8's sixteenths are a slow wave between 62.0 and 64.1 that never
  reaches 61.5 / 64.5 (closest 40-60 cents off), Celli bar 1's alternating 48 / 47.5 closest 70-180 cents off: the sound
  doesn't reach the quarter tones at that speed (UNHEARD is the finding, not a tracking loss). Whence's quarter-tone
  transitions heard: 32 / 141 kthost, 26 / 141 export; median closest approach of the unreached ones 60 cents (Violas,
  Violins 1), 90 (Celli).

Not covered: chords on one Performance route (not pitch-tracked); the export renderer's split runs play each part with
its own start lead and master trim (levels are compared within a route only, timing against each run's own lead).

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
