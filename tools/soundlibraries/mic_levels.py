#!/usr/bin/env python3
"""SSO's mics, measured (2026-10-15; the owner: "mic changes then only affect tone and sense of space, never the
balance"): how loud each patch is through each mic alone against its default mix, the mic faders' law and Mic Mix
Distance's, for [levels] micCompensation (libmscore/contextlevel.h › Mics; the map's <Mics> and <Instrument micLevels
micDefault>, written by gen_spitfire_sso.py from sso_mic_levels.json).

    mic_levels.py events <folder>                   kthost event files, notes files and the lists (main, law, mix, combo,
                                                    perf) for the Windows VM (run.cmd: "tag|patch|kthost args" per line)
    mic_levels.py analyze <folder> -o tools/soundlibraries/sso_mic_levels.json
                                                    <folder>: the lists' .json, the notes files and the renders <tag>.wav
    mic_levels.py extra <meas.json> <jobs.json> -o tools/soundlibraries/sso_mic_levels.json
                                                    adds patches measured later (the file's other entries kept): meas.json
                                                    {tag: {pitch: {measure: [takes]}}}, jobs.json [[tag, patch, cfg]]

Added 2026-10-15 (balance-refine, docs/BALANCE_LOG.md): the 15 patches that got a section level in the balance's
iteration 2 (Solo Violin 1 / 2, Solo Viola, Solo Cello: 3 mics; Motif Horns a4 / Trumpets a3 / Trombones a5, Piccolo, Alto /
Bass Flute, Contrabass Clarinet, Contrabass Trombone, Cimbasso Solo, Cimbassi a2, Contrabass Tuba: 4), their pitches the
quartiles of sso_gap_cc1_curves.json's; and Harp and Timpani (3 mics), which decay: their notes 6.0 s apart and their level
the energy of the note and its tail (both channels' mean square from the note-on to 6.0 s after it, "levelMeasure"), the
same window for every mic; Harp at MIDI 42 / 60 / 78, Timpani at keys 50 / 54 / 58 (key 50 sounds D2). The percussion
patches have no mic Controllers in the map (MuseScore never changes their mics): not measured. Strings Ensemble
(2026-10-15, docs/BALANCE_LOG.md › Iteration 9: it got a section level) the same way, 4 mics, at MIDI 42 / 60 / 78.

Method (Windows test VM, kthost: Kontakt 8.13.1 + SSO, one process per render, offline, 44.1 kHz; copies of MuseScore's
resaved setups, the strings' from MuseScore's setups folder, the winds' and brass' from the dynamics check's): SSO's All
techniques Long (UACC 1 by CC32), CC11 127, CC1 64 and velocity 64, a warm-up note, 6 s of silence, then three pitches
(the quartiles of the patch's CC1 sweep pitches: sso_cc1_curves.json / sso_winds_cc1_curves.json), 4 notes each (SSO
alternates two round robins), 3.0 s long, 1.5 s apart (sso_cc1_sweep.py's pattern). Kontakt's parameters "Mic N level"
(N 1-5: Close, Tree, Ambient, Outrigger, Leader; the woodwinds and brass have 1-4) and "Mic Mix Distance" set by kthost
--param-late (after the probe note, so SSO's script has settled; a value v of MuseScore's Controllers is v / 127, as
audio/vst3/librarycontrollers.cpp sends it). Every render starts the transport anew (a new process).
Per note sso_cc1_sweep.measure: rmsDb (the sustain's mean power 1.5-3.0 s after the note-on: the measure used, mics add
as powers), lufs, loudDb; per pitch the median of the 4 takes; a configuration against the patch's default render: the
per-pitch difference, the median over the three pitches.
Configurations per patch: def (as set up); a<i> (mic i alone at 1.0, the others 0); z2 (the Tree at 0, the others as set
up: what the default mix holds besides the Tree); the strings' Performance patches also d5 (every mic but Leader at 0).
Laws: the fader (a mic alone at 120 … 16 against itself at 127: Violins 1 Tree, Flute Solo Ambient, Horn Solo Close and
Tree); Mic Mix Distance 16 … 112 on Violins 1 with two of Close / Tree / Ambient set to 0 after it (a mic value set after
Mic Mix Distance wins, checked: 0 with Ambient 0 is silent), each mic against itself alone at 1.0. The prediction (power
sum, contextlevel.cpp's micLevelDb) checked against renders of mixed settings: Mic Mix Distance alone on Horn Solo and
Flute Solo (their own mics' levels, the law from Violins 1) and five mixes on Violins 1, Horn Solo, Basses.
"""
import argparse
import json
import math
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

