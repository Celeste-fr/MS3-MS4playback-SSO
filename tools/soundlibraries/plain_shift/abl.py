# tools/soundlibraries/plain_shift/abl.py (recovered 2026-10-26 from the private plain-shift copy of qt-bends' abl.py, only
# its paths made repo-relative): the render side of sso_plain_performance_shift.json's shiftMs (<Instrument plainShift>).
# The recipe that made the committed shiftMs (2026-10-17, Heard passages.musicxml, PLAINb, 2 passes, per string part):
#   1. dumps of share/verifyplayback/Heard passages.musicxml at Recommended (with a build of that day's main):
#        d/CUR/dump  tst_playbackaudit auditScore with MS_AUDIT_SCORE=<score> MS_AUDIT_DUMP=<work>/d/CUR/dump
#                    (predicted.tsv, routemap.txt, lead.txt, bars.txt and the routes' events)
#        d/CUR/ev    tst_liveequivalence dumpEvents with MS_DUMP_SCORE=<score> MS_DUMP_CHUNKED=1 MS_DUMP_OUT=<work>/d/CUR/ev
#                    (gains.txt)
#   2. abl.py make <work> <part> <bar0> <bar1>   (ABL_ONLY=PLAINb renders only that variant; part as the score names it,
#      e.g. Violins 1, Violoncellos)
#   3. VM: the <work>/ev rows rendered by kthost with MuseScore's resaved setups (tools/vm/vmjob.py + kthost_batch.ps1, as
#      tools/soundlibraries/accent_attack.py's header shows; list.txt is in <work>/ev), the WAVs fetched to <work>/r/
#   4. abl.py mix <work>; latency.py <work> <bar0> <bar1> PLAINb > <part>.json: its "all" median is the part's shiftMs
#      (plain_performance_shift.py shift writes them into the json). The VM renders were not kept.
#
# abl.py: which of MuseScore's adjustments make a passage sound worse than SSO's Performance patch played plainly (the
# owner, 2026-10-16, Whence's Violas bars 9-12: "sound better ... when all of it is played entirely within the
# performance patch with no parameters tweaked at all"). Variants of one part's passage, rendered through kthost on the
# VM (one process per patch and pass, every variant a segment after a transport restart), mixed back per variant with
# the gain stage, then measured by the heard check's analysis (tools/playbackverify/heard_check.py) on the mix.
#
#   abl.py make <work> <part> <bar0> <bar1>     variants -> <work>/ev/<patch>_p<pass>.txt, list.txt, plan.json
#   abl.py mix <work>                           <work>/r/*.wav -> <work>/v/<variant>/wav/mix.wav (+ dump/), and
#                                               <work>/wav/<variant>_p<pass>.wav (16-bit 48 kHz, the passage), _lm (the
#                                               loudness-matched copy, to PLAINb's integrated loudness, BS.1770 by
#                                               loudness.py's weighting where available, else RMS)
#
# <work>/d/<name>/ holds the dumps (tst_playbackaudit MS_AUDIT_DUMP in dump/, tst_liveequivalence dumpEvents chunked in
# ev/, for gains.txt) of the settings variants, d/CUR Recommended. Variants:
#   settings (their own dumps): CUR, noTE (slurs/transitionsEarly 0), noTech (slurs/transitions 0, smoothStart 0),
#     noOnset (heldNotes/early 0), noEven (levels/evenLevels 0), noContext (levels/context 0), noLevels (calibrated,
#     realRange, micCompensation, sectionBalance 0);
#   event edits of CUR: V64 (every Performance note-on at velocity 64), MOVE (the notes on other patches put on the
#     Performance patch at their written start, as long as CUR plays them, their tuning by a bend step), JOIN (every
#     Performance note-off at its written end, moved with its note-on), NOBEND (no pitch bend: quarter tones of the
#     Performance notes in 12-TET), NOCC11 (no CC11 at all);
#   PLAIN: every note of the passage on the Performance patch at its written start and end, velocity 64, no CC11, the
#     quarter tones by MuseScore's bend (the measured glide and smoothing, as libraryPitchBends, gen.py's bends_k);
#     PLAINa no CC1 (the patch's own), PLAINb CC1 as CUR's Performance routes play the written dynamic, PLAIN12 PLAINb
#     without bends.
import csv, json, math, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SETTINGS = ['CUR', 'noTE', 'noTech', 'noOnset', 'noEven', 'noContext', 'noLevels']
EDITS = ['V64', 'MOVE', 'JOIN', 'NOBEND', 'NOCC11']
PLAINS = ['PLAINa', 'PLAINb', 'PLAIN12']
SHIFTS = {}                     # (make: <work>/shifts.json, {"PLAINSHIFT": {"attack": ms, "transition": ms}, ...})


