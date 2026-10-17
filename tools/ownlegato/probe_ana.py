#!/usr/bin/env python3
"""The probe's renders (record.py probe, rendered twice: <dir>/wav/probe_p1.wav, probe_p2.wav) measured:
every transition's leave / arrival (ms after its note-on), the attacks' and transitions' null between the two processes,
the CC11 law (P5: a held note's RMS at each CC11 step).

    probe_ana.py <probe dir> [-o out.json]
"""
import argparse
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import SR, mono, f0, leave_arrive, load_json  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dir')
    ap.add_argument('-o')
    a = ap.parse_args()
    J = load_json(os.path.join(a.dir, 'probe.json'))
    W = {p: mono(os.path.join(a.dir, 'wav', 'probe_p%d.wav' % p)) for p in (1, 2)}
    rows = []
    for p in (1, 2):
        for s in J['segments']:
            if s['kind'] not in ('pair', 'chain'):
                continue
            for i in range(1, len(s['notes'])):
                n, pr = s['notes'][i], s['notes'][i - 1]
                tb = n['on']
                x = W[p][int((tb - 0.3) * SR):int((tb + 0.9) * SR)]
                t, m = f0(x)
                lv, ar = leave_arrive(t, m, pr['key'], n['key'], 0.25)
                y = W[3 - p][int((tb - 0.3) * SR):int((tb + 0.9) * SR)]
                d = x - y
                null = 20 * np.log10(np.sqrt(np.mean(d ** 2) / np.mean(x ** 2)) + 1e-12)
                rows.append(dict(p=p, test=s['test'], rep=s.get('rep'), a=pr['key'], b=n['key'], vel=n['vel'], cc1=s['cc1'],
                                 leave=None if lv is None else round(float(lv - 0.3) * 1000, 1),
                                 arrive=None if ar is None else round(float(ar - 0.3) * 1000, 1), null=round(float(null), 1)))
                r = rows[-1]
                print('p%d %-3s %-4s %d->%d v%-3d cc1 %-3d leave %6s arrive %6s  null vs other process %6.1f dB'
                      % (p, r['test'], r['rep'] if r['rep'] is not None else '', r['a'], r['b'], r['vel'], r['cc1'],
                         r['leave'], r['arrive'], r['null']))
    # CC11 law
    s = next(s for s in J['segments'] if s['kind'] == 'cc11')
    law = []
    for i, (t, v) in enumerate(s['cc11']):
        x = W[1][int((t + 0.35) * SR):int((t + 0.65) * SR)]
        law.append([v, round(float(10 * np.log10(np.mean(x ** 2))), 2)])
    ref = law[0][1]
    print('CC11 law (dB against 127):', ' '.join('%d:%.2f' % (v, d - ref) for v, d in law))
    if a.o:
        with open(a.o, 'w') as f:
            json.dump(dict(transitions=rows, cc11=[[v, round(d - ref, 2)] for v, d in law]), f, indent=1)


if __name__ == '__main__':
    main()
