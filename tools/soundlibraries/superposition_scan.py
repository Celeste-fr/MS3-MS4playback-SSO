#!/usr/bin/env python3
"""The superposition scan (2026-10-13, branch superposition-model): can a slurred phrase's level pattern in SSO's five
string sections' Performance legato be predicted by adding up each transition's own loudness ENVELOPE, measured in
isolation, at the phrase's note timings (overlap-add), instead of one number per transition? Written for this check,
not taken from anywhere. Why: the map's legatoDelay (a transition's arrival, 170-440 ms) is longer than Whence's
136 ms sixteenths, so in fast slurs 2-3 transitions sound at once; one number per transition (sso_transition_levels.
json, r 0.45 in runs) can't see that. Measurement only: the renderer doesn't read the result (sso_superposition.json).

  superposition_scan.py events <work>              kthost event files <work>/ev/<name>.txt + .json, list.txt, run.cmd
  superposition_scan.py analyze <folder> <name>    (on the VM) <name>.wav -> <name>.npz (band powers; no audio)
  superposition_scan.py predict <npz folder> [-o sso_superposition.json]

Renders (CC1 64, CC11 127, the validation phrases of transition_level_scan.py run_phrases: a spiccato note 112 then
four slurs of four 136 ms notes, each slur's first note bowed 93, the rest fingered 69, the note before off 24 ms after
the next note-on), per section, cells shuffled (fixed seed):
  T  every transition (x, y, velocity) occurring in the 30 phrases, isolated: x by an attack (spiccato 112) held
     0.8 s, then y at the velocity held 1.0 s (x off 24 ms after y's note-on), 1.0 s of silence
  A  every phrase's first note by an attack held 1.0 s
  E  every phrase's last transition with y off 136 ms after its note-on (as the phrase ends)
  Q  12 consecutive pairs from the phrases: x held 0.8 s -> y lasting L (136 / 272 ms) -> z held 1.0 s (the quick
     follow: what happens to y's voice when z comes soon)
  R  the 30 phrases themselves (one take; transition_level_scan's three takes are the reference too)
analyze keeps per render only derived numbers: the K-weighted power in the perceived-loudness model's auditory bands
(tools/playbackverify/loudness.py's perceived_envelope before its compression: 2048-point Hann FFTs every 4.99 ms,
ERB-spaced rounded-exponential filters), log10, float16; the perceived envelope itself; per T/Q/E cell each pitch's
own harmonics' power (those at least 3 % from the other pitches', analyze_sweep.unique_harmonics) every 5 ms.

predict (Linux): a phrase's band powers P(t) built from the isolated cells, then loudness.py's compression and
smoothing, and each note's 'swell' level (perturbation_scan.py's: mean over 136.4 ms from note-on + the map's
legatoDelay) compared with the phrase as rendered. Models:
  table   one number per transition: the isolated cell's own swell window (the earlier table, in this scan)
  splice  each transition's isolated cell replaces everything from its note-on (P(t) = S_k(t - on_k))
  add-g   overlap-add: P_k(t) = V_k(t - on_k) + g(t - on_k) P_{k-1}(t), V_k = S_k - g X_k (X_k: the cell's x steady),
          so an isolated cell is reproduced exactly; g(tau) the old voice's fade, (a) measured: the median of the old
          pitch's own harmonics' power after a transition, over the section's T cells, (b) fitted: 1 to d, then
          exponential with time constant T in power, (d, T) per section fitted on the even phrases, tested on the odd
Scores (held-out odd phrases; levels as shapes: minus each slur's mean): r, residual SD, and the median slur range
(max - min of a slur's four notes) after a CC11 correction by the prediction (each note measured + slur-mean(pred) -
pred), against uncorrected and against a correction from another take of the same phrases (measuring in place).

Batch 2 (--fast; after the patches' structure was known: a legato note's transition layers follow the time since the
previous note's start, and each note-on releases the previous transition voice, which fades over ~420 ms):
  F  every transition of the phrases after a 136 ms attack (the phrases' gap), y held 1.0 s
  A  every pitch of the phrases by an attack; G: 20 transitions per section after a 100 / 200 / 300 / 500 ms attack
more models: tableFast (F's swell window), addFast / addFastFit (overlap-add of F cells minus x's attack), released /
releasedFit (each voice = F's transition, its spectrum held after `freeze` s, released at the next note-on by a tail:
the FING group's release envelope, amplitude 1 -> 0.59 in 196 ms -> 0 in 224 ms more, or a tail fitted on the even
phrases).

Result (2026-10-13, Windows VM, Kontakt 8 + SSO via kthost; batch 1 103 min of audio in 13 min, batch 2 81 min in
13 min; analysis on Linux). Held-out (odd) phrases, 150 x 16 notes, all sections; r of the shapes, the slur range
removed by the prediction scaled by k fitted on the even phrases (median range 2.73 dB uncorrected):
  table (slow cells)  r -0.02  0 %     tableFast           r 0.21   0 %
  splice              r 0.49  14 %     add (measured fade) r 0.40  13 %     addFit   r 0.56  21 %
  addWarp             r 0.51  18 %     addFast             r 0.44  15 %     addFastFit r 0.56 29 %
  released            r 0.55  16 %     releasedFit         r 0.65  21 %
  in place (another take, 2026-10-12)  r 0.97  79 %   (Celli 0.94 / 60 %)
by section (releasedFit / best): Violins 1 0.65 / addFit 0.82, Violins 2 0.71 / addFit 0.72, Violas 0.34 / addFit
0.49, Celli 0.57 / addFit 0.68, Basses 0.79 / addFastFit 0.84 (Basses: the slow cells fail, r -0.03).
Findings: (1) one number per isolated transition predicts nothing in 136 ms runs (r ~0); an envelope sum is much
better (r 0.5-0.65 overall) but no better than the earlier tree models (r 0.66) and far from in place (0.97): a
correction from it removes 20-30 % of the slur range, measuring in place 79 %. (2) The quick cells: a transition
cut short by the next note keeps rising 50-100 ms after the next note-on (+4..+10 dB in its own harmonics), then
fades -2..-5 dB at 150 ms, -9..-11 at 300 ms, like a long note's release (-3 dB at ~140 ms, -10 at ~315 ms); the
next transition arrives 10-55 ms earlier after a 136 ms note than after a long one. Voices overlap as assumed. (3)
The gap matters at the fast end: the same transition after a 100 ms attack is +1.2..+3.5 dB (mean) louder in its
swell window than after 136 ms; 200-500 ms move it |0.4-1.1| dB p50 (Basses -1.2..-4.7 mean). (4) Fitted tails
and fades come out shorter than measured (old voices cut within 40-80 ms) and the predictions' shapes are too big
(k 0.4-0.8): the sum of isolated parts doesn't add up to the run; what is missing is not a fade law.
Verdict: superposition of isolated envelopes doesn't predict the swell well enough to replace measuring in place.
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
from transition_level_scan import (SECTIONS, RANGES, FINGERED, BOWED, SPICCATO, OVERLAP, MAP, avoid, run_phrases,
                                   map_info, legato_delay)

NOTE, RGAP = 0.136, 1.2
XHOLD, YHOLD, GAP = 0.8, 1.0, 1.0
QLENS = (0.136, 0.272)
NQ = 12
NG, GIOIS = 20, (0.1, 0.2, 0.3, 0.5)    # batch 2: transitions at other gaps (x's length = the IOI)
CHUNK = 600.0
LOUDNESS_A = 1e-3


def phrase_vels(sec, ps):
    return [avoid(sec, ps[0], SPICCATO)] + [BOWED if k % 4 == 0 else FINGERED for k in range(len(ps) - 1)]


def cell_list(sec):
    ps_all = run_phrases(sec)
    T, A, E = {}, {}, {}
    for ps in ps_all:
        vs = phrase_vels(sec, ps)
        A[ps[0]] = dict(kind='A', y=ps[0], vy=vs[0])
        for k in range(1, len(ps)):
            T[(ps[k - 1], ps[k], vs[k])] = dict(kind='T', x=ps[k - 1], y=ps[k], vy=vs[k])
        E[(ps[-2], ps[-1], vs[-1])] = dict(kind='E', x=ps[-2], y=ps[-1], vy=vs[-1])
    rnd = random.Random('supQ' + sec)
    Q = []
    for j in range(NQ):
        ps = ps_all[j * 2 + (1 if sec == 'vc' else 0)]
        vs = phrase_vels(sec, ps)
        k = rnd.randint(1, len(ps) - 3)
        for L in QLENS:
            Q.append(dict(kind='Q', x=ps[k - 1], y=ps[k], z=ps[k + 1], vy=vs[k], vz=vs[k + 1], L=L))
    cs = list(T.values()) + list(A.values()) + list(E.values()) + Q
    random.Random('sup' + sec).shuffle(cs)
    return cs


def fast_cell_list(sec):
    """batch 2: every transition of the phrases after a 136 ms attack (F), every pitch of the phrases by an attack (A)"""
    F, A = {}, {}
    for ps in run_phrases(sec):
        vs = phrase_vels(sec, ps)
        for k in range(len(ps)):
            A[ps[k]] = dict(kind='A', y=ps[k], vy=avoid(sec, ps[k], SPICCATO))
            if k:
                F[(ps[k - 1], ps[k], vs[k])] = dict(kind='F', x=ps[k - 1], y=ps[k], vy=vs[k])
    G = []
    for c in random.Random('supG' + sec).sample(sorted(F.values(), key=lambda c: (c['x'], c['y'], c['vy'])), NG):
        for ioi in GIOIS:
            G.append(dict(c, kind='F', ioi=ioi))
    cs = list(F.values()) + list(A.values()) + G
    random.Random('supF' + sec).shuffle(cs)
    return cs


def schedule(sec, fast=False):
    """[(notes, events)]"""
    files = []
    ev, notes, t = None, None, 0.0
    for c in (fast_cell_list(sec) if fast else cell_list(sec)):
        if ev is None or t > CHUNK:
            if ev is not None:
                files.append((notes, ev))
            ev = [(0.0, 'cc', 1, 64), (0.0, 'cc', 11, 127)]
            notes, t = [], 1.0
        n = dict(c)
        if c['kind'] == 'A':
            ev += [(t, 'on', c['y'], c['vy']), (t + YHOLD, 'off', c['y'], 0)]
            n['yOn'] = round(t, 5)
            t += YHOLD + GAP
        elif c['kind'] == 'F':
            xOn, yOn = t, t + c.get('ioi', NOTE)
            ev += [(xOn, 'on', c['x'], avoid(sec, c['x'], SPICCATO)), (yOn + OVERLAP, 'off', c['x'], 0),
                   (yOn, 'on', c['y'], c['vy']), (yOn + YHOLD, 'off', c['y'], 0)]
            n['yOn'] = round(yOn, 5)
            t = yOn + YHOLD + GAP
        else:
            xOn, yOn = t, t + XHOLD
            ev += [(xOn, 'on', c['x'], avoid(sec, c['x'], SPICCATO)), (yOn + OVERLAP, 'off', c['x'], 0),
                   (yOn, 'on', c['y'], c['vy'])]
            n['yOn'] = round(yOn, 5)
            if c['kind'] == 'T':
                ev.append((yOn + YHOLD, 'off', c['y'], 0))
                t = yOn + YHOLD + GAP
            elif c['kind'] == 'E':
                ev.append((yOn + NOTE, 'off', c['y'], 0))
                t = yOn + YHOLD + GAP
            else:
                zOn = yOn + c['L']
                ev += [(zOn + OVERLAP, 'off', c['y'], 0), (zOn, 'on', c['z'], c['vz']), (zOn + YHOLD, 'off', c['z'], 0)]
                n['zOn'] = round(zOn, 5)
                t = zOn + YHOLD + GAP
        notes.append(n)
    files.append((notes, ev))
    if fast:
        return files
    # the phrases (one take)
    ev = [(0.0, 'cc', 1, 64), (0.0, 'cc', 11, 127)]
    rn, t = [], 1.0
    for ps in run_phrases(sec):
        vels = phrase_vels(sec, ps)
        ons = []
        for k, (p, v) in enumerate(zip(ps, vels)):
            last = k == len(ps) - 1
            ev.append((t, 'on', p, v))
            ev.append((t + NOTE + (0 if last else OVERLAP), 'off', p, 0))
            ons.append(round(t, 5))
            t += NOTE
        rn.append(dict(kind='R', pitches=ps, vels=vels, ons=ons))
        t += RGAP
    files.append((rn, ev))
    return files


RUN_CMD = r'''@echo off
rem superposition scan (branch superposition-model): renders list.txt (name|patch) with kthost, analyses each wav
rem here and deletes it (literal path); only C:\claude\sps is touched
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist C:\claude\sps\st mkdir C:\claude\sps\st
if not exist C:\claude\sps\r mkdir C:\claude\sps\r
for %%f in ("Violins 1 - Performance" "Violins 2 - Performance" "Violas - Performance" "Celli - Performance" "Basses - Performance") do if not exist "C:\claude\sps\st\%%~f.vst3state" copy /y "%SRC%\%%~f.vst3state" C:\claude\sps\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
if errorlevel 1 (echo share login failed>>C:\claude\sps\run.out & exit /b 1)
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\sps\list.txt") do (
  if exist C:\claude\sps\stop.txt goto :end
  if not exist "C:\claude\sps\r\%%a.npz" (
    echo %%a start !time!>>C:\claude\sps\run.out
    set W=600
    if "%%b"=="Basses - Performance" set W=20
    C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\sps\st\%%b.vst3state" "C:\claude\sps\ev\%%a.txt" "C:\claude\sps\r\%%a.wav" --tail 3 --wait !W! > "C:\claude\sps\r\%%a.log" 2>&1
    "C:\Program Files\Python312\python.exe" C:\claude\sps\tools\superposition_scan.py analyze C:\claude\sps\r %%a >> "C:\claude\sps\r\%%a.log" 2>&1
    if exist "C:\claude\sps\r\%%a.npz" del "C:\claude\sps\r\%%a.wav"
    echo %%a done !time!>>C:\claude\sps\run.out
  )
)
:end
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE>>C:\claude\sps\run.out
'''


def events(work, fast=False):
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    lst, total = [], 0.0
    for sec, patch in SECTIONS.items():
        for k, (notes, ev) in enumerate(schedule(sec, fast)):
            name = f'{"f" if fast else ""}{sec}_{k + 1}'
            ev.sort(key=lambda e: (e[0], {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
            with open(os.path.join(work, 'ev', name + '.txt'), 'w', newline='\r\n') as f:
                for e in ev:
                    f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')
            length = max(e[0] for e in ev) + 3
            json.dump(dict(patch=patch, section=sec, length=length, notes=notes),
                      open(os.path.join(work, 'ev', name + '.json'), 'w'))
            lst.append(f'{name}|{patch}')
            total += length
    with open(os.path.join(work, 'list.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lst) + '\n')
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(RUN_CMD)
    print(len(lst), 'files', round(total / 60, 1), 'min of audio')


# --- analysis (VM) --------------------------------------------------------------------------------------------------

def band_powers(x, sr):
    """loudness.perceived_envelope's band powers: (E frames x bands, first_ms, hop_ms)"""
    import numpy as np
    from loudness import k_weighted, erb_weights
    N = 2048
    y = k_weighted(x, sr)
    hop = max(1, int(sr * 0.005))
    window = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(N) / (N - 1))
    padded = np.concatenate([y, np.zeros(N)])
    starts = np.arange(0, len(y) + N // 2 - N + 1, hop)
    W = erb_weights(sr, N)
    out = np.empty((len(starts), W.shape[0]))
    for c in range(0, len(starts), 512):
        idx = starts[c:c + 512]
        seg = np.stack([padded[s:s + N] for s in idx]) * window
        power = np.abs(np.fft.rfft(seg, axis=1)[:, :N // 2]) ** 2
        out[c:c + len(idx)] = power @ W.T
    return out, 1024 * 1000.0 / sr, hop * 1000.0 / sr


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12)


def own_harmonics(m, sr, t0, t1, pitches):
    """{pitch: dB list every 5 ms from t0} of each pitch's own harmonics (3 % from the others')"""
    import numpy as np
    from analyze_sweep import unique_harmonics
    N = 8192 if min(hz(p) for p in pitches) < 120 else 4096
    hop = int(sr * 0.005)
    top = min(8000.0, sr * 0.45)
    lobe = 3.0 * sr / N
    freqs = np.fft.rfftfreq(N, 1 / sr)
    win = np.hanning(N)
    starts = np.arange(max(0, int(t0 * sr) - N // 2), min(len(m) - N, int(t1 * sr) - N // 2), hop)
    seg = np.stack([m[s:s + N] for s in starts]) * win
    P = np.abs(np.fft.rfft(seg, axis=1)) ** 2
    out = {}
    for p in pitches:
        hs = list(unique_harmonics(hz(p), hz(pitches[0]) if p != pitches[0] else hz(pitches[1]), top, lobe))
        for o in pitches:
            if o != p:
                hs = [h for h in hs if h in unique_harmonics(hz(p), hz(o), top, lobe)]
        bins = [np.nonzero(np.abs(freqs / h - 1) <= 0.015)[0] for h in hs]
        bins = [b for b in bins if len(b)]
        if not bins:
            out[str(p)] = None
            continue
        e = sum(P[:, b].max(axis=1) for b in bins)
        out[str(p)] = [round(float(v), 1) for v in 10 * np.log10(np.maximum(e, 1e-20))]
    return out


def analyze(folder, name):
    import numpy as np
    import soundfile as sf
    from loudness import perceived_envelope
    meta = json.load(open(os.path.join(folder, '..', 'ev', name + '.json')))
    x, sr = sf.read(os.path.join(folder, name + '.wav'), always_2d=True)
    E, first, hop = band_powers(x, sr)
    env, _, _ = perceived_envelope(x, sr)
    m = x.mean(axis=1)
    harm = []
    for n in meta['notes']:
        if n['kind'] in ('T', 'E', 'F'):
            harm.append(own_harmonics(m, sr, n['yOn'] - 0.5, n['yOn'] + 1.6, [n['x'], n['y']]))
        elif n['kind'] == 'Q':
            ps = [n['x'], n['y'], n['z']]
            harm.append(own_harmonics(m, sr, n['yOn'] - 0.5, n['zOn'] + 1.6, ps) if len(set(ps)) == 3 else None)
        else:
            harm.append(None)
    meta['sr'], meta['firstMs'], meta['hopMs'] = sr, first, hop
    meta['harm'] = harm
    np.savez_compressed(os.path.join(folder, name + '.npz'),
                        bands=np.log10(np.maximum(E, 1e-12)).astype(np.float16),
                        env=env.astype(np.float32), meta=np.frombuffer(json.dumps(meta).encode(), dtype=np.uint8))
    print(name, len(meta['notes']), 'cells analysed', E.shape)


# --- prediction (Linux) ---------------------------------------------------------------------------------------------

def compress(E, hop_ms):
    """band powers -> loudness.py's perceived envelope (compression, smoothing, 33.2 log10)"""
    import numpy as np
    a03 = LOUDNESS_A ** 0.3
    loud = ((E + LOUDNESS_A) ** 0.3 - a03).sum(axis=1)
    dt = hop_ms / 1000
    att, rel = 1 - math.exp(-dt / 0.022), 1 - math.exp(-dt / 0.050)
    out = np.empty(len(loud))
    s = 0.0
    for j, l in enumerate(loud):
        s += (att if l > s else rel) * (l - s)
        out[j] = 33.2 * math.log10(s) if s > 0 else -200.0
    return out


def load(folder):
    import numpy as np
    out = {}
    for fn in sorted(os.listdir(folder)):
        if not fn.endswith('.npz'):
            continue
        z = np.load(os.path.join(folder, fn))
        meta = json.loads(bytes(z['meta']).decode())
        d = out.setdefault(meta['section'], dict(patch=meta['patch'], cells={}, runs=[], q=[], a={}, e={}, f={}))
        E = (10.0 ** z['bands'].astype(np.float64))
        first, hop = meta['firstMs'] / 1000, meta['hopMs'] / 1000

        def cut(t0, t1):
            i0 = int(round((t0 - first) / hop))
            return E[i0:i0 + int(round((t1 - t0) / hop))]
        for n, h in zip(meta['notes'], meta['harm']):
            if n['kind'] == 'R':
                d['runs'].append(dict(n, E=cut(n['ons'][0] - 0.3, n['ons'][-1] + 1.0), t0=n['ons'][0] - 0.3))
            elif n['kind'] == 'A':
                d['a'][n['y']] = cut(n['yOn'] - 0.3, n['yOn'] + 1.5)
            elif n['kind'] == 'T':
                d['cells'][(n['x'], n['y'], n['vy'])] = dict(S=cut(n['yOn'] - 0.3, n['yOn'] + 1.5), harm=h)
            elif n['kind'] == 'F' and 'ioi' in n:
                d.setdefault('g', {})[(n['x'], n['y'], n['vy'], n['ioi'])] = dict(
                    S=cut(n['yOn'] - 0.3, n['yOn'] + 1.5), harm=h)
            elif n['kind'] == 'F':
                d['f'][(n['x'], n['y'], n['vy'])] = dict(S=cut(n['yOn'] - 0.3, n['yOn'] + 1.5), harm=h)
            elif n['kind'] == 'E':
                d['e'][(n['x'], n['y'], n['vy'])] = dict(S=cut(n['yOn'] - 0.3, n['yOn'] + 1.5), harm=h)
            else:
                d['q'].append(dict(n, S=cut(n['yOn'] - 0.3, n['zOn'] + 1.5), harm=h))
        d['hop'] = hop
    return out


PRE = 0.3   # cells start 0.3 s before the note-on
ABOUT = ('SSO string Performance patches: slurred phrases\' per-note levels predicted from isolated transitions '
         '(one number, or their band-power envelopes added up at the phrase\'s timings) against the phrases as '
         'rendered. tools/soundlibraries/superposition_scan.py (its header has the method and result). Measured '
         '2026-10-13 on the Windows test VM: kthost (offline VST 3 host), Kontakt 8 + SSO, each patch from '
         'MuseScore\'s resaved setup (library defaults), 44.1 kHz, CC1 64, CC11 127. Levels: perturbation_scan.py\'s '
         'swell (perceived loudness, tools/playbackverify/loudness.py, ~dB), as shapes (minus each slur\'s mean). '
         'sections.<sec>.<model>: scores on the odd (held-out) phrases: r, residSd, measSd, rangeUnc / rangeCorr '
         '(median slur range before / after subtracting the prediction), removed, k (scale fitted on the even '
         'phrases) and rangeCorrScaled / removedScaled; inPlace: another take of the same phrases '
         '(sso_transition_levels.json runs, 2026-10-12) as the prediction; fits (fadeFit, warpFit, fadeFitFast, '
         'tailFit); fadeMeasured: the old pitch\'s own harmonics after a transition, dB every 10 ms; quick: the '
         'quick-follow cells; gap: the same transition after a 100 / 200 / 300 / 500 ms attack minus after 136 ms; '
         'phrases: per phrase the measured levels and each model\'s predicted shapes. all: every section\'s odd '
         'phrases together. Measurement only: the renderer does not read this file.')


def fade_measured(d):
    """median over T cells of the old pitch's own harmonics' power after y's note-on, against its level before
    (power ratio per 5 ms frame from the note-on)"""
    import numpy as np
    rows = []
    for (x, y, v), c in d['cells'].items():
        h = c['harm']
        if not h or h.get(str(x)) is None:
            continue
        a = np.array(h[str(x)])            # from yOn - 0.5, 5 ms
        pre = np.median(a[int(0.2 / 0.005):int(0.48 / 0.005)])
        post = a[int(0.5 / 0.005):]
        rows.append(10 ** ((post - pre) / 10))
    n = min(len(r) for r in rows)
    g = np.median(np.stack([r[:n] for r in rows]), axis=0)
    return np.minimum(1.0, g)


def fade_param(d_s, T_s, n, hop):
    import numpy as np
    t = np.arange(n) * hop
    return np.where(t < d_s, 1.0, np.exp(-(t - d_s) / max(T_s, 1e-3)))


def predict_phrase(d, run, model, g=None):
    """predicted band powers over run['E']'s frames"""
    import numpy as np
    hop = d['hop']
    E = run['E']
    nF = len(E)
    ons = [int(round((o - run['t0']) / hop)) for o in run['ons']]
    ps, vs = run['pitches'], run['vels']
    pre = int(round(PRE / hop))
    P = np.zeros_like(E)
    # the attack
    S = d['a'][ps[0]]
    a0 = ons[0] - pre
    seg = S[max(0, -a0):min(len(S), nF - a0)]
    P[max(0, a0):max(0, a0) + len(seg)] += seg
    last = len(ps) - 1
    for k in range(1, len(ps)):
        key = (ps[k - 1], ps[k], vs[k])
        c = (d['e'] if k == last and key in d['e'] else d['cells'])[key]['S']
        X = np.median(d['cells'][key]['S'][:pre - 4], axis=0)
        i0 = ons[k] - pre
        n = min(len(c), nF - i0)
        if model == 'splice':
            P[ons[k]:i0 + n] = c[pre:n]
            continue
        gg = np.ones(n)
        m = min(len(g), n - pre)
        gg[pre:pre + m] = g[:m]
        gg[pre + m:] = g[-1] if m < n - pre else gg[pre + m:]
        V = np.maximum(c[:n] - gg[:, None] * X[None, :], 0)
        # frames before the note-on: keep what's there
        P[ons[k]:i0 + n] = V[pre:] + gg[pre:, None] * P[ons[k]:i0 + n]
    return P


def warp(V, s, span, hop):
    """V's first span seconds played s times as long (s < 1: quicker), the rest after it"""
    import numpy as np
    n = int(round(span / hop))
    m = int(round(span * s / hop))
    src = np.concatenate([np.arange(m) / s, n + np.arange(len(V) - n)])
    i0 = np.clip(np.floor(src).astype(int), 0, len(V) - 1)
    i1 = np.clip(i0 + 1, 0, len(V) - 1)
    f = (src - np.floor(src))[:, None]
    return V[i0] * (1 - f) + V[i1] * f


def predict_voices(d, run, g, nmax, kill, s=1.0, span=0.4, cells='cells'):
    """overlap-add by voices: voice k = V_k (its cell minus the faded x steady) from its note-on, faded by g after
    each later transition's note-on; a voice with nmax later transitions started is ended by a fade of time constant
    kill (s) from that note-on (voice stealing); the attack is voice 0 (its cell)"""
    import numpy as np
    hop = d['hop']
    E = run['E']
    nF = len(E)
    ons = [int(round((o - run['t0']) / hop)) for o in run['ons']]
    ps, vs = run['pitches'], run['vels']
    pre = int(round(PRE / hop))
    voices = []
    S0 = d['a'][ps[0]][pre:]
    voices.append((ons[0], S0))
    last = len(ps) - 1
    for k in range(1, len(ps)):
        key = (ps[k - 1], ps[k], vs[k])
        n = len(d['cells'][key]['S']) - pre
        gg = np.ones(n)
        m = min(len(g), n)
        gg[:m] = g[:m]
        gg[m:] = g[-1]
        if cells == 'fast':
            # the cell after a 136 ms attack, minus that attack (its own cell, 136 ms on) faded by g
            c = d['f'][key]['S']
            sh = int(round(NOTE / hop))
            Ax = d['a'][ps[k - 1]][pre + sh:]
            n = min(n, len(c) - pre, len(Ax))
            V = np.maximum(c[pre:pre + n] - gg[:n, None] * Ax[:n], 0)
        else:
            c = (d['e'] if k == last and key in d['e'] else d['cells'])[key]['S']
            X = np.median(d['cells'][key]['S'][:pre - 4], axis=0)
            V = np.maximum(c[pre:] - gg[:, None] * X[None, :], 0)
        voices.append((ons[k], warp(V, s, span, hop) if s != 1.0 else V))
    P = np.zeros_like(E)
    t = np.arange(nF)
    for k, (o, V) in enumerate(voices):
        w = np.zeros(nF)
        n = min(len(V), nF - o)
        w[o:o + n] = 1.0
        for j in range(k + 1, len(voices)):
            oj = voices[j][0]
            tau = t[oj:] - oj
            if j - k >= nmax:
                w[oj:] *= np.exp(-tau * hop / max(kill, 1e-3))
                break
            gj = np.where(tau < len(g), g[np.minimum(tau, len(g) - 1)], g[-1])
            w[oj:] *= gj
        P[o:o + n] += V[:n] * w[o:o + n, None]
    return P


def tail_power(n, hop, tail):
    """a released voice's power gain, n frames from its note-off: amplitude 1 -> l1 in t1 s -> 0 in t2 s more
    (linear), squared"""
    import numpy as np
    t1, l1, t2 = tail
    t = np.arange(n) * hop
    a = np.where(t < t1, 1 + (l1 - 1) * t / t1, np.maximum(0.0, l1 * (1 - (t - t1) / t2)))
    return a * a


def predict_released(d, run, tail, freeze=0.2):
    """the voices as the script plays a fast slur (2026-10-13 notes on the patches' groups): at each legato note-on
    the previous transition voice (and the silent new sustain) gets its note-off and fades by its release tail;
    voice k = the 136 ms cell's transition (minus x's attack, faded by the slow cells' measured g) up to `freeze` s
    after its note-on (before the destination sustain's fade-in), its band spectrum held after that; voice 0 = the
    attack; every voice released at the next note-on (the last at its note-off)"""
    import numpy as np
    hop = d['hop']
    E = run['E']
    nF = len(E)
    ons = [int(round((o - run['t0']) / hop)) for o in run['ons']]
    ps, vs = run['pitches'], run['vels']
    pre = int(round(PRE / hop))
    g = d.setdefault('_gm', fade_measured(d))
    sh = int(round(NOTE / hop))
    fz = int(round(freeze / hop)) if freeze else None
    voices = [(ons[0], d['a'][ps[0]][pre:])]
    for k in range(1, len(ps)):
        c = d['f'][(ps[k - 1], ps[k], vs[k])]['S']
        Ax = d['a'][ps[k - 1]][pre + sh:]
        n = min(len(c) - pre, len(Ax), len(g))
        V = np.maximum(c[pre:pre + n] - g[:n, None] * Ax[:n], 0)
        if fz:
            V = np.concatenate([V[:fz], np.repeat(V[fz - 1:fz], len(V) - fz, axis=0)])
        voices.append((ons[k], V))
    offs = ons[1:] + [ons[-1] + sh]
    P = np.zeros_like(E)
    for (o, V), off in zip(voices, offs):
        n = min(len(V), nF - o)
        w = np.ones(n)
        r = off - o
        if r < n:
            w[r:] = tail_power(n - r, hop, tail)
        P[o:o + n] += V[:n] * w[:, None]
    return P


def note_levels(env, run, info, hop):
    """swell level per note (0: None)"""
    ps, ons = run['pitches'], run['ons']
    out = [None]
    for k in range(1, len(ps)):
        a = ons[k] + legato_delay(info, ps[k] - ps[k - 1]) / 1000
        i0 = int(round((a - run['t0']) / hop))
        i1 = int(round((a + 0.1364 - run['t0']) / hop))
        out.append(float(sum(env[i0:i1]) / (i1 - i0)))
    return out


def table_levels(d, run, info, hop):
    """one number per transition: the isolated cell's swell window"""
    ps, vs = run['pitches'], run['vels']
    pre = int(round(PRE / hop))
    out = [None]
    for k in range(1, len(ps)):
        c = d['cells'][(ps[k - 1], ps[k], vs[k])]
        if 'env' not in c:
            c['env'] = compress(c['S'], hop * 1000)
        a = pre + int(round(legato_delay(info, ps[k] - ps[k - 1]) / 1000 / hop))
        out.append(float(sum(c['env'][a:a + int(round(0.1364 / hop))]) / int(round(0.1364 / hop))))
    return out


def shapes(lv):
    """notes 1..16 minus their slur's mean"""
    out = []
    for s in range(4):
        v = lv[1 + 4 * s:5 + 4 * s]
        m = sum(v) / 4
        out += [x - m for x in v]
    return out


def slope(meas, pred):
    """least-squares scale of the prediction's shapes onto the measured ones"""
    import numpy as np
    M, P = np.concatenate(meas), np.concatenate(pred)
    return float((M * P).sum() / max((P * P).sum(), 1e-9))


def score(meas, pred, k=None, scaled=None):
    """r, residual SD, median slur range uncorrected / corrected by the prediction (each note measured - pred:
    'removed') and by the prediction times k, fitted on the training phrases ('removedScaled'; scaled: those
    predictions already scaled)"""
    import numpy as np
    M = np.concatenate(meas)
    Pp = np.concatenate(pred)
    rng = lambda a: [float(a[i:i + 4].max() - a[i:i + 4].min()) for i in range(0, len(a), 4)]
    unc = float(np.median(sum((rng(np.array(m)) for m in meas), [])))
    cor = float(np.median(sum((rng(np.array(m) - np.array(p)) for m, p in zip(meas, pred)), [])))
    if scaled is None and k is not None:
        scaled = [[k * x for x in p] for p in pred]
    out = dict(n=len(M), r=round(float(np.corrcoef(M, Pp)[0, 1]), 3), residSd=round(float(np.std(M - Pp)), 2),
               measSd=round(float(np.std(M)), 2), rangeUnc=round(unc, 2), rangeCorr=round(cor, 2),
               removed=round(1 - cor / unc, 2))
    if scaled is not None:
        cs = float(np.median(sum((rng(np.array(m) - np.array(p)) for m, p in zip(meas, scaled)), [])))
        out.update(k=round(k, 2) if k is not None else None, rangeCorrScaled=round(cs, 2),
                   removedScaled=round(1 - cs / unc, 2))
    return out


def quick(d, models):
    """Q cells: (1) the old voices after the quick note's successor starts: y's own harmonics' drop 50 / 100 / 200 ms
    after z's note-on against y's level at z's note-on, in Q (y L long) and in the T cell y -> z (y held 0.8 s);
    x's own harmonics at z's note-on + 0 / 100 ms, in Q against the T cell x -> y at the same time after y's note-on;
    (2) each model's perceived-envelope error over yOn .. zOn + 0.6 s (dB RMS) and the bias"""
    import numpy as np
    hop = d['hop']
    pre = int(round(PRE / hop))
    rows = []
    for q in d['q']:
        h = q['harm']
        kxy, kyz = (q['x'], q['y'], q['vy']), (q['y'], q['z'], q['vz'])
        if kxy not in d['cells'] or kyz not in d['cells']:
            continue
        r = dict(L=q['L'], x=q['x'], y=q['y'], z=q['z'])
        L5 = int(round(q['L'] / 0.005))
        o = int(0.5 / 0.005)                       # harm arrays start 0.5 s before y's note-on
        if h and h.get(str(q['y'])) and d['cells'][kyz]['harm'] and d['cells'][kyz]['harm'].get(str(q['y'])):
            a = np.array(h[str(q['y'])])
            b = np.array(d['cells'][kyz]['harm'][str(q['y'])])   # from z's note-on - 0.5 s
            at = a[o + L5]
            r['yDropQ'] = [round(float(a[o + L5 + k] - at), 1) for k in (10, 20, 40)]
            r['yDropT'] = [round(float(b[o + k] - b[o]), 1) for k in (10, 20, 40)]
        if h and h.get(str(q['x'])) and d['cells'][kxy]['harm'] and d['cells'][kxy]['harm'].get(str(q['x'])):
            a = np.array(h[str(q['x'])])
            b = np.array(d['cells'][kxy]['harm'][str(q['x'])])
            a = a - np.median(a[40:96])            # each against x's own steady level before y
            b = b - np.median(b[40:96])
            r['xAfterZ'] = [round(float(a[o + L5 + k] - b[o + L5 + k]), 1) for k in (0, 10, 20, 40)]
        # models: a two-note 'phrase' x (held, from the T cell's pre-roll) -> y -> z
        meas = compress(q['S'], hop * 1000)
        run = dict(pitches=[q['x'], q['y'], q['z']], vels=[0, q['vy'], q['vz']],
                   ons=[0.3, 0.3 + q['L']], t0=0.0, E=q['S'])
        i0, i1 = pre, pre + int(round((q['L'] + 0.6) / hop))
        for name, g in models.items():
            c1 = d['cells'][kxy]['S']
            n = min(len(c1), len(q['S']))
            P = np.zeros_like(q['S'])
            P[:n] = c1[:n]                          # x held then y (isolated)
            k = pre + int(round(q['L'] / hop))
            c2 = d['cells'][kyz]['S']
            m = min(len(c2) - pre, len(P) - k)
            if name == 'splice':
                P[k:k + m] = c2[pre:pre + m]
            else:
                X = np.median(c2[:pre - 4], axis=0)
                gg = np.ones(m)
                gm = min(len(g), m)
                gg[:gm] = g[:gm]
                gg[gm:] = g[-1]
                V = np.maximum(c2[pre:pre + m] - gg[:, None] * X[None, :], 0)
                P[k:k + m] = V + gg[:, None] * P[k:k + m]
            pe = compress(P, hop * 1000)
            dlt = pe[i0:i1] - meas[i0:i1]
            r[name] = [round(float(np.sqrt((dlt ** 2).mean())), 2), round(float(dlt.mean()), 2)]
        rows.append(r)
    out = {}
    for L in QLENS:
        rs = [r for r in rows if r['L'] == L]
        s = dict(n=len(rs))
        for key in ('yDropQ', 'yDropT', 'xAfterZ'):
            v = [r[key] for r in rs if key in r]
            if v:
                s[key] = [round(float(np.median([x[i] for x in v])), 1) for i in range(len(v[0]))]
        for name in models:
            v = [r[name] for r in rs]
            s[name] = dict(rms=round(float(np.median([x[0] for x in v])), 2),
                           bias=round(float(np.median([x[1] for x in v])), 2))
        out[str(L)] = s
    return out


def predict(folder, out_path, map_path, old_runs=None):
    import numpy as np
    infos = map_info(map_path)
    data = load(folder)
    ref = {}
    if old_runs:
        for p, rows in json.load(open(old_runs))['runs'].items():
            for r in rows:
                ref[(p, tuple(r['pitches']))] = r['takes']
    result = dict(sections={})
    allrows = {}
    for sec, d in data.items():
        info = infos[d['patch']]
        hop = d['hop']
        gm = fade_measured(d)
        runs = d['runs']
        even = [j for j in range(len(runs)) if j % 2 == 0]
        odd = [j for j in range(len(runs)) if j % 2]
        meas_sh = []
        for run in runs:
            run['meas'] = note_levels(compress(run['E'], hop * 1000), run, info, hop)
            meas_sh.append(shapes(run['meas']))

        def levels_of(fn):
            return [shapes(fn(run)) for run in runs]

        def env_levels(P, run):
            return note_levels(compress(P, hop * 1000), run, info, hop)

        def mse(pr, js):
            return float(np.mean([(a - b) ** 2 for j in js for a, b in zip(meas_sh[j], pr[j])]))
        preds, extra = {}, {}
        preds['table'] = levels_of(lambda run: table_levels(d, run, info, hop))
        preds['splice'] = levels_of(lambda run: env_levels(predict_phrase(d, run, 'splice'), run))
        preds['add'] = levels_of(lambda run: env_levels(predict_voices(d, run, gm, 99, 0.01), run))
        # fitted fade (d, T) on the even phrases
        best = None
        for dd in (0.0, 0.02, 0.04, 0.06, 0.08, 0.1, 0.13, 0.16, 0.2, 0.25):
            for TT in (0.01, 0.02, 0.04, 0.07, 0.1, 0.15, 0.2, 0.3):
                g = fade_param(dd, TT, 400, hop)
                pr = {j: shapes(env_levels(predict_voices(d, runs[j], g, 99, 0.01), runs[j])) for j in even}
                e = mse(pr, even)
                if best is None or e < best[0]:
                    best = (e, dd, TT)
        gf = fade_param(best[1], best[2], 400, hop)
        extra['fadeFit'] = dict(d=best[1], T=best[2])
        preds['addFit'] = levels_of(lambda run: env_levels(predict_voices(d, run, gf, 99, 0.01), run))
        # time-warped slow cells (the transition's first 0.5 s played s times as long), s fitted on the even phrases
        best = None
        for sw in (0.5, 0.65, 0.8, 1.0):
            pr = {j: shapes(env_levels(predict_voices(d, runs[j], gm, 99, 0.01, sw, 0.5), runs[j])) for j in even}
            e = mse(pr, even)
            if best is None or e < best[0]:
                best = (e, sw)
        extra['warpFit'] = best[1]
        sw = best[1]
        preds['addWarp'] = levels_of(lambda run: env_levels(predict_voices(d, run, gm, 99, 0.01, sw, 0.5), run))
        if d['f']:
            def table_fast(run):
                ps, vs = run['pitches'], run['vels']
                pre = int(round(PRE / hop))
                out = [None]
                for k in range(1, len(ps)):
                    c = d['f'][(ps[k - 1], ps[k], vs[k])]
                    if 'env' not in c:
                        c['env'] = compress(c['S'], hop * 1000)
                    a = pre + int(round(legato_delay(info, ps[k] - ps[k - 1]) / 1000 / hop))
                    w = int(round(0.1364 / hop))
                    out.append(float(sum(c['env'][a:a + w]) / w))
                return out
            preds['tableFast'] = levels_of(table_fast)
            preds['addFast'] = levels_of(lambda run: env_levels(predict_voices(d, run, gm, 99, 0.01, cells='fast'), run))
            best = None
            for dd in (0.0, 0.04, 0.08, 0.13, 0.2):
                for TT in (0.02, 0.05, 0.1, 0.2, 0.3):
                    g = fade_param(dd, TT, 400, hop)
                    pr = {j: shapes(env_levels(predict_voices(d, runs[j], g, 99, 0.01, cells='fast'), runs[j]))
                          for j in even}
                    e = mse(pr, even)
                    if best is None or e < best[0]:
                        best = (e, dd, TT)
            gff = fade_param(best[1], best[2], 400, hop)
            extra['fadeFitFast'] = dict(d=best[1], T=best[2])
            # released voices (the script's structure): FING's tail from the group data (amplitude 1 -> 0.59 in
            # 196 ms -> 0 in 224 ms more), and a tail fitted on the even phrases
            preds['released'] = levels_of(lambda run: env_levels(predict_released(d, run, (0.196, 0.59, 0.224)), run))
            best = None
            for t1 in (0.05, 0.1, 0.2, 0.3):
                for l1 in (0.3, 0.59, 0.85):
                    for t2 in (0.1, 0.224, 0.4):
                        for fz in (0.14, 0.2, 0.3):
                            pr = {j: shapes(env_levels(predict_released(d, runs[j], (t1, l1, t2), fz), runs[j]))
                                  for j in even}
                            e = mse(pr, even)
                            if best is None or e < best[0]:
                                best = (e, (t1, l1, t2), fz)
            extra['tailFit'] = dict(tail=best[1], freeze=best[2])
            tl, fzb = best[1], best[2]
            preds['releasedFit'] = levels_of(lambda run: env_levels(predict_released(d, run, tl, fzb), run))
            preds['addFastFit'] = levels_of(
                lambda run: env_levels(predict_voices(d, run, gff, 99, 0.01, cells='fast'), run))
        # in place: another take of the same phrases (transition_level_scan's runs, 2026-10-12) as the correction
        inpl = {}
        for j, run in enumerate(runs):
            t = ref.get((d['patch'], tuple(run['pitches'])))
            if t:
                inpl[j] = [shapes([None] + tk[0]['swell'][1:]) for tk in t.values()]
        sec_res = dict(extra, fadeMeasured=[round(float(10 * math.log10(max(v, 1e-6))), 1) for v in gm[:80:2]])
        for name, pr in preds.items():
            k = slope([meas_sh[j] for j in even], [pr[j] for j in even])
            sec_res[name] = score([meas_sh[j] for j in odd], [pr[j] for j in odd], k)
            r = allrows.setdefault(name, ([], [], []))
            r[0].extend(meas_sh[j] for j in odd)
            r[1].extend(pr[j] for j in odd)
            r[2].extend([[k * x for x in pr[j]] for j in odd])
        if inpl:
            m_, p_ = [], []
            for j in odd:
                for sh in inpl.get(j, []):
                    m_.append(meas_sh[j])
                    p_.append(sh)
            sec_res['inPlace'] = score(m_, p_, 1.0)
            r = allrows.setdefault('inPlace', ([], [], []))
            r[0].extend(m_)
            r[1].extend(p_)
            r[2].extend(p_)
        sec_res['quick'] = quick(d, {'splice': None, 'add': gm, 'addFit': gf})
        if d.get('g'):
            # the same transition after other gaps: its cell's swell window and its mean over 0-0.4 s after the
            # note-on, minus the 136 ms cell's (x's attack is in both, at another distance)
            pre = int(round(PRE / hop))
            gap = {}
            for (x, y, v, ioi), c in d['g'].items():
                f = d['f'].get((x, y, v))
                if not f:
                    continue
                a = pre + int(round(legato_delay(info, y - x) / 1000 / hop))
                w = int(round(0.1364 / hop))
                eg, ef = compress(c['S'], hop * 1000), compress(f['S'], hop * 1000)
                n4 = int(round(0.4 / hop))
                gap.setdefault(str(ioi), []).append((float(eg[a:a + w].mean() - ef[a:a + w].mean()),
                                                     float(eg[pre:pre + n4].mean() - ef[pre:pre + n4].mean())))
            sec_res['gap'] = {k: dict(n=len(v), swellMean=round(float(np.mean([x[0] for x in v])), 2),
                                      swellAbsP50=round(float(np.median([abs(x[0]) for x in v])), 2),
                                      first400Mean=round(float(np.mean([x[1] for x in v])), 2))
                              for k, v in sorted(gap.items())}
        sec_res['phrases'] = [dict(pitches=r['pitches'], measured=[None] + [round(v, 2) for v in r['meas'][1:]],
                                   **{n: [round(v, 2) for v in preds[n][j]] for n in preds})
                              for j, r in enumerate(runs)]
        result['sections'][sec] = sec_res
        print(sec, json.dumps({k: v for k, v in sec_res.items() if k not in ('phrases', 'fadeMeasured')}))
    result['all'] = {n: score(m, p, None, ps) for n, (m, p, ps) in allrows.items()}
    result['_about'] = ABOUT
    print('all')
    for n, v in result['all'].items():
        print(f'  {n:11s}', json.dumps(v))
    json.dump(result, open(out_path, 'w'), separators=(',', ':'))
    print('wrote', out_path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('work')
    e.add_argument('--fast', action='store_true', help='batch 2: transitions after a 136 ms attack')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('name')
    p = sub.add_parser('predict')
    p.add_argument('folder')
    p.add_argument('-o', default=os.path.join(HERE, 'sso_superposition.json'))
    p.add_argument('--map', default=MAP)
    p.add_argument('--old-runs', default=os.path.join(HERE, 'sso_transition_levels.json'))
    o = ap.parse_args()
    if o.cmd == 'events':
        events(o.work, o.fast)
    elif o.cmd == 'analyze':
        analyze(o.folder, o.name)
    else:
        predict(o.folder, o.o, o.map, o.old_runs)


if __name__ == '__main__':
    main()
