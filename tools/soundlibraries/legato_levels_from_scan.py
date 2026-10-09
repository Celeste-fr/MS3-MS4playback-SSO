#!/usr/bin/env python3
"""The level each legato transition arrives at, from the legato level scan (legato_level_scan.py: kthost renders of every
start pitch x interval +-1 2 3 5 7 12 through each Performance patch, 2026-10-02, branch legato-level-balance).

    legato_levels_from_scan.py <folder of "<dyn> <patch>.wav.json"> [-o tools/soundlibraries/sso_legato_levels.json]

Per patch (mf), per context: "run" (the note reached after a sixteenth at 110, heard from its arrival for a sixteenth:
the loudest perceived loudness) and "settled" (after a 0.8 s note, the median 0.45-0.85 s after the note-on), each
transition's level minus the reference of its target pitch: the median of every transition into that pitch (into
it and its neighbours when fewer than 3). So a value says how much louder (+) or softer (-) than the pitch's other
transitions this one arrives: SSO records each start x interval on its own, and they differ by 2-6 dB. Writes per patch
{"run" | "settled": {interval: [first start pitch, [dB or null per start pitch]]}, "attack": {"run" | "settled": the
patch's attacks minus the reference, median dB}} and prints a summary.

Measured 2026-10-02 (43 patches): per patch a median 0.1-1.8 dB off, 0-31 % over 3 dB; the same at p and f. In a
run, though, the notes around a transition move its level by as much (1.4-4.9 dB, Violas), so the legato level
balance that played these stayed off and went 2026-10-07 (the measurement is kept: HANDOFF.md › Legato level balance)."""
import argparse
import glob
import json
import os
import statistics


def table(rows, key):
    vals = {(r['start'], r['interval']): r[key] for r in rows if r.get(key) is not None}
    by_target = {}
    for (s, i), v in vals.items():
        by_target.setdefault(s + i, []).append(v)
    ref = {}
    for t in by_target:
        pool = list(by_target[t])
        if len(pool) < 3:
            pool += by_target.get(t - 1, []) + by_target.get(t + 1, [])
        ref[t] = statistics.median(pool)
    out = {}
    for i in sorted({i for _, i in vals}):
        starts = sorted(s for s, j in vals if j == i)
        lo, hi = starts[0], starts[-1]
        out[i] = [lo, [round(vals[(s, i)] - ref[s + i], 1) if (s, i) in vals else None for s in range(lo, hi + 1)]]
    return out, ref


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('folder')
    ap.add_argument('-o', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_legato_levels.json'))
    a = ap.parse_args()
    result = {}
    for f in sorted(glob.glob(os.path.join(a.folder, 'mf *.wav.json'))):
        patch = os.path.basename(f)[3:-9]
        rows = json.load(open(f))
        fast = [r for r in rows if r['kind'] == 'fast']
        slow = [r for r in rows if r['kind'] == 'slow']
        att = {r['pitch']: r for r in rows if r['kind'] == 'attack'}
        run, ref_run = table(fast, 'short')
        settled, ref_settled = table(slow, 'body')
        gap = {}
        for name, ref, key in (('run', ref_run, 'short'), ('settled', ref_settled, 'body')):
            d = [att[p][key] - ref[p] for p in ref if p in att and att[p].get(key) is not None]
            gap[name] = round(statistics.median(d), 1) if d else None
        result[patch] = {'run': run, 'settled': settled, 'attack': gap}
        allv = [v for t in (run, settled) for _, vs in t.values() for v in vs if v is not None]
        big = sum(1 for v in allv if abs(v) > 3) / max(1, len(allv))
        print(f"{patch:45s} |dev| median {statistics.median(abs(v) for v in allv):.1f} dB, over 3 dB {100 * big:.0f} %, "
              f"attacks vs reference run {gap['run']:+.1f} settled {gap['settled']:+.1f}")
    with open(a.o, 'w', encoding='utf-8') as fh:
        fh.write('{\n' + ',\n'.join(f'"{p}": ' + json.dumps(v, separators=(',', ':')) for p, v in result.items()) + '\n}\n')
    print(len(result), 'patches ->', a.o)


if __name__ == '__main__':
    main()
