#!/usr/bin/env python3
"""The timpani's level at written markings, from the Spheres dataset's Romeo and Juliet, on Clark & Luce's scale
(section_balance_cl.py reads the output for the Timpani's sectionLevel2; docs/BALANCE_LOG.md › Iteration 10).

    python3 -I spheres_timpani.py <Spheres Tchaikovsky folder> <sso_cl_section_balance.json> <out spheres_timpani.json>
                                  [<mics, default MainL,MainC,MainR>]

Source: The Spheres dataset (Colibri Ensemble, Pescara; arXiv 2511.21247, Zenodo 17347681, CC BY-SA 4.0), the
multichannel set's Tchaikovsky folder: Romeo and Juliet, overture-fantasia (1880 version), every part recorded alone
(one player per wind part, the strings by section), 23 microphones recording every take; the audio is read from a local
copy and never stored in the repository (derived numbers only). The markings are read from the first edition of the 1880
version's full score (Berlin: Bote & G. Bock, plate 9325, 1881; Sibley Music Library, http://hdl.handle.net/1802/24924,
public domain): review page https://claude.ai/artifact/R2bYpawLeH8MU2585HRoXj (crops beside each reading).

Measure: the main microphones, power summed (the earlier Spheres findings' method; the ones given). A sustained part: the
mean power over its playing 50 ms frames (within 20 dB of the window's 95th percentile), dB re FS^2; the timpani: the
energy of a passage's hits (the passage's whole energy, rings included, over the number of written hits), dB re FS^2 s,
the measure iteration 6 matched for TU Berlin's decaying tones and SSO's (sso_percussion_levels.json cumE).
Position: each part less its seat's session-1 sweep level at the same microphones (loudspeaker at the seat; SWEEP),
re the strings' mean seat, as the findings' "pos" correction (by it the timpani sit 4.0 dB under the strings, the horns
4.3). Both recording sessions as measured (hand claps: no gain change between them).
Scale: per calibration part, K = Clark & Luce's one-player sound power at the written marking (sso_cl_section_balance.json
`lw` of the patch named, one player) less the part's position-corrected level per player (the strings' lines: the
section's players as the findings assumed, Violin 3, Viola 4, Cello 3, each line recorded by the whole section); K = the
median over the eight string lines at mf (sectionLevel2 centres the strings; dB re 1 pW / FS^2), its RMS the scale's
uncertainty. The wind chorale's parts at pp (one player each) are checks only, where their stems could be fetched (2026-10-15:
Zenodo stopped answering after Flute 1; the main microphone L alone was complete for every part used). The timpani's
sound energy per hit at a marking (dB re 1 pW s) = its position-corrected energy per hit + K.
"""
import json
import math
import statistics
import sys

import numpy as np
import soundfile as sf

FS = 48000
MICS = ['MainL', 'MainC', 'MainR']
ALL_MICS = ('MainL', 'MainC', 'MainR')
# session-1 sweep, loudspeaker at the seat, level at Main L / C / R (dB re FS^2), from the ClapsAndSweeps set's
# "Session 1 - Sweeps - Instrument Position - <seat>" chapters (the earlier Spheres findings' sweep_levels.json)
SWEEP = {'Vln 1': (-34.65, -34.87, -34.32), 'Vln 2': (-35.66, -35.62, -35.07), 'Vla': (-35.73, -35.74, -35.06),
         'Vcl': (-35.38, -35.37, -34.52), 'DBass 1': (-35.04, -35.04, -34.55), 'DBass 2': (-34.64, -34.91, -34.56),
         'Flute': (-35.52, -35.69, -35.53), 'Oboe': (-36.48, -36.27, -35.44), 'Clarinet': (-34.84, -35.64, -35.81),
         'Bassoon': (-36.27, -36.54, -36.42), 'Horns': (-39.66, -39.56, -38.55), 'Timpani': (-38.89, -39.10, -38.94)}
STRING_SEATS = ['Vln 1', 'Vln 2', 'Vla', 'Vcl']
BASS_SEATS = ['DBass 1', 'DBass 2']


