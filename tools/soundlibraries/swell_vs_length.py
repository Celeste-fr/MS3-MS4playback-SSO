#!/usr/bin/env python3
"""Swell against note length (2026-10-13, branch swell-vs-length): above which note length (inter-onset interval, IOI)
do slurred passages on SSO's five string sections' Performance patches measure flat within SSO's own per-note
randomness, so that the background even-levels measurer (libmscore/evenlevels.h, branch even-auto) can skip them and
store "no correction needed"? Written for this check, not taken from anywhere. Measurement only: the renderer doesn't
read the result (sso_swell_vs_length.json).

  swell_vs_length.py events <work>              kthost event files <work>/ev/<name>.txt + .json, list1/2.txt, run.cmd
  swell_vs_length.py analyze <folder> <name>    (on the VM) <name>.wav -> <name>.res.json (each note's levels)
  swell_vs_length.py report <res folder> [-o sso_swell_vs_length.json]

Phrases (as transition_level_scan.py run_phrases builds them, as MuseScore plays slurs in Recommended): 10 notes, a
smooth attack from silence (velocity 5, early by the map's attackOnset for its pitch, as [slurs] smoothStart plays it),
then two slurs of four (notes 1-4, 5-8: each slur's first note a bowed transition 93 from the slur before, the rest
fingered 69) and a last note (9, bowed); every note IOI long, the note before off the map's overlap (24 ms) after the
next note-on, CC11 127, CC1 set 0.3 s before the phrase, 1.5 s of silence after. Pitches: random walks in the
section's legato range (steps +-1 .. +-5, +-7 now and then; fixed seed per section, start anywhere in the range but 4
semitones from its ends). IOI 100, 136, 180, 250, 350, 500, 750, 1000 ms. CC1 64: 8 patterns; CC1 40 and 100: 3 of
them. Mixed phrases (CC1 64, patterns 1-4, IOI 500 and 1000): the same notes with note 6 (F1) or notes 6 and 7 (F2)
136 ms long, a fast note inside the slow second slur.
Passes: each section's phrases in one file per pass, A shuffled (fixed seed), B = A reversed, C another shuffle;
each pass a separate kthost process from MuseScore's resaved setup (the job's 3 passes, each from a fresh start).
Level of a note (report), as the even-levels job measures it: 'swell', the mean perceived loudness
(tools/playbackverify/loudness.py, ~dB) over 136.4 ms from note-on + the map's legatoDelay for its interval (window
start; evenlevels.h windowStartMs), the median of the 3 passes. Slur range (evenlevels.h slurStats): per slur, max -
min of its notes' levels, the passage's first and last notes left out (so slurs 1-4 and 5-8 count). Also 'whole': the
mean from a note's window start to the next note's window start (a swell later in a long note, which the job's
window can't see), reported alongside.
Noise floor: what a flat-by-construction slur would measure from SSO's randomness alone. Per section x IOI x CC1, the
pass-to-pass SD sigma of a note's level around its 3-pass mean (each pass's slur mean taken out first, so a whole-pass
shift, which a slur range doesn't see, doesn't count; variance corrected for both means' degrees of freedom), then
Monte Carlo (40000 slurs): a slur of 4 notes, each the median of 3 draws N(0, sigma^2): p50 / p90 of its range.
Threshold criterion: the shortest IOI X of the grid such that at X and at every longer IOI the measured p90 slur
range is within the noise floor's p90 + 0.3 dB (the p90 of 8-16 slurs is itself uncertain by about that much; see
the result); also against the job's FLOOR_DB 1.0 (the listening test's floor).

Result (2026-10-13, Windows test VM, kthost + Kontakt 8 + SSO, 15 renders = 3 passes x 5 sections, 200 min of
audio in 14 min, two kthost processes at once): THERE IS NO SUCH LENGTH. Slow slurs are not flatter than fast ones;
in the job's window they are less even. Slur range p50 / p90, dB (noise floor p90 0.1-1.0, mostly 0.2-0.6):
  CC1 64    100 ms     136        250        500        1000
  Vn 1      2.7/4.2    2.2/5.0    3.8/5.3    4.4/6.5    3.7/6.5
  Vn 2      1.4/4.0    3.0/5.1    4.2/7.7    5.2/7.7    5.5/8.3
  Violas    2.7/4.1    2.8/3.6    4.2/5.6    5.2/7.5    4.2/6.8
  Celli     1.9/2.9    2.4/3.8    3.6/7.2    3.5/9.1    4.1/9.8
  Basses    3.0/3.9    2.6/5.2    3.8/4.8    5.0/7.5    5.8/7.5
CC1 40 and 100 the same picture (100 a little smaller: Violas 1.7-3.5 p50, Celli 1.5-3.0). Every section x CC1 x
IOI cell is far above its noise floor (median range 1.4-8.2 against noise p90 <= 1.0); no threshold under any of the
criteria (p90 <= noise p90 + 0.3, p90 <= 1.0 dB, p50 <= noise p90). The unevenness is deterministic (pass-to-pass
sigma 0.14-0.46 dB per note), not register (slurs spanning <= 4 semitones: median range 2.1 at 100 ms, 4.2-4.9 from
250 ms), and not only the bowed slur start (the three fingered notes alone: 1.0-2.3 at 100 ms, 2.3-4.5 from 250 ms);
a note's pitch + velocity explains about half the variance at 500-1000 ms (SD 1.8-2.4 -> 1.1-1.9), the interval it
comes from the rest. The 'whole' measure (window start to the next note's) agrees: p50 2.2-3.6 at 1000 ms. So at
slower tempi the window falls in each transition's own envelope and every note keeps its own sample-and-interval
level, while fast notes average neighbouring transitions. (Whence's flat slow bars were a different, small sample.)
Mixed (a 136 ms note at note 6 of a 500 / 1000 ms slur): the change is local: mean |delta| against the plain phrase,
median over sections, notes 3-5 (before) 0.07-0.33 (noise), note 6 1.1-1.4, note 7 0.9-1.1 (F2, two fast: 2.1-2.3),
note 8 0.2-0.3 (F2 0.8-1.1). A fast note moves itself and the next note (two fast: the next two), as the perturbation
scan found.
Skip rule: none by length. A passage on these five Performance patches needs measuring at any IOI from 100 to
1000 ms; an edit moves its own note and the 1-2 after it (the window rule of even_fast_checks: note +-3 covers it).
"""
import argparse
import json
import math
import os
import random
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
from transition_level_scan import SECTIONS, RANGES, FINGERED, BOWED, SMOOTH, OVERLAP, MAP  # noqa: E402

