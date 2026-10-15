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

## Iteration 4 (2026-10-15): the 15 gap patches swept; their real range; the markings checked in Kontakt

**Weakest link.** Iteration 2 gave the 15 patches without a section level a balance from a stand-in for SSO's side (the
dynamics check's one note at CC1 64 plus the median difference to the full sweep: SD 2.3, max 4.1 dB on the patches that
have both), and no real range at all: their markings played at SSO's own levels.

**Evidence.** The same sweep as `sso_winds_cc1_curves.json` (Windows VM, Kontakt 8.13.1 + SSO, kthost, the library's
setups: every 3 semitones over each Long's range + the test pitch, CC1 16 .. 127, 4 takes) for all 15:
`sso_gap_cc1_curves.json`. The solo strings and the three Motif patches play one sample per point; the other eight
alternate two round robins (A - B median 0.7 .. 1.5 dB). Several Longs barely rise above mp: Contrabass Tuba +0.9 dB from
CC1 64 to 127, Contrabass Trombone and Cimbassi a2 dip at CC1 80. **The marking check**: every patch with a real range
(the 28 before and these 15) rendered in Kontakt at its test pitch, the eight markings as MuseScore sends them with
realRange 2 (CC1 = velocity = the marking; CC11 = realDb less the patch's top), 4 takes: the measured steps from mp
against steps x S / 5: median RMS error 0.05 dB, 42 of 43 in order. So CC11 is a plain 20 log10 volume on the All
techniques Longs too (measured before only on the Performance patches). Cor Anglais is the exception: pp +1.2 and ff
+1.0 dB off, pp above p, its round robins 4.5 dB apart at those points against a marking step of 1.04 dB (S 5.2): one
note can't be put in order there. Solo Cello pp +1.1 dB, still in order.

**Decision.** SSO's side of the 15 patches' sectionLevel2 from their own sweep (loudDb at CC1 64, mean over every swept
semitone: Clark & Luce's levels are scales over the whole range); the stand-in stays in the file as a check
(`standInM2`). Real range (realRange 2): longLevel from the sweep; realSteps their own Clark & Luce instrument's (solo
violins 13.8, viola 15.9, cello 14.1, Motif horns 17.7, trumpets 8.8, trombones 17.0) or, an assumption marked as such,
their family's (Piccolo, Alto / Bass Flute the flute's 7.5, Contrabass Clarinet the clarinet's 8.3, Contrabass Trombone
and Cimbasso Solo / a2 the trombone's 17.0, Contrabass Tuba the tuba's 12.5; `real_range.py` GAP_FAMILY). The seven with
a database instrument also get realSpan (TU Berlin's span) and realPlay, so realRange 1 plays them too; the eight family
ones have no realSpan and realRange 1 leaves them as before (`ContextLevel::hasRealRange`: longLevel and realSpan or
realSteps).

**Before -> after** (`sectionLevel2`, dB, stand-in -> sweep): Solo Violin 1 +1.85 -> -0.10, Solo Violin 2 +0.45 ->
-0.33, Solo Viola -2.26 -> -3.64, Solo Cello -0.42 -> +3.71, Motif Horns a4 +10.71 -> +9.33, Motif Trumpets a3 +11.49 ->
+12.79, Motif Trombones a5 +9.11 -> +6.52, Piccolo -3.66 -> -4.95, Alto Flute +3.74 -> +1.24, Bass Flute -1.96 -> -3.58,
Contrabass Clarinet -2.84 -> -1.38, Contrabass Trombone -7.08 -> -3.65, Cimbasso Solo +3.12 -> +2.97, Cimbassi a2
+10.83 -> +12.92, Contrabass Tuba -9.09 -> -4.47 (within the stand-in's stated error, as expected). Every other patch
unchanged. The markings of the 15 now play as real players' (steps x S / 5 from mp).

## Iteration 5 (2026-10-15): the bass clarinet, contrabassoon and bass trombones by TU Berlin's ratio

**Weakest link.** Clark & Luce measured no bass clarinet, contrabassoon or bass trombone: these four patches took their
family member's change from sectionLevel to sectionLevel2, an assumption ("plays like its family").

**Evidence.** The TU Berlin database measured each of them and its sibling the same way (single tones, pp and ff, sound
power): their difference at mp (each placed between its own pp and ff as sectionLevel places it,
`section_balance_winds.py` mpFrac), averaged over the semitones both played: bass clarinet - clarinet +4.88 dB (MIDI
50 .. 75), contrabassoon - bassoon -4.46 (34 .. 51), bass trombone - tenor trombone +1.71 (31 .. 65). At pp / ff the
differences are +10.73 / +0.50, -4.47 / -2.70, +2.32 / +1.22: the bass clarinet's depends strongly on the dynamic (TU's
bass clarinettist's softest notes are far louder than the clarinettist's; Meyer 2009, 3.2.3.4, gives the bass clarinet's
single-note pp 59 dB against the clarinet's 57, about +2), so its value is uncertain by about +-5 dB; the other two agree
within 1.8 dB across dynamics.

**Decision.** Real side = the sibling's Clark & Luce level + that TU difference at mp (`SIBLING_TU`, "tuRatio" in
`sso_cl_section_balance.json`): one source's ratio, measured the same way for both instruments, not an assumption. Their
realSteps stay their family's (Clark & Luce has no span for them; TU's single-note extremes are not written markings).

**Before -> after** (`sectionLevel2`, dB): Bass Clarinet -5.63 -> -0.24, Contrabassoon -15.52 -> -14.70, Bass Trombone
Solo -0.12 -> +1.48, Bass Trombones a2 +3.44 -> +5.03. Every other patch unchanged.

## Iteration 6 (2026-10-15): the harp from TU Berlin; the timpani measured, not applied

**Weakest link.** The harp and the timpani had no section level: they played SSO's own level against an orchestra whose
levels are all corrected. Clark & Luce measured neither; the owner approved TU Berlin's sound power if it does better than
no correction (the coordinator's check at c6b1ec53f2: TU against Clark & Luce errs by 4.5 dB RMS after a constant offset,
no correction by 7.8).

**Evidence.** *TU's measure for a decaying tone.* The documentation (section 3) says transient sounds' squared pressure
was "time-weighted (fast)" before averaging, and decaying tones have no steady part (SteadyPart [0 0]). Recomputed from
the 32-channel recordings (`tu_transient.py`, the files read remotely, nothing stored): the fast-weighted pressure squared
averaged over the whole recording reproduces the CSV's "SoundPower in dB" for all 156 harp recordings within 0.005 dB
(without the weighting up to 0.65 dB off at pp; the fast maximum 6.8 dB off on average). That level depends on where the
recording was cut, so the comparison uses the energy (level x the recording's length, `tu_decaying.json`) and SSO's
energy over the same span after the note-on as TU's recording after its onset. *SSO* (Windows VM, Kontakt + SSO, kthost,
`sso_percussion_levels.json`): the harp (Normale) every 6 semitones MIDI 24 .. 96 at velocity 16 .. 127, 4 takes (spread
1 .. 4 dB); the timpani keys 46 .. 62. *Harp*: SSO at mp's velocity 64 less TU at mp (rule D, 3/7 of the way pp -> ff,
sectionLevel's placement where Meyer gives no figures): -118.54 dB, SD 3.25 over 13 pitches. *To the Clark & Luce
scale*: the median of (M2 + sectionLevel's D) over the nine solo winds with their own Clark & Luce instrument, -128.95
(RMS 3.29, max 6.3; over all 28 patches with both 4.3 / 9.0, the strings off by up to 9 dB: the solo winds are the
better-matched set, and single players like the harp). *Timpani*: TU has D2 only (Timpani and Pedal timpani, five series
of "6 dynamics" by mallet, the markings not named), the CSV none; SSO's keys sound an octave under their MIDI number (key
46 58.5 Hz, key 50 72.8 Hz = D2). Step 3 of 6 as mp (if the six are pp .. ff): 94.2 dB re 1 pW s (series 90.1 .. 97.2),
SSO key 50 at velocity 64: M2 +1.65.

**Decision.** Harp: `sectionLevel2` -10.41 dB, uncertainty about +-4 dB (the offset's RMS 3.3, the pitches' SD / sqrt(13)
0.9, mp's placement by rule D). No real range (no Clark & Luce span). Timpani: not applied: which of TU's six steps is mp
is unknown by a whole step (about 7 dB), more than the +1.65 correction; the measurement is in the file
(`decayingNotApplied`).

**Before -> after**: Harp none -> -10.41. Every other patch unchanged.

**Found on the way, not a balance matter.** SSO's timpani keys sound an octave under their MIDI number: a written D2
(MIDI 38) goes to a key SSO leaves silent (keys 42 .. 45 measured silent; 46 is the lowest that sounds). Not checked
further here.

## Iteration 7 (2026-10-15): the percussion section against Skaalevik 2019, no change

**Question.** Does SSO's percussion section, uncorrected, sit where an orchestra's does against its strings?

**Evidence.** Skaalevik 2019, slide 19 (review page https://claude.ai/artifact/JKYSTkAFHEv4hjZviRhCEn, the owner to
review): family totals re the whole orchestra, strings (50 players) -3 / -4 / -4 / -8, percussion (6) -11 / -8 / -6 / -11
(quiet / average / loud sessions / forte passages), so percussion re strings -8 / -4 / -2 / -3 dB (dosimetry: Leq, energy
over sessions, rests included, repertoire-dependent). SSO (same VM run): a six-player section, one hit each at mf's
velocity 80 (Timpani key 50 = D2, Bass Drum Hit, Piatti Hit, Snare 1 Hit, Tam Tam Hit, Triangle 1 Open Hit 1),
energetically summed, against the string family as the balance plays it (the five sections' Long at CC1 64, loudDb, +
their sectionLevel2, summed): 50 ms peak +1.84 dB, energy over 1 s -3.56, over the whole ring (8.5 s) -1.97. Piatti Hit
peaks 25 .. 30 dB under the other hits (not affecting the sum: bass drum, timpani and tam-tam dominate).

**Decision.** No change: Skaalevik's figures are energy (Leq), and SSO's group by energy is inside -8 .. -2 (1 s: -3.6,
at the median -3.5; the whole ring: -2.0, at the edge). Only the peak measure is outside (+1.8), and a peak is not what a
dosimeter integrates. The owner accepts about 2 .. 3 dB balance error: no refinement.

## Iteration 8 (2026-10-15): directivity by dynamic

**Weakest link.** Clark & Luce measured sound pressure in each instrument's loudest direction; turning it into sound power
used one directivity index per instrument, TU Berlin's at ff, for every marking. The 1a single-tone files hold ff only
(every one says "dynamic = ff", whatever the documentation's Table 1 implies), so pp was never checked.

**Evidence.** TU Berlin's 32-channel recordings (component 2) hold every note at pp and ff. `tu_directivity.py` read all 16
modern instruments' archives remotely (about 25 GB by HTTP range requests, nothing stored): per note each channel's mean
square over the recording's steady part, DI per receiver against the sphere's Voronoi-weighted mean, the loudest direction
the receiver with the highest mean ff DI. The method reproduces 1a's ff values within 0.21 dB at the same receiver for
all 16 (and 1a itself, re-run, gives the stored diMax of all 13 Clark & Luce instruments exactly). In that same direction
(Clark & Luce kept one microphone direction per instrument for every marking) the pp DI is lower for the brass: horn 3.24
against 9.71 dB at ff, tenor trombone 3.06 / 7.10, bass trombone 2.68 / 7.24, trumpet 4.33 / 7.39, tuba 4.02 / 6.34;
woodwinds and strings within -1.8 .. +1.2 dB (cor anglais higher at pp). pp's own loudest direction differs from ff's for
the flute, contrabassoon, horn, bass clarinet and clarinet (not used: one direction per instrument).

**Decision.** Each marking's own DI (`tu_directivity.json`, section_balance_cl.py `lw`): pp's at pp, ff's at ff, mf's
linear in dB between them at mf's place on the instrument's Table I span ((mf - pp) / S); mp from the markings' sound
power as before. The harp's TU-to-Clark & Luce offset recomputed with it (-128.35, RMS 3.54 over the nine solo winds).

**Before -> after** (`sectionLevel2`, dB): horns +3.49 (Horn Solo -2.92 -> +0.57, Horns a6 +7.46 -> +10.95, Motif Horns a4
+9.33 -> +12.82), trombones +1.77 (Trombones a6 +14.23 -> +16.00, Cimbassi a2 +12.92 -> +14.69), tubas +1.24, trumpets
+0.78 (Trumpets a6 +19.67 -> +20.45, still the loudest: S' 14.45), flutes +0.68, clarinets +0.61, bassoons +0.14, oboes
-0.05, Cor Anglais -0.97, strings -0.13 .. +0.10, Harp -10.41 -> -9.81. One brass player against one string player at pp
/ mf: +11.4 / +13.5 -> +14.8 / +14.5 dB; the Spheres dataset's whole sections (independent) +14.3: now closer.

## Investigated, no change (2026-10-15)

- **Pitch dependence.** Each patch has one offset over its range, so SSO's level against pitch stays as recorded. Linear
  slope over each patch's range (dB per octave): SSO against TU Berlin's single notes (mean of pp and ff) and Clark &
  Luce's mf parabolas (n from the instrument's standard lowest note, an assumption) agree within about ±1.5 dB per octave
  for most patches; TU and Clark & Luce differ from each other by up to 2.6 dB per octave for the brass. Only Flutes
  (SSO +3.8 / +4.4 against +5.7 and +7.4), Basses (-6.4 against -4.1 and -2.7) and Trumpet Solo (+7.6 against +5.9 and
  +3.3) deviate on the same side in both, by about 2 .. 4 dB at their range ends, within the two sources' own
  disagreement. A per-pitch correction would need its own map data and code; the data don't justify it.
- **Clarinet / trombone / tuba and flute / oboe against Spheres** (3 .. 6 dB apart at mp): no third source referees
  them per instrument at mp (TU and Meyer are single-note or forte values), and Clark & Luce shows no fault there (its
  players agree within 0.4 .. 4.2 dB); Clark & Luce stays (one method for every instrument).
- **The bassoons.** Bassoon Solo's sectionLevel2 sits 12.5 dB under Bassoons a2's, where one against two players is 3 dB:
  SSO's own levels (the solo patch 9.6 dB over the a2 at CC1 64 in the sweep, 11.7 dB in the separate dynamics check),
  so the correction is right.
