#!/usr/bin/env python3
"""Repeating figure against non-repeating line, by note length: a blind listening test of SSO's legato swell
(2026-10-13, branch tempo-line-test). Written for this test, not taken from anywhere. Why: the owner heard Whence's
136 ms Celli figure (the same four-note slur again and again) as uneven, and slow_slur_test.py's 500 / 1000 ms lines
(no figure repeated) as fine, corrected or not; length and repetition were confounded. The literature review
(research-unevenness, 2026-10-13) predicts a repeating figure's swell sounds wrong up to ~250-350 ms per note and a
non-repeating line is fine from ~200-250 ms. This renders both kinds at five lengths, uncorrected and corrected as
the even-levels job corrects, for the owner to judge. Measurement and audio only: the renderer doesn't read the result
(sso_tempo_line_test.json).

  tempo_line_test.py events <work>            kthost event files <work>/ev/<len><R|L>_U.txt, meta.json, run.cmd
  tempo_line_test.py measure <work> <name>    <work>/wav/<name>.wav -> <work>/res/<name>.json
  tempo_line_test.py correct <work> <round>   round 1 (from U) or 2 (from round 1): ev/<passage>_C<r>.txt
  tempo_line_test.py audio <work> <out> [-o sso_tempo_line_test.json]
        <out>/audio/<len>_<R|L>_<U|C>.mp3, <out>/summary.json and the committed json

Pipeline: slow_slur_test.py's, unchanged (its build, measure, fit, two rounds and loudness match; see its header),
with these passages and one correction (C = its W: the job's short window, 136.4 ms from each note's arrival, glide
nodes at the windows' middles, round 1 divided by K = 1.28, round 2 where the residual stays above 1 dB, divided by
round 1's measured gain, the round with the smaller median slur range kept). Celli - Performance, CC1 64 (mf), CC11
127, as Recommended plays slurs: the first note a smooth attack (velocity 5) sent early by the map's attackOnset, each
slur's first note a bowed transition from the slur before (93; touching slurs, [slurs] transitions), the rest fingered
(69), the note before off 24 ms (the map's overlap) after the next note-on. Note lengths (IOI) 136, 180, 250, 350,
500 ms; slurs per passage 14, 11, 8, 6, 5 (7.8-10.5 s). Both passages start with one lead-in note (smooth, its own
"slur", left out of the statistics as the passage's first note), so every counted slur is a bowed transition:
  R  repeating figure, Whence 12-TET bar 2's Celli (sso_slur_swell.json): C#3, then slurs of Eb3 E3 Eb3 C#3
     (51 52 51 49) repeated
  L  non-repeating line, D minor, stepwise and small leaps, no two slurs with the same intervals (checked): A2, then
     the first n of D3 E3 F3 G3 | A3 G3 F3 E3 | F3 A3 G3 Bb3 | A3 F3 G3 E3 | F3 D3 E3 C#3 | D3 F3 A3 G3 |
     F3 G3 E3 F3 | D3 E3 G3 F3 | E3 D3 C#3 E3 | A3 Bb3 G3 A3 | F3 E3 D3 F3 | G3 A3 F3 G3 | E3 F3 D3 E3 | C#3 D3 F3 E3
Each render: the passage three times (3 passes, one kthost process, MuseScore's resaved setup, library defaults).

Statistics (perceived envelope, tools/playbackverify/loudness.py, ~dB, 3-pass median), in the job's short window and
over each whole note (arrival to the next arrival): slur range (max - min per slur, the passage's first and last notes
left out) median and largest; and the swell split as research-unevenness section 0 did: each complete slur's levels
less its mean, averaged by position 1-4 (the repeating part: its range, 'profileRange'), the rest's RMS (the irregular
part, 'irregularRms').

Audio: pass 1, 0.5 s before the first note-on to 1.5 s after the last note-off, 44.1 kHz, LAME 192 kbps CBR; U and C
of a passage at the same EBU R128 integrated loudness, the highest at which both sample peaks stay at or below -1 dBFS
(checked on the decoded mp3s).

Result (2026-10-13, Windows test VM, kthost + Kontakt 8 + SSO, 29 renders (180R needed no round 2), ~6 min wall
time; sso_tempo_line_test.json). Slur range median / largest, then the swell's repeating part (profileRange) /
irregular part (irregularRms), dB, short window (the job's) | whole note; pass-to-pass |diff| p50 0.1-0.5:
          U short               U whole               C short               C whole            kept
  136R   3.78 4.60  3.84 0.36 | 3.77 4.46  3.80 0.33 | 1.04 1.47  0.67 0.31 | 1.07 1.58  0.77 0.30   C2
  136L   1.98 4.96  0.51 0.95 | 2.16 5.06  0.63 0.95 | 0.79 2.63  0.22 0.43 | 0.96 2.00  0.19 0.41   C2
  180R   4.29 5.07  4.17 0.38 | 4.12 4.47  4.08 0.30 | 1.02 1.44  0.85 0.26 | 1.07 1.25  0.75 0.22   C1
  180L   2.24 5.75  0.93 1.02 | 2.37 5.01  0.79 0.97 | 0.86 2.49  0.32 0.49 | 0.85 2.07  0.24 0.35   C2
  250R   3.45 3.54  3.34 0.13 | 3.03 3.18  2.91 0.16 | 1.52 2.01  1.54 0.14 | 0.86 1.26  0.88 0.17   C1
  250L   2.40 4.45  1.66 0.96 | 2.69 4.51  2.14 0.80 | 0.85 2.24  0.62 0.48 | 0.64 1.51  0.22 0.30   C2
  350R   6.07 6.49  6.12 0.14 | 5.22 5.51  5.24 0.14 | 1.57 2.07  1.64 0.32 | 1.34 1.49  1.04 0.29   C2
  350L   2.46 5.41  2.10 1.05 | 3.11 4.88  2.33 0.92 | 1.42 2.42  0.78 0.50 | 0.90 1.35  0.24 0.37   C2
  500R   6.05 6.39  5.99 0.19 | 4.39 4.58  4.32 0.15 | 0.92 1.41  1.12 0.15 | 2.90 3.27  2.97 0.17   C2
  500L   2.69 3.92  1.41 0.74 | 3.52 5.03  2.90 0.89 | 2.90 3.71  1.84 0.81 | 1.64 1.83  0.72 0.47   C1
The repeating figure's swell is almost all repeating part (profile 2.9-6.1 dB, irregular 0.13-0.38 dB) and grows with
length (3.8 dB at 136 ms to 6.0 at 350-500 ms, short window); the line's is a smaller repeating part (0.5-2.9 dB: the
bowed slur start and the arrival) with ~0.7-1.05 dB irregular. The correction brings every passage's short-window
median to 0.8-1.6 dB but 500L (2.90, no better than U: neither round helped; the job would keep it the same way);
500R's whole-note range falls only from 4.39 to 2.90 (its short window 6.05 to 0.92: the job flattens the first
136 ms of a 500 ms note). Round 1's measured gain 1.10-1.38 (K = 1.28). Loudness match: U and C of each passage
at the same integrated loudness (-12.3 to -15.8 LUFS); raw kthost renders are quiet, gains +26.6 to +29.9 dB (U) and
+28.7 to +33.0 dB (C: 2.1-3.8 dB more, the correction's shift under CC11 127); every mp3's sample peak -1.4 dBFS or
lower.
"""
import argparse
import json
import math
import os
import statistics as st
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import slow_slur_test as sst  # noqa: E402

