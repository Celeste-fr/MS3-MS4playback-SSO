#!/usr/bin/env python3
"""Listening clips: route renders mixed at their gain stage (dumpEvents' gains.txt: the patch's slot gain, per route), cut to
a stretch of the score, as mp3 (ffmpeg, 192 kb/s).

    mix.py --dump <dump> --gains <gains.txt> --out clip.mp3 --from S --to S --route R=wav[,R=wav...] [--norm dB]
           [--trim dB]

--from / --to: score seconds (render time = score time + the start lead + heard_check.OFFSET_S); --route ROUTE=path takes
the route's gain from gains.txt by its part / patch / lane in routemap.txt (our route: give ROUTE@<patch>|<lane>=path to
use another route's gain); --norm: a fixed gain for every clip of a page (dB), so clips compare in level.
"""
import argparse
import os
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import heard_check as hc  # noqa: E402


def gains(path):
    g = {}
    lines = open(path).read().splitlines()
    for ln in lines[1:]:
        p = ln.split('|')
        if len(p) >= 5:
            g[(p[0], p[1], int(p[2]))] = float(p[4])
    return g


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dump', required=True)
    ap.add_argument('--gains', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--from', dest='t0', type=float, required=True)
    ap.add_argument('--to', dest='t1', type=float, required=True)
    ap.add_argument('--route', action='append', default=[])
    ap.add_argument('--norm', type=float, default=0.0)
    a = ap.parse_args()
    G = gains(a.gains)
    rm = hc.read_routemap(a.dump)
    lead = hc.read_lead_ms(a.dump) / 1000
    s0 = int((a.t0 + lead + hc.OFFSET_S) * 44100)
    s1 = int((a.t1 + lead + hc.OFFSET_S) * 44100)
    mixd = np.zeros((s1 - s0, 2))
    for spec in a.route:
        for item in spec.split(','):
            key, path = item.split('=', 1)
            if '@' in key:
                _, pl = key.split('@')
                patch, lane = pl.split('|')
                part = next(v['part'] for v in rm.values() if v['patch'] == patch)
                g = G[(part, patch, int(lane))]
            else:
                info = rm[key]
                g = G[(info['part'], info['patch'], info['lane'])]
            x, sr = sf.read(path, dtype='float64', always_2d=True)
            seg = x[s0:s1]
            mixd[:len(seg)] += seg * 10 ** (g / 20)
    mixd *= 10 ** (a.norm / 20)
    n = int(0.02 * 44100)
    mixd[:n] *= np.linspace(0, 1, n)[:, None]
    n = int(0.3 * 44100)
    mixd[-n:] *= np.linspace(1, 0, n)[:, None]
    pk = np.abs(mixd).max()
    with tempfile.TemporaryDirectory() as d:
        w = os.path.join(d, 'm.wav')
        sf.write(w, mixd, 44100, subtype='FLOAT')
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', w, '-b:a', '192k', a.out], check=True)
    print('%s: peak %.1f dBFS' % (a.out, 20 * np.log10(pk + 1e-12)))


if __name__ == '__main__':
    main()
