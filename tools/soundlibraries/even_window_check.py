#!/usr/bin/env python3
"""Why the even-levels correction misses at 136-250 ms, a measure that agrees with the owner's ear, and a new
correction for a blind listening test (2026-10-13, branch even-window). Written for this check, not taken from
anywhere. Measurement and audio only: the renderer doesn't read the result (sso_even_window_check.json).

  even_window_check.py diagnose [--old <tempo_line_test work>] [--audio <its audio folder>]
        the owner's tempo-line verdicts against candidate measures, on the earlier renders (no VM)
  even_window_check.py events <work> --old <tempo_line_test work>
        kthost event files: <name>_U (uncorrected), <name>_C (the earlier test's kept correction, as the owner heard it)
  even_window_check.py measure <work> <name>          <work>/wav/<name>.wav -> <work>/res/<name>.json
  even_window_check.py correct <work> <round>         N round 1 (from U) or 2 (from N1): ev/<name>_N<r>.txt
  even_window_check.py audio <work> <out> [-o sso_even_window_check.json]
        <out>/audio/<len>_<R|L>_<U|C|N>.mp3, <out>/summary.json and the committed json

The owner's verdicts (blind, 2026-10-13, tempo_line_test.py: Celli - Performance, mf; U uncorrected, C as Even levels
corrects: each note's level the mean perceived loudness over 136.4 ms from its ARRIVAL = note-on + the map's
legatoDelay, a CC11 glide with one node per note at that window's middle, 2 rounds, K 1.28): 136 ms repeating figure
(R) U wrong, C fine; 136 line (L) C wrong; 180R both wrong ("C: the 1st and 3rd notes swell, U: the 3rd notes of each
slur swell"); 180L neither, U preferred; 250R both wrong, no difference; 250L U wrong, C preferred; 350 and 500 (R
and L) neither wrong, no difference. Yet the job's measure gave C slur ranges of 0.8-1.6 dB against U's 2-6.

Diagnosis (the earlier renders' envelopes, 3-pass median, and the mp3s as heard, pass 1; numbers in the json):
 1. At 136-250 ms the arrival comes after the next note-on (delays 200-310 ms against IOIs 136-250), and SSO's level
    peak of a transition comes BEFORE the pitch arrives: the folded 180R / 250R envelopes peak at note 3's note-on +
    120-240 ms, between note 2's window (ends 606 ms into the slur at 250 ms) and note 3's (starts 730). So the windows
    straddle the peaks and the dips after them: per-note means come out flat while the envelope still swings
    (C 250R: 5.4 dB peak to trough per slur in the fold, its slur range 1.5 dB).
 2. The CC11 is not mistimed: C - U equals the modelled glide (one pole 35 ms) at r 0.88-0.98, lag 10-15 ms, gain
    1.1-1.3 (K). The correction does what it is told; it is told the wrong thing, and one node per note (136-250 ms
    apart) can't follow a 60-120 ms dip-and-rise anyway.
 3. What the owner hears: no per-note mean level, wherever its window (note-on or arrival anchored, -100..+300 ms,
    60 ms .. one IOI, mean or peak), ranks C 180R's notes 1 and 3 above 2 and 4 with a range above ~0.7 dB; all say
    C is flat there. The within-note SWELL does: each note's largest rise of the perceived envelope (dip, then the
    peak after it) inside its note-on span [note-on, next note-on). 180R by slur position, the mp3s as heard: U 0.5
    2.1 3.6 1.9 (note 3), C 1.8 1.5 3.2 0.9 (notes 3 and 1); 250R U 0.9 1.7 6.0 1.3, C 1.5 1.0 4.2 2.0 (the
    correction left note 3's swell: "no difference, both wrong"); 136R U 0.2 1.6 2.5 1.0 -> C 0.3 1.6 1.2 1.3
    (evened: "C helps"). (The mp3s' envelope is on a slightly smaller dB scale than the raw renders': the loudness
    model's ~dB depends on the absolute level; 3-pass median, raw: 180R U 0.6 2.8 4.5 2.2, C 2.5 2.0 4.3 1.1.)
    Checks (8): the 180R positions (2), 180R C and 250R C more uneven than 136R C (2), 136R C < U, 136L C > U,
    250L C < U, 180L C >= U. Swell (p75 of the notes' swells): 7 of 8 on the mp3s (only 180L's "U preferred" missed,
    2.3 / 1.8, both heard fine), 5 of 8 on the 3-pass median; the job's measure 4 / 6 of 8; per-note means or peaks
    3-5. On the L passages swell differs by <= 0.2 dB between U and C (within a render's noise): consistent with
    "neither / no difference", not proof. 350 and 500 ms: swells 3.5-5.7 dB and heard fine: at those lengths the rise
    is the note's own attack (a tempo-dependent tolerance; not modelled).

The fix (N): flatten the envelope itself, not per-note windows: the target is each slur's median of the measured
envelope (from its first note-on to the next slur's), every 5 ms point from the passage's second note-on to its last
note-on weighted alike, and the glide's nodes every NODE_MS = 40 ms (not one per note): CC11 linear in dB between
nodes, a message every 5 ms where it changes, as the owner's glide; the fit as evenlevels.h fitGlide (smoothing 35 ms,
ridge RIDGE_N per node, round 1 / K 1.28, round 2 / round 1's measured gain when the envelope's rms off target stays
above 0.5 dB). 40 ms: CC11 acts within 10-15 ms with ~20-35 ms smoothing (above) and the envelope's analysis frame is
46 ms; 30 ms nodes gained < 0.2 dB of swell in the offline simulation, 60 ms lost 0.1-0.6. The glide is held at 0
before the second note's note-on - 100 ms (the lead-in note plays as uncorrected). Offline simulation (U + 1.28 x the
smoothed glide, validated: the same simulation of C gives C's measured position swells within 0.2-0.9 dB): swell p75
136R 2.5 -> 1.1, 180R 3.6 -> 1.6, 250R 4.3 -> 1.4, L 1.8-2.7 -> 0.9-1.1.

Renders: the same passages as tempo_line_test.py (136, 180, 250 ms; R and L), MuseScore's resaved setup, kthost +
Kontakt 8 + SSO on the Windows test VM, 3 passes; U and C re-rendered from the earlier test's event files. Audio:
pass 1, 0.5 s before the first note-on to 1.5 s after the last note-off, 44.1 kHz, LAME 192 kbps CBR; U, C and N of a
passage at the same EBU R128 integrated loudness, the highest at which every sample peak stays at or below -1 dBFS
(checked on the decoded mp3s).

Result (2026-10-13, Windows test VM, kthost + Kontakt 8 + SSO, 23 renders in ~7 min of kthost time; U and C
re-rendered measure as before: job 3.98 / 1.07 at 136R etc.). Job measure (slur range median) | swell p75 | envelope
rms off target, dB, pass 1 (the audio); swell by slur position for R:
          U                         C (as heard before)        N (new; kept round)
  136R   3.98 | 2.36 | 1.71         1.07 | 2.34 | 0.93         0.72 | 1.38 | 0.60  (N2)  pos U .2 1.8 3.2 1.0, C .3 2.3 1.6 2.0, N 1.1 1.1 .9 1.0
  136L   2.04 | 1.77 | 1.17         0.92 | 1.62 | 0.96         1.57 | 1.49 | 0.66  (N2)
  180R   4.43 | 3.33 | 1.93         0.95 | 3.52 | 1.02         0.89 | 1.72 | 0.62  (N2)  pos U 1.0 2.6 4.4 1.9, C 2.4 1.8 4.0 1.2, N 1.9 1.4 1.2 1.0
  180L   2.08 | 2.40 | 1.40         1.24 | 2.64 | 1.28         1.45 | 1.35 | 0.51  (N2)
  250R   3.45 | 4.42 | 2.03         1.83 | 4.03 | 1.38         0.79 | 1.67 | 0.54  (N1)  pos U 1.2 2.0 7.4 1.7, C 1.9 1.6 5.8 2.9, N 1.2 1.5 1.6 1.8
  250L   2.74 | 2.88 | 1.70         1.05 | 2.82 | 1.24         0.72 | 1.80 | 0.55  (N2)
N's round 1 measured gain 1.22-1.36 (K 1.28 holds for the 40 ms glide); round 2 lowered swell p75 a further 0.1-0.6
(250R stopped after round 1: rms 0.49). N lowers the envelope's 2-8 Hz modulation (where fluctuation strength is
largest) that C left or raised (180R 4-8 Hz: U 1.21, C 1.21, N 0.78; 136R: U 0.77, C 0.93, N 0.54) and adds nothing
above 16 Hz (0.18-0.24 in all). On the L passages the job's measure calls N worse than C (136L 1.57 vs 0.92, 180L
1.45 vs 1.24): the two measures disagree; the owner's ear decides. The 180R slur's first note (bowed) keeps the largest
swell in N (1.9 dB). Loudness match: U, C, N of a passage at the same integrated loudness (gains +27.9 to +34.6 dB:
kthost renders are quiet; N and C get 3-4.5 dB more, their shift under CC11 127), every mp3's sample peak -1.4 dBFS or
lower. Not done: a C++ version (libmscore/evenlevels.*, audio/vst3/evenmeasure.*): it changes what a note's value is
(a glide node every 40 ms, not one per note: the metaTag and Note::evenLevel would hold node values, not per-note
values) and waits for the owner's blind test of N.
"""
import argparse
import json
import math
import os
import shutil
import statistics as st
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import tempo_line_test as tlt  # noqa: E402  (sets slow_slur_test's passages)
import slow_slur_test as sst  # noqa: E402

