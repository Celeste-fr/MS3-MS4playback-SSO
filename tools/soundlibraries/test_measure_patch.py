#!/usr/bin/env python3
"""Tests of measure_patch.py: a plan rendered by a synthetic instrument whose onsets, levels, releases and legato glides
are known, then analysed; and (when MS_KTHOST and MS_TESTSYNTH name the built kthost and mstestsynth.vst3) the whole
chain plan -> kthost (run --local) -> analyze on the test synth.

    python3 tools/soundlibraries/test_measure_patch.py [-v]
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import measure_patch as mp  # noqa: E402

SR = 44100
ONSET_MS = 60.0         # the synthetic instrument's attack: silent for this long after the note-on, then a 20 ms rise
RISE_MS = 20.0
RELEASE_60_MS = 600.0   # exponential decay: 60 dB in this long after the note-off
GLIDE = (80.0, 200.0)   # a legato transition's glide: from 80 ms to 200 ms after the new note-on (linear in cents)


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12)


def level_db(v):
    return (v - 64) * 0.1          # velocity 122: +5.8 dB


def synth(meta, name):
    """the file's audio as the synthetic instrument plays it (each cell's notes on one voice, as a legato patch)"""
    import numpy as np
    fi = next(f for f in meta['files'] if f['name'] == name)
    n = int((fi['seconds'] + 3) * SR)
    out = np.zeros(n)
    for c in meta['cells']:
        if c['file'] != name:
            continue
        notes = c['notes']
        t0 = notes[0][3]
        t1 = max(x[4] for x in notes) + RELEASE_60_MS / 1000 * 1.5
        i0, i1 = int(t0 * SR), min(n, int(t1 * SR))
        t = np.arange(i0, i1) / SR
        cents = np.full(len(t), 100.0 * notes[0][1])
        amp_db = np.full(len(t), level_db(notes[0][2]))
        for k in range(1, len(notes)):
            prev, cur = notes[k - 1], notes[k]
            legato = prev[4] > cur[3]
            if legato:
                g0, g1 = cur[3] + GLIDE[0] / 1000, cur[3] + GLIDE[1] / 1000
                frac = np.clip((t - g0) / (g1 - g0), 0, 1)
                cents = np.where(t >= cur[3], 100.0 * prev[1] + frac * 100.0 * (cur[1] - prev[1]), cents)
            else:
                cents = np.where(t >= cur[3], 100.0 * cur[1], cents)
        # amplitude: the first note's attack (and a fresh attack after a gap), the last note's release
        env = np.zeros(len(t))
        for k, note in enumerate(notes):
            on, off = note[3], note[4]
            fresh = k == 0 or notes[k - 1][4] <= on
            nxt = notes[k + 1][3] if k + 1 < len(notes) else None
            end = off if (nxt is None or notes[k][4] <= nxt) else nxt
            a0 = on + ONSET_MS / 1000 if fresh else on
            seg = (t >= on) & (t < end)
            rise = np.clip((t - a0) / (RISE_MS / 1000), 0, 1) if fresh else 1.0
            lin = 10 ** (level_db(note[2]) / 20)
            env = np.where(seg, lin * rise, env)
            if nxt is None or notes[k][4] <= nxt:
                tail = t >= end
                # (released from where it is at the note-off)
                at_off = lin * (np.clip((end - a0) / (RISE_MS / 1000), 0, 1) if fresh else 1.0)
                env = np.where(tail, np.maximum(env, at_off * 10 ** (-60 * (t - end) / (RELEASE_60_MS / 1000) / 20)), env)
        f = 440.0 * 2 ** ((cents / 100 - 69) / 12)
        ph = 2 * np.pi * np.cumsum(f) / SR
        sig = sum(np.sin(h * ph) / h for h in range(1, 9)) * 0.1
        out[i0:i1] += sig * env
    return np.stack([out, out], axis=1).astype(np.float32)


class Args:
    level_window = 400.0
    gain = 0.0

    def __init__(self, **kw):
        self.__dict__.update(kw)


class Synthetic(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.mkdtemp(prefix='mp-test-')
        mp.main(['plan', '--patch', 'Synth', '--out', cls.work, '--what', 'level,onset,release,transition',
                 '--pitches', '57:69:6', '--velocity', '64,122', '--lengths', '211,1000',
                 '--before', 'attack,transition,staccato', '--before-len', '300,600', '--intervals=-2,3',
                 '--ioi', '400', '--takes', '2', '--gap', '2.5', '--chunk-seconds', '60'])
        import soundfile as sf
        meta = json.load(open(os.path.join(cls.work, 'plan.json')))
        for fi in meta['files']:
            w = os.path.join(cls.work, fi['name'] + '.wav')
            sf.write(w, synth(meta, fi['name']), SR, subtype='FLOAT')
            mp.analyze_file(cls.work, fi['name'], w, Args())
        a = Args()
        a.work, a.o = cls.work, None
        cls.result = mp.analyze(a)
        cls.cells = cls.result['cells']

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.work, ignore_errors=True)

    def notes(self, **kw):
        return [c for c in self.cells if c['key']['kind'] == 'note' and all(c['key'][k] == v for k, v in kw.items())]

    def trans(self, **kw):
        return [c for c in self.cells if c['key']['kind'] == 'transition' and all(c['key'][k] == v for k, v in kw.items())]

    def test_plan_abba(self):
        meta = json.load(open(os.path.join(self.work, 'plan.json')))
        t0 = [c['label'] + str(c['pitch']) for c in meta['cells'] if c['take'] == 0]
        t1 = [c['label'] + str(c['pitch']) for c in meta['cells'] if c['take'] == 1]
        self.assertEqual(t0, list(reversed(t1)))
        self.assertGreater(len(meta['files']), 1)        # (cut at --chunk-seconds)

    def test_every_cell_measured_twice(self):
        self.assertEqual(self.result['missingFiles'], [])
        for c in self.cells:
            self.assertEqual(c['n'], 2, c['label'])

    def test_onset(self):
        for c in self.notes():
            o = c['measures']['onset15']['median']
            # (a 20 ms linear rise: the -15 dB point lies ~17 ms into it, less the rule's 30 ms latency and the
            # envelope's smoothing: within 20 ms of the true silent time)
            self.assertLess(abs(o - ONSET_MS), 20, (c['label'], o))     # (this rule reads a fast rise ~17 ms early)
            self.assertIsNotNone(c['measures']['onset6']['median'])
            self.assertGreaterEqual(c['measures']['onset6']['median'], o - 1)

    def test_level_by_velocity(self):
        for p in (57, 63, 69):
            lo = self.notes(pitch=p, velocity=64, lengthMs=1000)[0]['measures']['loud50']['median']
            hi = self.notes(pitch=p, velocity=122, lengthMs=1000)[0]['measures']['loud50']['median']
            self.assertAlmostEqual(hi - lo, level_db(122), delta=0.3)
            plo = self.notes(pitch=p, velocity=64, lengthMs=1000)[0]['measures']['peak']['median']
            phi = self.notes(pitch=p, velocity=122, lengthMs=1000)[0]['measures']['peak']['median']
            self.assertGreater(phi - plo, 3.0)

    def test_release(self):
        for c in self.notes(lengthMs=1000):
            r = c['measures']['release40']['median']
            self.assertAlmostEqual(r, RELEASE_60_MS * 40 / 60, delta=25)

    def test_glide(self):
        span = GLIDE[1] - GLIDE[0]
        for c in self.trans(before='attack') + self.trans(before='transition'):
            m = c['measures']
            self.assertAlmostEqual(m['leave']['median'], GLIDE[0] + 0.1 * span, delta=10, msg=c['label'])
            self.assertAlmostEqual(m['mid']['median'], GLIDE[0] + 0.5 * span, delta=10, msg=c['label'])
            self.assertAlmostEqual(m['arrive']['median'], GLIDE[0] + 0.9 * span, delta=10, msg=c['label'])
            self.assertAlmostEqual(m['heardRef']['median'], m['leave']['median'] + mp.POLFREMAN_MS, delta=0.05)

    def test_attack_after_staccato(self):
        # (the synthetic release, 60 dB in 600 ms, is only 4.5 dB down after the 45 ms gap: masked, no reading)
        for c in self.trans(before='staccato'):
            self.assertEqual(c['measures']['bMasked']['median'], 1, c['label'])
            self.assertIsNone(c['measures']['bOnset15']['median'])

    def test_report(self):
        class R:
            what_path = self.work
            png = False
        text = mp.report(R())
        self.assertIn('perceptual reference', text)
        self.assertIn('arrive', text)

    def test_merge(self):
        out = os.path.join(self.work, 'merged.json')
        mp.main(['merge', os.path.join(self.work, 'result.json'), '--by', 'before,beforeLenMs', '--kind', 'transition',
                 '-o', out])
        g = json.load(open(out))['sections']['Synth']['by']['attack/600']
        self.assertEqual(g['leave']['n'], 3 * 2 * 2)          # 3 pitches x 2 intervals x 2 takes, pooled
        self.assertAlmostEqual(g['leave']['median'], GLIDE[0] + 0.1 * (GLIDE[1] - GLIDE[0]), delta=10)

    def test_parse(self):
        self.assertEqual(mp.parse_list('48:54:3', int), [48, 51, 54])
        self.assertEqual(mp.parse_list('-2,2', int), [-2, 2])
        self.assertEqual(mp.parse_switch('cc:32:20'), {'type': 'cc', 'number': 32, 'value': 20})
        lib = os.path.join(HERE, '..', '..', 'share', 'soundlibraries', 'Spitfire Symphony Orchestra.xml')
        self.assertEqual(mp.map_switch(lib, 'Violas', 'Spiccato'), {'type': 'cc', 'number': 32, 'value': 42})
        self.assertIsNone(mp.map_switch(lib, 'Violas - Performance', 'Legato'))


class Microtones(unittest.TestCase):
    """quarter tones as [slurs] microtoneBends 1 sends them; the note after; turns"""

    def test_spelling(self):
        # B3 -50 after A3 goes on B-flat3 +50 (the step and the bend the same way); G3 +50 after A3 on A-flat3 -50
        n = lambda x: ('X', x, 64, 0.0, 1.0)
        self.assertEqual(mp.spell([n(57), n(58.5)], [False, True]), [(57, 0.0), (58, 50.0)])
        self.assertEqual(mp.spell([n(57), n(55.5)], [False, True]), [(57, 0.0), (56, -50.0)])
        # never the key of the note before: 60 -> 60.5 goes on 61 -50
        self.assertEqual(mp.spell([n(60), n(60.5)], [False, True])[1], (61, -50.0))

    def test_bends(self):
        bend = {'range': 100.0, 'tauMs': 70.0, 'glide': [(136.0, 65.0, 158.0), (300.0, 68.0, 186.0)]}
        notes = [('A', 57, 64, 1.0, 1.160), ('B', 58.5, 64, 1.136, 1.5)]
        keys = mp.spell(notes, [False, True])
        ev = mp.bend_events(notes, keys, [False, True], [1.0, 1.136], bend)
        t = [e[0] for e in ev]
        v = [e[2] for e in ev]
        self.assertEqual(v[0], 8192)                        # A: its own tuning at its note-on
        self.assertAlmostEqual(t[-1], 1.136 + 0.158, 3)     # B: the tuning at its arrival (bendGlideFingered)
        self.assertEqual(v[-1], 8192 + round(50 / 100 * 8191))
        self.assertGreater(v[-2], v[-1])                    # the kick before it (Kontakt's smoothing compensated)
        self.assertAlmostEqual(t[-2], 1.136 + 0.065, 3)     # from the leave

    def test_after_and_turn(self):
        a = Args(after='transition', after_len=300.0, staccato_gap=45.0, overlap=24.0)
        c = {'kind': 'transition', 'pitch': 60, 'interval': 2, 'before': 'turn', 'beforeLenMs': 136, 'ioiMs': 136,
             'attackVelocity': 64, 'transitionVelocity': 64, 'bEarlyMs': 50, 'after': 'transition'}
        notes = mp.cell_notes(c, 1.0, a)
        self.assertEqual([x[0] for x in notes], ['Z', 'A', 'B', 'C'])
        self.assertEqual([x[1] for x in notes], [62, 60, 62, 60])          # B A B, then on alternating
        self.assertAlmostEqual(notes[2][3], 1.0 + 0.272 - 0.05)            # B 50 ms early
        self.assertAlmostEqual(notes[3][3], 1.0 + 0.272 + 0.136 - 0.05)    # C as early as B
        self.assertGreater(notes[2][4], notes[3][3])                       # B held into C (a transition)
        c['before'] = 'staccato-attack'
        notes = mp.cell_notes(c, 1.0, a)
        self.assertLess(notes[0][4], notes[1][3])                          # Z released before A: A is an attack
        self.assertGreater(notes[1][4], notes[2][3])                       # B by transition


@unittest.skipUnless(os.environ.get('MS_KTHOST') and os.environ.get('MS_TESTSYNTH'), 'MS_KTHOST / MS_TESTSYNTH not set')
class TestSynthChain(unittest.TestCase):
    def test_chain(self):
        work = tempfile.mkdtemp(prefix='mp-kt-')
        try:
            out = os.path.join(work, 'r.json')
            mp.main(['all', '--patch', 'MS Test Synth', '--out', work, '--what', 'level,onset', '--pitches', '60,72',
                     '--velocity', '64,127', '--lengths', '500', '--takes', '2', '--local', '--kthost',
                     os.environ['MS_KTHOST'], '--plugin', os.environ['MS_TESTSYNTH'], '--state', '-',
                     '--kthost-opts', '--tail 1', '-o', out])
            r = json.load(open(out))
            self.assertEqual(r['missingFiles'], [])
            self.assertEqual(len(r['cells']), 4)
            for c in r['cells']:
                self.assertEqual(c['n'], 2)
                self.assertIsNotNone(c['measures']['peak']['median'])
                self.assertLess(abs(c['measures']['onset15']['median']), 40)    # (the test synth starts at once: the rule reads a step ~30 ms early)
            self.assertFalse(os.path.exists(os.path.join(work, 'wav', 'f000.wav')))     # (deleted once analysed)
        finally:
            shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    unittest.main()
