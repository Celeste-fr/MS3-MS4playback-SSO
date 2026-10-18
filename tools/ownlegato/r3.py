#!/usr/bin/env python3
"""Round 3 recording plans for the own-legato instrument (Violas - Performance, 12-TET): kthost event files + JSON logs
(record.py's File), rendered by run_vm.cmd on the Windows VM, one fresh Kontakt process per file and pass.

Round 3 rules (the owner, 2026-10-17; private/own-legato-plan.md, "Result 2026-10-17"):
  - the recording setup is MuseScore's setup with the script's speed CC off ("Leg. Speed CC" $vfm12 = 0, as the
    fork's [slurs] speedFromWritten loads it) and every transition note-on is preceded by CC119 = 126 - min(126,
    floor(ms * 127 / 1000)), ms = the time since the previous note-on (the script's own formula), so the speed groups
    (Violas: RUNS CC119 114-127 for 1-3 semitones, RUNS RT 95-113 for 1-2, read from the .nki start criteria) are
    chosen exactly; CC119 sits at the note-on's time, sorted before it;
  - only portamento (velocity 5) and normal (64: FING + PORTAMENTO FAST / plain + RUNS / RUNS RT by CC119), never
    bowed / detached (85-127) for slurs;
  - no leaps over 12 semitones (the patch resets the phrase there).

    r3.py probe <out>            setup check, CC119 inside the slow band, instance age (see probe())
    r3.py port <out> [--keys 52-70]
                                 portamento chains over every interval +-1..+-12 (Eulerian), notes held 1.5 s, and the
                                 smooth attacks (vel 5) of every key held 6 s then released
    r3.py norm <out> [--keys 54-66]
                                 normal transitions (vel 64) by speed band, one file per band:
                                   n_slow  chains, notes held 1.5 s (CC119 0; the "Transition port" release groups'
                                           gate dt > 500 ms on)
                                   n_350 / n_150 / n_089  for every pair (A, B): X -> A -> B, X held dt, A held dt, B
                                           held 1.3 s, so B comes from the band of dt with A in the same band's
                                           context: 350 ms (CC119 82: 252-500 ms, no dt > 500 releases), 150 ms (107:
                                           RUNS RT for 1-2 semitones), 89 ms (115: RUNS for 1-3); 150 / 89 are the
                                           sections' speed bins read from the group data
                                 rendered with run_vm.cmd <work> <batch> N: N fresh processes = N takes
    r3.py bands <out> --dts 0.6,0.5357 [--keys 54-66]
                                 more normal speed bands, as norm's n_350/n_150/n_089 (n_<ms>): the CC119 values the
                                 blind-test phrases send that no band had (50 for 600 ms, 58 for 536 ms; round 2's
                                 timing tell: takes recorded at CC119 0 arrived up to 148 ms off SSO at CC119 50)
    r3.py rel <out>              releases after slurred lines (round 2's tail tell: the release region was cut after a
                                 6 s smooth attack): n_rel (vel 64, keys 54-66) and p_rel (vel 5, keys 52-70), each key B
                                 as X -> B, X attacked and held 0.6 s, B held 1.2 s, then released (REST_S)
    r3.py iso <out>              voice-isolation probe E1 (iso_e1.py): transitions A -> B (A 57 / 62, +-1/2/3/5/7) after
                                 three deterministic histories of A (A attacked vel 5; W -> A portamento, W = A -+ 4; W -> A
                                 vel 15 FING + PORT, W = A -+ 2), each twice: 'full' (A held dt, then B held 1.2 s) and 'S'
                                 (the same events without B's note-on: A held on, CC119 still sent at B's time), in files
                                 with identical timing (i_pf / i_ps portamento B vel 5 at dt 0.3 / 0.5 / 1.0; i_nf / i_ns
                                 normal B vel 64 at dt 0.089 / 0.15)
    r3.py test <out> --kind port|norm [--phrases 30] [--seed s]
                                 held-out phrases (never cut): a smooth attack and 5-11 slurred notes, one IOI per
                                 phrase (port: 0.3 / 0.5 / 1.0 / 1.5 s, intervals 1-12; norm: 0.089 / 0.136 / 0.15 /
                                 0.35 / 0.6 / 1.0 s, intervals 1-7), the last held 1.0 s
"""
import argparse
import json
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from record import File, euler_chains, REST_S, OVERLAP_S  # noqa: E402

