"""make_json.py <out anechoic_balance.json>: the committed result (derived numbers only, no audio)"""
import sys, json, os, math
R = os.path.expanduser('~/MuseScore/ms3fork/private/research-balance')
W = R + '/work/'
cmp_ = json.load(open(W + 'cmp_all.json'))
summ = json.load(open(W + 'summary.json'))
var = {k: json.load(open(W + 'summary_%s.json' % k)) for k in ('dbmean', 'nounc', 'nomahler', 'mic6')}
D = json.load(open(W + 'directivity.json'))
sys.path.insert(0, R + '/scripts')
MAPV = {'violin': (77.93, 13.8), 'viola': (75.27, 15.9), 'cello': (82.72, 14.1), 'bass': (round(90.87 - 10 * math.log10(8), 2), 14.1),
        'flute': (81.90, 7.5), 'oboe': (85.23, 7.4), 'clarinet': (82.05, 8.3), 'bassoon': (81.16, 10.2),
        'horn': (94.76, 17.7), 'trumpet': (96.59, 8.8), 'trombone': (89.76, 17.0), 'tuba': (95.12, 12.5)}
PIECES = {
    'beethoven7_i': dict(dataset='aalto', work='Beethoven, Symphony No. 7, I, bars 1-53', score='Breitkopf & Haertel Werke (IMSLP #312601)',
                         timing='PHENICX-Anechoic hand-aligned note times (score MIDI 4/4 at quarter = 100: bar b at 2.4 b s) mapped to audio'),
    'bruckner8_ii': dict(dataset='aalto', work='Bruckner, Symphony No. 8, II, bars 1-61', score='Eulenburg miniature score (IMSLP #30702)',
                         timing='PHENICX-Anechoic (score MIDI 3/4 at quarter = 100: bar b at 1.8 (b - 1) s)'),
    'mahler1_iv': dict(dataset='aalto', work='Mahler, Symphony No. 1, IV, bars 1-85', score='Universal Edition (IMSLP #17070)',
                       timing='PHENICX-Anechoic (score MIDI at quarter = 183; bar 1 at 1.47 s, 1.3114 s a bar, fitted to the readings; DTW agrees within 0.2 bar to bar 84); flutes / piccolos and the E-flat clarinet left out (the two flute tracks cannot be matched to the score players)'),
    'beethoven8_i': dict(dataset='tub8', work='Beethoven, Symphony No. 8, I, the open excerpt (74 s, bars 1-74)', score='Breitkopf & Haertel Werke (IMSLP #61814)',
                         timing='DTW of score activity (readings) against audio activity, 0.75 s a bar chosen'),
    'beethoven8_ii': dict(dataset='tub8', work='Beethoven, Symphony No. 8, II, the open excerpt (69.5 s, bars 1-23)', score='Breitkopf & Haertel Werke (IMSLP #61814)',
                          timing='DTW, 2.2 s a bar chosen'),
    'beethoven8_iv': dict(dataset='tub8', work='Beethoven, Symphony No. 8, IV, the open excerpt (79.5 s, bars 0-90)', score='Breitkopf & Haertel Werke (IMSLP #61814)',
                          timing='DTW, 0.55 s a bar chosen'),
}
for k, v in PIECES.items():
    v.update(cmp_['pieces'].get(k, {}))
