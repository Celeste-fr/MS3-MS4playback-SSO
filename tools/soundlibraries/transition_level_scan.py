#!/usr/bin/env python3
"""The transition level scan (2026-10-12, branch transition-levels): how loud a legato note of SSO's five string
sections' Performance patches sounds depending on the note it is reached from, against the same note reached by an
attack. Written for this check, not taken from anywhere. Why: the owner hears a swell in Whence 12-TET bar 2, Celli
(slurs of four sixteenths 51 52 51 49, every note sent at the same level; sso_slur_swell.json: perceived 32.8 35.1
35.3 32.0): the samples differ by where the transition comes from. Measurement only: the renderer doesn't read the
result (sso_transition_levels.json).

  transition_level_scan.py events <work> [--map <xml>]
      kthost event files <work>/ev/<name>.txt ("seconds on|off|cc a b") + <name>.json (the cells), list.txt
      ("name|state", in run order) and run.cmd for the Windows VM (one share login; each file rendered by kthost,
      analysed there, its wav deleted by literal path).
  transition_level_scan.py analyze <folder> <name>          (on the VM) <name>.wav + <name>.json -> <name>.res.json
  transition_level_scan.py table <res folder> [-o sso_transition_levels.json]
  transition_level_scan.py whence <sso_transition_levels.json>   bar 2 of Whence 12-TET, Celli, predicted

Cells (one file per patch x condition x take, about 10 min of audio each; CC1 set once at the start, CC11 127):
  legato  w -> x -> y -> z, each of the first three L long, z 0.4 s, the note before off 24 ms after the next
          note-on (the map's overlap, as MuseScore plays a slur). y = the destination, x = y - interval; w = x - 2
          (x + 2 at the range's bottom) so x is itself a transition at the same speed, as in a run; z = y + 2 (y - 2 at
          the top). w is an attack (spiccato 112; Celli 47-48: 99, the map's avoid), x and z fingered (69), y at the
          transition velocity under test: fingered 69 or bowed 93 (performancetechnique.h's values).
  attack  y alone by an attack, L long: spiccato 112 (avoid as above) or smooth 5, then z (fingered 69).
  1.0 s of silence after each cell. Takes: A in grid order, B reversed, C shuffled (fixed seed).
  Conditions: S64 (L 136 ms, Whence's sixteenths at 110, CC1 64; every semitone of the patch's legato range,
  intervals +-1 2 3 4 5 7 12 with x in range), L64 (L 600 ms, CC1 64, every 2nd semitone), S32 / S112 (136 ms at
  CC1 32 / 112, every 3rd semitone); Celli 49 51 52 in every condition. S64 three takes, L64 three, S32 / S112 two.
  R (validation): run_phrases, 30 phrases per section of four slurs of four 136 ms notes as MuseScore plays Whence
  (first note of each slur bowed 93, the rest fingered 69, overlap 24 ms), random walks (steps +-1 .. +-5, +-7), Celli
  also Whence bar 2 itself; three takes.
  kthost's probe note is pitch 60 (outside Basses - Performance's range): run.cmd gives Basses --wait 20, else each
  Basses file waits the full --wait for a probe that never sounds (their renders were complete either way).

Result (2026-10-12, Windows VM, Kontakt 8 + SSO via kthost, 159 renders, 19.6 h of audio in 2.4 h; read with
transition_levels_report.py): isolated, a destination's level moves with the interval it comes from by p50 0.4-0.8,
p90 1.3-2.2 dB (136 ms, CC1 64), repeatably (takes' spread p50 0.2-0.9 dB, split-half r 0.65-0.91), similar at CC1
32 / 112 (r 0.58-0.91) but not at 600 ms (r 0.07-0.57: the transition's own envelope, not the destination sample). In
the validation runs the note-to-note pattern repeats pass to pass (|diff| p50 0.15-0.34 dB) but the isolated table
predicts it only partly (r 0.45-0.63 'heard', 0.23-0.68 'swell'): subtracting it leaves the within-slur spread as it
was; half of it removes 10-23 % of the SD ('heard'), ~0-9 % ('swell'). The level depends on more context than
(destination, interval, velocity).

Measures (analyze; perceived loudness, tools/playbackverify/loudness.py, ~dB: a gain of g dB moves it by g):
  arrY / arrZ  ms from y's / z's note-on to when the new pitch's own harmonics pass the old one's
               (analyze_sweep.arrival, as legato_pair_scan.py); attacks: y's note-on + the map's onset for the attack
               (spiccato: the Legato articulation's onset by pitch; smooth: attackOnset)
  env          the perceived envelope, 10 ms steps from y's note-on - 0.2 s, to z's note-on + 0.6 s
table reads env again: 'heard' = the mean from y's arrival to z's arrival (the note as heard; a detection that failed:
the map's legatoDelay for the interval), 'swell' = the mean over 136.4 ms from y's note-on + the map's legatoDelay
(sso_slur_swell.json's 'perceivedArrival'), long notes also 'steady' = the median from max(0.3 s, arrival + 0.1 s)
to 0.6 s after y's note-on.
"""
import argparse
import json
import os
import random
import statistics as st
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
MAP = os.path.join(HERE, '..', '..', 'share', 'soundlibraries', 'Spitfire Symphony Orchestra.xml')