PATCH = 'Celli - Performance'
LENGTHS = (136, 180, 250, 350, 500)
NSLURS = {136: 14, 180: 11, 250: 8, 350: 6, 500: 5}
R_FIGURE = 'Eb3 E3 Eb3 C#3'
R_LEAD = 'C#3'
L_SLURS = ['D3 E3 F3 G3', 'A3 G3 F3 E3', 'F3 A3 G3 Bb3', 'A3 F3 G3 E3', 'F3 D3 E3 C#3', 'D3 F3 A3 G3',
           'F3 G3 E3 F3', 'D3 E3 G3 F3', 'E3 D3 C#3 E3', 'A3 Bb3 G3 A3', 'F3 E3 D3 F3', 'G3 A3 F3 G3',
           'E3 F3 D3 E3', 'C#3 D3 F3 E3']
L_LEAD = 'A2'


def passages():
    out = {}
    for ioi in LENGTHS:
        n = NSLURS[ioi]
        out[f'{ioi}R'] = dict(patch=PATCH, ioi=ioi, key='figure', slurs=[R_LEAD] + [R_FIGURE] * n)
        out[f'{ioi}L'] = dict(patch=PATCH, ioi=ioi, key='D minor', slurs=[L_LEAD] + L_SLURS[:n])
    return out


