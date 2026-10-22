#!/usr/bin/env python3
"""transition_midpoint.py: SSO's Performance legato transitions, note-on to pitch MIDPOINT, as plain Performance plays
them (velocity 64), and the early send that puts each midpoint on its target: the data behind a proposed
`[slurs] transitionsEarly` mode 4 ("midpoint from the measured table"; docs/OPEN_ITEMS.md items 2-3). Measurement and
prediction only: MuseScore doesn't read the output.

    transition_midpoint.py table WORK... -o sso_transition_midpoint.json
          WORK: measure_patch.py work folders (analysed: WORK/result.json), named <section>_<grid>; grids g1 (spacing x
          relative early x before x B's length, intervals +-2), g2 (after a staccato), g3 (intervals, quarter tones), g4
          (turns: B back on the pitch before A)
    transition_midpoint.py predict TABLE --dump DUMPDIR [--heard FILE[:CLIP]] [--no-context] -o out.json
          every Performance transition of a dump (predicted.tsv: dumpEvents' trace) predicted as the dump sends it (each
          note's sentMs: plainTiming 0 every note plainShift early, plainTiming 1 its own) and under mode 4 (attacks by
          plainAttack, each transition early by its fixed point, chained); joined with heard readings by bar and beat
          (heard_midpoints.py's output, or the owner's clips' readings with :CLIP)
    transition_midpoint.py calibrate TABLE PRED... [--max-len 300] [--source TEXT]
          the in-context offset per section (median predicted - heard over the predictions' heard transitions after
          notes <= --max-len ms, odd / even bars as a check) written into TABLE as 'inContext'; predict then subtracts it
          from the delay of such transitions

THE RULE (docs/OPEN_ITEMS.md item 2, real players: tools/soundlibraries/spheres_legato_transitions.json): a real
section puts the midpoint of a slurred pitch change on its beat: -3 ms against its bowed attacks (Violins 1 / 2,
Violas, n 161, 90 % CI -11 .. +4), +19 ms for Celli / Basses (n 14, weak). Here the attacks sit on the beat (mode 4
times them by the map's plainAttack, as plainTiming 1 does), so the target is T = -3 / +19 ms against the written beat.

THE MODEL. delay(before, L, r, ioi, interval) = ms from B's note-on to its f0 midpoint (half way from A's written pitch
to B's; measure_patch.py's rule), where L is the note before's written length (its IOI), r = how much earlier B is sent
than the note before it (B's early minus A's: the played spacing is L - r, and that spacing is what SSO's script turns
into CC119, docs/SSO_SCRIPT_KNOWLEDGE.md), ioi = B's own written length (a note C follows by transition, sent as early
as B), before = attack (A after silence) / transition (Z, A, B in one direction) / staccato-attack (A attacked 45 ms after
a staccato Z). Linear in r and in log L between the measured values, clamped at the ends; B's length 136 or 300 ms
(longer: 300's); the interval by g3's difference from +-2 at the same cell (quarter tones spelled and bent as
[slurs] microtoneBends 1 sends them). A chain: e_0 = the attack's early, e_i solves e_i = delay(L_i, e_i - e_{i-1}) - T
(scanned, 1 ms steps, 0 .. L_i - 15 ms: SSO drops a legato note-on closer than 15 ms to the one before); in a run of
equal notes r -> 0, so e = delay(L, 0) - T. A note sent later than the one before (r < 0) by the spacing rule: the
delay of r = 0 after a note before L - r long (the measured delay follows the played spacing: 'spacingCheck'). A turn
(the interval's sign changes) by g4's own cells per interval. Mid readings at or before B's note-on are left out (the
f0 already half way: A never reached; 'unreached' counts them). In context (a whole passage, the round robins cycling)
SSO's fast transitions come sooner than in these isolated figures: 'inContext' (from a heard check, `calibrate`) is
subtracted after notes <= its maxLenBeforeMs.
"""
import argparse
import csv
import glob
import json
import math
import os
import re
import statistics as st
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
MAP = os.path.join(HERE, '..', '..', 'share', 'soundlibraries', 'Spitfire Symphony Orchestra.xml')
SECTIONS = {'vn1': 'Violins 1 - Performance', 'vn2': 'Violins 2 - Performance', 'va': 'Violas - Performance',
            'vc': 'Celli - Performance', 'cb': 'Basses - Performance'}
