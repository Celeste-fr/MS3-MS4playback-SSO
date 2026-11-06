#!/usr/bin/env python3
"""Can the even-levels measurement of one edit be shortened to about a second? Two checks (2026-10-13, branch
even-fast-checks; the owner: "do the 2 checks"). Written for this check, not taken from anywhere. Why: the design for
measuring in the background near edits (HANDOFF.md › Even levels without measuring by hand) wants one pass instead of
three and one round instead of two or three, rendering only the edited note +-3 plus a run-in. Measurement only: the
renderer doesn't read the result (sso_even_fast_checks.json).

  even_fast_checks.py events <dump 12-TET> <dump quarter-tone> <work>
        kthost event files <work>/ev/<name>.txt + .json (layout), <work>/meta.json, list.txt, run.cmd
  even_fast_checks.py analyze <folder> <name>       (on the VM) <name>.wav -> <name>.res.json (each segment's envelope)
  even_fast_checks.py variants <work> <res folder>  check 2's corrected event files (batch 2) from batch 1's levels
  even_fast_checks.py round2 <work> <res folder>    a second round from batch 2's G1 / Gk3 (batch 3)
  even_fast_checks.py report <work> <res folder> [-o sso_even_fast_checks.json] [--times kthost logs folder]

Passages: the events MuseScore plays (tst_liveequivalence dumpEvents, integrate-1012, shipped map, Recommended:
MS_DUMP_SETTINGS = the preset's values, realRange and evenLevels on, no even-level values yet), one route each:
  W12C  Whence 12-TET bars 1-4, Celli - Performance (64 notes)
  QC    Whence (quarter tones) bars 1-8, Celli - Performance (120 notes)
  QV1   Whence bars 1-8, Violins 1 - Performance, lane 0 (56 notes, bars 5-8)
  QVa   Whence bars 1-8, Violas - Performance, lane 0 (88 notes, bars 3-8)
Every note of the three quarter-tone passages is on lane 0 at 0 cents (dump tunings.txt): no varispeed, the events
are exactly MuseScore's. Each passage is one joined run, the same notes, slurs and windows as the --even-levels job's
result for the same score (checked). A pass: the controllers in force 0.5 s before the first note-on, then the
passage's events, 3 s after its last note-off (the job's gap). Levels, slur statistics, glide fit: as
even_levels_test.py / evenlevels.h (perceived envelope, tools/playbackverify/loudness.py, mean over 136.4 ms from
note-on + the map's legatoDelay; a passage's first and last note left out of the slur statistics; fit ridge 0.5, 35 ms
smoothing, 5 ms grid).

Batch 1 (check 1, uncorrected N; per patch instance three kthost runs = separate processes): r1 8 passes of each
passage (Celli: W12C and QC alternating); r3 the same file again; r2 the passes in the other order (Celli: QC first)
with 0-3 test notes before some passes (as MuseScore's waitUntilSounding plays them while Kontakt loads: CC1 100, the
passage's median pitch at velocity 100 for 1.2 s, 2 s silence): 24 passes per passage. Plus a run f of fragments:
edited note j at 4 places per passage, rendered from r notes before j - 3 (r = 1, 2, 4) to 2 notes after j + 3 (the
run-out the window lag needs), 3 copies each, shuffled; notes j - 3 .. j + 3 measured against the full passage.
Batch 2 (check 2): glide corrections from batch 1, each rendered as 6 passes: G1 gain 1 from the median of r1's passes
1-3 (the job's round 1 as built); Gk3 the same scaled by 1/k; Gk1 scaled, from r1's pass 1 alone (one pass, one
round: the fast design); GkA scaled, from the median of all 24 passes (the best a single round can do). k = 1.28: the
median of the measured / predicted change (least squares slope over the notes) of the even-levels job's round 1 on
the VM (Whence 12-TET Celli 1.184, Whence Violins 1 1.414, Violas 1.349, Celli 1.22) and the listening test's G
(sso_even_levels_test.json, 1.282). Batch 3: a second round (the job's: fitted again on the round's own measurement,
divided by that round's measured gain) from G1 and Gk3.

Result (2026-10-13, Windows test VM, kthost + Kontakt 8 + SSO, 18 renders, 70 min of audio in ~19 min of Kontakt
time; sso_even_fast_checks.json). dB, p50 / p90 over notes:
  Check 1, one pass. |one pass - median of all 24|: W12C 0.29/0.77, QC 0.26/0.72, QV1 0.14/0.40, QVa 0.17/0.46; median of
  3 passes 0.18/0.49, 0.17/0.52, 0.09/0.28, 0.11/0.32. The slur range a correction from the measurement leaves (range of
  median of all - measured within each slur): one pass 0.81/1.43, 0.73/1.44, 0.40/0.70, 0.47/0.87; median of 3
  0.56/0.98, 0.50/1.02, 0.26/0.53, 0.32/0.58. The spread is random, per note, inside the plug-in: the same event file in
  another kthost process (r3 against r1, pass by pass) differs as much as two consecutive passes (Celli 0.38-0.42 /
  1.05, Violins 1 0.21, Violas 0.26 p50); the first pass after loading is no different (0.23-0.25 against later
  0.27-0.29; mean shift -0.11 .. +0.06); 1-3 test notes before a pass (MuseScore's load probe) change nothing. MuseScore
  itself is the same: the --even-levels job's own round-0 renders (3 passes, VM, build 38387460368) differ pass to pass
  by p50 0.33-0.43 (Celli) / 0.20-0.26 (Violins 1, Violas), three --verify-playback renders of the corrected Whence
  12-TET Celli by 0.45. So "MuseScore 0.6 dB against the scans' 0.1-0.25" is register / section (the Whence Celli are
  twice as noisy as the violins and violas, and the scans' 0.1-0.25 were 3-take means), not MuseScore, the first pass
  or round-robin history.
  Fragments (the edit window): run-in 1 note leaves note j-3 off by -0.25 dB mean (|d| 0.52/1.06); run-in 2 or 4: every
  note j-3..j+3 within the single-pass noise of the full passage (0.21/0.63 both, mean 0.00).
  Check 2, one round (slur range median / residual of the 6-pass median; noise floor of a 6-pass median 0.38, 0.30,
  0.15, 0.20; uncorrected 1.86/2.82, 2.29/2.89, 2.89/4.05, 2.06/2.79):
              W12C        QC          QV1         QVa
    G1        0.98/2.49   0.89/2.17   1.17/2.17   0.81/0.97   gain 1, median of 3 (the job's round 1)
    Gk3       0.93/1.31   0.91/1.30   0.58/1.56   0.61/1.03   / k, median of 3
    Gk1       1.15/1.57   0.88/2.15   0.58/1.54   0.73/1.21   / k, one pass (the fast design)
    GkA       0.90/1.18   0.74/1.05   0.70/1.15   0.45/1.14   / k, median of 24 (the best one round can do)
    G1 + 2    0.92/1.14   0.66/1.50   0.51/1.05   0.55/0.78   two rounds, the job's way
    Gk3 + 2   0.83/0.95   0.75/1.07   0.69/0.98   0.48/0.79
  Measured gain (measured / predicted window change) of G1 1.26-1.41 (Violins 1 1.41), of GkA 1.11-1.48: k = 1.28 holds.
  One round with k removes the overshoot (residual 2.2-2.5 -> 1.0-1.6) and its median range is within ~0.1-0.3 dB of
  two rounds; the largest note deviation stays ~0.3-0.6 dB above two rounds'. One pass instead of three costs ~0.2 dB
  of median range on the Celli (1.15 against 0.93 in W12C, equal elsewhere).
  Time: kthost renders 3.0-6.8x real time (median 4.4x) on the VM, one Kontakt instance. An edit window (0.5 s lead, 2
  run-in notes, the note +-3, 2 run-out notes, to the end of note j+3's window) at Whence's 136 ms notes is ~2.1 s of
  audio: ~0.5 s per pass; one pass, one round, applied without a check render: ~0.5 s of rendering per part (parts in
  parallel), plus the analysis.
"""
import argparse
import json
import math
import os
import random
import statistics as st
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))

