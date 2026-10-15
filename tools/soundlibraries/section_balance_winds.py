#!/usr/bin/env python3
"""The section balance M for SSO's string sections, woodwinds and brass, one rule for all (the map's sectionLevel:
gen_spitfire_sso.py reads its output's "strings" and "instruments").

    python3 -I section_balance_winds.py <3_Audio_Features.csv> <sso_sound_range.json> <sso_cc1_curves.json>
                                        <sso_winds_cc1_curves.json> <out sso_winds_section_balance.json>

The owner, 2026-10-14: the woodwinds and brass get a section balance M from the solo data (TU Berlin sound power, with
Meyer's levels) "until we can find better non-solo data"; then ("sure") the strings' M to Meyer's mp too, so one rule
covers everything. The strings' M by the method the owner chose 2026-10-11 (centred over the five), then the same for
every measured wind and brass patch WITHOUT re-centring:
each patch's D = SSO - real (mean over the semitones of the Long's range the database also measured), M = -(D - the
strings' mean D), so a wind's M is the dB it plays louder by beside the strings.
  - SSO side: the Long at CC1 = velocity 64, loudDb (the loudest 50 ms of the note and 0.5 s of its release), median of
    4, linear in dB between measured pitches (sso_cc1_curves.json / sso_winds_cc1_curves.json);
  - real side: Weinzierl et al. 2018, TU Berlin database v3, SoundPower in dB of single tones (_et_), per MIDI pitch,
    linear in dB between measured pitches (pp, ff as measured, not smoothed); a patch = one player + 10 log10 N, N its
    players (solo 1, a2 2, a6 6; strings 16 / 14 / 12 / 10 / 8, SSO's product page);
  - mp: every patch at mp as [levels] realRange plays it (2026-10-14): pp + f (ff - pp), f = a + 2 (b - a) / 5 by the
    Meyer rule (real_range.fractions: the bass clarinet's and bass trombones' their sibling's, an assumption), 3 / 7 by
    rule D (Cor Anglais, Contrabassoon). Until 2026-10-14 the strings' M (APPROVED_20261011, the owner's ABX-backed
    choice) placed mp at f = 0.4 (CC1 64 = level 4750 on MuseScore's pp 3750 .. ff 6250); stringsMp04 is that method on
    today's curves (Celli and Basses re-measured every semitone 2026-10-14), stringsMove the Meyer M less the 2026-10-11
    values, meyerScaleShift the winds' change from the f = 0.4 scale to Meyer's.
The CSV is CC BY-NC-SA 4.0: derived numbers only.
"""
import csv
import json
import math
import sys

sys.path.insert(0, __import__('os').path.dirname(__file__))
import real_range  # noqa: E402

STRINGS = {'Violins 1': ('Violin modern', 16), 'Violins 2': ('Violin modern', 14), 'Violas': ('Viola modern', 12),
           'Celli': ('Cello modern', 10), 'Basses': ('Double bass modern', 8)}
FRAC = 0.4          # the strings' M of 2026-10-11: mp 0.4 of the way pp -> ff (history, stringsMp04)
APPROVED_20261011 = {'Violins 1': -3.81, 'Violins 2': -2.66, 'Violas': -2.12, 'Celli': 2.77, 'Basses': 5.81}


def mpFrac(lab):
    """mp as a fraction of the single-note span above its pp, as [levels] realRange plays it: the Meyer rule's
    a + 2 (b - a) / 5 (pp and two of the five equal steps to ff), rule D's 3 / 7 where real_range.fractions has none"""
    ab = real_range.fractions(lab)
    if not ab:
        return 3 / 7
    a, b = ab
    return a + 2 * (b - a) / 5


def players(name):
    for n in (2, 6):
        if name.endswith(' a%d' % n):
            return n
    return 1


def power(path):
    """{label: {dyn: {midi: sound power dB}}}"""
    pc = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
    rows = list(csv.reader(open(path, encoding='utf-8')))
    col = rows[1].index('SoundPower in dB')
    out = {}
    for r in rows[2:]:
        if len(r) > col and '_et_' in r[0] and r[4] in ('pp', 'ff'):
            n = r[5]
            m = 12 * (int(n[-1]) + 1) + pc[n[0]] + (1 if n[1] == 'k' else 0)
            out.setdefault(r[1], {'pp': {}, 'ff': {}})[r[4]].setdefault(m, []).append(float(r[col]))
    return {lab: {d: {p: sum(v) / len(v) for p, v in t.items()} for d, t in dyn.items()} for lab, dyn in out.items()}


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
    out = {}
    for p, pts in section['points'].items():
        for pt in pts:
            if pt['cc'] == 64 and pt.get('loudDb') is not None:
                out[int(p)] = pt['loudDb']
    return out


