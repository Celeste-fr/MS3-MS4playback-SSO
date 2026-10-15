#!/usr/bin/env python3
"""Which string sections SSO's Strings Ensemble plays at each key, and how loud each is in it (2026-10-15,
docs/BALANCE_LOG.md › Iteration 9): the input of its section level (section_balance_cl.py ENSEMBLE).

    strings_ensemble.py events <folder>
        kthost event files (<tag>.txt) and notes files (<tag>.notes.json) for the Windows VM: s_ens, s_vn1, s_vn2,
        s_vla, s_vc, s_cb (Strings Ensemble and the five sections)
    strings_ensemble.py analyze <wav folder> <notes folder> -o tools/soundlibraries/sso_strings_ensemble.json

Renders (Windows test VM, kthost: Kontakt Player 8.13.1 + SSO, one process per patch, offline, 44.1 kHz stereo float;
the setups MuseScore made, unedited: the five sections' from MuseScore's setups folder, as sso_cc1_curves.json,
Strings Ensemble's from the rest check's): each patch's All techniques Long (UACC 1 by CC32), CC11 127, CC1 = velocity 64,
a warm-up note, then every even key of its range (Strings Ensemble 24 .. 96, its sample zones every 2 semitones;
Violins 1 / 2 56 .. 96, Violas 48 .. 90, Celli 36 .. 80, Basses 24 .. 54), 4 notes each (SSO alternates two round robins),
3.0 s long, 1.5 s apart (sso_cc1_sweep.make_events with --cc 64 --step 2). The sections' renders reproduce
sso_cc1_curves.json's loudDb at the keys both have to 0.01 dB (the same setups, a deterministic render).

Identification: a section's own recording in the ensemble. Strings Ensemble's samples turned out to be the sections'
own samples mixed (Spitfire's description: "a set of blended six-octave Ensemble patches", Sound On Sound's review,
D. Stewart, August 2024; the manual lists the patch, not its make-up). Per key, the normalised cross-correlation r (max
|r| over lags +-250 ms, mean of the two channels) of the ensemble's sustain (0.2 .. 2.8 s after the note-on) with each
section's at the same key, the best of the 2 x 2 pairings of the two round robins; the same against each section an
octave up and down (octave doublings). Two different recordings of a sustained note correlate too (the ensemble's own
two round robins, "ensRr": 0.12 .. 0.36 from key 48, up to 0.6 at the lowest keys, where a low sustained note is close
to periodic), so a section counts as present when r > max(PRESENT_R, ensRr + PRESENT_OVER) and its r^2 (its share of
the ensemble's energy, the parts being uncorrelated recordings) is at least PRESENT_SHARE; sumR2 (the present parts'
shares added) near 1 says nothing else is in the mix. mixDb: a present part's level in the ensemble against its own
patch, 10 log10(r^2 E_ens / E_section) over the same sustain window of the paired takes. At the lowest keys (Basses,
non-vibrato, close to periodic) two different recordings correlate as well as the same one, so r can't tell there.
Spectral fit (every key): the power at each harmonic k f0 < 12 kHz (+-max(2.5 bins, 1.5 %), Welch 8192-point Hann, the
sustain 1.5 .. 3.0 s, the 4 takes' mean), each channel, of the ensemble = a non-negative sum of the sections' at the same
key (NNLS, each harmonic weighted by 1 / the ensemble's power: a relative error): gainDb per section, fitErrDb (dB rms
over harmonics of fit / ensemble; the ensemble's own two round robins differ by 0.5 .. 2.3 dB); octaves: no section an
octave down (the power at f0 / 2 x 1, 3, 5 is 27 .. 51 dB under f0 x 1, 2, 3: subharmDb) and none an octave up (r of the
sections an octave up <= 0.31 above key 34). weightDb: a section's gainDb less its plateau (the median gainDb over the
keys where it is fully in, PLATEAU: its level in the mix there), capped at 0: how far it is faded at a key; a section
under WEIGHT_MIN dB is left out.
Levels per key: sso_cc1_sweep.measure (loudDb, the section balance's measure; rmsDb), the median of the 4 takes.
"""
import argparse
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

PATCHES = {'ens': ('Strings Ensemble', 24, 96), 'vn1': ('Violins 1', 56, 96), 'vn2': ('Violins 2', 56, 96),
           'vla': ('Violas', 48, 90), 'vc': ('Celli', 36, 80), 'cb': ('Basses', 24, 54)}
