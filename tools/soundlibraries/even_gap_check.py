#!/usr/bin/env python3
"""How short may the silence after each even-levels measuring pass be? (2026-10-13, branch even-auto; the owner: "the 3 s
silence after each measuring pass may shrink to the length the VM shows doesn't change results"). Written for this
check, not taken from anywhere. Measurement only: the renderer reads the chosen gap from evenlevels.h (GAP_S), not from
this file's output (sso_even_gap_check.json).

  even_gap_check.py events <dump 12-TET> <dump quarter-tone> <work> [--renders 8] [--basses]
        kthost event files <work>/ev/<name>.txt + .json, <work>/meta.json, list.txt, run.cmd
  even_gap_check.py analyze <folder> <name>      (on the VM) <name>.wav -> <name>.res.json (each pass's envelopes)
  even_gap_check.py report <work> <res folder> [-o sso_even_gap_check.json] [--logs <kthost logs folder>]

What the background measurer renders (libmscore/evenauto.cpp makeJob, audio/vst3/evenmeasure.cpp render): a window
of an edited note j, notes j-3 .. j+3 measured, RUN_IN notes before (2; Basses 4) and 2 after rendered with their
values held; a pass = the transport restarted (Vst3Plugin::restartTransport: kthost's "tr" event, one 64-frame
block with the playing flag off), the controllers in force 0.5 s before the first note-on, the window's events, then
silence: the gap after the last note-off; 3 passes in a row; the jobs of one plug-in instance one after another.
The same here: the passages of even_fast_checks.py (MuseScore's events, tst_liveequivalence dumpEvents, Recommended,
realRange and evenLevels on, no values yet; Whence 12-TET bars 1-4 Celli, Whence bars 1-8 Celli / Violins 1 / Violas
lane 0), 4 windows each (j at 0.2 .. 0.8 of the passage), every window rendered as the measurer renders it, with
gaps 0.25 / 0.5 / 1 / 1.5 / 2 / 3 s. One kthost process per section, gap and half (A / B), each holding --renders / 2
renders of every window (the windows' order shuffled per render), so a gap's renders follow renders of the same gap,
as in the measurer. SSO varies each note from play to play (~0.2-0.3 dB: the patch's random modulators), so each
gap gets several renders; a gap's effect is what its mean differs from the 3 s gap's beyond that noise.

Per render (3 passes): each note's level as the job measures it (perceived envelope, tools/playbackverify/loudness.py,
mean over 136.4 ms from note-on + legatoDelay, or the smooth attack's onset after silence; the median over the 3
passes) and the glide the job would fit to it (even_fast_checks.fit_glide with the run-in / run-out held, the
measured notes only). Criterion (stated before the run): a gap matches 3 s when, over all measured notes of all
windows of a section, (1) the mean squared z of (mean at gap g - mean at 3 s) / its standard error stays under the
chi-square 95 % bound for that many notes, and (2) the mean shift over notes stays within its 95 % interval, for the
levels and for the fitted corrections; the smallest gap that matches, with every longer gap matching too, is chosen.
Changed after the first batch: the standard errors were first taken from the renders' spread, but that test failed 3 s
against itself (the 3 s A / B halves: Violas z^2 1.85, Celli 1.34 over their bounds): a kthost process shifts every note a
little, so the process is the unit of noise (compare_files: per window the variance of a process mean pooled over the
gaps, the shift's interval by Student's t on the pooled degrees of freedom; selfCheck runs the same test within each gap),
and Violas / Celli got 4 more processes at 1-3 s. Release tail: the power envelope (5 ms, dB) after the last note-off of
the 3 s passes, against the level while the window's notes play (the median power over the measured notes' windows): the
time until it stays below -40 / -50 / -60 dB re that level.

Also (the owner's additions): early stop (2 passes, a 3rd only when a measured note differs by more than the 95th
percentile of |pass 1 - pass 2|; each render's fit against the other renders' 3-pass fits, bootstrap over windows) and
the render speed (per gap and section; sp_* files: the 3 s files again in blocks of 512 / 2048 / 4096, after 120 s idle,
two processes at once; kthost options --block, --idle, --probe-pitch added for this, "tr" events for the transport).

Result (2026-10-13, Windows test VM, kthost + Kontakt 8 + SSO; 91 kthost processes, 6.25 h of audio in 38 min of
rendering; sso_even_gap_check.json). Chosen gap: Violins 1, Violas, Celli 1.0 s, Basses 1.5 s (every longer gap matches 3 s;
0.25 / 0.5 s do not for any section: effect ~0.08-0.2 dB rms on levels and fits; Basses 1.0 s marginal, z^2 1.50 / 1.48).
Release: -40 dB re the notes' level 2.4-3.0 s after the last note-off (p50), -50 / -60 not reached above the envelope's
floor within 3 s. Early stop: thresholds 0.67 / 0.74 / 1.26 / 1.49 dB (Violins 1 / Violas / Celli / Basses), 69-75 % of
renders stop at 2 passes (23-25 % of passes saved), RMS error of the fit 0.256 -> 0.263, 0.27 -> 0.282, 0.377 -> 0.394,
0.426 -> 0.423 dB, each inside always-3's 95 % interval. Speed: median 8.9x real time at a 1 s gap (3.7-13.4), 16x at 3 s;
render time ~0.2 s per second of notes (Violas, Celli), 0.1 (Basses), 0.3 (Violins 1, colder runs), silence ~free; the same
file 31-65 s on different runs (samples streamed from the owner's PC over the network); blocks of 4096: 12-16 % faster than
512 (3 pairs), levels as a 512 repeat; idle 120 s: no gain; two processes at once: each 18.6-19.2 s for 242 s of audio.
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

import even_fast_checks as efc  # noqa: E402

LEAD = 0.5
STEP = 0.005
WIN = 0.1364
MARGIN = 3
RUN_IN = 2
RUN_IN_BASSES = 4
RUN_OUT = 2
PASSES = 3
GAPS = (0.25, 0.5, 1.0, 1.5, 2.0, 3.0)
REF = 3.0
FRACS = (0.2, 0.4, 0.6, 0.8)
HALVES = ('A', 'B')
PATCHES = {'vc': 'Celli - Performance', 'vn1': 'Violins 1 - Performance', 'va': 'Violas - Performance',
           'cb': 'Basses - Performance'}
TAILS = (40, 50, 60)


def jload(*p):
    return json.load(open(os.path.join(*p)))


# --- events ---------------------------------------------------------------------------------------------------------

def window(P, j, runin):
    """the measurer's window of edited note j: (events relative to from, rendered notes, first index, fixed flags,
    from, length to the last note-off)"""
    notes = P['notes']
    a, b = j - MARGIN - runin, j + MARGIN + RUN_OUT
    t_from = notes[a]['on'] - LEAD
    t_to = notes[b]['on'] + 0.001
    end = max(n['off'] for n in notes[a:b + 1])
    state, ev = {}, []
    for t, k, x, y in P['base']:
        if k == 'cc':
            if t < t_from - 1e-9:
                state[x] = y
            elif t < end:
                ev.append((t - t_from, k, x, y))
    for i in range(a, b + 1):
        ev.append((notes[i]['on'] - t_from, 'on', notes[i]['p'], notes[i]['v']))
        ev.append((notes[i]['off'] - t_from, 'off', notes[i]['p'], 0))
    ev = [(0.0, 'cc', x, y) for x, y in sorted(state.items())] + ev
    fixed = [i < j - MARGIN or i > j + MARGIN for i in range(a, b + 1)]
    return ev, a, b, fixed, t_from, round(end - t_from, 5)


def events(dump12, dumpq, work, renders, basses, halves=HALVES, gaps=GAPS, sections=None, list_name='list.txt'):
    import transition_level_scan as tls
    info = tls.map_info(tls.MAP)
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    meta = {}
    for name, d, route, patch, bars in efc.PASSAGES:
        meta[name] = efc.passage((dump12, dumpq)[d], route, patch, bars, info[patch])
    if basses:
        # Whence has no slurred Basses: QC an octave lower (its times, velocities and controllers), on Basses -
        # Performance, its windows by the Basses' own legato delays
        import even_levels_test as elt
        Q = meta['QC']
        notes = [dict(on=n['on'], off=n['off'], p=n['p'] - 12, v=n['v']) for n in Q['notes']]
        notes = elt.windows(notes, info[PATCHES['cb']])
        base = [(t, k, a - 12 if k in ('on', 'off') else a, b) for t, k, a, b in Q['base']]
        meta['QB'] = dict(Q, patch=PATCHES['cb'], route=Q['route'] + ' -12', notes=notes, base=base,
                          pitch=Q['pitch'] - 12)
    wins = {}
    for name, P in meta.items():
        runin = RUN_IN_BASSES if P['patch'] == PATCHES['cb'] else RUN_IN
        nn = len(P['notes'])
        for f in FRACS:
            j = int(round(f * (nn - 1)))
            j = min(max(j, MARGIN + runin), nn - 1 - MARGIN - RUN_OUT)
            ev, a, b, fixed, t_from, length = window(P, j, runin)
            wins[f'{name}_{j}'] = dict(passage=name, patch=P['patch'], j=j, first=a, last=b, fixed=fixed,
                                       shift=round(t_from, 6), length=length, events=ev, pitch=P['pitch'])
    json.dump(dict(passages=meta, windows={k: {x: y for x, y in w.items() if x != 'events'} for k, w in wins.items()}),
              open(os.path.join(work, 'meta.json'), 'w'))
    lst, total = [], 0.0
    half = renders // 2
    for sec, patch in PATCHES.items():
        if sections and sec not in sections:
            continue
        mine = [k for k, w in wins.items() if w['patch'] == patch]
        if not mine:
            continue
        for g in gaps:
            for hn in halves:
                h = 'ABCDEFGH'.index(hn)
                rng = random.Random(f'egc {sec} {g} {hn}')
                t, ev, lay = 1.0, [], []
                for r in range(half):
                    order = mine[:]
                    rng.shuffle(order)
                    for wk in order:
                        w = wins[wk]
                        for p in range(PASSES):
                            ev.append((t - 0.002, 'tr', 0, 0))
                            ev += [(t + x, k, a, b) for x, k, a, b in w['events']]
                            lay.append(dict(window=wk, gap=g, half=hn, render=h * half + r, pass_=p, start=round(t, 5),
                                            len=round(w['length'] + g, 5)))
                            t += w['length'] + g
                name = f'g_{sec}_{str(g).replace(".", "p")}_{hn}'
                write_events(os.path.join(work, 'ev', name + '.txt'), ev)
                json.dump(dict(name=name, patch=patch, gap=g, length=round(t + 1.0, 3), segments=lay),
                          open(os.path.join(work, 'ev', name + '.json'), 'w'))
                lst.append((name, patch))
                total += t
    efc.write_list(work, list_name, lst)
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(wins), 'windows,', len(lst), 'files,', round(total / 60, 1), 'min of audio')


def write_events(path, ev):
    order = {'tr': -1, 'cc': 0, 'off': 1, 'on': 2}
    ev = sorted(ev, key=lambda e: (round(e[0], 6), order[e[1]]))
    with open(path, 'w', newline='\r\n') as f:
        for e in ev:
            f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')


RUN_CMD = r'''@echo off
rem even gap check (branch even-auto): renders list.txt (name|patch) with kthost (C:\claude\egc\kt: transport restart
rem events, --probe-pitch: its probe note in the Basses' range), analyses each wav here and deletes it (literal path);
rem only C:\claude\egc is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\egc\st mkdir C:\claude\egc\st
if not exist C:\claude\egc\r mkdir C:\claude\egc\r
for %%f in ("Violins 1 - Performance" "Violas - Performance" "Celli - Performance" "Basses - Performance") do if not exist "C:\claude\egc\st\%%~f.vst3state" copy /y "%SRC%\%%~f.vst3state" C:\claude\egc\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\egc\%1") do (
  if exist C:\claude\egc\stop.txt goto :end
  if not exist "C:\claude\egc\r\%%a.res.json" (
    echo %%a start !date! !time!>>C:\claude\egc\run.out
    set X=
    if "%%b"=="Basses - Performance" set X=--probe-pitch 40
    C:\claude\egc\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\egc\st\%%b.vst3state" "C:\claude\egc\ev\%%a.txt" "C:\claude\egc\r\%%a.wav" --tail 3 --wait 600 !X! > "C:\claude\egc\r\%%a.log" 2>&1
    "C:\Program Files\Python312\python.exe" C:\claude\egc\tools\soundlibraries\even_gap_check.py analyze C:\claude\egc\r %%a >> "C:\claude\egc\r\%%a.log" 2>&1
    if exist "C:\claude\egc\r\%%a.res.json" del "C:\claude\egc\r\%%a.wav"
    echo %%a done !date! !time!>>C:\claude\egc\run.out
  )
)
:end
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE %1>>C:\claude\egc\run.out
'''


# --- analyze (on the VM) --------------------------------------------------------------------------------------------

def analyze(folder, name):
    import soundfile as sf
    from loudness import perceived_envelope, power_envelope
    lay = json.load(open(os.path.join(folder, '..', 'ev', name + '.json')))
    x, sr = sf.read(os.path.join(folder, name + '.wav'), always_2d=True)
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    pw = power_envelope(x, sr)
    t_pw = (np.arange(len(pw)) + 0.5) * 0.005
    for s in lay['segments']:
        ts = s['start'] + np.arange(-0.2, s['len'], STEP)
        s['env'] = [round(float(v), 2) for v in np.interp(ts, t_env, env)]
        s['pw'] = [round(float(v), 1) for v in np.interp(ts, t_pw, pw)]
    # the silence before the first pass: the output's floor
    lay['floorDb'] = round(float(np.median(pw[int(0.2 / 0.005):int(0.8 / 0.005)])), 1)
    lay['wavSeconds'] = round(len(x) / sr, 3)
    json.dump(lay, open(os.path.join(folder, name + '.res.json'), 'w'), separators=(',', ':'))
    print(name, len(lay['segments']), 'segments analysed')


# --- report ---------------------------------------------------------------------------------------------------------

def wpass(meta, wk):
    """the window as a passage for efc's levels / slur statistics / fit (times relative to the pass start)"""
    w = meta['windows'][wk]
    P = meta['passages'][w['passage']]
    notes = []
    for n in P['notes'][w['first']:w['last'] + 1]:
        notes.append(dict(n, on=n['on'] - w['shift'], off=n['off'] - w['shift']))
    return dict(notes=notes, len=w['length'] + REF, fixed=w['fixed'])


