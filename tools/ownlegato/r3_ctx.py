#!/usr/bin/env python3
"""SSO across contexts: the recorded takes (r3.py's recordings, SSO itself, played after another note before held
another time) of each held-out test transition against SSO's test renders, aligned at the note-on, with r3_judge.py's
measures. SSO-vs-SSO pairs of the test share every event, so they only show SSO's randomness; this shows how far SSO
itself lands from the test when the history before the note differs, which no sample set can match. Ours (r3_judge.py
-o json) is then judged against this distribution too.

    r3_ctx.py <test.json> --rec <dir> --audio <dir>[,<dir>] --sso a.flac,... --judged <r3_judge out.json> [--files ...]
"""
import argparse
import json
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import SR, stereo  # noqa: E402
from r3_judge import pair, KEYS  # noqa: E402
from make_sfz3 import kframe, cc_ranges  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('json')
    ap.add_argument('--rec', required=True)
    ap.add_argument('--audio', required=True)
    ap.add_argument('--sso', required=True)
    ap.add_argument('--judged', required=True)
    ap.add_argument('--files')
    ap.add_argument('--max-takes', type=int, default=4)
    ap.add_argument('-o')
    a = ap.parse_args()
    J = json.load(open(a.json))
    S = [stereo(p) for p in a.sso.split(',')][:2]
    takes = {}
    for f in os.listdir(a.rec):
        if not f.endswith('.json') or (a.files and f[:-5] not in a.files.split(',')):
            continue
        R = json.load(open(os.path.join(a.rec, f)))
        if 'segments' not in R:
            continue
        for s in R['segments']:
            ns, roles = s['notes'], s.get('roles')
            for i in range(1, len(ns)):
                if (roles and roles[i] != 'b') or 'cc119' not in ns[i]:
                    continue
                takes.setdefault((ns[i - 1]['key'], ns[i]['key'], ns[i]['vel']), []).append((f[:-5], ns[i]['on'], ns[i]['cc119']))
    files = {}
    for d in a.audio.split(','):
        for f in os.listdir(d):
            if f.endswith('.flac'):
                files[f[:-5]] = os.path.join(d, f)
    judged = {(r['seg'], r['i']): r for r in json.load(open(a.judged))['rows']}
    rows = []
    for si, s in enumerate(J['segments']):
        ns = s['notes']
        for i in range(1, len(ns)):
            k = (ns[i - 1]['key'], ns[i]['key'], ns[i]['vel'])
            if k not in takes:
                continue
            cc = ns[i]['cc119']
            rng = cc_ranges(ns[i]['vel'], sorted({t[2] for t in takes[k]}))
            want = next(v for v, rr in rng.items() if any(lo <= cc <= hi for lo, hi in rr))
            t0 = ns[i]['on']
            L = int(round(((ns[i + 1]['on'] if i + 1 < len(ns) else t0 + 1.0) - t0) * SR))
            f = kframe(t0)
            ctx = []
            names = sorted(n for n in files if any(n == tk[0] or n.startswith(tk[0] + '_p') for tk in takes[k]))
            for name, on, c in [t for t in takes[k] if t[2] == want][:1]:
                for fn in [n for n in names if n == name or n.startswith(name + '_p')][:a.max_takes]:
                    g = kframe(on)
                    x, _ = sf.read(files[fn], start=g - 4, stop=g + L + 4, dtype='float32', always_2d=True)
                    X = np.zeros((g + L + 8, 2), dtype='float32')
                    X[g - 4:g - 4 + len(x)] = x
                    for Sk in S:
                        ctx.append(pair(Sk, f, L, X, g))
            if ctx and (si, i) in judged:
                rows.append(dict(ioi=s.get('ioi'), ctx=ctx, ours=judged[(si, i)]['ours']))
    print('%d test transitions with recorded takes' % len(rows))
    print('%-6s %-8s %22s %22s %14s' % ('ioi', 'measure', 'SSO other ctx p10/50/90', 'ours-SSO p10/50/90', 'ours >p95/p99'))
    out = {}
    for ioi in sorted({r['ioi'] for r in rows}) + ['all']:
        R = [r for r in rows if ioi == 'all' or r['ioi'] == ioi]
        for k in KEYS:
            c = np.array([e[k] for r in R for e in r['ctx'] if e.get(k) is not None])
            o = np.array([np.median([e[k] for e in r['ours'] if e.get(k) is not None]) for r in R
                          if any(e.get(k) is not None for e in r['ours'])])
            if not len(c) or not len(o):
                continue
            p95, p99 = np.percentile(c, [95, 99])
            q = lambda v: '%6.1f/%6.1f/%6.1f' % tuple(np.percentile(v, [10, 50, 90]))  # noqa: E731
            out['%s|%s' % (ioi, k)] = dict(ctx=list(np.percentile(c, [10, 50, 90])), ours=list(np.percentile(o, [10, 50, 90])),
                                           ours95=float(np.mean(o > p95)), ours99=float(np.mean(o > p99)), n=len(R))
            print('%-6s %-8s %22s %22s %6.0f%%/%4.0f%%' % (ioi, k, q(c), q(o), 100 * np.mean(o > p95), 100 * np.mean(o > p99)))
    if a.o:
        json.dump(out, open(a.o, 'w'))


if __name__ == '__main__':
    main()