LENGTHS = (136, 180, 250)
NAMES = [f'{i}{k}' for i in LENGTHS for k in 'RL']
NODE_MS = 40
RIDGE_N = 0.2
FLOOR_N = 0.5          # rms off target (dB) below which no round 2
VERSIONS = ('U', 'C', 'N')

# the owner's verdicts (tempo_line_test, 2026-10-13): 1 wrong, 0 not
VERDICTS = {'136R': dict(U=1, C=0), '136L': dict(U=0, C=1), '180R': dict(U=1, C=1, note='C: notes 1 and 3 swell; U: note 3'),
            '180L': dict(U=0, C=0, note='U preferred'), '250R': dict(U=1, C=1, note='no difference'),
            '250L': dict(U=1, C=0, note='C preferred'), '350R': dict(U=0, C=0), '350L': dict(U=0, C=0),
            '500R': dict(U=0, C=0), '500L': dict(U=0, C=0)}


def jload(*p):
    return json.load(open(os.path.join(*p)))


# --- measures -------------------------------------------------------------------------------------------------------

def grid(m):
    return np.arange(m['first'] - 0.2, m['end'] + 1.0, sst.STEP)


def note_shapes(m, t, env):
    """per note, in its note-on span [on, on + IOI): peak, mean, and the swell: its largest rise (a dip, then the
    highest point after it)"""
    ioi = m['ioi'] / 1000
    out = []
    for n in m['notes']:
        idx = np.where((t >= n['on']) & (t < n['on'] + ioi))[0]
        x = env[idx]
        mn = np.minimum.accumulate(x)
        k = int(np.argmax(x - mn))
        out.append(dict(peak=float(x.max()), mean=float(x.mean()), swell=float(x[k] - mn[k])))
    return out