SECTIONS = {'vn1': 'Violins 1 - Performance', 'vn2': 'Violins 2 - Performance', 'va': 'Violas - Performance',
            'vc': 'Celli - Performance', 'cb': 'Basses - Performance'}
RANGES = {'vn1': (55, 97), 'vn2': (56, 95), 'va': (49, 88), 'vc': (38, 83), 'cb': (24, 57)}  # sso_legato_grid_pitches
INTERVALS = [1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 7, -7, 12, -12]
FINGERED, BOWED, SPICCATO, SMOOTH = 69, 93, 112, 5
OVERLAP, ZLEN, GAP, CHUNK = 0.024, 0.4, 1.0, 600.0
EXTRA = {'vc': (49, 51, 52)}
CONDITIONS = {  # name: (L, CC1, destination step)
    'S64': (0.136, 64, 1), 'L64': (0.6, 64, 2), 'S32': (0.136, 32, 3), 'S112': (0.136, 112, 3)}
RUN_ORDER = [('S64', 'A'), ('S64', 'B'), ('L64', 'A'), ('L64', 'B'), ('S32', 'A'), ('S112', 'A'), ('S32', 'B'),
             ('S112', 'B'), ('S64', 'C'), ('L64', 'C'), ('R', 'A'), ('R', 'B'), ('R', 'C')]


def avoid(sec, p, v):
    return 99 if sec == 'vc' and p in (47, 48) and 100 <= v <= 116 else v


def destinations(sec, step):
    lo, hi = RANGES[sec]
    return sorted(set(range(lo, hi + 1, step)) | set(EXTRA.get(sec, ())))


def cells(sec, cond):
    L, cc1, step = CONDITIONS[cond]
    lo, hi = RANGES[sec]
    out = []
    for y in destinations(sec, step):
        out.append(dict(kind='spiccato', y=y))
        out.append(dict(kind='smooth', y=y))
        for i in INTERVALS:
            if lo <= y - i <= hi:
                for v in (FINGERED, BOWED):
                    out.append(dict(kind='legato', y=y, i=i, v=v))
    return out


def schedule(sec, cond, take, cs):
    """[(file cells, events)] in chunks of about CHUNK s"""
    L, cc1, _ = CONDITIONS[cond]
    lo, hi = RANGES[sec]
    cs = list(cs)
    if take == 'B':
        cs.reverse()
    elif take == 'C':
        random.Random(f'{sec}{cond}').shuffle(cs)
    files = []
    ev, notes, t = None, None, 0.0
    for c in cs:
        if ev is None or t > CHUNK:
            if ev is not None:
                files.append((notes, ev))
            ev = [(0.0, 'cc', 1, cc1), (0.0, 'cc', 11, 127)]
            notes, t = [], 1.0
        y = c['y']
        z = y + 2 if y + 2 <= hi else y - 2
        seq = []                             # (pitch, velocity, length)
        if c['kind'] == 'legato':
            x = y - c['i']
            w = x - 2 if x - 2 >= lo else x + 2
            seq = [(w, avoid(sec, w, SPICCATO), L), (x, FINGERED, L), (y, c['v'], L)]
        else:
            seq = [(y, avoid(sec, y, SPICCATO if c['kind'] == 'spiccato' else SMOOTH), L)]
        seq.append((z, FINGERED, ZLEN))
        on = t
        ons = []
        for k, (p, v, ln) in enumerate(seq):
            last = k == len(seq) - 1
            ev.append((on, 'on', p, v))
            ev.append((on + ln + (0 if last else OVERLAP), 'off', p, 0))
            ons.append(on)
            on += ln
        n = dict(c, z=z, yOn=round(ons[-2], 5), zOn=round(ons[-1], 5), L=L, cc1=cc1)
        if c['kind'] == 'legato':
            n['x'] = y - c['i']
            n['vy'] = c['v']
        else:
            n['vy'] = seq[0][1]
        notes.append(n)
        t = on + GAP
    files.append((notes, ev))
    return files


