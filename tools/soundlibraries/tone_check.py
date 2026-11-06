#!/usr/bin/env python3
"""tone_check.py: the tone and noise of every note of a render, against the patch's own median: faulty round robins
(a narrow tone of their own, a dark / bright / noisy / quiet recording), clicks and pops at note-on / note-off, and the
map's `avoid` proposed from them. docs/TOOLS.md › 4 and docs/MEASURING.md › Tone are the guide; this header is the
reference.

    tone_check.py <wav> <notes> [--file NAME] [--slot N] [--patch NAME] [-o out.json] [thresholds]
    tone_check.py <measure_patch WORK> [-o out.json] [thresholds]      a measure_patch.py run with --what tone
    tone_check.py avoid <out.json>... [-o avoid.json]                  the avoid entries of several results, merged

<notes> is one of
  - a kthost event file (`seconds on|off key velocity [channel]`, the format of dumpEvents' `<route> museScore.txt`
    and of measure_patch.py's ev/*.txt): every note-on paired with its note-off (by key and channel);
  - a measure_patch.py plan.json (with --file fNNN, the render's file): the cells' notes, with their cents;
  - a --verify-playback events.tsv (`time tick slot type a b switch`; --slot N keeps one slot).

PER NOTE (the window: --window ms after the note-on, default 50-350 as sso_bad_samples.json's screen, cut at the
note-off + 100 ms and at the next note-on (a chord's notes share it); at least 80 ms):
  level        the perceived peak in the first 400 ms (tools/playbackverify/loudness.py, loudness units ~ dB; measure_patch's
               `peak`)
  rms          the window's RMS, dBFS
  centroid     the spectral centroid as heard (Hz; compared in cents): the harmonics up to 8 kHz (a clearly harmonic
               note, hnr >= --harmonic-hnr-db, 10; by = "harmonics") or else the 1/12-octave bands 50 Hz-8 kHz, each
               weighted by its power^0.3 (specific loudness), averaged on log2(f) (a power-weighted centroid follows the
               fundamental's level from one round robin to the next)
  tilt         the slope of the harmonics' (else the bands') levels 200 Hz-8 kHz against log2(f), dB per octave
  hnr          the note against the noise between its harmonics (dB): its whole power over the median of the bins between
               each harmonic of its pitch (+ cents) and the next (0.3-0.7 of the way), harmonics up to 5 kHz (a darker
               recording keeps it; noise lowers it)
  floor        the noise floor before the note: the median 5 ms power over the 150 ms before the note-on, dBFS, only when
               nothing sounded for --floor-gap s before it (default 0.5; else none); floorSlope its slope over the 0.5 s
               before (dB/s)
  clickOn      a linear-prediction model (order 24) fitted on the note's sustain (on + 50 .. 150 ms; before the note-off:
  clickOff     the 80 ms before it), its residual over [on - 4, on + 20 ms] / [off - 4, off + 30 ms]: the largest 0.25 ms
               RMS over the sustain's median, dB (the sustain's counted at least 50 dB under the sustain). A sustained
               tone, a smooth attack or release are predicted; a click (an impulse) or a cut waveform (a step) isn't. Real
               SSO string attacks (kthost, isolated, v64 / 122): clickOn -17 .. 0, clickOff 1 .. 7 dB; a waveform cut
               at its crest +24 dB
  harm, bands  the harmonics' peak levels and the 1/12-octave band levels, with the same in the 250 ms before the
               note-on (kept for the comparison, not in the output)

GROUPS. Notes of one sound: the same key, cents and velocity, joined or not (a note-on while a note of its channel still
sounds: a legato transition, another sample than an attack; and from which key), and length to 10 ms (in a WORK folder
the same cell and note). Each note against its group's median of the others:
  narrow       a note whose harmonics (hnr >= --harmonic-hnr-db, 10) or else whose bands stand out: each one's level
               minus the group's median, less the larger median of the 3 beside it on either side (a peak, not a step: a
               darker or louder recording isn't narrow), only where the note owns it (--narrow-own-db, 6, over the same
               harmonic / band in the 250 ms before the note-on: not a reverb tail of the note before); the largest.
               sso_bad_samples.json's faulty round robin: +9 .. +11 dB at harmonic 9 (Celli - Performance C3, 1184 Hz),
               the clean attacks 2 .. 4 dB. Flagged ("narrow tone") at --narrow-db (6; a band --narrow-band-db, 10) and
               --narrow-margin (4) dB over the group's median narrow
  dark/bright  centroid off by --centroid-cents (150) or tilt by --tilt-db (2 dB/oct), and 3 robust SD (1.4826 MAD) of
               the group
  noisy        hnr --hnr-db (6) under the group
  quiet/loud   level off by --level-db (3)
  noisy floor  floor --floor-db (10) over the group, the floor flat (|floorSlope| <= --floor-flat, 5 dB/s: a decaying
               tail of the note before, a hall's reverb, isn't noise)
  click / pop  clickOn / clickOff >= --click-db (15) and --click-margin (6) dB over the group's median; a group whose
               median itself is >= --click-all-db (25) is reported as "clicks on every note"
A flagged note's place in its run (the group's notes in time order; in a WORK folder its place in the cell's repeats)
is reported: two or more flags of one kind give the period (the gcd of their distances), "round robin 5 of 6 (attacks
5 and 11 of 12)" as sso_bad_samples.json says it.
KEYS. Each group's medians against its neighbours': the median of the groups of the same velocity, cell and joining
within +-2 semitones (at least two; a median, so that one bad neighbour doesn't move it); a key off by --key-level-db (4), --key-centroid-cents (300), --key-tilt-db
(3) or --key-hnr-db (8) is a "key outlier" (one recording for every round robin, or a sample set's seam).

AVOID. Per key, the velocities whose attack group (not joined: `avoid` steers attacks) has a tone flag (narrow, dark /
bright, noisy, quiet / loud: not a click) make bands of neighbouring measured velocities; each band plays at the nearest
clean velocity measured (below first), the map's `<Articulation avoid="keys:low-high:velocity">` (sso_bad_samples.json's
"avoid" schema: {"keys": [lo, hi], "band": [lo, hi], "velocity": v}); neighbouring keys with the same band are merged. A
proposal: a person checks it (the accented attack's velocities may be another technique: sso_bad_samples.json kept
117-127 out) before it goes into sso_bad_samples.json, which gen_spitfire_sso.py reads.

To find a faulty round robin the render has to play it: the same note repeated closer than the patch's round-robin
reset (SSO's Performance patches: 1.5 s; sso_bad_samples.json: 12 attacks, 0.5 s on, 0.4 s apart), e.g. measure_patch.py
plan --what level,tone --repeats 12 --lengths 500 --repeat-gap 0.4.

Output (-o; default <wav>.tone.json or WORK/tone.json): {"_about", "method", "patch", "notes": [...], "groups": [...],
"keys": [...], "flags": [...], "avoid": {patch: [...]}}; a text report on stdout.
Tests: tools/soundlibraries/test_tone_check.py (synthetic: a click, a pop, a dark, a noisy and a narrow-tone round robin,
the avoid proposal; MS_TONE_REAL=<wav>|<notes> a real render that must flag nothing).
"""
import argparse
import json
import math
import os
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))

