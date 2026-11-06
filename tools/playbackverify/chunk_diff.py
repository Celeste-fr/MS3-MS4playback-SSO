#!/usr/bin/env python3
"""chunk_diff.py: live playback's chunked render against the whole-score render (tst_liveequivalence chunkCompare).

    chunk_diff.py <score whole.txt> <score chunkN.txt> [--detail N] [--route R]

Each file: "# chunks utick:seconds ..." then one line an event, "utick seconds route type a b tuning flags note"
(route 0x10000 + port * 16 + channel for a library event). Compared per route:
  notes   every note-on (with its velocity and tuning, cents) and note-off, as a multiset of (utick, pitch, velocity,
          tuning) (before 2026-10-30 without the tuning: a note-on sent at another tuning passed; test_chunk_diff.py)
  state   each controller (CC n), the pitch bend and each plug-in parameter as the value in force over time: the
          intervals where the two renders' values differ (start, end, whole's, chunked's)
Normalised (the only one): a controller / bend / parameter event that sets the value already in force on its route is
dropped (a synth or plug-in changes nothing on it: Kontakt's CCs, the bend, Ample's parameters are values, not
triggers; a chunk restates its controllers at its start, renderMs4Dynamics / the lanes / libCarry). Its count is
reported. Order: events at one tick on one route in another order (after the normalisation) are counted (a switch after
its note-on would matter).
"""
import sys, collections, argparse

NOTEON, NOTEOFF, CTRL, PB, PARAM = 0x90, 0x80, 0xB0, 0xE0, 0x5


def read(path):
    bounds, ev = [], []
    with open(path) as f:
        for line in f:
            if line.startswith('# chunks'):
                bounds = [(int(b.split(':')[0]), float(b.split(':')[1])) for b in line.split()[2:]]
                continue
            p = line.split()
            if len(p) < 9:
                continue
            ev.append((int(p[0]), float(p[1]), int(p[2]), int(p[3]), int(p[4]), int(p[5]), float(p[6]), p[7], p[8]))
    return bounds, ev


def streams(ev):
    """per route: notes multiset, state change lists, effective per-tick order, redundant count"""
    notes = collections.defaultdict(collections.Counter)
    state = collections.defaultdict(list)          # (route, key) -> [(utick, secs, value)]
    inforce = {}
    order = collections.defaultdict(list)          # (route, utick) -> [kind...]
    redundant = collections.Counter()
    for u, s, r, t, a, b, tun, flags, note in ev:
        if t == NOTEON and b > 0:
            notes[r][(u, 'on', a, b, round(tun, 4))] += 1
            order[(r, u)].append(('on', a, b))
            continue
        if t == NOTEOFF or (t == NOTEON and b == 0):
            notes[r][(u, 'off', a, 0, 0.0)] += 1
            order[(r, u)].append(('off', a))
            continue
        if t == CTRL:
            key, val = ('cc', a), b
        elif t == PB:
            key, val = ('pb',), a | (b << 7) if b < 128 else a
        elif t == PARAM:
            key, val = ('param', a), round(tun, 6)
        else:
            key, val = ('t%d' % t, a), b
        k = (r, key)
        if inforce.get(k) == val:
            redundant[(r, key)] += 1
            continue
        inforce[k] = val
        state[k].append((u, s, val))
        order[(r, u)].append((key, val))
    return notes, state, order, redundant


def at(changes, u):
    v = None
    for cu, cs, cv in changes:
        if cu > u:
            break
        v = cv
    return v


