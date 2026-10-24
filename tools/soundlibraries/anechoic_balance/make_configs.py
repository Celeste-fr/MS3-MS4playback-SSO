"""configs for balance.py / compare.py"""
import json, math, os, re
R = os.path.expanduser('~/MuseScore/ms3fork/private/research-balance')
DIR = json.load(open(R + '/work/directivity.json'))
AINST = {'fl': ('flute', 'Transverse_flute_modern'), 'picc': ('flute', 'Transverse_flute_modern'),
         'ob': ('oboe', 'Oboe_modern'), 'cl': ('clarinet', 'Clarinet_modern'), 'bsn': ('bassoon', 'Bassoon_modern'),
         'hn': ('horn', 'French_horn_modern'), 'tr': ('trumpet', 'Trumpet_modern'), 'trb': ('trombone', 'Tenor_trombone_modern'),
         'tuba': ('tuba', 'Tuba_modern'), 'vl': ('violin', 'Violin_modern'), 'vla': ('viola', 'Viola_modern'),
         'vc': ('cello', 'Cello_modern'), 'kb': ('bass', 'Double_bass_modern')}
MIC = {'6': (2.30, 'aalto mic 6 front'), '8': (1.92, 'aalto mic 8 (az 144 clockwise)'), '5': (2.49, 'aalto mic 5 (az 288 clockwise)')}


def fam(p):
    return re.match(r'[a-z]+', p).group(0)


def aalto(name, piece, readings, timing, partTracks, beats):
    """partTracks: reading part -> list of track stems without the _<mic> suffix"""
    env = R + '/work/env_%s.npz' % piece
    import numpy as np
    files = set(np.load(env).files)
    cfg = dict(name=name, dataset='aalto', readings=R + '/readings/' + readings, env=env, beats=beats, timing=timing,
               out=R + '/work/res_%s.json' % name, tracks={}, instrument={}, calibration={}, readAs={})
    for p, stems in partTracks.items():
        inst, tu = AINST[fam(p)]
        for mic in '685':
            ts = [s + '_' + mic for s in stems if s + '_' + mic in files]
            if not ts:
                continue
            pid = p if mic == '6' else p + '@' + mic
            r, dname = MIC[mic]
            cfg['tracks'][pid] = ts
            cfg['readAs'][pid] = p
            cfg['instrument'][pid] = inst
            cfg['calibration'][pid] = 20 * math.log10(r) + 11 - DIR['aalto'][tu][dname]['di']
    json.dump(cfg, open(R + '/work/cfg_%s.json' % name, 'w'), indent=1)


B = 'beethoven'
aalto('beethoven7_i', B, 'beethoven7_i.tsv',
      {'type': 'phenicx', 'dir': R + '/dl/phenicx/PHENICX-Anechoic/annotations/beethoven', 'bar1': 2.4, 'barlen': 2.4},
      {'fl1': [B + '_fl1'], 'fl2': [B + '_fl2'], 'ob1': [B + '_ob1'], 'ob2': [B + '_ob2'], 'cl1': [B + '_cl1'],
       'cl2': [B + '_cl2'], 'bsn1': [B + '_bsn1'], 'bsn2': [B + '_bsn2'], 'hn1': [B + '_corno1'], 'hn2': [B + '_corno2'],
       'tr1': [B + '_tr1'], 'tr2': [B + '_tr2'], 'vl1': [B + '_vl1a', B + '_vl1b'], 'vl2': [B + '_vl2a', B + '_vl2b'],
       'vla': [B + '_vlaa', B + '_vlab'], 'vc': [B + '_vc'], 'kb': [B + '_kb']}, 4)

BR = 'bruckner'
aalto('bruckner8_ii', BR, 'bruckner8_ii_merged.tsv',
      {'type': 'phenicx', 'dir': R + '/dl/phenicx/PHENICX-Anechoic/annotations/bruckner', 'bar1': 0.0, 'barlen': 1.8},
      dict([('fl1', [BR + '_fl1']), ('fl3', [BR + '_fl3'])] +
           [('ob%d' % i, [BR + '_ob%d' % i]) for i in (1, 2, 3)] + [('cl%d' % i, [BR + '_cl%d' % i]) for i in (1, 2, 3)] +
           [('bsn%d' % i, [BR + '_bsn%d' % i]) for i in (1, 2, 3)] + [('hn%d' % i, [BR + '_corno%d' % i]) for i in range(1, 9)] +
           [('tr%d' % i, [BR + '_tr%d' % i]) for i in (1, 2, 3)] + [('trb%d' % i, [BR + '_trb%d' % i]) for i in (1, 2, 3)] +
           [('tuba', [BR + '_tuba']), ('vl1', [BR + '_vl1' + c for c in 'abcdefgh']), ('vl2', [BR + '_vl2' + c for c in 'abcd']),
            ('vla', [BR + '_vla' + c for c in 'abc']), ('vc', [BR + '_vc' + c for c in 'ab']), ('kb', [BR + '_kb' + c for c in 'ab'])]), 3)

# TU Berlin Beethoven 8 (Boehm, Ackermann & Weinzierl 2020, Table 3): SPL at 1 m in the mic direction =
# dBFS + 94 + 49.8 - mic gain - artistic gain (the paper's worked example: Flute 1, 40 / -17 -> -26.8 dBFS = 94 dB)
TUG = {'Vl_1_1': (40, -12), 'Vl_1_2': (40, -6), 'Vl_2_1': (40, -6), 'Vl_2_2': (40, -6), 'Vla_1': (40, -12),
       'Vla_2': (40, -12), 'Vc': (30, -6), 'Db': (30, -2), 'Bn_1': (40, -12), 'Bn_2': (40, -14), 'Fl_1': (40, -17),
       'Fl_2': (40, -18), 'Cl_1': (40, -16), 'Cl_2': (40, -15), 'Ob_1': (40, -15), 'Ob_2': (40, -15), 'Trp_1': (30, -7),
       'Trp_2': (30, -7), 'Hrn_1': (30, -14), 'Hrn_2': (30, -17), 'Tp': (15, -7)}