VERSION = 1
DEFAULTS = {'window': (50.0, 350.0), 'narrow_db': 6.0, 'narrow_margin': 4.0, 'narrow_own_db': 6.0,
            'narrow_band_db': 10.0, 'harmonic_hnr_db': 10.0, 'centroid_cents': 150.0, 'tilt_db': 2.0, 'hnr_db': 6.0,
            'level_db': 3.0, 'floor_db': 10.0, 'floor_gap': 0.5, 'floor_flat': 5.0, 'click_db': 15.0,
            'click_margin': 6.0, 'click_all_db': 25.0, 'key_level_db': 4.0, 'key_centroid_cents': 300.0,
            'key_tilt_db': 3.0, 'key_hnr_db': 8.0, 'robust_k': 3.0}
TONE_FLAGS = ('narrow tone', 'dark', 'bright', 'noisy', 'quiet', 'loud')
BAND_LO, BAND_HI, TILT_LO = 50.0, 8000.0, 200.0
RESIDUAL_FLOOR_DB = -50.0
CLICK_BLOCK = 11                # (0.25 ms at 44.1 kHz: the residual's RMS in blocks this long)


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12.0)


# -------------------------------------------------------------------------------------------------------------- notes

def read_events(path, slot=None):
    """kthost event file or verify events.tsv -> [{on, off, key, cents, velocity, channel}]"""
    held, out = {}, []
    for line in open(path, encoding='utf-8', errors='replace'):
        c = line.split('\t') if '\t' in line else line.split()
        if not c or c[0] in ('time', '#') or c[0].startswith('#'):
            continue
        try:
            if '\t' in line:                                       # events.tsv: time tick slot type a b switch
                t, ch, kind, a, b = float(c[0]), int(c[2]), c[3], int(c[4]), int(c[5])
                if slot is not None and ch != slot:
                    continue
            else:                                                  # kthost: seconds kind a b [channel]
                t, kind = float(c[0]), c[1]
                if kind not in ('on', 'off'):
                    continue
                a, b = int(c[2]), int(c[3])
                ch = int(c[4]) if len(c) > 4 else 0
        except (ValueError, IndexError):
            continue
        if kind == 'on' and b > 0:
            if (ch, a) in held:                                    # (a re-struck key: the old one ends here)
                out[held[(ch, a)]]['off'] = t
            held[(ch, a)] = len(out)
            out.append({'on': t, 'off': None, 'key': a, 'cents': 0.0, 'velocity': b, 'channel': ch})
        elif kind in ('off', 'on'):
            i = held.pop((ch, a), None)
            if i is not None:
                out[i]['off'] = t
    for n in out:
        if n['off'] is None:
            n['off'] = n['on'] + 0.5
    out.sort(key=lambda n: (n['on'], n['key']))
    mark_joined(out)
    return out