WIN = 0.1364
STEP = 0.005
LEAD = 0.5
TAIL = 3.0
NPASS = 8
K = 1.28
GAINS = {'Whence 12-TET Celli (job)': 1.184, 'Whence Violins 1 (job)': 1.414, 'Whence Violas (job)': 1.349,
         'Whence Celli (job)': 1.22, 'listening test G': 1.282}
PASSAGES = [   # name, dump (0: 12-TET, 1: quarter tones), route, patch, bars
    ('W12C', 0, '0-7', 'Celli - Performance', 4),
    ('QC', 1, '1-1', 'Celli - Performance', 8),
    ('QV1', 1, '0-4', 'Violins 1 - Performance', 8),
    ('QVa', 1, '0-11', 'Violas - Performance', 8),
]
PATCHES = {'vc': 'Celli - Performance', 'vn1': 'Violins 1 - Performance', 'va': 'Violas - Performance'}
RUNINS = (1, 2, 4)
RUNOUT = 2
FRAG_COPIES = 3
VARIANTS = ('G1', 'Gk3', 'Gk1', 'GkA')
VPASS = 6


def db(v):
    return 20 * math.log10(v / 127.0)


def jload(*p):
    return json.load(open(os.path.join(*p)))


# --- events ---------------------------------------------------------------------------------------------------------