PARTS = {'Violins 1': 'Violins 1 - Performance', 'Violins 2': 'Violins 2 - Performance', 'Violas': 'Violas - Performance',
         'Violoncellos': 'Celli - Performance', 'Contrabasses': 'Basses - Performance'}
TARGET = {'Violins 1 - Performance': -3.0, 'Violins 2 - Performance': -3.0, 'Violas - Performance': -3.0,
          'Celli - Performance': 19.0, 'Basses - Performance': 19.0}
MIN_GAP_MS = 15.0           # SSO drops a legato note-on < 15 ms after the one before (TRANSITION_MIN_GAP_MS)
LENS = (136, 273, 500, 1000, 2182)


def q(v, f):
    v = sorted(v)
    return v[min(len(v) - 1, int(f * (len(v) - 1) + 0.5))]


def summ(v):
    v = [x for x in v if x is not None]
    if not v:
        return {'n': 0}
    return {'n': len(v), 'median': round(st.median(v), 1), 'p10': round(q(v, 0.1), 1), 'p90': round(q(v, 0.9), 1)}


def map_attrs(path=MAP):
    out = {}
    for inst in ET.parse(path).getroot().iter('Instrument'):
        if inst.get('name') in TARGET:
            out[inst.get('name')] = {'plainShift': float(inst.get('plainShift', 0)),
                                     'plainAttack': float(inst.get('plainAttack', 0))}
    return out


# ------------------------------------------------------------------------------------------------------------ table

def load(works):
    """{patch: {grid: [cells]}} from measure_patch.py work folders <section>_<grid>"""
    data = {}
    for w in works:
        name = os.path.basename(os.path.normpath(w))
        m = re.match(r'(vn1|vn2|va|vc|cb)_(g\d)$', name)
        if not m or not os.path.exists(os.path.join(w, 'result.json')):
            print('skipped %s' % w, file=sys.stderr)
            continue
        r = json.load(open(os.path.join(w, 'result.json')))
        data.setdefault(r['patch'], {})[m.group(2)] = r
    return data


def takes(c, name):
    """a cell's readings; mid / arrive at or before B's note-on are left out: the f0 was already half way from A to B
    when B started (A never reached: a turn whose note before was cut short), no change of B's own to time"""
    v = [x for x in c['measures'].get(name, {}).get('takes', []) if x is not None]
    return [x for x in v if x > 0] if name in ('mid', 'arrive') else v


def unreached(cells):
    """the share of mid readings left out (at or before the note-on), per cell kind"""
    n = k = 0
    for c in cells:
        v = [x for x in c['measures'].get('mid', {}).get('takes', []) if x is not None]
        n += len(v)
        k += sum(1 for x in v if x <= 0)
    return {'readings': n, 'atOrBeforeNoteOn': k}


def group(cells, keyf, name='mid'):
    g = {}
    for c in cells:
        k = keyf(c['key'])
        if k is None:
            continue
        g.setdefault(k, []).extend(takes(c, name))
    return g


def take_sd(cells, keyf, name='mid'):
    """the median over a group's cells of the SD between their takes"""
    g = {}
    for c in cells:
        k = keyf(c['key'])
        v = takes(c, name)
        if k is None or len(v) < 2:
            continue
        g.setdefault(k, []).append(st.stdev(v))
    return {k: round(st.median(v), 1) for k, v in g.items()}


