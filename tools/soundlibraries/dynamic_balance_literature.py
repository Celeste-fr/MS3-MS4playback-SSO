#!/usr/bin/env python3
"""String section balance per dynamic: M(s, d) for d = pp p mp mf f ff.

    python3 -I dynamic_balance_literature.py <3_Audio_Features.csv> <sso_sound_range.json> <sso_cc1_curves.json> <out.json>

The section balance model M (one dynamic, CC1 64 = mp) per section s, averaged over every semitone p of the
section's SSO Long range that the TU Berlin database also measured, centred over the five sections:

    M(s) = real_player(s, p) + 10 log10 N_s - SSO(s, p, CC1)

here repeated for every dynamic d with
    real_player(s, p, d) = pp(p) + f_d * (ff(p) - pp(p))   in dB (TU Berlin database, sound power, single tones)
    SSO(s, p, d)         = loudDb of All techniques Long at CC1 = velocity = cc(d)   (sso_cc1_curves.json)
    cc(d)                = the fork's expressionLevel(ordinaryLevel(d)) (libmscore/ms4playback.cpp):
                           pp 32, p 48, mp 64, mf 80, f 96, ff 112
and the fraction f_d (where d sits between pp and ff, in dB) taken from the literature below instead of the
0.4 guess (MuseScore's six-mark scale read as linear in dB). The fraction sets are in FRACTIONS; each is
documented where it is defined. No fraction is invented: each comes from a measurement or a stated rule.

Inputs are read as data only. Inputs not in the repository: the TU Berlin CSV (CC BY-NC-SA 4.0, derived numbers
only) and sso_cc1_curves.json (branch sso-cc1-curves, commit 329681d3a2: `git show
private/sso-cc1-curves:tools/soundlibraries/sso_cc1_curves.json`).
"""
import csv
import json
import math
import sys

SECT = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
INST = {'Violins 1': 'Violin', 'Violins 2': 'Violin', 'Violas': 'Viola', 'Celli': 'Violoncello',
        'Basses': 'Double bass'}
DBLAB = {'Violin': 'Violin modern', 'Viola': 'Viola modern', 'Violoncello': 'Cello modern',
         'Double bass': 'Double bass modern'}
