# Handoff: start here

Current state of the work and what is open. Read `CLAUDE.md` first (rules, architecture map, build and test).
The dated work logs that used to fill this file (2026-09-25 … 2026-10-01: tuning, extracts, setups, dynamics,
Mixer, load times, Live integration, piano fixes, playback verification) are in `docs/HISTORY.md` Part 2; read
a section there when you need the background of that topic. Commit messages describe each step in detail.

## Before a build goes to the owner (2026-10-09)

Run the playback audit on *Whence 12-TET* with Recommended (VERIFY.md › Playback audit: `tst_playbackaudit auditScore`
with `MS_AUDIT_SCORE`) and read its report: no OVERLAP may remain, and every ARRIVAL fail and CAPPED note left must be
named in the hand-over with its reason (the owner, 2026-10-09: "why do I have to point out all of these issues?").
LEVEL STEP: values, marked against a proposed threshold (the difference limen, 0.69 dB at 60 dB SL, Jesteadt, Wier &
Green 1977) the owner hasn't approved yet.

## No Performance patches (2026-10-06)

The owner: "NO using performance patches. with all technique patches we have control of release, tightness, CC
mapped vel. … and sync to tempo". The SSO map has no Performance extras; slurs play the All techniques longs, each
note its own attack, early by its onset (`Articulation::playsTransitions`). The legato-timing items below are about
Performance patches and wait unless a legato patch comes back. Release, Tightness, *CC mapped vel.* and *Sync to
tempo* stay at the library's defaults (the owner, 2026-10-06): adjusted in Live (the plain set's Kontakt track), not
by MuseScore. Unmeasured: how the All techniques longs sound slurred at speed. A slurred
"espr." note now plays Long (Rachm.) as a held one does (`long legato`, as Long CS plays muted slurs; it played the
Performance legato): offer to the owner, revert in the generator's table if they prefer plain Long.

