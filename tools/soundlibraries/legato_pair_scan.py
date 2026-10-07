#!/usr/bin/env python3
"""The legato pair scan (2026-10-07, branch legato-pair-delays): when each legato transition of a Performance patch
reaches its new pitch, per start pitch and interval (not one value per interval: on Violas - Performance, 55 -> 54
arrives ~58 ms after its note-on, 58 -> 57 ~185 ms). Written for this check, not taken from anywhere.
Result (Violas - Performance, sso_legato_pairs.json, 2412 pairs, 26 not heard): the start pitch matters (+1 from 49:
163 ms, from 50: 68), and so does the context (slow - fast median +74 ms, chain - fast p10..p90 -75..+100), but the
table doesn't predict a real run: HANDOFF.md › No Performance patches. Not used by the map.

  legato_pair_scan.py events <out folder> <patch> [--grid tools/soundlibraries/sso_legato_grid_pitches.json]
      [--chunk 300]
      "<patch> <n>.txt" files for kthost (an offline VST 3 host: one Kontakt instance with the patch's MuseScore setup;
      LIVE.md › Measured with SSO), lines "seconds on|off|cc a b", at most --chunk pairs each, and manifest.json. Every
      start x in the grid's range and interval +-1 … +-12 with x + i in range, in three contexts:
        fast  : x (attack, a sixteenth at 110: 0.136 s) -> y (legato, 0.6 s)
        slow  : x (attack, 0.8 s) -> y
        chain : w (attack, 0.6 s) -> x (legato, 0.136 s) -> y; w = x - 2 (x + 2 at the range's bottom): x as in a run
      30 ms overlap (as MuseScore plays a slur), 0.8 s between pairs, velocity 64, CC1 80 (mf), CC11 127.
  legato_pair_scan.py analyse <manifest.json> <wav> ...  [--json out.json]
      per pair: arriveMs, when y's own harmonics pass x's for 20 ms after y's note-on (playbackverify/analyze_sweep.py's
      arrival, the one analyze_fastruns.py used); None: not within 0.6 s. Writes rows
      [context, x, interval, arriveMs] per patch (merged into out.json when it exists).
"""
import argparse
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))

OVER, GAP, VEL, CC1 = 0.03, 0.8, 64, 80
FAST = 60 / 110 / 4
INTERVALS = [i for k in range(1, 13) for i in (k, -k)]
CONTEXTS = (('fast', None, FAST), ('slow', None, 0.8), ('chain', 0.6, FAST))


def events(out, patch, grid_path, chunk):
    os.makedirs(out, exist_ok=True)
    grid = json.load(open(grid_path, encoding='utf-8'))
    lo, hi = list(grid[patch].values())[0]['range']
    pairs = [(ctx, w, lx, x, i) for ctx, w, lx in CONTEXTS for x in range(lo, hi + 1) for i in INTERVALS
             if lo <= x + i <= hi]
    manifest = dict(patch=patch, range=[lo, hi], files={})
    for n in range(0, len(pairs), chunk):
        ev = [(0.0, 'cc', 1, CC1), (0.0, 'cc', 11, 127)]
        notes = []
        t = 0.5
        for ctx, lw, lx, x, i in pairs[n:n + chunk]:
            y = x + i
            if lw:
                w = x - 2 if x - 2 >= lo else x + 2
                ev += [(t, 'on', w, VEL), (t + lw + OVER, 'off', w, 0)]
                t += lw
            ev += [(t, 'on', x, VEL), (t + lx + OVER, 'off', x, 0)]
            ev += [(t + lx, 'on', y, VEL), (t + lx + 0.6, 'off', y, 0)]
            notes.append(dict(context=ctx, x=x, interval=i, xOn=round(t, 4), yOn=round(t + lx, 4)))
            t += lx + 0.6 + GAP
        name = f'{patch} {n // chunk + 1}'
        ev.sort(key=lambda e: (e[0], e[1] != 'off'))
        with open(os.path.join(out, name + '.txt'), 'w', encoding='utf-8') as f:
            f.writelines(f'{e[0]:.4f} {e[1]} {e[2]} {e[3]}\n' for e in ev)
        manifest['files'][name] = notes
        print(f'{name}.txt: {len(notes)} pairs, {t:.0f} s')
    json.dump(manifest, open(os.path.join(out, 'manifest.json'), 'w', encoding='utf-8'), indent=1)


def analyse(manifest_path, wavs, out):
    import soundfile as sf
    from analyze_sweep import arrival, hz
    manifest = json.load(open(manifest_path, encoding='utf-8'))
    rows = []
    for wav in wavs:
        name = os.path.splitext(os.path.basename(wav))[0]
        x_, sr = sf.read(wav, always_2d=True)
        m = x_.mean(axis=1)
        for n in manifest['files'][name]:
            a = arrival(m, sr, n['yOn'], hz(n['x']), hz(n['x'] + n['interval']), n['xOn'] + 0.02, n['yOn'] + 0.6)
            rows.append([n['context'], n['x'], n['interval'], None if a is None else round(a * 1000)])
        print(f'{name}: {len(manifest["files"][name])} pairs')
    data = json.load(open(out, encoding='utf-8')) if os.path.exists(out) else {}
    data.setdefault(manifest['patch'], [])
    keep = {(r[0], r[1], r[2]) for r in rows}
    data[manifest['patch']] = sorted([r for r in data[manifest['patch']] if (r[0], r[1], r[2]) not in keep] + rows,
                                     key=lambda r: (r[0], r[1], r[2]))
    json.dump(data, open(out, 'w', encoding='utf-8'), separators=(',', ':'))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('out')
    e.add_argument('patch')
    e.add_argument('--grid', default=os.path.join(HERE, 'sso_legato_grid_pitches.json'))
    e.add_argument('--chunk', type=int, default=300)
    a = sub.add_parser('analyse')
    a.add_argument('manifest')
    a.add_argument('wavs', nargs='+')
    a.add_argument('--json', default=os.path.join(HERE, 'sso_legato_pairs.json'))
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.out, o.patch, o.grid, o.chunk)
    else:
        analyse(o.manifest, o.wavs, o.json)


if __name__ == '__main__':
    main()