out = {
    'what': 'In-context sound power of each orchestral part at its written dynamic, against the map\'s implied per-player level '
            '(sectionLevel2\'s Clark & Luce real side + realRange 2\'s steps): research only, not read by any generator or by MuseScore. '
            'docs/RESEARCH.md > 1.5a.',
    'sources': {
        'aalto': 'Paetynen, Pulkki & Lokki 2008, "Anechoic recording system for symphony orchestra", Acta Acustica u. Acustica 94(6) 856-865; '
                 'Lokki, Paetynen & Pulkki 2008, "Recording of anechoic symphony music", Acoustics 08 Paris (mic positions, Table 1). '
                 'research.cs.aalto.fi/acoustics/virtual-acoustics/research/acoustic-measurement-and-analysis/85-anechoic-recordings.html: '
                 '"The recordings are all made with exactly the same gain settings". MP3 192 kb/s, front mic 6 (az 0, el 10.8, 2.30 m '
                 'from the player\'s head), horns also mic 8 (az 144, 1.92 m), bassoons / tuba also mic 5 (az 288, el 52.6, 2.49 m). '
                 'One professional player per instrument (Finnish orchestras) played every part. Licence: "free for academic research"; '
                 'derived numbers only.',
        'phenicx': 'Miron, Carabias-Orti, Bosch, Gomez & Janer 2016, PHENICX-Anechoic note annotations (Zenodo 840025, CC BY 4.0): bar timing only.',
        'tub8': 'Boehm, Ackermann & Weinzierl 2020, "A Multi-channel Anechoic Orchestra Recording of Beethoven\'s Symphony No. 8 op. 93", '
                'J. Audio Eng. Soc. 68(12) 977-984, doi:10.17743/jaes.2020.0056; data doi:10.14279/depositonce-6729.2 (CC BY 4.0), the open '
                'excerpts of movements 1, 2, 4. Orchester Wiener Akademie (period instruments), recorded groupwise; 2 players per wind / '
                'violin / viola part, 1 cello, 1 bass. Every track normalised to 1 m and 10.1 mV/Pa: SPL at 1 m in the mic direction = '
                'dBFS + 94 + 49.8 - mic gain - artistic gain (Table 3; the paper\'s worked example), the engineer\'s artistic gain reversed.',
        'directivity': 'TU Berlin directivity database (Ackermann, Boehm, Brinkmann & Weinzierl 2024, doi:10.14279/depositonce-19858 v3, '
                       'CC BY-NC-SA 4.0: derived numbers only), 1a single tones (ff): DI toward each mic, p^2 interpolated over the 32 '
                       'receivers (inverse squared angle, 4 nearest), mean over notes; modern instruments for Aalto, the classical-era '
                       'ones for TU B8 (Violin/Viola/Cello/Double_bass_historical, Keyed_flute, Classic_oboe, Clarinet_historical, '
                       'Classic_bassoon, Natural_horn, Natural_trumpet).'},
    'method': [
        'Score readings (bar:beat dynamics, hairpins, cresc./dim., accents, technique texts, notes spans per player) by agents from '
        'public-domain scores: anechoic_balance/readings/*.tsv. NOT YET REVIEWED BY THE OWNER (readings rule).',
        'Envelope: 50 ms mean square after a 40 Hz high pass; a frame is active when over the track noise (5th percentile) + 10 dB and '
        'within 30 dB of its 99th percentile.',
        'Steady segment: where the part plays, one written marking, more than 0.25 bar (TU / Mahler 0.4) inside the segment\'s ends, '
        'outside hairpins / cresc. / dim. (+-0.25 bar) and 0.05 bar before .. 0.25 bar after a point accent (sf, fp, ...), outside '
        'stopped horn / mutes / pizz. Level = power mean of its active frames less the noise power; repeated passes of one player '
        'power-averaged.',
        'Lw = level + 20 log10(r) + 11 - DI(mic) (+ the dataset constant: unknown for Aalto, the calibration for TU B8); Aalto horns, '
        'bassoons, tuba: the dB mean of the two microphones\' estimates.',
        'Map\'s per-player Lw at a marking = Lw_mp (sso_cl_section_balance.json, one player) + markingSteps x realSteps / 5 '
        '(ContextLevel: pp -2, p -1, mp 0, mf 1, f 2, ff 3, fff 4).',
        'r = Lw - map Lw per part x marking (frame-weighted energy mean of its segments); rel = r - the strings\' mean r at the same '
        'marking in the same piece (at least two string parts, 10 s). rel < 0: the part plays softer against the strings in context '
        'than the map plays it.',
        'Per instrument and dataset: median over part x marking values, bootstrap 90 % interval; pooled = the mean of the two '
        'datasets\' medians (different players).'],
    'mapPerPlayer': {k: {'lwMp': v[0], 'realSteps': v[1]} for k, v in MAPV.items()},
    'directivityUsed': {'aalto': {k: {m: v[m]['di'] for m in v} for k, v in D['aalto'].items()},
                        'tub8': {k: {'instrument': v['instrument'], 'di': v['di'], 'sdOverNotes': v['sd']} for k, v in D['b8'].items()}},
    'pieces': PIECES,
    'instruments': summ,
    'sensitivity': {'meanOfDb': var['dbmean'], 'withoutUncertainReadings': var['nounc'], 'withoutMahler': var['nomahler'],
                    'aaltoFrontMicOnly': var['mic6']},
    'partMarkings': {i: v['detail'] for i, v in cmp_['instruments'].items()},
}
# TU B8 is calibrated: the absolute residual (Lw - the map's per-player Lw) too
import numpy as np
pm = {}
for r in cmp_['rows']:
    if r['dataset'] == 'tub8':
        pm.setdefault((r['piece'], r['part'], r['inst'], r['marking']), []).append(r)
ab = {}
for k, v in pm.items():
    w = np.array([x['frames'] for x in v], float)
    Lw = 10 * np.log10(np.sum(w * 10 ** (np.array([x['Lw'] for x in v]) / 10)) / w.sum())
    ab.setdefault(k[2], []).append(Lw - v[0]['mapLw'])
out['tub8Absolute'] = {i: {'medianDb': round(float(np.median(x)), 2), 'iqr': [round(float(q), 2) for q in np.percentile(x, [25, 75])],
                           'partMarkings': len(x)} for i, x in ab.items()}
json.dump(out, open(sys.argv[1], 'w'), indent=1, ensure_ascii=False)
print('written', sys.argv[1])
