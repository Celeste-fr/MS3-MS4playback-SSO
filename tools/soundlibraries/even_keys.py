#!/usr/bin/env python3
"""Per-key level evenness of SSO's woodwind and brass Long ([levels] evenKeys; docs/PLAYBACK_SETTINGS.md › Even keys).

    python3 -I even_keys.py [--csv <TU Berlin 3_Audio_Features.csv>] [-o sso_even_keys.json]

Why (2026-10-28): the every-semitone sweep of the winds' Long (sso_winds_cc1_semitone.json, sso_gap_cc1_semitone.json)
shows SSO's keys in zones, mostly two semitones a sample, whose levels jump by up to ~9 dB from zone to zone (Oboe Solo:
69-70 and 75-76 loud, 71-72 and 79-80 quiet); the 3-semitone sweep fell between them. Real players show no such jumps:
the TU Berlin database (Weinzierl et al. 2018, JASA 144(3), one player per instrument, every semitone, single takes) has a
key-to-key scatter at ff of 1.1 dB (robust SD) by the estimator below, pooled over the twelve wind and brass instruments
SSO's patches map to. The owner (2026-10-28): SSO's uneven keys are an artefact; every pitch should play at the real
player's level, smoothed only beyond the scatter real players show, smaller structure kept.

Estimator (per patch, per CC1 point): a key's deviation d(k) = its level - the Theil-Sen line (median of pairwise slopes,
median intercept: robust to a zone of outliers) through the 8 nearest OTHER keys of the sweep, evaluated at k. Level: loudDb
(the loudest 50 ms of the note and its release; the measure the section balance and the Vial match put SSO's Long on the
real scale with), the median of 4 takes = the mean of SSO's two round robins.

Threshold T: the 80th percentile of |d| over the TU Berlin database's ff tones by the same estimator (478 keys, the
instruments of real_range.py WINDS): 1.47 dB -> T = 1.5. Real data hold take-to-take noise too (one take a pitch), so T is
an upper bound of real structure. Not the pp tones: "as softly as possible" is near the player's control limit (clarinet
6.7 dB rms) and says nothing of key evenness in playing.

Correction c(k) = -sign(d) max(0, |d| - T): only the part of a deviation beyond T is taken away (a key 6 dB under the line
plays 1.5 dB under it; one 1 dB under stays). From the CC1 64 point only: MuseScore plays the correction with [levels]
realRange on (contextlevel.h › Real range), where a note's level is SSO's Long at CC1 64 at its key plus the player's
change from mp (longLevel, per semitone since 2026-10-28, takes SSO's own per-key change away), so only the key's mp level
is left uneven. The other CC1 points' deviations are written for the record ("byCc").

Not corrected (cannot be from a curve): the round robins, which alternate per note (|A - B| per key written, "rr"); the
level a short note reaches before a slow attack (the onset; loudDb, loud1sDb and rmsDb give the same zones within ~0.4-1.5
dB rms: "measures"). Patches: every swept wind and brass patch; the solo strings (also in sso_gap_cc1_semitone.json) not
(the owner's decision was about the winds).
"""
import argparse
import csv
import json
import math
import os
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import real_range  # noqa: E402

CURVES = [os.path.join(HERE, 'sso_winds_cc1_semitone.json'), os.path.join(HERE, 'sso_gap_cc1_semitone.json')]
STRINGS = {'Solo Violin 1', 'Solo Violin 2', 'Solo Viola', 'Solo Cello'}
NEIGHBOURS = 8
THRESHOLD = 1.5
PERCENTILE = 0.8
MEASURE = 'loudDb'
ANCHOR = 64
MIN_DB = 0.05
# The real player's key shape as the target (the owner, 2026-10-28): the patches whose key pattern follows TU Berlin's
# instrument at ff (Oboe Solo r +0.73, Oboes a2 +0.62: two independent sources, Spitfire's player and TU's, agree in
# direction): the target is the line plus TU's own deviation at ff at that key (the same estimator on TU's levels; TU's
# MIDI pitches are the patch's keys), not the line within T. TU is one player, one take a pitch: its deviation holds take
# noise too (real players' scatter at ff 1.1 dB robust SD). Keys TU doesn't cover: capped at T as the others
REAL_SHAPE = {'Oboe Solo': 'Oboe modern', 'Oboes a2': 'Oboe modern'}


def theil_sen_at(xs, ys, x0):
    sl = [(ys[j] - ys[i]) / (xs[j] - xs[i]) for i in range(len(xs)) for j in range(i + 1, len(xs)) if xs[j] != xs[i]]
    b = st.median(sl) if sl else 0.0
    a = st.median([y - b * x for x, y in zip(xs, ys)])
    return a + b * x0


