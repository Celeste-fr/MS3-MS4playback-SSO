#!/usr/bin/env python3
"""Real string players' level at pp / mf / ff, measured from recordings, against SSO's measured CC1 curves.

Question (2026-10-11): does a real string section's balance shift between soft and loud dynamics, and by how much?
The fork's SSO string balance M (Vn1 -3.81, Vn2 -2.66, Va -2.12, Vc +2.77, Cb +5.81 dB) is fixed and was built at
CC1 64 with the real side at 0.4 between Weinzierl et al. 2018's pp and ff ("as soft / as loud as possible"). This
script measures what real players recorded at pp, mf and ff with a FIXED recording gain, and builds the balance at
each dynamic from it. Research only: nothing here changes playback.

Data (not in git; downloaded to ~/MuseScore/ms3fork/data/real-dynamics/):
  University of Iowa Musical Instrument Samples (MIS), pre-2011 strings: violin, viola, cello, double bass, arco,
  pp / mf / ff chromatic scales, mono 16 bit 44.1 kHz, one Neumann KM 84, anechoic chamber. The MIS page
  (theremin.music.uiowa.edu/MIS.html, read 2026-10-11): "Because the recordings represent the complete dynamic
  structure of the instrument, input volume levels were not changed during the recording session"; "may be
  downloaded and used for any projects, without restrictions". (The post-2012 files were normalized to -0.4 dB:
  MISPost2012Intro.html step 11, so they carry no level.)

Measures per note, the same as the SSO side's (sso_cc1_sweep.py on branch sso-cc1-curves):
  loudDb  the loudest 50 ms (25 ms hop) of the note and 0.5 s after it, dB full scale
  rmsDb   mean power 1.5-3.0 s after the onset (notes shorter than 3.0 s: none, see `measure`)
  lufs    ITU-R BS.1770-4 integrated loudness (pyloudnorm) of the note
Notes are found by level (`segment`), their pitches from the file name's chromatic range and checked by YIN.

Usage (python3 -I; pass paths, run from outside the data folder):
  real_string_dynamics.py measure <iowa folder> <notes.json> [wide] [min note s]
      folder holds violin/ viola/ cello/ doublebass/; the 2012 set was measured with "wide 2.0"
  real_string_dynamics.py analyze <notes.json> <notes2012.json | -> <sso_cc1_curves.json> <3_Audio_Features.csv>
      <out.json> [sso_sound_range.json]
      -> tools/soundlibraries/real_string_dynamics.json (python3 -P -E: -I hides the user site's librosa /
      pyloudnorm; -P still keeps the script's and the current folder out of sys.path)
"""
import json
import math
import os
import re
import sys

INSTRUMENTS = {'violin': 'Violin', 'viola': 'Viola', 'cello': 'Violoncello', 'doublebass': 'Double bass'}
DYNS = ['pp', 'mf', 'ff']
# arco ranges searched with `wide` (MIDI; a choice wide enough for the files: violin G3 up, viola C3 up, cello C2 up,
# bass C1 up (the 2012 bass has a C extension))
INSTRUMENT_RANGE = {'violin': (55, 105), 'viola': (48, 93), 'cello': (36, 84), 'doublebass': (24, 67)}
PC = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}

# segmentation (choices, not measured): a note is where the 50 ms level is above both the file's noise floor + SNR_DB
# and the file's loudest 50 ms - SEG_DB, lasting at least MIN_NOTE s; gaps shorter than MERGE_GAP s are bridged.
# Noise floor: the NOISE_PCT percentile of the 50 ms levels that aren't digital silence (the editors zeroed some gaps)
SEG_DB = 40.0
SNR_DB = 10.0
NOISE_PCT = 10
SILENCE_DB = -150.0
MIN_NOTE = 0.5
MERGE_GAP = 0.3
SUSTAIN = (1.5, 3.0)      # sso_cc1_sweep.py's sustain window

# ---------------------------------------------------------------------------------------------------------------
# file names and pitches


def midi_of(name):
    """'Bb3' -> 58, 'Gb5' -> 78 (MIDI, C4 = 60)"""
    m = re.fullmatch(r'([A-G])(b|#)?(\d)', name)
    if not m:
        raise ValueError(name)
    return 12 * (int(m.group(3)) + 1) + PC[m.group(1)] + {'b': -1, '#': 1, None: 0}[m.group(2)]


def parse_name(fname):
    """Iowa MIS name -> (instrument key, dynamic, string, low MIDI, high MIDI) or None.
    'Violin.arco.pp.sulG.G3B3.aiff', 'Viola.arco.sulC.pp.C3B3.aiff', 'Bass.arco.pp.sulE.E1B1.aiff', '...D6.aiff',
    2012 set: 'Viola.arco.sulC.pp.C3B3.mono.aif'"""
    parts = [p for p in fname.split('.') if p not in ('mono', 'stereo')]
    if len(parts) < 5 or parts[1] != 'arco':
        return None
    inst = {'Violin': 'violin', 'Viola': 'viola', 'Cello': 'cello', 'Bass': 'doublebass'}.get(parts[0])
    dyn = next((p for p in parts if p in DYNS), None)
    string = next((p[3:] for p in parts if p.startswith('sul')), None)
    rng = parts[-2]
    notes = re.findall(r'[A-G][b#]?\d', rng)
    if inst is None or dyn is None or not notes or ''.join(notes) != rng:
        return None
    lo = midi_of(notes[0])
    hi = midi_of(notes[-1]) if len(notes) > 1 else lo
    return inst, dyn, string, lo, hi


# ---------------------------------------------------------------------------------------------------------------
# signal measures (numpy only here; pyloudnorm / librosa imported where used)


def db(power):
    return 10 * math.log10(power) if power > 0 else -200.0


