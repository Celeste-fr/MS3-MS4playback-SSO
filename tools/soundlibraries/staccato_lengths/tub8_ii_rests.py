#!/usr/bin/env python3
"""tub8_ii_rests.py <folder of movement2/anechoic wavs> <out.json>: eighths followed by a rest in Beethoven 8 / II bars
13-17 (TU Berlin anechoic recording; one track per wind player; the cello and the bass one player, 6 passes each).

Score (B&H Werke, IMSLP #61814, p. 26 system 1; reading flagged for the owner's review,
readings/beethoven8_ii_articulation.tsv): bars 14, 16, 17 end figures with an eighth followed by a sixteenth or eighth
rest, no dot or wedge (f / sf, then dimin.). Strings: unmarked. Winds: no mark printed, but "pp sempre staccato" stands
from bar 1, so the winds' eighths are kept apart ("winds, sempre staccato in force").
Kept: an onset in 37.0-51.5 s (bars 13-17 at the played tempo, a sixteenth ~0.357 s) whose next onset in the part is
0.9-1.6 s later (an eighth plus a sixteenth or eighth rest at that tempo).
"""
import glob, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notes

FAMILY = {'Ob': 'oboe', 'Cl': 'clarinet', 'Bn': 'bassoon', 'Hrn': 'horn', 'Vc': 'cello', 'Db': 'double bass'}


def main(folder, out):
    rows = []
    for f in sorted(glob.glob(os.path.join(folder, '*.wav'))):
        tr = os.path.basename(f)[:-4]
        fam = FAMILY.get(tr.split('_')[1])
        if not fam:
            continue
        x, sr = notes.load(f)
        pw, pe, pf = notes.envelopes(x, sr)
        on = notes.onsets(pw, 30.0, 60.0, rise_db=6, floor_db=np.percentile(pw, 99) - 30, min_ioi=0.05)
        for k in range(len(on) - 1):
            t0, t1 = on[k], on[k + 1]
            if not (37.0 <= t0 <= 51.5 and 0.9 <= t1 - t0 <= 1.6):
                continue
            m = notes.measure(pw, 0.0, t0, t1)
            p = notes.measure(pe, pf, t0, t1, levels=(10,))
            rows.append({'track': tr, 'instrument': fam, 't': round(t0, 3), 'ioi': round(t1 - t0, 3),
                         'kind': 'unmarked before rest' if fam in ('cello', 'double bass') else 'winds, sempre staccato in force',
                         'peakDb': round(m['peak'], 1), 'dipDb': round(m['dip'], 1),
                         'e6': round(m[6]['end'] - m[6]['start'], 3), 'e10': round(m[10]['end'] - m[10]['start'], 3),
                         'e20': round(m[20]['end'] - m[20]['start'], 3), 'joined20': m[20]['joined'],
                         'joined10': m[10]['joined'], 'p10': round(p[10]['end'] - p[10]['start'], 3),
                         'pJoined10': p[10]['joined']})
    for fam in FAMILY.values():
        n = [r for r in rows if r['instrument'] == fam]
        if n:
            print(fam, len(n), 'ioi %.3f' % np.median([r['ioi'] for r in n]),
                  'e20 %.3f e10 %.3f p10 %.3f' % tuple(np.median([r[k] for r in n]) for k in ('e20', 'e10', 'p10')),
                  'times', ' '.join('%.1f' % r['t'] for r in n[:8]))
    json.dump({'source': 'Beethoven 8 / II bars 13-17, TU Berlin anechoic (movement2.zip)', 'notes': rows},
              open(out, 'w'), indent=0)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
