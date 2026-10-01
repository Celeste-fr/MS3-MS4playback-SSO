# Handoff: state of the sound library work (updated 2026-10-01)

Read `CLAUDE.md` first: it describes the architecture, the build and every run the owner has made. The commit
messages on each branch explain each step in detail. The sections below the first three are older notes, kept for
their history; where they disagree with "Start here", "Start here" is right.

## Start here (2026-10-01: the cloud session ends, the work moves to the owner's Debian VM)

- **Branch**: `claude/intelligent-cray-6pd4o1`, last commit e82a202, everything pushed, `main` merged in on
  2026-09-30 (341aa78). Work on your own branch; the owner decides what goes to `main` (CLAUDE.md › Branches).
  To continue this session's conversation itself: `claude --teleport session_01DpUCQZEy2ysGW5HaKShRvm` in a clean
  clone (same claude.ai account).
- **Latest Windows build**: run 267, https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/36783186961
  (artifact `MuseScore-soundlibrary-win64-eddd1bc`). Windows builds come from `[windows-build]` in a pushed commit
  message on the branch (`.github/workflows/test_soundlibrary_windows.yml`). Name builds by run number and link.
- **What runs where**: Kontakt and SSO run only on the owner's Windows PC. The VM (like the cloud container before)
  can build MuseScore, run the tests with the test synth (`mtest/libmscore/soundlibrary/testsynth`) and read the
  zips the owner sends back; it can't play SSO. Build steps: CLAUDE.md › Building (Linux container); headless runs
  with the test synth: CLAUDE.md › Load times (isolated `HOME`, `~/.vst3/mstestsynth.vst3` linked, …).
- **The owner's direction (2026-09-30)**: "your job is only to collect data". The extraction is done (below); the
  playback problems it found are listed for whoever builds playback next, not fixed.
- **Background runs on Windows** (bat files next to the exe, each with its own log in Documents/MuseScore Sound
  Library Check, supervised: a crash or hang is retried, then that patch left out): `Measure what's left of SSO in
  background.bat` (dynamics, then timing, of every sound; skips what earlier runs did, as long as their folders are
  still in Documents/MuseScore Sound Library Check). A run that looks stuck usually isn't: Performance patches take
  12-16 s each. Hand-backs are zips; read them with `tools/soundlibraries/dynamics_from_check.py` and
  `timing_from_check.py` (derived numbers only go into the repository: it is public).
- **Gotcha**: a `.bat.in` line inside `if ( … )` must not contain parentheses (an `echo` with "(…)" closed the block
  and the window vanished at once, run 266).
- **Not measured** (the owner asked "is 100% measured?", 2026-10-01; none of it started, the owner to choose):
  1. each sound at one test pitch only (dynamics and timing across the range not known);
  2. each sound at the patch's default controls only (mic mix, vibrato, release … : how they change loudness and
     attack not measured, only that they change the sound);
  3. parameters tried at 0 and 1 only;
  4. legato at 3 velocities and 2 intervals (+2, -5) only, on the 42 Performance patches; Horn Solo / Horns a2 -
     Legato and Oboe Principal - Total Performance not as a slur at all (no map articulation named legato);
  5. round robins (one note per sound; earlier checks ±1-2 dB);
  6. percussion techniques that are off at a patch's defaults (no key; needs the switch-on system the owner plans);
  7. Vibraphone and Curated Tutti - Low Wood String Stab: measured, but no controller matched a control on screen.

## For the next agent: playback problems the extraction found (2026-09-30; not fixed, the owner: "your job is only to collect data")

All numbers are in `tools/soundlibraries/` (see CLAUDE.md for how each was measured). Ask the owner before building any.
1. **Slurred notes sound late.** On the 42 Performance patches the second note of a slur reaches its pitch 70-430 ms
   after its note-on (median 185; `sso_articulation_timing.json`, `legato`), in one step, with a 4-22 dB dip. Starting
   slurred notes earlier by the patch's own delay would put them in time.
2. **Velocity barely changes SSO's legato speed** (20 / 64 / 110 give the same transition on most patches). The renderer
   keeps MS4's velocity for legato "because Spitfire's legato speed is on velocity" (CLAUDE.md, Shorts): that reason
   doesn't hold for SSO; velocity can be used for something else or left.
