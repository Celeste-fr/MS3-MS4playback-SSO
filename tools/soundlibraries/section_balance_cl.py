#!/usr/bin/env python3
"""The section balance at playing levels, [levels] sectionBalance 2 ("Clark & Luce balance"): the map's sectionLevel2
(gen_spitfire_sso.py reads this script's output).

    python3 -I section_balance_cl.py <clark_luce_levels.json> <clark_luce_table2.json> <sso_cc1_curves.json>
                                     <sso_winds_cc1_semitone.json> <sso_winds_section_balance.json> <sso_sound_dynamics.json>
                                     <sso_gap_cc1_semitone.json> <3_Audio_Features.csv> <tu_directivity.json | ->
                                     <tu_decaying.json> <sso_percussion_levels.json> <out sso_cl_section_balance.json>
                                     [<sso_strings_ensemble.json | -> [<spheres_timpani.json | ->]]

Since 2026-10-28 the wind and brass curves are the every-semitone sweeps (sso_winds_cc1_semitone.json,
sso_gap_cc1_semitone.json; a section the gap file lacks, Solo Violin 2, is taken from sso_gap_cc1_curves.json beside it);
the 3-semitone files (sso_winds_cc1_curves.json, sso_gap_cc1_curves.json) give the old numbers: pass them instead.

The owner, 2026-10-14: "I prefer all the balance levels and the dynamic levels loudness to be based on real data".
Real side: Clark & Luce 1965, Fig. 2 (clark_luce_levels.json): each instrument's scales played pp / mf / ff as in
orchestral music, dB SPL at 10 m in its loudest direction; to sound power Lw = Lp + 20 log10(10 m) + 11 - DI_max
(DI_max: TU Berlin's directivity in that loudest direction, ff); mp = pp + 2/3 (mf - pp) (mp two of the three equal
steps from pp to mf); a patch = one player + 10 log10 N (strings 16 / 14 / 12 / 10 / 8, solo 1, a2 2, a6 6). SSO side:
the same as sectionLevel (section_balance_winds.py): the patch's Long at CC1 64 (loudDb), linear in dB between measured
pitches, its mean over the semitones of that patch's "common" range in sso_winds_section_balance.json. D = SSO - real,
M = -(D - the five string sections' mean D) (the strings centred, the winds on their scale), as sectionLevel.
Corroboration (one player against a string player: woodwind / brass): this +2.6..+4.0 / +11.3..+11.9 (pp..mf; since
iteration 8's directivity by dynamic, the solo patches' Lw against the strings' per player: +4.2 / +2.3 woodwind, +14.8 /
+14.5 brass at pp / mf, before +3.9 / +2.2 and +11.4 / +13.5); the Spheres dataset's sections +1.4 / +14.3; Skaalevik 2019 (orchestra dosimetry fit) +3.2 / +4.2 in average sessions (rests
included), +4.2 / +12.2 in forte passages; sectionLevel (TU single notes + Meyer mp) +11.9 / +18.9.
The French horn (since 2026-10-15, docs/BALANCE_LOG.md): Fig. 2's horn is not the mean of its Table II scales (every
other instrument's is) and follows player 2, whose levels are 20 .. 40 dB under player 1's; its pp / mf are player 1's
(clark_luce_table2.py: Table II, the same mean as Fig. 2's), mp from them as above; no ff (player 1 played none).
Each player counted once (since 2026-10-15, iteration 3): Fig. 2 + clark_luce_table2.json's playerWeight.
Directivity by dynamic (since 2026-10-15, iteration 5, docs/BALANCE_LOG.md; with tu_directivity.json, "-": ff's for
every marking as before): each marking's own DI in the same direction, the receiver loudest at ff (Clark & Luce kept one
microphone direction per instrument): pp's DI at pp, ff's at ff, mf's linear in dB between them at mf's place on the
instrument's Table I span ((mf - pp) / S); mp from the markings' Lw as before.
Patches with no Clark & Luce instrument (Bass Clarinet, Contrabassoon, Bass Trombone Solo / a2; since 2026-10-15,
iteration 6, docs/BALANCE_LOG.md): their sibling's Clark & Luce levels plus the TU Berlin database's difference between
the two instruments' sound power at mp (each placed between its own pp and ff as sectionLevel places it,
section_balance_winds.py mpFrac), averaged over the semitones both played (SIBLING_TU, "tuRatio": one source's ratio,
measured the same way for both, not an assumption; the Bass Clarinet since iteration 13 the median of four independent
sources, SOURCED_RATIO, 'ratioUsed'; 'tuRatio' keeps TU's). Before: the family member's change from sectionLevel to
sectionLevel2 added to their sectionLevel (an assumption, "plays like its family").
Decaying instruments (since 2026-10-15, iteration 6, docs/BALANCE_LOG.md): the harp from TU Berlin's sound power on the
Clark & Luce scale. TU's measure for a decaying tone is the fast-weighted (125 ms) pressure squared averaged over the
whole recording (tu_transient.py: it reproduces the CSV's harp values within 0.005 dB), so per note the energy (that
level x the recording's length) is window-free; SSO's harp (sso_percussion_levels.json) at mp's velocity 64 the same
energy over the same span after the note-on as TU's recording after its onset; TU's mp by rule D between its pp and ff
(sectionLevel's placement for an instrument Meyer has no figures for), D = SSO - TU averaged over SSO's measured pitches.
TU to Clark & Luce: M2 = median over OFFSET_SET of (M2 + sectionLevel's D, sso_winds_section_balance.json) - D: the nine
solo winds with a Clark & Luce instrument of their own (single players like the harp; TU against Clark & Luce agrees
better there, RMS 3.6 dB, than over every patch with both, 4.3; the patches whose real side comes from TU, SIBLING_TU,
are left out). Uncertainty about +-4 dB. The timpani were computed the same way but not applied: TU's D2 series are
"6 dynamics" without markings, so mp's place (step 3 of 6, if pp .. ff) is unknown by a whole step (about 7 dB), more than
the correction; kept as the Timpani entry's "tuBerlin" (without the Spheres file: "decayingNotApplied").
The timpani (since 2026-10-15, iteration 10, docs/BALANCE_LOG.md): written markings measured in a real orchestra, the
Spheres dataset's Romeo and Juliet (spheres_timpani.py -> spheres_timpani.json, the 14th argument): the energy per hit at
the written p and f on the Clark & Luce scale (the strings' lines at mf set the scale); mp one of the three marking steps
from p to f (SPHERES_MP_STEPS); SSO's side the hit's energy at velocity 64 over its whole ring (sso_percussion_levels.json
cumE), power-averaged over every measured key; M2 = -(SSO - real - centre) as every patch. Its realRange 2 data
("realRange": realSteps = (f - p) x 5 / 3, longLevel = SSO's hit energy by velocity less velocity 64's per written
pitch) go to the map with it (gen_spitfire_sso.py REAL_RANGE_DECAYING).
The grand piano (since 2026-10-15, iteration 11): Meyer 2009, 3.4.2 (two-voice scales, open lid, no pedal: pp about 88,
ff about 104 dB re 1 pW), mp two of the five steps from pp, one voice 10 log10 2 under two (two equal voices' powers add),
to the Clark & Luce scale by the median of (Clark & Luce's one-player mp - Meyer's in-play mp, placed the same way) over
the twelve instruments Meyer and Clark & Luce share (MEYER: RMS 2.5 dB after the offset, 3.9 without); SSO's side the
Grand Piano's Direct at velocity 64, its loudest 50 ms (loudDb, as the winds' Long), mean over the swept keys 21 .. 105
(sso_percussion_levels.json "Grand Piano").
Percussion group check (iteration 7, "percussionGroup", not applied): SSO's six-player section (Timpani at key 50 = D2,
Bass Drum, Piatti, Snare 1, Tam Tam, Triangle 1, each one hit at mf's velocity 80) energetically summed against the string
family (the five sections' Long at CC1 64, loudDb as above, + their M2) by the 50 ms peak and by the energy over 1 s and
the whole ring; Skaalevik 2019's percussion re strings -8 .. -2 dB (median -3.5): a group offset only outside that range.
Patches without sectionLevel (since 2026-10-15, docs/BALANCE_LOG.md › Iteration 2; before, they played SSO's own level
against an orchestra whose levels are all corrected): GAP. Real side: their Clark & Luce instrument (the solo strings,
the Motif brass) or a family member's (an assumption: Piccolo, Alto / Bass Flute by the flute, Contrabass Clarinet by
the clarinet, Contrabass Trombone and Cimbasso by the trombone, Contrabass Tuba by the tuba). SSO side (since
2026-10-15, iteration 4): the patch's Long at CC1 64 (loudDb) from its own sweep (sso_gap_cc1_curves.json, made as
sso_winds_cc1_curves.json), linear in dB between measured pitches, its mean over every semitone of the sweep (no
database range to meet: Clark & Luce's levels are scales over the whole playing range). Iteration 2's stand-in (the
dynamics check's Long at CC1 64 at the test pitch plus the median of (full-range mean - dynamics check) over the
patches that have both) is kept as a check only ("standIn", each gap patch's "standInM2").
Strings Ensemble (since 2026-10-15, docs/BALANCE_LOG.md › Iteration 9; before, it played SSO's own level): its Long is
the five sections' own samples mixed, each section where it has the note, faded in and out at its range's ends
(sso_strings_ensemble.json, strings_ensemble.py: cross-correlation with each section's recording and a harmonic-by-
harmonic fit). Per key p (every even key 24 .. 96): target = the power sum over the sections in it of (the section's
Long at p, loudDb, + its M2 + its weightDb: how far the ensemble has it faded there), the balanced sections playing what
the ensemble plays; M2(p) = target - the ensemble's loudDb at p; the patch's M2 the mean over the keys (as every patch's:
one offset over the range). Checks in "ensemble": "binary" (every section in at full level, faded or not), "rms" (the
sustain's power instead of loudDb), the keys' SD.
"""
import json
import math
import statistics
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import section_balance_winds as sbw  # noqa: E402