def passage(dump, route, patch, bars, info):
    import even_levels_test as elt
    bt = [float(x) for x in open(os.path.join(dump, 'bars.txt')).read().split()]
    notes, ev = elt.read_dump(os.path.join(dump, f'{route} museScore.txt'), bt[bars])
    notes = elt.windows(notes, info)
    t0 = max(0.0, notes[0]['on'] - LEAD)
    state, rel = {}, []
    for t, k, a, b in sorted(ev, key=lambda e: e[0]):
        if k == 'cc' and t < t0 - 1e-9:
            state[a] = b
        else:
            rel.append((round(t - t0, 6), k, a, b))
    rel = [(0.0, 'cc', a, b) for a, b in sorted(state.items())] + rel
    for n in notes:
        n['on'] = round(n['on'] - t0, 6)
        n['off'] = round(n['off'] - t0, 6)
    for k in range(1, len(notes)):
        if notes[k - 1]['off'] < notes[k]['on'] - 1e-4:
            raise SystemExit(f'{route}: note {k} not joined: more than one passage')
    end = max(n['off'] for n in notes)
    return dict(route=route, patch=patch, bars=bars, t0=t0, notes=notes, base=rel, len=round(end + TAIL, 4),
                pitch=int(st.median(n['p'] for n in notes)))


def frag_events(P, j, r):
    """events of notes j-3-r .. j+3+RUNOUT (controllers in force LEAD s before the first), shifted to start at LEAD;
    (events, the first note's index, the shift)"""
    a, b = j - 3 - r, j + 3 + RUNOUT
    notes = P['notes']
    t_a, t_b = notes[a]['on'] - LEAD, notes[b]['off']
    state, ev = {}, []
    for t, k, x, y in P['base']:
        if k == 'cc':
            if t < t_a - 1e-9:
                state[x] = y
            elif t <= t_b:
                ev.append((t - t_a, k, x, y))
    for i in range(a, b + 1):
        ev.append((notes[i]['on'] - t_a, 'on', notes[i]['p'], notes[i]['v']))
        ev.append((notes[i]['off'] - t_a, 'off', notes[i]['p'], 0))
    ev = [(0.0, 'cc', x, y) for x, y in sorted(state.items())] + ev
    return ev, a, t_a, round(t_b - t_a + 2.0, 4)


def test_notes(pitch, k):
    """MuseScore's waitUntilSounding, k times: CC1 100, the pitch at velocity 100 for 1.2 s, 0.4 s; then 2 s"""
    ev, t = [], 0.0
    for _ in range(k):
        ev += [(t, 'cc', 1, 100), (t, 'on', pitch, 100), (t + 1.2, 'off', pitch, 0)]
        t += 1.6
    return ev, (t + 2.0 if k else 0.0)


def write_file(work, name, patch, segs):
    """segs: [dict(kind, passage, events, len, pre (k test notes), ...)] laid out from 1.0 s; <name>.txt + .json"""
    import even_levels_test as elt
    t, ev, lay = 1.0, [], []
    for s in segs:
        pre, dt = test_notes(s['pitch'], s.get('pre', 0))
        ev += [(t + x, k, a, b) for x, k, a, b in pre]
        t += dt
        ev += [(t + x, k, a, b) for x, k, a, b in s['events']]
        d = {k: v for k, v in s.items() if k not in ('events',)}
        d['start'] = round(t, 5)
        lay.append(d)
        t += s['len']
    elt.write_events(os.path.join(work, 'ev', name + '.txt'), ev)
    json.dump(dict(name=name, patch=patch, length=round(t + 1.0, 3), segments=lay),
              open(os.path.join(work, 'ev', name + '.json'), 'w'))
    return t


RUN_CMD = r'''@echo off
rem even fast checks (branch even-fast-checks): renders list.txt (name|patch) with kthost, analyses each wav here and
rem deletes it (literal path); only C:\claude\efc is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\efc\st mkdir C:\claude\efc\st
if not exist C:\claude\efc\r mkdir C:\claude\efc\r
for %%f in ("Violins 1 - Performance" "Violas - Performance" "Celli - Performance") do if not exist "C:\claude\efc\st\%%~f.vst3state" copy /y "%SRC%\%%~f.vst3state" C:\claude\efc\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\efc\%1") do (
  if exist C:\claude\efc\stop.txt goto :end
  if not exist "C:\claude\efc\r\%%a.res.json" (
    echo %%a start !date! !time!>>C:\claude\efc\run.out
    C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\efc\st\%%b.vst3state" "C:\claude\efc\ev\%%a.txt" "C:\claude\efc\r\%%a.wav" --tail 3 --wait 600 > "C:\claude\efc\r\%%a.log" 2>&1
    "C:\Program Files\Python312\python.exe" C:\claude\efc\tools\soundlibraries\even_fast_checks.py analyze C:\claude\efc\r %%a >> "C:\claude\efc\r\%%a.log" 2>&1
    if exist "C:\claude\efc\r\%%a.res.json" del "C:\claude\efc\r\%%a.wav"
    echo %%a done !date! !time!>>C:\claude\efc\run.out
  )
)
:end
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE %1>>C:\claude\efc\run.out
'''


