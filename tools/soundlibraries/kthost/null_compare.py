#!/usr/bin/env python3
"""null_compare.py <a.wav> <b.wav> [--max-lag-ms 200] [--json]: how alike two renders of the same events are (kthost
against the restored kthost binary, two takes, two builds): the lag that lines b up with a (cross-correlation of the
samples within --max-lag-ms), the residual a - b at that lag against a (dB: 10 log10 of the
residual's power over a's), the correlation, and the level difference (dB). Samples are compared over the overlap.
"""
import argparse
import json
import math

import numpy as np
import soundfile as sf


def mono(x):
    return x.mean(axis=1) if x.ndim == 2 else x


def compare(a, b, sr, max_lag_ms=200.0):
    from scipy.signal import correlate
    ma, mb = mono(a), mono(b)
    n = min(len(ma), len(mb))
    cc = correlate(ma[:n], mb[:n], mode='full', method='fft')      # index n - 1 + d: a[k + d] against b[k]
    L = int(max_lag_ms * sr / 1000)
    mid = n - 1
    lo, hi = max(0, mid - L), min(len(cc), mid + L + 1)
    lag = int(np.argmax(cc[lo:hi])) + lo - mid         # positive: a is later than b
    if lag >= 0:
        xa, xb = a[lag:], b
    else:
        xa, xb = a, b[-lag:]
    m = min(len(xa), len(xb))
    xa, xb = xa[:m], xb[:m]
    pa, pb = float((xa ** 2).mean()), float((xb ** 2).mean())
    r = float(((xa - xb) ** 2).mean())
    c = float(np.corrcoef(mono(xa), mono(xb))[0, 1]) if pa > 0 and pb > 0 else float('nan')
    db = lambda p: 10 * math.log10(p) if p > 0 else -300.0
    return {'lagSamples': int(lag), 'lagMs': round(lag * 1000.0 / sr, 3), 'residualDb': round(db(r) - db(pa), 2),
            'correlation': round(c, 6), 'levelDiffDb': round(db(pb) - db(pa), 3), 'levelADbfs': round(db(pa), 2),
            'seconds': round(m / sr, 3)}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('a')
    ap.add_argument('b')
    ap.add_argument('--max-lag-ms', type=float, default=200.0)
    ap.add_argument('--json', action='store_true')
    o = ap.parse_args()
    a, sr = sf.read(o.a, always_2d=True)
    b, sr2 = sf.read(o.b, always_2d=True)
    if sr != sr2:
        raise SystemExit('sample rates differ: %d / %d' % (sr, sr2))
    r = compare(a, b, sr, o.max_lag_ms)
    print(json.dumps(r) if o.json else
          'lag %(lagSamples)d samples (%(lagMs).2f ms), residual %(residualDb).1f dB, correlation %(correlation).5f, '
          'level b - a %(levelDiffDb).3f dB over %(seconds).1f s' % r)


if __name__ == '__main__':
    main()