WHENCE_BAR2 = [49] + [51, 52, 51, 49] * 4


def run_phrases(sec):
    """validation runs: phrases of a spiccato note then four slurs of four sixteenths (136 ms) as Whence bar 2
    plays them (each slur's first note a bowed transition 93 from the slur before, the rest fingered 69), pitches a
    random walk in the range (steps +-1 .. +-5, +-7 now and then; fixed seed); Celli also Whence bar 2 itself"""
    lo, hi = RANGES[sec]
    rnd = random.Random('runs' + sec)
    out = [WHENCE_BAR2] * 2 if sec == 'vc' else []
    steps = [1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 1, -1, 2, -2, 7, -7]
    while len(out) < 30:
        p = [rnd.randint(lo + 4, hi - 4)]
        while len(p) < 17:
            q = p[-1] + rnd.choice(steps)
            if lo <= q <= hi and q != p[-1]:
                p.append(q)
        out.append(p)
    return out


def run_schedule(sec, take):
    phrases = run_phrases(sec)
    if take == 'B':
        phrases = phrases[::-1]
    elif take == 'C':
        random.Random('runsC' + sec).shuffle(phrases)
    ev = [(0.0, 'cc', 1, 64), (0.0, 'cc', 11, 127)]
    notes, t = [], 1.0
    for ps in phrases:
        vels = [avoid(sec, ps[0], SPICCATO)] + [BOWED if k % 4 == 0 else FINGERED for k in range(len(ps) - 1)]
        ons = []
        for k, (p, v) in enumerate(zip(ps, vels)):
            last = k == len(ps) - 1
            ev.append((t, 'on', p, v))
            ev.append((t + 0.136 + (0 if last else OVERLAP), 'off', p, 0))
            ons.append(round(t, 5))
            t += 0.136
        notes.append(dict(kind='run', pitches=ps, vels=vels, ons=ons, L=0.136, cc1=64))
        t += 1.2
    return [(notes, ev)]


RUN_CMD = r'''@echo off
rem transition-level scan (branch transition-levels): renders list.txt (name|patch) with kthost, analyses each wav
rem here and deletes it (literal path); only C:\claude\trl is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\trl\st mkdir C:\claude\trl\st
for %%f in ("Violins 1 - Performance" "Violins 2 - Performance" "Violas - Performance" "Celli - Performance" "Basses - Performance") do if not exist "C:\claude\trl\st\%%~f.vst3state" copy /y "%SRC%\%%~f.vst3state" C:\claude\trl\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
if errorlevel 1 (echo share login failed>>C:\claude\trl\run.out & exit /b 1)
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\trl\list.txt") do (
  if exist C:\claude\trl\stop.txt goto :end
  if not exist "C:\claude\trl\r\%%a.res.json" (
    echo %%a start !time!>>C:\claude\trl\run.out
    set W=600
    if "%%b"=="Basses - Performance" set W=20
    C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\trl\st\%%b.vst3state" "C:\claude\trl\ev\%%a.txt" "C:\claude\trl\r\%%a.wav" --tail 3 --wait !W! > "C:\claude\trl\r\%%a.log" 2>&1
    "C:\Program Files\Python312\python.exe" C:\claude\trl\tools\transition_level_scan.py analyze C:\claude\trl\r %%a >> "C:\claude\trl\r\%%a.log" 2>&1
    if exist "C:\claude\trl\r\%%a.res.json" del "C:\claude\trl\r\%%a.wav"
    echo %%a done !time!>>C:\claude\trl\run.out
  )
)
:end
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE>>C:\claude\trl\run.out
'''