IOIS = (100, 136, 180, 250, 350, 500, 750, 1000)
NPAT = {64: 8, 40: 3, 100: 3}
MIXED = dict(iois=(500, 1000), fast=136, variants={'F1': (6,), 'F2': (6, 7)}, patterns=4, cc1=64)
NNOTES = 10
GAP, CC1_LEAD = 1.5, 0.3
WIN = 0.1364
PASSES = ('A', 'B', 'C')


def patterns(sec):
    lo, hi = RANGES[sec]
    rnd = random.Random('swell' + sec)
    steps = [1, -1, 2, -2, 3, -3, 4, -4, 5, -5, 1, -1, 2, -2, 7, -7]
    out = []
    while len(out) < max(NPAT.values()):
        p = [rnd.randint(lo + 4, hi - 4)]
        while len(p) < NNOTES:
            q = p[-1] + rnd.choice(steps)
            if lo <= q <= hi and q != p[-1]:
                p.append(q)
        out.append(p)
    return out


def phrases(sec):
    """[dict(pat, cc1, ioi, variant, pitches, lens)]"""
    ps = patterns(sec)
    out = []
    for cc1, n in NPAT.items():
        for i in range(n):
            for ioi in IOIS:
                out.append(dict(pat=i, cc1=cc1, ioi=ioi, variant='B', pitches=ps[i], lens=[ioi] * NNOTES))
    for i in range(MIXED['patterns']):
        for ioi in MIXED['iois']:
            for v, fast in MIXED['variants'].items():
                lens = [MIXED['fast'] if k in fast else ioi for k in range(NNOTES)]
                out.append(dict(pat=i, cc1=MIXED['cc1'], ioi=ioi, variant=v, pitches=ps[i], lens=lens))
    return out


