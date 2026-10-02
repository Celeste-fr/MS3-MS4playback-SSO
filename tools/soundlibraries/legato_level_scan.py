#!/usr/bin/env python3
"""The legato level scan (2026-10-02, branch legato-level-balance): how loud each legato transition of a sound library's
Performance patches arrives, against the pitch's own attack and against the pitch's other transitions. Written for
this check, not taken from anywhere.

  legato_level_scan.py events <out folder> [--grid tools/soundlibraries/sso_legato_grid_pitches.json]
      per patch with a legato range (the grid's "range") and dynamic (mf all; p / f for PF): "<dyn> <patch>.txt", lines
      "seconds on|off|cc a b" for kthost (an offline VST 3 host: one Kontakt instance with the patch's MuseScore setup,
      offline rendering; LIVE.md › Measured with SSO), and manifest.json:
        attacks : every pitch of the range once, 0.9 s, 1.0 s apart
        slow    : every start x interval +-1 2 3 5 7 12 in range: x (attack, 0.8 s) -> y (legato, 0.9 s, 30 ms overlap)
        fast    : the same after a sixteenth at 110 (x 0.136 s) -> y (0.5 s)
      velocity 65, CC1 80 (mf) / 48 / 95, CC11 127.
  legato_level_scan.py analyse <manifest.json> <map.xml> <wav> ...
      per note (perceived loudness, playbackverify/loudness.py; a gain of g dB moves it by g): "body" the median 0.45-0.85
      s after the note-on (slow, attacks 0.40-0.85), "short" the loudest from 30 ms after where the renderer puts the
      written time to a sixteenth later (attacks: the note-on plus the map's onset; transitions: plus the measured
      delay times [legato] fastShare / fastFullMs); "dbody" / "dshort": minus the attack of the same pitch. Writes
      <wav>.json. legato_levels_from_scan.py reads them.

Measured with SSO through Kontakt 8 on the Windows VM (43 Performance patches, mf; 4 at p / f). Transitions repeat
exactly (no round robins); in this isolated setting they differ by 2-6 dB per start and interval. In a run the
same transition's level moves 1.4-4.9 dB with the notes around it (Violas, 2026-10-02), so these tables don't
predict what a run sounds like (legato_levels_from_scan.py's header)."""
import json
import os
import statistics as st
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
INTERVALS = [1, -1, 2, -2, 3, -3, 5, -5, 7, -7, 12, -12]
OVER, GAP, VEL = 0.03, 1.0, 65
FAST = 60 / 110 / 4
DYN = {'mf': 80, 'p': 48, 'f': 95}
PF = ['Violas - Performance', 'Celli - Performance', 'Flute Solo - Total Performance', 'Horn Solo - Performance']


def events(out, grid_path):
    os.makedirs(out, exist_ok=True)
    grid = json.load(open(grid_path, encoding='utf-8'))
    manifest = {}
    for patch, sounds in grid.items():
        lo, hi = list(sounds.values())[0]['range']
        for tag, cc1 in DYN.items():
            if tag != 'mf' and patch not in PF:
                continue
            ev = [(0.0, 'cc', 1, cc1), (0.0, 'cc', 11, 127)]
            notes = []
            t = 0.5
            for p in range(lo, hi + 1):
                ev += [(t, 'on', p, VEL), (t + 0.9, 'off', p, 0)]
                notes.append(dict(kind='attack', pitch=p, on=t, off=t + 0.9))
                t += 0.9 + GAP
            for ctx, lx, ly in (('slow', 0.8, 0.9), ('fast', FAST, 0.5)):
                for x in range(lo, hi + 1):
                    for i in INTERVALS:
                        y = x + i
                        if not lo <= y <= hi:
                            continue
                        ev += [(t, 'on', x, VEL), (t + lx + OVER, 'off', x, 0), (t + lx, 'on', y, VEL), (t + lx + ly, 'off', y, 0)]
                        notes.append(dict(kind=ctx, pitch=y, start=x, interval=i, on=t + lx, off=t + lx + ly, before=lx))
                        t += lx + ly + GAP
            ev.sort(key=lambda e: (e[0], 0 if e[1] == 'cc' else (1 if e[1] == 'off' else 2)))
            name = f'{tag} {patch}'
            with open(os.path.join(out, name + '.txt'), 'w', newline='\r\n') as f:
                for e in ev:
                    f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')
            manifest[name] = dict(patch=patch, dyn=tag, cc1=cc1, range=[lo, hi], length=t, notes=notes)
    json.dump(manifest, open(os.path.join(out, 'manifest.json'), 'w'), indent=0)
    print(len(manifest), 'files', round(sum(m['length'] for m in manifest.values()) / 3600, 2), 'h of audio')


