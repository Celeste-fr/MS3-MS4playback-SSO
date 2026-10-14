#!/usr/bin/env python3
"""How loud each instrument plays each written dynamic marking inside an orchestra: TU Berlin's anechoic Beethoven 8.

    python3 b8_marking_levels.py <data root> <3_Audio_Features.csv> <1a_DirectivitiesSingleTones_SOFA.zip> <out json>

<data root> holds the dataset's excerpts unzipped as <root>/m1, <root>/m2, <root>/m4 (each with anechoic/*.wav).

Source: Böhm, Ackermann & Weinzierl 2020, "A Multi-channel Anechoic Orchestra Recording of Beethoven's Symphony No. 8
op. 93", J. Audio Eng. Soc. 68(12) 977-984, doi:10.17743/jaes.2020.0056; data: depositonce
eacae473-68e1-48fa-8137-3c2cd30919f0 (CC BY 4.0): Orchester Wiener Akademie (period instruments), M. Haselböck;
excerpts of about 1 minute of movements 1, 2 and 4; 61 mono tracks, each one player (the strings recorded six times
in a row: Violin 1.1 = tracks 14-20 are seven takes of one player, and so on). Groups (strings, woodwinds, brass,
timpani) were recorded one after another, so a track holds no bleed from another group; within a group the paper gives
a frequency-weighted crosstalk attenuation of at least 20.7 dB.

Calibration (the paper, section 1.5 and Table 3): every track is normalised to 1 m distance and to 10.1 mV/Pa; 94 dB
SPL = -37.8 dBu, plus the documented mic gain G (dB), at the converter's +12 dBu = 0 dBFS, plus the documented
"artistic gain" A (dB). So SPL at 1 m, in the direction of the microphone = dBFS + 143.8 - G - A (the paper's own
example: Flute 1, G 40, A -17: -26.8 dBFS = 94 dB). This undoes the artistic gain.

Written dynamics: read from the first edition of the full score (Vienna: S. A. Steiner, plate 2570, [1816/17];
Beethoven-Haus Bonn C 93/30, digital archive, IIIF). The readings (one per marking and part, with crops) are not in
the repository; REGIONS below holds their result: per passage, the marking, the parts it applies to and its time
window in the excerpt, found from the audio (entries after rests, long notes) and the bar counts. Bars: movements 1
and 2 from bar 1; movement 4 in modern numbering (its opening half-bar upbeat = bar 0). A window covers only bars
under one marking for those parts (no crescendo / diminuendo / hairpin bars, no sf inside a p or f window).

Measure per note: each track is segmented into notes (YIN pitch, 10 ms frames: a note ends where the pitch moves by
more than 0.6 semitone or the level falls 45 dB under the track's 99th percentile; the dataset's rests are digital
silence). "steady": notes of at least 0.30 s, the energy mean from 100 ms after the onset to 50 ms before the end
(no attack, no release); "peak": every note of at least 60 ms, its loudest 50 ms (energy mean). Per part and
marking: the median over notes (all its players / takes and passages), the interquartile range and n. The steady
measure is the one comparable with the TU database's sound power of held tones; peaks are reported separately (short
and staccato notes, sf).

Sound power: the recording is one microphone per player, so SPL at 1 m in one direction. L_W = L_p(1 m) + 10 lg(4 pi)
- DI, DI the directivity index in the microphone's direction. The microphone positions (Table 3) are given relative to
the musician's head, while the TU directivity database gives the instrument's radiation on 32 microphones relative to
the instrument, and the musician's orientation in that frame is not documented; so DI in the recording's direction is
not known. Written instead: per instrument, the median, minimum and maximum of the database's broadband DI over its 32
directions (energy sum of the partials, median over the notes measured), i.e. the range a power estimate spans.

Rule D (real_range.py; the owner, 2026-10-12): the database's solo "as softly / loudly as possible" (Weinzierl et al.
2018, 3_Audio_Features.csv, "SoundPower in dB", `_et_` files) = ppp / fff, 7 equal dB steps between; here with the
median over all pitches per instrument (modern instruments, as rule D; and the database's historical instruments, for
the period orchestra).
"""
import csv
import glob
import io
import json
import os
import sys
import zipfile

import numpy as np
import soundfile as sf
import librosa