def build_table(data, attrs):
    out = {}
    for patch, grids in sorted(data.items()):
        sec = {'target': TARGET[patch], 'plainShift': attrs[patch]['plainShift'],
               'plainAttack': attrs[patch]['plainAttack']}
        # main grid: before / L / ioi -> r -> mid (ms after B's note-on), pooled over pitches, +-2 and takes
        if 'g1' in grids:
            cells = grids['g1']['cells']
            kf = lambda k: '%s/%d/%d' % (k['before'], k['beforeLenMs'], k['ioiMs'])
            main = {}
            for name in ('leave', 'mid', 'arrive'):
                g = group(cells, lambda k: (kf(k), k.get('bEarlyMs', 0)), name)
                for (cell, r), v in g.items():
                    main.setdefault(cell, {}).setdefault(str(r), {})[name] = summ(v)
            sds = take_sd(cells, lambda k: (kf(k), k.get('bEarlyMs', 0)))
            for (cell, r), sd in sds.items():
                main[cell][str(r)]['mid']['takeSd'] = sd
            # the CC119 that reached B and the played spacing (the script's own writing)
            for c in cells:
                k = c['key']
                main[kf(k)][str(k.get('bEarlyMs', 0))]['spacingMs'] = k['beforeLenMs'] - k.get('bEarlyMs', 0)
            sec['main'] = main
        if 'g2' in grids:
            cells = grids['g2']['cells']
            kf = lambda k: ('%s/%d/%d' % (k['before'], k['beforeLenMs'], k['ioiMs']), k.get('bEarlyMs', 0))
            st2 = {}
            for name in ('leave', 'mid', 'arrive'):
                for (cell, r), v in group(cells, kf, name).items():
                    st2.setdefault(cell, {}).setdefault(str(r), {})[name] = summ(v)
            sec['staccatoAttack'] = st2
        if 'g3' in grids:
            cells = grids['g3']['cells']
            # interval -> (before, L, r) -> mid; and its difference from +-2 (same sign) in the same cell and pitch
            byk = {}
            for c in cells:
                k = c['key']
                byk[(k['before'], k['beforeLenMs'], k.get('bEarlyMs', 0), k['pitch'], k['interval'])] = takes(c, 'mid')
            iv = {}
            for (b, L, r, p, i), v in byk.items():
                ref = byk.get((b, L, r, p, 2 if i > 0 else -2))
                if not v or not ref:
                    continue
                d = st.median(v) - st.median(ref)
                iv.setdefault(str(i), {}).setdefault('%s/%d/%d' % (b, L, r), []).append(d)
                iv[str(i)].setdefault('all', []).append(d)
                iv[str(i)].setdefault('L%d' % L, []).append(d)
            sec['interval'] = {i: {cell: summ(v) for cell, v in sorted(g.items())} for i, g in sorted(iv.items(), key=lambda x: float(x[0]))}
            g3 = {}
            for name in ('mid',):
                for (cell, i), v in group(cells, lambda k: ('%s/%d/%d' % (k['before'], k['beforeLenMs'], k.get('bEarlyMs', 0)),
                                                            k['interval'])).items():
                    g3.setdefault(str(i), {})[cell] = summ(v)
            sec['byInterval'] = {i: g3[i] for i in sorted(g3, key=float)}
        sec['unreached'] = {g: unreached(r['cells']) for g, r in sorted(grids.items())}
        if 'g4' in grids:
            # turns (Z A B with B back on Z's pitch), per interval: interval -> 'turn/L/136' -> r -> mid
            cells = grids['g4']['cells']
            tu = {}
            for name in ('leave', 'mid', 'arrive'):
                for (i, cell, r), v in group(cells, lambda k: (str(k['interval']), 'turn/%d/%d' % (k['beforeLenMs'], k['ioiMs']),
                                                               k.get('bEarlyMs', 0)), name).items():
                    tu.setdefault(i, {}).setdefault(cell, {}).setdefault(str(r), {})[name] = summ(v)
            sec['turn'] = {i: tu[i] for i in sorted(tu, key=float)}
        # the spacing hypothesis: does delay depend on the played spacing L - r alone? (r > 0 cells against r = 0 cells of
        # the L nearest that spacing, interpolated in log L)
        if 'main' in sec:
            res = []
            for cell, rs in sec['main'].items():
                b, L, ioi = cell.split('/')
                for r, m in rs.items():
                    r = float(r)
                    if r == 0 or m['mid'].get('median') is None:
                        continue
                    s = float(L) - r
                    ref = interp_main(sec['main'], b, s, 0.0, int(ioi), clamp=False)
                    if ref is not None:
                        res.append({'cell': cell, 'r': r, 'spacingMs': s, 'mid': m['mid']['median'], 'midAtSpacing': round(ref, 1),
                                    'diff': round(m['mid']['median'] - ref, 1)})
            sec['spacingCheck'] = {'rows': res, 'diff': summ([x['diff'] for x in res])}
            sec['fixedPoint'] = fixed_points(sec)
        out[patch] = sec
    return out


