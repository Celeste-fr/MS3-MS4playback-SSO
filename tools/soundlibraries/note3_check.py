#!/usr/bin/env python3
"""Why note 3 of the 180 ms repeating figure swells (2026-10-13, branch even-180): the transition sample (E3 -> Eb3,
a semitone down into Eb3) or the context (the fast overlap, its place in the slur, the repetition)? Written for this
check, not taken from anywhere. Measurement only, uncorrected (CC11 127): the renderer doesn't read the result
(sso_note3_check.json).

  note3_check.py events <work>        passages, kthost event files, list1.txt (TAKES fresh renders of each)
  note3_check.py measure <work>       <work>/wav/*.wav -> <work>/res/<name>.json (per note: swell, dip, peak)
  note3_check.py report <work> <out>  the summary (out: the numbers-only JSON)

The owner (blind, db evenfig1): in the figure Eb3 E3 Eb3 C#3 (Celli - Performance, CC1 64, slurs touching: note 1 a
bowed transition from the slur before, 2-4 fingered) note 3 swells, at 180 ms with every correction tried; at 250 ms the
correction with each note's node in its own span fixed it. Uncorrected note 3 dips ~66 ms after its note-on and rises
4-4.5 dB by ~125 ms (even_180_check.py).

Cells (all Celli - Performance, mf, each passage a smooth lead note then touching 4-note slurs as Recommended plays
them; the figure is a cycle, so rotating where the slurs start keeps the pitch sequence and every overlap identical and
moves only the slur boundary, i.e. which transitions are bowed (velocity 93) and fingered (69)):
  fig180      Eb3 E3 Eb3 C#3   E3 -> Eb3 at position 3 (the owner's)
  rot1_180    Eb3 C#3 Eb3 E3   ... at position 1: bowed
  rot2_180    E3 Eb3 C#3 Eb3   ... at position 2
  rot4_180    C#3 Eb3 E3 Eb3   ... at position 4 (the slur's last note)
  up2_180 / dn3_180 / up5_180  the figure transposed up a whole tone, down a minor third, up a fourth (other samples,
              same intervals and timing)
  line180     X E3 Eb3 Y, X and Y different in every slur (no repetition; E3 -> Eb3 at position 3)
  fig136 / fig250 / fig350     the figure at other note lengths
  iso600 / isoUp2_600 / isoDn3_600   one slur E3 Eb3 E3 Eb3 ... at 600 ms (transposed): the semitone down and up with
              no overlap (SSO's transition tails end ~420-440 ms after their note-on)

Result (Windows test VM, kthost + Kontakt 8 + SSO, MuseScore's resaved setup; 3 fresh takes x 3 passes per passage;
swell mean dB, dip / peak ms after the note-on, medians):
  It is the transition (a), not the context. E3 -> Eb3 swells wherever it is: position 3 (the owner's) 4.49, 1 (bowed)
  3.60, 2 3.76, 4 3.70; in the non-repeating line (position 3, other neighbours every slur) 4.21. The other transitions
  of the same cycle, same positions and overlaps: 0.3-2.8. Alone at 600 ms (no overlap) it still dips and rises: 7.2
  (the slur's first E3 -> Eb3), 3.2-3.3 (later ones), dip 46-106, peak 171-361.
  It is that sample: the same semitone down into other pitches swells less (figure transposed, position 3): into F3
  3.80, Ab3 2.44, C3 2.01 (Eb3 4.49); at 600 ms into F3 4.4-6.5, C3 2.8-3.0 (and its dip comes late, ~450 ms).
  The note length decides how much of it is heard: figure position 3 at 136 / 180 / 250 / 350 ms: 3.21 / 4.49 / 7.34 /
  6.39; the rise takes ~55 ms at 180 ms (dip 67, peak 122) but ~200 ms at 250 (dip 12, peak 212) and 350 (2, 267).
  That is why S (one node per note, at its span's middle) fixed 250 ms and not 180: a ramp of one value per note can
  follow a rise spread over the note, not one within ~55 ms. A level fix at 180 ms needs faster changes (rejected:
  the 40 ms correction wobbled); what is left is changing what SSO plays for that note (another transition kind or
  velocity, through the Performance technique), not measured here.
"""
import argparse
import json
import os
import statistics as st
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import slow_slur_test as sst  # noqa: E402

PATCH = 'Celli - Performance'
TAKES = 3
FIG = ['Eb3', 'E3', 'Eb3', 'C#3']


def rot(r, k=0):
    """the figure (transposed by k semitones) rotated to start at its r-th note; the lead is the note before"""
    cyc = [sst.midi(n) + k for n in FIG]
    cyc = cyc[r:] + cyc[:r]
    lead = cyc[-1]
    return lead, cyc


