#!/usr/bin/env python3
"""Chooses [legato] keepMs from renders of make_fastrun_scores.py's scores at several keepMs (numbers-measured,
2026-10-03): analyze_fastruns.py --json per family and value, files named "keep Fast <family> @k<ms>.json".

Rule (stated before the renders): the value whose median |arrival - written time| of the slurred transitions
(arriveMs; not a slur's first note), pooled over the 12 parts and 4 tempi, is the smallest, among the values with the
fewest swallowed transitions (no arrival found); within 1 ms of each other: the larger value (more of the note before
kept).

  choose_keep_ms.py <json> ...  [--out keep.json]"""
import json, os, re, statistics, sys

args = sys.argv[1:]
out = None
if '--out' in args:
    k = args.index('--out'); out = args[k + 1]; args = args[:k] + args[k + 2:]
by = {}
for f in args:
    m = re.search(r'@k(\d+)\.json$', f)
    k = int(m.group(1))
    d = by.setdefault(k, dict(errs=[], swallowed=0, firsts=[], n=0))
    for part, rows in json.load(open(f)).items():
        for r in rows:
            if 'arriveMs' not in r:
                continue
            if r.get('first'):
                if r['arriveMs'] is not None:
                    d['firsts'].append(abs(r['arriveMs']))
                continue
            d['n'] += 1
            if r['arriveMs'] is None:
                d['swallowed'] += 1
            else:
                d['errs'].append(abs(r['arriveMs']))
res = {}
for k in sorted(by):
    d = by[k]
    res[k] = dict(transitions=d['n'], swallowed=d['swallowed'], medianAbsMs=statistics.median(d['errs']),
                  p90AbsMs=sorted(d['errs'])[int(0.9 * (len(d['errs']) - 1))],
                  firstsMedianAbsMs=statistics.median(d['firsts']) if d['firsts'] else None)
    print(f"keepMs {k:4d}: {d['n']} transitions, {d['swallowed']} swallowed, median |arrival| {res[k]['medianAbsMs']:.1f} ms, "
          f"90 % {res[k]['p90AbsMs']:.0f} ms; slur firsts median {res[k]['firstsMedianAbsMs']}")
fewest = min(r['swallowed'] for r in res.values())
cand = {k: r for k, r in res.items() if r['swallowed'] == fewest}
best = min(r['medianAbsMs'] for r in cand.values())
chosen = max(k for k, r in cand.items() if r['medianAbsMs'] <= best + 1)
print(f'chosen keepMs {chosen} (fewest swallowed {fewest}; median |arrival| {cand[chosen]["medianAbsMs"]:.1f} ms, best {best:.1f})')
if out:
    json.dump(dict(chosen=chosen, values=res), open(out, 'w'), indent=1)