def sweep(seat):
    return 10 * math.log10(sum(10 ** (SWEEP[seat][ALL_MICS.index(m)] / 10) for m in MICS))


# coda (letter U, Moderato assai, p. 106-108): bar k starts at CODA0 + (k - 1) x BAR (the timpani's first hit of bar 1
# and its quarter on bar 10, beat 1)
CODA0, BAR = 1162.07, (1216.15 - 1162.07) / 9


def coda(k):
    return CODA0 + (k - 1) * BAR


# the timpani's passages: (t0, t1, written hits, marking, where)
TIMPANI = [
    (394.5, 407.9, 8, 'f', 'Allegro giusto, its first 4 bars (p. 17-18): B, E E E (sixteenths), B, B, B B (eighths); f at '
                           'the first note'),
    (407.9, 420.0, 3, 'f', 'p. 19 last bar, p. 20 bars 1-2: one B eighth each, f at the first'),
    (coda(1) - 0.27, coda(10) + 5.85, 77, 'p', 'coda (U, Moderato assai, p. 106-107): 12 triplet eighths, 8 bars of 4 '
                                              'dotted-eighth + sixteenth pairs, a quarter; p at the first note'),
]
# calibration parts: (stem, seat, players, Clark & Luce patch, marking, t0, t1, where)
CAL = [(s, seat, n, patch, 'mf', coda(2) + 1.5, coda(9) - 1.0, 'coda bars 2-8 (p. 106-107): two phrases, mf at each entry')
       for s, seat, n, patch in (('Violin_1_1', 'Vln 1', 3, 'Solo Violin 1'), ('Violin_1_2', 'Vln 1', 3, 'Solo Violin 1'),
                                 ('Violin_2_1', 'Vln 2', 3, 'Solo Violin 1'), ('Violin_2_2', 'Vln 2', 3, 'Solo Violin 1'),
                                 ('Viola_1', 'Vla', 4, 'Solo Viola'), ('Viola_2', 'Vla', 4, 'Solo Viola'),
                                 ('Cello_1', 'Vcl', 3, 'Solo Cello'), ('Cello_2', 'Vcl', 3, 'Solo Cello'))]
CAL += [(s, seat, 1, patch, 'pp', coda(10) + 1.5, coda(15), 'coda bars 10-14 (p. 107 last bar, p. 108 bars 1-4): the '
                                                            'wind chorale, pp, before its hairpins')
        for s, seat, patch in (('Flute_1', 'Flute', 'Flute Solo'), ('Flute_2', 'Flute', 'Flute Solo'),
                               ('Oboe_1', 'Oboe', 'Oboe Solo'), ('Oboe_2', 'Oboe', 'Oboe Solo'),
                               ('coranglais', 'Oboe', 'Cor Anglais'),
                               ('Clarinet_1', 'Clarinet', 'Clarinet Solo'), ('Clarinet_2', 'Clarinet', 'Clarinet Solo'),
                               ('Bassoon_1', 'Bassoon', 'Bassoon Solo'), ('Bassoon_2', 'Bassoon', 'Bassoon Solo'),
                               ('Horn_1', 'Horns', 'Horn Solo'), ('Horn_2', 'Horns', 'Horn Solo'),
                               ('Horn_3', 'Horns', 'Horn Solo'), ('Horn_4', 'Horns', 'Horn Solo'))]


def power(folder, stem, t0, t1):
    a, b = int(t0 * FS), int(t1 * FS)
    return sum(sf.read('%s/%s/%s.flac' % (folder, m, stem), start=a, stop=b, dtype='float64')[0] ** 2 for m in MICS)


def playingLevel(p):
    hop = int(0.05 * FS)
    n = len(p) // hop
    f = p[:n * hop].reshape(n, hop).mean(1)
    db = 10 * np.log10(f + 1e-30)
    on = db > np.percentile(db, 95) - 20
    return 10 * math.log10(f[on].mean()), float(on.mean())