def levels(W, seg):
    env = np.asarray(seg['env'])
    out = []
    for n in W['notes']:
        a = n['on'] + n['d']
        i0, i1 = int(round((a + 0.2) / STEP)), int(round((a + 0.2 + WIN) / STEP))
        out.append(float(np.mean(env[i0:i1])))
    return out


def fit_held(W, env, lv, lam=0.5):
    """efc.fit_glide with the run-in / run-out held (their columns left out), on the window's own slur medians"""
    import even_levels_test as elt
    notes = W['notes']
    t = np.arange(-0.2, W['len'], STEP)
    env = np.asarray(env)[:len(t)]
    t = t[:len(env)]
    s = efc.slur_stats(dict(notes=notes), lv)
    starts = np.array([n['on'] + n['d'] for n in notes])
    k = np.clip(np.searchsorted(starts, t, side='right') - 1, 0, None)
    target = np.array([s['median'].get(notes[i]['slur'], np.nan) for i in k])
    w = ((t >= starts[1]) & (t < notes[-1]['off']) & ~np.isnan(target)).astype(float)
    r = np.where(w > 0, np.nan_to_num(target) - env, 0.0)
    free = [j for j, f in enumerate(W['fixed']) if not f]
    M = np.zeros((len(t), len(free)))
    for c, j in enumerate(free):
        e = np.zeros(len(notes))
        e[j] = 1.0
        M[:, c] = elt.smooth(elt.curve_db('G', e, notes, t))
    Wm = M * w[:, None]
    du = np.linalg.solve(Wm.T @ M + lam * np.eye(len(free)), Wm.T @ r)
    return du