def interp1(xs, ys, x, clamp=True):
    pts = sorted((a, b) for a, b in zip(xs, ys) if b is not None)
    if not pts:
        return None
    if x <= pts[0][0]:
        return pts[0][1] if clamp or abs(x - pts[0][0]) < 1e-9 else None
    if x >= pts[-1][0]:
        return pts[-1][1] if clamp or abs(x - pts[-1][0]) < 1e-9 else None
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if x0 <= x <= x1:
            return y0 + (y1 - y0) * (x - x0) / (x1 - x0) if x1 > x0 else y0


def interp_main(main, before, L, r, ioi, clamp=True, name='mid'):
    """delay at the note before's length L (log-linear between measured lengths) and relative early r (linear)"""
    ioi = 136 if ioi < 218 else 300
    ls = sorted({int(c.split('/')[1]) for c in main if c.startswith(before + '/') and c.endswith('/%d' % ioi)})
    if not ls:
        return None
    vals = []
    for l in ls:
        rs = main['%s/%d/%d' % (before, l, ioi)]
        xs = [float(k) for k in rs]
        ys = [rs[k][name].get('median') for k in rs]
        vals.append(interp1(xs, ys, r, clamp))
    return interp1([math.log(l) for l in ls], vals, math.log(max(1.0, L)), clamp)


def delay(sec, before, L, r, ioi, interval=2, name='mid'):
    """the model: ms from B's note-on to its midpoint"""
    b = before if before in ('attack', 'transition') else ('transition' if before == 'turn' else 'attack')
    if r < 0:
        # (B sent LATER than the note before, relative to it: a longer spacing, L - r; by the spacing rule -- the delay
        # follows the played spacing, 'spacingCheck' -- the r = 0 delay of a note before that long)
        L, r = L - r, 0.0
    if before == 'turn' and sec.get('turn'):
        keys = [k for k in sec['turn'] if (float(k) > 0) == (interval > 0)] or list(sec['turn'])
        k = min(keys, key=lambda x: abs(abs(float(x)) - abs(interval)))
        return interp_main(sec['turn'][k], 'turn', L, r, 136, name=name)
    if before == 'staccato-attack' and 'staccatoAttack' in sec:
        d = interp_main(sec['staccatoAttack'], 'staccato-attack', L, r, 136, name=name)
    else:
        d = interp_main(sec['main'], b, L, r, ioi, name=name)
    if d is None:
        return None
    if name == 'mid' and 'interval' in sec and abs(interval) != 2 and before != 'turn':
        d += interval_offset(sec, b, L, r, interval)
    ctx = sec.get('inContext')
    if name == 'mid' and ctx and USE_CONTEXT[0] and L <= ctx['maxLenBeforeMs']:
        d -= ctx['offsetMs']
    return d


USE_CONTEXT = [True]


def interval_offset(sec, before, L, r, interval):
    """g3's mid difference from +-2 (same sign) for this interval: pooled over the note before's kind, r and pitches at
    the measured length nearest L (136 / 273 / 1000 ms; a single cell's 6 readings of a small interval are dominated by
    vibrato), else over all; the nearest measured interval within a semitone, else 0"""
    keys = sorted(sec['interval'], key=float)
    if not keys:
        return 0.0
    same = [k for k in keys if (float(k) > 0) == (interval > 0)] or keys
    k = min(same, key=lambda x: abs(float(x) - interval))
    if abs(float(k) - interval) > 1.01:
        return 0.0
    g = sec['interval'][k]
    near = min((136, 273, 1000), key=lambda l: abs(math.log(l) - math.log(max(1.0, L))))
    m = g.get('L%d' % near)
    if m and m.get('n', 0) >= 6 and m.get('median') is not None:
        return m['median']
    return g['all'].get('median', 0.0) or 0.0


def solve(sec, before, L, e_prev, ioi, interval, T):
    """the early send e (ms) of a transition that puts its midpoint at T against its written time, the note before sent
    e_prev early: e = delay(L, e - e_prev) - T, scanned over 0 .. e_prev + L - 15; (e, mid as written, found)"""
    best = None
    hi = int(e_prev + L - MIN_GAP_MS)
    for e in range(0, max(1, hi) + 1):          # (never later than written: mode 4 only sends early)
        d = delay(sec, before, L, e - e_prev, ioi, interval)
        if d is None:
            return None
        err = d - e - T
        if best is None or abs(err) < abs(best[2]):
            best = (e, d - e, err)
    if best is None:
        return None
    return {'earlyMs': best[0], 'midWritten': round(best[1], 1), 'found': abs(best[2]) <= 2.0}


