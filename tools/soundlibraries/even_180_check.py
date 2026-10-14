#!/usr/bin/env python3
"""Why the even-levels correction still swells in the 180 ms repeating figure (2026-10-13, branch even-180), and small
fixes for a blind test. Written for this check, not taken from anywhere. Measurement and audio only: the renderer
doesn't read the result (sso_even_180_check.json).

  even_180_check.py takes <work> --old <even_window_check work>
        kthost event files and list1.txt: TAKES fresh renders of 180R and 250R, U and C (the C the owner heard)
  even_180_check.py measure <work> <name>          <work>/wav/<name>.wav -> <work>/res/<name>.json
  even_180_check.py consistency <work>             per-note shapes by slur position over the takes
  even_180_check.py sim <even_window_check work>   candidate corrections offline, on the earlier U
  even_180_check.py correct <work> <1|2> --old <even_window_check work>    S round 1 / 2 (all six passages)
  even_180_check.py stakes <work>                  list4.txt: fresh takes of S's kept round (180R, 250R)
  even_180_check.py compare <work>                 U / C / S measured, pass 1, mean over takes
  even_180_check.py audio <work> <mp3 folder> <key.json>                   blind C / S clips (180R, 250R)

The owner (blind, twice): C (the even-levels correction as the job makes it) sounds wrong in the 180 ms repeating figure
(Celli - Performance, mf, slurs Eb3 E3 Eb3 C#3: note 1 a bowed transition from the slur before, 2-4 fingered),
"the 1st and 3rd notes swell"; U (uncorrected) wrong too, "the 3rd notes of each slur swell".

1. Consistent, not SSO's randomness (Windows test VM, kthost + Kontakt 8 + SSO, MuseScore's resaved setup; a fresh
   process per take: SSO's round robins start as after a transport restart). Swell (the largest rise of the perceived
   envelope within [note-on, next note-on), dB) by slur position, 6 takes' pass 1, mean / sd:
     U  0.79 / .15   2.70 / .29   4.45 / .19   2.10 / .23     two largest: notes 2 and 3 in 5 of 6 takes
     C  2.67 / .29   1.92 / .16   4.20 / .15   1.17 / .11     two largest: notes 1 and 3 in 6 of 6 takes (the owner's)
   Note 3's dip comes 66 ms after its note-on, its peak 125 ms (both versions, every take).
2. Causes. Note 3: SSO's transition E3 -> Eb3 dips ~60 ms after the note-on and rises 4-4.5 dB within the next 60 ms;
   U has it, C leaves it (C's nodes are 180 ms apart). No correction with one value per note removes it: the best
   per-position node timing found (coordinate search on the simulation) leaves 3.7 dB; nodes at each note's own dip,
   peak, rise or note-on 3.65-4.3. Note 1: C made it. U's bow change is loud and decays within ~100 ms; C answers the
   loud slur boundary with its lowest values there and ramps up through note 1, because each note's node sits 270-310 ms
   after its own note-on (the middle of the job's window from the arrival), i.e. inside the NEXT note's span: SSO's
   decay then C's ramp = a dip, then a rise (0.8 -> 2.7 dB).
3. Candidate S: C with each note's glide node at the middle of its own note-on span (note-on + IOI / 2), everything else
   as the job (the fit flattening the envelope to each slur's median, ridge 0.5, 35 ms smoothing, 2 rounds, K 1.28; the
   round kept by the smaller envelope rms). One value per note, no faster modulation than C. Fitted on the U C was
   fitted on (the earlier render's 3-pass median). Measured, pass 1, mean over takes (S: 180R 6, 250R 3 takes; the others
   one render; round 1's gain 1.19-1.37): swell p75 | job measure | envelope rms, dB
            U                  C                  S
     136R   2.57 4.06 1.79     2.41 1.15 0.97     2.31 1.48 0.86
     136L   1.95 2.15 1.16     1.80 0.96 0.99     1.74 1.39 0.88
     180R   3.53 4.37 1.91     3.61 1.12 1.04     3.16 1.02 0.94   by position U .79 2.70 4.46 2.10, C 2.67 1.92 4.20
                                                                  1.17, S 1.61 2.19 3.89 1.71
     180L   2.26 1.95 1.30     2.51 1.45 1.19     2.35 1.37 0.87
     250R   4.45 3.51 2.04     4.01 1.73 1.34     3.29 1.67 0.98   note 3: U 7.32, C 5.45, S 4.23
     250L   3.18 2.50 1.78     2.48 1.07 1.29     2.77 1.55 0.93
   S halves C's added swell on note 1 (it is still 0.8 dB above U's), lowers note 3 a little (0.3 dB at 180R) and moves
   some to note 4 (+0.5); 250L's swell 0.3 dB above C's; the job's measure (not trusted, it ranked C's misses as fine)
   0.4-0.5 dB higher on 136L and 250L. Measures are not the owner's ear: the swell measure rated the rejected N best.
"""
import argparse
import json
import os
import shutil
import statistics as st
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import even_window_check as ewc  # noqa: E402
import slow_slur_test as sst  # noqa: E402