def _table(s):
    return {int(k): float(v) for k, v in (t.split(':') for t in (s or '').replace(',', ' ').split())}


def _interp(t, p):
    ks = sorted(t)
    if p <= ks[0]:
        return t[ks[0]]
    if p >= ks[-1]:
        return t[ks[-1]]
    for a, b in zip(ks, ks[1:]):
        if a <= p <= b:
            return t[a] + (t[b] - t[a]) * (p - a) / (b - a)


def map_info(map_path=MAP):
    import xml.etree.ElementTree as ET
    out = {}
    for ins in ET.parse(map_path).getroot().iter('Instrument'):
        if ins.get('name') not in SECTIONS.values():
            continue
        for a in ins.iter('Articulation'):
            if a.get('legatoDelay') and a.get('modifiers') == 'performance':
                sm = (a.get('attackOnset') or '').split(';')
                out[ins.get('name')] = dict(delay=_table(a.get('legatoDelay')),
                                            smooth=_table(sm[1]) if len(sm) > 1 else {},
                                            overlap=float(a.get('overlap', '24')) / 1000)
    return out


def legato_delay(info, i):
    """evenlevels windowStartMs for a transition: the map's legatoDelay by interval (linear within a sign)"""
    d = info['delay']
    if i in d:
        return d[i]
    return _interp({k: v for k, v in d.items() if (k > 0) == (i > 0)}, i)


RUN_CMD = r'''@echo off
rem swell vs length (branch swell-vs-length): renders list%1.txt (name|patch) with kthost, analyses each wav here
rem and deletes it (literal path); only C:\claude\svl is touched
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\svl\list%1.txt") do (
  if exist C:\claude\svl\stop.txt goto :end
  if not exist "C:\claude\svl\r\%%a.res.json" (
    echo %%a start !time!>>C:\claude\svl\run%1.out
    set W=600
    if "%%b"=="Basses - Performance" set W=20
    C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\svl\st\%%b.vst3state" "C:\claude\svl\ev\%%a.txt" "C:\claude\svl\r\%%a.wav" --tail 3 --wait !W! > "C:\claude\svl\r\%%a.log" 2>&1
    "C:\Program Files\Python312\python.exe" C:\claude\svl\tools\swell_vs_length.py analyze C:\claude\svl\r %%a >> "C:\claude\svl\r\%%a.log" 2>&1
    if exist "C:\claude\svl\r\%%a.res.json" del "C:\claude\svl\r\%%a.wav"
    echo %%a done !time!>>C:\claude\svl\run%1.out
  )
)
:end
echo ALL DONE>>C:\claude\svl\run%1.out
'''


