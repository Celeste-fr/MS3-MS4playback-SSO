# Balance log

The orchestra's level balance (`[levels] sectionBalance` 2, the map's `sectionLevel2`) and the level of each marking
(`[levels] realRange` 2, the map's `realSteps`), refined one weakest link at a time (the owner, 2026-10-15: "recursively
try to improve our current level balance until you think it's impossible to improve anymore"). The rules: real measured
data only, chosen by data quality (one method across instruments, independent corroboration, player agreement); no ear
tests; every assumption marked; a source mixed in only with a written reason; a reading of a scan on a review page before
it changes the map. One entry per iteration: the weakest link, the evidence, the decision, the numbers before and after.
How the balance is built: docs/PLAYBACK_SETTINGS.md › Section balance.

Notation: Lw sound power of one patch at mp (dB re 1 pW); "horn - trumpet" one player each, sound power.

## Iteration 1 (2026-10-15): the French horn

**Weakest link.** Trumpets a2 played 17.9 dB over Horns a2 at mp (Horn Solo `sectionLevel2` -16.46 dB, 20.4 dB under its
TU Berlin `sectionLevel`). Every other source puts one horn within 0 .. 7 dB of one trumpet.

**Evidence.**
- Clark & Luce's Fig. 2 is described as the mean over every note of every scale of both players at a marking. Table II
  (each scale as a parabola a + b n + c n², read from the scan: review page
  https://claude.ai/artifact/5W2xDamjK9vLRcJ4S6cFC2) reproduces Fig. 2 as the mean in dB over n = 0 .. N for the twelve
  other instruments within 0.05 .. 1.05 dB rms (ten within 0.5; N 33 .. 45), but not for the horn (best 3.3 dB rms).
  Fig. 2's horn follows player 2 alone (his power mean over n = 0 .. 30: mf 58.1, ff 70.8; Fig. 2 57.8, 71.0).
- The horn's players disagree by 14 .. 22 dB (Table III already lists them as the study's least consistent, 9.7 dB in level).
  Player 2's scales start at 15.4 (pp) / 33.7 (mf) dB SPL at 10 m, 20 .. 40 dB under player 1 and under every other wind
  instrument's lowest notes; player 1 played no ff.
- Horn - trumpet against other sources: pp: TU Berlin single notes -0.3, Meyer 2009 in-play -3, Spheres' quiet scales
  -4.2, Clark & Luce player 1 -6.2, Fig. 2 -16.1 (the outlier). ff / loud: TU +0.9, Meyer +3, Spheres -7.0 (main
  microphones in front: the trumpets face them, the horns face away), Fig. 2 -7.1.

**Decision.** The horn's balance takes player 1's pp and mf scales (Table II, the same mean as Fig. 2, N 34 = the median
of the other instruments' N): pp 62.5, mf 73.2, mp 69.6 dB SPL at 10 m (69.5 .. 70.9 over N 33 .. 45); Fig. 2's mp was
56.2. Still Clark & Luce, still one method; one player instead of two (the others' players differ by 0 .. 4.2 dB in
level, tuba 7.7: the horn's uncertainty is about that). `realSteps` keeps Table I's 17.7 dB: player 1's pp -> mf (10.7 dB)
is three of realRange 2's five equal steps, a pp -> ff of 17.8 dB. Tools: `clark_luce_table2.py` ->
`clark_luce_table2.json` -> `section_balance_cl.py`.

**Before -> after** (`sectionLevel2`, dB): Horn Solo -16.46 -> -3.08, Horns a2 -9.86 -> +3.52, Horns a6 -6.08 -> +7.30;
nothing else moves. Horn Solo Lw at mp 77.5 -> 90.9. Trumpets a2 over Horns a2 at mp: 17.9 -> 4.5 dB; horn - trumpet
(one player) at mp: -17.9 -> -4.5.

## Iteration 2 (2026-10-15): patches without a section level

**Weakest link.** 15 main patches had no `sectionLevel` / `sectionLevel2` and played SSO's own level, against an
orchestra whose levels are all corrected (strings centred at 0, Trumpets a6 +19.51): the Motif brass (Horns a4,
Trumpets a3, Trombones a5) about 8 .. 11 dB too quiet, Contrabass Tuba about 11 dB and Contrabass Trombone about 8 dB
too loud, Cimbassi a2 about 10 dB too quiet.

**Evidence.** Real side: Clark & Luce has the solo strings' and the Motif brass's instruments (one player + 10 log10 N,
as every other patch). SSO side: no full-range measurement exists for these patches, but the dynamics check
(`sso_sound_dynamics.json`) measured every patch's Long against CC1 at one test pitch. On the 24 patches that have both,
the dynamics check at CC1 64 + 1.50 dB (their median difference) reproduces their M2 within SD 2.27 dB (max 4.14); at the
same pitch the two measurements agree within SD 1.3 dB, so most of the spread is the patch's level changing with pitch.

**Decision.** Give the 15 patches a `sectionLevel2` by the same method, SSO side from the dynamics check (± 2.3 dB).
Real side by their own instrument (Solo Violin 1 / 2, Solo Viola, Solo Cello, Motif Horns a4 / Trumpets a3 / Trombones
a5) or, an assumption marked in the map's source, by a family member's per-player level: Piccolo, Alto Flute, Bass Flute
by the flute; Contrabass Clarinet by the clarinet; Contrabass Trombone, Cimbasso Solo, Cimbassi a2 by the (tenor)
trombone (the cimbasso: a valved contrabass trombone); Contrabass Tuba by the tuba. Checks of the family assumption
where data exist: Spheres' piccolo per player 1.2 dB over its flutes (same session); nothing for the others. Left
without: Strings Ensemble (which sections sound at its test pitch isn't known), harp, keyboards, percussion (no source
measured them as orchestral players play).

**Before -> after** (`sectionLevel2`, dB; before: none, played as 0): Solo Violin 1 +1.69, Solo Violin 2 +0.29, Solo
Viola -1.98, Solo Cello -0.56, Motif Horns a4 +10.55, Motif Trumpets a3 +11.34, Motif Trombones a5 +8.40, Piccolo -3.90,
Alto Flute +3.50, Bass Flute -2.20, Contrabass Clarinet -2.27, Contrabass Trombone -7.79, Cimbasso Solo +2.41, Cimbassi
a2 +10.12, Contrabass Tuba -10.67. Trumpets a6 stay the loudest patch (S' unchanged). One solo violin against Violins 1
(16) at mp: -12.0 dB real; played before -13.7, now -12.0 (± 2.3).

## Iteration 3 (2026-10-15): each player counted once

**Weakest link.** The tuba: its players differ by 7.7 dB (Table III), and Fig. 2's mean over scales counts the quieter
player 2 twice (he played with and without vibrato, player 1 only without). Against the strings at ff / loud, TU Berlin,
Meyer and Spheres put the tuba 11, 2 and 9 dB higher than Fig. 2 does.

**Evidence.** Players are the independent samples; a player's two styles differ by 0.3 .. 3.0 dB (Table III,
white - vibrato). Table II gives each player's mean at the N that reproduces Fig. 2 (iteration 1). Mean of the players'
means less the mean of the scales: tuba +1.37 / +1.44 / +1.12 dB (pp / mf / ff), trombone mf +0.79, clarinet -0.86 ..
-0.66, double bass, viola, English horn, cello -1.0 .. +0.15 at one marking or another; violin, flute, oboe, bassoon,
trumpet 0 (both players the same number of scales).

**Decision.** Add these differences to Fig. 2 (`clark_luce_table2.json` `playerWeight`). The spans (`realSteps`, Table I as
printed) stay: the same weighting would move them by -0.9 (tuba) .. +0.2 dB, within Table III's span disagreement.

**Before -> after** (`sectionLevel2`, dB): Tuba Solo -3.93 -> -2.36, Contrabass Tuba -10.67 -> -9.09, the trombones
+0.7, Clarinet Solo -2.84 -> -3.40, Violas -6.75 -> -7.03, every other patch within ±0.3 (the strings' centre moved
-0.16). Trumpets a6 +19.51 -> +19.67, still the loudest patch (S' 13.67).

## Investigated, no change (2026-10-15)

- **Pitch dependence.** Each patch has one offset over its range, so SSO's level against pitch stays as recorded. Linear
  slope over each patch's range (dB per octave): SSO against TU Berlin's single notes (mean of pp and ff) and Clark &
  Luce's mf parabolas (n from the instrument's standard lowest note, an assumption) agree within about ±1.5 dB per octave
  for most patches; TU and Clark & Luce differ from each other by up to 2.6 dB per octave for the brass. Only Flutes
  (SSO +3.8 / +4.4 against +5.7 and +7.4), Basses (-6.4 against -4.1 and -2.7) and Trumpet Solo (+7.6 against +5.9 and
  +3.3) deviate on the same side in both, by about 2 .. 4 dB at their range ends, within the two sources' own
  disagreement. A per-pitch correction would need its own map data and code; the data don't justify it.
- **Real range for the 15 patches of iteration 2.** They have no `longLevel` (SSO's change with CC1), so `realRange` 2
  leaves their markings at SSO's own levels. The dynamics check has their CC1 response at one pitch, with one note per
  point: against the full sweep (4 takes) of the 28 measured patches it differs by SD 1.4 .. 2.3 dB per CC1 value (it caught
  alternating round robins), as large as one marking's step (S / 5 = 1.5 .. 3.5 dB): markings could play out of order. The
  data to do it: the same sweep as `sso_winds_cc1_curves.json` for these 15 patches on the Windows VM (pitches every 6
  semitones, 8 CC1 values, 2 takes: about 7 minutes each). The VM wasn't reachable on 2026-10-15 (the dev machine's
  network moved to 192.168.4.x); it needs the VM's new address.
- **Clarinet / trombone / tuba and flute / oboe against Spheres** (3 .. 6 dB apart at mp): no third source referees
  them per instrument at mp (TU and Meyer are single-note or forte values), and Clark & Luce shows no fault there (its
  players agree within 0.4 .. 4.2 dB); Clark & Luce stays (one method for every instrument).
- **The bassoons.** Bassoon Solo's sectionLevel2 sits 12.5 dB under Bassoons a2's, where one against two players is 3 dB:
  SSO's own levels (the solo patch 9.6 dB over the a2 at CC1 64 in the sweep, 11.7 dB in the separate dynamics check),
  so the correction is right.