def fixed_points(sec):
    """per main cell (before / L / ioi): the early send whose midpoint lands on T with the note before sent as early
    (a run: r = 0, e = delay(L, 0) - T) and after an attack sent plainAttack early (the slur's first transition)"""
    out = {}
    T = sec['target']
    for cell in sorted(sec['main']):
        b, L, ioi = cell.split('/')
        L, ioi = int(L), int(ioi)
        run = delay(sec, b, L, 0.0, ioi)
        first = solve(sec, b, L, sec['plainAttack'], ioi, 2, T) if b == 'attack' else None
        out[cell] = {'runEarlyMs': None if run is None else round(run - T, 1),
                     'midNowMs': None if run is None else round(run - sec['plainShift'], 1),
                     'afterAttack': first}
    return out


# ---------------------------------------------------------------------------------------------------------- predict

def dump_notes(dumpdir):
    rows = list(csv.DictReader(open(os.path.join(dumpdir, 'predicted.tsv')), delimiter='\t'))
    out = {}
    for r in rows:
        if r['part'] not in PARTS or 'Performance' not in r['patch']:
            continue
        out.setdefault(r['part'], []).append(r)
    for p in out:
        out[p].sort(key=lambda r: (float(r['written']), int(r['pitch'])))
    return out


def prev_of(r, rows):
    """the note a transition comes from: at its fromWritten, on its route (fromPitch is the key it was sent on, which
    [slurs] microtoneBends 1 may have moved), else the nearest pitch"""
    m = [p for p in rows if abs(float(p['written']) - float(r['fromWritten'])) < 1e-4]
    if not m:
        return None
    exact = [p for p in m if int(p['pitch']) == int(r['fromPitch'])]
    same = [p for p in m if p['route'] == r['route']]
    return (exact or same or sorted(m, key=lambda p: abs(int(p['pitch']) - int(r['fromPitch']))))[0]


def chains(rows):
    """each transition with its note before: [(row, prev row)]"""
    res = []
    for r in rows:
        if r['transition'] != '1':
            continue
        p = prev_of(r, rows)
        if p is not None:
            res.append((r, p))
    return res


def pitch_of(r):
    return int(r['pitch']) + float(r['cents']) / 100


def kind_of(r, prev, rows):
    if prev['transition'] == '1':
        pp = prev_of(prev, rows)
        if pp is not None and (pitch_of(r) - pitch_of(prev)) * (pitch_of(prev) - pitch_of(pp)) < 0:
            return 'turn'
        return 'transition'
    before = [p for p in rows if float(p['writtenEnd']) <= float(prev['written']) + 1e-6
              and float(prev['written']) - float(p['writtenEnd']) < 0.3]
    return 'staccato-attack' if before else 'attack'


SECTION_NAMES = {'Violins 1': 'Violins 1 - Performance', 'Violins 2': 'Violins 2 - Performance', 'Violas': 'Violas - Performance',
                 'Violoncellos': 'Celli - Performance', 'Celli': 'Celli - Performance', 'Contrabasses': 'Basses - Performance',
                 'Basses': 'Basses - Performance'}


def readings(spec):
    """FILE[:CLIP]: heard readings, a list of {part or section, bar, beat, mid[, leave, arrive, hcLeave]} (whence_mid.py's
    output, or the owner's clips' sso_clips.json, CLIP naming the clip) -> {(patch, bar, beat): row}"""
    path, _, clip = spec.partition(':')
    out = {}
    for x in json.load(open(path)):
        if clip and x.get('clip') != clip:
            continue
        patch = SECTION_NAMES.get(x.get('part') or x.get('section'))
        if patch:
            out[(patch, int(x['bar']), round(float(x['beat']), 3))] = x
    return out