SECTIONS = ['vn1', 'vn2', 'vla', 'vc', 'cb']
PRESENT_R = 0.3
PRESENT_OVER = 0.1
PRESENT_SHARE = 0.05
# the keys where a section is fully in the mix (its gainDb flat within about 1 dB; outside them it fades)
PLATEAU = {'Violins 1': (62, 96), 'Violins 2': (62, 96), 'Violas': (56, 82), 'Celli': (36, 68), 'Basses': (24, 42)}
WEIGHT_MIN = -25.0
WINDOW = (0.2, 2.8)
LAG_S = 0.25


def events(folder):
    import sso_cc1_sweep as S
    os.makedirs(folder, exist_ok=True)
    for tag, (name, lo, hi) in PATCHES.items():
        text, notes = S.make_events(lo, hi, step=2, ccs=[64], repeats=4)
        with open(os.path.join(folder, 's_%s.txt' % tag), 'w', newline='\r\n') as f:
            f.write(text)
        with open(os.path.join(folder, 's_%s.notes.json' % tag), 'w') as f:
            json.dump(notes, f)
        print(name, len(notes['notes']), 'notes')


def analyze(wavs, notesDir, out):
    import numpy as np
    import soundfile as sf
    import pyloudnorm
    from scipy.signal import fftconvolve
    import sso_cc1_sweep as S
    x, notes, levels = {}, {}, {}
    sr = None
    for tag in PATCHES:
        x[tag], sr = sf.read(os.path.join(wavs, 's_%s.wav' % tag), dtype='float32', always_2d=True)
        notes[tag] = json.load(open(os.path.join(notesDir, 's_%s.notes.json' % tag)))
        meter = pyloudnorm.Meter(sr)
        per = {}
        for n in notes[tag]['notes']:
            per.setdefault(n['pitch'], []).append(S.measure(x[tag], sr, n['on'], n['off'], meter, sones=False))
        levels[tag] = {p: {k: round(statistics.median(m[k] for m in ms), 2) for k in ('loudDb', 'rmsDb')}
                       for p, ms in per.items()}

    def takes(tag, p):
        ts = [n for n in notes[tag]['notes'] if n['pitch'] == p][:2]
        return [x[tag][int((n['on'] + WINDOW[0]) * sr):int((n['on'] + WINDOW[1]) * sr)].astype('float64') for n in ts]

    def energy(t):
        return float((t ** 2).mean())

    def r(u, v):
        L = int(LAG_S * sr)
        res = []
        for c in range(2):
            a, b = u[:, c], v[:, c]
            cc = fftconvolve(a, b[::-1], mode='full')[len(b) - 1 - L:len(b) + L]
            j = int(np.abs(cc).argmax())
            res.append((abs(float(cc[j])) / float(np.sqrt((a ** 2).sum() * (b ** 2).sum())), (j - L) / sr))
        return float(np.mean([v for v, _ in res])), [round(v, 3) for v, _ in res], [round(l * 1000, 1) for _, l in res]

    from scipy.signal import welch
    from scipy.optimize import nnls
    spectra = {}
    for tag in PATCHES:
        for p in levels[tag]:
            ts = [n for n in notes[tag]['notes'] if n['pitch'] == p]
            segs = [x[tag][int((n['on'] + 1.5) * sr):int((n['on'] + 3.0) * sr)].astype('float64') for n in ts]
            f, pl = welch([sg[:, 0] for sg in segs], sr, nperseg=8192, axis=-1)
            _, pr = welch([sg[:, 1] for sg in segs], sr, nperseg=8192, axis=-1)
            spectra[(tag, p)] = (pl.mean(axis=0), pr.mean(axis=0))

    def harmonics(tag, p, f0):
        PL, PR = spectra[(tag, p)]
        ids = [np.where(np.abs(f - k * f0) <= max(2.5 * (f[1] - f[0]), 0.015 * k * f0))[0]
               for k in range(1, 200) if k * f0 < 12000]
        return np.concatenate([[PL[i].sum() for i in ids], [PR[i].sum() for i in ids]])

    def subharm(p, f0):
        P = spectra[('ens', p)][0] + spectra[('ens', p)][1]
        around = lambda v: P[(f > v * 0.985) & (f < v * 1.015)].sum()
        s, h = sum(around(f0 / 2 * m) for m in (1, 3, 5)), sum(around(f0 * m) for m in (1, 2, 3))
        return round(float(10 * np.log10(max(s, 1e-30) / h)), 1) if h > 0 else None

    rows = {}
    for p in sorted(levels['ens']):
        e = takes('ens', p)
        ensRr = r(e[0], e[1])[0]
        row = {'loudDb': levels['ens'][p]['loudDb'], 'rmsDb': levels['ens'][p]['rmsDb'], 'ensRr': round(ensRr, 3),
               'parts': {}, 'octaves': {}}
        for tag in SECTIONS:
            name = PATCHES[tag][0]
            for sh in (0, 12, -12):
                s = takes(tag, p + sh)
                if len(s) < 2:
                    continue
                best = max(((r(e[i], s[j]), i, j) for i in range(2) for j in range(2)), key=lambda b: b[0][0])
                (rv, rLR, lag), i, j = best
                if sh:
                    row['octaves']['%s %+d' % (name, sh)] = round(rv, 3)
                    continue
                share = rv * rv
                present = rv > max(PRESENT_R, ensRr + PRESENT_OVER) and share >= PRESENT_SHARE
                row['parts'][name] = {'r': round(rv, 3), 'rLR': rLR, 'lagMs': lag, 'pair': [i, j], 'share': round(share, 3),
                                      'present': present, 'loudDb': levels[tag][p]['loudDb'], 'rmsDb': levels[tag][p]['rmsDb'],
                                      'mixDb': round(10 * np.log10(share * energy(e[i]) / energy(s[j])), 2)}
        f0 = 440 * 2 ** ((p - 69) / 12)
        y = harmonics('ens', p, f0)
        cand = [t for t in SECTIONS if (t, p) in spectra]
        w = 1 / np.maximum(y, 1e-30)
        A = np.array([harmonics(t, p, f0) * w for t in cand]).T
        g, _ = nnls(A, y * w)
        fit = A @ g / w
        ok = y > y.max() * 1e-6
        row['fitErrDb'] = round(float(np.sqrt(np.mean((10 * np.log10(np.maximum(fit[ok], 1e-30) / y[ok])) ** 2))), 2)
        row['subharmDb'] = subharm(p, f0)
        for t, gi in zip(cand, g):
            row['parts'][PATCHES[t][0]]['gainDb'] = round(float(10 * np.log10(gi)), 2) if gi > 0 else None
        row['present'] = [n for n, v in row['parts'].items() if v['present']]
        row['sumR2'] = round(sum(v['share'] for v in row['parts'].values() if v['present']), 3)
        row['maxOctaveR'] = round(max(row['octaves'].values()), 3) if row['octaves'] else None
        rows[str(p)] = row
        print(p, 'ens %.1f rr %.2f' % (row['loudDb'], ensRr),
              ' '.join('%s %.2f%s' % (n, v['r'], '*' if v['present'] else '') for n, v in row['parts'].items()),
              '| sumR2 %.2f, octaves max %s' % (row['sumR2'], row['maxOctaveR']))
    plateau = {}
    for name, (lo, hi) in PLATEAU.items():
        gs = [rows[str(p)]['parts'][name]['gainDb'] for p in range(lo, hi + 1, 2)]
        plateau[name] = {'keys': [lo, hi], 'gainDb': round(statistics.median(gs), 2),
                         'spreadDb': round(max(gs) - min(gs), 2)}
    for p, row in rows.items():
        row['weights'] = {}
        for name, v in row['parts'].items():
            if v.get('gainDb') is None:
                continue
            wdb = min(0.0, v['gainDb'] - plateau[name]['gainDb'])
            if wdb >= WEIGHT_MIN:
                row['weights'][name] = round(wdb, 2)
        print(p, 'fit %.2f dB, sub %s' % (row['fitErrDb'], row['subharmDb']), row['weights'])
    print('plateaus', plateau)
    res = {'method': __doc__.strip().split('\n\n', 1)[1],
           'plateau': plateau,
           'rule': {'weightMinDb': WEIGHT_MIN, 'presentR': PRESENT_R, 'presentOver': PRESENT_OVER, 'presentShare': PRESENT_SHARE,
                    'windowS': list(WINDOW), 'lagS': LAG_S},
           'ranges': {v[0]: [v[1], v[2]] for v in PATCHES.values()},
           'keys': rows}
    with open(out, 'w') as f:
        json.dump(res, f, indent=1)
        f.write('\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('folder')
    a = sub.add_parser('analyze')
    a.add_argument('wavs')
    a.add_argument('notes')
    a.add_argument('-o', '--out', required=True)
    args = ap.parse_args()
    if args.cmd == 'events':
        events(args.folder)
    else:
        analyze(args.wavs, args.notes, args.out)


if __name__ == '__main__':
    main()
