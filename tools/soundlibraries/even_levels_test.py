#!/usr/bin/env python3
"""Evening out a slur's note levels by CC11, for a listening test (2026-10-12, branch even-levels-test). Written for
this test, not taken from anywhere. Why: in Whence 12-TET the Celli's slurs of four sixteenths (bar 2: 51 52 51 49) swell
(sso_slur_swell.json, sso_transition_levels.json: notes 2-3 ~3 dB over 1 and 4, every note sent at the same level); a
per-transition table can't predict it, so each note is measured in its own context and corrected by CC11 (CC11 is
volume only in SSO). The owner (2026-10-10) found a stepped per-note CC11 "sounds horrible" in an earlier test, so the
test compares how the correction moves between notes. Measurement and audio only: the renderer doesn't read this.

  even_levels_test.py events <dump folder> <work> [--route 0-7] [--bars 4]
      MuseScore's own events (tst_liveequivalence dumpEvents, MS_DUMP_SETTINGS = Recommended's values) of Celli -
      Performance, bars 1..bars, three passes in one kthost event file <work>/ev/N.txt; <work>/meta.json
  even_levels_test.py measure <work> <variant>        <work>/wav/<variant>.wav (kthost) -> <work>/res/<variant>.json
  even_levels_test.py variants <work> [--kinds S R G] [--iterate]
      CC11 curves S, R, G (<work>/ev/<V>.txt, values.json) from N's measurement, or one more iteration from each
      variant's own (<V>2.txt; copy values.json to values1.json first: audio reads both)
  even_levels_test.py audio <work> <out folder> [--use N S R G] [-o sso_even_levels_test.json]
      celli_<V>.mp3 from pass 1 (one gain for all: the loudest peak at -1 dBFS; LEAD s before bar 1, TAIL s after
      bar 4), times.json there, and the summary json

Measure (as sso_slur_swell.json's 'perceivedArrival' / transition_level_scan.py's 'swell'): the mean of the perceived
envelope (tools/playbackverify/loudness.py, ~dB: a gain of g dB moves it by g) over 136.4 ms from a note's note-on plus
the map's Celli - Performance legatoDelay for the interval from the note before (the first note, a smooth attack: the
map's smooth attackOnset at its pitch). Three passes per render (round robins advancing), median per note. Left out of
the slur statistics: the passage's first note (an attack from silence) and its last (its window falls after its
note-off, in the release).

Target: each note at its slur's median level (N's); correction c = target - measured (dB). The variants' CC11 goes on
top of N's own per-note CC11 (102 in bars 1 and 3, 109 in bars 2 and 4: the level rule's per-slur gain), which N sends
as a step at each note-on. A note's heard window lies 200-240 ms after its note-on (legatoDelay), more than the 136 ms
IOI, and CC11 scales the output when it is sent, so the per-note values are not c itself: each shape's values are
solved (least squares, small ridge) so that its CC11 curve in dB, smoothed as the envelope is (one pole, 35 ms),
flattens N's measured envelope (pass median, 5 ms grid) to each slur's median from the heard window of note 2 to the
last note-off; the windows' predicted changes then match c (time_fit). Shapes:
  S  step:  CC11 jumps to note k's value at its note-on
  R  ramp:  CC11 moves linearly (in CC value) from note k-1's value to note k's from note k's note-on to its arrival,
            a message per 5 ms while it moves; arrival = the map's bendGlide for a 136 ms IOI (note-on + 128 ms: where a
            transition's pitch reaches the new note in slurred runs, sso_legato_glide.json), as legatoDelay (200-440 ms)
            exceeds the IOI
  G  glide: CC11 linear in dB from the middle of one note's heard window to the next's, a message per 5 ms
Values above CC11 127 lower the whole curve (no clipping of corrections). A variant whose residual (largest |note -
its slur's median|) stays above 1 dB gets one more iteration (--iterate).

Result (2026-10-12, Windows test VM, kthost + Kontakt 8 + SSO; sso_even_levels_test.json): within-slur range (max -
min) in bars 2 and 4, median over slurs, N 3.7 dB -> S 1.1, R 0.8, G 0.7 (bars 1 and 3, no swell: ~1.0 in all four,
the noise floor: passes differ by ~0.4 dB per note). The measured change was ~1.2-1.3 x the CC11 dB predicted
(20 log10(v/127), smoothed), so round 1 overshot ~25 %; the extra iteration (all three had residual > 1 dB) made no
variant better (S2 1.35, R2 0.76, G2 1.08 median; larger maxima): the audio uses round 1. R and G lower the whole curve
2.2 / 2.5 dB (their peaks need room under 127): measured 2.5 / 3.0 dB quieter than N on average, S 0.7.
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
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import transition_level_scan as tls  # noqa: E402

PATCH = 'Celli - Performance'
LEAD, TAIL = 0.5, 1.5
PASS0, PASS_GAP = 1.0, 13.0
WIN = 0.1364
RAMP = 0.128          # bendGlide 136:-9:128 (Celli - Performance Legato): arrival 128 ms after the note-on
TAU = 0.035
STEP = 0.005
NPASS = 3


def db(v):
    return 20 * math.log10(v / 127.0)


def load(work, name):
    return json.load(open(os.path.join(work, name)))


# --- events ---------------------------------------------------------------------------------------------------------

def read_dump(path, end):
    ev = []
    for line in open(path):
        t, kind, a, b = line.split()
        ev.append((float(t), kind, int(a), int(b)))
    notes, open_ = [], {}
    for t, kind, a, b in ev:
        if kind == 'on' and t < end - 1e-6:
            open_[a] = len(notes)
            notes.append(dict(on=t, p=a, v=b))
        elif kind == 'off' and a in open_:
            notes[open_.pop(a)]['off'] = t
    # passage events: note-ons before the end, their offs, controllers before the end
    keep = []
    for t, kind, a, b in ev:
        if kind == 'on' and t < end - 1e-6:
            keep.append((t, kind, a, b))
        elif kind == 'cc' and t < end - 1e-6:
            keep.append((t, kind, a, b))
    for n in notes:
        keep.append((n['off'], 'off', n['p'], 0))
    return notes, keep


def windows(notes, info):
    """measurement window start of each note (s after its note-on) and the slur index"""
    out = []
    slur = -1
    for k, n in enumerate(notes):
        if k == 0 or n['v'] <= 9 or n['v'] >= 85:     # an attack or a bowed transition starts a slur
            slur += 1
        if k == 0 or n['v'] <= 9:
            d = (tls._interp(info['smooth'], n['p']) or 172.2) / 1000
        else:
            d = tls.legato_delay(info, n['p'] - notes[k - 1]['p']) / 1000
        out.append(dict(n, d=round(d, 4), slur=slur))
    return out


def write_events(path, ev):
    ev = sorted(ev, key=lambda e: (round(e[0], 6), {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
    with open(path, 'w', newline='\r\n') as f:
        for e in ev:
            f.write(f'{e[0]:.5f} {e[1]} {e[2]} {e[3]}\n')


def passes(ev):
    out = []
    for p in range(NPASS):
        off = PASS0 + p * PASS_GAP
        out += [(t + off, k, a, b) for t, k, a, b in ev]
    return out


def events(dump, work, route, bars):
    bt = [float(x) for x in open(os.path.join(dump, 'bars.txt')).read().split()]
    end = bt[bars]
    notes, ev = read_dump(os.path.join(dump, f'{route} museScore.txt'), end)
    info = tls.map_info(tls.MAP)[PATCH]
    notes = windows(notes, info)
    for n in notes:             # N's CC11 at each note-on (the last sent at or before it), dB re 127
        n['cc11'] = [b for t, k, a, b in sorted(ev, key=lambda e: e[0]) if k == 'cc' and a == 11 and t <= n['on'] + 1e-6][-1]
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    write_events(os.path.join(work, 'ev', 'N.txt'), passes(ev))
    meta = dict(patch=PATCH, route=route, bars=bt[:bars + 1], end=end, passes=[PASS0 + p * PASS_GAP for p in range(NPASS)],
                notes=notes, base=ev)
    json.dump(meta, open(os.path.join(work, 'meta.json'), 'w'))
    ccs = sorted(set((a, b) for t, k, a, b in ev if k == 'cc'))
    print(len(notes), 'notes,', len(ev), 'events, end', end, 's; controllers', ccs)


# --- measure --------------------------------------------------------------------------------------------------------

def measure(work, var):
    import soundfile as sf
    from loudness import perceived_envelope
    meta = load(work, 'meta.json')
    x, sr = sf.read(os.path.join(work, 'wav', var + '.wav'), always_2d=True)
    env, first, hop = perceived_envelope(x, sr)
    t_env = (first + np.arange(len(env)) * hop) / 1000.0
    per = []
    for p0 in meta['passes']:
        lv = []
        for n in meta['notes']:
            a = p0 + n['on'] + n['d']
            m = (t_env >= a) & (t_env < a + WIN)
            lv.append(float(np.mean(env[m])))
        per.append(lv)
    med = [st.median(v) for v in zip(*per)]
    tg = grid(meta)
    envm = np.median([np.interp(tg + p0, t_env, env) for p0 in meta['passes']], axis=0)
    os.makedirs(os.path.join(work, 'res'), exist_ok=True)
    json.dump(dict(passes=per, median=med, sr=sr, env=[round(float(x), 3) for x in envm]),
              open(os.path.join(work, 'res', var + '.json'), 'w'))
    s = slur_stats(meta, med)
    print(var, 'pass-to-pass |diff| p50', round(st.median(abs(a - b) for a, b in zip(per[0], per[1])), 2),
          '; slur range median', round(st.median(s['ranges']), 2), 'max', round(max(s['ranges']), 2),
          '; residual', round(s['residual'], 2))
    return med


def slur_stats(meta, lv):
    notes = meta['notes']
    by = {}
    for k, n in enumerate(notes):
        # left out: the passage's first note (a smooth attack from silence, left as it is) and its last (the window
        # falls after its note-off, in the release)
        if 0 < k < len(notes) - 1:
            by.setdefault(n['slur'], []).append(k)
    med = {s: st.median(lv[k] for k in ks) for s, ks in by.items()}
    ranges = [max(lv[k] for k in ks) - min(lv[k] for k in ks) for s, ks in sorted(by.items()) if len(ks) > 1]
    dev = [0.0] + [lv[k] - med[notes[k]['slur']] for k in range(1, len(notes) - 1)] + [0.0]
    return dict(median=med, ranges=ranges, dev=dev, residual=max(abs(x) for x in dev))


# --- curves ---------------------------------------------------------------------------------------------------------

def curve_db(kind, v, notes, t):
    """CC11 dB at times t for per-note values v (R ramps linear in dB here; the file's ramp is linear in CC value)"""
    ons = np.array([n['on'] for n in notes])
    g = np.zeros_like(t)
    if kind == 'S':
        idx = np.searchsorted(ons, t, side='right') - 1
        g = np.where(idx >= 0, np.asarray(v)[np.clip(idx, 0, None)], v[0])
    elif kind == 'R':
        idx = np.searchsorted(ons, t, side='right') - 1
        vv = np.asarray(v)
        prev = np.where(idx >= 1, vv[np.clip(idx - 1, 0, None)], vv[0])
        cur = vv[np.clip(idx, 0, None)]
        frac = np.clip((t - ons[np.clip(idx, 0, None)]) / RAMP, 0, 1)
        g = np.where(idx >= 0, prev + (cur - prev) * frac, vv[0])
    else:
        cen = np.array([n['on'] + n['d'] + WIN / 2 for n in notes])
        g = np.interp(t, cen, v)
    return g


def smooth(g):
    a = 1 - math.exp(-STEP / TAU)
    out = np.empty_like(g)
    s = g[0]
    for i, x in enumerate(g):
        s += a * (x - s)
        out[i] = s
    return out


def grid(meta):
    return np.arange(-0.2, meta['end'] + 1.0, STEP)


def time_fit(kind, meta, env, lv, lam=0.5):
    """per-note CC11 changes (dB) of this shape that flatten the measured envelope (pass median, 5 ms) to each slur's
    median level, from the heard window of note 2 to the last note-off (smoothed as the envelope is; least squares, ridge lam)"""
    notes = meta['notes']
    t = grid(meta)
    s = slur_stats(meta, lv)
    starts = np.array([n['on'] + n['d'] for n in notes])
    k = np.clip(np.searchsorted(starts, t, side='right') - 1, 0, None)
    target = np.array([s['median'].get(notes[i]['slur'], np.nan) for i in k])
    w = ((t >= starts[1]) & (t < notes[-1]['off'])).astype(float)    # not into the last note's release
    r = np.where(w > 0, target - np.asarray(env), 0.0)
    n = len(notes)
    M = np.zeros((len(t), n))
    for j in range(n):
        e = np.zeros(n)
        e[j] = 1.0
        M[:, j] = smooth(curve_db(kind, e, notes, t))
    Wm = M * w[:, None]
    du = np.linalg.solve(Wm.T @ M + lam * np.eye(n), Wm.T @ r)
    fit = float(np.sqrt(np.sum(w * (M @ du - r) ** 2) / np.sum(w)))
    flat = float(np.sqrt(np.sum(w * r ** 2) / np.sum(w)))
    return du, fit, flat


def window_matrix(kind, notes, end):
    t = np.arange(-0.2, end + 1.0, STEP)
    n = len(notes)
    A = np.zeros((n, n))
    for j in range(n):
        e = np.zeros(n)
        e[j] = 1.0
        g = smooth(curve_db(kind, e, notes, t))
        for k, nt in enumerate(notes):
            a = nt['on'] + nt['d']
            m = (t >= a) & (t < a + WIN)
            A[k, j] = g[m].mean()
    return A


def cc_events(kind, v, notes, end):
    """[(t, 'cc', 11, value)] for per-note CC11 values v (dB re 127) and the shift (dB) that kept them within 127"""
    t = np.arange(0.0, end + 0.3, STEP)
    if kind == 'S':
        pts = [(n['on'], v[k]) for k, n in enumerate(notes)]
    g = curve_db('G', v, notes, t) if kind == 'G' else None
    shift = max(0.0, max(v) - db(127.4))

    def val(x_db):
        return int(max(1, min(127, round(127 * 10 ** ((x_db - shift) / 20)))))
    out = []
    if kind == 'S':
        for tt, x in pts:
            out.append((tt, 'cc', 11, val(x)))
    elif kind == 'R':
        ons = [n['on'] for n in notes]
        out.append((0.0, 'cc', 11, val(v[0])))
        last = val(v[0])
        for k in range(1, len(notes)):
            a, b = val(v[k - 1]), val(v[k])
            tt = ons[k]
            while tt <= ons[k] + RAMP + 1e-9:
                y = int(round(a + (b - a) * min(1.0, (tt - ons[k]) / RAMP)))
                if y != last:
                    out.append((round(tt, 5), 'cc', 11, y))
                    last = y
                tt += STEP
            if last != b:
                out.append((round(ons[k] + RAMP, 5), 'cc', 11, b))
                last = b
    else:
        last = None
        for tt, x in zip(t, g):
            y = val(x)
            if y != last:
                out.append((round(float(tt), 5), 'cc', 11, y))
                last = y
    return out, shift


def variants(work, kinds, iterate):
    meta = load(work, 'meta.json')
    notes = meta['notes']
    base = [tuple(e) for e in meta['base'] if not (e[1] == 'cc' and e[2] == 11)]
    w = [0.0] + [1.0] * (len(notes) - 1)
    vals = load(work, 'values.json') if os.path.exists(os.path.join(work, 'values.json')) else {}
    for kind in kinds:
        src = kind if iterate else 'N'
        res = load(os.path.join(work, 'res'), src + '.json')
        lv = res['median']
        s = slur_stats(meta, lv)
        c = [-x for x in s['dev']]
        A = window_matrix(kind, notes, meta['end'])
        nb = np.array([db(n['cc11']) for n in notes])          # N: steps at the note-ons
        du, fit, flat = time_fit(kind, meta, res['env'], lv)
        # the shape as rendered (N's CC11 for the first round; else the variant's values less its shift) plus du
        u = (np.array(vals[kind]['v']) - vals[kind]['shift'] if iterate else nb) + du
        v = [float(x) for x in u]
        dv = u - nb
        pred = A @ du
        ev, shift = cc_events(kind, v, notes, meta['end'])
        name = kind + ('2' if iterate else '')
        write_events(os.path.join(work, 'ev', name + '.txt'), passes(base + ev))
        rounds = vals.get(kind, {}).get('rounds', 0) + 1
        vals[kind] = dict(v=v, shift=shift, reN=[float(x) - shift for x in dv], correction=c, predicted=[float(x) for x in pred], ccCount=len(ev),
                          cc=[[t, x] for t, _, _, x in ev], rounds=rounds, file=name, fitRms=fit, envRms=flat)
        print(name, f'envelope rms off target {flat:.2f} -> fit {fit:.2f} dB;', 'shift', round(shift, 2), 'dB;', len(ev), 'CC11 messages; values',
              ' '.join(f'{x - shift:+.1f}' for x in dv[16:20]), 'dB re N (bar 2, slur 1)')
    json.dump(vals, open(os.path.join(work, 'values.json'), 'w'))


# --- audio ----------------------------------------------------------------------------------------------------------

def audio(work, out, which, pass_no):
    import soundfile as sf
    meta = load(work, 'meta.json')
    p0 = meta['passes'][pass_no - 1]
    a, b = p0 - LEAD, p0 + meta['end'] + TAIL
    segs = {}
    for label, var in which.items():
        x, sr = sf.read(os.path.join(work, 'wav', var + '.wav'), always_2d=True)
        seg = x[int(round(a * sr)):int(round(b * sr))]
        if sr != 44100:
            raise SystemExit(f'{var}: {sr} Hz')
        segs[label] = seg
    peak = max(float(np.max(np.abs(s))) for s in segs.values())
    gain_db = -1.0 - 20 * math.log10(peak)
    g = 10 ** (gain_db / 20)
    os.makedirs(out, exist_ok=True)
    for label, seg in segs.items():
        tmp = os.path.join(work, f'celli_{label}.wav')
        sf.write(tmp, seg * g, 44100, subtype='PCM_16')
        mp3 = os.path.join(out, f'celli_{label}.mp3')
        subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-codec:a', 'libmp3lame', '-b:a', '160k', mp3],
                       check=True)
        os.remove(tmp)
        print(mp3, os.path.getsize(mp3), 'bytes')
    return gain_db