NAMES_SHARP = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']


def name_of(p):
    return f'{NAMES_SHARP[p % 12]}{p // 12 - 1}'


def figure(ioi, n, r=0, k=0):
    lead, cyc = rot(r, k)
    slur = ' '.join(name_of(p) for p in cyc)
    return dict(patch=PATCH, ioi=ioi, key='figure', slurs=[name_of(lead)] + [slur] * n)


LINE = ['G3 E3 Eb3 F3', 'C3 E3 Eb3 D3', 'A3 E3 Eb3 G3', 'D3 E3 Eb3 C3', 'Bb3 E3 Eb3 F3', 'F3 E3 Eb3 A3',
        'B2 E3 Eb3 C#3', 'G3 E3 Eb3 Bb2', 'C#3 E3 Eb3 G3', 'A3 E3 Eb3 D3', 'F#3 E3 Eb3 B2']


def iso(k=0):
    a, b = name_of(sst.midi('E3') + k), name_of(sst.midi('Eb3') + k)
    return dict(patch=PATCH, ioi=600, key='iso', slurs=[name_of(sst.midi('D3') + k), ' '.join([a, b] * 4)])


PASSAGES = {
    'fig180': figure(180, 11),
    'rot1_180': figure(180, 11, r=2),
    'rot2_180': figure(180, 11, r=1),
    'rot4_180': figure(180, 11, r=3),
    'up2_180': figure(180, 11, k=2),
    'dn3_180': figure(180, 11, k=-3),
    'up5_180': figure(180, 11, k=5),
    'line180': dict(patch=PATCH, ioi=180, key='line', slurs=['D3'] + LINE),
    'fig136': figure(136, 14),
    'fig250': figure(250, 8),
    'fig350': figure(350, 6),
    'iso600': iso(),
    'isoUp2_600': iso(2),
    'isoDn3_600': iso(-3),
}

RUN_CMD = r'''@echo off
rem note 3 check (branch even-180): renders each out|events|patch of list%1.txt with kthost (MuseScore's resaved
rem setup, copied), a fresh process per line; only C:\claude\n3 is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\n3\st mkdir C:\claude\n3\st
if not exist C:\claude\n3\wav mkdir C:\claude\n3\wav
if not exist "C:\claude\n3\st\Celli - Performance.vst3state" copy /y "%SRC%\Celli - Performance.vst3state" C:\claude\n3\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
for /f "usebackq tokens=1,2,3 delims=|" %%a in ("C:\claude\n3\list%1.txt") do (
  echo %%a start %time%>>C:\claude\n3\run.out
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\n3\st\%%c.vst3state" "C:\claude\n3\ev\%%b.txt" "C:\claude\n3\wav\%%a.wav" --tail 3 --wait 600 > "C:\claude\n3\wav\%%a.log" 2>&1
  echo %%a done>>C:\claude\n3\run.out
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE %1>>C:\claude\n3\run.out
'''


def jload(*p):
    return json.load(open(os.path.join(*p)))