STRINGS = {'Violins 1': ('Violin', 16), 'Violins 2': ('Violin', 14), 'Violas': ('Viola', 12), 'Celli': ('Cello', 10),
           'Basses': ('Double bass', 8)}
CL = [('flute', 'Flute'), ('oboe', 'Oboe'), ('cor anglais', 'English horn'), ('clarinet', 'Clarinet'),
      ('bassoon', 'Bassoon'), ('horn', 'French horn'), ('trumpet', 'Trumpet'), ('trombone', 'Trombone'), ('tuba', 'Tuba')]
# patch: (its TU database instrument, its sibling's) for the tuRatio; the Clark & Luce instrument is the sibling's
OFFSET_SET = ['Flute Solo', 'Oboe Solo', 'Cor Anglais', 'Clarinet Solo', 'Bassoon Solo', 'Horn Solo', 'Trumpet Solo',
              'Tenor Trombone Solo', 'Tuba Solo']
# Meyer 2009, ch. 3: rapid two-octave scales, every note sounded, pp / ff, sound power dB re 1 pW (the Meyer rule's P column,
# docs/PLAYBACK_SETTINGS.md › Real range), for the patches whose instrument Clark & Luce also measured (iteration 11)
MEYER = {'Violins 1': ('3.3.1.2', 74, 94), 'Violas': ('3.3.2.2', 73, 91), 'Celli': ('3.3.3.2', 74, 96),
         'Basses': ('3.3.4.2', 79, 96), 'Flute Solo': ('3.2.1.2', 82, 94), 'Oboe Solo': ('3.2.2.2', 83, 95),
         'Clarinet Solo': ('3.2.3.2', 77, 97), 'Bassoon Solo': ('3.2.4.2', 81, 96), 'Horn Solo': ('3.1.1.2', 86, 107),
         'Trumpet Solo': ('3.1.2.2', 89, 104), 'Tenor Trombone Solo': ('3.1.3.2', 89, 105), 'Tuba Solo': ('3.1.4.2', 93, 108)}
