#!/usr/bin/env python3
"""Known-answer tests for onset_detect.py: synthetic string-like notes whose onset is known.

    python3 tools/soundlibraries/test_onset_detect.py [-v]          unit tests (a few seconds)
    python3 tools/soundlibraries/test_onset_detect.py --report [out]  every condition -> onset_detect_accuracy.json

Synthetic signals only (numpy): this measures the detectors, not SSO. The notes are played as the measurement runs
played SSO: a passage's odd and even notes on two instances (rows as fam11.py split writes them), each note held to
the next note-on + 30 ms (fam5.OVER), written = on (no early start, so arrival - written is the error).

The notes
  spectrum    bowed: harmonics 1/k in amplitude (the ideal bowed string's sawtooth, Helmholtz motion; Fletcher &
              Rossing, The Physics of Musical Instruments, 2nd ed. 1998, sec. 10.1), up to 0.45 sr, random phases.
  attack      linear in amplitude from 0 to 1 over T, then held. Its rise time R (30 dB to 6 dB under the peak in
              5 ms power windows, as ArticulationCheck's startMs -> fullMs) is 0.4696 T. R from SSO itself: the
              string patches' mf fullMs - startMs in sso_articulation_timing.json (293 articulations of Violins 1/2,
              Violas, Celli, Basses), its 5/25/50/75/95th percentiles 25, 70, 170, 420, 865 ms, plus Long (Rachm.)'s
              median 248 ms (8 values, 247.5). ATTACKS_MS below; test_attacks_from_sso recomputes them from the file.
  release     after the note-off, exponential: 30 dB down after 845 ms, the median releaseMs of the strings' Long
              and Long (Rachm.) in the same file (9 values). Cut at 90 dB down.
  tempo       110 bpm sixteenths (the violas passage of the split runs and the standard test score's tempo) and
              the slow run's x4.
  vibrato     Seashore 1938 (Psychology of Music) as commonly cited: violinists' vibrato about 6.5 Hz, about a
              quarter tone wide (+-25 cents); +-50 cents to show what width does.
  noise       white noise 60 / 40 / 20 dB under the note's held power: CHOSEN levels, a sweep.
  registers   vln 60-84, vla 48-72, vc 38-62 (MIDI): the harmonics detector's frame is 2048 / 4096 / 8192 points.

Ground truth, per note
  physical    the first sample of the new note (its note-on).
  perceptual  Vos & Rasch 1981's convention, which the detectors claim: where the note's own amplitude envelope
              (the synthetic one, alone, no smoothing) first reaches 15 dB under its own peak: on + 0.1778 T (or
              0.1778 of the held length when the note is released before T).
Errors are detector - truth (ms; + = late). Per condition: median (bias), population SD, and the share of notes
without a reading.
"""
import json
import os
import statistics as st
import sys
import unittest

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _HERE)
import onset_detect as D  # noqa: E402

SR = 44100
ATTACKS_MS = [25, 70, 170, 248, 420, 865]       # rise times R (see the header)
RISE_PER_T = 10 ** (-6 / 20) - 10 ** (-30 / 20)  # R = 0.4696 T for a linear amplitude ramp
PAT_FRAC = 10 ** (-15 / 20)                      # 0.1778: 15 dB under the peak
RELEASE_30DB_S = 0.845
OVER = 0.03                                      # fam5.OVER: the note before held 30 ms into the next
SIXTEENTH_S = 60 / 110 / 4
REGISTERS = {'vln': (60, 84), 'vla': (48, 72), 'vc': (38, 62)}
VIBRATO = (6.5, 25.0)
ISOLATED_GAP_S = 3.0