def mark_joined(notes):
    """joined: a note-on while another note of its channel still sounds (a legato transition on a legato patch, a
    different sample from an attack after silence), from: that note's key (a transition's sample depends on its
    interval); grouped apart"""
    for i, n in enumerate(notes):
        held = [m for m in notes[max(0, i - 16):i] if m.get('channel', 0) == n.get('channel', 0) and
                m['on'] < n['on'] - 0.001 and m['off'] > n['on'] + 0.001]
        n['joined'] = bool(held)
        if held:
            n['from'] = held[-1]['key']


def read_plan(path, name):
    """measure_patch.py plan.json -> the file's notes, with cell, note name, cents and place in the cell's repeats"""
    meta = json.load(open(path))
    out = []
    for c in meta['cells']:
        if c['file'] != name:
            continue
        reps = [n for n in c['notes'] if n[0] == 'N' or n[0].startswith('R')]
        for n in c['notes']:
            nm, key, vel, on, off = n[:5]
            cents = float(n[6]) if len(n) > 6 else 0.0
            d = {'on': on, 'off': off, 'key': key, 'cents': cents, 'velocity': vel, 'channel': 0, 'note': nm,
                 'cell': c['label'], 'cellId': c['id'], 'take': c['take']}
            if n in reps:
                d['rr'], d['runLen'] = reps.index(n) + 1, len(reps)
            out.append(d)
    out.sort(key=lambda n: (n['on'], n['key']))
    mark_joined(out)
    return out


# ----------------------------------------------------------------------------------------------------------- features

class Render:
    """one render's audio with the whole-file envelopes the notes share"""

    def __init__(self, x, sr):
        import numpy as np
        from loudness import perceived_envelope, power_envelope
        self.x = np.asarray(x, dtype=np.float64)
        self.sr = sr
        self.mono = self.x.mean(axis=1) if self.x.ndim == 2 else self.x
        self.env, self.first, self.hop = perceived_envelope(self.x, sr)
        self.power = power_envelope(self.x, sr)
        self.pwin = int(sr * 0.005) / sr

    def perceived_peak(self, t0, t1):
        a = max(0, int(round((t0 * 1000 - self.first) / self.hop)))
        b = min(len(self.env), int(round((t1 * 1000 - self.first) / self.hop)))
        return float(self.env[a:b].max()) if b > a else None

    def seg(self, t0, t1):
        return self.mono[max(0, int(t0 * self.sr)):max(0, int(t1 * self.sr))]


def band_centres():
    out, f = [], BAND_LO
    while f <= BAND_HI * 1.0001:
        out.append(f)
        f *= 2 ** (1 / 12)
    return out


def spectrum(seg, sr, nfft, norm_len=None):
    """the power spectrum (Hann) and its 1/12-octave band powers (linear); norm_len: scaled as a segment of that length
    (a shorter one compared band by band with the note's)"""
    import numpy as np
    win = np.hanning(len(seg))
    P = np.abs(np.fft.rfft(seg * win, nfft)) ** 2 + 1e-24
    if norm_len:
        P = P * (norm_len / len(seg))
    cs = np.cumsum(P)
    bands = []
    for fc in band_centres():
        lo, hi = fc * 2 ** (-1 / 24), fc * 2 ** (1 / 24)
        i0, i1 = int(math.ceil(lo * nfft / sr)), int(math.ceil(hi * nfft / sr))
        if i1 >= len(P):
            break
        bands.append(max(cs[i1 - 1] - cs[i0 - 1], 1e-24))
    return P, np.array(bands)


def harmonics(P, sr, nfft, f0, top=8000.0):
    """each harmonic's peak bin (dB) within +-50 cents (at least +-2 bins), up to top Hz / harmonic 40; and the median
    bin between it and the next (0.3-0.7 of the way)"""
    import numpy as np
    Pd = 10 * np.log10(P)
    bin_hz = sr / nfft
    peaks, gaps = [], []
    k = 1
    while k * f0 <= min(top, 0.45 * sr) and k <= 40:
        fk = k * f0
        a = min(int(fk * 2 ** (-50 / 1200) / bin_hz), int(fk / bin_hz) - 2)
        b = max(int(math.ceil(fk * 2 ** (50 / 1200) / bin_hz)) + 1, int(fk / bin_hz) + 3)
        peaks.append(float(Pd[max(0, a):b].max()))
        g0, g1 = int((fk + 0.3 * f0) / bin_hz), int((fk + 0.7 * f0) / bin_hz)
        gaps.append(float(np.median(Pd[g0:g1])) if g1 > g0 + 1 else None)
        k += 1
    return peaks, gaps


