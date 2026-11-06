#!/usr/bin/env python3
"""The context reuse scan (2026-10-13, branch context-reuse): in SSO's five string sections' Performance legato, is a
slurred note's level fixed by its exact local context (so a table measured once could be reused), and does a context
keep its level shape when transposed? Written for this check, not taken from anywhere. Why: perturbation_scan.py found
a note's level depends on ~2 notes before and 1 after (Basses ~4 before), but an earlier offline look at the
validation runs (the same note in the 'same' context in different phrases) still spread ~1.3 dB. Measurement only: the
renderer doesn't read the result (sso_context_reuse.json).

  context_reuse_scan.py events <work>              kthost event files <work>/ev/<name>.txt + .json, list.txt, run.cmd
  context_reuse_scan.py analyze <folder> <name>    (on the VM) <name>.wav -> <name>.res.json (perturbation_scan's)
  context_reuse_scan.py report <res folder> [-o sso_context_reuse.json]

Phrases as transition_level_scan.py run_phrases plays them, five slurs instead of four: a spiccato note (112) then
five slurs of four 136 ms notes (each slur's first note a bowed transition 93, the rest fingered 69, the note before
off 24 ms after the next note-on), CC1 64, CC11 127, 1.2 s between phrases. Notes 0 (the attack) to 20.
Windows: per section six 5-note windows w0 w1 T a1 a2 (2 before the target T, 2 after), by intervals and the target's
place in its slur (WINDOWS: Whence-like steps, a leap into T, a leap out of T; T the 2nd, 3rd or 4th note of its
slur), at a fraction of the section's range (Celli's first: Whence 12-TET bar 2's 53 51 52 51 49 shape). A window is
put at one of two places in the phrase (T = note 10 or 14 for a 2nd note, 7 / 11 for a 3rd, 8 / 12 for a 4th: at
least 5 notes before w0, 4 after a2), the rest of the phrase a fresh random walk (steps +-1 .. +-5, +-7; fixed seed)
per embedding, the note just before w0 and just after a2 different in every embedding: same pitches, velocities,
lengths and slur position inside the window, different surroundings.
  shift 0   5 embeddings (e0 e2 e4 at the first place, e1 e3 at the second), each twice (copies 0 / 1: the noise
            floor, the same phrase at another place in the same render)
  +1 +2 +5  the whole phrase of e0 and e1 transposed (the window's range fraction leaves room)
96 phrases per render, one render per section x take; takes A (shuffled, fixed seed), B (A reversed), C (another
shuffle): 15 renders.
Level of a note: perturbation_scan.py's 'swell' (mean perceived loudness, tools/playbackverify/loudness.py, ~dB, over
136.4 ms from its note-on + the map's legatoDelay for its interval). 'shape' = a window note's level minus the median
of its window's five levels in the same phrase. Report: (a) for every window note, |level(e_i) - level(e_j)| over pairs
of embeddings (copy 0, shift 0) against the noise |copy 1 - copy 0|, single take and 3-take means, absolute and shape;
same-place and other-place pairs apart. (b) shape(shift s) - shape(shift 0), same embedding, 3-take means, against
the shape noise; the absolute offset (window mean, shift - original).

Result (2026-10-13, Windows VM, Kontakt 8 + SSO via kthost, 15 renders, 98 min of audio in 29 min). |diff| p50 / p90
dB, all sections, 3-take means (single takes in brackets):
  (a) the same window in other surroundings   T 0.23/0.64 (0.32/0.85)   inner w1 T a1 0.26/0.91   noise 0.13/0.35
      by window note: w0 0.95/2.76, w1 0.40/1.26, T 0.23/0.64, a1 0.20/0.75, a2 0.96/2.40 (noise 0.10-0.14 /
      0.34-0.44): w0 and a2, whose outside neighbours changed, move ~1 dB; T and a1, whose 2 before and 1 after are
      inside the window, stay within ~0.1 dB of the noise at p50 (a context SD of ~0.2 dB, Basses' p90 the widest);
      the window's place in the phrase doesn't matter (same place 0.22/0.59, other place 0.23/0.66).
  (b) the same phrase transposed +1 / +2 / +5   T abs 0.91/2.01, 1.18/2.54, 1.34/2.75; T shape (minus the window's
      median) 0.74/1.77, 0.81/2.11, 0.72/1.96 against noise 0.11/0.45 and other surroundings 0.30/1.45; the window's
      mean moves by -0.6 .. +0.0 dB on average (SD 0.8-1.4 per window). One semitone already changes the shape as
      much as five: per-pitch sample differences, not a smooth trend.
Verdict: a note's level is fixed by its exact local context (2 before, 1-2 after, their velocities, absolute pitches)
to ~0.2 dB, so a lookup table works; but transposition doesn't keep the shape, so pitch can't be dropped or
interpolated: every absolute pitch context is its own cell (perturbation_scan.py's 1.4-2.8 M cells per section stand).
The earlier offline ~1.3 dB spread (the validation runs, the same pitches in different phrases) came from keys that
left out the neighbours' velocities, i.e. the slur position (bowed 93 vs fingered 69): on those runs, pairs from
different phrases with the same 1 before .. 1 after pitches and target velocity differ by p50 0.67 dB (79 pairs), with
all three velocities matched 0.30 (37 pairs); 2 before .. 1 after, target velocity only 1.35 (8), all velocities 0.17
(3); the table cell alone (1 before + target) 1.17 (716). Those runs' take noise was SD 0.25 dB.
"""
import argparse
import itertools
import json
import os
import random
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
from transition_level_scan import (SECTIONS, RANGES, FINGERED, BOWED, SPICCATO, OVERLAP, MAP, avoid, map_info,
                                   legato_delay)
