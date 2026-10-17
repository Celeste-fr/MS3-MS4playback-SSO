#!/usr/bin/env python3
"""Bad joins: the share of a render's hand-overs worse than SSO's own 95th / 99th percentile (SSO's two renders of the
held-out test set pooled; dipDb: below its 5th / 1st), per measure, and the share worse in any of the spectral / level
ones and the pitch's (fluxZ, dipDb, pumpDb, hfDb, outC). SSO's own renders score ~5 % / ~1 % per measure by construction.

    tails.py <sso te json>[,<sso te json>] <te json> [<te json> ...]
"""
import json
import sys

import numpy as np

K = ['fluxZ', 'dipDb', 'pumpDb', 'hfDb', 'roughC', 'jumpC', 'gapMs', 'outC']
ANY = ['fluxZ', 'dipDb', 'pumpDb', 'hfDb', 'outC']


def rows(p):
    return json.load(open(p))['rows']


def main():
    sso = sum((rows(p) for p in sys.argv[1].split(',')), [])
    lim = {}
    for k in K:
        v = np.array([r[k] for r in sso if r.get(k) is not None])
        lim[k] = (np.percentile(v, 5), np.percentile(v, 1)) if k == 'dipDb' else (np.percentile(v, 95), np.percentile(v, 99))
    out = {}
    print('%-28s' % 'render' + ''.join('%14s' % k for k in K) + '   any5 >p95 / >p99')
    for p in sys.argv[1].split(',') + sys.argv[2:]:
        R = rows(p)
        res = {}
        for k in K:
            v = [r[k] for r in R if r.get(k) is not None]
            bad = lambda x, j: x < lim[k][j] if k == 'dipDb' else x > lim[k][j]  # noqa: E731
            res[k] = (100 * np.mean([bad(x, 0) for x in v]), 100 * np.mean([bad(x, 1) for x in v]))
        any95 = 100 * np.mean([any((r.get(k) is not None and ((r[k] < lim[k][0]) if k == 'dipDb' else (r[k] > lim[k][0])))
                                   for k in ANY) for r in R])
        any99 = 100 * np.mean([any((r.get(k) is not None and ((r[k] < lim[k][1]) if k == 'dipDb' else (r[k] > lim[k][1])))
                                   for k in ANY) for r in R])
        out[p] = dict(res=res, any95=any95, any99=any99)
        print('%-28s' % p.split('/')[-1] + ''.join('%14s' % ('%.0f%%/%.1f%%' % res[k]) for k in K)
              + '   %.0f%% / %.1f%%' % (any95, any99))
    return out


if __name__ == '__main__':
    main()