def is_staccato(r):
    """a staccato as the score writes it (the row's techniques: staccato / staccatissimo / spiccato)"""
    t = r.get('techniques', '') + ',' + r.get('articulation', '')
    return any(w in t.lower() for w in ('staccat', 'spiccato'))
PASSES = int(os.environ.get('ABL_PASSES', '2'))
PASS0 = int(os.environ.get('ABL_PASS0', '1'))
GAP_S = 4.0                     # silence after a segment's last note-off (the Legato's release 2.6 s)
PRE_S = 1.0                     # a segment's controllers and transport restart before its first event
OFFSET_S = 1.0                  # (heard_check.py's: the mix's sample 0 is the window's start - 1 s)
# every string section's Performance patch: range, bendSmoothing, bendGlide read from the map
import re as _re
MAP = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'share', 'soundlibraries',
                   'Spitfire Symphony Orchestra.xml')
PERF = {'Violins 1': 'Violins 1 - Performance', 'Violins 2': 'Violins 2 - Performance', 'Violas': 'Violas - Performance',
        'Violoncellos': 'Celli - Performance', 'Contrabasses': 'Basses - Performance'}
BEND, STATES = {}, {}
_m = open(MAP, encoding='utf-8').read()
for _p in PERF.values():
    _blk = _m[_m.index('<Instrument name="%s"' % _p):]
    _blk = _blk[:_blk.index('</Instrument>')]
    _R = float(_re.search(r' bend="([\d.]+)"', _blk).group(1))
    _tau = float(_re.search(r'bendSmoothing="([\d.]+)"', _blk).group(1)) / 1000
    _gl = [tuple(int(v) for v in x.split(':')) for x in _re.search(r'bendGlide="([^"]+)"', _blk).group(1).split()]
    BEND[_p] = (_R, _tau, _gl)
    STATES[_p] = _p + '.vst3state'


def rd(p):
    return open(p, encoding='utf-8').read()


def rows_of(dump):
    return list(csv.DictReader(open(f'{dump}/predicted.tsv', encoding='utf-8'), delimiter='\t'))


def routemap(dump):
    out = {}
    for l in rd(f'{dump}/routemap.txt').splitlines():
        if l.strip():
            r, part, patch, lane, cents, bend = l.split('|')
            out[r] = dict(part=part, patch=patch, lane=int(lane), cents=float(cents), bend=float(bend))
    return out


def gains(evdir, part):
    """patch -> gain stage dB (gains.txt: part|patch|lane|...|total)"""
    g = {}
    for l in rd(f'{evdir}/gains.txt').splitlines()[1:]:
        x = l.split('|')
        if len(x) >= 7 and x[0] == part:
            g[x[1]] = float(x[-1])
    return g


def events(dump, route):
    return [l.split() for l in rd(f'{dump}/{route} museScore.txt').splitlines() if l.strip() and not l.startswith('#')]


def unit(c, R):
    return max(0, min(16383, int(round(8192 + c / R * (8192 if c < 0 else 8191)))))


def glide_at(gl, ms):
    if ms <= gl[0][0]:
        return gl[0][1], gl[0][2]
    for a, b in zip(gl, gl[1:]):
        if a[0] <= ms <= b[0]:
            f = (ms - a[0]) / (b[0] - a[0])
            return a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f
    return gl[-1][1], gl[-1][2]