MEYER_MP = 2 / 5            # mp two of the five equal marking steps from pp to ff (realRange's steps)
# Meyer 2009, 3.4.2: a grand piano's scales in two voices, open lid, no pedal: pp "of the order of 88 dB", ff "around 104"
MEYER_PIANO = ('3.4.2', 88, 104, 2)
# the timpani's mp from its written p and f in the Spheres recording: one of the three marking steps p -> mp -> mf -> f
SPHERES_MP_STEPS = 1 / 3
TIMPANI_KEY_SHIFT = 12      # gen_spitfire_sso.py KEY_SHIFT: SSO's timpani keys sound an octave under their number
GROUP = [('Timpani', 50), ('Bass Drum', 48), ('Piatti', 48), ('Snare 1', 48), ('Tam Tam', 48), ('Triangle 1', 48)]
SKAALEVIK = {'re strings': [-8, -4, -2, -3], 'columns': ['quiet', 'average', 'loud sessions', 'forte passages'],
             'source': 'Skaalevik 2019, slide 19 (review page https://claude.ai/artifact/JKYSTkAFHEv4hjZviRhCEn): '
                       'family totals re the whole orchestra, strings (50) -3 / -4 / -4 / -8, percussion (6) -11 / -8 / '
                       '-6 / -11'}
SIBLING_TU = {'Bass Clarinet': ('Bass clarinet modern', 'Clarinet modern'),
              'Contrabassoon': ('Contrabassoon modern', 'Bassoon modern'),
              'Bass Trombone Solo': ('Bass trombone modern', 'Tenor trombone modern'),
              'Bass Trombones a2': ('Bass trombone modern', 'Tenor trombone modern')}
# Sourced ratios (since 2026-10-15, iteration 13, docs/BALANCE_LOG.md): where independent sources give a patch's level over its
# sibling's, the ratio at mp is their median instead of TU Berlin's alone (the owner checked the sources, 2026-10-15, review
# page https://claude.ai/artifact/6LwMCt4X82fVsiJgZjHCz8). Bass Clarinet over Clarinet, dB (sound power unless noted):
#   ISO 23591 Annex A forte, Burghauser & Spelda via NS 8178, printed in Skaalevik 2023 (Forum Acusticum), Table 4: -1
#   O'Brien 2013, practice-session sound power, Skaalevik 2023, Table 2: 0
#   TU Berlin ff (Weinzierl et al. 2018's database, tuRatio['ff']): +0.5
#   University of Iowa MIS, one player (C. Bellomy), SPL at one point, mf, octaves 3 and 4: -1.9 .. +0.5, midpoint -0.7
#       (octave 5 -3.6 outside the range the owner confirmed; research3/lowwinds/results.json)
# Not used: Detzner 2015's -2 (TU's recordings again, not independent); TU's mp (+4.88) rests on one player's loud pp.
# The Contrabassoon and the Bass Trombones keep SIBLING_TU (nothing better for the contrabassoon; the bass trombone's +1.71 is
# corroborated by Iowa MIS bass - tenor trombone, +1.1 pp / +1.0 mf / +2.0 ff, and ISO 23591 Annex A +1).
SOURCED_RATIO = {'Bass Clarinet': [('ISO 23591 Annex A forte (Burghauser & Spelda via NS 8178; Skaalevik 2023, Table 4)', -1.0),
                                   ("O'Brien 2013 practice sessions (Skaalevik 2023, Table 2)", 0.0),
                                   ('TU Berlin ff (Weinzierl et al. 2018)', 0.5),
                                   ('University of Iowa MIS mf, octaves 3-4, midpoint of -1.9 .. +0.5', -0.7)]}
