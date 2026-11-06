#!/usr/bin/env python3
"""Evening out SLOW slurs: the even-levels job's short window against a whole-note window, for a blind listening test
(2026-10-13, branch slow-slur-test). Written for this test, not taken from anywhere. Why: swell_vs_length.py found
that slow slurs (250-1000 ms notes) on SSO's string Performance patches are no flatter than fast ones (slur range p50
3.5-5.8 dB in the job's 136.4 ms window), and at slow tempi that window sees only the start of each note. So: does a
correction measured over each note's whole length sound more even than the job's? Measurement and audio only: the
renderer doesn't read the result (sso_slow_slur_test.json).

  slow_slur_test.py events <work>                 kthost event files <work>/ev/<passage>_U.txt, meta.json, run.cmd
  slow_slur_test.py measure <work> <name>         <work>/wav/<name>.wav -> <work>/res/<name>.json (both measures)
  slow_slur_test.py correct <work> <round>        round 1 (from U) or 2 (from round 1): ev/<passage>_W<r>.txt, _N<r>.txt
  slow_slur_test.py audio <work> <out> [-o sso_slow_slur_test.json]
        <out>/audio/<passage>_U|W|N.mp3 (pass 1), <out>/summary.json and the committed json

Passages (written for this test: stepwise lines with small leaps and a melodic contour, each ending on its tonic),
CC1 64 (mf) from 0.6 s before, CC11 127, as swell_vs_length.py builds a phrase, i.e. as Recommended plays a slur
([slurs] performance, transitions, smoothStart): the first note a smooth attack (velocity 5) sent early by the map's
attackOnset at its pitch, every other slur's first note a bowed transition from the slur before (93; Violas 100, the
map's legatoVelocity, as rendermidi plays a bowed transition), the rest fingered (69); every note IOI long, the note
before off the map's overlap (24 ms) after the next note-on, the last note off at its end.
  vc500   Celli - Performance, 500 ms, D minor, 4 slurs of 4: D3 F3 E3 G3 | F3 A3 G3 Bb3 | A3 G3 F3 E3 | F3 E3 C#3 D3
  vn500   Violins 1 - Performance, 500 ms, G major, 4 slurs of 4: D5 G5 F#5 E5 | D5 E5 F#5 G5 | B5 A5 G5 F#5 | E5 D5 F#5 G5
  va1000  Violas - Performance, 1000 ms, C major, 3 slurs of 4: C4 D4 E4 F4 | G4 F4 E4 D4 | E4 D4 B3 C4
Each render: the passage three times (3 passes, one kthost process from MuseScore's resaved setup, library defaults),
4 s apart.

Two measures of a note's level (perceived envelope, tools/playbackverify/loudness.py, ~dB; 3-pass median):
  short  the job's (evenlevels.h): mean over 136.4 ms from its arrival = note-on + the map's legatoDelay for the
         interval (the first note: its smooth attackOnset after its note-on as sent)
  whole  mean from its arrival to the next note's arrival (the last note: to its note-off)
Slur statistics (evenlevels.h slurStats): per slur max - min, the passage's first and last notes left out (so the
first and last slurs count three notes); 'range' below = median over the passage's slurs, also each slur's.

Versions:
  U  uncorrected (as Recommended plays it without Even levels; CC11 127)
  W  the job's correction: target each slur's median of 'short', glide nodes at the middles of the short windows
  N  the same with 'whole': target each slur's median of 'whole', nodes at the middles of the whole windows
Both fit as evenlevels.h fitGlide / even_levels_test.time_fit: per-note values solved (least squares, ridge 0.5) so
that the glide (CC11 linear in dB between the nodes, the ends held), smoothed as the envelope (one pole, 35 ms), flattens
the measured envelope (3-pass median, 5 ms grid) to each slur's median target from note 2's arrival to the last
note-off; sent as CC11 every 5 ms where it changes, the whole lowered to stay within 127 (shift; playback adds it back
by the patch volume: a constant gain, and the audio is loudness-matched anyway). The job's two rounds with the 1.28
factor (even_fast_checks.py): round 1 the fit divided by K = 1.28; round 2 where the residual (largest |note - its
slur's median|, in the version's own measure) stays above 1 dB: fitted again on round 1's measurement, divided by
round 1's measured gain (least squares slope of the measured against the predicted change of the notes but the first
and last, about their means, as evenlevelsjob.cpp), on top of round 1; kept: the round with the smaller median slur
range in the version's own measure (as the job).

Audio: pass 1 of U and of each correction's kept round, 0.5 s before the first note-on to 1.5 s after the last
note-off, 44.1 kHz, LAME 192 kbps CBR (ffmpeg libmp3lame). Loudness-matched: the three versions of a passage at the same
integrated loudness, ITU-R BS.1770 / EBU R128 (ffmpeg's ebur128 filter, gated, LUFS), at the highest level at which
every version's sample peak stays at or below -1 dBFS (checked again on the decoded mp3s).

Result (2026-10-13, Windows test VM, kthost + Kontakt 8 + SSO, 13 renders, ~9 min of audio in ~3 min; the job's
two rounds with K = 1.28 in round 1; sso_slow_slur_test.json). Median slur range / largest slur range, dB, 3-pass
median levels (pass-to-pass |diff| p50 0.03-0.22):
                 short (job's window)        whole note
  vc500   U      3.75 / 4.75                 2.17 / 5.65
          W      1.97 / 2.54  (round 2)      1.42 / 2.17
          N      3.22 / 3.44  (round 1)      0.63 / 0.74
  vn500   U      4.62 / 4.83                 2.25 / 4.14
          W      1.33 / 2.91  (round 1)      0.39 / 0.65
          N      2.05 / 4.02  (round 1)      0.38 / 0.40
  va1000  U      2.36 / 4.12                 2.01 / 3.50
          W      1.89 / 2.84  (round 2)      1.31 / 1.51
          N      4.05 / 4.85  (round 2)      0.56 / 0.95
Round 1's measured gain (measured / predicted change): 1.19-1.25 at 500 ms (K = 1.28 holds), 0.99-1.00 at 1000 ms.
Each correction flattens its own measure; the job's (W) also lowers the whole-note range (the fit flattens the whole
envelope either way), but in the short window neither gets below ~1.3-2 dB at these lengths. N leaves the short window
as uneven as U or more (va1000 4.05 against 2.36): flattening each note's whole length moves each note's start.
Which one sounds even is the listening test's question. Loudness match: U, W, N of each passage at the same BS.1770
integrated loudness (vc500 -15.2, vn500 -13.8, va1000 -17.5 LUFS); the corrections' renders were 3.0-3.7 (vc500,
vn500) and 3.9-6.2 (va1000) dB quieter than U before matching (their shift under CC11 127), so they got that much more
gain; every mp3's sample peak -1.4 dBFS or lower.
"""
import argparse
import json
import math
import os
import re
import statistics as st
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
from swell_vs_length import map_info, legato_delay, _interp  # noqa: E402