def plain_bends(notes, R, tau, gl):
    """libraryPitchBends on one lane: a step at an attack, a transition's compensated glide (measured leave / arrival
    by the note before's length, one value so that the one-pole heard bend reaches the tuning at the arrival)"""
    out = []
    st = dict(heard=0.0, at=-1e9, sent=0.0)

    def hear(t):
        if t > st['at']:
            st['heard'] = st['sent'] + (st['heard'] - st['sent']) * math.exp(-(t - st['at']) / tau)
            st['at'] = t

    def send(t, c):
        hear(t)
        out.append((t, unit(c, R)))
        st['sent'] = c

    for k, n in enumerate(notes):
        nxt = notes[k + 1]['on'] if k + 1 < len(notes) else None
        if not n['transition']:
            send(n['on'], n['cents'])
            st['heard'] = n['cents']
            continue
        leave, arrive = glide_at(gl, (n['on'] - notes[k - 1]['on']) * 1000)
        t0 = n['on'] + max(0.0, leave) / 1000
        t1 = max(t0 + 0.0005, n['on'] + max(0.0, arrive) / 1000)
        if nxt is not None and nxt > t0:
            t1 = min(t1, nxt)
        if t0 > n['on']:
            send(n['on'], notes[k - 1]['cents'])
        hear(t0)
        e = math.exp(-(t1 - t0) / tau)
        send(t0, max(-R, min(R, (n['cents'] - st['heard'] * e) / (1 - e))))
        if nxt is None or t1 < nxt:
            send(t1, n['cents'])
    return out


def window_rows(rows, part, b0, b1):
    return [r for r in rows if r['part'] == part and b0 <= int(r['bar']) <= b1]


def pair_ons(ev, rows):
    """the route's note-on events paired with its rows in order (same key): [(event index, row)]"""
    ons = [i for i, x in enumerate(ev) if x[1] == 'on' and int(x[3]) > 0]
    rr = sorted(rows, key=lambda r: float(r['written']) + float(r['sentMs']) / 1000)
    assert len(ons) == len(rr), (len(ons), len(rr))
    for i, r in zip(ons, rr):
        assert int(ev[i][2]) == int(r['pitch']), (ev[i], r['pitch'])
    return list(zip(ons, rr))


def segment(ev, pairs, keep, lead_s):
    """a route's events of the window's notes (keep: rows to keep) and the controllers between them, and the state in
    force before; times stay the dump's"""
    mine = [i for i, r in pairs if r in keep]
    if not mine:
        return None
    on_t = [float(ev[i][0]) for i in mine]
    keys = {int(ev[i][2]) for i in mine}
    t0, t_last = min(on_t) - 0.02, max(on_t)
    # each kept note's own off: the first off of its key after it
    offs = []
    for i in mine:
        for j in range(i + 1, len(ev)):
            if ev[j][1] == 'off' and ev[j][2] == ev[i][2] or ev[j][1] == 'on' and int(ev[j][3]) == 0 and ev[j][2] == ev[i][2]:
                offs.append(j)
                break
    t1 = max(float(ev[j][0]) for j in offs) if offs else t_last + 0.5
    state = {}
    for x in ev:
        if float(x[0]) >= t0:
            break
        if x[1] in ('cc', 'pb'):
            state[(x[1], x[2] if x[1] == 'cc' else '')] = x
    out = []
    sel = set(mine) | set(offs)
    for i, x in enumerate(ev):
        t = float(x[0])
        if i in sel or x[1] in ('cc', 'pb') and t0 <= t <= t1:
            out.append(x)
    init = [x for k, x in sorted(state.items())]
    return dict(events=out, init=init, t0=t0, t1=t1)