SOURCED_NOT_INDEPENDENT = {'Bass Clarinet': 'Detzner 2015: -2 (the same TU Berlin recordings)'}
# patch: (Clark & Luce instrument, players, family assumption)
GAP = {'Solo Violin 1': ('Violin', 1, False), 'Solo Violin 2': ('Violin', 1, False), 'Solo Viola': ('Viola', 1, False),
       'Solo Cello': ('Cello', 1, False), 'Motif Horns a4': ('French horn', 4, False), 'Motif Trumpets a3': ('Trumpet', 3, False),
       'Motif Trombones a5': ('Trombone', 5, False), 'Piccolo': ('Flute', 1, True), 'Alto Flute': ('Flute', 1, True),
       'Bass Flute': ('Flute', 1, True), 'Contrabass Clarinet': ('Clarinet', 1, True),
       'Contrabass Trombone': ('Trombone', 1, True), 'Cimbasso Solo': ('Trombone', 1, True),
       'Cimbassi a2': ('Trombone', 2, True), 'Contrabass Tuba': ('Tuba', 1, True)}


def players(name):
    for n in (2, 6):
        if name.endswith(' a%d' % n):
            return n
    return 1


def instrument(name):
    n = name.lower()
    return next((inst for key, inst in CL if key in n), None)


def interp(table, p):
    if p in table:
        return table[p]
    ks = sorted(table)
    if p <= ks[0]:
        return table[ks[0]]
    if p >= ks[-1]:
        return table[ks[-1]]
    a = max(k for k in ks if k < p)
    b = min(k for k in ks if k > p)
    return table[a] + (table[b] - table[a]) * (p - a) / (b - a)


def at64(section):
    return {int(p): pt['loudDb'] for p, pts in section['points'].items() for pt in pts
            if pt['cc'] == 64 and pt.get('loudDb') is not None}


def lw(levels, inst, table2, di=None):
    """one player's sound power (dB re 1 pW) at pp / mp / mf / ff (the horn: player 1's pp / mf, no ff); Fig. 2 with
    each player counted once (table2's playerWeight); di: {TU label: {'pp': DI, 'ff': DI}} (each marking's own DI), else
    diMax (ff's) for every marking"""
    v = levels[inst]
    t = table2.get(inst)
    w = table2.get('playerWeight', {}).get(inst, {})
    lp = {'pp': t['pp'], 'mf': t['mf']} if t else {m: v[m] + w.get(m, 0.0) for m in ('pp', 'mf', 'ff')}
    d = di.get(v['tu']) if di else None
    if d:
        dis = {'pp': d['pp'], 'ff': d['ff'], 'mf': d['pp'] + (lp['mf'] - lp['pp']) / v['tableI'] * (d['ff'] - d['pp'])}
    else:
        dis = {m: v['diMax'] for m in ('pp', 'mf', 'ff')}
    out = {m: x + 20 * math.log10(10) + 11 - dis[m] for m, x in lp.items()}
    out['mp'] = out['pp'] + 2 / 3 * (out['mf'] - out['pp'])
    return out


def tuRatio(power, lab, sib):
    """the TU database's sound power of lab less sib's, at mp (sectionLevel's placement) and at pp / ff, averaged over
    the semitones both played at both dynamics"""
    a, b = power[lab], power[sib]
    lo = max(min(a['pp']), min(a['ff']), min(b['pp']), min(b['ff']))
    hi = min(max(a['pp']), max(a['ff']), max(b['pp']), max(b['ff']))
    fa, fb = sbw.mpFrac(lab), sbw.mpFrac(sib)
    mp = lambda t, f, p: interp(t['pp'], p) + f * (interp(t['ff'], p) - interp(t['pp'], p))
    ps = range(lo, hi + 1)
    out = {'mp': sum(mp(a, fa, p) - mp(b, fb, p) for p in ps) / len(ps)}
    for dyn in ('pp', 'ff'):
        out[dyn] = sum(interp(a[dyn], p) - interp(b[dyn], p) for p in ps) / len(ps)
    out = {k: round(v, 2) for k, v in out.items()}
    out.update({'common': [lo, hi], 'mpFrac': [round(fa, 4), round(fb, 4)], 'instrument': lab, 'sibling': sib})
    return out


def psum(xs):
    return 10 * math.log10(sum(10 ** (x / 10) for x in xs))


