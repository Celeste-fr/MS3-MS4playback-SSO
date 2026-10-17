#!/usr/bin/env python3
"""Does each note reach its written pitch, quarter tones included? For a part's notes in a compare.py result (the heard
check's notes: written time, pitch, cents, heard time, route): pYIN (2-cent resolution) over the note's settled part, from
its heard arrival (else written time) + 30 ms to the next note's written time - 10 ms (at least 25 ms), the median f0
against pitch + cents, in cents.

    tuning_check.py <cmp.json> <wav dir> [--route-map ol=ol] [-o out.json]
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import heard_check as hc  # noqa: E402
from common import SR, mono, f0  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmp')
    ap.add_argument('wavs')
    ap.add_argument('--lead-ms', type=float, default=172.2)
    ap.add_argument('-o')
    a = ap.parse_args()
    d = json.load(open(a.cmp))
    notes = sorted(d['notes'], key=lambda n: n['written'])
    W, out = {}, []
    for k, n in enumerate(notes):
        if 'Performance' not in n['patch'] and n['route'] != 'ol':
            continue
        if n['route'] not in W:
            W[n['route']] = mono(os.path.join(a.wavs, n['route'] + '.wav')).astype(np.float64)
        x = W[n['route']]
        base = n['written'] + hc.OFFSET_S + a.lead_ms / 1000
        t0 = base + (n['heardMs'] or 0) / 1000 + 0.03
        nxt = notes[k + 1]['written'] + hc.OFFSET_S + a.lead_ms / 1000 if k + 1 < len(notes) else base + 0.3
        t1 = max(nxt - 0.01, t0 + 0.025)
        target = n['pitch'] + n['cents'] / 100
        _, m = f0(x[int((t0 - 0.03) * SR):int((t1 + 0.03) * SR)], int(target) - 3, int(target) + 4, resolution=0.02)
        m = m[int(0.03 * SR / 128):len(m) - int(0.03 * SR / 128)]
        m = m[np.isfinite(m)]
        err = None if len(m) < 3 else round(float(np.median(m) - target) * 100, 1)
        out.append(dict(bar=n['bar'], beat=n['beat'], pitch=n['pitch'], cents=n['cents'], route=n['route'],
                        transition=n['transition'], centsError=err, spanMs=round((t1 - t0) * 1000)))
    e = np.array([abs(r['centsError']) for r in out if r['centsError'] is not None])
    qt = np.array([abs(r['centsError']) for r in out if r['centsError'] is not None and r['cents']])
    summ = dict(n=len(e), median=round(float(np.median(e)), 1), p90=round(float(np.percentile(e, 90)), 1),
                max=round(float(e.max()), 1), quarterTones=len(qt),
                qtMedian=round(float(np.median(qt)), 1) if len(qt) else None, qtMax=round(float(qt.max()), 1) if len(qt) else None,
                over10=int((e > 10).sum()), unread=sum(1 for r in out if r['centsError'] is None))
    print(json.dumps(summ))
    for r in out:
        if r['centsError'] is None or abs(r['centsError']) > 10:
            print('  bar %s beat %s pitch %d%+d cents: error %s (%d ms)' % (r['bar'], r['beat'], r['pitch'], r['cents'],
                                                                            r['centsError'], r['spanMs']))
    if a.o:
        json.dump(dict(summary=summ, notes=out), open(a.o, 'w'), indent=0)


if __name__ == '__main__':
    main()