def lpc_click(seg_fit, seg_ev, order=24):
    """a click / a cut waveform: the largest 0.25 ms RMS of the event segment's linear-prediction residual (an AR model
    fitted on the note's own sustain) over the sustain's median, dB; None when a segment is too short. A sustained tone
    and a smooth fade are predicted; an impulse or a step isn't"""
    import numpy as np
    from numpy.lib.stride_tricks import sliding_window_view as sw
    if len(seg_fit) < 20 * order or len(seg_ev) < order + 40 or not np.any(seg_fit):
        return None
    X = sw(seg_fit[:-1], order)[:, ::-1]
    y = seg_fit[order:]
    c = np.linalg.lstsq(X, y, rcond=None)[0]

    def res(s):
        return s[order:] - sw(s[:-1], order)[:, ::-1] @ c

    def blocks(e, n=CLICK_BLOCK):
        m = len(e) // n
        return np.sqrt((e[:m * n].reshape(m, n) ** 2).mean(axis=1)) if m else np.zeros(0)
    base = blocks(res(seg_fit))
    ev = blocks(res(seg_ev))
    if not len(base) or not len(ev):
        return None
    # (the sustain's residual counted at least RESIDUAL_FLOOR_DB under the sustain: a sample's own noise, a
    # synthetic tone's rounding, aren't the reference)
    ref = max(float(np.median(base)), math.sqrt(float(np.mean(seg_fit ** 2))) * 10 ** (RESIDUAL_FLOOR_DB / 20), 1e-12)
    return round(20 * math.log10(max(float(ev.max()), 1e-12) / ref), 2)


def note_features(r, n, prev_off, opt, next_on=None):
    """one note's measures (module header)"""
    import numpy as np
    sr = r.sr
    on, off = n['on'], n['off']
    w0 = on + opt['window'][0] / 1000
    w1 = min(on + opt['window'][1] / 1000, off + 0.1, next_on if next_on else 1e9)
    if w1 - w0 < 0.08:
        w1 = w0 + 0.08
    seg = r.seg(w0, w1)
    out = {}
    lv = r.perceived_peak(on, on + 0.4)
    out['level'] = None if lv is None else round(lv, 2)
    if len(seg) < int(0.04 * sr) or not np.any(seg):
        return out
    out['rms'] = round(10 * math.log10(max(float(np.mean(seg * seg)), 1e-20)), 2)
    nfft = 1 << max(14, int(math.ceil(math.log2(len(seg)))))
    P, bp = spectrum(seg, sr, nfft)
    fcs = np.array(band_centres()[:len(bp)])
    w = bp ** 0.3                                   # (specific loudness: the centroid as heard, not the fundamental's)
    out['centroid'] = round(float(2 ** ((w * np.log2(fcs)).sum() / w.sum())), 1)
    bands = 10 * np.log10(bp)
    t = fcs >= TILT_LO
    if t.sum() > 3:
        out['tilt'] = round(float(np.polyfit(np.log2(fcs[t]), bands[t], 1)[0]), 2)
    out['bands'] = [round(float(b), 2) for b in bands]
    pre = r.seg(on - 0.25, on - 0.005)
    if len(pre) > int(0.05 * sr):
        Pp, bpp = spectrum(pre, sr, nfft, len(seg))
        out['preBands'] = [round(float(b), 2) for b in 10 * np.log10(bpp)]
    if n.get('key') is not None:
        f0 = hz(n['key'] + n.get('cents', 0.0) / 100)
        f0 = f0 if f0 >= 20 else None
    else:
        f0 = None
    if f0:
        out['f0'] = round(f0, 2)
        peaks, gaps = harmonics(P, sr, nfft, f0)
        out['harm'] = [round(p, 2) for p in peaks]
        gl = [gaps[i] for i in range(len(peaks)) if gaps[i] is not None and (i + 1) * f0 <= 5000]
        if gl:                          # (the note's whole power over the bins between its harmonics: a darker recording
            out['hnr'] = round(10 * math.log10(float(P.sum())) - float(np.median(gl)), 2)    # keeps it, noise doesn't)
        if len(pre) > int(0.05 * sr):
            out['preHarm'] = [round(p, 2) for p in harmonics(Pp, sr, nfft, f0)[0]]
        if out.get('hnr', 0) >= opt['harmonic_hnr_db']:
            # a clearly harmonic note: centroid and tilt from its harmonics (1/12-octave bands between sparse low
            # harmonics hold only leakage, which any noise fills)
            hf = f0 * np.arange(1, len(peaks) + 1)
            hp = np.array(peaks)
            w = (10 ** (hp / 10)) ** 0.3
            out['centroid'] = round(float(2 ** ((w * np.log2(hf)).sum() / w.sum())), 1)
            t = (hf >= TILT_LO) & (hf <= BAND_HI)
            if t.sum() > 3:
                out['tilt'] = round(float(np.polyfit(np.log2(hf[t]), hp[t], 1)[0]), 2)
            out['by'] = 'harmonics'
    if prev_off is None or on - prev_off >= opt['floor_gap']:
        a, b = int(max(0, on - 0.15) / r.pwin), int((on - 0.005) / r.pwin)
        if b > a:
            out['floor'] = round(float(np.median(r.power[a:b])), 2)
        a2 = int(max(0, on - 0.5) / r.pwin)
        if b - a2 > 10:                 # the floor's slope: a decaying tail (reverb, a release) isn't a noise floor
            seg_p = r.power[a2:b]
            out['floorSlope'] = round(float(np.polyfit(np.arange(len(seg_p)) * r.pwin, seg_p, 1)[0]), 1)
    # clicks: the linear-prediction residual at the note-on / note-off over the note's own sustain
    if off - on >= 0.1:
        c = lpc_click(r.seg(on + 0.05, min(on + 0.15, off - 0.005)), r.seg(on - 0.004, on + 0.02))
        if c is not None:
            out['clickOn'] = c
        c = lpc_click(r.seg(max(on + 0.03, off - 0.08), off - 0.005), r.seg(off - 0.004, off + 0.03))
        if c is not None:
            out['clickOff'] = c
    return out