N = {'Violins 1': 16, 'Violins 2': 14, 'Violas': 12, 'Celli': 10, 'Basses': 8}   # Spitfire SSO product page
DYN = ['pp', 'p', 'mp', 'mf', 'f', 'ff']
# libmscore/ms4playback.cpp: ordinaryLevel pp 3750 ... ff 6250, expressionLevel = 16 + (level - 3250) / 500 * 16
CC = {d: 16 + (3750 + 500 * i - 3250) // 500 * 16 for i, d in enumerate(DYN)}
SHIPPED_M = {'Violins 1': -3.81, 'Violins 2': -2.66, 'Violas': -2.12, 'Celli': 2.77, 'Basses': 5.81}

# --- the literature (violin only; nothing found for viola, cello or double bass that could be read) ---------------
# Demoucron 2008, doctoral thesis KTH / UPMC "On the control of virtual violins", ch. 6 "Detache at different
# dynamic levels" (p. 212 of the thesis, Figs. 6.4-6.5): one professional violinist, G4 on the D string, whole notes
# (4 s) with the whole bow, bow velocity held at about 13 cm/s; recorded audio (sound level of the recording,
# relative dB); "the sound level increased by 6.1 dB from mf to ff and decreased by 6.9 dB from mf to pp".
DEMOUCRON = {'pp': -6.9, 'mf': 0.0, 'ff': 6.1}
# Askenfelt 1988, STL-QPSR 29(1) 1-30, "Measurement of the bowing parameters in violin playing", sec. III.A.1(d)
# and Fig. 3d: two professional violinists, open G string, detache whole notes (about 2 s), f / mf / p, level =
# top-plate vibration (acceleration) near the bass-side bridge foot (string displacement checked within 1 dB):
# f -> mf about 5 and 6 dB, mf -> p 4 and 3 dB (9 dB p..f for each player; 10 dB on a repeat of one player).
ASKENFELT = [{'p': -4.0, 'mf': 0.0, 'f': 5.0}, {'p': -3.0, 'mf': 0.0, 'f': 6.0}]
# Clark & Luce 1965 (JAES 13(2) 151-157) as reported by Askenfelt 1988, p. 9 (secondary: the paper itself could not
# be read): violin, "a typical difference of 14 dB between fortissimo and pianissimo appropriate for typical
# orchestra music".
CLARK_LUCE_PP_FF = 14.0


def fractions():
    """{set name: {dyn: fraction of (ff - pp) in dB}, with how each was made}"""
    out = {}
    # A (main): Demoucron's mf; p, mp between pp and mf and f between mf and ff in equal dB steps (a rule: the
    # thesis measured only pp, mf, ff).
    span = DEMOUCRON['ff'] - DEMOUCRON['pp']
    mf = (DEMOUCRON['mf'] - DEMOUCRON['pp']) / span
    out['A_demoucron'] = {'pp': 0.0, 'p': mf / 3, 'mp': 2 * mf / 3, 'mf': mf, 'f': (1 + mf) / 2, 'ff': 1.0}
    # C1, C2: Askenfelt's p -> mf -> f steps per player inside Clark & Luce's 14 dB pp..ff, the 5 dB left split
    # equally below p and above f (a rule), mp halfway between p and mf (a rule).
    for i, a in enumerate(ASKENFELT):
        pf = a['f'] - a['p']
        edge = (CLARK_LUCE_PP_FF - pf) / 2
        lv = {'pp': 0.0, 'p': edge, 'mf': edge + a['mf'] - a['p'], 'f': edge + pf, 'ff': CLARK_LUCE_PP_FF}
        lv['mp'] = (lv['p'] + lv['mf']) / 2
        out['C%d_askenfelt_player%d' % (i + 1, i + 1)] = {d: lv[d] / CLARK_LUCE_PP_FF for d in DYN}
    # B: MuseScore's own scale (pp 3750 ... ff 6250 in equal steps) read as linear in dB: the shipped M's rule
    # (0.4 for mp), for comparison.
    out['B_equal_steps'] = {d: i / 5 for i, d in enumerate(DYN)}
    # bounds: every dynamic at the pp profile / at the ff profile. Under either, the real side is the same for every
    # dynamic up to a constant, which centring removes (Weinzierl et al. 2018: ff reproducible within 4 dB of
    # Meyer 1990, pp within 10 dB, so the ff profile is the better-founded one).
    out['bound_pp_profile'] = {d: 0.0 for d in DYN}
    out['bound_ff_profile'] = {d: 1.0 for d in DYN}
    return out


def power(path):
    """{instrument: {'pp'|'ff': {midi: LW dB}}}: TU Berlin single tones ('_et_'), mean over repeats"""
    pc = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
    rows = list(csv.reader(open(path)))[2:]
    out = {k: {'pp': {}, 'ff': {}} for k in DBLAB}
    for k, lab in DBLAB.items():
        for r in rows:
            if r[1] == lab and '_et_' in r[0] and r[4] in ('pp', 'ff'):
                name = r[5]
                m = 12 * (int(name[-1]) + 1) + pc[name[0]] + (1 if name[1] == 'k' else 0)
                out[k][r[4]].setdefault(m, []).append(float(r[6]))
    return {k: {d: {p: sum(v) / len(v) for p, v in t.items()} for d, t in out[k].items()} for k in out}


def interp(table, p):
    """linear in dB between the nearest measured pitches, the nearest one outside them"""
    if p in table:
        return table[p]
    ks = sorted(table)
    if p < ks[0]:
        return table[ks[0]]
    if p > ks[-1]:
        return table[ks[-1]]
    a = max(k for k in ks if k < p)
    b = min(k for k in ks if k > p)
    return table[a] + (table[b] - table[a]) * (p - a) / (b - a)


def main():
    csv_path, range_path, curves_path, out_path = sys.argv[1:5]
    real = power(csv_path)
    rng = json.load(open(range_path))
    cur = json.load(open(curves_path))
    rows = {s: {r[0] for r in rng[s]['Long']['range']} for s in SECT}

    def common(s):
        k = INST[s]
        lo = max(min(rows[s]), min(real[k]['pp']), min(real[k]['ff']))
        hi = min(max(rows[s]), max(real[k]['pp']), max(real[k]['ff']))
        return lo, hi

    def sso(s, cc):
        t = {}
        for p, pts in cur['sections'][s]['points'].items():
            for pt in pts:
                if pt['cc'] == cc and pt.get('loudDb') is not None:
                    t[int(p)] = pt['loudDb']
        return t

    ssotab = {d: {s: sso(s, CC[d]) for s in SECT} for d in DYN}

    def terms(s, d, frac):
        k = INST[s]
        lo, hi = common(s)
        ps = range(lo, hi + 1)
        r = sum(interp(real[k]['pp'], p) + frac * (interp(real[k]['ff'], p) - interp(real[k]['pp'], p))
                for p in ps) / len(ps)
        q = sum(interp(ssotab[d][s], p) for p in ps) / len(ps)
        return r + 10 * math.log10(N[s]), q

    def M(d, frac):
        D = {}
        for s in SECT:
            r, q = terms(s, d, frac)
            D[s] = q - r
        m = sum(D.values()) / len(SECT)
        return {s: m - D[s] for s in SECT}

    fr = fractions()
    res = {'cc1_per_dynamic': CC, 'common_range_midi': {s: list(common(s)) for s in SECT}, 'fractions': fr}
    res['check_shipped_M_reproduced'] = M('mp', 0.4)
    res['M'] = {name: {d: M(d, f[d]) for d in DYN} for name, f in fr.items()}
    main_set = res['M']['A_demoucron']
    res['main_minus_shipped'] = {d: {s: main_set[d][s] - SHIPPED_M[s] for s in SECT} for d in DYN}
    lit = [n for n in fr if n.startswith(('A_', 'C'))]
    res['spread_over_literature_sets'] = {d: {s: [min(res['M'][n][d][s] for n in lit),
                                                  max(res['M'][n][d][s] for n in lit)] for s in SECT} for d in DYN}
    res['spread_over_bounds'] = {d: {s: [min(res['M'][n][d][s] for n in fr), max(res['M'][n][d][s] for n in fr)]
                                     for s in SECT} for d in DYN}
    # per section: how much the SSO side alone moves from mp (centred), i.e. M(s,d) - M(s,mp) at a fixed fraction
    res['sso_term_change_from_mp'] = {d: {s: M(d, 0.4)[s] - M('mp', 0.4)[s] for s in SECT} for d in DYN}
    flags = []
    for d in DYN:
        for s in SECT:
            lo, hi = res['spread_over_literature_sets'][d][s]
            if abs(main_set[d][s] - SHIPPED_M[s]) >= 5:
                flags.append({'section': s, 'dynamic': d, 'main_minus_shipped': main_set[d][s] - SHIPPED_M[s],
                              'all_literature_sets_beyond_5dB': min(abs(lo - SHIPPED_M[s]), abs(hi - SHIPPED_M[s])) >= 5
                              and (lo - SHIPPED_M[s]) * (hi - SHIPPED_M[s]) > 0})
    res['flags_5dB_from_shipped'] = flags
    # the TU Berlin violin pp..ff (single tones, extremes) against the literature's musical pp..ff span
    v = real['Violin']
    lo, hi = common('Violins 1')
    spans = sorted(interp(v['ff'], p) - interp(v['pp'], p) for p in range(lo, hi + 1))
    res['tu_berlin_violin_ff_minus_pp_dB'] = {'median': spans[len(spans) // 2], 'min': spans[0], 'max': spans[-1]}
    res['literature_violin_pp_to_ff_dB'] = {'Demoucron 2008 (pp..ff)': DEMOUCRON['ff'] - DEMOUCRON['pp'],
                                            'Clark & Luce 1965 via Askenfelt 1988': CLARK_LUCE_PP_FF,
                                            'Askenfelt 1988 p..f, two players': [9.0, 9.0]}

    def rnd(x):
        if isinstance(x, dict):
            return {k: rnd(w) for k, w in x.items()}
        if isinstance(x, list):
            return [rnd(w) for w in x]
        if isinstance(x, float):
            return round(x, 2)
        return x

    res = rnd(res)
    res['sources'] = [
        {'ref': 'Demoucron, M. (2008). On the control of virtual violins. Doctoral thesis, KTH / UPMC, TRITA-CSC-A '
                '2008:17, ch. 6, "Detache at different dynamic levels", Figs. 6.4-6.5 (open at diva-portal.org)',
         'measured': 'sound level of the recorded audio, relative dB; 1 professional violinist, G4 on the D string, '
                     'whole notes 4 s, whole bow, bow velocity held at about 13 cm/s by instruction',
         'values': 'mf -> ff +6.1 dB, mf -> pp -6.9 dB (averages)'},
        {'ref': 'Askenfelt, A. (1988). Measurement of the bowing parameters in violin playing. STL-QPSR 29(1), '
                '1-30, sec. III.A.1(d), Fig. 3d (open at speech.kth.se/qpsr)',
         'measured': 'top-plate vibration (acceleration) near the bass-side bridge foot, relative dB, 0.5 dB '
                     'resolution; 2 professional violinists, open G string, detache whole notes about 2 s, averages '
                     'over about 10 s',
         'values': 'f -> mf about 5 and 6 dB, mf -> p 4 and 3 dB (9 dB p..f each; 10 dB on one repeat)'},
        {'ref': 'Clark, M. & Luce, D. (1965). Intensities of orchestral instrument scales played at prescribed '
                'dynamic markings. JAES 13(2), 151-157',
         'measured': 'NOT READ (AES e-library paywall); only the violin pp..ff figure quoted by Askenfelt 1988, p. 9',
         'values': 'violin about 14 dB between ff and pp "appropriate for typical orchestra music" (secondary)'},
        {'ref': 'Meyer, J. (2009). Acoustics and the Performance of Music, 5th ed.; Meyer, J. (1990). Zur Dynamik '
                'und Schalleistung von Orchesterinstrumenten. Acustica 71',
         'measured': 'NOT READ (no accessible copy found)',
         'values': 'none used; Weinzierl et al. 2018 (read) compare with Meyer 1990: ff within 4 dB (mean 2 dB), '
                   'pp within 10 dB (mean 6.8 dB) over 12 instruments'},
        {'ref': 'Weinzierl, S. et al. (2018). Sound power and timbre as cues for the dynamic strength of orchestral '
                'instruments. JASA 144(3), 1347 (read: depositonce.tu-berlin.de)',
         'measured': 'sound power, ISO 3745, 32 microphones at 2.1 m, one player per instrument, pp and ff only',
         'values': 'the pp / ff endpoints of the real side (3_Audio_Features.csv)'},
        {'ref': 'Patynen, J. & Lokki, T. (2010). Directivities of symphony orchestra instruments. Acta Acust. '
                'united Ac. 96 (read)',
         'measured': 'p, f, ff recorded, but directivity is published normalised: no level per dynamic',
         'values': 'none usable'},
        {'ref': 'Jansson, E. & Karlsson, K. (1982). STL-QPSR 23(1) 31-48 (read)',
         'measured': 'whole-orchestra levels at players\' positions, p / mf / ff chosen from level plots',
         'values': 'none usable (not per instrument, not per marked dynamic)'},
    ]
    res['choices'] = [
        'Only violin data could be read; the violin fractions are used for all four instruments.',
        'Main set A: Demoucron mf; p, mp at 1/3, 2/3 of pp..mf and f halfway mf..ff (equal dB steps between the '
        'measured marks). C1/C2: Askenfelt per player inside Clark & Luce 14 dB, outer 5 dB split equally, mp '
        'halfway p..mf. B: MuseScore scale linear in dB (the shipped rule). Bounds: pp or ff profile only.',
        'The fraction is applied to the TU Berlin per-pitch pp..ff (extremes of single tones), which is wider than '
        'the literature\'s musical pp..ff (13-14 dB, violin): the markings are placed inside the extremes by fraction.',
        'SSO side: loudDb (loudest 50 ms of the note and 0.5 s release), All techniques Long, library default mics, '
        'CC1 = velocity, median of 4 repeats, every 3 semitones, linear in dB between; same as shipped M.',
        'Section = one player + 10 log10 N; whole common range, equal weights; centred over the five sections; '
        'M = dB to add to the section.',
    ]
    res['licences'] = 'TU Berlin database CC BY-NC-SA 4.0 and SSO measurements: derived numbers only'
    json.dump(res, open(out_path, 'w'), indent=1)

    print('cc1', CC)
    print('shipped M reproduced:', res['check_shipped_M_reproduced'])
    for n, f in fr.items():
        print('%-22s' % n, ' '.join('%s %.3f' % (d, f[d]) for d in DYN))
    for n in res['M']:
        print('\nM', n)
        print('%-4s' % '', ' '.join('%9s' % s[:9] for s in SECT))
        for d in DYN:
            print('%-4s' % d, ' '.join('%9.2f' % res['M'][n][d][s] for s in SECT))
    print('\nmain - shipped')
    for d in DYN:
        print('%-4s' % d, ' '.join('%9.2f' % res['main_minus_shipped'][d][s] for s in SECT))
    print('\nflags', res['flags_5dB_from_shipped'])
    print('TU violin ff-pp', res['tu_berlin_violin_ff_minus_pp_dB'])


if __name__ == '__main__':
    main()
