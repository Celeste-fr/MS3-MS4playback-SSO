#!/usr/bin/env python3
"""heard_midpoints.py <heard check dir> <out.json> [<pitch dump dir>]: the Performance transitions of a heard check's
export (tools/playbackverify/heard_check.sh with HC_KEEP, its dump/predicted.tsv): leave / mid / arrive (the f0 0.1 / 0.5
/ 0.9 of the way from the note before's written pitch to the note's, measure_patch.py's rule: a crossing counts when the
next 6 frames stay within 0.25 of it), ms against the written start, from the part mix's f0 cached as f0_<part>.npz
(arrays t: score time in s, m: f0 in MIDI semitones, NaN unvoiced: pYIN, fmin 60, fmax 900, 48 kHz, hop 128, as the
fast-microtones in-tune check writes it); plus the heard check's own leave reading where report-leave.json exists. Only
notes with no other note of the part sounding. <pitch dump dir>: a dump of the same score without [slurs]
microtoneBends 1 for the written pitches: with it, the trace (predicted.tsv) gives a respelled quarter tone the key it is
sent on with its written cents (a semitone off). Output: transition_midpoint.py predict --heard."""
import collections
import csv
import json
import os
import sys

import numpy as np

W, OUT = sys.argv[1], sys.argv[2]
# (the written pitches: a dump without [slurs] microtoneBends 1, whose trace gives a respelled note its sent key with its
# written cents; argv[3], the same score's notes in the same order)
pred = list(csv.DictReader(open(f'{sys.argv[3] if len(sys.argv) > 3 else W}/dump/predicted.tsv'), delimiter='\t'))
rep = {}
for n in (json.load(open(f'{W}/report-leave.json'))['notes'] if os.path.exists(f'{W}/report-leave.json') else []):
    rep[(n['part'], round(n['written'], 3))] = n
out = []
for part in ('Violins 1', 'Violas', 'Violoncellos', 'Contrabasses', 'Violins 2'):
    rows = sorted([r for r in pred if r['part'] == part], key=lambda r: (float(r['written']), int(r['pitch'])))
    f = f'{W}/f0_{part.replace(" ", "_")}.npz'
    z = np.load(f) if os.path.exists(f) else None
    for r in rows:
        if r['transition'] != '1':
            continue
        w = float(r['written']); we = float(r['writtenEnd'])
        x = int(r['pitch']) + float(r['cents']) / 100
        prev = [p for p in rows if abs(float(p["written"]) - float(r["fromWritten"])) < 1e-4]
        prev.sort(key=lambda p: (p["route"] != r["route"], abs(int(p["pitch"]) - int(r["fromPitch"]))))
        if not prev:
            continue
        p = prev[0]
        xp = int(p['pitch']) + float(p['cents']) / 100
        alone = not any(o is not r and o is not p and float(o['written']) < we - 1e-6 and float(o['writtenEnd']) > float(p['written']) + 1e-6 for o in rows)
        nxt = [o for o in rows if float(o['written']) > w + 1e-6]
        n1 = float(nxt[0]['written']) if nxt else we
        d = dict(part=part, bar=int(r['bar']), beat=float(r['beat']), written=w, pitch=x, fromPitch=xp, interval=round(x - xp, 2),
                 ioiBefore=round((w - float(p['written'])) * 1000, 1), ioiAfter=round((we - w) * 1000, 1),
                 before='attack' if p['transition'] == '0' else 'transition', sentMs=float(r['sentMs']), alone=alone)
        k = (part, round(w, 3))
        if k in rep:
            d['hcLeave'] = rep[k].get('leaveMs'); d['hcArrive'] = rep[k].get('arriveMs'); d['hcHow'] = rep[k].get('how')
        if z is not None and alone:
            t, m = z['t'], z['m']
            on = w + float(r['sentMs']) / 1000
            sel = np.where((t >= on - 0.01) & (t <= min(n1 + 0.15, on + 1.2)))[0]
            if len(sel) > 10:
                mm = m[sel]
                ok = ~np.isnan(mm)
                if ok.sum() > 10:
                    mm = np.interp(np.arange(len(mm)), np.where(ok)[0], mm[ok])
                    # (octave errors: fold to within 6 semitones of the pair)
                    c = (xp + x) / 2
                    mm = mm - 12 * np.round((mm - c) / 12)
                    phi = (mm - xp) / (x - xp)
                    for q, name in ((0.1, 'leave'), (0.5, 'mid'), (0.9, 'arrive')):
                        for j in range(len(sel)):
                            if phi[j] >= q and np.min(phi[j:j + 6]) >= q - 0.25:
                                d[name] = round((t[sel[j]] - w) * 1000, 1)
                                break
        out.append(d)
json.dump(out, open(OUT, 'w'), indent=0)
print(len(out), collections.Counter((d['part'], 'mid' in d) for d in out))
