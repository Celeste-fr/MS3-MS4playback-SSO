#!/usr/bin/env python3
"""The perturbation scan (2026-10-13, branch perturbation-scan): in SSO's five string sections' Performance legato,
how far along a slurred phrase does a change to ONE note move the OTHER notes' levels? Written for this check, not
taken from anywhere. Why: Even Out Levels (libmscore/evenlevels.h) renders and measures each score's slurred passages
on the VM; if a note's level depended only on its own and its neighbours' notes, a table measured once over n-grams
could replace that. If a change carries far along the phrase, the level is set by state the table can't see.
Measurement only: the renderer doesn't read the result (sso_perturbation_scan.json).

  perturbation_scan.py events <work>              kthost event files <work>/ev/<name>.txt + .json, list.txt, run.cmd
  perturbation_scan.py analyze <folder> <name>    (on the VM) <name>.wav -> <name>.res.json (each phrase's envelope)
  perturbation_scan.py report <res folder> [-o sso_perturbation_scan.json]

Phrases (transition_level_scan.py run_phrases, the validation runs): a spiccato note (112) then four slurs of four
136 ms notes as MuseScore plays Whence (each slur's first note a bowed transition 93, the rest fingered 69, the note
before off 24 ms after the next note-on), CC1 64, CC11 127, 1.2 s between phrases. Notes are numbered 0 (the attack)
to 16; slurs 1-4 are notes 1-4, 5-8, 9-12, 13-16. Per section 10 base phrases (Celli: the generator's 2nd to 11th,
the 2nd being Whence 12-TET bar 2), each in six versions:
  B, B'   the base, twice (the noise floor: the same phrase at another place in the same render)
  P2, P6  note k = 2 / 6 (the 2nd note of slur 1 / 2, fingered) moved by s semitones, s by base index from
          +2 -2 +1 -1 (the first that stays in range and differs from both neighbours): changes the transitions into
          k and into k + 1
  V2, V6  note k = 2 / 6 sent bowed (93) instead of fingered (69): changes only k's own transition
60 phrases per render, one render per section x take; takes A (shuffled, fixed seed), B (A reversed), C (another
shuffle): 15 renders.
Level of a note (report): 'swell', the mean perceived loudness (tools/playbackverify/loudness.py, ~dB) over 136.4 ms
from its note-on + the map's legatoDelay for its interval (sso_slur_swell.json's window). For a version against the
base, delta(k + d) = level in the version - level in B, same take; noise = B' - B, same note, same take. d = 0 and
d = 1 have changed transitions under P (d = 0 under V); d >= 2 (V: d >= 1) is the carry. The map's legatoDelay is
70-440 ms (mostly 170-330), longer than a note, so the window of note j lies while notes j + 1 / j + 2 sound: d = -1
and -2 hear the changed note itself; d <= -3 can't be touched by the change (a control). Also 3-take means (version mean - B mean against B' mean - B mean).

Result (2026-10-13, Windows VM, Kontakt 8 + SSO via kthost, 15 renders, 53 min of audio in 14 min): the effect is
local. 3-take means, all sections, |delta| p50 / p90 against the noise floor B' - B (p50 0.09-0.14, p90 0.29-0.46):
  P (pitch moved):     d=+1 0.60/1.82   +2 0.31/0.86   +3 0.22/0.48   +4 0.14/0.38   +5..+14 0.06-0.14 (= noise)
  V (sent bowed):      d=+0 1.04/2.97   +1 0.33/2.29   +2 0.24/1.39   +3 0.13/0.68   +4 0.13/0.49   +5.. = noise
  both, behind:        d=-1 0.72-1.35 (the window lag), d=-2 0.15-0.18 / 0.55-0.81, d<=-3 = noise (the control)
Violins 1 / 2, Violas, Celli are at noise from d=+3 (P) / +2 (V); Basses carry further (V: +2 1.39, +3 0.64, +5 0.33
p50 against 0.12-0.18). A change in slur 1 doesn't move slurs 3-4 (mean shift per phrase within the B' - B spread in
every section). No hidden phrase-long state; a note's level depends on about 2 notes before it and 1 after (Basses
~4 before). An n-gram table would need, per section at CC1 64 and 136 ms only, pitch x 3 intervals (12 each) x
bowed / fingered of 3 notes: 203 pitches x 1728 x 4-8 = 1.4-2.8 M cells, ~72-145 h of VM time at the measured speed
(16 legato notes per VM second per take, 3 takes); every 3rd pitch interpolated, 24-48 h, other dynamics and lengths
not included.
"""
import argparse
import json
import os
import random
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
from transition_level_scan import (SECTIONS, RANGES, FINGERED, BOWED, SPICCATO, OVERLAP, MAP, avoid, run_phrases,
                                   map_info, legato_delay)

