#!/usr/bin/env python3
"""tub8_ii.py <folder of movement2/anechoic wavs> <out.json>: the winds' staccato sixteenths of Beethoven 8 / II
(TU Berlin anechoic recording, Orchester Wiener Akademie; Böhm, Ackermann & Weinzierl 2020, CC BY 4.0).

Score (B&H Werke, IMSLP #61814, p. 25-26; reading flagged for the owner's review, readings/beethoven8_ii_articulation.tsv):
oboes, clarinets, bassoons and horns play repeated sixteenth chords marked "pp sempre staccato" with dots from bar 1
(Allegretto scherzando, eighth = 88); bars 18-19 cresc., 20-22 f, still dotted. One track per player (two per section).

Notes kept: inside a run of the repeated sixteenths, i.e. the IOI before and after within 0.28-0.45 s (the played
sixteenth is ~0.355 s), in bars 1-8 (pp) and 18-22 (cresc. -> f). Bar positions from the played tempo: bar 1 at the first
wind onset, bar 9's tied ff note at ~25.4 s and bar 22's last eighth at ~66.3 s anchor the two windows.
"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notes

TRACKS = {'03_Ob_1': 'oboe', '04_Ob_2': 'oboe', '05_Cl_1': 'clarinet', '06_Cl_2': 'clarinet',
          '07_Bn_1': 'bassoon', '08_Bn_2': 'bassoon', '09_Hrn_1': 'horn', '10_Hrn_2': 'horn'}
# (label, t0, t1, dynamic): bar 1 starts ~1.50 s, a bar ~2.84 s; bar 9's tied ff note ~25.4 s; bars 18-22 ~53.6-66.3 s
WINDOWS = [('b1-8 pp', 1.3, 24.0, 'pp'), ('b18-19 cresc', 53.4, 59.0, 'cresc'), ('b20-22 f', 59.0, 66.2, 'f')]
IOI = (0.28, 0.45)


def main(folder, out):
    rows = []
    for tr, inst in TRACKS.items():
        x, sr = notes.load(os.path.join(folder, tr + '.wav'))
        pw, pe, pf = notes.envelopes(x, sr)
        on = np.array(notes.onsets(pw, 0, len(pw) * notes.HOP, floor_db=np.percentile(pw, 99) - 30))
        for k in range(1, len(on) - 1):
            a, b = on[k] - on[k - 1], on[k + 1] - on[k]
            if not (IOI[0] <= a <= IOI[1] and IOI[0] <= b <= IOI[1]):
                continue
            w = [w for w in WINDOWS if w[1] <= on[k] < w[2]]
            if not w:
                continue
            m = notes.measure(pw, 0.0, on[k], on[k + 1])
            p = notes.measure(pe, pf, on[k], on[k + 1], levels=(10,))
            if not m or not p:
                continue
            rows.append({'track': tr, 'instrument': inst, 'window': w[0][0], 'dynamic': w[0][3], 't': round(on[k], 3),
                         'ioi': round(b, 3), 'peakDb': round(m['peak'], 1), 'dipDb': round(m['dip'], 1),
                         'e6': round(m[6]['end'] - m[6]['start'], 3), 'e10': round(m[10]['end'] - m[10]['start'], 3),
                         'e20': round(m[20]['end'] - m[20]['start'], 3),
                         'joined20': m[20]['joined'], 'joined10': m[10]['joined'],
                         'p10': round(p[10]['end'] - p[10]['start'], 3), 'pJoined10': p[10]['joined']})
        n = [r for r in rows if r['track'] == tr]
        print(tr, len(n), 'e20 median %.3f' % np.median([r['e20'] for r in n]), 'ioi %.3f' % np.median([r['ioi'] for r in n]),
              'joined20 %d' % sum(r['joined20'] for r in n), flush=True)
    json.dump({'source': 'Beethoven 8 / II, TU Berlin anechoic (movement2.zip), winds bars 1-8 / 18-22', 'notes': rows},
              open(out, 'w'), indent=0)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
