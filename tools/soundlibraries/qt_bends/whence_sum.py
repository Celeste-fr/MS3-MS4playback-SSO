# whence_sum.py <work dir> [--key]: ana.py's rows of whence_variants.py's renders (<work>/ana.json, <work>/index.json)
# summed per variant: transitions into a quarter-tone note / a 12-TET note at IOI <= 200 ms (and quarter tones above):
# |end| median (cents, the note's last 40 % against the written pitch) and reached (the heard check's rule).
# --key: index.json's notes carry 'wtarget' (the written pitch) and 'target' = the key (a run without bends measured
# against its keys: how far SSO's own transition gets).
import json, sys
import numpy as np

W = sys.argv[1]
idx = json.load(open(f'{W}/index.json'))
rows = json.load(open(f'{W}/ana.json'))
for name in sorted(rows):
    ns = [n for n in idx[name]['notes'] if n['k']]
    out = []
    for lab, f in (('qt<=200', lambda w, i: abs(w % 1 - 0.5) < 0.01 and i <= 0.2),
                   ('12<=200', lambda w, i: abs(w % 1) < 0.01 and i <= 0.2),
                   ('qt>200', lambda w, i: abs(w % 1 - 0.5) < 0.01 and i > 0.2)):
        g = [r for n, r in zip(ns, rows[name]) if f(n.get('wtarget', n['target']), n['ioi']) and r['end'] is not None]
        if g:
            out.append(f"{lab}: |end| {np.median([abs(r['end']) for r in g]):5.1f} reached {sum(r['hit'] for r in g)}/{len(g)}")
    print(f'{name:8s}', ' | '.join(out))