W = 'fl ob cl fg hn tp'.split()
S = 'vn1 vn2 vla vc db'.split()
PARTS = W + ['timp'] + S
NAMES = {'fl': 'flute', 'ob': 'oboe', 'cl': 'clarinet', 'fg': 'bassoon', 'hn': 'horn', 'tp': 'trumpet',
         'timp': 'timpani', 'vn1': 'violins 1', 'vn2': 'violins 2', 'vla': 'violas', 'vc': 'cellos', 'db': 'basses'}

# track number -> (part, player, mic gain G dB, artistic gain A dB): Böhm et al. 2020, Table 3
PLAYERS = [((1, 1), 'fl', 'Flute 1', 40, -17), ((2, 2), 'fl', 'Flute 2', 40, -18),
           ((3, 3), 'ob', 'Oboe 1', 40, -15), ((4, 4), 'ob', 'Oboe 2', 40, -15),
           ((5, 5), 'cl', 'Clarinet 1', 40, -16), ((6, 6), 'cl', 'Clarinet 2', 40, -15),
           ((7, 7), 'fg', 'Bassoon 1', 40, -12), ((8, 8), 'fg', 'Bassoon 2', 40, -14),
           ((9, 9), 'hn', 'Horn 1', 30, -14), ((10, 10), 'hn', 'Horn 2', 30, -17),
           ((11, 11), 'tp', 'Trumpet 1', 30, -7), ((12, 12), 'tp', 'Trumpet 2', 30, -7),
           ((13, 13), 'timp', 'Timpani', 15, -7),
           ((14, 20), 'vn1', 'Violin 1.1', 40, -12), ((21, 27), 'vn1', 'Violin 1.2', 40, -6),
           ((28, 33), 'vn2', 'Violin 2.1', 40, -6), ((34, 38), 'vn2', 'Violin 2.2', 40, -6),
           ((39, 44), 'vla', 'Viola 1', 40, -12), ((45, 49), 'vla', 'Viola 2', 40, -12),
           ((50, 55), 'vc', 'Cello', 30, -6), ((56, 61), 'db', 'Double bass', 30, -2)]


def player(track):
    for (a, b), part, name, g, art in PLAYERS:
        if a <= track <= b:
            return part, name, 143.8 - g - art
    raise KeyError(track)


