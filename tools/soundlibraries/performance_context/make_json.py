# writes tools/soundlibraries/sso_performance_context.json from fit_L.json / fit_K.json (PCL_WORK) and the take noise
import json, os, sys
import numpy as np
D = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, D)
from gen import SEC, SHORT, DS, IOIS
W = os.environ.get('PCL_WORK', '.')
out = dict(
    source='Windows test VM, Kontakt 8 + Spitfire Symphony Orchestra, offline renders by kthost (2026-10-10); events by '
           'tools/soundlibraries/performance_context/gen.py, which copies the rules of MuseScore Recommended (dumpEvents of '
           'Whence 12-TET): Spiccato (CC32 42) at the calibrated velocity, sent its onset early, off at half its IOI; '
           'Performance Legato on the Performance patch at the calibrated CC1, attack 112 sent the median Legato onset early, '
           'transitions 93 (Violas 100) overlapping 24 ms, re-attack 106 with the note before it ending 54 ms before',
    metric="the owner's: loudest 50 ms RMS (5 ms hop) per note from note-on to max(off, on + 150 ms) + 50 ms, mono; "
           "median per figure; L plain, K after BS.1770 K-weighting; two takes (B in reverse order), averaged",
    figure='staccatos: 6 notes [p, p-2, p, p+1, p, p-2] at 2 x IOI; slurs: [p, p-2, p, p+1] from silence (attack) then '
           '[p+2, p, p+2, p+3] (re-attack), at IOI; 3 s between slur figures, 1.5 s between staccato figures',
    relation='need = staccatos - slurs + 1.3 dB: the gain on the Performance notes over their calibrated CC1 that gives '
             "staccatos - slurs = -1.3 dB (the Violas' approved relation)",
    grid=dict(dynamic=DS, ioiMs=[round(i * 1000) for i in IOIS], pitches={s: SEC[s][3:] for s in SEC}),
    sections={})
for s in SEC:
    sec = {}
    for M in 'LK':
        F = json.load(open(f'{W}/fit_{M}.json'))[s]
        sec[M] = {f'{d}|{round(i * 1000)}|{r}': {k: (round(v, 2) if isinstance(v, float) else v) for k, v in
                  F[f'{d}|{i}|{r}'].items() if k in ('need', 'st', 'pf', 'a', 'b', 'cc', 'max127', 'pf64', 'a64', 'pfd')}
                  for d in DS for i in IOIS for r in SEC[s][3:]}
    sec['note'] = ("need, st (staccatos), pf (slurs at the calibrated CC1 cc), a / b (slurs from silence / re-attacked), "
                   "pf64 / a64 (slurs at CC1 64), pfd (slurs at CC1 = the dynamic d: the written dynamic's value), max127 (what CC1 127 adds over cc), dB")
    out['sections'][s] = sec
open(os.path.join(D, '..', 'sso_performance_context.json'), 'w').write(json.dumps(out, indent=1) + '\n')