def energyAt(note, cumT, w):
    return note['cumE'][min(range(len(cumT)), key=lambda j: abs(cumT[j] - w))]


def decaying(tuD, perc, out, balance):
    """(harp entry, timpani entry): the decaying instruments on the Clark & Luce scale"""
    dtu = {k: v['D'] for k, v in balance['instruments'].items()}
    x = [out['patches'][n]['M2'] + dtu[n] for n in OFFSET_SET]
    off = statistics.median(x)
    res = [v - off for v in x]
    offset = {'value': round(off, 2), 'set': OFFSET_SET, 'rms': round((sum(r * r for r in res) / len(res)) ** 0.5, 2),
              'max': round(max(map(abs, res)), 2)}
    harp, cumT = perc['patches']['Harp']['notes'], perc['patches']['Harp']['cumT']
    tu = {int(k): v for k, v in tuD['harp'].items() if 'pp' in v and 'ff' in v}
    frac = sbw.mpFrac('Double action harp modern')
    rows = []
    for p in sorted({n['pitch'] for n in harp}):
        k = min(tu, key=lambda q: abs(q - p))
        if abs(k - p) > 1:
            continue
        t = tu[k]
        w = statistics.mean(t[d]['lengthS'] - t[d]['onsetS'] for d in ('pp', 'ff'))
        e = statistics.median(energyAt(n, cumT, w) for n in harp if n['pitch'] == p and n['velocity'] == 64)
        mp = t['pp']['energy'] + frac * (t['ff']['energy'] - t['pp']['energy'])
        rows.append({'pitch': p, 'tuPitch': k, 'windowS': round(w, 2), 'sso': round(e, 2), 'tuMp': round(mp, 2),
                     'd': round(e - mp, 2)})
    d = statistics.mean(r['d'] for r in rows)
    h = {'M2': round(off - d, 2), 'M1': None, 'instrument': 'Double action harp modern (TU Berlin)', 'players': 1,
         'D': round(d, 2), 'dPitchSd': round(statistics.pstdev(r['d'] for r in rows), 2), 'mpFrac': round(frac, 4),
         'pitches': rows, 'offset': offset, 'uncertaintyDb': 'about +-4 (the offset\'s RMS 3.6, the pitches\' SD / sqrt(n), '
                                                              'mp placed by rule D)'}
    tim, cumT = perc['patches']['Timpani']['notes'], perc['patches']['Timpani']['cumT']
    steps = [v for k, v in tuD['timpani'].items() if '6 dynamics' in v['description'] and k.endswith('_03')]
    w = min(statistics.mean(v['lengthS'] - v['onsetS'] for v in steps), cumT[-1])
    e = statistics.median(energyAt(n, cumT, w) for n in tim if n['pitch'] == 50 and n['velocity'] == 64)
    mp = statistics.mean(v['energy'] for v in steps)
    t = {'M2': round(off - (e - mp), 2), 'D': round(e - mp, 2), 'tuMp': round(mp, 2),
         'tuSeries': [v['energy'] for v in steps], 'sso': round(e, 2), 'key': 50, 'windowS': round(w, 2),
         'why': 'not applied: TU\'s six steps carry no markings; step 3 as mp is a guess worth a whole step (about 7 dB)'}
    return h, t


def piano(perc, out, centre):
    """the Grand Piano on the Clark & Luce scale from Meyer 2009's in-play levels (iteration 11)"""
    rows = {}
    for name, (sec, pp, ff) in MEYER.items():
        v = out['patches'][name]
        lw = {k: x - 10 * math.log10(v['players']) for k, x in v['lw'].items()}
        mp = pp + MEYER_MP * (ff - pp)
        rows[name] = {'meyerSection': sec, 'meyer': {'pp': pp, 'mp': round(mp, 2), 'ff': ff},
                      'clMp': round(lw['mp'], 2), 'diffMp': round(lw['mp'] - mp, 2)}
    diffs = [r['diffMp'] for r in rows.values()]
    off = statistics.median(diffs)
    rms = lambda xs: (sum(x * x for x in xs) / len(xs)) ** 0.5
    sec, pp, ff, voices = MEYER_PIANO
    real = pp + MEYER_MP * (ff - pp) - 10 * math.log10(voices) + off
    notes = perc['patches']['Grand Piano']['notes']
    pitches = sorted({n['pitch'] for n in notes})
    at = {p: statistics.mean(n['loudDb'] for n in notes if n['pitch'] == p and n['velocity'] == 64) for p in pitches}
    sso = statistics.mean(at.values())
    mid = statistics.mean(v for p, v in at.items() if 45 <= p <= 81)
    return {'M2': round(-(sso - real - centre), 2), 'M1': None, 'instrument': 'Grand piano (Meyer 2009, %s)' % sec,
            'players': 1, 'realMp': round(real, 2), 'sso': round(sso, 2), 'ssoPitches': {str(p): round(v, 2) for p, v in at.items()},
            'ssoMid45to81': round(mid, 2), 'M2ifMid': round(-(mid - real - centre), 2),
            'offset': {'value': round(off, 2), 'rms': round(rms([d - off for d in diffs]), 2),
                       'max': round(max(abs(d - off) for d in diffs), 2), 'rmsNoCorrection': round(rms(diffs), 2),
                       'rows': rows},
            'uncertaintyDb': 'about +-3 (the offset\'s RMS 2.5; SSO\'s mean over the whole keyboard against the middle '
                             '45 .. 81: M2ifMid; Meyer\'s levels are a scale\'s, SSO\'s one note\'s loudest 50 ms)'}


