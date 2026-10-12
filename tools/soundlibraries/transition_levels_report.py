#!/usr/bin/env python3
"""Reads sso_transition_levels.json (transition_level_scan.py table): how much a legato note's level depends on where
it is reached from, and whether that is steady enough to correct. Written for this check (2026-10-12).

    transition_levels_report.py [sso_transition_levels.json] [--measure heard]

Per patch, condition and transition velocity, with T(y, i) the level (perceived, ~dB) of destination y reached from
y - i, A(y) the same y by the spiccato attack:
  vs attack    T - A: median and 90th percentile of |T - A| (and the signed median)
  interval     T(y, i) - median over i of T(y, .): the part a per-interval correction removes (median / p90 of |.|)
  destination  D(y) = median over i of T(y, .) against the running median of D over y +-3 semitones (p50 / p90 |.|)
  direction    T(y, +k) - T(y, -k), median over y and k
  repeat       the takes' spread of T (max - min; median, p90) and the split-half check: the interval term from take A
               against take B's (Pearson r, and the median |A - B|)
  length       r of the interval term at 136 ms (S64) against 600 ms (L64: heard, steady)
  dynamic      r of the interval term at CC1 64 against 32 / 112 (S32, S112), median |difference|
  bowed        T(93) - T(69), median
Whence 12-TET bar 2 (Celli): the four notes predicted from the table (sso_slur_swell.json's window, 'swell') against
that file's means, and what a correction rule would do to them.
"""
import argparse
import json
import math
import os
import statistics as st

HERE = os.path.dirname(os.path.abspath(__file__))


def pct(v, f):
    v = sorted(v)
    if not v:
        return float('nan')
    k = (len(v) - 1) * f
    a, b = math.floor(k), math.ceil(k)
    return v[a] + (v[b] - v[a]) * (k - a)


def pearson(a, b):
    if len(a) < 3:
        return float('nan')
    ma, mb = st.fmean(a), st.fmean(b)
    sa = math.sqrt(sum((x - ma) ** 2 for x in a))
    sb = math.sqrt(sum((x - mb) ** 2 for x in b))
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (sa * sb) if sa and sb else float('nan')


def levels(cond, v, meas, take=None):
    """{y: {i: T}}"""
    out = {}
    for y, ivs in cond.get('legato', {}).get(str(v), {}).items():
        for i, e in ivs.items():
            val = e.get('takes', {}).get(take) if take else e.get(meas)
            if val is not None:
                out.setdefault(int(y), {})[int(i)] = val
    return out


def interval_term(T):
    """{(y, i): T(y, i) - median_i T(y, .)} for destinations with at least 4 intervals"""
    out = {}
    for y, ivs in T.items():
        if len(ivs) >= 4:
            m = st.median(ivs.values())
            for i, val in ivs.items():
                out[(y, i)] = val - m
    return out


def dest_term(T):
    D = {y: st.median(ivs.values()) for y, ivs in T.items() if len(ivs) >= 4}
    out = {}
    for y, d in D.items():
        near = [D[k] for k in range(y - 3, y + 4) if k in D]
        if len(near) >= 4:
            out[y] = d - st.median(near)
    return D, out


def common(a, b):
    ks = sorted(set(a) & set(b))
    return [a[k] for k in ks], [b[k] for k in ks]


