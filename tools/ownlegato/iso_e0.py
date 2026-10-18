#!/usr/bin/env python3
"""Voice-isolation probe E0 (existing round-3/4 portamento renders, no new recording): does SSO's output after a
transition note-on obey out(t) = S(t) * R(t) + N(t), S = the old note held on alone (same age), R = one gain envelope
(the old voices' release), N = the new note's components (history-independent)?
Pairs X -> B recorded twice: p_rel (X attacked, age 0.6 s, CC119 50) and p3port (X reached by a transition, age 1.5 s,
CC119 0; portamento is identical over CC119 0-61). S from p3atk (X attacked, held 6 s) at the same age.
Fit R per 10 ms window from d = out1 - out2 = R (S1 - S2); report R(t) and the consistency residual
e = d - R (S1 - S2) against N's energy, by time region."""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa

R3 = os.path.expanduser('~/.cache/ownlegato/r3'); R4 = os.path.expanduser('~/.cache/ownlegato/r4')
L = int(1.15 * SR); W = 441


def segs(js):
    return json.load(open(js))['segments']


def at(x, t, n=L):
    i = int(round(t * SR)); return x[i:i + n]


def lag(o, to, a, ta, n=int(0.25 * SR)):
    """the S file's lag (samples) that nulls the 0.25 s before the note-on best (event-time rounding: 0 or +-1)"""
    i, j = int(round(to * SR)) - n, int(round(ta * SR)) - n
    return min(range(-3, 4), key=lambda l: ((o[i:i + n] - a[j + l:j + l + n]) ** 2).sum())


def main():
    atk = stereo(R3 + '/audio/b1/p3atk.flac'); port = stereo(R3 + '/audio/b1/p3port.flac')
    rel = stereo(R4 + '/audio/b_rel/p_rel_p1.flac')
    aon = {s['notes'][0]['key']: s['notes'][0]['on'] for s in segs(R3 + '/b1/p3atk.json')}
    ch = {}
    for s in segs(R3 + '/b1/p3port.json'):
        n = s['notes']
        for i in range(len(n) - 1):
            ch.setdefault((n[i]['key'], n[i + 1]['key']), (n[i + 1]['on'], n[i + 1]['on'] - n[i]['on'], i))
    regs = [(0, 0.03), (0.03, 0.1), (0.1, 0.3), (0.3, 0.6), (0.6, 1.15)]
    Rs = []; rows = []
    for s in segs(R4 + '/b_rel/p_rel.json'):
        x, b = s['notes'][0]['key'], s['notes'][1]['key']
        if (x, b) not in ch: continue
        tb2, age2, idx = ch[(x, b)]; tb1 = s['notes'][1]['on']; age1 = tb1 - s['notes'][0]['on']
        o1, o2 = at(rel, tb1), at(port, tb2)
        s1, s2 = at(atk, aon[x] + age1 + lag(rel, tb1, atk, aon[x] + age1) / SR), at(atk, aon[x] + age2 + lag(port, tb2, atk, aon[x] + age2) / SR)
        d, ds = o1 - o2, s1 - s2
        nw = L // W; Rw = np.zeros(nw); e = np.zeros_like(d)
        for w in range(nw):
            sl = slice(w * W, (w + 1) * W)
            den = (ds[sl] ** 2).sum()
            Rw[w] = (d[sl] * ds[sl]).sum() / den if den > 0 else 0
        Rc = np.clip(Rw, 0, 1.2)
        Rt = np.repeat(Rc, W)[:nw * W]
        e[:nw * W] = d[:nw * W] - Rt[:, None] * ds[:nw * W]
        N1 = o1 - np.r_[Rt, np.zeros(L - nw * W)][:, None] * s1
        Rs.append(Rc)
        r = []
        for a, z in regs:
            sl = slice(int(a * SR), int(z * SR))
            r.append(10 * np.log10((e[sl] ** 2).sum() / ((N1[sl] ** 2).sum() + 1e-20) + 1e-20))
        # without isolation: the plain difference of the two contexts against out1
        r0 = [10 * np.log10((d[int(a * SR):int(z * SR)] ** 2).sum() / (o1[int(a * SR):int(z * SR)] ** 2).sum()) for a, z in regs]
        rows.append((x, b, idx, r, r0))
        print('%d->%d (chain idx %d)  resid/N %s   raw ctx diff %s' % (x, b, idx, ' '.join('%6.1f' % v for v in r), ' '.join('%6.1f' % v for v in r0)))
    Rm = np.median(np.array(Rs), 0)
    print('median R by 10 ms:', ' '.join('%.2f' % v for v in Rm[:40]))
    print('regions', regs)
    print('median resid/N', ' '.join('%6.1f' % v for v in np.median([r[3] for r in rows], 0)),
          ' raw', ' '.join('%6.1f' % v for v in np.median([r[4] for r in rows], 0)))


main()
