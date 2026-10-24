#!/usr/bin/env python3
"""tub8_iv.py <folder of movement4/anechoic wavs> <out.json>: Violins 1's quarters in Beethoven 8 / IV bars 6-10 (TU
Berlin anechoic recording; two players, 7 passes each = 14 single-player tracks; Böhm, Ackermann & Weinzierl 2020,
CC BY 4.0).

Score (B&H Werke, IMSLP #61814, p. 37 system 2; reading flagged for the owner's review,
readings/beethoven8_iv_articulation.tsv): Allegro vivace, whole = 84, pp from bar 0 (no other dynamic until bar 17).
Violin I: bar 6 two slurred quarters then two staccato quarters; bars 7-8 four staccato quarters each; bar 9 four
unmarked quarters (leaps); bar 10 one unmarked quarter, a quarter rest, then six eighths.
Located per track: bar 10's downbeat A = the quarter onset in 8.8-9.1 s before the eighths' run; the quarter onsets
before it (a rise of 5 dB, re-armed only after 0.15 s) are bar 9 (4), bars 7-8 (8), bar 6 (2), checked to lie 0.15-0.30 s apart.
"""
import glob, json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notes
RISE = float(os.environ.get("TUB8_RISE", "5"))


def main(folder, out):
    rows, skipped = [], []
    for f in sorted(glob.glob(os.path.join(folder, '*_Vl_1_*.wav'))):
        tr = os.path.basename(f)[:-4]
        x, sr = notes.load(f)
        pw, pe, pf = notes.envelopes(x, sr)
        fl = np.percentile(pw, 99) - 30
        q = notes.onsets(pw, 5.0, 10.5, rise_db=RISE, floor_db=fl, min_ioi=0.15)
        A = [t for t in q if 8.8 <= t <= 9.1]
        if not A:
            skipped.append(tr); continue
        ia = q.index(A[0])
        if ia < 14:
            skipped.append(tr); continue
        grid = q[ia - 14:ia + 1]
        d = np.diff(grid)
        if not ((d >= 0.15) & (d <= 0.30)).all():
            skipped.append(tr + ' (spacing)'); continue
        nxt = [t for t in q if t > A[0] + 0.25]
        labels = ['b6 staccato'] * 2 + ['b7-8 staccato'] * 8 + ['b9 unmarked'] * 4 + ['b10 unmarked before rest']
        ends = grid[1:] + [nxt[0] if nxt else A[0] + 0.45]
        for t0, t1, lab in zip(grid, ends, labels):
            m = notes.measure(pw, 0.0, t0, t1)
            p = notes.measure(pe, pf, t0, t1, levels=(10,))
            rows.append({'track': tr, 'player': tr.split('_')[3], 'label': lab, 't': round(t0, 3), 'ioi': round(t1 - t0, 3),
                         'peakDb': round(m['peak'], 1), 'dipDb': round(m['dip'], 1),
                         'e6': round(m[6]['end'] - m[6]['start'], 3), 'e10': round(m[10]['end'] - m[10]['start'], 3),
                         'e20': round(m[20]['end'] - m[20]['start'], 3), 'joined20': m[20]['joined'],
                         'joined10': m[10]['joined'], 'p10': round(p[10]['end'] - p[10]['start'], 3),
                         'pJoined10': p[10]['joined']})
    for lab in sorted({r['label'] for r in rows}):
        n = [r for r in rows if r['label'] == lab]
        print(lab, len(n), 'ioi %.3f' % np.median([r['ioi'] for r in n]),
              'e20 %.3f e10 %.3f p10 %.3f' % tuple(np.median([r[k] for r in n]) for k in ('e20', 'e10', 'p10')),
              'joined20 %d' % sum(r['joined20'] for r in n))
    print('skipped', skipped)
    json.dump({'source': 'Beethoven 8 / IV bars 6-10, TU Berlin anechoic (movement4.zip), Violins 1 tracks',
               'skipped': skipped, 'notes': rows}, open(out, 'w'), indent=0)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