def collect(work, res):
    meta = jload(work, 'meta.json')
    data = {}       # (wk, gap) -> list of renders: dict(levels (3 x n), env (3 x m), pw, half)
    floors, speed = [], []
    for fn in sorted(os.listdir(res)):
        if not (fn.startswith('g_') and fn.endswith('.res.json')):
            continue
        r = jload(res, fn)
        floors.append(r.get('floorDb'))
        by = {}
        for s in r['segments']:
            by.setdefault((s['window'], s['render']), []).append(s)
        for (wk, ri), segs in by.items():
            segs.sort(key=lambda s: s['pass_'])
            if len(segs) != PASSES:
                continue
            W = wpass(meta, wk)
            data.setdefault((wk, segs[0]['gap']), []).append(dict(
                half=segs[0]['half'], render=ri, levels=[levels(W, s) for s in segs],
                env=[s['env'] for s in segs], pw=[s['pw'] for s in segs]))
    return meta, data, floors


def chi2_95(n):
    """the chi-square 95 % quantile / n (Wilson-Hilferty)"""
    z = 1.6449
    return (1 - 2 / (9 * n) + z * math.sqrt(2 / (9 * n))) ** 3


def compare(values_g, values_ref):
    """values: {note key: [per-render value]}; z per note of (mean g - mean ref), the mean shift and its SE"""
    d, z, se2 = [], [], []
    for k in values_ref:
        a, b = values_g.get(k), values_ref[k]
        if not a or len(a) < 2 or len(b) < 2:
            continue
        diff = st.mean(a) - st.mean(b)
        var = st.variance(a) / len(a) + st.variance(b) / len(b)
        d.append(diff)
        se2.append(var)
        z.append(diff / math.sqrt(var) if var > 0 else 0.0)
    n = len(d)
    mean = st.mean(d)
    se = math.sqrt(sum(se2)) / n
    mz2 = st.mean([x * x for x in z])
    noise = math.sqrt(st.mean(se2))
    effect = math.sqrt(max(0.0, st.mean([x * x for x in d]) - st.mean(se2)))
    return dict(notes=n, meanShift=round(mean, 3), meanShiftSE=round(se, 3), meanZ2=round(mz2, 3),
                bound=round(chi2_95(n), 3), rmsDiff=round(math.sqrt(st.mean([x * x for x in d])), 3),
                rmsNoise=round(noise, 3), effectRms=round(effect, 3),
                match=bool(mz2 <= chi2_95(n) and abs(mean) <= 2 * se))