def write_list(work, fname, names):
    with open(os.path.join(work, fname), 'w', newline='\r\n') as f:
        f.write('\n'.join(f'{n}|{p}' for n, p in names) + '\n')


def events(dump12, dumpq, work):
    import transition_level_scan as tls
    info = tls.map_info(tls.MAP)
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    meta = {}
    for name, d, route, patch, bars in PASSAGES:
        meta[name] = passage((dump12, dumpq)[d], route, patch, bars, info[patch])
    json.dump(meta, open(os.path.join(work, 'meta.json'), 'w'))
    by = {sec: [n for n, _, _, p, _ in PASSAGES if p == patch] for sec, patch in PATCHES.items()}
    lst, total = [], 0.0
    pres = [1, 0, 2, 0, 3, 1, 0, 2]
    for sec, names in by.items():
        patch = PATCHES[sec]
        for run in ('r1', 'r2', 'r3'):
            order = names if run != 'r2' else names[::-1]
            segs = []
            for i in range(NPASS):
                for n in order:
                    P = meta[n]
                    segs.append(dict(kind='pass', passage=n, run=run, index=i, pre=pres[i] if run == 'r2' else 0,
                                     pitch=P['pitch'], events=P['base'], len=P['len']))
            total += write_file(work, f'b1_{sec}_{run}', patch, segs)
            lst.append((f'b1_{sec}_{run}', patch))
        segs = []
        for n in names:
            P = meta[n]
            nn = len(P['notes'])
            for frac in (0.2, 0.4, 0.6, 0.8):
                j = int(round(frac * (nn - 1)))
                j = min(max(j, 3 + max(RUNINS) + 1), nn - 1 - 3 - RUNOUT - 1)
                for r in RUNINS:
                    ev, a, t_a, ln = frag_events(P, j, r)
                    for c in range(FRAG_COPIES):
                        segs.append(dict(kind='frag', passage=n, j=j, runin=r, copy=c, first=a, shift=round(t_a, 6),
                                         pitch=P['pitch'], events=ev, len=ln))
        random.Random('efc' + sec).shuffle(segs)
        total += write_file(work, f'b1_{sec}_f', patch, segs)
        lst.append((f'b1_{sec}_f', patch))
    write_list(work, 'list1.txt', lst)
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(lst), 'files', round(total / 60, 1), 'min of audio')


# --- analyze (on the VM) --------------------------------------------------------------------------------------------

def analyze(folder, name):
    import soundfile as sf
    from loudness import perceived_envelope
    lay = json.load(open(os.path.join(folder, '..', 'ev', name + '.json')))
    x, sr = sf.read(os.path.join(folder, name + '.wav'), always_2d=True)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    for s in lay['segments']:
        ts = s['start'] + np.arange(-0.2, s['len'], STEP)
        s['env'] = [round(float(v), 2) for v in np.interp(ts, t_env, env)]
    lay['wavSeconds'] = round(len(x) / sr, 3)
    json.dump(lay, open(os.path.join(folder, name + '.res.json'), 'w'), separators=(',', ':'))
    print(name, len(lay['segments']), 'segments analysed')


# --- levels ---------------------------------------------------------------------------------------------------------

def seg_levels(P, s, first=0, count=None):
    """levels of notes first .. first+count-1 of P in segment s (a pass: first 0; a fragment: its own first)"""
    env = np.asarray(s['env'])
    notes = P['notes']
    count = len(notes) if count is None else count
    shift = s.get('shift', 0.0)
    out = []
    for i in range(first, first + count):
        a = notes[i]['on'] - shift + notes[i]['d']
        i0, i1 = int(round((a + 0.2) / STEP)), int(round((a + 0.2 + WIN) / STEP))
        out.append(float(np.mean(env[i0:i1])))
    return out


def included(P):
    return list(range(1, len(P['notes']) - 1))


def slur_stats(P, lv):
    by = {}
    for k in included(P):
        by.setdefault(P['notes'][k]['slur'], []).append(k)
    med = {s: st.median(lv[k] for k in ks) for s, ks in by.items()}
    ranges = [max(lv[k] for k in ks) - min(lv[k] for k in ks) for s, ks in sorted(by.items()) if len(ks) > 1]
    dev = {k: lv[k] - med[P['notes'][k]['slur']] for k in included(P)}
    return dict(median=med, ranges=ranges, rangeMedian=st.median(ranges), residual=max(abs(x) for x in dev.values()),
                dev=dev)


def load_res(res, prefix):
    out = []
    for fn in sorted(os.listdir(res)):
        if fn.startswith(prefix) and fn.endswith('.res.json'):
            out.append(jload(res, fn))
    return out


def passes_of(meta, rs, kind='pass'):
    """{passage: [(segment, levels)]} in file order"""
    out = {}
    for r in rs:
        for s in r['segments']:
            if s['kind'] == kind:
                P = meta[s['passage']]
                out.setdefault(s['passage'], []).append((dict(s, file=r['name']), seg_levels(P, s)))
    return out