MICS = ['Close', 'Tree', 'Ambient', 'Outrigger', 'Leader']
STRINGS = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
FADER_POINTS = (120, 112, 96, 80, 64, 48, 32, 16)
MIX_POINTS = (16, 32, 48, 80, 96, 112)
MEASURES = ['rmsDb', 'lufs', 'loudDb']


def pitches():
    out = {}
    for f in ('sso_cc1_curves.json', 'sso_winds_cc1_curves.json'):
        for k, v in json.load(open(os.path.join(HERE, f)))['sections'].items():
            p = v['pitches']
            out[k] = [p[len(p) // 4], p[len(p) // 2], p[3 * len(p) // 4]]
    for s in STRINGS:
        out[s + ' - Performance'] = out[s]
    return out


def nmics(patch):
    return 5 if patch.split(' - ')[0] in STRINGS else 4


def short(patch):
    # (14 characters, as the renders were named; Tenor Trombone Solo would collide with Tenor Trombones a2)
    return {'Tenor Trombone Solo': 'tenortbsolo'}.get(patch, ''.join(ch for ch in patch.lower() if ch.isalnum())[:14])


def M(**kv):
    idx = {'c': 1, 't': 2, 'a': 3, 'o': 4, 'l': 5}
    return {f'Mic {idx[k]} level': v / 127 for k, v in kv.items()}


def events(ps, repeats=4, note=3.0, gap=1.5, lead=6.0, switch=True):
    # (a Performance patch has no switch (the map's <Switch type="none">): CC32 1 silences it)
    ev = ([(0.5, 'cc', 32, 1)] if switch else []) + [(0.5, 'cc', 11, 127), (0.5, 'cc', 1, 64), (1.0, 'on', ps[1], 64), (1.0 + note, 'off', ps[1], 0)]
    t = 1.0 + note + lead
    notes = []
    for p in ps:
        for r in range(repeats):
            ev += [(round(t, 4), 'on', p, 64), (round(t + note, 4), 'off', p, 0)]
            notes.append({'pitch': p, 'cc': 64, 'velocity': 64, 'repeat': r, 'on': round(t, 4), 'off': round(t + note, 4)})
            t += note + gap
    ev.sort(key=lambda e: (e[0], {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
    return ''.join(f'{e[0]:.4f} {e[1]} {e[2]} {e[3]}\n' for e in ev), {
        'pitches': ps, 'ccs': [64], 'repeats': repeats, 'note': note, 'gap': gap, 'switch': 1, 'warmPitch': ps[1], 'notes': notes}


def jobs():
    P = pitches()
    main = []
    for p in P:
        if ' - ' in p:
            continue
        n = nmics(p)
        s = short(p)
        main.append((f'm_{s}_def', p, {}))
        for i in range(1, n + 1):
            main.append((f'm_{s}_a{i}', p, {f'Mic {j} level': (1.0 if j == i else 0.0) for j in range(1, n + 1)}))
        main.append((f'm_{s}_z2', p, {'Mic 2 level': 0.0}))
    law = [('l_fl_def', 'Flute Solo', {})] + [(f'l_fl_a{x}', 'Flute Solo', {**M(c=0, t=0, o=0), **M(a=x)}) for x in FADER_POINTS]
    law += [('l_hn_def', 'Horn Solo', {})] + [(f'l_hn_c{x}', 'Horn Solo', {**M(t=0, a=0, o=0), **M(c=x)}) for x in FADER_POINTS]
    law += [(f'l_hn_t{x}', 'Horn Solo', {**M(c=0, a=0, o=0), **M(t=x)}) for x in (96, 64, 32)]
    mix = [('mx_v1_def', 'Violins 1', {})]
    for m in MIX_POINTS:
        mm = {'Mic Mix Distance': m / 127}
        mix += [(f'mx_v1_{m}_c', 'Violins 1', {**mm, **M(t=0, a=0)}), (f'mx_v1_{m}_t', 'Violins 1', {**mm, **M(c=0, a=0)}),
                (f'mx_v1_{m}_a', 'Violins 1', {**mm, **M(c=0, t=0)}), (f'mx_v1_{m}', 'Violins 1', mm)]
    mix.append(('mx_v1_0_order', 'Violins 1', {'Mic Mix Distance': 0.0, **M(a=0)}))
    for p, s in (('Horn Solo', 'hn'), ('Flute Solo', 'fl')):
        mix += [(f'mx_{s}_def', p, {})] + [(f'mx_{s}_{m}', p, {'Mic Mix Distance': m / 127}) for m in (32, 96)]
    combo = [('cb_v1_def', 'Violins 1', {}), ('cb_v1_1', 'Violins 1', M(c=100, t=127, a=80)), ('cb_v1_2', 'Violins 1', M(t=90, l=127)),
             ('cb_v1_3', 'Violins 1', {'Mic Mix Distance': 32 / 127, **M(c=80)}),
             ('cb_hn_def', 'Horn Solo', {}), ('cb_hn_1', 'Horn Solo', M(c=127, a=127, o=64)), ('cb_hn_2', 'Horn Solo', M(t=64, a=96)),
             ('cb_cb_def', 'Basses', {}), ('cb_cb_1', 'Basses', M(c=110, t=100, a=110, o=90, l=60))]
    perf = []
    for main_ in STRINGS:
        p = main_ + ' - Performance'
        s = 'p' + short(main_)[:8]
        perf.append((f'm_{s}_def', p, {}))
        for i in range(1, 6):
            perf.append((f'm_{s}_a{i}', p, {f'Mic {j} level': (1.0 if j == i else 0.0) for j in range(1, 6)}))
        perf.append((f'm_{s}_z2', p, {'Mic 2 level': 0.0}))
        perf.append((f'm_{s}_d5', p, {f'Mic {j} level': 0.0 for j in range(1, 5)}))
    # Mic Mix Distance's law, fitted: a mic value set with it is not played (mx_v1_0_order: Ambient 0 after Mic Mix
    # Distance 0 still sounds the Ambient at full), so the totals at each value on five patches whose mics' levels
    # differ, and whether Outrigger / Leader values still play with it
    mix2 = []
    for p, s in (('Horn Solo', 'hn'), ('Flute Solo', 'fl'), ('Celli', 'vc'), ('Basses', 'cb')):
        mix2 += [(f'mx2_{s}_def', p, {})] + [(f'mx2_{s}_{m}', p, {'Mic Mix Distance': m / 127}) for m in MIX_POINTS]
    mix2 += [('mx2_v1_def', 'Violins 1', {}), ('mx2_v1_32_o', 'Violins 1', {'Mic Mix Distance': 32 / 127, **M(o=127)}),
             ('mx2_v1_32_l', 'Violins 1', {'Mic Mix Distance': 32 / 127, **M(l=127)}),
             ('mx2_v1_32_t', 'Violins 1', {'Mic Mix Distance': 32 / 127, **M(t=127)})]
    return {'main': main, 'law': law, 'mix': mix, 'combo': combo, 'perf': perf, 'mix2': mix2}


def write_events(folder):
    P = pitches()
    for name, js in jobs().items():
        lines = []
        for tag, patch, cfg in js:
            text, notes = events(P[patch], switch=' - Performance' not in patch)
            with open(os.path.join(folder, tag + '.txt'), 'w', newline='\r\n') as f:
                f.write(text)
            json.dump(notes, open(os.path.join(folder, tag + '.notes.json'), 'w'))
            # (the readiness probe on the patch's middle pitch: kthost's default 60 is out of the Basses' and the
            # Contrabassoon's range, and it then waits its whole --wait)
            args = f'--probe-pitch {P[patch][1]}' + ((' --param-late ' + ' '.join(f'--param "{k}={v:.6f}"' for k, v in cfg.items())) if cfg else '')
            lines.append(f'{tag}|{patch}|{args}')
        with open(os.path.join(folder, name + '.lst'), 'w', newline='\r\n') as f:
            f.write('\n'.join(lines) + '\n')
        json.dump([[t, p, c] for t, p, c in js], open(os.path.join(folder, name + '.lst.json'), 'w'), indent=0)
        print(name, len(js))


class Renders:
    def __init__(self, folder):
        self.folder = folder
        self.cache_file = os.path.join(folder, 'meas.json')
        self.cache = json.load(open(self.cache_file)) if os.path.exists(self.cache_file) else {}

    def meas(self, tag):
        if tag not in self.cache:
            import soundfile as sf
            import pyloudnorm
            import sso_cc1_sweep as S
            notes = json.load(open(os.path.join(self.folder, tag + '.notes.json')))
            wav = os.path.join(self.folder, 'wav', tag + '.wav')
            if not os.path.exists(wav):
                wav = os.path.join(self.folder, tag + '.wav')
            x, sr = sf.read(wav, dtype='float32', always_2d=True)
            meter = pyloudnorm.Meter(sr)
            per = {}
            for n in notes['notes']:
                per.setdefault(str(n['pitch']), []).append(S.measure(x, sr, n['on'], n['off'], meter, sones=False))
            self.cache[tag] = {p: {k: [m[k] for m in ms] for k in MEASURES} for p, ms in per.items()}
            json.dump(self.cache, open(self.cache_file, 'w'))
        return self.cache[tag]

    def med(self, tag, k='rmsDb'):
        return {p: statistics.median(v[k]) for p, v in self.meas(tag).items()}

    def rel(self, tag, ref, k='rmsDb'):
        """median over pitches of tag - ref (None: silent), and the per-pitch values"""
        a, b = self.med(tag, k), self.med(ref, k)
        d = [a[p] - b[p] for p in sorted(a, key=int) if a[p] > -150 and b[p] > -150]
        return (round(statistics.median(d), 2) if d else None), [round(v, 2) for v in d]

    def spread(self, tag):
        return round(max(max(v['rmsDb']) - min(v['rmsDb']) for v in self.meas(tag).values()), 2)


def power_sum(levels):
    p = sum(10 ** (v / 10) for v in levels if v is not None and not math.isinf(v))
    return 10 * math.log10(p) if p > 0 else None


def fader_db(law, x):
    """contextlevel.cpp micFaderDb"""
    if x <= 0:
        return -math.inf
    if x >= 127:
        return 0.0
    pts = sorted(law.items())
    if x <= pts[0][0]:
        return pts[0][1] + 20 * math.log10(x / pts[0][0])
    pts.append((127, 0.0))
    for (a, da), (b, db_) in zip(pts, pts[1:]):
        if a <= x <= b:
            return da + (db_ - da) * (x - a) / (b - a)
    return 0.0


def mix_db(mix, m):
    """contextlevel.cpp micMixDb: linear in amplitude between the measured values"""
    pts = sorted(mix.items())
    for a, b in zip(pts, pts[1:]):
        if a[0] <= m <= b[0]:
            t = (m - a[0]) / (b[0] - a[0])
            out = []
            for i in range(3):
                ga = 0 if a[1][i] is None else 10 ** (a[1][i] / 20)
                gb = 0 if b[1][i] is None else 10 ** (b[1][i] / 20)
                g = ga + (gb - ga) * t
                out.append(20 * math.log10(g) if g > 0 else None)
            return out
    return pts[-1][1]


def predict(levels, default, law, mix, faders):
    """contextlevel.cpp micLevelDb: levels {mic: dB at full}, default {mic: fader}, faders {mic | 'mix': value}; with
    Mic Mix Distance the mic values are not played: Close / Tree / Ambient by its law, Outrigger / Leader their default"""
    m = mix_db(mix, faders['mix']) if 'mix' in faders else None
    out = []
    for i, name in enumerate(MICS):
        if name not in levels:
            continue
        if m is not None:
            g = (-math.inf if m[i] is None else m[i]) if i < 3 else fader_db(law, default.get(name, 0))
        elif name in faders:
            g = fader_db(law, faders[name])
        else:
            g = fader_db(law, default.get(name, 0))
        out.append(levels[name] + g)
    return power_sum(out)


def analyze(folder, out):
    R = Renders(folder)
    lists = {n: json.load(open(os.path.join(folder, n + '.lst.json'))) for n in ('main', 'law', 'mix', 'combo', 'perf', 'mix2')
             if os.path.exists(os.path.join(folder, n + '.lst.json'))}
    patches = {}
    for name in ('main', 'perf'):
        by = {}
        for tag, patch, cfg in lists.get(name, []):
            by.setdefault(patch, {})[tag.rsplit('_', 1)[1]] = tag
        for patch, t in by.items():
            ref = t['def']
            rec = {'pitches': [int(p) for p in sorted(R.med(ref), key=int)], 'defaultRmsDb': R.med(ref),
                   'defaultSpread': R.spread(ref), 'mics': {}, 'perPitch': {}}
            for i in range(1, nmics(patch) + 1):
                v = {k: R.rel(t[f'a{i}'], ref, k)[0] for k in MEASURES}
                rec['mics'][MICS[i - 1]] = v
                rec['perPitch'][MICS[i - 1]] = R.rel(t[f'a{i}'], ref)[1]
            # the default mix: the Tree's share (Tree alone at 127 against the mix) and what's left with the Tree at 0
            z = R.rel(t['z2'], ref)[0]
            rec['treeOff'] = z
            if 'd5' in t:
                rec['leaderOnly'] = R.rel(t['d5'], ref)[0]
            tree = rec['mics']['Tree']['rmsDb']
            rec['default'] = {'Tree': 127} if z is None and tree is not None and abs(tree) < 0.05 else None
            patches[patch] = rec
    # the fader law: each curve against its own full fader
    curves = {}
    if 'law' in lists:
        L = {t: (p, c) for t, p, c in lists['law']}
        fl = {x: R.rel(f'l_fl_a{x}', 'm_flutesolo_a3')[0] for x in FADER_POINTS}
        hc = {x: R.rel(f'l_hn_c{x}', 'm_hornsolo_a1')[0] for x in FADER_POINTS}
        ht = {x: R.rel(f'l_hn_t{x}', 'm_hornsolo_a2')[0] for x in (96, 64, 32)}
        curves = {'Flute Solo Ambient': fl, 'Horn Solo Close': hc, 'Horn Solo Tree': ht}
    if 'mix' in lists or os.path.exists(os.path.join(folder, 'x_v1.lst.json')):
        pass
    v1 = {}
    for x in (112, 96, 80, 64, 48, 32, 16):
        tag = f'x_v1_t{x}'
        if os.path.exists(os.path.join(folder, tag + '.notes.json')):
            v1[x] = R.rel(tag, 'x_v1_def')[0]
    if v1:
        curves['Violins 1 Tree'] = v1
    law = {}
    for x in FADER_POINTS:
        vs = [c[x] for c in curves.values() if x in c and c[x] is not None]
        if vs:
            law[x] = round(statistics.median(vs), 2)
    lawSpread = {x: round(max(c[x] for c in curves.values() if x in c) - min(c[x] for c in curves.values() if x in c), 2)
                 for x in law}
    # Mic Mix Distance: a mic value sent with it (in one block, as MuseScore sends a part's values) is not played
    # (mx_v1_0_order, mx2_v1_32_o / _l / _t: the mix alone to 0.00 dB), so Close / Tree / Ambient's gains at each value are
    # fitted to the totals of five patches whose mics' levels differ: non-negative least squares on the powers, each
    # patch's equation relative to its total; each patch also predicted from a fit without it (leave one out)
    mix = {}
    mixFit = {}
    mixTotals = {}
    if 'mix' in lists and 'mix2' in lists:
        import numpy as np
        from scipy.optimize import nnls
        mixTotals['orderCheck (0, then Ambient 0)'] = R.rel('mx_v1_0_order', 'mx_v1_def')[0]
        for o, name in (('o', 'Outrigger'), ('l', 'Leader'), ('t', 'Tree')):
            mixTotals[f'32 with {name} 127 against 32 alone'] = round(
                R.rel(f'mx2_v1_32_{o}', 'mx2_v1_def')[0] - R.rel('mx_v1_32', 'mx_v1_def')[0], 2)
        fitPatches = {'Violins 1': ('mx_v1_{}', 'mx_v1_def'), 'Horn Solo': ('mx2_hn_{}', 'mx2_hn_def'),
                      'Flute Solo': ('mx2_fl_{}', 'mx2_fl_def'), 'Celli': ('mx2_vc_{}', 'mx2_vc_def'),
                      'Basses': ('mx2_cb_{}', 'mx2_cb_def')}
        CTA = ('Close', 'Tree', 'Ambient')

        def fit(m, names):
            A, b = [], []
            for n in names:
                tag, ref = fitPatches[n]
                tot = 10 ** (R.rel(tag.format(m), ref)[0] / 10)
                A.append([10 ** (patches[n]['mics'][k]['rmsDb'] / 10) / tot for k in CTA])
                b.append(1.0)
            x, _ = nnls(np.array(A), np.array(b))
            return [None if v < 1e-6 else round(10 * math.log10(v), 2) for v in x]

        def total(n, g):
            return power_sum([patches[n]['mics'][k]['rmsDb'] + gg for k, gg in zip(CTA, g) if gg is not None])
        mix = {0: [None, None, 0.0], 64: [None, 0.0, None], 127: [0.0, None, None]}
        for m in MIX_POINTS:
            mix[m] = fit(m, list(fitPatches))
            for n in fitPatches:
                tag, ref = fitPatches[n]
                loo = total(n, fit(m, [k for k in fitPatches if k != n]))
                mixFit.setdefault(n, {})[m] = {'measuredDb': R.rel(tag.format(m), ref)[0], 'fittedDb': round(total(n, mix[m]), 2),
                                               'leaveOneOutDb': None if loo is None else round(loo, 2)}
        mix = dict(sorted(mix.items()))
    # the prediction against mixed renders
    checks = []

    def check(name, patch, tag, ref, faders):
        lv = {k: v['rmsDb'] for k, v in patches[patch]['mics'].items() if v['rmsDb'] is not None}
        pred = predict(lv, patches[patch]['default'] or {'Tree': 127}, law, mix, faders)
        meas = R.rel(tag, ref)[0]
        checks.append({'patch': patch, 'setting': name, 'predictedDb': None if pred is None else round(pred, 2), 'measuredDb': meas,
                       'errorDb': None if pred is None or meas is None else round(pred - meas, 2)})
    if 'combo' in lists:
        check('Close 100, Tree 127, Ambient 80', 'Violins 1', 'cb_v1_1', 'cb_v1_def', {'Close': 100, 'Tree': 127, 'Ambient': 80})
        check('Tree 90, Leader 127', 'Violins 1', 'cb_v1_2', 'cb_v1_def', {'Tree': 90, 'Leader': 127})
        check('Close 127, Ambient 127, Outrigger 64', 'Horn Solo', 'cb_hn_1', 'cb_hn_def', {'Close': 127, 'Ambient': 127, 'Outrigger': 64})
        check('Tree 64, Ambient 96', 'Horn Solo', 'cb_hn_2', 'cb_hn_def', {'Tree': 64, 'Ambient': 96})
        check('Close 110, Tree 100, Ambient 110, Outrigger 90, Leader 60', 'Basses', 'cb_cb_1', 'cb_cb_def',
              {'Close': 110, 'Tree': 100, 'Ambient': 110, 'Outrigger': 90, 'Leader': 60})
    errs = [abs(c['errorDb']) for c in checks if c['errorDb'] is not None]
    res = {
        'about': __doc__.split('\n\n')[0].replace('\n', ' '),
        'method': 'tools/soundlibraries/mic_levels.py (its header). Levels: rmsDb (sso_cc1_sweep.measure: the sustain\'s mean power '
                  '1.5-3.0 s after the note-on), median of 4 takes per pitch, the median over the 3 pitches of the difference '
                  'against the patch\'s default render; lufs and loudDb the same way, for reference.',
        'faderLaw': {'db': law, 'spread': lawSpread, 'curves': curves,
                     'note': 'a mic alone at a fader value against itself at 127; the median of the curves; 0 is silent'},
        'mixLaw': {'db': {k: v for k, v in mix.items()}, 'checks': mixTotals, 'fit': mixFit,
                   'leaveOneOutMaxAbsErrorDb': max((round(abs(v['leaveOneOutDb'] - v['measuredDb']), 2) for f in mixFit.values()
                                                    for v in f.values() if v['leaveOneOutDb'] is not None), default=None),
                   'note': 'Mic Mix Distance value: Close / Tree / Ambient each against itself alone at 1.0 (null: silent), '
                           'fitted (non-negative least squares on the powers) to the totals of Violins 1, Horn Solo, Flute Solo, '
                           'Celli and Basses; 0, 64, 127: Ambient / Tree / Close alone at 1.0 (renders equal to 0.00 dB). With it '
                           'the part\'s mic values are not played (checks): Outrigger and Leader stay at their default 0'},
        'predictionChecks': {'checks': checks, 'maxAbsErrorDb': round(max(errs), 2) if errs else None,
                             'medianAbsErrorDb': round(statistics.median(errs), 2) if errs else None},
        'patches': patches,
    }
    json.dump(res, open(out, 'w'), indent=1)
    print(f'{len(patches)} patches; fader law {law}; prediction error max {res["predictionChecks"]["maxAbsErrorDb"]} '
          f'median {res["predictionChecks"]["medianAbsErrorDb"]} dB')


def extra(measPath, jobsPath, out):
    meas = json.load(open(measPath))
    by = {}
    for tag, patch, cfg in json.load(open(jobsPath)):
        if tag in meas:
            by.setdefault(patch, {})[tag.rsplit('_', 1)[1]] = tag
    res = json.load(open(out))
    for patch, t in by.items():
        decaying = patch in ('Harp', 'Timpani')
        lv = 'energyDb' if decaying else 'rmsDb'
        med = lambda tag, k: {p: statistics.median(v[k]) for p, v in meas[tag].items()}

        def rel(tag, k):
            a, b = med(tag, k), med(t['def'], k)
            d = [a[p] - b[p] for p in sorted(a, key=int) if a[p] > -150 and b[p] > -150]
            return (round(statistics.median(d), 2) if d else None), [round(v, 2) for v in d]
        ref = meas[t['def']]
        rec = {'pitches': [int(p) for p in sorted(ref, key=int)], 'defaultRmsDb': med(t['def'], lv),
               'defaultSpread': round(max(max(v[lv]) - min(v[lv]) for v in ref.values()), 2), 'mics': {}, 'perPitch': {}}
        if decaying:
            rec['levelMeasure'] = 'energyDb (stored as rmsDb): the note and its tail, 0 .. 6.0 s after the note-on'
        n = sum(1 for k in t if k.startswith('a'))
        for i in range(1, n + 1):
            # a mic alone at about -80 dB rms or less at every pitch is an empty signal (samples not loaded offline), not a level
            if all(statistics.median(v['rmsDb']) < -80 for v in meas[t[f'a{i}']].values()):
                rec['mics'][MICS[i - 1]] = {'rmsDb': None, 'lufs': None, 'loudDb': None}
                rec.setdefault('unmeasured', {})[MICS[i - 1]] = (
                    'the mic alone rendered an empty signal (under -80 dB rms at every pitch): its samples were not loaded in '
                    'the offline render; not a level, left out (a mic without a level is not compensated)')
                continue
            rec['mics'][MICS[i - 1]] = {'rmsDb': rel(t[f'a{i}'], lv)[0], 'lufs': rel(t[f'a{i}'], 'lufs')[0],
                                        'loudDb': rel(t[f'a{i}'], 'loudDb')[0]}
            rec['perPitch'][MICS[i - 1]] = rel(t[f'a{i}'], lv)[1]
        z = rel(t['z2'], lv)[0]
        rec['treeOff'] = z
        tree = rec['mics']['Tree']['rmsDb']
        rec['default'] = {'Tree': 127} if z is None and tree is not None and abs(tree) < 0.05 else None
        res['patches'][patch] = rec
        print('%-22s %s  tree off %s  default %s' % (patch, {m: v['rmsDb'] for m, v in rec['mics'].items()}, z, rec['default']))
    json.dump(res, open(out, 'w'), indent=1)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    e = sub.add_parser('events')
    e.add_argument('folder')
    a = sub.add_parser('analyze')
    a.add_argument('folder')
    a.add_argument('-o', '--out', required=True)
    x = sub.add_parser('extra')
    x.add_argument('meas')
    x.add_argument('jobs')
    x.add_argument('-o', '--out', required=True)
    args = ap.parse_args()
    if args.cmd == 'events':
        write_events(args.folder)
    elif args.cmd == 'extra':
        extra(args.meas, args.jobs, args.out)
    else:
        analyze(args.folder, args.out)


if __name__ == '__main__':
    main()