def _pooled(fl, skip=None):
    """per window, the variance of one process mean (the processes of each gap about their own mean, pooled over the
    gaps and the window's notes), and the same for the section's overall mean, with its degrees of freedom"""
    ss_w, df_w, ss_m, df_m = {}, {}, 0.0, 0
    for gg in sorted(fl):
        if gg == skip:
            continue
        hs = sorted(fl[gg])
        if len(hs) < 2:
            continue
        keys = [k for k in fl[gg][hs[0]] if all(k in fl[gg][h] for h in hs)]
        for k in keys:
            v = [fl[gg][h][k] for h in hs]
            m = st.mean(v)
            ss_w[k[0]] = ss_w.get(k[0], 0.0) + sum((x - m) ** 2 for x in v)
            df_w[k[0]] = df_w.get(k[0], 0) + len(v) - 1
        om = [st.mean(fl[gg][h][k] for k in keys) for h in hs]
        m = st.mean(om)
        ss_m += sum((x - m) ** 2 for x in om)
        df_m += len(om) - 1
    return {w: ss_w[w] / df_w[w] for w in ss_w}, ss_m / df_m, df_m


def compare_files(fl, g, ref=REF):
    """fl[gap][process][(window, note)] = the mean over the renders of one kthost process. A process shifts every note
    a little (SSO's state per instance), so the unit of noise is the process: per window, the variance of a process
    mean (_pooled); per note z = (mean of g's processes - mean of 3 s's) / its SE; the section's mean shift against the
    process-level SE of the overall mean (Student's t, 97.5 %, the pooled degrees of freedom)"""
    from scipy import stats
    var_w, var_m, df_m = _pooled(fl)
    hg, hr = sorted(fl[g]), sorted(fl[ref])
    keys = [k for k in fl[ref][hr[0]] if all(k in fl[ref][h] for h in hr) and all(k in fl[g][h] for h in hg)]

    def mean_of(gg, hs, k):
        return st.mean(fl[gg][h][k] for h in hs)
    d = [mean_of(g, hg, k) - mean_of(ref, hr, k) for k in keys]
    f = 1 / len(hg) + 1 / len(hr)
    z = [x / math.sqrt(var_w[k[0]] * f) for x, k in zip(d, keys)]
    n = len(d)
    shift = st.mean(d)
    se = math.sqrt(var_m * f)
    tcrit = float(stats.t.ppf(0.975, df_m))
    mz2 = st.mean([x * x for x in z])
    noise = math.sqrt(st.mean([var_w[k[0]] * f for k in keys]))
    return dict(notes=n, processes=[len(hg), len(hr)], meanShift=round(shift, 3), meanShiftSE=round(se, 3),
                t=round(tcrit, 3), meanZ2=round(mz2, 3), bound=round(chi2_95(n), 3),
                rmsDiff=round(math.sqrt(st.mean([x * x for x in d])), 3), rmsNoise=round(noise, 3),
                effectRms=round(math.sqrt(max(0.0, st.mean([x * x for x in d]) - noise ** 2)), 3),
                match=bool(mz2 <= chi2_95(n) and abs(shift) <= tcrit * se))


