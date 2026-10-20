#!/usr/bin/env python3
"""Score renders of held-out phrases against SSO's passes per hold class and join position (r4_eval.py's measures:
bump / dip / hf / spec, arrival, tails; joinbreak.py's positions): median |ours - SSO_k| over joins and k vs the floor
median |SSO_i - SSO_j|. Phrases are classed by their run IOI (nearest of 150 / 300 / 600 / 1000 ms).
    ol_score.py <test.json> <ours.wav> <sso1> <sso2> [<sso3> ...] [--json out.json] [--loo]
--loo: also score each SSO pass as 'ours' against the others (the floor's own spread)."""
import json, os, sys
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.expanduser('~/MuseScore/ms3fork/private/own-legato-tells/scripts'))
import r4_eval as E  # noqa
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ol_build import cls_of, frames  # noqa

MS = ('bump', 'dip', 'hf', 'spec')


def labels(tp):
    T = json.load(open(tp))
    lab, tl = [], []
    for s in T['segments']:
        on = [frames(q['on']) for q in s['notes']]
        c = cls_of(on[2] - on[1]) if len(on) > 2 else cls_of(on[1] - on[0])
        n = len(on)
        for i in range(1, n):
            lab.append((c, 'a2' if i == 1 else 'last' if i == n - 1 else 'run'))
        tl.append(c)
    return lab, tl


def score(F0, Fs, lab, tl):
    out = {}
    def d(a, b, k, idx):
        return [np.sqrt(np.mean((a[0][i]['bands'] - b[0][i]['bands']) ** 2)) if k == 'spec' else abs(a[0][i][k] - b[0][i][k])
                for i in idx]
    for c in sorted(set(x[0] for x in lab)) + ['all']:
        for pos in ('a2', 'run', 'last', 'all'):
            idx = [i for i, (cc, pp) in enumerate(lab) if (c == 'all' or cc == c) and (pos == 'all' or pp == pos)]
            if not idx:
                continue
            row = {}
            for k in MS:
                o = np.median([d(F0, F, k, idx) for F in Fs], 0)
                f = np.median([d(Fs[i], Fs[j], k, idx) for i in range(len(Fs)) for j in range(i + 1, len(Fs))], 0)
                row[k] = (round(float(np.median(o)), 2), round(float(np.median(f)), 2))
            ar_o = [1000 * abs(F0[0][i]['arr'] - F[0][i]['arr']) for F in Fs for i in idx
                    if F0[0][i]['arr'] is not None and F[0][i]['arr'] is not None]
            ar_f = [1000 * abs(Fs[a][0][i]['arr'] - Fs[b][0][i]['arr']) for a in range(len(Fs)) for b in range(a + 1, len(Fs))
                    for i in idx if Fs[a][0][i]['arr'] is not None and Fs[b][0][i]['arr'] is not None]
            if ar_o and ar_f:
                row['arr_ms'] = (round(float(np.median(ar_o)), 1), round(float(np.median(ar_f)), 1))
            row['n'] = len(idx)
            out['%s/%s' % (c, pos)] = row
        pi = [i for i, cc in enumerate(tl) if c == 'all' or cc == c]
        to = np.median([np.abs(F0[1][pi] - F[1][pi]) for F in Fs], 0)
        tf = np.median([np.abs(Fs[a][1][pi] - Fs[b][1][pi]) for a in range(len(Fs)) for b in range(a + 1, len(Fs))], 0)
        out['%s/tail' % c] = dict(zip(('0.1', '0.25', '0.5', '1.0'), [(round(float(x), 2), round(float(y), 2))
                                                                      for x, y in zip(np.median(to, 0), np.median(tf, 0))]))
    return out


def show(out, title):
    print(title)
    for k, row in out.items():
        if k.endswith('/tail'):
            print('  %-10s tails ' % k[:-5] + ' '.join('+%s %.2f [%.2f]' % (t, a, b) for t, (a, b) in row.items()))
            continue
        print('  %-10s n=%3d ' % (k, row['n']) + ' | '.join('%s %.2f [%.2f]' % (m, *row[m]) for m in MS) +
              (' | arr %.1f [%.1f] ms' % row['arr_ms'] if 'arr_ms' in row else ''))


def main():
    a = [x for x in sys.argv[1:] if not x.startswith('--')]
    jo = sys.argv[sys.argv.index('--json') + 1] if '--json' in sys.argv else None
    if jo:
        a.remove(jo)
    tp, ours, ssos = a[0], a[1], a[2:]
    L = E.lines(tp)
    lab, tl = labels(tp)
    F0 = E.feats(sf.read(ours, always_2d=True)[0], L)
    Fs = [E.feats(sf.read(p, always_2d=True)[0], L) for p in ssos]
    res = {'ours': score(F0, Fs, lab, tl)}
    show(res['ours'], 'ours (%s) vs %d SSO passes: ours [floor]' % (os.path.basename(ours), len(Fs)))
    if '--loo' in sys.argv:
        for i in range(len(Fs)):
            r = score(Fs[i], [F for j, F in enumerate(Fs) if j != i], lab, tl)
            res['sso%d' % (i + 1)] = r
            show({k: v for k, v in r.items() if k.startswith('all/')}, 'SSO pass %d as ours vs the others' % (i + 1))
    if jo:
        json.dump(res, open(jo, 'w'), indent=1)


if __name__ == '__main__':
    main()