# --- fit (even_levels_test.time_fit for G, one joined passage) -------------------------------------------------------

def grid(P):
    return np.arange(-0.2, P['len'], STEP)


def fit_glide(P, env, lv, lam=0.5):
    import even_levels_test as elt
    notes = P['notes']
    t = grid(P)
    s = slur_stats(P, lv)
    starts = np.array([n['on'] + n['d'] for n in notes])
    k = np.clip(np.searchsorted(starts, t, side='right') - 1, 0, None)
    target = np.array([s['median'].get(notes[i]['slur'], np.nan) for i in k])
    w = ((t >= starts[1]) & (t < notes[-1]['off']) & ~np.isnan(target)).astype(float)
    r = np.where(w > 0, np.nan_to_num(target) - np.asarray(env), 0.0)
    n = len(notes)
    M = np.zeros((len(t), n))
    for j in range(n):
        e = np.zeros(n)
        e[j] = 1.0
        M[:, j] = elt.smooth(elt.curve_db('G', e, notes, t))
    Wm = M * w[:, None]
    du = np.linalg.solve(Wm.T @ M + lam * np.eye(n), Wm.T @ r)
    return du, M


def window_change(P, M, du):
    """predicted change (dB) in each note's window for glide values du"""
    t = grid(P)
    g = M @ du
    out = []
    for n in P['notes']:
        a = n['on'] + n['d']
        m = (t >= a) & (t < a + WIN)
        out.append(float(g[m].mean()))
    return np.array(out)


def corrected_events(P, du):
    """the passage's events with CC11 = N's schedule (dB) + the glide of du, a message per 5 ms where it changes, the
    whole lowered to stay within 127 (as playback: the patch volume adds it back, a constant gain)"""
    notes = P['notes']
    base = [e for e in P['base'] if not (e[1] == 'cc' and e[2] == 11)]
    cc11 = sorted((t, v) for t, k, a, v in P['base'] if k == 'cc' and a == 11)
    cen = np.array([n['on'] + n['d'] + WIN / 2 for n in notes])
    end = max(n['off'] for n in notes)
    ts = sorted(set([round(x, 5) for x in np.arange(0.0, end + 0.3, STEP)] + [round(t, 5) for t, _ in cc11]))
    ts = np.array(ts)
    bt = np.array([t for t, _ in cc11])
    bv = np.array([db(max(v, 1)) for _, v in cc11])
    idx = np.clip(np.searchsorted(bt, ts + 1e-9, side='right') - 1, 0, None)
    total = bv[idx] + np.interp(ts, cen, du)
    shift = max(0.0, float(total.max()) - db(127.4))
    out, last = [], None
    for t, x in zip(ts, total):
        y = int(max(1, min(127, round(127 * 10 ** ((x - shift) / 20)))))
        if y != last:
            out.append((float(t), 'cc', 11, y))
            last = y
    return sorted(base + out, key=lambda e: e[0]), shift


def med_env(segs):
    return np.median([np.asarray(s['env']) for s in segs], axis=0)


def variants(work, res, prefix='b1_', out_prefix='b2_'):
    meta = jload(work, 'meta.json')
    p1 = passes_of(meta, [r for r in load_res(res, prefix) if not r['name'].endswith('_f')])
    vals = {}
    segs_by = {sec: [] for sec in PATCHES}
    for name, P in meta.items():
        allp = p1[name]
        r1 = [x for x in allp if x[0]['file'].endswith('_r1')]
        sources = {
            'G1': (r1[:3], 1.0), 'Gk3': (r1[:3], K), 'Gk1': (r1[:1], K), 'GkA': (allp, K),
        }
        vals[name] = {}
        for v, (src, gain) in sources.items():
            lv = list(np.median([x[1] for x in src], axis=0))
            env = med_env([x[0] for x in src])
            du, M = fit_glide(P, env, lv)
            du = du / gain
            ev, shift = corrected_events(P, du)
            vals[name][v] = dict(du=[round(float(x), 3) for x in du], shift=round(shift, 3), gain=gain,
                                 predicted=[round(float(x), 3) for x in window_change(P, M, du)],
                                 sourceLevels=[round(float(x), 3) for x in lv])
            sec = [s for s, p in PATCHES.items() if p == P['patch']][0]
            for i in range(VPASS):
                segs_by[sec].append(dict(kind='var', passage=name, variant=v, index=i, pitch=P['pitch'], events=ev,
                                         len=P['len']))
    lst = []
    for sec, segs in segs_by.items():
        # passes interleaved: pass i of every variant before pass i + 1 of any
        segs.sort(key=lambda s: (s['index'], s['passage'], VARIANTS.index(s['variant'])))
        write_file(work, f'{out_prefix}{sec}', PATCHES[sec], segs)
        lst.append((f'{out_prefix}{sec}', PATCHES[sec]))
    json.dump(vals, open(os.path.join(work, 'values.json'), 'w'))
    write_list(work, 'list2.txt', lst)
    print('wrote', lst)


