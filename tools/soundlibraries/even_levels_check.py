#!/usr/bin/env python3
"""Checks MuseScore's own playback of the even-levels corrections (libmscore/evenlevels.h) on a render made elsewhere:
each measured note's level in a WAV, as the measurement and even_levels_test.py measure it (the perceived envelope,
tools/playbackverify/loudness.py, mean over 136.4 ms from the note's window start), per slur the range (max - min),
the passage's first and last note left out. Written for the Windows VM check of 2026-10-13 (branch even-levels).

  even_levels_check.py <result.json> <wav> [<wav> ...] [--bars 2 4] [--ticks-per-bar 1920] [--offset 0]

result.json: the --even-levels job's (its notes' played note-on "on", "windowMs", "slur", "tick"); each wav a render
of the same part starting at score time 0 (--verify-playback --verify-wav "<score> <part> library.wav", or the job's
own "<part> round <r>.wav" with --offset its pass start). Prints per wav the slur ranges and their median, all slurs and
those in the bars given (by the slur's first note).
"""
import argparse
import json
import os
import statistics as st
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
from loudness import perceived_envelope  # noqa: E402

WIN = 0.1364


def levels(env, t_env, notes, offset):
    out = []
    for n in notes:
        a = offset + n['on'] + n['windowMs'] / 1000.0
        m = (t_env >= a) & (t_env < a + WIN)
        out.append(float(np.mean(env[m])) if m.any() else float('nan'))
    return out


def slur_ranges(notes, lv, tpb, bars):
    by = {}
    for k, n in enumerate(notes):
        if 0 < k < len(notes) - 1:
            by.setdefault(n['slur'], []).append(k)
    rows = []
    for s, ks in sorted(by.items()):
        if len(ks) < 2:
            continue
        bar = notes[ks[0]]['tick'] // tpb + 1
        rows.append((bar, max(lv[k] for k in ks) - min(lv[k] for k in ks)))
    all_ = [r for _, r in rows]
    sel = [r for b, r in rows if not bars or b in bars]
    return rows, all_, sel


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('result')
    ap.add_argument('wavs', nargs='+')
    ap.add_argument('--bars', nargs='*', type=int, default=[])
    ap.add_argument('--ticks-per-bar', type=int, default=1920)
    ap.add_argument('--offset', type=float, default=0.0)
    ap.add_argument('--part', default=None, help='only the passages of this part')
    o = ap.parse_args()
    import soundfile as sf
    res = json.load(open(o.result))
    for wav in o.wavs:
        x, sr = sf.read(wav, always_2d=True)
        x = x[:, 0] if x.shape[1] == 1 else x
        env, first, hop = perceived_envelope(x, sr)
        t_env = (first + np.arange(len(env)) * hop) / 1000.0
        print(f'== {os.path.basename(wav)}')
        for p in res['passages']:
            if o.part and p['part'] != o.part:
                continue
            notes = p['notes']
            lv = levels(env, t_env, notes, o.offset)
            rows, all_, sel = slur_ranges(notes, lv, o.ticks_per_bar, set(o.bars))
            print(f"  {p['part']} bars {p['bars']} ({len(notes)} notes, kept round {p['chosen']}): slur range median "
                  f"{st.median(all_):.2f} dB (all {len(all_)} slurs)"
                  + (f", bars {' '.join(map(str, o.bars))}: median {st.median(sel):.2f}, "
                     f"{' '.join(f'{r:.2f}' for r in sel)}" if o.bars and sel else ''))


if __name__ == '__main__':
    main()
