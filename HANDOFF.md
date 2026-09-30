# Handoff: state of the sound library work (2026-09-25)

The work so far was done in a claude.ai cloud session, which has ended. This file tells the next
agent (Claude Code on the owner's local machine) where things stand. Read `CLAUDE.md` first: it
describes the architecture, the build, the articulation check and every run the owner has
made. The commit messages on this branch explain each step in detail.

## Where things are

- Branch: `main` (called `ms4-playback` until 2026-09-27), the default branch (the
  sound-library branch was merged into it and deleted on 2026-09-25). Develop and push there;
  no pull requests.
- Latest Windows build: run 11 (commit 6bc3a62). All 10 `tst_soundlibrary` tests passed on
  Windows. Artifact `MuseScore-soundlibrary-win64`:
  https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/36178930191
- That build has:
  - the check memory (`checks.json`; *Tick what needs checking*);
  - the scan of every value 0–127;
  - *Add a patch…* for patches missing from the map;
  - the map fixes from the owner's full 42-patch run: Legato 20 is removed from the woodwinds
    and brass, and trills 70/71 from Trumpets a6.

## Tuning and Ethanol bar 14 (2026-09-26, edb5eec)

Built-in tuning is in (see `CLAUDE.md` › Tuning) along with the all-sound-off after a faded MS3
hairpin (Ethanol bar 14). Still open:
- the playback regression (`ab/trace/regress.sh`) has not been run on edb5eec. The `ab/` harness
  is not in the repository, so a new cloud container doesn't have it;
- *Tools › Tuning…*: the owner won't test it for now (2026-09-26: "leave it for now"); Ethanol
  bar 14 confirmed good;
- the owner: try the Playback drop-down (Mixer, Play Panel) switching MS3 / MS4 / SSO;
- the owner: pitches for the 49 accidentals MuseScore gives none;
- the kits' hit lists: done (2026-09-26, the owner's screenshots); the 42 one-drum patches' keys: a key
  scan (*Tick the patches to scan*), named from the `.nki` group order (CLAUDE.md › Articulations from the files);
- the owner: re-check the tuned percussion (Timpani, Celeste, Marimba, Vibraphone, Tubular
  Bells) with the fixed key scan.

## Extract plug-in data (2026-09-27)

The owner asked for all the data the SSO plug-in gives, for more control of it. *Check articulations…* ›
*Extract plug-in data* (see `CLAUDE.md` › Extract plug-in data). Next: the owner runs it (first without
*Try every controller* on all patches: fast; then with it on one patch per family: strings, woodwinds,
brass, tuned percussion, a Performance patch, Grand Piano) and hands back the zip. From it: which CCs
Spitfire's patches answer (expression, vibrato, release, tightness, reverb …) and their values, which
parameters Kontakt exposes by name, and whether its state holds anything readable. Then decide with the
owner which of those MuseScore should drive.

The MuseScore side for that is in (2026-09-27, see `CLAUDE.md` › Controllers): map `<Controller>`
(a CC or a plug-in parameter by title, default, staff texts), per-part values in the score, sent
in playback and export, set in *View › Sound Library…* › *Controllers…*. When an extract with
*Try every controller* arrives: run `tools/soundlibraries/controllers_from_extract.py <zip>`,
choose with the owner which controllers MuseScore should set and what they are called, put them
in `CONTROLLERS` / `PATCH_CONTROLLERS` of `gen_spitfire_sso.py`, regenerate the map.

## Extract library files (2026-09-27)

The owner asked to extract all the data possible from SSO for a full integration later. The plug-in's side is
*Extract plug-in data* (above); the library's files are `tools/soundlibraries/extract_library_files.py`
(see `CLAUDE.md` › Extract library files). Next: the owner downloads `ExtractLibraryFiles-win64` from the
workflow "Tool: Extract library files", double-clicks `ExtractLibraryFiles.exe` (or gives it the SSO folder)
and hands back the zip it names. From it (`--report`, then `library.json` and `archives/`):
- first run (03:27, library.json left out: 800 MB): SSO's 700 patches are readable, none encrypted, although
  the owner has the free Kontakt Player (see `CLAUDE.md` › Extract library files); 432,829 sample names.
  The extractor now sums zones up per group (small enough to send); the owner re-runs it to send the groups