def self_check(fl):
    """the same test within each gap, its first process against the rest (the variance from the other gaps): how
    often it fails where there is no effect"""
    out = {}
    for g in sorted(fl):
        hs = sorted(fl[g])
        if len(hs) < 2:
            continue
        var_w, _, _ = _pooled(fl, skip=g)
        a, rest = hs[0], hs[1:]
        f = 1 + 1 / len(rest)
        keys = [k for k in fl[g][a] if all(k in fl[g][h] for h in rest)]
        z = [(fl[g][a][k] - st.mean(fl[g][h][k] for h in rest)) / math.sqrt(var_w[k[0]] * f) for k in keys]
        out[str(g)] = dict(meanZ2=round(st.mean([x * x for x in z]), 3), bound=round(chi2_95(len(z)), 3))
    return out


def tails(meta, data):
    out = {}
    for (wk, g), rs in data.items():
        if g != REF:
            continue
        w = meta['windows'][wk]
        W = wpass(meta, wk)
        sec = [s for s, p in PATCHES.items() if p == w['patch']][0]
        off = max(n['off'] for n in W['notes'])
        for r in rs:
            for pw in r['pw']:
                pw = np.asarray(pw)
                lv = []
                for n, f in zip(W['notes'], W['fixed']):
                    if not f:
                        a = n['on'] + n['d']
                        lv.append(np.median(pw[int(round((a + 0.2) / STEP)):int(round((a + 0.2 + WIN) / STEP))]))
                ref = float(np.median(lv))
                i_off = int(round((off + 0.2) / STEP))
                tail = pw[i_off:]
                for db in TAILS:
                    above = np.nonzero(tail > ref - db)[0]
                    tt = (above[-1] + 1) * STEP if len(above) else 0.0
                    out.setdefault(sec, {}).setdefault(db, []).append(round(tt, 3))
                out.setdefault(sec, {}).setdefault('levelDb', []).append(round(ref, 1))
                out.setdefault(sec, {}).setdefault('endDb', []).append(round(float(np.median(tail[-40:])), 1))
    return out


