#!/usr/bin/env python3
"""spheres_k550.py <folder of Spheres Mozart close stems (<Section>_<Section>.flac)> <out.json>: the sounding length of
the K. 550 notes whose readings the owner confirmed (2026-10-15, technique_offsets.py), per note onset from
tools/soundlibraries/spheres_k550_staccato.json (t0; the next note's onset = t0 + dur):

- Violins 1, I bars 30-33: staccato quarters at f ("A stacc"), IOI ~0.33 s; bar 29's dotted half held ("A long").
- Celli / Basses, I bars 191-197: staccato eighths at f ("C stacc8"), IOI ~0.165 s; bar 198's whole note.
- Celli / Basses, I bars 1-9: unmarked quarters at p, one per bar, followed by rests ("B quarter"), IOI ~1.2 s (written
  quarter ~0.30 s at the played tempo); bars 10-15's whole notes ("B whole").
The Spheres dataset (Colibri Ensemble; arXiv 2511.21247, Zenodo 17347681, CC BY-SA 4.0): each section recorded alone, its
own close stem (a section, several players: the sounding length is the section's, players' spread included).
"""
import json, os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import notes

HERE = os.path.dirname(os.path.abspath(__file__))
STEMS = {'Violin_1': 'Violin_1_Violin_1.flac', 'Cello': 'Cello_Cello.flac', 'Bass_1': 'Bass_1_Bass.flac'}
WRITTEN = {'A stacc f b30-33 (dotted quarters)': ('staccato', 'quarter'), 'C stacc8 f b191-197': ('staccato', 'eighth'),
           'B quarter p b1-9': ('unmarked before rests', 'quarter'), 'A long f b29 (single dotted half)': ('held', 'dotted half'),
           'C whole f b198': ('held', 'whole'), 'B whole p b10-15': ('held', 'whole')}


def main(folder, out):
    src = json.load(open(os.path.join(HERE, '..', 'spheres_k550_staccato.json')))['sections']
    rows = []
    for stem, f in STEMS.items():
        x, sr = notes.load(os.path.join(folder, f))
        pw, pe, pf = notes.envelopes(x, sr)
        for passage, ns in src[stem].items():
            kind, value = WRITTEN[passage]
            for n in ns:
                t0, t1 = n['t0'], n['t0'] + n['dur']
                m = notes.measure(pw, 0.0, t0, t1)
                p = notes.measure(pe, pf, t0, t1, levels=(10,))
                if not m or not p:
                    continue
                rows.append({'stem': stem, 'passage': passage, 'kind': kind, 'written': value, 't': t0, 'ioi': n['dur'],
                             'peakDb': round(m['peak'], 1), 'dipDb': round(m['dip'], 1),
                             'e6': round(m[6]['end'] - m[6]['start'], 3), 'e10': round(m[10]['end'] - m[10]['start'], 3),
                             'e20': round(m[20]['end'] - m[20]['start'], 3), 'joined20': m[20]['joined'],
                             'joined10': m[10]['joined'], 'p10': round(p[10]['end'] - p[10]['start'], 3),
                             'pJoined10': p[10]['joined']})
            r = [q for q in rows if q['stem'] == stem and q['passage'] == passage]
            print(stem, passage, len(r), 'ioi %.3f' % np.median([q['ioi'] for q in r]),
                  'e20 %.3f e10 %.3f p10 %.3f' % tuple(np.median([q[k] for q in r]) for k in ('e20', 'e10', 'p10')),
                  'joined20 %d' % sum(q['joined20'] for q in r), flush=True)
    json.dump({'source': 'Spheres, Mozart K. 550 / I, close stems; onsets from spheres_k550_staccato.json', 'notes': rows},
              open(out, 'w'), indent=0)


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