# id, movement, marking, parts, t0, t1 (s), bars, score crops (readings), kind of notes
REGIONS = [
    ('m1_b01-04_f', 1, 'f', W + ['timp'] + S, 0.92, 4.45, '1-4', 'mixed'),
    ('m1_b05-08_p_clfg', 1, 'p', ['cl', 'fg'], 4.50, 8.25, '5-8', 'steady'),
    ('m1_b06-08_p_obhn', 1, 'p', ['ob', 'hn'], 5.10, 8.25, '6-8', 'steady'),
    ('m1_b10-28_f_winds', 1, 'f', W + ['timp'], 9.22, 27.60, '10-28', 'mixed'),
    ('m1_b11-28_f_vn', 1, 'f', ['vn1', 'vn2'], 10.20, 27.60, '11-28', 'mixed'),
    ('m1_b09-28_f_lowstr', 1, 'f', ['vla', 'vc', 'db'], 8.30, 27.60, '9-28', 'mixed'),
    ('m1_b34-37_p_str', 1, 'p', ['vn1', 'vn2', 'vla'], 32.50, 36.40, '34-37', 'short'),
    ('m1_b36-44_p_fg', 1, 'p', ['fg'], 34.40, 43.60, '36-44', 'short'),
    ('m1_b38-45_p_vn', 1, 'p', ['vn1', 'vn2'], 36.40, 44.40, '38-45', 'steady'),
    ('m1_b45-51_p_dol', 1, 'p', ['fl', 'ob', 'fg'], 44.30, 50.60, '45-51', 'steady'),
    ('m1_b52-57_pp', 1, 'pp', ['fl', 'ob', 'fg', 'hn', 'vn2', 'vc', 'db'], 50.70, 57.10, '52-57', 'steady'),
    ('m1_b53-57_pp_str', 1, 'pp', ['vn1', 'vla'], 51.80, 57.10, '53-57', 'mixed'),
    ('m1_b55_pp_tptimp', 1, 'pp', ['tp', 'timp'], 53.90, 55.00, '55', 'short'),
    ('m1_b63-65_sf', 1, 'sf', W + ['timp'] + S, 61.60, 64.55, '63-65', 'sf'),
    ('m1_b66-71_ff', 1, 'ff', W + ['timp'] + S, 64.62, 70.30, '66-71', 'mixed'),
    ('m1_b72-75_p_flobfg', 1, 'p', ['fl', 'ob', 'fg'], 71.10, 74.00, '72-75', 'steady'),
    ('m1_b73-75_p_vn2', 1, 'p', ['vn2'], 71.40, 74.00, '73-75', 'mixed'),
    ('m2_b01-08_pp', 2, 'pp', ['ob', 'cl', 'fg', 'hn', 'vn1'], 1.00, 25.20, '1-8', 'short'),
    ('m2_b03-08_pp_bass', 2, 'pp', ['vc', 'db'], 7.00, 25.20, '3-8', 'short'),
    ('m4_b00-12_pp_vn', 4, 'pp', ['vn1', 'vn2'], 1.20, 11.40, '0-12', 'mixed'),
    ('m4_b01-12_pp_vla', 4, 'pp', ['vla'], 2.00, 11.40, '1-12', 'mixed'),
    ('m4_b02-04_pp_flob', 4, 'pp', ['fl', 'ob'], 2.45, 4.70, '2-4', 'short'),
    ('m4_b15-16_ppp_str', 4, 'ppp', ['vn1', 'vn2', 'vla'], 13.00, 14.90, '15-16', 'short'),
    ('m4_b15-17_pp_obhn', 4, 'pp', ['ob', 'hn'], 13.10, 15.10, '15-17', 'short'),
    ('m4_b17-33_ff', 4, 'ff', ['fl', 'ob', 'cl', 'fg'] + S, 15.30, 30.10, '17-33', 'mixed'),
    ('m4_b18-33_ff_brass', 4, 'ff', ['hn', 'tp', 'timp'], 16.20, 30.10, '18-33', 'mixed'),
    ('m4_b48-55_p', 4, 'p', ['cl', 'fg'] + S, 41.30, 47.60, '48-55', 'steady'),
    ('m4_b48_p_single', 4, 'p', ['fl', 'ob', 'hn', 'tp'], 41.30, 42.00, '48', 'short'),
    ('m4_b60-66_p', 4, 'p', ['fl', 'ob', 'hn', 'tp'] + S, 50.90, 56.40, '60-66', 'steady'),
    ('m4_b68-75_pp', 4, 'pp', ['fl', 'ob', 'fg', 'vn1', 'vn2', 'vla'], 58.50, 66.00, '68-75', 'mixed'),
    ('m4_b71-75_pp_cl', 4, 'pp', ['cl'], 61.30, 66.00, '71-75', 'short'),
    ('m4_b76-83_f', 4, 'f', ['fl', 'ob', 'cl', 'fg'] + S, 66.30, 73.10, '76-83', 'mixed'),
    ('m4_b77-83_f_brass', 4, 'f', ['hn', 'tp', 'timp'], 66.85, 73.10, '77-83', 'short'),
    ('m4_b84-87_piuf', 4, 'più f', ['fl', 'ob', 'cl', 'fg'] + S, 73.20, 76.55, '84-87', 'steady'),
    ('m4_b88-89_ff', 4, 'ff', W + ['timp'] + S, 76.65, 79.40, '88-89', 'mixed'),
]
MARKINGS = ['ppp', 'pp', 'p', 'f', 'più f', 'ff', 'sf']
RULE_D_STEP = {'ppp': 0, 'pp': 1, 'p': 2, 'mp': 3, 'mf': 4, 'f': 5, 'ff': 6, 'fff': 7}

SR = 22050
HOP_S = 0.01
STEADY_MIN, ONSET, END = 0.30, 0.10, 0.05


