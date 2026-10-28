#!/usr/bin/env python3
"""Vial Instrumental's levels against SSO's: measure each "SSO imitations" Long preset against CC1 and pitch by the method
SSO's Long was measured (sso_cc1_sweep.py), then fit what the Vial maps need so that a Vial part plays a written dynamic
as loud as the matching SSO patch plays it under Recommended (the owner, 2026-10-26/27: "equally loud at the same written
dynamic, measured, not by ear").

    vial_levels.py sweep --kthost <kthost> --vial <Vial.vst3> --presets <data folder> [-o vial_cc1_curves.json]
                         [--repeats 2] [--only "Flute Solo"]
    vial_levels.py fit [--curves vial_cc1_curves.json] [-o vial_levels.json]
    vial_levels.py check --kthost <kthost> --vial <Vial.vst3> --presets <data folder> [--levels vial_levels.json]
                         [--map "share/soundlibraries/Vial Instrumental.xml"] [-o vial_levels_check.json]

sweep: for each preset (gen_vial.py PRESETS), the events of sso_cc1_sweep.py (CC11 127, a warm-up note, then at every
pitch SSO's sweep measured - sso_cc1_curves.json / sso_winds_cc1_curves.json - CC1 16 32 48 64 80 96 112 127, velocity =
CC1 as MuseScore sends a Long, `repeats` notes of 3.0 s, 1.5 s apart; no UACC switch: Vial has none), rendered offline by
kthost (MuseScore's own hosting code, 44.1 kHz, 512-frame blocks split at each event) with the preset's bytes as the
component state, as MuseScore loads a preset library (SoundLibraryHost::savedSetupState: writeState("Vial", bytes)), then
sso_cc1_sweep.analyze: rmsDb (the sustain's power 1.5-3.0 s), lufs (BS.1770), sones (ISO 532-1), loudDb (the loudest 50 ms
of the note and 0.5 s of its release). Also each preset's CC11 law (CC11 64 at its middle pitch against 127: the real
range plays by CC11). Numbers only: no preset content.

fit: what MuseScore plays for a Long under Recommended ([levels] realRange 2, sectionBalance 2; contextlevel.h):
    heard(x, p) = A(cc1(x), p) + realDb(x, p) + O
    realDb(x, p) = steps(x) * S / 5 - longLevel(cc1(x), p)          (realSteps S: Clark & Luce's, the instrument's)
A the patch's measured level, longLevel (SSO: its rmsDb at CC1 x less at 64, real_range.py), O its sectionLevel2. SSO's
patch plays the SSO map's. Vial's preset gets the same S, realSpan, realPlay (same instrument, same players' data) and
    O_vial = O_sso + D,   longLevel_vial(x, p) = longLevel_sso(x, p) + A_vial(x, p) - A_sso(x, p) + D
D = the mean over the measured pitches of A_sso(64, p) - A_vial(64, p) (so longLevel_vial at CC1 64 averages 0), A in
loudDb (the measure the section balance put SSO's Long on the real scale with: section_balance_cl.py at64). Then
heard_vial = heard_sso at every measured CC1 and pitch (linear between them, as MuseScore reads longLevel) under any
realRange other than 0 (the Meyer rule too: both take the same player's change), with sectionBalance 1 or 2 (sectionLevel
gets the same D). The fit reports, per marking and measure (rmsDb, loudDb, lufs, sones as phon), the mean / SD / min / max
over the pitches of heard_vial - heard_sso: loudDb 0 by construction, the others how far another loudness measure would
judge it differently (the presets' envelope against SSO's samples'): the uncertainty.

check: renders every preset at each marking at three pitches (low / middle / high of the sweep) as MuseScore sends it under
Recommended (CC1 = velocity = the marking's, CC11 = round(127 x 10^((realDb - R) / 20)), output + (O + R) dB: the gain
stage, R the largest realDb over the check's notes: Volumes::range) from the generated Vial map, measures it as the sweep
and compares with SSO's heard level from SSO's measured curve at the same CC1 and pitch (sso_*_cc1_curves.json + the SSO
map's realDb and O): the real plug-in, the real CC11 quantisation.
"""
import argparse
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_vial            # noqa: E402  (PRESETS)
import sso_cc1_sweep       # noqa: E402  (make_events, analyze, MEASURES)

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
SSO_MAP = os.path.join(ROOT, 'share', 'soundlibraries', 'Spitfire Symphony Orchestra.xml')
VIAL_MAP = os.path.join(ROOT, 'share', 'soundlibraries', 'Vial Instrumental.xml')
SSO_CURVES = [os.path.join(HERE, 'sso_cc1_curves.json'), os.path.join(HERE, 'sso_winds_cc1_curves.json')]
CURVES = os.path.join(HERE, 'vial_cc1_curves.json')
LEVELS = os.path.join(HERE, 'vial_levels.json')
MARKINGS = [('ppp', 16), ('pp', 32), ('p', 48), ('mp', 64), ('mf', 80), ('f', 96), ('ff', 112), ('fff', 127)]
MEASURES = ['rmsDb', 'loudDb', 'lufs', 'phon']
OFFSET_MEASURE = 'loudDb'