TAKES = 6
GUARD_TAKES = 3

RUN_CMD = r'''@echo off
rem even 180 check (branch even-180): renders each out|events|patch of list%1.txt with kthost (MuseScore's resaved
rem setup, copied), a fresh process per line; only C:\claude\f180 is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\f180\st mkdir C:\claude\f180\st
if not exist C:\claude\f180\wav mkdir C:\claude\f180\wav
if not exist "C:\claude\f180\st\Celli - Performance.vst3state" copy /y "%SRC%\Celli - Performance.vst3state" C:\claude\f180\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
for /f "usebackq tokens=1,2,3 delims=|" %%a in ("C:\claude\f180\list%1.txt") do (
  echo %%a start %time%>>C:\claude\f180\run.out
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\f180\st\%%c.vst3state" "C:\claude\f180\ev\%%b.txt" "C:\claude\f180\wav\%%a.wav" --tail 3 --wait 600 > "C:\claude\f180\wav\%%a.log" 2>&1
  echo %%a done>>C:\claude\f180\run.out
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE %1>>C:\claude\f180\run.out
'''


def jload(*p):
    return json.load(open(os.path.join(*p)))


def write_list(work, n, lines):
    with open(os.path.join(work, f'list{n}.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')


# --- takes ----------------------------------------------------------------------------------------------------------

def takes(work, old):
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    shutil.copyfile(os.path.join(old, 'meta.json'), os.path.join(work, 'meta.json'))
    meta = jload(work, 'meta.json')
    lines = []
    for name, n in (('180R', TAKES), ('250R', GUARD_TAKES)):
        for ver in ('U', 'C'):
            shutil.copyfile(os.path.join(old, 'ev', f'{name}_{ver}.txt'), os.path.join(work, 'ev', f'{name}_{ver}.txt'))
            lines += [f'{name}_{ver}_t{i}|{name}_{ver}|{meta[name]["patch"]}' for i in range(1, n + 1)]
    write_list(work, 1, lines)
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(lines), 'renders')


# --- measure --------------------------------------------------------------------------------------------------------

def shapes(m, t, env):
    """per note, in its note-on span [on, on + IOI): mean, peak, swell (largest rise: a dip, then the highest point
    after it) and the dip's and the peak's times after the note-on (ms)"""
    ioi = m['ioi'] / 1000
    out = []
    for n in m['notes']:
        idx = np.where((t >= n['on']) & (t < n['on'] + ioi))[0]
        x = env[idx]
        mn = np.minimum.accumulate(x)
        k = int(np.argmax(x - mn))
        j = int(np.argmin(x[:k + 1]))
        out.append(dict(mean=float(x.mean()), peak=float(x.max()), swell=float(x[k] - x[j]),
                        dipMs=round(1000 * (t[idx[j]] - n['on'])), peakMs=round(1000 * (t[idx[k]] - n['on']))))
    return out