def report(path, meas):
    data = json.load(open(path))['patches']
    for patch, conds in data.items():
        print(f'== {patch}')
        for cname, c in sorted(conds.items()):
            m = 'steady' if cname == 'L64' and meas == 'heard' else meas
            for v in (69, 93):
                T = levels(c, v, m)
                if not T:
                    continue
                A = {int(y): e[m] for y, e in c.get('spiccato', {}).items() if m in e}
                off = [T[y][i] - A[y] for y in T for i in T[y] if y in A]
                it = interval_term(T)
                D, dt = dest_term(T)
                dirs = [T[y][k] - T[y][-k] for y in T for k in (1, 2, 3, 4, 5, 7, 12) if k in T[y] and -k in T[y]]
                spreads = [e.get(m + 'Spread') for ys in c['legato'][str(v)].values() for e in ys.values()
                           if e.get(m + 'Spread') is not None and e.get('n', 0) > 1]
                line = (f'{cname} v{v}: vs attack {st.median(off):+.1f} (|.| p50 {pct([abs(x) for x in off], .5):.1f} '
                        f'p90 {pct([abs(x) for x in off], .9):.1f}); interval |.| p50 {pct([abs(x) for x in it.values()], .5):.1f} '
                        f'p90 {pct([abs(x) for x in it.values()], .9):.1f}; dest |.| p50 {pct([abs(x) for x in dt.values()], .5):.1f} '
                        f'p90 {pct([abs(x) for x in dt.values()], .9):.1f}; up-down {st.median(dirs):+.2f}')
                if spreads:
                    line += f'; take spread p50 {pct(spreads, .5):.1f} p90 {pct(spreads, .9):.1f}'
                ta, tb = levels(c, v, m, 'A'), levels(c, v, m, 'B')
                if ta and tb and cname != 'L64':
                    a, b = common(interval_term(ta), interval_term(tb))
                    line += (f'; split-half r {pearson(a, b):.2f} |A-B| p50 {pct([abs(x - y) for x, y in zip(a, b)], .5):.1f}')
                print(line)
        s = conds.get('S64')
        for v in (69, 93):
            if not s or not levels(s, v, meas):
                continue
            base = interval_term(levels(s, v, meas))
            parts = []
            for other, om in (('L64', 'heard'), ('L64', 'steady'), ('S32', meas), ('S112', meas)):
                if other in conds and levels(conds[other], v, om):
                    a, b = common(base, interval_term(levels(conds[other], v, om)))
                    parts.append(f'{other} {om}: r {pearson(a, b):.2f}, |diff| p50 {pct([abs(x - y) for x, y in zip(a, b)], .5):.1f} (n {len(a)})')
            print(f'  S64 v{v} interval term against ' + '; '.join(parts))
        if s:
            t69, t93 = levels(s, 69, meas), levels(s, 93, meas)
            d = [t93[y][i] - t69[y][i] for y in t93 for i in t93[y] if y in t69 and i in t69[y]]
            if d:
                print(f'  S64 bowed - fingered: median {st.median(d):+.2f}, p10 {pct(d, .1):+.1f} p90 {pct(d, .9):+.1f}')


BAR2 = [(51, 2, 93), (52, 1, 69), (51, -1, 69), (49, -2, 69)]


def whence(path, meas):
    data = json.load(open(path))['patches']['Celli - Performance']['S64']
    swell = json.load(open(os.path.join(HERE, 'sso_slur_swell.json')))['means']
    for label, notes, var in (('as rendered', BAR2, 'A'), ('all fingered', [(51, 2, 69)] + BAR2[1:], 'B')):
        pred = [data['legato'][str(v)][str(y)][f'{i:+d}'][meas] for y, i, v in notes]
        got = swell[var]['perceivedArrival']
        print(f'Whence bar 2 {label}: predicted {" ".join(f"{x - pred[0]:+.1f}" for x in pred)} (abs '
              f'{" ".join(f"{x:.1f}" for x in pred)}); sso_slur_swell {var} {" ".join(f"{x - got[0]:+.1f}" for x in got)}')
        # correction: each note to its destination's median over intervals (same velocity band), and to the
        # destination's running-median trend
        T = {v: levels(data, v, meas) for v in (69, 93)}
        c1 = [-(T[v][y][i] - st.median(T[v][y].values())) for y, i, v in notes]
        D, dt = dest_term(T[69])
        c2 = [c1[k] - dt.get(y, 0.0) for k, (y, i, v) in enumerate(notes)]
        for name, c in (('interval only', c1), ('interval + destination', c2)):
            after = [g + x for g, x in zip(got, c)]
            print(f'   correction {name}: CC11 dB {" ".join(f"{x:+.1f}" for x in c)} -> measured + correction '
                  f'{" ".join(f"{x:.1f}" for x in after)} (spread {max(after) - min(after):.1f}, was {max(got) - min(got):.1f})')


def slur_pred(tab, ps, idx, meas):
    out = []
    for k in idx:
        v = 93 if (k - 1) % 4 == 0 else 69
        e = tab.get(str(v), {}).get(str(ps[k]), {}).get(f'{ps[k] - ps[k - 1]:+d}')
        out.append(e.get(meas) if e else None)
    return out


