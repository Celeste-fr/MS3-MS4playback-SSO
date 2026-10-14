#!/usr/bin/env python3
"""The section balance M for SSO's woodwinds and brass on the strings' scale (a proposal: not written into the map).

    python3 -I section_balance_winds.py <3_Audio_Features.csv> <sso_sound_range.json> <sso_cc1_curves.json>
                                        <sso_winds_cc1_curves.json> <out sso_winds_section_balance.json>

The strings' M (gen_spitfire_sso.py SECTION_FADERS, the owner's choice 2026-10-11) by its own method, then the same
for every measured wind and brass patch WITHOUT re-centring: each patch's D = SSO - real (mean over the semitones of
the Long's range the database also measured), M = -(D - the strings' mean D), so the strings keep -3.81 -2.66 -2.12
+2.77 +5.81 and a wind's M is the dB it would play louder by beside them.
  - SSO side: the Long at CC1 = velocity 64, loudDb (the loudest 50 ms of the note and 0.5 s of its release), median of
    4, every 3 semitones, linear in dB between (sso_cc1_curves.json / sso_winds_cc1_curves.json);
  - real side: Weinzierl et al. 2018, TU Berlin database v3, SoundPower in dB of single tones (_et_), per MIDI pitch,
    linear in dB between measured pitches; mp = pp + 0.4 (ff - pp) (CC1 64 = level 4750 on MuseScore's pp 3750 .. ff
    6250, as for the strings); a patch = one player + 10 log10 N, N its players (solo 1, a2 2, a6 6; strings 16 / 14 /
    12 / 10 / 8, SSO's product page).
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
FRAC = 0.4


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


def D(real, lab, n, rangePitches, sso):
    lo = max(min(rangePitches), min(real[lab]['pp']), min(real[lab]['ff']))
    hi = min(max(rangePitches), max(real[lab]['pp']), max(real[lab]['ff']))
    acc = []
    for p in range(lo, hi + 1):
        pp, ff = interp(real[lab]['pp'], p), interp(real[lab]['ff'], p)
        acc.append(interp(sso, p) - (pp + FRAC * (ff - pp) + 10 * math.log10(n)))
    return sum(acc) / len(acc), [lo, hi]


def main():
    csvPath, rangePath, curvesPath, windsPath, outPath = sys.argv[1:6]
    real = power(csvPath)
    rng = json.load(open(rangePath, encoding='utf-8'))
    strings = json.load(open(curvesPath, encoding='utf-8'))['sections']
    winds = json.load(open(windsPath, encoding='utf-8'))['sections']
    rows = lambda s: [r[0] for r in rng[s]['Long']['range']]
    d = {}
    common = {}
    for s, (lab, n) in STRINGS.items():
        d[s], common[s] = D(real, lab, n, rows(s), at64(strings[s]))
    centre = sum(d.values()) / len(d)
    strM = {s: round(-(d[s] - centre), 2) for s in STRINGS}
    out = {'method': __doc__.strip().split('\n\n', 1)[1], 'stringsReproduced': strM, 'strings': {}, 'instruments': {}}
    for s in STRINGS:
        out['strings'][s] = {'M': strM[s], 'D': round(d[s], 2), 'players': STRINGS[s][1], 'common': common[s]}
    for name, lab in real_range.WINDS.items():
        if name not in winds:
            continue
        n = players(name)
        dw, cw = D(real, lab, n, rows(name), at64(winds[name]))
        out['instruments'][name] = {'M': round(-(dw - centre), 2), 'D': round(dw, 2), 'instrument': lab, 'players': n,
                                    'common': cw}
    with open(outPath, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
        f.write('\n')
    print('strings', ' '.join('%s %+.2f' % (s, v) for s, v in strM.items()))
    for name, v in out['instruments'].items():
        print('%-22s M %+6.2f  (N %d, %s, %d-%d)' % (name, v['M'], v['players'], v['instrument'], *v['common']))


if __name__ == '__main__':
    main()
