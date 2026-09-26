# Handoff: state of the sound library work (2026-09-25)

The work so far was done in a claude.ai cloud session, which has ended. This file tells the next
agent (Claude Code on the owner's local machine) where things stand. Read `CLAUDE.md` first: it
describes the architecture, the build, the articulation check and every run the owner has
made. The commit messages on this branch explain each step in detail.

## Where things are

- Branch: `ms4-playback`, the default and only branch (the sound-library branch was merged into
  it and deleted on 2026-09-25). Develop and push there; no pull requests.
- Latest Windows build: run 11 (commit 6bc3a62). All 10 `tst_soundlibrary` tests passed on
  Windows. Artifact `MuseScore-soundlibrary-win64`:
  https://github.com/Celeste-fr/musescore3-ms4-playback/actions/runs/36178930191
- That build has:
  - the check memory (`checks.json`; *Tick what needs checking*);
  - the scan of every value 0–127;
  - *Add a patch…* for patches missing from the map;
  - the map fixes from the owner's full 42-patch run: Legato 20 is removed from the woodwinds
    and brass, and trills 70/71 from Trumpets a6.

## What the owner is doing now

The fifth run (all 42 patches, scanned) is in: see `CLAUDE.md` › Tried by the owner. Steps 1, 2
and 4 of the last request are done (Oboe Solo and Solo Cello pass; the scanned articulations
are mapped where notation can ask for them). Still open:

- The instruments the map lacked are added (see `CLAUDE.md`). Motif Brass's UACC numbers
  come from the standard, not from Kontakt: the owner sets up those three patches (and the
  percussion ones they use) and runs *Check*; fix any differences through the generator.
- Violins Long Sul G / Celli Long Sul C: settled, Spitfire's "All techniques" patch plays no
  samples for them (Kontakt's Voices stays 0).

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
   in about 15 minutes. The owner allows this without asking, but don't overuse it:
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