def predict(a):
    """every Performance transition of a dump: its midpoint predicted as the dump sends it (each note's sentMs: e_i =
    -sentMs; r = e_i - e_prev), and under mode 4 (chained fixed points, attacks by plainAttack); joined with readings"""
    table = json.load(open(a.table))['sections']
    notes = dump_notes(a.dump)
    heard = readings(a.heard) if a.heard else {}
    out = []
    for part, rows in notes.items():
        patch = PARTS[part]
        sec = table.get(patch)
        if not sec or 'main' not in sec:
            continue
        T, shift, att = sec['target'], sec['plainShift'], sec['plainAttack']
        sent = {id(r): -float(r['sentMs']) for r in rows}
        early4 = {}
        for r, p in chains(rows):
            L = (float(r['written']) - float(p['written'])) * 1000
            ioi = (float(r['writtenEnd']) - float(r['written'])) * 1000
            iv = round(pitch_of(r) - pitch_of(p), 2)
            kind = kind_of(r, p, rows)
            e, ep = sent[id(r)], sent[id(p)]
            d = delay(sec, kind, L, e - ep, ioi, iv)
            ep4 = early4.get(id(p), att)
            s4 = solve(sec, kind, L, ep4, ioi, iv, T)
            if s4:
                early4[id(r)] = s4['earlyMs']
            row = {'part': part, 'bar': int(r['bar']), 'beat': float(r['beat']), 'written': round(float(r['written']), 4),
                   'pitch': pitch_of(r), 'interval': iv, 'before': kind, 'lenBeforeMs': round(L, 1), 'ioiMs': round(ioi, 1),
                   'target': T, 'sentEarlyMs': e, 'relEarlyMs': round(e - ep, 1),
                   'predMid': None if d is None else round(d - e, 1),
                   'mode4EarlyMs': s4['earlyMs'] if s4 else None, 'mode4Found': s4['found'] if s4 else None,
                   'predMidMode4': s4['midWritten'] if s4 else None,
                   'moveMs': None if not s4 else round(s4['earlyMs'] - e, 1)}
            h = heard.get((patch, int(r['bar']), round(float(r['beat']), 3)))
            if h:
                for k in ('mid', 'leave', 'arrive', 'hcLeave'):
                    if h.get(k) is not None:
                        row['heard' + k[0].upper() + k[1:]] = h[k]
            out.append(row)
    res = {'_about': 'transition_midpoint.py predict: dump %s, table %s, readings %s' % (
        os.path.basename(os.path.normpath(a.dump)), os.path.basename(a.table), os.path.basename(a.heard or '-')),
        'notes': out, 'summary': {}}
    for part in sorted({x['part'] for x in out}):
        v = [x for x in out if x['part'] == part]
        T = v[0]['target']
        s = {'n': len(v), 'predMid': summ([x['predMid'] for x in v]), 'predMidMode4': summ([x['predMidMode4'] for x in v]),
             'mode4EarlyMs': summ([x['mode4EarlyMs'] for x in v]), 'moveMs': summ([x['moveMs'] for x in v]),
             'found': sum(1 for x in v if x['mode4Found'])}
        hv = [x for x in v if x.get('heardMid') is not None and x['predMid'] is not None]
        if hv:
            s['heard'] = {'n': len(hv), 'heardMid': summ([x['heardMid'] for x in hv]), 'predMid': summ([x['predMid'] for x in hv]),
                          'predMinusHeard': summ([x['predMid'] - x['heardMid'] for x in hv]),
                          'corr': corr([x['predMid'] for x in hv], [x['heardMid'] for x in hv]),
                          'heardAbsFromTarget': round(st.median([abs(x['heardMid'] - T) for x in hv]), 1),
                          'predAbsFromTarget': round(st.median([abs(x['predMid'] - T) for x in hv]), 1)}
        res['summary'][part] = s
    json.dump(res, open(a.o, 'w'), indent=1)
    for part, s in res['summary'].items():
        print(part, json.dumps(s))


def corr(x, y):
    if len(x) < 3:
        return None
    mx, my = st.mean(x), st.mean(y)
    sx = math.sqrt(sum((a - mx) ** 2 for a in x))
    sy = math.sqrt(sum((b - my) ** 2 for b in y))
    return round(sum((a - mx) * (b - my) for a, b in zip(x, y)) / (sx * sy), 2) if sx and sy else None


