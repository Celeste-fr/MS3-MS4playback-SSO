import json, statistics as S
o = json.load(open('/tmp/oct/octave_measure.json'))
fams = {'Horn': 'Horn', 'Tuba': 'Tuba', 'Oboe': 'Oboe', 'Violins2': 'Violins 2 - Perf', 'Basses': 'Basses', 'Trombone': 'Trombone', 'Piccolo': 'Piccolo'}
def q(a, p):
    a = sorted(a); k = (len(a) - 1) * p; i = int(k); r = k - i
    return a[i] + (a[min(i + 1, len(a) - 1)] - a[i]) * r
for f, key in fams.items():
    ps = [p for p in o if key in p]
    for d in ('-12', '12'):
        vals = []; rel = []
        for p in ps:
            for st, v in o[p][d]['byStart'].items():
                vals.append(v); rel.append(v - o[p]['nonOctaveMidMedian'])
        print('%-9s %+3s patches %d n %2d  med %5.0f IQR [%4.0f,%4.0f]  vs-nonoct-mid med %+4.0f IQR [%+4.0f,%+4.0f]  oldcur(patch medians) %s' % (
            f, int(d), len(ps), len(vals), S.median(vals), q(vals, .25), q(vals, .75), S.median(rel), q(rel, .25), q(rel, .75), [o[p]['cur'][d] for p in ps]))
