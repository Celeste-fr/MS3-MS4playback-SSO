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
- the owner: hit lists for the unpitched drum kits (see CLAUDE.md › Eighth run: click each drum
  icon in Kickstart and screenshot its hits on the keyboard);
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

## Setups made from the .nki files (2026-09-27)

`tools/soundlibraries/make_setups.py` (see `CLAUDE.md` › Setups made from the `.nki` files). Next: the owner
runs `MakeSetups.exe` (artifact `ExtractLibraryFiles-win64`) and makes one setup of a patch not in the map
(e.g. "Violins 1 - Core techniques"), then in MuseScore (run 72) *Check articulations* shows it "(not in the
map)", Ready; *Set up…* opens it in Kontakt: the patch loaded, its samples found, "UACC & UI only". If so:
`all`, then (a MuseScore build for this) tick all and a describe-only Extract plug-in data of every patch.
If Kontakt refuses it or its samples are missing: look at the sample list first (absolute version 2 paths
in a multi, where Kontakt 8 itself writes version 3).

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
