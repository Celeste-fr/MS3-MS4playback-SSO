# verify_shift.py <sso_slur_rhythm.json> <out verify.json> <tag>=<heard rows.json>:<seed>:<shift by section or 0> …: the VM check of
# [slurs] slurShift (2026-10-16, branch slur-shift): the slur-rhythm grid (score.py) dumped by this build with every string
# section's Performance slurs shifted (a test map: Violas their measured 37 ms, the other sections 60, only for the check)
# and without, rendered by kthost + Kontakt 8 + SSO (run.cmd, CC11 127) and read by heard.py, against the grid's A rows in
# sso_slur_rhythm.json (the same shuffles: take A seed 16, take B 17).
#   shift: each note's heard - written with the shift less the same note's without it (same seed: SSO's round robins in
#     the same order), less the shift sent; inside a slur, each later note's change less the first note's (the heard
#     timing inside the slur: 0 when the shift keeps it);
#   noise: the same for two renders of the grid without a shift (the A rows of take A against this build's a16: two
#     renders, same order), and between the takes (the same cell, the round robins in another order: take A against B);
#   with: the shifted renders' errors by slur position and the slurs' worst notes (Violas: the map's 37 ms).
# Shift arguments: "Violas=37,*=60" (a section's, else *'s).
import json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_argv, sys.argv = sys.argv, sys.argv[:1]
import score as S  # noqa: E402
sys.argv = _argv
PN = {'Violins I': 'Violins 1', 'Violins II': 'Violins 2', 'Violas': 'Violas', 'Violoncellos': 'Celli', 'Contrabasses': 'Basses'}
OUT = 250.0


def cells(seed):
    S.SEED = seed
    _, idx, _ = S.build()
    return idx


def old_rows(path):
    """sso_slur_rhythm.json's A rows: section -> seed -> list of slurs (lists of errors)"""
    src = json.load(open(path, encoding='utf-8'))['sections']
    out = {}
    for patch, v in src.items():
        sec = patch.replace(' - Performance', '')
        slurs, cur = [], None
        for r in v['rows']:
            if r[1] == 0:
                cur = []
                slurs.append(cur)
            cur.append((r[4] + r[6]) if r[6] is not None else None)
        h = len(slurs) // 2
        out[sec] = {16: slurs[:h], 17: slurs[h:]}
    return out


def new_rows(path):
    rows = json.load(open(path, encoding='utf-8'))
    by = {}
    for r in rows:
        sec = PN.get(r['part'], r['part'])
        if r['slur'] < 0 or r['k'] < 0:
            continue
        by.setdefault(sec, {}).setdefault(r['slur'], []).append(r)
    out = {}
    for sec, d in by.items():
        out[sec] = [[x['heard'] for x in sorted(d[s], key=lambda x: x['k'])] for s in sorted(d, key=lambda s: d[s][0]['w'])]
    return out


def ok(v):
    return v is not None and abs(v) <= OUT


def pairs(a, b, dshift):
    """per note: (b - a - dshift) and inside the slur (that, less the first note's)"""
    tot, inner = [], []
    for sa, sb in zip(a, b):
        if len(sa) != len(sb):
            continue
        d = [(y - x - dshift) if ok(x) and ok(y) else None for x, y in zip(sa, sb)]
        tot += [v for v in d if v is not None]
        if d[0] is not None:
            inner += [v - d[0] for v in d[1:] if v is not None]
    return tot, inner


def stats(v):
    v = np.abs(np.array(v)) if v else np.array([np.nan])
    return dict(n=int(np.sum(~np.isnan(v))), medianAbsMs=round(float(np.nanmedian(v)), 1), p90AbsMs=round(float(np.nanpercentile(v, 90)), 1),
                within5=round(float(np.nanmean(v <= 5)), 3))