def attacks_from_sso(path=os.path.join(_HERE, 'sso_articulation_timing.json')):
    d = json.load(open(path))
    strings = ('Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses')
    rise, rachm = [], []
    for patch, arts in d.items():
        if patch not in strings and not any(patch.startswith(s + ' - ') for s in strings):
            continue
        for name, v in arts.items():
            if not isinstance(v, dict) or 'startMs' not in v:
                continue
            s, f = v['startMs'][1], v['fullMs'][1]
            if s < 0 or f < 0:
                continue
            rise.append(f - s)
            if name == 'Long (Rachm.)':
                rachm.append(f - s)
    return [float(np.percentile(rise, q)) for q in (5, 25, 50, 75, 95)], st.median(rachm), len(rise)


def release_from_sso(path=os.path.join(_HERE, 'sso_articulation_timing.json')):
    d = json.load(open(path))
    return st.median(d[p][a]['releaseMs'] for p in ('Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses')
                     for a in ('Long', 'Long (Rachm.)') if a in d[p])


# ---- synthesis -------------------------------------------------------------------------------------------------

def add_note(buf, pitch, on, off, attack_ms, rng, vibrato=None, level=1.0):
    """adds one bowed note to buf; returns (physical onset, perceptual onset) in s"""
    T = attack_ms / 1000 / RISE_PER_T
    held = off - on
    tail = RELEASE_30DB_S * 3            # 90 dB down
    n0, n1 = int(round(on * SR)), min(len(buf), int(round((off + tail) * SR)))
    t = np.arange(n1 - n0) / SR
    env = np.minimum(1.0, t / T)
    peak = min(1.0, held / T)
    rel = t >= held
    env[rel] = peak * 10 ** (-1.5 * (t[rel] - held) / RELEASE_30DB_S)
    f0 = D.hz(pitch)
    if vibrato:
        rate, cents = vibrato
        ph0 = rng.uniform(0, 2 * np.pi)
        inst = f0 * 2 ** (cents / 1200 * np.sin(2 * np.pi * rate * (on + t) + ph0))
        phase = 2 * np.pi * np.cumsum(inst) / SR
    else:
        phase = 2 * np.pi * f0 * t
    top = SR * 0.45 / (2 ** ((vibrato[1] if vibrato else 0) / 1200))
    sig = np.zeros(len(t))
    k = 1
    while k * f0 < top:
        sig += np.sin(k * phase + rng.uniform(0, 2 * np.pi)) / k
        k += 1
    buf[n0:n1] += level * env * sig
    return on, on + PAT_FRAC * peak * T


def held_power(pitch):
    """mean power of a held bowed note at level 1: sum of (1/k)^2 / 2"""
    f0, top = D.hz(pitch), SR * 0.45
    return sum(0.5 / k ** 2 for k in range(1, int(top / f0) + 1) if k * f0 < top)


def render(pitches, step_s, attack_ms, seed, vibrato=None, noise_db=None, isolated=False):
    """rows (as fam11.py split), the two instances' signals, truths {i: (physical, perceptual)}"""
    rng = np.random.default_rng(seed)
    n = len(pitches)
    on = [0.5 + i * step_s for i in range(n)]
    rows = []
    for i, p in enumerate(pitches):
        off = on[i] + step_s if isolated or i + 1 == n else on[i + 1] + OVER
        if isolated:
            off = on[i] + 1.5
        rows.append({'i': i, 'written': on[i], 'on': on[i], 'off': off, 'pitch': p,
                     'pos': 1 if isolated else (0 if i == 0 else i), 'inst': i % 2})
    length = int((on[-1] + 1.5 + RELEASE_30DB_S * 3 + 0.5) * SR)
    bufs = [np.zeros(length), np.zeros(length)]
    truth = {}
    for r in rows:
        truth[r['i']] = add_note(bufs[r['inst']], r['pitch'], r['on'], r['off'], attack_ms, rng, vibrato)
    if noise_db is not None:
        pw = st.median(held_power(p) for p in pitches)
        for b in bufs:
            b += rng.normal(0, np.sqrt(pw * 10 ** (noise_db / 10)), len(b))
    return rows, bufs, truth


