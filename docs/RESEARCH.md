# Research base for playback values

Written 2026-10-21 (branch `kb-research`, from integrate-1017 0ab003951f). This file collects the published data and
literature behind each playback question the fork has or will have, so that a later session can set values from real
data. CLAUDE.md's rules apply: levels come from measurements of real players, chosen by data quality, with the
uncertainty stated; timing too where possible; anything without data is marked as an assumption.

**How to read it**
- Each topic gives the question as the fork meets it, the sources, the numbers they give, the data quality, what the
  fork uses now and what is missing. It ends with a **value** (what it would fix, from the open items in HANDOFF.md)
  and an **effort**.
- The **Top 15** list at the end ranks the tasks for a future session, each with its source and method.
- Every source carries a tag saying how far this research verified it:
  - **[read]**: the full text, or the table quoted, was read during this research.
  - **[abstract]**: only the abstract, a publisher page or a secondary summary was seen. Its numbers may be
    second-hand.
  - **[memory]**: the researcher's recollection only. Check it before use.
- Only the publishers' and authors' pages were consulted. No dataset was downloaded except a few small PDFs and one
  metadata CSV, all deleted.
- No copyrighted text is reproduced beyond short citations. Tables of numbers are facts quoted with their source.

**Already used by the fork** (so not re-derived here; see docs/BALANCE_LOG.md, docs/PLAYBACK_SETTINGS.md):
- *Balance and dynamics:* Clark & Luce 1965; Weinzierl et al. 2018 (TU Berlin); Meyer 2009; the Spheres dataset
  (arXiv 2511.21247); Skålevik 2019 / 2023; ISO 23591 Annex A; O'Brien 2013; Iowa MIS.
- *Timing:* Rasch 1979; Vos & Rasch 1981; Gordon 1987; Villing 2010; Polfreman 2013; Desain & Honing 2003; Honing 2006.
- *Perception:* Jesteadt, Wier & Green 1977; Glasberg & Moore 1990 / 2002.

**Owner decisions that limit what may be built from this research**:
- **Phrasing and vibrato are not automatic** (2026-10-14: "something I should manually edit in the automation curves
  anyway"; HANDOFF.md › Dynamics like a real player). The vibrato, note-shape and phrase data below are reference data
  for drawing lanes, or for checking SSO. They are not a basis for automatic rules unless the owner asks for them.
- **No listening test decides a level** (2026-10-14). The perception studies below set rules from other people's
  measured perception: an onset threshold, a JND, a tolerated asynchrony. That is data. A listening test of our own
  renders is not.
- **Stage B (tone)** is planned after Stage A (HANDOFF.md): CC1 / velocity is chosen for the real instrument's
  brightness at each dynamic, and loudness then comes entirely from CC11.

## Contents

1. Levels
   - 1.1 String playing techniques against normal bowing
   - 1.1a CC1 techniques: the 2026-10-29 search, the Philharmonia check and the SSO sweep plan
   - 1.2 Accents (>, sf, sfz, marcato, fp)
   - 1.3 Divisi and section size
   - 1.4 Dynamic ranges per instrument (after Clark & Luce)
   - 1.5 Winds, brass, percussion, harp, piano, celesta balance
   - 1.5a In-context check of the winds / brass against the strings (anechoic orchestra recordings, 2026-10-23)
   - 1.6 Directivity
   - 1.7 Tone against dynamic (Stage B)
2. Timing
   - 2.1 Perceived onset of slow attacks
   - 2.2 Ensemble synchrony and orchestral asynchrony
   - 2.3 Legato transitions in real string playing
   - 2.4 Vibrato onset
   - 2.5 Articulation durations (staccato, spiccato, legato overlap)
   - 2.5a How long real players sound short notes (measured, 2026-10-24)
   - 2.6 Rubato, phrase-final lengthening, tempo changes
3. Tone and expression
   - 3.1 Vibrato rate and width
   - 3.2 Portamento
   - 3.3 Note shapes inside long notes (messa di voce)
   - 3.4 Releases
   - 3.5 Room and reverb
4. Datasets
5. MuseScore 4.7.5's note model
6. Top 15 research and measurement tasks

---

## 1. Levels

### 1.1 String playing techniques against normal bowing

**Question.** At the same written dynamic, how much louder or softer is each technique than normal bowing (the Long)?
The techniques are sul tasto, flautando, sul ponticello, col legno (battuto / tratto), natural and artificial harmonics,
con sordino, pizzicato, Bartók pizzicato, tremolo and trills, each for violins, violas, celli and basses. The map's
`<Articulation heldOffset>` (calibrated 2) holds the answer per technique. SSO's CC1 techniques keep the library's own
balance.

The Mixed techniques heard check (VM, build 457, 2026-10-21; private/mixed-techniques/RESULT.md) measured SSO itself,
each technique against the Performance attacks at mf:

| technique | SSO, against the Performance attacks |
|---|---|
| sul tasto | −19.5 .. −53.5 dB |
| flautando | −16 .. −31 dB |
| harmonics | −4 .. −31 dB |
| col legno | −8 .. −19 dB (Basses +3.5) |
| Bartók | +1 .. +12 dB |

No real-player figure was found then. HANDOFF.md › Plain timing, open item (3).

**Sources and numbers**

*Physics of the bowed string.* These bound what a technique can do to the level:
- **Askenfelt, A. (1989).** "Measurement of the bowing parameters in violin playing. II: Bow–bridge distance, dynamic
  range, and limits of bow force." *JASA* 86(2), 503–516, doi:10.1121/1.398230. [abstract; via Guettler 2010, Vienna
  Talk, https://viennatalk.mdw.ac.at/papers/Pap_01_20_Guettler.pdf]
  - Two professional violinists.
  - p strokes about 42 mm from the bridge at 0.4–0.5 N; f strokes 18–20 mm at 1.7–2.3 N; bow speed 21–29 cm/s.
  - The players' p → f swing was about 9 dB.
  - Bow speed and bow–bridge distance set the level; bow force sets mainly the timbre.
- **Helmholtz-motion theory** (Cremer, *The Physics of the Violin*, MIT Press 1984; Schelleng, J. C. (1973), "The bowed
  string and the player", *JASA* 53, 26–41). [memory]
  - The bridge force scales with v_bow / β, where β is the bow–bridge distance as a share of the string length.
  - Doubling the distance at the same bow speed gives −6 dB.
- **Derived here, an estimate, not a measurement:**
  - Sul tasto at 55–80 mm against an mf contact point near 30 mm gives about −5.5 to −8.5 dB at equal bow speed.
  - Askenfelt's players already bow near 42 mm at p, so sul tasto at p lies only about −3 to −6 dB under p.
  - Players bow faster for flautando, which cancels part of the drop.
  - **Physics and practice bound a sustained sul tasto / flautando at about −3 to −9 dB under ordinario at the same
    written dynamic.** SSO's −16 .. −54 dB lies outside every bound found.
- **Guettler, K.** "The violin bow in action – 'A sound-sculpturing wand'", CISM lecture notes, about 2006
  (https://knutsacoustics.com/files/The-Sound-Sculpturing-bow-CISM_7opl67i1.pdf). [read]
  - Simulated bridge force, violin G3, at the same bow speed:
    - ponticello (β = 1/24, low force): −11 dB RMS
    - "harmonic" 2nd-mode motion: −8 dB. This is not a flageolet; don't use it for harmonics.
    - raucous: +12 dB
    - anomalous low frequencies: +15 dB
  - Real players press harder near the bridge, so a written sul pont. may land anywhere from about −6 to 0 dB. That
    range is an assumption.
- **Schoonderwaldt, E. (2009).** "The violinist's sound palette: spectral centroid, pitch flattening and anomalous low
  frequencies." *Acta Acustica u. Acustica* 95(5), 901–914. [abstract] Bowing machine. Bow force controls the centroid;
  bow–bridge distance has a marginal effect. No level table seen.

*Mutes:*
- **da Silva Evangelista, M. V. & Freire, S. (2025).** "Audio analysis of the effects of heavy violin practice mutes:
  spectral and loudness changes." *Acta Acustica* 9, 45, doi:10.1051/aacus/2025029, CC BY 4.0. [abstract; the
  publisher refused automated access]
  - LTAS and loudness of performance and practice mutes.
  - Heavy mutes (over 35 g) take energy out of the low region.
- **The same authors (2022)**, conference paper on violin-mute construction
  (https://musica.ufmg.br/sfreire/wp-content/uploads/sites/13/2022/09/marcus-vinicius-freire-fia-2022.pdf). [abstract;
  search snippet] Loudness: unmuted 34.18 sone; small ebony mute 27.33 sone (−3.2 phon); large ebony mute 23.49 sone
  (−5.4 phon).
- **da Silva Evangelista, M. V. (2019)**, master's dissertation, UFMG, hdl 1843/32235
  (https://repositorio.ufmg.br/handle/1843/32235). [abstract] Three mutes on **six violins**, RMS level and LTAS. This is
  the only multi-instrument mute data found; the numbers were not reachable.
- **Shar Music blog**, "Mutes: what difference do they make?" (one violin, room dBA, method not stated). [read]
  - unmuted 73; Tourte 69 (−4); ebony 68 (−5); rubber practice 68 (−5); metal practice mute 58 (−15).
  - Retailer quality.
- **Horne, E. P. (1937)**, *Proc. Iowa Acad. Sci.* 44, 160. [abstract] Mutes cut the upper partials unevenly, and not
  the same on every string.
- **Elie, B. et al. (2014)**, *JASA* 136, doi:10.1121/1.4892762. [abstract] The mechanism: a mute lowers the bridge
  mobility, with most of the loss in the bridge hill around 1.8–3 kHz.
- **Consensus:** an orchestral mute takes the violin about **−3 to −5 dB** (A-weighted or phon); heavy practice mutes
  −10 to −15. One violin per source, informal methods. **Nothing measured for viola, cello or bass.**

*Pizzicato:*
- Meyer 2009 ch. 3 gives −7 .. −9 dB of sound power (already used).
- OrchideaSOL has `pizzicato_l_vib` / `pizzicato_secco` at pp / mf / ff for all four instruments: a second source.

*Col legno, Bartók, harmonics, tremolo, trills:* **no measured level data found anywhere.**

**Dataset that covers most of the gap.** OrchideaSOL (§4). It has for Vn, Va, Vc and Cb:
- ordinario at pp / mf / ff
- `sul_ponticello` and `sul_ponticello_tremolo` at mf
- `sul_tasto_tremolo` at mf: no plain sul tasto, but against `tremolo` mf it gives a sul tasto offset
- `col_legno_battuto` / `col_legno_tratto` at mf
- `pizzicato_bartok` at ff
- `artificial_harmonic` (+ tremolo) at mf
- `tremolo` at pp / mf / ff
- `sforzato` at f / fp
- the Sordina folder (ordinario with mute) at pp / mf / ff, and Piombo (a lead practice mute) at mf

No natural harmonics, no trills. The technique list was read from the CC BY metadata CSV (downloaded, then deleted).

Two caveats on its levels:
- **The gains are approximate.** The proximity mic's gain was changed during the sessions. The string families' gains
  were "inferred family-wise" (Cella et al. 2020). A technique recorded in another session, such as the mute folders,
  may carry an unknown gain step.
- **One player per instrument**, and a close mic that colours each technique, so use perceived loudness, not RMS.

**Data quality.**
- The physical bounds are solid but indirect.
- The mute figures are consistent but informal.
- OrchideaSOL is one player per instrument with inferred gains: corroboration grade, unless its ordinario pp → ff spans
  agree with Clark & Luce's (a gain-consistency check, §6 task 1).

**Fork now.**
- heldOffset is set for bowed shorts (−3.4), Short 1.0 (+0.3), pizzicato (Meyer) and the Basses' Bartók / col legno (0).
- Every other technique plays SSO's own balance: sul tasto −16 .. −54 dB.

**Missing.**
1. Real players' sul tasto / flautando at a written dynamic.
2. Harmonics, col legno, Bartók, tremolo, trills against ordinario.
3. Mute effect on viola, cello, bass.
4. Section data for any of them.

Experiments, in order of effort:
- **(a)** OrchideaSOL technique − ordinario, per pitch and dynamic, in the fork's loudness metric (`loudness.py`'s STL),
  after the gain check.
- **(b)** Scores of the multitrack sets with these markings: the Spheres dataset's Romeo and Juliet / K. 550 (con sord.
  passages?), Aalto's Mahler 1 / IV (sul tasto, col legno, con sord. are common in Mahler), TU Berlin's Beethoven 8 (few
  techniques). Measure the marked notes against their unmarked neighbours at the same written dynamic, as for the
  accents.
- **(c)** A controlled session (3+ players per instrument, ISO 532-1 loudness) is beyond what an agent can run.

**Value: high.** Sul tasto / flautando play 16–54 dB too soft in Recommended today, the largest level error known.
Harmonics and col legno are 5–30 dB uncertain. **Effort: medium** for (a), about a day: an IRCAM Forum account is needed
for the audio, which the owner must create. (b) takes 1–2 days per score.

### 1.1a CC1 techniques: the 2026-10-29 search, the Philharmonia check and the SSO sweep plan

Open item 4 (sul tasto / flautando / harmonics and the other CC1 techniques), branch `bowed-join`, 2026-10-29. No VM
was used; the SSO sweep below is prepared, not run.

**Searched again, nothing new with numbers** (all [read] unless marked):
- Bow position against level: Guettler, Schoonderwaldt & Askenfelt, "Bow speed or bowing position: which one
  influences the spectrum the most?", SMAC 03, 67–70 [abstract, via Guettler 2010]: at a constant bow speed a new bow
  position changes the level by about the ratio of the speeds a player would need, so the level follows bow speed
  over β, as §1.1's physics says. Schoonderwaldt, Guettler & Askenfelt, "An empirical investigation of bow-force limits
  in the Schelleng diagram" (*Acta Acustica u. Acustica* 94, 2008; knutsacoustics.com): force limits only, no levels.
- UNSW (Wolfe, newt.phys.unsw.edu.au/jw/violinarticulation.html): sul tasto / sul ponticello recordings in arbitrary
  units, no levels.
- University of Iowa MIS: violin, viola, cello and bass pages (pre-2012 and 2012) have arco and pizzicato only.
- Spheres (Zenodo 17347681): the metadata lists Mozart, Tchaikovsky and chromatic scales / solos per instrument; no
  technique labels. Romeo and Juliet's muted passages ([memory]: the love theme's violas con sord.) would need a score
  reading (review page) and a matched open passage at the same marking: experiment (b) of §1.1, not done.