def main():
    folder, clPath, outPath = sys.argv[1:4]
    if len(sys.argv) > 4:
        MICS[:] = sys.argv[4].split(',')
    cl = json.load(open(clPath, encoding='utf-8'))['patches']
    ref = statistics.mean([sweep(s) for s in STRING_SEATS] + [statistics.mean(sweep(s) for s in BASS_SEATS)])
    pos = lambda seat: sweep(seat) - ref
    cal = []
    for stem, seat, n, patch, mark, t0, t1, where in CAL:
        try:
            level, playing = playingLevel(power(folder, stem, t0, t1))
        except (OSError, RuntimeError) as e:
            print('%-12s not available (%s)' % (stem, str(e)[:60]))
            continue
        if playing < 0.3:
            print('%-12s silent in its window (playing %.2f): left out' % (stem, playing))
            continue
        perPlayer = level - pos(seat) - 10 * math.log10(n)
        k = cl[patch]['lw'][mark] - perPlayer
        cal.append({'stem': stem, 'seat': seat, 'players': n, 'clarkLuce': patch, 'marking': mark,
                    'window': [round(t0, 2), round(t1, 2)], 'where': where, 'levelDb': round(level, 2),
                    'playing': round(playing, 2), 'positionDb': round(pos(seat), 2),
                    'perPlayerDb': round(perPlayer, 2), 'clLw': cl[patch]['lw'][mark], 'k': round(k, 2)})
    # the scale from the strings (sectionLevel2 centres them); a wind part, where its stem was fetched, is a check only
    for c in cal:
        c['role'] = 'scale' if c['marking'] == 'mf' else 'check'
    ks = [c['k'] for c in cal if c['role'] == 'scale']
    K = statistics.median(ks)
    rms = (sum((x - K) ** 2 for x in ks) / len(ks)) ** 0.5
    byMark = {m: statistics.median(c['k'] for c in cal if c['marking'] == m) for m in sorted({c['marking'] for c in cal})}
    tim = []
    for t0, t1, hits, mark, where in TIMPANI:
        p = power(folder, 'Timpani', t0, t1)
        e = 10 * math.log10(p.sum() / FS) - 10 * math.log10(hits)
        tim.append({'window': [round(t0, 2), round(t1, 2)], 'hits': hits, 'marking': mark, 'where': where,
                    'energyPerHitDb': round(e, 2), 'positionDb': round(pos('Timpani'), 2),
                    'soundEnergyDb': round(e - pos('Timpani') + K, 2)})
    marks = {}
    for m in ('p', 'f'):
        xs = [t['soundEnergyDb'] for t in tim if t['marking'] == m]
        marks[m] = round(10 * math.log10(statistics.mean(10 ** (x / 10) for x in xs)), 2)
    out = {'method': __doc__.strip().split('\n\n', 1)[1], 'mics': MICS, 'k': round(K, 2), 'kRms': round(rms, 2),
           'kByMarking': {m: round(v, 2) for m, v in byMark.items()}, 'calibration': cal, 'timpani': tim,
           'soundEnergyDb': marks, 'fMinusP': round(marks['f'] - marks['p'], 2)}
    with open(outPath, 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1)
        fh.write('\n')
    for c in cal:
        print('%-12s %-3s level %7.2f playing %.2f pos %+5.2f per player %7.2f  CL %6.2f  K %7.2f' % (
            c['stem'], c['marking'], c['levelDb'], c['playing'], c['positionDb'], c['perPlayerDb'], c['clLw'], c['k']))
    print('K %.2f (RMS %.2f over %d parts; by marking %s), mics %s' % (K, rms, len(ks), out['kByMarking'], ','.join(MICS)))
    for t in tim:
        print('timpani %s %3d hits: %.2f dB re FS^2 s per hit -> %.2f dB re 1 pW s' % (
            t['marking'], t['hits'], t['energyPerHitDb'], t['soundEnergyDb']))
    print('timpani p %.2f, f %.2f: f - p %.2f dB' % (marks['p'], marks['f'], out['fMinusP']))


if __name__ == '__main__':
    main()
