#!/usr/bin/env python3
"""The section balance at playing levels, [levels] sectionBalance 2 ("Clark & Luce balance"): the map's sectionLevel2
(gen_spitfire_sso.py reads this script's output).

    python3 -I section_balance_cl.py <clark_luce_levels.json> <clark_luce_table2.json> <sso_cc1_curves.json>
                                     <sso_winds_cc1_curves.json> <sso_winds_section_balance.json> <sso_sound_dynamics.json>
                                     <out sso_cl_section_balance.json>

The owner, 2026-10-14: "I prefer all the balance levels and the dynamic levels loudness to be based on real data".
Real side: Clark & Luce 1965, Fig. 2 (clark_luce_levels.json): each instrument's scales played pp / mf / ff as in
orchestral music, dB SPL at 10 m in its loudest direction; to sound power Lw = Lp + 20 log10(10 m) + 11 - DI_max
(DI_max: TU Berlin's directivity in that loudest direction, ff); mp = pp + 2/3 (mf - pp) (mp two of the three equal
steps from pp to mf); a patch = one player + 10 log10 N (strings 16 / 14 / 12 / 10 / 8, solo 1, a2 2, a6 6). SSO side:
the same as sectionLevel (section_balance_winds.py): the patch's Long at CC1 64 (loudDb), linear in dB between measured
pitches, its mean over the semitones of that patch's "common" range in sso_winds_section_balance.json. D = SSO - real,
M = -(D - the five string sections' mean D) (the strings centred, the winds on their scale), as sectionLevel.
Corroboration (one player against a string player: woodwind / brass): this +2.6..+4.0 / +11.3..+11.9 (pp..mf); the
Spheres dataset's sections +1.4 / +14.3; Skaalevik 2019 (orchestra dosimetry fit) +3.2 / +4.2 in average sessions (rests
included), +4.2 / +12.2 in forte passages; sectionLevel (TU single notes + Meyer mp) +11.9 / +18.9.
The French horn (since 2026-10-15, docs/BALANCE_LOG.md): Fig. 2's horn is not the mean of its Table II scales (every
other instrument's is) and follows player 2, whose levels are 20 .. 40 dB under player 1's; its pp / mf are player 1's
(clark_luce_table2.py: Table II, the same mean as Fig. 2's), mp from them as above; no ff (player 1 played none).
Patches with no Clark & Luce instrument (Bass Clarinet, Contrabassoon, Bass Trombone Solo / a2): their family member's
change from sectionLevel to sectionLevel2 added to their sectionLevel (FAMILY: an assumption, "plays like its family").
Patches without sectionLevel (since 2026-10-15, docs/BALANCE_LOG.md › Iteration 2; before, they played SSO's own level
against an orchestra whose levels are all corrected): GAP. Real side: their Clark & Luce instrument (the solo strings,
the Motif brass) or a family member's (an assumption: Piccolo, Alto / Bass Flute by the flute, Contrabass Clarinet by
the clarinet, Contrabass Trombone and Cimbasso by the trombone, Contrabass Tuba by the tuba). SSO side: the dynamics
check's Long at CC1 64 at the patch's test pitch (sso_sound_dynamics.json; no full-range measurement exists for them),
plus the median of (full-range mean - dynamics check) over the patches that have both (`standIn`): on those, the
stand-in's M2 differs from the real one by SD 2.3 dB. Strings Ensemble stays without: which sections sound at its test
pitch isn't known.
"""
import json
import math
import statistics
import sys

STRINGS = {'Violins 1': ('Violin', 16), 'Violins 2': ('Violin', 14), 'Violas': ('Viola', 12), 'Celli': ('Cello', 10),
           'Basses': ('Double bass', 8)}
CL = [('flute', 'Flute'), ('oboe', 'Oboe'), ('cor anglais', 'English horn'), ('clarinet', 'Clarinet'),
      ('bassoon', 'Bassoon'), ('horn', 'French horn'), ('trumpet', 'Trumpet'), ('trombone', 'Trombone'), ('tuba', 'Tuba')]