def envelope(x, sr, win=0.05, hop=0.025):
    """50 ms mean power every 25 ms (dB), and the frame starts in samples"""
    import numpy as np
    p = x.astype('float64') ** 2
    w, h = int(round(win * sr)), int(round(hop * sr))
    if len(p) < w:
        return np.array([]), np.array([], dtype=int)
    c = np.concatenate([[0.0], np.cumsum(p)])
    st = np.arange(0, len(p) - w + 1, h)
    e = (c[st + w] - c[st]) / w
    return 10 * np.log10(np.maximum(e, 1e-20)), st


def loudest50(x, sr):
    e, _ = envelope(x, sr)
    return float(e.max()) if len(e) else -200.0


def noise_floor(x, sr):
    """the NOISE_PCT percentile of the 50 ms levels above digital silence, dB (None: all silence)"""
    import numpy as np
    e, _ = envelope(x, sr)
    e = e[e > SILENCE_DB]
    return float(np.percentile(e, NOISE_PCT)) if len(e) else None


def gap_noise(x, sr, segs, guard=0.5):
    """median 50 ms level between the notes (each note widened by guard s), digital silence left out: the
    recording chain's noise, which moves with the input gain (the fixed-gain check), dB; None when no gap"""
    import numpy as np
    e, st = envelope(x, sr)
    t = st / sr
    keep = e > SILENCE_DB
    for a, b in segs:
        keep &= ~((t + 0.05 > a - guard) & (t < b + guard))
    return float(np.median(e[keep])) if keep.sum() >= 20 else None


def gap_band_noise(x, sr, segs, band=(4000.0, 16000.0), guard=0.5, block=0.1, min_blocks=10):
    """mean power in band (Hz) of the 0.1 s blocks between the notes (each note widened by guard s) that hold no
    exact digital zero, dB: high up, the microphone and preamp's own noise rather than the player's, so a change of
    input gain between files moves it by the same dB; None when fewer than min_blocks blocks"""
    import numpy as np
    keep = x != 0
    for a, b in segs:
        keep[max(0, int((a - guard) * sr)):int((b + guard) * sr)] = False
    n = int(block * sr)
    starts = [i for i in range(0, len(x) - n, n) if keep[i:i + n].all()]
    if len(starts) < min_blocks:
        return None
    fr = np.fft.rfftfreq(n, 1.0 / sr)
    m = (fr >= band[0]) & (fr < band[1])
    w = np.hanning(n)
    p = sum(float((np.abs(np.fft.rfft(x[i:i + n] * w)) ** 2)[m].sum()) for i in starts) / len(starts)
    # one-sided spectrum: x 2 (Parseval over both halves); / n / sum(w^2): the band's mean power (white noise of
    # power P gives P x band / (sr / 2), tested)
    return db(2 * p / n / (w ** 2).sum())


def segment(x, sr, seg_db=SEG_DB, min_note=MIN_NOTE, merge_gap=MERGE_GAP):
    """[(on s, off s)] of the notes in a scale file: the 50 ms level above the noise floor + SNR_DB and within
    seg_db of the file's loudest"""
    e, st = envelope(x, sr)
    if not len(e):
        return []
    hop = 0.025
    nf = noise_floor(x, sr)
    on = e > max(e.max() - seg_db, (nf if nf is not None else SILENCE_DB) + SNR_DB)
    runs, i = [], 0
    while i < len(on):
        if on[i]:
            j = i
            while j < len(on) and on[j]:
                j += 1
            runs.append([i * hop, (j - 1) * hop + 0.05])
            i = j
        else:
            i += 1
    merged = []
    for r in runs:
        if merged and r[0] - merged[-1][1] < merge_gap:
            merged[-1][1] = r[1]
        else:
            merged.append(r)
    return [(a, b) for a, b in merged if b - a >= min_note]


def yin_midi(x, sr, lo, hi):
    """median YIN pitch (fractional MIDI) of a sustained segment, searched lo-2 .. hi+2 semitones"""
    import numpy as np
    import librosa
    fmin = 440.0 * 2 ** ((lo - 2 - 69) / 12)
    fmax = 440.0 * 2 ** ((hi + 2 - 69) / 12)
    frame = max(2048, int(2 ** math.ceil(math.log2(2.5 * sr / fmin))))
    if len(x) < frame:
        return None
    f0 = librosa.yin(x.astype('float64'), fmin=fmin, fmax=fmax, sr=sr, frame_length=frame)
    f0 = f0[np.isfinite(f0)]
    if not len(f0):
        return None
    return float(69 + 12 * math.log2(float(np.median(f0)) / 440.0))


def measure_note(x, sr, on, off, meter):
    import numpy as np
    i0, i1 = int(round(on * sr)), int(round(off * sr))
    r = {'on': round(on, 3), 'len': round(off - on, 3)}
    r['loudDb'] = round(loudest50(x[i0:min(len(x), i1 + int(0.5 * sr))], sr), 2)
    # sustain: SSO's 1.5-3.0 s when the note is that long, else the note's middle half (a choice: many Iowa
    # notes last 1.5-2.5 s); rmsWindow says which
    if off - on >= SUSTAIN[1]:
        a, b, r['rmsWindow'] = on + SUSTAIN[0], on + SUSTAIN[1], 'sso'
    else:
        a, b, r['rmsWindow'] = on + 0.25 * (off - on), on + 0.75 * (off - on), 'middle'
    s = x[int(round(a * sr)):int(round(b * sr))].astype('float64')
    r['rmsDb'] = round(db(float((s ** 2).mean())), 2)
    v = meter.integrated_loudness(x[i0:i1].astype('float64'))
    r['lufs'] = round(float(v), 2) if np.isfinite(v) else None
    return r