def measure(x, sr, notes, opt=None):
    """every note's features (notes from read_events / read_plan, times in s): the notes with their measures added"""
    opt = dict(DEFAULTS, **(opt or {}))
    r = Render(x, sr)
    rows = []
    last_off = None
    notes = sorted(notes, key=lambda n: n['on'])
    for j, n in enumerate(notes):
        d = dict(n)
        nxt = next((m['on'] for m in notes[j + 1:] if m['on'] > n['on'] + 0.01), None)
        d.update(note_features(r, n, last_off, opt, nxt))     # (last_off after the note-on: no floor)
        rows.append(d)
        last_off = n['off'] if last_off is None else max(last_off, n['off'])
    return rows


# --------------------------------------------------------------------------------------------------------- assessment

def _median(v):
    v = [x for x in v if x is not None]
    return st.median(v) if v else None


def _mad(v, med):
    v = [x for x in v if x is not None]
    return 1.4826 * st.median([abs(x - med) for x in v]) if v else 0.0


def group_key(n):
    if 'cell' in n:
        nm = n.get('note', '')
        nm = 'N' if nm == 'N' or (nm.startswith('R') and nm[1:].isdigit()) else nm      # (the repeats are one sound)
        return (n['cell'], nm, n['key'], round(n.get('cents', 0.0)), n['velocity'])
    return (n.get('channel', 0), n['key'], round(n.get('cents', 0.0)), n['velocity'], bool(n.get('joined')),
            n.get('from'), round((n['off'] - n['on']) * 100))


def _cents(a, b):
    return 1200 * math.log2(a / b) if a and b and a > 0 and b > 0 else None