**Performance legato tried again, 2026-10-10: not worth it** (the owner: "if we can't get the 4 separate slurs to work on
performance patch, then it's maybe just not worth it"). *Whence* 12-TET bars 1-4, Violas / Celli on Violas / Celli -
Performance (Windows VM, real SSO, kthost offline; hand-made events: each four-sixteenth slur's first note early by the
map's Legato onset, the others transitions on their beat, velocity 64, CC1 64, CC11 127). The owner heard the line pulse
on every beat: each slur's first note is a new bow stroke, louder than the transitions after it. Per-16th level SD
(RMS, 60 ms after the written time): Celli 2.6-2.9 dB (three takes) against plain Long's 0.7, first note +1.8..+2.1 dB;
Violas 1.7-2.3 against 1.6. The group-start velocity is no lever (30 / 45 / 64 / 90 / 120: Celli first note +1.0..+2.1,
Violas worse at 90-120; takes alone differ by about ±0.5 dB of SD). One slur per bar measured flat (Celli 1.9, Violas 1.1:
the per-beat pattern gone), which is why the first Performance try (until df273c3250, 2026-09-30, a slur's last note
overlapped the next) didn't pulse. A measured CC11 dip on each slur's first note was tried too
(2026-10-10; allowed: the same kind of measured level matching as calibrated levels and `[slurs] quickLevel`): first notes
lowered by how much louder they measured than the transitions against C (Celli 2.7 dB = CC11 93, Violas 0.9 dB = 115, the
fork's CC11 law, confirmed in the render), from the written time (from the early send it lowers the note before by ~2 dB).
Celli four-note bump 2.9 → ~2.0 dB, but the note-to-note spread stays 2.5 dB (C 0.7): most of it isn't the group pattern.
The owner: "still terrible". Then every note evened by a CC11 value per note, fitted by rendering (each note against
C's, two rounds): per-16th spread Celli 2.6 → 0.8 dB, Violas 2.0 → ~1.7, but CC11 steps of up to ~14 dB between
sixteenths, landing on transitions that still carry the note before: the owner, "sounds horrible, the volume seems to jump
all over the place" (a per-16th RMS spread doesn't see jumps inside a note). Performance stays dropped: the transitions'
own 2-6 dB differences can't be evened by level without audible steps. **How the Performance patches pick samples** (Spitfire Symphonic Strings
manual p. 10, the same recordings as SSO's strings; the SSO manual has no such section): a phrase's first note, velocity
1-9 smooth attack, 10-127 spiccato/staccato attack (3-4 layers, 6 round robins); transitions need overlap; slow playing
1-19 portamento, 20-84 fingered, 85-127 bowed; faster and "runs" (very fast) sets chosen by playing speed (how measured: not
stated), each 1-84 / 85-127. Measured 2026-10-10 (Windows VM): single notes, only velocity 7 of 7..127 has a slow attack;
transitions change type between 79 and 87. So P's slur starts at 64 were spiccato attacks: the pulse. Slur starts at
velocity 7 (smooth): Celli spread 2.6 → 1.5 dB, no four-note pattern; Violas 2.0 → 2.35 (first note low: the smooth
attack is slower, still sent early by the spiccato onset). Waiting for the owner's ear (listening page, version S). Velocity picks the transition (no other control on these patches: `sso_patch_controls.json`), but it
barely matters. The restored kthost (MD5 ea7e692e…, after C:\claude was lost 2026-10-10) renders the Long patches
bit-identically to the old one (050b4970…, -137 dB null); Performance renders differ take to take.

**Plain Long for every section, lined up (the owner, 2026-10-07: "just using plain long for everything"; goal: pairs of sections within Rasch 1979's 30-50 ms)**, branch `legato-pair-delays`. Every slurred note plays the All techniques Long, early by its onset (`<Onset early="100"/>`). Measured on the Windows VM (real SSO, kthost offline, *Whence* violas bars 3-7 moved into each of 12 patches' range, odd and even notes on two instances so no attack is masked): with the map's old onsets the slow run (durations x4) had 40 of 66 section pairs within 50 ms (loudness detector; 58 by the new pitch's harmonics), the fast run 14 of 15. The residual followed pitch (Oboe r 0.94, Tuba 0.90), so `sso_long_onset_fit.json` (`onset_fit_from_split.py`) gives Violins 1, Violas, Flute, Oboe, Clarinet and Tuba a per-semitone Long onset (isolated notes, each the median of those within a semitone, + the section's offset) and moves Basses' (-84) and Tenor Trombone's (-36); `gen_spitfire_sso.py` reads it. Rendered again (batch 7): slow 56 of 66 pairs within 50 ms (loudness, median SD 29 ms), 62 of 66 (harmonics, 28), fast 16 of 16 (34; the fast loudness detector reads too few notes). The replay predicted each to within one pair. Left over 50 ms, not fixable by a per-pitch early start: Basses on loudness (round-robin variants; the detector misses one below pitch 38, and pitch 40 alternates 150 / 320 ms; harmonics puts their pairs at 25-42) and Oboe on harmonics (pitch 76 has two attacks ~60 ms apart, at 79 the detectors differ by ~105 ms). Next only by ear: the owner listens, against `[heldNotes] early 0`. `[heldNotes] early 0` plays notes as written, to compare.

**Performance with every note shifted to a consistent arrival (the owner's "B", 2026-10-07): not reachable by shifting**
(branch `legato-pair-delays`; Windows VM, real SSO, kthost offline, Violas - Performance at mf, *Whence* 12-TET violas
bars 3-7 three times). Heard arrival after note-on in the run, unshifted: median 93 ms, 10-90 % 58..183 (the same note
in the three passes within 10 ms: SSO is repeatable). (1) The pair scan (`legato_pair_scan.py`, every start x interval
±1..12 in three contexts, `sso_legato_pairs.json`) doesn't predict the run: shifting each note by its pair's value
leaves the 10-90 % width at 115-130 ms (was 125). (2) Closed loop on the passage itself (shift by each note's own heard
lateness, render, measure again) diverges: width 125 → 165 → 328 ms; even notes whose predecessor kept ≥ 60 ms got no
tighter (130 → 144). Moving a note-on changes SSO's transition about as much as the shift. Left: a uniform shift (the
old `legatoDelay` by interval, `Legato early`), which centres the run but keeps its spread.

**Performance as an opt-in, its spread in the players' range (the owner, 2026-10-07: "let's use the performance patch,
but tune the note spread so that they are within the established range for professional players")**, branch
`legato-pair-delays`, waiting for the owner (changes the 2026-10-06 decision where a score asks for it). Staff text
"performance" (until "ord." / "non performance") plays slurred and held notes on the part's Performance patch (modifier
`performance`, 42 Legato articulations); Violas - Performance plays its transitions at velocity 100, the others at the note's own (map `legatoVelocity`; the playback.ini
override `[legato] velocity` was removed 2026-10-07: to hear the note's own, a map without it). Target: Rasch 1979, between-player asynchrony SD 30-50 ms typical (string
quartets 24-28 ms at fast tempi). Measured (same passage and VM): arrival - written SD 49 ms at velocity 64, **41 ms at
100** (on the notes heard in every render 45-46 → 37-38); 159 of 180 transitions detected against 133; median arrival
68 ms against 88; median note peak 1.5 dB lower. Per-note nudges toward the run's median (gain 0.5, ±60 ms) made it no
tighter (49 → 46 at 64; 41 → 51 at 100). Inside 30-50, not down to 24-28. To hear: velocity 100 is Spitfire's "fast
slurred with accent" (slow transitions 85-127: bowed), so offer both by ear (a map without `legatoVelocity` vs the default). Same passage on every family (moved by octaves near each patch's test note), SD at 64 / at 100 (ms): Violas 54 / 39, Violins 1 53 / 65, Celli 53 / 70, Basses 39 / 65, Flute 35 / 65, Trumpet 26 / 35; Oboe 29, Clarinet 31, Bassoon 98, Horn 29, Tenor Trombone 31, Tuba 55 render the same at both. So only Violas - Performance gets 100; the others keep the note's own. A sweep at 30 / 50 / 84 / 120 (Violins 1 53 / 50 / 53 / 58, Celli 44 / 35 / 47 / 64) looked like 50 for both, but rendering again gave Celli 49 at 50 and 42 at 64, Violins 1 55 / 53: renders differ by about ±10 ms of SD, so 50 was taken back. Bassoon (98) and Tuba (55) don't react to velocity; a per-note nudge helps Bassoon (98 → 62) and hurts Tuba (55 → 87). Lateness grows in slower music (every duration x4: Violas median 98 → 189, Horn 98 → 249 ms), so one early start per patch fits one tempo (map `trackDelay` was tried and reverted 2026-10-07: commit "Map track delays", c82c63d5c3, has every family's numbers, and the All techniques Long patches'). Transitions play on their beat (`Legato early` removed 2026-10-07; notes as written: the run's median arrives 68 ms late; an early start at
velocity 100 is unmeasured, and any start shift changes SSO's transition, above).

## Where things are (2026-10-02)

- **Standard test score** (2026-10-06): the owner's *Whence* in 12-TET, `~/MuseScore/ms3fork/test-scores/Whence
  12-TET.mscz` on the dev VM (not in the repo; CLAUDE.md › Building and testing).
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
  slurred sixteenths arriving late, and the fast-note ramp; branch `fast-slurs-on-time`). History: the early
  transitions, `fastFirsts`, `overlapTicks` and `fastTechnique` below were removed 2026-10-07 (the owner: "remove all
  settings not currently being used"); `fastShare` / `fastFullMs` now time only a bent transition's glide.** SSO's Performance patches
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
- **Legato level balance (branch `legato-level-balance`, 2026-10-02; `[legato] levelBalance`, off; removed 2026-10-07
  with map `legatoLevel` / `legatoLevelLong`, the measurement kept in `sso_legato_levels.json`).** Measured on the
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

1. Phrase gap: done, `[legato] phraseGapMs` 60 (c4c8860); off 2026-10-06, removed 2026-10-07.
2. Pedal 40 / 90 ms: off 2026-10-06, `[pedal]` removed 2026-10-07 (a change one tick after its chord).
3. Thresholds: 10 dB short length and 50 % legato arrival approved as the owner's rules (ISO 3382-1's early decay time
   as an analogy for the first); T30 for the tuning tail approved (docs/PLAYBACK_SETTINGS.md › 3C). The standard's text is not
   needed (the owner, 2026-10-04): both are the owner's rules, the standard only an analogy.
4. Velocity lane: 0-200 % and "shape" default confirmed (LIVE.md).
5. Tuning copies: the free memory split between the parts that need copies only (8a53711).
6. Checker fast basses: 94 ms / 1.25 s (19fedbe), re-read against the books (review page
   https://claude.ai/artifact/KzdKk7UrH1HyJ2aFoYFR9h): they don't support a limit, so S12 is now an advisory
   ("playable, may sound unclear", still yellow) with the same numbers as the owner's trigger; no register
   condition (PLAYABILITY.md).
7. Smallest microtone: by frequency from Wier, Jesteadt & Green 1977, Table IV at 40 dB SL, held at the 200 Hz value
   below 200 Hz (option A; `microMinCents`, playabilityrules.h): 3.4-5.9 cents, was a fixed 5.

## Waiting for the owner (built, not yet confirmed on Windows / by ear)

Each has its background in docs/HISTORY.md Part 2 under the same name.
- The plain Live set (Create Live Set, 2026-10-06): the owner's first try, with Kontakt 8.13 or later on the PC
  (setups saved by the VM's 8.13.1 don't load in an older Kontakt).
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
- Calibrated levels (branch `calibrated-levels`, from `legato-pair-delays`, 2026-10-08; the owner: shorts too
  quiet, "use MuseScore 4.7.5's articulation profiles for everything incl. marcato"): `[levels] calibrated` (on,
  Recommended) plays SSO's velocity techniques as loud as the part's Long at the same dynamic, plus MS4's
  articulation offset, plus the Inspector's *Level* (every articulation sign and technique staff text, ±35.5 dB;
  metaTag `marcatoLevels`); calibration shipped as `share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json`.
  The owner listens: Recommended against Library default (shorts, marcatos, pizzicato against Long), and decides
  whether it merges (with `legato-pair-delays` under it). docs/PLAYBACK_SETTINGS.md › Calibrated levels.
  Measured 2026-10-08 (Windows VM, kthost offline, real SSO, All techniques Violins 1 / Flute Solo / Horn Solo, events
  from 1bceb0b4da; each note's loudest 50 ms RMS, median of 4, minus Long at the same dynamic, p / mf / f):
  Violins Short 0.5 off -3.6 / +1.6 / -5.2, on -4.1 / +2.5 / -1.2; Pizzicato off -13.2 / -4.2 / -5.5, on +1.4 / +2.4 /
  -1.2; Flute staccato off -13.8 / -8.1 / -0.8, on -5.1 / -2.1 / -1.6; Flute Marcato off -15.4 / -6.6 / +0.4, on -0.6 /
  +4.2 / +3.8 (target about +4.5); Horn staccato off -2.0 / -3.2 / -4.6, on -1.3 / -3.6 / +0.6; Horn Marcato off +4.1 /
  +3.6 / -2.7, on +4.7 / +2.1 / +1.5. Open: at p the shipped curves are bumpy (Flute's 48 quieter than its 32), so
  straight-line reading misses by up to 5 dB; Horn at mf picks a lower velocity than off (staccato alternates samples,
  5 dB note to note). On a K-weighted peak, Flute Marcato on reads +4 .. +8.6 dB (may sound louder than the RMS says).
  Since then (d8e7aa2001, 29aca978b8): calibration by ear (perceived curves), every held Long measured at 8 CC1 points.
  Measured again 2026-10-08 (same VM and patches, kthost, 3 pitches each: Violins A3 / E5 / D6, Flute E4 / F5 / E6,
  Horn F3 / B♭3 / F4 sounding; perceived peak, technique minus Long, 54 rows): off 6 .. 35 dB away (low pizz -34.7),
  on median |2.95| dB, 41 of 54 within 5 dB. The rest is pitch: one velocity per part and dynamic for every pitch, the
  curves measured at one test pitch. Worst on: Horn F4 staccato mf -10.4, Flute E4 staccato -9.9 / -9.5 / -7.9,
  Horn F4 Marcato p +8.2, Violins D6 pizz +6.9 / +6.0, Violins A3 pizz f -7.4. A fix needs per-pitch level curves
  (a new measurement: the check at several pitches); offered to the owner, not started.
  Also on this branch (12b6b30593, 2026-10-08): every measured technique starts early by its median onset, shorts
  and marcato too (map onset= on 343 more articulations); untried by ear with SSO. Most shorts move 3-8 ms, marcato /
  tenuto ~33, Short 0.5 61, Short 1.0 113 ms (docs/PLAYBACK_SETTINGS.md). Listen: slurred shorts against Longs.
- Tuning: the playback regression (`ab/trace/regress.sh`, not in the repository) not run since edb5eec;
  *Tools › Tuning…* left untested by the owner's choice.

No longer waiting (moot since "no automatic adjustments", 2026-10-06; the settings and their code removed
2026-10-07, docs/PLAYBACK_SETTINGS.md lists them): even dynamic steps and attack salience (calibrated short
velocities), `[tuning] oneInstance`, the timing A/B renders, the Track Delay value, per-patch octave corrections,
pedal 40 / 90 ms, audit #4 (event timing within a block).

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