def D(real, lab, n, rangePitches, sso, frac=FRAC):
    lo = max(min(rangePitches), min(real[lab]['pp']), min(real[lab]['ff']))
    hi = min(max(rangePitches), max(real[lab]['pp']), max(real[lab]['ff']))
    acc = []
    for p in range(lo, hi + 1):
        pp, ff = interp(real[lab]['pp'], p), interp(real[lab]['ff'], p)
        acc.append(interp(sso, p) - (pp + frac * (ff - pp) + 10 * math.log10(n)))
    return sum(acc) / len(acc), [lo, hi]


def main():
    csvPath, rangePath, curvesPath, windsPath, outPath = sys.argv[1:6]
    real = power(csvPath)
    rng = json.load(open(rangePath, encoding='utf-8'))
    strings = json.load(open(curvesPath, encoding='utf-8'))['sections']
    winds = json.load(open(windsPath, encoding='utf-8'))['sections']
    rows = lambda s: [r[0] for r in rng[s]['Long']['range']]
    # the strings by the approved method (mp 0.4) on today's curves, and by the Meyer rule's mp
    d04, d, common = {}, {}, {}
    for s, (lab, n) in STRINGS.items():
        d04[s], common[s] = D(real, lab, n, rows(s), at64(strings[s]))
        d[s], _ = D(real, lab, n, rows(s), at64(strings[s]), mpFrac(lab))
    c04 = sum(d04.values()) / len(d04)
    centre = sum(d.values()) / len(d)
    strM04 = {s: round(-(d04[s] - c04), 2) for s in STRINGS}
    strM = {s: round(-(d[s] - centre), 2) for s in STRINGS}
    moves = {s: round(strM[s] - APPROVED_20261011[s], 2) for s in STRINGS}
    meyerStrings = True     # the owner, 2026-10-14 ("sure"): the strings by Meyer's mp too
    # (with the strings' Meyer M every wind would play this much louder against the strings' mean: the scales' offset)
    shift = round(centre - c04, 2)
    if not meyerStrings:
        centre = c04        # the approved strings kept: the winds on their scale (the approved method's centre)
    out = {'method': __doc__.strip().split('\n\n', 1)[1], 'approved20261011': APPROVED_20261011, 'stringsMp04': strM04,
           'meyerScaleShift': shift,
           'stringsMeyer': strM, 'stringsMove': moves, 'stringsTake': 'meyer' if meyerStrings else 'approved',
           'strings': {}, 'instruments': {}}
    for s in STRINGS:
        out['strings'][s] = {'M': strM[s] if meyerStrings else APPROVED_20261011[s], 'D': round(d[s], 2),
                             'mpFrac': round(mpFrac(STRINGS[s][0]), 4), 'players': STRINGS[s][1], 'common': common[s]}
    for name, lab in real_range.WINDS.items():
        if name not in winds:
            continue
        n = players(name)
        dw, cw = D(real, lab, n, rows(name), at64(winds[name]), mpFrac(lab))
        out['instruments'][name] = {'M': round(-(dw - centre), 2), 'D': round(dw, 2), 'instrument': lab, 'players': n,
                                    'mpFrac': round(mpFrac(lab), 4),
                                    'rule': 'meyer' if real_range.fractions(lab) else 'D', 'common': cw}
    with open(outPath, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
        f.write('\n')
    print('strings mp 0.4  ', ' '.join('%s %+.2f' % (s, v) for s, v in strM04.items()))
    print('strings Meyer   ', ' '.join('%s %+.2f' % (s, v) for s, v in strM.items()))
    print('move vs 2026-10-11', moves, '->', out['stringsTake'])
    for name, v in out['instruments'].items():
        print('%-22s M %+6.2f  (N %d, %s, %s mp %.3f, %d-%d)' % (name, v['M'], v['players'], v['instrument'], v['rule'],
                                                               v['mpFrac'], *v['common']))


if __name__ == '__main__':
    main()
