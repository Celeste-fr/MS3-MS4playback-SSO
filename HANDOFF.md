# Handoff: start here

Current state of the work and what is open. Read `CLAUDE.md` first (rules, architecture map, build and test).
The dated work logs that used to fill this file (2026-09-25 … 2026-10-01: tuning, extracts, setups, dynamics,
Mixer, load times, Live integration, piano fixes, playback verification) are in `docs/HISTORY.md` Part 2; read
a section there when you need the background of that topic. Commit messages describe each step in detail.

## Where things are (2026-10-01)

- Tip of the sound-library work: `live-set-export` (has `claude/intelligent-volta-gx7gmw` merged), then
  `automation-editor` on top. Measurements and the Windows-VM tools: `claude/intelligent-cray-6pd4o1` (its own
  HANDOFF.md, "Measurements for the legato-timing fixes", has the VM jobs, sweep tools and results). Playability:
  `playability-checker`. None of these is in `main` yet; the owner decides the merge.
- Windows test builds come from a branch on request (CLAUDE.md › Branches and CI). The Windows VM with Kontakt 8
  and SSO is reachable for agents (VERIFY.md › With the real library; access in the agents' own notes).

## Legato and onset timing (2026-10-01, branch `claude/intelligent-volta-gx7gmw`)

On top of `legato-timing` (3f0cda5): 696341d takes the measurement branch's data (f8908f8 on
`claude/intelligent-cray-6pd4o1`; its HANDOFF.md, "Measurements for the legato-timing fixes", has the VM jobs, the
sweep tools and the sweep results of ed3a294 and 3f0cda5). Not heard or swept with SSO yet: the next step is the VM
sweep (`make_sweep_scores.py`, task `claude-sweep`) on a Windows build of this branch against 3f0cda5's.
Open, deliberately not changed (no data here supports a change):
- **Octave slurs of the other 35 Performance patches** (winds, brass, Solo Cello and the Sul G / Sul C Performance patches) still use
  their ±7 delay. Only the 8 string Performance patches have the 5-pitch grid (`sso_legato_grid_pitches.json`);
  `sso_legato_grid.json`'s ±12 are octave errors. Needs the rest check's `legatopitches` part on those 35 (VM, build
  c7338f4; the measurement HANDOFF's "Resume"). The single-pitch grid can't be corrected by a rule: on the 8 string
  patches its arrival is above the 5-pitch 50 % time by a median 95 ms (Basses), 98 (Violins 2), 58 (Violas), 25-30
  (Celli, Violins 1), -25…+5 (the three solo strings), per interval -150…+190: no common offset to carry to the winds.
- **Long slurred notes early** (sweep of 3f0cda5: ≥ 0.5 s notes to -156 ms in Basses, Horn, Violas, Violins 2 -51…-76):
  Basses, Violas and Violins 2 now use the 5-pitch grid, which is exactly where the single-pitch grid read 58-98 ms too
  long (above); whether that closes it needs the sweep. Horn Solo - Performance is still on the single-pitch grid
  (no 5-pitch data): leave it until `legatopitches` measures it.
- **Strings' slurred sixteenths 59-84 ms late**: the price of the `legatoTransition` cap (none up to 125 ms, half from
  250; docs/HISTORY.md › Fast runs), chosen on the VM for even levels in fast runs (a quarter's shift: 4.2 dB spread). Not
  changed: undoing it needs a measurement that shows even levels with a larger shift.

## Waiting for the owner (built, not yet confirmed on Windows / by ear)

Each has its background in docs/HISTORY.md Part 2 under the same name.
- Even dynamic steps: the owner compares the four modes (Advanced Options › *Even dynamic steps*) and picks one.
- Attack salience in the Recommended balance (`attack-prominence`): needs a background dynamics run with a build
  that has it (fills the "attack" curves), then the owner judges the families' predictions by ear.
- The Mixer on library parts (`mixer-sso`) and Controllers / Mixer live while playing (`live-controls`): the
  owner's Windows check.
- SSO load times (`sso-load-times`): the owner's `--measure-load-times` run; worker-thread loading stays off until
  it shows Kontakt takes it.
- SSO's controls: which controls say "not in <patch>" on the owner's patches.
- Playback verification with Kontakt: the owner's runs on the test scores and the piano score (VERIFY.md).
- Live: everything LIVE.md lists under "what only Live can show" and "Open questions for the owner".
- Tuning: the playback regression (`ab/trace/regress.sh`, not in the repository) not run since edb5eec;
  *Tools › Tuning…* left untested by the owner's choice.

## When a check zip arrives (*Check articulations* hand-back)

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
5. Record what the run showed in the commit message (long narratives: a dated section in `docs/HISTORY.md`).
6. Build and run `tst_soundlibrary` locally if you can (`CLAUDE.md` › Building and testing). The
   `spitfireMap` test checks instrument matching.
7. Push. If code changed (not just the map) and the owner needs a new Windows build, put
   `[windows-build]` in the last commit message. The workflow then builds, tests and uploads
   in about 15 minutes. Follow `CLAUDE.md` › Windows build etiquette.

   A map-only change also needs a new build to reach the owner, because the XML is installed
   with MuseScore. Alternatively the owner can copy the regenerated XML over
   `share/soundlibraries/` in their installed folder.

## Things an agent here can't do

- Hear Kontakt, SSO or Windows (see CLAUDE.md › Rules: say what was tested with what).
- Read the owner's review page "SSO Articulation Map" without claude.ai's ArtifactData tool: ask the owner to
  paste their marks if they're needed.
