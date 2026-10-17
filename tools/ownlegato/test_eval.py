#!/usr/bin/env python3
"""The held-out test set (record.py test) measured in a render: SSO's (run_vm.cmd, note-ons at the written times) or
ours (test_events.py: note-ons L_t / L_a earlier). Per transition, on the same music:

  hand-over   joins.handover from the note-on as sent to the next note-on as sent (the whole hand-over: our crossfade
              and SSO's own both lie in it): dipDb, pumpDb, fluxZ, hfDb, roughC, jumpC, gapMs
  heardMs     the arrival by the heard check's rule (heard_check.transition_time: 3 pYIN frames within 30 cents, looked
              for until the next note-on as sent) less the written time
  attack      each phrase's first note (a slur's first note): heard_check.attack_time (-15 dB under the slur's peak,
              less 30 ms) less the written time
  levelStep   the change of the notes' loudest perceived level (loudness.perceived_envelope, as heard_check's LEVEL)
              over each note's heard span (its arrival, else written time, to the next one's), dB; LEVEL: |step| > 0.69

    test_eval.py <test.json> <render.wav> [--lt 0 --la 0] [-o out.json] [--title t]
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
import joins  # noqa: E402
from common import SR, stereo  # noqa: E402
from loudness import perceived_envelope  # noqa: E402


def stats(v, worst=max):
    v = np.array([x for x in v if x is not None], dtype=float)
    if not len(v):
        return None
    return dict(n=int(len(v)), median=round(float(np.median(v)), 2), p10=round(float(np.percentile(v, 10)), 2),
                p90=round(float(np.percentile(v, 90)), 2), worst=round(float(worst(v)), 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('json')
    ap.add_argument('wav')
    ap.add_argument('--lt', type=float, default=0.0)
    ap.add_argument('--la', type=float, default=0.0)
    ap.add_argument('--lt-slow', type=float)
    ap.add_argument('-o')
    ap.add_argument('--title', default='')
    a = ap.parse_args()
    J = json.load(open(a.json))
    x2 = stereo(a.wav)
    x = x2.mean(1).astype(np.float64)
    env, first, hop = perceived_envelope(x2, SR)
    from scipy.signal import butter, sosfilt
    hp = sosfilt(butter(4, 5000, 'highpass', fs=SR, output='sos'), x)
    rows, attacks = [], []
    for si, s in enumerate(J['segments']):
        ns = s['notes']
        sent = [n['on'] - ((a.lt_slow if a.lt_slow is not None and n['on'] - ns[i - 1]['on'] >= 0.35 else a.lt)
                           if i else a.la) / 1000 for i, n in enumerate(ns)]
        lo, hi = min(n['key'] for n in ns) - 3, max(n['key'] for n in ns) + 3
        w0, w1 = sent[0] - 0.3, ns[-1]['off'] + 0.3
        f0, fh = hc.track(x[int(w0 * SR):int(w1 * SR)], SR, lo, hi)
        heard = [None] * len(ns)
        for i in range(1, len(ns)):
            nxt = sent[i + 1] if i + 1 < len(ns) else ns[-1]['off']
            ta, why = hc.transition_time(f0, fh, sent[i] - w0, ns[i - 1]['key'], ns[i]['key'],
                                         after_s=max(sent[i - 1], sent[i] - 0.3) - w0, until_s=nxt - w0)
            heard[i] = None if ta is None else ta + w0
        # the phrase's first note: the heard check's attack rule with the slur's peak (heard_check.analyse: a slur's first
        # note is judged against the loudest point of its slur, at most SPAN_MAX_S)
        on_ms = sent[0] * 1000
        end_ms = min(ns[-1]['off'] * 1000, on_ms + hc.SPAN_MAX_S * 1000)
        t_at, _, _ = hc.attack_time(env, first, hop, on_ms, on_ms - 3000.0, end_ms)
        attacks.append(dict(seg=si, key=ns[0]['key'], ioi=s['ioi'],
                            heardMs=None if t_at is None else round(t_at - ns[0]['on'] * 1000, 1)))
        # levels over heard spans
        span = [(heard[i] if heard[i] else ns[i]['on']) for i in range(len(ns))] + [ns[-1]['off']]
        lev = []
        for i in range(len(ns)):
            ia = int(round((span[i] * 1000 + hc.LATENCY_MS - first) / hop))
            ib = int(round((span[i + 1] * 1000 + hc.LATENCY_MS - first) / hop))
            lev.append(float(env[max(0, ia):max(ia + 1, ib)].max()) if ib > ia else None)
        for i in range(1, len(ns)):
            nxt = sent[i + 1] if i + 1 < len(ns) else ns[-1]['off']
            m = joins.handover(x, sent[i], nxt, ns[i - 1]['key'], ns[i]['key'], hp) or {}
            m.update(seg=si, i=i, a=ns[i - 1]['key'], b=ns[i]['key'], ioi=s['ioi'], type=s['type'],
                     heardMs=None if heard[i] is None else round((heard[i] - ns[i]['on']) * 1000, 1),
                     levelStep=None if lev[i] is None or lev[i - 1] is None else round(lev[i] - lev[i - 1], 2))
            rows.append(m)
    out = dict(title=a.title, lt=a.lt, la=a.la, rows=rows, attacks=attacks, summary={})
    out['summary']['attackMs'] = stats([r['heardMs'] for r in attacks])
    for k in joins.HANDOVER_KEYS:
        out['summary'][k] = stats([r.get(k) for r in rows], worst=min if k == 'dipDb' else max)
    out['summary']['heardMs'] = stats([r['heardMs'] for r in rows])
    out['summary']['heardSd'] = round(float(np.std([r['heardMs'] for r in rows if r['heardMs'] is not None])), 1)
    out['summary']['unheard'] = sum(1 for r in rows if r['heardMs'] is None)
    st = [abs(r['levelStep']) for r in rows if r['levelStep'] is not None]
    out['summary']['levelStepAbs'] = stats(st)
    out['summary']['LEVEL'] = sum(1 for v in st if v > hc.LEVEL_DL_DB)
    for ioi in sorted({r['ioi'] for r in rows}):
        rr = [r for r in rows if r['ioi'] == ioi]
        out['summary']['ioi%d' % round(ioi * 1000)] = dict(
            n=len(rr), unheard=sum(1 for r in rr if r['heardMs'] is None),
            **{k: (stats([r.get(k) for r in rr]) or {}).get('median') for k in ('fluxZ', 'dipDb', 'pumpDb', 'heardMs')})
    print(a.title, json.dumps(out['summary']))
    if a.o:
        with open(a.o, 'w') as f:
            json.dump(out, f, indent=0)


if __name__ == '__main__':
    main()
