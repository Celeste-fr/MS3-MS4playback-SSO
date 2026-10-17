# Per-note levels of grid_score.py's renders (MuseScore3Evo --verify-playback --verify-wav: "<score> <part> library.wav"
# and "<score> <part> notes.tsv" per part), matched to index.json's notes in time order.
#   grid_ana.py cells <index.json> <folder> <date> -> <folder>/cells.json (every cell: held, slur, stac, single, per take,
#                                                     both metrics) and sso_performance_context.json's "plain"
#   grid_ana.py groups <index.json> <folder> <score> -> each figure less held per dynamic and part (calib.mscx)
#   grid_ana.py parts <folder> <score>               -> per part: Performance / other notes' medians (any score: Whence)
# Metrics: P, ArticulationCheck::perceivedLoudnessDb (perceived.py: the peak short-term loudness, the measure the
# calibration curves and the 2026-10-15 check used), window from the note-on as sent to the next note-on of the part
# or its end + 0.3 s (at least 0.1 s); L, the 2026-10-10 grid's (loudest 50 ms RMS, 5 ms hop, mono, on to
# max(off, on + 150 ms) + 50 ms). Median per figure (held: 4 notes, slurred: 8 notes, two touching slurs).
import csv, json, os, re, statistics as st, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perceived

PARTS = {'Violins 1': 'Violins I', 'Violins 2': 'Violins II', 'Violas': 'Violas', 'Celli': 'Violoncellos',
         'Basses': 'Contrabasses'}
NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def midi(n):
    m = re.match(r'([A-G])([#b]*)[^0-9-]*(-?\d+)', n)
    return None if not m else 12 * (int(m.group(3)) + 1) + NAMES[m.group(1)] + m.group(2).count('#') - m.group(2).count('b')


def loud50(m, sr, s, e):
    w = int(.05 * sr); h = int(.005 * sr)
    seg = m[int(s * sr): int((max(e, s + .15) + .05) * sr)]
    c = np.concatenate([[0], np.cumsum(seg.astype(np.float64) ** 2)])
    r = (c[w::h] - c[:-w:h][:len(c[w::h])]) / w
    return 10 * np.log10(max(r.max(), 1e-18))


def measure(folder, score, part):
    cache = f'{folder}/{score} {part} lev.json'  # kept so the wav can go once it is measured
    if os.path.exists(cache):
        return json.load(open(cache))
    out = measure_wav(folder, score, part)
    json.dump(out, open(cache, 'w'))
    return out


def measure_wav(folder, score, part):
    a, sr = sf.read(f'{folder}/{score} {part} library.wav', dtype='float32')
    rows = list(csv.DictReader(open(f'{folder}/{score} {part} notes.tsv'), delimiter='\t'))
    rows.sort(key=lambda r: float(r['time']))
    ons = [float(r['time']) for r in rows]
    mono = a.mean(1)
    out = []
    for i, r in enumerate(rows):
        t = ons[i]; ln = float(r['length'])
        end = max(min(t + ln + 0.3, ons[i + 1] if i + 1 < len(rows) else 1e9), t + 0.1)
        out.append(dict(on=t, length=ln, pitch=midi(r['pitch']), articulation=r['articulation'],
                        P=perceived.perceived_db(a[int(t * sr):int(end * sr)], sr), L=loud50(mono, sr, t, t + ln)))
    return out


def matched(idx, folder, score, sec):
    got = measure(folder, score, PARTS[sec]); want = idx['scores'][score][sec]
    if len(got) != len(want):
        raise SystemExit(f'{score} {sec}: {len(got)} notes rendered, {len(want)} written')
    bad = sum(1 for g, w in zip(got, want) if g['pitch'] != w['pitch'])
    if bad:
        raise SystemExit(f'{score} {sec}: {bad} notes out of order (pitch)')
    arts = {}
    for g, w in zip(got, want):
        arts.setdefault(w['kind'], set()).add(g['articulation'])
    print(f'  {score} {sec}: {len(got)} notes; techniques {arts}', flush=True)
    return [dict(w, **g) for g, w in zip(got, want)]