import perturbation_scan

TAKES = ('A', 'B', 'C')
NOTE, GAP, NNOTES = 0.136, 1.2, 21
SHIFTS = (1, 2, 5)
NEMB = 5
STEPS = [1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 1, -1, 2, -2, 7, -7]
# (intervals w0->w1, w1->T, T->a1, a1->a2; T's place in its slur 1..3; range fraction of w0; what)
WINDOWS = [((-2, 1, -1, -2), 1, 0.45, 'steps (Whence bar 2 shape)'),
           ((2, 1, -1, -2), 2, 0.30, 'steps up and back'),
           ((-1, 7, -2, -1), 3, 0.40, 'leap up into T'),
           ((2, -2, 1, 2), 1, 0.55, 'steps'),
           ((-1, -2, 3, -5), 2, 0.60, 'leap down out of T'),
           ((1, -5, 2, 1), 3, 0.35, 'leap down into T')]
PLACES = {1: (10, 14), 2: (7, 11), 3: (8, 12)}


def window_pitches(sec, w):
    ivs, pos, frac, _ = WINDOWS[w]
    lo, hi = RANGES[sec]
    rel = [0]
    for i in ivs:
        rel.append(rel[-1] + i)
    if sec == 'vc' and w == 0:
        start = 53
    else:
        top = hi - max(SHIFTS) - max(rel) - 2
        bot = lo + 2 - min(rel)
        start = round(bot + frac * (top - bot))
    return [start + r for r in rel]


def velocities():
    return [SPICCATO] + [BOWED if k % 4 == 0 else FINGERED for k in range(NNOTES - 1)]


def walk(rnd, start, n, lo, hi, avoid_first):
    out, p = [], start
    while len(out) < n:
        q = p + rnd.choice(STEPS)
        if lo <= q <= hi and q != p and not (not out and q in avoid_first):
            out.append(q)
            p = q
    return out