- Mutes: da Silva Evangelista & Freire 2025 (*Acta Acustica* 9, 45) still refuses automated access (HTTP 403); it
  measures heavy practice mutes anyway. Nothing for viola, cello or bass mutes (patents, retailers' reviews only).
- Natural / artificial harmonics, tremolo, trills: no measured level found anywhere.

**Philharmonia samples, measured** (https://philharmonia.co.uk/audio-sound-samples/, Strings.zip, 2020; MP3; free use,
no redistribution; audio deleted after measuring, numbers kept in `/data/cc1tech/ph_*.json`, outside the repo). Each
file's first 3 s: perceived peak (`tools/playbackverify/loudness.py`), the 400 ms momentary maximum (K-weighted) and a
noise floor (the 5th percentile of 5 ms power above −90 dBFS). Offset = technique − the median of arco-normal at the
same note and dynamic (any length but phrases), then the median over notes, 90 % bootstrap interval of the median.
- **Gain check.** The violin is not peak-normalised: arco-normal pp → ff spans 19.3 dB (median of 14 notes, 10–90 %
  16.8–21.6), 101 of 113 dynamic steps rising. (BALANCE_LOG's bass clarinet, 5.4 dB pp → ff, was normalised; the
  strings are not.) But the **controls fail**: techniques that should play as loud as ordinario don't. Non-vibrato mp
  +3.3 dB (violin), −7.1 (viola); con sordino +6.3 at mp but −3.1 at f (violin; a mute can't make it louder); pizzicato
  −2.7 … +21 (violin). The technique files come from other sessions or batches than the arco-normal ones, with gain or
  playing steps of about ±3–7 dB; the noise floors differ (violin con sord. 13 dB above arco-normal). One player per
  instrument, in a room, unknown distance.
- **Violin results** (perceived; momentary within 1 dB of it): sul tasto p **+5.5** [4.0, 7.1] (n 23); sul ponticello p
  +3.0 [2.5, 5.5] (23), mf +1.0 [−0.2, 2.4] (16); natural harmonic mf **−5.4** [−7.7, −3.1] (19), p −0.2 [−1.2, 3.6]
  (10); artificial harmonic mf −4.7 [−6.3, −3.4] (17); con sord. f −3.1 [−4.7, −2.1] (18), mp +6.3 (13); tremolo pp
  +4.6, mp +8.5, mf −2.5, ff +1.9; col legno battuto / tratto pp-p 0 ± 0.6; trill (M2) mp +6.7. Viola, cello and bass have
  one or two files per technique (phrases), except cello trills mp −1.7 / −3.2 and bass col legno battuto mf −5.8,
  harmonics p −5.3 (n 4).
- **Verdict: not usable for heldOffset**, corroboration grade at best: the controls' ±3–7 dB is as large as the
  effects. Two readings agree with other evidence: the **mute at f −3 dB** (the violin-mute measurements of §1.1:
  −3 to −5) and **harmonics at mf −5 dB** (no other source). The sul tasto +5.5 at p contradicts the physical bound
  (−3 to −9) and is most likely the session step (non-vibrato mp, recorded with it, reads +3.3), or the player bowing
  faster than at an ordinary p; it says at least that a written p sul tasto is not much softer than an ordinary p.

**Real-player offsets against ordinario at the same written dynamic** (what the fork can use; none is a measurement
of real players' levels with a known gain):

| technique | offset, dB | source, grade | uncertainty |
|---|---|---|---|
| sul tasto | −3 to −9 (centre −5) | physics (Schelleng / Cremer, Askenfelt's β at p and f), derived; Philharmonia violin p reads +5.5 (failed controls) | ±4; an assumption |
| flautando | −3 to −9 (centre −5) | the same bound (faster, lighter bow over the fingerboard); no data | ±4; an assumption |
| sul ponticello | −6 to 0 (centre −3) | Guettler's simulation −11 at equal speed, players press harder; Philharmonia violin p +3.0, mf +1.0 | ±4; an assumption |
| con sordino (violin) | −3 to −5 (centre −4) | UFMG 2022 −3.2 / −5.4 phon, Shar −4 / −5 dBA (one violin each); Philharmonia f −3.1 | ±2 |
| con sordino (viola, cello, bass) | as the violin | no data | ±4; an assumption |
| natural / artificial harmonics | −5 (mf) | Philharmonia violin mf −5.4 / −4.7, bass p −5.3 (n 4); p −0.2 | ±5; one player per instrument, failed controls |
| tremolo (measured, unmeasured) | 0 | no data; Philharmonia −2.5 … +8.5 by dynamic | ±5; an assumption |
| trills | 0 | no data; Philharmonia violin mp +6.7, cello mp −1.7 / −3.2 | ±5; an assumption |

**SSO's side today** (the shipped `dynamics.json`, one pitch per patch, perceived at CC1 80 against the Long):

| section | CS | Flautando | Harmonics | Sul Tasto | Super Sul Tasto | Sul Pont | Tremolo | Trem Sul Pont |
|---|---|---|---|---|---|---|---|---|
| Violins 1 | −6.6 | −29.1 | −12.3 | −29.7 | −49.0 | −2.3 | −0.3 | −9.0 |
| Violins 2 | −5.5 | −22.0 | −9.8 | – | −61.1 | −2.8 | −2.0 | −7.9 |
| Violas | −10.3 | −25.6 | −14.8 | – | −63.0 | −1.3 | −2.7 | −8.3 |
| Celli | −6.2 | −28.6 | −17.6 | – | −44.7 | −7.9 | −4.7 | −10.1 |
| Basses | – | −27.0 | (not at its pitch) | – | −32.3 | +10.4 | +4.5 | +3.5 |

Against the table above, SSO's flautando / sul tasto are about 15–25 dB too soft and Super Sul Tasto 25–55 dB (its
probe pitch may lie outside its range: the sweep checks), harmonics 5–13 dB too soft, the mutes within 1–6 dB, sul
pont within its range except the Basses. These are single pitches and single takes; the sweep measures them by pitch,
CC1 and take. Only Violins 1 has a Long Sul Tasto; the other sections' "sul tasto" is Super Sul Tasto.

**SSO sweep plan** (prepared 2026-10-29, not run: `/home/volensia/.claude/jobs/c3349e77/tmp/cc1tech/vm_plan.sh`, a
command file around `measure_patch.py`, docs/MEASURING.md):
- The five sections' All-techniques patches, articulations by UACC as the map names them: Long (the reference, in the
  same session), Long CS, Long Harmonics, Long Flautando, Long Sul Pont, Long Sul Tasto (Violins 1), Long Super Sul
  Tasto, Long CS Sul Pont, Tremolo, Trem Sul Pont, Trem CS (Violins 2, Violas, Celli), Trill (Major 2nd); 54 plans.
- Notes after silence, 2.5 s, velocity 64, CC11 127, CC1 16 / 32 / 48 / 64 / 80 / 96 / 112 / 127 (sso_cc1_curves.json's
  points), pitches every 5 semitones over each technique's range from the map's onset keys (harmonics every 4, the
  Basses' every 3, sounding pitches), 2 takes ABBA, 3.5 s after each note-off (Flautando releases up to 3.5 s).
- Measured: peak over 2500 ms (the slow attacks peak late), mean, loud50, onset15 / onset6 and release40 / 20. Output:
  an offset per section, technique, pitch and CC1 against the Long, and each technique's onset by pitch (item 4's timing:
  the map's onsets came from single takes).
- Phase 1 (Long, Flautando, Sul Tasto, Super Sul Tasto, Harmonics: 21 plans, 5.0 h of audio) about 1–1.5 h of VM time;
  phase 2 (the rest, 33 plans, 7.5 h) about 1.5–2.2 h; one section and phase per background job.
- Then: heldOffset-style D for CC1 techniques = (the table's centre) − SSO's measured offset, played by the calibrated
  CC1 (`SoundLib::calibratedController`) or CC11 where CC1 can't reach it. The table's values are assumptions except the
  violin mute: the owner decides whether to use them (CLAUDE.md: a gap is marked as one).

**Value: high** (the largest level error known). **Effort:** the sweep about 3–4 h of VM time plus a day of analysis;
real-player data still needs OrchideaSOL (§6 task 1) or a controlled recording.

### 1.2 Accents (>, sf, sfz, marcato, fp)

**Question.** How much louder does each accent sign play, by dynamic, in strings and in winds? `[slurs] plainAccents` 2
plays the accented attack at +5.0 dB, from the Spheres dataset's sf at f, for every sign. A ">" may play softer than an
sf.

**Sources and numbers**

*The fork's own measurement* (private/accent-spheres/RESULT.md): the Spheres dataset, K. 550, strings.

| case | level over the neighbours | interval |
|---|---|---|
| sf at f | +5.0 dB | 90 % [+3.8, +6.0] |
| sf at p | +10.3 dB | an sf inside p plays near f: a change of level |
| mfp at p | +1.7 dB | |

The five sections agree within an SD of 0.6 dB.

*Other sources:*
- **Dahl, S. (2004).** "Playing the accent – comparing striking velocity and timing in an ostinato rhythm performed by
  four drummers." *Acta Acustica u. Acustica* 90(4), 762–776. Summarised in Dahl (2011), *Acoust. Sci. & Tech.* 32(5),
  168–173, doi:10.1250/ast.32.168. [read: the 2011 survey]
  - Four players, 288 strokes per point.
  - An accented stroke strikes about 2× faster at pp, mf and ff alike: about +6 dB of impact velocity, a constant ratio.
  - The stroke before the accent is softened, and the interval after it lengthened.
  - Percussion only.
- **Goebl, W. (2001).** "Melody lead in piano performance: expressive device or artifact?" *JASA* 110(1), 563–572,
  doi:10.1121/1.1376133. [read] A melody's dynamic emphasis brings a ~30 ms lead, a hammer-velocity artefact. No
  accent-sign dB.
- **OrchideaSOL** `sforzato` at f and fp for the four strings. Isolated notes: sf against ordinario f, and the fp
  envelope.
- No study was found measuring ">" against sfz against marcato in strings or winds.

**Fork now.** +5.0 dB for every accent sign (plainAccents 2). MS4.7.5's offset (+5.3) is inside the interval.

**Missing.**
1. ">" and marcato data.
2. Accents at p and ff as accents rather than level changes.
3. Winds and brass accents.

Experiment: the accent pipeline of private/accent-spheres (STL peak in [−0.05, +0.40] s against the median of unmarked
neighbours in the same part and dynamic) on:
- **Romeo and Juliet** (Spheres; ">" in the strings, e.g. score p. 75; also the winds and brass)
- **Aalto Mahler 1 / IV** and **Bruckner 8 / II** (">", "^", sfz)
- **TU Berlin Beethoven 8** (sf, fp)

**Value: medium.** It fixes the ">" assumption and gives the winds accents from data. **Effort: medium.** Score reading,
alignment and a review page with crops (the readings rule) take 1–2 days a score.

### 1.3 Divisi and section size

**Question.** When a section divides into n parts, how loud is each part, and is a section's level 10 log10 N over one
player? `[levels] divisi` plays each of n voices 10 log10 n dB softer, an assumption. The section balance takes
"one player + 10 log10 N".

**Sources and numbers**
- **Physics.** Players are never phase-locked: detuning of a few cents and independent vibrato. Their powers add over
  any window longer than a beat period, so N players give 10 log10 N of power. A coherent 20 log10 N would need locked
  sources.
- **Ternström, S. (2002).** "Choir acoustics – an overview of scientific research published to date." *TMH-QPSR* 43(1),
  1–8 (diva2:1416134). [read] Also Ternström (2012), "Acoustics for choral singing" (diva2:581695). [read]
  - Self-to-other ratio in choirs of 20–32: +3 to +4 dB (+1 to +8 by position).
  - About hearing, not about the growth of the total level.
- **Titze, I. R. (2011).** "The economy of choir size." *J. Singing* 68(2), 175–176. [read] Uses +3 dB per doubling as
  an assumption, citing Ternström 1991 (*J. Voice* 5(2), 128–143).
- **Rossing, T. D., Sundberg, J. & Ternström, S. (1986).** "Acoustic comparison of voice use in solo and choir
  singing." *JASA* 79(6), 1975–1981, doi:10.1121/1.393205. [abstract]
  - Eight basses / baritones.
  - In choir mode the singers adjusted their level to what they heard from the others.
  - So a player's effort is not fixed in an ensemble. A smaller divided group may play louder per player; the size is
    not known.
- **Ternström, S. (1993).** "Perceptual evaluation of voice scatter in unison choir sounds." *J. Voice* 7(2), 129–135.
  [abstract] About pitch scatter, §3.1.
- **Meyer 2009** and **Burghauser & Špelda 1967/1971** are said to give section-growth figures and the "chorus effect".
  [memory; not reached]
- No measured series of 1, 2, 4, 8 orchestral string players in unison was found.

**Fork now.** 10 log10 N (balance) and −10 log10 n (divisi), both from player counts. Correct physics, but unmeasured
behaviour.

**Missing.** Measured unison growth and the per-player compensation in divisi. Ways to fill it:
- **(a)** Dagstuhl ChoirSet (CC BY-NC-SA 4.0, per-singer close mics with full-choir takes, §4): compare each part's mix
  with the power sum of its singers. Corroboration only (voices).
- **(b)** The Spheres dataset: a section's chromatic scales against its leader's solo scales, at the same dynamic and
  session. This gives the section over one player, against 10 log10 N, with N from the session notes.
- **(c)** Divisi passages in the Spheres / Aalto / Beethoven 8 scores, unison against divided at the same written
  dynamic.

**Value: low–medium.** The physics is unlikely to be far off; the open part is a dB or two of compensation. **Effort:
low** for (b), which reuses the Spheres pipeline; medium for (a).

### 1.4 Dynamic ranges per instrument (after Clark & Luce)

**Question.** The span pp → ff and each marking's step, per instrument, as orchestral players play it. The fork uses
Clark & Luce 1965 Table I (`realSteps`, realRange 2), the Meyer rule (realRange 1) and TU Berlin's extremes.

**Sources and numbers**
- **No newer per-marking dataset (pp / p / mf / f / ff) with one method across the orchestral instruments was found.**
  Clark & Luce stays the only one. Since then there are only:
  - the two extremes (TU Berlin 2018; Meyer 1990 / 2009)
  - forte only (Burghauser & Špelda via ISO 23591)
  - levels at the players' ears (noise-exposure studies, §1.5)
- **Weinzierl, Lepa, Schultz, Detzner, von Coler & Behler (2018).** "Sound power and timbre as cues for the dynamic
  strength of orchestral instruments." *JASA* 144(3), 1347–1355, doi:10.1121/1.5053113; repository copy
  depositonce-8692, CC BY 4.0. [read]
  - Method: ISO 3745, 32 microphones on a sphere of r ≈ 2.1 m in an anechoic room, one player per instrument, 3,482
    single notes, "as soft / as loud as possible".
  - Table I, sound power in dB re 1 pW. "av" is weighted by pitch frequency in Beethoven's symphonies.

    | instrument | ff max | pp min | ff av | pp av | av range |
    |---|---|---|---|---|---|
    | Violin | 95 | 52 | 91 | 56 | 35 |
    | Viola | 97 | 53 | 93 | 62 | 31 |
    | Cello | 97 | 63 | 93 | 70 | 23 |
    | Double bass | 100 | 56 | 96 | 70 | 26 |
    | Flute | 105 | 77 | 101 | 89 | 12 |
    | Oboe | 101 | 80 | 99 | 84 | 15 |
    | Cor anglais | 101 | 79 | – | – | – |
    | Clarinet B♭ | 110 | 53 | 102 | 69 | 33 |
    | Bass clarinet | 102 | 65 | – | – | – |
    | Bassoon | 104 | 82 | 101 | 86 | 15 |
    | Contrabassoon | 98 | 80 | 94 | 85 | 9 |
    | Horn | 114 | 71 | 112 | 85 | 27 |
    | Trumpet C | 112 | 74 | 107 | 83 | 24 |
    | Tenor trombone | 113 | 68 | 112 | 82 | 30 |
    | Bass trombone | 112 | 72 | – | – | – |
    | Tuba | 122 | 71 | – | – | – |
    | Timpani (pedal) | 108 | 58 | – | – | – |
    | Harp | 91 | 54 | – | – | – |

  - **Reproducibility against Meyer 1990 on 12 instruments:**
    - **at ff, mean absolute difference 2 dB**, all within ±4 except the tuba (Meyer 10 dB lower);
    - **at pp, mean absolute difference 6.8 dB**, within ±10.
  - So anything built on TU's pp (the Meyer rule's F = 0, realSpan) carries about ±7 dB at the bottom; anything
    anchored at ff about ±2 dB.
  - TU *does* give the cor anglais' extremes: 101 / 79. The "no extremes" note in docs/PLAYBACK_SETTINGS.md concerns
    Meyer's realPlay fractions.
  - A linear discriminant classified pp / ff 92 % right with sound power, and 85 % from timbre alone (spectral skewness,
    flatness, attack slope).
- **ISO 23591:2021**, *Acoustic quality criteria for music rehearsal rooms and spaces*, Annex A, Table A.1
  (https://www.iso.org/standard/76335.html). [abstract; ToC]
  - **Rindel, J. H. (2022).** "On the importance of sound strength in music rehearsal rooms." DAGA 2022
    (https://odeon.dk/pdf/C149-DAGA_2022_000171.pdf). [read]
  - Rindel states that Table A.1's forte is **computed**: L_W(f) = L_W(ff) − D / 4, with D the ff − pp range, from
    Burghauser & Špelda's 1960s data.
  - **So the fork's ISO-based corroborations (bass clarinet, bass trombone) are ratios of Burghauser & Špelda's ff
    values adjusted by a quarter of their range: independent of TU, but not a measured forte.** docs/BALANCE_LOG.md
    should say so.
- **Patterson, B. (1974).** "Musical dynamics." *Scientific American* 231(5), 78–95,
  doi:10.1038/scientificamerican1174-78. [abstract] Players' steps are smaller and less consistent than composers
  expect. No usable table seen.
- **Nakamura, T. (1987).** "The communication of dynamics between musicians and listeners through musical
  performance." *Perception & Psychophysics* 41, 525–533. [abstract: citation only] Intended against perceived
  dynamics. Worth a library copy.
- **Fabiani, M. & Friberg, A. (2011).** "Influence of pitch, loudness, and timbre on the perception of instrument
  dynamics." *JASA* 130(4), EL193–EL199. [abstract]
- **Elowsson, A. & Friberg, A. (2017).** "Predicting the perception of performed dynamics in music audio with ensemble
  learning." *JASA* 141(3), 2224–2242. [abstract] Listeners agree well on perceived dynamics.
- **De la Torre, de la Prida & Azpicueta-Ruiz (2026)**, Forum Acusticum 2026, trumpet players by style, CC BY 3.0
  (https://dael.euracoustics.org/confs/fa2026/data/articles/000836.pdf). [read]
  - 13 professionals, anechoic, front mic.
  - Classical against modern players: about 2 dB L_A at f, under 1 dB at p.
  - A measure of how much players spread at f.

**Fork now.** Clark & Luce spans (realRange 2), with the Spheres dataset as a cross-check (within ~2 dB where 3+
dynamics exist).

**Missing.** A modern many-player per-marking study (none exists). The best a session can do:
- add the TU − Meyer reproducibility as the stated uncertainty of realSpan / the Meyer rule;
- transcribe ISO Table A.1 (the owner would need to buy it, about CHF 100) for a second ff dataset on 39 instruments;
- per-marking levels in context from the Aalto and Beethoven 8 anechoic recordings, whose parts carry written dynamics
  (§1.5, §4).

**Value: medium**: confidence in Stage A rather than a change. **Effort: low** for the uncertainty notes; medium for the
anechoic measurements.

### 1.5 Winds, brass, percussion, harp, piano, celesta balance

**Question.** The level of each section against the strings at the same marking. In use:
- `sectionLevel2` (Clark & Luce at mp + TU's DI);
- the timpani from the Spheres dataset (Romeo and Juliet);
- the harp from TU (±4 dB);
- the piano from Meyer (±3 dB).

Open:
- the owner has not yet judged winds / brass against the strings;
- mallets, celesta and tubular bells have no source;
- the gap patches (piccolo, alto / bass flute, contrabass clarinet / trombone, cimbasso, tuba) rest partly on family
  assumptions.

**Sources and numbers**
- **Noise-exposure studies.** Levels at the players' ears, dominated by their own and neighbouring instruments. Not
  sound power, not an audience level, so they can't set balance. They corroborate its direction.
  - O'Brien, Wilson & Bradley 2008 (*JASA*, Queensland Symphony Orchestra). [abstract] Reanalysed by Skålevik, "Sound
    levels in symphony orchestra musicians", BNAM
    (https://akutek.info/Papers/MS_Sound-Levels-symphony-orchestra-musicians.pdf). [read] 1,619 dosimeter sessions.

    | position | N | L_Aeq | SD | L_Cpeak |
    |---|---|---|---|---|
    | Violin 1 | 26 | 84.2 | 3.0 | 120 |
    | Violin 2 | 62 | 84.7 | 2.7 | 120 |
    | Viola | 79 | 85.3 | 3.6 | 121 |
    | Cello | 46 | 84.6 | 2.7 | 122 |
    | Bass | 79 | 84.5 | 3.0 | 123 |
    | Flute | 151 | 87.9 | 2.7 | 121 |
    | Oboe | 90 | 87.0 | 2.4 | 121 |
    | Clarinet | 155 | 87.9 | 2.7 | 123 |
    | Bassoon | 135 | 87.6 | 2.7 | 123 |
    | Horn | 273 | 89.2 | 2.7 | 123 |
    | Trumpet | 212 | 89.4 | 3.1 | 126 |
    | Trombone | 157 | 88.6 | 3.0 | 128 |
    | Tuba | 24 | 87.1 | 4.2 | 127 |
    | Timpani | 63 | 87.7 | 3.9 | 133 |
    | Percussion | 67 | 88.8 | 3.1 | 136 |

    - Audience positions: L_eq 80 dB, median 64, SD 15 (the Binaural Project). The stage runs 5–7 dB above the hall.
    - Skålevik's ASA 2019 slides (https://www.akutek.info/Presentations/MS_orchestra_ASA_2019.pdf) fit a per-musician
      source power to O'Brien's data. The text layer is garbled, so don't use the numbers without seeing the figure.
  - **Schmidt et al.**, two Danish orchestras (https://orbit.dtu.dk/en/publications/sound-exposure-of-symphony-orchestra-musicians/).
    [abstract] Brass L_Aeq 86–98 dB, high strings 82–98.
  - **Lee, Behar, Kunov & Wong (2005)**, *Applied Acoustics* 66 (opera pit). [abstract]
  - **Wenmaekers & Hak**, a model separating a player's own instrument from the others
    (https://www.akutek.info/Papers/RW_Own_Instrument_reOthers.pdf). [abstract]
- **Sivian, Dunn & White (1931).** "Absolute amplitudes and spectra of certain musical instruments and orchestras."
  *JASA* 2, 330–371. [abstract] Peak powers over short windows, 1931 equipment: cymbals 9.5 W, snare 11.9 W, timpani
  12 W, orchestra 66.5 W. These come from the Sengpiel sheet, which doesn't name its source; the attribution to Sivian is
  an inference. **An upper bound for percussion transients only.**
- **Percussion.**
  - TU Berlin: timpani 108 / 58 dB.
  - The Spheres dataset: timpani, used.
  - **No measured sound power or SPL per marking was found for glockenspiel, xylophone, celesta, tubular bells, bass
    drum or cymbals.** HSE's HSG260 lists player-position levels of mixed origin (low quality).
- **Harp.** TU 91 / 54 dB, one instrument; nothing new. Given TU's pp reproducibility, the harp's uncertainty at pp is
  nearer ±7 than ±4 dB.
- **Piano.** Nothing beyond Meyer 2009. MAESTRO (§4) gives velocity statistics, not levels.

**Fork now.** As listed in the question. BALANCE_LOG.md has the iterations.

**Missing.**
- Percussion levels by marking (needs real instruments: not an agent's experiment).
- An in-context check of winds / brass against the strings: **done 2026-10-23, §1.5a** (Aalto and TU's Beethoven 8:
  horns and trumpets play softer, flutes louder than the map implies). Still open: trombones and tuba (too little
  data), and a modern-orchestra dataset with more than one player per instrument. The Spheres dataset's own balance is
  unusable (one chamber orchestra, gains unknown; BALANCE_LOG.md).

**Value: medium–high.** The winds / brass balance has never been confirmed. **Effort: medium.** Aalto needs the
licence question settled first: its terms say "academic research", so commit derived numbers only, and preferably ask
Pätynen / Lokki.

### 1.5a In-context check of the winds / brass against the strings (anechoic orchestra recordings, 2026-10-23)

Branch `research-balance` (from integrate-1017 fd909bc9a3). Research only: nothing in the map, the generator or MuseScore
changed. Numbers: `tools/soundlibraries/anechoic_balance.json`; tools and the score readings:
`tools/soundlibraries/anechoic_balance/` (the audio stays outside the repo and was deleted after measuring).

**Question.** Does each wind and brass instrument play, against the strings at the same written marking in real music,
as loud as the map implies? The map's real side (`sectionLevel2` + realRange 2) says one player plays
Lw(m) = Lw_mp + markingSteps(m) × realSteps / 5, Lw_mp being Clark & Luce's one-player mp
(`sso_cl_section_balance.json`, `lw.mp`), markingSteps pp −2, p −1, mp 0, mf 1, f 2, ff 3, fff 4 (`ContextLevel`).
Until now nothing had checked this with an orchestra playing music.

**Data** (all [read]; licences checked)
- **Aalto anechoic symphony recordings.** Pätynen, Pulkki & Lokki 2008, *Acta Acust. u. Acust.* 94(6), 856–865;
  Lokki, Pätynen & Pulkki 2008, "Recording of anechoic symphony music", Acoustics 08 Paris, Table 1 (microphone
  positions).
  - The download page states: "The recordings are all made with exactly the same gain settings … if you just add all
    tracks together the balance should be correct". The unknown absolute gain is one constant for every part of every
    piece. The strings' reference came out the same in all three pieces (−120.4, −120.5, −120.2 dB on the arbitrary
    scale), which supports this.
  - One professional player per instrument (Finnish National Opera and Helsinki orchestras) played every part one after
    another, watching a conductor video and hearing a piano reduction.
  - MP3 at 192 kb/s. Front microphone 6 (azimuth 0°, elevation 10.8°, 2.30 m from the player's head). The horns also have
    mic 8 (azimuth 144°, 1.92 m); the bassoons and tuba also have mic 5 (azimuth 288°, elevation 52.6°, 2.49 m).
  - Used: Beethoven 7 / I bars 1–53, Bruckner 8 / II bars 1–61, Mahler 1 / IV bars 1–85. Mozart (a soprano aria with a
    small wind section) was left out.
  - Licence: "free for academic research", no formal licence. Only derived numbers are committed.
  - Bar timing: the PHENICX-Anechoic hand-aligned note annotations (Miron et al. 2016, Zenodo 840025, **CC BY 4.0**; §4
    said CC BY-NC-SA 3.0, which was wrong). Each family's score-MIDI times (`*_o.txt`) are paired note by note with the
    aligned times.
- **TU Berlin anechoic Beethoven 8.** Böhm, Ackermann & Weinzierl 2020, *JAES* 68(12), 977–984,
  doi:10.17743/jaes.2020.0056; data doi:10.14279/depositonce-6729.2, **CC BY 4.0**.
  - The open excerpts: movement I bars 1–74, II bars 1–23, IV bars 0–90.
  - Orchester Wiener Akademie on period instruments, recorded groupwise, two players per wind, violin and viola part,
    one cello, one bass.
  - **Calibrated.** Every track is normalised to 1 m and 10.1 mV/Pa. Table 3 gives each track's mic gain G and
    artistic gain A, so SPL at 1 m in the mic direction = dBFS + 94 + 49.8 − G − A (the paper's worked example: Flute 1,
    40 / −17 dB, −26.8 dBFS = 94 dB).
  - The sound engineer's artistic gains (−2 … −18 dB) are reversed. Left in, they shift the winds by −5 … −8 dB against
    the strings: a mix, not the players' balance (kept in the json as a check).
  - Bar timing: dynamic time warping of the score activity (the readings' notes spans, every part) against the audio
    activity (every track). On Beethoven 7 it reproduced PHENICX's bar positions within 0.15 bar (median), 0.7 (90 %).
- **Directivity.** TU Berlin directivity database 1a (ff single tones; CC BY-NC-SA, derived numbers only). DI toward
  each microphone, p² interpolated over the 32 receivers, mean over the notes. Modern instruments for Aalto; for TU B8
  the classical-era ones (Violin / Viola / Cello / Double bass historical, keyed flute, classic oboe and bassoon,
  historical clarinet, natural horn and trumpet).
  - Aalto's azimuth runs clockwise. Checked by the second microphones: the tuba's mic 5 − mic 6 is +5.5 / +6.4 dB
    measured against +4.1 predicted; the opposite convention predicts 0.
  - The same check shows the DI's error toward one microphone is about ±2 dB. For the horns it is ±2.5: mic 8 gives Lw
    4.6–5.1 dB under mic 6's. The horns' value is the dB mean of the two microphones' estimates; mic 6 alone moves them
    +2.4 dB. The bassoon and tuba take the dB mean of mics 5 and 6.

**Readings of the scores.** Agents transcribed every part's notes spans, dynamics, hairpins, cresc. / dim., point
accents and technique texts from public-domain scores into `anechoic_balance/readings/*.tsv` (format in
`FORMAT.txt`; about 5,300 rows, each doubtful one marked "uncertain"):
- Beethoven 7: B&H Werke, IMSLP #312601
- Bruckner 8: Eulenburg, IMSLP #30702, probably the 1892 text
- Mahler 1: UE, IMSLP #17070
- Beethoven 8: B&H Werke, IMSLP #61814

**Reviewed by the owner, 2026-10-24**: a spot-check page of 40 readings drawn at random (stratified: horns 9, trumpets 7,
flutes 7, oboes 7, the rest strings, clarinets, bassoons), each next to a crop of the score; all 40 right. If the share
marked wrong had been 5 % across all 5,300 readings, the horn's, trumpet's, flute's and oboe's results could have moved
by up to 0.8 / 0.9 / 1.1 / 0.6 dB (90th percentile, a marking taken from another stretch). (Before the review: by the
readings rule, a review page with crops is needed before any of this changes the map.) Dropping the "uncertain" segments changes no instrument by more than 0.4 dB except
trombone / tuba (see Sensitivity).

**Method** (`anechoic_balance/`: envelope.py, tu_envelope.py, dir_at.py, balance.py, align_dtw.py, compare.py,
summarize.py, make_json.py)
1. **Envelope and activity.** A 50 ms mean-square envelope after a 40 Hz high pass. A frame counts as active when it is
   over the track's noise (its 5th percentile) + 10 dB and within 30 dB of its 99th percentile.
2. **Steady segments.** A segment is where the part plays at one written marking. Excluded:
   - 0.25 bar at each end (0.4 bar for TU and Mahler, timed less exactly)
   - hairpins and cresc. / dim. spans, ±0.25 bar
   - 0.05 bar before to 0.25 bar after a point accent (sf, fp, …)
   - stopped horn, mutes and pizz.

   The segment's level is the power mean of its active frames less the noise power. A player's repeated passes (TU's
   string voices, Aalto's doubled string tracks) are power-averaged.
3. **Sound power.** Lw = level + 20 log10 r + 11 − DI(mic), plus the dataset's constant (unknown for Aalto, the
   calibration for TU). The map's per-player value is r = Lw − Lw_map(instrument, marking), taken as each part × marking's
   frame-weighted energy mean.
4. **Against the strings.** rel = r − the strings' mean r at the same marking in the same piece (at least two string
   parts and 10 s). This cancels Aalto's unknown gain, the dynamic that a marking means in a given passage, and any
   compression of the strings' span. **rel < 0 means the instrument plays softer against the strings in context than the
   map plays it.**
5. **Summary.** Per instrument and dataset, the median over its part × marking values, with a bootstrap 90 % interval.
   "Pooled" is the mean of the two datasets' medians: different players, instruments and recordings.

**Results.** rel in dB, per player, against the strings at the same marking. n = part × marking values; the seconds
measured are in the json.

| instrument | Aalto (1 player, modern) | TU B8 (2 players, period) | pooled | map now (sectionLevel2, solo patch) | in context, against the map |
|---|---|---|---|---|---|
| flute | +3.2 [+1.4, +6.6] n 11 | +4.8 [+3.9, +7.0] n 14 | **+4.0** | −2.04 | louder: **+4 ± 3** |
| oboe | −2.0 [−2.8, −0.6] n 17 | −6.4 [−7.1, −3.7] n 20 | **−4.2** | −6.67 | softer: **−4 ± 3** (datasets 4.4 apart) |
| clarinet | +2.7 [+0.7, +4.1] n 17 | −0.8 [−1.6, +0.1] n 19 | +1.0 | −2.79 | **agrees** (±3) |
| bassoon | +2.7 [+2.3, +3.9] n 21 | +0.7 [−0.7, +1.4] n 21 | +1.7 | −15.00 | about agrees (+1.7 ± 2.5) |
| horn | −7.6 [−8.0, −6.7] n 36 | −9.0 [−12.1, −6.7] n 17 | **−8.3** | +0.57 | softer: **−8 ± 3** |
| trumpet | −4.1 [−5.4, −0.6] n 11 | −5.1 [−6.5, −4.3] n 7 | **−4.6** | +9.04 | softer: **−4.5 ± 2.5** |
| trombone | −1.0 [−3.4, +2.7] n 11 | – | (−1.0) | −0.35 | **no conclusion** (Bruckner +2.9, Mahler −3.3; spread 9 dB) |
| tuba | −1.6 [−10.7, +5.2] n 4 | – | (−1.6) | −1.12 | **no conclusion** (4 values) |
| *violin* | −2.5 | +0.4 | −1.0 | | the strings against each other: |
| *viola* | +3.9 | −2.1 | +0.9 | | the datasets disagree by 3–6 dB |
| *cello* | −0.2 | −0.7 | −0.5 | | (one or two players each); no |
| *double bass* | +2.9 | +0.7 | +1.8 | | consistent difference from the map |

The "±" combines:
- the statistical interval;
- the DI toward one microphone (±2 dB, horns ±2.5);
- half the datasets' difference, standing for the players: Aalto has one player per instrument, TU two.

**Corroboration**
- **TU's absolute levels.** TU Beethoven 8 is calibrated, so its absolute residuals (Lw − the map's per-player Lw at the
  marking) say the same without the strings as reference:

  | | violin | viola | cello | bass | flute | oboe | clarinet | bassoon | horn | trumpet |
  |---|---|---|---|---|---|---|---|---|---|---|
  | median (dB) | +3.0 | −1.1 | +0.6 | +0.7 | +6.7 | −4.5 | +0.7 | +2.6 | **−8.6** | **−5.5** |

  These players' strings sit within ±3 dB of Clark & Luce's absolute per-player levels. Their horns and trumpets sit
  8.6 / 5.5 dB under them.
- **The same horn and trumpet offset by marking.** rel by marking is about the same from p to ff:
  - horn: Aalto p −10.0, mf −7.6, f −6.9, ff −7.7; TU pp −10.3, p −9.1, f −10.6, ff −7.9
  - trumpet: f −3.0 / −4.2, ff −5.3 / −5.5

  So it is an offset of the balance, not a different span.
- **The horn's history.** Iteration 1 (BALANCE_LOG.md) replaced Fig. 2's horn (player 2, 13.4 dB lower at mp) with
  Clark & Luce's player 1. In context the horns land 8 dB under player 1 and about 5 dB over Fig. 2: between the two
  players, nearer player 2. Iteration 1's other sources put horn − trumpet at 0 … −7 dB. The pooled in-context values
  give horn − trumpet (one player each, mp) = (94.76 − 8.3) − (96.59 − 4.6) = −5.5 dB, inside that range.

**Sensitivity** (pooled dB; the json has every variant)

| variant | flute | oboe | clarinet | bassoon | horn | trumpet |
|---|---|---|---|---|---|---|
| as above | +4.0 | −4.2 | +1.0 | +1.7 | −8.3 | −4.6 |
| mean of dB instead of power mean | +3.8 | −3.8 | +1.4 | +2.7 | −8.2 | −4.9 |
| without the "uncertain" readings | +4.1 | −4.1 | +1.4 | +1.6 | −8.4 | −4.5 |
| without Mahler | +4.0 | −3.7 | +0.4 | +1.7 | −7.9 | −4.0 |
| Aalto front mic 6 only | +4.0 | −4.2 | +1.0 | +2.2 | −7.1 | −4.6 |

**Applied 2026-10-24** (the owner: "apply the full measured balance"; docs/BALANCE_LOG.md › Iteration 15): the pooled
values unrounded, flutes +3.96, oboes -4.17, horns -8.29, trumpets -4.59 dB on `sectionLevel2`, by `anechoic_shift.py`
(applied where both datasets measured the instrument, agree in sign and each 90 % interval excludes 0). The proposal
as written before:

**What it implies for the map** (a proposal at the time; the owner decided as above)
- Change the woodwind and brass families' `sectionLevel2` by the pooled in-context difference, the strings staying the
  centre. Each change is about ±3 dB uncertain. Every patch of a family moves together, the same per-player offset:
  - **horns −8 dB**: Horn Solo +0.57 → about −7.7; Horns a2, a6 and Motif Horns a4 the same −8
  - **trumpets −4.5 dB**: Trumpets a6 +20.45 → about +16; the Motif trumpets too
  - **flutes +4 dB**: piccolo, alto and bass flute only by the family assumption
  - **oboes −4 dB**: the cor anglais only by the family assumption
- Leave the clarinet and bassoon as they are: the data agree within the uncertainty, and the two datasets straddle 0.
  Leave the trombones, tuba and the low brass gap patches too: not enough data.
- It would be the first change of the balance from one whole-orchestra source in context. It ranks above Clark & Luce's
  out-of-context scales for this question: real music, one gain (Aalto) or a documented calibration (TU), two
  independent orchestras that agree within 1.5 dB on horns, trumpets and flutes. It ranks below them in players: one or
  two per instrument against Clark & Luce's two, period instruments in TU.
- A conservative half-step (−4 horns, −2 trumpets, +2 flutes, −2 oboes) would be within both sources' intervals. That is
  for the owner to decide.

**Side finding for §1.4 (not part of the balance).** In context the strings' level moves less between markings than
Clark & Luce's spans:
- TU: the strings' residual is +6 … +9 dB at pp and −1 … −5 dB at ff, so their pp → ff in context is about 4–6 dB against
  the map's 13.8–15.9.
- Aalto: 3 dB less than the map's span.

This is consistent with the anechoic, headphone situation and with passages written pp lying in other registers. It
needs its own study before it touches realSteps.

**Still unknown**
- **Trombones and tuba**: 11 and 4 values, Aalto only, 9 dB apart between Bruckner and Mahler.
- **Percussion, harp, keyboards**: not measured here (Aalto's timpani track exists; TU's timpani DI toward its
  overhead mic is unreliable).
- **The players**: one Aalto player per instrument and two TU players. Clark & Luce's players differed by 0–4 dB (horn:
  14–22). No dataset with several orchestras' players per instrument in context is known.
- **The recording situation**: players recorded alone or in groups with headphones and a pilot track. Their effort
  against a real orchestra's (Lombard effect, §1.3's Rossing 1986) is unknown. It acts on strings and winds alike, so
  rel is more robust than the absolute levels.
- **Directivity**: TU's database players are not these players. The horns' two Aalto microphones disagree with TU's
  pattern by about 4 dB.
- **Bar timing in TU B8** (DTW): about ±0.2–0.7 bar, handled by the 0.4-bar trims.
- ~~The owner's review of the readings.~~ Done 2026-10-24 (spot check, 40 of 40 right).

### 1.6 Directivity

**Question.** The level toward the audience against the sound power. sectionLevel2 uses +31 − TU Berlin's DI in one
direction, per marking.

**Sources and numbers**
- **Shabtai, Behler, Vorländer & Weinzierl (2017).** "Generation and analysis of an acoustic radiation pattern database
  for forty-one musical instruments." *JASA* 141(2), 1246–1256, doi:10.1121/1.4976071 (depositonce). [abstract] The
  source of the current DI.
- **Ackermann, Brinkmann & Weinzierl (2023).** "A database with directivities of musical instruments."
  arXiv:2307.02110. **CC BY-SA 4.0.** [abstract] 41 instruments, third-octave directivity per note, in SOFA / OpenDAFF /
  GLL.
- **Ackermann, Brinkmann & Weinzierl (2024).** "Musical instruments as dynamic sound sources." *JASA* 155(4),
  2302–2313, doi:10.1121/10.0025463, CC BY 4.0. [read]
  - Brass DI rises with frequency (piston-like) and changes little from note to note.
  - Woodwind DI swings from below 0 to about 10 dB by note.
  - Strings focus by up to 20 dB at short wavelengths.
  - The spread of sound strength G at a seat (2,000 m³, T ≈ 1 s, twice the critical distance), notes / notes and the
    player's movement:

    | | dB |
    |---|---|
    | violin | 2.1 / 2.8 |
    | viola | 2.7 / 3.4 |
    | cello | 3.6 / 3.8 |
    | bass | 3.4 / 3.9 |
    | flute | 1.3 / 2.4 |
    | oboe | 1.1 / 1.7 |
    | clarinet | 2.2 / 2.9 |
    | bassoon | 2.5 / 3.0 |
    | trumpet | 0.7 / 1.4 |
    | horn | 0.7 / 1.5 |

- **Pätynen, J. & Lokki, T. (2010).** "Directivities of symphony orchestra instruments." *Acta Acustica u. Acustica*
  96(1), 138–167, doi:10.3813/AAA.918265 (https://users.aalto.fi/~ktlokki/Publs/patynen_aaua_2010.pdf). [read]
  - 14 instruments, 22 mics at 2.13 m, A-major chord tones at p / f / ff.
  - Normalised patterns only, no sound power.
  - **Dynamics don't change the directivity, only the spectral slope:** trumpet −21 / −13 / −9 dB per octave at
    p / f / ff. Useful for §1.7.

**Data quality.** Good. A one-direction DI is stable for brass but varies 2–4 dB for strings and woodwinds.

**Fork now.** One direction per instrument and marking.

**Missing.** Either sound power alone for strings and woodwinds (the reverberant field dominates past twice the
critical distance), keeping a direction term only for brass, or a seat-averaged gain from the 2023 database: the
directivity averaged over the audience's solid angle (0 to −30° elevation, ±40° azimuth), loudness-weighted.

**Value: medium.** It could move string and woodwind balances by 1–3 dB, and it gives a defensible uncertainty (Ackermann
2024's table). **Effort: medium.** The SOFA files are small per instrument; the averaging is a script.

### 1.7 Tone against dynamic (Stage B)

**Question.** Choose CC1 / velocity so that SSO's brightness matches the real instrument's at each marking, and set the
loudness by CC11. Planned in HANDOFF.md after Stage A.

**Sources.**
- **Weinzierl et al. 2018** (§1.4) give the spectral centroid and other features per note at pp and ff, in
  `3_Audio_Features.csv` (TU database v3, depositonce-19858). [read: the paper]
- **Pätynen & Lokki 2010** (§1.6) give the spectral slope by dynamic for 14 instruments. [read]
- **OrchideaSOL** pp / mf / ff ordinario (§4) adds an mf point.

**Fork now.** Nothing. SSO's CC1 sets both tone and level.

**Missing.** SSO's centroid per CC1 (from the sweeps the fork already made, `sso_cc1_curves.json`'s renders; the
centroid has to be computed) against TU's centroid at pp / ff, with OrchideaSOL's at mf. Then the CC1 whose centroid
matches the real instrument's at each marking.

**Value: high in the owner's plan** ("important for realism"); it would also make the level rule simpler. **Effort:
medium.** The data exist on both sides; a re-render on the VM may be needed for the centroids.

---

## 2. Timing

### 2.1 Perceived onset of slow attacks

**Question.** When is a note with a slow rise (SSO strings, 150–400 ms) heard? The fork's rule is −15 dB under the
peak, less 30 ms (Vos & Rasch 1981). −6 dB matched the owner's ear on smooth first notes. No data exist for real
section strings with 200–300 ms rises.

**Sources and numbers**

| source | method, n | numbers | quality |
|---|---|---|---|
| **Vos & Rasch 1981**, *Percept. Psychophys.* 29(4), 323–335 [abstract; via Villing] | adjustment, 400 Hz sawtooth, rise and level varied | threshold re peak **−7 dB at 20 dB SL, −15 dB at 70 dB SL**; Villing's fit: **L_PC = L_peak − 3.18 − 0.17·L_SL** (L_SL ≥ 20). Softer = later | simple tones; duration confounded |
| **Gordon 1987**, *JASA* 82(1), 88–105 [abstract; via Villing] | 16 resynthesised orchestral tones | spread of perceived attack times only 49 ms; SD of judgements 6–17 ms; best model: normalised power envelope, slope threshold over a 19 ms window, PAT = t₁ + 0.08 (t₂ − t₁) | lab tones, no section strings |
| **Villing 2010**, PhD, NUI Maynooth (https://mural.maynoothuniversity.ie/2284/1/Villing_2010_-_PhD_Thesis.pdf) [read]; **Villing et al. 2011**, *APP* 73, 1614–1629, doi:10.3758/s13414-011-0110-1 | ~10 models against measured P-centres | Vos & Rasch and Gordon "cannot be recommended" for complex onsets; Rasch 1979 took onsets 15–20 dB under the maximum | strong method; speech and synthetic sounds |
| **Wright 2008**, PhD, Stanford / CCRMA (https://ccrma.stanford.edu/~matt/diss/Matthew-Wright-Dissertation.pdf) [read] | online, 40–57 listeners | synthetic bowed violin note, rise ~100 ms: **PAT 28–41 ms after onset, SD 11–18 ms**; PAT as a probability density | best violin datum; one synthetic note |
| **Bechtold & Senn 2018**, *Front. Psychol.* 9, 1692, doi:10.3389/fpsyg.2018.01692 [read] | 40 musicians, 9 tenor-sax tones (rise 60–173 ms) | PAT **23–83 ms** after onset, SD 20–35 ms; rise time correlates only r = 0.14 with PAT, articulation type dominates | good n; wind |
| **Danielsen et al. 2019**, "Where is the beat in that note?", *JEP:HPP* 45(3), 402–418, doi:10.1037/xhp0000611 [abstract] | click alignment and tapping | slow and long sounds widen the P-centre into a "beat bin" | per-stimulus numbers not seen |
| **Danielsen et al. 2021**, "Sounds familiar(?)", *APP*, doi:10.3758/s13414-021-02393-z [read] | 59 experts, includes **real slow Hardanger-fiddle notes** | **slow-long fiddle note: tri-modal P-centre near 100, 200 and 400 ms; SD > 60 ms** | the closest datum to a real bowed note with a long rise |
| **Polfreman 2013**, ISMIR, 523–528 (https://archives.ismir.net/ismir2013/paper/000224.pdf) [abstract; server refused] | onset detectors against perceived attack time | non-percussive sounds need corrections | **the fork's +23.5 ms was not re-verified here** |
| Scott 1998; Vos, Mates & van Kruysbergen 1995 [via Villing] | linear-ramp tones | P-centre ≈ 0.2–0.24 × rise time (rises ≤ 75 ms) | short rises only |

**Data quality.** Consistent on direction: softer and slower rises are heard later, and the spread widens. No
measurement exists on real section strings. For 200 ms+ rises, listeners scatter 30–60 ms.

**What the data support.**
- A **level-dependent threshold** (Vos & Rasch: −7 dB near 20 dB SL to −15 dB at 70 dB SL), instead of one −15. A pp
  note's onset then sits at a shallower threshold, i.e. later, which is what the owner heard (−6 dB on soft smooth
  starts).
- Treat a slow note's onset as a window (SD 30–60 ms), not a point. The audit's thresholds should allow that.

**Fork now.** −15 dB less 30 ms, for every level.

**Missing.** The perceived onset of real string sections, and of SSO's own attacks, at pp / mf / ff. An agent can't run
a listening experiment with 30 musicians, which is the method of Wright and Villing. What a session *can* do is make the
threshold depend on the note's sensation level by Vos & Rasch's fit, then measure what that does to the heard check's
attack times. That needs an assumed listening level, e.g. 75–85 dB SPL at ff in the hall, from §1.5's audience levels.

**Value: high.** The onset rule times every early start: plainAttack, onsets, the audit. Whether to use −6 or −15 is an
open owner question. **Effort: low–medium** for the rule plus a heard-check re-run on the VM.

### 2.2 Ensemble synchrony and orchestral asynchrony

**Question.** How far apart do real players and sections sound on one beat? This sets the audit's ARRIVAL thresholds
(warn 30, fail 50 ms; Rasch 1979) and the heard check's RHYTHM thresholds.

**Sources and numbers**
- **Rasch, R. A. (1979).** "Synchronization in performed ensemble music." *Acustica* 43(2), 121–131. [abstract; figures
  via Wing 2014b]
  - Trios: the SD of the onset differences is 30–50 ms (string trio 49 ms at 79 bpm, wind trio 32 ms at 88 bpm, range
    24–73).
  - Onsets taken 15–20 dB under the maximum.
- **Rasch, R. A. (1988)**, in Sloboda (ed.), *Generative Processes in Music*, 70–90. [memory] Slow attacks tolerate
  more asynchrony.
- **Wing, Endo, Bradbury & Vorberg (2014).** "Optimal feedback correction in string quartet synchronization." *J. R. Soc.
  Interface* 11, 20131125, doi:10.1098/rsif.2013.1125. [read]
  - Two professional quartets, Haydn Op. 74/1, IOI ~192 ms.
  - SD of the asynchrony 28.3 / 24.4 ms; mean against violin 1 −11.7 / −3.2 ms.
- **Wing, Endo, Yates & Bradbury (2014b).** *Front. Psychol.* 5, 1115. [read] Listeners' detection threshold: about
  8 ms SD of asynchrony.
- **Timmers, Endo, Bradbury & Wing (2014).** *Front. Psychol.* 5, 645. [read] In a quartet, violin 1 leads by 14–15 ms
  (SD 19–20).
- **D'Amario, Daffern & Bailes (2018).** *Front. Psychol.* 9, 1208. [read] Vocal duos: mean absolute asynchrony 59 ms.
- **Pätynen, Tervo & Lokki (2012).** "Temporal differences in string bowing of symphony orchestra players." *JNMR*
  41(3), 223–237, doi:10.1080/09298215.2012.708048. [abstract] Bow reversals in a professional orchestra: half of three
  players' strokes fall within about 60 ms, about ±30 ms.
- **Tahvanainen, Haapaniemi, Pätynen & Lokki (2015).** Vienna Talk 2015, 208–214
  (https://viennatalk2015.mdw.ac.at/proceedings/ViennaTalk2015_submission_55.pdf). [read]
  - Auralised orchestra.
  - Basses first, middle +20 ms, treble +40 ms: preferred over, or equal to, synchrony. Treble first was the least
    liked.
- **Meals (2020).** *Front. Psychol.* 11, 573030. [read] The orchestra sounds 51 ms after the conductor's beat (SD 98).
  Not relevant to playback.
- **Goebl 2001** (§1.2): the piano melody leads by ~30 ms.
- Keller & Appel 2010 (*Music Percept.* 28(1), 27–46) and Papiotis et al. 2014 (*Front. Psychol.* 5, 963) give no
  usable ms values. [abstract]

**Data quality.** Consistent: real players scatter with an SD of 25–50 ms. Sections hold about ±30 ms.

**What the data support.**
- The audit's 50 ms fail is in line with Rasch and with Pätynen.
- A model error under about 20–25 ms is inside what players do.
- "Basses first by ~20 ms" rests on one preference study. It can be offered to the owner as an option, not set.
- **The heard check's RHYTHM 0.10 IOI-share threshold, from Desain & Honing 2003 / Honing 2006, was not confirmed in
  this research.** Honing 2006 is about tempo-specific timing.

**Fork now.** ARRIVAL warn 30, fail 50 ms; RHYTHM fail 0.10, warn 0.05.

**Missing.** The onset spread within and between orchestral sections from multitrack recordings of a live ensemble. The
Spheres and Aalto parts were overdubbed, so they can't give it. TU's Beethoven 8 was recorded "groupwise", so the
spread within a group is real there. QUARTET / EEP (MTG) are real simultaneous quartets, licence unknown.

**Value: low–medium.** The thresholds are already defensible. **Effort: medium.**

**Humanize from real players (2026-10-28; the owner asked whether to add more randomness, "shift them according to
real player patterns").**
- What already varies: SSO records whole sections (the spread within a section is in the samples), its round robins
  and random modulators (the owner kept them: fixedTransitions "keep A"). What doesn't: the sections against each
  other, which MuseScore puts on the beat exactly.
- The size: sections hold about ±30 ms (Pätynen et al. 2012), quartet players scatter with an SD of 24–28 ms (Wing
  et al. 2014), listeners detect about 8 ms SD (Wing et al. 2014b).
- The pattern: listeners prefer timing deviations correlated over long spans to uncorrelated random ones (Hennig et
  al. 2011, *PLoS ONE* 6, e26457 [abstract]: the "humanize" of audio software is white noise, the less liked); added
  microtiming lowered groove and naturalness ratings in synthesised Jazz / Funk / Samba (Davies et al. 2013, *Music
  Percept.* 30(5) [abstract]), so more is not better by itself.
- A generative model fitted to real players: first-order linear phase correction (Wing et al. 2014, J. R. Soc.
  Interface 11, 20131125): each player corrects a share α of its asynchrony to the others at every onset, plus its own
  timekeeper and motor noise; both quartets near the variance-minimising α (1/N for N players, per a summary, not
  checked in the full text). Simulated per section, this gives slowly drifting, mutually corrected deviations of the
  measured size, not jitter. Basses first by ~20 ms (Tahvanainen et al. 2015) could be its offset, as an option.
- Missing: the parameters for an orchestra's sections (the quartet's are the nearest), and any series from a live
  orchestra's multitracks (Spheres / Aalto overdubbed, above).

### 2.3 Legato transitions in real string playing

**Question.** How long does a slurred note change take in real playing, by interval and note length, finger change
against shift against bow change, and when within it does the listener hear the new note? SSO's transitions arrive
88–440 ms after note-on by interval. The modelled arrival (bendGlide) misses the Celli / Basses by 40–50 ms, with
opposite signs in the two test scores, and the Violas' slurs after a held first note arrive ~100 ms sooner than
modelled (HANDOFF.md › Plain timing, open item 1).

**Sources and numbers**
- **Guettler, K. & Askenfelt, A. (1997).** "Acceptance limits for the duration of pre-Helmholtz transients in bowed
  string attacks." *JASA* 101(5), 2903–2913 (https://knutsacoustics.com/files/acceptance-limits-jas002903.pdf). [read]
  - Bowing machine, violin open G, 20 string players listening.
  - Accepted transients: ≤ 50 ms with prolonged periods, ≤ 90 ms with multiple flyback.
  - The best-rated attacks had 15–25 ms of flyback.
  - All accepted attacks built up (to −1 dB) in under 130 ms.
  - Attacks from rest, not slurs.
- **Baader, Kazennikov & Wiesendanger (2005).** *Cogn. Brain Res.* 23, 436–443. [abstract] The finger leads or lags the
  bow reversal by −12 to +60 ms without being heard as an error.
- **Kazennikov & Wiesendanger (2009).** *Motor Control* 13(3), 297–309, doi:10.1123/mcj.13.3.297. [abstract] Offsets up
  to 100 ms go unnoticed.
- **Schoonderwaldt & Altenmüller (2014).** *PLoS ONE* 9, e106615. [read] 22 violinists. String crossings lead bow
  changes by ~15° of phase, about 11–21 ms at 3.9–7.5 notes/s (converted here).
- **Visentin, Li, Tardif & Shan (2015).** *PeerJ* 3, e1299, doi:10.7717/peerj.1299. [read]
  - 6 professionals, motion capture, shifts of a third.
  - **The shift takes 294–461 ms** per player (SD 18–62), almost independent of tempo (60–100 bpm).
  - It ends 27–68 ms from the note's time.
  - This is movement time, not the audible glide.
- **Geringer, Allen & MacLeod (2005)** (§3.1): vibrato stops for 0.42–0.50 s around a slurred change (students).
- **VIOLET** (Tian et al., arXiv:2608.07944) *assumes* a perceived delay of 100 ms for slurs and 30 ms for shorts,
  citing Guettler 2002. An assumption, not data. [read]
- Maestre 2009 (PhD, UPF), Maestre & Ramírez, and Pérez-Carrillo model bowing contours. Yang, Chew & Rajab (QMUL)
  model portamento. None of them was found to report transition durations. [abstract]
- **No acoustic measurement of a plain slurred finger change's duration by interval was found, and no study of when the
  new note is heard within a string or sung legato.**

**Data quality.** Indirect. Hand coordination tolerates 50–100 ms. A finger change is nearly instant on the left hand;
a shift's movement takes ~300–450 ms.

**What the fork can take.** SSO's long transitions on small intervals are longer than real finger changes. Its large
intervals look more like real shifts. The heard check measures SSO itself, which is what playback needs. A real-player
measurement would be a target for the *model*: where a real player's pitch arrives against the beat.

**Missing.** Real slurred transitions: the f0 leaves and arrives, by interval (1–12 semitones), note length (125–1000
ms) and finger / shift, then where the beat is heard. Ways to get it:
- the Spheres dataset's legato chromatic scales and the legato phrases of K. 550 / Romeo and Juliet, per section;
- URMP's solo string tracks (overdubbed, but each track is a real player);
- TU Beethoven 8.

Method: pYIN or CREPE f0; leave = the f0 leaves the old note's ±50 cents band; arrive = it enters the new one's;
against the written times from a score alignment. Where the beat lands within the glide needs a listening experiment,
which an agent can't run. In its place, the Polfreman / Wright PAT models could be applied to the pitch-filtered
envelope.

**Value: high.** It is the largest timing error open (40–100 ms), and the data would also help the `own-legato` branch.
**Effort: medium–high**: alignment, f0 and review pages, 2–3 days.

### 2.4 Vibrato onset

**Question.** How long after a note's start does vibrato begin, and how does it develop? Reference data for drawing CC21
lanes; not automatic (owner decision).

**Sources.**
- No measured string vibrato onset delay was found.
- Pedagogy suggests ~250 ms of straight tone. [memory]
- Prame 1994 (singers, §3.1): the rate rises ~15 % at a note's end.
- Geringer et al. 2005: 0.42–0.50 s without vibrato around slurred changes.
- Timmers & Desain 2000 (ICMPC6) and Desain, Honing et al., "Rhythmic aspects of vibrato": the sources to read; their
  site refused access. [abstract]

**Missing.** The onset delay and the vibrato's shape over a note. Datasets: URMP (f0 annotations), the MTG Vibrato
Analysis Dataset (vibrato start / end annotated, AGPL-3.0), and Spheres' sections.

**Value: low** (the owner draws vibrato). **Effort: low–medium.**

### 2.5 Articulation durations (staccato, spiccato, legato overlap)

**Question.** How long do real staccato and spiccato notes sound, by tempo? How much do legato notes overlap? The fork
keeps MS4's note-off for a staccato, the 45 ms clearance of plainPerformance's spiccato attacks, `keepMs` 40, and
`overlap` / `reattackGap` from the map.

**Sources and numbers**
- **Friberg, A. (1991).** "Generative rules for music performance: a formal description of a rule system." *CMJ* 15(2),
  56–71 (https://www.continuum-hypothesis.com/music/Friberg-GenerativeRulesMusic-1991.pdf). [read] KTH rules at k = 1:
  - duration contrast: notes of 200 ms shortened 16.5 ms, of 400 ms 10.5 ms;
  - a phrase's last note +40 ms plus an 80 ms micropause;
  - the last note of a piece +80 ms;
  - repetition gap 35 ms.
  - **Rules tuned by listening, not measured on players.**
- **Friberg, Bresin & Sundberg (2006).** "Overview of the KTH rule system for musical performance." *Adv. Cogn. Psychol.*
  2(2–3), 145–161, doi:10.2478/v10053-008-0052-x. [abstract]
- **Bresin, R. & Battel, G. U. (2000).** "Articulation strategies in expressive piano performance." *JNMR* 29(3),
  211–224. [abstract] Piano staccato sounds about 40 % of the IOI. Legato overlap falls relative to the IOI as the IOI
  grows.
- **Repp, B. H. (1997).** "Acoustics, perception, and production of legato articulation on a computer-controlled grand
  piano." *JASA* 102(3), 1878–1890. [abstract] Overlap is longer for high notes, consonant intervals and slow tempi;
  there are large individual differences.
- **No string-specific staccato or spiccato length data were found** (in this first search; measured since: §2.5a).

**Missing.** Sounding lengths of string staccato / spiccato against IOI and tempo, from the Spheres dataset's K. 550
staccato passages (already aligned for the −3.4 dB study) or URMP.

**Value: low–medium.** SSO's shorts have their own sampled length. This matters for the release of a held note before a
rest, or for the clearance between spiccato notes. **Effort: low** on the K. 550 alignment that exists.

### 2.5a How long real players sound short notes (measured, 2026-10-24)

Branch `research-staccato` (from integrate-1017 329e1fa54a). This is research only: nothing in the map, the generator
or MuseScore changed. The numbers are in `tools/soundlibraries/staccato_lengths.json`. The tools and the articulation
readings are in `tools/soundlibraries/staccato_lengths/`. The audio stays outside the repo and was deleted after
measuring. Linux only, no VM.

**Question.** How long do orchestral players actually sound staccato, staccatissimo, spiccato-type and unmarked short
notes, as a share of the written value and in ms, by tempo, note value and family? How does that compare with what
MuseScore plays?

**Data**
- **TU Berlin anechoic Beethoven 8** (§1.5a; CC BY 4.0). Orchester Wiener Akademie on period instruments. Every track
  is one player, and the strings have 5–7 passes per player, all anechoic. Three passages:
  - **II bars 1–8 and 18–22.** Oboes, clarinets, bassoons and horns (two players each) play repeated sixteenth chords
    marked "pp sempre staccato" (eighth = 88). The played sixteenth is 355 ms. Notes are kept inside runs: the IOI
    before and after is 0.28–0.45 s.
  - **II bars 13–17.** Eighths followed by a rest: winds, cello (6 passes), bass (6 passes).
  - **IV bars 6–10.** Violin I (two players × 7 passes; 9 tracks located). Staccato quarters in bars 6–8, unmarked
    quarters in bar 9, and an unmarked quarter before a rest in bar 10, all pp. The played quarter is about 200 ms.
- **Spheres, Mozart K. 550 / I** (CC BY-SA 4.0). Each section's own close stem, so a section, and its sounding length
  includes the spread between its players. The onsets come from `spheres_k550_staccato.json`, with the readings the
  owner confirmed on 2026-10-15:
  - Violins 1, staccato quarters at f (IOI 305 ms);
  - Celli and Basses, staccato eighths at f (IOI 150 ms);
  - Celli and Basses, unmarked quarters at p followed by rests.
- **Not used:** Aalto (no articulation readings yet) and URMP.

**Readings.** `staccato_lengths/readings_articulation.tsv`.
- The Beethoven 8 marks were read by Claude from page renders and **reviewed by the owner on 2026-10-24** (below); the
  crops are in the private `research-staccato/review/`.
  - Two points are uncertain: whether "sempre staccato" still holds after II's f / sf bars 13–17, and the undotted
    sixteenths in bars 3, 5 and 19–22.
  - By the readings rule, these readings need a review page before any of this changes the map.
- The K. 550 readings are the owner-confirmed ones.

**Method** (`notes.py`; the windows are ArticulationCheck's, `tools/playbackverify/loudness.py`)
- **Envelopes.**
  - Power in 5 ms windows after a 40 Hz high pass.
  - The perceived envelope (ERB filters, 22 / 50 ms), the measure used for SSO's `p10` and `from=`.
- **Note span.** A note runs from its onset to the next onset in the part, minus 15 ms.
- **Length at X dB.** From the first window of the rise that carries the peak to the end of the last window within
  X dB of the peak.
  - **e20** is the requested −20 dB under the peak.
  - **e10** and **p10** are the same at −10 dB on power and on the perceived envelope.
- **Joined notes.** A note still within X dB at the next onset is "joined". Its length is then only a lower bound,
  and no median is given once half the notes are joined.
- **dip.** How far the envelope falls between two notes.
- **Spread between players.** The per-track medians.

**Results** (median ms, and the share of the written value at the played tempo)

| passage | notes / tracks | written | e20 | e10 | p10 | note |
|---|---|---|---|---|---|---|
| B8/II winds, staccato 16ths, all | 690 / 8 | 355 | **125 (0.35)**; players 90–195 | 90 (0.25) | 115 (0.32) | the envelope falls to silence between notes |
| oboe pp / cresc.–f | 90 / 65 | 355 | 125 / 135 | 85 / 110 | 115 / 140 | |
| clarinet pp / cresc.–f | 124 / 68 | 355 | 110 / 143 | 75 / 105 | 100 / 125 | |
| bassoon pp / cresc.–f | 107 / 61 | 355 | 95 / 105 | 70 / 75 | 100 / 110 | |
| horn pp / cresc.–f | 109 / 66 | 355 | 165 / 172 | 110 / 125 | 125 / 140 | |
| B8/II winds, eighth before a rest ("sempre staccato" in force?) | 20 / 8 | 710 | **410 (0.58)** | 320 (0.45) | 305 (0.43) | |
| B8/II cello, unmarked eighth before a rest | 12 / 6 | 710 | 622 (0.88) | **280 (0.39)** | 282 (0.40) | the free string rings after the bow stops |
| B8/II bass, unmarked eighth before a rest | 6 / 6 | 710 | ≥ 555 (half joined) | **178 (0.25)** | 162 (0.23) | ringing |
| B8/IV violin I, staccato quarters pp | 90 / 9 | 200 | ≥ 155 (0.77; 40 % joined) | **88 (0.44)** | 118 (0.59) | players 138–178 (e20) |
| B8/IV violin I, unmarked quarters in a line | 36 / 9 | 200 | ≥ 145 (0.72; 44 % joined) | 82 (0.41) | 125 (0.62) | played like the staccatos |
| B8/IV violin I, unmarked quarter before a rest | 9 / 9 | 200 | 170 (0.85) | 105 (0.52) | 140 (0.70) | |
| K550 violins 1 (section), staccato quarters f | 26 / 1 | 305 | 220 (0.72; 23 % joined) | **110 (0.36)** | 160 (0.52) | dip 29 dB |
| K550 celli / basses (sections), staccato eighths f | 55 / 51 | 145 / 155 | joined | joined (67–73 %) | joined | **dip only 11–12 dB**: the notes never part by 20 dB |
| K550 celli / basses, unmarked quarters p before rests | 18 / 18 | 300 | 995 / 1148 | 495 / 768 | 415 / 525 | low strings ring; held 1.4–1.75 × the written quarter (p10) |

**The owner's review of the Beethoven 8 readings (2026-10-24, https://claude.ai/artifact/GNwFDt1PcNHJ1UWoWugvDc).**
- Right: R01–R05 (the tempo mark; II bars 1–2, every wind staff dotted, "pp sempre staccato"), R12–R17 (the eighths before a
  rest, winds and cello / bass), R19–R24 (IV: tempo, pp, Violin I bars 6–10).
- Can't tell: R06–R07 (II bars 3–8), R08–R11 (II bars 18–22, cresc. to f) and R18 (whether "sempre staccato" holds after
  bars 13–17). No row wrong.
- So the winds' staccato is recomputed from **bars 1–2 alone** (re-measured from the re-fetched audio, deleted again):
  bars 3–8 and 18–22 stay in the json, `readingsConfirmed: false`, and set nothing. The winds' eighths before a rest are
  confirmed as written (no mark), but whether bar 1's "sempre staccato" still applies to them is not (R18).

| winds' staccato sixteenths, II bars 1–2, pp (confirmed) | notes / players | e20 ms (90 % interval) | e10 | p10 |
|---|---|---|---|---|
| oboe | 15 / 1 (oboe 2's track: onsets unreliable, bleed) | **115** [115, 125] | 80 | 110 |
| clarinet | 30 / 2 | **103** [90, 110] | 65 | 95 |
| bassoon | 18 / 2 | **95** [80, 105] | 65 | 100 |
| horn | 26 / 2 | **158** [155, 160] | 115 | 115 |
| all four | 89 / 7 | **115** [100, 155] | 80 | 110 |

The unconfirmed bars agree within their spread (bars 3–8 at pp: 95–175 ms; 18–22 at cresc.–f: 105–172, 10–30 ms longer than
pp), so the confirmed numbers are not an outlier, but they rest on fewer notes (89 against 690) and on pp only. Brass other
than horns and the flutes have no measurement: the horn's and the pooled value are assumptions. Hähnel & Berndt's 173 ms
(their note end at a 66 % loudness drop, earlier than −20 dB) is a cross-check only, not used. 90 % intervals:
a two-level bootstrap over players then notes (summarize.py).

**Literature** (an agent's search, 2026-10-24; PDFs in the private `research-staccato/lit/dl1/`)

Measured:
- **Hähnel & Berndt 2010** (NIME, pp. 277–282). 10 professionals on 14 string, wind and brass instruments, note end at
  the 66 % loudness drop. [read]
  - Staccato 0.57 of the IOI, 173 ms (IQR 160–225).
  - Staccatissimo 0.52, 161 ms (143–189).
  - Unmarked 0.77, tenuto 0.89.
  - Staccato is better described in ms than as a share; no difference between families.
- **Pàmies-Vilà, Hofmann & Chatziioannou 2018** (*Front. Psychol.* 9:617; 10 clarinettists, mouthpiece pressure at
  5 %). [read]
  - Staccato at IOI 500 ms sounds about 198 ms when the tongue stops it (0.40) and about 304 ms when the breath does
    (0.61).
  - At IOI 250 ms it sounds about 166 ms (0.66).
- **Hofmann & Goebl 2014** (*Front. Psychol.* 5:690; 18 saxophonists, tongue–reed contact at IOI 144–250 ms).
  [read via summary]
  - The staccato gap is 0.25–0.29 of the IOI, so about 0.71–0.75 sounds.
- **Piano: Bresin 2000** (thesis), **Bresin & Battel 2000**, **Bresin & Widmer 2000** (KDR), and **Hu et al. 2026**
  (*TISMIR* 9(1)). The key is down for about 0.40 of the IOI (0.20 in Menuetto to 0.39 in Adagio), independent of IOI
  within a piece. This is key-down time, not sound. [read; Hu et al.'s values are read off Fig. 4]
- **Demoucron 2010** (sautillé, IOI ≈ 167 ms). Bow contact is about 75 ms (0.45), and the string sounds longer.
  [values read off a figure]
- **De Poli et al. 1998.** One violinist, unmarked notes 0.79–0.95 of the IOI by style. [read]

Not measured:
- No measured staccato length was found for brass.
- The historical rule (C.P.E. Bach, Quantz) is "less than half the value", a convention.

**What it says**
1. **Staccato length is closer to a fixed time than to a fixed share.**
   - Winds at IOI 355 ms sound 95–170 ms to −20 dB (0.26–0.48); strings at 200–305 ms sound 150–220 ms.
   - This agrees with Hähnel & Berndt's 173 ms and the clarinettists' 166–304 ms.
   - As a share it runs from about 0.3 (slow) to "joined" (fast): at 150 ms IOI a section's staccato eighths dip only
     ~11 dB between notes.
2. **Family.**
   - Horns are the longest winds (165–172 ms) and bassoons the shortest (95–105 ms).
   - Low strings ring after the bow stops (cello and bass e20 two or more times e10). The bowed part is better read at
     −10 dB.
   - Louder (cresc. to f) is a little longer: +10–30 ms.
3. **Unmarked short notes.**
   - A note before a rest is shortened: a wind eighth sounds about 0.45–0.58 of its value, about 300–400 ms of 710.
   - In a fast line of quarters (IOI 200 ms) unmarked notes are played like the staccatos around them.
   - Isolated low-string quarters at p ring past their written value.
4. **Staccatissimo:** no data in these recordings. The literature's 161 ms is barely shorter than staccato.

**Against MuseScore**

| | real (this study) | MuseScore now |
|---|---|---|
| staccato | about 100–220 ms to −20 dB; 0.25–0.45 of the value at IOI 355 ms, 0.4–0.8 at 200–300 ms, joined at 150 ms | MS4.7.5: note-off at **0.50** of the written value at every tempo. The library chooses a short by meant length = 0.50 × written (`shorts/staccato`, `from=`) |
| staccatissimo | 161 ms, 0.52 (literature only) | 0.25 |
| unmarked before a rest | winds about 0.45–0.58 of the value; strings 0.25–0.40 bowed, plus the ring | 0.99 |
| SSO's shorts (VM rest check, Tree mic, so the hall is included), p10 | real p10: winds 100–140, violins 118–160 | Spiccato: Violins 1 273, Violas 281, Celli 316, Basses 433. Wind staccato: oboe 223, flute 296, clarinet 298, bassoon 323, horn 341, trumpet 293. Short 0.5 about 530–550 |

SSO's shorts sound about twice as long as the real notes at −10 dB perceived. Part of that is the hall (the Tree mic).
SSO's dry length (Close mic) was not measured.

**Proposal (for the owner; nothing changed)**
- **a) Meant length of a staccato.** Replace the fixed 50 % with "the shorter of 50 % of the written value and a
  measured absolute":
  - about 0.13 s for winds and 0.16–0.22 s for strings (e20); after the review, from confirmed readings only: winds
    0.095–0.16 s by instrument (built as `[shorts] staccatoLength`, branch `staccato-length`);
  - the same for staccatissimo, about 0.16 s.

  The library would then never pick Short 0.5 for a staccato on a slow note (today: a Violins 1 staccato from 0.86 s
  written). The built-in sounds would play slow staccatos shorter and fast ones barely cut. This is offered as "try
  both, decide by ear".
- **b) A note before a rest.** A note followed by a rest could be meant to sound about 0.5–0.6 of its value (winds).
  For the strings the bowed part is about 0.4, with the ring left to the sample.
  - The data are thin: 20 wind notes and 18 string notes, one passage, and a reading that is not reviewed.
- **c) Measure SSO's shorts on the Close mic (VM).** This separates the hall from the sample before comparing lengths.
- **d) For `[slurs] plainPerformance`'s 45 ms spiccato clearance:** real section staccatos at IOI 150 ms don't part
  (11 dB dips). The clearance suits slower staccatos only.

**Uncertainty**
- **One performance per passage.** TU is one period orchestra (two players per wind part, one or two string players);
  Spheres is one ensemble; there are no modern-instrument anechoic data.
- **Spread between players** is ±30–50 ms (the e20 per-track medians). The −20 dB point of strings includes the free
  ring, and Spheres' close mic some hall.
- **Readings.** The TU readings are not reviewed. The "sempre staccato" reach and the B8/IV positions are located from
  onsets: 9 of 14 tracks were placed and the rest skipped.
- **Tempo and note value can't be separated** with these passages: winds were measured only at IOI 355 ms, strings
  at 145–305 ms.
- **No brass** other than horns, no staccatissimo, and no flute in these passages.

**Value: medium** (it decides which SSO short a staccato plays, and how long the built-in sounds hold it).
**Effort for more:** the Aalto passages with PHENICX onsets plus articulation readings (low); K. 550's other movements
and winds (readings needed; low–medium).

### 2.6 Rubato, phrase-final lengthening, tempo changes

**Question.** How do players shape a rit. / accel.? `tempochange.h` plays MS4's methods (linear, ease in / out,
exponential), with MS4's default factors (rit. 75 %).

**Sources and numbers**
- **Friberg, A. & Sundberg, J. (1999).** "Does music performance allude to locomotion? A model of final ritardandi
  derived from measurements of stopping runners." *JASA* 105(3), 1469–1484, doi:10.1121/1.426687. [read]
  - The model: **v(x) = [1 + (v_end^q − 1) x]^(1/q)**, where x is the normalised score position from the start of the
    rit. (0) to the onset of the last note (1).
  - Tone times: t(x) = q [(1 + kx)^((q−1)/q) − 1] / ((q − 1) k), with k = v_end^q − 1.
  - Fitted to 12 recorded final ritardandi: **mean q = 2.8** (1.2 to above 4), **v_end = 0.32–0.51**, r² 0.98.
  - Listeners preferred q = 2–3.
- **Sundberg, J. & Verrillo, V. (1980).** "On the anatomy of the retard." *JASA* 68, 772–779. [memory] The source data.
- **Honing, H. (2003).** "The final ritard: on music, motion, and kinematic models." *CMJ* 27(3), 66–72. [memory]
  Objects that the curve ignores the number of notes and the rhythm.
- No orchestral (non-final) rit. / accel. shapes were found. Candidates: tempo curves from orchestral recordings (CHARM
  style).

**Fork now.** MS4's methods and factors: the goal is to play like MS4.

**Value: low.** It would be an extra method ("final ritardando, q 2.8") for the owner to choose; a final rit.'s end
tempo of 0.32–0.51 is far slower than MS4's 75 %. **Effort: low.**

---

## 3. Tone and expression

### 3.1 Vibrato rate and width

**Question.** Vibrato rate and width by instrument, dynamic, register and note length, for CC21 lanes drawn by hand and
for judging SSO's own vibrato. Not automatic.

**Sources and numbers**

| source | n, method | rate | width | effects |
|---|---|---|---|---|
| **Geringer & Allen 2004**, *JRME* 52(2), 167–178 [abstract] | 40 violin / cello students | ≈ 5.5 Hz, no instrument difference | violin wider than cello | oscillates about the target pitch |
| **MacLeod 2008**, *JRME* 56(1), 43–54, doi:10.1177/0022429408323070 [abstract] | 40 violin / viola players | +0.32 Hz high against low | **+26 cents high register; +4 cents f against p** | violin a little faster and wider than viola |
| **MacLeod 2010**, *String Research J.* 1(1), 75–83, doi:10.1177/194849921000100105 [abstract] | 4 soloists, commercial recordings | mean 6.63 Hz, changes with dynamic | mean 63 cents peak to peak | wider in the upper register |
| **Geringer, Allen & MacLeod 2005**, *JRME* 53(3), 248–259 [abstract] | 40 students | – | – | every player stops vibrating at a slurred change: 0.42 s (university), 0.50 s (high school) |
| **Geringer, MacLeod & Allen 2010**, *JRME* 57(4), 351–363, doi:10.1177/0022429409350510 [abstract] | 72 listeners | – | – | the heard pitch is the mean frequency |
| **Hu, Kim, Kim, Mackay & Heller 2025**, arXiv:2512.18162 [read] | cello: 54 commercial files, 40 TinySOL | 6.48 ± 0.42 Hz commercial; 5.26 ± 0.28 TinySOL | depth grows with hand position along the string | quotes Meyer 1992 (*Acustica* 76, 283–291): strings 0.2–0.35 semitone peak to peak |
| **Fletcher & Sanders 1967**, *JASA* 41(6), 1534–1544 [abstract] | a few violinists | ≈ 6 Hz | ≈ 30 cents (10–60) | each harmonic's amplitude moves differently |
| **Prame 1994**, *JASA* 96, 1979–1984, doi:10.1121/1.410141 [abstract] | 10 singers | 5.5–6.7 Hz | ±60 cents | the rate rises over the last few cycles (~15 %) |

Pitch scatter between players in a unison (the chorus effect) has been measured only in choirs:
- Ternström 1993: listeners prefer 0–5 cents, tolerate ~14.
- Jers & Ternström: measured 25–30 cents.
- Cuesta et al. 2018: 16–30 cents.

No orchestral string-section measurement was found.

**Fork now.** Vibrato is whatever SSO's sample and its CC21 give. The owner draws it.

**Missing.** Per-instrument absolute widths for viola, cello and bass. Section vibrato and scatter. Note-length
effects. Experiment: an f0 run over URMP and the Spheres sections, rate and width per note longer than 0.5 s, regressed
on pitch, dynamic and length. The same run would also measure SSO's vibrato at each CC21 on the VM.

**Value: low** (owner decision). **Effort: low–medium.**

### 3.2 Portamento

**Sources.**
- **Philip, R. (1992).** *Early Recordings and Musical Style.* CUP. [memory] Qualitative: slides were frequent
  1900–1930 and rare after 1940.
- **Katz, M.** *Capturing Sound* (2004 / 2010), the "phonograph effect". [memory]
- **Leech-Wilkinson, D. (2009).** *The Changing Sound of Music*, CHARM online, ch. 4: slide counts and durations. [not
  reachable]
- **Solé 2025**, arXiv:2502.00030. [read] Beethoven cello sonatas: −35 to −61 % fewer slides after 1970.
- **Solé 2026**, arXiv:2604.22037. [read] Slide gradient ~4000 Hz/s in the early 1900s, ~600 Hz/s late; n = 22
  recordings.
- **Yang, Rajab & Chew 2016**, "AVA", ISMIR 2016, 108–114. [abstract] Interval depends on the score; slope, duration
  and inflection on the performer.

**Fork now.** The Performance portamento at velocity 5, chosen by the notation or by hand (Performance technique). SSO's
own glide is measured (`bendGlide`).

**Missing.** A modern orchestra's rate of use and its durations. Any automatic portamento would be an assumption.

**Value: low.** **Effort: medium.**

### 3.3 Note shapes inside long notes (messa di voce)

**Sources.**
- **Schoonderwaldt, E. (2009).** *Mechanics and Acoustics of Violin Bowing* (KTH PhD). [abstract] Bow speed and bow–bridge
  distance set the level over a note.
- **Schoonderwaldt, Sinclair & Wanderley (2007)**, Enactive conference: bow force in crescendo–decrescendo notes.
  [abstract]
- **Pauget Ballesteros, Lalitte & Fritz (2025)**, HAL hal-05339455. [abstract] Bow speed dominates the note's envelope.
- **Bretos & Sundberg (2003)**, *J. Voice* 17(3), 343–352: messa di voce in 10 sopranos. [abstract]
- **KTH Phrase arch** (Friberg 1995, KTH thesis): a few dB per phrase at k = 1. Tuned by listening, so not data under
  the owner's rule. HANDOFF.md already notes ~1 dB, near threshold.
- A claimed study of long-note envelope clusters in violin–cello duos (peaks at 0.25 / 0.75 of the note) **could not be
  confirmed to exist**. Don't cite it.

**Fork now.** Even levels flattens SSO's swells to each slur's median. There is no target shape.

**Missing.** The real players' envelope of held notes by dynamic and length. Experiment: anechoic or close-mic notes
over 1 s (Aalto, TU Beethoven 8, URMP, the Spheres sections), envelope smoothed over a vibrato period, time normalised,
clustered. It would tell whether flattening is right, i.e. whether real held notes are flat within ~1 dB.

**Value: low–medium.** It checks even levels' premise. **Effort: medium.**

### 3.4 Releases

- **Strings in slurs:** vibrato stops for 0.42–0.50 s around the change (§3.1).
- **The bow stroke's own release:** no study found.
- **Wind releases:** no study found.
- **Legato overlap:** piano only (§2.5).
- **Hall ring:** T60 (§3.5). The fork's tuning-lane tail takes 2 × the measured release to −30 dB (ISO 3382-1's T30
  extrapolation), which is consistent.

**Missing.** Note-off to −20 dB per instrument and articulation in anechoic recordings, the room left out. **Value:
low.** **Effort: low–medium.**

### 3.5 Room and reverb

**Question.** How much room should MS3 / MS4 parts get, and should library parts get any? SSO is recorded in AIR
Lyndhurst Hall with its own mics. The fork's `effects/musereverb` is MS4's.

**Sources and numbers**

Concert halls, from Beranek, L. (2004), *Concert Halls and Opera Houses*, 2nd ed., Springer, as tabulated at
https://akutek.info/concert_hall_acoustics_files/parameters.htm [read]. On that page EDT, G and C80 are computed from
T, V and the distance, not measured.

| hall | V m³ | seats | T occupied, mid (s) | EDT | G dB | C80 dB |
|---|---|---|---|---|---|---|
| Vienna Musikverein | 15 000 | 1680 | 2.0 | 2.0 | 4 | −1 |
| Amsterdam Concertgebouw | 18 780 | 2037 | 2.0 | 1.9 | 3 | −1 |
| Boston Symphony Hall | 18 750 | 2625 | 1.9 | 1.9 | 2 | 0 |
| Berlin Philharmonie | 21 000 | 2218 | 1.9 | 1.8 | 2 | 0 |
| Birmingham Symphony Hall | 25 000 | 2211 | 1.9 | 1.8 | 2 | 0 |
| London Barbican | 17 750 | 2026 | 1.7 | 1.6 | 2 | 0 |
| Royal Festival Hall | 21 950 | 2901 | 1.5 | 1.4 | −1 | 1 |
| Royal Albert Hall | 86 650 | 5222 | 2.4 | 2.3 | −3 | −1 |

- Beranek's measured G (AES Heyser lecture, https://aes2.org/wp-content/uploads/2023/08/AES123heyser-Beranek.pdf)
  [read]: Musikverein T 2.0 s, G 6.5 dB; Boston T 1.9 s, G 4.2 dB.
- The parameters are defined in ISO 3382-1:2009.
- **Lokki, Pätynen, Kuusinen, Vertanen & Tervo (2011).** "Concert hall acoustics assessment with individually elicited
  attributes." *JASA* 130(2), 835–849, doi:10.1121/1.3607422. [abstract] Loudness and distance are the strongest
  perceptual dimensions.
- **Lokki, McLeod & Kuusinen (2020).** "Perception of loudness and envelopment for different orchestral dynamics."
  *JASA* 148(4), 2137–2145, doi:10.1121/10.0002101. [abstract] Perceived loudness and envelopment depend on the dynamic,
  because the spectrum changes with level.

**MuseScore 4's reverb.** `reverbprocessor.cpp` at MS4 v4.5.2 [read], which the fork's `effects/musereverb/
reverbprocessor.cpp` matches (ReverbTimeMs 2200, PreDelay 10 checked in the fork):
- feedback-delay network, quality 4 (24 lines)
- ReverbTimeMs **2200**; TimeLow / Mid / High 100 / 100 / **60 %** (crossovers 400 / 4000 Hz)
- PreDelay **10 ms**; LateRoomScale 0.8; FeedbackTop 8000 Hz
- ERLevel −20, LateLevel −20, DryLevel −60 dB (wet path)
- ModFreq 1 Hz, ModAmp 0.2

So MS4's default tail is **2.2 s mid and 1.32 s above 4 kHz**, a little longer than the Musikverein's or Boston's
occupied 1.9–2.0 s.

**AIR Lyndhurst Hall.**
- RT "just under three seconds" with the canopy (Stewart, D., *Sound On Sound*, July 2024, review of Spitfire's AIR
  Studios Reverb, https://www.soundonsound.com/reviews/spitfire-audio-air-studios-reverb). [read]
- 3–7.5 s adjustable (Kentish Towner, 2015). [read]
- **No measured RT60 by band, and not the canopy setting Spitfire used.**

SSO's Tree / Ambient mics carry Lyndhurst's tail, so adding MS4's 2.2 s on top stacks two rooms. LIVE.md already
notes: no return tracks, "since SSO brings its own room".

**Measured: Lyndhurst as SSO's mics carry it** (2026-10-24, branch `research-reverb`; Windows test VM, kthost of build
#474, Kontakt 8 + SSO, offline 44.1 kHz; `tools/soundlibraries/sso_room.py` → `sso_room.json`; no audio kept).
- **What was rendered.** 55 renders, one per patch × mic configuration: the five string sections (All techniques) and
  Flute, Oboe, Horn, Trumpet and Tenor Trombone Solo, each mic alone at 1.0 (the others 0, as `mic_levels.py` did), plus
  the Tree with the Release control at 1.0. In each: the shortest articulation (strings Spiccato, winds / brass staccato)
  at velocity 100, CC1 100, on 3 pitches. The shorts were played released after 0.15 s (3 takes; as MuseScore plays a
  staccato) and held 5.5 s (2 takes; the sample's own tail). The strings also played pizzicato both ways, and every
  patch played a 2.5 s Long and its release (2 takes).
- **Analysis.** Octave bands by a 4th-order Butterworth filter. Lundeby's floor and crossing point, Schroeder
  integration, EDT / T20 / T30 (ISO 3382-1). The analysis stops 0.35 s before the sample's end. Each mic's relative
  impulse response against the Close mic comes from regularised deconvolution over the held notes. Close is not dry,
  so this is an approximation.
- **A property of SSO's samples.** Every sample ends in digital silence about 2.2–2.5 s (shorts) or 2.7–3.1 s (Long
  releases) after it starts, with a fade over its last ~0.3 s. Held shorts reach that end at −45 to −50 dB broadband
  and −65 to −80 dB at 1 kHz. Below 125 Hz a recording floor (hall rumble) sits ~45 dB under the peak, so the 125 Hz
  values are unreliable. 8 kHz is unreliable too, because a short note has little energy there.

**The room by mic: medians over patches, T30 in s (500 Hz / 1 kHz / 2 kHz / 4 kHz)**

| mic | strings, held short | winds / brass, held short | as played (short released at 0.15 s), strings | Long's release, strings | EDT 1 kHz, held | level 0.5 s after the peak (strings / winds) | lag after Close | DRR / C80 of h (strings) |
|---|---|---|---|---|---|---|---|---|
| Close | 1.92 / 1.93 / 1.87 / 1.68 | 2.01 / 1.76 / 1.58 / 1.42 | 1.48 / 1.52 / 1.45 / 1.34 | 1.54 / 1.31 / 1.23 / 1.14 | 0.60 (strings) / 0.29 | −26 / −38 dB | 0 | (reference) |
| Tree (the default mix) | 2.05 / 2.02 / 1.92 / 1.71 | 2.09 / 2.11 / 1.92 / 2.04 | 1.60 / 1.54 / 1.49 / 1.36 | 1.59 / 1.54 / 1.43 / 1.33 | 1.84 / 1.63 | −19 / −21 dB | +6.8 ms (winds +16.6) | −7.7 dB / +2.8 dB |
| Ambient | 2.10 / 2.02 / 1.97 / 1.79 | 2.18 / 2.14 / 2.04 / 1.95 | 1.67 / 1.56 / 1.55 / 1.44 | 1.69 / 1.53 / 1.47 / 1.38 | 2.17 / 2.19 | −17 / −16 dB | +21.7 ms (winds +32.0) | −11.8 dB / −0.3 dB |
| Outrigger | 2.08 / 1.94 / 1.87 / 1.71 | 2.18 / 2.13 / 1.91 / 2.74 | 1.59 / 1.53 / 1.48 / 1.38 | 1.49 / 1.45 / 1.43 / 1.34 | 1.86 / 1.84 | −17 / −21 dB | +10.2 ms (winds +17.3) | −9.3 dB / +2.9 dB |
| Leader (strings only) | 2.03 / 1.92 / 1.88 / 1.66 | — | 1.54 / 1.49 / 1.46 / 1.35 | 1.53 / 1.35 / 1.34 / 1.13 | 1.03 | −27 dB | −0.1 ms | −3.4 dB / +8.8 dB |

What the measurements show:
- **One room, about 2.0 s at mid frequencies, on every mic.** The Tree's held-short T30 at 1 kHz lies between 1.98 and
  2.12 s for nine of the ten patches; Tenor Trombone Solo gives 1.63 s. The tail falls towards the highs: about
  1.7–1.8 s at 4 kHz, a ratio of 0.85 to the mid value. That is a concert hall's decay, near Beranek's best halls
  occupied (1.9–2.0 s, table above), and shorter than the "just under three seconds" quoted for the empty hall with the
  canopy. An orchestra in the room absorbs, and the canopy setting is unknown.
- **The mics differ in direct sound, not in decay time.** Close's EDT is 0.6 s (strings) or 0.3 s (winds) against its
  T30 of 1.9 s: a strong direct sound over the same tail. Its tail 0.5 s after the peak is 7–17 dB lower than the
  Tree's. The Tree and Outrigger give C80 ≈ +3 dB and Ambient ≈ 0 dB (the deconvolved h against Close; a hall seat's
  C80 is −1 to +1, Beranek's table). So the Tree sounds a little closer than a good seat, and Ambient like one. Leader
  is a second close mic (C80 +9 dB, no lag).
- **Pre-delay.** The mics keep their natural delays: Tree +7 ms after Close for the strings and +17 ms for the winds,
  who sit further back. Ambient +22 / +32 ms, Outrigger +10 / +17 ms. The first reflection can't be resolved by this
  method, because the notes' own 2–20 ms structure fills those bins.
- **The release shortens what is heard.** As MuseScore plays a staccato (the key released after 0.15 s), Kontakt's
  release envelope cuts the tail. T30 drops from ~2.0 to ~1.5 s on every mic and the decay ends sooner. The Long's
  release also measures ~1.5 s. With Release at 1.0 instead of the patch default 0.4961, the Long's release starts
  louder: +6 dB at 0.2 s (strings −0.4 against −6.6 dB; winds −5.5 against −8.2). Its T30 hardly changes (1.51 / 1.53
  s against 1.59 / 1.54 at 500 Hz / 1 kHz on the Tree). A held short's decay is the same at either Release.

**MuseScore 4's reverb, measured the same way** (`tools/soundlibraries/room/musereverb_ir.cpp`: the fork's
`effects/musereverb` on a unit impulse, default parameters):
- T30 by band: 2.22 s (125 Hz), 2.23 (250), 2.24 (500), 2.20 (1 k), 2.03 (2 k), 1.53 (4 k), 0.96 (8 k).
- First energy at 25 ms, peak at 30 ms: the 10 ms PreDelay plus the early reflections.
- With MuseReverb's send 0.3, the wet energy lies 13.4 dB under the dry signal, so DRR ≈ +13 dB. That is much drier
  than any SSO room mic: the Tree's h gives −7.7 dB.
- The decay is 0.2 s longer than SSO's mid, and 0.25 s shorter than SSO's at 4 kHz (1.53 against ~1.75 s).

**What MuseScore does with it now.** `Vst3Synth` is a dry guest synthesizer (`dry()` true, `MasterSynthesizer::process`
adds guests after the effects), so **no MuseScore reverb reaches library parts**. MS3 / MS4 parts get MuseReverb. The
plain Live set has no return tracks (LIVE.md).

**Implication (a proposal, not applied; the owner decides what is heard):**
- **Library parts:** add no reverb. SSO's mics already carry a ~2.0 s hall at a seat-like clarity, and a second ~2 s
  room on top would double the tail (two FDNs in series lengthen the EDT). If the owner wants more room, the right lever
  is SSO's own: Ambient or Outrigger in place of the Tree (same T, more late energy: −17 against −19 dB at 0.5 s, C80
  ≈ 0 against +3 dB). Mic compensation keeps the balance, so the change is "tone and sense of space" only. Close or
  Leader alone gives a drier, nearer sound with the same tail behind it, not a dry signal: its T30 is still ~1.9 s, 7–17
  dB lower.
- **Staccato tails:** a staccato's tail is cut to T ~1.5 s by the key-up, which a real hall wouldn't do. If the owner
  hears shorts as too dry, a measured fix would hold the key of a short (or a note-off after the sample's end) instead
  of adding reverb. This would be a new setting, measured first.
- **Mixed scores (built-in + library parts):** to put MS3 / MS4 parts in SSO's room, MuseReverb could be set to
  ReverbTimeMs ≈ 2000, TimeHigh ≈ 85 % (SSO's 4 k / mid ratio) and PreDelay ≈ 15–20 ms (the Tree's lag behind Close
  plus a few ms), with a send giving DRR near the Tree's (−8 dB, i.e. far more wet than 0.3 gives). These are untried
  numbers to offer, not defaults: MS4's own sound is the MS4 path's goal (§5).
- **The crest check:** the master trim's 14.5 dB crest was measured on rendered SSO notes with their room, so it already
  includes the room's build-up. Nothing changes.

**Uncertainty.** The spread between patches is ±0.1 s at mid frequencies (one outlier, the tenor trombone). The 125 Hz
and 8 kHz values are unreliable (floor, little energy). The deconvolved DRR / C80 rest on Close being near-dry: they
are relative numbers, comparable between mics, not absolute ISO values. Only the patch defaults were measured (Release
0.4961 and Tightness as set up, velocity 100 / CC1 100: forte). One sample set per articulation, each mic heard alone.

---

## 4. Datasets

Licences and contents were checked on the dataset pages ([read]) unless marked. **The crucial property is whether the
gain is known**: normalised samples can't give levels.

| rank | dataset | what the fork could measure | gain | licence |
|---|---|---|---|---|
| 1 | **OrchideaSOL** (Cella, Ghisi, Lostanlen, Lévy, Fineberg & Maresz 2020, "OrchideaSOL: a dataset of extended instrumental techniques for computer-aided orchestration", ICMC 2020, arXiv:2007.00763; Zenodo 10.5281/zenodo.3686252 / 3740399) | technique against ordinario per string instrument (§1.1), sforzato (§1.2), pp/mf/ff spans, mf tone (§1.7) | un-normalised, gains reverted where reported, inferred per family otherwise; close mic only; levels trustworthy within one instrument and session, uncertain across sessions | metadata CC BY 4.0; audio Ircam Forum licence (free, non-commercial, account needed). TinySOL (ordinario only) CC BY 4.0 |
| 2 | **Spheres** (Garcia-Martinez, Diaz-Guerra, Anderson, Falcon-Perez, Cabañas-Molero, Virtanen, Carabias-Orti & Vera-Candeas, arXiv:2511.21247, IEEE TASLP; Zenodo 10.5281/zenodo.17347681; 46.6 GB) | sections in context: accents, staccato, legato transitions, vibrato; section scales (legato / pizz / staccato, quiet and loud) against the leader's solo scales (§1.3) | not documented; parts recorded separately in sessions; ask the authors for gain sheets | CC BY-SA 4.0 |
| 3 | **TU Berlin anechoic Beethoven 8** (Böhm, Ackermann & Weinzierl 2020, "A multi-channel anechoic orchestra recording of Beethoven's Symphony No. 8 op. 93", *JAES*, doi:10.17743/jaes.2020.0056; dataset doi:10.14279/depositonce-6729.2; mvts 1, 2, 4, ~1.9 GB) | a professional (period-instrument) orchestra recorded **groupwise**: timing within a group (§2.2), accents, legato, balance if gains are documented | **calibrated**: SPL at 1 m in the mic direction from Table 3's mic and artistic gains (the JAES paper; used in §1.5a) | **CC BY 4.0** |
| 4 | **Aalto anechoic symphony recordings** (Pätynen, Pulkki & Lokki 2008, "Anechoic recording system for symphony orchestra", *Acta Acustica u. Acustica* 94(6), 856–865; https://users.aalto.fi/~ktlokki/Sinfrec/sinfrec.html) and **PHENICX-Anechoic** (Miron, Carabias-Orti, Bosch, Gómez & Janer 2016, *J. Electr. Comput. Eng.*; Zenodo 840025) | Mozart aria, Beethoven 7 / I (b. 1–53), Bruckner 8 / II (b. 1–61), Mahler 1 / IV (b. 1–85); one player per part, divisi strings; **relative levels of every part at its written dynamic** (§1.5), accents (§1.2), techniques in Mahler (§1.1) | one gain setting for every recording: confirmed on the download page ("exactly the same gain settings"; §1.5a). MP3 front mic (+ mic 8 horns, mic 5 bassoons / tuba); PCM on request | "free for academic research" (no formal licence); PHENICX annotations **CC BY 4.0** (Zenodo 840025). Public repo: derived numbers only |
| 5 | **TU Berlin anechoic instrument DB** (Weinzierl, Vorländer et al., doi:10.14279/depositonce-19858, v3 2024) + **directivity DB** (Ackermann, Brinkmann & Weinzierl 2023, arXiv:2307.02110) | calibrated sound power pp/ff, spectral features (§1.4, §1.7), directivity (§1.6) | calibrated (ISO 3745) | DB: "In Copyright" + item licence.txt (read it before committing per-note tables); directivity DB CC BY-SA 4.0; the 2018 paper CC BY 4.0 |
| 6 | **QUARTET / EEP** (MTG; Marchini et al. 2014; Papiotis et al. 2016; Maestre et al., *IEEE MultiMedia* 2017) | real simultaneous quartet timing (§2.2), bowing parameters, vibrato | contact mics, relative | not found; ask MTG |
| 7 | **University of Iowa MIS** (https://theremin.music.uiowa.edu/mis.html) | pp/mf/ff single notes, arco / pizz, vibrato / non-vibrato | fixed within a session | "without restriction" |
| 8 | **Dagstuhl ChoirSet** (Rosenzweig, Cuesta, Weiß, Scherbaum, Gómez & Müller 2020, *TISMIR* 3(1), 98–110, doi:10.5334/tismir.48); **ChoralSingingDataset** (Cuesta et al., ICMPC 2018) [abstract] | divisi / unison summation (§1.3), scatter within a section (§3.1) | close mics, relative | DCS CC BY-NC-SA 4.0; CSD: check |
| 9 | **URMP** (Li, Liu, Dinesh, Duan & Sharma 2018, *IEEE Trans. Multimedia*, arXiv:1612.08727; 12.5 GB) | 44 chamber pieces, parts recorded separately, note and f0 annotations: vibrato (§3.1), legato (§2.3), staccato length (§2.5) | not documented | not stated (research use [memory]) |
| 10 | **MTG Vibrato Analysis Dataset** (Cavdir et al. 2016, Zenodo 10.5281/zenodo.182244) | violin vibrato start / end and rate (§2.4) | – | AGPL-3.0 |
| 11 | **Good-sounds** (Romaní Picas et al. 2015, AES 138th Conv.; CORA doi:10.34810/DATA2314) | attack quality labels | undocumented | CC BY 4.0 (CORA 2025); CC BY-NC 4.0 (Zenodo v1) |
| 12 | **RWC Musical Instrument DB** (Goto et al.) | dynamics and styles per instrument | undocumented | research only; the 2026 Zenodo re-release (17177919) excludes it |
| 13 | **Philharmonia Orchestra samples** (https://philharmonia.co.uk/audio-sound-samples/) | wide technique coverage, in a room | strings measured 2026-10-29 (§1.1a): not peak-normalised (violin pp → ff 19 dB), but technique files differ from arco-normal by ±3–7 dB in controls (other sessions): corroboration only; the bass clarinet was normalised | free use, no resale or redistribution "as is" |
| 14 | MusicNet (Thickstun, Harchaoui & Kakade 2017, Zenodo 5120004), Bach10 (Duan, Pardo & Zhang 2010), TRIOS (Fritsch & Plumbley 2013) | aligned labels on mixes, small sets: timing statistics within one instrument | no | mixed |

**Not useful:**
- NSynth (commercial libraries' arbitrary levels).
- EnsembleSet and SynthSOD: **rendered from Spitfire's BBC Symphony Orchestra**, so measuring them is circular.
- MAESTRO (CC BY-NC-SA 4.0, piano velocity only) and SMD: only for the Grand Piano's velocity curve.
- MUSDB18, MedleyDB, OpenMIC, the Schubert Winterreise Dataset: pop, too few classical pieces, labels only, or piano
  and voice.
- McGill MUMS: a commercial DVD, gain unknown.
- Vienna Symphonic Library: commercial, excluded.
- ChoraleBricks (*TISMIR* 2025, a wind band, 193 tracks): maybe for wind blending.

**No public annotated corpus of real violin legato / portamento transitions exists.**

---

## 5. MuseScore 4.7.5's note model

The MS4 path's goal is to *sound like MS4*, not like players (`libmscore/ms4playback.h`: "Quirks of the original are
kept"). Its sources are MS4's code, not literature:
- engraving/playback/metaparsers
- the mpe ArticulationMap
- FluidSequencer
- playbackcontext

Its tables are generated by `tools/ms4playback/gen_tables.py`. No research is needed. The one research link is that
MS4's articulation offsets are reused as defaults elsewhere (plainAccents 1: +5.3 dB). This research supports the
value: the Spheres sf at f is +5.0 [+3.8, +6.0].

---

## 6. Top 15 research and measurement tasks

Ranked by value for the fork (what it fixes among the open items) against effort. Every result goes through the
readings rule (a review page) where it relies on reading a score or a picture, and is offered to the owner as a
setting.

1. **String technique levels from OrchideaSOL.**
   - *Fixes:* sul tasto / flautando (16–54 dB too soft), harmonics, col legno, Bartók, tremolo, mute and pizzicato
     levels (§1.1).
   - *Source:* OrchideaSOL, Zenodo 10.5281/zenodo.3686252 (metadata CC BY; audio by Ircam Forum account, which the owner
     creates).
   - *Method:*
     1. Check the gains: each instrument's ordinario pp → ff span against Clark & Luce's and TU's.
     2. Per instrument, technique and pitch, take the perceived loudness (`tools/playbackverify/loudness.py`, STL)
        minus ordinario at the same dynamic. Sul tasto comes from `sul_tasto_tremolo` − `tremolo`.
     3. Pool by the two-level bootstrap of `technique_offsets.py` → heldOffset candidates.
   - Interim, until measured: the physical bound of −3 to −9 dB for sul tasto / flautando, an assumption.
2. **Real legato transitions by interval and note length.**
   - *Fixes:* the transition model's 40–100 ms misses (§2.3).
   - *Source:* the Spheres dataset's legato scales and legato phrases (K. 550, Romeo and Juliet), per section; URMP's
     string tracks as a second source.
   - *Method:* pYIN / CREPE f0. Leave and arrive (±50 cents bands) against the score-aligned written times, by interval,
     length and position in the slur. Compare with SSO's measured `bendGlide` and the heard check's arrivals.
3. **Level-dependent onset threshold.**
   - *Fixes:* the −6 / −15 dB onset question behind plainAttack, onsets and the audit (§2.1).
   - *Source:* Vos & Rasch 1981 via Villing 2010 (L_PC = L_peak − 3.18 − 0.17 L_SL), with Danielsen 2021 and Bechtold &
     Senn 2018 for the spread.
   - *Method:*
     1. Assume a listening level (ff ≈ 80 dB SPL L_eq in the hall, §1.5).
     2. Set each note's sensation level from its calibrated level.
     3. Recompute plainAttack / onsets with the threshold that follows.
     4. Re-run the heard check (VM) and the audit.
     5. Offer it as a setting.
4. **Tone against dynamic (Stage B).**
   - *Fixes:* SSO's brightness doesn't follow the written dynamic once CC11 sets the loudness (§1.7).
   - *Source:* TU Berlin `3_Audio_Features.csv` (spectral centroid at pp / ff per note); Pätynen & Lokki 2010's slopes
     per dynamic; OrchideaSOL's mf.
   - *Method:* SSO's centroid per CC1 from VM renders of the existing CC1 sweeps. Then the CC1 that matches the real
     centroid at each marking, per section and register.
5. **Accent types (">", marcato, sfz) and wind accents.**
   - *Fixes:* the ">" = sf assumption (§1.2).
   - *Source:* Romeo and Juliet in the Spheres dataset (strings' ">", winds, brass); Aalto Mahler 1 / IV and Bruckner
     8 / II; TU Beethoven 8.
   - *Method:* the accent-spheres pipeline (STL peak in [−0.05, +0.40] s against the unmarked neighbours' median, same
     part and dynamic), by sign and dynamic.
6. **Sul tasto / con sord. / col legno in context.**
   - *Fixes:* the same gap as task 1, with real sections.
   - *Source:* the Spheres dataset's scores, Aalto Mahler 1 / IV.
   - *Method:* find the marked passages (review page with crops), then marked against unmarked neighbours at the same
     written dynamic.
7. **In-context balance of the whole orchestra.**
   - *Fixes:* the unconfirmed winds / brass against strings (§1.5).
   - **Done 2026-10-23 (§1.5a)** for flutes, oboes, clarinets, bassoons, horns, trumpets; trombones / tuba still open. Applied to the map 2026-10-24
     (flutes, oboes, horns, trumpets; BALANCE_LOG.md › Iteration 15).
   - *Source:* the Aalto anechoic recordings (one gain for all parts: confirmed); TU Beethoven 8 (calibrated).
   - *Method:* each part's loudness at its written dynamics, corrected by TU's directivity for the front mic (Ackermann
     2023, CC BY-SA), against `sectionLevel2`'s prediction for the same parts. Settle the licence (Aalto: derived
     numbers only; ask the authors).
8. **Seat-averaged directivity.**
   - *Fixes:* the one-direction DI's 2–4 dB instability for strings and woodwinds (§1.6).
   - *Source:* Ackermann, Brinkmann & Weinzierl 2023 (arXiv:2307.02110, CC BY-SA 4.0); Ackermann 2024 for the
     uncertainty.
   - *Method:* average the third-octave directivity over the audience's solid angle, loudness-weighted, per instrument
     and register. Compare with the current DI; recompute sectionLevel2 as an option.
9. **Uncertainty and labelling fixes in BALANCE_LOG.md / PLAYBACK_SETTINGS.md.**
   - *Fixes:* stated uncertainties that are too small; ISO 23591's forte mislabelled as measured (§1.4).
   - *Source:* Weinzierl et al. 2018 (TU against Meyer: ff ±2, pp ±6.8 dB); Rindel 2022 (ISO forte = ff − D / 4).
   - *Method:* documentation edits. The owner may buy ISO 23591 for Table A.1, a second ff dataset on 39 instruments.
10. **Section against one player, the divisi premise.**
    - *Fixes:* the 10 log10 N / divisi assumption (§1.3).
    - *Source:* the Spheres dataset's section chromatic scales against the leaders' solo scales (same session and
      dynamic); Dagstuhl ChoirSet as corroboration.
    - *Method:* loudness of section against solo, compared with 10 log10 N (N from the session notes); unison mix
      against the power sum of the singers.
11. **Lyndhurst's reverberation through SSO's mics.** *Done 2026-10-24 (§3.5 › Measured): ~2.0 s mid on every mic,
    no extra reverb for library parts proposed.*
    - *Fixes:* the room for mixed built-in and library scores; the crest check (§3.5).
    - *Source:* SSO renders on the VM; Beranek 2004 for the targets.
    - *Method:* `sso_room.py`: shorts held and released, pizzicato, Long releases, each mic alone; Schroeder
      integration, EDT / T20 / T30 per octave; MuseReverb's own impulse response.
12. **Mute attenuation per instrument.**
    - *Fixes:* con sord. levels for violas, celli, basses (§1.1).
    - *Source:* da Silva Evangelista 2019 (UFMG, six violins; hdl 1843/32235) and 2025 (*Acta Acustica*, CC BY);
      OrchideaSOL's Sordina folders.
    - *Method:* read the two texts' tables; OrchideaSOL sordina against ordinario per pitch and dynamic, after the
      session-gain check.
13. **String staccato / spiccato sounding lengths.** Done 2026-10-24 for K. 550 and TU Beethoven 8: §2.5a.
    - *Fixes:* the plain spiccato clearance, short lengths (§2.5).
    - *Source:* the Spheres K. 550 staccato passages already aligned for the −3.4 dB study; URMP.
    - *Method:* the sounding length (onset to −20 dB) against IOI and tempo.
14. **Held-note envelopes of real players.**
    - *Fixes:* checks even levels' premise that a held legato line is flat (§3.3).
    - *Source:* Aalto / TU Beethoven 8 anechoic parts; the Spheres sections.
    - *Method:* envelope over notes longer than 1 s, smoothed over a vibrato period, time-normalised, clustered by
      dynamic and length. Report the median swell in dB.
15. **Timing within and between sections.**
    - *Fixes:* confirms the ARRIVAL / RHYTHM thresholds (§2.2); the "basses first" option.
    - *Source:* TU Beethoven 8 (recorded groupwise); QUARTET / EEP (ask MTG for the licence); Pätynen, Tervo & Lokki
      2012 for the orchestra.
    - *Method:* onset asynchrony per group from close mics, SD against Rasch's 30–50 ms. Re-derive or confirm the 0.10
      IOI share (Desain & Honing 2003 / Honing 2006 not confirmed here).

**Not in the list, and why:**
- **Percussion levels per marking** (mallets, celesta, bells): no data exists, and only real instruments could give it.
  They stay assumptions.
- **Vibrato onset / rate and portamento:** the owner draws them.
- **A final-ritardando curve** (Friberg & Sundberg 1999, q 2.8, v_end 0.32–0.51): a cheap extra tempo-change method if
  the owner wants one (§2.6).