PATCH = 'Violas - Performance vfm0'   # the setup file run_vm.cmd uses: MuseScore's own with $vfm12 = 0 (setvalues)
PORT, NORM = 5, 64
SHARE_HOLD = 1.3


def cc119(ms):
    """the script's speed value (engine line 17147-17155) for ms since the previous note-on"""
    return 126 - min(126, int(math.floor(ms * 127 / 1000 + 1e-9)))


def add_cc119(f, seg=None, force=None):
    """CC119 before every transition note-on of the file's segments (or one segment); force: a fixed value"""
    for s in ([seg] if seg else f.seg):
        ns = s['notes']
        for i in range(1, len(ns)):
            v = force if force is not None else cc119((ns[i]['on'] - ns[i - 1]['on']) * 1000)
            f.cc(ns[i]['on'], 119, v)
            ns[i]['cc119'] = v


def write_list(out, names):
    with open(os.path.join(out, 'list.txt'), 'w', newline='') as fl:
        fl.write(''.join('%s|%s\r\n' % (n, PATCH) for n in names))


def probe(out):
    """(a) setup check: twin.py's material exactly (its event file, <twin dir>/twin.txt) plus CC119 at each transition:
    must null against twin.py's renders (library setup, the script's own CC119) where SSO is deterministic;
    (b) CC119 inside the slow band and at the speed groups, portamento: the chain 58 -> 58+i -> 58 (i = 1..12) held
    1.5 s at CC119 0 / 63 / 94 / 107 / 115 (deterministic: any difference is CC119's);
    (c) normal (vel 64) chain at CC119 0 / 63 / 94 (against the take-to-take spread);
    (d) instance age: the chain 58 -> 60 -> 58 (vel 5) 12 times at irregular starts."""
    from twin import keys as tkeys
    f = File('p3probe')
    ks = tkeys()
    for v in (0, 63, 94, 107, 115):
        s = f.chain(ks, PORT, PORT, 64, kind='chain', test='b', cc=v, tvel=PORT)
        add_cc119(f, s, force=v)
    for v in (0, 63, 94):
        s = f.chain(ks, PORT, NORM, 64, kind='chain', test='c', cc=v, tvel=NORM)
        add_cc119(f, s, force=v)
    for r in range(12):
        f.t += 0.137 * r
        s = f.chain([58, 60, 58], PORT, PORT, 64, kind='chain', test='d', rep=r, tvel=PORT)
        add_cc119(f, s)
    d = f.write(out)
    names = ['p3probe']
    print('p3probe: %.1f s' % d)
    write_list(out, names)


SWEEP_KEYS = [58, 59, 58, 60, 58, 63, 58, 65, 58, 70, 58]     # intervals 1, 2, 5, 7, 12 up and down


