# Handoff: start here

Current state of the work and what is open. Read `CLAUDE.md` first (rules, architecture map, build and test).
The dated work logs that used to fill this file (2026-09-25 … 2026-10-01: tuning, extracts, setups, dynamics,
Mixer, load times, Live integration, piano fixes, playback verification) are in `docs/HISTORY.md` Part 2; read
a section there when you need the background of that topic. Commit messages describe each step in detail.

## SSO script fixes (2026-10-13, branch `sso-script-fixes`, from reading SSO's Kontakt scripts; untried with the real library)

- **Round robins reset on every playback start**: SSO's All-techniques and single-technique patches reset their per-key
  round-robin counters at a transport start; the host reported "playing" in every block, so Kontakt saw one start per session.
  `Vst3Synth::playbackStarted` (called by `Seq::process` when the transport changes to play) / `restartTransport`: each
  plug-in gets one block (`Vst3Plugin::TRANSPORT_STOP_FRAMES`) with the playing flag off, no events, output dropped, then
  playing. Also at `beginExport` (exports, `--live-equivalence`, verify renders, even levels' library instance) and at the
  start of each `PlaybackVerifier::renderPasses` pass (the even-levels measurement passes and any repeated pass: each is a
  playback, so each starts from the same round robins). To check on the Windows VM when a build is made: play twice, the
  same repeated note should sound the same sample sequence both times (Kontakt untested here; the test synth logs the
  host's flag, `MSTESTSYNTH_TRANSPORT_LOG`, test `transportRestart`).
- **Kickstart "Roll on high vel."**: ticked in 13 one-drum metal patches (Anvil, Cymbal Hi / Lo / Med, Mark Tree, Mini Anvil,
  Rain Sheet, Rivet Cymbal, Tam Tam, Trash Metals, Triangle 1 / 2, Wind Gong), where velocity 127 plays a roll; elsewhere
  Kickstart plays 127 as 126. Map attribute `<Instrument maxVelocity="126">` (generator `ROLL_ON_HIGH_VELOCITY`; README),
  applied to every library note-on last (`MidiRenderer::libraryMaxVelocity`), so Live's clips and the plain set follow.
  A correctness fix, not an adjustment (no setting, no preset). A roll the owner wants is the patch's Roll hit.

## Remind the owner (raise these at the start of the next conversation)

- **Whence, Violin I's vibrato** (the owner, 2026-10-12: "record this so I can be reminded later"): the score's Violin I
  part has Controllers › Vibrato (CC21) at 0, non-vibrato (SSO's zones: 0-41 non-vibrato, 42-84 vibrato, 85-127 molto
  vibrato). That is why Violins 1's eighth in bar 12 sounds quiet (Long at vibrato 0 is a slow, quiet swell, measured
  15.9 dB under). The owner will raise it in Mixer › Controllers and listen. Ask whether it's done and how it sounds;
  then remove this item.
- **Violas' runs at fingered 69** (2026-10-12): `[slurs] transitions` sends the Violas' slurred notes fingered at 69;
  until then they went at 100 (map `legatoVelocity`, chosen 2026-10-07 for timing: a run's arrival SD 54 -> 39 ms,
  never judged by ear at 69). Ask the owner to listen to Whence's Violas runs in build 422; if they smear, offer a
  measured tighter velocity inside the fingered band (20-84) as an option.
- **Stage A verdict** (2026-10-12): the listening page https://claude.ai/artifact/SYgoXqPuHmJpicoCRrTRpc (the owner's new account, 2026-10-13) (db collection
  `realrange1`) A/Bs `[levels] realRange` on a ppp -> fff ladder per section; read the verdicts (ArtifactData) and put
  realRange into Recommended only for what the owner approves. A score with written dynamics is the next test (Whence has
  almost none).
- **Even levels, to hear** (2026-10-13, branch `even-levels`, build https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38387460368):
  the slur swell evened out as the owner chose in the blind test (Glide), built and **off everywhere** (`[levels]
  evenLevels` 0, Recommended too). To hear it: open a score, *Tools › Even Out Levels…* (Kontakt + SSO measure it, ~1
  min for Whence 12-TET's Celli), then Mixer › Advanced Options… › Playback adjustments › Even levels 1 (or the score's
  playbackSettings). Ask: on in Recommended? See "Even levels" below.
- **Try in the next build:** the automation headers for every part (fold at will; `( + | All )`, round Draw Mode button,
  Grid menu), the bar ruler (View > Bar Ruler), Update Live Set (Mixer > Advanced Options > Ableton Live), the MuseScore
  Link device fix (copy the new device from the build into the User Library; Live no longer stalls with 28 copies, phrase-
  first notes on time), smooth starts on time, a repeated key under a slur spiccato.

## Dynamics like a real player (the owner's decisions, 2026-10-12)

A written dynamic is one gesture (bow speed, pressure, contact point): tone and loudness change together. In SSO, CC1
(and a short's velocity) picks the recording, mostly tone; CC11 / patch volume is loudness only. Plan:
- **Stage A, loudness:** CC11 (with patch-volume headroom) makes each note as loud as the real player at its written
  dynamic: Weinzierl et al. 2018 (TU Berlin, 3_Audio_Features.csv: SoundPower per pitch at pp and ff) plus section size
  and section balance M, against SSO's measured loudness at that CC1 (sso_cc1_curves.json, after the Long calibration
  fix, branch long-calibration). CC1 and CC11 move together (hairpins ramp both); the Performance gain follows hairpins;
  shorts' velocity no longer capped silently (CC11 covers the rest). **Target: the real player's full range** (the
  owner). **ppp and fff: rule D** (the owner, 2026-10-12, after the data page
  https://claude.ai/artifact/Lcohqt9KsWsHanfD1jxxmw): TU Berlin's pp / ff ("as softly / loudly as possible") are the
  player's ppp / fff, the markings between them in 7 equal dB steps ((ff - pp) / 7 at each pitch; e.g. Celli A3 72.3 ->
  91.4 dB, 2.7 dB a step), relative to mp, where M was set. Rejected: A (beyond the measured extremes), C (ppp = pp,
  fff = ff: two pairs of markings alike), E (Edinburgh's 30 dB exceeds the cellists' measured 19.1). No source measures
  ff against fff (only pp / ff exist for strings), so the steps' sizes come from the rule; stage B adds the tone.