def events(work):
    infos = map_info()
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    lists, total = {1: [], 2: []}, {1: 0.0, 2: 0.0}
    files = []
    for p in PASSES:
        for sec, patch in SECTIONS.items():
            info = infos[patch]
            phs = phrases(sec)
            order = list(range(len(phs)))
            random.Random('svlA' + sec).shuffle(order)
            if p == 'B':
                order.reverse()
            elif p == 'C':
                random.Random('svlC' + sec).shuffle(order)
            ev = [(0.0, 'cc', 11, 127)]
            notes, t = [], 1.0
            for i in order:
                ph = phs[i]
                ps, lens = ph['pitches'], ph['lens']
                t0 = t + CC1_LEAD
                ev.append((t, 'cc', 1, ph['cc1']))
                on = t0
                ons, wins, vels = [], [], []
                for k, (q, ln) in enumerate(zip(ps, lens)):
                    v = SMOOTH if k == 0 else (BOWED if k % 4 == 1 else FINGERED)
                    start = on - (_interp(info['smooth'], q) / 1000 if k == 0 else 0.0)
                    last = k == len(ps) - 1
                    ev.append((start, 'on', q, v))
                    ev.append((on + ln / 1000 + (0 if last else info['overlap']), 'off', q, 0))
                    ons.append(round(on, 5))
                    vels.append(v)
                    wins.append(None if k == 0 else round(on + legato_delay(info, q - ps[k - 1]) / 1000, 5))
                    on += ln / 1000
                notes.append(dict(ph, ons=ons, wins=wins, vels=vels))
                t = on + GAP
            fn = f'{p}_{sec}'
            ev.sort(key=lambda e: (e[0], {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
            with open(os.path.join(work, 'ev', fn + '.txt'), 'w', newline='\r\n') as f:
                for e in ev:
                    f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')
            length = max(e[0] for e in ev) + 3
            json.dump(dict(patch=patch, section=sec, take=p, length=length, notes=notes),
                      open(os.path.join(work, 'ev', fn + '.json'), 'w'))
            files.append((fn, patch, length))
    # two runners, balanced by audio length (longest first)
    for fn, patch, length in sorted(files, key=lambda f: -f[2]):
        k = 1 if total[1] <= total[2] else 2
        lists[k].append(f'{fn}|{patch}')
        total[k] += length
    for k in (1, 2):
        with open(os.path.join(work, f'list{k}.txt'), 'w', newline='\r\n') as f:
            f.write('\n'.join(lists[k]) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(files), 'files', {k: round(v / 60, 1) for k, v in total.items()}, 'min of audio per runner')


def analyze(folder, name):
    import numpy as np
    import soundfile as sf
    from loudness import perceived_envelope
    meta = json.load(open(os.path.join(folder, '..', 'ev', name + '.json')))
    x, sr = sf.read(os.path.join(folder, name + '.wav'), always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0

    def mean(a, b):
        m = (t_env >= a) & (t_env < b)
        return round(float(np.mean(env[m])), 3) if m.any() else None
    for n in meta['notes']:
        w = n['wins']
        n['swell'] = [None] + [mean(a, a + WIN) for a in w[1:]]
        n['whole'] = [None] + [mean(w[k], w[k + 1]) for k in range(1, len(w) - 1)] + [None]
    json.dump(meta, open(os.path.join(folder, name + '.res.json'), 'w'), separators=(',', ':'))
    print(name, len(meta['notes']), 'phrases analysed')


# --- report ---------------------------------------------------------------------------------------------------------

SLURS = ((1, 2, 3, 4), (5, 6, 7, 8))


def q(v, f):
    v = sorted(v)
    if not v:
        return None
    i = f * (len(v) - 1)
    a = int(math.floor(i))
    b = min(a + 1, len(v) - 1)
    return v[a] + (v[b] - v[a]) * (i - a)


def noise_ranges(sigma, n=4, draws=40000, seed=1):
    rnd = random.Random(seed)
    out = []
    for _ in range(draws):
        lv = [st.median([rnd.gauss(0, sigma) for _ in range(3)]) for _ in range(n)]
        out.append(max(lv) - min(lv))
    return q(out, 0.5), q(out, 0.9)


def report(res, out_path):
    data = {}       # (sec, cc1, ioi, variant, pat) -> {pass: phrase}
    for fn in sorted(os.listdir(res)):
        if fn.endswith('.res.json'):
            d = json.load(open(os.path.join(res, fn)))
            for n in d['notes']:
                data.setdefault((d['section'], n['cc1'], n['ioi'], n['variant'], n['pat']), {})[d['take']] = n
    cells = {}
    for (sec, cc1, ioi, var, pat), takes in data.items():
        if len(takes) < 3:
            continue
        cells.setdefault((sec, cc1, ioi, var), []).append((pat, takes))
    out = dict(cells={}, mixed={})
    rows = []
    for (sec, cc1, ioi, var), items in sorted(cells.items()):
        ranges, wranges, resid, ss, dof = [], [], [], 0.0, 0
        meds = {}
        for pat, takes in items:
            lv = {m: [None if any(takes[p][m][k] is None for p in PASSES) else st.median(takes[p][m][k] for p in PASSES) for k in range(NNOTES)]
                  for m in ('swell', 'whole')}
            meds[pat] = lv
            for s in SLURS:
                a = [lv['swell'][k] for k in s]
                ranges.append(max(a) - min(a))
                b = [lv['whole'][k] for k in s if lv['whole'][k] is not None]   # None: windows out of order
                if len(b) > 1:
                    wranges.append(max(b) - min(b))
                md = st.median(a)
                resid.append(max(abs(x - md) for x in a))
                # pass-to-pass noise: take out each pass's slur mean, then each note's mean over passes
                if var == 'B':
                    X = [[takes[p]['swell'][k] for k in s] for p in PASSES]
                    X = [[x - st.fmean(r) for x in r] for r in X]
                    for j in range(len(s)):
                        col = [X[i][j] for i in range(3)]
                        mu = st.fmean(col)
                        ss += sum((c - mu) ** 2 for c in col)
                    dof += (3 - 1) * (len(s) - 1)
        c = dict(n=len(ranges), rangeP50=round(q(ranges, 0.5), 2), rangeP90=round(q(ranges, 0.9), 2),
                 rangeMax=round(max(ranges), 2), residualP50=round(q(resid, 0.5), 2),
                 wholeRangeP50=round(q(wranges, 0.5), 2), wholeRangeP90=round(q(wranges, 0.9), 2),
                 ranges=[round(r, 2) for r in ranges], levels={str(p): {m: [None if x is None else round(x, 2) for x in v]
                                                                      for m, v in lv.items()} for p, lv in meds.items()})
        if var == 'B' and dof:
            sigma = math.sqrt(ss / dof)
            n50, n90 = noise_ranges(sigma)
            c.update(sigma=round(sigma, 3), noiseP50=round(n50, 2), noiseP90=round(n90, 2))
        out['cells'].setdefault(sec, {}).setdefault(str(cc1), {}).setdefault(var, {})[str(ioi)] = c
    # thresholds
    thr = {}
    for sec, by in out['cells'].items():
        for cc1, vs in by.items():
            B = vs['B']
            res_ = {}
            for name, ok in (('noise', lambda c: c['rangeP90'] <= c['noiseP90'] + 0.3),
                             ('floor1dB', lambda c: c['rangeP90'] <= 1.0),
                             ('noiseP50', lambda c: c['rangeP50'] <= c['noiseP90'])):
                x = None
                for ioi in sorted(IOIS, reverse=True):
                    c = B.get(str(ioi))
                    if c is None or not ok(c):
                        break
                    x = ioi
                res_[name] = x
            thr.setdefault(sec, {})[cc1] = res_
    out['thresholds'] = thr
    # mixed: per note, delta against the plain phrase (same pattern, same pass), 3-pass medians; noise: the plain
    # cell's sigma -> sd of a difference of two 3-pass medians (Monte Carlo)
    for sec, by in out['cells'].items():
        vs = by.get(str(MIXED['cc1']), {})
        for ioi in MIXED['iois']:
            base = vs.get('B', {}).get(str(ioi))
            if not base:
                continue
            for v in MIXED['variants']:
                c = vs.get(v, {}).get(str(ioi))
                if not c:
                    continue
                deltas = {}
                for pat, lv in c['levels'].items():
                    bl = base['levels'].get(pat)
                    if not bl:
                        continue
                    for k in range(1, NNOTES - 1):
                        deltas.setdefault(k, []).append(lv['swell'][k] - bl['swell'][k])
                slur2 = [r for i, r in enumerate(c['ranges']) if i % 2 == 1]
                base2 = [r for i, r in enumerate(base['ranges']) if i % 2 == 1][:len(slur2)]
                out['mixed'].setdefault(sec, {}).setdefault(str(ioi), {})[v] = dict(
                    slur2Ranges=[round(r, 2) for r in slur2], slur2RangesPlain=[round(r, 2) for r in base2],
                    meanAbsDeltaByNote={str(k): round(st.fmean(abs(x) for x in d), 2) for k, d in deltas.items()},
                    noiseP90=base.get('noiseP90'), sigma=base.get('sigma'))
    # text
    print('slur range (swell window, 3-pass median) p50/p90 [noise p50/p90] by IOI')
    for sec, by in out['cells'].items():
        for cc1 in sorted(by, key=int):
            B = by[cc1]['B']
            line = ' '.join(f'{ioi}:{B[str(ioi)]["rangeP50"]:.1f}/{B[str(ioi)]["rangeP90"]:.1f}'
                            f'[{B[str(ioi)]["noiseP50"]:.1f}/{B[str(ioi)]["noiseP90"]:.1f}]'
                            for ioi in IOIS if str(ioi) in B)
            print(f'{sec} cc{cc1} {line}  thr {thr[sec][cc1]}')
    print('whole-note range p50/p90')
    for sec, by in out['cells'].items():
        B = by['64']['B']
        print(sec, ' '.join(f'{ioi}:{B[str(ioi)]["wholeRangeP50"]:.1f}/{B[str(ioi)]["wholeRangeP90"]:.1f}'
                            for ioi in IOIS if str(ioi) in B))
    print('mixed (CC1 64): slur 2 ranges vs plain; mean |delta| notes 4..8')
    for sec, m in out['mixed'].items():
        for ioi, vs in m.items():
            for v, r in vs.items():
                print(sec, ioi, v, r['slur2Ranges'], 'plain', r['slur2RangesPlain'],
                      {k: r['meanAbsDeltaByNote'][k] for k in ('3', '4', '5', '6', '7', '8')}, 'sigma', r['sigma'])
    out['_about'] = ('SSO string Performance patches: slur range against note length (IOI). '
                     'tools/soundlibraries/swell_vs_length.py (its header has the method and result). Measured '
                     '2026-10-13 on the Windows test VM: kthost (offline VST 3 host), Kontakt 8 + SSO, each patch from '
                     'MuseScore\'s resaved setup (library defaults), 44.1 kHz, 3 passes. cells: section / CC1 / variant '
                     '(B plain, F1 / F2 with one / two 136 ms notes at note 6 / 6-7) / IOI ms: slur ranges (dB, 3-pass '
                     'median of the perceived level over 136.4 ms from note-on + legatoDelay; first and last notes left '
                     'out), sigma (pass-to-pass SD), noise p50 / p90 (Monte Carlo slur of 4, median of 3), levels per '
                     'pattern. Measurement only: the renderer does not read this file.')
    out['method'] = dict(iois=IOIS, patterns=NPAT, mixed=MIXED, notes=NNOTES, slurs=SLURS, window=WIN, passes=PASSES,
                         velocities=dict(smooth=SMOOTH, bowed=BOWED, fingered=FINGERED), gap=GAP)
    json.dump(out, open(out_path, 'w'), separators=(',', ':'))
    print('wrote', out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    sub.add_parser('events').add_argument('work')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('name')
    r = sub.add_parser('report')
    r.add_argument('res')
    r.add_argument('-o', default=os.path.join(HERE, 'sso_swell_vs_length.json'))
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.work)
    elif o.cmd == 'analyze':
        analyze(o.folder, o.name)
    else:
        report(o.res, o.o)


if __name__ == '__main__':
    main()
