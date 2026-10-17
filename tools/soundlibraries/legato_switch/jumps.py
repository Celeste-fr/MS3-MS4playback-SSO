#!/usr/bin/env python3
"""jumps.py <rows.json> [kind] [field]: per section and interval (and key), the probe's arrival (or leave / level) against dt,
printed as a strip, and every step between neighbouring dt where it jumps by more than 25 ms (level: 1.5 dB)."""
import json, sys, collections, statistics as st

rows = json.load(open(sys.argv[1]))
kind = sys.argv[2] if len(sys.argv) > 2 else 'pair'
field = sys.argv[3] if len(sys.argv) > 3 else 'arrive'
thr = 1.5 if field == 'level' else 25
by = collections.defaultdict(lambda: collections.defaultdict(list))
for r in rows:
    if r['kind'] != kind or r.get(field) is None:
        continue
    by[(r['section'], r['iv'], r['key'])][r['dt']].append(r[field])
for k in sorted(by):
    d = by[k]
    xs = sorted(d)
    ys = [st.median(d[x]) for x in xs]
    print('==', k, 'n', sum(len(v) for v in d.values()))
    print('   ', ' '.join(f'{x}:{y:.0f}' for x, y in zip(xs, ys)))
    for i in range(1, len(xs)):
        if abs(ys[i] - ys[i - 1]) > thr:
            print(f'    jump {xs[i-1]} -> {xs[i]}: {ys[i-1]:.1f} -> {ys[i]:.1f}')