def timpani(sp, perc, centre, tuEntry):
    """the timpani on the Clark & Luce scale from the Spheres dataset's Romeo and Juliet (iteration 10)"""
    e = sp['soundEnergyDb']
    steps = SPHERES_MP_STEPS
    real = e['p'] + (e['f'] - e['p']) * steps
    tim, cumT = perc['patches']['Timpani']['notes'], perc['patches']['Timpani']['cumT']
    keys = sorted({n['pitch'] for n in tim})
    at = {k: statistics.mean(n['cumE'][-1] for n in tim if n['pitch'] == k and n['velocity'] == 64) for k in keys}
    sso = 10 * math.log10(statistics.mean(10 ** (v / 10) for v in at.values()))
    vel = lambda v: 10 * math.log10(statistics.mean(10 ** (n['cumE'][-1] / 10) for n in tim if n['velocity'] == v))
    ssoAt = {v: vel(v) for v in (32, 64, 80, 112)}
    ssoP = ssoAt[32] + (ssoAt[64] - ssoAt[32]) * (48 - 32) / 32
    ssoF = ssoAt[80] + (ssoAt[112] - ssoAt[80]) * (96 - 80) / 32
    # [levels] realRange 2 (iteration 10): the written p -> f is three of the five equal steps pp -> ff, so S = (f - p) x 5 / 3;
    # longLevel: SSO's hit energy at each velocity less velocity 64's, per written pitch (the key less keyShift's 12)
    long = {}
    for k in keys:
        base = statistics.mean(n['cumE'][-1] for n in tim if n['pitch'] == k and n['velocity'] == 64)
        long[str(k - TIMPANI_KEY_SHIFT)] = {str(v): round(statistics.mean(n['cumE'][-1] for n in tim if n['pitch'] == k
                                                                           and n['velocity'] == v) - base, 2)
                                            for v in (32, 64, 80, 112)}
    realRange = {'sectionSpan': {'db': round((e['f'] - e['p']) * 5 / 3, 1),
                                 'source': 'the Spheres dataset\'s Romeo and Juliet, written p and f (spheres_timpani.json)'},
                 'long': long}
    return {'M2': round(-(sso - real - centre), 2), 'M1': None, 'instrument': 'Timpani (the Spheres dataset)',
            'realRange': realRange,
            'players': 1, 'realMp': round(real, 2), 'realP': e['p'], 'realF': e['f'], 'k': sp['k'], 'kRms': sp['kRms'],
            'sso': round(sso, 2), 'ssoKeys': {str(k): round(v, 2) for k, v in at.items()},
            'M2atP': round(-(ssoP - e['p'] - centre), 2), 'M2atF': round(-(ssoF - e['f'] - centre), 2),
            'spanPtoF': {'real': round(e['f'] - e['p'], 2), 'sso': round(ssoF - ssoP, 2)},
            'tuBerlin': tuEntry,
            'uncertaintyDb': 'about +-3 (the scale K\'s RMS over the calibration parts, one passage per marking, the '
                             'seats\' sweep correction)'}


def group(perc, strings):
    """the percussion group against the string family, by three measures (not applied)"""
    out = {'velocity': 80, 'stringsFamilyLoudDb': round(psum(strings.values()), 2), 'skaalevik': SKAALEVIK, 'hits': {}}
    for name, key in GROUP:
        ns = [n for n in perc['patches'][name]['notes'] if n['pitch'] == key and n['velocity'] == 80]
        cumT = perc['patches'][name]['cumT']
        out['hits'][name] = {'peak': statistics.median(n['loudDb'] for n in ns),
                             'energy1s': statistics.median(energyAt(n, cumT, 1.0) for n in ns),
                             'energyRing': statistics.median(n['cumE'][-1] for n in ns)}
    fam = out['stringsFamilyLoudDb']
    for m in ('peak', 'energy1s', 'energyRing'):
        out[m + 'ReStrings'] = round(psum(v[m] for v in out['hits'].values()) - fam, 2)
    lo, hi = min(SKAALEVIK['re strings']), max(SKAALEVIK['re strings'])
    out['decision'] = ('no change: the energy (Skaalevik\'s are Leq, energy) within %d .. %d' % (lo, hi)
                       if lo <= out['energy1s' + 'ReStrings'] <= hi else 'outside: an offset is due')
    return out