def round2(work, res):
    """a second round from G1 and Gk3 (the job's: fitted on the round's own measurement, divided by its measured gain)"""
    meta = jload(work, 'meta.json')
    vals = jload(work, 'values.json')
    p2 = passes_of(meta, load_res(res, 'b2_'), kind='var')
    p0 = passes_of(meta, [r for r in load_res(res, 'b1_') if not r['name'].endswith('_f')])
    segs_by = {sec: [] for sec in PATCHES}
    for name, P in meta.items():
        r1 = [x for x in p0[name] if x[0]['file'].endswith('_r1')][:3]
        lv0 = np.median([x[1] for x in r1], axis=0)
        for v in ('G1', 'Gk3'):
            mine = [x for x in p2[name] if x[0]['variant'] == v]
            src = mine[:3]
            lv = np.median([x[1] for x in src], axis=0)
            env = med_env([x[0] for x in src])
            pred = np.array(vals[name][v]['predicted'])
            inc = included(P)
            x = pred[inc] - pred[inc].mean()
            y = (lv - lv0)[inc] - (lv - lv0)[inc].mean()
            gain = float((x * y).sum() / (x * x).sum()) if (x * x).sum() > 1e-9 and (x * y).sum() > 0 else 1.0
            du2, M = fit_glide(P, env, list(lv))
            du = np.array(vals[name][v]['du']) + du2 / gain
            ev, shift = corrected_events(P, du)
            vals[name][v + '2'] = dict(du=[round(float(z), 3) for z in du], shift=round(shift, 3), gain=round(gain, 3),
                                       predicted=[round(float(z), 3) for z in window_change(P, M, du)])
            sec = [s for s, p in PATCHES.items() if p == P['patch']][0]
            for i in range(VPASS):
                segs_by[sec].append(dict(kind='var', passage=name, variant=v + '2', index=i, pitch=P['pitch'], events=ev,
                                         len=P['len']))
    lst = []
    for sec, segs in segs_by.items():
        segs.sort(key=lambda s: (s['index'], s['passage'], s['variant']))
        write_file(work, f'b3_{sec}', PATCHES[sec], segs)
        lst.append((f'b3_{sec}', PATCHES[sec]))
    json.dump(vals, open(os.path.join(work, 'values.json'), 'w'))
    write_list(work, 'list3.txt', lst)
    print('wrote', lst)


# --- report ---------------------------------------------------------------------------------------------------------

def q(v, f):
    v = sorted(v)
    return round(float(v[min(len(v) - 1, int(f * len(v)))]), 2) if len(v) else None


def pq(v):
    return dict(p50=q(v, 0.5), p90=q(v, 0.9), n=len(v))


def check1(meta, res):
    rs = [r for r in load_res(res, 'b1_') if not r['name'].endswith('_f')]
    pp = passes_of(meta, rs)
    out = {}
    for name, P in meta.items():
        inc = included(P)
        segs = pp[name]
        L = np.array([lv for _, lv in segs])
        med = np.median(L, axis=0)
        info = [s for s, _ in segs]
        err = np.abs(L - med)[:, inc]
        first = [i for i, s in enumerate(info) if s['index'] == 0]
        later = [i for i, s in enumerate(info) if s['index'] > 0]
        runs = {}
        for i, s in enumerate(info):
            runs.setdefault(s['run'], []).append(i)
        # r1 vs r3: the same file in another process, pass by pass
        same = []
        for i in runs['r1']:
            for j2 in runs['r3']:
                if info[j2]['index'] == info[i]['index']:
                    same += list(np.abs(L[i] - L[j2])[inc])
        # within a run: consecutive passes
        within = []
        for rn, ix in runs.items():
            for a, b in zip(ix, ix[1:]):
                within += list(np.abs(L[a] - L[b])[inc])
        # pass 1 against the median of the run's later passes, and later passes against it
        p1dev, pldev, p1mean = [], [], []
        for rn, ix in runs.items():
            m = np.median(L[ix[1:]], axis=0)
            p1dev += list(np.abs(L[ix[0]] - m)[inc])
            p1mean.append(float(np.mean((L[ix[0]] - m)[inc])))
            for i in ix[1:]:
                rest = [x for x in ix[1:] if x != i]
                pldev += list(np.abs(L[i] - np.median(L[rest], axis=0))[inc])
        # test notes before the pass (r2) against none (r1, r3), same pass index: mean shift and |diff|
        pre = {}
        for i in runs['r2']:
            k = info[i]['pre']
            ref = [j2 for j2 in runs['r1'] + runs['r3'] if info[j2]['index'] == info[i]['index']]
            d = L[i] - np.mean(L[ref], axis=0)
            pre.setdefault(k, []).extend(list(np.abs(d)[inc]))
        # median of 3 consecutive passes against the median of all
        m3 = []
        for rn, ix in runs.items():
            for a in range(0, len(ix) - 2, 3):
                m3 += list(np.abs(np.median(L[ix[a:a + 3]], axis=0) - med)[inc])
        # the slur range a correction from one pass (or 3) would leave: range within each slur of (med - measured)
        def left(Lm):
            e = med - Lm
            by = {}
            for k in inc:
                by.setdefault(P['notes'][k]['slur'], []).append(e[k])
            return [max(v) - min(v) for v in by.values() if len(v) > 1]
        left1, left3, left6 = [], [], []
        for i in range(len(L)):
            left1 += left(L[i])
        for rn, ix in runs.items():
            for a in range(0, len(ix) - 2, 3):
                left3 += left(np.median(L[ix[a:a + 3]], axis=0))
            left6 += left(np.median(L[ix[:6]], axis=0))     # the noise floor of check 2's 6-pass measurements
        out[name] = dict(
            passes=len(L), notes=len(inc),
            singleVsMedianAll=pq(err.ravel()), firstPassVsMedianAll=pq(err[first].ravel()),
            laterPassesVsMedianAll=pq(err[later].ravel()), median3VsMedianAll=pq(m3),
            sameFileOtherProcess=pq(same), consecutivePasses=pq(within),
            firstPassVsLaterMedian=pq(p1dev), laterPassVsOtherLaterMedian=pq(pldev),
            firstPassMeanShift=[round(x, 2) for x in p1mean],
            testNotesBefore={str(k): pq(v) for k, v in sorted(pre.items())},
            slurRangeLeftOnePass=pq(left1), slurRangeLeftMedian3=pq(left3), slurRangeNoiseMedian6=pq(left6),
            uncorrected=dict(rangeMedian=round(slur_stats(P, list(med))['rangeMedian'], 2),
                             residual=round(slur_stats(P, list(med))['residual'], 2)),
            medianAll=[round(float(x), 2) for x in med])
    return out, pp


