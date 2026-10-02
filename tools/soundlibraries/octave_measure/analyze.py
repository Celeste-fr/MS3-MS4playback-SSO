import json, glob, statistics as S, sys, collections
REPO = '/home/volensia/MuseScore/ms3fork/wt-5u4n0j/tools/soundlibraries/sso_legato_grid_pitches.json'
old = json.load(open(REPO))
runs = collections.OrderedDict()          # folder -> patch -> rows
for f in sorted(glob.glob('/tmp/oct/res/*/results.json')):
    j = json.load(open(f))
    for p in j['patches']:
        for s in p.get('rest', []):
            for l in s.get('legatoPitches', []):
                runs.setdefault(f.split('/')[-2], {}).setdefault(p['patch'], []).append(l)

def q(a, p):
    a = sorted(a); k = (len(a) - 1) * p; i = int(k); r = k - i
    return a[i] + (a[min(i + 1, len(a) - 1)] - a[i]) * r

out = {}
rep = []
print('runs:', list(runs))
for patch in old:
    if patch not in {pp for r in runs.values() for pp in r}:
        continue
    sound = list(old[patch])[0]
    rows = old[patch][sound]['rows']
    rng = old[patch][sound]['range']
    cur = {}
    oldv = {}
    for d in (-12, 12):
        v = [r[7] for r in rows if r[1] == d and len(r) > 7 and r[7] is not None and r[7] >= 0]
        oldv[d] = {r[0]: r[7] for r in rows if r[1] == d}
        cur[d] = S.median(v) if v else None
    nonoct = [r[3] for r in rows if abs(r[1]) != 12 and r[3] >= 0]
    new = {-12: {}, 12: {}}
    seen = {}
    for run, pp in runs.items():
        for l in pp.get(patch, []):
            if abs(l['interval']) != 12 or l.get('tMidMs', -1) is None:
                continue
            key = (l['start'], l['interval'])
            if l['tMidMs'] < 0:
                continue
            if key in seen:
                rep.append(l['tMidMs'] - seen[key]); continue
            seen[key] = l['tMidMs']
            new[l['interval']][l['start']] = l['tMidMs']
    res = {'range': rng, 'nonOctaveMidMedian': S.median(nonoct), 'cur': cur}
    for d in (-12, 12):
        n = new[d]
        n_new = {k: v for k, v in n.items() if k not in oldv[d]}
        allv = dict(n);
        for k, v in oldv[d].items():
            if v is not None and v >= 0: allv.setdefault(k, v)
        off = [v - cur[d] for v in n_new.values()] if cur[d] is not None else []
        e = {'nNewStarts': len(n_new), 'nAll': len(allv), 'nZero': sum(1 for v in allv.values() if v == 0)}
        if off:
            e.update(offMedian=S.median(off), offQ1=q(off, .25), offQ3=q(off, .75),
                     sameSideShare=round(sum(1 for o in off if (o > 0) == (S.median(off) > 0)) / len(off), 2))
        if allv:
            av = list(allv.values())
            e.update(allMedian=S.median(av), allQ1=q(av, .25), allQ3=q(av, .75),
                     allMedianNoZero=S.median([v for v in av if v > 0]) if any(v > 0 for v in av) else None)
            ks = sorted(allv); h = len(ks) // 2
            if h:
                e.update(lowHalfMedian=S.median([allv[k] for k in ks[:h]]), highHalfMedian=S.median([allv[k] for k in ks[h:]]))
            e['byStart'] = {k: allv[k] for k in ks}
        res[str(d)] = e
    out[patch] = res
print('repeat diffs (same start/interval across runs): n=%d, max |d|=%s' % (len(rep), max(map(abs, rep)) if rep else None))
json.dump(out, open('/tmp/oct/octave_measure.json', 'w'), indent=1)
for p, r in out.items():
    print('%-40s range %s nonoct %s' % (p, r['range'], r['nonOctaveMidMedian']))
    for d in ('-12', '12'):
        e = r[d]
        print('   %+3s cur %5s  newN %2d  off med %6s IQR [%s,%s] same-side %s | all n %d med %s [%s,%s] noZero %s lowHalf %s highHalf %s zeros %d' % (
            d, r['cur'][int(d)], e['nNewStarts'], e.get('offMedian'), e.get('offQ1'), e.get('offQ3'), e.get('sameSideShare'),
            e['nAll'], e.get('allMedian'), e.get('allQ1'), e.get('allQ3'), e.get('allMedianNoZero'), e.get('lowHalfMedian'), e.get('highHalfMedian'), e['nZero']))