3. **Slow attacks.** Held notes reach full level 175 ms after the note-on (median), Violas ~300 ms, Flautando / Sul Tasto
   / Harmonics up to 1 s; brass and woodwinds 60-120 ms (`fullMs`). Same remedy as 1, per articulation.
4. **Shorts ring on.** A short's body (to 20 dB under its peak) is 265-2670 ms, median ~1 s, the hall in the recording;
   a 0.1 s note sounds 370-1000 ms (`bodyMs`, `shortNoteBodyMs`). Matters for choosing Short 0.5 / 1.0 by length
   (`<Articulation length>` uses Spitfire's nominal 0.5 / 1.0 s).
5. **Releases** ring 0.6-2.9 s after the note-off (`releaseMs`, median 855 ms); a lane (tuning copy) is reused after
   `tail` 1.5 s, shorter than some releases (Flautando 2.9 s, Sul Tasto 2 s, Super Sul Tasto 2.4 s).
6. **Microtones by pitch bend**: the Performance patches and Horn Solo / Horns a2 - Legato bend ±100 cents linearly,
   Kickstart percussion and some solo strings ±195 (`sso_patch_measurements.json`, `pitchBend`). The owner's first choice
   (2026-09-27) was pitch bend; varispeed was built because the All techniques patches don't bend.
7. **Controller links** (`sso_patch_measurements.json`, `links`): CC 16 Mute, 17 Release, 18 Variation (Tightness on 2),
   21 (and 104 on Performance) Vibrato, 22-25 Mic 1-4 level, 40-46 Harp Pedal 1-7, besides CC 1 / 11. The map drives
   these as Kontakt parameters by title; CCs would also work.
8. **Patches that play nothing where the map might send notes**: Long Sul G / Sul C in Violins 1 / 2 and Celli All
   techniques and Core (`noSamples` in `sso_nki_articulation_details.json`); Rain Sheet Swell mp; Low Ensemble's Toms 3-5
   and Field Drum Rim / X Stick share keys (the owner: a real conflict).
9. **Kit techniques that are off** at the kits' defaults (rolls, swells …) are only in the one-drum patches; the owner's
   plan: switch one on in the kit when a score needs it (CLAUDE.md, Kits).

## How complete the extraction is (2026-09-30)

- **From the files (all 700 `.nki`)**: every patch's groups, zones, key and velocity ranges, articulation names, round
  robins, dynamic / velocity layers, release groups, loops, lengths; the 279 archives' 432,829 sample names. Done.
- **Switching**: every map value checked by picture and ear; the 12 Core / Decorative and 4 Curated patches scanned;
  Harp glissandi keyswitches; every percussion hit list (82 lists, 519 hits). Done.
- **Named controls** of all 700 patches; **controllers and parameters** measured on all 700 (672 put back within their
  noise, 28 measured but not put back: round-robin percussion, Fanfares, Flutter); **links** on 698 (Vibraphone and Curated
  Tutti - Low Wood String Stab have none); **pitch bend** on all 700. Done, apart from those 2 links.
- **Dynamics curves**: every sound of all 700 patches (1804: articulations, 504 drum hits, one-sound patches;
  `sso_sound_dynamics.json`, build 261's run, 2026-09-30). Done.
- **Timing and legato**: every sound of all 700 patches (`sso_articulation_timing.json`, run 267's build, 2026-09-30).
  Done, but for the slur transitions of Horn Solo - Legato, Horns a2 - Legato and Oboe Principal - Total Performance
  (timed as one sound: the run measures a slur only on an articulation whose first technique is legato).
- **Parameters** measured at 0 and 1 only, not the curve between.

## Where things were (2026-09-25; superseded by "Start here")

- Branch then: `main`; latest Windows build then: run 11 (6bc3a62).

## Tuning and Ethanol bar 14 (2026-09-26, edb5eec)

Built-in tuning is in (see `CLAUDE.md` › Tuning) along with the all-sound-off after a faded MS3
hairpin (Ethanol bar 14). Still open:
- the playback regression (`ab/trace/regress.sh`) has not been run on edb5eec. The `ab/` harness
  is not in the repository, so a new cloud container doesn't have it;
- *Tools › Tuning…*: the owner won't test it for now (2026-09-26: "leave it for now"); Ethanol
  bar 14 confirmed good;
- the owner: try the Playback drop-down (Mixer, Play Panel) switching MS3 / MS4 / SSO;
- the owner: pitches for the 49 accidentals MuseScore gives none;
- the kits' hit lists: done (2026-09-26, the owner's screenshots); the 42 one-drum patches: scanned in the
  background (2026-09-28 01:46, which keys sound: `sso_drum_keys_sounding.json`); they don't follow the `.nki` group
  order, so keys are named from the owner's hit-list screenshots. Snare 1 / 2 and Triangle 1 / 2 are in the map (the
  sounds the kit patches lack at their defaults); the other 38 and the 6 ensembles: read from the owner's picture run of 2026-09-28 05:02 and confirmed, in `tools/soundlibraries/sso_percussion_hits.json` (with notes on conflicts and techniques that are off); the 42 one-drum lists and the 6 ensembles' are in the map (reference), with every technique that is off as a key-less entry; next, when a score needs a technique a kit patch has off (the rolls of bongos, congas, bass drum, field drum, cymbals, tam-tam, thunder sheet, tambourine, sleigh bells, castanets; Rain Sheet Swell mp …): a system that switches it on in the kit patch and gives it a key (the owner, 2026-09-28: to be built later); Harp glissandi's modes: in the map (reviewed); the three-mic patches' mics: done (Close, Tree, Ambient);
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
- Every controller and pitch bend on all 700 patches: `Measure SSO controllers in background.bat` (offline, no window; the owner to run it and hand back the extract zips; read with `read_plugin_data.py`). The first run (build b903d9a) crashed in Kontakt 16 s into Violins 1: it went offline while the patch still loaded; now only after the patch sounds in real time. The second (build fe5d050) crashed on Violas and put CC 7 back to 0 (silence, so later controllers read nothing); now each controller's own value is searched and a supervisor process restarts the run after a crash or hang, leaving that patch out (see CLAUDE.md). **Done (2026-09-28 18:48): 695 of 700 measured**, kept as `tools/soundlibraries/sso_patch_measurements.json`; the 5 then missing (Cimbassi a2 - Long, Curated Woodwind Ensembles, Bass Trombone Solo - Fall, Field Drum, Cimbassi a2 - Long Alt) came in later runs: all 700 are in `sso_patch_measurements.json` (672 complete, 28 not put back). Done (2026-09-29): 698 of 700 patches' controller links in `sso_patch_measurements.json` (see CLAUDE.md › The links run finished); the 4 with cc 23 left out now take it from a later run (672 complete, 28 not put back); per-articulation key ranges, round robins, dynamic / velocity layers, releases and lengths from the `.nki` files in `sso_nki_articulation_details.json` (CLAUDE.md › Articulations from the files). Timing per articulation and legato transitions: built (2026-09-29), `Measure SSO timing in background.bat` (CLAUDE.md › Timing, in the background); done (build 254, 2026-09-30: 96 patches in 17 minutes; the first run on build 249 had hung, now supervised and resumable); kept as `sso_articulation_timing.json`. Not yet used by playback: slurred notes on the Performance patches sound their pitch 70-430 ms after the note-on (median 185; velocity barely changes it), so starting them earlier per patch would put them in time; attacks of 100-1000 ms likewise (ask the owner first). Optional, not built: parameters between 0 and 1, loudness curves beyond the ~160 patches. Before that (run 226's links were wrong, box-based): the owner ran `Link SSO controllers in background.bat` again on the build with window cells (which named control each controller moves on 2 patches of each of 64 groups, 113, everything on the 11 left incomplete; see CLAUDE.md › Links run, › Two patches a group) and hands back the extract folders and the log; then `measurements_from_extract.py` on them and the earlier ones. Open: the Performance patches bend ±100 cents linearly, so their microtones could use pitch bend instead of varispeed (the owner's first choice); ask before building it.

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