def calibrate(a):
    """the table's in-context offset per section: the median of (predicted - heard) midpoints over a heard check's
    transitions after notes <= --max-len ms (one or more predict outputs with readings), with the two halves (odd / even
    bars) as a check of its stability; written into the table as 'inContext'"""
    USE_CONTEXT[0] = False
    t = json.load(open(a.table))
    rows = []
    for f in a.predictions:
        rows += json.load(open(f))['notes']
    for part, patch in PARTS.items():
        v = [x for x in rows if x['part'] == part and x.get('heardMid') is not None and x['predMid'] is not None
             and x['lenBeforeMs'] <= a.max_len]
        if len(v) < 10 or patch not in t['sections']:
            continue
        d = [x['predMid'] - x['heardMid'] for x in v]
        half = lambda k: round(st.median([x['predMid'] - x['heardMid'] for x in v if x['bar'] % 2 == k]), 1)
        t['sections'][patch]['inContext'] = {
            'offsetMs': round(st.median(d), 1), 'n': len(v), 'p10': round(q(d, 0.1), 1), 'p90': round(q(d, 0.9), 1),
            'oddBars': half(1), 'evenBars': half(0), 'corr': corr([x['predMid'] for x in v], [x['heardMid'] for x in v]),
            'maxLenBeforeMs': a.max_len, 'source': a.source}
        print(patch, t['sections'][patch]['inContext'])
    json.dump(t, open(a.table, 'w'), indent=1, sort_keys=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('table')
    p.add_argument('works', nargs='+')
    p.add_argument('--map', default=MAP)
    p.add_argument('-o', required=True)
    p = sub.add_parser('predict')
    p.add_argument('table')
    p.add_argument('--dump', required=True)
    p.add_argument('--heard', help='readings FILE[:CLIP]')
    p.add_argument('-o', required=True)
    p = sub.add_parser('calibrate')
    p.add_argument('table')
    p.add_argument('predictions', nargs='+')
    p.add_argument('--max-len', type=float, default=300.0)
    p.add_argument('--source', default='')
    p = [x for x in sub.choices.values()][1]
    p.add_argument('--no-context', action='store_true', help='without the table\'s in-context offsets')
    a = ap.parse_args()
    if getattr(a, 'no_context', False):
        USE_CONTEXT[0] = False
    if a.cmd == 'calibrate':
        calibrate(a)
    elif a.cmd == 'table':
        data = load(a.works)
        sections = build_table(data, map_attrs(a.map))
        methods = {patch: {g: r['method'] for g, r in grids.items()} for patch, grids in data.items()}
        for m in methods.values():
            for g in m.values():
                g['command'] = re.sub(r'\S*/(?=measure_patch\.py)', '', str(g.get('command', ''))).replace(os.path.expanduser('~'), '~')
        json.dump({'_about': ABOUT, 'method': methods, 'sections': sections}, open(a.o, 'w'), indent=1, sort_keys=True)
        print('%d sections -> %s' % (len(sections), a.o))
    else:
        predict(a)


ABOUT = ("SSO's five string sections' Performance legato transitions as plain Performance plays them (velocity 64 attacks "
         "and transitions, the note before held 24 ms into the next, CC1 64, CC11 127, CC119 written by the script from the "
         "spacing as played: [slurs] speedFromWritten 0, Recommended): ms from B's note-on to its f0 leave / MIDPOINT / arrive "
         "(0.1 / 0.5 / 0.9 of the way from A's written pitch to B's; YIN, measure_patch.py), by the note before (attack after "
         "silence / transition in a run Z A B / an attack 45 ms after a staccato), its written length L (136 .. 2182 ms), B's "
         "relative early send r (B sent r ms before its written time, the note before not: the played spacing L - r), B's own "
         "length (136 / 300 ms; a note C follows by transition, as early as B) and the interval (+-2 in 'main'; quarter tones "
         "spelled and bent as [slurs] microtoneBends 1 sends them, in 'interval' as the difference from +-2). Pooled over 3 "
         "pitches per section and 2 takes (ABBA, 1.6 s silences: round robins reset), random modulators on (as shipped). "
         "Measured 2026-10-22 on the Windows test VM: Kontakt 8 + SSO, the VM MuseScore's setups (library defaults, max voices "
         "512), offline, kthost (tools/soundlibraries/kthost) through measure_patch.py. 'fixedPoint': the early send that puts "
         "the midpoint on 'target' (ms against the written beat: real sections' slurred midpoints, -3 ms upper strings / +19 "
         "Celli and Basses, spheres_legato_transitions.json) in a run (r = 0: e = delay(L, 0) - target) and as a slur's first "
         "transition after an attack sent plainAttack early; midNowMs: the midpoint as Recommended plays it now (every note "
         "plainShift early). For a proposed [slurs] transitionsEarly mode 4; the renderer doesn't read this file.")

if __name__ == '__main__':
    main()