def make(work, part, b0, b1):
    d = f'{work}/d'
    cur = f'{d}/CUR/dump'
    if os.path.exists(f'{work}/shifts.json'):
        SHIFTS.update(json.load(open(f'{work}/shifts.json')))
    only = os.environ.get('ABL_ONLY', '').split(',') if os.environ.get('ABL_ONLY') else None
    plan = dict(part=part, bars=[b0, b1], segments=[], variants={})
    bars = [float(x) for x in rd(f'{cur}/bars.txt').split()]
    w_start = bars[b0 - 1] - 0.5           # (written time: half a second before the window's first bar)
    w_end = bars[b1] if b1 < len(bars) else bars[-1] + (bars[-1] - bars[-2])
    plan['writtenStart'] = w_start
    plan['writtenEnd'] = w_end

    def variant_from_dump(name, dump, evdir, edit=None):
        rm = routemap(dump)
        lead = float(rd(f'{dump}/lead.txt').split()[0]) / 1000
        rows = rows_of(dump)
        keep = window_rows(rows, part, b0, b1)
        g = gains(evdir, part)
        segs = []
        new_rows = []
        moved = []                          # MOVE: (time, key, vel, off time, cents) for the Performance lane 0
        perf_routes = [r for r, v in rm.items() if v['part'] == part and v['patch'] in BEND]
        for route, info in sorted(rm.items()):
            if info['part'] != part:
                continue
            rr = [r for r in rows if r['route'] == route]
            if not rr or not os.path.exists(f'{dump}/{route} museScore.txt'):
                continue
            ev = events(dump, route)
            pairs = pair_ons(ev, rr)
            kr = [r for r in keep if r['route'] == route]
            sg = segment(ev, pairs, kr, lead)
            if not sg:
                continue
            perf = info['patch'] in BEND
            if edit == 'MOVE' and not perf:
                for i, r in pairs:
                    if r in kr:
                        on = float(ev[i][0]) + float(-float(r['sentMs'])) / 1000      # (its written start)
                        off = next(float(x[0]) for x in ev[i + 1:] if x[1] == 'off' and x[2] == ev[i][2])
                        moved.append((on, int(ev[i][2]), int(ev[i][3]), on + (off - float(ev[i][0])), info['cents'], r))
                        new_rows.append(dict(r, sentMs='0'))
                continue
            es = sg['events']
            if perf and edit == 'V64':
                es = [x[:3] + ['64'] if x[1] == 'on' and int(x[3]) > 0 else x for x in es]
            if perf and edit == 'NOBEND':
                es = [x for x in es if x[1] != 'pb']
                sg['init'] = [x for x in sg['init'] if x[1] != 'pb']
            if edit == 'NOCC11':
                es = [x for x in es if not (x[1] == 'cc' and x[2] == '11')]
                sg['init'] = [x for x in sg['init'] if not (x[1] == 'cc' and x[2] == '11')]
            if perf and edit == 'JOIN':
                # each note-off at its written end, moved with its note-on's shift
                ons = {}
                for i, r in pairs:
                    if r in kr:
                        ons[float(ev[i][0])] = r
                out = []
                pending = {}
                for x in es:
                    if x[1] == 'on' and int(x[3]) > 0 and float(x[0]) in ons:
                        r = ons[float(x[0])]
                        pending.setdefault(x[2], []).append(float(x[0]) + float(r['writtenEnd']) - float(r['written']))
                        out.append(x)
                    elif x[1] == 'off' and pending.get(x[2]):
                        out.append(['%.6f' % pending[x[2]].pop(0)] + x[1:])
                    else:
                        out.append(x)
                es = sorted(out, key=lambda x: (float(x[0]), {'pb': -1, 'cc': 0, 'off': 1, 'on': 2}[x[1]]))
            segs.append(dict(patch=info['patch'], cents=info['cents'], route=route, events=es, init=sg['init'],
                             gain=g.get(info['patch'], 0.0), lead=lead))
            for r in kr:
                new_rows.append(r)
        if edit == 'MOVE':
            # onto the Performance patch's lane 0 (its route), each with a bend step to its tuning and back to the
            # lane's bend after it
            p0 = min(perf_routes, key=lambda r: rm[r]['lane'])
            s0 = next(s for s in segs if s['route'] == p0)
            R = BEND[rm[p0]['patch']][0]
            add = []
            for on, key, vel, off, cents, r in moved:
                c = 0.0 if not math.isfinite(cents) else cents
                add += [['%.6f' % on, 'pb', str(unit(c, R)), '0'], ['%.6f' % on, 'on', str(key), str(vel)],
                        ['%.6f' % off, 'off', str(key), '0']]
            # (each moved staccato a spiccato attack: a note of the lane sounding at its start ends 45 ms before it)
            es0 = [list(x) for x in s0['events']]
            for on, key, vel, off, cents, r in moved:
                for j, x in enumerate(es0):
                    if x[1] != 'off' or not (on - 0.045 < float(x[0]) <= on + 0.5):
                        continue
                    starts = [float(y[0]) for y in es0[:j] if y[1] == 'on' and y[2] == x[2] and int(y[3]) > 0]
                    if starts and starts[-1] < on:
                        x[0] = '%.6f' % (on - 0.045)
            s0['events'] = sorted(es0 + add, key=lambda x: (float(x[0]), {'pb': -1, 'cc': 0, 'off': 1, 'on': 2}[x[1]]))
        return dict(segments=segs, rows=new_rows, lead=lead)

    def plain(name):
        dump = cur
        lead = float(rd(f'{dump}/lead.txt').split()[0]) / 1000
        rows = sorted(window_rows(rows_of(dump), part, b0, b1), key=lambda r: float(r['written']))
        rm = routemap(dump)
        patch = PERF[part]
        perf = next((r for r, v in sorted(rm.items(), key=lambda kv: kv[1]['lane']) if v['part'] == part and v['patch'] == patch),
                    'perf')
        R, tau, gl = BEND[patch]
        # a staccato as long as CUR plays it (its own note-on to note-off), so that it stays a spiccato attack on the
        # patch (the note after starts > 40 ms later: no transition), as any player of the score's MIDI would send it
        played = {}
        for route, info in rm.items():
            if info['part'] != part or not os.path.exists(f'{dump}/{route} museScore.txt'):
                continue
            rr = [r for r in rows_of(dump) if r['route'] == route]
            if not rr:
                continue
            ev = events(dump, route)
            for i, r in pair_ons(ev, rr):
                off = next((float(x[0]) for x in ev[i + 1:] if x[1] == 'off' and x[2] == ev[i][2]), None)
                if off is not None:
                    played[(r['written'], r['pitch'])] = off - float(ev[i][0])
        notes = []
        for k, r in enumerate(rows):
            on = float(r['written']) + lead
            off = float(r['writtenEnd']) + lead
            if is_staccato(r) and (r['written'], r['pitch']) in played:
                off = on + played[(r['written'], r['pitch'])]
                # (and at least 45 ms before the next note: SSO joins a note-on < 40 ms after a note-off as a transition)
                if k + 1 < len(rows):
                    off = min(off, float(rows[k + 1]['written']) + lead - 0.045)
            prev = notes[-1] if notes else None
            if prev is not None and is_staccato(r) and prev['off'] > on - 0.045:
                # (a staccato is a spiccato attack on the patch: the note before ends 45 ms before it, as after a
                # slur's last note)
                prev['off'] = on - 0.045
            tr = prev is not None and prev['off'] >= on - 0.040
            notes.append(dict(on=on, off=off, key=int(r['pitch']), cents=float(r['cents']), transition=tr, row=r, shift=0.0))
        # PLAINSHIFT: every event earlier by one constant (<work>/shifts.json, from PLAINb's heard latency);
        # PLAINSHIFT2: attacks (after silence or a staccato) by one, transitions by another, a joined note before
        # still ending at the transition's note-on as it did (its own end moves with the later of the two)
        shift = SHIFTS.get(name)
        if shift:
            for k, n in enumerate(notes):
                n['shift'] = shift['transition' if n['transition'] else 'attack'] / 1000
            for k, n in enumerate(notes):
                nxt = notes[k + 1] if k + 1 < len(notes) else None
                joined = nxt is not None and nxt['transition'] and abs(n['off'] - nxt['on']) < 1e-6
                n['on'] -= n['shift']
                n['off'] = nxt['on'] - nxt['shift'] if joined else n['off'] - n['shift']
        es = []
        if name != 'PLAIN12':
            for t, v in plain_bends(notes, R, tau, gl):
                es.append(['%.6f' % t, 'pb', str(v), '0'])
        for n in notes:
            es += [['%.6f' % n['on'], 'on', str(n['key']), '64'], ['%.6f' % n['off'], 'off', str(n['key']), '0']]
        init = []
        if name in ('PLAINb', 'PLAIN12') or name.startswith('PLAINSHIFT'):
            cc1 = [x for x in events(dump, perf) if x[1] == 'cc' and x[2] == '1'] if perf != 'perf' else []
            # (plain-shift: every string route of Whence holds CC1 64 throughout, checked in the CUR dump)
            init.append(cc1[0] if cc1 else ['0', 'cc', '1', '64'])
        es = sorted(es, key=lambda x: (float(x[0]), {'pb': -1, 'cc': 0, 'off': 1, 'on': 2}[x[1]]))
        new_rows = []
        for k, n in enumerate(notes):
            r = dict(n['row'])
            r['route'] = perf
            r['patch'] = patch
            r['sentMs'] = '%.3f' % (-1000 * n['shift'])
            r['velocity'] = '64'
            r['transition'] = '1' if n['transition'] else '0'
            r['performance'] = '2' if n['transition'] else '1'
            r['fromPitch'] = str(notes[k - 1]['key']) if n['transition'] else '-1'
            r['fromWritten'] = notes[k - 1]['row']['written'] if n['transition'] else '-1'
            r['onsetMs'] = '0'
            new_rows.append(r)
        return dict(segments=[dict(patch=patch, cents=float('nan'), route=perf, events=es, init=init, gain=0.0, lead=lead)],
                    rows=new_rows, lead=lead)

    V = {'PLAINb': plain('PLAINb')}
    for name in []:
        if os.path.exists(f'{d}/{name}/dump/predicted.tsv'):
            V[name] = variant_from_dump(name, f'{d}/{name}/dump', f'{d}/{name}/ev')

    # MOVE's rows: the moved notes on the Performance lane, at their written start
    # (re-made here: their rows from CUR's window, route p0, sent 0)
    rm = routemap(cur)
    pass  # (MOVE's rows: not made here)
    mv = []
    for r in []:
        r = dict(r)
        if rm[r['route']]['patch'] not in BEND:
            r['route'] = p0
            r['sentMs'] = '0'
        mv.append(r)

    # the processes: one per patch / tuning and pass, the segments in order (PLAINa first: before any CC1 is sent)
    if only:
        V = {k: v for k, v in V.items() if k in only}
    order = (['PLAINa'] if 'PLAINa' in V else []) + [v for v in V if v != 'PLAINa']
    procs = {}
    for p in range(PASS0, PASS0 + PASSES):
        for name in order:
            for s in V[name]['segments']:
                key = s['patch'] if s['patch'] in BEND else (s['patch'], int(round(s['cents'])))
                pr = procs.setdefault((key, p), dict(t=0.5, lines=[], segs=[]))
                ts = [float(x[0]) for x in s['events']]
                if not ts:
                    continue
                a, b = min(ts), max(ts)
                start = pr['t'] + PRE_S
                pr['lines'].append((start - PRE_S + 0.01, 'tr 0 0'))
                if name != 'PLAINa' or pr['t'] > 0.5:
                    pr['lines'].append((start - PRE_S + 0.05, 'pb 8192 0'))
                    pr['lines'].append((start - PRE_S + 0.05, 'cc 11 127'))
                    pr['lines'].append((start - PRE_S + 0.05, 'cc 1 64'))
                for x in s['init']:
                    pr['lines'].append((start - PRE_S + 0.1, ' '.join(x[1:])))
                for x in s['events']:
                    pr['lines'].append((start + float(x[0]) - a, ' '.join(x[1:])))
                pr['segs'].append(dict(variant=name, route=s['route'], gain=s['gain'], fileStart=start, dumpStart=a,
                                       lead=s['lead']))
                pr['t'] = start + (b - a) + GAP_S
    os.makedirs(f'{work}/ev', exist_ok=True)
    lst = []
    pord = {'tr': -2, 'pb': -1, 'cc': 0, 'off': 1, 'on': 2}
    for (key, p), pr in sorted(procs.items(), key=lambda kv: str(kv[0])):
        state = STATES[key]
        nm = (key if isinstance(key, str) else f'{key[0]} {key[1]:+d}').replace(' ', '_') + f'_p{p}'
        lines = sorted(pr['lines'], key=lambda l: (round(l[0], 6), pord[l[1].split()[0]]))
        open(f'{work}/ev/{nm}.txt', 'w').write(''.join('%.6f %s\r\n' % (t, l) for t, l in lines))
        probe = next(int(l.split()[1]) for t, l in lines if l.startswith('on '))
        lst.append(f'{nm}|{state[:-len(".vst3state")]}|{probe}')
        plan['segments'] += [dict(s, file=nm, passNo=p) for s in pr['segs']]
    open(f'{work}/ev/list.txt', 'w').write('\r\n'.join(lst) + '\r\n')
    for name, v in V.items():
        plan['variants'][name] = dict(rows=v['rows'], lead=v['lead'])
    json.dump(plan, open(f'{work}/plan.json', 'w'), indent=0)
    print(len(lst), 'processes;', ' '.join(order))