def segments(x, sr, low):
    """[(t0, t1, midi, core power or None, peak50 power)] of one track; power in linear units of full scale"""
    h = int(sr * HOP_S)
    n = len(x) // h
    p = np.mean(x[:n * h].reshape(n, h).astype(np.float64) ** 2, axis=1)
    lev = 10 * np.log10(p + 1e-20)
    y = librosa.resample(x, orig_sr=sr, target_sr=SR)
    f0 = librosa.yin(y, fmin=30.0 if low else 120.0, fmax=2400, sr=SR, frame_length=4096 if low else 2048,
                     hop_length=int(SR * HOP_S))
    m = min(len(f0), n)
    f0, lev, p = f0[:m], lev[:m], p[:m]
    p50 = np.convolve(p, np.ones(5) / 5, mode='same')
    midi = 69 + 12 * np.log2(np.maximum(f0, 1) / 440.0)
    sounding = lev[lev > -150]
    top = np.percentile(sounding, 99) if len(sounding) else -100
    on = lev > top - 45
    out = []
    i = 0
    while i < m:
        if not on[i]:
            i += 1
            continue
        j = i
        buf = [midi[i]]
        while j + 1 < m and on[j + 1]:
            med = np.median(buf[-8:])
            if abs(midi[j + 1] - med) > 0.6:
                if j + 3 < m and on[j + 3] and abs(midi[j + 3] - med) <= 0.6:
                    j += 1
                    continue
                break
            buf.append(midi[j + 1])
            j += 1
        a, b = i + int(ONSET / HOP_S), j + 1 - int(END / HOP_S)
        core = float(np.mean(p[a:b])) if (j + 1 - i) * HOP_S >= STEADY_MIN and b > a else None
        out.append((i * HOP_S, (j + 1) * HOP_S, float(np.median(midi[i:j + 1])), core, float(np.max(p50[i:j + 1]))))
        i = j + 1
    return out


def db(v):
    return 10 * np.log10(v)


def stats(v):
    v = np.asarray(v, dtype=float)
    if len(v) == 0:
        return None
    return {'median': round(float(np.median(v)), 2), 'q25': round(float(np.percentile(v, 25)), 2),
            'q75': round(float(np.percentile(v, 75)), 2), 'n': int(len(v))}


def csv_medians(path):
    """{label: {'pp': median, 'ff': median, 'n_pp', 'n_ff'}} of "SoundPower in dB", `_et_` files"""
    rows = list(csv.reader(open(path, encoding='utf-8')))
    head = rows[1]
    col = head.index('SoundPower in dB')
    v = {}
    for r in rows[2:]:
        if len(r) > col and '_et_' in r[0] and r[4] in ('pp', 'ff') and r[col]:
            v.setdefault(r[1], {}).setdefault(r[4], []).append(float(r[col]))
    return {k: {'pp': round(float(np.median(d['pp'])), 2), 'ff': round(float(np.median(d['ff'])), 2),
                'n_pp': len(d['pp']), 'n_ff': len(d['ff'])} for k, d in v.items() if 'pp' in d and 'ff' in d}


DB_MODERN = {'fl': 'Transverse flute modern', 'ob': 'Oboe modern', 'cl': 'Clarinet modern', 'fg': 'Bassoon modern',
             'hn': 'French horn modern', 'tp': 'Trumpet modern', 'vn1': 'Violin modern', 'vn2': 'Violin modern',
             'vla': 'Viola modern', 'vc': 'Cello modern', 'db': 'Double bass modern'}
DB_HIST = {'fl': 'Keyed flute historical', 'ob': 'Classic oboe historical', 'cl': 'Clarinet historical',
           'fg': 'Classic bassoon historical', 'hn': 'Natural horn historical', 'tp': 'Natural trumpet historical',
           'vn1': 'Violin historical', 'vn2': 'Violin historical', 'vla': 'Viola historical',
           'vc': 'Cello historical', 'db': 'Double bass historical'}
SOFA_DIR = {'fl': 'Transverse_flute_modern', 'ob': 'Oboe_modern', 'cl': 'Clarinet_modern', 'fg': 'Bassoon_modern',
            'hn': 'French_horn_modern', 'tp': 'Trumpet_modern', 'timp': 'Timpani', 'vn1': 'Violin_modern',
            'vn2': 'Violin_modern', 'vla': 'Viola_modern', 'vc': 'Cello_modern', 'db': 'Double_bass_modern'}


