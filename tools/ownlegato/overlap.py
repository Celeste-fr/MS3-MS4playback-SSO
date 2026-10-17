#!/usr/bin/env python3
"""An event file (kthost format) with every legato note-off (24 ms after a note-on: record.py's OVERLAP_S) moved to
`ms` after that note-on: our instrument's hand-over starts at the old note's note-off.

    overlap.py <in.txt> <out.txt> --ms 1
"""
import argparse


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('inp')
    ap.add_argument('out')
    ap.add_argument('--ms', type=float, default=1.0)
    ap.add_argument('--was', type=float, default=24.0)
    a = ap.parse_args()
    L = [l.split() for l in open(a.inp).read().splitlines() if l.strip()]
    ons = {round(float(l[0]), 6) for l in L if l[1] == 'on'}
    n = 0
    for l in L:
        if l[1] == 'off' and round(float(l[0]) - a.was / 1000, 6) in ons:
            l[0] = '%.6f' % (float(l[0]) - (a.was - a.ms) / 1000)
            n += 1
    order = {'cc': 0, 'pb': 0, 'off': 1, 'on': 2}
    L.sort(key=lambda l: (float(l[0]), order[l[1]]))
    with open(a.out, 'w', newline='') as f:
        f.write(''.join(' '.join(l) + '\r\n' for l in L))
    print('%d note-offs moved to %g ms' % (n, a.ms))


if __name__ == '__main__':
    main()