def measure(folder, out, wide=False, min_note=MIN_NOTE):
    """every arco file of the four instruments: notes, pitches, measures -> out (JSON).
    wide: search each note's pitch over the instrument's whole range (the 2012 set's names often don't match the
    notes: e.g. Viola.arco.sulA.mf.C5B5 holds Bb3-A4) instead of the name's range"""
    import soundfile as sf
    import pyloudnorm
    res = {'files': [], 'notes': []}
    for inst in INSTRUMENTS:
        d = os.path.join(folder, inst)
        if not os.path.isdir(d):
            continue
        for fname in sorted(os.listdir(d)):
            meta = parse_name(fname)
            if meta is None:
                continue
            _, dyn, string, lo, hi = meta
            x, sr = sf.read(os.path.join(d, fname), always_2d=True)
            x = x.mean(axis=1)
            meter = pyloudnorm.Meter(sr)
            segs = segment(x, sr, min_note=min_note)
            want = hi - lo + 1
            nf = noise_floor(x, sr)
            info = {'file': fname, 'instrument': inst, 'dynamic': dyn, 'string': string, 'lo': lo, 'hi': hi,
                    'expected': want, 'found': len(segs), 'peakDb': round(loudest50(x, sr), 2),
                    'noiseDb': None if nf is None else round(nf, 2), 'kept': 0}
            gn = gap_noise(x, sr, segs)
            info['gapNoiseDb'] = None if gn is None else round(gn, 2)
            gh = gap_band_noise(x, sr, segs)
            info['gapHfNoiseDb'] = None if gh is None else round(gh, 2)
            # pitch: YIN of the note's middle half, as measured (fractional MIDI: some files aren't semitones apart,
            # e.g. Cello.arco.pp.sulC.C2Gb2 has 12 notes 65.7-91.2 Hz); kept within the name's range +- 1 semitone
            for a, b in segs:
                mid = x[int((a + 0.25 * (b - a)) * sr):int((a + 0.75 * (b - a)) * sr)]
                f = yin_midi(mid, sr, *(INSTRUMENT_RANGE[inst] if wide else (lo, hi)))
                if f is None or (not wide and not lo - 1 <= f <= hi + 1):
                    continue
                n = measure_note(x, sr, a, b, meter)
                n.update({'file': fname, 'instrument': inst, 'dynamic': dyn, 'string': string,
                          'pitch': round(f, 2), 'snrDb': None if nf is None else round(n['loudDb'] - nf, 2)})
                res['notes'].append(n)
                info['kept'] += 1
            res['files'].append(info)
    json.dump(res, open(out, 'w'), indent=0)
    print('files', len(res['files']), 'notes', len(res['notes']))
    for f in res['files']:
        flag = '' if f['kept'] == f['expected'] else ' <-'
        print('  %-40s expected %2d found %2d kept %2d noise %7s gap %7s peak %6.1f%s' % (
            f['file'], f['expected'], f['found'], f['kept'], f['noiseDb'], f['gapNoiseDb'], f['peakDb'], flag))


# ---------------------------------------------------------------------------------------------------------------
# analysis

SECT = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
SECT_INST = {'Violins 1': 'violin', 'Violins 2': 'violin', 'Violas': 'viola', 'Celli': 'cello', 'Basses': 'doublebass'}
N_PLAYERS = {'Violins 1': 16, 'Violins 2': 14, 'Violas': 12, 'Celli': 10, 'Basses': 8}   # Spitfire SSO product page
FIXED_M = {'Violins 1': -3.81, 'Violins 2': -2.66, 'Violas': -2.12, 'Celli': 2.77, 'Basses': 5.81}
# MuseScore's dynamic levels (ms4playback.cpp ordinaryLevel) and the CC1 the fork sends for each
# (expressionLevel: 16 + (level - 3250) / 500 * 16; none of these is NATURAL 5000): pp 32, p 48, mp 64, mf 80,
# f 96, ff 112
LEVELS = {'pp': 3750, 'p': 4250, 'mp': 4750, 'mf': 5250, 'f': 5750, 'ff': 6250}
DYN6 = ['pp', 'p', 'mp', 'mf', 'f', 'ff']
MEASURES = ['loudDb', 'rmsDb', 'lufs']
MIN_SNR = 20.0          # a choice: notes less than 20 dB above their file's noise floor are left out
WZ_LABEL = {'violin': 'Violin modern', 'viola': 'Viola modern', 'cello': 'Cello modern',
            'doublebass': 'Double bass modern'}
# the 2012 players used as a second player (gainCheck2012 in the output): viola, 4-16 kHz noise between the notes
# within 0.04-0.12 dB over pp / mf / ff on every string, about 5 dB above the 16-bit dither floor (fixed gain);
# double bass, no 4-16 kHz blocks (its gaps hold exact zeros), broadband gap noise within 0.68-3.09 dB (fixed within
# about 3 dB at worst: weaker). Left out: violin 2012 (4-16 kHz gap noise moves 22.6-47.9 dB between files, pp
# notes barely above a -49 dB noise) and cello 2012 (broadband gap noise moves up to 8.9 dB).
SECOND_PLAYERS = ['viola', 'doublebass']
CLARK_LUCE_SPAN = 11.0  # Clark & Luce 1965 (JAES 13(2) 151-157), abstract: written pp-ff spans of about 11 dB


def cc_of_level(level):
    """libmscore/ms4playback.cpp expressionLevel for levels between ppp (3250) and fff (6750), none NATURAL"""
    return max(16, min(127, int(round(16 + (level - 3250) / 500.0 * 16))))


def interp(table, p, clamp=True):
    """linear in dB between the table's pitches; outside them the nearest (clamp) or None"""
    if p in table:
        return table[p]
    ks = sorted(table)
    if not ks:
        return None
    if p < ks[0] or p > ks[-1]:
        if not clamp:
            return None
        return table[ks[0]] if p < ks[0] else table[ks[-1]]
    a = max(k for k in ks if k < p)
    b = min(k for k in ks if k > p)
    return table[a] + (table[b] - table[a]) * (p - a) / (b - a)


def wz_midi(name):
    """TU Berlin database pitch names: 'a2', 'ck4' (k = sharp)"""
    pc = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
    return 12 * (int(name[-1]) + 1) + pc[name[0]] + (1 if name[1] == 'k' else 0)