DESCRIBE = {
    'N': 'as MuseScore plays it now (fork dump, Recommended): CC11 127 then the level rule\'s value (102 bars 1 and 3, '
         '109 bars 2 and 4) at every note-on',
    'S': 'step: CC11 jumps to a per-note value at each note-on (64 messages)',
    'R': 'ramp: CC11 moves linearly (in CC value) from the note before\'s value to this note\'s from the note-on to its '
         'arrival (note-on + 128 ms: the map\'s bendGlide for a 136 ms IOI), a message per 5 ms while it moves',
    'G': 'glide: CC11 linear in dB from the middle of one note\'s heard window to the next\'s (window: note-on + '
         'legatoDelay, 136.4 ms), a message per 5 ms while it moves',
}


ABOUT = (
    'A listening test of evening out a slur\'s note levels by CC11 (branch even-levels-test, 2026-10-12): Whence 12-TET '
    'bars 1-4, Celli - Performance alone, as MuseScore plays it (N) and with a per-note CC11 correction moving as a step '
    '(S), a ramp over each transition (R) or a glide between notes (G). Rendered on the Windows test VM: kthost (offline '
    'VST 3 host), Kontakt 8 + SSO, Celli - Performance from MuseScore\'s resaved setup (library defaults), 44.1 kHz, '
    'three passes per render (round robins advancing), no patch volume (MuseScore\'s +6 dB slot volume is a constant '
    'gain). Levels: perceived (tools/playbackverify/loudness.py, ~dB) over 136.4 ms from note-on + the map\'s '
    'legatoDelay (sso_slur_swell.json\'s window), median of the passes; notes 1 (an attack from silence) and 64 (window '
    'after its note-off) left out of the slur statistics. correction: target (the slur\'s median, N) - N\'s level. '
    'variants.<V>.cc11ReN: the CC11 sent at each note (S, R: from its note-on; G: at its window\'s middle), dB re N\'s, '
    'solved so the curve (smoothed 35 ms as the envelope) flattens N\'s measured envelope to the slur medians (5 ms '
    'grid, least squares) - the heard window lags the note-on by 200-240 ms, more than the 136 ms IOI, so a note-on\'s '
    'value mostly corrects the note heard then; shift: dB the whole curve was lowered to stay within 127. <V>2: one more '
    'iteration from that variant\'s own measurement (made things no better: the measured level moved ~1.2-1.3 x the '
    'CC11 dB predicted, plus pass-to-pass noise ~0.4 dB per note). stats: per slur max - min (dB). '
    'tools/soundlibraries/even_levels_test.py has the method. Measurement only: the renderer does not read this file.')


