# The grid's renders without the MuseScore GUI (branch stac-levels, 2026-10-17): MuseScore's own events from the Linux
# dump (tst_liveequivalence dumpEvents: "<route> museScore.txt", volumes.txt, routemap.txt), rendered per route through
# kthost (C:\claude\kt on the Windows VM: Kontakt 8 with MuseScore's setup of the patch, <patch>.vst3state), each route's
# audio times its patch volume (volumes.txt), measured as grid_ana.py measures a verify render. Checked against the
# 2026-10-17 verify renders (build 16d3dbd): gridA_mp's 285 cells, kthost - verify P median -0.03 dB, sd 0.21 (the
# take-to-take sd 0.19); calib.mscx's 30 figure - held values the same to 0.1 dB.
#
#   grid_kthost.py events <work> <dump>...      -> <work>/ev/<patch>.txt, list.txt, plan.json: per route one kthost file,
#                                                  each dump a segment (transport restart, controllers reset, 1 s before;
#                                                  4 s after its last event)
#   (VM)  run.cmd <work>                         -> <work>/r/<patch>.wav (performance_context/run.cmd)
#   grid_kthost.py measure <work> <score> <out folder>
#                                                -> "<out>/<score> <part> lev.json", grid_ana.py's cache (on, length,
#                                                   pitch, articulation = the patch, P, L, P0), from the first segment;
#                                                   then grid_ana.py cells / groups read them as a verify render's
# Window (grid_ana.py's): the note-on as sent to min(its note-off + 0.3 s, the part's next note-on), at least 0.1 s.
import json, os, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import perceived
from grid_ana import loud50

PRE, GAP = 1.0, 4.0


def events(work, dumps):
    os.makedirs(f'{work}/ev', exist_ok=True)
    routes = [l.split('|')[:3] for l in open(f'{dumps[0]}/routemap.txt') if l.strip()]
    plan, lst = {}, []
    for r, part, patch in routes:
        lines, t, segs = [], 0.5, []
        for d in dumps:
            ev = [l.split() for l in open(f'{d}/{r} museScore.txt') if l.strip()]
            if not any(x[1] == 'on' for x in ev):
                continue
            start = t + PRE
            lines += [(start - PRE + 0.01, 'tr 0 0'), (start - PRE + 0.05, 'pb 8192 0'), (start - PRE + 0.05, 'cc 11 127'),
                      (start - PRE + 0.05, 'cc 1 64')]
            lines += [(start + float(x[0]), ' '.join(x[1:])) for x in ev]
            segs.append(dict(dump=d, start=start))
            t = start + float(ev[-1][0]) + GAP
        if not segs:
            continue
        order = {'tr': -2, 'pb': -1, 'cc': 0, 'off': 1, 'on': 2}
        lines.sort(key=lambda l: (round(l[0], 6), order[l[1].split()[0]]))
        name = (patch + ('' if sum(1 for x in routes if x[2] == patch) == 1 else ' ' + r)).replace(' ', '_')
        open(f'{work}/ev/{name}.txt', 'w').write(''.join('%.6f %s\r\n' % l for l in lines))
        probe = next(int(l[1].split()[1]) for l in lines if l[1].startswith('on '))
        lst.append(f'{name}|{patch}|{probe}')
        plan[r] = dict(part=part, patch=patch, file=name, segs=segs)
    open(f'{work}/ev/list.txt', 'w').write('\r\n'.join(lst) + '\r\n')
    json.dump(plan, open(f'{work}/plan.json', 'w'), indent=1)


def notes(dump, r):
    on, out = {}, []
    for l in open(f'{dump}/{r} museScore.txt'):
        t, k, *x = l.split()
        if k == 'on':
            on[int(x[0])] = float(t)
        elif k == 'off' and int(x[0]) in on:
            out.append(dict(on=on.pop(int(x[0])), off=float(t), pitch=int(x[0]), r=r))
    return out


def volume(dump, r):
    return next(float(l.split('|')[-1]) for l in open(f'{dump}/volumes.txt') if l.startswith(r + '|'))


def measure(work, score, out, seg=0):
    plan = json.load(open(f'{work}/plan.json'))
    parts = {}
    for r, p in plan.items():
        parts.setdefault(p['part'], []).append(r)
    for part, routes in parts.items():
        wav = {r: sf.read(f"{work}/r/{plan[r]['file']}.wav", dtype='float32') for r in routes}
        mono = {r: w[0].mean(1) for r, w in wav.items()}
        ns = sorted((dict(n, start=plan[r]['segs'][seg]['start'], g=10 ** (volume(plan[r]['segs'][seg]['dump'], r) / 20))
                     for r in routes for n in notes(plan[r]['segs'][seg]['dump'], r)), key=lambda n: n['on'])
        rows = []
        for i, n in enumerate(ns):
            nxt = ns[i + 1]['on'] if i + 1 < len(ns) else 1e9
            end = max(min(n['off'] + 0.3, nxt), n['on'] + 0.1)
            a, sr = wav[n['r']]
            clip = a[int((n['start'] + n['on']) * sr):int((n['start'] + end) * sr)] * n['g']
            rows.append(dict(on=n['on'], length=n['off'] - n['on'], pitch=n['pitch'], articulation=plan[n['r']]['patch'],
                             P=perceived.perceived_db(clip, sr), P0=perceived.perceived_db(clip, sr, 0.0),
                             L=loud50(mono[n['r']] * n['g'], sr, n['start'] + n['on'], n['start'] + n['off'])))
        json.dump(rows, open(f'{out}/{score} {part} lev.json', 'w'))
        print(score, part, len(rows), flush=True)


if __name__ == '__main__':
    if sys.argv[1] == 'events':
        events(sys.argv[2], sys.argv[3:])
    elif sys.argv[1] == 'measure':
        measure(sys.argv[2], sys.argv[3], sys.argv[4])