# ---- pitch patterns ---------------------------------------------------------------------------------------------

def walk(reg, n, seed):
    """a stepwise line in the register (steps 1-5 semitones), never the pitch two back"""
    lo, hi = REGISTERS[reg]
    rng = np.random.default_rng(seed)
    ps = [(lo + hi) // 2]
    while len(ps) < n:
        p = ps[-1] + int(rng.choice([-5, -4, -3, -2, -1, 1, 2, 3, 4, 5]))
        if lo <= p <= hi and (len(ps) < 2 or p != ps[-2]):
            ps.append(p)
    return ps


def alternate(reg, n, interval_two_back):
    """i and i-2 interval_two_back apart (0: X Y X Y ...), i and i-1 a third (4) apart"""
    lo, hi = REGISTERS[reg]
    a = lo + 2
    ps = []
    for i in range(n):
        base = a + (interval_two_back if (i // 2) % 2 else 0)
        ps.append(base + (4 if i % 2 else 0))
    return [min(hi, p) for p in ps]


def isolated_line(reg):
    lo, hi = REGISTERS[reg]
    return list(range(lo, hi + 1, 2))


# ---- one run ----------------------------------------------------------------------------------------------------

def run(pitches, step_s, attack_ms, seed, **kw):
    """per note: truths and the three detectors' readings (raw: without the 30 ms subtraction where it applies)"""
    rows, bufs, truth = render(pitches, step_s, attack_ms, seed, **kw)
    envs = D.envelopes(bufs, SR)
    loud, total = D.loudness_arrivals(rows, envs, latency_ms=0)
    harm, _, _ = D.harmonic_arrivals(rows, bufs, SR)
    notes = []
    for r in rows:
        if r['pos'] == 0:
            continue
        phys, pat = truth[r['i']]
        env, first, hop = envs[r['inst']]
        iso = D.isolated_onset(env, first, hop, r['on'], latency_ms=0) if kw.get('isolated') else None
        notes.append({'pat_ms': (pat - phys) * 1000, 'loud_raw': loud.get(r['i']), 'harm': harm.get(r['i']),
                      'iso_raw': iso, 'attack': attack_ms, 'pitch': r['pitch']})
    return notes


def conditions():
    """name -> list of (pitches, step_s, attack_ms, seed, kwargs)"""
    out = {}
    def add(name, pitch_fn, step, **kw):
        out[name] = [(pitch_fn(reg, si), step, a, 1000 * si + ai, kw)
                     for si, reg in enumerate(REGISTERS) for ai, a in enumerate(ATTACKS_MS)]
    add('isolated', lambda reg, s: isolated_line(reg), ISOLATED_GAP_S, isolated=True)
    add('slurred 110 bpm 16ths', lambda reg, s: walk(reg, 48, s), SIXTEENTH_S)
    add('slurred slow (x4)', lambda reg, s: walk(reg, 24, s), SIXTEENTH_S * 4)
    add('repeated pitch X Y X, 110', lambda reg, s: alternate(reg, 48, 0), SIXTEENTH_S)
    add('repeated pitch X Y X, slow', lambda reg, s: alternate(reg, 24, 0), SIXTEENTH_S * 4)
    for iv, nm in ((12, 'octave'), (7, 'fifth'), (5, 'fourth')):
        add(f'two back a {nm}, 110', lambda reg, s, iv=iv: alternate(reg, 48, iv), SIXTEENTH_S)
        add(f'two back a {nm}, slow', lambda reg, s, iv=iv: alternate(reg, 24, iv), SIXTEENTH_S * 4)
    add('vibrato 6.5 Hz +-25 c, isolated', lambda reg, s: isolated_line(reg), ISOLATED_GAP_S, isolated=True,
        vibrato=VIBRATO)
    add('vibrato 6.5 Hz +-25 c, 110', lambda reg, s: walk(reg, 48, s), SIXTEENTH_S, vibrato=VIBRATO)
    add('vibrato 6.5 Hz +-50 c, 110', lambda reg, s: walk(reg, 48, s), SIXTEENTH_S, vibrato=(6.5, 50.0))
    for nd in (-60, -40, -20):
        add(f'noise {nd} dB, isolated', lambda reg, s: isolated_line(reg), ISOLATED_GAP_S, isolated=True,
            noise_db=nd)
        add(f'noise {nd} dB, slow', lambda reg, s: walk(reg, 24, s), SIXTEENTH_S * 4, noise_db=nd)
    return out


def stats(errs, total):
    v = [e for e in errs if e is not None]
    return {'n': total, 'read': len(v), 'no_reading_pct': round(100 * (1 - len(v) / total), 1) if total else None,
            'bias_ms': round(st.median(v), 1) if v else None, 'sd_ms': round(st.pstdev(v), 1) if v else None}


def summarize(notes, isolated):
    """per detector, errors against both truths; loudness and isolated with the shipped 30 ms subtracted"""
    def errs(key, sub, truth):
        return [None if n[key] is None else n[key] - sub - (n['pat_ms'] if truth == 'perceptual' else 0)
                for n in notes]
    res = {}
    dets = [('loudness', 'loud_raw', D.LATENCY_MS), ('harmonics', 'harm', 0)]
    if isolated:
        dets.append(('isolated onset', 'iso_raw', D.LATENCY_MS))
    for name, key, sub in dets:
        res[name] = {t: stats(errs(key, sub, t), len(notes)) for t in ('physical', 'perceptual')}
        if name == 'isolated onset':  # its max(0, ...) applied after the subtraction
            e = [None if n[key] is None else max(0.0, n[key] - sub) - n['pat_ms'] for n in notes]
            res[name]['perceptual'] = stats(e, len(notes))
            res[name]['physical'] = stats([None if n[key] is None else max(0.0, n[key] - sub) for n in notes],
                                          len(notes))
    both = [n['harm'] - (n['loud_raw'] - D.LATENCY_MS) for n in notes if n['harm'] is not None and n['loud_raw'] is not None]
    res['harmonics - loudness'] = {'n': len(both), 'median_ms': round(st.median(both), 1) if both else None,
                                   'sd_ms': round(st.pstdev(both), 1) if both else None}
    return res


def report(out_path):
    result = {'about': 'test_onset_detect.py --report: onset_detect.py against synthetic bowed notes with known '
                       'onsets (NOT SSO). bias = median error (ms, + late), sd = population SD, against the physical '
                       'onset and the 15 dB perceptual onset (Vos & Rasch 1981) of each synthetic note; loudness '
                       'and isolated onset with the shipped 30 ms subtracted.',
              'attacks_ms': ATTACKS_MS, 'release_30db_ms': RELEASE_30DB_S * 1000, 'sample_rate': SR,
              'conditions': {}, 'by_attack': {}}
    iso_notes = []
    all_notes = {}
    for name, runs in conditions().items():
        notes = []
        for pitches, step, a, seed, kw in runs:
            ns = run(pitches, step, a, seed, **kw)
            notes += ns
        all_notes[name] = notes
        result['conditions'][name] = summarize(notes, kw.get('isolated', False))
        if name == 'isolated':
            iso_notes = notes
        print(name, json.dumps({k: (v['perceptual'] if 'perceptual' in v else v)
                                for k, v in result['conditions'][name].items()}), flush=True)
    for name in ('isolated', 'slurred 110 bpm 16ths', 'slurred slow (x4)'):
        result['by_attack'][name] = {str(a): summarize([n for n in all_notes[name] if n['attack'] == a],
                                                       name == 'isolated') for a in ATTACKS_MS}
    # the latency the loudness detector should subtract: raw arrival - perceptual onset, isolated notes
    lat = [n['loud_raw'] - n['pat_ms'] for n in iso_notes if n['loud_raw'] is not None]
    lat_iso = [n['iso_raw'] - n['pat_ms'] for n in iso_notes if n['iso_raw'] is not None]
    per = {str(a): round(st.median(n['loud_raw'] - n['pat_ms'] for n in iso_notes
                                   if n['attack'] == a and n['loud_raw'] is not None), 1) for a in ATTACKS_MS}
    phys = {str(a): round(st.median(n['loud_raw'] for n in iso_notes
                                    if n['attack'] == a and n['loud_raw'] is not None), 1) for a in ATTACKS_MS}
    result['latency_replacement_ms'] = {
        'loudness_to_perceptual': round(st.median(lat), 1), 'loudness_to_perceptual_sd': round(st.pstdev(lat), 1),
        'isolated_onset_to_perceptual': round(st.median(lat_iso), 1),
        'loudness_to_perceptual_by_attack': per, 'loudness_to_physical_by_attack': phys,
        'shipped': D.LATENCY_MS,
        'how': 'median over the isolated notes (3 registers x 6 attacks x 12-13 pitches) of the raw arrival '
               '(no subtraction) minus the note\'s 15 dB perceptual onset'}
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(result, f, indent=1)
        f.write('\n')
    print('latency', json.dumps(result['latency_replacement_ms']))


# ---- unit tests -------------------------------------------------------------------------------------------------

class OnsetDetect(unittest.TestCase):
    def test_attacks_from_sso(self):
        q, rachm, n = attacks_from_sso()
        self.assertEqual(sorted([round(x) for x in q] + [round(rachm)]), ATTACKS_MS)
        self.assertEqual(round(release_from_sso()), RELEASE_30DB_S * 1000)

    def test_isolated_fast_attack_near_perceptual(self):
        notes = run(isolated_line('vla')[:6], ISOLATED_GAP_S, 25, 1, isolated=True)
        e = [n['loud_raw'] - n['pat_ms'] for n in notes]
        self.assertEqual(len(e), 6)
        self.assertTrue(0 < st.median(e) < 60, e)          # a smoothed envelope reads late, never early
        h = [n['harm'] - n['pat_ms'] for n in notes if n['harm'] is not None]
        self.assertEqual(len(h), 6)

    def test_slow_attack_reads_later_physically(self):
        fast = run(isolated_line('vln')[:4], ISOLATED_GAP_S, 25, 2, isolated=True)
        slow = run(isolated_line('vln')[:4], ISOLATED_GAP_S, 420, 2, isolated=True)
        self.assertGreater(st.median(n['loud_raw'] for n in slow), st.median(n['loud_raw'] for n in fast) + 50)

    def test_repeated_pitch_two_back_has_no_harmonic_reading(self):
        notes = run(alternate('vla', 12, 0), SIXTEENTH_S * 4, 70, 3)
        self.assertTrue(all(n['harm'] is None for n in notes[1:]))

    def test_masked_sixteenths_loudness_mostly_unread(self):
        notes = run(walk('vla', 24, 4), SIXTEENTH_S, 70, 4)
        read = sum(n['loud_raw'] is not None for n in notes)
        self.assertLess(read, len(notes) / 2)

    def test_written_shift(self):
        """arrival is against written: moving written later by 100 ms reads 100 ms earlier"""
        rows, bufs, _ = render(isolated_line('vln')[:3], ISOLATED_GAP_S, 70, 5, isolated=True)
        envs = D.envelopes(bufs, SR)
        a, _ = D.loudness_arrivals(rows, envs)
        for r in rows:
            r['written'] += 0.1
        b, _ = D.loudness_arrivals(rows, envs)
        for i in a:
            self.assertAlmostEqual(a[i] - b[i], 100.0, places=6)


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == '--report':
        report(sys.argv[2] if len(sys.argv) > 2 else os.path.join(_HERE, 'onset_detect_accuracy.json'))
    else:
        unittest.main()
