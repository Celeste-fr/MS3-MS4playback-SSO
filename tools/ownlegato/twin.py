#!/usr/bin/env python3
"""Twin-pass null test of SSO's Violas - Performance (the handoff's "null test of two identical SSO passes"): does the
patch play the same legato material the same way twice? Decides whether renders of SSO are repeatable enough to cut
into a sample set whose joins match SSO's.

    twin.py <out dir> [--mat chain_v64,... --plays 4 --name twin4]
                             writes <name>.txt / .json / list.txt (kthost event file, record.py's format);
                             --mat / --plays: only those materials, each played --plays times per process
                             (the rank test: how many independent random components a transition carries)

The file plays the material M twice, separated by SEP_S of silence (SSO's round robins reset after 1.5 s without a
note-on, its legato after the release: the second M starts in the same script state as the first). Rendered in several
processes (run_vm.cmd <work> twin 3): each process is a fresh Kontakt instance. So per material there are
"fresh" pairs (pass p, first M vs pass q, first M) and "reused" pairs (pass p, first M vs pass p, second M).

M (all at CC1 64, CC11 127; transitions with the note before's note-off 24 ms after, as MuseScore sends them):
  chain_v<vel>   one chain 58 -> 58+i -> 58 for i = 1..12 (every interval up and down), notes held 1.5 s
                 (the script's CC119 = 0: the slow set), a smooth attack (vel 5) first;
                 vel 64 = fingered / normal (FING 10-84 + the plain interval groups 20-127), vel 5 = clean portamento
                 (PORT 1-19 only), vel 15 = FING + PORT without the plain groups (diagnostic: the plain groups are
                 the only interval groups with a src10 modulator, perf_legato_layers / perfreport)
  ioi136_v<vel>  the same keys, every note 136 ms after the one before (Whence's sixteenths; CC119 ~109: RUNS RT for
                 1-2 semitones), the last held 1.5 s
  ioi1000_v64    the same keys, every note 1 s after the one before (CC119 0)
Never bowed (85-127): the owner rejects bowed transitions on slurs.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from record import File, REST_S  # noqa: E402

BASE = 58
SEP_S = 4.0


def keys():
    ks = [BASE]
    for i in range(1, 13):
        ks += [BASE + i, BASE]
    return ks


MATERIAL = [  # (name, transition velocity, hold per note (s) or None for record.py's HOLD_S)
    ('chain_v64', 64, None), ('chain_v5', 5, None), ('chain_v15', 15, None),
    ('ioi136_v64', 64, 0.136), ('ioi136_v5', 5, 0.136), ('ioi1000_v64', 64, 1.0),
]


def main():
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--mat', default=','.join(m[0] for m in MATERIAL))
    ap.add_argument('--plays', type=int, default=2)
    ap.add_argument('--name', default='twin')
    a = ap.parse_args()
    out = a.out
    os.makedirs(out, exist_ok=True)
    f = File(a.name)
    ks = keys()
    want = a.mat.split(',')
    for half in range(a.plays):
        if half:
            f.t += SEP_S
        for name, vel, hold in [m for m in MATERIAL if m[0] in want]:
            if hold is None:
                f.chain(ks, 5, vel, 64, kind='chain', mat=name, half=half, tvel=vel)
            else:
                f.chain(ks, 5, vel, 64, kind='run', hold=hold, attack_hold=hold,
                        holds=[hold] * (len(ks) - 2) + [1.5], mat=name, half=half, tvel=vel)
    d = f.write(out)
    with open(os.path.join(out, 'list.txt'), 'w') as fl:
        fl.write('%s|Violas - Performance\n' % a.name)
    print('%s: %.1f s, %d segments, rest %.1f s' % (a.name, d, len(f.seg), REST_S))


if __name__ == '__main__':
    main()