def early_stop(meta, data, gaps):
    """2 passes, the 3rd only when the 2 differ on a measured note by more than T (the 95th percentile of |pass 1 - pass
    2| over the measured notes of all renders at these gaps): each render's fitted glide as always-3 and as early-stop,
    each against the mean of the OTHER renders' always-3 fits of the same window (leave one out: the error each makes);
    a bootstrap over windows (2000 draws) gives always-3's 95 % interval of the RMS error"""
    diffs = []
    rows = []      # (wk, render index, always-3 du, early du, stopped)
    for (wk, g), rs in data.items():
        if g not in gaps:
            continue
        W = wpass(meta, wk)
        free = [j for j, f in enumerate(W['fixed']) if not f]
        for r in rs:
            L = np.array(r['levels'])
            diffs += list(np.abs(L[0] - L[1])[free])
    T = float(np.percentile(diffs, 95))
    by = {}
    for (wk, g), rs in data.items():
        if g not in gaps:
            continue
        W = wpass(meta, wk)
        free = [j for j, f in enumerate(W['fixed']) if not f]
        for r in rs:
            L = np.array(r['levels'])
            E = np.array([np.asarray(e)[:min(len(x) for x in r['env'])] for e in r['env']])
            du3 = fit_held(W, np.median(E, axis=0), list(np.median(L, axis=0)))
            stopped = bool(np.max(np.abs(L[0] - L[1])[free]) <= T)
            if stopped:
                du2 = fit_held(W, np.mean(E[:2], axis=0), list(np.mean(L[:2], axis=0)))
            else:
                du2 = du3
            by.setdefault(wk, []).append((du3, du2, stopped))
    err3, err2, stops, n = {}, {}, 0, 0
    for wk, xs in by.items():
        e3, e2 = [], []
        for i, (du3, du2, stopped) in enumerate(xs):
            others = np.mean([x[0] for k, x in enumerate(xs) if k != i], axis=0)
            e3 += list(du3 - others)
            e2 += list(du2 - others)
            stops += stopped
            n += 1
        err3[wk], err2[wk] = e3, e2
    rms = lambda ks, d: math.sqrt(st.mean([x * x for k in ks for x in d[k]]))
    keys = sorted(by)
    rng = random.Random('early stop')
    boot3, bootd = [], []
    for _ in range(2000):
        ks = [rng.choice(keys) for _ in keys]
        boot3.append(rms(ks, err3))
        bootd.append(rms(ks, err2) - rms(ks, err3))
    boot3.sort()
    bootd.sort()
    R3, R2 = rms(keys, err3), rms(keys, err2)
    return dict(thresholdDb=round(T, 3), renders=n, stoppedAt2=stops, passesSaved=round(stops / (3.0 * n), 3),
                rmsErrorAlways3=round(R3, 3), rmsErrorEarlyStop=round(R2, 3),
                always3Interval95=[round(boot3[50], 3), round(boot3[1949], 3)],
                differenceInterval95=[round(bootd[50], 3), round(bootd[1949], 3)],
                match=bool(boot3[50] <= R2 <= boot3[1949]))


def process_means(meta, res, fn):
    """one kthost process: each measured note's mean over its renders of the median of 3 passes"""
    r = jload(res, fn)
    by = {}
    for sg in r['segments']:
        by.setdefault((sg['window'], sg['render']), []).append(sg)
    vals = {}
    for (wk, ri), segs in by.items():
        segs.sort(key=lambda x: x['pass_'])
        W = wpass(meta, wk)
        L = np.median([levels(W, x) for x in segs], axis=0)
        for j, f in enumerate(W['fixed']):
            if not f:
                vals.setdefault((wk, j), []).append(float(L[j]))
    return {k: st.mean(v) for k, v in vals.items()}