def deviations(levels, n=NEIGHBOURS):
    """{key: level - the Theil-Sen line through the n nearest other keys, at the key}"""
    ks = sorted(levels)
    out = {}
    for k in ks:
        nb = sorted((q for q in ks if q != k), key=lambda q: (abs(q - k), q))[:n]
        out[k] = levels[k] - theil_sen_at(nb, [levels[q] for q in nb], k)
    return out


def correction(d, t=THRESHOLD):
    return -math.copysign(max(0.0, abs(d) - t), d)


def rms(v):
    return math.sqrt(sum(x * x for x in v) / len(v)) if v else 0.0


def tu_levels(path, labels):
    """{label: {dyn: {midi: sound power dB}}} (real_range.spans' reading, mean of a pitch's files)"""
    pc = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
    rows = list(csv.reader(open(path, encoding='utf-8')))
    col = rows[1].index('SoundPower in dB')
    out = {}
    for lab in labels:
        lv = {'pp': {}, 'ff': {}}
        for r in rows[2:]:
            if len(r) > col and r[1] == lab and '_et_' in r[0] and r[4] in lv:
                name = r[5]
                m = 12 * (int(name[-1]) + 1) + pc[name[0]] + (1 if name[1] == 'k' else 0)
                lv[r[4]].setdefault(m, []).append(float(r[col]))
        out[lab] = {d: {p: sum(v) / len(v) for p, v in t.items()} for d, t in lv.items()}
    return out