def ensemble(ens, m2):
    """Strings Ensemble's M2 from the sections it plays at each key (Iteration 9)"""
    rows, variants = [], {'binary': [], 'rms': []}
    for p, row in sorted(ens['keys'].items(), key=lambda kv: int(kv[0])):
        parts = row['weights']
        target = psum(row['parts'][n]['loudDb'] + m2[n] + w for n, w in parts.items())
        m = target - row['loudDb']
        rows.append({'pitch': int(p), 'sections': parts, 'targetDb': round(target, 2), 'ssoDb': row['loudDb'], 'M2': round(m, 2)})
        variants['binary'].append(psum(row['parts'][n]['loudDb'] + m2[n] for n in parts) - row['loudDb'])
        variants['rms'].append(psum(row['parts'][n]['rmsDb'] + m2[n] + w for n, w in parts.items()) - row['rmsDb'])
    ms = [r['M2'] for r in rows]
    mean = statistics.mean(ms)
    return {'M2': round(mean, 2), 'M1': None, 'instrument': 'the five string sections (Iteration 9)', 'players': 60,
            'keys': rows, 'keySd': round(statistics.pstdev(ms), 2), 'keyRange': [round(min(ms), 2), round(max(ms), 2)],
            'binary': round(statistics.mean(variants['binary']), 2), 'rms': round(statistics.mean(variants['rms']), 2),
            'uncertaintyDb': 'the sections\' own (their M2, about +-2) plus the mean\'s: keys\' SD / sqrt(37), the variants'}