def sso_curves():
    out = {}
    for f in SSO_CURVES:
        out.update(json.load(open(f, encoding='utf-8'))['sections'])
    return out


def state(component):
    """MuseScore's setup format (SoundLibraryHost writeState): "MSV3", version 1, the plug-in's name, component, controller"""
    import struct
    name = 'Vial'.encode('utf-16-be')
    return (b'MSV3' + struct.pack('>I', 1) + struct.pack('>I', len(name)) + name + struct.pack('>I', len(component))
            + component + struct.pack('>I', 0xFFFFFFFF))


def preset_file(folder, preset):
    return os.path.join(folder, 'Vial Instrumental', 'Presets', 'SSO imitations', preset + '.vital')


def render(kthost, vial, preset_path, events, wav, gain=0.0):
    with open(preset_path, 'rb') as f:
        st = state(f.read())
    d = os.path.dirname(wav)
    sp, ep = os.path.join(d, 'state.vst3state'), os.path.join(d, 'events.txt')
    open(sp, 'wb').write(st)
    open(ep, 'w').write(events)
    cmd = [kthost, vial, sp, ep, wav, '--tail', '1.5', '--quiet']
    if gain:
        cmd += ['--gain', f'{gain:.4f}']
    subprocess.run(cmd, check=True)


def phon(sones):
    # ISO 532-1's loudness level (phon) from sones: 40 + 10 log2 N (N >= 1), 40 (N + 0.0005)^0.35 below
    if sones is None or sones <= 0:
        return None
    return 40 + 10 * math.log2(sones) if sones >= 1 else 40 * (sones + 0.0005) ** 0.35


def cc11_law(kthost, vial, path, pitch, tmp):
    ev = []
    for i, c in enumerate((127, 64)):
        t = 1.0 + i * 4.5
        ev += [(t - 0.5, 'cc', 11, c), (t - 0.5, 'cc', 1, 64), (t, 'on', pitch, 64), (t + 3.0, 'off', pitch, 0)]
    text = ''.join(f'{e[0]:.4f} {e[1]} {e[2]} {e[3]}\n' for e in ev)
    wav = os.path.join(tmp, 'cc11.wav')
    render(kthost, vial, path, text, wav)
    notes = {'pitches': [pitch], 'ccs': [127, 64], 'repeats': 1, 'note': 3.0, 'switch': 0,
             'notes': [{'pitch': pitch, 'cc': c, 'on': 1.0 + i * 4.5, 'off': 4.0 + i * 4.5} for i, c in enumerate((127, 64))]}
    a = sso_cc1_sweep.analyze(wav, notes, sones=False)
    rows = {r['cc']: r['rmsDb'] for r in a['points'][str(pitch)]}
    return round(rows[64] - rows[127], 2)


