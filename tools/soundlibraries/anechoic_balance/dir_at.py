"""dir_at.py <1a SOFA root> <out.json>: TU Berlin single-tone (ff) directivity index toward given directions.
DI(u) = 10 log10(p^2(u) / sum_q w_q p_q^2), p^2(u) interpolated over the 32 receivers (inverse squared angle, the 4
nearest; exact at a receiver), w the spherical Voronoi areas; per note, then the mean (dB) and SD over notes.
Directions in the SOFA convention (azimuth counter-clockwise from the player's front, +90 = player's left)."""
import sys, os, json, glob, numpy as np, h5py
from scipy.spatial import SphericalVoronoi

def unit(az, el):
    a, e = np.radians(az), np.radians(el)
    return np.stack([np.cos(e) * np.cos(a), np.cos(e) * np.sin(a), np.sin(e)], axis=-1)

def weights(pos):
    sv = SphericalVoronoi(unit(pos[:, 0], pos[:, 1]), 1.0, np.zeros(3)); a = sv.calculate_areas(); return a / a.sum()

def interp(p2, pos, az, el):
    u = unit(az, el); R = unit(pos[:, 0], pos[:, 1])
    ang = np.degrees(np.arccos(np.clip(R @ u, -1, 1)))
    i = np.argsort(ang)[:4]
    if ang[i[0]] < 0.5:
        return p2[i[0]]
    w = 1 / ang[i] ** 2
    return float((w * p2[i]).sum() / w.sum())

# (label, TU instrument, [(name, az, el)])  az in the SOFA convention
AALTO = [(0, 10.8, 'aalto mic 6 front'), (216, 10.8, 'aalto mic 8 (az 144 clockwise)'), (72, 52.6, 'aalto mic 5 (az 288 clockwise)')]
B8 = {  # Boehm et al. 2020 Table 3 (phi, theta); assumed the SOFA convention of the same lab (not stated in the paper)
    'Vl_1_1': (12, 62), 'Vl_1_2': (30, 55), 'Vl_2_1': (68, 59), 'Vl_2_2': (0, 69), 'Vla_1': (9, 66), 'Vla_2': (39, 58),
    'Vc': (0, -19), 'Db': (22, -14), 'Bn_1': (-28, 51), 'Bn_2': (0, 40), 'Fl_1': (-35, 49), 'Fl_2': (-81, 39),
    'Cl_1': (35, 23), 'Cl_2': (0, 39), 'Ob_1': (0, 20), 'Ob_2': (6, 56), 'Trp_1': (31, 19), 'Trp_2': (38, 30),
    'Hrn_1': (-134, 36), 'Hrn_2': (-118, 62), 'Tp': (0, 80)}
B8INST = {'Vl': 'Violin_historical', 'Vla': 'Viola_historical', 'Vc': 'Cello_historical', 'Db': 'Double_bass_historical',
          'Fl': 'Keyed_flute_historical', 'Ob': 'Classic_oboe_historical', 'Cl': 'Clarinet_historical',
          'Bn': 'Classic_bassoon_historical', 'Hrn': 'Natural_horn_historical', 'Trp': 'Natural_trumpet_historical',
          'Tp': 'Timpani'}
B8MOD = {'Vl': 'Violin_modern', 'Vla': 'Viola_modern', 'Vc': 'Cello_modern', 'Db': 'Double_bass_modern',
         'Fl': 'Transverse_flute_modern', 'Ob': 'Oboe_modern', 'Cl': 'Clarinet_modern', 'Bn': 'Bassoon_modern',
         'Hrn': 'French_horn_modern', 'Trp': 'Trumpet_modern', 'Tp': 'Timpani'}
AALTOINST = ['Transverse_flute_modern', 'Oboe_modern', 'Clarinet_modern', 'Bassoon_modern', 'French_horn_modern',
             'Trumpet_modern', 'Tenor_trombone_modern', 'Tuba_modern', 'Violin_modern', 'Viola_modern', 'Cello_modern',
             'Double_bass_modern', 'Timpani', 'Pedal_timpani']

def run(root, inst, dirs):
    fs = sorted(glob.glob(os.path.join(root, inst, '*.sofa')))
    rows = {d[2]: [] for d in dirs}; w = pos = None
    for fn in fs:
        f = h5py.File(fn, 'r')
        if w is None:
            pos = f['ReceiverPosition'][:]; w = weights(pos)
        p2 = (f['Data.Real'][0] ** 2).sum(axis=1); tot = float((w * p2).sum())
        for az, el, name in dirs:
            rows[name].append(10 * np.log10(interp(p2, pos, az, el) / tot))
    return {k: {'di': round(float(np.mean(v)), 2), 'sd': round(float(np.std(v)), 2), 'notes': len(v)} for k, v in rows.items()}

root, outp = sys.argv[1], sys.argv[2]
out = {'aalto': {}, 'b8': {}, 'b8modern': {}}
for inst in AALTOINST:
    if os.path.isdir(os.path.join(root, inst)):
        out['aalto'][inst] = run(root, inst, AALTO)
for track, (az, el) in B8.items():
    fam = track.split('_')[0]
    for key, table in (('b8', B8INST), ('b8modern', B8MOD)):
        inst = table[fam]
        if os.path.isdir(os.path.join(root, inst)):
            out[key][track] = dict(instrument=inst, **run(root, inst, [(az % 360, el, 'mic')])['mic'])
json.dump(out, open(outp, 'w'), indent=1)
for k in out:
    for i, v in out[k].items():
        print(k, i, v)
