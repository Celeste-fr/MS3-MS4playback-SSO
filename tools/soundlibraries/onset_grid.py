#!/usr/bin/env python3
"""Onsets by pitch, CC1 and played length, and transition timing by the note before's length ([notes] onsetModel 1).

Reads the re-measure's measure_patch.py analyze files (one per folder: a_<section>.json the All techniques Long,
b_<section>.json the Performance smooth attack at velocity 5, c_<section>_v<vel>.json the Performance transitions after a
smooth attack) and writes sso_onset_grid.json, which gen_spitfire_sso.py turns into the map's onsetGrid= (Long),
attackOnsetGrid= (smooth) and transitionGlide= (fingered / bowed) on the patches measured.

    python3 tools/soundlibraries/onset_grid.py <raw folder> [tools/soundlibraries/sso_onset_grid.json]

Rules (fixed before looking at the map's effect):
- a note cell's value is the median of its takes' onset15 (the map's onset rule: 15 dB under the note's peak, minus the
  30 ms latency); the takes alternate SSO's round robins (ABBA), so the median sits between them;
- a dimension (CC1, length) is kept only if it is real: the range of its main effect (cells less their pitch's mean,
  medians over pitches and the other dimension) is > 15 ms AND larger than twice its bootstrap SE over pitches; a
  dimension that isn't real is collapsed (the median over it), so the map gets one value there;
- transitions: leave / arrive medians over pitches, intervals and takes per note-before length, valid readings only
  (the rule of sso_legato_glide_bowed.json's BOWED_ARRIVE: leave > -5 ms, arrive < 550 ms, both present).
"""
import json, os, random, statistics as S, sys, collections

MIN_MS = 15.0
SECTIONS = {'Violins1': 'Violins 1', 'Violins2': 'Violins 2', 'Violas': 'Violas', 'Celli': 'Celli', 'Basses': 'Basses'}
KIND_BY_VELOCITY = {69: 'fingered', 93: 'bowed'}   # the bands Auto plays on a Performance patch (attacks=/transitions=)


def med(v):
    v = [x for x in v if x is not None]
    return S.median(v) if v else None


def quality_notes(cells):
    sd, rr, n, missing, outliers = [], [], [], 0, 0
    for c in cells:
        t = c['measures'].get('onset15', {}).get('takes', [])
        missing += sum(1 for x in t if x is None)
        t = [x for x in t if x is not None]
        n.append(len(t))
        if len(t) >= 2:
            sd.append(S.pstdev(t))
            m = S.median(t)
            mad = S.median([abs(x - m) for x in t]) or 1.0
            outliers += sum(1 for x in t if abs(x - m) > 5 * max(mad, 5.0))
        ev, od = t[0::2], t[1::2]
        if ev and od:
            rr.append(abs(S.median(ev) - S.median(od)))
    return {'cells': len(cells), 'takesPerCell': S.median(n) if n else 0, 'missingTakes': missing,
            'medianTakeSdMs': round(S.median(sd), 1) if sd else None,
            'medianRoundRobinDiffMs': round(S.median(rr), 1) if rr else None,
            'outlierTakes': outliers}


def effect(table, pitches, ccs, lens, dim, sample=None):
    """the main effect of dim ('cc1' / 'length') over the given pitches: dict value -> ms"""
    sample = sample or pitches
    out = {}
    vals = ccs if dim == 'cc1' else lens
    for v in vals:
        d = []
        for p in sample:
            row = [table[(p, c, l)] for c in ccs for l in lens if (p, c, l) in table]
            if not row:
                continue
            mean = S.mean(row)
            cells = [table[(p, c, l)] - mean for c in ccs for l in lens
                     if (p, c, l) in table and (c if dim == 'cc1' else l) == v]
            d += cells
        out[v] = S.median(d) if d else 0.0
    return out