def slur_groups(m):
    by = {}
    for k in range(1, len(m['notes']) - 1):
        by.setdefault(m['notes'][k]['slur'], []).append(k)
    return by


def range_median(m, lv):
    return st.median(max(lv[k] for k in ks) - min(lv[k] for k in ks) for ks in slur_groups(m).values() if len(ks) > 1)


def by_position(m, lv):
    return [float(np.mean([lv[k] for ks in slur_groups(m).values() if len(ks) == 4 for k in [ks[j]]])) for j in range(4)]


def target(m, t, env):
    """each slur's median of the envelope from its first note-on to the next slur's (the last: to the last note-on);
    nan outside [second note-on, last note-on)"""
    notes = m['notes']
    starts = {}
    for n in notes[1:]:
        starts.setdefault(n['slur'], n['on'])
    sl = sorted(starts.items())
    T = np.full(len(t), np.nan)
    for i, (s, a) in enumerate(sl):
        b = sl[i + 1][1] if i + 1 < len(sl) else notes[-1]['on']
        sel = (t >= a) & (t < b)
        T[sel] = np.median(env[sel])
    return T


def new_measures(m, t, env):
    sh = note_shapes(m, t, env)
    sw = [q['swell'] for q in sh]
    T = target(m, t, env)
    ok = ~np.isnan(T)
    out = dict(swellP75=round(float(np.percentile(sw[1:-1], 75)), 2), swellMedian=round(float(np.median(sw[1:-1])), 2),
               peakRange=round(range_median(m, [q['peak'] for q in sh]), 2),
               spanMeanRange=round(range_median(m, [q['mean'] for q in sh]), 2),
               envRms=round(float(np.sqrt(np.mean((env[ok] - T[ok]) ** 2))), 2),
               swells=[round(x, 2) for x in sw])
    if m['key'] == 'figure':
        out['swellByPosition'] = [round(x, 2) for x in by_position(m, sw)]
    return out