def level_at(section, cc, measure=MEASURE):
    out = {}
    for p, rows in section['points'].items():
        for r in rows:
            if r['cc'] == cc and r.get(measure) is not None and r[measure] > -150:
                out[int(p)] = r[measure]
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--csv', help="TU Berlin's 3_Audio_Features.csv: recomputes the threshold and compares shapes")
    ap.add_argument('-o', '--out', default=os.path.join(HERE, 'sso_even_keys.json'))
    args = ap.parse_args(argv)
    sections = {}
    for f in CURVES:
        sections.update(json.load(open(f, encoding='utf-8'))['sections'])
    db = dict(real_range.WINDS)
    db.update({k: v for k, v in real_range.GAP_OWN.items() if k not in STRINGS})
    real = {}
    if args.csv:
        tu = tu_levels(args.csv, sorted(set(real_range.WINDS.values())))
        pooled = {'ff': [], 'pp': []}
        per = {}
        for lab, byDyn in tu.items():
            per[lab] = {}
            for dyn, lv in byDyn.items():
                dv = deviations(lv)
                pooled[dyn] += [abs(v) for v in dv.values()]
                per[lab][dyn] = {'n': len(dv), 'rmsDb': round(rms(list(dv.values())), 2),
                                 'robustSdDb': round(1.4826 * st.median([abs(v) for v in dv.values()]), 2)}
                real.setdefault(lab, {})[dyn] = dv
        q = {d: sorted(v) for d, v in pooled.items()}
        pct = q['ff'][int(PERCENTILE * len(q['ff']))]
        print(f'TU Berlin ff: {len(q["ff"])} keys, p80 |d| {pct:.2f} dB (T = {THRESHOLD})', file=sys.stderr)
        assert abs(round(pct * 2) / 2 - THRESHOLD) < 1e-9, 'the threshold moved: update THRESHOLD and the docs'
        tuSummary = {'perInstrument': per,
                     'pooled': {d: {'n': len(v), 'rmsDb': round(rms(v), 2),
                                    'robustSdDb': round(1.4826 * st.median(v), 2),
                                    'p50': round(v[len(v) // 2], 2), 'p80': round(v[int(.8 * len(v))], 2),
                                    'p90': round(v[int(.9 * len(v))], 2), 'p95': round(v[int(.95 * len(v))], 2)}
                                for d, v in q.items()}}
    out = {'method': {
        'what': "per key, the dB that brings SSO's Long's level at CC1 64 to within T of a smooth curve over pitch: "
                "the <Articulation keyLevels> of each wind and brass patch's Long ([levels] evenKeys)",
        'level': f'{MEASURE} at CC1 {ANCHOR} (sso_winds_cc1_semitone.json, sso_gap_cc1_semitone.json: every semitone, '
                 'median of 4 takes = the round robins\' mean)',
        'deviation': f'level - the Theil-Sen line through the {NEIGHBOURS} nearest other keys, at the key',
        'threshold': f'T = {THRESHOLD} dB: the 80th percentile of |deviation| of the TU Berlin database\'s ff tones by '
                     'the same estimator (Weinzierl et al. 2018; the instruments of real_range.py WINDS, pooled)',
        'correction': '-sign(d) max(0, |d| - T), rounded to 0.01 dB; keys under 0.05 dB left out; for the patches whose '
                      'key pattern follows the real player\'s (REAL_SHAPE: Oboe Solo, Oboes a2; each patch\'s "target") '
                      'TU\'s deviation at ff - d at the keys TU measured (the owner, 2026-10-28: match the real oboe\'s '
                      'key shape; TU one take a pitch)',
        'byCc': 'each CC1 point\'s deviations, for the record (MuseScore plays the CC1 64 one: realRange takes the rest)',
        'rr': '|A - B| of the two round robins at CC1 64 per key (rmsDbTakes A B A B): not correctable per note',
        'measures': 'rms over keys and CC1 points of the deviation by loudDb against loud1sDb and rmsDb',
        'tool': 'tools/soundlibraries/even_keys.py'}, 'patches': {}}
    if args.csv:
        out['realPlayers'] = tuSummary
    for name, sec in sections.items():
        if name in STRINGS:
            continue
        lv = level_at(sec, ANCHOR)
        d = deviations(lv)
        corr = {k: round(correction(v), 2) for k, v in d.items()}
        target = {'kind': 'line', 'why': f'the line within T = {THRESHOLD} dB (real players\' key-to-key scatter)'}
        if name in REAL_SHAPE:
            assert real, f'{name} takes the real player\'s key shape: --csv is needed'
            ff = real[REAL_SHAPE[name]]['ff']
            for k in d:
                if k in ff:
                    corr[k] = round(ff[k] - d[k], 2)
            target = {'kind': 'realShape', 'instrument': REAL_SHAPE[name],
                      'keys': sorted(k for k in d if k in ff),
                      'why': "the line plus TU Berlin's deviation at ff at each key (the owner, 2026-10-28): SSO's key "
                             "pattern follows the real player's (two independent sources agree in direction); TU is one "
                             "player, one take a pitch (take noise included); keys TU lacks: the line within T"}
        after = {k: d[k] + corr[k] for k in d}
        byCc = {}
        for cc in sec['ccs']:
            l2 = level_at(sec, cc)
            if len(l2) == len(lv):
                byCc[str(cc)] = {str(k): round(v, 2) for k, v in sorted(deviations(l2).items())}
        rr = {}
        for p, rows in sec['points'].items():
            r = next(x for x in rows if x['cc'] == ANCHOR)
            t = r.get('rmsDbTakes', [])
            if len(t) >= 4:
                rr[p] = round(abs((t[0] + t[2]) / 2 - (t[1] + t[3]) / 2), 2)
        meas = {}
        for m in ('loud1sDb', 'rmsDb'):
            diffs = []
            for cc in sec['ccs']:
                a, b = level_at(sec, cc), level_at(sec, cc, m)
                if len(a) == len(b) == len(lv):
                    da, dbm = deviations(a), deviations(b)
                    diffs += [da[k] - dbm[k] for k in da]
            meas[m] = round(rms(diffs), 2)
        e = {'keys': [min(lv), max(lv)],
             'keyLevels': {str(k): v for k, v in sorted(corr.items()) if abs(v) >= MIN_DB},
             'deviation': {str(k): round(v, 2) for k, v in sorted(d.items())},
             'rmsDeviationDb': round(rms(list(d.values())), 2), 'rmsAfterDb': round(rms(list(after.values())), 2),
             'target': target, 'largestDb': round(max(corr.values(), key=abs), 2), 'corrected': sum(1 for v in corr.values() if abs(v) >= MIN_DB),
             'byCc': byCc, 'rr': rr, 'rrMedianDb': round(st.median(rr.values()), 2) if rr else 0.0, 'measures': meas}
        lab = db.get(name)
        if lab and real:
            ff = real[lab]['ff']
            shared = [k for k in d if k in ff]
            if len(shared) > 3:
                x, y = [d[k] for k in shared], [ff[k] for k in shared]
                mx, my = st.mean(x), st.mean(y)
                sx = math.sqrt(sum((a - mx) ** 2 for a in x))
                sy = math.sqrt(sum((b - my) ** 2 for b in y))
                cor = sum((a - mx) * (b - my) for a, b in zip(x, y)) / (sx * sy) if sx and sy else 0.0
                # the corrected keys whose real player deviates the same way by more than T at ff
                same = [k for k in shared if abs(corr[k]) >= MIN_DB and ff[k] * d[k] > 0 and abs(ff[k]) > THRESHOLD]
                e['realPlayer'] = {'instrument': lab, 'keys': len(shared), 'correlationFf': round(cor, 2),
                                   'sameWayOverT': same}
        out['patches'][name] = e
        print(f"{name:22s} rms d {e['rmsDeviationDb']:.2f} -> {e['rmsAfterDb']:.2f} dB, {e['corrected']:2d} keys, largest "
              f"{e['largestDb']:+.2f}" + (f", TU r {e['realPlayer']['correlationFf']:+.2f} same-way {e['realPlayer']['sameWayOverT']}"
                                         if 'realPlayer' in e else ''), file=sys.stderr)
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
        f.write('\n')


if __name__ == '__main__':
    main()
