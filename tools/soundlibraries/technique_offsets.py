#!/usr/bin/env python3
# The technique offsets of [levels] calibrated 2, option B' (the owner, 2026-10-15: "if the data clearly point to b',
# then b' it is"): a velocity technique of the five string sections plays as loud as the part's held Long at the same
# marking (calibrated 1's match) plus D dB, D from real players where there is data (map <Articulation heldOffset>,
# written by gen_spitfire_sso.py from technique_offsets.json, which this script writes).
#
#   python3 technique_offsets.py [spheres_k550_staccato.json] [technique_offsets.json]
#
# Inputs
# - spheres_k550_staccato.json: the Spheres dataset (Colibri Ensemble; arXiv 2511.21247, Zenodo 17347681), Mozart
#   K. 550, each section's own close stem: every note's perceived level (short-term loudness, ITU-R BS.1771-style
#   STL maximum, dB) in a staccato passage and the held notes next to it at the same marking (f): Violins 1 bars
#   30-33's dotted quarters against bar 29's single dotted half, Celli / Basses bars 191-197 against bar 198; and
#   quarter notes against whole notes at p (bars 1-9 / 10-15, Celli and Basses). Re-measured 2026-10-15 with the
#   readings the owner confirmed or corrected that day (review page https://claude.ai/artifact/8FoTWaNAb3Ekf2zFEDqWg8):
#   bar 28 is a double stop in both violin sections and bar 29 one in Violins 2, all left out, so Violins 2 has no
#   single held note at f and is not measured; a segment 15 dB under its passage's median is dropped as a tail or split
#   note. (The first readings, with bar 28 as Violins 1's held note, gave -4.2 [-5.8, -2.5] over four sections.)
# - Meyer 2009 (Acoustics and the Performance of Music, 5th ed., ch. 3): pizzicato minus arco sound power at pp / ff
#   per instrument (the numbers only).
#
# Method (bowed shorts): per section d = median(staccato) - median(held); pooled = the mean of the sections' d (a
# section is the unit: its own players, stem and room position); its interval by a two-level bootstrap (sections
# resampled, then each section's notes), 90 %, 20000 draws, seed 1. Per section vs pooled, by data quality: each
# measured section has one held note (Violins 1 its bar 29, played twice; Celli and Basses one: their own intervals
# hold only the staccatos' spread, not the held note's), and one held note varied by 2.6-4.2 dB (SD, the first
# readings' Violins 2 / 1). The corrected readings' sections differ by 3.1 dB (SD: Violins 1 -0.4, Celli -6.7, Basses
# -3.1), about one held note's own error, so with one held note each the difference between sections can't be told
# from it: one pooled value for all five, its interval carrying that spread; Violins 2 and Violas (not measured) take
# it: an assumption.
import json, sys, random, statistics

HERE = __import__('os').path.dirname(__file__) or '.'
SECTIONS = {   # the dataset's stem folder -> (map section, staccato passage, held passage)
    'Violin_1': ('Violins 1', 'A stacc f b30-33 (dotted quarters)', 'A long f b29 (single dotted half)'),
    'Cello': ('Celli', 'C stacc8 f b191-197', 'C whole f b198'),
    'Bass_1': ('Basses', 'C stacc8 f b191-197', 'C whole f b198'),
}
QUARTERS = {'Cello': 'Celli', 'Bass_1': 'Basses'}   # B quarter p b1-9 against B whole p b10-15
QUARTER, WHOLE = 'B quarter p b1-9', 'B whole p b10-15'
MAP_SECTIONS = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
# Meyer 2009 ch. 3: pizzicato minus arco, dB of sound power, at pp / ff (as cited in the B validation, 2026-10-15)
MEYER_PIZZ = {'Violins 1': (-7, -9), 'Violins 2': (-7, -9), 'Violas': (-9, -5), 'Celli': (-12, -8), 'Basses': (-6, -7)}
PP_CC, FF_CC = 32, 112   # Ms4::expressionLevel of pp (3750) and ff (6250)
# A fixed value the map would play instead of the pooled one (written once into technique_offsets.json's "staccato" >
# "use", referred to from every bowed short, "@staccato"); None: the pooled value. -4.2 until the owner's corrected
# K. 550 readings arrived (2026-10-15).
STACCATO_PLACEHOLDER = None


def kept(notes):
    v = [n['stl'] for n in notes]
    med = statistics.median(v)
    return [x for x in v if x >= med - 15]


def pooled(pairs, draws=20000, seed=1):
    rng = random.Random(seed)
    names = list(pairs)
    point = statistics.mean(statistics.median(a) - statistics.median(b) for a, b in pairs.values())
    boots = []
    for _ in range(draws):
        ds = []
        for s in (rng.choice(names) for _ in names):
            a, b = pairs[s]
            ds.append(statistics.median(rng.choices(a, k=len(a))) - statistics.median(rng.choices(b, k=len(b))))
        boots.append(statistics.mean(ds))
    boots.sort()
    return point, boots[int(0.05 * draws)], boots[int(0.95 * draws) - 1]


def own(a, b, draws=2000, seed=1):
    rng = random.Random(seed)
    d = statistics.median(a) - statistics.median(b)
    boots = sorted(statistics.median(rng.choices(a, k=len(a))) - statistics.median(rng.choices(b, k=len(b)))
                   for _ in range(draws))
    return d, boots[int(0.05 * draws)], boots[int(0.95 * draws) - 1]