# --- diagnose (the earlier renders) ---------------------------------------------------------------------------------

def diagnose(old, adir):
    """the candidate measures against the owner's verdicts, on the earlier test's 3-pass median envelopes and on its
    mp3s (pass 1, as heard)"""
    meta = jload(old, 'meta.json')
    summ = jload(os.path.join(old, '..', 'summary.json')) if os.path.exists(os.path.join(old, '..', 'summary.json')) else None
    kept = {n: summ[n]['correction']['keptRound'] for n in summ} if summ else {}
    from loudness import perceived_envelope

    def env_median(name, ver):
        m = meta[name]
        f = name + '_U' if ver == 'U' else f'{name}_C{kept[name]}'
        e = np.array(jload(old, 'res', f + '.json')['env'])
        return grid(m)[:len(e)], e

    def env_mp3(name, ver):
        m = meta[name]
        tg, ref = env_median(name, ver)
        x = decode(os.path.join(adir, f'{name[:-1]}_{name[-1]}_{ver}.mp3'))
        e, first, hop = perceived_envelope(x, 44100)
        t = (first + np.arange(len(e)) * hop) / 1000.0 + (m['first'] - sst.LEAD)
        y = np.interp(tg, t, e)
        sel = (tg > m['notes'][1]['on']) & (tg < m['notes'][-1]['on'])
        return tg, y - np.median(y[sel]) + np.median(ref[sel])

    def arrival(name, t, e):
        return [float(e[(t >= n['arr']) & (t < n['arr'] + sst.WIN)].mean()) for n in meta[name]['notes']]

    cands = {
        'arrival 136.4 ms mean (the job)': lambda nm, t, e: arrival(nm, t, e),
        'note-on span mean': lambda nm, t, e: [q['mean'] for q in note_shapes(meta[nm], t, e)],
        'note-on span peak': lambda nm, t, e: [q['peak'] for q in note_shapes(meta[nm], t, e)],
        'swell (largest rise in the note-on span)': lambda nm, t, e: [q['swell'] for q in note_shapes(meta[nm], t, e)],
    }
    out = {}
    for src, envf in (('median3', env_median), ('mp3pass1', env_mp3)):
        envs = {(n, v): envf(n, v) for n in meta for v in ('U', 'C')}
        out[src] = {}
        for cname, f in cands.items():
            lv = {k: f(k[0], *envs[k]) for k in envs}
            if cname.startswith('swell'):
                s = {k: float(np.percentile(v[1:-1], 75)) for k, v in lv.items()}
            else:
                s = {k: range_median(meta[k[0]], v) for k, v in lv.items()}
            pu, pc = np.array(by_position(meta['180R'], lv[('180R', 'U')])), np.array(by_position(meta['180R'], lv[('180R', 'C')]))
            checks = {
                '180R U: note 3 highest': int(np.argmax(pu)) == 2,
                '180R C: notes 1 and 3 highest': set(np.argsort(pc)[-2:].tolist()) == {0, 2},
                '180R C above 136R C': s[('180R', 'C')] > s[('136R', 'C')],
                '250R C above 136R C': s[('250R', 'C')] > s[('136R', 'C')],
                '136R C below U': s[('136R', 'C')] < s[('136R', 'U')],
                '136L C above U': s[('136L', 'C')] > s[('136L', 'U')],
                '250L C below U': s[('250L', 'C')] < s[('250L', 'U')],
                '180L C not below U': s[('180L', 'C')] >= s[('180L', 'U')],
            }
            out[src][cname] = dict(agree=sum(checks.values()), of=len(checks), checks=checks,
                                   values={f'{n}_{v}': round(x, 2) for (n, v), x in s.items()},
                                   position180R=dict(U=[round(x, 2) for x in pu], C=[round(x, 2) for x in pc]))
            if 'swell' in cname:
                out[src][cname]['position'] = {f'{n}_{v}': [round(x, 2) for x in by_position(meta[n], lv[(n, v)])]
                                               for n in meta if n.endswith('R') for v in ('U', 'C')}
            print(f'{src:9s} {cname:42s} {sum(checks.values())}/{len(checks)}',
                  ' '.join(k for k, v in checks.items() if not v and True) and '| missed: ' + ', '.join(k for k, v in checks.items() if not v))
    return out


