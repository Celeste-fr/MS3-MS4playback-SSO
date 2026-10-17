#!/usr/bin/env python3
"""Recording plans for the own-legato pilot (the owner, 2026-10-16: "record all the samples the performance patch uses, and
then make our own patch"): kthost event files that play SSO's Violas - Performance (Kontakt 8 + SSO on the Windows VM,
offline) so that each sound the patch makes can be cut out of the render, plus a JSON log of what was sent and when.

    record.py probe <out dir>                    the probe: round robins, velocity bands, CC11 law, determinism
    record.py fast  <out dir> --keys 52-70 --types fingered:69,bowed:100 --cc1 64 [--dts 0.2:7,0.1:3] [--rr 2]
                                                 transitions as SSO plays them in quick notes (its fast / runs sets)
    record.py runs  <out dir> --types fingered:69,bowed:100 --cc1 64 --dts 0.136:7,0.2:7,0.1:3 --chain 24
                                                 the same, each in runs at dt (every transition after a transition)
    record.py pairs <out dir> --chains 55-63-55 [--name sup] [--types ...] [--cc1 ...] [--rr 2]
                                                 a supplement of chosen chains
    record.py plan  <out dir> --keys 52-70 [--intervals 7] [--types fingered:69,bowed:100,portamento:10]
                    [--cc1 32,64,100] [--rr 2] [--per-file 150]
                                                 the pilot's recording: every transition between the keys up / down to
                                                 --intervals semitones, each type, each CC1 layer, each round robin;
                                                 smooth attacks with long holds and releases

Event files are kthost's "time on|off|cc|pb a b" lines (seconds; kthost renders one fresh plug-in instance per file, so the
round-robin counters start the same in every file: the "transport restart before each pass"). Each file's log
(<name>.json) lists its segments: kind, keys, velocities, CC1, the times of every note-on / note-off.

How the sounds are played (MuseScore's own rules for the Performance Legato, map attacks= / transitions= / overlap=):
  an attack: a note after silence, its velocity picks the attack (smooth 1-9, spiccato 10-116, accented 117-127);
  a transition: a note-on while the note before is held, its velocity picks the transition (portamento 1-19, fingered
  20-84, bowed 85-127); the note before's note-off OVERLAP_S after the new note-on, as MuseScore sends it.
Each transition starts from a note held HOLD_S (its sustain settled), the new note held HOLD_S too. Transitions are played
in chains (A -> B -> C ..., an Eulerian circuit over the wanted pairs), so every note is the B of one transition and the A
of the next; a chain ends in a release (REST_S of silence: the release rings out) and the next chain starts with an attack
(smooth, the attack MuseScore plays for a slur after silence), which is recorded as an attack sample with its hold.
CC11 127 throughout (the patch's own level); CC1 set during the silence before a chain, REST_S / 2 before its first note.
"""
import argparse
import json
import os
import sys

HOLD_S = 1.5        # each note of a chain (the task: A held ~1.5 s, then B ~1.5 s)
OVERLAP_S = 0.024   # the map's overlap="24": the note before ends 24 ms after the new note-on
REST_S = 3.2        # silence after a chain (Violas - Performance release="2620" ms)
ATTACK_HOLD_S = 2.5 # a chain's first note (the attack): its own attack sample is recorded apart (atk), held SMOOTH_HOLD_S
RUN_END_S = 1.5    # runs: a run's last note held this long
FAST_HOLD_S = 1.3   # fast: B held this long after its fast-set transition
SMOOTH_HOLD_S = 6.0 # the attack recordings: attack + a sustain long enough to loop, then the release
START_S = 1.0


