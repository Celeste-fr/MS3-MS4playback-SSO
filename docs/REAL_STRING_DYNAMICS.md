# Real string dynamics and the SSO section balance

Research note (2026-10-11). No playback code or default changed. Numbers: `tools/soundlibraries/real_string_dynamics.json`;
script and test: `tools/soundlibraries/real_string_dynamics.py`, `test_real_string_dynamics.py`.

**Question.** The SSO string balance M (Vn1 −3.81, Vn2 −2.66, Va −2.12, Vc +2.77, Cb +5.81 dB) is fixed, built at
CC1 64 with the real side placed at 0.4 between Weinzierl et al. 2018's single-note pp and ff ("as soft / as loud as
possible"). Does a real section's balance shift between soft and loud dynamics, and by how much?

## Data

| Source | What | Licence | Fixed gain? |
|---|---|---|---|
| Iowa MIS, pre-2011 strings | violin, viola, cello, bass; arco pp / mf / ff scales; one KM 84; anechoic; 143 files | "used for any projects, without restrictions" | stated by the site; confirmed for cello and bass (4–16 kHz gap noise equal within 0.06–0.51 dB, 7–10 dB above the 16-bit dither floor); violin and viola gaps are edited silent, so unchecked |
| Iowa MIS, 2012 strings | other players, centre mic, mono 16/44.1 | same | not stated; viola confirmed (within 0.04–0.12 dB); bass within ~3 dB (broadband only); violin and cello fail, not used |
| Weinzierl et al. 2018 (DepositOnce 5861.3) | single-note sound power pp / ff | CC BY-NC-SA 4.0 | (calibrated) |
| SSO (`sso_cc1_curves.json`, branch sso-cc1-curves) | All techniques Long, CC1 16–127 | measured by us | — |

The post-2012 Iowa files are normalized (−0.4 dB), so they carry no level. The TU Berlin anechoic Beethoven 8 (CC BY 4.0,
real section players in context) was not used: period instruments, 0.6–0.7 GB per movement with < 3 GB free disk, and
it needs score alignment. It is the next source to try.

## Spans of one player (pre-2011, loudest 50 ms, median over semitones, dB)

| | pp→mf | mf→ff | pp→ff | mf's place (0 = pp, 1 = ff) | as a MuseScore level |
|---|---|---|---|---|---|
| violin | 8.1 | 8.0 | 15.9 | 0.48 | 4950 |
| viola | 3.0 | 3.5 | 6.2 | 0.45 | 4870 |
| cello | 8.3 | 5.7 | 14.2 | 0.59 | 5220 |
| double bass | 3.6 | 6.4 | 9.8 | 0.36 | 4640 |

mf sits near the middle in dB (0.36–0.59). MuseScore's scale puts mf at 0.6 (pp 3750, mf 5250, ff 6250). By octave the
violin's mf→ff grows above C6 (11–12 dB), and the cello's and viola's spans shrink at the top. The four spans average
11.5 dB, matching Clark & Luce's ~11 dB (1965, abstract). They are 25–65 % of Weinzierl's "possible" extremes on
the same pitches: violin 29, viola 25, cello 22, bass 21 dB. Within each instrument, the shape of ff over the range
follows Weinzierl's within 2.7–3.9 dB RMS.

The second players differ a lot: the 2012 viola spans 16.5 dB against 6.9 dB, and the 2012 bass 12.4 dB against 7.7
(pitch-agnostic session medians for both). The pre-2011 viola player's narrow range is the player's, not the
instrument's.

## Balance at each dynamic (main: pre-2011 players, loudDb, anchored at Weinzierl's ff; dB to add, centred)

| | Vn1 | Vn2 | Va | Vc | Cb |
|---|---|---|---|---|---|
| pp (CC1 32) | −3.7 | −3.8 | +3.5 | −1.6 | +5.6 |
| p (48) | −3.2 | −2.8 | +2.6 | −0.5 | +4.0 |
| mp (64) | −3.2 | −2.1 | +1.5 | +0.7 | +3.1 |
| mf (80) | −3.3 | −1.9 | +0.1 | +1.3 | +3.8 |
| f (96) | −2.1 | −0.7 | −0.1 | +1.6 | +1.4 |
| ff (112) | −0.8 | +0.6 | −0.9 | +1.8 | −0.7 |
| **pp→ff change** | **+2.9** | **+4.4** | **−4.4** | **+3.4** | **−6.3** |
| fixed M | −3.8 | −2.7 | −2.1 | +2.8 | +5.8 |

**Why it moves.** From CC1 32 to 112, SSO's sections rise by Vn1 9.1, Vn2 7.6, Va 6.8, Vc 5.8 and Cb 12.4 dB. The real
players rise by violin 15.6, viola 5.9, cello 12.7 and bass 9.6 dB over the same pitches. SSO's basses grow louder
than real basses do, and its violins and celli grow less.

**How robust each change is.** The changes do not depend on the anchor. Across the variants below they range as
follows:

| Section | pp→ff change across variants |
|---|---|
| Basses | −4.4 to −12.3 dB: always over 3 dB |
| Violins 2 | +2.7 to +6.5 |
| Violins 1 | +1.1 to +4.4 |
| Celli | +0.7 to +3.8 |
| Violas | −4.9 to +4.2: the sign depends on which viola player is used |

The variants are:
- three measures (loudDb, rmsDb, LUFS);
- Weinzierl linear;
- the 2012 viola and bass;
- SNR thresholds of 10 and 30 dB;
- restricting to Iowa's own pitch range.

Bootstrapping over semitones gives ±0.5–0.7 dB, which understates the real uncertainty. Between the strings of one
player, the pp→ff span varies with an SD of 1.1–3.7 dB per pitch.

**mf against fixed M.** The difference depends on where the measured spans are anchored:

| Anchor | mf minus fixed M |
|---|---|
| Weinzierl's ff | Vn1 +0.5, Vn2 +0.7, Va +2.2, Vc −1.4, Cb −2.0 |
| pp–ff midpoint | within ±1.0 |
| pp | up to ±3.4 |

So mf is close to M; M's mf is nearly right.

**Answer.** Yes, by more than the owner's 3 dB threshold. Measured from fixed-gain single players, the SSO balance
needed for real proportions changes from pp to ff by 10.7 dB between Violins 2 and Basses (main result). Basses need
about 6 dB less at ff than at pp relative to the others (4–12 dB across variants), and the violins 3–4 dB more.

**Limits.** These are single players (one or two per instrument) playing scales of single notes out of context, with
the microphone at an unknown distance and an anechoic room. Dynamics other than pp, mf and ff are interpolated, and
the section is taken as N players adding in power. Violin and viola fixed gain rests on the site's statement only.
