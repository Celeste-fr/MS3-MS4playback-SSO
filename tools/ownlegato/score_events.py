#!/usr/bin/env python3
"""A score's Performance notes for our patch (make_sfz.py's SFZ through sfizz), from the heard check's dump
(tst_playbackaudit auditScore, MS_AUDIT_DUMP: predicted.tsv + "<route> museScore.txt"), and a copy of the dump in which
those notes play on our patch, so heard_check.analyse measures our render by its own rules.

    score_events.py <dump> <out dir> --part Violas --patch "Violas - Performance" --sfz-json <make_sfz's .json>
                    [--bars 7-13] [--route ol]

Writes <out>/dump/ (the dump with the part's Performance rows moved to one route <route>, their sentMs = -L_t for a
transition, -L_a for an attack, and that route's events) and <out>/<route>.txt (kthost / sfzrender events: dump time +
heard_check.OFFSET_S).

How MuseScore would play our patch (what the events here do):
  note-on   at its written time + the start lead - L_t (a transition: the note before still held) or - L_a (an attack);
            the key and velocity as MuseScore sends them to SSO (the dump's note-on: Auto's technique by velocity)
  note-off  the note before of a transition: 24 ms after the new note-on (the map's overlap); else the written end
            (MuseScore's Performance note-offs, as in the dump)
  bend      the note's cents from its key at 8192 + cents * 8192 / 100 (+-100 cents); after silence set 2 ms before the
            note-on; a transition's bend glides linearly over the last GLIDE_MS before its arrival (note-on + L_t), every
            5 ms (the regions' median glide D; make_sfz.json)
  CC11      each note's CC11 from the dump (the level MuseScore sends with the note), at the new note-on
  CC1       the dump's (the route's first value)
  CC68      127 just before a transition's note-on, 0 just before a legato line's last note-off: our patch's releases
            play only with it up (make_sfz.py: no release of the note before in legato)
The Performance notes merge into one route: our patch bends every tuning, no tuning lanes.
"""
import argparse
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import heard_check as hc  # noqa: E402