def fragments(meta, res, pp):
    rs = [r for r in load_res(res, 'b1_') if r['name'].endswith('_f')]
    out = {}
    noise = {}
    for name, segs in pp.items():
        L = np.array([lv for _, lv in segs])
        noise[name] = np.median(L, axis=0)
    for r in rs:
        for s in r['segments']:
            P = meta[s['passage']]
            j = s['j']
            lv = seg_levels(P, s, first=j - 3, count=7)
            med = noise[s['passage']][j - 3:j + 4]
            for pos, (a, b) in enumerate(zip(lv, med)):
                out.setdefault(s['runin'], {}).setdefault(pos - 3, []).append(a - b)
                out[s['runin']].setdefault('all', []).append(a - b)
    tab = {}
    for rn, d in sorted(out.items()):
        tab[str(rn)] = {str(k): dict(pq([abs(x) for x in v]), mean=round(float(np.mean(v)), 2)) for k, v in d.items()}
    return tab


def check2(meta, res, vals):
    pv = passes_of(meta, load_res(res, 'b2_') + load_res(res, 'b3_'), kind='var')
    out = {}
    for name, P in meta.items():
        out[name] = {}
        for v in sorted(set(s['variant'] for s, _ in pv.get(name, []))):
            L = np.array([lv for s, lv in pv[name] if s['variant'] == v])
            med = list(np.median(L, axis=0))
            ss = slur_stats(P, med)
            single = [slur_stats(P, list(x))['rangeMedian'] for x in L]
            out[name][v] = dict(passes=len(L), rangeMedian=round(ss['rangeMedian'], 2), rangeMax=round(max(ss['ranges']), 2),
                                residual=round(ss['residual'], 2), singlePassRangeMedian=pq(single),
                                shift=vals[name][v]['shift'], gain=vals[name][v]['gain'])
    return out


def measured_gain(meta, res, vals, pp):
    pv = passes_of(meta, load_res(res, 'b2_'), kind='var')
    out = {}
    for name, P in meta.items():
        inc = included(P)
        L0 = np.median([lv for _, lv in pp[name]], axis=0)
        for v in ('G1', 'GkA'):
            L = np.median([lv for s, lv in pv[name] if s['variant'] == v], axis=0)
            pred = np.array(vals[name][v]['predicted'])
            x = pred[inc] - pred[inc].mean()
            y = (L - L0)[inc] - (L - L0)[inc].mean()
            out.setdefault(name, {})[v] = dict(slope=round(float((x * y).sum() / (x * x).sum()), 3),
                                               r=round(float(np.corrcoef(x, y)[0, 1]), 3))
    return out


def times(logs):
    """kthost 'main render N ms' against the wav length, and the load (probe) times"""
    import re
    out = []
    for fn in sorted(os.listdir(logs)):
        if not fn.endswith('.log'):
            continue
        txt = open(os.path.join(logs, fn), errors='replace').read()
        m = re.search(r'main render (\d+) ms', txt)
        w = re.search(r'wrote .*\(([\d.]+) s\)', txt)
        p = re.search(r'probe \w+ after (\d+) ms', txt)
        if m and w:
            out.append(dict(file=fn[:-4], renderMs=int(m.group(1)), audioS=float(w.group(1)),
                            probeMs=int(p.group(1)) if p else None))
    return out