def weinzierl(csv_path):
    """{instrument: {'pp': {midi: dB}, 'ff': {...}}}: sound power, single tones (_et_), modern instruments"""
    import csv
    rows = list(csv.reader(open(csv_path)))[2:]
    out = {}
    for k, lab in WZ_LABEL.items():
        t = {'pp': {}, 'ff': {}}
        for r in rows:
            if r[1] == lab and '_et_' in r[0] and r[4] in t:
                t[r[4]].setdefault(wz_midi(r[5]), []).append(float(r[6]))
        out[k] = {d: {p: sum(v) / len(v) for p, v in t[d].items()} for d in t}
    return out


def level_tables(notes, measure, min_snr=MIN_SNR):
    """{(instrument, string, dynamic): {fractional pitch: value}} of the kept notes (repeated pitches averaged)"""
    t = {}
    for n in notes:
        v = n.get(measure)
        if v is None or v <= -150 or (n.get('snrDb') is not None and n['snrDb'] < min_snr):
            continue
        t.setdefault((n['instrument'], n['string'], n['dynamic']), {}).setdefault(n['pitch'], []).append(v)
    return {k: {p: sum(v) / len(v) for p, v in d.items()} for k, d in t.items()}


def string_grid(tables, inst, string):
    """{midi: {'pp': dB, 'mf': dB, 'ff': dB}} on the semitones that all three dynamics' notes on this string span
    (linear in dB between measured notes; no extrapolation): one recording session, so one gain"""
    tt = [tables.get((inst, string, d)) for d in DYNS]
    if any(not t for t in tt):
        return {}
    lo = math.ceil(max(min(t) for t in tt))
    hi = math.floor(min(max(t) for t in tt))
    return {p: {d: interp(t, p, clamp=False) for d, t in zip(DYNS, tt)} for p in range(lo, hi + 1)}


def spans(tables, inst):
    """{midi: {'pp_mf': dB, 'mf_ff': dB, 'pp_ff': dB, 'strings': n, 'sd_pp_ff': dB across strings or None}}:
    each string's spans, averaged over the strings that play the pitch"""
    per = {}
    for (i, s, _d) in tables:
        if i != inst:
            continue
        for p, v in string_grid(tables, inst, s).items():
            per.setdefault(p, {})[s] = v
    out = {}
    for p, bys in sorted(per.items()):
        a = [v['mf'] - v['pp'] for v in bys.values()]
        b = [v['ff'] - v['mf'] for v in bys.values()]
        c = [v['ff'] - v['pp'] for v in bys.values()]
        sd = None
        if len(c) > 1:
            m = sum(c) / len(c)
            sd = math.sqrt(sum((x - m) ** 2 for x in c) / (len(c) - 1))
        out[p] = {'pp_mf': sum(a) / len(a), 'mf_ff': sum(b) / len(b), 'pp_ff': sum(c) / len(c),
                  'strings': len(c), 'sd_pp_ff': sd, 'ff': {s: v['ff'] for s, v in bys.items()}}
    return out


def session_spans(notes, inst, measure, min_snr=MIN_SNR):
    """pitch-agnostic spans: per string (one session, one gain) the median level of each dynamic's notes, then the
    spans; {'strings': {string: {pp_mf, mf_ff, pp_ff}}, 'median': {...}}. For the 2012 set, whose YIN pitches are
    often wrong (stuck at the search range's floor) or noise, notes pinned within 0.1 of the range's ends are left out"""
    lo, hi = INSTRUMENT_RANGE[inst]
    by = {}
    for n in notes:
        v = n.get(measure)
        if n['instrument'] != inst or v is None or v <= -150 or (n.get('snrDb') or 0) < min_snr:
            continue
        if n['pitch'] < lo - 2 + 0.1 or n['pitch'] > hi + 2 - 0.1:
            continue
        by.setdefault(n['string'], {}).setdefault(n['dynamic'], []).append(v)
    out = {}
    for s, d in sorted(by.items()):
        if all(k in d for k in DYNS):
            m = {k: median(d[k]) for k in DYNS}
            out[s] = {'pp_mf': m['mf'] - m['pp'], 'mf_ff': m['ff'] - m['mf'], 'pp_ff': m['ff'] - m['pp'],
                      'notes': {k: len(d[k]) for k in DYNS}}
    med = {k: median([v[k] for v in out.values()]) for k in ('pp_mf', 'mf_ff', 'pp_ff')} if out else None
    return {'strings': out, 'median': med}


def constant_spans(ss, lo, hi):
    """a span table with the same spans on every semitone lo..hi (for session_spans)"""
    m = ss['median']
    return {p: {'pp_mf': m['pp_mf'], 'mf_ff': m['mf_ff'], 'pp_ff': m['pp_ff'], 'strings': len(ss['strings']),
                'sd_pp_ff': None, 'ff': {}} for p in range(lo, hi + 1)}