def events(work):
    sst.PASSAGES = PASSAGES
    sst.events(work)          # meta.json, ev/<name>_U.txt (3 passes each, CC1 64, CC11 127)
    meta = jload(work, 'meta.json')
    lines = [f'{n}_t{i}|{n}_U|{PATCH}' for n in meta for i in range(1, TAKES + 1)]
    with open(os.path.join(work, 'list1.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(lines), 'renders')


def shapes(m, t, env):
    """per note, in its note-on span [on, on + IOI): mean, swell (the largest rise: a dip, then the highest point after
    it), the dip's and the peak's times after the note-on (ms), and the level at the note-on"""
    ioi = m['ioi'] / 1000
    out = []
    for n in m['notes']:
        idx = np.where((t >= n['on']) & (t < n['on'] + ioi))[0]
        x = env[idx]
        mn = np.minimum.accumulate(x)
        k = int(np.argmax(x - mn))
        j = int(np.argmin(x[:k + 1]))
        out.append(dict(mean=round(float(x.mean()), 2), swell=round(float(x[k] - x[j]), 2),
                        dipMs=round(1000 * (t[idx[j]] - n['on'])), peakMs=round(1000 * (t[idx[k]] - n['on'])),
                        start=round(float(x[0]), 2), dip=round(float(x[j]), 2), peak=round(float(x[k]), 2)))
    return out


def measure(work):
    import soundfile as sf
    from loudness import perceived_envelope
    meta = jload(work, 'meta.json')
    os.makedirs(os.path.join(work, 'res'), exist_ok=True)
    for f in sorted(os.listdir(os.path.join(work, 'wav'))):
        if not f.endswith('.wav'):
            continue
        name = f[:-4]
        m = meta[name.rsplit('_t', 1)[0]]
        x, sr = sf.read(os.path.join(work, 'wav', f), always_2d=True)
        env, first, hop = perceived_envelope(x, sr)
        t_env = (first + np.arange(len(env)) * hop) / 1000.0
        tg = np.arange(m['first'] - 0.2, m['end'] + 1.0, sst.STEP)
        res = dict(peak=round(float(np.max(np.abs(x))), 3),
                   passes=[shapes(m, tg, np.interp(tg + p0, t_env, env)) for p0 in m['passes']])
        json.dump(res, open(os.path.join(work, 'res', name + '.json'), 'w'))
        print(name, 'peak', res['peak'])


def tag(m, k):
    """a note's place: slur position (1-4, 0 the lead), interval from the note before, its pitch"""
    n = m['notes'][k]
    if k == 0:
        return dict(pos=0, iv=None, p=n['p'], v=n['v'])
    j = k
    while j > 0 and m['notes'][j - 1]['slur'] == n['slur']:
        j -= 1
    return dict(pos=k - j + 1, iv=n['p'] - m['notes'][k - 1]['p'], p=n['p'], v=n['v'])


def report(work, out_path):
    meta = jload(work, 'meta.json')
    out = {}
    for name, m in meta.items():
        files = sorted(f for f in os.listdir(os.path.join(work, 'res')) if f.startswith(name + '_t'))
        if not files:
            continue
        rs = [jload(work, 'res', f) for f in files]
        groups = {}
        for k in range(1, len(m['notes']) - 1):          # the lead and the last note left out
            g = tag(m, k)
            key = f"pos{g['pos']} {g['iv']:+d} -> {name_of(g['p'])}" + (' bowed' if g['v'] == sst.BOWED else '')
            for r in rs:
                for p in r['passes']:
                    groups.setdefault(key, []).append(p[k])
        rec = {}
        for key, sh in sorted(groups.items()):
            rec[key] = dict(n=len(sh), swell=round(st.mean(s['swell'] for s in sh), 2),
                            swellSd=round(st.pstdev(s['swell'] for s in sh), 2),
                            dipMs=round(st.median(s['dipMs'] for s in sh)), peakMs=round(st.median(s['peakMs'] for s in sh)),
                            mean=round(st.mean(s['mean'] for s in sh), 2))
        allmean = st.mean(v['mean'] for v in rec.values())
        for v in rec.values():
            v['meanRel'] = round(v['mean'] - allmean, 2)
            del v['mean']
        out[name] = dict(ioi=m['ioi'], slurs=m['slurs'][:3], takes=len(rs), notes=rec)
        print(f"{name} ({m['ioi']} ms, {len(rs)} takes x {len(rs[0]['passes'])} passes): {m['slurs'][1]}")
        for key, v in rec.items():
            print(f"   {key:28s} swell {v['swell']:5.2f} sd {v['swellSd']:4.2f}  dip {v['dipMs']:4d}  peak {v['peakMs']:4d} ms"
                  f"  level {v['meanRel']:+5.2f}  (n {v['n']})")
    about = ('SSO Celli - Performance, uncorrected (CC1 64, CC11 127): note 3 of the 180 ms figure Eb3 E3 Eb3 C#3 against '
             'rotations of the figure (E3 -> Eb3 at slur positions 1-4), transpositions, a non-repeating line with E3 -> '
             'Eb3 at position 3, other note lengths and 600 ms notes. note3_check.py (its header has the cells). Windows '
             'test VM, kthost + Kontakt 8 + SSO, MuseScore\'s resaved setup (library defaults), a fresh process per take, '
             '44.1 kHz; perceived loudness (tools/playbackverify/loudness.py, ~dB). Per passage, per note group (slur '
             'position, interval from the note before, pitch; bowed = velocity 93), over takes x 3 passes, the passage\'s '
             'lead and last notes left out: swell (largest rise of the envelope within [note-on, note-on + IOI), mean, '
             'sd), dip and peak times after the note-on (median ms), level (mean over the span, relative to the '
             'passage\'s groups\' mean). Measurement only: the renderer does not read this file.')
    json.dump(dict(_about=about, passages=out), open(out_path, 'w'), indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for c in ('events', 'measure'):
        sub.add_parser(c).add_argument('work')
    r = sub.add_parser('report')
    r.add_argument('work')
    r.add_argument('out')
    o = ap.parse_args()
    {'events': lambda: events(o.work), 'measure': lambda: measure(o.work), 'report': lambda: report(o.work, o.out)}[o.cmd]()


if __name__ == '__main__':
    main()