FINGERED, BOWED, SMOOTH, OVERLAP_DEFAULT = 69, 93, 5, 0.024
BOWED_BY_PATCH = {'Violas - Performance': 100}      # the map's legatoVelocity
WIN = 0.1364
STEP = 0.005
TAU = 0.035
RIDGE = 0.5
K = 1.28
FLOOR = 1.0
MAX_DB = 24.0          # evenlevels.h clampDb
NPASS = 3
PASS0, PASS_GAP_EXTRA = 1.0, 4.0
CC_LEAD = 0.6
LEAD, TAIL = 0.5, 1.5
MEASURES = ('short', 'whole')
OWN = {'W': 'short', 'N': 'whole'}

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def midi(n):
    m = re.fullmatch(r'([A-G])(#|b)?(-?\d)', n)
    p = NAMES[m.group(1)] + {'#': 1, 'b': -1, None: 0}[m.group(2)]
    return 12 * (int(m.group(3)) + 1) + p


PASSAGES = {
    'vc500': dict(patch='Celli - Performance', ioi=500, key='D minor',
                  slurs=['D3 F3 E3 G3', 'F3 A3 G3 Bb3', 'A3 G3 F3 E3', 'F3 E3 C#3 D3']),
    'vn500': dict(patch='Violins 1 - Performance', ioi=500, key='G major',
                  slurs=['D5 G5 F#5 E5', 'D5 E5 F#5 G5', 'B5 A5 G5 F#5', 'E5 D5 F#5 G5']),
    'va1000': dict(patch='Violas - Performance', ioi=1000, key='C major',
                   slurs=['C4 D4 E4 F4', 'G4 F4 E4 D4', 'E4 D4 B3 C4']),
}