def runs(path, meas):
    """the validation runs (sso_transition_levels.json 'runs'): per slur of four, each note's level less the slur's mean,
    measured against the table's T(y, i, v) (S64); pass-to-pass repeatability; the slur-position profile; and what four
    correction rules would leave (per-note CC11 dB, applied to the measured levels: none; bowed: the slur's first note
    by minus the patch's median bowed - fingered; table: minus the table's slur-relative level; table/2: half of it)"""
    j = json.load(open(path))
    data, allruns = j['patches'], j.get('runs', {})
    for patch, phrases in allruns.items():
        tab = data[patch]['S64']['legato']
        bowd = [tab['93'][y][i][meas] - tab['69'][y][i][meas] for y in tab['93'] for i in tab['93'][y]
                if y in tab['69'] and i in tab['69'][y]]
        bow = st.median(bowd)
        md, pd, tk, prof = [], [], [], [[], [], [], []]
        opts = {k: ([], []) for k in ('none', 'bowed', 'table', 'table/2')}
        for ph in phrases:
            ps = ph['pitches']
            passes = [x[meas] for t in ph['takes'].values() for x in t]
            for s0 in range(1, len(ps), 4):
                idx = list(range(s0, min(s0 + 4, len(ps))))
                for lv in passes:
                    m = [lv[k] for k in idx]
                    mm = st.fmean(m)
                    for q, x in enumerate(m):
                        prof[q].append(x - mm)
                pred = slur_pred(tab, ps, idx, meas)
                if None in pred:
                    continue
                pm = st.fmean(pred)
                for lv in passes:
                    m = [lv[k] for k in idx]
                    mm = st.fmean(m)
                    md += [x - mm for x in m]
                    pd += [x - pm for x in pred]
                    for name, (rng, dev) in opts.items():
                        c = [0.0] * len(idx)
                        if name == 'bowed':
                            c[0] = -bow
                        elif name == 'table':
                            c = [-(x - pm) for x in pred]
                        elif name == 'table/2':
                            c = [-(x - pm) / 2 for x in pred]
                        mc = [x + y for x, y in zip(m, c)]
                        mcm = st.fmean(mc)
                        rng.append(max(mc) - min(mc))
                        dev += [x - mcm for x in mc]
                if len(passes) >= 2:
                    a, b = passes[0], passes[1]
                    ma, mb = st.fmean(a[k] for k in idx), st.fmean(b[k] for k in idx)
                    tk += [abs((a[k] - ma) - (b[k] - mb)) for k in idx]
        print(f'runs {patch} ({meas}): {len(md)} notes; slur-relative SD {st.pstdev(md):.2f} dB; table r '
              f'{pearson(pd, md):.2f}; pass-to-pass |diff| p50 {pct(tk, .5):.2f} p90 {pct(tk, .9):.2f}; by slur position '
              f'{" ".join(f"{st.fmean(x):+.2f}" for x in prof)}')
        print('   corrections (within-slur range p50 / p90, SD): ' + '; '.join(
            f'{k} {pct(r, .5):.1f} / {pct(r, .9):.1f}, {st.pstdev(d):.2f}' for k, (r, d) in opts.items())
              + f' (bowed - fingered {bow:+.2f})')
        for ph in phrases:
            if ph['pitches'][:5] == [49, 51, 52, 51, 49]:
                passes = [x[meas] for t in ph['takes'].values() for x in t]
                pos = [st.fmean(l[k] for l in passes for k in range(1 + q, 17, 4)) for q in range(4)]
                sd = [st.pstdev([st.fmean(l[k] for k in range(1 + q, 17, 4)) for l in passes]) for q in range(4)]
                pred = slur_pred(tab, ph['pitches'], range(1, 5), meas)
                pm = st.fmean(pred)
                c = [-(x - pm) / 2 for x in pred]
                print(f'   Whence bar 2 in the runs ({len(passes)} passes): {" ".join(f"{x:.1f}" for x in pos)} (pass SD '
                      f'{max(sd):.2f}); with table/2 (CC11 {" ".join(f"{x:+.1f}" for x in c)}): '
                      f'{" ".join(f"{x + y:.1f}" for x, y in zip(pos, c))}')
                break


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('json', nargs='?', default=os.path.join(HERE, 'sso_transition_levels.json'))
    ap.add_argument('--measure', default='heard')
    o = ap.parse_args()
    report(o.json, o.measure)
    whence(o.json, 'swell')
    whence(o.json, o.measure)
    runs(o.json, o.measure)
    runs(o.json, 'swell')


if __name__ == '__main__':
    main()