def speed(meta, work, res, tm):
    """render speed: per gap (median of the processes' audio / render seconds); per section a fit of render seconds on
    seconds of notes and of silence; the speed checks (sp_*: the 3 s files again in blocks of 512 / 2048 / 4096 frames,
    after 120 s idle, two processes at once), each one's levels against the 3 s processes of the same windows (z per
    note with the processes' own spread)"""
    by_file = {t['file']: t for t in tm}
    out = dict(byGap={}, bySection={}, checks={})
    rows = []
    for name, t in by_file.items():
        if not name.startswith('g_'):
            continue
        lay = os.path.join(work, 'ev', name + '.json')
        if not os.path.exists(lay):
            continue
        L = jload(lay)
        notes = sum(meta['windows'][x['window']]['length'] for x in L['segments'])
        rows.append((name.split('_')[1], L['gap'], t['audioS'], t['renderMs'] / 1000, notes, t['audioS'] - notes))
    for g in sorted(set(r[1] for r in rows)):
        x = [r[2] / r[3] for r in rows if r[1] == g]
        out['byGap'][str(g)] = dict(processes=len(x), medianX=round(st.median(x), 2), min=round(min(x), 1), max=round(max(x), 1))
    for sec in sorted(set(r[0] for r in rows)):
        x = [r for r in rows if r[0] == sec]
        A = np.array([[r[4], r[5]] for r in x])
        coef, *_ = np.linalg.lstsq(A, np.array([r[3] for r in x]), rcond=None)
        out['bySection'][sec] = dict(renderPerNoteSecond=round(float(coef[0]), 3), renderPerSilenceSecond=round(float(coef[1]), 3),
                                     processes=len(x))
    for name in sorted(by_file):
        if not name.startswith('sp_') or not os.path.exists(os.path.join(res, name + '.res.json')):
            continue
        sec = name.split('_')[2] if name.startswith('sp_par_') else name.split('_')[1]
        refs = [f for f in sorted(os.listdir(res)) if f.startswith(f'g_{sec}_3p0_') and f.endswith('.res.json')]
        R = [process_means(meta, res, f) for f in refs]
        X = process_means(meta, res, name + '.res.json')
        keys = [k for k in R[0] if k in X]
        d = [X[k] - st.mean(x[k] for x in R) for k in keys]
        z = [(X[k] - st.mean(x[k] for x in R)) / math.sqrt(st.variance([x[k] for x in R]) * (1 + 1 / len(R))) for k in keys]
        t = by_file[name]
        out['checks'][name] = dict(audioS=t['audioS'], renderS=round(t['renderMs'] / 1000, 2),
                                   x=round(t['audioS'] / (t['renderMs'] / 1000), 1), against=len(R),
                                   meanShift=round(st.mean(d), 3), rmsDiff=round(math.sqrt(st.mean([v * v for v in d])), 3),
                                   meanZ2=round(st.mean([v * v for v in z]), 2), bound=round(chi2_95(len(z)), 2))
    return out


def q(v, f):
    v = sorted(v)
    return round(float(v[min(len(v) - 1, int(f * len(v)))]), 3) if v else None