KS = (2, 6)
SHIFTS = (2, -2, 1, -1)
TAKES = ('A', 'B', 'C')
NOTE, GAP, NBASE = 0.136, 1.2, 10


def bases(sec):
    ps = run_phrases(sec)
    return ps[1:1 + NBASE] if sec == 'vc' else ps[:NBASE]


def versions(sec):
    """[(base index, version name, pitches, velocities, shift or None)]"""
    lo, hi = RANGES[sec]
    out = []
    for b, ps in enumerate(bases(sec)):
        vels = [avoid(sec, ps[0], SPICCATO)] + [BOWED if k % 4 == 0 else FINGERED for k in range(len(ps) - 1)]
        out.append((b, 'B', ps, vels, None))
        out.append((b, "B'", ps, vels, None))
        for k in KS:
            order = SHIFTS[b % 4:] + SHIFTS[:b % 4]
            for s in order:
                q = ps[k] + s
                if lo <= q <= hi and q != ps[k - 1] and q != ps[k + 1]:
                    pp = list(ps)
                    pp[k] = q
                    out.append((b, f'P{k}', pp, vels, s))
                    break
            vv = list(vels)
            vv[k] = BOWED
            out.append((b, f'V{k}', ps, vv, None))
    return out


def take_order(sec, take):
    vs = versions(sec)
    order = list(range(len(vs)))
    if take in ('A', 'B'):
        random.Random('pertA' + sec).shuffle(order)
        if take == 'B':
            order.reverse()
    else:
        random.Random('pertC' + sec).shuffle(order)
    return [vs[i] for i in order]


RUN_CMD = r'''@echo off
rem perturbation scan (branch perturbation-scan): renders list.txt (name|patch) with kthost, analyses each wav here
rem and deletes it (literal path); only C:\claude\pts is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\pts\st mkdir C:\claude\pts\st
if not exist C:\claude\pts\r mkdir C:\claude\pts\r
for %%f in ("Violins 1 - Performance" "Violins 2 - Performance" "Violas - Performance" "Celli - Performance" "Basses - Performance") do if not exist "C:\claude\pts\st\%%~f.vst3state" copy /y "%SRC%\%%~f.vst3state" C:\claude\pts\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
if errorlevel 1 (echo share login failed>>C:\claude\pts\run.out & exit /b 1)
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\pts\list.txt") do (
  if exist C:\claude\pts\stop.txt goto :end
  if not exist "C:\claude\pts\r\%%a.res.json" (
    echo %%a start !time!>>C:\claude\pts\run.out
    set W=600
    if "%%b"=="Basses - Performance" set W=20
    C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\pts\st\%%b.vst3state" "C:\claude\pts\ev\%%a.txt" "C:\claude\pts\r\%%a.wav" --tail 3 --wait !W! > "C:\claude\pts\r\%%a.log" 2>&1
    "C:\Program Files\Python312\python.exe" C:\claude\pts\tools\perturbation_scan.py analyze C:\claude\pts\r %%a >> "C:\claude\pts\r\%%a.log" 2>&1
    if exist "C:\claude\pts\r\%%a.res.json" del "C:\claude\pts\r\%%a.wav"
    echo %%a done !time!>>C:\claude\pts\run.out
  )
)
:end
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE>>C:\claude\pts\run.out
'''