TUPART = {'fl1': 'Fl_1', 'fl2': 'Fl_2', 'ob1': 'Ob_1', 'ob2': 'Ob_2', 'cl1': 'Cl_1', 'cl2': 'Cl_2', 'bsn1': 'Bn_1',
          'bsn2': 'Bn_2', 'hn1': 'Hrn_1', 'hn2': 'Hrn_2', 'tr1': 'Trp_1', 'tr2': 'Trp_2'}


def tub8(name, mvt, readings, beats, artistic=True):
    import numpy as np
    env = R + '/work/tuenv_m%d.npz' % mvt
    files = np.load(env).files
    cfg = dict(name=name, dataset='tub8' if artistic else 'tub8-mixed', readings=R + '/readings/' + readings, env=env,
               beats=beats, timing=None, out=R + '/work/res_%s.json' % name, tracks={}, instrument={}, calibration={},
               readAs={}, trim=0.4)

    def add(pid, rp, key, prefix):
        ts = [f for f in files if f[3:].startswith(prefix)]
        ts = [t for t in ts if (np.load(env)[t] > -150).sum() > 100]
        if not ts:
            return
        g, a = TUG[key]
        cfg['tracks'][pid] = ts; cfg['readAs'][pid] = rp
        cfg['instrument'][pid] = AINST[fam(rp)][0]
        cfg['calibration'][pid] = 94 + 49.8 - g - (a if artistic else 0) + 11 - DIR['b8'][key]['di']
    for rp, key in TUPART.items():
        add(rp, rp, key, key)
    for rp, fam_ in (('vl1', 'Vl_1'), ('vl2', 'Vl_2')):
        for pl in '12':
            add(rp + '.' + pl, rp, fam_ + '_' + pl, fam_ + '_' + pl + '_')
    for pl in '12':
        add('vla.' + pl, 'vla', 'Vla_' + pl, 'Vla_' + pl + '_')
    add('vc', 'vc', 'Vc', 'Vc_'); add('kb', 'kb', 'Db', 'Db_')
    p = R + '/work/cfg_%s.json' % name
    old = json.load(open(p)) if os.path.exists(p) else {}
    if old.get('timing'):
        cfg['timing'] = old['timing']
    json.dump(cfg, open(p, 'w'), indent=1)

tub8('beethoven8_i', 1, 'beethoven8_i.tsv', 3)
tub8('beethoven8_ii', 2, 'beethoven8_ii.tsv', 2)
tub8('beethoven8_iv', 4, 'beethoven8_iv.tsv', 2)
tub8('beethoven8_i_mixed', 1, 'beethoven8_i.tsv', 3, artistic=False)
tub8('beethoven8_ii_mixed', 2, 'beethoven8_ii.tsv', 2, artistic=False)
tub8('beethoven8_iv_mixed', 4, 'beethoven8_iv.tsv', 2, artistic=False)


def merge_divisi(src, dst, names):
    L = []
    for l in open(src, encoding='utf-8'):
        c = l.split('\t')
        if not l.startswith('#') and c[0] in names:
            if c[0].endswith('b') and len(c) > 1 and c[1] != 'notes':
                continue
            c[0] = names[c[0]]; l = '\t'.join(c)
        L.append(l)
    open(dst, 'w', encoding='utf-8').write(''.join(L))


MA = 'mahler'
merge_divisi(R + '/readings/mahler1_iv.tsv', R + '/readings/mahler1_iv_merged.tsv',
             {'vl1a': 'vl1', 'vl1b': 'vl1', 'vl2a': 'vl2', 'vl2b': 'vl2', 'vca': 'vc', 'vcb': 'vc'})
# flutes / piccolos left out: the recording's two flute tracks (fl1 "Piccolo 1-2", fl3 "Flutes 1-2") can't be matched
# to the score's players with confidence; cl4 (E-flat clarinet) left out (no map instrument)
p = R + '/work/cfg_mahler1_iv.json'
old = json.load(open(p)) if os.path.exists(p) else {}
aalto('mahler1_iv', MA, 'mahler1_iv_merged.tsv', None,
      dict([('ob%d' % i, [MA + '_ob%d' % i]) for i in (1, 2, 3, 4)] + [('cl%d' % i, [MA + '_cl%d' % i]) for i in (1, 2, 3)] +
           [('bsn%d' % i, [MA + '_bsn%d' % i]) for i in (1, 2, 3)] + [('hn%d' % i, [MA + '_corno%d' % i]) for i in range(1, 8)] +
           [('tr%d' % i, [MA + '_tr%d' % i]) for i in (1, 2, 3, 4)] + [('trb%d' % i, [MA + '_trb%d' % i]) for i in (1, 2, 3)] +
           [('tuba', [MA + '_tuba']), ('vl1', [MA + '_vl1a', MA + '_vl1b']), ('vl2', [MA + '_vl2a', MA + '_vl2b']),
            ('vla', [MA + '_vla']), ('vc', [MA + '_vc']), ('kb', [MA + '_kb'])]), 4)
c = json.load(open(p)); c['notesEndPad'] = 0.25; c['trim'] = 0.4
c['timing'] = old.get('timing')  # set by hand: PHENICX, bar1 1.47, barlen 1.3114
json.dump(c, open(p, 'w'), indent=1)