def db(v):
    return 20 * math.log10(v / 127.0)


def jload(*p):
    return json.load(open(os.path.join(*p)))


# --- events ---------------------------------------------------------------------------------------------------------

def build(name, info):
    """notes (passage time: the first note's written start 0) and the base events (no CC11)"""
    P = PASSAGES[name]
    ioi = P['ioi'] / 1000
    overlap = info.get('overlap', OVERLAP_DEFAULT)
    bowed = BOWED_BY_PATCH.get(P['patch'], BOWED)
    notes = []
    k = 0
    for s, slur in enumerate(P['slurs']):
        for i, n in enumerate(slur.split()):
            notes.append(dict(k=k, p=midi(n), name=n, slur=s, written=round(k * ioi, 5)))
            k += 1
    ev = [(-CC_LEAD, 'cc', 1, 64)]
    for j, n in enumerate(notes):
        last = j == len(notes) - 1
        if j == 0:
            n['v'] = SMOOTH
            onset = _interp(info['smooth'], n['p']) / 1000
            n['on'] = round(n['written'] - onset, 5)
            n['d'] = round(onset, 4)            # arrival after the note-on as sent
        else:
            n['v'] = bowed if notes[j - 1]['slur'] != n['slur'] else FINGERED
            n['on'] = n['written']
            n['d'] = round(legato_delay(info, n['p'] - notes[j - 1]['p']) / 1000, 4)
        n['off'] = round(n['written'] + ioi + (0 if last else overlap), 5)
        n['arr'] = round(n['on'] + n['d'], 5)
        ev.append((n['on'], 'on', n['p'], n['v']))
        ev.append((n['off'], 'off', n['p'], 0))
    for j, n in enumerate(notes):
        n['wholeEnd'] = notes[j + 1]['arr'] if j + 1 < len(notes) else n['off']
    end = max(n['off'] for n in notes)
    return notes, ev, end