def report(work, res, out_path, logs):
    meta = jload(work, 'meta.json')
    vals = jload(work, 'values.json') if os.path.exists(os.path.join(work, 'values.json')) else {}
    c1, pp = check1(meta, res)
    fr = fragments(meta, res, pp)
    c2 = check2(meta, res, vals) if vals else {}
    mg = measured_gain(meta, res, vals, pp) if vals else {}
    tm = times(logs) if logs else []
    for name, d in c1.items():
        print(f"{name}: single |x-medAll| {d['singleVsMedianAll']['p50']}/{d['singleVsMedianAll']['p90']}  first "
              f"{d['firstPassVsMedianAll']['p50']}/{d['firstPassVsMedianAll']['p90']}  later {d['laterPassesVsMedianAll']['p50']}/"
              f"{d['laterPassesVsMedianAll']['p90']}  med3 {d['median3VsMedianAll']['p50']}/{d['median3VsMedianAll']['p90']}  "
              f"r1-r3 {d['sameFileOtherProcess']['p50']}/{d['sameFileOtherProcess']['p90']}  consecutive "
              f"{d['consecutivePasses']['p50']}/{d['consecutivePasses']['p90']}  p1 mean shift {d['firstPassMeanShift']}")
        print(f"     left slur range: one pass {d['slurRangeLeftOnePass']['p50']}/{d['slurRangeLeftOnePass']['p90']}, "
              f"median of 3 {d['slurRangeLeftMedian3']['p50']}/{d['slurRangeLeftMedian3']['p90']}; uncorrected "
              f"{d['uncorrected']}; test notes {json.dumps(d['testNotesBefore'])}")
    for rn, d in fr.items():
        print('fragments run-in', rn, 'all', d['all'], ' by position', {k: (v['p50'], v['p90'], v['mean']) for k, v in d.items() if k != 'all'})
    for name, d in c2.items():
        print(name, {v: (x['rangeMedian'], x['residual'], x['singlePassRangeMedian']['p50']) for v, x in d.items()})
    print('measured gain', mg)
    if tm:
        print('render x realtime', [round(t['audioS'] / (t['renderMs'] / 1000), 1) for t in tm])
    result = dict(
        _about='Even levels, two checks for measuring an edit in about a second (one pass, one round). '
               'tools/soundlibraries/even_fast_checks.py has the method and result. Measured 2026-10-13 on the Windows '
               'test VM: kthost (offline VST 3 host), Kontakt 8 + SSO, each Performance patch from MuseScore\'s resaved '
               'setup (library defaults), 44.1 kHz, the events MuseScore plays (Recommended). Levels: perceived '
               '(loudness.py, ~dB), mean over 136.4 ms from note-on + legatoDelay. check1: per passage, |level - x| '
               'p50/p90 over notes (first and last left out), x the median of all 24 passes; left: slur range (max - '
               'min within a slur) of (median of all - measured), what a correction from that measurement leaves. '
               'fragments: by run-in (notes rendered before the edited note - 3; 2 after the edited note + 3), level '
               'of notes j-3..j+3 minus the full passage\'s median of all. check2: per variant, the slur range '
               'median and residual of the 6-pass median, and per single pass. Measurement only: the renderer '
               'does not read this file.',
        k=K, gains=GAINS, passages={n: dict(route=P['route'], patch=P['patch'], bars=P['bars'], notes=len(P['notes']))
                                     for n, P in meta.items()},
        check1=c1, fragments=fr, check2=c2, measuredGain=mg,
        renderSpeed=[dict(file=t['file'], audioS=t['audioS'], renderS=round(t['renderMs'] / 1000, 2),
                          probeS=round(t['probeMs'] / 1000, 1) if t['probeMs'] is not None else None) for t in tm])
    json.dump(result, open(out_path, 'w'), separators=(',', ':'))
    print('wrote', out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('dump12')
    e.add_argument('dumpq')
    e.add_argument('work')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('name')
    v = sub.add_parser('variants')
    v.add_argument('work')
    v.add_argument('res')
    r2 = sub.add_parser('round2')
    r2.add_argument('work')
    r2.add_argument('res')
    r = sub.add_parser('report')
    r.add_argument('work')
    r.add_argument('res')
    r.add_argument('-o', default=os.path.join(HERE, 'sso_even_fast_checks.json'))
    r.add_argument('--logs')
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.dump12, o.dumpq, o.work)
    elif o.cmd == 'analyze':
        analyze(o.folder, o.name)
    elif o.cmd == 'variants':
        variants(o.work, o.res)
    elif o.cmd == 'round2':
        round2(o.work, o.res)
    else:
        report(o.work, o.res, o.o, o.logs)


if __name__ == '__main__':
    main()
