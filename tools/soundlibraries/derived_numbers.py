#!/usr/bin/env python3
"""Numbers the fork derives from the committed SSO measurements (the owner's rule, 2026-10-03: every number measured,
computed by a stated algorithm, or taken from software or a book). Each subcommand prints the value and how it was
reached; the code quotes it.

    derived_numbers.py marcato       Marcato level range (until 2026-10-08 libmscore/articulation.h MarcatoLevel::MIN_DB / MAX_DB)
    derived_numbers.py levels        an articulation's or technique text's level range (MarcatoLevel::MIN_DB / MAX_DB)
    derived_numbers.py sensitivity   how much the map's measured values move when a threshold inside the measuring
                                     algorithm is varied (the owner's decision 3C, 2026-10-03)

marcato: SSO's marcato samples against the same instrument's plain held note ("Long", or "Long (Muted)" for a muted
marcato) at each dynamic both were measured at (sso_sound_dynamics.json "curve": the loudest 50 ms in dB, the level the
renderer's dynamics calibration reads), a marcato playing at its dynamic's level as a plain note does (no accent boost
since 2026-10-02). The range they span, rounded outward to the Inspector's 0.5 dB step, is the spin box's range
(the owner, 2026-10-03: "the range SSO's marcatos actually span").

levels (the owner, 2026-10-08: every notation that chose a technique gets an Inspector level): as marcato, for every
measured sound the map plays (an Articulation with techniques=, no drum hit) against its plain held note ("Long", or "Long (X)" for a sound "... (X)"), each dynamic both
were measured at. The range spans what SSO's techniques differ from the held note, so a level can bring any of them to
the held note's loudness or as far beyond it again.

sensitivity: per threshold, the map values the alternatives give against the one used.
"""
import bisect
import json
import math
import os
import re
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load(name):
    with open(os.path.join(HERE, name), encoding='utf-8') as f:
        return json.load(f)


def marcato():
    d = load('sso_sound_dynamics.json')
    diffs = []
    for patch, e in d.items():
        for sound, v in e.items():
            if not isinstance(v, dict) or 'arcato' not in sound or 'curve' not in v:
                continue
            m = re.search(r'\((.*)\)', sound)
            plain = f'Long ({m[1]})' if m else 'Long'
            if plain not in e or 'curve' not in e[plain]:
                continue
            lo, mm = dict(e[plain]['curve']), dict(v['curve'])
            for x in sorted(set(mm) & set(lo)):
                diffs.append((round(mm[x] - lo[x], 1), patch, sound, x))       # (the data: 0.1 dB)
    diffs.sort()
    low, high = diffs[0], diffs[-1]
    print(f'{len(diffs)} marcato/plain pairs (patch x sound x dynamic); median {statistics.median(x[0] for x in diffs):+.1f} dB')
    print(f'lowest  {low[0]:+.1f} dB: {low[1]} {low[2]} at {low[3]}')
    print(f'highest {high[0]:+.1f} dB: {high[1]} {high[2]} at {high[3]}')
    print(f'range, rounded outward to 0.5 dB: {math.floor(low[0] * 2) / 2:+.1f} ... {math.ceil(high[0] * 2) / 2:+.1f} dB')


def levels():
    sys.path.insert(0, HERE)
    from calibration_from_sound_dynamics import MAP, map_articulations
    played = map_articulations(MAP)         # only what a notation plays (the map's Articulations with techniques=)
    d = load('sso_sound_dynamics.json')
    diffs = []
    for patch, e in d.items():
        for sound, v in e.items():
            if not isinstance(v, dict) or 'curve' not in v or sound.startswith('Long') or 'key' in v:
                continue
            if not played.get(patch, {}).get(v.get('value', -1)):
                continue
            m = re.search(r'\((.*)\)', sound)
            plain = f'Long ({m[1]})' if m and f'Long ({m[1]})' in e else 'Long'
            if plain not in e or not isinstance(e[plain], dict) or 'curve' not in e[plain]:
                continue
            lo, mm = dict(e[plain]['curve']), dict(v['curve'])
            for x in sorted(set(mm) & set(lo)):
                diffs.append((round(mm[x] - lo[x], 1), patch, sound, x))
    diffs.sort()
    low, high = diffs[0], diffs[-1]
    span = max(-low[0], high[0])
    print(f'{len(diffs)} technique/plain pairs (patch x sound x dynamic); median {statistics.median(x[0] for x in diffs):+.1f} dB')
    print(f'lowest  {low[0]:+.1f} dB: {low[1]} {low[2]} at {low[3]}')
    print(f'highest {high[0]:+.1f} dB: {high[1]} {high[2]} at {high[3]}')
    print(f'largest difference {span:.1f} dB; range +- that, rounded outward to 0.5 dB: '
          f'{-math.ceil(span * 2) / 2:+.1f} ... {math.ceil(span * 2) / 2:+.1f} dB')