def write_events(path, ev):
    ev = sorted(ev, key=lambda e: (round(e[0], 6), {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
    with open(path, 'w', newline='\r\n') as f:
        for e in ev:
            f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')


def passes(meta, ev):
    out = []
    for p0 in meta['passes']:
        out += [(round(t + p0, 6), k, a, b) for t, k, a, b in ev]
    return out


RUN_CMD = r'''@echo off
rem slow slur test (branch slow-slur-test): renders each name|patch of list%1.txt with kthost (MuseScore's resaved
rem setup, copied); only C:\claude\sst is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\sst\st mkdir C:\claude\sst\st
if not exist C:\claude\sst\wav mkdir C:\claude\sst\wav
for %%f in ("Violins 1 - Performance" "Violas - Performance" "Celli - Performance") do if not exist "C:\claude\sst\st\%%~f.vst3state" copy /y "%SRC%\%%~f.vst3state" C:\claude\sst\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\sst\list%1.txt") do (
  echo %%a start %time%>>C:\claude\sst\run.out
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\sst\st\%%b.vst3state" "C:\claude\sst\ev\%%a.txt" "C:\claude\sst\wav\%%a.wav" --tail 3 --wait 600 > "C:\claude\sst\wav\%%a.log" 2>&1
  echo %%a done>>C:\claude\sst\run.out
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE %1>>C:\claude\sst\run.out
'''


def events(work):
    infos = map_info()
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    meta = {}
    lines = []
    for name, P in PASSAGES.items():
        notes, ev, end = build(name, infos[P['patch']])
        first = min(n['on'] for n in notes)
        gap = end - first + CC_LEAD + PASS_GAP_EXTRA
        m = dict(name=name, patch=P['patch'], ioi=P['ioi'], key=P['key'], slurs=P['slurs'], notes=notes, base=ev,
                 end=end, first=first, passes=[round(PASS0 + CC_LEAD + p * gap, 4) for p in range(NPASS)])
        meta[name] = m
        write_events(os.path.join(work, 'ev', name + '_U.txt'), passes(m, ev + [(-CC_LEAD, 'cc', 11, 127)]))
        lines.append(f'{name}_U|{P["patch"]}')
        print(name, len(notes), 'notes,', round(end - first, 2), 's;',
              ' '.join(f'{n["name"]}:{n["v"]}/{round(n["d"] * 1000)}' for n in notes))
    json.dump(meta, open(os.path.join(work, 'meta.json'), 'w'), indent=1)
    with open(os.path.join(work, 'list1.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)


# --- measure --------------------------------------------------------------------------------------------------------

def grid(m):
    return np.arange(m['first'] - 0.2, m['end'] + 1.0, STEP)


def spans(m, measure):
    return [(n['arr'], n['arr'] + WIN) if measure == 'short' else (n['arr'], n['wholeEnd']) for n in m['notes']]


def levels_on(t, env, m, measure, offset=0.0):
    out = []
    for a, b in spans(m, measure):
        sel = (t >= a + offset) & (t < b + offset)
        out.append(float(np.mean(env[sel])))
    return out


def measure(work, name):
    import soundfile as sf
    from loudness import perceived_envelope
    meta = jload(work, 'meta.json')
    m = meta[name.split('_')[0]]
    x, sr = sf.read(os.path.join(work, 'wav', name + '.wav'), always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    per = {ms: [levels_on(t_env, env, m, ms, p0) for p0 in m['passes']] for ms in MEASURES}
    med = {ms: [st.median(v) for v in zip(*per[ms])] for ms in MEASURES}
    tg = grid(m)
    envm = np.median([np.interp(tg + p0, t_env, env) for p0 in m['passes']], axis=0)
    peak = float(np.max(np.abs(x)))
    os.makedirs(os.path.join(work, 'res'), exist_ok=True)
    json.dump(dict(passes=per, median=med, sr=sr, peak=peak, env=[round(float(v), 3) for v in envm]),
              open(os.path.join(work, 'res', name + '.json'), 'w'))
    for ms in MEASURES:
        s = slur_stats(m, med[ms])
        print(name, ms, 'slur ranges', [round(r, 2) for r in s['ranges']], 'median', round(s['rangeMedian'], 2),
              'residual', round(s['residual'], 2), '| pass-to-pass |d| p50',
              round(st.median(abs(a - b) for a, b in zip(per[ms][0], per[ms][1])), 2))


def slur_stats(m, lv):
    notes = m['notes']
    by = {}
    for k in range(1, len(notes) - 1):
        by.setdefault(notes[k]['slur'], []).append(k)
    med = {s: st.median(lv[k] for k in ks) for s, ks in by.items()}
    ranges = [max(lv[k] for k in ks) - min(lv[k] for k in ks) for s, ks in sorted(by.items()) if len(ks) > 1]
    dev = [0.0] + [lv[k] - med[notes[k]['slur']] for k in range(1, len(notes) - 1)] + [0.0]
    return dict(median=med, ranges=ranges, rangeMedian=st.median(ranges), dev=dev, residual=max(abs(x) for x in dev))


# --- correct --------------------------------------------------------------------------------------------------------

def smooth(g):
    a = 1 - math.exp(-STEP / TAU)
    out = np.empty_like(g)
    s = g[0]
    for i, x in enumerate(g):
        s += a * (x - s)
        out[i] = s
    return out


def centres(m, measure):
    return np.array([(a + b) / 2 for a, b in spans(m, measure)])


def basis(m, measure):
    t = grid(m)
    c = centres(m, measure)
    n = len(m['notes'])
    M = np.zeros((len(t), n))
    for j in range(n):
        e = np.zeros(n)
        e[j] = 1.0
        M[:, j] = smooth(np.interp(t, c, e))
    return t, M


def fit(m, measure, env, lv):
    """per-note glide values (dB) flattening env to each slur's median of lv (evenlevels.h fitGlide)"""
    notes = m['notes']
    t, M = basis(m, measure)
    s = slur_stats(m, lv)
    starts = np.array([n['arr'] for n in notes])
    k = np.clip(np.searchsorted(starts, t, side='right') - 1, 0, None)
    target = np.array([s['median'].get(notes[i]['slur'], np.nan) for i in k])
    w = ((t >= starts[1]) & (t < notes[-1]['off']) & ~np.isnan(target)).astype(float)
    r = np.where(w > 0, np.nan_to_num(target) - np.asarray(env), 0.0)
    Wm = M * w[:, None]
    du = np.linalg.solve(Wm.T @ M + RIDGE * np.eye(len(notes)), Wm.T @ r)
    return du


def predicted(m, measure, v):
    """the change each note's window (of this measure) hears of glide values v"""
    t, M = basis(m, measure)
    g = M @ np.asarray(v)
    return [float(g[(t >= a) & (t < b)].mean()) for a, b in spans(m, measure)]


def cc_events(m, measure, v):
    c = centres(m, measure)
    t = np.arange(-CC_LEAD, m['end'] + 0.3, STEP)
    g = np.interp(t, c, v)
    shift = max(0.0, float(np.max(v)) - db(127.4))
    out, last = [], None
    for tt, x in zip(t, g):
        y = int(max(1, min(127, round(127 * 10 ** ((x - shift) / 20)))))
        if y != last:
            out.append((round(float(tt), 5), 'cc', 11, y))
            last = y
    return out, shift


def gain_of(m, measure, v1, lv0, lv1):
    pred = np.array(predicted(m, measure, v1))[1:-1]
    y = (np.array(lv1) - np.array(lv0))[1:-1]
    x = pred - pred.mean()
    y = y - y.mean()
    sxy, sxx = float((x * y).sum()), float((x * x).sum())
    g = sxy / sxx if sxx > 1e-9 and sxy > 0 else 1.0
    if g < 0.5:
        print('  gain', round(g, 3), 'below 0.5: 0.5 used')
    return max(0.5, g)


def correct(work, rnd):
    meta = jload(work, 'meta.json')
    vpath = os.path.join(work, 'values.json')
    vals = jload(vpath) if os.path.exists(vpath) else {}
    lines = []
    for name, m in meta.items():
        u = jload(work, 'res', name + '_U.json')
        for ver, ms in OWN.items():
            key = f'{name}_{ver}'
            if rnd == 1:
                du = fit(m, ms, u['env'], u['median'][ms])
                v = np.clip(du / K, -MAX_DB, MAX_DB)
                rec = dict(measure=ms, v1=[float(x) for x in v], k=K)
            else:
                rec = vals[key]
                r1 = jload(work, 'res', f'{key}1.json')
                s1 = slur_stats(m, r1['median'][ms])
                g = gain_of(m, ms, rec['v1'], u['median'][ms], r1['median'][ms])
                rec['gain1'] = g
                rec['residual1'] = s1['residual']
                if s1['residual'] <= FLOOR:
                    print(key, 'round 1 residual', round(s1['residual'], 2), '<= floor: no round 2')
                    rec['round2'] = False
                    vals[key] = rec
                    continue
                du = fit(m, ms, r1['env'], r1['median'][ms])
                v = np.clip(np.array(rec['v1']) + du / g, -MAX_DB, MAX_DB)
                rec['v2'] = [float(x) for x in v]
                rec['round2'] = True
            ev, shift = cc_events(m, ms, v)
            rec[f'shift{rnd}'] = shift
            rec[f'cc{rnd}'] = len(ev)
            rec[f'pred{rnd}'] = predicted(m, ms, v)
            vals[key] = rec
            write_events(os.path.join(work, 'ev', f'{key}{rnd}.txt'), passes(m, m['base'] + ev))
            lines.append(f'{key}{rnd}|{m["patch"]}')
            print(f'{key}{rnd}', 'values', ' '.join(f'{x:+.1f}' for x in v), 'shift', round(shift, 2),
                  len(ev), 'CC11' + (f'; gain1 {rec["gain1"]:.2f}' if rnd == 2 else ''))
    json.dump(vals, open(vpath, 'w'), indent=1)
    with open(os.path.join(work, f'list{rnd + 1}.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')


# --- audio ----------------------------------------------------------------------------------------------------------

def lufs(path):
    r = subprocess.run(['ffmpeg', '-hide_banner', '-nostats', '-i', path, '-af', 'ebur128=peak=sample', '-f', 'null', '-'],
                       capture_output=True, text=True)
    txt = r.stderr[r.stderr.rfind('Summary:'):]
    i = float(re.search(r'I:\s+(-?[\d.]+) LUFS', txt).group(1))
    pk = float(re.search(r'Peak:\s+(-?[\d.inf]+) dBFS', txt).group(1))
    return i, pk


def rnd2(v, n=2):
    return [round(float(x), n) for x in v]


def audio(work, out, out_json):
    import soundfile as sf
    meta = jload(work, 'meta.json')
    vals = jload(work, 'values.json')
    adir = os.path.join(out, 'audio')
    os.makedirs(adir, exist_ok=True)
    summary = {}
    for name, m in meta.items():
        chosen = {'U': name + '_U'}
        rounds = {}
        u = jload(work, 'res', name + '_U.json')
        for ver, ms in OWN.items():
            key = f'{name}_{ver}'
            cand = [r for r in (1, 2) if os.path.exists(os.path.join(work, 'res', f'{key}{r}.json'))]
            best = min(cand, key=lambda r: slur_stats(m, jload(work, 'res', f'{key}{r}.json')['median'][ms])['rangeMedian'])
            chosen[ver] = f'{key}{best}'
            rounds[ver] = dict(rounds=cand, kept=best)
        a, b = m['passes'][0] + m['first'] - LEAD, m['passes'][0] + m['end'] + TAIL
        segs, loud = {}, {}
        for ver, f in chosen.items():
            x, sr = sf.read(os.path.join(work, 'wav', f + '.wav'), always_2d=True)
            assert sr == 44100, sr
            seg = x[int(round(a * sr)):int(round(b * sr))]
            tmp = os.path.join(work, f'tmp_{name}_{ver}.wav')
            sf.write(tmp, seg, sr, subtype='FLOAT')
            loud[ver] = lufs(tmp)
            os.remove(tmp)
            segs[ver] = seg
        # the common loudness: the highest at which every version's sample peak stays <= -1.2 dBFS (mp3 margin)
        target = min(loud[v][0] + (-1.2 - 20 * math.log10(float(np.max(np.abs(segs[v]))))) for v in segs)
        gains, finals = {}, {}
        for ver, seg in segs.items():
            g = target - loud[ver][0]
            for attempt in range(4):
                tmp = os.path.join(work, f'tmp_{name}_{ver}.wav')
                sf.write(tmp, seg * 10 ** (g / 20), 44100, subtype='PCM_24')
                mp3 = os.path.join(adir, f'{name}_{ver}.mp3')
                subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-codec:a', 'libmp3lame', '-b:a', '192k',
                                '-ar', '44100', mp3], check=True)
                os.remove(tmp)
                li, pk = lufs(mp3)
                if pk <= -1.0:
                    break
                g -= pk + 1.05        # lower all three alike next time round: here only this one; re-matched below
            gains[ver] = round(g, 2)
            finals[ver] = dict(lufs=li, peak=pk)
        # if a version had to come down, bring the others to it (re-encode)
        low = min(gains[v] - (target - loud[v][0]) for v in gains)
        if low < -1e-6:
            for ver, seg in segs.items():
                g = target + low - loud[ver][0]
                tmp = os.path.join(work, f'tmp_{name}_{ver}.wav')
                sf.write(tmp, seg * 10 ** (g / 20), 44100, subtype='PCM_24')
                mp3 = os.path.join(adir, f'{name}_{ver}.mp3')
                subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-codec:a', 'libmp3lame', '-b:a', '192k',
                                '-ar', '44100', mp3], check=True)
                os.remove(tmp)
                li, pk = lufs(mp3)
                gains[ver] = round(g, 2)
                finals[ver] = dict(lufs=li, peak=pk)
        meas = {}
        for ver, f in chosen.items():
            r = jload(work, 'res', f + '.json')
            meas[ver] = {}
            for ms in MEASURES:
                s = slur_stats(m, r['median'][ms])
                meas[ver][ms] = dict(slurRanges=rnd2(s['ranges']), rangeMedian=round(s['rangeMedian'], 2),
                                     rangeMax=round(max(s['ranges']), 2), residual=round(s['residual'], 2),
                                     levels=rnd2(r['median'][ms]),
                                     passToPassP50=round(st.median(abs(x - y) for x, y in zip(r['passes'][ms][0], r['passes'][ms][1])), 2))
        allr = {}
        for ver in OWN:
            for rd in rounds[ver]['rounds']:
                r = jload(work, 'res', f'{name}_{ver}{rd}.json')
                allr[f'{ver}{rd}'] = {ms: round(slur_stats(m, r['median'][ms])['rangeMedian'], 2) for ms in MEASURES}
        corr = {}
        for ver in OWN:
            rec = vals[f'{name}_{ver}']
            kept = rounds[ver]['kept']
            corr[ver] = dict(measure=rec['measure'], keptRound=kept, k=K, gainRound1=round(rec.get('gain1', float('nan')), 3),
                             values=rnd2(rec['v%d' % kept]), shift=round(rec['shift%d' % kept], 2),
                             ccMessages=rec['cc%d' % kept])
        summary[name] = dict(
            description=f'{m["patch"]}, {m["key"]}, {m["ioi"]} ms notes, CC1 64: ' + ' | '.join(m['slurs']),
            patch=m['patch'], noteMs=m['ioi'], slurs=m['slurs'],
            notes=[dict(name=n['name'], p=n['p'], vel=n['v'], slur=n['slur'], arrivalMs=round(n['d'] * 1000, 1),
                        wholeMs=round((n['wholeEnd'] - n['arr']) * 1000, 1)) for n in m['notes']],
            measures=meas, roundRangeMedians=allr, corrections=corr,
            loudness=dict(method='ITU-R BS.1770 / EBU R128 integrated loudness (ffmpeg ebur128, gated), each version '
                                 'gained to the same LUFS: the highest at which every sample peak stays <= -1 dBFS',
                          lufsBefore={v: loud[v][0] for v in loud}, gainDb=gains,
                          mp3={v: finals[v] for v in finals}),
            files={v: f'{name}_{v}.mp3' for v in chosen}, renders=chosen,
            clip=dict(lead=LEAD, tail=TAIL, seconds=round(b - a, 3)))
        print(name, 'gains', gains, 'mp3', finals)
        for ver in chosen:
            print(' ', ver, {ms: (meas[ver][ms]['rangeMedian'], meas[ver][ms]['rangeMax']) for ms in MEASURES})
    json.dump(summary, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    about = ('SSO string Performance patches, slow slurs (500 / 1000 ms notes), uncorrected (U) and evened out by '
             'CC11 with the even-levels job\'s short window (W) or a whole-note window (N); '
             'tools/soundlibraries/slow_slur_test.py has the method and result. Measured 2026-10-13 on the Windows '
             'test VM: kthost (offline VST 3 host), Kontakt 8 + SSO, each patch from MuseScore\'s resaved setup '
             '(library defaults), 44.1 kHz, 3 passes. measures.<V>.<short|whole>: slur ranges (dB, max - min, the '
             'passage\'s first and last notes left out), their median and max, the residual, each note\'s level '
             '(3-pass median). Measurement only: the renderer does not read this file.')
    slim = {n: {k: v for k, v in s.items() if k not in ('files', 'renders')} for n, s in summary.items()}
    json.dump(dict(_about=about, passages=slim), open(out_json, 'w'), separators=(',', ':'))
    print('wrote', out_json)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('events').add_argument('work')
    m = sub.add_parser('measure')
    m.add_argument('work')
    m.add_argument('name')
    c = sub.add_parser('correct')
    c.add_argument('work')
    c.add_argument('round', type=int, choices=(1, 2))
    a = sub.add_parser('audio')
    a.add_argument('work')
    a.add_argument('out')
    a.add_argument('-o', default=os.path.join(HERE, 'sso_slow_slur_test.json'))
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.work)
    elif o.cmd == 'measure':
        measure(o.work, o.name)
    elif o.cmd == 'correct':
        correct(o.work, o.round)
    else:
        audio(o.work, o.out, o.o)


if __name__ == '__main__':
    main()