- from the groups (articulation × dynamic × round robin, mic headers, release triggers), their key and
  velocity ranges and samples: check the map's articulations, ranges and keyswitches against the patches
  themselves (e.g. why Violins Long Sul G is silent in "All techniques");
- the sample names in the `.nkx` archives: which articulations, dynamics, round robins and mic positions
  exist per instrument, and their ranges;
- ProductHints, registry and database rows: the library's id, version and product names.
Then decide with the owner what goes into the map (ranges per articulation, round-robin counts, release
samples) and what MuseScore should drive.

## SSO's controls in MuseScore (2026-09-27)

The map drives SSO's named controls as Kontakt parameters (see `CLAUDE.md` › Controllers): per part in
*View › Sound Library… › Controllers…*. Next: the owner tries them on a build with this, checks which
controls say "not in <patch>" (a wrong title guess: fix `_p(...)` in `gen_spitfire_sso.py` with the title
Kontakt shows) and which mic "Mic 1" … "Mic 5" is, then the "?" names become real ones. Untried with
Kontakt: whether Spitfire's script keeps a value set this way.

## Plug-in extract, first run (2026-09-27 03:58)

Violins 1 only (see `CLAUDE.md` › Extract plug-in data › First run on SSO). Its 15 named automation slots
(Dynamics, Vibrato, Release, Tightness, Expression, Mic 1-5, Mic Mix Distance, Articulation Controller)
are what the Controllers map should drive, by parameter title. Open: whether every patch names the same
slots (a quick Extract plug-in data without *Try every controller* on one patch per family answers it), and
generating setups from the `.nki` files (Kontakt's state embeds the whole patch in a multi: a writer for
that multi, then one generated setup tried by the owner).

## Setups made by MuseScore (2026-09-27)

The owner loaded a setup made by `make_setups.py` in run 91 ("everything looks correct"), then asked for it
in MuseScore with no manual set-up, every patch at the library's defaults. Done (see `CLAUDE.md` › Plugin
hosting › Setups made by MuseScore): `KontaktSetup` (C++), the map's `<Files>` / `nki=` / `setup=` / `<Patch>`,
the host making setups on demand, *Set up…* / *Save setup* / *Add a patch…* gone, all 700 patches in Check
articulations with *Tick all*. The owner's `learned_settings.json` showed `$iooxo` 3 as the one setting.
Next, with the next Windows build: the owner plays a score (setups made on first load; the hand-made ones
moved to `old setups (not used)`), then *Check articulations* › *Tick all* › *Extract plug-in data* (describe
only) for every patch, and a *Check* of the map's patches with the made setups. If Kontakt refuses a setup
or its samples are missing: compare with make_setups.py's (which Kontakt loaded) and look at the sample
list first. Kits: at the library's defaults now (the owner: what they have off comes from the one-drum
patches, whose keys at their defaults aren't known yet: a key scan of those, or the owner's screenshots).

## Dynamics across techniques (2026-09-28)

Calibration in (see `CLAUDE.md` › Dynamics calibration, › Faster, in the background). Next: the owner
double-clicks `Measure SSO dynamics in background.bat` (every map patch, Performance ones included, in a
copy of MuseScore of its own) and hands back the zip; the summary's "# Dynamics balance" shows what's left
("!" lines).

Shorts on velocity now follow the dynamics (see `CLAUDE.md` › Shorts). Next: the owner runs *Check articulations*
with *Dynamics* ticked on the patches they use (one per family at least: strings, woodwinds, brass, a
Performance patch) and hands back the zip; from summary.txt, fix `<Dynamics velocity>` in `gen_spitfire_sso.py`
(techniques flagged "on velocity but not listed" / "not needed") and look at the flagged spans.