- **Stage B, tone:** after A ("important for realism"): CC1 / short velocity chosen so the tone matches the dynamic
  (brightness change pp -> ff of the real instrument, TU Berlin spectral centroids, against SSO's measured per CC1 /
  velocity), loudness then entirely CC11.
- Other sections than strings (woodwind / brass Performance transitions): later.

### Stage A built: `[levels] realRange` (branch `real-range`, 2026-10-12; 0 everywhere until the owner has heard it)

Design: contextlevel.h › Real range, docs/PLAYBACK_SETTINGS.md › Real range. Each string note plays by CC11
realDb = steps(written CC1) × span / 7 - SSO Long's change from CC1 64 (span: Weinzierl's ff - pp at the pitch,
smoothed, k = 5 by leave-one-out; SSO: `sso_cc1_curves.json` rmsDb), following CC1 through hairpins; a capped short's
rest by CC11; each part's largest R as its patches' volume within the level rule's / section balance's S'. Windows
build of 8ce52a7: https://github.com/Celeste-fr/MS3-MS4playback-SSO/actions/runs/38294617925 (on the VM:
`C:\claude\realrange\ms`).

Measured with Kontakt 8 + SSO on the Windows VM (`--verify-playback --verify-wav`, VM playback.ini preset Recommended,
realRange by the score's metaTag), a dynamics ladder (`tools/playbackverify/make_dynamics_ladder.py`: each section ppp
… fff held, then staccatos, at its check pitch), each section alone, the held note's sustain K-weighted, ppp → fff
(pp → ff) in dB, off / on / rule D: Violins 1 10.3 (7.7) / 31.6 (23.2) / 31.8 (22.7); Violins 2 8.7 (6.4) / 31.8
(23.3) / 31.8 (22.7); Violas 10.8 (6.7) / 27.9 (19.2) / 26.8 (19.2); Celli 8.6 (2.3) / 22.8 (12.8) / 21.3 (15.2);
Basses 15.7 (9.3) / 20.7 (12.0) / 19.7 (14.1). Every violin / viola marking within 1.0 dB of rule D. Celli and Basses
miss by up to 3.7 dB at single markings (Celli p -5.9 / mf -0.1 against -3.0 / +3.0; Basses p -6.5, mf -0.9): the
same dips are in the off render (Celli mf 2.0 dB under mp, Basses mf 2.8 under), one note per marking, so round robins
(sso_cc1_curves.json: up to ~4 dB apart at soft CC1) or the pitch, not the CC11. Staccatos follow the held notes (within
~1.5 dB of them, both ways). Mp: everything 5.6 dB quieter (S': Violins 2's R 14.2 + M -2.66 puts it at +6).
Whence (quarter tones; no written range: only mp in bar 9 Celli / Basses, every note at CC1 64): bars 5-12, on minus
off, -0.08 … -0.14 dB per section (median), the mix -0.12 (a capped Celli short's +0.10 dB of headroom); dump the same.
Audio for the owner: no-measure-bats/nmb-lvlpage/audio/realrange/ (`<section>_off|on.mp3`, mix, times.json; one gain per
pair).

Open for the owner: (1) listen to the ladder pairs and decide whether realRange goes into Recommended; (2) Whence has
no dynamics to hear it in: a score with written dynamics is needed to judge it in music; (3) with realRange the whole
mix plays ~5-6 dB quieter at mp (the violins' fff needs +18 dB over mp; Live's Volume top is +6) — fine for Kontakt,
but louder playback means the Mixer's master; (4) each edit re-renders the score once to find R while realRange is on
(a big score may lag after an edit); (5) Celli / Basses: the SSO side is measured every 3 semitones with ~4 dB round-robin
spread at soft CC1: a denser or repeated measurement at the parts' common pitches would tighten them; (6) stage B (tone).

## Before a build goes to the owner (2026-10-09)

Run the playback audit on *Whence* (the quarter-tone original, `Whence.mscz`, the standard score since 2026-10-11) with Recommended (VERIFY.md › Playback audit: `tst_playbackaudit auditScore`
with `MS_AUDIT_SCORE`) and read its report: no OVERLAP may remain, and every ARRIVAL fail and CAPPED note left must be
named in the hand-over with its reason (the owner, 2026-10-09: "why do I have to point out all of these issues?").
LEVEL STEP: values, marked against a proposed threshold (the difference limen, 0.69 dB at 60 dB SL, Jesteadt, Wier &
Green 1977) the owner hasn't approved yet.
DROPPED (fail: a legato note sent < 15 ms after the one before at velocity <= 64, which SSO drops) and LEAP (warn: a transition beyond an octave, which SSO attacks afresh) were added 2026-10-13: name any left in the hand-over.

## Timing set in Live (2026-10-11)

Branch `live-timing-overrides` (from `live-written-clips`). The owner: *Live plays the score* keeps the played timing in
its clips, "but if I manually adjust the timings in Live, it stays overridden like that". A note moved or resized in a
"MuseScore: …" clip becomes that note's override (ms after its written start / end), kept in the score (metaTag
`liveTimingOverrides`), played by MuseScore too, never moved back by the next push (the device doesn't write over a
clip edited since: `base`, protocol 10). LIVE.md › Live plays the score › Timing you set in Live has the design, rules and
what only real Live can show. Not tried in real Live yet; the route set and the plain set's clips get no edits back
from Live this way (the plain set plays an override through its played times when MuseScore writes the set again).

