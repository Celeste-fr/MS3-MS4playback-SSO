#!/usr/bin/env python3
"""The held-out test set (record.py test) as our patch plays it: every note-on sent L_t (a transition) or L_a (an
attack) before its written time (the time SSO got it), the note before's note-off `overlap` ms after a transition's
note-on, CC1 and CC11 as SSO got them; no CC68 with --no-cc68 (make_sfz.py --release sw).

    test_events.py <test.json> <out.txt> --lt 100 --la 210 [--overlap 0.1] [--no-cc68] [--lt-slow 250]
"""
import argparse
import json

SLOW_FROM_S = 0.35   # SSO's slow set from 350 ms since the previous legato note-on (read from the patch's script)
VEL = {(False, True): 36, (False, False): 69, (True, True): 91, (True, False): 104}   # (bowed, slow): make_sfz VEL_SETS


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('json')
    ap.add_argument('out')
    ap.add_argument('--lt', type=float, required=True)
    ap.add_argument('--la', type=float, required=True)
    ap.add_argument('--overlap', type=float, default=24.0)
    ap.add_argument('--no-cc68', action='store_true')
    ap.add_argument('--lt-slow', type=float, help='make_sfz.py --vel-sets: a note after one of 350 ms or more is sent '
                    'L_slow early, at the slow band\'s velocity')
    a = ap.parse_args()
    J = json.load(open(a.json))
    ev = [(0.3, 'cc', 1, 64), (0.3, 'cc', 11, 127), (0.3, 'pb', 8192, 0)]
    for s in J['segments']:
        ns = s['notes']
        for i, n in enumerate(ns):
            slow = bool(i) and a.lt_slow is not None and n['on'] - ns[i - 1]['on'] >= SLOW_FROM_S
            on = n['on'] - ((a.lt_slow if slow else a.lt) if i else a.la) / 1000
            vel = n['vel']
            if i and a.lt_slow is not None:
                vel = VEL[(vel >= 85, slow)]
            if i:
                if not a.no_cc68:
                    ev.append((on - 0.001, 'cc', 68, 127))
                ev.append((on, 'on', n['key'], vel))
                ev.append((on + a.overlap / 1000, 'off', ns[i - 1]['key'], 0))
            else:
                ev.append((on, 'on', n['key'], n['vel']))
        if not a.no_cc68:
            ev.append((ns[-1]['off'] - 0.001, 'cc', 68, 0))
        ev.append((ns[-1]['off'], 'off', ns[-1]['key'], 0))
    order = {'cc': 0, 'pb': 0, 'off': 1, 'on': 2}
    ev.sort(key=lambda e: (e[0], order[e[1]]))
    with open(a.out, 'w') as f:
        f.write(''.join('%.6f %s %d %d\n' % e for e in ev))


if __name__ == '__main__':
    main()