def events(work, only=None):
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    lst = []
    total = 0.0
    for cond, take in RUN_ORDER:
        for sec, patch in SECTIONS.items():
            if only and f'{cond}{take}' not in only:
                continue
            sched = run_schedule(sec, take) if cond == 'R' else schedule(sec, cond, take, cells(sec, cond))
            for k, (notes, ev) in enumerate(sched):
                name = f'{cond}{take}_{sec}_{k + 1}'
                ev.sort(key=lambda e: (e[0], {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
                with open(os.path.join(work, 'ev', name + '.txt'), 'w', newline='\r\n') as f:
                    for e in ev:
                        f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')
                length = max(e[0] for e in ev) + 3
                json.dump(dict(patch=patch, section=sec, cond=cond, take=take, length=length, notes=notes),
                          open(os.path.join(work, 'ev', name + '.json'), 'w'))
                lst.append(f'{name}|{patch}')
                total += length
    with open(os.path.join(work, 'list.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lst) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(lst), 'files', round(total / 3600, 2), 'h of audio')


# --- map ----------------------------------------------------------------------------------------------------------

def _table(s):
    out = {}
    for tok in (s or '').split():
        k, v = tok.split(':')
        out[int(k)] = float(v)
    return out


def _interp(t, p):
    ks = sorted(t)
    if not ks:
        return None
    if p <= ks[0]:
        return t[ks[0]]
    if p >= ks[-1]:
        return t[ks[-1]]
    for a, b in zip(ks, ks[1:]):
        if a <= p <= b:
            return t[a] + (t[b] - t[a]) * (p - a) / (b - a)


def map_info(map_path):
    root = ET.parse(map_path).getroot()
    out = {}
    for ins in root.iter('Instrument'):
        if ins.get('name') not in SECTIONS.values():
            continue
        for a in ins.iter('Articulation'):
            if a.get('legatoDelay') and a.get('modifiers') == 'performance':
                smooth = (a.get('attackOnset') or '').split(';')
                out[ins.get('name')] = dict(delay=_table(a.get('legatoDelay')), onset=_table(a.get('onset')),
                                            smooth=_table(smooth[1].replace(',', ' ')) if len(smooth) > 1 else {})
    return out


def legato_delay(info, i):
    d = info['delay']
    if i in d:
        return d[i]
    return _interp({k: v for k, v in d.items() if (k > 0) == (i > 0)}, i) or 200.0


# --- analyze (on the VM) --------------------------------------------------------------------------------------------

def analyze(folder, name):
    import numpy as np
    import soundfile as sf
    from loudness import perceived_envelope
    from analyze_sweep import arrival, hz
    meta = json.load(open(os.path.join(folder, '..', 'ev', name + '.json')))
    x, sr = sf.read(os.path.join(folder, name + '.wav'), always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    m = x.mean(axis=1)
    rows = []
    for n in meta['notes']:
        r = dict(n)
        if n['kind'] == 'run':
            ps, ons = n['pitches'], n['ons']
            arr = [None]
            for k in range(1, len(ps)):
                a = arrival(m, sr, ons[k], hz(ps[k - 1]), hz(ps[k]), ons[k], ons[k] + 0.5)
                arr.append(None if a is None else round(a * 1000))
            r['arr'] = arr
            ts = np.arange(ons[0] - 0.2, ons[-1] + 0.8, 0.01)
            r['env'] = [round(float(v), 2) for v in np.interp(ts, t_env, env)]
            rows.append(r)
            continue
        y, z = n['y'], n['z']
        if n['kind'] == 'legato':
            a = arrival(m, sr, n['yOn'], hz(n['x']), hz(y), n['yOn'], n['yOn'] + 0.6)
            r['arrY'] = None if a is None else round(a * 1000)
        a = arrival(m, sr, n['zOn'], hz(y), hz(z), n['zOn'], n['zOn'] + 0.5)
        r['arrZ'] = None if a is None else round(a * 1000)
        ts = np.arange(n['yOn'] - 0.2, n['zOn'] + 0.6, 0.01)
        r['env'] = [round(float(v), 2) for v in np.interp(ts, t_env, env)]
        rows.append(r)
    json.dump(dict((k, v) for k, v in meta.items() if k != 'notes') | dict(rows=rows),
              open(os.path.join(folder, name + '.res.json'), 'w'), separators=(',', ':'))
    print(name, len(rows), 'cells analysed')


# --- table ----------------------------------------------------------------------------------------------------------

def measures(r, info):
    """{'heard', 'swell', 'first', 'steady'} of one cell (perceived, ~dB)"""
    env = r['env']
    t0 = r['yOn'] - 0.2

    def mean(a, b, f=st.fmean):
        i0, i1 = int(round((a - t0) / 0.01)), int(round((b - t0) / 0.01))
        i0, i1 = max(0, i0), min(len(env), i1)
        return f(env[i0:i1]) if i1 > i0 else None
    L = r['L']
    if r['kind'] == 'legato':
        mapd = legato_delay(info, r['i']) / 1000
        ay = r['arrY'] / 1000 if r.get('arrY') is not None and 0 < r['arrY'] < 600 else mapd
    elif r['kind'] == 'spiccato':
        mapd = ay = (_interp(info['onset'], r['y']) or 60.0) / 1000
    else:
        mapd = ay = (_interp(info['smooth'], r['y']) or 150.0) / 1000
    az = r['arrZ'] / 1000 if r.get('arrZ') is not None and 0 < r['arrZ'] < 500 else legato_delay(info, r['z'] - r['y']) / 1000
    yOn, zOn = r['yOn'], r['zOn']
    out = dict(heard=mean(yOn + ay, max(zOn + az, yOn + ay + 0.03)),
               swell=mean(yOn + mapd, yOn + mapd + 0.1364),
               first=mean(yOn + ay, yOn + ay + 0.1364), arr=round(ay * 1000))
    if L > 0.3:
        out['steady'] = mean(yOn + max(0.3, ay + 0.1), yOn + 0.6, st.median)
    return out


def run_levels(row, info):
    """([heard], [swell]) of each note of a validation run; the first (an attack) None"""
    ps, ons, env = row['pitches'], row['ons'], row['env']
    t0 = ons[0] - 0.2

    def mean(a, b):
        i0, i1 = int(round((a - t0) / 0.01)), int(round((b - t0) / 0.01))
        return st.fmean(env[i0:i1])
    mapd = [None] + [legato_delay(info, ps[k] - ps[k - 1]) / 1000 for k in range(1, len(ps))]
    arr = [None] + [(a / 1000 if a is not None and 0 < a < 500 else mapd[k]) for k, a in enumerate(row['arr'][1:], 1)]
    heard, swell = [None], [None]
    for k in range(1, len(ps)):
        a = ons[k] + arr[k]
        b = ons[k + 1] + arr[k + 1] if k + 1 < len(ps) else ons[k] + 0.136 + 0.15
        heard.append(mean(a, max(b, a + 0.03)))
        swell.append(mean(ons[k] + mapd[k], ons[k] + mapd[k] + 0.1364))
    return heard, swell


def q(v, f):
    v = sorted(v)
    return v[min(len(v) - 1, int(f * len(v)))]


def table(res_folder, out_path, map_path):
    infos = map_info(map_path)
    # (patch, cond, kind, v, y, i) -> {take: measures}
    data = {}
    runs = {}
    for fn in sorted(os.listdir(res_folder)):
        if not fn.endswith('.res.json'):
            continue
        d = json.load(open(os.path.join(res_folder, fn)))
        info = infos[d['patch']]
        if d['cond'] == 'R':
            for k, r in enumerate(d['rows']):
                heard, swell = run_levels(r, info)
                runs.setdefault(d['patch'], {}).setdefault('rows', {}).setdefault(json.dumps(r['pitches']), dict(
                    pitches=r['pitches'], vels=r['vels'], takes={}))['takes'].setdefault(d['take'], []).append(
                    dict(heard=[None if x is None else round(x, 1) for x in heard],
                         swell=[None if x is None else round(x, 1) for x in swell]))
            continue
        for r in d['rows']:
            key = (d['patch'], d['cond'], r['kind'], r.get('v'), r['y'], r.get('i'))
            data.setdefault(key, {})[d['take']] = measures(r, info)
    MEAS = ('heard', 'swell', 'first', 'steady')
    out = {}
    for (patch, cond, kind, v, y, i), takes in data.items():
        e = {}
        for m in MEAS:
            vals = [t[m] for t in takes.values() if t.get(m) is not None]
            if vals:
                e[m] = round(st.median(vals), 1)
                e[m + 'Spread'] = round(max(vals) - min(vals), 1)
        e['n'] = len(takes)
        e['takes'] = {k: round(t['heard'], 1) for k, t in sorted(takes.items()) if t.get('heard') is not None}
        e['arrMs'] = round(st.median([t['arr'] for t in takes.values()]))
        if cond != 'L64':
            e.pop('first', None)
            e.pop('firstSpread', None)
        c = out.setdefault(patch, {}).setdefault(cond, {})
        if kind == 'legato':
            c.setdefault('legato', {}).setdefault(str(v), {}).setdefault(str(y), {})[f'{i:+d}'] = e
        else:
            c.setdefault(kind, {})[str(y)] = e
    # offsets: against the same destination's spiccato / smooth attack, same condition and measure
    for patch, conds in out.items():
        for cond, c in conds.items():
            for v, ys in c.get('legato', {}).items():
                for y, ivs in ys.items():
                    for i, e in ivs.items():
                        for ref in ('spiccato', 'smooth'):
                            a = c.get(ref, {}).get(y)
                            for m in (MEAS if ref == 'spiccato' else ('heard', 'steady')):
                                if a and m in a and m in e:
                                    e[f'd{m.capitalize()}{ref.capitalize()}'] = round(e[m] - a[m], 1)
    result = dict(
        _about='SSO string Performance patches: the level of a note reached by a legato transition against the interval '
               'it comes from and its pitch, and of the same note reached by an attack. tools/soundlibraries/'
               'transition_level_scan.py (its header has the method). Measured 2026-10-12 on the Windows test VM: kthost '
               '(offline VST 3 host, C:\\claude\\kt), Kontakt 8 + SSO, each patch from MuseScore\'s resaved setup '
               '(library defaults), 44.1 kHz. Perceived loudness (tools/playbackverify/loudness.py, ~dB). Per patch / '
               'condition (S64: notes 136 ms at CC1 64; L64: 600 ms, CC1 64; S32 / S112: 136 ms at CC1 32 / 112) / '
               'transition velocity (69 fingered, 93 bowed) / destination pitch / interval from the note before: '
               'median over takes of heard (mean from the arrival to the next note\'s), swell (sso_slur_swell.json\'s '
               'window), first (136.4 ms from the arrival), steady (long notes: median 0.3-0.6 s), their spread (max - '
               'min over takes), n takes; d<Measure><Ref>: minus the same destination\'s spiccato (112) / smooth (5) '
               'attack in the same condition; takes: heard per take. runs: the validation runs (_runs). Result: '
               'transition_level_scan.py\'s header; transition_levels_report.py prints it. Measurement only: the '
               'renderer does not read this file.',
        method=dict(cells='w (spiccato 112) -> x (fingered 69) -> y (transition under test) -> z (fingered 69, '
                          '0.4 s); x = y - interval, w = x - 2 (x + 2 at the bottom), z = y + 2 (y - 2 at the top); '
                          'the note before off 24 ms after the next note-on; attacks: y alone then z; 1 s silence; '
                          'CC11 127', ranges=RANGES, intervals=INTERVALS, conditions=CONDITIONS,
                    takes='A grid order, B reversed, C shuffled; separate renders'),
        patches=out,
        runs={p: list(v['rows'].values()) for p, v in runs.items()},
        _runs='validation runs (transition_level_scan.py run_phrases): phrases of a spiccato note then four slurs of four '
              '136 ms notes as MuseScore plays Whence (each slur\'s first note bowed 93 from the slur before, the rest '
              'fingered 69), CC1 64; Celli also Whence 12-TET bar 2 (49 then 51 52 51 49 x4, twice per take); per note '
              '(the first, the attack, null) heard (mean from its detected arrival to the next note\'s) and swell '
              '(sso_slur_swell.json\'s window: 136.4 ms from the note-on + the map\'s legatoDelay), per take')
    json.dump(result, open(out_path, 'w'), indent=None, separators=(',', ':'))
    print('wrote', out_path)


# --- Whence bar 2 ---------------------------------------------------------------------------------------------------

def whence(path, meas='heard'):
    d = json.load(open(path))['patches']['Celli - Performance']['S64']
    # slur: 51 (bowed 93 from 49, +2), 52 (fingered from 51, +1), 51 (fingered from 52, -1), 49 (fingered from 51, -2)
    for label, notes in (('as rendered (A)', [(51, 2, 93), (52, 1, 69), (51, -1, 69), (49, -2, 69)]),
                         ('all fingered (B)', [(51, 2, 69), (52, 1, 69), (51, -1, 69), (49, -2, 69)])):
        lv = [d['legato'][str(v)][str(y)][f'{i:+d}'][meas] for y, i, v in notes]
        print(label, ' '.join(f'{x:.2f}' for x in lv), ' rel. note 1:', ' '.join(f'{x - lv[0]:+.2f}' for x in lv))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('work')
    e.add_argument('--only', nargs='*')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('name')
    t = sub.add_parser('table')
    t.add_argument('res')
    t.add_argument('-o', default=os.path.join(HERE, 'sso_transition_levels.json'))
    t.add_argument('--map', default=MAP)
    w = sub.add_parser('whence')
    w.add_argument('json')
    w.add_argument('--measure', default='heard')
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.work, o.only)
    elif o.cmd == 'analyze':
        analyze(o.folder, o.name)
    elif o.cmd == 'table':
        table(o.res, o.o, o.map)
    else:
        whence(o.json, o.measure)


if __name__ == '__main__':
    main()
