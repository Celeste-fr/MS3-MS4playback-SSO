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