class File:
    def __init__(self, name):
        self.name, self.ev, self.seg, self.t = name, [], [], START_S
        self.cc(START_S - 0.5, 11, 127)
        self.cc(START_S - 0.5, 1, 64)
        self.pb(START_S - 0.5, 8192)

    def cc(self, t, n, v):
        self.ev.append((round(t, 6), 'cc', n, v))

    def pb(self, t, v):
        self.ev.append((round(t, 6), 'pb', v, 0))

    def on(self, t, k, v):
        self.ev.append((round(t, 6), 'on', k, v))

    def off(self, t, k):
        self.ev.append((round(t, 6), 'off', k, 0))

    def chain(self, keys, attack_vel, trans_vels, cc1, kind='chain', hold=HOLD_S, attack_hold=ATTACK_HOLD_S, holds=None,
              **meta):
        """keys[0] attacked (attack_vel), each next key a transition at trans_vels[i-1]; then a release and REST_S.
        holds: each transition note's own hold (s), else `hold`."""
        self.cc(self.t - REST_S / 2 if self.t > START_S else self.t - 0.4, 1, cc1)
        t = self.t
        notes = [{'key': keys[0], 'vel': attack_vel, 'on': round(t, 6)}]
        self.on(t, keys[0], attack_vel)
        t += attack_hold
        for i, k in enumerate(keys[1:]):
            v = trans_vels[i] if isinstance(trans_vels, list) else trans_vels
            self.on(t, k, v)
            self.off(t + OVERLAP_S, keys[i])
            notes[-1]['off'] = round(t + OVERLAP_S, 6)
            notes.append({'key': k, 'vel': v, 'on': round(t, 6)})
            t += holds[i] if holds else hold
        self.off(t, keys[-1])
        notes[-1]['off'] = round(t, 6)
        seg = dict(kind=kind, cc1=cc1, notes=notes, end=round(t + REST_S, 6))
        seg.update(meta)
        self.seg.append(seg)
        self.t = t + REST_S
        return seg

    def single(self, key, vel, cc1, hold=HOLD_S, kind='attack', **meta):
        return self.chain([key], vel, [], cc1, kind=kind, attack_hold=hold, **meta)

    def write(self, out):
        order = {'cc': 0, 'pb': 0, 'off': 1, 'on': 2}
        ev = sorted(self.ev, key=lambda e: (e[0], order[e[1]]))
        with open(os.path.join(out, self.name + '.txt'), 'w', newline='') as f:
            f.write(''.join('%.6f %s %d %d\r\n' % e for e in ev))
        with open(os.path.join(out, self.name + '.json'), 'w') as f:
            json.dump(dict(name=self.name, holdS=HOLD_S, overlapS=OVERLAP_S, restS=REST_S, segments=self.seg,
                           duration=round(self.t, 3)), f, indent=1)
        return self.t


def probe(out):
    """Round robins, the velocity bands, the CC11 law, chains against pairs. Rendered twice (two processes): determinism."""
    f = File('probe')
    for r in range(8):                                  # P1: one pair 8 times: the transition's round robins
        f.chain([60, 62], 5, 69, 64, kind='pair', test='P1', rep=r, attack_hold=HOLD_S)
    f.chain([60, 62, 60, 62, 60, 62, 60, 62, 60], 5, 69, 64, kind='chain', test='P2', attack_hold=HOLD_S)
    for v in (5, 10, 19, 20, 40, 69, 84, 85, 100, 127):  # P3: the transition by velocity
        f.chain([60, 62], 5, v, 64, kind='pair', test='P3', tvel=v, attack_hold=HOLD_S)
    for v in (5, 9, 10, 64, 116, 117, 127):              # P4: the attack by velocity
        f.single(60, v, 64, test='P4', avel=v)
    # P5: CC11 law on a held note (steps of 0.7 s)
    t0 = f.t
    f.on(t0, 60, 5)
    steps = [127, 119, 111, 100, 90, 80, 64, 48, 32, 127]
    for i, v in enumerate(steps):
        f.cc(t0 + 2.0 + 0.7 * i, 11, v)
    t1 = t0 + 2.0 + 0.7 * len(steps)
    f.off(t1, 60)
    f.seg.append(dict(kind='cc11', test='P5', cc1=64, notes=[{'key': 60, 'vel': 5, 'on': t0, 'off': t1}],
                      cc11=[[round(t0 + 2.0 + 0.7 * i, 6), v] for i, v in enumerate(steps)], end=t1 + REST_S))
    f.t = t1 + REST_S
    for r in range(8):                                  # P6: the smooth attack's round robins
        f.single(60, 5, 64, test='P6', rep=r)
    for r in range(4):                                  # P7: two pairs alternating: round robins per key or shared?
        f.chain([60, 62], 5, 69, 64, kind='pair', test='P7', rep=r, pair='60-62', attack_hold=HOLD_S)
        f.chain([64, 65], 5, 69, 64, kind='pair', test='P7', rep=r, pair='64-65', attack_hold=HOLD_S)
    for c in (32, 100):                                 # P8: other CC1 layers
        for r in range(3):
            f.chain([60, 62], 5, 69, c, kind='pair', test='P8', rep=r, attack_hold=HOLD_S)
    d = f.write(out)
    with open(os.path.join(out, 'list.txt'), 'w') as fl:
        fl.write('probe|Violas - Performance\n')
    print('probe: %.1f s' % d)


