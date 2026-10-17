#!/usr/bin/env python3
"""One part's bars of a score measured by the heard check's rules (heard_check.analyse) in a render, plus the joins' quality
(joins.py) on its Performance routes: for SSO's render (heard_check.sh with HC_KEEP=1) and for ours (score_events.py's
dump + sfzrender's render of its route beside SSO's other routes).

    compare.py <dump> <wav dir> --part Violas --bars 8-13 --perf-routes 0-12,0-13,0-14 [-o out.json] [--title t]

The dump is cut to the part's notes in the bars (so every slur, run, ARRIVAL pair and LEVEL step is inside them; the bar
before's last note is not judged). Scatter: the transitions' heard - written, ms (median, SD, 10th-90th percentile).
"""
import argparse
import json
import os
import shutil
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import heard_check as hc  # noqa: E402
import joins  # noqa: E402
from common import mono  # noqa: E402


def crop(dump, part, b0, b1):
    d = tempfile.mkdtemp(prefix='olcmp-')
    for f in os.listdir(dump):
        p = os.path.abspath(os.path.join(dump, f))
        if os.path.isfile(p):
            os.symlink(p, os.path.join(d, f)) if f != 'predicted.tsv' else None
    lines = hc._read(os.path.join(dump, 'predicted.tsv')).splitlines()
    head = lines[0].split('\t')
    ip, ib = head.index('part'), head.index('bar')
    keep = [lines[0]] + [l for l in lines[1:] if l.split('\t')[ip] == part and b0 <= int(l.split('\t')[ib]) <= b1]
    hc._write(os.path.join(d, 'predicted.tsv'), '\n'.join(keep) + '\n')
    return d


def stats(v):
    v = np.array([x for x in v if x is not None], dtype=float)
    if not len(v):
        return None
    return dict(n=int(len(v)), median=round(float(np.median(v)), 1), sd=round(float(np.std(v)), 1),
                p10=round(float(np.percentile(v, 10)), 1), p90=round(float(np.percentile(v, 90)), 1),
                min=round(float(v.min()), 1), max=round(float(v.max()), 1))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('wavs')
    ap.add_argument('--part', default='Violas')
    ap.add_argument('--bars', default='8-13')
    ap.add_argument('--perf-routes', default='0-12,0-13,0-14')
    ap.add_argument('-o')
    ap.add_argument('--title', default='')
    a = ap.parse_args()
    b0, b1 = (int(v) for v in a.bars.split('-'))
    d = crop(a.dump, a.part, b0, b1)
    try:
        res = hc.analyse(d, a.wavs, parts={a.part})
    finally:
        shutil.rmtree(d)
    lead = res['lead']
    notes = [n for n in res['notes'] if n['part'] == a.part]
    tr = [n for n in notes if n['transition']]
    out = dict(title=a.title, notes=notes, findings=res['findings'], groups=res['groups'],
               scatterTransitions=stats([n['heardMs'] for n in tr]),
               scatterAttacks=stats([n['heardMs'] for n in notes if not n['transition'] and n['route'] in a.perf_routes.split(',')]),
               scatterAll=stats([n['heardMs'] for n in notes]),
               unheard=sum(1 for n in tr if n['heardMs'] is None))
    # joins: each transition's note-on on its route's render; the baseline at quiet points of the same routes' notes
    J, B = [], []
    for route in a.perf_routes.split(','):
        p = os.path.join(a.wavs, route + '.wav')
        if not os.path.exists(p):
            continue
        x = mono(p).astype(np.float64)
        rn = sorted([n for n in notes if n['route'] == route], key=lambda n: n['written'])
        ons = [((n['written'] + hc.OFFSET_S) * 1000 + lead + n['sentMs']) / 1000 for n in rn]
        jt = [t for n, t in zip(rn, ons) if n['transition']]
        bt = []
        for i, n in enumerate(rn):
            if n['heardMs'] is None or i + 1 >= len(rn):
                continue
            h = (n['written'] + hc.OFFSET_S) + lead / 1000 + n['heardMs'] / 1000
            if ons[i + 1] - h > 0.10:
                bt.append((h + ons[i + 1]) / 2)
        J += joins.all_measures(x, jt)
        B += joins.all_measures(x, bt)
    out['joins'] = dict(handovers=joins.summary(J), baseline=joins.summary(B), each=J)
    txt = ['%s' % a.title,
           'transitions heard - written: %s' % out['scatterTransitions'],
           'attacks (Performance routes): %s' % out['scatterAttacks'],
           'unheard transitions: %d of %d' % (out['unheard'], len(tr))]
    for f in res['findings']:
        txt.append('%s %s bar %s beat %s: %s' % (f['severity'], f['kind'], f['bar'], f['beat'], f['text']))
    for g in res['groups']:
        txt.append('%s %s: max dp %s heard %s written %s' % (g['kind'], g['label'], g['maxDeviation'], g['heardIoiMs'], g['writtenIoiMs']))
    txt.append('joins: %s' % json.dumps(out['joins']['handovers']))
    txt.append('baseline: %s' % json.dumps(out['joins']['baseline']))
    print('\n'.join(txt))
    if a.o:
        with open(a.o, 'w') as f:
            json.dump(out, f, indent=1)


if __name__ == '__main__':
    main()
