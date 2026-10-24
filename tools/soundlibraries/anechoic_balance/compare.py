"""compare.py <out.json> <result.json:config.json> ...
Per segment: Lw = level + calibration (dataset) - DI(mic direction) ; the map's implied per-player Lw at the written
marking: Lw_mp (sso_cl_section_balance.json, one player) + markingSteps(m) x realSteps / 5 (ContextLevel's Clark &
Luce spans). Residual r = Lw - Lw_map. Per piece the strings' mean residual (frame-weighted over every string
part x marking, each part-marking one value) is the reference; an instrument's in-context difference against the map
= its mean residual - the strings' mean (the same piece), pooled over pieces with the pieces' own string reference."""
import os, sys, json, math, numpy as np

STEPS = {'ppp': -3, 'pp': -2, 'p': -1, 'mp': 0, 'mf': 1, 'f': 2, 'ff': 3, 'fff': 4}
# one player, the map's sectionLevel2 real side (sso_cl_section_balance.json lw.mp of the solo patch, or the
# section's less 10 log10 N) and realSteps (the map)
MAP = {'violin': (77.93, 13.8), 'viola': (75.27, 15.9), 'cello': (82.72, 14.1), 'bass': (90.87 - 10 * math.log10(8), 14.1),
       'flute': (81.90, 7.5), 'oboe': (85.23, 7.4), 'clarinet': (82.05, 8.3), 'bassoon': (81.16, 10.2),
       'horn': (94.76, 17.7), 'trumpet': (96.59, 8.8), 'trombone': (89.76, 17.0), 'tuba': (95.12, 12.5)}
STRINGS = {'violin', 'viola', 'cello', 'bass'}


def mapLw(inst, m):
    mp, S = MAP[inst]
    return mp + STEPS[m] * S / 5.0


def main():
    outp = sys.argv[1]
    rows = []
    for arg in sys.argv[2:]:
        rp, cp = arg.split(':')
        res, cfg = json.load(open(rp)), json.load(open(cp))
        for s in res['segments']:
            p = s['part']
            if os.environ.get('EXCL_UNC') and s['uncertainReading']:
                continue
            inst = cfg['instrument'].get(p)
            if inst not in MAP:
                continue
            cal = cfg['calibration'][p]          # dB to add to the level: -> Lw (or Lw + unknown constant)
            Lw = s['levelDbFs'] + cal
            rows.append(dict(piece=cfg['name'], dataset=cfg['dataset'], part=p.split('@')[0], mic=(p.split('@') + ['6'])[1], inst=inst, marking=s['marking'],
                             frames=s['frames'], Lw=Lw, mapLw=mapLw(inst, s['marking']), r=Lw - mapLw(inst, s['marking']),
                             frm=s['from'], to=s['to'], unc=s['uncertainReading']))
    out = {'rows': rows, 'pieces': {}, 'instruments': {}}
    # per piece and part x marking: frame-weighted energy mean of the residual
    pm = {}
    for r in rows:
        pm.setdefault((r['piece'], r['part'], r['inst'], r['marking']), {}).setdefault(r['mic'], []).append(r)
    pmv = {}
    for k, bymic in pm.items():
        est = {}
        for mic, v in bymic.items():
            w = np.array([x['frames'] for x in v], float)
            est[mic] = (10 * np.log10(np.sum(w * 10 ** (np.array([x['Lw'] for x in v]) / 10)) / w.sum()), len(v), int(w.sum()))
        Lw = float(np.mean([e[0] for e in est.values()]))      # every microphone's estimate, dB-averaged
        n, fr = est.get('6', list(est.values())[0])[1:]
        pmv[k] = dict(r=Lw - mapLw(k[2], k[3]), n=n, frames=fr, Lw=Lw,
                      micSpread=round(float(np.ptp([e[0] for e in est.values()])), 2) if len(est) > 1 else None)
    for piece in sorted(set(k[0] for k in pmv)):
        sv = [(v['r'], v['frames']) for k, v in pmv.items() if k[0] == piece and k[2] in STRINGS]
        if not sv:
            continue
        ref = float(np.average([x for x, _ in sv], weights=[f for _, f in sv]))
        out['pieces'][piece] = {'stringsRef': round(ref, 2), 'stringPartMarkings': len(sv),
                                'stringsSd': round(float(np.std([x for x, _ in sv])), 2)}
        refm = {}
        for m in STEPS:
            x = [(v['r'], v['frames']) for k, v in pmv.items() if k[0] == piece and k[2] in STRINGS and k[3] == m]
            if len(x) >= 2 and sum(f for _, f in x) * 0.05 >= 10:       # at least two string parts, 10 s
                refm[m] = float(np.average([a for a, _ in x], weights=[f for _, f in x]))
        out['pieces'][piece]['stringsRefByMarking'] = {m: round(v, 2) for m, v in refm.items()}
        for k, v in pmv.items():
            if k[0] == piece:
                v['relAll'] = v['r'] - ref
                if k[3] in refm:
                    v['rel'] = v['r'] - refm[k[3]]
    for inst in MAP:
        v = [(k, x) for k, x in pmv.items() if k[2] == inst and 'rel' in x]
        if not v:
            continue
        rel = np.array([x['rel'] for _, x in v])
        out['instruments'][inst] = {
            'meanDb': round(float(rel.mean()), 2), 'sdDb': round(float(rel.std(ddof=1)) if len(rel) > 1 else None or 0, 2),
            'semDb': round(float(rel.std(ddof=1) / math.sqrt(len(rel))), 2) if len(rel) > 1 else None,
            'partMarkings': len(rel), 'segments': int(sum(x['n'] for _, x in v)),
            'pieces': sorted(set(k[0] for k, _ in v)),
            'byMarking': {m: round(float(np.mean([x['rel'] for k, x in v if k[3] == m])), 2)
                          for m in STEPS if any(k[3] == m for k, _ in v)},
            'detail': [{'piece': k[0], 'part': k[1], 'marking': k[3], 'relDb': round(x['rel'], 2), 'segments': x['n'],
                        'seconds': round(x['frames'] * 0.05, 1)} for k, x in v]}
    json.dump(out, open(outp, 'w'), indent=1)
    for p, v in out['pieces'].items():
        print(p, v)
    for i, v in out['instruments'].items():
        print('%-9s %+6.2f sd %5.2f n %2d seg %3d %s %s' % (i, v['meanDb'], v['sdDb'], v['partMarkings'], v['segments'],
                                                        v['byMarking'], v['pieces']))


main()
