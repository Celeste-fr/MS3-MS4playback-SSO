# Handoff: start here

Current state of the work and what is open. Read `CLAUDE.md` first (rules, architecture map, build and test).
The dated work logs that used to fill this file (2026-09-25 … 2026-10-01: tuning, extracts, setups, dynamics,
Mixer, load times, Live integration, piano fixes, playback verification) are in `docs/HISTORY.md` Part 2; read
a section there when you need the background of that topic. Commit messages describe each step in detail.

## Where things are (2026-10-02)

- `main` has everything: `live-set-export` / `claude/intelligent-volta-gx7gmw`, `automation-editor`, the
  measurement branch `claude/intelligent-cray-6pd4o1` and `playability-checker` were merged on 2026-10-02 (the
  branches are kept). Work on a branch; finished work by Claude and its subagents is merged into `main` without asking (the owner, 2026-10-02), anything incomplete or changing an owner-decided value waits for the owner.
- Windows test builds come from a branch on request (CLAUDE.md › Branches and CI). The Windows VM with Kontakt 8
  and SSO is reachable for agents (VERIFY.md › With the real library; access in the agents' own notes). VM
  measurement jobs (task `claude-measure`, `C:\claude\measure\job*.cmd`) and regression sweeps (task
  `claude-sweep`; `tools/playbackverify/make_sweep_scores.py`, `fix_sweep_ids.py`, `analyze_sweep.py`,
  `compare_sweeps.py`): docs/HISTORY.md Part 3 › Measurements for the legato-timing fixes. Keep VM outputs under
  ~2 GB; delete renders once analysed.

## Measurements: open items (from the measurement branch; background in docs/HISTORY.md Part 3)

- Not measured: parameters only in 5 steps (0-1) and at the test pitch only; the rest check's legato grid from the
  test pitch only; links for Vibraphone and Curated Tutti - Low Wood String Stab (none found).
- Octaves: the template times (`tLeaveMs` / `tMidMs` / `tArriveMs` in `sso_legato_grid_pitches.json`, f0bef96) are
  the reliable ±12; the harmonic times are not (reverb, the lower note's harmonics).
- Velocity barely changes SSO's legato speed (20 / 64 / 110 alike on most patches): the renderer's reason for
  keeping MS4's legato velocity doesn't hold for SSO; unused so far.
- Sounds that play nothing where the map might send notes: Long Sul G / Sul C in Violins 1 / 2 and Celli All
  techniques and Core (`noSamples`), Rain Sheet Swell mp; Low Ensemble's Toms 3-5 and Field Drum Rim / X Stick share
  keys (the owner: a real conflict; set them in Kontakt when a part needs them).
- Kit techniques off at the defaults: the owner's plan is to switch one on in the kit patch (and give it a key)
  when a score needs it; the "(all on)" patches and `unpurgeSwitchedOn` are the measurement-only start of that.
- Controller links (`sso_patch_measurements.json` `links`: CC 16 Mute, 17 Release, 18 Variation, 21 / 104 Vibrato,
  22-25 mics, 40-46 Harp pedals): the map drives these as parameters by title; CCs would also work.
- Tam Tam FX Scrape and Wind Gong FX Bow (slow swells) missed the rest check's 10 dB "sounds" test though they sound.

## Legato and onset timing (state 2026-10-02, after the VM sweeps)

Measured on SSO (sweeps of builds e6f44e6, c27da62, a289780, 47b872f; tools in tools/playbackverify, history in
docs/HISTORY.md Part 3). Settings and their sources: docs/PLAYBACK_SETTINGS.md (owner's page:
https://claude.ai/artifact/XYLfhVe44uPJemn4uexhAM).
- Done: the 5-pitch legato grid covers all 43 Performance patches; octaves (±12) come from the template fit (the
  harmonic method was wrong by 75-320 ms); per-family medians now: strings +5 / −10 ms (+12 / −12), woodwinds +5 / 0,
  brass −45 / +20; other intervals 0…+10. Sul tasto / flautando / harmonics: per-semitone onset at −12 dB clamped to the
  neighbours, median +58 ms, none early. Oboe +60 and Violins 2 +25 ms corrections.
- Octaves by start pitch (branch `octave-per-pitch`, owner's option C, 2026-10-02): the 16 patches measured at
  ~16-30 starts (branch octave-measure, `tools/soundlibraries/octave_measure/`) time each ±12 slur by its start
  pitch's own value (map `octaveUp` / `octaveDown`; rule in docs/PLAYBACK_SETTINGS.md); the others keep one ±12 value.
  Not used: Violins 2 - Sul G (4 starts, inconclusive), Oboe Principal (plays no notated legato).
  Sweep (make_octave_sweep_scores.py, every start, 15 patches) 49b00a0 → 5698181: median |offset| 40 → 20 ms,
  within 50 ms 63 → 76 %, over 100 ms 129 → 60 of 690 slurs (Horn +12 170 → 10, Basses +12 155 → 20, Horns a2 +12
  120 → 15, Tuba +12 70 → 15, Piccolo −12 80 → 5); controls unchanged. Oboe Solo +12 was −60: its +60
  sweep correction no longer applies to its +12 octave values (owner 2026-10-02; −12 keeps it; re-sweep to confirm). Violins 2 ±12 were −30 (both directions): `OCTAVE_SWEEP_CORRECTION` −30 / −30 (branch legato-level-balance; re-sweep to confirm).
  Violins 1 flautando low register still +256 ms.
- **Fast slurs on time (the owner, 2026-10-02: "I want fast slurs to not sound late"; replaces the earlier acceptance of
  slurred sixteenths arriving late, and the fast-note ramp; branch `fast-slurs-on-time`).** SSO's Performance patches
  sound a slurred sixteenth's pitch 100-170 ms after its note-on (strings; woodwinds / brass 60-130) at 100-200 bpm,
  whatever the note's own length; the ramp gave a 136 ms note 6 ms, so they were heard 90-125 ms late (median), and a
  slur's first note inside a run (its own attack right after the note before) 100-160 ms late. Now: the note before
  keeps `keepMs` (40) of its length as played (not a share of the written one), so every note of a run starts early
  by about the same and keeps its length; after a short note a transition takes 65 % of its measured delay rising to
  all of it at 800 ms (refit 2026-10-03, numbers-measured: 50 % / 380 ms, docs/PLAYBACK_SETTINGS.md › Measured by sweeps) (`fastShare`, `fastFullMs`: fitted to 2666 measured transitions, each patch's median within 11
  ms); a slur's first note in a fast run starts as early as a transition would (`fastFirsts`); a transition's note
  before ends `overlapTicks` after the new start as played. Measured (Windows VM, kthost re-timing the renderer's events
  offline through Kontakt; 13 Performance patches; `tools/playbackverify/make_fastrun_scores.py`,
  `analyze_fastruns.py`): transitions' median arrival +4 / +11 / +19 ms (strings / woodwinds / brass; before +125 /
  +89 / +109), slur firsts +34 / +32 / +16 (before +160 / +104 / +114), level spread over slur positions 1.5 / 1.8 /
  1.9 dB (before 1.3 / 1.9 / 2.0); Whence cellos bars 1-4 +3 ms, firsts +28, 0.7 dB (before +138, +163, 1.2). Per note
  the spread stays wide (10-90 %: -60 … +100 ms; SSO's transition time differs by interval). The fast technique
  (`fastTechnique=1`: own attacks on the same patch, as early) measured about the same (strings +10, Whence cellos +28,
  2.5 dB); with only the onset as lead it was 40-85 ms late. Owner to judge by ear: default vs `fastTechnique=1`.
  Confirmed with the real build (Windows run 37027355685, 2bc46bc; MuseScore --verify-playback of Whence on the VM,
  heard pitch arrival of slurred notes, median / 10-90 %): violas bars 9-12 +93 (+2…+253, 2 swallowed) before → +8
  (-42…+76, none) now; violins 1 bars 3-7 +108 → -32; cellos +143 → -17; violas bars 3-7 +83 → -32 (now slightly
  early; `fastShare=57` brought the cellos to +3 but not the violins: kept 65). With `fastTechnique=1` the violas
  bars 9-12 +12 but 2 swallowed, cellos +33 with a wider spread. Levels by slur position, violas bars 3-6 (heard
  windows): bars 4 / 6 within 0.8 / 1.3 dB; bars 3 / 5 still 3.9 / 4.4 dB (the third note, Eb3 reached again from D3,
  is 3-4 dB down in every variant, own attacks too: SSO's sample, not the timing). Open: the legato level balance
  below didn't fix it.
- **Legato level balance (branch `legato-level-balance`, 2026-10-02; `[legato] levelBalance`, off).** Measured on the
  Windows VM with kthost (offline Kontakt 8 + SSO, the Performance patches' own setups): CC11 is a plain volume on every
  Performance patch, 20 log10(v/127) (101 / 80 / 64: -2.0 / -4.0 / -6.0 dB, spectrum unchanged within 0.1 dB; Tuba
  Solo - Performance ignores it), heard 10-240 ms after a step (the hall). Every start x interval +-1 2 3 5 7 12 of the 43
  Performance patches at mf (`tools/soundlibraries/legato_level_scan.py`, `legato_levels_from_scan.py` →
  `sso_legato_levels.json` → map `legatoLevel` / `legatoLevelLong`): transitions repeat exactly; against the median of
  the other transitions into the same pitch they differ by a median 0.1-1.8 dB per patch (over 3 dB: 0-31 %, Cor
  Anglais most), the same at p and f (correlation 0.7-0.99 on 4 patches); attacks against them -3.8 … +5.3 dB in a
  run, -2.4 … +5.8 settled (most woodwinds and solo strings: their legato notes settle 3-6 dB under their attacks).
  But in a run the notes around a transition move its level 1.4-4.9 dB (Violas, the same transition after other
  notes), and the isolated tables predict a run's per-note levels poorly (correlation 0.1-0.3 for sixteenths and
  eighths, 0.1-0.7 for quarters). Renderer runs (`make_levelrun_scores.py`, `analyze_levelruns.py`; 16 parts,
  sixteenths / eighths / quarters / halves; tst_liveequivalence dumpEvents through kthost): unevenness of the slurred
  notes (sd around their pitch neighbours) off 1.77 / 1.96 / 2.19 dB, on with 3 dB headroom 1.91 / 1.62 / 1.92, on
  without headroom 1.74 / 1.71 / 2.00; Whence violas bars 3-6 position spreads off 3.8 / 1.3 / 4.7 / 0.5 dB, on (3 dB
  headroom) 3.1 / 3.8 / 3.3 / 3.9, settled tables only 3.5 / 1.4 / 3.8 / 1.5, no headroom 4.0 / 1.2 / 4.4 / 0.7. So
  it stays off; the tables and the CC11 path (`libraryNoteLevels`, shared with the marcato level) are there for a
  model that knows the run's context. A raise needs headroom (`levelHeadroomDb`: the part rests that much down).

## The owner's decisions of 2026-10-04 (from the pause list of numbers-measured)

1. Phrase gap: done, `[legato] phraseGapMs` 60 (c4c8860).
2. Pedal 40 / 90 ms: kept until the owner's PC measures them: an export of `Piano pedal chords` with `[pedal] upAfterMs`
   0 … 60 ms (docs/PLAYBACK_SETTINGS.md › Measured by sweeps); not urgent.
3. Thresholds: 10 dB short length and 50 % legato arrival approved as the owner's rules (ISO 3382-1's early decay time
   as an analogy for the first); T30 for the tuning tail approved (docs/PLAYBACK_SETTINGS.md › 3C).
4. Velocity lane: 0-200 % and "shape" default confirmed (LIVE.md).
5. Tuning copies: the free memory split between the parts that need copies only (8a53711).
6. Checker fast basses: candidate A, 94 ms / 1.25 s (19fedbe). Re-read against the books on 2026-10-04 (review page
   https://claude.ai/artifact/KzdKk7UrH1HyJ2aFoYFR9h): the books don't support it. **Shelved by the owner for a
   later review**; see Waiting for the owner › Checker fast basses.
7. `MICRO_MIN_CENTS`: to depend on frequency from Wier, Jesteadt & Green 1977; waits for the paper (5 cents until then).

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
- Clip-tab automation lanes as the clip's envelopes (branch `clip-envelopes`): the owner installs the Control
  Surface script `tools/live/MuseScoreEnvelopes` once (LIVE.md › Automation lanes in a clip tab) and tries a Windows
  build (the new device, protocol 5, from that build's `tools/live`); arrangement clips get no lanes (open question).
- One instance for a line's tunings (`[tuning] oneInstance`, branch `bend-one-instance`, off by default): the owner
  listens to Whence with 2 (aggressive: 19 → 15 instances; a detached note's tail is bent to the next note's tuning)
  and decides; 1 (safe) saves nothing on Whence. Only patches that bend (Performance) share; SSO's All techniques
  patches don't bend, so detached notes keep their copies (12 instances if they did).
- Marcato level (Inspector › Articulation › *Marcato level*, per sign, metaTag `marcatoLevels`; default "Library
  default" = the library's plain marcato): the owner tries it on SSO (velocity-driven Marcato on winds / brass;
  Marcato Attack on strings by CC11 down / CC1 up). MS4's accent velocity boost no longer reaches library marcatos
  (owner, 2026-10-02): a plain note's velocity at the dynamic (mf 80), the level on top.
- Checker fast basses (S12, shelved by the owner 2026-10-04; review page
  https://claude.ai/artifact/KzdKk7UrH1HyJ2aFoYFR9h): candidate A stays in `main` until the owner decides. The 1.25 s
  limit was taken as the longest run the books accept, but Forsyth's Ex 282 (Beethoven 4 finale: 32 sixteenths at
  94 ms, at least 3.0 s) is accepted, the very passage Adler calls "muddied"; note length doesn't separate praised from criticised passages (94 ms both ways, Prout's Ex 51 criticised
  at 129 ms); the books criticise clarity, not reach (Prout: players can play them). Options: a clarity advisory
  (recommended), remove S12, keep it. Change nothing until the owner decides.
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