def main(src, dst):
    data = json.load(open(src))['sections']
    stacc, quart, per = {}, {}, {}
    for stem, (sec, s, h) in SECTIONS.items():
        a, b = kept(data[stem][s]), kept(data[stem][h])
        stacc[sec] = (a, b)
        d, lo, hi = own(a, b)
        per[sec] = {'d': round(d, 2), 'interval90': [round(lo, 2), round(hi, 2)], 'nShort': len(a), 'nHeld': len(b)}
    for stem, sec in QUARTERS.items():
        quart[sec] = (kept(data[stem][QUARTER]), kept(data[stem][WHOLE]))
    st, st_lo, st_hi = pooled(stacc)
    qu, qu_lo, qu_hi = pooled(quart)
    r1 = lambda x: round(x, 1)
    out = {
        'about': 'D for [levels] calibrated 2 (option B\'): dB a velocity technique plays over the part\'s held Long '
                 'at the same marking (map <Articulation heldOffset>, gen_spitfire_sso.py). Written by '
                 'technique_offsets.py; a technique of the five string sections not listed here keeps SSO\'s own '
                 'loud-end difference (SoundLib::loudEndDb): an assumption, no real-player data.',
        'staccato': {
            'method': 'per section median(staccato) - median(held) at f, perceived (STL); pooled = mean of the '
                      'sections; 90 % interval by a two-level bootstrap (sections, then notes), 20000 draws',
            'source': 'Spheres dataset, Mozart K. 550 (spheres_k550_staccato.json)',
            'sections': per,
            'pooled': {'d': r1(st), 'interval90': [r1(st_lo), r1(st_hi)], 'sections': len(stacc)},
            # the one value every bowed short plays ("@staccato" below)
            'use': r1(st) if STACCATO_PLACEHOLDER is None else STACCATO_PLACEHOLDER,
            'placeholder': None if STACCATO_PLACEHOLDER is None else
                           'a fixed value, not the pooled measurement',
            'choice': 'pooled for all five sections: one held note per measured section (Violins 1, Celli, Basses); '
                      'the sections differ by 3.1 dB (SD), about one held note\'s own variation (2.6-4.2 dB SD, the '
                      'first readings), so one held note each can\'t separate them; '
                      'Violins 2 (no single held note at f in bars 28-33: double stops) and Violas not measured',
            'assumptions': ['the same D at every marking (measured at f only; untested at p)',
                            'Violins 2 and Violas take the pooled value (not measured)'],
        },
        'quarters': {
            'method': 'as staccato: quarter notes (bars 1-9) against whole notes (bars 10-15) at p, Celli and Basses',
            'source': 'Spheres dataset, Mozart K. 550 (spheres_k550_staccato.json)',
            'pooled': {'d': r1(qu), 'interval90': [r1(qu_lo), r1(qu_hi)], 'sections': len(quart)},
            'assumptions': ['Short 1.0 (a full-length bowed short) plays as these unaccented quarters',
                            'measured at p only; Violins and Violas take the Celli / Basses value'],
        },
        'pizzicato': {
            'method': 'pizzicato minus arco at pp and ff, linear in the written dynamic\'s CC between (pp CC 32, '
                      'ff CC 112), constant outside',
            'source': 'Meyer 2009, Acoustics and the Performance of Music, ch. 3 (sound power, not perceived: '
                      'an uncertainty of several dB)',
            'sections': {s: {'pp': MEYER_PIZZ[s][0], 'ff': MEYER_PIZZ[s][1]} for s in MAP_SECTIONS},
            'assumptions': ['Violins 2 take the violin\'s values'],
            'corroboration': {
                'source': 'University of Iowa Musical Instrument Samples (5 sessions, 5 players: violin, viola, double '
                          'bass): each pizzicato note\'s peak loudness (ISO 532-1) against a held arco note at the same '
                          'marking; measured 2026-10-15',
                'pizzMinusArco': {'pp': [-7.3, -8.7, -6.3], 'mf': [-6.3, -12.1, -2.3], 'ff': [-4.0, -8.4, -2.6]},
                'note': 'median and range over the sessions; only pp agrees within 3 dB across sessions; independent '
                        'corroboration of Meyer, uncertainty about +-3-4 dB; not used for the values',
            },
        },
        'zero': {
            'Basses': ['Pizzicato Bartok', 'Col Legno'],
            'why': 'SSO\'s own loud-end difference disagrees by ~10 dB between the perceived and the 50 ms metric '
                   '(2026-10-15 validation): D = 0, SSO\'s balance at the matched level (an assumption)',
        },
        'articulations': {},
    }
    # the map's articulations of the five sections' All techniques patches -> heldOffset
    for s in MAP_SECTIONS:
        a = {}
        for name in ('Spiccato', 'Short 0.5'):
            a[name] = '@staccato'
        a['Short 1.0'] = f'{r1(qu):g}'
        pp, ff = MEYER_PIZZ[s]
        a['Pizzicato'] = f'{PP_CC}:{pp:g} {FF_CC}:{ff:g}'
        if s in out['zero']:
            for name in out['zero'][s]:
                a[name] = '0'
        out['articulations'][s] = a
    json.dump(out, open(dst, 'w'), indent=1)
    print(f"staccato pooled {st:+.2f} [{st_lo:+.2f}, {st_hi:+.2f}]; per section " +
          ', '.join(f"{s} {v['d']:+.2f} [{v['interval90'][0]:+.2f}, {v['interval90'][1]:+.2f}] n={v['nShort']}/{v['nHeld']}"
                    for s, v in per.items()))
    print(f"quarters pooled {qu:+.2f} [{qu_lo:+.2f}, {qu_hi:+.2f}]")


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else f'{HERE}/spheres_k550_staccato.json',
         sys.argv[2] if len(sys.argv) > 2 else f'{HERE}/technique_offsets.json')