## Even dynamic steps (2026-09-28): in, waiting for the owner's ear

See `CLAUDE.md` › Even dynamic steps. Next: the owner runs *Measure dynamics in the background* with this build
(it measures the held notes' CC11 curves; the recording modes work from the older measurement too), restarts,
then compares the four modes of Advanced Options › *Even dynamic steps* on a score and picks one. Then: make
the chosen one the default (or keep the switch) and drop the others if the owner wants.

## Attack salience in the Recommended balance (2026-09-28, branch `attack-prominence`)

See `CLAUDE.md` › Attack salience. The owner's ear put strings' shorts at -4 dB where the loudness model said +1.
The recommendation now adds the shorts' attack salience with one weight fitted to what the owner heard right
(map: strings -4). Next: the owner runs *Measure dynamics in the background* with a build of this branch
(~5 min; it fills the "attack" curves), restarts, and reads summary.txt's "# Dynamics balance": per family
loudness only / with attack salience, and the fitted weight. Strings will show -4 (the reference); the other
families are the model's prediction: the owner tries them by ear (e.g. brass, woodwinds in a score) and, where
one sounds right at another value, sets it in Advanced Options › *Heard right* (the fit then uses every
reference, least squares). If the weight comes out 0 or the report says the attacks stand out no more than
their loudness, the feature doesn't explain the ear: look at the per-short "attack … beyond that" numbers.

## Automation (2026-09-28): infrastructure in, no UI; being redone through Ableton Live

The owner (2026-09-28): edit automation in Live 12 instead of an editor of our own; MuseScore keeps playing on
its own, Live's automation imported back read-only. Built on branch `live-integration` (worktree `wt-live`).
See `CLAUDE.md` › Automation. Earlier next steps: the UI (lanes to draw per part, targets from the part's patch's
controllers), a dynamics lane on top of the notation's, the map's controller entries for SSO's named
controls (`gen_spitfire_sso.py` from sso_patch_controls.json), and one try with Kontakt that a parameter
set by automation is heard (e.g. Vibrato on Violins 1).

## The Mixer on library parts (2026-09-28, branch `mixer-sso`): in, waiting for the owner's Windows check

The owner: "the mixer panning tool doesn't work … make all buttons in the Mixer work with SSO". See `CLAUDE.md`
› Plugin hosting › The Mixer on library parts (audit table, design). Volume and pan act in MuseScore on each
Kontakt instance's output (not CC7 / CC10), mute and solo silence the part's instances, reverb and chorus are
disabled for hosted parts (SSO's own room), the patch and port / channel show the library's. Tested here with
the test synth only. The owner, on a Windows build of this branch, with an SSO score:
- pan a string part hard left and right while it plays (it should move smoothly, no clicks), and a part with
  extra patches (a Performance legato plus shorts) and one with copies for other tunings: every note follows;
- volume down and up while playing; volume 100 / pan centre sounds exactly as before;
- mute and solo while long notes ring: they stop at once, nothing hangs, unmute brings the next notes back;
- export audio with a part panned, quieter and muted: the file matches (solo is ignored in an export, as for
  MuseScore's own sounds);
- the Mixer's details panel for an SSO part: reverb / chorus greyed with the tooltip, the patch reads the SSO
  patch, port / channel greyed; switch the part to "MuseScore 4" in "This part plays:": all back as before;
- if Kontakt was relied on for pan (its own Pan knob): it stays where the setup put it; the Mixer adds to it.

## Controllers and the Mixer live while playing (2026-09-29, branch `live-controls`): in, waiting for the owner's Windows check

The owner: "make what I do in the mixer or sound library controller reflect in the live playback". The
Controllers window (*View › Sound Library…* › *Controllers…*) was modal and stopped playback on OK; plug-in
parameters were only set at the next play. Now it is a window of its own that playback goes on behind: each
move is heard at once (Kontakt parameters on all the part's patches, extras and tuning copies; MIDI controllers
sent now and the ~10 measures rendered ahead corrected), OK keeps the values (undoable), Cancel puts back what
the part had. The Mixer was already live (volume, pan, mute, solo); the idle timer now also runs while the
patches load at score open. See `CLAUDE.md` › Controllers › Live. Tested with the test synth only (unit tests;
the window itself not run here). The owner, on a Windows build of this branch, with an SSO score playing:
- open *Controllers…* for a string part and drag Mic 1-5 levels / Mic Mix Distance / Release while it plays:
  the sound should change at once, without clicks or dropouts; the same on a part with extra patches (the
  Performance legato plus shorts): every patch follows;
- untick a control while playing: the patch's own setting comes back; Cancel: everything back as it was
  when the window opened; OK: playback goes on, Ctrl+Z undoes the change (heard from the next play);
- stop and start playback with the window open: the values stay as set in the window;
- does Kontakt keep a value set this way (a Spitfire script could reset a mic fader on the next note)? If a
  change is heard and then jumps back, say which control;
- the Mixer while playing, right after pressing play and just after opening a score (patches still loading):
  volume, pan, mute, solo act at once.

## Merging with claude/intelligent-cray-6pd4o1 (prepared 2026-09-28; done 2026-09-28 on that branch, as planned)

The owner decides what goes to main; this is the plan. Both branches start at main 16e6933. That
branch was still being pushed to (a38eeba, 00:47 UTC): merge once its session pauses.
- Its commits not here: the background extract (098e85a, b7b274a, 32e8bfc, a38eeba), 4d1d657 (a setup
  Kontakt gives back unchanged isn't taken for its state), 036dc3e's NIKT title rule (soundlibrarycheck.cpp;
  the rest of 036dc3e is 9fa4ce2 here), 17ee5fb, 22219702 (soundfont), b209f4e (notes).
- `git merge-tree` conflicts, four files: `audio/vst3/vst3synth.cpp/.h` → this branch's (its `deliver`
  keeps varispeed; the other side is a subset); `.github/workflows/test_soundlibrary_windows.yml` → this
  branch's soundfont fix (67e5a42: kept with the cache, no download at each run; drop 22219702's step);
  `CLAUDE.md` → this branch's text, adding from the other: Kontakt receives the pitch bend (run 134),
  Timpani bends ±2 semitones. Everything else merges cleanly.
- After merging: build, `tst_soundlibrary`, `tst_tuning`; check the NIKT rule and 4d1d657 are in.

## SSO load times (2026-09-28, branch `sso-load-times`): in, waiting for the owner's measurement

The owner: "have a sub agent try to optimize load times of the SSO plugin". See `CLAUDE.md` › Plugin
hosting › Load times. In: the routes worked out once (not before every instance at score open and at every
play), no 100 ms pause between loads, a faster title index, Kontakt's own states shared between the
working setups and the background runs' copies (no slow first load for a patch any run has loaded), a
resaved setup kept over a Kontakt update, `load times.log` by step and per batch with when the memory
settled. Off until measured: setState on worker threads (`io/soundLibraryLoadThreads`).

The owner, once, with a build of this branch: close MuseScore, drag the score to speed up onto `Measure SSO
load times in background.bat` (bin), wait (20-60 min), hand back `Documents\MuseScore Sound Library
Check\Spitfire Symphony Orchestra load times <date>.txt`. Then, optionally, open that score in the new build
and hand back `load times.log` (setups folder) too: the real score open with the new code. From the report:
- phase 1: where a patch's time goes (new instance vs setState's component / controller; "one object" and a
  controller part as long as the component would mean Kontakt parses its state twice), how long until it
  sounds and how much memory;
- phases 2 and 3: whether Kontakt takes setState on worker threads, and how much faster the score is ready.
  Faster and no failure: make `io/soundLibraryLoadThreads` 2 or 4 by default. A hang (the run's last line
  says so) or failures: leave it off;
- phase 4: whether a spare taking a new patch frees the old one's samples and is faster than a new instance;
- phase 5: whether the Mic levels at 0 free memory (then mics a score doesn't use could be set to 0), and
  which saved script value, turned to 0, frees memory and silences which articulations. If one holds the
  technique switches: a setup per part with only the techniques its notation plays (`usedPatches` already
  knows them per patch), made with `KontaktSetup::withScriptValues` from Kontakt's own state.

## Piano v3.7's missing notes (2026-09-29, branch `piano-v37-fixes`): fixed, checked on the Windows VM

Measured with Kontakt 8 + SSO on the VM (see CLAUDE.md › Plugin hosting › A patch's own script, VERIFY.md ›
With the real library). Causes: (1) the Grand Piano's 256 voices, too few once the score's mics are on
(31-40 notes dropped in busy bars, the owner's e39fe815 export); (2) the patch's script initialised only at
the first render or play, which then dropped the Controllers set at score open (so only the second render
had the mics, and the drops); (3) bars 68-69 (a key struck while still sounding) was 7755d65, confirmed.
Fixed: 512 voices for every Kontakt patch MuseScore sets up, and `Vst3Plugin::settle` after each load.
The owner: listen to the clips handed over (bars 49-52, 59-62, 68-69) and to a live play of the piece with a
build of this branch; say whether the mic mix sounds as set in Controllers… from the first play on.

## What the owner is doing now

The fifth run (all 42 patches, scanned) is in: see `CLAUDE.md` › Tried by the owner. Steps 1, 2
and 4 of the last request are done (Oboe Solo and Solo Cello pass; the scanned articulations
are mapped where notation can ask for them). Still open:

- Sixth run (build before the extras): Motif Brass's six UACC values are right (names and
  sound); Grand Piano has Direct (1) and Tape (2), now mapped; every patch had its instrument
  loaded. The tuned percussion has no UACC setting: keyswitches from C-2 (Kickstart). The key
  scan now finds keyswitches; the owner runs *Check* on the tuned patches they use, then map
  them like Timpani (KEYSWITCHED and SPITFIRE_ADD in `gen_spitfire_sso.py`; techniques from the
  names on the sheet, e.g. Roll → tremolo, Muted → muted, Swell → none).
- Violins Long Sul G / Celli Long Sul C: settled, Spitfire's "All techniques" patch plays no
  samples for them (Kontakt's Voices stays 0).
- Extra patches and percussion kits are in (see `CLAUDE.md`). The owner was asked to set up,
  in *Check articulations*, the Performance patches of the instruments they use, and the
  percussion patches (Drums - High / Low, Unpitched - Metal / Wood, Other - Toys), then Check
  (tick *Scan every value* for the Performance patches: their UACC value is a guess) and send
  the zip. From it:
  - a Performance patch's scan: its real articulation values; fix LEGATO / PERFORMANCE in
    `gen_spitfire_sso.py`;
  - a percussion patch's key sheet (`<patch>.png`, `results.json` "keys"): which key plays
    which sound; fill `DRUMS` in `gen_spitfire_sso.py` with (MuseScore drum pitch, key, name,
    ids). MuseScore's drum pitches are GM-like (35/36 bass drum, 38 snare, 49/57 crash, 51 ride,
    52 chinese, 55 splash, 80/81 triangle, 54 tambourine, 56 cowbell, 75 claves, 76/77 wood
    block, 85 castanets …); check `share/instruments/instruments.xml` for each instrument.

## When the zip arrives

1. Unzip it. Read `summary.txt` and `results.json`: per patch, `passed`, the verdicts, and for
   scans `found`, `notInMap` and `mapValuesShowingNone`. A scan marked "inconclusive" doesn't
   mean anything is missing; use its pictures only.
2. Run `python3 tools/soundlibraries/read_loaded_patches.py "<folder>"`. It reads the loaded
   Kontakt instrument's name from each `(window).png` and says whether each patch was set up
   with the right `.nki` (numbers must match: Violins 1 vs 2, Horns a2 vs a4). Tell the owner
   about any "BAD" line before trusting that patch's results.
3. Run `python3 tools/soundlibraries/read_check_names.py "<folder>"` (it needs tesseract and
   ImageMagick `convert`). It OCRs the articulation name in each picture and lists values
   whose name differs from the map's, or that show "None". The OCR misreads some names
   (Long becomes "Large"), so look at the `<patch>.png` contact sheets for anything it flags
   before changing the map. For added patches, the "map" column reads "(not in map)": the OCR
   name is what the patch calls that value.
4. Fix or extend the map only through `tools/soundlibraries/gen_spitfire_sso.py`:
   - A new patch is a row in `I` (bank, name shown, MuseScore instrument ids, partName regexp).
     Its values come from the Reaticulate bank. When the bank lacks the patch, add its
     articulations from the scan the same way `SPITFIRE_ADD` does.
   - New articulation names need an entry in `T` (techniques, modifiers). Otherwise they are
     left out.
   - `SPITFIRE_ADD`, `SPITFIRE_DROP` and `SPITFIRE_RENAME` hold per-patch corrections.
   - Regenerate the map (the command is in the script's header; it needs a clone of
     github.com/jtackaberry/reaticulate) and diff `share/soundlibraries/Spitfire Symphony
     Orchestra.xml`. Only the intended entries should change.
5. Update the "Tried by the owner" part of `CLAUDE.md` with what the run showed.
6. Build and run `tst_soundlibrary` locally if you can (`CLAUDE.md` › Building). The
   `spitfireMap` test checks instrument matching.
7. Push. If code changed (not just the map) and the owner needs a new Windows build, put
   `[windows-build]` in the last commit message. The workflow then builds, tests and uploads
   in about 15 minutes. Use Actions minutes conservatively (the owner, 2026-09-27; see
   `CLAUDE.md`): only when the owner needs a new build to try something;
   - validate locally first;
   - batch fixes into one run;
   - don't retry blindly.

   A map-only change also needs a new build to reach the owner, because the XML is installed
   with MuseScore. Alternatively the owner can copy the regenerated XML over
   `share/soundlibraries/` in their installed folder.

## Things only the cloud session could do

- The owner's review page "SSO Articulation Map" (a claude.ai artifact, see `CLAUDE.md`) was
  read with claude.ai's ArtifactData tool. Claude Code on a local machine can't read it, so
  ask the owner to paste their marks if they're needed.
- Nothing here can hear Kontakt or SSO. Say whether something was tested with the owner's
  Kontakt, the test synth or sfizz when you report.

## Playback verification (2026-09-28, branch `playback-verify`)

`MuseScore --verify-playback` and `Verify SSO playback in background.bat` (see `VERIFY.md`, `CLAUDE.md` ›
Playback verification): renders scores through Kontakt offline as an export does, again with the
built-in synth, and reports missing chords and notes, notes cut short, silences, clipping and drift,
each with what happens on its slot at that moment. Tried here only: with the MS Test Synth and injected
faults (all detectable faults found, nothing else flagged) and on the owner's piano exports through
`--verify-audio`. Next:
- the owner: a Windows build of this branch (the branch doesn't trigger the build workflow: *Run
  workflow* on it), then double-click the `.bat` once (the test scores) and once with the piano score
  dropped on it, and hand back both zips. Check with `read_verify_report.py`: the run time, whether the
  clean test scores pass with Kontakt (thresholds were set on one piano score: strings and winds
  through SSO are untried), and the piano score's missing notes in bars 59-62 with their "notes in the
  2 s before" (a voice limit: then Kontakt's voice settings, or fewer release samples);
- look at what the report says about repeated notes of the same key (an earlier note still on when
  the next starts): with the test synth that cuts the new note; with Kontakt unknown;
- the self-hosted runner workflow is there but optional; the owner is wary of it (2026-09-28).