def events(work):
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    lst, total = [], 0.0
    for take in TAKES:
        for sec, patch in SECTIONS.items():
            ev = [(0.0, 'cc', 1, 64), (0.0, 'cc', 11, 127)]
            notes, t = [], 1.0
            for b, name, ps, vels, s in take_order(sec, take):
                ons = []
                for k, (p, v) in enumerate(zip(ps, vels)):
                    last = k == len(ps) - 1
                    ev.append((t, 'on', p, v))
                    ev.append((t + NOTE + (0 if last else OVERLAP), 'off', p, 0))
                    ons.append(round(t, 5))
                    t += NOTE
                notes.append(dict(base=b, version=name, pitches=ps, vels=vels, shift=s, ons=ons))
                t += GAP
            fn = f'{take}_{sec}'
            ev.sort(key=lambda e: (e[0], {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
            with open(os.path.join(work, 'ev', fn + '.txt'), 'w', newline='\r\n') as f:
                for e in ev:
                    f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')
            length = max(e[0] for e in ev) + 3
            json.dump(dict(patch=patch, section=sec, take=take, length=length, notes=notes),
                      open(os.path.join(work, 'ev', fn + '.json'), 'w'))
            lst.append(f'{fn}|{patch}')
            total += length
    with open(os.path.join(work, 'list.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lst) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(lst), 'files', round(total / 60, 1), 'min of audio')


def analyze(folder, name):
    import numpy as np
    import soundfile as sf
    from loudness import perceived_envelope
    meta = json.load(open(os.path.join(folder, '..', 'ev', name + '.json')))
    x, sr = sf.read(os.path.join(folder, name + '.wav'), always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    for n in meta['notes']:
        ts = np.arange(n['ons'][0] - 0.2, n['ons'][-1] + 0.8, 0.005)
        n['env'] = [round(float(v), 2) for v in np.interp(ts, t_env, env)]
    json.dump(meta, open(os.path.join(folder, name + '.res.json'), 'w'), separators=(',', ':'))
    print(name, len(meta['notes']), 'phrases analysed')


# --- report ---------------------------------------------------------------------------------------------------------

def levels(n, info):
    """swell level of notes 1..16 (index 0 None)"""
    ps, ons, env = n['pitches'], n['ons'], n['env']
    t0 = ons[0] - 0.2
    out = [None]
    for k in range(1, len(ps)):
        a = ons[k] + legato_delay(info, ps[k] - ps[k - 1]) / 1000
        i0, i1 = int(round((a - t0) / 0.005)), int(round((a + 0.1364 - t0) / 0.005))
        out.append(round(st.fmean(env[i0:i1]), 2))
    return out


def q(v, f):
    v = sorted(v)
    return v[min(len(v) - 1, int(f * len(v)))] if v else None


def report(res, out_path, map_path):
    infos = map_info(map_path)
    data = {}       # sec -> base -> version -> take -> levels
    meta = {}
    for fn in sorted(os.listdir(res)):
        if not fn.endswith('.res.json'):
            continue
        d = json.load(open(os.path.join(res, fn)))
        info = infos[d['patch']]
        for n in d['notes']:
            data.setdefault(d['section'], {}).setdefault(n['base'], {}).setdefault(n['version'], {})[d['take']] = \
                levels(n, info)
            meta.setdefault(d['section'], {}).setdefault(n['base'], {})[n['version']] = dict(
                pitches=n['pitches'], vels=n['vels'], shift=n['shift'])
    DS = list(range(-5, 15))
    # rows: (sec, kind 'P'/'V', d, single-take |delta|, take-mean |delta|, noise single, noise mean)
    stats = {}

    def add(key, val):
        stats.setdefault(key, []).append(abs(val))
    for sec, bs in data.items():
        for b, vs in bs.items():
            B, B2 = vs['B'], vs["B'"]
            takes = sorted(set(B) & set(B2))
            for k in KS:
                for kind in ('P', 'V'):
                    V = vs.get(f'{kind}{k}')
                    if not V:
                        continue
                    tk = [t for t in takes if t in V]
                    for d in DS:
                        j = k + d
                        if not 1 <= j <= 16:
                            continue
                        for t in tk:
                            add((sec, kind, d, 'single'), V[t][j] - B[t][j])
                            add((sec, kind, d, 'noise1'), B2[t][j] - B[t][j])
                        if len(tk) >= 2:
                            mv = st.fmean(V[t][j] for t in tk)
                            mb = st.fmean(B[t][j] for t in tk)
                            mb2 = st.fmean(B2[t][j] for t in tk)
                            add((sec, kind, d, 'mean'), mv - mb)
                            add((sec, kind, d, 'noiseM'), mb2 - mb)
                        # slur 3-4 (notes 9-16) after a change in slur 1 (k = 2): kept by d anyway
    secs = list(data)

    def summ(sec, kind, d, what):
        v = []
        for s in (secs if sec == 'all' else [sec]):
            v += stats.get((s, kind, d, what), [])
        return dict(n=len(v), p50=round(q(v, 0.5), 2), p90=round(q(v, 0.9), 2)) if v else None
    table = {}
    for sec in ['all'] + secs:
        for kind in ('P', 'V'):
            for d in DS:
                row = {w: summ(sec, kind, d, w) for w in ('single', 'noise1', 'mean', 'noiseM')}
                if row['single']:
                    table.setdefault(sec, {}).setdefault(kind, {})[str(d)] = row
    # whole slurs: mean |delta| over slur 3-4 (notes 9-16) after a change at k = 2, per phrase, take means
    far = {}
    for sec, bs in data.items():
        for kind in ('P', 'V'):
            dv, nv = [], []
            for b, vs in bs.items():
                V = vs.get(f'{kind}2')
                tk = [t for t in vs['B'] if t in vs["B'"] and V and t in V]
                if len(tk) < 2:
                    continue
                m = lambda X, j: st.fmean(X[t][j] for t in tk)
                dv.append(st.fmean(m(V, j) - m(vs['B'], j) for j in range(9, 17)))
                nv.append(st.fmean(m(vs["B'"], j) - m(vs['B'], j) for j in range(9, 17)))
            far.setdefault(sec, {})[kind] = dict(
                slur34MeanDelta=[round(x, 2) for x in dv], slur34MeanNoise=[round(x, 2) for x in nv])
    for kind in ('P', 'V'):
        print(f'\n{kind}: |delta| of note k+d, all sections, dB: single take p50/p90 (noise B\'-B) | 3-take mean '
              f'p50/p90 (noise)')
        for d in DS:
            r = table['all'][kind].get(str(d))
            if r:
                f = lambda x: f'{x["p50"]:.2f}/{x["p90"]:.2f}' if x else '-'
                print(f'  d={d:+3d}  {f(r["single"])} ({f(r["noise1"])})   {f(r["mean"])} ({f(r["noiseM"])})'
                      f'  n={r["single"]["n"]}')
    for sec in secs:
        line = []
        for kind in ('P', 'V'):
            for d in (1, 2, 3, 5, 8):
                r = table[sec][kind].get(str(d))
                if r and r['mean']:
                    line.append(f'{kind}{d:+d} {r["mean"]["p50"]:.2f}/{r["noiseM"]["p50"]:.2f}')
        print(sec, ' '.join(line))
    for sec in secs:
        for kind in ('P', 'V'):
            f = far[sec][kind]
            print(sec, kind, 'slur 3-4 mean shift after a change at k=2 (take means):',
                  ' '.join(f'{x:+.2f}' for x in f['slur34MeanDelta']), '| noise',
                  ' '.join(f'{x:+.2f}' for x in f['slur34MeanNoise']))
    result = dict(
        _about='SSO string Performance patches: how far a change to one note of a slurred phrase moves the other '
               'notes\' levels. tools/soundlibraries/perturbation_scan.py (its header has the method and result). '
               'Measured 2026-10-13 on the Windows test VM: kthost (offline VST 3 host), Kontakt 8 + SSO, each patch '
               'from MuseScore\'s resaved setup (library defaults), 44.1 kHz. levels: per section / base / version / '
               'take, the swell level (perceived loudness, tools/playbackverify/loudness.py, ~dB, mean over 136.4 ms '
               'from the note-on + the map\'s legatoDelay) of notes 0-16 (0, the attack: null). table: |delta| of note '
               'k + d (version - B, same take: single; 3-take means: mean) and the noise floor (B\' - B: noise1, '
               'noiseM), p50 / p90, per section and all. far: per base, the mean delta over slurs 3-4 (notes 9-16) '
               'after a change at k = 2, take means. Measurement only: the renderer does not read this file.',
        method=dict(note=NOTE, overlap=OVERLAP, gap=GAP, cc1=64, cc11=127, ks=KS, shifts=SHIFTS, takes=TAKES,
                    velocities=dict(spiccato=SPICCATO, bowed=BOWED, fingered=FINGERED)),
        phrases=meta, levels=data, table=table, far=far)
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
    r.add_argument('-o', default=os.path.join(HERE, 'sso_perturbation_scan.json'))
    r.add_argument('--map', default=MAP)
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.work)
    elif o.cmd == 'analyze':
        analyze(o.folder, o.name)
    else:
        report(o.res, o.o, o.map)


if __name__ == '__main__':
    main()
