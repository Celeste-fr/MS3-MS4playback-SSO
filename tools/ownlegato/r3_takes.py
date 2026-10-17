#!/usr/bin/env python3
"""Round 3: how many takes of a normal (velocity 20-84) transition the own-legato instrument needs, from SSO's own
render-to-render spread (twin.py --plays 4 rendered in several fresh processes: every instance plays the same chain
58 -> 58+i -> 58, i = 1..12, at velocity 64).

    r3_takes.py <twin dir> [--name twin4] [--trials 2000] [-o out.json]

Distance between two instances of one transition: the null (10 log10 sum (a-b)^2 / sum a^2, both channels) over the
note-on .. next note-on, whole-sample lag removed (twin_ana.py's rule). SSO's spread = every pair of every transition.

Coverage (the method that picks N): a take set of N instances covers SSO's spread for one transition when its own
pairs reach both of SSO's tails for that transition: its closest pair at or below the transition's 10th percentile and
its farthest at or above its 90th (percentiles over all the instances' pairs of that transition; transitions differ in
spread, so the pooled tails are reported but not used: a narrow transition never reaches them). Played in rotation, such a set then repeats a transition as alike / as unlike as SSO itself does. For each N,
random N-subsets of the instances (--trials per transition) give the share of transitions covered; N = the smallest
with >= 90 %. --holdout: the
tails from the instances outside the subset (unbiased; needs >= 6 left). Also reported: the median |difference| between the subset's own 10th / 50th / 90th percentile and SSO's.
"""
import argparse
import itertools
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import SR, stereo, load_json  # noqa: E402
from twin_ana import lag  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('--name', default='twin4')
    ap.add_argument('--trials', type=int, default=2000)
    ap.add_argument('--share', type=float, default=0.9)
    ap.add_argument('--holdout', action='store_true', help='each subset judged against the other instances\' tails')
    ap.add_argument('-o')
    a = ap.parse_args()
    J = load_json(os.path.join(a.dir, a.name + '.json'))
    wd = os.path.join(a.dir, 'wav')
    passes = sorted(int(f[len(a.name) + 2:-4]) for f in os.listdir(wd) if f.startswith(a.name + '_p') and f.endswith('.wav'))
    W = {p: stereo(os.path.join(wd, '%s_p%d.wav' % (a.name, p))) for p in passes}
    segs = J['segments']
    inst = [(p, si) for p in passes for si in range(len(segs))]
    n0 = segs[0]['notes']
    D = []          # per transition: matrix of pair nulls
    for i in range(1, len(n0)):
        span = (n0[i + 1]['on'] - n0[i]['on']) if i + 1 < len(n0) else 1.5
        L = int(round(span * SR))
        X = {}
        S = {}
        for p, si in inst:
            s0 = int(round(segs[si]['notes'][i]['on'] * SR))
            X[(p, si)], S[(p, si)] = W[p][s0:s0 + L].astype(np.float64), s0
        M = np.zeros((len(inst), len(inst)))
        for u, v in itertools.combinations(range(len(inst)), 2):
            x, y = X[inst[u]], X[inst[v]]
            lg = lag(x, y)
            if round(lg):
                s0 = S[inst[v]] - int(round(lg))
                y = W[inst[v][0]][s0:s0 + L].astype(np.float64)
            d = 10 * np.log10(((x - y) ** 2).sum() / (x ** 2).sum())
            M[u, v] = M[v, u] = d
        D.append(M)
    allp = np.concatenate([M[np.triu_indices(len(inst), 1)] for M in D])
    p10, p50, p90 = np.percentile(allp, [10, 50, 90])
    rng = np.random.default_rng(1)
    res = {}
    for N in range(2, len(inst) + 1):
        cov, dev = [], []
        for M in D:
            ref = np.percentile(M[np.triu_indices(len(inst), 1)], [10, 50, 90])
            for _ in range(a.trials if N < len(inst) else 1):
                s = rng.choice(len(inst), N, replace=False)
                v = M[np.ix_(s, s)][np.triu_indices(N, 1)]
                if a.holdout and len(inst) - N >= 6:     # tails from the other instances only (no shared pairs)
                    o = np.setdiff1d(np.arange(len(inst)), s)
                    ref = np.percentile(M[np.ix_(o, o)][np.triu_indices(len(o), 1)], [10, 50, 90])
                cov.append(v.min() <= ref[0] and v.max() >= ref[2])
                q = np.percentile(v, [10, 50, 90])
                dev.append(np.abs(q - ref))
        dev = np.median(np.array(dev), 0)
        res[N] = dict(covered=round(float(np.mean(cov)), 3), devP10=round(float(dev[0]), 2),
                      devP50=round(float(dev[1]), 2), devP90=round(float(dev[2]), 2))
    pick = next((N for N in sorted(res) if res[N]['covered'] >= a.share), None)
    print('%d instances (%d passes), %d transitions, %d pairs; SSO spread p10 %.1f / p50 %.1f / p90 %.1f dB'
          % (len(inst), len(passes), len(D), len(allp), p10, p50, p90))
    w = [np.percentile(M[np.triu_indices(len(inst), 1)], 90) - np.percentile(M[np.triu_indices(len(inst), 1)], 10)
         for M in D]
    print('per transition p10..p90 width: median %.1f dB [%.1f..%.1f]' % (np.median(w), min(w), max(w)))
    for N in sorted(res):
        print('N %2d: covered %.3f, |subset - SSO| p10 %.2f p50 %.2f p90 %.2f dB' % (N, res[N]['covered'], res[N]['devP10'],
                                                                                   res[N]['devP50'], res[N]['devP90']))
    print('N =', pick, '(smallest with >= %.0f %% of transitions covered)' % (100 * a.share))
    if a.o:
        with open(a.o, 'w') as f:
            json.dump(dict(spread=dict(p10=p10, p50=p50, p90=p90, pairs=len(allp)), byN=res, N=pick), f, indent=1)


if __name__ == '__main__':
    main()