def sweep(args):
    sso = sso_curves()
    out = {'method': {
        'what': "Vial Instrumental's \"SSO imitations\" Long presets against CC1 (velocity = CC1, CC11 127) at the pitches "
                "SSO's Long was swept at (sso_cc1_curves.json, sso_winds_cc1_curves.json), by sso_cc1_sweep.py's events "
                "and analyze (no UACC switch)",
        'host': 'kthost (Linux), offline, 44.1 kHz stereo float, 512-frame blocks split at each event; the preset as '
                'its component state (MuseScore: a preset library)',
        'plugin': os.path.basename(os.path.dirname(os.path.abspath(args.vial))) + '/' + os.path.basename(args.vial),
        'repeats': args.repeats,
        'cc11': 'cc11At64: rmsDb at CC11 64 less at 127, CC1 64, the middle pitch (20 log10 (64 / 127) = -5.95)'},
        'sections': {}}
    if os.path.exists(args.out):
        old = json.load(open(args.out, encoding='utf-8'))
        out['sections'] = old.get('sections', {})
    for inst, preset, _ in gen_vial.PRESETS:
        if args.only and inst not in args.only:
            continue
        pitches = sso[inst]['pitches']
        text, notes = sso_cc1_sweep.make_events(min(pitches), max(pitches), step=1000, extra=pitches,
                                                repeats=args.repeats)
        text = ''.join(l for l in text.splitlines(True) if not l.split()[1:3] == ['cc', '32'])
        path = preset_file(args.presets, preset)
        with tempfile.TemporaryDirectory() as tmp:
            wav = os.path.join(tmp, 'sweep.wav')
            render(args.kthost, args.vial, path, text, wav)
            sec = sso_cc1_sweep.analyze(wav, notes, inst)
            sec['cc11At64'] = cc11_law(args.kthost, args.vial, path, pitches[len(pitches) // 2], tmp)
        sec['preset'] = preset
        out['sections'][inst] = sec
        print(inst, 'cc11 64:', sec['cc11At64'], 'dB', file=sys.stderr)
        with open(args.out, 'w', encoding='utf-8') as f:
            f.write(sso_cc1_sweep.dump(out) + '\n')


# ---- the level chain (contextlevel.cpp: markingSteps, longLevelDb's linear interpolation, realRangeDb with spans) ----

def marking_steps(x):
    if x <= 16:
        return -3.0
    if x <= 112:
        return (x - 64) / 16.0
    if x < 127:
        return 3.0 + (x - 112) / 15.0
    return 4.0


def between(m, x, value=lambda v: v):
    ks = sorted(m)
    if not ks:
        return 0.0
    if x <= ks[0]:
        return value(m[ks[0]])
    if x >= ks[-1]:
        return value(m[ks[-1]])
    hi = min(k for k in ks if k >= x)
    if hi == x:
        return value(m[hi])
    lo = max(k for k in ks if k < x)
    t = (x - lo) / (hi - lo)
    return value(m[lo]) + t * (value(m[hi]) - value(m[lo]))


def parse_long(s):
    out = {}
    for t in s.split():
        cp, v = t.split(':')
        c, p = cp.split('/')
        out.setdefault(int(p), {})[int(c)] = float(v)
    return out


def long_level(long, x, p):
    return between(long, p, lambda byCc: between(byCc, x))


def real_db(inst, x, p):
    return marking_steps(x) * inst['realSteps'] / 5.0 - long_level(inst['long'], x, p)


def map_instruments(path):
    out = {}
    for i in ET.parse(path).getroot().findall('Instrument'):
        if i.get('with') is not None or i.get('name') in out:
            continue
        a = i.attrib
        out[a['name']] = {'realSteps': float(a.get('realSteps', 0) or 0),
                          'long': parse_long(a.get('longLevel', '')),
                          'sectionLevel': float(a.get('sectionLevel', 0) or 0),
                          'sectionLevel2': float(a.get('sectionLevel2', a.get('sectionLevel', 0)) or 0),
                          'attrs': {k: a[k] for k in ('realSpan', 'realPlay', 'realSteps') if k in a}}
    return out


def value(row, k):
    if k == 'phon':
        return phon(row.get('sones'))
    return row.get(k)


def points(section, k):
    """{pitch: {cc: value}} of measure k"""
    out = {}
    for p, rows in section['points'].items():
        for r in rows:
            v = value(r, k)
            if v is not None and v > -150:
                out.setdefault(int(p), {})[r['cc']] = v
    return out


def heard(inst, pts, x, p):
    return pts[p][x] + real_db(inst, x, p) + inst['sectionLevel2']


def fit(args):
    sso = sso_curves()
    vial = json.load(open(args.curves, encoding='utf-8'))['sections']
    smap = map_instruments(SSO_MAP)
    out = {'method': {
        'what': "what the Vial maps need to play a Long as loud as SSO's matching patch at every written dynamic under "
                "Recommended (realRange 2, sectionBalance 2): the SSO instrument's realSteps / realSpan / realPlay, "
                "sectionLevel(2) = SSO's + offsetDb, long = SSO's longLevel + Vial - SSO at that CC1 and pitch + offsetDb",
        'offset': f'mean over SSO\'s measured pitches of SSO Long - Vial preset at CC1 64, {OFFSET_MEASURE} (the section '
                  'balance\'s measure, section_balance_cl.py at64)',
        'error': 'per marking: mean / SD / min / max over the pitches of heard_vial - heard_sso (heard = measured level at '
                 'the marking\'s CC1 + realDb + sectionLevel2), per measure; phon from sones (ISO 532-1)',
        'sources': 'vial_cc1_curves.json; sso_cc1_curves.json, sso_winds_cc1_curves.json; the SSO map'},
        'instruments': {}}
    for inst, preset, _ in gen_vial.PRESETS:
        s, v, m = sso[inst], vial[inst], smap[inst]
        pitches = [p for p in s['pitches'] if str(p) in v['points']]
        sa, va = points(s, OFFSET_MEASURE), points(v, OFFSET_MEASURE)
        pitches = [q for q in pitches if all(x in sa.get(q, {}) and x in va.get(q, {}) for _, x in MARKINGS)]
        diffs = [sa[q][64] - va[q][64] for q in pitches]
        d = statistics.mean(diffs)
        # Vial's longLevel: SSO's (the map's) + how much louder the preset is than SSO's patch at that CC1 and pitch + D,
        # so that realDb takes the difference away at every measured point: heard_vial = heard_sso there (in loudDb)
        long = {q: {x: round(long_level(m['long'], x, q) + va[q][x] - sa[q][x] + d, 2) for _, x in MARKINGS}
                for q in pitches}
        vinst = {'realSteps': m['realSteps'], 'long': long, 'sectionLevel2': m['sectionLevel2'] + d}
        err = {}
        for k in MEASURES:
            sp, vp = points(s, k), points(v, k)
            rows = {}
            for name, x in MARKINGS:
                e = [heard(vinst, vp, x, p) - heard(m, sp, x, p) for p in pitches if x in sp.get(p, {}) and x in vp.get(p, {})]
                rows[name] = [round(statistics.mean(e), 2), round(statistics.pstdev(e), 2), round(min(e), 2),
                              round(max(e), 2)]
            err[k] = rows
        out['instruments'][inst] = {
            'preset': preset, 'pitches': pitches,
            'offsetDb': round(d, 2), 'offsetSd': round(statistics.pstdev(diffs), 2),
            'offsetBy': {k: round(statistics.mean(points(s, k)[p][64] - points(v, k)[p][64] for p in pitches), 2)
                         for k in MEASURES},
            'sectionLevel': round(m['sectionLevel'] + d, 2), 'sectionLevel2': round(m['sectionLevel2'] + d, 2),
            'long': {str(p): {str(c): val for c, val in per.items()} for p, per in long.items()},
            'cc11At64': v.get('cc11At64'),
            'error': err}
        print(f"{inst:22s} D {d:+6.2f} (SD {statistics.pstdev(diffs):.2f}; "
              + ' '.join(f"{k} {out['instruments'][inst]['offsetBy'][k]:+.2f}" for k in MEASURES)
              + f") worst mean error {max(abs(r[0]) for k in MEASURES for r in err[k].values()):.2f}", file=sys.stderr)
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write(sso_cc1_sweep.dump(out) + '\n')


def check(args):
    sso = sso_curves()
    smap, vmap = map_instruments(SSO_MAP), map_instruments(args.map)
    out = {'method': 'each preset at each marking (CC1 = velocity), CC11 and gain stage as MuseScore sends them under '
                     'Recommended from the Vial map; heard_vial (rendered, measured) - heard_sso (SSO\'s measured curve at '
                     'the CC1 + the SSO map\'s realDb + sectionLevel2)', 'instruments': {}}
    for inst, preset, _ in gen_vial.PRESETS:
        if args.only and inst not in args.only:
            continue
        s, m, vi = sso[inst], smap[inst], vmap[inst]
        ps = s['pitches']
        pitches = [ps[0], ps[len(ps) // 2], ps[-1]]
        top = max(real_db(vi, x, p) for _, x in MARKINGS for p in pitches)
        ev, notes, t = [(0.5, 'cc', 1, 64), (1.0, 'on', pitches[1], 64), (4.0, 'off', pitches[1], 0)], [], 5.5
        for p in pitches:
            for name, x in MARKINGS:
                cc11 = max(1, min(127, round(127 * 10 ** ((real_db(vi, x, p) - top) / 20))))
                ev += [(t - 1.0, 'cc', 1, x), (t - 1.0, 'cc', 11, cc11), (t, 'on', p, x), (t + 3.0, 'off', p, 0)]
                notes.append({'pitch': p, 'cc': x, 'on': t, 'off': t + 3.0, 'cc11': cc11})
                t += 4.5
        ev.sort(key=lambda e: (e[0], {'cc': 0, 'off': 1, 'on': 2}[e[1]]))
        text = ''.join(f'{e[0]:.4f} {e[1]} {e[2]} {e[3]}\n' for e in ev)
        with tempfile.TemporaryDirectory() as tmp:
            wav = os.path.join(tmp, 'check.wav')
            render(args.kthost, args.vial, preset_file(args.presets, preset), text, wav, gain=vi['sectionLevel2'] + top)
            a = sso_cc1_sweep.analyze(wav, {'pitches': pitches, 'ccs': [x for _, x in MARKINGS], 'repeats': 1,
                                            'note': 3.0, 'switch': 0, 'notes': notes})
        rows = {}
        for k in MEASURES:
            vp, sp = points(a, k), points(s, k)
            rows[k] = {name: [round(vp[p][x] - heard(m, sp, x, p), 2) for p in pitches] for name, x in MARKINGS}
            rows[k]['mean'] = round(statistics.mean(v for r in rows[k].values() for v in r), 2)
        out['instruments'][inst] = {'pitches': pitches, 'gainDb': round(vi['sectionLevel2'] + top, 2), 'error': rows}
        print(f"{inst:22s} " + ' '.join(f"{k} {rows[k]['mean']:+.2f}" for k in MEASURES), file=sys.stderr)
    with open(args.out, 'w', encoding='utf-8') as f:
        f.write(sso_cc1_sweep.dump(out) + '\n')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for name in ('sweep', 'check'):
        c = sub.add_parser(name)
        c.add_argument('--kthost', required=True)
        c.add_argument('--vial', required=True, help='Vial.vst3')
        c.add_argument('--presets', required=True, help="Vial's data folder (holds 'Vial Instrumental/Presets')")
        c.add_argument('--only', nargs='*')
    s = sub.choices['sweep']
    s.add_argument('-o', '--out', default=CURVES)
    s.add_argument('--repeats', type=int, default=2)
    c = sub.choices['check']
    c.add_argument('--map', default=VIAL_MAP)
    c.add_argument('-o', '--out', default=os.path.join(tempfile.gettempdir(), 'vial_levels_check.json'))
    f = sub.add_parser('fit')
    f.add_argument('--curves', default=CURVES)
    f.add_argument('-o', '--out', default=LEVELS)
    args = ap.parse_args(argv)
    {'sweep': sweep, 'fit': fit, 'check': check}[args.cmd](args)


if __name__ == '__main__':
    main()