def probe2(out):
    """(e) block partition: the chain 58 -> 60 -> 58 (vel 5) 8 times with the same gap (kthost splits its blocks at
    every event, so equal gaps give the same blocks), then with the gap 1 / 16 / 32 / 64 / 512 / 2048 samples longer;
    (f) CC119 sweep inside 0-94, portamento (deterministic: SWEEP_KEYS at CC119 0 .. 94);
    (g) the same at velocity 64, two plays each (against the take spread)"""
    f = File('p3sweep')
    # (e) exact frames ((F + 0.5) / SR printed to 1 us lands on F under kthost's floor): starts 733 x 512 frames apart
    # (kthost's blocks lie on a 512-frame grid from 0, split at each event), then off the grid by `extra` frames
    SR = 44100
    rel = [(0, 'on', 58, PORT), (110250, 'cc', 119, 0), (110250, 'on', 60, PORT), (110250 + 1058, 'off', 58, 0),
           (176400, 'cc', 119, 0), (176400, 'on', 58, PORT), (176400 + 1058, 'off', 60, 0), (242550, 'off', 58, 0)]
    base = int(round(f.t * SR / 512)) * 512 + 100
    for r, extra in enumerate([0] * 8 + [1, 16, 32, 64, 256]):
        F0 = base + r * 733 * 512 + extra
        for d, ty, k, v in rel:
            f.ev.append(((F0 + d + 0.5) / SR, ty, k, v))
        ons = [(F0 + d + 0.5) / SR for d, ty, k, v in rel if ty == 'on']
        offs = [(F0 + d + 0.5) / SR for d, ty, k, v in rel if ty == 'off']
        f.seg.append(dict(kind='chain', test='e', rep=r, extra=extra, cc1=64, frames=F0,
                          notes=[dict(key=k, vel=PORT, on=round(o, 6), off=round(x, 6))
                                 for k, o, x in zip([58, 60, 58], ons, offs)]))
    f.t = (base + 13 * 733 * 512) / SR + REST_S
    for v in (0, 4, 8, 16, 24, 32, 40, 48, 56, 63, 72, 80, 88, 94):
        s = f.chain(SWEEP_KEYS, PORT, PORT, 64, kind='chain', test='f', cc=v, tvel=PORT)
        add_cc119(f, s, force=v)
    for rep in range(2):
        for v in (0, 16, 32, 48, 63, 80, 94):
            s = f.chain(SWEEP_KEYS, PORT, NORM, 64, kind='chain', test='g', cc=v, rep=rep, tvel=NORM)
            add_cc119(f, s, force=v)
    print('p3sweep: %.1f s' % f.write(out))
    write_list(out, ['p3sweep'])


def probe3(out):
    """(h) CC119 fine sweep, portamento (deterministic), 56..94 step 1 and 95..126 step 2, chain 58 60 58 65 58;
    (i) normal (vel 64), two plays each, 64..94 step 2 and 95..126 step 3 (against the take spread)"""
    f = File('p3fine')
    ks = [58, 60, 58, 65, 58]
    for v in list(range(56, 95)) + list(range(95, 127, 2)):
        s = f.chain(ks, PORT, PORT, 64, kind='chain', test='h', cc=v, tvel=PORT)
        add_cc119(f, s, force=v)
    for rep in range(2):
        for v in list(range(64, 95, 2)) + list(range(95, 127, 3)):
            s = f.chain(ks, PORT, NORM, 64, kind='chain', test='i', cc=v, rep=rep, tvel=NORM)
            add_cc119(f, s, force=v)
    print('p3fine: %.1f s' % f.write(out))


def pgrid(out, keys, dts):
    """portamento at the speeds of dts (s): for every pair (A, B) X -> A -> B with X and A held dt, B held 1.5 s (B
    comes at A's age dt, with CC119 for dt, A reached from X at dt)"""
    pairs = [(a, b) for a in keys for b in keys if a != b and abs(a - b) <= 12]
    names = []
    for dt in dts:
        name = 'p3g_%03d' % int(round(dt * 1000))
        f = File(name)
        todo = list(pairs)
        random.Random(int(dt * 1000)).shuffle(todo)
        while todo:
            ks, hs, roles = [], [], []
            while todo and len(roles) < 30:
                a, b = todo.pop(0)
                x = a + (2 if a < (keys[0] + keys[-1]) / 2 else -2)
                if ks and (abs(x - ks[-1]) > 12 or x == ks[-1]):
                    x = 2 * a - x
                ks += [x, a, b]
                hs += [dt, dt, 1.5]
                roles += ['x', 'a', 'b']
            s = f.chain(ks, PORT, PORT, 64, kind='band', holds=hs[1:], attack_hold=hs[0], type='portamento', tvel=PORT,
                        band='g%d' % int(round(dt * 1000)), dt=dt, roles=roles)
            add_cc119(f, s)
        print('%s: %d pairs, %.0f s' % (name, len(pairs), f.write(out)))
        names.append(name)
    return names