def phrases(sec):
    """[(window, embedding, shift, copy, place T, pitches, velocities)]"""
    lo, hi = RANGES[sec]
    hi0 = hi - max(SHIFTS)
    out = []
    for w in range(len(WINDOWS)):
        win = window_pitches(sec, w)
        pos = WINDOWS[w][1]
        rnd = random.Random(f'ctx{sec}{w}')
        before_used, after_used = set(), set()
        for e in range(NEMB):
            t = PLACES[pos][e % 2]
            nb, na = t - 2, NNOTES - (t + 3)
            bef = walk(rnd, win[0], nb, lo, hi0, before_used | {win[1]})[::-1]
            aft = walk(rnd, win[-1], na, lo, hi0, after_used | {win[-2]})
            before_used.add(bef[-1])
            after_used.add(aft[0])
            ps = bef + win + aft
            assert len(ps) == NNOTES and all(ps[i] != ps[i + 1] for i in range(NNOTES - 1)), ps
            vel = velocities()
            vel[0] = avoid(sec, ps[0], SPICCATO)
            for c in (0, 1):
                out.append((w, e, 0, c, t, ps, vel))
            if e < 2:
                for s in SHIFTS:
                    pp = [p + s for p in ps]
                    assert lo <= min(pp) and max(pp) <= hi, (sec, w, pp)
                    vv = list(vel)
                    vv[0] = avoid(sec, pp[0], SPICCATO)
                    out.append((w, e, s, 0, t, pp, vv))
    return out


def take_order(sec, take):
    ps = phrases(sec)
    order = list(range(len(ps)))
    if take in ('A', 'B'):
        random.Random('ctxA' + sec).shuffle(order)
        if take == 'B':
            order.reverse()
    else:
        random.Random('ctxC' + sec).shuffle(order)
    return [ps[i] for i in order]


RUN_CMD = perturbation_scan.RUN_CMD.replace('pts', 'crs').replace('perturbation_scan.py', 'context_reuse_scan.py') \
    .replace('perturbation scan (branch perturbation-scan)', 'context reuse scan (branch context-reuse)')


