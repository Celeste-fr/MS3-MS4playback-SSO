#!/usr/bin/env python3
"""Join features (test_eval.py's hand-over measures: fluxZ, dipDb, pumpDb, hfDb, outC) judged against SSO's own
render-to-render spread: leave one SSO render out, take the 95th / 99th percentile (dipDb: 5th / 1st) of the other SSO
renders pooled, and count how often the left-out SSO render and each of ours fall beyond them; averaged over the
left-out render. Inside SSO's spread = our share no larger than SSO's own.

    r3_tails.py --sso te1.json,te2.json,... <ours te json> [...] [--type fingered] [--ioi 0.136]
"""
import argparse
import json

import numpy as np

K = ['fluxZ', 'dipDb', 'pumpDb', 'hfDb', 'outC']


def rows(p, a):
    R = json.load(open(p))['rows']
    if a.type:
        R = [r for r in R if r.get('type') in a.type.split(',')]
    if a.ioi:
        R = [r for r in R if abs(r.get('ioi', 0) - a.ioi) < 1e-6]
    return R


def lim(R):
    out = {}
    for k in K:
        v = np.array([r[k] for r in R if r.get(k) is not None])
        out[k] = np.percentile(v, [5, 1]) if k == 'dipDb' else np.percentile(v, [95, 99])
    return out


def rate(R, L, j):
    bad = lambda r, k: r.get(k) is not None and ((r[k] < L[k][j]) if k == 'dipDb' else (r[k] > L[k][j]))  # noqa: E731
    per = {k: 100 * np.mean([bad(r, k) for r in R if r.get(k) is not None]) for k in K}
    per['any'] = 100 * np.mean([any(bad(r, k) for k in K) for r in R])
    return per


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('ours', nargs='*')
    ap.add_argument('--sso', required=True)
    ap.add_argument('--type')
    ap.add_argument('--ioi', type=float)
    a = ap.parse_args()
    S = [rows(p, a) for p in a.sso.split(',')]
    O = {p: rows(p, a) for p in a.ours}
    res = {'SSO (left out)': [], **{p: [] for p in O}}
    for j in range(len(S)):
        L = lim(sum((S[i] for i in range(len(S)) if i != j), []))
        for q in (0, 1):
            pass
        res['SSO (left out)'].append([rate(S[j], L, q) for q in (0, 1)])
        for p, R in O.items():
            res[p].append([rate(R, L, q) for q in (0, 1)])
    print('n joins per render: %d; shares beyond SSO\'s p95 / p99 (left-out SSO renders: %d)' % (len(S[0]), len(S)))
    print('%-34s' % '' + ''.join('%14s' % k for k in K + ['any']))
    for p, v in res.items():
        m = {k: (np.mean([x[0][k] for x in v]), np.mean([x[1][k] for x in v])) for k in K + ['any']}
        print('%-34s' % p.split('/')[-1][-34:] + ''.join('%14s' % ('%.1f/%.1f' % m[k]) for k in K + ['any']))


if __name__ == '__main__':
    main()