def assess(rows, opt=None, patch=None):
    """groups, per-note deviations and flags, key outliers, the avoid proposal"""
    import numpy as np
    opt = dict(DEFAULTS, **(opt or {}))
    k = opt['robust_k']
    groups = {}
    for i, n in enumerate(rows):
        groups.setdefault(group_key(n), []).append(i)
    gout, flags = [], []
    for gk, idx in groups.items():
        idx.sort(key=lambda i: rows[i]['on'])
        for pos, i in enumerate(idx):                  # place in the run (event files: the group in time order)
            if 'rr' not in rows[i]:
                rows[i]['rr'], rows[i]['runLen'] = pos + 1, len(idx)
        g = {'key': rows[idx[0]]['key'], 'cents': rows[idx[0]].get('cents', 0.0), 'velocity': rows[idx[0]]['velocity'],
             'joined': bool(rows[idx[0]].get('joined')), 'from': rows[idx[0]].get('from'), 'n': len(idx)}
        if 'cell' in rows[idx[0]]:
            g['cell'], g['note'] = rows[idx[0]]['cell'], gk[1]
        for name in ('level', 'rms', 'centroid', 'tilt', 'hnr', 'floor', 'clickOn', 'clickOff'):
            m = _median([rows[i].get(name) for i in idx])
            if m is not None:
                g[name] = round(m, 2)
        gflags = []
        for j, i in enumerate(idx):
            n = rows[i]
            others = [x for x in idx if x != i]
            dev, why = {}, []

            def check(name, value, ref, floor, lo_name, hi_name, scale=None):
                if value is None or ref is None:
                    return
                d = value - ref
                dev[name] = round(d, 2)
                s = scale if scale is not None else 0.0
                if abs(d) >= max(floor, k * s):
                    why.append(hi_name if d > 0 else lo_name)
            if len(others) >= 2:
                for name, floor, lo, hi in (('level', opt['level_db'], 'quiet', 'loud'),
                                            ('tilt', opt['tilt_db'], 'dark', 'bright'),
                                            ('hnr', opt['hnr_db'], 'noisy', None),
                                            ('floor', opt['floor_db'], None, 'noisy floor')):
                    ov = [rows[x].get(name) for x in others]
                    ref = _median(ov)
                    if ref is None:
                        continue
                    if name == 'floor' and abs(n.get('floorSlope') or 0.0) > opt['floor_flat']:
                        continue                     # (still decaying: the note before's tail)
                    check(name, n.get(name), ref, floor, lo, hi, _mad(ov, ref))
                ov = [rows[x].get('centroid') for x in others]
                ref = _median(ov)
                if ref and n.get('centroid'):
                    cc = [_cents(c, ref) for c in ov if c]
                    check('centroidCents', _cents(n['centroid'], ref), 0.0, opt['centroid_cents'], 'dark', 'bright',
                          _mad(cc, _median(cc)))
                why = [w for w in why if w]
            # narrow: harmonics (a clearly harmonic note) or bands against the others' median, over the deviations
            # beside them (a peak, not a step: a darker recording isn't a narrow tone)
            hv = n.get('hnr') is not None and n['hnr'] >= opt['harmonic_hnr_db'] and n.get('harm')
            nm, own = ('harm', 'preHarm') if hv else ('bands', 'preBands')
            vals = [rows[x].get(nm) for x in others if rows[x].get(nm)]
            if len(vals) >= 2 and n.get(nm):
                L = min([len(v) for v in vals] + [len(n[nm])])
                d = np.array(n[nm][:L]) - np.median(np.array([v[:L] for v in vals]), axis=0)
                ex = np.full(L, -99.0)
                for q in range(L):
                    left, right = d[max(0, q - 3):q], d[q + 1:q + 4]
                    sides = [float(np.median(x)) for x in (left, right) if len(x)]
                    if sides:
                        ex[q] = d[q] - max(sides)
                pre = n.get(own)
                if pre and len(pre) >= L:      # (one the note doesn't own: what rang before it, a reverb tail)
                    ex = np.where(np.array(n[nm][:L]) - np.array(pre[:L]) >= opt['narrow_own_db'], ex, -99.0)
                q = int(np.argmax(ex))
                n['narrow'] = round(float(ex[q]), 2)
                n['narrowHz'] = round((q + 1) * n['f0'] if hv else band_centres()[q], 1)
                n['narrowBy'] = 'harmonic %d' % (q + 1) if hv else '1/12-octave band'
            dev = {kk: v for kk, v in dev.items()}
            n['dev'] = dev
            n['why'] = list(dict.fromkeys(why))
        # narrow flags need the whole group's narrow values
        nv = [rows[i].get('narrow') for i in idx]
        if any(v is not None for v in nv):
            for i in idx:
                v = rows[i].get('narrow')
                if v is None:
                    continue
                om = _median([rows[x].get('narrow') for x in idx if x != i])
                lim = opt['narrow_db'] if rows[i].get('narrowBy', '').startswith('harmonic') else opt['narrow_band_db']
                if v >= lim and (om is None or v >= om + opt['narrow_margin']):
                    rows[i]['why'].append('narrow tone')
        for name, label in (('clickOn', 'click at note-on'), ('clickOff', 'pop at note-off')):
            vals = [rows[i].get(name) for i in idx]
            med = _median(vals)
            if med is None:
                continue
            if med >= opt['click_all_db'] and len(idx) >= 2:
                gflags.append('%s on every note (median %.1f dB)' % (label, med))
            for i in idx:
                v = rows[i].get(name)
                om = _median([rows[x].get(name) for x in idx if x != i])
                if v is not None and v >= opt['click_db'] and (om is None or v >= om + opt['click_margin']):
                    rows[i].setdefault('why', []).append(label)
        # recurrence: a kind flagged twice or more in one run gives the period
        byk = {}
        for i in idx:
            for w in rows[i].get('why', []):
                byk.setdefault(w, []).append(i)
        for w, ii in byk.items():
            runs = {}
            for i in ii:
                runs.setdefault((rows[i].get('cellId', 0) if 'cellId' in rows[i] else 0), []).append(rows[i]['rr'])
            desc = []
            for run, pos in runs.items():
                pos = sorted(pos)
                L = rows[ii[0]]['runLen']
                per = period(pos, L)
                if per:
                    rr = sorted({(p - 1) % per + 1 for p in pos})
                    desc.append('round robin%s %s of %d (attacks %s of %d)' % (
                        's' if len(rr) > 1 else '', ', '.join(map(str, rr)), per,
                        ' and '.join(map(str, pos)) if len(pos) == 2 else ', '.join(map(str, pos)), L))
                    continue
                desc.append('attack%s %s of %d' % ('s' if len(pos) > 1 else '', ', '.join(map(str, pos)), L))
            gflags.append('%s: %s%s' % (w, '; '.join(sorted(set(desc))),
                                        (' at %.0f Hz' % st.median([rows[i]['narrowHz'] for i in ii]))
                                        if w == 'narrow tone' else ''))
        g['flags'] = gflags
        g['flagged'] = sorted({i for i in idx if rows[i].get('why')})
        gout.append(g)
        for f in gflags:
            flags.append('%s key %d%s v%d%s: %s' % (patch or '', g['key'], (' %+.0fc' % g['cents']) if g['cents'] else '',
                                                    g['velocity'], (' joined from %d' % g['from']) if g['joined'] else '',
                                                    f))
    keys = key_outliers(gout, opt)
    for kk in keys:
        flags.append('%s key %d v%d: key outlier (%s)' % (patch or '', kk['key'], kk['velocity'], ', '.join(kk['why'])))
    return {'groups': gout, 'keys': keys, 'flags': flags, 'avoid': propose_avoid(gout, rows, patch)}