def twin_cc(twin_dir, out):
    """probe (a): twin.py's event file with CC119 inserted at each of its transitions (from its log)"""
    J = json.load(open(os.path.join(twin_dir, 'twin.json')))
    lines = open(os.path.join(twin_dir, 'twin.txt')).read().split('\r\n' if '\r\n' in open(os.path.join(twin_dir, 'twin.txt')).read() else '\n')
    lines = [l for l in lines if l.strip()]
    add = []
    for s in J['segments']:
        ns = s['notes']
        for i in range(1, len(ns)):
            add.append('%.6f cc 119 %d' % (ns[i]['on'], cc119((ns[i]['on'] - ns[i - 1]['on']) * 1000)))
    order = {'cc': 0, 'pb': 0, 'off': 1, 'on': 2}
    ev = sorted(lines + add, key=lambda l: (float(l.split()[0]), order[l.split()[1]]))
    with open(os.path.join(out, 'p3twin.txt'), 'w', newline='') as fo:
        fo.write(''.join(l + '\r\n' for l in ev))
    J['name'] = 'p3twin'
    with open(os.path.join(out, 'p3twin.json'), 'w') as fo:
        json.dump(J, fo, indent=1)


def port(out, keys):
    chains = euler_chains(keys, 12, 12)
    f = File('p3port')
    for ci, c in enumerate(chains):
        s = f.chain(c, PORT, PORT, 64, kind='chain', type='portamento', tvel=PORT, chain=ci, band='slow')
        add_cc119(f, s)
    d = f.write(out)
    a = File('p3atk')
    for k in keys:
        a.single(k, PORT, 64, hold=6.0, kind='attack', attack='smooth', avel=PORT)
    d2 = a.write(out)
    write_list(out, ['p3port', 'p3atk'])
    print('p3port: %d chains, %d transitions, %.0f s; p3atk %.0f s' % (len(chains), sum(len(c) - 1 for c in chains), d, d2))


BANDS = [('n_350', 0.350), ('n_150', 0.150), ('n_089', 0.089)]


def norm(out, keys):
    names = []
    chains = euler_chains(keys, 12, 12)
    f = File('n_slow')
    for ci, c in enumerate(chains):
        s = f.chain(c, PORT, NORM, 64, kind='chain', type='normal', tvel=NORM, chain=ci, band='slow')
        add_cc119(f, s)
    print('n_slow: %d transitions, %.0f s' % (sum(len(c) - 1 for c in chains), f.write(out)))
    names.append('n_slow')
    names += band_files(out, keys, BANDS)
    write_list(out, names)


def rel(out):
    names = []
    for name, vel, keys in (('n_rel', NORM, list(range(54, 67))), ('p_rel', PORT, list(range(52, 71)))):
        f = File(name)
        for b in keys:
            x = b + (2 if b < (keys[0] + keys[-1]) / 2 else -2)
            s = f.chain([x, b], PORT, vel, 64, kind='release', attack_hold=0.6, holds=[1.2], type='release', tvel=vel)
            add_cc119(f, s)
        print('%s: %d keys, %.0f s' % (name, len(keys), f.write(out)))
        names.append(name)
    write_list(out, names)


def band_files(out, keys, bands):
    names = []
    pairs = [(a, b) for a in keys for b in keys if a != b and abs(a - b) <= 12]
    for name, dt in bands:
        f = File(name)
        todo = list(pairs)
        rnd = random.Random(int(dt * 1000))
        rnd.shuffle(todo)
        while todo:
            ks, hs, roles = [], [], []
            while todo and len(roles) < 30:
                a, b = todo.pop(0)
                # X: a step towards the middle of the keys (a run's note before), never a's own key
                x = a + (2 if a < (keys[0] + keys[-1]) / 2 else -2)
                if ks and (abs(x - ks[-1]) > 12 or x == ks[-1]):      # (never the note before's own key)
                    x = 2 * a - x
                ks += [x, a, b]
                hs += [dt, dt, SHARE_HOLD]
                roles += ['x', 'a', 'b']
            s = f.chain(ks, PORT, NORM, 64, kind='band', holds=hs[1:], attack_hold=hs[0], type='normal', tvel=NORM,
                        band=name[2:], dt=dt, roles=roles)
            add_cc119(f, s)
        print('%s: %d pairs, %.0f s' % (name, len(pairs), f.write(out)))
        names.append(name)
    return names