def rnd(v, n=2):
    return [round(float(x), n) for x in v]


def summary(work, chosen, out_json, times_path, gain_db):
    meta = load(work, 'meta.json')
    vals1 = load(work, 'values1.json')
    vals2 = load(work, 'values.json')
    notes = meta['notes']
    res = {v: load(os.path.join(work, 'res'), v + '.json') for v in ('N', 'S', 'R', 'G', 'S2', 'R2', 'G2')}
    levels, stats = {}, {}
    for v, r in res.items():
        sst = slur_stats(meta, r['median'])
        levels[v] = rnd(r['median'], 2)
        stats[v] = dict(slurRanges=rnd(sst['ranges'], 2), rangeMedian=round(st.median(sst['ranges']), 2),
                        rangeMax=round(max(sst['ranges']), 2), residual=round(sst['residual'], 2),
                        passToPassMedianAbsDiff=round(st.median(abs(a - b) for a, b in zip(r['passes'][0], r['passes'][1])), 2),
                        meanLevelReN=round(st.fmean(r['median'][1:-1]) - st.fmean(res['N']['median'][1:-1]), 2))
    corr = {}
    for k in 'SRG':
        for rd, vv in (('1', vals1[k]), ('2', vals2[k])):
            corr[k + ('' if rd == '1' else '2')] = dict(cc11ReN=rnd(vv['reN'], 2), shift=round(vv['shift'], 2),
                                                        messages=vv['ccCount'], predictedWindowChange=rnd(vv['predicted'], 2))
    lead = LEAD
    bars = [round(lead + b, 4) for b in meta['bars']]
    common = dict(
        source='Whence 12-TET bars 1-4, Celli - Performance alone: the fork\'s own events (tst_liveequivalence dumpEvents, '
               'integrate-1012 build, shipped map, Recommended: MS_DUMP_SETTINGS = the preset\'s values), route 0-7',
        notes=[dict(p=n['p'], vel=n['v'], on=round(n['on'], 4), windowMs=round(n['d'] * 1000), slur=n['slur'],
                    cc11=n['cc11']) for n in notes],
        correction=rnd(slur_stats(meta, res['N']['median'])['dev'], 2),
        levels=levels, stats=stats, variants=corr)
    common['correction'] = [-x for x in common['correction']]
    if out_json:
        json.dump(dict(_about=ABOUT, describe=DESCRIBE, audioFrom=chosen, **common), open(out_json, 'w'),
                  separators=(',', ':'))
    times = dict(lead=lead, tail=TAIL, barStarts=bars, end=round(lead + meta['end'], 4), sampleRate=44100,
                 gainDb=round(gain_db, 2), gainNote='one gain for all four files: the loudest peak of the four at -1 dBFS',
                 renderPass=1, audioFrom=chosen, describe=DESCRIBE, **common)
    json.dump(times, open(times_path, 'w'), indent=1)
    return times


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('dump')
    e.add_argument('work')
    e.add_argument('--route', default='0-7')
    e.add_argument('--bars', type=int, default=4)
    m = sub.add_parser('measure')
    m.add_argument('work')
    m.add_argument('variant')
    v = sub.add_parser('variants')
    v.add_argument('work')
    v.add_argument('--kinds', nargs='*', default=['S', 'R', 'G'])
    v.add_argument('--iterate', action='store_true')
    a = sub.add_parser('audio')
    a.add_argument('work')
    a.add_argument('out')
    a.add_argument('--use', nargs=4, default=['N', 'S', 'R', 'G'], help='renders for N S R G')
    a.add_argument('-o', default=os.path.join(HERE, 'sso_even_levels_test.json'))
    o = ap.parse_args()
    if o.cmd == 'audio':
        which = dict(zip('NSRG', o.use))
        g = audio(o.work, o.out, which, 1)
        summary(o.work, which, o.o, os.path.join(o.out, 'times.json'), g)
    elif o.cmd == 'events':
        events(o.dump, o.work, o.route, o.bars)
    elif o.cmd == 'measure':
        measure(o.work, o.variant)
    elif o.cmd == 'variants':
        variants(o.work, o.kinds, o.iterate)


if __name__ == '__main__':
    main()
