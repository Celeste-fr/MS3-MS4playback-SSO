"""summarize.py <cmp_all.json> <out.json>: per instrument and dataset the median in-context difference against the
map (rel, dB, per player, against the strings at the same written marking), a bootstrap 90 % interval over the
part x marking values, and the pooled estimate (the mean of the two datasets' medians: independent players)."""
import sys, json, numpy as np
d = json.load(open(sys.argv[1]))
rng = np.random.default_rng(1)
DS = {'aalto': ['beethoven7_i', 'bruckner8_ii', 'mahler1_iv'], 'tub8': ['beethoven8_i', 'beethoven8_ii', 'beethoven8_iv']}
out = {}
for inst, v in d['instruments'].items():
    e = {}
    for ds, pieces in DS.items():
        x = np.array([q['relDb'] for q in v['detail'] if q['piece'] in pieces])
        secs = sum(q['seconds'] for q in v['detail'] if q['piece'] in pieces)
        if len(x) == 0:
            continue
        bs = [np.median(rng.choice(x, len(x))) for _ in range(4000)] if len(x) > 1 else [x[0]]
        e[ds] = {'median': round(float(np.median(x)), 2), 'mean': round(float(x.mean()), 2),
                 'ci90': [round(float(np.percentile(bs, 5)), 2), round(float(np.percentile(bs, 95)), 2)],
                 'partMarkings': int(len(x)), 'seconds': round(secs, 1),
                 'pieces': sorted(set(q['piece'] for q in v['detail'] if q['piece'] in pieces)),
                 'byMarking': {m: round(float(np.median([q['relDb'] for q in v['detail'] if q['piece'] in pieces and q['marking'] == m])), 2)
                               for m in ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff']
                               if any(q['piece'] in pieces and q['marking'] == m for q in v['detail'])}}
    if e:
        meds = [x['median'] for x in e.values()]
        e['pooled'] = {'db': round(float(np.mean(meds)), 2), 'datasets': len(meds),
                       'datasetDifference': round(float(abs(meds[0] - meds[1])), 2) if len(meds) == 2 else None}
    out[inst] = e
json.dump(out, open(sys.argv[2], 'w'), indent=1)
print('%-9s %24s %24s %8s' % ('inst', 'Aalto median [90%] n', 'TU B8 median [90%] n', 'pooled'))
for i, e in out.items():
    f = lambda k: ('%+.1f [%+.1f,%+.1f] %d' % (e[k]['median'], *e[k]['ci90'], e[k]['partMarkings'])) if k in e else '-'
    print('%-9s %24s %24s %8s' % (i, f('aalto'), f('tub8'), '%+.1f' % e['pooled']['db']))
