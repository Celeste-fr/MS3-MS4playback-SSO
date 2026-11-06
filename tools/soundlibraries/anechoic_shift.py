#!/usr/bin/env python3
"""The in-context correction of [levels] sectionBalance 2's sectionLevel2 (docs/BALANCE_LOG.md › Iteration 15; the owner,
2026-10-24: "apply the full measured balance", after checking the readings' spot-check page, 40 of 40 right).

    python3 -I anechoic_shift.py <anechoic_balance.json> <sso_cl_section_balance.json> <out sso_anechoic_shift.json>

anechoic_balance.json (docs/RESEARCH.md › 1.5a) holds, per instrument, how loud one player plays against the strings at
the same written marking in two anechoic orchestra recordings (Aalto: Beethoven 7, Bruckner 8, Mahler 1; TU Berlin:
Beethoven 8), less what the map's real side (Clark & Luce's one-player mp + realRange 2's steps) implies: rel < 0, the
instrument plays softer against the strings in context than the map plays it. An instrument is applied when both
datasets measured it, their medians agree in sign and each one's bootstrap 90 % interval excludes 0 (flute, oboe, horn,
trumpet; the clarinet's and bassoon's TU intervals include 0, the trombone and tuba have Aalto only). Its shift is the
pooled value (the mean of the two datasets' medians), unrounded: the measurement as it stands; rounding to the proposal's
-8 / -4.5 / +4 / -4 would move it by at most 0.3 dB, a tenth of its +-3 dB uncertainty.

Every patch of the instrument takes the shift (one per-player offset, so ensembles a2 / a3 / a4 / a6 and the Motif
ensembles of the same instrument too: their real side is the same Clark & Luce instrument x players). Family members
without data of their own take their family's shift, as their Clark & Luce level is already their family's or their
own instrument's unchecked in context: an assumption ("plays like its family", BALANCE_LOG's sibling convention),
marked "assumption" in the output. The strings stay the centre (their own rel is not applied: the datasets disagree by
1-6 dB between the sections, no consistent difference); everything else is unchanged.
gen_spitfire_sso.py adds "shiftDb" to sso_cl_section_balance.json's M2 for the map's sectionLevel2.
"""
import json
import sys

# instrument (anechoic_balance.json) -> {patch: assumption?}
FAMILIES = {
    'flute': {'Flute Solo': False, 'Flutes a2': False,
              'Piccolo': True, 'Alto Flute': True, 'Bass Flute': True},
    'oboe': {'Oboe Solo': False, 'Oboes a2': False, 'Cor Anglais': True},
    'clarinet': {'Clarinet Solo': False, 'Clarinets a2': False, 'Bass Clarinet': True, 'Contrabass Clarinet': True},
    'bassoon': {'Bassoon Solo': False, 'Bassoons a2': False, 'Contrabassoon': True},
    'horn': {'Horn Solo': False, 'Horns a2': False, 'Horns a6': False, 'Motif Horns a4': False},
    'trumpet': {'Trumpet Solo': False, 'Trumpets a2': False, 'Trumpets a6': False, 'Motif Trumpets a3': False},
    'trombone': {'Tenor Trombone Solo': False, 'Tenor Trombones a2': False, 'Trombones a6': False,
                 'Motif Trombones a5': False, 'Bass Trombone Solo': True, 'Bass Trombones a2': True,
                 'Contrabass Trombone': True, 'Cimbasso Solo': True, 'Cimbassi a2': True},
    'tuba': {'Tuba Solo': False, 'Contrabass Tuba': True},
}
ASSUMPTION = {
    'Piccolo': 'the flute\'s shift: no piccolo in either recording (Mahler 1\'s flutes and piccolo were left out)',
    'Alto Flute': 'the flute\'s shift: no alto flute in either recording',
    'Bass Flute': 'the flute\'s shift: no bass flute in either recording',
    'Cor Anglais': 'the oboe\'s shift: no cor anglais in either recording (its Clark & Luce level is its own instrument\'s, '
                   'unchecked in context)',
}
# the owner's choice, 2026-10-24: the instruments the rule above selects; checked here so that a re-measurement that
# changes the selection fails loudly instead of changing the map silently
OWNER_APPLIED = {'flute', 'oboe', 'horn', 'trumpet'}


def applies(inst):
    a, t = inst.get('aalto'), inst.get('tub8')
    if not a or not t:
        return False, 'one dataset only'
    if (a['median'] > 0) != (t['median'] > 0):
        return False, 'the datasets disagree in sign (%+.2f / %+.2f)' % (a['median'], t['median'])
    for name, d in (('Aalto', a), ('TU B8', t)):
        lo, hi = d['ci90']
        if lo <= 0 <= hi:
            return False, '%s\'s 90 %% interval [%+.2f, %+.2f] includes 0' % (name, lo, hi)
    return True, 'both datasets, the same sign, both 90 % intervals exclude 0'


def main():
    anechoic, cl, outPath = sys.argv[1:4]
    a = json.load(open(anechoic, encoding='utf-8'))['instruments']
    m2 = {n: v['M2'] for n, v in json.load(open(cl, encoding='utf-8'))['patches'].items()}
    out = {'method': __doc__.strip().split('\n\n', 1)[1], 'instruments': {}, 'patches': {}}
    applied = set()
    for inst, patches in FAMILIES.items():
        ok, why = applies(a[inst])
        pooled = a[inst]['pooled']['db']
        out['instruments'][inst] = {'applied': ok, 'why': why, 'pooledDb': pooled,
                                    'aalto': a[inst]['aalto']['median'] if a[inst].get('aalto') else None,
                                    'tub8': a[inst]['tub8']['median'] if a[inst].get('tub8') else None,
                                    'uncertaintyDb': 'about +-3 (docs/RESEARCH.md › 1.5a)' if ok else None}
        if not ok:
            continue
        applied.add(inst)
        for p, assumed in patches.items():
            if p not in m2:
                raise SystemExit('%s: no M2 in %s' % (p, cl))
            row = {'instrument': inst, 'shiftDb': pooled, 'M2': m2[p], 'sectionLevel2': round(m2[p] + pooled, 2)}
            if assumed:
                row['assumption'] = ASSUMPTION[p]
            out['patches'][p] = row
    if applied != OWNER_APPLIED:
        raise SystemExit('the rule selects %s, the owner chose %s: ask the owner' % (sorted(applied), sorted(OWNER_APPLIED)))
    with open(outPath, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1, ensure_ascii=False)
        fh.write('\n')
    for inst, v in out['instruments'].items():
        print('%-9s %s %+.2f  %s' % (inst, 'applied' if v['applied'] else 'kept   ', v['pooledDb'], v['why']))
    for p, v in out['patches'].items():
        print('  %-20s %+7.2f -> %+7.2f%s' % (p, v['M2'], v['sectionLevel2'], '  (assumption)' if 'assumption' in v else ''))


if __name__ == '__main__':
    main()