def report(work, res, out_path, logs):
    meta, data, floors = collect(work, res)
    secs = {}
    for (wk, g), rs in data.items():
        sec = [s for s, p in PATCHES.items() if p == meta['windows'][wk]['patch']][0]
        secs.setdefault(sec, {}).setdefault(g, {})[wk] = rs
    result = {}
    for sec, by_gap in sorted(secs.items()):
        if REF not in by_gap:
            print(sec, 'no 3 s renders yet')
            continue
        lv, du = {}, {}
        flv, fdu = {}, {}
        sd_lv, sd_pass = [], []
        for g, wins in by_gap.items():
            lv[g], du[g] = {}, {}
            hl, hd = {}, {}
            for wk, rs in wins.items():
                W = wpass(meta, wk)
                free = [j for j, f in enumerate(W['fixed']) if not f]
                for r in rs:
                    L = np.array(r['levels'])
                    med = np.median(L, axis=0)
                    env = np.median(np.array(r['env']), axis=0)
                    d = fit_held(W, env, list(med))
                    for c, j in enumerate(free):
                        lv[g].setdefault((wk, j), []).append(float(med[j]))
                        du[g].setdefault((wk, j), []).append(float(d[c]))
                        hl.setdefault(r['half'], {}).setdefault((wk, j), []).append(float(med[j]))
                        hd.setdefault(r['half'], {}).setdefault((wk, j), []).append(float(d[c]))
                    if g == REF:
                        sd_pass += list(np.std(L[:, free], axis=0, ddof=1))
            flv[g] = {h: {k: st.mean(v) for k, v in x.items()} for h, x in hl.items()}
            fdu[g] = {h: {k: st.mean(v) for k, v in x.items()} for h, x in hd.items()}
            if g == REF:
                sd_lv = [st.stdev(v) for v in lv[g].values() if len(v) > 1]
        rows = {}
        for g in sorted(by_gap):
            if g == REF:
                continue
            rows[str(g)] = dict(levels=compare_files(flv, g), fit=compare_files(fdu, g),
                                renders=st.median([len(v) for v in lv[g].values()]),
                                perRender=dict(levels=compare(lv[g], lv[REF]), fit=compare(du[g], du[REF])))
        selfcheck = dict(levels=self_check(flv), fit=self_check(fdu))
        ok = [g for g in sorted(by_gap) if g != REF and rows[str(g)]['levels']['match'] and rows[str(g)]['fit']['match']]
        chosen = REF
        for g in sorted(by_gap, reverse=True):
            if g == REF:
                continue
            if g in ok:
                chosen = g
            else:
                break
        es = early_stop(meta, {k: v for k, v in data.items() if meta['windows'][k[0]]['patch'] == PATCHES[sec]},
                        [g for g in by_gap if g >= chosen])
        result[sec] = dict(windows=len(by_gap.get(REF, {})), byGap=rows, chosen=chosen, earlyStop=es, selfCheck=selfcheck,
                           noise=dict(perPassSD=dict(p50=q(sd_pass, 0.5), p90=q(sd_pass, 0.9)),
                                      median3SD=dict(p50=q(sd_lv, 0.5), p90=q(sd_lv, 0.9))))
    tl = tails(meta, data)
    for sec, d in tl.items():
        result.setdefault(sec, {})['tail'] = {f'-{k}dB': dict(p50=q(v, 0.5), p90=q(v, 0.9), max=max(v)) for k, v in d.items()
                                              if isinstance(k, int)}
        result[sec]['tail']['levelDb'] = q(d['levelDb'], 0.5)
        result[sec]['tail']['endDb'] = q(d['endDb'], 0.5)
    tm = efc.times(logs) if logs else []
    for sec, d in sorted(result.items()):
        print(sec, 'chosen', d.get('chosen'), 'noise', d.get('noise'))
        print('  early stop', d.get('earlyStop'))
        for g, row in d.get('byGap', {}).items():
            print(f"  {g:>10}  levels {row['levels']}\n{'':14}fit    {row['fit']}")
        print('  tail', d.get('tail'))
    if tm:
        print('render x realtime', [round(t['audioS'] / (t['renderMs'] / 1000), 1) for t in tm])
    out = dict(
        _about='Even levels, the background measurer: the silence after each pass (evenlevels.cpp MEASURED gap), the '
               'early stop (MEASURED earlyStopDb) and the render speed. tools/soundlibraries/even_gap_check.py has the '
               'method, criterion and result. Measured 2026-10-13 on the Windows test VM: kthost (offline VST 3 host, a '
               'transport restart before each pass), Kontakt 8 + SSO, each Performance patch from MuseScore\'s resaved '
               'setup (library defaults: Tree mic + Leader as shipped), 44.1 kHz, the windows the background measurer '
               'renders of MuseScore\'s events (Recommended; Basses: Whence Celli bars 1-8 an octave lower). Per section '
               'and gap against 3 s, with the kthost process as the unit of noise: levels = each measured note\'s '
               'median-of-3-passes level per render, averaged per process (perceived, ~dB); fit = the glide fitted to it '
               '(dB); meanShift (SE, t), meanZ2 (bound: chi-square 95 %), rmsDiff / rmsNoise / effectRms; perRender: '
               'the same with the render as the unit (too strict: processes differ). selfCheck: the test within one gap '
               '(no effect). chosen: the smallest gap matching with every longer one. earlyStop: 2 passes, a 3rd only '
               'when a measured note differs by more than thresholdDb (95th percentile of |pass 1 - pass 2|), each '
               'render\'s fit against the other renders\' 3-pass fits. tail: seconds after the last note-off until the '
               'power envelope stays below -40/-50/-60 dB re the measured notes\' level (the envelope floors at -100 dBFS, '
               'so -50/-60 read as the gap itself). speed: per gap and section, and the sp_* checks (block size, idle, '
               'two processes at once) with their levels against the 3 s processes. Measurement only.',
        gaps=list(GAPS), passages={n: dict(route=P['route'], patch=P['patch'], bars=P['bars'], notes=len(P['notes']))
                                   for n, P in meta['passages'].items()},
        windows={k: dict(passage=w['passage'], j=w['j'], rendered=w['last'] - w['first'] + 1,
                         seconds=w['length']) for k, w in meta['windows'].items()},
        floorDb=q([f for f in floors if f is not None], 0.5),
        sections=result,
        speed=speed(meta, work, res, tm) if tm else None,
        renderSpeed=[dict(file=t['file'], audioS=t['audioS'], renderS=round(t['renderMs'] / 1000, 2)) for t in tm])
    json.dump(out, open(out_path, 'w'), separators=(',', ':'))
    print('wrote', out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('dump12')
    e.add_argument('dumpq')
    e.add_argument('work')
    e.add_argument('--renders', type=int, default=8)
    e.add_argument('--halves', default=','.join(HALVES), help='processes per gap (A,B; more: C,D …)')
    e.add_argument('--gaps', default=','.join(str(g) for g in GAPS))
    e.add_argument('--sections', help='vc,vn1,va,cb (default all)')
    e.add_argument('--list', default='list.txt')
    e.add_argument('--basses', action='store_true', help='QC an octave lower on Basses - Performance (Whence has no slurred Basses)')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('name')
    r = sub.add_parser('report')
    r.add_argument('work')
    r.add_argument('res')
    r.add_argument('-o', default=os.path.join(HERE, 'sso_even_gap_check.json'))
    r.add_argument('--logs')
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.dump12, o.dumpq, o.work, o.renders, o.basses, o.halves.split(','),
               [float(x) for x in o.gaps.split(',')], o.sections.split(',') if o.sections else None, o.list)
    elif o.cmd == 'analyze':
        analyze(o.folder, o.name)
    else:
        report(o.work, o.res, o.o, o.logs)


if __name__ == '__main__':
    main()