FAMILY = {'Bass Clarinet': 'Clarinet Solo', 'Contrabassoon': 'Bassoon Solo', 'Bass Trombone Solo': 'Tenor Trombone Solo',
          'Bass Trombones a2': 'Tenor Trombones a2'}
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
    if name in FAMILY:
        return None
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


def lw(levels, inst, table2):
    """one player's sound power (dB re 1 pW) at pp / mp / mf / ff (the horn: player 1's pp / mf, no ff)"""
    v = levels[inst]
    t = table2.get(inst)
    lp = {'pp': t['pp'], 'mf': t['mf']} if t else {'pp': v['pp'], 'mf': v['mf'], 'ff': v['ff']}
    lp['mp'] = lp['pp'] + 2 / 3 * (lp['mf'] - lp['pp'])
    return {d: x + 20 * math.log10(10) + 11 - v['diMax'] for d, x in lp.items()}


def main():
    levelsPath, table2Path, curvesPath, windsPath, balancePath, dynamicsPath, outPath = sys.argv[1:8]
    dynamics = json.load(open(dynamicsPath, encoding='utf-8'))
    levels = json.load(open(levelsPath, encoding='utf-8'))['levels']
    table2 = {k: v for k, v in json.load(open(table2Path, encoding='utf-8')).items() if k in levels}
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
        real[name] = {k: x + 10 * math.log10(n) for k, x in lw(levels, inst, table2).items()}
        d[name] = sso64[name] - real[name]['mp']
    centre = sum(d[s] for s in STRINGS) / len(STRINGS)
    check = lambda name: dict(dynamics[name]['Long']['curve'])[64]
    both = [name for name in sso64 if 'Long' in dynamics.get(name, {})]
    standIn = statistics.median(sso64[name] - check(name) for name in both)
    err = [check(name) + standIn - sso64[name] for name in both]
    out = {'method': __doc__.strip().split('\n\n', 1)[1], 'patches': {}}
    for name, (inst, n, curve, b) in patches.items():
        if inst:
            m = -(d[name] - centre)
            out['patches'][name] = {'M2': round(m, 2), 'M1': b['M'], 'instrument': inst, 'players': n,
                                    'lw': {k: round(x, 2) for k, x in real[name].items()}}
    for name, fam in FAMILY.items():
        if name in patches:
            f = out['patches'][fam]
            out['patches'][name] = {'M2': round(patches[name][3]['M'] + f['M2'] - f['M1'], 2), 'M1': patches[name][3]['M'],
                                    'instrument': None, 'family': fam, 'players': players(name)}
    out['standIn'] = {'db': round(standIn, 2), 'patches': len(both), 'sd': round(statistics.pstdev(err), 2),
                      'max': round(max(map(abs, err)), 2)}
    for name, (inst, n, family) in GAP.items():
        r = {k: x + 10 * math.log10(n) for k, x in lw(levels, inst, table2).items()}
        out['patches'][name] = {'M2': round(-(check(name) + standIn - r['mp'] - centre), 2), 'M1': None, 'instrument': inst,
                                'players': n, 'familyAssumption': family, 'sso': 'dynamics check, pitch %d' % dynamics[name]['pitch'],
                                'lw': {k: round(x, 2) for k, x in r.items()}}
    with open(outPath, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1)
        fh.write('\n')
    for name, v in out['patches'].items():
        print('%-22s M1 %6s -> M2 %+6.2f%s' % (name, '%+.2f' % v['M1'] if v['M1'] is not None else '-', v['M2'],
                                                '  (family: %s)' % v['family'] if v.get('family') else
                                                '  (family assumption: %s)' % v['instrument'] if v.get('familyAssumption') else ''))
    print('stand-in: %+.2f dB, SD %.2f, max %.2f over %d patches' % (standIn, out['standIn']['sd'], out['standIn']['max'], len(both)))


if __name__ == '__main__':
    main()