def median(xs):
    xs = sorted(xs)
    n = len(xs)
    return None if not n else (xs[n // 2] if n % 2 else (xs[n // 2 - 1] + xs[n // 2]) / 2)


def pct(xs, q):
    xs = sorted(xs)
    if not xs:
        return None
    k = (len(xs) - 1) * q
    a = int(math.floor(k))
    b = min(a + 1, len(xs) - 1)
    return xs[a] + (xs[b] - xs[a]) * (k - a)


def register_rows(sp, lo, hi):
    """the spans by octave (C to B) over lo..hi: median and count"""
    rows = []
    for o in range(lo // 12, hi // 12 + 1):
        ps = [p for p in sp if o * 12 <= p < o * 12 + 12 and lo <= p <= hi]
        if not ps:
            continue
        rows.append({'octave': 'C%d-B%d' % (o - 1, o - 1), 'pitches': len(ps),
                     'pp_mf': round(median([sp[p]['pp_mf'] for p in ps]), 1),
                     'mf_ff': round(median([sp[p]['mf_ff'] for p in ps]), 1),
                     'pp_ff': round(median([sp[p]['pp_ff'] for p in ps]), 1)})
    return rows


class Sso:
    """SSO All techniques Long loudDb / rmsDb / lufs against CC1 (sso_cc1_curves.json, median of 4 repeats)"""

    def __init__(self, path, measure):
        cur = json.load(open(path))
        self.ccs = cur['method']['ccs']
        self.t = {s: {int(p): {pt['cc']: pt[measure] for pt in pts} for p, pts in cur['sections'][s]['points'].items()}
                  for s in SECT}

    def at(self, s, p, c):
        import bisect
        ccs = self.ccs

        def at_pitch(q):
            t = self.t[s][q]
            if c <= ccs[0]:
                return t[ccs[0]]
            if c >= ccs[-1]:
                return t[ccs[-1]]
            i = bisect.bisect_right(ccs, c) - 1
            a, b = ccs[i], ccs[i + 1]
            return t[a] + (t[b] - t[a]) * (c - a) / (b - a)
        return interp({q: at_pitch(q) for q in self.t[s]}, p)


def real_at(wz, sp, inst, p, dyn, anchor, clamp=True):
    """one player's level (sound power, dB) at pitch p and MuseScore dynamic dyn.
    Measured spans (Iowa) piecewise linear in MuseScore level through pp 3750, mf 5250, ff 6250; anchored to
    Weinzierl's sound power at ff ('ff'), at pp ('pp') or so that the pp-ff midpoint matches ('mid').
    anchor 'wlinear': Weinzierl alone, linear between pp and ff (the M / M_dyn model)."""
    L = LEVELS[dyn]
    wpp, wff = interp(wz[inst]['pp'], p), interp(wz[inst]['ff'], p)
    if anchor == 'wlinear':
        return wpp + (L - 3750) / 2500.0 * (wff - wpp)
    s = {k: interp({q: v[k] for q, v in sp.items()}, p, clamp=clamp) for k in ('pp_mf', 'mf_ff')}
    if s['pp_mf'] is None:
        return None
    rel = {'pp': 0.0, 'mf': s['pp_mf'], 'ff': s['pp_mf'] + s['mf_ff']}     # relative to pp
    if L <= 5250:
        r = rel['pp'] + (L - 3750) / 1500.0 * (rel['mf'] - rel['pp'])
    else:
        r = rel['mf'] + (L - 5250) / 1000.0 * (rel['ff'] - rel['mf'])
    base = {'ff': wff - rel['ff'], 'pp': wpp, 'mid': (wpp + wff) / 2 - rel['ff'] / 2}[anchor]
    return base + r


def balance(wz, sps, sso, ranges, anchor, clamp=True, ccs=None):
    """{dyn: {section: dB to add, centred}} and the per-section D (SSO - real section) behind it"""
    out = {}
    for dyn in DYN6:
        c = cc_of_level(LEVELS[dyn]) if ccs is None else ccs[dyn]
        D = {}
        for s in SECT:
            inst = SECT_INST[s]
            lo, hi = ranges[s]
            acc, n = 0.0, 0
            for p in range(lo, hi + 1):
                r = real_at(wz, sps[inst], inst, p, dyn, anchor, clamp)
                if r is None:
                    continue
                acc += sso.at(s, p, c) - (r + 10 * math.log10(N_PLAYERS[s]))
                n += 1
            D[s] = acc / n
        m = sum(D.values()) / len(D)
        out[dyn] = {s: -(D[s] - m) for s in SECT}
    return out


def common_ranges(wz, range_json, sps=None):
    """M's range per section: the SSO Long's sounding range (sso_sound_range.json) that Weinzierl also measured;
    with sps, also within Iowa's measured spans"""
    rng = json.load(open(range_json))
    out = {}
    for s in SECT:
        inst = SECT_INST[s]
        rows = [r[0] for r in rng[s]['Long']['range']]
        lo = max(min(rows), min(wz[inst]['pp']), min(wz[inst]['ff']))
        hi = min(max(rows), max(wz[inst]['pp']), max(wz[inst]['ff']))
        if sps is not None:
            lo, hi = max(lo, min(sps[inst])), min(hi, max(sps[inst]))
        out[s] = (lo, hi)
    return out


def bootstrap_change(wz, sps, sso, ranges, anchor, n=1000, seed=1):
    """pp -> ff change of each section's offset, with each instrument's span pitches resampled (with replacement):
    5 / 50 / 95 percentiles. Neighbouring semitones share notes, so this understates the spread."""
    import random
    rnd = random.Random(seed)
    res = {s: [] for s in SECT}
    for _ in range(n):
        bs = {}
        for inst, sp in sps.items():
            ks = sorted(sp)
            pick = [rnd.choice(ks) for _ in ks]
            # resampled spans put back on the original pitches (keeps the range, shuffles the values)
            bs[inst] = {p: sp[q] for p, q in zip(ks, sorted(pick))}
        b = balance(wz, bs, sso, ranges, anchor)
        for s in SECT:
            res[s].append(b['ff'][s] - b['pp'][s])
    return {s: [round(pct(v, q), 2) for q in (0.05, 0.5, 0.95)] for s, v in res.items()}


def rnd(x, k=2):
    if isinstance(x, dict):
        return {(str(a) if not isinstance(a, str) else a): rnd(v, k) for a, v in x.items()}
    if isinstance(x, (list, tuple)):
        return [rnd(v, k) for v in x]
    if isinstance(x, float):
        return round(x, k)
    return x


def instrument_summary(sp, wz, inst, tables):
    ps = sorted(sp)
    col = {k: [sp[p][k] for p in ps] for k in ('pp_mf', 'mf_ff', 'pp_ff')}
    frac = [sp[p]['pp_mf'] / sp[p]['pp_ff'] for p in ps if abs(sp[p]['pp_ff']) > 3]
    sds = [sp[p]['sd_pp_ff'] for p in ps if sp[p]['sd_pp_ff'] is not None]
    # Weinzierl's ff - pp on the same semitones; the ff register shape against Iowa's, per string (one gain each)
    wspan = [interp(wz[inst]['ff'], p) - interp(wz[inst]['pp'], p) for p in ps]
    shape = []
    for st in sorted({s for p in ps for s in sp[p]['ff']}):
        q = [p for p in ps if st in sp[p]['ff']]
        if len(q) < 4:
            continue
        a = [sp[p]['ff'][st] for p in q]
        b = [interp(wz[inst]['ff'], p) for p in q]
        ma, mb = sum(a) / len(a), sum(b) / len(b)
        shape.extend((x - ma) - (y - mb) for x, y in zip(a, b))
    return {
        'semitones': [ps[0], ps[-1]], 'n': len(ps),
        'notesKept': {d: sum(len(v) for (i, _s, dd), v in tables.items() if i == inst and dd == d) for d in DYNS},
        **{k: {'median': median(v), 'p10': pct(v, .1), 'p90': pct(v, .9)} for k, v in col.items()},
        'mfFraction': {'median': median(frac), 'p10': pct(frac, .1), 'p90': pct(frac, .9)},
        'mfOnMuseScoreScale': 3750 + 2500 * median(frac),
        'acrossStringsSdPpFf': {'median': median(sds), 'max': max(sds) if sds else None, 'pitches': len(sds)},
        'byOctave': register_rows(sp, ps[0], ps[-1]),
        'weinzierl': {'pp_ff_samePitches': median(wspan),
                      'ffShapeRmsDiff': math.sqrt(sum(x * x for x in shape) / len(shape)) if shape else None},
    }


def analyze(notes_path, notes2_path, curves_path, csv_path, out_path, range_json=None):
    """notes_path: the pre-2011 set (main); notes2_path: the 2012 set or '-' (its double bass as a second player)"""
    if range_json is None:
        range_json = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_sound_range.json')
    data = json.load(open(notes_path))
    notes = data['notes']
    data2 = json.load(open(notes2_path)) if notes2_path and notes2_path != '-' else None
    wz = weinzierl(csv_path)
    res = {'measures': {}}
    for measure in MEASURES:
        tables = level_tables(notes, measure)
        sps = {inst: spans(tables, inst) for inst in INSTRUMENTS}
        sso = Sso(curves_path, measure)
        rM = common_ranges(wz, range_json)
        rI = common_ranges(wz, range_json, sps)
        r = {'instruments': {inst: instrument_summary(sp, wz, inst, tables) for inst, sp in sps.items()}}
        bal = {}
        for anchor in ('ff', 'pp', 'mid', 'wlinear'):
            bal[anchor] = balance(wz, sps, sso, rM, anchor)
        bal_in = balance(wz, sps, sso, rI, 'ff')
        # why the balance moves: each section's SSO level change from CC1 32 to 112 against its real section's
        # pp -> ff change, both averaged over M's range; change = -(SSO - real) + the five's mean
        why = {}
        for s in SECT:
            inst = SECT_INST[s]
            lo, hi = rM[s]
            ssod = [sso.at(s, p, cc_of_level(LEVELS['ff'])) - sso.at(s, p, cc_of_level(LEVELS['pp']))
                    for p in range(lo, hi + 1)]
            reald = [real_at(wz, sps[inst], inst, p, 'ff', 'ff') - real_at(wz, sps[inst], inst, p, 'pp', 'ff')
                     for p in range(lo, hi + 1)]
            why[s] = {'ssoCc32To112': sum(ssod) / len(ssod), 'realPpToFf': sum(reald) / len(reald)}
        r['whyItMoves'] = why
        r['ranges'] = {'M': rM, 'withinIowa': rI}
        r['balance'] = bal
        r['balanceWithinIowaRange'] = bal_in
        r['changePpToFf'] = {s: bal['ff']['ff'][s] - bal['ff']['pp'][s] for s in SECT}
        r['changePpToFfWithinIowaRange'] = {s: bal_in['ff'][s] - bal_in['pp'][s] for s in SECT}
        r['changePpToFfWeinzierlLinear'] = {s: bal['wlinear']['ff'][s] - bal['wlinear']['pp'][s] for s in SECT}
        r['mfMinusFixedM'] = {a: {s: bal[a]['mf'][s] - FIXED_M[s] for s in SECT} for a in bal}
        r['mpWeinzierlLinearMinusFixedM'] = {s: bal['wlinear']['mp'][s] - FIXED_M[s] for s in SECT}
        if measure == 'loudDb':
            r['bootstrapChangePpToFf'] = bootstrap_change(wz, sps, sso, rM, 'ff')
            snr = {}
            for m in (10.0, 30.0):
                t2 = level_tables(notes, measure, m)
                s2 = {inst: spans(t2, inst) for inst in INSTRUMENTS}
                if any(not v for v in s2.values()):
                    snr['minSnr%d' % m] = None      # an instrument has no notes left
                    continue
                b2 = balance(wz, s2, sso, rM, 'ff')
                snr['minSnr%d' % m] = {s: b2['ff'][s] - b2['pp'][s] for s in SECT}
            r['changePpToFfOtherSnr'] = snr
        if data2 is not None:
            # the 2012 players: every instrument summarised; SECOND_PLAYERS (those whose 2012 files pass the gain
            # check) put in place of the pre-2011 player, one at a time and together
            # pitch-agnostic (session_spans): the 2012 set's pitches aren't reliable enough for per-semitone spans;
            # the same for the pre-2011 players, to compare like with like
            sec = {'used': SECOND_PLAYERS, 'sessionSpans2012': {}, 'sessionSpansPre2011': {}}
            s2 = {}
            for inst in INSTRUMENTS:
                a = session_spans(data2['notes'], inst, measure)
                b = session_spans(notes, inst, measure)
                sec['sessionSpans2012'][inst] = a
                sec['sessionSpansPre2011'][inst] = b
                if a['median'] is not None:
                    s2[inst] = constant_spans(a, *INSTRUMENT_RANGE[inst])
            # the pre-2011 players with constant (session) spans: the baseline the swaps are compared with
            s1 = {inst: constant_spans(sec['sessionSpansPre2011'][inst], *INSTRUMENT_RANGE[inst]) for inst in INSTRUMENTS}
            b1 = balance(wz, s1, sso, rM, 'ff')
            sec['changePpToFf_pre2011Constant'] = {s: b1['ff'][s] - b1['pp'][s] for s in SECT}
            for name, swap in [(i, [i]) for i in SECOND_PLAYERS] + [('+'.join(SECOND_PLAYERS), SECOND_PLAYERS)]:
                mix = dict(s1, **{i: s2[i] for i in swap})
                b2 = balance(wz, mix, sso, rM, 'ff')
                sec['changePpToFf_' + name] = {s: b2['ff'][s] - b2['pp'][s] for s in SECT}
                sec['balance_' + name] = b2
            r['players2012'] = sec
        res['measures'][measure] = r
    res['ccs'] = {d: cc_of_level(LEVELS[d]) for d in DYN6}
    res['files'] = {'count': len(data['files']), 'notes': len(notes),
                    'nameRangeMismatch': sum(1 for f in data['files'] if f['found'] != f['expected'])}
    res['gainCheck'] = gap_noise_table(data['files'])
    if data2 is not None:
        res['gainCheck2012'] = gap_noise_table(data2['files'])
        res['files2012'] = {'count': len(data2['files']), 'notes': len(data2['notes']),
                            'notesKeptPerDynamic': {i: {d: sum(1 for n in data2['notes'] if n['instrument'] == i and
                                                           n['dynamic'] == d and (n['snrDb'] or 0) >= MIN_SNR)
                                                        for d in DYNS} for i in INSTRUMENTS}}
    main = res['measures']['loudDb']
    summary = {
        'note': 'main result: pre-2011 Iowa players, per-semitone spans, loudDb, anchored at Weinzierl ff; dB to add '
                'to each section, centred over the five',
        'spansPpMfFf': {i: {k: main['instruments'][i][k]['median'] for k in ('pp_mf', 'mf_ff', 'pp_ff')}
                        for i in INSTRUMENTS},
        'mfFraction': {i: main['instruments'][i]['mfFraction']['median'] for i in INSTRUMENTS},
        'balance': {d: main['balance']['ff'][d] for d in ('pp', 'mf', 'ff')},
        'changePpToFf': main['changePpToFf'],
        'over3dB': [s for s in SECT if abs(main['changePpToFf'][s]) > 3.0],
        'mfMinusFixedM': main['mfMinusFixedM']['ff'],
    }
    out = rnd(dict(HEADER, summary=summary, **res))
    if out_path:
        json.dump(out, open(out_path, 'w'), indent=1)
    return out


HEADER = {
    'what': 'Real string players\' level at pp / mf / ff measured from fixed-gain recordings, and the SSO string '
            'section balance it implies at each MuseScore dynamic (pp p mp mf f ff), against the fixed balance M '
            '(Vn1 -3.81, Vn2 -2.66, Va -2.12, Vc +2.77, Cb +5.81). Research only (2026-10-11): no playback change.',
    'made_by': 'tools/soundlibraries/real_string_dynamics.py measure + analyze (python3 -P -E; numpy, soundfile, '
               'librosa 1.0.0 YIN, pyloudnorm 0.2.0)',
    'sources': {
        'iowaPre2011': {
            'what': 'University of Iowa Musical Instrument Samples, pre-2011 strings: Violin, Viola, Cello, Double Bass, '
                    'arco, pp / mf / ff chromatic scales note by note, mono 16 bit 44.1 kHz, one Neumann KM 84, '
                    'anechoic chamber (Wendell Johnson Speech and Hearing Center). 143 files, 676 MB.',
            'url': 'https://theremin.music.uiowa.edu/MIS.html (pages MISviolin / MISviola / MIScello / MISdoublebass.html)',
            'licence': '"may be downloaded and used for any projects, without restrictions" (MIS.html, read 2026-10-11). '
                       'Only derived numbers are kept here; no audio in git.',
            'fixedGain': 'MIS.html, of the recordings until 2011: "Because the recordings represent the complete '
                         'dynamic structure of the instrument, input volume levels were not changed during the '
                         'recording session." Checked here (gainCheck): cello and double bass, the 4-16 kHz noise '
                         'between notes is the same at pp / mf / ff within 0.06-0.51 dB on every string, at -89.4 to '
                         '-92.2 dB, 7-10 dB above the 16-bit TPDF dither floor in that band (-99.0 dB, computed with '
                         'the same function in the test): a gain change would move it. Violin and viola: the gaps '
                         'were edited to near digital silence (no 0.1 s block without exact zeros), so unchecked; the '
                         'statement stands alone.',
        },
        'iowa2012': {
            'what': 'Iowa MIS strings recorded December 2011 - May 2012 (MISPost2012Intro.html), the centre microphone of '
                    'three Earthworks QTC-40 downsampled to 16 bit 44.1 kHz mono (MIS.html); other players. 129 files, '
                    '922 MB. The post-2012 edited files were normalized to -0.4 dB (MISPost2012Intro.html step 11) and '
                    'are not used.',
            'licence': 'as above',
            'fixedGain': 'not stated for these. Checked (gainCheck2012): viola 4-16 kHz gap noise within 0.04-0.12 dB '
                         'over pp / mf / ff on every string (-94.1 dB, 5 dB above the dither floor): fixed. Double bass: '
                         'no 4-16 kHz blocks, broadband gap noise within 0.68-3.09 dB per string: fixed within about '
                         '3 dB at worst, weaker evidence. Violin (4-16 kHz gap noise moves 22.6-47.9 dB, pp notes '
                         'barely above a -49 dB noise) and cello (broadband gap noise moves up to 8.9 dB) fail and are '
                         'not used for level. Many file names do not match the '
                         'notes (e.g. Viola.arco.sulA.mf.C5B5 holds Bb3-A4) and YIN pitches are unreliable, so these '
                         'players enter as pitch-agnostic session spans only (players2012).',
        },
        'weinzierl2018': {
            'what': 'Weinzierl, Lepa, Schultz, Detzner, von Coler, Behler (2018), "Sound power and timbre as cues for '
                    'the dynamic strength of orchestral instruments", JASA 144(3) 1347-1355, doi 10.1121/1.5053113: '
                    'single-note sound power at pp ("as soft as possible") and ff ("as loud as possible"), Table I. '
                    'Per-note values: database version 3, 3_Audio_Features.csv, column SoundPower in dB, _et_ files, '
                    'modern instruments (doi 10.14279/depositonce-5861.3).',
            'licence': 'CC BY-NC-SA 4.0 (0_Documentation.pdf of the database, lines "The database is provided under a '
                       'Creative Commons BY-NC-SA licence"): derived numbers only.',
        },
        'clarkLuce1965': 'Clark & Luce (1965), "Intensities of orchestral instrument scales played at prescribed '
                         'dynamic markings", JAES 13(2) 151-157: abstract only (secondary, via the 2026-10-11 '
                         'literature search): average pp -> ff range about 11 dB for non-percussive instruments.',
        'sso': 'sso_cc1_curves.json (branch sso-cc1-curves, commit 329681d3a2, md5 7787c2044af6fb89a73c63d2532f96f9): '
               'SSO All techniques Long at CC1 16-127 every 3 semitones, measured 2026-10-11 on the Windows VM with the '
               'real library (Kontakt Player 8.13.1), median of 4 repeats.',
        'museScoreLevels': 'libmscore/ms4playback.cpp: ordinaryLevel pp 3750 p 4250 mp 4750 mf 5250 f 5750 ff 6250; '
                           'expressionLevel -> CC1 32 48 64 80 96 112.',
    },
    'method': [
        'Notes: 50 ms level above the file\'s noise floor + 10 dB and within 40 dB of its loudest, >= 0.5 s (2012: '
        '>= 2.0 s, its gaps hold noise bursts); gaps < 0.3 s bridged. Pitch: YIN (librosa) median over the note\'s '
        'middle half, within the file name\'s range +- 1 semitone (pre-2011; 67 of 143 files hold another number of '
        'notes than the name says, and some pitches sit between semitones, e.g. Cello.arco.pp.sulC.C2Gb2: 12 notes '
        '65.7-91.2 Hz), the instrument\'s range (2012).',
        'Measures as the SSO side (sso_cc1_sweep.py): loudDb = loudest 50 ms (25 ms hop) of the note and 0.5 s after; '
        'rmsDb = mean power 1.5-3.0 s after the onset, or the middle half of notes shorter than 3.0 s (many Iowa notes '
        'last 1.5-2.5 s: a choice); lufs = BS.1770-4 integrated (pyloudnorm 0.2.0). Notes under 20 dB SNR left out '
        '(a choice; 10 and 30 dB in changePpToFfOtherSnr).',
        'Spans: per string (one recording session, one gain) each dynamic linear in dB between its notes, on the '
        'semitones all three dynamics cover; pp->mf and mf->ff per semitone, averaged over strings. Absolute levels '
        'are never compared across sessions or instruments.',
        'Real side of the balance: one player\'s level at MuseScore level L piecewise linear in dB through the measured '
        'pp (3750), mf (5250), ff (6250) (p, mp between pp and mf; f between mf and ff: a choice), anchored to '
        'Weinzierl\'s sound power at ff (main: both are a player\'s loudest single notes, while Iowa\'s pp spans '
        'only 25-65 % of Weinzierl\'s "as soft as possible"), at pp, or at the pp-ff midpoint (variants); section = '
        'player + 10 log10 N (16/14/12/10/8). Outside Iowa\'s semitones the nearest span (balanceWithinIowaRange '
        'restricts instead).',
        'Balance as M (m_final.py): per section D = mean over M\'s pitch range of SSO(p, CC1 of the dynamic) - real '
        'section(p, dynamic); offset = -(D - mean of the five D). Anchor "wlinear" is the M / M_dyn model (Weinzierl '
        'alone, linear between pp and ff): at mp it reproduces M exactly (mpWeinzierlLinearMinusFixedM = 0).',
        'Uncertainty: bootstrapChangePpToFf resamples each instrument\'s span semitones (understates: neighbours share '
        'notes); acrossStringsSdPpFf = SD of the pp->ff span between strings at one pitch (same player, other '
        'session); players2012 = a second viola and double bass player; three measures; SNR 10 / 30.',
        'Not used: TU Berlin anechoic Beethoven 8 (Boehm, Ackermann, Weinzierl 2020, CC BY 4.0): in-context '
        'dynamics of real section players, but period instruments, the open excerpt is 0.6-0.7 GB per movement '
        '(disk had < 3 GB free) and it needs score-to-audio alignment; the next source to try.',
    ],
}


def gap_noise_table(files):
    """{instrument: {string: {'broad'|'hf': {dyn: median dB}, spread: max - min over the dynamics}}}: the fixed-gain
    check. 'broad': median 50 ms level between the notes; 'hf': 4-16 kHz power between the notes (gap_band_noise)"""
    t = {}
    for f in files:
        for key, k in (('gapNoiseDb', 'broad'), ('gapHfNoiseDb', 'hf')):
            if f.get(key) is None:
                continue
            t.setdefault(f['instrument'], {}).setdefault(f['string'], {}).setdefault(k, {}) \
                .setdefault(f['dynamic'], []).append(f[key])
    out = {}
    for i, ss in t.items():
        for s, kk in ss.items():
            o = {}
            for k, ds in kk.items():
                m = {d: median(v) for d, v in ds.items()}
                o[k] = m
                if len(m) == 3:
                    o[k + 'Spread'] = max(m.values()) - min(m.values())
            out.setdefault(i, {})[s] = o
    return out


if __name__ == '__main__':
    cmd = sys.argv[1] if len(sys.argv) > 1 else ''
    if cmd == 'measure':
        # measure <folder> <out> [wide] [min note s]
        rest = sys.argv[4:]
        measure(sys.argv[2], sys.argv[3], 'wide' in rest, *[float(a) for a in rest if a != 'wide'])
    elif cmd == 'analyze':
        analyze(*sys.argv[2:8])
    else:
        print(__doc__)