def check_line():
    """no two slurs of L with the same intervals (from the note before, then within)"""
    seq = [L_LEAD] + L_SLURS
    pitches = [[sst.midi(x) for x in s.split()] for s in seq]
    seen = set()
    for i in range(1, len(pitches)):
        p = [pitches[i - 1][-1]] + pitches[i]
        iv = tuple(b - a for a, b in zip(p, p[1:]))
        assert iv not in seen, (seq[i], iv)
        seen.add(iv)


sst.PASSAGES = passages()
sst.OWN = {'C': 'short'}

RUN_CMD = r'''@echo off
rem tempo line test (branch tempo-line-test): renders each name|patch of list%1.txt with kthost (MuseScore's resaved
rem setup, copied); only C:\claude\tlt is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\tlt\st mkdir C:\claude\tlt\st
if not exist C:\claude\tlt\wav mkdir C:\claude\tlt\wav
if not exist "C:\claude\tlt\st\Celli - Performance.vst3state" copy /y "%SRC%\Celli - Performance.vst3state" C:\claude\tlt\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\tlt\list%1.txt") do (
  echo %%a start %time%>>C:\claude\tlt\run.out
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\tlt\st\%%b.vst3state" "C:\claude\tlt\ev\%%a.txt" "C:\claude\tlt\wav\%%a.wav" --tail 3 --wait 600 > "C:\claude\tlt\wav\%%a.log" 2>&1
  echo %%a done>>C:\claude\tlt\run.out
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE %1>>C:\claude\tlt\run.out
'''


def events(work):
    check_line()
    sst.events(work)
    # at 136-180 ms the last note's arrival comes after its note-off: its whole window runs to arrival + 136.4 ms
    # (the last note is left out of every statistic; this only keeps its level a number)
    p = os.path.join(work, 'meta.json')
    meta = sst.jload(p)
    for m in meta.values():
        n = m['notes'][-1]
        n['wholeEnd'] = max(n['off'], round(n['arr'] + sst.WIN, 5))
    json.dump(meta, open(p, 'w'), indent=1)
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)


def profile(m, lv):
    """the swell's repeating part (mean by slur position of each complete slur less its mean) and the rest"""
    notes = m['notes']
    by = {}
    for k in range(1, len(notes) - 1):
        by.setdefault(notes[k]['slur'], []).append(k)
    devs = []
    for s, ks in sorted(by.items()):
        if len(ks) == 4:
            mu = sum(lv[k] for k in ks) / 4
            devs.append([lv[k] - mu for k in ks])
    d = np.array(devs)
    prof = d.mean(axis=0)
    rest = d - prof
    return dict(profile=[round(float(x), 2) for x in prof], profileRange=round(float(prof.max() - prof.min()), 2),
                irregularRms=round(float(math.sqrt((rest ** 2).mean())), 2), slurs=len(devs))