def main():
    old = old_rows(sys.argv[1])
    runs = {}
    for a in sys.argv[3:]:
        tag, rest = a.split('=', 1)
        f, seed, sh = rest.split(':')
        shift = {}
        for kv in sh.split(','):
            if '=' in kv:
                k, v = kv.split('=')
                shift[k] = float(v)
            else:
                shift['*'] = float(kv)
        runs[tag] = dict(rows=new_rows(f), seed=int(seed), shift=shift)
    res = dict(shift={}, noise={}, withShift={})
    secs = sorted(old)
    for tag, r in runs.items():
        if not any(r['shift'].values()):
            continue
        base = [t for t, x in runs.items() if x['seed'] == r['seed'] and not any(x['shift'].values())]
        for sec in secs:
            d = r['shift'].get(sec, r['shift'].get('*', 0.0))
            ref = runs[base[0]]['rows'][sec] if base else old[sec][r['seed']]
            tot, inner = pairs(ref, r['rows'][sec], d)
            res['shift'].setdefault(sec, {})[tag] = dict(shiftMs=d, against=base[0] if base else 'A rows', note=stats(tot), inside=stats(inner))
            # with the shift: by position, worst notes
            sl = [[v for v in s if ok(v)] for s in r['rows'][sec]]
            sl = [np.array(s) for s in sl if len(s) >= 2]
            pos = {}
            for k in range(4):
                v = [s[k] for s in r['rows'][sec] if len(s) > k and ok(s[k])]
                if v:
                    pos['k%d' % k] = round(float(np.median(v)), 1)
            w = np.array([np.max(np.abs(s)) for s in sl])
            res['withShift'].setdefault(sec, {})[tag] = dict(shiftMs=d, byPosition=pos, medianWorstMs=round(float(np.median(w)), 1),
                                                           notesWithin50=round(float(np.mean(np.abs(np.concatenate(sl)) <= 50)), 3),
                                                           slursWithin50=round(float(np.mean(w <= 50)), 3))
    for sec in secs:
        n = {}
        for tag, r in runs.items():
            if not any(r['shift'].values()):
                tot, inner = pairs(old[sec][r['seed']], r['rows'][sec], 0.0)
                n['renders (%s against the A rows)' % tag] = dict(note=stats(tot), inside=stats(inner))
        # the takes: the same cell, another order
        c16, c17 = cells(16)[sec], cells(17)[sec]
        key = lambda c: (c['l1'], c['n'], c['bpm'], c['dyn'], c['pi'])
        at17 = {key(c): i for i, c in enumerate(c17)}
        a = old[sec][16]
        b = [old[sec][17][at17[key(c)]] for c in c16]
        tot, inner = pairs(a, b, 0.0)
        n['takes (A rows, take A against B)'] = dict(note=stats(tot), inside=stats(inner))
        res['noise'][sec] = n
    json.dump(res, open(sys.argv[2], 'w', encoding='utf-8'), indent=1)
    for sec in secs:
        print('==', sec)
        for tag, x in res['shift'].get(sec, {}).items():
            print('  shift %-4s %5.0f ms vs %-9s note |dev| med %5.1f p90 %5.1f  inside med %5.1f p90 %5.1f within5 %.2f' % (
                tag, x['shiftMs'], x['against'], x['note']['medianAbsMs'], x['note']['p90AbsMs'], x['inside']['medianAbsMs'],
                x['inside']['p90AbsMs'], x['inside']['within5']))
        for k, x in res['noise'][sec].items():
            print('  noise %-40s note med %5.1f p90 %5.1f  inside med %5.1f p90 %5.1f within5 %.2f' % (
                k, x['note']['medianAbsMs'], x['note']['p90AbsMs'], x['inside']['medianAbsMs'], x['inside']['p90AbsMs'], x['inside']['within5']))
        for tag, x in res['withShift'].get(sec, {}).items():
            print('  with  %-4s %s worst med %.1f notes<=50 %.2f slurs<=50 %.2f' % (tag, x['byPosition'], x['medianWorstMs'],
                                                                              x['notesWithin50'], x['slursWithin50']))


if __name__ == '__main__':
    main()