## Written clips in the plain set (2026-10-11)

Branch `live-written-clips` (from `claude/whence-listen-2`). The owner (2026-10-11): Live clips hold the WRITTEN notes;
the timing is applied at playback, per technique by Track Delay, the rest by the MuseScore Link device. LIVE.md › The
plain set › Written clips has the design; `libmscore/livetiming.h`, MuseScoreLink.js › Written clips.

Where played timing reached Live before: the plain set's technique clips (onset early, keepMs cap, the note before cut,
Performance joins, the same-key note-off after them, so also the switch runs' clip starts): now written. The Kontakt
track's Controllers clip: still at played times (controllers, not notes; meets the notes as before). *Live plays the
score*'s route clips, the route set and `--live-equivalence`: still played (open, below). Clip tabs: show the clip, so
the written notes now.

Step 2, measured on the Linux dev machine (`tst_liveequivalence dumpEvents` with `MS_DUMP_TIMING`, the shipped SSO map,
no Kontakt), 454 notes, Whence 12-TET, Recommended (`heldNotes/byPitch` 0), MuseScore's played times against Live:
- Track Delay alone (each technique track minus its most common early start; the median gives the same): note-ons
  equal for 377, off for 77 (all > 30 ms, 9 > 50, max 87.5 ms: capped or cut notes); note-offs off for 439 (median 26 ms,
  78 > 30 ms, max 170.5 ms: the note-off stays where written while the track moves it early, and the note before an early
  start is cut there).
