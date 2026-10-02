#!/usr/bin/env python3
"""Reads renders of make_levelrun_scores.py's scores (one part each, its Performance patch's events: tst_liveequivalence
dumpEvents replayed through kthost, or a MuseScore --verify-playback library.wav):

    analyze_levelruns.py <folder of "Level <part>.notes.json"> <"<set> <part>.wav"> ...

Every note's heard level (perceived loudness, loudness.py): the loudest from 30 ms after its written time to the next
note (at most 0.4 s); notes of a quarter or longer also settled (the median from 0.3 s to their end). Per section
(sixteenths, eighths, quarters, halves): the mean level by slur position and their spread (max - min); the slurred
notes against their slur's first note (median, and the median of its size); "even": the spread (standard deviation)
of the slurred notes' levels around the median of the slurred notes within 2 semitones, what a per-note level
correction has to shrink. Writes <wav>.json."""
import json
import os
import statistics as st
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from loudness import perceived_envelope


def analyse(notes_dir, wav):
    base = os.path.basename(wav)[:-4]
    _, part = base.split(' ', 1)
    meta = json.load(open(os.path.join(notes_dir, f'Level {part}.notes.json')))
    x, sr = sf.read(wav, always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0

    def win(a, b, f):
        i0, i1 = np.searchsorted(t_env, a), np.searchsorted(t_env, b)
        return float(f(env[i0:i1])) if i1 > i0 else None
    rows = []
    for n in meta['notes']:
        t, d = n['time'], n['seconds']
        r = dict(n)
        r['heard'] = win(t + 0.03, t + min(d, 0.4), np.max)
        r['settled'] = win(t + 0.3, t + d - 0.05, np.median) if d >= 0.5 else None
        rows.append(r)
    json.dump(rows, open(wav + '.json', 'w'))
    out = [f'{base}:']
    for sec in sorted({r['section'] for r in rows}):
        rs = [r for r in rows if r['section'] == sec and r['heard'] is not None]
        key = 'settled' if sec >= 4 else 'heard'
        size = max(r['pos'] for r in rs) + 1
        pos = [st.mean([r['heard'] for r in rs if r['pos'] == p]) for p in range(size)]
        firsts = {r['group']: r['heard'] for r in rs if r['first']}
        dev = [r['heard'] - firsts[r['group']] for r in rs if not r['first'] and r['group'] in firsts]
        leg = [r for r in rs if not r['first'] and r.get(key) is not None]
        resid = []
        for r in leg:
            near = [q[key] for q in leg if q is not r and abs(q['pitch'] - r['pitch']) <= 2]
            if len(near) >= 2:
                resid.append(r[key] - st.median(near))
        out.append(f"  {sec:>2}/16: positions {' '.join(f'{p:.1f}' for p in pos)} spread {max(pos) - min(pos):.1f}; "
                   f"slurred - first {st.median(dev):+.1f} (size {st.median([abs(v) for v in dev]):.1f}); "
                   f"even ({key}) {np.std(resid):.2f}")
    return '\n'.join(out)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    for w in sys.argv[2:]:
        print(analyse(sys.argv[1], w))