def decode(path):
    r = subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', path, '-f', 'f32le', '-ac', '2', '-ar', '44100', '-'],
                       capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype=np.float32).reshape(-1, 2).astype(np.float64)


# --- events ---------------------------------------------------------------------------------------------------------

RUN_CMD = r'''@echo off
rem even window check (branch even-window): renders each name|patch of list%1.txt with kthost (MuseScore's resaved
rem setup, copied); only C:\claude\ewc is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\ewc\st mkdir C:\claude\ewc\st
if not exist C:\claude\ewc\wav mkdir C:\claude\ewc\wav
if not exist "C:\claude\ewc\st\Celli - Performance.vst3state" copy /y "%SRC%\Celli - Performance.vst3state" C:\claude\ewc\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\ewc\list%1.txt") do (
  echo %%a start %time%>>C:\claude\ewc\run.out
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\ewc\st\%%b.vst3state" "C:\claude\ewc\ev\%%a.txt" "C:\claude\ewc\wav\%%a.wav" --tail 3 --wait 600 > "C:\claude\ewc\wav\%%a.log" 2>&1
  echo %%a done>>C:\claude\ewc\run.out
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE %1>>C:\claude\ewc\run.out
'''


def events(work, old):
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    meta_old = jload(old, 'meta.json')
    vals_old = jload(old, 'values.json')
    summ = jload(os.path.join(old, '..', 'summary.json'))
    meta, lines = {}, []
    for name in NAMES:
        meta[name] = meta_old[name]
        k = summ[name]['correction']['keptRound']
        for ver, src in (('U', f'{name}_U.txt'), ('C', f'{name}_C{k}.txt')):
            shutil.copyfile(os.path.join(old, 'ev', src), os.path.join(work, 'ev', f'{name}_{ver}.txt'))
            lines.append(f'{name}_{ver}|{meta[name]["patch"]}')
        meta[name]['cKept'] = k
        meta[name]['cValues'] = vals_old[f'{name}_C'][f'v{k}']
    json.dump(meta, open(os.path.join(work, 'meta.json'), 'w'), indent=1)
    with open(os.path.join(work, 'list1.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)


# --- measure --------------------------------------------------------------------------------------------------------

def measure(work, name):
    import soundfile as sf
    from loudness import perceived_envelope
    meta = jload(work, 'meta.json')
    m = meta[name.split('_')[0]]
    x, sr = sf.read(os.path.join(work, 'wav', name + '.wav'), always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    tg = grid(m)
    per = [np.interp(tg + p0, t_env, env) for p0 in m['passes']]
    med = np.median(per, axis=0)
    old = {ms: [sst.levels_on(t_env, env, m, ms, p0) for p0 in m['passes']] for ms in sst.MEASURES}
    res = dict(sr=sr, peak=float(np.max(np.abs(x))), env=[round(float(v), 3) for v in med],
               pass1=[round(float(v), 3) for v in per[0]],
               old={ms: dict(passes=old[ms], median=[st.median(v) for v in zip(*old[ms])]) for ms in sst.MEASURES},
               new=dict(median3=new_measures(m, tg, med), pass1=new_measures(m, tg, per[0])))
    os.makedirs(os.path.join(work, 'res'), exist_ok=True)
    json.dump(res, open(os.path.join(work, 'res', name + '.json'), 'w'))
    o = res['old']['short']['median']
    print(name, 'job measure', round(sst.slur_stats(m, o)['rangeMedian'], 2), '| new (3-pass median):',
          {k: v for k, v in res['new']['median3'].items() if k != 'swells'})


# --- correct --------------------------------------------------------------------------------------------------------

def nodes_of(m):
    n = m['notes']
    return np.arange(n[1]['on'] - 0.1, n[-1]['off'] + 0.1 + 1e-9, NODE_MS / 1000)


def glide(m, t, v):
    """the glide at t: 0 until the first node (the lead-in plays uncorrected: an anchor 40 ms before it), then linear"""
    nd = nodes_of(m)
    xs = np.concatenate([[nd[0] - NODE_MS / 1000], nd])
    ys = np.concatenate([[0.0], v])
    return np.interp(t, xs, ys)


def basis(m, t):
    nd = nodes_of(m)
    M = np.zeros((len(t), len(nd)))
    for j in range(len(nd)):
        e = np.zeros(len(nd))
        e[j] = 1.0
        M[:, j] = sst.smooth(glide(m, t, e))
    return M


def fit_n(m, t, env):
    T = target(m, t, env)
    w = (~np.isnan(T)).astype(float)
    M = basis(m, t)
    r = np.where(w > 0, np.nan_to_num(T) - env, 0.0)
    Wm = M * w[:, None]
    return np.linalg.solve(Wm.T @ M + RIDGE_N * np.eye(M.shape[1]), Wm.T @ r), M


def cc_events_n(m, v):
    t = np.arange(-sst.CC_LEAD, m['end'] + 0.3, sst.STEP)
    g = glide(m, t, v)
    shift = max(0.0, float(np.max(v)) - sst.db(127.4))
    out, last = [], None
    for tt, x in zip(t, g):
        y = int(max(1, min(127, round(127 * 10 ** ((x - shift) / 20)))))
        if y != last:
            out.append((round(float(tt), 5), 'cc', 11, y))
            last = y
    return out, shift


def correct(work, rnd):
    meta = jload(work, 'meta.json')
    vpath = os.path.join(work, 'values.json')
    vals = jload(vpath) if os.path.exists(vpath) else {}
    lines = []
    for name in NAMES:
        m = meta[name]
        tg = grid(m)
        u = np.array(jload(work, 'res', name + '_U.json')['env'])
        key = f'{name}_N'
        if rnd == 1:
            du, M = fit_n(m, tg, u)
            v = np.clip(du / sst.K, -sst.MAX_DB, sst.MAX_DB)
            rec = dict(nodeMs=NODE_MS, ridge=RIDGE_N, k=sst.K, v1=[float(x) for x in v])
        else:
            rec = vals[key]
            r1 = jload(work, 'res', f'{key}1.json')
            e1 = np.array(r1['env'])
            M = basis(m, tg)
            T = target(m, tg, u)
            ok = ~np.isnan(T)
            pred = (M @ np.array(rec['v1']))[ok]
            y = (e1 - u)[ok]
            x0, y0 = pred - pred.mean(), y - y.mean()
            g = float((x0 @ y0) / (x0 @ x0)) if (x0 @ x0) > 1e-9 and (x0 @ y0) > 0 else 1.0
            g = max(0.5, g)
            rec['gain1'] = g
            rec['rms1'] = r1['new']['median3']['envRms']
            if rec['rms1'] <= FLOOR_N:
                rec['round2'] = False
                vals[key] = rec
                print(key, 'round 1 rms', rec['rms1'], '<= floor: no round 2')
                continue
            du, _ = fit_n(m, tg, e1)
            v = np.clip(np.array(rec['v1']) + du / g, -sst.MAX_DB, sst.MAX_DB)
            rec['v2'] = [float(x) for x in v]
            rec['round2'] = True
        ev, shift = cc_events_n(m, v)
        rec[f'shift{rnd}'] = shift
        rec[f'cc{rnd}'] = len(ev)
        vals[key] = rec
        sst.write_events(os.path.join(work, 'ev', f'{key}{rnd}.txt'), sst.passes(m, m['base'] + ev))
        lines.append(f'{key}{rnd}|{m["patch"]}')
        print(f'{key}{rnd}', len(v), 'nodes, range', round(float(v.min()), 1), round(float(v.max()), 1), 'shift',
              round(shift, 2), len(ev), 'CC11' + (f'; gain1 {rec["gain1"]:.2f}' if rnd == 2 else ''))
    json.dump(vals, open(vpath, 'w'), indent=1)
    with open(os.path.join(work, f'list{rnd + 1}.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')


# --- audio ----------------------------------------------------------------------------------------------------------

def audio(work, out, out_json, diag=None):
    import soundfile as sf
    meta = jload(work, 'meta.json')
    vals = jload(work, 'values.json')
    adir = os.path.join(out, 'audio')
    os.makedirs(adir, exist_ok=True)
    summary = {}
    for name in NAMES:
        m = meta[name]
        ioi, kind = name[:-1], name[-1]
        cand = [r for r in (1, 2) if os.path.exists(os.path.join(work, 'res', f'{name}_N{r}.json'))]
        best = min(cand, key=lambda r: jload(work, 'res', f'{name}_N{r}.json')['new']['median3']['envRms'])
        chosen = {'U': name + '_U', 'C': name + '_C', 'N': f'{name}_N{best}'}
        a, b = m['passes'][0] + m['first'] - sst.LEAD, m['passes'][0] + m['end'] + sst.TAIL
        segs, loud = {}, {}
        for ver, f in chosen.items():
            x, sr = sf.read(os.path.join(work, 'wav', f + '.wav'), always_2d=True)
            assert sr == 44100, sr
            segs[ver] = x[int(round(a * sr)):int(round(b * sr))]
            tmp = os.path.join(work, f'tmp_{name}_{ver}.wav')
            sf.write(tmp, segs[ver], sr, subtype='FLOAT')
            loud[ver] = sst.lufs(tmp)
            os.remove(tmp)
        target_l = min(loud[v][0] + (-1.2 - 20 * math.log10(float(np.max(np.abs(segs[v]))))) for v in segs)
        for attempt in range(4):
            gains, finals = {}, {}
            for ver, seg in segs.items():
                g = target_l - loud[ver][0]
                tmp = os.path.join(work, f'tmp_{name}_{ver}.wav')
                sf.write(tmp, seg * 10 ** (g / 20), 44100, subtype='PCM_24')
                mp3 = os.path.join(adir, f'{ioi}_{kind}_{ver}.mp3')
                subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-codec:a', 'libmp3lame', '-b:a',
                                '192k', '-ar', '44100', mp3], check=True)
                os.remove(tmp)
                li, pk = sst.lufs(mp3)
                gains[ver] = round(g, 2)
                finals[ver] = dict(lufs=li, peak=pk)
            worst = max(f['peak'] for f in finals.values())
            if worst <= -1.0:
                break
            target_l -= worst + 1.05
        meas = {}
        for ver, f in chosen.items():
            r = jload(work, 'res', f + '.json')
            s = sst.slur_stats(m, r['old']['short']['median'])
            sw = sst.slur_stats(m, r['old']['whole']['median'])
            meas[ver] = dict(job=dict(rangeMedian=round(s['rangeMedian'], 2), rangeMax=round(max(s['ranges']), 2),
                                      levels=sst.rnd2(r['old']['short']['median'])),
                             wholeNote=dict(rangeMedian=round(sw['rangeMedian'], 2)),
                             new=r['new'])
        rec = vals[f'{name}_N']
        summary[name] = dict(
            description=f'{m["patch"]}, {"repeating figure" if kind == "R" else "non-repeating line"}, {ioi} ms '
                        f'notes, CC1 64: ' + ' | '.join(m['slurs']),
            kind=kind, noteMs=int(ioi), seconds=round(b - a, 2), measures=meas,
            nRounds={f'N{rd}': jload(work, 'res', f'{name}_N{rd}.json')['new']['median3']['envRms'] for rd in cand},
            correctionN=dict(keptRound=best, nodeMs=NODE_MS, ridge=RIDGE_N, k=sst.K, gainRound1=rec.get('gain1'),
                             nodes=len(rec['v1']), shift=round(rec[f'shift{best}'], 2), ccMessages=rec[f'cc{best}'],
                             valueRange=[round(min(rec[f'v{best}']), 2), round(max(rec[f'v{best}']), 2)]),
            correctionC=dict(fromTempoLineTest=True, keptRound=m['cKept'], values=sst.rnd2(m['cValues'])),
            loudness=dict(lufsBefore={v: loud[v][0] for v in loud}, gainDb=gains, mp3=finals),
            files={v: f'{ioi}_{kind}_{v}.mp3' for v in chosen})
        print(name, 'N kept', best, 'gains', gains, 'peaks', {v: finals[v]['peak'] for v in finals})
        for ver in chosen:
            nm = meas[ver]['new']['pass1']
            print('  ', ver, 'job', meas[ver]['job']['rangeMedian'], '| swell p75 (pass 1)', nm['swellP75'],
                  'env rms', nm['envRms'], 'peak range', nm['peakRange'], nm.get('swellByPosition', ''))
    json.dump(summary, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    about = ('SSO Celli - Performance, repeating figure (R) and non-repeating line (L) at 136 / 180 / 250 ms notes, CC1 64: '
             'uncorrected (U), the even-levels correction as the owner heard it in the tempo-line test (C), and the new '
             'envelope-flattening correction (N, glide nodes every 40 ms); tools/soundlibraries/even_window_check.py has '
             'the diagnosis, method and result. Rendered 2026-10-13 on the Windows test VM: kthost (offline VST 3 host), '
             'Kontakt 8 + SSO, MuseScore\'s resaved setup (library defaults), 44.1 kHz, 3 passes. diagnosis: the owner\'s '
             'tempo-line verdicts against candidate measures on the earlier renders. passages.<name>.measures.<V>: job = '
             'the even-levels job\'s measure (136.4 ms from the arrival; slur range median / max), new = swell (each '
             'note\'s largest rise of the perceived envelope within [note-on, next note-on); p75, median, by slur '
             'position), peakRange / spanMeanRange (slur range of the note-on spans\' peak / mean), envRms (the envelope\'s '
             'rms off each slur\'s median, second to last note-on), on the 3-pass median and on pass 1 (the audio). '
             'Measurement only: the renderer does not read this file.')
    slim = {n: {k: v for k, v in s.items() if k != 'files'} for n, s in summary.items()}
    out_obj = dict(_about=about, passages=slim)
    if diag:
        out_obj['diagnosis'] = jload(diag)
    json.dump(out_obj, open(out_json, 'w'), separators=(',', ':'))
    print('wrote', out_json)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    d = sub.add_parser('diagnose')
    d.add_argument('--old', required=True)
    d.add_argument('--audio', required=True)
    d.add_argument('-o', help='write the diagnosis json here')
    e = sub.add_parser('events')
    e.add_argument('work')
    e.add_argument('--old', required=True)
    m = sub.add_parser('measure')
    m.add_argument('work')
    m.add_argument('name')
    c = sub.add_parser('correct')
    c.add_argument('work')
    c.add_argument('round', type=int, choices=(1, 2))
    a = sub.add_parser('audio')
    a.add_argument('work')
    a.add_argument('out')
    a.add_argument('-o', default=os.path.join(HERE, 'sso_even_window_check.json'))
    a.add_argument('--diag')
    o = ap.parse_args()
    if o.cmd == 'diagnose':
        r = diagnose(o.old, o.audio)
        if o.o:
            json.dump(r, open(o.o, 'w'), indent=1)
    elif o.cmd == 'events':
        events(o.work, o.old)
    elif o.cmd == 'measure':
        measure(o.work, o.name)
    elif o.cmd == 'correct':
        correct(o.work, o.round)
    else:
        audio(o.work, o.out, o.o, o.diag)


if __name__ == '__main__':
    main()
