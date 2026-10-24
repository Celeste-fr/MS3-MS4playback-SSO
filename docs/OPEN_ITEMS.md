# Open items: every known imperfection in playback, ranked

State of integrate-1017 @ 0ab003951f, 2026-10-21. Gathered from CLAUDE.md, HANDOFF.md, LIVE.md, VERIFY.md,
PLAYABILITY.md, docs/*.md, the commit messages since 2026-10-08 and the private investigation reports (private, not in
repo: `~/MuseScore/ms3fork/private/<folder>/RESULT.md` and others). The tools named here are described in
`docs/TOOLS.md`; tools that don't exist yet are in **Missing tools** at the end.

How to read an item: **W** what's wrong (numbers) · **Src** where it was measured · **Cause** suspected · **Tool** to
measure it · **Fix** proposed · **Miss** data missing · **Own** owner decision pending · **Untried** what has never run
on the real thing. Ranks are by audibility (how much a listener would notice in normal scores), then by reach.
"Tested with" follows CLAUDE.md: Linux = stand-in only; VM = real Kontakt + SSO / real AGM demo, offline renders;
owner = heard.

Keep this file current: close an item by deleting it (the commit message says what fixed it), add new ones in rank.

---

## Tier 1: clearly audible, or makes results untrustworthy

### 1. CLOSED 2026-10-21 (Windows build #461): chord voice / tuning copy slots not repeatable between renders
- Was: in #458's export Whence's Violins 1 line went onto 1, 2 or 4 copies from one render to the next (92 | 79/13 |
  63/18/10/1 note-ons), byte-identical score; cause: the copies' maximum from the free memory, read once a run, on the
  VM's Hyper-V dynamic memory (private `solo-clicks/`). Fixed by afd801287d (memory not yet visible counted), then made
  moot by `all-copies` (fa08fef3de: every copy the music needs, memory only warns).
- Closed by `repeat_check.py vm Whence.mscz --settings recommended --n 4` on build #461
  (https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/39714089661, d673d96837), no `--free-memory-mb` pin:
  note-ons per slot identical in all four runs (Violins 1 63/18/10/1, the Linux dump's) and every slot's event list
  identical; the WAV hashes differ on 16 of 17 routes, as expected without `--fixed-transitions` (Performance legato
  differs take to take). The heard check's two takes on #461 split Violins 1 the same way (private `check461/`).

### 2. Plain Performance timing (default `[slurs] plainPerformance` 1 with one `plainShift` per patch)
- **W** (Mixed techniques, build 457, export, −15 dB rule): Performance fresh attacks early (V1 −46, V2 −37, Va −74,
  Vc −41, Cb −21 ms), transitions late (+86 … +203 ms; 60 bpm +150 … +208, 144 bpm +100 … +131). A beat's sections
  spread 160-230 ms; an All-techniques quick note (pizz., col legno, tremolo) sounds ~30 ms after another section's
  Performance attack and ~150 ms before its transition. Whence: attacks −50/−19/−62/−31/−9, transitions
  (V1/Va/Vc/Cb) +43/−3/+134/+182 ms.
- **Src**: HANDOFF.md › Mixed techniques; commit 83857c4a45; private `mixed-techniques/RESULT.md`.
- **Cause**: one constant per patch (65.8 / 66.0 / 111.0 / 80.2 / 59.1 ms, the all-notes median) can't place both
  attacks and transitions.
- **Tool**: heard check (export; `--takes N`, `HC_TRANSITION_RULE=leave`), `heard_summary.py` (spread, drift, both
  readings), `clips.py` (per-section click clips for the owner, `measure` for what he heard).
- **Fix**: `[slurs] plainTiming` 1 (7e6f20f5fd): attacks by `plainAttack`, transitions by their modelled arrival.
  Mixed: attacks −7 … +3, transitions V1 +1, V2 −17, Va +4, Vc +42, Cb −42 ms; within ±30 ms of the beat 58 → 67 %.
  Whence: attacks −4 … +27 (V2's +27 masked readings), transitions −52 … +18; ARRIVAL 35/26 → 36/19, RHYTHM slurs
  1/0 → 3/1 (worse), runs 9/6 → 7/3, max beat spread 814 → 336 ms but median 62 → 93 ms.
- **Own (heard 2026-10-21)**: the owner, each section alone with a metronome click (Whence bars 5-14, real SSO, build 458):
  Violins 1 **A** (plainTiming 0) better, Violins 2 no difference, Violas "maybe B, can't really tell", Celli **A** (B
  "sounds early"), Basses **A** ("B is way too early"). So plainTiming stays 0. **This contradicts the heard check**:
  by its readings (attacks −15 dB, transitions at pitch arrival) B was closer to the beat for Celli and Basses, and A's
  transitions are +130 … +200 ms "late", yet A sounds on time. The ear places a Performance transition near where the
  pitch LEAVES the note before (Polfreman 2013's leave + 23.5 ms, `transitionsEarly` 2's rule), not at its arrival.
  **Re-judged by leave (2026-10-21, kb-tools)**: `clips.py measure` read every transition in the very clips he heard
  (aligned on their clicks; the clip's arrivals agree with the reports' within 0.2-4.4 ms median). Median heard −
  written, attacks (−15 dB) + transitions, A / B, and which is nearer the beat (median |d|):
  | section | owner | by arrival | by leave + 23.5 |
  |---|---|---|---|
  | Violins 1 | A | +26 / −3 → B | +2 / −20 → B |
  | Violins 2 | no difference | 3 attacks only, no transitions (−50 / −15) | same |
  | Violas | maybe B | −12 / −18 → same | −89 / −54 → B |
  | Celli | A (B early, A late?) | −48 / −36 → B (transitions alone: A, on 9-10 arrivals) | −106 / −154 → A |
  | Basses | A (B way too early) | +182 / +16 → B | +63 / −178 → A |
  The leave reading agrees with his choice for Violas, Celli and Basses, and with "B way too early" (Basses B −178);
  the arrival reading agrees with none of his clear choices (most transitions never arrive within 30 cents: Celli 10-12
  of 56). Neither explains Violins 1 (A preferred, both readings say B), nor "A late" for Celli (leave says A early).
  One owner, one take, one passage: evidence, not proof. `heard_check.py --transition-rule leave`
  (`HC_TRANSITION_RULE=leave`) now judges by it; report.json carries both readings (`leaveMs`, `arriveMs`) and
  `heard_summary.py` shows both. Next: a second passage heard by him (clips.py), and the transition model (item 3)
  fitted to leave times rather than arrivals. Private: `kb-tools/owner-ears/` (ears.json, measured.json),
  `plain-timing/OWNER_EARS.txt`.
- **Real players (2026-10-21, Spheres K. 550 mvt 1, 175 slurred changes, `tools/soundlibraries/spheres_legato_transitions.json`,
  CC BY-SA 4.0; private `legato-spheres/RESULT.md`; readings page https://claude.ai/artifact/LFYgh6JWiy5Xn4q2fD8KTN, not
  yet checked by the owner)**: a real section puts the **midpoint** of a slurred change on its own beat (leave −19, mid −3,
  arrive +10 ms against its bowed attacks, upper strings n 161; Vc/Cb mid +19, n 14); real changes take 3-43 ms, SSO's
  57-110 ms. The midpoint (or leave) rule predicts the owner's five verdicts; the arrival rule doesn't (it picks B for
  the Basses). Supported rule: send each Performance transition early by SSO's own in-context note-on-to-midpoint delay
  (`transitionsEarly` 3 is the nearest mechanism; its model misses in-context midpoints by up to ±70 ms: measure them).
  Prediction to test by ear: Mixed techniques' slow bars (IOI 500-1000 ms) should sound better with plainTiming 1.
- **Midpoint table (2026-10-22, branch `transition-midpoint`; data and a proposal only, no playback change)**:
  `tools/soundlibraries/sso_transition_midpoint.json` (`transition_midpoint.py table`; its `_about` is the method),
  measured by `measure_patch.py` on the Windows VM (kthost + Kontakt 8 + SSO, plain velocity 64, CC119 written by the
  script from the played spacing, random modulators on, 2 takes ABBA, 1.6 s silences): SSO's note-on → f0 midpoint
  delay for the five sections' Performance legato, by the note before (attack / transition in a run / turn back /
  attack after a staccato), its length (136-2182 ms), how much earlier the note is sent than the one before (0-200
  ms), its own length (a note follows by transition) and the interval (±2 main; ±0.5 … 7 semitones, quarter tones sent
  as microtoneBends 1 sends them). Take SD 1.8-4.4 ms (median); pitch to pitch p10-p90 ±30-80 ms.
  - **The delay follows the played spacing**: a note sent r ms early after an L ms note behaves as one after an L − r
    ms note (median difference −5 … +5 ms; p10-p90 ±10-25, Basses ±35). So an early send changes its own delay.
  - **At short spacings sending earlier slows SSO down**: after a 136 ms attack, sending the transition 50-100 ms
    early makes the midpoint 15-50 ms later (Violas 131 → 170-180 ms), so V1, V2 and Violas have no early send that
    puts the first transition after a 136 ms attack on its target (`fixedPoint`, `found` false).
  - Delay in a run (r = 0, ±2), after 136 / 273 / 500 / 1000 / 2182 ms: V1 158 / 158 / 189 / 235 / 233, V2 155 / 166 /
    184 / 186 / 183, Va 139 / 118 / 180 / 193 / 198, Vc 185 / 188 / 210 / 195 / 159, Cb 138 / 160 / 155 / 160 / 173
    ms. Now (every note plainShift early) that is +92, +89, +28, +105, +78 ms after the beat at 136 ms and +69 … +170
    ms in slow notes. A semitone up is 36-78 ms quicker than ±2, a semitone down 9-70 ms slower; ±1.5 −21 … +33 ms.
  - **Turns** (Whence's alternating figures): at 136 ms 41-158 ms; 24-33 % of the turn readings show no change at all
    (the f0 never reached the note before: `unreached`).
  - **Validation** (`transition_midpoint.py predict`, every transition as the dump sends it): Mixed techniques' slow
    bars, the owner's clips (#458): predicted − measured midpoint V1 +8 / +4 (A / B), V2 −1 / +19, Va −9 / −62, Vc −57
    / −67, Cb +11 / −15 ms. Whence (fast quarter-tone figures) against #464's export (f0 midpoints,
    `heard_midpoints.py`): the isolated figures are late by V1 +18, Va +23, Vc +75 ms (the same in odd and even bars;
    per-note r 0.53 / 0.27 / 0.79): in a whole passage SSO's fast transitions come sooner. Stored as `inContext`
    (after notes ≤ 300 ms).
  - **The owner's verdicts**: the table does not reproduce the clips he judged (#458, microtoneBends 0): it puts V1,
    Vc and Cb's A late and predicts B for them (he chose A), A for Violas (he: maybe B). The clips' own measured
    midpoints do predict him (above). On the same notes `[slurs] microtoneBends` 1 (#464) moved the measured midpoints
    later: V1 +17 → +48, Va −20 → 0, Vc −51 → +69 ms (V1 / Va on the clips' notes, Vc on all; one reader, #458 →
    #464). **The Violins 1 case** is the midpoint itself: in his clips A's −7 ms against B's −34 (target −3); neither
    model nor leave reading gets it. Under today's microtoneBends 1 V1's and Celli's A midpoints are ~+50 / +70 ms
    late again.
  - **Proposal: `[slurs] transitionsEarly` 4, "midpoint from the measured table"** (needs the owner's ear first, then
    code): under plainPerformance, attacks by plainAttack (as plainTiming 1); each transition sent early by e solving
    e = delay(L, e − e_before) − target (−3 ms upper strings, +19 Celli / Basses), chained through a slur, scanned 0 …
    e_before + L − 15 ms (DROPPED), the bend glide moving with it; the map carries the table (a `midpointDelay` per
    Articulation by kind × L × r × interval class, plus `inContext`). Whence, against now: V1 72 ms earlier (56-80),
    Violas +2 (−5 … +28; only the first transitions after a staccato +30-47), Celli +36 (3-79; whole-note transitions
    bars 10-16 +130-155), Basses +163 (84-229, no in-context reading: the Basses' rising thirds after whole notes
    leave +62-85 and arrive +274-596 ms after the beat in #464). Risks: the Basses move the way of B, which he called
    "way too early"; Celli turns read bimodal in context (+88 runs, −89 turns). Private:
    `transition-midpoint/RESULT.md`.
- **Untried**: *Live plays the score* and the route set don't apply the shift at all (Violas 95.2 ms late there); the
  plain set's Track Delay untried in real Live.
- See also items 3, 9, 15.

### 3. Plain Performance transitions: the model misses Celli / Basses and transitions after a held note
- **W** (plainTiming 1, build 458, one take): Mixed Celli transitions +42 ms, Basses −42 ms; Whence Celli −52 (the
  other sign). Transitions scatter ±100 ms p10-p90; by tempo the model is off −65 … +63 ms. Whence Violas bars 9 and
  13 (written 273/136/136 ms slurs) heard 174/194/115 and 189/194/101: the second note's transition after a held first
  note arrives ~100 ms sooner than modelled (two new RHYTHM slur fails). Solo-click runs (same build): Whence ARRIVAL
  32/28 → 39/17, RHYTHM slurs 1 → 6 fail; drift Celli transition-after-attack +82, transition-after-transition −80 ms;
  Violins 1 alone bar 14 beats 3-3.5 heard IOI −77 ms (order inversion) and a −9.85 dB LEVEL step at beat 3.75.
- **Src**: private `plain-timing/RESULT.md` (› Open 1), `solo-clicks/` (w0, w1, wv0, wv1); HANDOFF.md › Plain timing.
- **Cause**: `bendGlide`'s arrival is modelled only by the note-before's length (measured on runs of equal notes);
  after an attack or a long note SSO's Performance legato behaves differently; random modulators (item 11).
- **Tool**: heard check, private `tdrift.py`; missing: the per-patch × situation drift report (Missing tools #2) and
  a transition sweep by interval × IOI × preceding length × preceding kind (Missing tools #1).
- **Fix**: a transition-after-attack / after-held model by preceding length, fitted on a dedicated sweep (Heard
  passages, plain), with ≥ 2 takes per cell.
- **Data (2026-10-21, patch-sweep)**: `tools/soundlibraries/sso_transition_context.json`, measured by
  `measure_patch.py` (VM, kthost + SSO, plain velocities 64).
  - Transitions by the note before: attack / transition × 136-1000 ms × the IOI after, CC119 played / written.
  - Leave grows with the note before's length (136 ms: 44-146 ms after the note-on; `bendGlide` says −8).
  - Celli's heardRef is flat at ~165-185 ms, the Basses' 67-112 ms.
  - Take SD 1-5 ms.
  - Docs: MEASURING.md › Proof.
- **Miss**: velocities other than 64; long passages with the round robins cycling (the isolated figures run 18-75 ms
  later than Whence's: item 2 › Midpoint table, `inContext`). Early starts, intervals and turns: measured 2026-10-22
  (`sso_transition_midpoint.json`, item 2).
- **Own**: the Violas bars 9/13 passage is behind his "C" choice: needs his ear.

### 4. Sul tasto / flautando / harmonics and other CC1 techniques: levels and timing
- **W levels** (Mixed techniques, perceived peak against the Performance attacks at mf): sul tasto V1 −30.2, V2 −19.5,
  Va −53.5, Vc −41.8, Cb −32.7 dB; flautando −27.5/−16.0/−30.6/−23.5/−25.1; Basses harmonics −31.4. Others: pizz. −8 …
  −13, col legno −19.1 … +3.5, Bartók +0.9 … +12.2, tremolo −2 … −7, trem. sul pont. −6 … −15, con sord. −4 … −6.
  The shipped dynamics.json agrees (Super Sul Tasto perceives 3-8 at CC1 80 against Long's 40-58).
- **W timing**: Long Sul Tasto / Super Sul Tasto / Flautando / Long Harmonics start 380-770 ms early and are heard
  −580 … +870 ms from the beat (sul tasto −188 ms median, at 144 bpm −537; flautando −123; harmonics −55); Violins 2 bar
  38 Super Sul Tasto's 767 ms early start is cut 211 ms after a 417 ms note (2 ARRIVAL fails, also under plainTiming).
  Violins 1 flautando's low register +256 ms (2026-10-02, never closed).
- **Src**: private `mixed-techniques/RESULT.md`; HANDOFF.md › Plain timing › Open 3, Mixed techniques.
- **Cause**: techniques on CC1 keep the library's balance (uncalibrated); slow attacks have no onset rule that fits
  (item 16).
- **Tool**: `--check-dynamics`, `sso_cc1_sweep.py`, heard check.
- **Fix**: calibrate CC1 techniques to the Long minus a real-player offset (HANDOFF proposal 3).
- **Miss**: any real-player data for sul tasto / flautando / harmonics against ordinario (searched: none found). Without
  it the offset is an assumption (CLAUDE.md: balance from real data; a gap is marked as one).
- **Own**: whether to use an assumption until data exists.

### 5. Fast quarter-tone slurs never reach their pitch
- **W**: Whence Violas bar 8 never within 30 cents of 61.5 / 64.5 (40-60 c off); Celli bar 1 70-180 c off; quarter-tone
  transitions reached 32/141 (kthost), 26/141 (export); 69 of 147 bending-copy quarter tones never settle; 141 notes in
  tune for less than half their length (mean 10-16 %); UNHEARD 126-142 on Whence.
- **Src**: HANDOFF.md (legato bends, qt-bends), VERIFY.md heard check runs.
- **Cause**: SSO's legato is slower than 136 ms notes; the bend retunes the note still sounding; Kontakt smooths a
  bend over 71-80 ms. Measured 2026-10-21 (branch `fast-microtones`): plain Performance plays the fingered transition
  (velocity 64), which leaves 25-100 ms after its note-on and arrives 130-200 ms after it (`bendGlideFingered`), while the
  bend followed the bowed one (`bendGlide`: from the note-on); and key and bend went opposite ways (B3 -50 after A3 sent
  on B3 bent down): the pitch hooked a quarter tone the wrong way first (the owner's "weird" bar 7 of the Violas).
- **Tool**: heard check (UNHEARD), `legato_glide/` (`gen.py --velocity=64`, `glide.py --summarise --moving`).
- **Fix**: `[slurs] microtoneBends` (3 in Recommended and by default since 2026-10-22: the owner heard B on Whence's Violas (1), then Celli and Violins 1 "both B" (3)): 1 the key on the side of the note before + the
  fingered glide uncut, 2 the glide only. Whence Violas bars 5-8, quarter tones in tune 8-10 % -> 35-40 % of their
  heard time (kthost); in the export (run 462) Whence Violas bars 5-14 15 -> 27 %, the wrong-way hook of bar 7 40-44 % ->
  0 %; Violins 1 12 -> 28 %, Celli 5 -> 14 %, their 12-TET notes after a quarter tone 39 -> 30 % and 28 -> 20 %
  (docs/PLAYBACK_SETTINGS.md › Microtone bends). Still a glide
  through each sixteenth: SSO's fingered transition is longer than the note. 2026-10-22 (branch `microtone-next`): #464's
  12-TET notes between the quarter tones were late, not detuned: Violins 1's and the Celli's fingered transitions sound 60-70
  ms later against plainShift than the Violas'; `microtoneBends` 3 sends them early by the glide's middle (export: Violins 1
  12-TET 30 -> 52 %, quarter tones 23 -> 34 %; Celli 22 -> 56 %, 11 -> 24 %; Violas unchanged), by hand until the owner has
  heard it (clips: integrate-1017/.mixedtech/next/clips). Left: another technique for fast
  quarter-tone runs (the bowed transition 30-35 %, detached tuned attacks 25 %: both measured, neither better).
- **Own**: yes, how to play them.

### 6. *Live plays the score* and the route set lack most level and timing layers
- **W**: no Track Delay through Live's API: played timing kept, plainShift missing (Violas 95.2 ms late), start lead
  lost (Whence Celli 172 ms late); no gain stage / Utility, so level rule, section balance, realRange, mic compensation,
  even levels and master trim are all absent; track delays absent; a clicked note plays 3.6's click.
- **Src**: LIVE.md › Live against MuseScore › What still differs.
- **Tool**: `tst_liveequivalence`, `--live-equivalence` (simulation).
- **Fix**: write Track Delays and a Utility per track in the route set (hand-made tracks would need them typed).
- **Own**: whether the route set should write them. The plain set (Create Live Set) is the path that carries them.

### 7. Live parameter-lane edges late by a per-start constant; a silent Solo Violin 1 route
- **W**: parameter-lane edges in Live playback late by +11 / +17 / +40 / +40 / +52 ms, a constant per transport start
  (`plugphasor~` +17 / −0.2). Build 49b00a0's lanes test: the Solo Violin 1 route exported silence from Live, never
  investigated.
- **Src**: LIVE.md.
- **Cause**: not found.
- **Tool**: `tools/live/research/edges.py <wav> [bpm]`; `tools/live/realcheck/realcheck.py run <lanescore> --lanes
  mic-steps` (`realcheck.py lanescore`).
- **Re-checked 2026-10-22** (real Live 12.4.7, build #467, Solo Violin 1, Mic 2 stepped every bar): the six edges within
  −5.0 … 0.0 ms of MuseScore's export, the route not silent; the late edges did not appear. Not re-checked on Live
  12.4.6 (the VM's Live updated itself) or with other parameters.

### 8. Chords on mono Performance patches: unconfirmed on Windows
- **W**: chord-voices (da91f0b459) is meant to make Whence Celli bars 15-16 sound all three chord notes (two had
  been dropped: −0.5 / +3.9 dB against 38-40 for the kept note). A Windows build must confirm. The 42 patches are
  assumed to share the Violas' script (checked on Celli only). Each extra voice costs ~1.3-1.7 s load, ~220 MB.
- **Src**: private `bars9-12/SILENT.md`, `CHORDS.md`; HANDOFF.md › Chords on monophonic patches.
- **Tool**: `--verify-playback` with `MS_VERIFY_ROUTES=1`; audit CHORD.
- **Untried**: with Kontakt / SSO in the GUI and in Live. Linked to item 1.

### 9. Accents under plainAccents 2 (default since 2026-10-21): onset unmeasured, Basses' C2
- **W**: the accented attack (velocity 122) uses `plainAttack` (or, under plainShift, velocity 64's shift): its own
  onset is unmeasured. Level: target +5.0 dB, 90 % [+3.8, +6.0] (Spheres K. 550 sf at f, 34 instances); in context
  (Whence bars 8-9) V1 +5.9, V2 +3.3 … +5.3, Va +4.5, Vc +4.9, **Cb +3.3**: the Basses' C2 at 205 ms misses ±1.5 dB
  (the held C2 before it releases into the measuring window); earlier, the accented C2 played −6.3 dB under its own
  held C2 (38.5 vs 44.8; the built-in synth +4.1), and in plain-accents about 13 dB under whatever the value. A positive
  D is capped at CC11 127 (Violas Eb4 211 ms: +3.4 of +4.7 dB). SSO's 122 − 64 is +5.6 … +8.0 dB at 100 ms but +1.2 …
  +6.1 at 450 ms; per key −9.0 … +5.9.
- **Src**: private `accent-level/RESULT.md`, `accent-spheres/RESULT.md`, `bars9-12/ACCENT*.md`, `plain-accents/`;
  HANDOFF.md › Remind; PS › Plain accents. The owner heard plainAccents 2 on Whence bars 7-9: "fine" (3d783a6ac7).
- **Cause**: Basses - Performance's register / attack behaviour at C2 (not investigated); the release overlap.
- **Tool**: private accent grids (`mkgrid.py` / `anafull2.py`); missing: onset by velocity in the patch sweep
  (Missing tools #1).
- **Miss**: the accented attack's onset by section and pitch; "&gt;" (not sf) in real players (next source: Romeo and
  Juliet's strings, Spheres, not aligned); a short-note CC1 curve (for value 1, which overshoots to +10 dB on V1 / Va).

### 10. Quiet or dark Ample sounds and Ample-specific gaps
- **Found (2026-10-21, branch `ample-pipeline`)**: the quiet / dark strums are **not the demo**: AGM's strummer reads its
  set-up (strum mode, Detect, the chord keys) only from an earlier process block than the strum key. In the same block
  every strum plays dark and partial (centroid ~350-600 Hz against ~1200, 2-6 dB quieter) and, at exact multiples of
  7.5 s of render time (to ~1 ms, independent of tempo, transport, pacing, pre-roll), quiet; plain chords never. Least
  lead: block 16 1 ms, 64 2 ms, 512 6 ms, 2048 about one block (amphost, `measure_ample.py strumtiming / strumlead /
  demo15 / demofine`). MuseScore #458 plays them dark too (`check.py` strum bright: 639 Hz). **Fix**: map `strumLead`
  50 (`Guitar::strumLeadMs`, guitarplayback.cpp: the set-up 50 ms before the strum key, a previous strum's release moved
  there; at the score's start the set-up stays at 0 and the start lead plays the rest later). StrumLevel re-measured
  with the lead for all three sets (Pick measured, no longer Finger's). **Left**: a strum on a later chunk's first tick
  (every 10 bars) still gets its set-up in the strum key's block (the chunk before may release a strum there); Live's
  clips have no start lead (a strum at 0 in the plain set: dark).
- **W CC11**: CC11 does nothing on Ample by default, so every per-note CC11 level (divisi, track levels, marcato /
  Inspector levels, even levels) never reaches it. Measured 2026-10-21 (`measure_ample.py cc`): with a MIDI-learn entry
  Control ID 16 → M-Master Vol, CC11 acts exactly as CC7 (linear, 64 unity), but the two then overwrite each other (last
  writer wins), so a map entry alone can't make them multiply. Fix: fold the per-note CC11 levels into the M-Master Vol
  value in `libraryHostedParameters` / the guitar pass (the strum level already multiplies the volume lane there).
- Strums ≈ 2.7 dB quieter than the same chord as a 60 ms roll (before strumLead; with it +2.4 dB); pp renders at velocity
  32-33, above `softStrum` 30, so only ppp gets the soft forms; a 6-note chord leaves B3 22 dB under its register; scapo
  clips at +0.7 dBFS.
- **Src**: private `ample-check453/455/456/457/RESULT.md`, `ample-wincheck/`, `mv/`, `ample-pipeline/RESULT.md`.
- **Tool**: `tools/ample/` (measure_ample.py, check.py), `tools/soundlibraries/amphost/` (docs/AMPLE_PIPELINE.md).
- **Own**: `[guitar] strums`, `softStrums` 0/1/2 (ppp: 0 −32.5, 1 −26.5, 2 −28.2 dB), `pinchHarmonics` (Ample has no
  pinch sound): all 0, unheard.

### 11. SSO Performance patches aren't repeatable (random modulators)
- **W**: two passes null only at −6.9 … −8.4 dB; per note ±2-6 dB; one heard-check run is one take, and take spread ≈
  renderer difference (Whence 33/30 vs 41/27).
- **Fix**: `[slurs] fixedTransitions` (b990008a7d), 0 everywhere: nulls −73.5 (Va) / −70.4 (V1) / −96.6 (Vc) / −85.8
  (Cb) dB, per-note SD 0.22-0.50. Still random: bowed (velocity 85+) fast transitions at IOI ≤ 251 ms (Across Strings
  groups); Basses (D1 states) and Violins 2 never rendered; spectra up to 2.5 dB off the stock mean (lowest band, Celli).
- **Src**: private `sso-legato-fixes/RESULT.md`; HANDOFF.md › SSO legato fixes.
- **Untried**: in MuseScore with Kontakt; Update Live Set and *Live plays the score* don't carry it.
- **Own**: hear it (`/data/ssolf/listen/` on the dev machine).
- **Round robins too** (2026-10-21, branch `sso-script-fixes2`): the five string Performance patches and Tuba Solo -
  Performance don't reset their round robins at a transport start ("Reset on transport" saved off, read from the
  patch's script), only after 1.5 s without a note-on. kthost, Violas / Celli, passes 0.5 s apart: -3 .. -6 dB even with
  fixedTransitions. **Fix**: `[notes] transportReset` 1 (0 everywhere): with fixedTransitions passes repeat at -95 ..
  -100 dB. Proposed 1 in Recommended with fixedTransitions (owner). docs/PLAYBACK_SETTINGS.md › Transport reset.

### 11a. SSO's Hall trigger: an extra release at a CC1 step while a Performance note is held (2026-10-21)
- **W**: a CC1 step down of 10+ while a key is held plays a diminuendo release on every held key (read from the patch's
  script; VM kthost: a layer 14 dB (Violas) / 25 dB (Celli) under the note for ~1 s). MuseScore makes it at a subito
  dynamic inside a slur, at fp, at a dynamic on a slurred note after a diminuendo (`hall.musicxml`: 3); Whence, the
  passages, Mixed techniques, Strings: none.
- **Fix**: `[levels] hallGuard` (0 everywhere): steps of 9, 1 ms apart (measured: no release). Proposed 1 in Recommended.
- **Tool**: the audit's HALL. **Own**: hear `hall.musicxml` bars 1 / 4 with 0 and 1.
- **Unknown**: which other patches have an "Rls diminuendo" group (only the five string Performance patches read).

### 11b. SSO's shared settings can change every patch on a machine (2026-10-21)
- `%LOCALAPPDATA%\Spitfire Symphony Orchestra\Data\Share__Settings.nka` (read from the patch's script): a "share" tick
  in any patch saves the articulation mode, Reset on transport, reset key, transpose, velocity source or Hall trigger for
  every patch loaded later. Now checked as a patch loads (status bar, load times.log) and in every render's preflight.
  The VM's: nothing shared. **Own**: the owner's PC not checked (the preflight shows it on the next render there).

### 11c. Live doesn't play a plain set's first note when it starts early (real Live, 2026-10-22)
- **W**: a note at the song's start that MuseScore plays early (onset, smooth attack) is played by its technique track's
  negative Track Delay, before song time 0; Live 12.4.6 and 12.4.7 drop the whole note in *Export* (Strings: the first
  Violin and Cello notes silent for 2.5 s; the lane score's one held note silent for 19 s). With the Violin's Track
  Delay set to 0 by hand the note played (late by its onset). Every plain set of a score whose first note starts early
  (Recommended: most) loses it.
- **Src**: `tools/live/realcheck` (Strings #464 / #467, lanes #464), LIVE.md › Checked in real Live.
- **Fix**: the plain set starts one bar later (an empty lead-in bar: every clip, envelope, locator and the read-back
  shifted), or such notes go late by their onset in Live (as *Live plays the score* does). Untested whether Live's Play
  from 1.1.1 (not Export) behaves the same. **Own**: which.

### 11d. Whence's Celli in real Live: not silent; its chord voices' CC119 never sent (2026-10-22)
- **Was reported** "Celli - Performance silent the whole piece" (#464 / 12.4.6, #467 / 12.4.7, exp2). **Not so**: Live
  plays it (rms within 0.35 dB of MuseScore's; the exported file's peak -14.2 dBFS). The real-Live check's comparison
  aligned the track a figure off (+407 ms where the set's lag is -30.7 ms: the Celli's repeating three-quarter figure
  fooled the envelope match; corr 0.05) and then reported the merged span's *quietest* 250 ms window (-224.7 dB: zero
  padding) as the level of the whole track. Isolated on the VM (branch `live-celli`): Violas + Celli alone, Celli alone,
  its multi as 1 / 3 / 4 slots, the full set re-saved, all-techniques Kontakts removed: Celli plays in every one, the same
  render each time. **Fixed in the tool** (`compare.py`): a track far from the set's lag (the other tracks' consensus less
  its own Track Delay) is aligned again near it when that correlates better (Celli: corr 0.895, gain -0.57 dB, null -7.2,
  -24.3 dB outside its spans: like Violins 1 / Violas); a span's level is its energy over the span (`quietestDb`,
  `firstDb` beside it); the start-lead feature reads the first window. `test_realcheck.py` (16).
- **What also differed** (the divisi / voice-copy check, now testable): at each chord change of the voice copies (bars
  9-16) Live was +5.4 .. +7.6 dB louder for ~0.4 s, the voices' transitions faster (D3 ~0.3 s after the note-on,
  MuseScore ~0.48 s); between the changes the voices matched within 0.1 dB (the divisi level plays as in MuseScore).
  Cause: `[slurs] speedFromWritten`'s CC119. Every chord-voice transition is at speed 0 (half / whole notes), so each
  copy's technique clip envelope held 0 throughout, and Live sends a clip envelope's value only where it changes: the
  copies' slots never got it and played their transitions at the speed they had. (Ruled out first, same bursts within
  0.6 dB: the copy tracks without the MuseScore Link device, the CC11 steps 2 ms earlier, the notes touching / 1.1 ms
  apart / 1.1, 11 or 27 ms overlapping; MuseScore with its own 24 ms overlap on them didn't match Live either.) The same
  set with each copy's envelope stepping 1 -> 0 before its first note: the bursts gone (-0.0 .. +0.2 dB, one +1.8),
  corr 0.895 -> 0.975, null -6.5 -> -13.1 dB. **Fixed** (`PlainLiveSet` `speedEnvelope`, branch `live-celli`): a
  technique track's first CC119 value is always a step (from 0, or from 1 when it is 0);
  `tst_liveequivalence::speedSentLive` (what Live has sent before each note-on is MuseScore's value). **Confirmed in
  real Live** (#469, 12.4.7, realcheck on Whence with fixedTransitions / speedFromWritten 1): Celli corr 0.895 -> 0.975,
  null -7.2 -> -14.0 dB, the chord changes +0.0 .. +0.2 dB (one +1.8); the Basses gained too (0.882 -> 0.921, notes' level
  median +1.7 -> -0.1 dB). Left: the Celli's first second +3 dB (the start lead), a Basses note at 16.4 s +16.6 dB (11e).
- **Src**: private `live-celli/` (exp E1-E3, F1-F2, G0-G11, w1, a468, b468), `live-realcheck/` (w467, exp2).

### 11e. A re-struck key on a Performance patch with no gap (real Live, 2026-10-22)
- **W**: Whence's Basses, a plain C2 (bar 14) followed by the same C2 accented (velocity 122, plainAccents 2): MuseScore
  sends the off and the on at one sample; Kontakt plays that quieter than any gap: in Live the accented note is +8 dB
  louder (+14 dB with the off 0.15 ms first, the retrigger margin; by hand, before the lookahead fix). Live can't keep a
  same-sample order across the device, so the two disagree whatever the plain set writes.
- **Cause**: the renderer's reattackGap (a touching note re-attacked) doesn't reach a re-struck key on the plain
  Performance path. **Fix**: give that note-off a real gap in MuseScore (then Live and MuseScore agree); **Own**: the gap
  changes what MuseScore plays (the accent as Kontakt plays it after a gap: louder).

---

## Tier 2: audible in some passages, or on all notes but small

### 12. Ample: hosted playback doesn't wait for the background load; AGM load crashes on the VM
- **W wait**: `SoundLibraryHost::sync` loads in parallel without waiting for Ample's own sample-set thread: Play right
  after a load may play the wrong set for ~1 s (Pick G3 −34.4 dB at 0 s pre-roll, −67.6 at 0.5 s, stable −25.5 from
  1 s). Offline renders wait (`backgroundLoad="60"`, 4488ee93e4; #456 3/3 bit-identical). The even-levels measurer
  doesn't wait either (SSO only today).
- **W crash**: AGM crashes during instance load (0xC0000005, VCRUNTIME140 memcpy): #457 3 of 21 verify runs; reruns
  #457 6/12, #456 4/12 (0/27 the day before). 9 minidumps: only AGM.vst3, msvcp140 and cryptnet.dll frames on the
  faulting thread, no MuseScore / Qt. Probably AGM's licence / demo check (network-dependent; rose after the VM's
  reboot and address change). Rerun is enough; every rerun matched.
- **Src**: private `ample-check457/RESULT.md` (Finding 3), `ample-check455/FIX4.md`; HANDOFF.md › Lane jumps and
  Ample's background load.
- **Done (2026-10-21, branch `ample-pipeline`)**: hosted loads of a `backgroundLoad` map wait for the plug-in's loader
  (`setStateWaiting`, serialised; test backgroundLoadHosted); `tools/ample/check.py` retries crashed runs. Measured:
  amphost single loads 0 / 30 with the network, 0 / 30 with amphost's outbound traffic blocked; MuseScore (3 instances
  loading in parallel) #458 2 of 15 render attempts, #463 1 of 14 with the network and **9 of 22 with MuseScore's outbound
  traffic blocked** (`check.py --block-network`; two renders failed 3 times running): the network matters, so the licence /
  demo check is the likely cause.
- **Fix (left)**: let `Seq::start` wait as for even levels; retries in the heard check (as `superviseExtract` does).
- **Miss**: whether an activated AGM crashes at all (the bought guitars: `check.py` twice, once with `--block-network`).
- **Untried**: GUI (a GUI crash would take MuseScore down). AGM trial ends ~2026-10-25.

### 13. Ample capo, state lanes and other Ample limits
- **Real Live (2026-10-22, Live 12.4.7, #464, `tools/live/realcheck`)**: Ample's plain set opens, exports and saves; per
  note Live plays MuseScore's level and time (113 notes: median −0.07 dB, onsets 0 ms) but the Sustain track doesn't
  null (correlation 0.32; MuseScore against itself bit-identical): AGM's own variation isn't reset by Live's transport,
  some notes up to 14.6 dB apart. Live fills empty AGM panel slots (S-Strum Toggle, S-Chord Mode) keeping the empty
  value, which Update Live Set took for a turned knob (fixed, Linux only: `LiveTracks::isEmptySlotValue`). The saved
  state holds M-Master Vol at its last automated value (0.435 / 0.44 for 0.5).
- **W**: playing from a later tick may keep the instance's last capo (HANDOFF › Phase 2 says not resent; MV.md says
  `Seq::updateSynthesizerState` replays ME_PARAMETER on seek: contradictory, untried in the GUI). Live's clips get the
  capo at 0 with the first note (no start lead); over MIDI out M-Capo is set by hand. State lanes: hosted and Live play
  the lane's first value only; Update Live Set doesn't rewrite the state. M-Master Vol in Live may act up to one buffer
  (≤ 11.6 ms) early. Max Voices 1: which string survives changes between VM sessions (−14.3 vs −21.6 dB).
- **Not measured / assumed**: Auto Legato values (which is Off), Pop (no rule), slide speed by velocity (510 / 330 / 170 /
  40 ms at 20 / 60 / 100 / 127, on the Lite; not modelled), softRoll's linear spacing between 20:10 and 40:15 ms
  (v20's 10 ms: level can't choose, 5-25 ms within 0.7 dB), hammer-on evidence weak; 204 state settings with menus / ranges
  partly assumed (2026-10-21 `stateclamp`: every one at its assumed ends loads and is kept; ~70 toggles clamp to 0 / 1 or
  -1 / 1; S-Pattern Index -1 / 8 and FX-Reverb-IRType -1 crash AGM, so 0-7 / >= 0 confirmed; numbers are echoed as
  written, so a range end isn't proven by it: EQ
  type 0-4, chord type 0-23, delay grid 0-8, reverb size 0-200, ±12 tone, …; Capo Logic / Open String / S-TempoMode
  meaning unknown); Master Tune ignored by setState.
- **Src**: private `ample-state/CTL.md`, `ample-check453-457/`; HANDOFF.md › Ample guitars.
- **Fix**: read each assumed setting at both ends in AGM's GUI (review-page rule for the screenshots), regenerate with
  `gen_ample.py`.
- **Untried**: everything Ample in real Live; the GUI seek.

### 14. Only one sound library plays at a time
- **W**: `SoundLib::current()` holds one library (preference `io/soundLibrary`): a score can't play SSO and Ample parts
  together. Found reading the code; not documented elsewhere.
- **Fix**: per-instrument `<Plugin>` / several libraries active (new C++), or one "Ample Guitars" map hosting several
  plug-ins.
- **Own**: whether that matters, and how the four other guitars ship (Missing tools #6).

### 15. plainAttack and plainShift come from one take each
- **W**: `plainAttack` (V1 19.5, V2 29.2, Va 37.1, Vc 38.7, Cb 37.5 ms) = medians of 39 attacks per section in one heard
  check (#457); `plainShift` measured at velocity 64 on kthost. Take spread is as large as the renderer difference.
- **Fix**: ≥ 3 takes per value (needs Missing tools #1 / #4).

### 16. The onset rule (−15 dB vs −6 dB under the peak)
- **W**: map onsets and the heard check use −15 dB under the peak, −30 ms. The owner's ear on Whence's Violas smooth
  first notes (~200 / 210 / 105 ms) matches −6 dB; at −6 dB the Mixed attack gap vanishes (Performance +27 … +61,
  quick +23 … +61) while 8 of 10 Violas slurs then fail RHYTHM. Measured latency 24 ms isolated but ~3 ms in context
  (30 chosen). If −6 dB is right, the slow-attack onsets in the map are 50-100 ms off.
- **Src**: private `plain-timing/RESULT.md` › Onset rule (literature: Vos & Rasch 1981 ≈ −15 dB at normal levels,
  −7 dB only ~20 dB above threshold; Gordon 1987 ≈ −18 … −22 dB on 100-300 ms rises; Villing 2010; Polfreman 2013
  +23.5 ms SD 22.8; Collins 2006). Decision there: keep −15 dB. Evidence moderate to weak (synthetic rises ≤ 160 ms,
  nothing on real section strings with 200-300 ms rises, listeners scatter 20-60 ms).
- **Fix**: a listening test with real SSO attacks: the owner aligns clicks against slow attacks (Missing tools #7).
- **Own**: yes. Matters under plainPerformance 0 for `smoothStart`'s `attackOnset`; under plainTiming the attacks are
  spiccato (19-39 ms after note-on) and the question is moot.

### 17. Even levels: benefit doubtful outside Whence; background measurer untried with Kontakt
- **W benefit**: Whence step medians roughly halve (V1 1.70 → 0.90, Va 0.88 → 0.48, Vc 0.97 → 0.44 dB); on Heard
  passages (2-4-note mixed slurs) no reduction or worse (V2 2.32 → 3.78, Cb 4.94 → 7.12 dB) (a69ebef56a,
  `even_need.py`). Dense CC streams themselves change Kontakt's sound (+0.21 dB per note on Violas, p90 +0.49; a −17 dB
  null on an unused CC): the 5 ms CC11 glides may carry a +0.1-0.2 dB effect of their own.
- **W measurer**: never run in MuseScore with Kontakt / SSO: time per edit (est. 1.5-2.5 s a part), memory (+1 Kontakt
  per part and patch, ~85-90 MB plus samples), threaded setState, the countdown, a whole score at open; only 4 patches
  have a measured gap (rest 3 s, 3 passes); it doesn't wait for a plug-in's background load (item 12); Whence ships no
  stored values, so it plays no glide until measured; passages with a repeated note play uncorrected.
- **Src**: HANDOFF.md › Even levels; private `sso-legato-fixes/RESULT.md`.
- **Own**: whether even levels stays on (it is 1 in Recommended).

### 18. Joins across 10-measure chunks and tuning lanes; glissando / ornament velocity
- **W**: a slur crossing a chunk boundary may re-attack; joins across tuning lanes aren't made; glissando steps and
  ornament sub-notes keep plain velocity (891a142ed3).
- **Fix**: carry the join state across chunks.

### 19. Fast Celli transitions don't finish below ~300 ms a note; SSO's 500 ms transition release
- **W**: Celli Performance transitions don't complete below ~300 ms per note, in 12-TET too. SSO releases a transition
  at holds ≤ 480 ms (Violas silent; Vn1 +3.3 dB above 500 ms; Violins 1 also has an old-key release under 500 ms).
- **Fix**: none built. **Own**: yes.

### 20. DROPPED transitions; speed from written
- **W**: SSO ignores a legato note-on < 15 ms after the previous at velocity ≤ 64. `[slurs] fastTransitions` sends 65
  (0 everywhere); portamento velocities 1-19 stay dropped. Whence has 0. `[slurs] speedFromWritten` (0 everywhere,
  approved in principle; null test equal within the floor): lanes other than 0 not rendered, a slur over a repeat sends
  no CC119, other Performance patches not covered, Live carrier value 1 plays as 0, a device of protocol ≤ 10 plays key
  113 as a note, resave restore untried with real Kontakt.
- **Src**: private `cc119-null/`, HANDOFF.md › Speed from written.

### 21. Basses con sord. / espressivo leave the Performance patch
- **W**: no con sord. / Rachm. sample for the Basses: the notes fall back to All techniques Long / Spiccato
  (`choosePlain` refuses a dropped modifier). Fixed only under plainTiming 1 (12 notes in Mixed, none in Whence).
- **Fix**: move the mapping fix out of plainTiming into the default (it changes the sound: owner).

### 22. Section and technique balance: assumptions and uncertainties
- **W**: technique D (calibrated 2): staccato −3.4 dB, 90 % [−5.7, −0.9], sections disagree (V1 −0.4, Vc −6.7, Cb −3.1;
  SD 3.1); V2 / Va pooled; same D at every marking (untested at p); Basses' Bartók / col legno 0 (SSO's two metrics
  differ ~10 dB). Calibration per part, not per pitch: Horn F4 staccato mf −10.4, Flute E4 staccato −9.9, Vn D6 pizz
  +6.9 dB worst cases. Celli's Long spans ~8 dB, so Celli shorts play velocity ~95-127 at every dynamic.
  Section balance 2: horn from player 1 only (±4 dB; players disagree 14-22 dB); eight gap patches use a family member's
  level; harp −9.81 ±4, timpani −6.92 ±3, Grand Piano −1.11 ±3; celeste, mallets, bells, other percussion: SSO's own;
  Strings Ensemble +11.93 (keys SD 2.03; basses-only bottom 2-4 dB hot); pitch slope 2-4 dB off at range ends;
  woodwinds against Spheres 3-6 dB apart. Real range 2: Celli / Basses miss single markings by 1.4-3.9 dB (round
  robins 0.6-5.5 dB apart); Cor Anglais pp plays above p; flute ppp +7.4. Level rule: slurs and single notes 0.5-1 dB
  under held at mp in all five sections (unexplained). Mic compensation: power-sum error up to +1.4 dB; Mic Mix
  Distance fit 2.05 dB; timpani Close empty offline. Master trim: crest from strings only; single soft takes peak up to
  20.2 dB (trim up to ~6 dB short). MIDI out: the receiver's CC7 law unmeasured.
- **Src**: docs/BALANCE_LOG.md, docs/PLAYBACK_SETTINGS.md, HANDOFF.md.
- **Miss**: per-pitch technique curves; real data for gap instruments; review pages unread by the owner (Table II
  5W2xDamjK9vLRcJ4S6cFC2, Skålevik JKYSTkAFHEv4hjZviRhCEn, C&L 67i6bLfUGkfrkLVLWQdT2e, winds B8 1Eyi6i8tTt8wkzXaEtUZE9,
  Stage A ladder SYgoXqPuHmJpicoCRrTRpc).
- **Untried**: none of calibrated 2, sectionBalance 2, realRange 2 with music that has dynamics, micCompensation or
  divisi has been heard by the owner.

### 23. Live first-note and plain-set limits
- **W**: the plain set's Controllers clip starts at 0, so the first early note's CC1 arrives up to its start lead late
  (Whence Celli 172 ms; a plainTiming attack at the start 20-39 ms). A note at time 0: first note −0.9 dB, the legato
  after it +1.2 dB (VM simulation). Simultaneous techniques on one Kontakt and program-change switches are reported, not
  written; Bank / Sub not re-sent on a locate while stopped; a CC32 envelope is dropped at load. Volume tops at +6 dB:
  headroom × a Mixer above 100 loses up to ~4 dB. Plug-in parameter lanes up to 1 ms late. Microtones: a bending copy
  beyond its bend range plays by varispeed in MuseScore, which Live can't; many tunings can exhaust 64 routes.
- **Real Live (2026-10-22)**: a first note that starts early isn't played at all (11c); the Controllers clip's start
  values are chased (Live sends a clip envelope's value at the start), so no part lost its CC1 / CC11.
- **Src**: LIVE.md.

---

## Tier 3: small or rare

24. **Divisi level is an assumption** (10 log10 n from player counts, no data); a voice entering under a held note gives
    up to +1.8 dB over one section; a fast slurred line entering a chord would have its step flattened by even levels.
25. **Long (Rachm.)**: Violas / Celli context fits unused; quickLevel's length not modelled (held-out −3.7 dB, ~−6 at
    4-8 32nds); hold-out Violas ~+70 ms never refitted. Celli C3 bad round robin avoided only at velocity 100-116.
    Same-pitch re-attack barely audible (+0.9-1.9 dB).
26. **D6 at a fast slur's end shortened**: `PLAIN_STACCATO_GAP_MS` (45 ms) ends a note before a staccato that plays on
    another slot (Whence V1 D6 91 instead of 136 ms, heard in its release). Options: skip the gap across slots; hold a
    fast slur's last note ≥ legatoDelay + ~100 ms (private `bars9-12/SILENT.md`). Owner.
27. **Phrase break needs a gap ≥ 40 ms**; pedal timing waits for the owner's PC (his export lost 1 chord in 8);
    octave corrections to re-sweep (Oboe Solo +12, Violins 2 ±12 −30); silent sounds (Long Sul G / Sul C in Vn1 / Vn2 /
    Celli; Rain Sheet Swell mp; Tam Tam FX Scrape, Wind Gong FX Bow missed the rest check); parameters measured only in
    5 steps at the test pitch.
28. **Hosting / load** (not sound): setState 17-18 s for Whence's 8 instances, +1.3-1.7 s per tuning slot (KSP compile
    55 %, redone per slot); VM numbers aren't the owner's (SMB streaming). `load-small` fix 1 hangs Kontakt 4/4 (drop
    it); `kontakt-procs` (parallel host processes, the one real lever: 3 patches 4-7 s vs 8-14 s) never compiled with
    MSVC, no VM null test. Kontakt's own background sample loading isn't waited for either. A 0xC0000005 after a Kontakt
    "VirtualUnlock failed" while playing (lag-probe phase e) left the playback mode at MS4. Clicked notes: the first
    click on a route whose patch isn't loaded is silent. `LANE_COPY_BYTES` (245 MB) stale against 425-1017 MB measured
    (an instance; a tuning slot ~222 MB, `kontaktsetup.h`): since `all-copies` (the owner, 2026-10-21: every copy the
    music needs, a warning when memory is short) it no longer caps anything, it only sizes the memory warning
    (`SoundLib::copiesMemory`), which may come late on a machine where a copy costs more. No per-patch load size is kept
    (`SoundLibraryHost`'s `memory(slot)` is measured in the session only): keeping it per patch would make the estimate
    the patch's own. The warning is untried in the GUI with Kontakt / SSO; Create Live Set gives none (Live loads the
    copies).
29. **Update Live Set after a rewrite saves the score once more** (`liveSet.setTime` recorded before the rewrite;
    harmless). The command-line Create → Update fix (2ef9dd9dfb) is not yet re-run on the VM with AGM.
30. **MS4 / built-in**: `ab/trace/regress.sh` not run since edb5eec (and its paths are stale); MIDI export's
    same-instant order fixed 2026-10-04 but the 3.6 round trip not re-run.
31. **Playability**: microtone threshold below 200 Hz clamped at 5.9 c (from pure tones); scordatura view not saved.
32. **Tightness not offered** (2026-10-21, measured, VM kthost): the panel's Tightness 1 starts spiccato / pizzicato /
    col legno 10 ms earlier (Celli's pizzicato / col legno 15), Violins 1's Short CS 20; Brushed CS and Celli's Short CS
    don't move; level unchanged. Recommended already sends shorts early by their onset, so a setting would change the
    attack (less pre-noise), and timing only as written (Library default). Needs `onset=` re-measured at the chosen value.
    Owner. docs/PLAYBACK_SETTINGS.md › Tightness.

---

## Tier 4: tool defects (no sound, but they hide or invent problems)

31a. **The VM's Live updated itself to 12.4.7** (2026-10-22, about 08:40, while a `realcheck` run's Live was open: Live's
    automatic update, on by default): the VM no longer runs the owner's 12.4.6; its preferences moved to `Live 12.4.7\`
    (`realcheck` backs up the newest). Nothing measured here needs a Live after 12.4. **Own**: reinstall 12.4.6 and turn
    Live's automatic updates off on the VM, or keep 12.4.7. Also in Live's log: another session's control surface
    ("Tester", looking up tracks such as '7-Timps') is active in the VM's Live.

32. **Verify detector false positives on Performance legato** (private `bars9-12/SILENT.md`, build #454). **Fixed
    (the first three) 2026-10-23, branch `verify-detector`** (Missing tools #8; VERIFY.md › Detector regression): the
    offset by each note's expected latency (the audit's model) with a ±60 ms residual, else the margin and "offset
    ambiguous"; a short legato note's peak window; fractional bins; and, found on the regression, the held level as the
    mean power from 40 to 70 % (two points read the kept Celli F#3 at 32.76 s 21 dB under at a dip). Whence #454: false
    positives 2 → 0, dropped Celli notes found 2 → 3 of 4 (bar 13's Eb3 still missed). Left: the fourth point below, and
    no Windows run of the new detector yet (the next build's verify run on Whence will show it). **Build #474's run**
    (check474 item 4): the offsets right, 1 false positive (Celli bar 13's quarter-flat Ab3 on its voice copy): its
    register level, not its bins: a 4-note copy held against the main slot's short notes' peaks. Fixed 2026-10-24, branch
    `verify-detector2`: levels among notes measured the same way, a small slot's from its patch's, a held note missing at
    12 dB under held peers (set on #454 / #474: present held at worst 7.2, dropped 10.8-20.0; an assumption for other
    scores). #474: 0 findings; #454: 0 false positives, 3 of 4 dropped found. MS_VERIFY_ROUTES=1 (the heard check's
    export) now runs the detector too. Was:
    - Offset alias: `bestOffset`'s onset-flux curve is flat for soft legato parts (V2 0.060 s within 0.6 % of the
      chosen 0.335 s; Celli 1.2 %; Violas 0.1 %), so a part is read one note late: V2 C6 "24 dB under" is a false
      positive (−9.5 dB at the real offset). Fix: report the curve's margin and mark "offset ambiguous" (< ~3 % above
      any offset ≥ 50 ms away), demoting legato missing-note findings; or align on each note's expected onset (sent +
      onset) with a ±60 ms search. Anchor-only and pitch-aware variants (private `offset_anchor.py`, `offset_pitch.py`)
      weren't robust.
    - Short legato notes (< ~0.25 s) are read at len − 20 ms, before SSO's transition lands. Fix: judge on the peak over
      [on + 0.05, off + 0.25].
    - Quarter tones are measured on 12-TET bins (`finePartialBins` takes the integer pitch). Fix: pass each note's cents
      (`playbackTuning` under a `ScoreTuningScope`) into `PV::Note` and use fractional bins.
    - `--verify-playback` thresholds were tuned on one piano piece; misses wrong articulation / sample, re-strikes under
      the pedal, masked chord notes.
33. **Heard-check limits**: one take (`--takes N` runs several, `heard_summary.py` gives their spread); masked re-attacked Longs unreliable (Celli bar 11 +539 ms); kthost reads
    Performance attacks 30-60 ms later than export; Violins 1 tuned-copy Longs swell in export (+67 / +225, suspected
    the vibrato 0 flat lane); chords on one route not pitch-tracked; a whole-passages run crashed 0xC0000409 after 24/34
    routes (avoid with `HC_SPLIT=parts`); builds 98572aa7e2 / e1386f9e57 measured nothing (results void); RHYTHM
    borderline slurs (~7 / ~12 %) still to check by ear; the arrival rule disagrees with the owner's ears where the leave
    rule mostly agrees (item 2): which rule judges by default is open.
33a. **The trace's pitch under `[slurs] microtoneBends` 1** (found 2026-10-22, branch transition-midpoint): a
    respelled quarter tone's row in `predicted.tsv` (dumpEvents, `PlaybackAudit`) has the key it is sent on with its
    WRITTEN cents (Whence Violas bar 4 beat 1.75: written 52 +50, sent 53 −50, listed 53 +50: a semitone off).
    `heard_check.py` takes its target pitch from it (pitch + cents), so #464's leave / arrival readings of respelled
    notes (most of Whence's quarter tones) aim at the wrong pitch: likely the Celli "−129 ms" bound leaves (private
    `check464/RESULT.md`). Fix: write the sent cents (or the written key) in the trace. Workaround:
    `heard_midpoints.py`'s pitch dump. **Fixed** by branch microtone-next (merged 2026-10-22, c27eebc1cb: the audit's
    respelled cents); the heard check of #464 and the sweeps before it read those notes at the wrong pitch.
34. **Audit**: checks only its own model (reported 0 fails while the Violas played a triplet); with plainPerformance on
    the strings' audit is nearly vacuous; LEVEL STEP 0.69 dB still "proposed".
35. **Onset detectors**: harmonics SD 56 ms on SSO unexplained (synthetic ≤ 17); loudness reads only near-isolated
    notes (2-30 of 120-207).
36. **Tools that can't be re-run as they stand**: `qt_bends/abl.py` (not in this tree); `ab/`'s MS4 A/B and trace tools
    (stale install paths; the round-trip test moved to `tools/roundtrip/roundtrip2.py`, which runs); 13 jsons feeding the
    map have no committed producer (TOOLS.md › 6; `sso_accent_attack` has one since 2026-10-21); kthost has no source
    (Missing tools #4). (Fixed 2026-10-21: octave_measure's paths, compare_renders.py's silent-note crash,
    run_fork_tests.sh's missing Node tests, the heard check's stale VM address.)
37. **Docs drift**: CLAUDE.md says `[slurs] plainAccents` 0 everywhere (2 by default and in Recommended since
    4cec902067); its `sectionLevel2` numbers differ from PLAYBACK_SETTINGS.md (V1 −0.13 vs −0.03, V2 +1.02 vs +1.12,
    Va −7.03 vs −7.08, Vc +2.69 vs +2.56, Cb +3.45 vs +3.43, Trumpets a6 +19.67 vs +20.45); PLAYBACK_SETTINGS.md gives the
    harp as −9.81 and −10.41 and the presets table still says staccato D −4.2 (now −3.4); HANDOFF l.1320's "other
    patches ~24 dB lower" was superseded by the gain stage (a7b8520990). Check against the map and fix.

---

## Owner decisions pending

**Settings 0 by default "until the owner has heard it"**: `[slurs] plainTiming` (item 2), `speedFromWritten`
(20), `fixedTransitions` (11), `fastTransitions` (20), `[notes] transportReset` (11), `[levels] hallGuard` (11a), `[guitar] strums`, `softStrums` 0/1/2, `pinchHarmonics` (10);
`[slurs] evenTransitions` (weak proposal).

**On by default, never heard or confirmed**: calibrated 2 (B′), sectionBalance 2, realRange 2 on music with
dynamics, micCompensation, divisi, chord voices, evenLevels outside Whence, legato bend glide and settle, keepMs 40,
the Velocity / Join lanes.

**Other decisions**: the onset rule (16); LEVEL STEP 0.69 dB; CC1-technique levels without data (4); plain-timing
proposals 1-4 (HANDOFF › Mixed techniques); route set Track Delays / Utility (6); fast quarter-tone runs (5); fast
Celli slurs (19); D6 slur-end options (26); Basses' modifier mapping in the default (21); one library at a time (14);
review pages (22); RHYTHM borderline check; LIVE.md's open questions.

## Untried in real Live

Checked 2026-10-22 by `tools/live/realcheck` (LIVE.md › Checked in real Live; Live 12.4.6 / 12.4.7, builds #464 /
#467): the plain set opens, exports, saves and quits unattended; Live's save keeps every value MuseScore writes (Strings,
lanes, Whence; Ample: Live fills empty panel slots, now ignored by the read-back); Update Live Set on a set Live saved
(unchanged: writes nothing; edited: updated, reopened, exported, saved; the backup's name); the read-back of a saved set;
written clips with Track Delay and the device's delays (onsets 0 ms, levels 0.2 dB median); plainShift's Track Delay;
the gain-stage Utility; tuning slots in a multi; fixedTransitions' and speedFromWritten's states kept; parameter-lane
edges. Found: 11c, 11d (the Celli not silent: the comparison misaligned it; fixed in the tool, branch live-celli), 11e, the re-struck key (fixed, #467), Ample's empty slots (fixed, Linux only).
Still untried:
Update Live Set end to end (Manual values over the state, Configure slots, ControllerTargets, backups, inserted sends,
derived-parameter and M-Master Vol read-back); hosted slot gain with Utility and panel values; timing overrides (hash
stability, `get_all_notes_extended` cost); written clips (one-decimal Track Delay, Bank / Sub through the device,
`"Timing"` restore, group delays); the plain set's plainShift / plainTiming, start-lead CC1, voice copies, divisi,
speedFromWritten (key 113), fixedTransitions' state, fastTransitions, plainAccents' CC11; everything Ample (Create /
Update / Live plays the score, Configure panel, capo, state lanes, soft rolls); the route set in 12.2, the device from
the User Library, Add missing tracks; *Live plays the score* write speed, chase at a mid-song start, long pieces; the
whole clip-tab chain; a tempo change while playing; Live helpers; a real Live export A/B with SSO (only simulated: run
271 correlation 0.99961, residual −31.1 dB). Confirmed in Live 12.4.6: the Utility (to 0.001 dB), Tune slots, a negative
Track Delay moving MIDI To, the device's CPU fix, phrase-first notes within 3.3 ms in export.

## Untried with Kontakt / SSO (or AGM) in the MuseScore GUI

Even levels' background measurer, Play's wait and countdown, the Tools-menu job; edit-lag warm-up with Kontakt; chord
voices and divisi; calibrated 2; mic compensation (a mic changed alone after Mic Mix Distance); hosted slot gain;
fixedTransitions, fastTransitions, speedFromWritten's resave / reload; transportReset and hallGuard in MuseScore (kthost
only); transport-restart round robins (play twice, same
sample sequence); the start lead; play from a tick; `renderAgainPlaying` after a Mixer change; flat-lane dashed values
before a patch ever loaded; clicked notes; the Performance technique / Velocity / Join lanes; Ample's hosted-sync wait
(not built) and every Ample phase except the #453 / #455 / #456 / #457 offline checks. Kontakt's own window values are
never read anywhere.

---

# Missing tools (to build this month), ranked

Ranked by how many Tier 1-2 items each unlocks per day of work. Effort is for one agent session on the dev machine plus
VM time. Every tool: in the repo (no Spitfire / NI / Ample files, no owner scores, no personal paths), a `--help`, a
test on synthetic input, a row in TOOLS.md, outputs as JSON plus a short text report, VM locks honoured.

### 1. `measure_patch.py`: one-command "measure any patch" sweep: BUILT (branch `patch-sweep`, 2026-10-21)
- **Built**: `tools/soundlibraries/measure_patch.py` `plan` / `run` / `analyze` / `report` / `merge` / `all`. docs/MEASURING.md
  is the guide.
  - Note cells and transition cells. The transition grid: before attack / transition / staccato × its length × IOI ×
    CC119 speed played / written × early start.
  - ABBA takes, VM locks, watchdog and retry, resumable, WAVs deleted.
  - Per cell: perceived peak / mean, loud50, onset at −15 / −6 dB, `isolated_onset`, release, f0 leave / mid / arrive,
    and **heardRef = leave + 23.5 ms as a transition's perceptual reference**.
  - Tests: `test_measure_patch.py`.
- **Proof** (VM, Kontakt 8 + SSO, through the new kthost): the accent study re-measured, Violas - Performance, 11 pitches
  × 100 / 211 / 450 ms. The 122 − 64 perceived difference agrees with `sso_accent_attack.json`: mean |diff| 0.04 dB,
  max 0.12, r 1.000, 33 cells.
- **Still missing**:
  - B's onset after a staccato when A's release masks it (no tail extrapolation);
  - several instances in one render;
  - generalising `string_dynamics_sweep` / `crest_sweep` outputs into the generators' exact old schemas: `merge`
    gives a common schema, and the generators still read the old files;
  - a tap test for heardRef's 23.5 ms.
- **Unlocks**: 3 (first data: the transitions after an attack / a transition by length, private `patch-sweep/RESULT.md`),
  4, 9 (the accented attack's onset per section: the cells exist), 15, 19, 25, any new library.

### 2. `heard_summary.py` (built 2026-10-21, branch kb-tools)
- `tools/playbackverify/heard_summary.py` and `heard_check.sh --takes N` (TOOLS.md › 2a). Also new: every transition
  read by leave as well as arrival, `--transition-rule leave`. Left: the per-tempo drift needs bar tempi from the notes
  (no tempo map in report.json); groups (RHYTHM windows) have no part, so they are counted, not split.

### 3. Repeat-determinism checker (built 2026-10-21, branch kb-tools)
- `tools/playbackverify/repeat_check.py linux|vm|compare` and `dump_compare.py --slots` (TOOLS.md › 1). Left: an ASan /
  valgrind build to run it under; the mtest rendering a chord-voice score 10× (with the fix on branch voice-determinism).

### 4. kthost source in the repo: BUILT (branch `patch-sweep`, 2026-10-21)
- **Built**: `tools/soundlibraries/kthost/kthost.cpp`, compiled from `vst3plugin.cpp` / `kontaktsetup.cpp`.
  - Built with MuseScore under `BUILD_VST3`, installed into Windows `bin/`; or on its own with msvc-wine.
  - The VM copy: `C:\claude\ps\kt\kthost.exe` + `Qt5Core.dll`.
  - The restored binary's arguments and main options, plus state preparation (`--max-voices`, `--tune`, `--slots`,
    `--random-sources`, `--script-value`, `--prepared`), `--gain`, `--repeat`, `--report`, exit codes and
    `param` / `tr` / `vs` events.
  - Differences: docs/MEASURING.md › kthost.
- **Null test** against the restored binary (VM, SSO Violas All techniques Long, 7 notes, 20 s):
  - each binary against itself: −144 / −141 dB;
  - new against old: lag 0, level +0.003 dB, correlation 0.99999, residual −47.5 dB;
  - note-ons sample-identical; the differences start after note-offs (releases).
- **Still missing**:
  - Windows CI: run 459 compiled it but the install found no `kthost.exe`; fixed (`add_dependencies(mscore kthost)`),
    not re-run;
  - kthost2's realtime editor mode;
  - `--offsets` / `--warm` / `--from` / `--to`;
  - several instances in one process;
  - flat lanes read from a dump's `instances.txt`.

### 5. Real-Live check script: BUILT (branch `live-realcheck`, 2026-10-22: `tools/live/realcheck`, docs/TOOLS.md › 5)
- Built as `realcheck.py run <score>` (one VM job: set, Live's open / export / save, comparison with MuseScore's export
  and its floor, the set's diff, read-back, Update Live Set twice, a feature table; resumable). Results: LIVE.md ›
  Checked in real Live, items 7, 11c-11e. The original spec:
- **Spec**: `tools/live/live_check/`: `run_vm.ps1 <set.als> <out> [--export-wav] [--bars a-b]` drives Live on the VM
  desktop (scheduled task + `tscon`, as the private lc1012 / ug drivers do): opens the set, waits for Kontakt states, runs
  Live's export of the arrangement (or records the master), saves, closes; `compare.py <live.wav> <musescore.wav>
  <notes.txt>` → correlation, residual, per-note level / onset (the `--live-equivalence` metrics) and parameter-lane edge
  times (`edges.py`); a checklist run for LIVE.md's untried items (Update twice, timing overrides, written clips).
- **Effort**: 2-3 days (GUI automation is the risk). VM: one heavy job.
- **Unlocks**: 6, 7, 23, the whole "Untried in real Live" list; Ample in Live (13).

### 6. Ample per-guitar map pipeline (for the four guitars bought later)
- **Built (2026-10-21, branch `ample-pipeline`)**: docs/AMPLE_PIPELINE.md; `tools/ample/`, `tools/soundlibraries/amphost/`,
  `ample_products.py`, `ample_AGM.json`, `gen_ample.py --product`. Left: the C++ tests still count AGM's map (`ampleMap`).
- **Spec**: `tools/soundlibraries/amphost/` (sanitized amphost `info` / `render` / multi-instance, msvc-wine) and
  `ample_state.py` (st.py: read / diff / set a state); `gen_ample.py --product <code> <dump> [out]` with a per-product
  table (vst3 file, library / instrument names and ids: steel / nylon / 12-string, sample-set DBName codes, keyswitch and
  natural-harmonic ranges, slide guitar key, strings / courses, capo parameter, config folder) and measured numbers in
  `tools/soundlibraries/ample_<code>.json`; `measure_ample.py` running the per-guitar measurements: keyswitches, strummer
  bands and single-string keys, CC7 / M-Master Vol law, StrumLevel per sample set, softRoll pairs, capo block lead,
  background-load time per set (→ `backgroundLoad`), velocity layers; a check harness (the private check453 → 457 one:
  test scores, settings into the .mscz, hashes, watchdog, automatic retry for AGM's load crashes). Tests stop
  hard-coding AGMFg / AGM counts.
- **Effort**: 3-4 days for the pipeline with AGM as the regression case; then ~0.5-1 day + VM per guitar.
- **Unlocks**: 10, 12, 13, 14 (needs the owner's decision on one library vs several), the post-purchase list.

### 7. Listening-clip maker (built 2026-10-21, branch kb-tools)
- `tools/playbackverify/clips.py make` (long rests cut, clicks, A/B at one gain, splits at 20 s, JSON with the cuts)
  and `clips.py measure` (what the clip itself plays: leave and arrival per transition) (TOOLS.md › 2a). Left: the
  "tap" page (the owner taps where he hears each onset: data for the onset rule, item 16).

### 8. Verify-detector fixes as a tested tool: BUILT (branch `verify-detector`, 2026-10-23)
- **Built**: `audio/vst3/playbackverify.*` (`Note::cents` / `expected`, `offsetCurve`, `Settings::expectedOnsets` /
  `offsetMargin` / `heldMean` / `shortPeak` / `tunedBins`, `Result::offset*`), `mscore/playbackverify.cpp`
  (`expectedOnsets`: cents and latencies from the playback audit's rows; report.json / notes.tsv fields),
  `tools/playbackverify/verify_detector.py` (the replica) + `test_verify_detector.py`, `regression/whence_expect.json` +
  `whence_features.json`, `tst_soundlibrary::playbackVerifyOffset` / `ShortLegato` / `QuarterTones` / `Regression`
  (private renders). Before / after on Whence #454: false positives 2 → 0, dropped notes found 2 → 3 of 4 (item 32).
- **Spec** (as asked): in `audio/vst3/playbackverify.*`: offset margin + "offset ambiguous", expected-onset alignment ±60 ms,
  short-legato peak window, fractional (cents) bins; a Python replica (`offset_curve.py`, `legato_level.py` moved into
  `tools/playbackverify/`) and a regression set from Whence bar 8's known false positives and the Celli chord notes
  (true positives).
- **Effort**: 1-1.5 days (code change: owner rule "sound-identical" doesn't apply, it is a checker).
- **Unlocks**: 32; makes `--verify-playback` usable on Performance parts and quarter tones.

### 9. Dump compare and audit wrapper (built 2026-10-21, branch kb-tools)
- `tools/playbackverify/dump_compare.py`, `dump_and_audit.sh` (TOOLS.md › 1).

### 10. VM job runner with locks and retry (built 2026-10-21, branch kb-tools)
- `tools/vm/vmlib.py` (the address, key and lock protocol in one place; `vm.sh` for sh), `vmjob.py` (watchdog, crash
  retries, fetch, guarded delete), `kthost_batch.ps1` (TOOLS.md › 10). `heard_check.sh` and `repeat_check.py` use it.
  Left: the private batch scripts and the Ample harness still have their own ssh lines (move them when next used).

### 11. Re-runnable producers for the jsons without one; fixed stale tools (partly built 2026-10-21, branch kb-tools)
- Fixed: `octave_measure/analyze.py` / `fam.py` (paths from the command line), `compare_renders.py` (silent note),
  `run_fork_tests.sh` (every Node test, the new Python tests), the round trip (`tools/roundtrip/roundtrip2.py` +
  `portable_fork.sh`), the heard check's VM address (`tools/vm/vmlib.py`'s config). Producer: `accent_attack.py`
  (`sso_accent_attack`). Left: the other 13 (their recipes are in their own "about" keys; the private sources of
  `sso_bad_samples`, `sso_smooth_onset`, `sso_plain_performance_shift` (qt-bends' `abl.py`), `sso_performance_layers`,
  `sso_percussion_levels`, `sso_rachm_cc1_sweep`, `sso_rachm_context_match`, `sso_slur_swell` were not found on the dev
  machine), best rebuilt as `measure_patch.py` (#1) cells; Spheres' stem fetcher `rzip.py` (private accent-spheres).

### 12. Patch tone / noise analyzer: BUILT (branch `tone-check`, 2026-10-23)
- **Built**: `tools/soundlibraries/tone_check.py` (docs/MEASURING.md › Tone, TOOLS.md › 4) and measure_patch.py
  `--what …,tone --repeats N --repeat-gap s`; tests `test_tone_check.py` (synthetic faults; `MS_TONE_REAL` a real render
  that must flag nothing: 120 SSO attacks, none flagged). Left: re-find sso_bad_samples.json's Celli C3 round robin in a
  real render (no render of it is kept: one VM run of the MEASURING.md command); Ample's quiet / dark strum (10) through
  it; Stage B's tone measures by dynamic.
- **Spec**: `tools/soundlibraries/tone_check.py <wav> <notes.txt>`: per note spectral centroid, tilt, noise floor,
  click / pop detection at note-on / off, round-robin outliers (the method behind `sso_bad_samples.json`), against the
  patch's own median; feeds `avoid` and flags faulty samples automatically.
- **Effort**: 1 day.
- **Unlocks**: Stage B (tone) of the dynamics plan, bad round robins in new patches, Ample's quiet / dark strum (10).