def state_diffs(a, b):
    """intervals [u0, u1) where the values in force differ"""
    ticks = sorted(set([c[0] for c in a] + [c[0] for c in b]))
    secs = {c[0]: c[1] for c in a + b}
    out = []
    ia = ib = 0
    va = vb = None
    for i, u in enumerate(ticks):
        while ia < len(a) and a[ia][0] <= u:
            va = a[ia][2]; ia += 1
        while ib < len(b) and b[ib][0] <= u:
            vb = b[ib][2]; ib += 1
        if va != vb:
            end = ticks[i + 1] if i + 1 < len(ticks) else None
            out.append((u, secs[u], end, secs.get(end), va, vb))
    # merge touching intervals
    merged = []
    for d in out:
        if merged and merged[-1][2] == d[0]:
            m = merged[-1]
            merged[-1] = (m[0], m[1], d[2], d[3], m[4], m[5], max(m[6], magnitude(d[4], d[5])))
        else:
            merged.append(d + (magnitude(d[4], d[5]),))
    return merged


def magnitude(x, y):
    if x is None or y is None:
        return float('inf')
    return abs(x - y)


def nearest_bound(bounds, s):
    if not bounds:
        return None
    return min((abs(s - bs) * 1000.0, bu) for bu, bs in bounds)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('whole')
    ap.add_argument('chunked')
    ap.add_argument('--detail', type=int, default=5)
    ap.add_argument('--route', type=int, default=None)
    o = ap.parse_args(argv)
    wb, we = read(o.whole)
    cb, ce = read(o.chunked)
    wn, ws, wo, wr = streams(we)
    cn, cs, co, cr = streams(ce)
    print('whole: %d events, %d chunks; chunked: %d events, %d chunks' % (len(we), len(wb), len(ce), len(cb)))
    print('redundant (dropped) state events: whole %d, chunked %d' % (sum(wr.values()), sum(cr.values())))
    total = 0
    routes = sorted(set(wn) | set(cn) | set(r for r, k in ws) | set(r for r, k in cs))
    classes = collections.Counter()
    for r in routes:
        if o.route is not None and r != o.route:
            continue
        lines = []
        a, b = wn.get(r, collections.Counter()), cn.get(r, collections.Counter())
        only_w = a - b
        only_c = b - a
        if only_w or only_c:
            classes['notes'] += sum(only_w.values()) + sum(only_c.values())
            lines.append('  notes: %d only whole, %d only chunked' % (sum(only_w.values()), sum(only_c.values())))
            for (u, kind, p, v, c), n in sorted(only_w.items())[:o.detail]:
                lines.append('    whole   %8d %s %d v%d %+g c' % (u, kind, p, v, c))
            for (u, kind, p, v, c), n in sorted(only_c.items())[:o.detail]:
                lines.append('    chunked %8d %s %d v%d %+g c' % (u, kind, p, v, c))
        keys = sorted(set(k for rr, k in ws if rr == r) | set(k for rr, k in cs if rr == r))
        for k in keys:
            d = state_diffs(ws.get((r, k), []), cs.get((r, k), []))
            if not d:
                continue
            classes[k[0] if k[0] != 'cc' else 'cc%d' % k[1]] += len(d)
            lines.append('  %s: %d intervals differ, largest %s' % (k, len(d), max(x[6] for x in d)))
            for u0, s0, u1, s1, va, vb, m in d[:o.detail]:
                nb = nearest_bound(cb, s0)
                lines.append('    %8d (%.3f s) .. %s: whole %s, chunked %s (%.0f ms after/before bound %s)'
                             % (u0, s0, u1, va, vb, nb[0] if nb else -1, nb[1] if nb else '-'))
        # order within ticks (same set, other order)
        nord = 0
        for (rr, u), seq in wo.items():
            if rr != r:
                continue
            other = co.get((rr, u))
            if other is not None and other != seq and sorted(map(str, other)) == sorted(map(str, seq)):
                nord += 1
                if nord <= o.detail:
                    lines.append('    order at %d: whole %s / chunked %s' % (u, seq, other))
        if nord:
            classes['order'] += nord
            lines.insert(0, '  order: %d ticks' % nord)
        if lines:
            total += 1
            print('route %d (%s)' % (r, 'port %d ch %d' % ((r - 0x10000) // 16, (r - 0x10000) % 16) if r >= 0x10000 else 'synth'))
            print('\n'.join(lines))
    print('routes differing: %d; by class: %s' % (total, dict(classes)))
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