def euler_chains(keys, maxint, maxlen):
    """Chains covering every directed pair (a, b), 1 <= |a - b| <= maxint, both in keys, exactly once (Hierholzer: the
    graph is symmetric, so one circuit exists), split into chains of at most maxlen transitions."""
    adj = {k: [b for b in keys if b != k and abs(b - k) <= maxint] for k in keys}
    for k in adj:                                       # alternate up / down so a chain wanders less
        adj[k].sort(key=lambda b: (abs(b - k), b < k))
    used = {k: list(reversed(v)) for k, v in adj.items()}
    stack, path = [keys[len(keys) // 2]], []
    while stack:
        v = stack[-1]
        if used[v]:
            stack.append(used[v].pop())
        else:
            path.append(stack.pop())
    path.reverse()
    chains, i = [], 0
    while i < len(path) - 1:
        c = path[i:i + maxlen + 1]
        chains.append(c)
        i += maxlen
    return chains


def plan(out, keys, maxint, types, cc1s, rr, per_file, chain_len):
    chains = euler_chains(keys, maxint, chain_len)
    npairs = sum(len(c) - 1 for c in chains)
    files, lst, total = [], [], 0.0
    cur, n = None, 0
    for tname, tvel in types:
        for cc1 in cc1s:
            for r in range(rr):
                for ci, c in enumerate(chains):
                    if cur is None or n >= per_file:
                        if cur is not None:
                            total += cur.write(out)
                        cur = File('rec%02d' % len(files))
                        files.append(cur)
                        lst.append('%s|Violas - Performance' % cur.name)
                        n = 0
                    cur.chain(c, 5, tvel, cc1, kind='chain', type=tname, tvel=tvel, rep=r, chain=ci)
                    n += len(c) - 1
    total += cur.write(out)
    # attacks after silence at every key: smooth (what MuseScore plays for a slur after silence) at each CC1 layer, held
    # SMOOTH_HOLD_S (attack + a sustain to loop) then released; spiccato / accented at the middle layer, held 2 s
    f = File('atk')
    files.append(f)
    lst.append('atk|Violas - Performance')
    for cc1 in cc1s:
        for k in keys:
            f.single(k, 5, cc1, hold=SMOOTH_HOLD_S, kind='attack', attack='smooth', avel=5)
    mid = cc1s[len(cc1s) // 2]
    for name, v in (('spiccato', 112), ('accented', 122)):
        for k in keys:
            f.single(k, v, mid, hold=2.0, kind='attack', attack=name, avel=v)
    total += f.write(out)
    with open(os.path.join(out, 'list.txt'), 'w') as fl:
        fl.write(''.join(l + '\n' for l in lst))
    print('%d keys, %d pairs per pass, %d chains, %d types x %d CC1 x %d rr = %d transitions, %d attacks; %d files, %.0f s'
          % (len(keys), npairs, len(chains), len(types), len(cc1s), rr, npairs * len(types) * len(cc1s) * rr,
             len(chains) * len(types) * len(cc1s) * rr, len(files), total))


def pairs(out, name, chains, types, cc1s, rr):
    """A supplement: the given chains (e.g. 55-63-55: Whence's bar 7 -> 8 leap of +8, outside the plan's +-7), every type,
    CC1 layer and round robin, in one file <name>."""
    f = File(name)
    for tname, tvel in types:
        for cc1 in cc1s:
            for r in range(rr):
                for ci, c in enumerate(chains):
                    f.chain(c, 5, tvel, cc1, kind='chain', type=tname, tvel=tvel, rep=r, chain=ci)
    d = f.write(out)
    with open(os.path.join(out, 'list.txt'), 'w') as fl:
        fl.write('%s|Violas - Performance\n' % name)
    print('%s: %.0f s' % (name, d))


def fast(out, keys, maxint, types, cc1, rr, dts, maxpairs, name='fast'):
    """Transitions played the way SSO's faster sample sets are picked (read from the patch's script, the legato-switch
    branch: the set by the time since the previous legato note-on, fallback bands 115 / 350 ms; the 'runs' set only for
    minor 2nd, major 2nd, minor 3rd): for each wanted pair (A, B) A is reached by a transition and held dt (s), then B
    by a transition (so B's transition comes from the dt set) and held FAST_HOLD_S, long enough to cut B's sustain from.
    Chained: ... B_i -> A_{i+1} (a slow-set transition after FAST_HOLD_S, logged as such) -> B_{i+1} ...; each chain
    begins with a smooth attack on a neighbour X of A_1 held 0.6 s. dts: {dt: max |interval|}."""
    files, lst = [], []
    for dt, lim in dts:
        pairs = [(a, b) for a in keys for b in keys if a != b and abs(a - b) <= min(maxint, lim)]
        for tname, tvel in types:
            for r in range(rr):
                todo = list(pairs)
                f = File('%s_%03d_%s_%d' % (name, int(dt * 1000), tname[:4], r))
                files.append(f)
                lst.append('%s|Violas - Performance' % f.name)
                while todo:
                    a, b = todo.pop(0)
                    x = a - 2 if a - 2 in keys else a + 2
                    ks, hs, vs, roles = [x, a, b], [dt, FAST_HOLD_S], [tvel, tvel], ['x', 'a', 'b']
                    n = 1
                    while todo and n < maxpairs:
                        nxt = next((p for p in todo if p[0] != ks[-1] and abs(p[0] - ks[-1]) <= maxint), None)
                        if nxt is None:
                            break
                        todo.remove(nxt)
                        ks += [nxt[0], nxt[1]]
                        hs += [dt, FAST_HOLD_S]
                        vs += [tvel, tvel]
                        roles += ['a', 'b']
                        n += 1
                    f.chain(ks, 5, vs, cc1, kind='fast', holds=hs, attack_hold=0.6, type=tname, tvel=tvel, rep=r, dt=dt,
                            roles=roles)
    total = sum(f.write(out) for f in files)
    with open(os.path.join(out, 'list.txt'), 'w') as fl:
        fl.write(''.join(l + '\n' for l in lst))
    print('%d files, %.0f s' % (len(files), total))


def runs(out, keys, maxint, types, cc1, rr, dts, chain_len, name='run'):
    """Runs: every note dt after the one before (so every transition but a run's first comes from SSO's set for dt, read
    from the patch's script: runs < 115 ms (minor 2nd .. minor 3rd only), fast < 350 ms, and follows a transition of the
    same set, as in a real run), covering every directed pair once per take (euler_chains, chain_len transitions a run);
    the run's first note a smooth attack held dt, its last held RUN_END_S (the tail to cut a held note's sustain from),
    then REST_S (SSO resets its legato after 1.5 s of silence). dts: [(dt, max |interval|)]."""
    files, lst = [], []
    for dt, lim in dts:
        chains = euler_chains(keys, min(maxint, lim), chain_len)
        for tname, tvel in types:
            for r in range(rr):
                f = File('%s_%03d_%s_%d' % (name, int(round(dt * 1000)), tname[:4], r))
                files.append(f)
                lst.append('%s|Violas - Performance' % f.name)
                for c in chains:
                    f.chain(c, 5, [tvel] * (len(c) - 1), cc1, kind='run', hold=dt, attack_hold=dt,
                            holds=[dt] * (len(c) - 2) + [RUN_END_S], type=tname, tvel=tvel, rep=r, dt=dt)
    total = sum(f.write(out) for f in files)
    with open(os.path.join(out, 'list.txt'), 'w') as fl:
        fl.write(''.join(l + '\n' for l in lst))
    print('%d files, %.0f s' % (len(files), total))


TEST_IOIS = [0.136, 0.136, 0.2, 0.273, 0.5, 1.0]   # Whence's Violas sixteenths / eighths, and slower notes


def test(out, keys, types, cc1, phrases, seed, name='test'):
    """A held-out test set (never cut into the patch): random phrases in Whence's Violas register, each a smooth attack
    and 5-11 slurred notes, intervals 1-5 semitones, every note's length from TEST_IOIS (a phrase keeps one length but
    its last note, held 0.8 s), the type per phrase (types' velocities). SSO plays it (run_vm.cmd), our patch plays the
    same notes (ol2/testevents.py), and joins / arrivals / level steps are compared on the same music."""
    import random
    rnd = random.Random(seed)
    f = File(name)
    for p in range(phrases):
        n = rnd.randint(6, 12)
        ks = [rnd.choice(keys)]
        while len(ks) < n:
            k = ks[-1] + rnd.choice([-1, 1]) * rnd.randint(1, 5)
            if keys[0] <= k <= keys[-1]:
                ks.append(k)
        ioi = rnd.choice(TEST_IOIS)
        tname, tvel = types[p % len(types)]
        f.chain(ks, 5, tvel, cc1, kind='test', hold=ioi, attack_hold=max(ioi, 0.273), holds=[ioi] * (n - 2) + [0.8],
                type=tname, tvel=tvel, ioi=ioi)
    total = f.write(out)
    with open(os.path.join(out, 'list.txt'), 'w') as fl:
        fl.write('%s|Violas - Performance\n' % f.name)
    print('1 file, %.0f s' % total)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['probe', 'plan', 'pairs', 'fast', 'runs', 'test'])
    ap.add_argument('--phrases', type=int, default=48)
    ap.add_argument('--seed', type=int, default=20261016)
    ap.add_argument('--dts', default='0.2:7,0.1:3', help='fast: dt (s) : largest |interval|, comma separated')
    ap.add_argument('--maxpairs', type=int, default=10)
    ap.add_argument('out')
    ap.add_argument('--keys', default='52-70')
    ap.add_argument('--intervals', type=int, default=7)
    ap.add_argument('--types', default='fingered:69,bowed:100,portamento:10')
    ap.add_argument('--cc1', default='32,64,100')
    ap.add_argument('--rr', type=int, default=2)
    ap.add_argument('--per-file', type=int, default=150)
    ap.add_argument('--chain', type=int, default=12, help='transitions per chain')
    ap.add_argument('--chains', default='55-63-55', help='pairs: chains of keys, comma separated')
    ap.add_argument('--name', default='sup')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    types = [(t.split(':')[0], int(t.split(':')[1])) for t in a.types.split(',')]
    if a.cmd == 'probe':
        probe(a.out)
    elif a.cmd == 'fast':
        lo, hi = (int(x) for x in a.keys.split('-'))
        fast(a.out, list(range(lo, hi + 1)), a.intervals, types, int(a.cc1.split(',')[0]), a.rr,
             [(float(d.split(':')[0]), int(d.split(':')[1])) for d in a.dts.split(',')], a.maxpairs)
    elif a.cmd == 'test':
        lo, hi = (int(x) for x in a.keys.split('-'))
        test(a.out, list(range(lo, hi + 1)), types, int(a.cc1.split(',')[0]), a.phrases, a.seed)
    elif a.cmd == 'runs':
        lo, hi = (int(x) for x in a.keys.split('-'))
        runs(a.out, list(range(lo, hi + 1)), a.intervals, types, int(a.cc1.split(',')[0]), a.rr,
             [(float(d.split(':')[0]), int(d.split(':')[1])) for d in a.dts.split(',')], a.chain)
    elif a.cmd == 'pairs':
        pairs(a.out, a.name, [[int(k) for k in c.split('-')] for c in a.chains.split(',')], types,
              [int(c) for c in a.cc1.split(',')], a.rr)
    else:
        lo, hi = (int(x) for x in a.keys.split('-'))
        plan(a.out, list(range(lo, hi + 1)), a.intervals, types, [int(c) for c in a.cc1.split(',')], a.rr, a.per_file,
             a.chain)


if __name__ == '__main__':
    main()