OVERLAP_S = 0.024
REATTACK_OFF_MS = 30.0     # a re-attack's note before ends this long after the attack's onset (at L_t): ended at the
                           # attack sample's start (L_t - its onset) the old voice was gone (REL_MS 50: -16 dB in 10 ms)
                           # before the attack rose, a 3.6 / 5.1 dB dip (Whence bars 9 / 10); --reattack-off


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('dump')
    ap.add_argument('out')
    ap.add_argument('--part', default='Violas')
    ap.add_argument('--patch', default='Violas - Performance')
    ap.add_argument('--sfz-json', required=True)
    ap.add_argument('--bars', default='7-13')
    ap.add_argument('--route', default='ol')
    ap.add_argument('--lt', type=float)
    ap.add_argument('--la', type=float)
    ap.add_argument('--overlap', type=float, default=OVERLAP_S * 1000,
                    help='ms the note before ends after a transition note-on (round 2 equal-power hand-over: 0)')
    ap.add_argument('--no-cc68', action='store_true', help='round 2: releases by sw_previous, no CC68 sent')
    ap.add_argument('--reattack-off', type=float, default=REATTACK_OFF_MS)
    ap.add_argument('--reattack', help='bar:beat:velocity,...: those notes as re-attacks on our patch')
    a = ap.parse_args()
    S = json.load(open(a.sfz_json))
    lt = a.lt if a.lt is not None else S['lt']
    la = a.la if a.la is not None else S['la']
    glide = max(5.0, round(S['D']['median'] / 5) * 5)
    b0, b1 = (int(v) for v in a.bars.split('-'))
    lead = hc.read_lead_ms(a.dump) / 1000
    rows = hc.read_rows(a.dump)
    routemap = hc.read_routemap(a.dump)
    mine = [r for r in rows if r['part'] == a.part and r['patch'] == a.patch]
    # --reattack bar:beat:velocity: that note of the part (whatever patch MuseScore gave it) played on our patch as a
    # re-attack straight after the legato note before (make_sfz.py --reattack: velocity 111-116 spiccato, 117-127
    # accented), the note before's note-off at the attack's onset
    for spec in (a.reattack.split(',') if a.reattack else []):
        bar, beat, vel = spec.split(':')
        for r in rows:
            if r['part'] == a.part and r['bar'] == int(bar) and abs(r['beat'] - float(beat)) < 1e-3 and r not in mine:
                prev = max((q for q in mine if q['written'] < r['written']), key=lambda q: q['written'])
                r.update(reattack=int(vel), transition=True, fromWritten=prev['written'])
                mine.append(r)
    # each row's note-on in its route's events (key, velocity, CC11 at that instant)
    ev = {}
    for route in {r['route'] for r in mine}:
        ev[route] = [l.split() for l in hc._read(os.path.join(a.dump, route + ' museScore.txt')).splitlines() if l.strip()]
    cc1 = 64
    for route in ev:
        for e in ev[route]:
            if e[1] == 'cc' and e[2] == '1':
                cc1 = int(e[3])
                break
    for r in mine:
        t = r['written'] + lead + r['sentMs'] / 1000
        cand = [e for e in ev[r['route']] if e[1] == 'on' and abs(float(e[0]) - t) < 0.0006]
        if not cand:
            raise SystemExit('no note-on for %s bar %d beat %.2f at %.6f' % (r['route'], r['bar'], r['beat'], t))
        r['key'], r['vel'] = int(cand[0][2]), int(cand[0][3])
        if r.get('reattack'):
            r['vel'] = r['reattack']
        c11 = [e for e in ev[r['route']] if e[1] == 'cc' and e[2] == '11' and abs(float(e[0]) - t) < 0.0006]
        r['cc11'] = int(c11[-1][3]) if c11 else None
    mine.sort(key=lambda r: (r['written'], r['pitch']))
    sel = [r for r in mine if b0 <= r['bar'] <= b1]
    out = []
    bend = 8192
    last_cc11 = None
    for i, r in enumerate(sel):
        L = (lt if r['transition'] else la) / 1000
        on = r['written'] + lead - L
        r['sentNew'] = -L * 1000
        want = (r['pitch'] * 100 + r['cents']) - r['key'] * 100
        pb = max(0, min(16383, int(round(8192 + want * 8192 / 100))))
        if r['cc11'] is not None and r['cc11'] != last_cc11:
            out.append((on, 'cc', 11, r['cc11']))
            last_cc11 = r['cc11']
        if r.get('reattack') and i > 0:
            out.append((on - 0.002, 'pb', pb, 0))
            out.append((on, 'on', r['key'], r['vel']))
            out.append((on + (lt + a.reattack_off) / 1000, 'off', sel[i - 1]['key'], 0))
        elif r['transition'] and i > 0 and sel[i - 1]['written'] < r['written']:
            # glide the bend over the last `glide` ms before the arrival
            t_a = on + lt / 1000
            n = int(glide / 5)
            for k in range(1, n + 1):
                out.append((t_a - glide / 1000 + k * 0.005, 'pb', int(round(bend + (pb - bend) * k / n)), 0))
            if not a.no_cc68:
                out.append((on - 0.001, 'cc', 68, 127))
            out.append((on, 'on', r['key'], r['vel']))
            out.append((on + a.overlap / 1000, 'off', sel[i - 1]['key'], 0))
        else:
            out.append((on - 0.002, 'pb', pb, 0))
            out.append((on, 'on', r['key'], r['vel']))
        bend = pb
        nxt = sel[i + 1] if i + 1 < len(sel) else None
        if not (nxt and nxt['transition'] and abs(nxt['fromWritten'] - r['written']) < 1e-6):
            if not a.no_cc68:
                out.append((r['writtenEnd'] + lead - 0.001, 'cc', 68, 0))
            out.append((r['writtenEnd'] + lead, 'off', r['key'], 0))
    t0 = min(e[0] for e in out) - 0.5
    head = [(t0, 'cc', 1, cc1), (t0, 'cc', 11, 127), (t0, 'pb', 8192, 0)]
    order = {'cc': 0, 'pb': 0, 'off': 1, 'on': 2}
    out = sorted(head + out, key=lambda e: (e[0], order[e[1]]))
    os.makedirs(a.out, exist_ok=True)
    lines = ['%.6f %s %d %d' % (e[0] + hc.OFFSET_S, e[1], e[2], e[3]) for e in out]
    hc._write(os.path.join(a.out, a.route + '.txt'), '\n'.join(lines) + '\n')
    # the dump, the part's Performance rows on route <route>
    d = os.path.join(a.out, 'dump')
    if os.path.isdir(d):
        shutil.rmtree(d)
    shutil.copytree(a.dump, d)
    hc._write(os.path.join(d, a.route + ' museScore.txt'), '\n'.join('%.6f %s %d %d' % e for e in out) + '\n')
    lines = hc._read(os.path.join(d, 'predicted.tsv')).splitlines()
    head_ = lines[0].split('\t')
    ir, isent = head_.index('route'), head_.index('sentMs')
    byid = {r['id']: r for r in sel}
    allmine = {r['id'] for r in mine}
    new = [lines[0]]
    for i, ln in enumerate(lines[1:]):
        c = ln.split('\t')
        if i in allmine:
            c[ir] = a.route
            if i in byid:
                c[isent] = '%.2f' % byid[i]['sentNew']
        new.append('\t'.join(c))
    hc._write(os.path.join(d, 'predicted.tsv'), '\n'.join(new) + '\n')
    rm = hc._read(os.path.join(d, 'routemap.txt')).rstrip('\n')
    hc._write(os.path.join(d, 'routemap.txt'), rm + '\n%s|%s|%s (own)|0|nan|100.0\n' % (a.route, a.part, a.patch))
    print('%d notes (bars %d-%d), L_t %.0f ms, L_a %.0f ms, bend glide %.0f ms; events %s'
          % (len(sel), b0, b1, lt, la, glide, os.path.join(a.out, a.route + '.txt')))


if __name__ == '__main__':
    main()