- With `byPitch` 1: note-ons off for 95 (84 > 30 ms, max 95.5), note-offs 439 (max 179.5).
- Whence (quarter tones, 28 technique tracks): as Whence 12-TET by the median.
- Track Delay (the lookahead: the earliest note-on or note-off before its written place, 59.1-170.5 ms by track, most
  94.4) plus the device's delays: every note-on and note-off at MuseScore's time (0.00 ms, all three runs). Before the
  lookahead covered note-offs too, 42 note-offs (Performance re-attacks: the note before ends its gap before the next
  one's early start) came up to 52 ms late.

Open, for the owner:
- *Live plays the score* (and the route set it uses): written clips there need each route track's Track Delay, which
  Live's API can't set and those tracks are often made by hand; left played. Decide whether to change it (the route set
  could write the Track Delay; hand-made tracks would need it typed in).
- The device's side in real Live 12.4.6 (branch `link-device-fix`, 2026-10-12, the VM; LIVE.md › Create Live Set › What
  is tested): the Track Delay moves a MIDI track's MIDI To output; the patcher sees the note's own song position
  (`TIM_SEES_TRACK_DELAY` false; true had made every phrase-first Performance note late by its track's default); the
  `"Timing"` store comes back from the set (all 28 copies' records); the timing record now goes into the buffer once
  (the 1 s ring was outrun by the export). Export: phrase-first notes within 3.3 ms of MuseScore's time. Still open:
  Bank / Sub through the device, `[pipe]`'s precision in playback (only the export was recorded). The device no longer
  stalls Live with 28 copies (LIVE.md › Live plays the score › The device's load on Live).

## Section balance, `[levels] sectionBalance` (2026-10-11)

Branch `section-balance-m` (from main; section-balance's mechanism + `sso-cc1-curves`'s measurements): the owner chose
**M** (2026-10-11), on in Recommended and by default, 0 in Library default. Each string section's patches play its map
`sectionLevel` O louder against the rule's flat level: Violins 1 -3.81, Violins 2 -2.66, Violas -2.12, Celli +2.77,
Basses +5.81 dB (a real orchestra's balance at CC1 64 from Weinzierl et al. 2018's sound power and SSO's section sizes,
less SSO's measured All techniques Long at CC1 64, `sso_cc1_curves.json`, centred; `gen_spitfire_sso.py`
`SECTION_FADERS`, docs/PLAYBACK_SETTINGS.md › Section balance). A phrase plays gain + O, everything lower together by
S' = max(0, loudest gain + O - 6). Listening: the owner's blind ABX heard the basses' 1.6-3.6 dB differences soloed
(14/14), not in the full mix (5/8); M was chosen for its model. The earlier balance A′ (R's faders + the rule's S) is
replaced; branch `section-balance` keeps it.

Whence (quarter tones), Recommended, `dumpEvents` on the Linux dev machine (no Kontakt): S' 12.063 (the Celli -
Performance's slurs, G 15.293 + 2.77, set the top: Celli - Performance +6.000). Volumes on / off: Violins 1 -15.873 /
-9.293, its Performance -0.580 / +6.000; Violins 2 -14.723 / -9.293; Violas -14.183 / -9.293, Performance +1.110 /
+6.000; Celli -9.293 / -9.293, Performance +6.000 / +6.000; Basses -6.253 / -9.293. Every note's level (CC11 + volume),
on minus off, is O - 2.77 exactly, hosted (454 notes, chunked) and in the plain set (Live's clips: the same CC11 at all
454 note-ons on and off; each Kontakt track's volume O - 2.77 dB apart, tuning copies too). So the sections are M apart
and the whole mix is 2.77 dB quieter than without the balance (only the Celli keep their level). Nothing above +6, no
CC11 over 127, nothing near the -42.08 floor. Playback audit identical on and off. Not yet heard by the owner as M.

## The level rule, `[levels] context` (2026-10-10)

The owner (2026-10-10): the same written dynamic sounds equally loud in every string part and technique, judged in the
music. Branch `performance-level-rule`: SSO's five string sections' Performance Legato slurs play CC1 at the written
dynamic and one gain per slur, by volume only (CC11 and patch volumes; never CC1): gain = st_fit - pfd + 1.3 dB
(`level_rule.py`, map `contextLevel`; docs/PLAYBACK_SETTINGS.md › Level rule). Headroom: the score's largest gain G puts
the Performance patches at G - S and every other patch of the library's parts at -S, S = max(0, G - 6) (Whence: G 15.3,
Performance +6.0, the rest -9.3 dB). On in Recommended and by default, 0 in Library default.

Checked with the real Kontakt + SSO on the Windows VM (kthost renders of `dumpEvents`' routes, 2026-10-10) against the
owner's lowered R set (render + faders), loudest 50 ms per note, median, after one common offset (-1.6 dB):

| Part | Bars | Slurs (Performance) | Staccatos (All techniques) | Without the rule (all notes) |
|---|---|---|---|---|
| Violins 1 | 1-12 | +0.3 | +3.1 (2 notes) | -6.3 |
| Violins 1 | 13-16 | +6.7 | +3.1 | +3.8 |
| Violins 2 | 1-12 / 13-16 | - | +2.5 / +2.5 | +3.3 / +3.3 |
| Violas | 1-12 / 13-16 | +0.3 / -0.7 | -0.4 / -0.4 | -3.3 / -1.0 |
| Celli | 1-12 / 13-16 | -0.0 / - | -2.2 / -1.3 | -6.9 / -0.4 |
| Basses | 1-12 / 13-16 | - | -0.4 / -0.4 | +0.4 / +0.4 |

Approved by the owner on A′ (2026-10-11; HANDOFF › No Performance patches' Whence decisions). Open:
- ~~Violins 1 bars 13-16 slurs +6.7 dB against R~~: the owner approved the rule's level (A′, 2026-10-11, below).
- ~~The All techniques balance between sections~~: `[levels] sectionBalance` (above, Section balance) plays M.
  Before (Violins 1 / 2 +3.1 / +2.5, Celli -2.2 / -1.3): `[levels] calibrated`, not this rule. The measured staccato offsets (fit `a`: Violins 1 -1.5, Violins 2 -1.1, Violas +2.1, Celli +0.6,
  Basses -0.1 dB) point the other way from R's faders (violins lower, violas higher), so equalising measured level
  would move further from R. Not applied.
- Held out (the fit without a block of cells, against each cell's own measured gain; cells' take-to-take noise 0.9-1.3
  dB): RMS 1.2 / 1.9 / 1.9 / 3.2 / 3.5 dB (Violins 1 / 2, Violas, Celli, Basses) leaving out an IOI, 1.1 / 1.9 / 1.1 /
  3.6 / 4.5 leaving out the middle register; Celli and Basses pp cells break the rule themselves (4.9 / -4.8 dB).
- Live: clips, the plain set's CC11 lanes and Kontakt volumes carry the same values (`tst_liveequivalence::
  liveContextLevel`, Linux, test synth). The VM `--live-equivalence` run needs a Windows build of this branch: not run.
- A/B (dry strings, bars 1-16, one common gain): `~/MuseScore/ms3fork/uploads/levelrule/` A (rule), B (R lowered), C
  (before).

## Even levels without measuring by hand (2026-10-13, design pending two checks)

Predicting the swell from the notes failed (offline, `predict/levels`: best model r 0.66, removes 20-30 % of the slur
range against 65-80 % measured in place). The perturbation scan (`sso_perturbation_scan.json`, VM, real SSO) found a
note's level depends on ~2 notes before and 1 after (Basses ~4 before), nothing further. The owner's direction
(2026-10-13): **combine** a table of common contexts measured once (shipped) with measuring the rest locally in the
background (only the edited note +-3), every measurement saved to the user's own table for reuse. **If play is pressed
while a measurement runs: wait only when the passage being measured is in what is about to play** (a short countdown,
then play; otherwise play at once; the owner: "the third"). Checked (2026-10-13): the same exact local context (2 notes
before, 1-2 after, their pitches and velocities) gives the same level in any phrase to ~0.2 dB (`sso_context_reuse.json`,
VM, real SSO; the earlier 1.3 dB had unmatched velocities), but not transposed (+1 semitone already moves the shape
0.74 / 1.77 dB p50 / p90): pitch stays in the key. Real scores barely share contexts (offline count, Whence + Glazunov
op. 97 + Dvorak op. 22 ii: ~2,200 contexts for 90 % of 2,958 notes, 83-96 % heard once, Glazunov <-> Dvorak <= 1 %
shared). So a shipped table would almost never be hit: the design is **background measuring near edits**, its results
kept per exact context for reuse (edits and re-measures of the same score), no shipped table. Untested: whether note
length / dynamic change the shape (the scans used 136 ms at CC1 64). **No predicted stand-in** (the owner, 2026-10-13:
"you would be hearing a 'fake' version for that moment. that's not acceptable, I'd rather just wait"): a note plays its
measured correction or waits for it, never a model's guess. To shorten the wait (estimate from the VM's render speeds,
~1 s per edit): one pass instead of three, one round with CC11's measured strength (~25 % over the law) built in, only
the edited note +-3 plus a run-in, parts in parallel; both "one pass" (MuseScore's renders vary ~0.6 dB per note, the
scans 0.1-0.25) and "one round within ~1 dB" to check on the VM first. Checked (`sso_even_fast_checks.json`, VM, kthost +
SSO, MuseScore's events): a window of the edited note +-3 with 2 run-in / run-out notes matches the whole passage; one pass
or one round leaves the worst notes 0.3-0.7 dB further off; ~0.5 s of rendering per part per pass-set (4.4x real time).
**The owner (2026-10-13) chose the full method** (3 passes, 2 rounds, CC11's measured strength 1.28 built in): ~3 s per
edit, parts in parallel. Branch `even-auto` builds it.

## Even levels (2026-10-13, branch `even-levels`; `[levels] evenLevels` on in Recommended since 02614c9eb3)

Built from the listening test below (libmscore/evenlevels.h, mscore/evenlevelsjob.h, docs/PLAYBACK_SETTINGS.md › Even
levels): *Tools › Even Out Levels…* / `--even-levels <score>` renders each slurred Performance-legato passage through the
hosted plug-ins (3 passes), measures each note, fits the glide, renders again (once more above the 1 dB floor) and keeps
each note's value + a hash of its context on the note (metaTag `evenLevels`; a changed context is stale, shown in
Inspector › Note › Even level, not played). The renderer glides CC11 (Live's clips and the plain set too); each patch's
notes play its largest value lower by CC11 (never cut at 127), its volume adds it back up to +6 dB, the rest lowers every
library patch alike (`Delays::evenAll`).
Windows VM, Kontakt 8 + SSO, Recommended (realRange on), build 38387460368: the job on Whence 12-TET Celli bars 1-8 (the
passage), median slur range 2.11 -> 0.84 dB (round 1 kept); Whence (quarter tones) bars 1-8, Violins 1 / Violas / Celli
2.54 / 1.96 / 1.54 -> 0.75 / 0.65 / 0.74. **MuseScore's own playback** (`--verify-playback --verify-wav` of the corrected
score, Celli alone, `tools/soundlibraries/even_levels_check.py`): Whence 12-TET bars 2 / 4 slur range 3.6 dB uncorrected
-> 0.60 (median of 3 renders; the listening test's G 0.7); a single render 0.9-1.6: SSO's pass-to-pass noise is ~0.6 dB
per note (SD), so one hearing still varies. The first build (38382322224) cut the boosts at CC11 127 (Celli - Performance
at +6 dB by the level rule): fixed. Open: the owner's ear (then Recommended?); the whole library plays quieter by the rest
(Whence 12-TET Celli: 3.7 dB); the GUI path (Tools menu) is untried with Kontakt; correct whole scores, not just Whence.

## Transition levels: the slur swell (2026-10-12, branch `transition-levels`, measurement only)

The owner hears a swell in Whence 12-TET bar 2, Celli (`sso_slur_swell.json`) and wants it evened out eventually.
`sso_transition_levels.json` (`transition_level_scan.py`, `transition_levels_report.py`; Windows VM, Kontakt 8 + SSO,
kthost): every destination x interval (+-1..5, 7, 12) x fingered 69 / bowed 93 of the five string Performance patches
in isolation (136 ms at CC1 64 / 32 / 112, 600 ms at 64), plus 30 slurred validation phrases per section as MuseScore
plays Whence. Isolated, the interval moves a note's level by p50 0.4-0.8, p90 1.3-2.2 dB, repeatably; but in runs the
table predicts the (very repeatable, 0.2-0.3 dB pass to pass) note-to-note pattern only at r 0.45-0.63: a per-note CC11
from it doesn't narrow a slur's spread; half of it removes 10-23 % of the SD. Whence bar 2 reproduced in the runs
(sso_slur_swell's window 34.1 37.1 37.2 34.1). Open, for the owner: a correction would need the passage's own context
(render and measure in place, CC11 ramped across each transition, not stepped: the 2026-10-10 stepped try "sounds
horrible"), or accept the half-table rule as a weak option (`[slurs] evenTransitions`, off) to try by ear.
In-place listening test (branch `even-levels-test`, `even_levels_test.py`, `sso_even_levels_test.json`; VM, kthost):
Whence 12-TET bars 1-4 Celli, each note measured in context and corrected by CC11 moving as a step (S), a ramp over
each transition (R) or a glide between notes (G): bars 2 / 4 slur range 3.7 -> 1.1 / 0.8 / 0.7 dB (floor ~1.0). The
heard window lags the note-on by more than a sixteenth, so per-note values are solved against the envelope, not c.
Waiting for the owner's ear; no renderer change.

## Performance legato for the strings' slurs in Recommended (2026-10-10)

The owner (2026-10-10, after hand-building *Whence*'s Live set R): Recommended plays the five string sections' slurs on
their Performance patches. `[slurs] performance` (1 in Recommended and by default, 0 in Library default) plays a slurred
note as under staff text "performance" (staff text "non performance" opts out); it replaces `[slurs] quick`'s Long
(Rachm.) swap for those notes; the solo strings and the winds keep `[slurs] quick`. A staccato before a slur stays on All
techniques, so the slur starts from silence (an attack). With `[levels] calibrated` the Performance notes' CC1 is mapped
so they sound as loud as Long (`calibratedController`; docs/PLAYBACK_SETTINGS.md › Performance legato in Recommended).
This reverses the section below for Recommended only, and with it approves what branch `legato-pair-delays` left
waiting for the owner (the Violas - Performance transitions at velocity 100, map `legatoVelocity`; already merged).
No track delay was added. Measured on *Whence* 12-TET (Recommended, `dumpEvents`): the renderer already starts each
slur's first note (an attack, velocity 112 / 106) early by the Legato's onset, Violins 1 50.0 ms, Violas 87.5, Celli
70.5 (the map's onset at that velocity), and plays the transitions on the beat; Live's clips carry the same times. R's
-50 ms TrackDelay on a set made from these clips would count the attacks' early start twice (100 / 137.5 / 120.5 ms) and
move the transitions 50 ms early, which MuseScore doesn't. R's own clips sit on the grid, so there the -50 ms is the
only early start.

Open:
- **The playback audit has no model of a transition's arrival**: it judges Performance transitions by the attack
  onset, so on *Whence 12-TET* (Recommended) ARRIVAL reads 19 fail / 136 warn (before: 1 fail) and CAPPED 289 info (226 transitions, nearly
  all on the beat, and 63 re-attacks held back by the keep below). It needs the measured transition delays (`sso_legato_*.json`, `legatoDelay`) before
  those counts mean anything.
- **A re-attack keeps the note before** (fixed 2026-10-10, integrate-2026-10-10): a slur's last sixteenth (136 ms at
  *Whence*'s tempo) followed by a new attack on the same Performance patch sounded 1.1 ms (Violas: the attack 87.5 ms
  early, the map's reattackGap 54 ms), 11.4 ms (Celli) and 31.8 ms (Violins 1): 63 notes. `onsetEarliest` now keeps
  `[legato] keepMs` (40) after the re-attack gap: those notes sound 39.8 ms and their attacks start 42.0 ms early
  instead of 87.5 / 70.5 / 50 (predicted arrival 45.5 / 28.5 / 8 ms late; Rasch's fail is > 50). Owner to hear.
- **Quarter tones on the Performance patches** (branch `legato-bend-timing`, 2026-10-11, owner to hear): a legato
  transition's bend now glides straight from the note before's tuning to its own while SSO's own transition moves its
  pitch (map `bendGlide`: when the f0 leaves the note before / reaches the new one, by the note before's length,
  pitch-tracked from Kontakt 8 + SSO renders, `sso_legato_glide.json`), then holds; the plain set writes the bend
  envelope with hold pairs. Real renders on the VM (Kontakt 8 + SSO, kthost): slurred runs at 300 / 600 ms a note in
  tune (±10 c) 0.81-0.91 / 0.91-0.95 of each note's steady time (before: 0.14-0.15 / 0.83-0.85), the note before intact
  0.98-1.00. *Whence* (quarter tones, Recommended): the 40 quarter-tone notes the patch settles on in the real
  render were in tune 0.15 of that time before, 0.54 after (Violins 1 0.06 → 0.66, Celli 0.00 → 0.29: Kontakt smooths
  a bend over 29-80 ms, `sso_bend_smoothing.json`, measured but not compensated: an inverse overshot ~15 c in
  context). 69 of the 147 quarter-tone notes on bending copies have no steady time at all (in 136 ms sixteenths the
  patch's own f0 never settles). With `slurs/performance=0` every quarter-tone note is on a tuned copy. Owner to
  decide by ear (mp3 pairs in the dev VM's `uploads/legato-bends/`).
- **Quarter tones settle in sixteenths** (branch `legato-bend-settle`, 2026-10-11, untested with SSO): Kontakt smooths a
  bend as one pole (71-80 ms), so the glide sent as the straight path (arrive 100-130 ms at IOI 136) was heard 35-43 % of
  the step short at the next note-on. Now (map `bendSmoothing`) one value from the leave makes the heard bend reach the
  tuning at the arrival (or an earlier note-on on the lane: slurs ending on a quarter tone before a re-attack, 94 ms);
  Whence dump with the pole modelled: within 5 cents before the next note-on 139 of 140 transitions (before 5), median 80
  ms after the written time, then held (median 33 ms). Needs a real render (cents per note) and the owner's ear.
- **Levels against R**: the calibration predicts Performance over All +2.7 dB (Violins 1), +3.8 (Violas), +7.8 (Celli)
  at *Whence*'s CC1 64 (perceived curves of `Spitfire Symphony Orchestra.dynamics.json`, Long minus Performance
  Legato); the owner's R faders say +13.1, +8.6, +14.0 (Violins 1 bars 13-16: +6.6). Measured on held notes along
  velocity = CC1, one pitch; R's slurs are 136 ms sixteenths (tempo 110), and the All techniques faders also balance the
  spiccatos. The owner decides by ear; nothing was tuned.
- **Performance level in context, measured, not applied** (branch `performance-context-level`, 2026-10-10;
  `tools/soundlibraries/sso_performance_context.json`, scripts in `performance_context/`): real Kontakt + SSO renders of
  Whence-like figures (staccatos around slurs) for the five sections × dynamic 32-112 × IOI 100-500 ms × three
  registers. The gain the slurs need over their calibrated CC1 for staccatos - slurs = -1.3 dB is mostly +2 … +9 dB
  (Violins 2 up to +13, Basses about 0), rising with register and falling with IOI; CC1 127 can't give it in most cells.
  The owner chose the rule (2026-10-10): applied as `[levels] context`, above.

## No Performance patches (2026-10-06; superseded for Recommended 2026-10-10)

Superseded: since 2026-10-10 Recommended plays the strings' slurs on the Performance patches (`[slurs] performance`,
section "Performance legato for the strings' slurs in Recommended" above); Library default still plays as below.

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

**How the Performance patches work (measured 2026-10-10)**: Violins 1 - Performance on the Windows VM (real Kontakt 8 +
SSO, kthost2 in real time with the editor open, the window's Attack & Interval / Vibrato icons read from screen shots;
A4 area, CC1 64, CC11 127; review page with the crops: https://claude.ai/artifact/NNRZK4h1VPsTBGM54mqvUw). Other
sections' Performance patches not measured.
- First note of a phrase: velocity 1-9 smooth (no bite, -6 dB at ~165 ms), 10-116 spiccato (bite at 50-80 ms, then a
  swell, -6 dB at ~260 ms), 117-127 an accented attack (-20 dB at 30-40 ms; icon and sound change at 117).
- Slow transition (IOI 700): 1-19 portamento, 20-84 fingered (new pitch at ~234-266 ms), 85-127 bowed (~185 ms up,
  ~136 down). The window shows a third interval icon from 118; 85-127 sound the same.
- Legato = the next note-on before the note-off, **or a gap up to 39 ms**; 40 ms or more re-attacks (both passes).
  CC64 changes nothing. The same pitch again (20 ms gap) re-attacks. Violas - Performance (2026-10-10, E4 → E♭4, two
  passes): 39 ms still legato, 39.98 ms re-attacks, so the owner's 39.98 ms clip gaps re-attack. Same pitch (C4 112 → C4
  106, gaps 30-60 ms): the spiccato attack icon lights, but the old note still rings within 6 dB of the new one's peak, so
  the attack barely shows (level step +0.9..+1.9 dB).
- **A staccato before a slur stays off the Performance patch** (the owner, 2026-10-10, *Whence* bars 9-12 Violas, each
  slur after a staccato eighth on its pitch): moving the eighth onto Violas - Performance (velocity 112) so the slur
  start re-attacks as bars 1-8's do was rendered (real SSO) with the eighth ending 39.98 ms before (T), shortened to the
  figure notes' 96.38 ms (V), and 4.1 dB quieter by CC1 48 (W). V's attack measured clearest (2-8 kHz step +6.4 dB against
  T's +0.5), but the owner: "all of these are worse than not doing anything". The eighth stays on the All techniques
  patch and the slur starts from silence.
- **Slurs after staccatos need the Performance patch louder** (the owner, 2026-10-10, chose B): in *Whence* bars 9-12
  (renders as above; loudest 50 ms per note, median) MuseScore's own playback has the Violas slurs 2.5 dB under the
  staccatos around them (-36.6 / -34.1 dB), the set R (Violas - Performance fader +2.0, Violas -1.6, levelled over bars
  5-8) 5.0 dB (-37.1 / -32.2). The Performance patch's short slur notes are no quieter than bars 3-8's (-42 against
  -40 … -44 dB raw); the staccatos (Spiccato 110) are what's louder. Offered +2.5 / +5.0 dB on Violas - Performance; the
  owner chose +5.0 (fader +7.0: slurs as loud as the staccatos). Raises bars 1-8's Violas slurs too.
- **Performance levels move together, by lowering** (meaning, confirmed 2026-10-12: one shared gain for every
  Performance patch, the rest lowered so nothing exceeds Live's +6 dB; section balance M, 2026-10-11, adds each
  section's own offset on top, as intended) (the owner, 2026-10-10: raising only Violas - Performance "makes the
  volume unbalanced between the parts"; then chose the lowered version as "perfect on bars 1-12"): every string
  Performance track 5 dB over R against the All techniques tracks, done by lowering since Live's fader stops at +6 dB:
  Performance R - 0.7 (Violins 1 +2.3, Violas +1.3, Celli +6.0), All techniques R - 5.7 (Violins 1 -10.8, Violins 2
  -10.2, Violas -7.3, Celli -8.0, Basses -7.3). R's Celli - Performance (+6.7) and B's Violas (+7.0) were past +6.
  Measured on the page's stems (K-weighted, BS.1770): Violins 1 at most +0.6 LU over the loudest other part (bars 5, 8).
  Bars 13-16: the model "slurs about as loud as the staccatos around them" (the Violas' approved relation, median -1.3
  dB) predicted Violins 1 - Performance -1.7 dB from R; the owner heard -1.5 as "about right" (2026-10-10). In the
  lowered set that is Violins 1 - Performance -4.2 in bars 13-16 (volume automation, step in bar 11 where the track is
  silent): an exception to moving together, which the owner heard; the rule-keeping alternative (Violins 1 All
  techniques +6.5 in bars 13-16) is unheard.
- **The level rule approved on A′** (the owner, 2026-10-11): A′ = the owner's R set (*Whence 1-16 UACC lowered*, Violins
  1 13-16) with only the three Performance tracks' levels set by the rule (each note's gain 20·log10(CC11/127) + G from
  the rule's dump, plus the section's All techniques fader). Against R (B): Violins 1 -3.0 dB bars 1-12, +3.0 bars 13-16;
  Violas +0.1 / -0.9; Celli -0.1. Violins 1 solo, bars 13-16: "I think A' sounds good", "yeah I think A' sounds good":
  Violins 1 bars 13-16 keep the rule's level, no per-score override. The All techniques balance between sections: done,
  `[levels] sectionBalance` (Section balance, above: M, 2026-10-11). Still open: R's grid + -50 ms timing
  against MuseScore's per-note onsets. Fixed separately: Celli - Performance C3's bad round robin at high velocity.
- **A hand-built set must switch techniques by clip Bank / Sub**, never a CC32 clip envelope (LIVE.md: Live 12.4.6 drops
  it at load): sets R … W did, so in Live every All techniques track played Long throughout (the owner's Kontakt window,
  2026-10-10) while the renders, which send CC32 directly, played Spiccato.
- Intervals ±1..±12 transition; over an octave (±13..±24) no transition: the new note plays its own attack by its
  velocity (v5 slow, v127 fast) and the old one releases.
- Speed (pair 69 v5 → 71, three passes per IOI; passes at slow IOIs agree within 0.1-1 dB, so round robins don't blur):
  v100 (bowed) ≥ 260 ms slow form (arrival 160-185 ms), 145-230 a second form (early burst at 50-110 ms, a dip; 145-200
  differ pass to pass), ≤ 130 an early form (40-55 ms). v64 (fingered): arrival shortens smoothly 700 → 130 ms
  (130 → 60 ms arrival), ≤ 108 a later form (125-200). History doesn't matter: four notes at 70 or 500 ms before the
  same last transition give the same arrival. **The limits are fixed in ms**: host tempo 60 / 120 / 180 (kthost
  `--tempo`, `ProcessContext::tempo`; no song position sent) differ no more than passes at one tempo (≤ 3.7 dB against
  ≤ 4.2). Transitions at 30 / 50 / 64 / 84 play the same fingered sample; arrival shortens smoothly 130 ms (IOI 700) →
  75-80 (300-200), one consistent 10-15 ms step earlier between IOI 200 and 180 (same curve shifted, not a new form);
  the only distinct fast form is the later one at ≤ 108.
- CC21 = vibrato: Non-vib 0-41, Vibrato 42-84, Molto-vib 85-127 (window zones; the level follows continuously).
- CC16 = Mute (an on/off switch: window MUTE OFF at 0-64, ON at 72-127, edge 65-71 not measured): a filter on the same
  sample, 1 kHz -2, 2 kHz -6, 4 kHz -4 dB, level -1 to -3 dB (higher notes more); transition timing unchanged.
- CC20 changes nothing measurable on a freshly loaded patch (held notes, first notes, pairs at 500 / 150 ms; the window
  labels it "UACC CC#20").
- **CC32 (UACC) switches the patch to "None / NO ACTIVE TECHNIQUE"**: silent until reloaded. Never send CC32 to a
  Performance patch.
- The owner's own set (2026-10-10, recorded on the Launchkey): first notes at 63-99 (spiccato), transitions 85-100
  (bowed), a lower neighbour at 60-75 (fingered), 2-5 ms gaps (still legato), a 54 ms gap (re-attack). Its clips play
  Whence's own figures. Rendered from it (*Whence* 12-TET bars 1-4, Violas / Celli - Performance, kthost offline, real
  SSO): velocities per figure and place from clip "Kontakt 8" (neighbour below 94 / 69 / 93 / 94, above 97 / 102 / 92 /
  88; first 112), overlap 24 ms (its median). **O** all legato; **R** as the owner's other clips, each slur re-attacked
  (the note before ends 54 ms early, velocity 106: medians of the set's 9 re-attacks), so the window lights the spiccato
  attack per slur and the transition on notes 2-4. Note-to-note level SD: Violas C 1.6, O 1.2, R 1.2 dB; Celli C 0.7,
  O 1.9, R 1.5 dB; no place in the group over 1 dB (P: 2.5). As rendered quieter than C (O 4.9 / 10.9, R 4.0 / 8.7 dB);
  listening page (level-matched): https://claude.ai/artifact/HiaXmvp2m4v5GJSCLhhus8, waiting for the owner's ear.

**Performance technique per note (2026-10-10, branch `performance-techniques`)**: under staff text "performance" the
five string sections' Performance patch plays each note by technique (`libmscore/performancetechnique.h`, the map's
`attacks` / `transitions` / `reattack` / `reattackGap` / `overlap` from the owner's set above). Auto plays the
notation: a note after silence spiccato 112, a slur's first note right after another a re-attack (106, the note before
ending 54 ms earlier), notes under a slur bowed 93 (Violas 100, `legatoVelocity`) overlapping by 24 ms, an accent
accented 122 (the sections' Performance Legato also lists `longmarcato` so an accented held note stays on it; short
accented notes keep the All techniques Marcato), a portamento glissando into a note portamento 10. The Inspector
(Note › Performance attack / transition) overrides it for every selected note at once; metaTag `performanceTechniques`.
`tst_performancetechnique` (rendered events only, Violins section from the template; not heard with SSO yet). Not
covered: a chromatic / diatonic glissando's steps and ornament sub-notes (`renderAt`) keep their plain velocity; joins
across 10-measure chunks and tuning lanes.
**Velocity and Join lanes** (2026-10-10, same branch; `libmscore/notelane.h`, `mscore/notelanes.h`): in Continuous
View, under a library part's automation header, a stem per note. Velocity 1-127 over coloured bands: a Performance
note's attacks (smooth 1-9, spiccato 10-116, accented 117-127) or, joined by transition, its transitions (portamento
1-19, fingered 20-84, bowed 85-127); a note on a velocity technique (Spiccato, Pizzicato …) the dynamics' bands, each
edge the velocity a plain note gets at ppp … fff; CC1 techniques gray. Join (ms, Performance notes after one of their
patch): + the note before overlaps, − a gap; the dashed line at the map's `legatoGap` 39 (−39 still a transition, −40
re-attacks). A dragged stem is the note's own value (filled); double-click / Delete / Reset to Auto give it back; an
Inspector technique resets them. Not tried in the GUI by the owner yet; Live gets the values through the rendered
events (not checked with `tst_liveequivalence` on a Performance score).

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

- **Standard test score** (2026-10-11): the owner's *Whence*, the quarter-tone original,
  `~/MuseScore/ms3fork/test-scores/Whence.mscz` on the dev VM (not in the repo; CLAUDE.md › Building and testing);
  *Whence 12-TET* (`Whence 12-TET.mscz`, 2026-10-06's standard) only where a test needs no tuning lanes.
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
