#!/usr/bin/env python3
"""Fits [legato] fastShare / fastFullMs (MidiRenderer::libFastDelay: after a note L long a legato transition's delay is
the map's legatoDelay times min(1, share + (1 - share) * L / full)) to renders of make_fastrun_scores.py's scores with
legato transitions on time (every note-on at its written time, so a transition's heard
arrival after its written time is SSO's own delay). Written for numbers-measured (2026-10-03); the fit behind the
earlier 65 % / 800 ms (2026-10-02) left no script or data, so these replace them.

  fit_fast_share.py <map.xml> <analyze_fastruns.py --json output> ... [--out fit.json]

Data: every slurred transition (not a slur's first note, interval not an octave) whose arrival was measured
(analyze_fastruns arriveMs); L = the note before's written length; the patch: the part's Performance patch in the map.
Error measure (stated before fitting): for each part and tempo, the median measured arrival against the median
predicted delay; the fit minimises the sum of the squared differences over all part x tempo groups; grid: share
0.30 ... 1.00 in steps of 0.01, full 100 ... 1200 ms in steps of 10 (1.2 s: the legato grid's own first notes, whose
delays are the map's, i.e. share 1). Prints the best, the groups' errors and how flat the minimum is."""
import json, re, statistics, sys
import xml.etree.ElementTree as ET

PARTS = {   # part (make_fastrun_scores FAMILIES) -> its Performance patch in the SSO map
    "Violins 1": "Violins 1 - Performance", "Violas": "Violas - Performance", "Celli": "Celli - Performance",
    "Basses": "Basses - Performance", "Flute": "Flute Solo - Total Performance", "Oboe": "Oboe Solo - Performance",
    "Clarinet": "Clarinet Solo - Performance", "Bassoon": "Bassoon Solo - Performance", "Horn": "Horn Solo - Performance",
    "Trumpet": "Trumpet Solo - Total Performance", "Trombone": "Tenor Trombone Solo - Total Performance",
    "Tuba": "Tuba Solo - Performance"}


def delays(map_path):
    out = {}
    for inst in ET.parse(map_path).getroot().iter('Instrument'):
        for a in inst.iter('Articulation'):
            t = a.get('legatoDelay')
            if t:
                table = sorted((int(k), float(v)) for k, v in (x.split(':') for x in t.split()))
                out[inst.get('name')] = table
    return out


def keyed(table, key):                  # SoundLib keyedMsAt
    if key <= table[0][0]:
        return table[0][1]
    if key >= table[-1][0]:
        return table[-1][1]
    for (ka, va), (kb, vb) in zip(table, table[1:]):
        if key <= kb:
            return vb if kb == ka else va + (vb - va) * (key - ka) / (kb - ka)
    return table[-1][1]


def main():
    args = sys.argv[1:]
    out = None
    if '--out' in args:
        k = args.index('--out'); out = args[k + 1]; args = args[:k] + args[k + 2:]
    D = delays(args[0])
    rows = []
    for f in args[1:]:
        for part, notes in json.load(open(f)).items():
            patch = PARTS.get(part)
            if not patch or patch not in D:
                continue
            for i, n in enumerate(notes):
                if n.get('first') or i == 0 or n.get('arriveMs') is None:
                    continue
                prev = notes[i - 1]
                iv = n['pitch'] - prev['pitch']
                if iv == 0 or abs(iv) == 12:
                    continue
                rows.append(dict(part=part, tempo=n['tempo'], L=prev['seconds'], grid=keyed(D[patch], iv),
                                 arrive=n['arriveMs']))
    groups = {}
    for r in rows:
        groups.setdefault((r['part'], r['tempo']), []).append(r)
    meas = {g: statistics.median(r['arrive'] for r in rs) for g, rs in groups.items()}

    def err(share, full):
        e = 0.0
        for g, rs in groups.items():
            p = statistics.median(r['grid'] * min(1.0, share + (1 - share) * r['L'] * 1000 / full) for r in rs)
            e += (p - meas[g]) ** 2
        return e

    grid = [(err(s / 100, f), s, f) for s in range(30, 101) for f in range(100, 1201, 10)]
    grid.sort()
    best = grid[0]
    s, f = best[1] / 100, best[2]
    print(f'{len(rows)} transitions in {len(groups)} part x tempo groups; best share {best[1]} %, full {best[2]} ms, '
          f'rms group error {(best[0] / len(groups)) ** 0.5:.1f} ms')
    within = [x for x in grid if x[0] <= best[0] * 1.05]
    print(f'within 5 % of the best squared error: share {min(x[1] for x in within)}-{max(x[1] for x in within)} %, '
          f'full {min(x[2] for x in within)}-{max(x[2] for x in within)} ms ({len(within)} grid points)')
    print('old 65 %% / 800 ms: rms group error %.1f ms' % ((err(0.65, 800) / len(groups)) ** 0.5))
    res = []
    for g, rs in sorted(groups.items()):
        p = statistics.median(r['grid'] * min(1.0, s + (1 - s) * r['L'] * 1000 / f) for r in rs)
        res.append(dict(part=g[0], tempo=g[1], n=len(rs), measured=meas[g], predicted=round(p, 1)))
        print(f'  {g[0]:10s} {g[1]:3d} bpm  n={len(rs):3d}  measured {meas[g]:6.1f}  predicted {p:6.1f}  '
              f'grid median {statistics.median(r["grid"] for r in rs):6.1f}')
    if out:
        json.dump(dict(share=best[1], fullMs=best[2], groups=res), open(out, 'w'), indent=1)


if __name__ == '__main__':
    main()