def mix(work):
    import numpy as np, soundfile as sf
    plan = json.load(open(f'{work}/plan.json'))
    ws, we = plan['writtenStart'], plan['writtenEnd']
    sr = None
    out = {}
    for s in plan['segments']:
        f = f'{work}/r/{s["file"]}.wav'
        if not os.path.exists(f):
            print('missing', f)
            continue
        x, sr = sf.read(f, always_2d=True)
        # mix time m = dump time - lead - ws + OFFSET_S; dump time t sits at fileStart + (t - dumpStart)
        n = int((we - ws + OFFSET_S + 3.0) * sr)
        m0 = s['dumpStart'] - s['lead'] - ws + OFFSET_S          # the segment's first event in the mix
        f0 = s['fileStart']
        key = (s['variant'], s['passNo'])
        buf = out.setdefault(key, np.zeros((n, x.shape[1])))
        a = int(round(m0 * sr))
        b = int(round(f0 * sr))
        # from 0.3 s before its first event to the end of the mix window
        pre = int(0.3 * sr)
        lo = max(0, a - pre)
        seg = x[b - (a - lo): b - (a - lo) + (n - lo)]
        buf[lo:lo + len(seg)] += seg * 10 ** (s['gain'] / 20)
    os.makedirs(f'{work}/wav', exist_ok=True)
    passages = {}
    for (name, p), buf in sorted(out.items()):
        v = plan['variants'][name]
        vd = f'{work}/v/{name}_p{p}'
        os.makedirs(f'{vd}/wav', exist_ok=True)
        os.makedirs(f'{vd}/dump', exist_ok=True)
        sf.write(f'{vd}/wav/mix.wav', buf.astype('float32'), sr, subtype='FLOAT')
        # the heard check's dump: every row on route "mix"; lead so that the mix's time is (written - ws + 1 s)
        rows = v['rows']
        head = list(rows[0].keys())
        with open(f'{vd}/dump/predicted.tsv', 'w', encoding='utf-8') as fo:
            fo.write('\t'.join(head) + '\n')
            for r in rows:
                r = dict(r, route='mix')
                fo.write('\t'.join(str(r[k]) for k in head) + '\n')
        lead_ms = -ws * 1000
        open(f'{vd}/dump/lead.txt', 'w').write('%.3f\n' % lead_ms)
        open(f'{vd}/dump/routemap.txt', 'w').write('mix|%s|mix|0|0|0\n' % plan['part'])
        open(f'{vd}/dump/bars.txt', 'w').write(rd(f'{work}/d/CUR/dump/bars.txt'))
        # the passage only (from bar0 - 0.2 s to the window's end + 2 s), 16-bit 48 kHz
        import scipy.signal as ss
        a = int((OFFSET_S + 0.5 - 0.2) * sr)
        b = int((we - ws + OFFSET_S + 2.0) * sr)
        y = buf[a:b]
        if sr != 48000:
            y = ss.resample_poly(y, 48000, sr, axis=0)
        sf.write(f'{work}/wav/{name}_p{p}.wav', np.clip(y, -1, 1), 48000, subtype='PCM_16')
        passages[(name, p)] = y
    # loudness-matched copies: every passage at PLAINb pass 1's integrated loudness (ITU-R BS.1770, pyloudnorm)
    import pyloudnorm as pyln
    meter = pyln.Meter(48000)
    ref = meter.integrated_loudness(passages[('PLAINb', 1)]) if ('PLAINb', 1) in passages else -23.0
    lufs = {}
    for (name, p), y in sorted(passages.items()):
        l = meter.integrated_loudness(y)
        lufs[f'{name}_p{p}'] = round(l, 2)
        g = 10 ** ((ref - l) / 20)
        z = y * g
        peak = np.abs(z).max()
        if peak > 0.999:                       # (one gain for all if a copy would clip: the reference lowered)
            z = z * 0.999 / peak
        sf.write(f'{work}/wav/{name}_p{p}_lm.wav', z, 48000, subtype='PCM_16')
    json.dump(dict(referenceLufs=round(ref, 2), lufs=lufs), open(f'{work}/wav/loudness.json', 'w'), indent=1)
    print(len(out), 'mixes')


if __name__ == '__main__':
    if sys.argv[1] == 'make':
        make(sys.argv[2], sys.argv[3], int(sys.argv[4]), int(sys.argv[5]))
    elif sys.argv[1] == 'mix':
        mix(sys.argv[2])