def table(s):
    out = {}
    for tok in (s or '').split():
        if ':' not in tok:
            return {60: float(tok)}         # one value for every pitch
        k, v = tok.split(':')
        out[int(k)] = float(v)
    return out


def patch_info(root, name):
    for ins in root.iter('Instrument'):
        if ins.get('name') == name:
            for a in ins.iter('Articulation'):
                if a.get('legatoDelay'):
                    return dict(delay=table(a.get('legatoDelay')), up=table(a.get('octaveUp')),
                                down=table(a.get('octaveDown')), onset=table(a.get('onset')))
    return dict(delay={}, up={}, down={}, onset={})


def onset_at(info, p):
    t = info['onset']
    if not t:
        return 60.0
    ks = sorted(t)
    if p <= ks[0]:
        return t[ks[0]]
    if p >= ks[-1]:
        return t[ks[-1]]
    for a, b in zip(ks, ks[1:]):
        if a <= p <= b:
            return t[a] + (t[b] - t[a]) * (p - a) / (b - a)


def delay_at(info, start, interval):
    def nearest(t):
        return t[min(t, key=lambda q: (abs(q - start), q))]
    if interval == 12 and info['up']:
        return nearest(info['up'])
    if interval == -12 and info['down']:
        return nearest(info['down'])
    return info['delay'].get(interval, 200.0)


def analyse(meta, wav, root):
    import numpy as np
    import soundfile as sf
    sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
    from loudness import perceived_envelope
    x, sr = sf.read(wav, always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0

    def win(a, b, f):
        i0, i1 = np.searchsorted(t_env, a), np.searchsorted(t_env, b)
        return float(f(env[i0:i1])) if i1 > i0 else None

    info = patch_info(root, meta['patch'])
    rows = []
    for n in meta['notes']:
        on = n['on']
        r = dict(n)
        if n['kind'] == 'attack':
            w = on + onset_at(info, n['pitch']) / 1000.0
            r['body'] = win(on + 0.40, on + 0.85, np.median)
            r['short'] = win(w + 0.03, w + 0.136, np.max)
        else:
            d = delay_at(info, n['start'], n['interval']) / 1000.0
            r['body'] = win(on + 0.45, on + 0.85, np.median) if n['kind'] == 'slow' else None
            w = on + d * min(1.0, 0.65 + 0.35 * n['before'] / 0.8)
            r['short'] = win(w + 0.03, w + 0.136, np.max)
        rows.append(r)
    att = {r['pitch']: r for r in rows if r['kind'] == 'attack'}
    for r in rows:
        a = att.get(r['pitch'])
        if r['kind'] != 'attack' and a:
            if r.get('body') is not None and a['body'] is not None:
                r['dbody'] = round(r['body'] - a['body'], 2)
            if r.get('short') is not None and a['short'] is not None:
                r['dshort'] = round(r['short'] - a['short'], 2)
    return rows


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'events':
        grid = sys.argv[sys.argv.index('--grid') + 1] if '--grid' in sys.argv else \
            os.path.join(HERE, 'sso_legato_grid_pitches.json')
        events(sys.argv[2], grid)
    elif len(sys.argv) >= 5 and sys.argv[1] == 'analyse':
        manifest = json.load(open(sys.argv[2]))
        root = ET.parse(sys.argv[3]).getroot()
        for wav in sys.argv[4:]:
            name = os.path.basename(wav)[:-4]
            rows = analyse(manifest[name], wav, root)
            json.dump(rows, open(wav + '.json', 'w'))
            def q(kind, k):
                v = sorted(r[k] for r in rows if r['kind'] == kind and r.get(k) is not None)
                return f"{st.median(v):+.1f}" if v else '-'
            print(f"{name}: against the attack, median dB: slow body {q('slow', 'dbody')}, fast short {q('fast', 'dshort')}")
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == '__main__':
    main()