def audio(work, out, out_json):
    import soundfile as sf
    meta = sst.jload(work, 'meta.json')
    vals = sst.jload(work, 'values.json')
    adir = os.path.join(out, 'audio')
    os.makedirs(adir, exist_ok=True)
    summary = {}
    for name, m in meta.items():
        ioi, kind = name[:-1], name[-1]
        key = f'{name}_C'
        cand = [r for r in (1, 2) if os.path.exists(os.path.join(work, 'res', f'{key}{r}.json'))]
        best = min(cand, key=lambda r: sst.slur_stats(m, sst.jload(work, 'res', f'{key}{r}.json')['median']['short'])['rangeMedian'])
        chosen = {'U': name + '_U', 'C': f'{key}{best}'}
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
        target = min(loud[v][0] + (-1.2 - 20 * math.log10(float(np.max(np.abs(segs[v]))))) for v in segs)
        for attempt in range(4):
            gains, finals = {}, {}
            for ver, seg in segs.items():
                g = target - loud[ver][0]
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
            target -= worst + 1.05
        meas = {}
        for ver, f in chosen.items():
            r = sst.jload(work, 'res', f + '.json')
            meas[ver] = {}
            for ms in sst.MEASURES:
                s = sst.slur_stats(m, r['median'][ms])
                meas[ver][ms] = dict(slurRanges=sst.rnd2(s['ranges']), rangeMedian=round(s['rangeMedian'], 2),
                                     rangeMax=round(max(s['ranges']), 2), residual=round(s['residual'], 2),
                                     swell=profile(m, r['median'][ms]), levels=sst.rnd2(r['median'][ms]),
                                     passToPassP50=round(st.median(abs(x - y) for x, y in zip(r['passes'][ms][0], r['passes'][ms][1])), 2))
        rounds = {f'C{rd}': {ms: round(sst.slur_stats(m, sst.jload(work, 'res', f'{key}{rd}.json')['median'][ms])['rangeMedian'], 2)
                             for ms in sst.MEASURES} for rd in cand}
        rec = vals[key]
        summary[name] = dict(
            description=f'{m["patch"]}, {"repeating figure" if kind == "R" else "non-repeating line"}, {ioi} ms '
                        f'notes, CC1 64: ' + ' | '.join(m['slurs']),
            kind=kind, noteMs=int(ioi), slurs=m['slurs'], seconds=round(b - a, 2),
            notes=[dict(name=n['name'], p=n['p'], vel=n['v'], slur=n['slur'], arrivalMs=round(n['d'] * 1000, 1))
                   for n in m['notes']],
            measures=meas, roundRangeMedians=rounds,
            correction=dict(measure='short', keptRound=best, k=sst.K, gainRound1=round(rec['gain1'], 3) if 'gain1' in rec else None,
                            values=sst.rnd2(rec['v%d' % best]), shift=round(rec['shift%d' % best], 2),
                            ccMessages=rec['cc%d' % best]),
            loudness=dict(lufsBefore={v: loud[v][0] for v in loud}, gainDb=gains, mp3=finals),
            files={v: f'{ioi}_{kind}_{v}.mp3' for v in chosen})
        print(name, 'kept C%d' % best, 'gains', gains, 'mp3', finals)
        for ver in chosen:
            print(' ', ver, {ms: (meas[ver][ms]['rangeMedian'], meas[ver][ms]['rangeMax'],
                                  meas[ver][ms]['swell']['profileRange'], meas[ver][ms]['swell']['irregularRms'])
                             for ms in sst.MEASURES})
    json.dump(summary, open(os.path.join(out, 'summary.json'), 'w'), indent=1)
    about = ('SSO Celli - Performance, repeating figure (R) against non-repeating line (L) at 136-500 ms notes, CC1 64, '
             'uncorrected (U) and evened out by CC11 as the even-levels job does (C); tools/soundlibraries/'
             'tempo_line_test.py has the method and result. Measured 2026-10-13 on the Windows test VM: kthost (offline '
             'VST 3 host), Kontakt 8 + SSO, MuseScore\'s resaved setup (library defaults), 44.1 kHz, 3 passes. '
             'measures.<U|C>.<short|whole>: slur ranges (dB, max - min, the passage\'s first and last notes left out), '
             'median and max, residual, swell (profile: the mean by slur position of complete slurs less their mean; '
             'profileRange its range; irregularRms the rest), each note\'s level (3-pass median). Measurement only: the '
             'renderer does not read this file.')
    slim = {n: {k: v for k, v in s.items() if k != 'files'} for n, s in summary.items()}
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
    a.add_argument('-o', default=os.path.join(HERE, 'sso_tempo_line_test.json'))
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.work)
    elif o.cmd == 'measure':
        sst.measure(o.work, o.name)
    elif o.cmd == 'correct':
        sst.correct(o.work, o.round)
    else:
        audio(o.work, o.out, o.o)


if __name__ == '__main__':
    main()