# ---------------------------------------------------------------- sensitivity

def short_from_by(index):
    """gen_spitfire_sso.shortFrom with the sounding length read at another threshold (index into [6, 10, 15, 20] dB)"""
    lengths = load('sso_short_lengths.json')
    nxt = {'Short 0.5': 'Spiccato', 'Short 1.0': 'Short 0.5'}

    def curve(patch, sound):
        rows = lengths.get(patch, {}).get(sound)
        if not isinstance(rows, list):
            return None
        by = {}
        for r in rows:
            if r[3] and r[3][index] is not None and r[3][index] >= 0:
                by.setdefault(r[1], []).append(r[3][index])
        held = sorted(by)
        return (held, [statistics.median(by[h]) for h in held]) if len(held) >= 2 else None

    def sounding(c, seconds):
        held, ms = c
        x = seconds * 1000
        if x <= held[0]:
            return ms[0] / 1000
        if x >= held[-1]:
            return ms[-1] / 1000
        i = bisect.bisect_right(held, x)
        a, b = held[i - 1], held[i]
        return (ms[i - 1] + (ms[i] - ms[i - 1]) * (x - a) / (b - a)) / 1000

    out = {}
    for patch in lengths:
        for sound in nxt:
            a, b = curve(patch, sound), curve(patch, nxt[sound])
            if not a or not b:
                continue
            w, last = 2.5, None
            while w > 0.05:
                if abs(sounding(a, w) - w) <= abs(sounding(b, w) - w):
                    last = w
                else:
                    break
                w = round(w - 0.01, 2)
            out[(patch, sound)] = last
    return out


def bend_count(share):
    m = load('sso_patch_measurements.json')
    n = 0
    for name, e in m.items():
        pb = e.get('pitchBend') if isinstance(e, dict) else None
        if not pb:
            continue
        down, up = pb
        if -down < 50 or up < 50:
            continue
        if abs(up + down) > share * (up - down) / 2:
            continue
        n += 1
    return n


def legato_by(index):
    """per patch, the median legato delay by interval at another crossing (grid rows' tLeave / tMid / tArrive: the second
    note's share of the power 10 / 50 / 90 %, row[6] / [7] / [8]; the map uses tMid: gen_spitfire_sso.py)"""
    g = load('sso_legato_grid_pitches.json')
    out = {}
    for patch, e in g.items():
        for sound, v in e.items():
            rows = v.get('rows') if isinstance(v, dict) else None
            if not rows:
                continue
            by = {}
            for r in rows:
                t = r[index] if len(r) > index else None
                if len(r) > index and t is not None and t > 0 and abs(r[1]) < 12:
                    by.setdefault(r[1], []).append(t)
            for i, ts in by.items():
                out[(patch, sound, i)] = statistics.median(ts)
    return out


def sensitivity():
    print('Short lengths, from= (the last time within 10 dB of the peak; measured also at 6, 15 and 20 dB):')
    base = short_from_by(1)
    for idx, db in ((0, 6), (2, 15), (3, 20)):
        alt = short_from_by(idx)
        ch = [abs((alt.get(k) or 0) - (v or 0)) for k, v in base.items() if v is not None and alt.get(k) is not None]
        lost = sum(1 for k, v in base.items() if v is not None and alt.get(k) is None)
        print(f'  {db:2d} dB: {len(ch)} from= values, median change {statistics.median(ch):.2f} s, largest {max(ch):.2f} s'
              f', {sum(1 for c in ch if c > 0.05)} move over 0.05 s; {lost} no longer found')
    print('Bends cleanly (both ways >= 50 cents, within 3 % of each other):')
    for s in (0.015, 0.03, 0.06):
        print(f'  within {s * 100:.1f} %: {bend_count(s)} patches')
    print('Legato arrival (the 50 % crossing of the two notes\' power; also measured at 10 and 90 %):')
    try:
        mid = legato_by(7)
        for idx, name in ((6, '10 % (leave)'), (8, '90 % (arrive)')):
            alt = legato_by(idx)
            ch = [alt[k] - v for k, v in mid.items() if k in alt]
            print(f'  {name}: {len(ch)} patch x interval medians, median shift {statistics.median(ch):+.0f} ms, '
                  f'10-90 % {sorted(ch)[len(ch) // 10]:+.0f} … {sorted(ch)[len(ch) * 9 // 10]:+.0f} ms')
    except Exception as e:                      # noqa: BLE001 (the grid's layout: see onset_from_check.py)
        print('  not computed:', e)
    print('Release (to 30 dB under the level): measured at that one level only (sso_sound_range.json): '
          'varying it needs the VM (the rest check\'s range part at 15 and 60 dB)')


if __name__ == '__main__':
    {'marcato': marcato, 'levels': levels, 'sensitivity': sensitivity}[sys.argv[1] if len(sys.argv) > 1 else 'marcato']()