def main():
    (levelsPath, table2Path, curvesPath, windsPath, balancePath, dynamicsPath, gapPath, csvPath, diPath, tuDecayPath,
     percPath, outPath) = sys.argv[1:13]
    ensPath = sys.argv[13] if len(sys.argv) > 13 and sys.argv[13] != '-' else None
    spheresPath = sys.argv[14] if len(sys.argv) > 14 else '-'
    power = sbw.power(csvPath)
    di = None
    if diPath != '-':
        di = {k: {'pp': v['diPp'], 'ff': v['diFf']} for k, v in json.load(open(diPath, encoding='utf-8'))['instruments'].items()}
    ratio = {name: tuRatio(power, *labs) for name, labs in SIBLING_TU.items()}
    sourced = {}
    for name, rows in SOURCED_RATIO.items():
        vals = [v for _, v in rows]
        sourced[name] = {'mp': round(statistics.median(vals), 2), 'range': [min(vals), max(vals)],
                         'sources': [{'source': src, 'db': v} for src, v in rows],
                         'notIndependent': SOURCED_NOT_INDEPENDENT[name], 'uncertaintyDb': 'about +-2 (the sources spread)',
                         'tuMp': ratio[name]['mp']}
        ratio[name] = dict(ratio[name], mp=sourced[name]['mp'])
    gap = json.load(open(gapPath, encoding='utf-8'))['sections']
    # the every-semitone gap file (2026-10-28) lacks Solo Violin 2: its curve comes from the 3-semitone file beside it
    oldGap = os.path.join(os.path.dirname(os.path.abspath(gapPath)), 'sso_gap_cc1_curves.json')
    if os.path.exists(oldGap) and os.path.abspath(gapPath) != os.path.abspath(oldGap):
        for k, v in json.load(open(oldGap, encoding='utf-8'))['sections'].items():
            gap.setdefault(k, v)
    dynamics = json.load(open(dynamicsPath, encoding='utf-8'))
    levels = json.load(open(levelsPath, encoding='utf-8'))['levels']
    table2 = {k: v for k, v in json.load(open(table2Path, encoding='utf-8')).items() if k in levels or k == 'playerWeight'}
    strings = json.load(open(curvesPath, encoding='utf-8'))['sections']
    winds = json.load(open(windsPath, encoding='utf-8'))['sections']
    balance = json.load(open(balancePath, encoding='utf-8'))
    patches = {}
    for s, (inst, n) in STRINGS.items():
        patches[s] = (inst, n, strings[s], balance['strings'][s])
    for name, v in balance['instruments'].items():
        patches[name] = (instrument(name), players(name), winds.get(name), v)
    d, real, sso64 = {}, {}, {}
    for name, (inst, n, curve, b) in patches.items():
        if not inst:
            continue
        lo, hi = b['common']
        sso = at64(curve)
        sso64[name] = sum(interp(sso, p) for p in range(lo, hi + 1)) / (hi - lo + 1)
        add = ratio[name]['mp'] if name in ratio else 0.0
        real[name] = {k: x + 10 * math.log10(n) + add for k, x in lw(levels, inst, table2, di).items()}
        d[name] = sso64[name] - real[name]['mp']
    centre = sum(d[s] for s in STRINGS) / len(STRINGS)
    check = lambda name: dict(dynamics[name]['Long']['curve'])[64]
    both = [name for name in sso64 if 'Long' in dynamics.get(name, {}) and name not in ratio]
    standIn = statistics.median(sso64[name] - check(name) for name in both)
    err = [check(name) + standIn - sso64[name] for name in both]
    out = {'method': __doc__.strip().split('\n\n', 1)[1], 'patches': {}}
    for name, (inst, n, curve, b) in patches.items():
        if inst:
            m = -(d[name] - centre)
            out['patches'][name] = {'M2': round(m, 2), 'M1': b['M'], 'instrument': inst, 'players': n,
                                    'lw': {k: round(x, 2) for k, x in real[name].items()}}
            if name in ratio:
                out['patches'][name]['tuRatio'] = dict(ratio[name], mp=sourced[name]['tuMp']) if name in sourced else ratio[name]
                if name in sourced:
                    out['patches'][name]['ratioUsed'] = sourced[name]
    out['standIn'] = {'db': round(standIn, 2), 'patches': len(both), 'sd': round(statistics.pstdev(err), 2),
                      'max': round(max(map(abs, err)), 2)}
    for name, (inst, n, family) in GAP.items():
        r = {k: x + 10 * math.log10(n) for k, x in lw(levels, inst, table2, di).items()}
        sso = at64(gap[name])
        lo, hi = min(sso), max(sso)
        mean = sum(interp(sso, p) for p in range(lo, hi + 1)) / (hi - lo + 1)
        out['patches'][name] = {'M2': round(-(mean - r['mp'] - centre), 2), 'M1': None, 'instrument': inst,
                                'players': n, 'familyAssumption': family, 'sso': 'sweep, pitches %d-%d' % (lo, hi),
                                'standInM2': round(-(check(name) + standIn - r['mp'] - centre), 2),
                                'lw': {k: round(x, 2) for k, x in r.items()}}
    tuD = json.load(open(tuDecayPath, encoding='utf-8'))
    perc = json.load(open(percPath, encoding='utf-8'))
    out['patches']['Harp'], tuTimpani = decaying(tuD, perc, out, balance)
    if spheresPath != '-':
        out['patches']['Timpani'] = timpani(json.load(open(spheresPath, encoding='utf-8')), perc, centre, tuTimpani)
    else:
        out['decayingNotApplied'] = {'Timpani': tuTimpani}
    if 'Grand Piano' in perc['patches']:
        out['patches']['Grand Piano'] = piano(perc, out, centre)
    out['percussionGroup'] = group(perc, {s: sso64[s] + out['patches'][s]['M2'] for s in STRINGS})
    if ensPath:
        out['patches']['Strings Ensemble'] = ensemble(json.load(open(ensPath, encoding='utf-8')),
                                                      {s: out['patches'][s]['M2'] for s in STRINGS})
    with open(outPath, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1)
        fh.write('\n')
    for name, v in out['patches'].items():
        print('%-22s M1 %6s -> M2 %+6.2f%s' % (name, '%+.2f' % v['M1'] if v['M1'] is not None else '-', v['M2'],
                                                '  (TU ratio to %s: %+.2f at mp, %+.2f pp, %+.2f ff)' % (
                                                    v['tuRatio']['sibling'], v['tuRatio']['mp'], v['tuRatio']['pp'],
                                                    v['tuRatio']['ff']) if v.get('tuRatio') else
                                                '  (family assumption: %s)' % v['instrument'] if v.get('familyAssumption') else ''))
    h, g = out['patches']['Harp'], out['percussionGroup']
    print('harp D %+.2f (pitch SD %.2f), offset %.2f (rms %.2f) -> M2 %+.2f' % (
        h['D'], h['dPitchSd'], h['offset']['value'], h['offset']['rms'], h['M2']))
    t = out['patches'].get('Timpani')
    if t:
        print('timpani (Spheres): real mp %.2f (p %.2f, f %.2f), SSO %.2f -> M2 %+.2f (at p %+.2f, at f %+.2f; span p->f real '
              '%.2f, SSO %.2f; TU Berlin step 3 of 6 gave %+.2f)' % (t['realMp'], t['realP'], t['realF'], t['sso'], t['M2'],
                                                                  t['M2atP'], t['M2atF'], t['spanPtoF']['real'],
                                                                  t['spanPtoF']['sso'], t['tuBerlin']['M2']))
    pn = out['patches'].get('Grand Piano')
    if pn:
        print('piano (Meyer): offset %+.2f (RMS %.2f, none %.2f), real mp %.2f, SSO %.2f -> M2 %+.2f (middle keys %+.2f)' % (
            pn['offset']['value'], pn['offset']['rms'], pn['offset']['rmsNoCorrection'], pn['realMp'], pn['sso'], pn['M2'],
            pn['M2ifMid']))
    print('percussion group re strings: peak %+.2f, energy 1 s %+.2f, ring %+.2f: %s' % (
        g['peakReStrings'], g['energy1sReStrings'], g['energyRingReStrings'], g['decision']))
    if 'Strings Ensemble' in out['patches']:
        e = out['patches']['Strings Ensemble']
        print('Strings Ensemble M2 %+.2f (keys SD %.2f, %+.2f .. %+.2f; binary %+.2f, rms %+.2f)' % (
            e['M2'], e['keySd'], e['keyRange'][0], e['keyRange'][1], e['binary'], e['rms']))
    print('stand-in: %+.2f dB, SD %.2f, max %.2f over %d patches' % (standIn, out['standIn']['sd'], out['standIn']['max'], len(both)))


if __name__ == '__main__':
    main()
