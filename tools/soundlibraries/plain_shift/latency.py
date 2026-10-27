# latency.py <work> <bar0> <bar1> [variant] : heard - written (heard_check.analyse's heardMs) over every pass of
# <work>/v/<variant>_p*/, all notes of bars bar0..bar1; split staccato attacks / other attacks / transitions.
import json, os, sys
import numpy as np
QB = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
sys.path.insert(0, QB + '/playbackverify')
import heard_check as hc
W, B0, B1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
VAR = sys.argv[4] if len(sys.argv) > 4 else 'PLAINb'
def is_stacc(r):
    t = (r.get('techniques') or '') + ',' + (r.get('articulation') or '')
    return any(w in t.lower() for w in ('staccat', 'spiccato'))
g = dict(all=[], staccato=[], attack=[], transition=[])
unheard, per_pass = 0, {}
for vd in sorted(d for d in os.listdir(f'{W}/v') if d.startswith(VAR + '_p') and int(d.split('_p')[-1]) <= int(os.environ.get('MAXPASS', '99'))):
    dump = f'{W}/v/{vd}/dump'
    a = hc.analyse(dump, f'{W}/v/{vd}/wav')
    rows = {(round(r['written'], 4), r['pitch']): r for r in hc.read_rows(dump)}
    pp = []
    for n in a['notes']:
        if not B0 <= n['bar'] <= B1:
            continue
        if n['heardMs'] is None:
            unheard += 1
            continue
        r = rows[(round(n['written'], 4), n['pitch'])]
        h = n['heardMs']
        pp.append(h)
        g['all'].append(h)
        g['transition' if n['transition'] else ('staccato' if is_stacc(r) else 'attack')].append(h)
    per_pass[vd] = dict(n=len(pp), median=round(float(np.median(pp)), 1) if pp else None)
f = lambda v: dict(n=len(v), median=round(float(np.median(v)), 1), p10=round(float(np.percentile(v, 10)), 1),
                   p90=round(float(np.percentile(v, 90)), 1)) if v else dict(n=0)
out = {k: f(v) for k, v in g.items()}
out['unheard'] = unheard
out['perPass'] = per_pass
print(json.dumps(out))