def events(work):
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    lst, total = [], 0.0
    for take in TAKES:
        for sec, patch in SECTIONS.items():
            ev = [(0.0, 'cc', 1, 64), (0.0, 'cc', 11, 127)]
            notes, t = [], 1.0
            for w, e, s, c, tt, ps, vels in take_order(sec, take):
                ons = []
                for k, (p, v) in enumerate(zip(ps, vels)):
                    last = k == len(ps) - 1
                    ev.append((t, 'on', p, v))
                    ev.append((t + NOTE + (0 if last else OVERLAP), 'off', p, 0))
                    ons.append(round(t, 5))
                    t += NOTE
                notes.append(dict(window=w, emb=e, shift=s, copy=c, target=tt, pitches=ps, vels=vels, ons=ons))
                t += GAP
            fn = f'{take}_{sec}'
            ev.sort(key=lambda x: (x[0], {'cc': 0, 'off': 1, 'on': 2}[x[1]]))
            with open(os.path.join(work, 'ev', fn + '.txt'), 'w', newline='\r\n') as f:
                for x in ev:
                    f.write(f'{x[0]:.5f} {x[1]} {x[2]} {x[3]}\n')
            length = max(x[0] for x in ev) + 3
            json.dump(dict(patch=patch, section=sec, take=take, length=length, notes=notes),
                      open(os.path.join(work, 'ev', fn + '.json'), 'w'))
            lst.append(f'{fn}|{patch}')
            total += length
    with open(os.path.join(work, 'list.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lst) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(lst), 'files', round(total / 60, 1), 'min of audio')


# --- report ---------------------------------------------------------------------------------------------------------

q = perturbation_scan.q
POSN = ('w0', 'w1', 'T', 'a1', 'a2')


def pq(v):
    return dict(n=len(v), p50=round(q(v, 0.5), 2), p90=round(q(v, 0.9), 2)) if v else None


def report(res, out_path, map_path):
    infos = map_info(map_path)
    L = {}      # (sec, w, e, s, c) -> take -> 5 window levels
    meta = {}
    for fn in sorted(os.listdir(res)):
        if not fn.endswith('.res.json'):
            continue
        d = json.load(open(os.path.join(res, fn)))
        info = infos[d['patch']]
        for n in d['notes']:
            lv = perturbation_scan.levels(n, info)
            t = n['target']
            L.setdefault((d['section'], n['window'], n['emb'], n['shift'], n['copy']), {})[d['take']] = \
                lv[t - 2:t + 3]
            meta.setdefault(d['section'], {}).setdefault(str(n['window']), {})[f"e{n['emb']}s{n['shift']}"] = dict(
                pitches=n['pitches'], target=t)
    secs = sorted({k[0] for k in L}, key=list(SECTIONS).index)

    def shape(v):
        m = st.median(v)
        return [x - m for x in v]

    def mean_take(d, tk):
        return [st.fmean(d[t][i] for t in tk) for i in range(5)]
    A = {}

    def add(*key_val):
        *key, v = key_val
        A.setdefault(tuple(key), []).append(abs(v))
    for sec in secs:
        for w in range(len(WINDOWS)):
            embs = [e for e in range(NEMB) if (sec, w, e, 0, 0) in L and (sec, w, e, 0, 1) in L]
            tk = sorted(set.intersection(*(set(L[(sec, w, e, 0, c)]) for e in embs for c in (0, 1))))
            for kind, f in (('abs', lambda v: v), ('shape', shape)):
                # noise: copy 1 - copy 0
                for e in embs:
                    for t in tk:
                        a, b = f(L[(sec, w, e, 0, 0)][t]), f(L[(sec, w, e, 0, 1)][t])
                        for i in range(5):
                            add(sec, kind, 'single', 'noise', i, b[i] - a[i])
                    a, b = f(mean_take(L[(sec, w, e, 0, 0)], tk)), f(mean_take(L[(sec, w, e, 0, 1)], tk))
                    for i in range(5):
                        add(sec, kind, 'mean', 'noise', i, b[i] - a[i])
                # between embeddings, copy 0 (and copy 1, the same pairs again)
                for e1, e2 in itertools.combinations(embs, 2):
                    place = 'same' if e1 % 2 == e2 % 2 else 'other'
                    for c in (0, 1):
                        for t in tk:
                            a, b = f(L[(sec, w, e1, 0, c)][t]), f(L[(sec, w, e2, 0, c)][t])
                            for i in range(5):
                                add(sec, kind, 'single', 'emb', i, b[i] - a[i])
                                add(sec, kind, 'single', place, i, b[i] - a[i])
                        a, b = f(mean_take(L[(sec, w, e1, 0, c)], tk)), f(mean_take(L[(sec, w, e2, 0, c)], tk))
                        for i in range(5):
                            add(sec, kind, 'mean', 'emb', i, b[i] - a[i])
                            add(sec, kind, 'mean', place, i, b[i] - a[i])
                # transposed against the original, same embedding, 3-take means (original: both copies' mean)
                for e in (0, 1):
                    if (sec, w, e, 0, 0) not in L:
                        continue
                    o = [(x + y) / 2 for x, y in zip(mean_take(L[(sec, w, e, 0, 0)], tk),
                                                     mean_take(L[(sec, w, e, 0, 1)], tk))]
                    for s in SHIFTS:
                        k = (sec, w, e, s, 0)
                        if k not in L or not set(tk) <= set(L[k]):
                            continue
                        m = mean_take(L[k], tk)
                        a, b = f(o), f(m)
                        for i in range(5):
                            add(sec, kind, 'mean', f'shift{s}', i, b[i] - a[i])
                        if kind == 'abs':
                            A.setdefault((sec, 'offset', s), []).append(st.fmean(m) - st.fmean(o))
    allsecs = ['all'] + secs

    def summ(sec, *key):
        v = []
        for s in (secs if sec == 'all' else [sec]):
            v += A.get((s,) + key, [])
        return pq(v)
    table = {}
    for sec in allsecs:
        for kind in ('abs', 'shape'):
            for avg in ('single', 'mean'):
                for what in ('noise', 'emb', 'same', 'other') + tuple(f'shift{s}' for s in SHIFTS):
                    for i in range(5):
                        r = summ(sec, kind, avg, what, i)
                        if r:
                            table.setdefault(sec, {}).setdefault(kind, {}).setdefault(avg, {}).setdefault(
                                what, {})[POSN[i]] = r
                    inner = []
                    for s in (secs if sec == 'all' else [sec]):
                        for i in (1, 2, 3):
                            inner += A.get((s, kind, avg, what, i), [])
                    if inner:
                        table[sec][kind][avg][what]['inner'] = pq(inner)
        for s in SHIFTS:
            v = []
            for x in (secs if sec == 'all' else [sec]):
                v += A.get((x, 'offset', s), [])
            if v:
                table.setdefault(sec, {}).setdefault('offset', {})[f'shift{s}'] = dict(
                    n=len(v), mean=round(st.fmean(v), 2), sd=round(st.pstdev(v), 2),
                    absP50=round(q([abs(x) for x in v], 0.5), 2))
    f2 = lambda r: f'{r["p50"]:.2f}/{r["p90"]:.2f}' if r else '   -    '
    for kind in ('abs', 'shape'):
        for avg in ('single', 'mean'):
            print(f'\n{kind} {avg}: |diff| p50/p90 dB   noise | between embeddings (same place, other place) | '
                  f'shift +1 +2 +5 (mean only)')
            for sec in allsecs:
                tb = table[sec][kind][avg]
                for pn in ('T', 'inner'):
                    g = lambda wh: tb.get(wh, {}).get(pn)
                    print(f'  {sec:4s} {pn:5s} {f2(g("noise"))} | {f2(g("emb"))} ({f2(g("same"))}, '
                          f'{f2(g("other"))}) | ' + ' '.join(f2(g(f'shift{s}')) for s in SHIFTS))
    print('\nby window position (all, abs, 3-take means): noise | embeddings')
    for pn in POSN:
        tb = table['all']['abs']['mean']
        print(f'  {pn:3s} {f2(tb["noise"][pn])} | {f2(tb["emb"][pn])}   shape: '
              f'{f2(table["all"]["shape"]["mean"]["noise"][pn])} | {f2(table["all"]["shape"]["mean"]["emb"][pn])}')
    print('\noffset (window mean, transposed - original, 3-take means): mean sd |.|p50')
    for sec in allsecs:
        print(f'  {sec:4s} ' + '  '.join(f'+{s}: {table[sec]["offset"][f"shift{s}"]["mean"]:+.2f} '
                                          f'{table[sec]["offset"][f"shift{s}"]["sd"]:.2f} '
                                          f'{table[sec]["offset"][f"shift{s}"]["absP50"]:.2f}' for s in SHIFTS))
    levels = {f'{k[0]}|w{k[1]}|e{k[2]}|s{k[3]}|c{k[4]}': v for k, v in L.items()}
    result = dict(
        _about='SSO string Performance patches: is a slurred note\'s level fixed by its exact local context, and '
               'does a context keep its level shape when transposed? tools/soundlibraries/context_reuse_scan.py (its '
               'header has the method and result). Measured 2026-10-13 on the Windows test VM: kthost (offline VST 3 '
               'host), Kontakt 8 + SSO, each patch from MuseScore\'s resaved setup (library defaults), 44.1 kHz. '
               'levels: "section|window|embedding|shift|copy" -> take -> the swell levels (perceived loudness, '
               'tools/playbackverify/loudness.py, ~dB) of the window\'s five notes w0 w1 T a1 a2. table: per section '
               'and all, abs / shape (level minus the window\'s median, same phrase), single take / 3-take means, '
               '|diff| p50 / p90 per window note and inner (w1 T a1): noise (copy 1 - copy 0), emb (between '
               'embeddings; same / other place in the phrase), shift1/2/5 (transposed - original, same embedding); '
               'offset: window mean, transposed - original. Measurement only: the renderer does not read this file.',
        method=dict(note=NOTE, overlap=OVERLAP, gap=GAP, notes=NNOTES, cc1=64, cc11=127, shifts=SHIFTS,
                    embeddings=NEMB, takes=TAKES, places=PLACES,
                    windows=[dict(intervals=w[0], targetSlurPlace=w[1], rangeFraction=w[2], what=w[3])
                             for w in WINDOWS],
                    velocities=dict(spiccato=SPICCATO, bowed=BOWED, fingered=FINGERED)),
        phrases=meta, levels=levels, table=table)
    json.dump(result, open(out_path, 'w'), separators=(',', ':'))
    print('wrote', out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('work')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('name')
    r = sub.add_parser('report')
    r.add_argument('res')
    r.add_argument('-o', default=os.path.join(HERE, 'sso_context_reuse.json'))
    r.add_argument('--map', default=MAP)
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.work)
    elif o.cmd == 'analyze':
        perturbation_scan.analyze(o.folder, o.name)
    else:
        report(o.res, o.o, o.map)


if __name__ == '__main__':
    main()