def test(out, kind, keys, phrases, seed):
    rnd = random.Random(seed)
    iois = [0.3, 0.5, 1.0, 1.5] if kind == 'port' else [0.089, 0.136, 0.15, 0.35, 0.6, 1.0]
    maxint = 12 if kind == 'port' else 7
    vel = PORT if kind == 'port' else NORM
    name = 'p3test' if kind == 'port' else 'n3test'
    f = File(name)
    for p in range(phrases):
        n = rnd.randint(6, 12)
        ks = [rnd.choice(keys)]
        while len(ks) < n:
            k = ks[-1] + rnd.choice([-1, 1]) * rnd.randint(1, maxint)
            if keys[0] <= k <= keys[-1]:
                ks.append(k)
        ioi = iois[p % len(iois)]
        s = f.chain(ks, PORT, vel, 64, kind='test', hold=ioi, attack_hold=max(ioi, 0.3), holds=[ioi] * (n - 2) + [1.0],
                    type='portamento' if kind == 'port' else 'normal', tvel=vel, ioi=ioi)
        add_cc119(f, s)
    d = f.write(out)
    write_list(out, [name])
    print('%s: %d phrases, %.0f s' % (name, phrases, d))


ISO_KEYS = (57, 62)
ISO_INTS = (1, -1, 2, -2, 3, -3, 5, -5, 7, -7)
ISO_HIST = (('atk', None, None), ('p4', 4, PORT), ('f2', 2, 15))   # (name, |W - A| on the side away from B, A's vel)


def iso_seg(ff, fs, a, b, dt, bvel, hist, meta):
    """one history of A, then B (ff) / A held on (fs); hist: [] (A attacked) or [(dw, vel, hold), ...] the notes before
    A from the earliest: key A - dw * sign(B - A), attacked / reached at vel, held hold s"""
    sg = 1 if b > a else -1
    if not hist:
        sf_ = ff.chain([a, b], PORT, [bvel], 64, attack_hold=dt, holds=[SHARE_HOLD - 0.1], **meta)
        ss_ = fs.chain([a], PORT, [], 64, attack_hold=dt + SHARE_HOLD - 0.1, **meta)
    else:
        ks = [a - dw * sg for dw, _, _ in hist] + [a]
        vs = [v for _, v, _ in hist[1:]]
        hs = [hd for _, _, hd in hist]
        sf_ = ff.chain(ks + [b], PORT, vs + [hist[-1][1] if len(hist) > 1 else hist[0][1], bvel], 64, attack_hold=hs[0],
                       holds=hs[1:] + [dt, SHARE_HOLD - 0.1], **meta)
        ss_ = fs.chain(ks, PORT, vs + [hist[-1][1] if len(hist) > 1 else hist[0][1]], 64, attack_hold=hs[0],
                       holds=hs[1:] + [dt + SHARE_HOLD - 0.1], **meta)
    add_cc119(ff, sf_)
    add_cc119(fs, ss_)
    tb = sf_['notes'][-1]['on']
    fs.cc(tb, 119, cc119(dt * 1000))
    ss_['tb'] = tb
    assert abs(ff.t - fs.t) < 1e-9


def iso(out):
    """E1: see the docstring (r3.py iso)"""
    names = []
    hists = {'atk': [], 'p4': [(4, PORT, 0.6)], 'f2': [(2, 15, 0.6)]}
    for tag, bvel, dts in (('p', PORT, (0.3, 0.5, 1.0)), ('n', NORM, (0.089, 0.15))):
        ff, fs = File('i_%sf' % tag), File('i_%ss' % tag)
        for a in ISO_KEYS:
            for iv in ISO_INTS:
                for dt in dts:
                    for hn in ('atk', 'p4', 'f2'):
                        iso_seg(ff, fs, a, a + iv, dt, bvel, hists[hn], dict(kind='iso', type=tag, hist=hn, a=a, b=a + iv, dt=dt))
        print('i_%sf / i_%ss: %d segments, %.0f s each' % (tag, tag, len(ff.seg), ff.write(out)), fs.write(out))
        names += ['i_%sf' % tag, 'i_%ss' % tag]
    write_list(out, names)