def notes(path):
    d = json.load(open(path))
    takes = collections.defaultdict(list)
    for c in d['cells']:
        k = c['key']
        takes[(k['pitch'], k['cc1'], k['lengthMs'])] += [x for x in c['measures'].get('onset15', {}).get('takes', []) if x is not None]
    table = {k: S.median(v) for k, v in takes.items() if v}
    pitches = sorted({k[0] for k in table})
    ccs = sorted({k[1] for k in table})
    lens = sorted({k[2] for k in table})
    rnd = random.Random(1)
    dims = {}
    for dim in ('cc1', 'length'):
        e = effect(table, pitches, ccs, lens, dim)
        rng = max(e.values()) - min(e.values())
        boots = []
        for _ in range(400):
            s = [rnd.choice(pitches) for _ in pitches]
            b = effect(table, pitches, ccs, lens, dim, s)
            boots.append(max(b.values()) - min(b.values()))
        se = S.pstdev(boots)
        dims[dim] = {'effectMs': {str(k): round(v, 1) for k, v in e.items()}, 'rangeMs': round(rng, 1),
                     'bootstrapSeMs': round(se, 1), 'real': rng > MIN_MS and rng > 2 * se}
    # pitch: the spread of the pitch means
    pm = [S.mean([table[(p, c, l)] for c in ccs for l in lens if (p, c, l) in table]) for p in pitches]
    keepC = ccs if dims['cc1']['real'] else [64 if 64 in ccs else ccs[len(ccs) // 2]]
    keepL = lens if dims['length']['real'] else [lens[len(lens) // 2]]
    grid = []
    for p in pitches:
        for c in keepC:
            for l in keepL:
                v = [table[(p, cc, ll)] for cc in (ccs if not dims['cc1']['real'] else [c])
                     for ll in (lens if not dims['length']['real'] else [l]) if (p, cc, ll) in table]
                if v:
                    grid.append([c, l, p, round(S.median(v), 1)])
    cmd = d.get('method', {}).get('command', '')
    cmd = cmd.split(' --out ')[0]
    return {'patch': d.get('patch'), 'articulation': d.get('method', {}).get('articulation'),
            'velocity': d['cells'][0]['key'].get('velocity'),
            'quality': quality_notes(d['cells']), 'pitchMeanRangeMs': round(max(pm) - min(pm), 1),
            'dims': dims, 'grid': grid, 'command': cmd, 'takes': d.get('method', {}).get('takes')}


def transitions(path):
    d = json.load(open(path))
    rows = collections.defaultdict(lambda: {'leave': [], 'arrive': [], 'heardRef': []})
    byInterval = collections.defaultdict(list)
    total = valid = 0
    for c in d['cells']:
        k = c['key']
        m = c['measures']
        L = m.get('leave', {}).get('takes', [])
        A = m.get('arrive', {}).get('takes', [])
        for le, ar in zip(L, A):
            total += 1
            if le is None or ar is None or le <= -5 or ar >= 550:
                continue
            valid += 1
            r = rows[k['beforeLenMs']]
            r['leave'].append(le); r['arrive'].append(ar); r['heardRef'].append(le + 23.5)
            byInterval[(k['beforeLenMs'], k['interval'])].append(ar)
    glide = [[L, round(S.median(r['leave']), 1), round(S.median(r['arrive']), 1), len(r['arrive'])]
             for L, r in sorted(rows.items())]
    iv = {}
    for L in sorted(rows):
        m = {i: S.median(v) for (l, i), v in byInterval.items() if l == L}
        iv[str(L)] = {('%+d' % i): round(x, 1) for i, x in sorted(m.items())}
    vel = d['cells'][0]['key']['transitionVelocity']
    cmd = d.get('method', {}).get('command', '').split(' --out ')[0]
    return {'patch': d.get('patch'), 'velocity': vel, 'kind': KIND_BY_VELOCITY.get(vel),
            'validTakes': valid, 'takes': total, 'glide': glide, 'arriveByInterval': iv, 'command': cmd}


def main():
    raw = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_onset_grid.json')
    res = {'_about': 'Onsets by pitch x CC1 x played length (onsetGrid= / attackOnsetGrid=) and transition leave / arrive by '
                     'the note before\'s length (transitionGlide=), read by [notes] onsetModel 1. Measured 2026-11-01 on the '
                     'Windows VM: kthost + Kontakt 8 + SSO, measure_patch.py (level,onset / transition), ABBA takes; '
                     'grid entries [cc1, lengthMs, pitch, ms]; glide entries [beforeLenMs, leaveMs, arriveMs, n valid]. '
                     'Made by onset_grid.py (its docstring has the rules).',
           'long': {}, 'smooth': {}, 'transitions': {}}
    for f in sorted(os.listdir(raw)):
        if not f.endswith('.json'):
            continue
        name = f[:-5]
        kind, rest = name.split('_', 1)
        section = SECTIONS[rest.split('_')[0]]
        path = os.path.join(raw, f)
        if kind == 'a':
            res['long'][section] = notes(path)
        elif kind == 'b':
            res['smooth'][section + ' - Performance'] = notes(path)
        elif kind == 'c':
            t = transitions(path)
            if t['kind']:
                res['transitions'].setdefault(section + ' - Performance', {})[t['kind']] = t
            else:
                res['transitions'].setdefault(section + ' - Performance', {})['v%d (unused)' % t['velocity']] = t
    json.dump(res, open(out, 'w'), indent=1)
    for part in ('long', 'smooth'):
        for s, v in res[part].items():
            q = v['quality']
            print('%-6s %-24s cells %3d takes %s missing %d sd %s rr %s outl %d | cc1 %5.1f±%4.1f %s | len %5.1f±%4.1f %s | pitch %5.1f'
                  % (part, s, q['cells'], q['takesPerCell'], q['missingTakes'], q['medianTakeSdMs'], q['medianRoundRobinDiffMs'],
                     q['outlierTakes'], v['dims']['cc1']['rangeMs'], v['dims']['cc1']['bootstrapSeMs'], v['dims']['cc1']['real'],
                     v['dims']['length']['rangeMs'], v['dims']['length']['bootstrapSeMs'], v['dims']['length']['real'],
                     v['pitchMeanRangeMs']))
    for s, kinds in res['transitions'].items():
        for k, t in kinds.items():
            print('trans  %-24s %-14s valid %d/%d glide %s' % (s, k, t['validTakes'], t['takes'], t['glide']))
    print('wrote', out)


if __name__ == '__main__':
    main()
