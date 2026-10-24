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
(MIDI 38) goes to a key SSO leaves silent (keys 42 .. 45 measured silent; 46 is the lowest that sounds). **Fixed
2026-10-15** (`<Instrument keyShift="12">` on the Timpani, the generator's `KEY_SHIFT`; the key sent is the written pitch
+ 12, note-on and note-off, Live's clips and the plain set follow): the existing f0 reads agree key by key (keys 50 / 55 /
58 / 62 sound 72.8 / 99 / 115.6 / 146.5 Hz = D2 / G2 / Bb2 / D3, i.e. 12 semitones under the number; the other keys'
peaks were a fifth-up partial), and a fresh kthost render on the VM (Kontakt Player 8.13.1 + SSO, velocity 80, two takes
each) gave key 50 73.2 / 73.0 Hz (D2 73.4) and key 57 109.2 / 109.2 Hz (A2 110), so a written D2 and A2 now land on them.

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

## Iteration 9 (2026-10-15): Strings Ensemble from the sections it mixes

**Weakest link.** Strings Ensemble had no section level: it played SSO's own level beside five string sections that are
all corrected (iteration 2 left it out: which sections sound at a given key wasn't known).

**Evidence.** Windows VM (Kontakt Player 8.13.1 + SSO, kthost, offline; the setups MuseScore made, unedited): Strings
Ensemble's Long and the five sections' Long at CC1 = velocity 64, every even key of each range (the ensemble's sample
zones are every 2 semitones, 24 .. 96), 4 takes (`strings_ensemble.py` -> `sso_strings_ensemble.json`). The sections'
renders reproduce `sso_cc1_curves.json` to 0.01 dB. Spitfire says only "a set of blended six-octave Ensemble patches"
(Sound On Sound's review, D. Stewart, August 2024; the SSO manual v3 / v5 lists the patch, not its make-up). Two
measures, per key:
- *The sections' own recordings in it.* Cross-correlation of the ensemble's sustain with each section's (the best of the
  two round robins' pairings, lags +-250 ms): from key 48 up the sections in it correlate 0.34 .. 0.88 against 0.12 ..
  0.36 for two different recordings (the ensemble's own round robins), and their r^2 (energy shares) add up to 0.69 ..
  1.17 (median 0.98; under 0.85 only at 48 and 58, where a faded section's r stays under the threshold): the ensemble is
  the sections' own samples mixed, nothing else. At keys 24 .. 46 two different
  recordings of the non-vibrato basses correlate up to 0.6, so r can't decide there (key 28: 0.73).
- *A harmonic-by-harmonic fit* (each harmonic's power, both channels, NNLS over the sections at the same key): error 0.14
  .. 0.64 dB from key 36 up, 0.54 .. 1.04 dB at 24 .. 34 (the ensemble's own two round robins differ by 0.5 .. 2.3), and
  every section fully in sits at the same level against its own patch: Violins 1 -13.78, Violins 2 -13.22, Violas
  -13.41, Celli -13.06 (spread 0.5 .. 2.9 dB; Violins 1 and 2 sound alike, so the fit trades them), Basses -5.71. No
  octave doubling: the power at f0/2 x 1, 3, 5 is 27 .. 51 dB under f0 x 1, 2, 3 (nothing an octave down) and the
  sections an octave up correlate <= 0.31 above key 34.

Which sections play (dB: faded against their full level; certain from key 36 up by both measures, 24 .. 34 by the fit
alone, the only section with notes there): Basses 24 .. 42, fading to 50 (-1.8 at 44, -4.5 .. -4.7 at 46 .. 50), gone
from 52; Celli 36 .. 68, fading 70 .. 80 (-3 .. -7.6); Violas from 48 (-8.4, -6.4, -4.3, -2.0 at 48 .. 54), 56 .. 82,
fading 84 .. 90 (-2.3 .. -6.9); Violins 1 and 2 from 56 (-6.8 / -5.9, then -4, -2 at 58 / 60), 62 .. 96. Every section
that has the note, in unison.

**Decision.** Per key, target = the power sum of the sections it plays, each at its Long (loudDb) + its sectionLevel2 +
how far the ensemble fades it there: the balanced sections playing what the ensemble plays. sectionLevel2 = the mean
over the 37 keys of (target - the ensemble's loudDb), on the sections' scale (`section_balance_cl.py` `ensemble`).
Real range: none (realSteps only when its sections share a span; the violas' 15.9 dB isn't the others' 13.8 .. 14.1).
Mic levels measured as `mic_levels.py` does (MIDI 42 / 60 / 78, 4 takes, each mic alone at full; the default mix the
Tree alone at 127): Close -4.07, Tree 0, Ambient -5.28, Outrigger -5.81 dB (`sso_mic_levels.json`).

**Uncertainty.** The keys' SD 2.03 dB (+7.29 .. +15.14; the mean's standard error 0.33): the basses-only bottom
(24 .. 34) needs +7.3 .. +10.4, the rest +9.5 .. +15.1, because SSO mixes its basses 7.6 dB hotter than its other
sections; with one offset the bottom plays 2 .. 4 dB over the balanced sections and keys 48 .. 54 / 88 .. 96 1 .. 3 dB
under. Variants: every section counted at full level where it is in at all +12.48; by the sustain's power (rmsDb)
instead of loudDb +11.95. The sections' own balance uncertainty (their sectionLevel2) carries over.

**Before -> after** (`sectionLevel2`, dB): Strings Ensemble none (SSO's level, about 12 dB under the balanced sections it
mixes) -> +11.93. Every other patch unchanged; Trumpets a6 (+20.45) stay the loudest (S' unchanged).

## Iteration 10 (2026-10-15): the timpani from a real orchestra's written markings

**Weakest link.** The timpani played SSO's own level and SSO's own dynamics beside an orchestra corrected from real data:
iteration 6 had measured them against TU Berlin but could not apply it (TU's D2 series name no markings).

**Evidence.** *Real side*: the Spheres dataset's Romeo and Juliet (Colibri Ensemble; arXiv 2511.21247, Zenodo 17347681,
CC BY-SA 4.0; every part recorded alone, all 23 microphones on every take). The markings read from the first edition of the
1880 version's score (Bote & G. Bock, Berlin 1881, plate 9325; Sibley Music Library, public domain): review page
https://claude.ai/artifact/R2bYpawLeH8MU2585HRoXj (checked by the owner the same day: four readings corrected, the coda's
hit count with them; the numbers below are this iteration's, Iteration 14 has the current ones). Written f: the Allegro giusto's first bars (B, three
E sixteenths, B, B, two B eighths; 8 hits) and three B eighths on pp. 19-20; written p: the coda's heartbeat (U, Moderato
assai, 77 hits: triplets, then dotted-eighth + sixteenth pairs, a quarter). The timpani's energy per hit at the main
microphone L (the passage's whole energy, rings included, over the written hits: the measure iteration 6 matched for TU
and SSO), less its seat's sweep level against the strings' (-3.64 dB). *Scale*: the strings' eight lines at their written mf
in coda bars 2-8, each line's level per player (the findings' section sizes) against Clark & Luce's one-player Lw at mf:
K = 125.29, RMS 2.31 dB (Violins II's inner parts lowest, Violins I's melody highest). The timpani per hit: p 75.11, f 98.62
dB re 1 pW s; f - p 23.51 dB. Main L + C, where both were fetched (no cello lines): p 75.56, f 98.44. One wind part at its
written pp could be fetched before Zenodo stopped answering (Flute 1, the chorale on pp. 107-108): its K is 118.0, 7.3 dB
off the strings', so players' pp and Clark & Luce's differ by about that much for the flute: a check, not used.
*SSO side*: the hit's energy over its ring (sso_percussion_levels.json, velocity 64, power mean over keys 46 .. 62: -34.25
dB FS^2 s; key 59, B2, alone -30.72). *Corroboration*: TU Berlin's five D2 series (iteration 6) from step 2 to step 5 rise
20.9 .. 24.7 dB (mean 22.5), against the Spheres' 23.5 from p to f, if TU's six steps are pp .. ff; TU's level at its step 3
(if mp) sits about 5 dB over the Spheres' mp on the same SSO keys. Meyer 2009, 3.7.1 (Burghauser & Spelda): single-stroke
extremes 67 .. 70 / 115 dB; TU: 58 .. 60 / 108 dB: the Spheres' written p .. f lies inside both.

**Decision.** `sectionLevel2` at mp (one of the three steps from p to f): real 82.95 dB re 1 pW s, M2 -5.09 dB (at p alone
-8.80, at f alone +7.11: SSO's own p .. f spans 7.6 dB against the player's 23.5). Its real range (realRange 2): realSteps
= 23.51 x 5 / 3 = 39.2 dB (the markings 7.84 dB apart), longLevel SSO's hit energy by velocity (32 / 64 / 80 / 112) less
velocity 64's at each written pitch (the key less keyShift's 12). A velocity patch: the timpani's velocity is MS4's level of
the written dynamic (`Ms4::expressionLevel`), the x realRange reads; CC11 is a plain volume on SSO's timpani (measured on the
VM today, key 59 at velocity 64: CC11 90 / 64 / 32 -2.9 / -6.4 / -12.2 dB by energy, two takes, against 20 log10's -3.0 / -6.0 / -12.0). So
the code needed no change: the map's longLevel and realSteps on the Timpani (`gen_spitfire_sso.py` REAL_RANGE_DECAYING,
from `section_balance_cl.py`'s "realRange"). Uncertainty: about +-3 dB on the level (the scale's RMS 2.3, the seat
correction, the flute check's 7 dB says how far a section's marking can sit from Clark & Luce's), about +-4 dB on the span
(one passage per marking, one player; the coda's sixteenths are pickups and may be softer than its beats).

**Before -> after**: Timpani none (SSO's level and dynamics) -> sectionLevel2 -5.09, realSteps 39.2. At mf the two together
play about as before (-5.09 + 7.84 - SSO's 2.6 from velocity 64 to 80), so iteration 7's percussion group stays inside
Skaalevik's range; p plays about 9 dB softer, f about 7 dB louder, ff about 13 dB louder than SSO's own.

## Iteration 11 (2026-10-15): the grand piano from Meyer 2009

**Weakest link.** The Grand Piano played SSO's own level: no Clark & Luce or TU Berlin figure.

**Evidence.** Meyer 2009, 3.4.2 (read from the text): two-voice scales, open lid, no pedal, ff "around 104 dB", pp "of the
order of 88 dB" sound power, single notes at pp below 65. The same kind of figure (rapid scales, every note sounded, pp /
ff) exists for the twelve instruments Meyer and Clark & Luce share (the Meyer rule's P column). mp two of the five marking
steps from pp on both sides' terms (Meyer: pp + 0.4 (ff - pp); Clark & Luce: as section_balance_cl.py). Clark & Luce's
one-player mp less Meyer's: median -3.92 dB, RMS 2.50 after it (max 5.5), 3.87 without, so the offset does better than none
(at pp it would not: RMS 3.15 against 3.01). The piano at mp: 88 + 6.4 - 3.01 (one voice of two equal ones) - 3.92 = 87.47
dB re 1 pW. SSO (Windows VM, Kontakt 8.13.1 + SSO, kthost, today): Grand Piano Direct every 6 semitones 21 .. 105 at
velocity 32 .. 112, two takes; the loudest 50 ms (loudDb, the winds' Long's measure); the mean at velocity 64 over the keys
-33.70 dB FS^2 (the dynamics check's one note at 62 said -31.7). Meyer's levels are scales' and SSO's one note's loudest 50
ms; a scale's level is its notes' onsets, so the measures agree in kind, not exactly.

**Decision.** Grand Piano `sectionLevel2` -1.11 dB, about +-3 (the offset's RMS 2.5; SSO's middle keys 45 .. 81 alone would
give -3.55). No realSteps: Meyer's 16 dB pp -> ff are in-play limits (the owner chose Clark & Luce's written spans over them,
2026-10-14), and Iowa's single notes (iteration 12) are extremes, not markings.

**Before -> after**: Grand Piano none -> -1.11. Every other patch unchanged.

## Iteration 12 (2026-10-15): mallets, celeste and piano spans from Iowa, not applied

**Question.** Can the University of Iowa Musical Instrument Samples (theremin.music.uiowa.edu; gain fixed within a session)
give the mallets' and the piano's written pp -> ff?

**Evidence.** The original MIS files at pp / mf / ff, pitch by pitch, each hit's energy over 1 s through the fast weighting
(the 50 ms peak agrees within about 1 dB); ff - pp per pitch, median (IQR, n, SD): glockenspiel 28.8 (25.2 .. 33.1, 41, 8.3),
crotales 38.7 (36.8 .. 44.2, 23, 6.3; +6.4 dB an octave), marimba yarn 18.3 (15.7 .. 21.2, 61, 4.6), cord 23.0 (SD 9.0),
rubber 21.5 (SD 6.5), vibraphone 36.4 (28.9 .. 47.2, 40, 10.1; +8.9 an octave), xylophone 26.0 / 25.3 (hard rubber /
rosewood; SD 4.1 / 5.4), piano 24.7 (21.1 .. 30.4, 28, 6.4). Iowa has no celeste, tubular bells or desk bells. The method on
strings came out 2 .. 3 dB under Clark & Luce (violin 10.7 / 13.9, cello 12.1 / 12.0 against 13.8 / 14.1), but here every
span is beyond any Clark & Luce span (5.2 .. 17.7) and Meyer's piano in-play 16 dB: Iowa's percussion pp is a softest
stroke, not a written pp. Pitch SD 4 .. 10 dB, strong slopes with pitch.

**Decision.** No realSteps for the mallets, celeste or piano (not consistent across pitches; not markings). Their balance
stays SSO's: no real source measured them as orchestral players play (marked as no data). The marimba (yarn) is the only
consistent one (SD 4.6, no slope), a candidate at most.

## Iteration 13 (2026-10-15): the bass clarinet from four independent sources, not TU's mp

**Weakest link.** The Bass Clarinet's real side was the clarinet's Clark & Luce level plus TU Berlin's bass clarinet -
clarinet difference at mp, +4.88 dB (Iteration 5): one source, and one bass clarinettist's loud pp (TU: +10.73 at pp, +0.50
at ff), so uncertain by about +-5 dB.

**Evidence** (bass clarinet - clarinet, dB; the owner checked the sources, 2026-10-15, review page
https://claude.ai/artifact/6LwMCt4X82fVsiJgZjHCz8; Iowa's and TU's numbers as in `research3/lowwinds/results.json`; ISO's
and O'Brien's are from the review page, not in that file):
- ISO 23591 Annex A forte sound power (Burghauser & Spelda via NS 8178, printed in Skaalevik 2023, Forum Acusticum,
  Table 4): -1.
- O'Brien 2013 practice-session sound power (Skaalevik 2023, Table 2): 0.
- TU Berlin's own ff: +0.5.
- University of Iowa MIS, one player, SPL at one point, same chain: mf octave 3 -1.9, octave 4 +0.5 (midpoint -0.7); pp
  +6.2 / +6.2, ff +0.5 / +2.7 (gain unverifiable: gated silence; octave 5 is lower at every dynamic, -0.8 / -3.6 / -1.3,
  and is outside the range used).
- Not counted: Detzner 2015's -2 (mean f) and scales / single-note statistics: the same TU recordings, not independent.
- Rejected: the Philharmonia samples (levels normalised per file, 5.4 dB bass clarinet pp -> ff).
Four independent sources agree within -1 .. +0.5 (Iowa's own range -1.9 .. +0.5) at forte / mf; only TU's pp, and so its mp
interpolation, sits +5 dB higher.

**Decision.** `SOURCED_RATIO` in `section_balance_cl.py`: the Bass Clarinet's ratio at mp is the median of the four,
-0.35 dB (spread -1.0 .. +0.5, uncertainty about +-2 dB with Iowa's range and its unverified gain), recorded with each
source in "ratioUsed" of `sso_cl_section_balance.json` ("tuRatio" keeps TU's). The forte / mf values stand for mp: an
assumption that the bass clarinet's mp difference lies near its mf / f difference, which TU's pp (+10.7) puts in doubt but
no independent source supports. The Contrabassoon (nothing better than TU's -4.46) and the Bass Trombones keep
`SIBLING_TU`: the bass trombone's +1.71 is corroborated by the University of Iowa's bass - tenor trombone (same day, same
chain, one player each; SPL: +1.1 pp, +1.0 mf, +2.0 ff; gain +-3 dB) and ISO 23591 Annex A (+1), within about 1 dB;
Wenmaekers & Hak 2015 (-1, A-weighted at the player's own ears) not usable.

**Before -> after** (`sectionLevel2`, dB): Bass Clarinet +0.38 -> -4.85 (the ratio +4.88 -> -0.35, so the patch's real side
5.23 dB lower and its offset 5.23 dB lower). Every other patch unchanged (map diff: one attribute of one line).
Uncertainty: about +-2 dB (was +-5). Python tests only (no build): `tst_soundlibrary`'s expected value updated, not run.

## Iteration 14 (2026-10-15): the timpani's readings corrected by the owner

**Weakest link.** Iteration 10's hit counts came from my reading of the score, unchecked. The level is an energy per hit
(the passage's whole energy over its written hits), so a wrong count moves it by 10 log10 of the ratio.

**Evidence.** The owner checked the review page (https://claude.ai/artifact/R2bYpawLeH8MU2585HRoXj; "everything else is
fine"; rhythms in jianpu, `[...]` one underline, `3(...)` a triplet). Four readings corrected: p. 17 bar 1's E figure is two
sixteenths and an eighth (was three sixteenths: still 3 hits); p. 18 bar 1 is a B and an E (was two B: still 2 hits); the
coda, p. 106 bars 1-4 and p. 107 bars 1-5, is `[3(777)7]` on every beat: a sixteenth triplet and an eighth, 4 hits a beat,
16 a bar (was 12 triplet eighths in bar 1 and dotted-eighth + sixteenth pairs after: 8 a bar). The scan at 400 dpi agrees:
bar 1's first two beats are written out (three sixteenths under a 3, beamed to an eighth), from beat 3 on the triplet is
abbreviated to one stroked note under the 3, which I had read as a dotted eighth. The recording agrees: where the onsets
are clear, four strokes about 0.2 s apart, then a gap to the next beat (1.5 s), never more than four. So the written f
passages keep 8 + 3 hits; the coda has 9 x 16 + 1 = 145 (was 77). Re-run (`spheres_timpani.py --scale-from`: the timpani
stem re-fetched from Zenodo, main microphone L; the string lines' scale K 125.29, RMS 2.31, taken from Iteration 10's output,
as no string reading changed; the f windows reproduce Iteration 10's -28.63 / -33.06 dB exactly): p 72.36 dB re 1 pW s (was
75.11), f 98.62 (unchanged), f - p 26.26 dB (was 23.51). Still inside Meyer's single-stroke extremes (pp 67 .. 70, ff 115;
TU 58 .. 60 / 108); TU Berlin's D2 steps 2 -> 5 (20.9 .. 24.7 dB, iteration 6) now corroborate less well (the Spheres' p -> f
lies 1.6 dB over their largest rise).

**Decision.** The same rules as iteration 10 on the corrected counts: real mp 81.11 dB re 1 pW s, sectionLevel2 -6.92 dB (at
p alone -11.55, at f alone +7.11); realSteps 26.26 x 5 / 3 = 43.8 dB (the markings 8.75 dB apart). Uncertainty as before:
about +-3 dB on the level, about +-4 dB on the span (one passage per marking, one player; within each beat the triplet's
strokes may be softer than the eighth).

**Before -> after**: Timpani sectionLevel2 -5.09 -> -6.92, realSteps 39.2 -> 43.8 (the map's Timpani line only). At mf
about 0.9 dB softer than iteration 10 (-6.92 + 8.75 - 2.6 against -5.09 + 7.84 - 2.6), still inside Skaalevik's range for the
percussion group; p about 11.5 dB softer than SSO's own (was 9), f about 7 dB louder (unchanged), ff about 14 (was 13).

## Iteration 15 (2026-10-24): the flutes, oboes, horns and trumpets in context

**Weakest link.** Every wind and brass level rested on Clark & Luce's scales played alone, out of context: nothing had
checked them against the strings in an orchestra playing music. The horn's level especially: one player's (Iteration 1),
the two Clark & Luce horn players 13 dB apart at mp.

**Evidence.** docs/RESEARCH.md › 1.5a (branch `research-balance`, merged into integrate-1017): two anechoic orchestra
recordings, every part recorded separately, each part's level at its written marking against the strings at the same
marking in the same piece, less what the map implies (Clark & Luce's one-player mp + realRange 2's steps):
- Aalto (Pätynen, Pulkki & Lokki 2008): Beethoven 7 / I, Bruckner 8 / II, Mahler 1 / IV; one modern player per part;
  one gain for every track (the download page); bar timing from PHENICX-Anechoic (CC BY 4.0).
- TU Berlin's Beethoven 8 (Böhm, Ackermann & Weinzierl 2020; CC BY 4.0): Orchester Wiener Akademie, period instruments,
  two players per wind part; calibrated (Table 3's gains).
- Sound power by TU Berlin's directivity toward each microphone; the median per instrument and dataset with a bootstrap
  90 % interval; pooled = the mean of the two datasets' medians.
- The score readings (about 5,300 rows, agents' readings of public-domain scores) checked by the owner on a spot-check
  page (40 drawn at random, stratified to the instruments that move: horns 9, trumpets 7, flutes 7, oboes 7, strings 6,
  clarinets 2, bassoons 2), 2026-10-24: all 40 right (r29's stray "ff" in the correction box: he confirmed "p is right").
  Sensitivity (90th percentile of the pooled value's change, 1,000 draws): at 5 % wrong (a marking from another stretch)
  horn 0.8, trumpet 0.9, flute 1.1, oboe 0.6 dB; a marking off by one step moves none by more than 0.7 dB even at 20 %.

| instrument | Aalto | TU B8 | pooled (dB) | applied |
|---|---|---|---|---|
| flute | +3.16 [+1.36, +6.59] | +4.76 [+3.89, +6.95] | +3.96 | yes |
| oboe | -1.97 [-2.81, -0.56] | -6.37 [-7.12, -3.67] | -4.17 | yes |
| clarinet | +2.72 [+0.73, +4.13] | -0.80 [-1.58, +0.14] | +0.96 | no: the datasets differ in sign |
| bassoon | +2.73 [+2.33, +3.94] | +0.67 [-0.65, +1.37] | +1.70 | no: TU's interval includes 0 |
| horn | -7.56 [-8.04, -6.70] | -9.03 [-12.07, -6.71] | -8.29 | yes |
| trumpet | -4.13 [-5.39, -0.55] | -5.06 [-6.53, -4.33] | -4.59 | yes |
| trombone | -1.00 [-3.39, +2.68] | - | (-1.00) | no: one dataset, Bruckner and Mahler 6 dB apart |
| tuba | -1.65 [-10.70, +5.17] | - | (-1.65) | no: one dataset, 4 values |

Corroboration (RESEARCH.md › 1.5a): TU's absolute levels without the strings as reference (horn -8.6, trumpet -5.5, the
strings within ±3 dB); the horn's and trumpet's offset about the same from p to ff (an offset, not a span); the in-context
horn - trumpet, -5.5 dB, inside Iteration 1's other sources' 0 .. -7.

**Decision** (the owner, 2026-10-24: "apply the full measured balance", not the half step). `anechoic_shift.py`
(`anechoic_balance.json` -> `sso_anechoic_shift.json`, read by `gen_spitfire_sso.py` as `ANECHOIC_SHIFT`, added to
`sso_cl_section_balance.json`'s M2; that file and `section_balance_cl.py` unchanged):
- Applied where both datasets measured the instrument, their medians agree in sign and each 90 % interval excludes 0. The
  rule selects exactly the owner's four (flute, oboe, horn, trumpet); the script stops if a re-measurement changes that.
- The pooled values, unrounded (+3.96 / -4.17 / -8.29 / -4.59), not the proposal's rounded +4 / -4 / -8 / -4.5: the map
  takes the measurement as it stands, and rounding moves it by at most 0.3 dB, a tenth of the ±3 dB uncertainty.
- Every patch of the instrument: one per-player offset, so solo, a2, a6 and SSO's Motif Horns a4 / Motif Trumpets a3
  (their real side is the same Clark & Luce instrument x players) move together.
- **Assumptions** (no data of their own in either recording; BALANCE_LOG's sibling convention, "plays like its family"):
  Piccolo, Alto Flute, Bass Flute take the flute's +3.96 (their Clark & Luce level is already the flute's); Cor Anglais
  the oboe's -4.17 (its Clark & Luce level is its own instrument's, unchecked in context).
- Unchanged: the strings (the centre; their own in-context values differ 1-6 dB between the datasets, no consistent
  difference), clarinets, bass / contrabass clarinet, bassoons, contrabassoon, trombones, bass trombones, Motif
  Trombones, contrabass trombone, cimbassi, tubas, harp, timpani, Grand Piano, Strings Ensemble.
- Not followed through (stated, not applied): the harp's level reaches Clark & Luce's scale by the median of (M2 + TU's D)
  over the nine solo winds (Iteration 6); with the four shifted that median moves -3.39 dB, so the harp would play 3.4 dB
  softer. It is left as it is: the harp has no in-context data, the move is inside its ±4 dB, and whether TU's single
  notes should be read against the in-context or the out-of-context wind scale is open. The Grand Piano's Meyer -> Clark &
  Luce offset (Iteration 11) would move -0.10 dB: negligible.

**Before -> after** (`sectionLevel2`, dB; map diff: these 16 attributes, nothing else):

| patch | before | after |
|---|---|---|
| Flute Solo | -2.04 | +1.92 |
| Flutes a2 | -3.87 | +0.09 |
| Piccolo (assumption) | -4.26 | -0.30 |
| Alto Flute (assumption) | +1.93 | +5.89 |
| Bass Flute (assumption) | -2.90 | +1.06 |
| Oboe Solo | -6.67 | -10.84 |
| Oboes a2 | -9.03 | -13.20 |
| Cor Anglais (assumption) | -3.86 | -8.03 |
| Horn Solo | +0.57 | -7.72 |
| Horns a2 | +7.17 | -1.12 |
| Horns a6 | +10.95 | +2.66 |
| Motif Horns a4 | +12.82 | +4.53 |
| Trumpet Solo | +9.04 | +4.45 |
| Trumpets a2 | +14.26 | +9.67 |
| Trumpets a6 | +20.45 | +15.86 |
| Motif Trumpets a3 | +13.57 | +8.98 |

Uncertainty: about ±3 dB each (the statistical interval, the DI toward one microphone ±2 dB, the two datasets' players);
one or two players per instrument; players recorded alone or in groups with headphones (their effort against a real
orchestra unknown, but it acts on strings and winds alike). The loudest patch is now Trombones a6 (+16.00), not
Trumpets a6.

**Master trim** (`ContextLevel::masterTrim`: the power sum of the sounding library notes' levels + crest, trim =
max(0, peak); `tst_liveequivalence::dumpEvents` gains.txt, the shipped calibration, the test synth's map only, no sound):
- Whence (strings only): every dump file identical before and after (trim 0.00 dB, peak -5.71 dBFS): unchanged.
- Three made-up 13-part scores (flutes, oboes, clarinets, bassoons a2, Horns a6, Trumpets a2, Tenor Trombones a2, tuba,
  strings; two bars of whole notes; not in the repo): everyone ff: trim 7.81 -> 2.81 dB; the brass ff over the rest pp:
  7.63 -> 2.24; the flutes ff over the rest pp: trim 0 both, peak -7.46 -> -9.59 dBFS (the softer horns and trumpets
  at pp outweigh the louder flutes).
- So nothing can clip more: the trim follows each score's loudest moment, and with the horns and trumpets lower that
  moment is usually lower; a passage led by flutes over a quiet orchestra peaks up to 3.96 dB higher, and the trim takes
  it if it reaches 0 dBFS. What changes is the reverse: in a score where the brass set the trim, every other patch plays
  that much louder (about +5 dB above), as the balance means.

**Tests** (Linux, the overlay over the integrate builds; the test synth, no Kontakt / SSO): `tst_soundlibrary` 147 passed,
5 skipped (`levelSectionBalance`: the 16 new values, and with the flute and Trumpets a6 under 2 S' = 15.86 - 6 = 9.86, the
flute at 1.92 - 9.86 = -7.94); `tst_liveequivalence` 80 passed, 3 skipped; `tools/run_fork_tests.sh` all passed (Node and
Python too); `check_windows_names` none. Unheard: the owner has not listened to it; nothing ran on the VM.

## Mic levels for the added patches (2026-10-15)

The mic-compensation work (`[levels] micCompensation`) measured each mic alone for the 28 patches that had a section
level then. The 15 patches given a level in iterations 2 and 4, the harp and the timpani had none, so a mic change on
them changed their balance. Measured on the Windows VM the same way (`mic_levels.py extra`, kthost, Kontakt 8 + SSO,
offline): the Long at CC1 64, or the hit at MuseScore's mf velocity 80 for the harp (keys 42 / 60 / 78) and timpani
(keys 50 / 54 / 58), 3 pitches x 4 takes, each mic alone at its full fader against the default mix (96 renders,
`sso_mic_levels.json`); decaying notes by energy, 0 .. 6 s after the note-on. All 17 have SSO's default mix the Tree
alone at 127 (silent with the Tree at 0), as the mic agent found for the others. The timpani's Close mic rendered an
empty signal (about -92 dB rms, every take the same: its samples weren't loaded offline), so it has no level and isn't
compensated (`extra` now leaves out a mic under -80 dB rms at every pitch). The percussion kits have no mic Controllers
in the map, so MuseScore never changes their mics: not measured. No sectionLevel2 changed; the map gains `micLevels`
on the 17. tst_soundlibrary checks that every patch with a sectionLevel2, the harp and the timpani have mic levels.
The Grand Piano (iteration 11 gave it a sectionLevel2) measured the same way, 4 mics, MIDI 45 / 62 / 79 at velocity 80
(Direct, pedal off; 24 renders' notes, kthost, VM, 2026-10-15): Close -1.52 (per pitch -3.71 / +3.91 / -1.52: the
close mic's balance moves with the register), Tree 0, Ambient -2.91, Outrigger -9.53 dB; the default mix the Tree alone
at 127 (silent with the Tree at 0).

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