def iso2(out):
    """E2: which part of A's history changes the new note's components N (normal B vel 64 at 89 / 150 ms)?
    W -> A interval 2 / 3 / 4 / 5 (vel 5), 2 at vel 15, W held 1.2 s instead of 0.6, two notes before A (U -> W -> A),
    A attacked; 8 pairs"""
    hists = {'atk': [], 'p2': [(2, PORT, 0.6)], 'p3': [(3, PORT, 0.6)], 'p4': [(4, PORT, 0.6)], 'p5': [(5, PORT, 0.6)],
             'f2': [(2, 15, 0.6)], 'p4L': [(4, PORT, 1.2)], 'p4x': [(7, PORT, 0.6), (4, PORT, 0.6)],
             'p4f': [(7, PORT, 0.6), (4, PORT, 0.15)]}
    ff, fs = File('i2_nf'), File('i2_ns')
    for a, iv in ((57, 1), (57, -2), (57, 3), (57, -5), (62, -1), (62, 2), (62, -3), (62, 5)):
        for dt in (0.089, 0.15):
            for hn, h in hists.items():
                iso_seg(ff, fs, a, a + iv, dt, NORM, h, dict(kind='iso', type='n', hist=hn, a=a, b=a + iv, dt=dt))
    print('i2_nf / i2_ns: %d segments, %.0f s each' % (len(ff.seg), ff.write(out)), fs.write(out))
    write_list(out, ['i2_nf', 'i2_ns'])


def iso3(out):
    """E3: A reached inside a fast run (every note held dt, so no "transition release" tail voices: their gate needs
    > 500 ms): W -> A interval 2 / 3 / 4 / 5 (vel 5), 4 at vel 15, another note before W, a longer run"""
    ff, fs = File('i3_nf'), File('i3_ns')
    for a, iv in ((57, 1), (57, -2), (57, 3), (57, -5), (62, -1), (62, 2), (62, -3), (62, 5)):
        for dt in (0.089, 0.15):
            hists = {'r2': [(4, PORT, dt), (2, PORT, dt)], 'r3': [(5, PORT, dt), (3, PORT, dt)],
                     'r4': [(6, PORT, dt), (4, PORT, dt)], 'r5': [(7, PORT, dt), (5, PORT, dt)],
                     'r4v': [(6, PORT, dt), (4, 15, dt)], 'r4u': [(9, PORT, dt), (4, PORT, dt)],
                     'r4l': [(2, PORT, dt), (8, PORT, dt), (6, PORT, dt), (4, PORT, dt)]}
            for hn, h in hists.items():
                iso_seg(ff, fs, a, a + iv, dt, NORM, h, dict(kind='iso', type='n', hist=hn, a=a, b=a + iv, dt=dt))
    print('i3_nf / i3_ns: %d segments, %.0f s each' % (len(ff.seg), ff.write(out)), fs.write(out))
    write_list(out, ['i3_nf', 'i3_ns'])


def iso6(out):
    """E6: Kontakt's 512-frame block grid against history: E3's r4 / r2 runs for 4 pairs, each segment started on the
    block grid at offsets 0, 0 again, 37, 128, 300 frames (segment starts aligned to 512 frames first)"""
    ff, fs = File('i6_nf'), File('i6_ns')
    for a, iv in ((57, 1), (57, -2), (62, 2), (62, -3)):
        for dt in (0.089, 0.15):
            for hn, h in (('r4', [(6, PORT, dt), (4, PORT, dt)]), ('r2', [(4, PORT, dt), (2, PORT, dt)])):
                for k, off in enumerate((0, 0, 37, 128, 300)):
                    for f in (ff, fs):
                        f.t = (math.ceil(f.t * 44100 / 512) * 512 + off) / 44100
                    iso_seg(ff, fs, a, a + iv, dt, NORM, h, dict(kind='iso', type='n', hist=hn, a=a, b=a + iv, dt=dt, off=off, rep=k))
    print('i6_nf / i6_ns: %d segments, %.0f s each' % (len(ff.seg), ff.write(out)), fs.write(out))
    write_list(out, ['i6_nf', 'i6_ns'])