def period(pos, L):
    """the smallest round-robin cycle P that explains the flagged places pos (1-based, of a run of L): every place
    with the same place in the cycle flagged and no other, each flagged round robin at least twice; else None"""
    for P in range(2, L // 2 + 1):
        res = {(p - 1) % P for p in pos}
        if all(sum(1 for p in pos if (p - 1) % P == x) >= 2 for x in res) and \
                [q for q in range(1, L + 1) if (q - 1) % P in res] == pos:
            return P
    return None


def key_outliers(groups, opt):
    """each group's medians against its neighbours' (same velocity, cell and note, within +-2 semitones, at least two):
    the median, so that one bad neighbour doesn't move it"""
    out = []
    for g in groups:
        x0 = g['key'] + g['cents'] / 100
        nb = [h for h in groups if h is not g and h['velocity'] == g['velocity'] and h.get('cell') == g.get('cell')
              and h['joined'] == g['joined']
              and h.get('note') == g.get('note') and 0 < abs(h['key'] + h['cents'] / 100 - x0) <= 2]
        if len(nb) < 2:
            continue
        why = {}
        for name, floor in (('level', opt['key_level_db']), ('centroid', opt['key_centroid_cents']),
                            ('tilt', opt['key_tilt_db']), ('hnr', opt['key_hnr_db'])):
            ys = [h.get(name) for h in nb if h.get(name) is not None]
            if g.get(name) is None or len(ys) < 2:
                continue
            ref = st.median(ys)
            d = _cents(g[name], ref) if name == 'centroid' else g[name] - ref
            if d is not None and abs(d) >= floor:
                why[name] = round(d, 1)
        if why:
            out.append({'key': g['key'], 'cents': g['cents'], 'velocity': g['velocity'], 'cell': g.get('cell'),
                        'why': ['%s %+g%s' % (k, v, ' cents' if k == 'centroid' else ' dB/oct' if k == 'tilt'
                                              else ' dB') for k, v in why.items()], 'dev': why})
    return out


def propose_avoid(groups, rows, patch):
    """per key: neighbouring measured velocities with a tone flag -> a band, played at the nearest clean velocity"""
    bykey = {}
    for g in groups:
        if g['joined']:
            continue                    # (avoid steers attacks; a legato transition is another sample)
        bad = any(w in TONE_FLAGS for i in g['flagged'] for w in rows[i].get('why', []))
        bykey.setdefault(g['key'], {})
        bykey[g['key']][g['velocity']] = bykey[g['key']].get(g['velocity'], False) or bad
    ents = []
    for key in sorted(bykey):
        vs = sorted(bykey[key])
        i = 0
        while i < len(vs):
            if not bykey[key][vs[i]]:
                i += 1
                continue
            j = i
            while j + 1 < len(vs) and bykey[key][vs[j + 1]]:
                j += 1
            below = [v for v in vs[:i] if not bykey[key][v]]
            above = [v for v in vs[j + 1:] if not bykey[key][v]]
            clean = below[-1] if below else (above[0] if above else None)
            if clean is not None:
                ents.append({'keys': [key, key], 'band': [vs[i], vs[j]], 'velocity': clean})
            i = j + 1
    merged = []
    for e in ents:
        m = next((x for x in merged if x['band'] == e['band'] and x['velocity'] == e['velocity'] and
                  x['keys'][1] + 1 == e['keys'][0]), None)
        if m:
            m['keys'][1] = e['keys'][1]
        else:
            merged.append(e)
    return {patch or '': merged} if merged else {}


# ---------------------------------------------------------------------------------------------------------- front end

def strip(rows):
    return [{k: v for k, v in n.items() if k not in ('bands', 'preBands', 'harm', 'preHarm')} for n in rows]


def result(rows, opt, patch, source):
    a = assess(rows, opt, patch)
    return {'_about': ('tone_check.py (tools/soundlibraries; docs/TOOLS.md › 4): per note the perceived peak, spectral '
                       'centroid, tilt, harmonics-to-noise, the floor before it and clicks at note-on / off, each against '
                       'its group (same key, cents, velocity) and each group against its neighbouring keys; narrow = the '
                       "largest 1/12-octave band over the group's median (sso_bad_samples.json's screen). avoid: a "
                       'proposal, checked by a person before it goes into sso_bad_samples.json.'),
            'version': VERSION, 'method': {k: (list(v) if isinstance(v, tuple) else v) for k, v in
                                           dict(DEFAULTS, **(opt or {})).items()},
            'source': source, 'patch': patch, 'notes': strip(rows), 'groups': a['groups'], 'keys': a['keys'],
            'flags': a['flags'], 'avoid': a['avoid']}


def report_text(res):
    lines = ['%s: %d notes, %d groups, %d flags' % (res.get('patch') or res.get('source'), len(res['notes']),
                                                   len(res['groups']), len(res['flags']))]
    lines += ['  ' + f for f in res['flags']]
    for i, n in enumerate(res['notes']):
        if n.get('why'):
            lines.append('    note %d at %.3f s key %d v%d (attack %d of %d): %s  %s' % (
                i, n['on'], n['key'], n['velocity'], n['rr'], n['runLen'], ', '.join(n['why']),
                ' '.join('%s %+g' % kv for kv in sorted(n.get('dev', {}).items())) +
                (' narrow %+.1f dB at %.0f Hz' % (n['narrow'], n['narrowHz']) if n.get('narrow') is not None else '')))
    if res['avoid']:
        lines.append('  avoid (proposal): ' + json.dumps(res['avoid']))
    return '\n'.join(lines)


def opts_from(a):
    o = {k: getattr(a, k) for k in DEFAULTS if k != 'window' and getattr(a, k, None) is not None}
    if getattr(a, 'window', None):
        lo, hi = a.window.split(':')
        o['window'] = (float(lo), float(hi))
    return o


def check_wav(wav, notes_path, file=None, slot=None, patch=None, opt=None):
    import soundfile as sf
    x, sr = sf.read(wav, always_2d=True)
    if notes_path.endswith('.json'):
        if not file:
            file = os.path.splitext(os.path.basename(wav))[0]
        notes = read_plan(notes_path, file)
        if patch is None:
            patch = json.load(open(notes_path)).get('patch')
    else:
        notes = read_events(notes_path, slot)
    if not notes:
        raise SystemExit('no notes in %s' % notes_path)
    rows = measure(x, sr, notes, opt)
    return result(rows, opt, patch, os.path.basename(wav))


def check_work(work, opt=None):
    """a measure_patch.py work folder whose res/*.json have tone rows (run with --what ...,tone)"""
    meta = json.load(open(os.path.join(work, 'plan.json')))
    rows = []
    for fi in meta['files']:
        p = os.path.join(work, 'res', fi['name'] + '.json')
        if os.path.exists(p):
            for r in json.load(open(p))['rows']:
                rows += [dict(t, file=fi['name']) for t in r.get('tone', [])]
    if not rows:
        raise SystemExit('%s: no tone rows (plan with --what ...,tone and run again)' % work)
    rows.sort(key=lambda n: (n['file'], n['on']))
    return result(rows, opt, meta['patch'], os.path.basename(os.path.normpath(work)))


def merge_avoid(paths):
    out = {}
    for p in paths:
        for patch, ents in json.load(open(p)).get('avoid', {}).items():
            out.setdefault(patch, [])
            for e in ents:
                if e not in out[patch]:
                    out[patch].append(e)
    for v in out.values():
        v.sort(key=lambda e: (e['keys'], e['band']))
    return out


def add_thresholds(p):
    p.add_argument('--window', help='lo:hi ms after the note-on (default 50:350)')
    for k, v in DEFAULTS.items():
        if k in ('window',):
            continue
        p.add_argument('--' + k.replace('_', '-'), dest=k, type=float, default=None, help='default %g' % v)


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if argv and argv[0] == 'avoid':
        ap = argparse.ArgumentParser(prog='tone_check.py avoid')
        ap.add_argument('results', nargs='+')
        ap.add_argument('-o')
        a = ap.parse_args(argv[1:])
        out = merge_avoid(a.results)
        s = json.dumps({'avoid': out}, indent=1)
        if a.o:
            open(a.o, 'w').write(s + '\n')
        print(s)
        return 0
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog='See the module header.')
    ap.add_argument('src', help='a render (.wav) or a measure_patch.py work folder')
    ap.add_argument('notes', nargs='?', help='kthost event file, plan.json (with --file) or events.tsv')
    ap.add_argument('--file', help='plan.json: the render\'s file name (default the wav\'s name)')
    ap.add_argument('--slot', type=int, help='events.tsv: one slot')
    ap.add_argument('--patch')
    ap.add_argument('-o')
    ap.add_argument('-q', action='store_true', help='no report on stdout')
    add_thresholds(ap)
    a = ap.parse_args(argv)
    opt = opts_from(a)
    if os.path.isdir(a.src):
        res = check_work(a.src, opt)
        out = a.o or os.path.join(a.src, 'tone.json')
    else:
        if not a.notes:
            raise SystemExit('a render needs its notes (kthost event file, plan.json or events.tsv)')
        res = check_wav(a.src, a.notes, a.file, a.slot, a.patch, opt)
        out = a.o or os.path.splitext(a.src)[0] + '.tone.json'
    with open(out, 'w') as f:
        json.dump(res, f, indent=1)
    if not a.q:
        print(report_text(res))
        print('-> %s' % out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