def measure(work, name):
    import soundfile as sf
    from loudness import perceived_envelope
    meta = jload(work, 'meta.json')
    m = meta[name.split('_')[0]]
    x, sr = sf.read(os.path.join(work, 'wav', name + '.wav'), always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    tg = ewc.grid(m)
    per = [np.interp(tg + p0, t_env, env) for p0 in m['passes']]
    res = dict(peak=float(np.max(np.abs(x))), passes=[])
    for p0, e in zip(m['passes'], per):
        job = sst.levels_on(t_env, env, m, 'short', p0)
        res['passes'].append(dict(env=[round(float(v), 3) for v in e], shapes=shapes(m, tg, e), job=job,
                                  jobRange=sst.slur_stats(m, job)['rangeMedian'], new=ewc.new_measures(m, tg, e)))
    os.makedirs(os.path.join(work, 'res'), exist_ok=True)
    json.dump(res, open(os.path.join(work, 'res', name + '.json'), 'w'))
    p = res['passes'][0]
    print(name, 'pass 1: job', round(p['jobRange'], 2), 'swell p75', p['new']['swellP75'], 'by position',
          p['new'].get('swellByPosition'))


# --- consistency ----------------------------------------------------------------------------------------------------

def positions(m):
    """slur position (0-3) of each note of the full 4-note slurs (the passage's first and last notes left out)"""
    g = ewc.slur_groups(m)
    return {k: j for ks in g.values() if len(ks) == 4 for j, k in enumerate(ks)}


def consistency(work, names=('180R', '250R')):
    meta = jload(work, 'meta.json')
    out = {}
    for name in names:
        m = meta[name]
        pos = positions(m)
        out[name] = {}
        for ver in ('U', 'C'):
            files = sorted(f[:-5] for f in os.listdir(os.path.join(work, 'res')) if f.startswith(f'{name}_{ver}_t'))
            rs = [jload(work, 'res', f + '.json') for f in files]
            rec = dict(takes=len(rs))
            for pi, label in ((0, 'pass1'), (1, 'pass2'), (2, 'pass3')):
                sh = [r['passes'][pi]['shapes'] for r in rs]
                by = {}
                for key in ('swell', 'mean', 'peak', 'dipMs', 'peakMs'):
                    vals = [[np.mean([s[k][key] for k, j in pos.items() if j == q]) for q in range(4)] for s in sh]
                    by[key] = dict(mean=[round(float(np.mean([v[q] for v in vals])), 2) for q in range(4)],
                                   sd=[round(float(np.std([v[q] for v in vals])), 2) for q in range(4)])
                # per take: which positions carry the two largest swells; per slur: which note swells most
                top2 = [sorted(np.argsort(v)[-2:].tolist()) for v in
                        [[np.mean([s[k]['swell'] for k, j in pos.items() if j == q]) for q in range(4)] for s in sh]]
                slurs = ewc.slur_groups(m)
                most = [[int(np.argmax([s[k]['swell'] for k in ks])) for ks in slurs.values() if len(ks) == 4] for s in sh]
                # mean level by position relative to the slur's mean (the job's question: are notes 1 / 3 louder?)
                rel = [[np.mean([s[k]['mean'] - np.mean([s[kk]['mean'] for kk in ks]) for ks in slurs.values() if len(ks) == 4
                                 for k in [ks[q]]]) for q in range(4)] for s in sh]
                rec[label] = dict(byPosition=by, top2PerTake=top2,
                                  mostPerSlurCount=[[sum(1 for x in t if x == q) for q in range(4)] for t in most],
                                  meanRelative=dict(mean=[round(float(np.mean([v[q] for v in rel])), 2) for q in range(4)],
                                                    sd=[round(float(np.std([v[q] for v in rel])), 2) for q in range(4)]),
                                  swellP75=[r['passes'][pi]['new']['swellP75'] for r in rs],
                                  jobRange=[round(r['passes'][pi]['jobRange'], 2) for r in rs])
            out[name][ver] = rec
            p = rec['pass1']
            print(f"{name} {ver} ({len(rs)} takes, pass 1) swell by position {p['byPosition']['swell']['mean']} "
                  f"sd {p['byPosition']['swell']['sd']} | dip ms {p['byPosition']['dipMs']['mean']} peak ms "
                  f"{p['byPosition']['peakMs']['mean']} | mean rel {p['meanRelative']['mean']} | top2 {p['top2PerTake']} | "
                  f"p75 {p['swellP75']} | job {p['jobRange']}")
    json.dump(out, open(os.path.join(work, 'consistency.json'), 'w'), indent=1)
    return out


# --- candidates (offline) -------------------------------------------------------------------------------------------

LAG = 0.012            # CC11 acts 10-15 ms late (even_window_check.py)


def lagged(g):
    k = int(round(LAG / sst.STEP))
    return np.concatenate([np.full(k, g[0]), g[:-k]]) if k else g


def rise_offsets(m, t, env):
    """each slur position's level arrival: the time of the steepest rise of env within its note-on span, after the
    note-on (s; median over the full slurs)"""
    ioi = m['ioi'] / 1000
    pos = positions(m)
    d = np.gradient(env, t)
    out = {}
    for k, q in pos.items():
        n = m['notes'][k]
        idx = np.where((t >= n['on']) & (t < n['on'] + ioi))[0]
        out.setdefault(q, []).append(t[idx[int(np.argmax(d[idx]))]] - n['on'])
    return [float(np.median(out[q])) for q in range(4)]


def switch_times(m, kind, offs=None):
    """where each note's value starts (steps) or its node (glide)"""
    notes = m['notes']
    if kind == 'glide':
        return sst.centres(m, 'short')
    if kind == 'spanMid':
        return np.array([n['on'] + m['ioi'] / 2000 for n in notes])
    if kind == 'stepOn':
        return np.array([n['on'] for n in notes])
    pos = positions(m)
    mean_off = float(np.mean(offs))
    return np.array([n['on'] + (offs[pos[k]] if k in pos else mean_off) for k, n in enumerate(notes)])


def shape_of(m, t, kind, v, offs=None):
    """the correction's level (dB) at t: linear between nodes (glide) or held from each switch (steps), the first
    note's value before it"""
    s = switch_times(m, kind, offs)
    v = np.asarray(v, dtype=float)
    if kind in ('glide', 'spanMid'):
        return np.interp(t, s, v)
    k = np.clip(np.searchsorted(s, t, side='right') - 1, 0, None)
    return v[k]


def basis(m, t, kind, offs=None):
    n = len(m['notes'])
    M = np.zeros((len(t), n))
    for j in range(n):
        e = np.zeros(n)
        e[j] = 1.0
        M[:, j] = lagged(sst.smooth(shape_of(m, t, kind, e, offs)))
    return M


def fit(m, t, env, kind, offs=None):
    """per-note values (the change heard, dB) flattening env to each slur's median, from note 2's note-on to the last
    note-on (as even_window_check.target), ridge as the job's"""
    T = ewc.target(m, t, env)
    w = (~np.isnan(T)).astype(float)
    M = basis(m, t, kind, offs)
    r = np.where(w > 0, np.nan_to_num(T) - env, 0.0)
    Wm = M * w[:, None]
    return np.linalg.solve(Wm.T @ M + sst.RIDGE * np.eye(M.shape[1]), Wm.T @ r), M


def scores(m, t, env):
    nm = ewc.new_measures(m, t, env)
    job = sst.slur_stats(m, sst.levels_on(t, env, m, 'short'))['rangeMedian']
    return dict(job=round(job, 2), swellP75=nm['swellP75'], envRms=nm['envRms'], byPosition=nm.get('swellByPosition'))


def sim(old, names=None):
    """U + the candidate's heard change (K x the sent values = the fitted values, smoothed and lagged) against the
    measured C; C itself simulated from its sent values to check the simulation"""
    meta = jload(old, 'meta.json')
    out = {}
    for name in names or ewc.NAMES:
        m = meta[name]
        t = ewc.grid(m)
        u = np.array(jload(old, 'res', name + '_U.json')['env'])[:len(t)]
        c = np.array(jload(old, 'res', name + '_C.json')['env'])[:len(t)]
        t = t[:len(u)]
        cs = u + sst.K * (basis(m, t, 'glide') @ np.array(m['cValues']))
        offs = rise_offsets(m, t, u) if m['key'] == 'figure' else None
        rec = dict(U=scores(m, t, u), C=scores(m, t, c), Csim=scores(m, t, cs),
                   CsimR=round(float(np.corrcoef(cs - u, c - u)[0, 1]), 2))
        if offs is None:      # a line: the rise by interval is not folded here; the steps' switch: the mean figure's
            offs = [0.12] * 4
        rec['riseOffsetsMs'] = [round(1000 * x) for x in offs]
        for kind in ('stepOn', 'stepRise', 'glideRefit'):
            kk = 'glide' if kind == 'glideRefit' else kind
            v, M = fit(m, t, u, kk, offs)
            rec[kind] = scores(m, t, u + M @ v)
            rec[kind]['sentRange'] = [round(float(v.min() / sst.K), 1), round(float(v.max() / sst.K), 1)]
        out[name] = rec
        print(name, 'rise ms', rec['riseOffsetsMs'], 'Csim r', rec['CsimR'])
        for k in ('U', 'C', 'Csim', 'glideRefit', 'stepOn', 'stepRise'):
            print(f'   {k:10s}', rec[k])
    return out


# --- candidate S: rendered ------------------------------------------------------------------------------------------

def median_env(work, name):
    r = jload(work, 'res', name + '.json')
    return np.median([p['env'] for p in r['passes']], axis=0)


def cc_events(m, kind, v):
    """CC11 every 5 ms where it changes, the glide of sent values v (dB), lowered to stay within 127 (as sst)"""
    t = np.arange(-sst.CC_LEAD, m['end'] + 0.3, sst.STEP)
    g = shape_of(m, t, kind, v)
    shift = max(0.0, float(np.max(v)) - sst.db(127.4))
    out, last = [], None
    for tt, x in zip(t, g):
        y = int(max(1, min(127, round(127 * 10 ** ((x - shift) / 20)))))
        if y != last:
            out.append((round(float(tt), 5), 'cc', 11, y))
            last = y
    return out, shift


def correct(work, old, rnd, kind='spanMid'):
    """S round 1 from the U the owner's C was fitted on (the earlier render's 3-pass median), round 2 from S1's render
    (its 3-pass median) divided by round 1's measured gain, as the job"""
    meta = jload(work, 'meta.json')
    vpath = os.path.join(work, 'values.json')
    vals = jload(vpath) if os.path.exists(vpath) else {}
    lines = []
    for name in ewc.NAMES:
        m = meta[name]
        t = ewc.grid(m)
        u = np.array(jload(old, 'res', name + '_U.json')['env'])[:len(t)]
        t = t[:len(u)]
        key = f'{name}_S'
        if rnd == 1:
            du, M = fit(m, t, u, kind)
            v = np.clip(du / sst.K, -sst.MAX_DB, sst.MAX_DB)
            rec = dict(kind=kind, k=sst.K, v1=[float(x) for x in v])
        else:
            rec = vals[key]
            e1 = median_env(work, f'{key}1')[:len(t)]
            M = basis(m, t, kind)
            T = ewc.target(m, t, u)
            ok = ~np.isnan(T)
            pred = (M @ np.array(rec['v1']))[ok]
            y = (e1 - u)[ok]
            x0, y0 = pred - pred.mean(), y - y.mean()
            g = float((x0 @ y0) / (x0 @ x0)) if (x0 @ x0) > 1e-9 and (x0 @ y0) > 0 else 1.0
            rec['gain1'] = max(0.5, g)
            du, _ = fit(m, t, e1, kind)
            v = np.clip(np.array(rec['v1']) + du / rec['gain1'], -sst.MAX_DB, sst.MAX_DB)
            rec['v2'] = [float(x) for x in v]
        ev, shift = cc_events(m, kind, v)
        rec[f'shift{rnd}'] = shift
        vals[key] = rec
        sst.write_events(os.path.join(work, 'ev', f'{key}{rnd}.txt'), sst.passes(m, m['base'] + ev))
        lines.append(f'{key}{rnd}|{key}{rnd}|{m["patch"]}')
        print(f'{key}{rnd}', 'sent', ' '.join(f'{x:+.1f}' for x in v[:9]), '... shift', round(shift, 2), len(ev), 'CC11'
              + (f'; gain1 {rec["gain1"]:.2f}' if rnd == 2 else ''))
    json.dump(vals, open(vpath, 'w'), indent=1)
    write_list(work, rnd + 1, lines)
    if rnd == 1:      # the earlier C and U of the passages not re-rendered as takes, for the measured comparison
        extra = []
        for name in ewc.NAMES:
            if name in ('180R', '250R'):
                continue
            for ver in ('U', 'C'):
                shutil.copyfile(os.path.join(old, 'ev', f'{name}_{ver}.txt'), os.path.join(work, 'ev', f'{name}_{ver}.txt'))
                extra.append(f'{name}_{ver}_t1|{name}_{ver}|{m["patch"]}')
        write_list(work, 2, lines + extra)


def kept_round(work, name):
    """S's kept round: the smaller rms of the 3-pass median envelope off each slur's median (the fit's own aim)"""
    c = [r for r in (1, 2) if os.path.exists(os.path.join(work, 'res', f'{name}_S{r}.json'))]
    return min(c, key=lambda r: float(np.median([p['new']['envRms'] for p in jload(work, 'res', f'{name}_S{r}.json')['passes']])))


def summary_of(work, files, pi=0):
    """mean over the files of pass pi's measures"""
    rs = [jload(work, 'res', f + '.json')['passes'][pi] for f in files]
    out = dict(takes=len(rs), job=round(float(np.mean([r['jobRange'] for r in rs])), 2),
               swellP75=round(float(np.mean([r['new']['swellP75'] for r in rs])), 2),
               envRms=round(float(np.mean([r['new']['envRms'] for r in rs])), 2))
    if rs[0]['new'].get('swellByPosition'):
        out['byPosition'] = [round(float(np.mean([r['new']['swellByPosition'][q] for r in rs])), 2) for q in range(4)]
        out['byPositionSd'] = [round(float(np.std([r['new']['swellByPosition'][q] for r in rs])), 2) for q in range(4)]
    return out


def takes_of(work, name, ver):
    if ver == 'S':
        k = kept_round(work, name)
        fs = sorted(f[:-5] for f in os.listdir(os.path.join(work, 'res')) if f.startswith(f'{name}_S{k}_t'))
        return fs or [f'{name}_S{k}']
    return sorted(f[:-5] for f in os.listdir(os.path.join(work, 'res')) if f.startswith(f'{name}_{ver}_t'))


def compare(work):
    out = {}
    for name in ewc.NAMES:
        out[name] = {v: summary_of(work, takes_of(work, name, v)) for v in ('U', 'C', 'S')}
        out[name]['S']['keptRound'] = kept_round(work, name)
        print(name, ' | '.join(f"{v}: job {s['job']} swell {s['swellP75']} rms {s['envRms']} {s.get('byPosition', '')}"
                               for v, s in out[name].items()))
    json.dump(out, open(os.path.join(work, 'compare.json'), 'w'), indent=1)
    return out


def representative(work, files):
    """the take whose pass-1 swell by position (or p75) is nearest the takes' mean"""
    rs = {f: jload(work, 'res', f + '.json')['passes'][0]['new'] for f in files}
    key = 'swellByPosition' if rs[files[0]].get('swellByPosition') else 'swellP75'
    mean = np.mean([np.atleast_1d(r[key]) for r in rs.values()], axis=0)
    return min(files, key=lambda f: float(np.sum((np.atleast_1d(rs[f][key]) - mean) ** 2)))


def audio(work, adir, key_out, names=('180R', '250R'), seed=180):
    """blind clips: per passage C (the owner's) and S, each its representative take's pass 1, 0.5 s before the first
    note-on to 1.5 s after the last note-off, loudness-matched (EBU R128, highest level with every sample peak at or
    below -1 dBFS), LAME 192 kbps; letters shuffled"""
    import math
    import random
    import subprocess
    import soundfile as sf
    meta = jload(work, 'meta.json')
    cmp = jload(work, 'compare.json')
    os.makedirs(adir, exist_ok=True)
    rnd = random.Random(seed)
    key = {}
    for i, name in enumerate(names, 1):
        m = meta[name]
        chosen = {v: representative(work, takes_of(work, name, v)) for v in ('C', 'S')}
        a, b = m['passes'][0] + m['first'] - sst.LEAD, m['passes'][0] + m['end'] + sst.TAIL
        segs, loud = {}, {}
        for ver, f in chosen.items():
            x, sr = sf.read(os.path.join(work, 'wav', f + '.wav'), always_2d=True)
            assert sr == 44100, sr
            segs[ver] = x[int(round(a * sr)):int(round(b * sr))]
            tmp = os.path.join(work, f'tmp_{name}_{ver}.wav')
            sf.write(tmp, segs[ver], sr, subtype='FLOAT')
            loud[ver] = sst.lufs(tmp)[0]
            os.remove(tmp)
        letters = ['a', 'b']
        rnd.shuffle(letters)
        lmap = dict(zip(('C', 'S'), letters))
        target = min(loud[v] + (-1.2 - 20 * math.log10(float(np.max(np.abs(segs[v]))))) for v in segs)
        for attempt in range(4):
            finals = {}
            for ver, seg in segs.items():
                g = target - loud[ver]
                tmp = os.path.join(work, f'tmp_{name}_{ver}.wav')
                sf.write(tmp, seg * 10 ** (g / 20), 44100, subtype='PCM_24')
                mp3 = os.path.join(adir, f'f{i}{lmap[ver]}.mp3')
                subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-codec:a', 'libmp3lame', '-b:a', '192k',
                                '-ar', '44100', mp3], check=True)
                os.remove(tmp)
                li, pk = sst.lufs(mp3)
                finals[ver] = dict(file=os.path.basename(mp3), gainDb=round(g, 2), lufs=li, peak=pk, take=chosen[ver])
            if max(f['peak'] for f in finals.values()) <= -1.0:
                break
            target -= max(f['peak'] for f in finals.values()) + 1.05
        key[f'f{i}'] = dict(passage=name, letters={lmap[v]: v for v in lmap}, files=finals,
                            measures={v: cmp[name][v] for v in ('U', 'C', 'S')})
        print(f'f{i}', name, {lmap[v]: v for v in lmap}, {v: (finals[v]['lufs'], finals[v]['peak']) for v in finals})
    json.dump(dict(about='even-180 blind clips: C = the even-levels correction the owner heard (tempo-line test), S = '
                         'the same with each note\'s glide node at the middle of its own note-on span; measures: pass 1 '
                         'of fresh kthost renders (Kontakt 8 + SSO, Windows test VM), mean over takes', clips=key),
              open(key_out, 'w'), indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    t = sub.add_parser('takes')
    t.add_argument('work')
    t.add_argument('--old', required=True)
    m = sub.add_parser('measure')
    m.add_argument('work')
    m.add_argument('name')
    c = sub.add_parser('consistency')
    c.add_argument('work')
    s = sub.add_parser('sim')
    s.add_argument('old')
    s.add_argument('names', nargs='*')
    r = sub.add_parser('correct')
    r.add_argument('work')
    r.add_argument('round', type=int, choices=(1, 2))
    r.add_argument('--old', required=True)
    cp = sub.add_parser('compare')
    cp.add_argument('work')
    au = sub.add_parser('audio')
    au.add_argument('work')
    au.add_argument('adir')
    au.add_argument('key')
    sk = sub.add_parser('stakes')
    sk.add_argument('work')
    o = ap.parse_args()
    if o.cmd == 'compare':
        compare(o.work)
    elif o.cmd == 'audio':
        audio(o.work, o.adir, o.key)
    elif o.cmd == 'stakes':
        meta = jload(o.work, 'meta.json')
        lines = []
        for name, n in (('180R', TAKES), ('250R', GUARD_TAKES)):
            k = kept_round(o.work, name)
            lines += [f'{name}_S{k}_t{i}|{name}_S{k}|{meta[name]["patch"]}' for i in range(1, n + 1)]
        write_list(o.work, 4, lines)
        print('\n'.join(lines))
    elif o.cmd == 'correct':
        correct(o.work, o.old, o.round)
    elif o.cmd == 'sim':
        sim(o.old, o.names)
    elif o.cmd == 'takes':
        takes(o.work, o.old)
    elif o.cmd == 'measure':
        measure(o.work, o.name)
    else:
        consistency(o.work)


if __name__ == '__main__':
    main()