def iso8(out):
    """E8 (for d1 renders): runs at vel 64 throughout (the real context): U attacked, U -> W -> A -> B, every note held
    dt; W -> A interval 2 / 3 / 4 / 5 on the side away from B; A 57, B 59 / 60 / 55 / 54; segments block-aligned"""
    ff, fs = File('i8_nf'), File('i8_ns')
    for a, iv in ((57, 2), (57, 3), (57, -2), (57, -3)):
        for dt in (0.089, 0.15):
            for wd in (2, 3, 4, 5):
                for f in (ff, fs):
                    f.t = math.ceil(f.t * 44100 / 512) * 512 / 44100
                iso_seg(ff, fs, a, a + iv, dt, NORM, [(wd + 2, PORT, dt), (wd, NORM, dt)],
                        dict(kind='iso', type='n', hist='w%d' % wd, a=a, b=a + iv, dt=dt))
    print('i8_nf / i8_ns: %d segments, %.0f s each' % (len(ff.seg), ff.write(out)), fs.write(out))
    write_list(out, ['i8_nf', 'i8_ns'])

BLK = 512
IOI9, ATK9, END9, MID9 = 13, 26, 86, 260      # blocks: 150.9 ms (CC119 107), the attack 301.8 ms, last note 1.0 s, held 3 s
KEYS9 = list(range(55, 63))
INTS9 = (2, 3, 4, 5)


def blk(n):
    """block n's first frame as a time kthost puts on that frame (it truncates; +0.25 frame against the rounding)"""
    return (n * BLK + 0.25) / 44100