if __name__ == '__main__':
    what = sys.argv[1]
    if what == 'cells':
        idx = json.load(open(sys.argv[2])); F = sys.argv[3]
        cells = {}
        for sec in idx['sections']:
            c = {}
            for take, score in [(sc[4], sc) for sc in idx['scores'] if sc.startswith('grid')]:   # (gridA_<d>, gridB_<d>)
                for n in matched(idx, F, score, sec):
                    key = f"{n['d']}|{n['p']}" if n['kind'] == 'held' else f"{n['d']}|{n['ioi']}|{n['p']}"
                    for M in 'PL':
                        c.setdefault(M, {}).setdefault(n['kind'], {}).setdefault(key, {}).setdefault(take, []).append(n[M])
            cells[sec] = {M: {kind: {k: {t: round(st.median(v), 2) for t, v in by.items()} for k, by in kk.items()}
                              for kind, kk in mm.items()} for M, mm in c.items()}
        json.dump(cells, open(f'{F}/cells.json', 'w'), indent=1)
        # the measured part of sso_performance_context.json's "plain" (level_rule.py adds its rule; the 2026-10-15 grid of
        # [slurs] performance's own legato, the slurs only, stays at the top level)
        out = dict(
            source='Windows test VM, Kontakt 8 + Spitfire Symphony Orchestra, ' + sys.argv[4] + ': MuseScore3Evo.exe '
                   "--verify-playback --verify-wav of performance_context/grid_score.py's scores, each part rendered alone "
                   'through the hosted plug-ins, Recommended ([slurs] plainPerformance 1, [levels] calibrated 2) with, cancelling '
                   "in held - figure, sectionBalance / realRange / micCompensation 0, evenLevels 0; the map's contextLevel* "
                   "zeroed in the VM's copy",
            metric='P: ArticulationCheck::perceivedLoudnessDb (perceived.py) from the note-on as sent to the next note-on (or '
                   'its end + 0.3 s); L: loudest 50 ms RMS (5 ms hop, mono) from the note-on to max(off, on + 150 ms) + 50 ms; '
                   'median per figure, per take (A, B: B in reverse order)',
            figure='held: four half notes [p, p-2, p+2, p] at q = 60 under "non performance", All techniques Long at CC1 = d; '
                   'slur / stac / single: eight sixteenths [p, p-2, p, p+1, p, p+2, p, p-1] at the IOI, the plain Performance '
                   'legato at CC1 = d, velocity 64: two touching slurs of four / every note staccato (a spiccato attack, 45 ms '
                   'gap) / no slur, no articulation (touching)',
            relation='gain = held - figure (level_rule.py)',
            grid=dict(dynamic=idx['dynamics'], ioiMs=idx['ioiMs'], pitches=idx['pitches']),
            sections=cells)
        P = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'sso_performance_context.json')
        j = json.load(open(P))
        j['plain'] = out
        open(P, 'w').write(json.dumps(j, indent=1) + '\n')
    elif what == 'groups':
        idx = json.load(open(sys.argv[2])); F = sys.argv[3]; score = sys.argv[4]
        res = {}
        for sec in idx['sections']:
            g = {}
            for n in matched(idx, F, score, sec):
                g.setdefault((n['d'], n['kind']), []).append(n['P'])
            res[sec] = {k: st.median(v) for k, v in g.items()}
        kinds = [k for k in ('slur', 'stac', 'single') if any((d, k) in r for r in res.values() for d in idx['dynamics'] + [64, 96])]
        for d in sorted({k[0] for r in res.values() for k in r}):
            for kind in kinds:
                line = [f'd {d} {kind} - held:']
                for sec in idx['sections']:
                    r = res[sec]
                    line.append(f"{sec} {r[(d, kind)] - r[(d, 'held')]:+.2f}")
                print('  '.join(line))
            for kind in ['held'] + kinds:
                v = {s: res[s][(d, kind)] for s in idx['sections']}
                print(f"   {kind}: Va-V1 {v['Violas'] - v['Violins 1']:+.2f}  Vc-Va {v['Celli'] - v['Violas']:+.2f}  "
                      f"V2-V1 {v['Violins 2'] - v['Violins 1']:+.2f}  Cb-Vc {v['Basses'] - v['Celli']:+.2f}")
        json.dump({s: {f'{k[0]}|{k[1]}': v for k, v in r.items()} for s, r in res.items()},
                  open(f'{F}/{score} groups.json', 'w'), indent=1)
    elif what == 'parts':
        F = sys.argv[2]; score = sys.argv[3]
        res = {}
        for sec, part in PARTS.items():
            names = [n for n in (part, sec, sec.replace('Celli', 'Violoncellos').replace('Basses', 'Contrabasses'))
                     if os.path.exists(f'{F}/{score} {n} lev.json') or os.path.exists(f'{F}/{score} {n} library.wav')]
            if not names:
                continue
            part = names[0]
            ns = measure(F, score, part)
            pf = [n['P'] for n in ns if 'Legato' in n['articulation'] or 'Performance' in n['articulation']]
            ot = [n['P'] for n in ns if not ('Legato' in n['articulation'] or 'Performance' in n['articulation'])]
            res[sec] = dict(all=st.median(n['P'] for n in ns), perf=st.median(pf) if pf else None, other=st.median(ot) if ot else None,
                            n=len(ns), nperf=len(pf), arts=sorted({n['articulation'] for n in ns}))
            r = res[sec]
            print(f"  {sec:10s} notes {r['n']:3d} (Performance {r['nperf']:3d}) all {r['all']:.2f}  Performance "
                  f"{r['perf'] if r['perf'] is None else round(r['perf'], 2)}  other {r['other'] if r['other'] is None else round(r['other'], 2)}  {r['arts']}", flush=True)
        def d(a, b, k):
            return (res[a][k] - res[b][k]) if a in res and b in res and res[a][k] is not None and res[b][k] is not None else float('nan')
        for k in ('all', 'perf', 'other'):
            print(f"  {k}: Va-V1 {d('Violas', 'Violins 1', k):+.2f}  Vc-Va {d('Celli', 'Violas', k):+.2f}  V2-V1 {d('Violins 2', 'Violins 1', k):+.2f}")
        json.dump(res, open(f'{F}/{score} parts.json', 'w'), indent=1)
