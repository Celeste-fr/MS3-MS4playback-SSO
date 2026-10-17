#!/usr/bin/env python3
"""r3.py probe's render measured: CC119 inside the slow band and at the speed groups (portamento, deterministic), the
normal chain's CC119 differences against SSO's take spread, and the instance-age effect (one chain at 12 starts).

    r3_probe.py <p3probe.json> <p3probe.flac>

Null per note window (note-on .. next note-on), both channels, best whole-sample lag in +-3; stages as twin_ana.py.
"""
import json
import sys

import numpy as np

sys.path.insert(0, __import__('os').path.dirname(__import__('os').path.abspath(__file__)))
from common import SR, stereo  # noqa: E402
from r3_judge import pair  # noqa: E402


def notes_null(X, sa, sb, keys=('null', 's0', 's70', 's300', 's900')):
    out = []
    na, nb = sa['notes'], sb['notes']
    for i in range(len(na)):
        span = (na[i + 1]['on'] - na[i]['on']) if i + 1 < len(na) else 1.5
        L = int(round(span * SR))
        r = pair(X, int(float('%.6f' % na[i]['on']) * SR), L, X, int(float('%.6f' % nb[i]['on']) * SR))
        r.update(i=i, a=na[i - 1]['key'] if i else None, b=na[i]['key'])
        out.append(r)
    return out


def main():
    J = json.load(open(sys.argv[1]))
    X = stereo(sys.argv[2])
    seg = J['segments']
    B = [s for s in seg if s.get('test') == 'b']
    C = [s for s in seg if s.get('test') == 'c']
    D = [s for s in seg if s.get('test') == 'd']
    # instance age: each later play of the chain against the first
    print('(d) instance age: chain 58-60-58 (vel 5) at its k-th start vs the 1st (start s): null of the transition '
          'notes / the attack, dB')
    for k in range(1, len(D)):
        r = notes_null(X, D[0], D[k])
        print('  k %2d start %7.2f s (+%.3f): attack %6.1f, transitions %s' % (
            k, D[k]['notes'][0]['on'], D[k]['notes'][0]['on'] - D[0]['notes'][0]['on'], r[0]['null'],
            ' '.join('%6.1f' % e['null'] for e in r[1:])))
    print('(b) portamento, CC119 v vs 0 (the same chain, later in the file): median over transitions [by stage], '
          'and the intervals 1-3 alone')
    for s in B[1:]:
        r = notes_null(X, B[0], s)[1:]
        small = [e['null'] for e in r if abs(e['b'] - e['a']) <= 3]
        print('  CC119 %3d: %6.1f [%s]; 1-3 st %6.1f; bands %.2f dB' % (
            s['cc'], np.median([e['null'] for e in r]),
            ' / '.join('%.1f' % np.median([e[k] for e in r if k in e]) for k in ('s0', 's70', 's300', 's900')),
            np.median(small), np.median([e['bands'] for e in r])))
    print('(c) normal (vel 64), CC119 v vs 0: null / bands (SSO\'s take spread at vel 64: null -12 median)')
    for s in C[1:]:
        r = notes_null(X, C[0], s)[1:]
        print('  CC119 %3d: null %6.1f, bands %.2f dB, s900 %.1f' % (s['cc'], np.median([e['null'] for e in r]),
              np.median([e['bands'] for e in r]), np.median([e['s900'] for e in r if 's900' in e])))
    r = notes_null(X, C[1], C[2])[1:]
    print('  CC119 63 vs 94: null %6.1f, bands %.2f dB' % (np.median([e['null'] for e in r]), np.median([e['bands'] for e in r])))


if __name__ == '__main__':
    main()