def chain9(f, keys, holds, **meta):
    """a chain on the block grid: keys[0] attacked (vel 5), the rest vel 64; holds in blocks (the last: its note-off)"""
    n0 = -(-int(round(f.t * 44100)) // BLK) + 1
    f.t = blk(n0)
    t0 = n0
    notes = []
    f.cc(blk(t0) - 0.4, 1, 64)
    for i, k in enumerate(keys):
        on = blk(t0)
        if i:
            f.cc(on, 119, cc119((on - notes[-1]['on']) * 1000))
            f.off(on + OVERLAP_S, keys[i - 1])
            notes[-1]['off'] = round(on + OVERLAP_S, 6)
        f.on(on, k, PORT if i == 0 else NORM)
        notes.append({'key': k, 'vel': PORT if i == 0 else NORM, 'on': round(on, 6), 'blk': t0})
        t0 += holds[i]
    f.off(blk(t0), keys[-1])
    notes[-1]['off'] = round(blk(t0), 6)
    seg = dict(notes=notes, end=round(blk(t0) + REST_S, 6), cc1=64)
    seg.update(meta)
    f.seg.append(seg)
    f.t = blk(t0) + REST_S
    return seg


def iso9(out, phrases=16, seed=20261018):
    """round-4 layer instrument, 150 ms class: held-out test phrases t9 (keys 55-62, intervals 2-5, an attack held
    26 blocks then notes every 13 blocks = 150.9 ms, CC119 107, the last held 86 blocks; all on Kontakt's 512-frame
    grid) and the layer recordings each needs, one file per destination key and direction (key-solo renders):
      run  U (attacked, 26) -> A (13) -> B: B's layer at CC119 107
      atk  K (attacked, 26) -> N: N's layer at CC119 88 (the phrase's second note), K's attack layer with and without N
    each 'mid' (destination held 3 s) and 'end' (held 86 blocks, then released)"""
    rnd = random.Random(seed)
    f = File('t9')
    need = set()
    for p in range(phrases):
        n = rnd.randint(7, 10)
        ks = [rnd.choice(KEYS9)]
        while len(ks) < n:
            k = ks[-1] + rnd.choice([-1, 1]) * rnd.choice(INTS9)
            if KEYS9[0] <= k <= KEYS9[-1]:
                ks.append(k)
        chain9(f, ks, [ATK9] + [IOI9] * (n - 2) + [END9], kind='test', phrase=p)
        need.add(('atk', None, ks[0], ks[1]))
        for i in range(2, n):
            need.add(('run', ks[i - 2], ks[i - 1], ks[i]))
    f.write(out)
    names = ['t9']
    groups = {}
    for kind, x, a, b in sorted(need, key=str):
        groups.setdefault((b, 'u' if b > a else 'd'), []).append((kind, a))
    man = []
    for (b, dr), lst in sorted(groups.items()):
        g = File('l9_b%d%s' % (b, dr))
        for kind, a in sorted(set(lst)):
            for var, hold in (('mid', MID9), ('end', END9)):
                if kind == 'run':
                    u = a - 3 * (1 if b > a else -1)
                    if u == b or abs(u - a) > 12: u = a + 3 * (1 if b > a else -1)
                    chain9(g, [u, a, b], [ATK9, IOI9, hold], kind='run', a=a, b=b, var=var)
                else:
                    chain9(g, [a, b], [ATK9, hold], kind='atk', a=a, b=b, var=var)
        g.write(out)
        names.append(g.name)
        man.append(dict(file=g.name, solo=[b, b - 24 if dr == 'u' else b + 24], b=b, dir=dr))
    akeys = sorted({a for kind, x, a, b in need if kind == 'atk'})
    for k in akeys:
        g = File('a9_k%d' % k)
        for kind, x, a, b in sorted(need, key=str):
            if kind == 'atk' and a == k:
                chain9(g, [k, b], [ATK9, MID9], kind='atkfull', a=k, b=b)
        chain9(g, [k], [ATK9 + MID9], kind='atkheld', a=k)
        g.write(out)
        names.append(g.name)
        man.append(dict(file=g.name, solo=[k], a=k))
    write_list(out, names)
    json.dump(man, open(os.path.join(out, 'manifest.json'), 'w'), indent=1)
    print('t9: %d phrases; %d layer files, %d attack files' % (phrases, len(groups), len(akeys)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('cmd', choices=['probe', 'probe2', 'probe3', 'pgrid', 'port', 'norm', 'bands', 'rel', 'test', 'iso', 'iso2', 'iso3', 'iso6', 'iso8', 'iso9'])
    ap.add_argument('--dts', default='0.5,0.3')
    ap.add_argument('out')
    ap.add_argument('--keys')
    ap.add_argument('--kind', default='port')
    ap.add_argument('--phrases', type=int, default=30)
    ap.add_argument('--seed', type=int, default=20261017)
    ap.add_argument('--twin', help='probe: twin.py\'s dir (its twin.txt / twin.json) for the setup check')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    rng = lambda d: list(range(int((a.keys or d).split('-')[0]), int((a.keys or d).split('-')[1]) + 1))  # noqa: E731
    if a.cmd == 'probe':
        probe(a.out)
        if a.twin:
            twin_cc(a.twin, a.out)
            write_list(a.out, ['p3probe', 'p3twin'])
    elif a.cmd == 'probe2':
        probe2(a.out)
    elif a.cmd == 'probe3':
        probe3(a.out)
        names = pgrid(a.out, rng('54-68'), [float(v) for v in a.dts.split(',')])
        write_list(a.out, ['p3fine'] + names)
    elif a.cmd == 'pgrid':
        write_list(a.out, pgrid(a.out, rng('54-68'), [float(v) for v in a.dts.split(',')]))
    elif a.cmd == 'port':
        port(a.out, rng('52-70'))
    elif a.cmd == 'norm':
        norm(a.out, rng('54-66'))
    elif a.cmd == 'bands':
        dts = [float(v) for v in a.dts.split(',')]
        write_list(a.out, band_files(a.out, rng('54-66'), [('n_%03d' % int(round(d * 1000)), d) for d in dts]))
    elif a.cmd == 'rel':
        rel(a.out)
    elif a.cmd == 'iso':
        iso(a.out)
    elif a.cmd == 'iso2':
        iso2(a.out)
    elif a.cmd == 'iso3':
        iso3(a.out)
    elif a.cmd == 'iso6':
        iso6(a.out)
    elif a.cmd == 'iso8':
        iso8(a.out)
    elif a.cmd == 'iso9':
        iso9(a.out)
    else:
        test(a.out, a.kind, rng('54-68' if a.kind == 'port' else '54-66'), a.phrases, a.seed)


if __name__ == '__main__':
    main()