def directivity(zpath):
    """{part: DI over the 32 directions: median, min, max} (dB), TU Berlin single-tone SOFA"""
    import h5py
    z = zipfile.ZipFile(zpath)
    out = {}
    for part, d in SOFA_DIR.items():
        rows = []
        for name in z.namelist():
            if '/%s/' % d in name and name.endswith('.sofa'):
                h = h5py.File(io.BytesIO(z.read(name)), 'r')
                pw = np.nansum(h['Data.Real'][0] ** 2 + h['Data.Imag'][0] ** 2, axis=1)
                if np.all(pw > 0):
                    rows.append(db(pw / pw.mean()))
        med = np.median(np.array(rows), axis=0)
        out[part] = {'median': round(float(np.median(med)), 2), 'min': round(float(med.min()), 2),
                     'max': round(float(med.max()), 2), 'n_notes': len(rows), 'source': d}
    return out


def main():
    root, csv_path, sofa_zip, out_path = sys.argv[1:5]
    notes = {}   # (part, marking) -> list of dicts
    per_region = {}
    for mvt in (1, 2, 4):
        files = sorted(glob.glob(os.path.join(root, 'm%d' % mvt, 'anechoic', '*.wav')))
        regs = [r for r in REGIONS if r[1] == mvt]
        for f in files:
            track = int(os.path.basename(f)[:2])
            part, pname, cal = player(track)
            mine = [r for r in regs if part in r[3]]
            if not mine:
                continue
            x, sr = sf.read(f, dtype='float32')
            segs = segments(x, sr, part in ('fg', 'vc', 'db', 'timp'))
            for rid, _, mark, _, t0, t1, bars, kind in mine:
                st, pk = [], []
                for s0, s1, mi, core, peak in segs:
                    if s0 >= t0 and s1 <= t1:
                        if core is not None:
                            st.append(db(core) + cal)
                        if s1 - s0 >= 0.06:
                            pk.append(db(peak) + cal)
                        if core is not None or s1 - s0 >= 0.06:
                            notes.setdefault((part, mark), []).append(
                                {'region': rid, 'track': track, 'player': pname, 'midi': mi,
                                 'steady': db(core) + cal if core is not None else None, 'peak': db(peak) + cal})
                pr = per_region.setdefault(rid, {}).setdefault(part, {'steady': [], 'peak': [], 'tracks': 0})
                pr['steady'] += st
                pr['peak'] += pk
                pr['tracks'] += 1
            print('m%d %s' % (mvt, os.path.basename(f)), flush=True)

    levels = {}
    for (part, mark), v in notes.items():
        stv = [n['steady'] for n in v if n['steady'] is not None]
        pkv = [n['peak'] for n in v]
        players = {}
        for n in v:
            if n['steady'] is not None:
                players.setdefault(n['player'], []).append(n['steady'])
        levels.setdefault(part, {})[mark] = {
            'steady': stats(stv), 'peak': stats(pkv),
            'steady_by_player': {k: round(float(np.median(a)), 2) for k, a in sorted(players.items())},
            'median_midi_steady': round(float(np.median([n['midi'] for n in v if n['steady'] is not None])), 1)
            if stv else None,
            'regions': sorted(set(n['region'] for n in v))}
    regions_out = {rid: {p: {'steady': stats(d['steady']), 'peak': stats(d['peak']), 'tracks': d['tracks']}
                         for p, d in parts.items()} for rid, parts in per_region.items()}

    def rel(measure, mark, ref):
        """each part's median minus the reference's (mean of the string sections or the oboe), same measure"""
        vals = {p: levels.get(p, {}).get(mark, {}).get(measure) for p in PARTS}
        vals = {p: v['median'] for p, v in vals.items() if v}
        if ref == 'strings':
            rv = [vals[p] for p in S if p in vals]
            if len(rv) < 3:
                return None
            r = float(np.mean(rv))
        else:
            if ref not in vals:
                return None
            r = vals[ref]
        return {p: round(v - r, 2) for p, v in vals.items()}

    comparison = {}
    for mark in MARKINGS:
        comparison[mark] = {m: {'re_strings': rel(m, mark, 'strings'), 're_oboe': rel(m, mark, 'ob')}
                            for m in ('steady', 'peak')}

    csvm = csv_medians(csv_path)

    def rule_d(table):
        out = {}
        for mark, k in RULE_D_STEP.items():
            v = {}
            for p, lab in table.items():
                d = csvm[lab]
                v[p] = round(d['pp'] + k * (d['ff'] - d['pp']) / 7, 2)
            strings = float(np.mean([v[p] for p in S]))
            out[mark] = {'level': v, 're_strings': {p: round(x - strings, 2) for p, x in v.items()},
                         're_oboe': {p: round(x - v['ob'], 2) for p, x in v.items()}}
        return out

    dirx = directivity(sofa_zip)
    position = {}
    for p in PARTS:
        if p not in DB_MODERN:
            continue
        for mark, v in levels.get(p, {}).items():
            st = v['steady']
            if not st:
                continue
            lw = st['median'] + 10 * np.log10(4 * np.pi) - dirx[p]['median']
            e = {'power_est_DImedian': round(float(lw), 2),
                 'power_est_range': [round(float(st['median'] + 10 * np.log10(4 * np.pi) - dirx[p]['max']), 2),
                                     round(float(st['median'] + 10 * np.log10(4 * np.pi) - dirx[p]['min']), 2)]}
            for key, tab in (('modern', DB_MODERN), ('historical', DB_HIST)):
                c = csvm[tab[p]]
                e['steps_ppp0_fff7_' + key] = round(float(7 * (lw - c['pp']) / (c['ff'] - c['pp'])), 2)
            position.setdefault(p, {})[mark] = e

    result = {
        'source': {
            'recording': 'Böhm, Ackermann & Weinzierl 2020, JAES 68(12) 977-984, doi:10.17743/jaes.2020.0056; data '
                         'depositonce eacae473-68e1-48fa-8137-3c2cd30919f0 (CC BY 4.0), movement1/2/4.zip excerpts',
            'calibration': 'SPL at 1 m in the microphone direction = dBFS + 143.8 - mic gain - artistic gain '
                           '(paper section 1.5, Table 3)',
            'score': 'first edition full score, Vienna: S. A. Steiner, plate 2570 [1816/17], Beethoven-Haus Bonn '
                     'C 93/30 (digital archive); readings to be confirmed by the owner (not in the repository)',
            'rule_d': 'Weinzierl et al. 2018 database v3, 3_Audio_Features.csv "SoundPower in dB" (_et_ files), '
                      'median over pitches per instrument; ppp = pp, fff = ff, 7 equal steps (real_range.py)',
            'directivity': 'TU Berlin 1a_DirectivitiesSingleTones_SOFA (32 mics), broadband DI over the 32 '
                           'directions per instrument (modern instruments; timpani)'},
        'method': {'steady': 'per note >= %.2f s: energy mean from %.0f ms after onset to %.0f ms before end, dB SPL '
                             'at 1 m' % (STEADY_MIN, ONSET * 1000, END * 1000),
                   'peak': 'per note >= 60 ms: loudest 50 ms, dB SPL at 1 m',
                   'pitch': 'YIN, 10 ms frames; a note ends at a 0.6 semitone change or 45 dB under the track 99th '
                            'percentile'},
        'regions': [{'id': r[0], 'movement': r[1], 'marking': r[2], 'parts': r[3], 't0': r[4], 't1': r[5],
                     'bars': r[6], 'kind': r[7]} for r in REGIONS],
        'levels_spl_1m': levels,
        'by_region': regions_out,
        'comparison_measured': comparison,
        'rule_d_modern': rule_d(DB_MODERN),
        'rule_d_historical': rule_d(DB_HIST),
        'csv_medians': {k: csvm[k] for k in sorted(set(DB_MODERN.values()) | set(DB_HIST.values()))},
        'directivity_index_range': dirx,
        'power_note': 'L_W = SPL(1 m) + 10 lg(4 pi) (= 10.99 dB) - DI; DI in the recording direction unknown: '
                      'see directivity_index_range',
        'position_in_solo_range': position,
        'position_note': 'steady median as sound power with the median DI, placed on rule D\'s scale of the '
                         'instrument (0 = TU pp = ppp, 7 = TU ff = fff; pp 1, p 2, mp 3, mf 4, f 5, ff 6); '
                         'the DI range moves it by (DI range) x 7 / span',
    }
    json.dump(result, open(out_path, 'w'), indent=1, ensure_ascii=False)


if __name__ == '__main__':
    main()
