#!/usr/bin/env python3
"""Tests of tone_check.py on a synthetic instrument with six round robins whose faults are known: one round robin with
a narrow tone of its own above a velocity (sso_bad_samples.json's Celli C3 case, rebuilt: 12 attacks, 0.5 s on, 0.4 s
apart, round robin 5 of 6 at +10 dB in one harmonic from velocity 100), a dark one, a noisy one, a click at a note-on,
a pop (a cut waveform) at a note-off, a dark key; the avoid proposal; measure_patch.py's --what tone --repeats path.
With MS_TONE_REAL="<wav>|<kthost events>" (a real render of clean isolated attacks) it must flag nothing.

    python3 tools/soundlibraries/test_tone_check.py [-v]
"""
import json
import os
import shutil
import sys
import tempfile
import unittest

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tone_check as tc  # noqa: E402

SR = 44100
ON, GAP = 0.5, 0.4


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12)


def tone(pitch, vel, length, rr, fault=None, seed=0):
    """one note: harmonics 1/h with each round robin's own small (+-0.7 dB) differences, noise 40 dB under them, a
    5 ms rise, a 40 ms release;
    fault: 'narrow' (harmonic 9 +10 dB), 'dark' (above harmonic 3 -12 dB), 'noisy' (white noise -22 dB), 'pop' (no
    release: the waveform cut at its crest at the note-off)"""
    rng = np.random.default_rng(1000 * rr + seed)
    n = int((length + 0.06) * SR)
    t = np.arange(n) / SR
    f0 = hz(pitch)
    amp = 10 ** ((vel - 100) / 40)
    sig = np.zeros(n)
    for h in range(1, 40):
        if h * f0 > 15000:
            break
        g = 1.0 / h * 10 ** (rng.normal(0, 0.7) / 20)
        if fault == 'narrow' and h == 9:
            g *= 10 ** (10 / 20)
        if fault == 'dark' and h > 3:
            g *= 10 ** (-12 / 20)
        sig += g * np.sin(2 * np.pi * h * f0 * t + rng.uniform(0, 2 * np.pi))
    # (every recording has some noise: bow, room, mic; -40 dB, as a real sample's linear-prediction residual)
    sig += rng.normal(0, 1, n) * np.sqrt(np.mean(sig ** 2)) * 10 ** ((-22 if fault == 'noisy' else -40) / 20)
    env = np.clip(t / 0.005, 0, 1)
    if fault == 'pop':              # (cut where the waveform is at its crest, within 2 ms of the note-off: a cut sample)
        i = int(length * SR)
        i += int(np.argmax(np.abs(sig[i:i + int(0.002 * SR)])))
        env = env * (np.arange(n) < i)
    else:
        env = env * np.clip(1 - (t - length) / 0.04, 0, 1)
    return 0.05 * amp * sig * env


def render(spec, path_wav, path_ev):
    """spec: [(pitch, velocity, count, faults {attack index (1-based): fault}, rr offset)] played one after another:
    each note ON s, GAP s apart; round robins 1..6 in turn per (pitch, velocity) run. Returns the notes."""
    import soundfile as sf
    t = 0.5
    notes, parts = [], []
    for pitch, vel, count, faults, extra in spec:
        for i in range(count):
            rr = i % 6 + 1
            fault = faults.get(i + 1) or faults.get('rr%d' % rr)
            x = tone(pitch, vel, ON, rr, fault, seed=pitch)
            parts.append((t, x))
            notes.append((t, t + ON, pitch, vel))
            if (i + 1) in extra:
                parts.append((t + extra[i + 1][0], extra[i + 1][1]))
            t += ON + GAP
        t += 1.0
    out = np.zeros(int((t + 1) * SR))
    for t0, x in parts:
        i = int(round(t0 * SR))
        out[i:i + len(x)] += x
    sf.write(path_wav, np.stack([out, out], axis=1).astype(np.float32), SR, subtype='FLOAT')
    with open(path_ev, 'w') as f:
        f.write('0.000000 cc 1 64\n')
        for on, off, p, v in notes:
            f.write('%.6f on %d %d\n%.6f off %d 0\n' % (on, p, v, off, p))
    return notes


def click():
    x = np.zeros(int(0.002 * SR))
    x[0] = 0.4
    return x


class RoundRobins(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix='tone-test-')
        # the Celli C3 case: keys 47 / 48, round robin 5 faulty from velocity 100; 45, 46, 49, 50 clean at 106
        spec = []
        for p in (47, 48):
            for v in (90, 99, 100, 106, 116):
                spec.append((p, v, 12, {'rr5': 'narrow'} if v >= 100 else {}, {}))
        for p in (45, 46, 49, 50):
            spec.append((p, 106, 12, {}, {}))
        w, ev = os.path.join(cls.tmp, 'rr.wav'), os.path.join(cls.tmp, 'rr.txt')
        render(spec, w, ev)
        cls.rr = tc.check_wav(w, ev, patch='Synth')
        # one key: a dark round robin (2 of 6), a noisy one (3 of 6), a click at attack 7's note-on, a pop at attack
        # 10's note-off
        spec = [(60, 64, 12, {'rr2': 'dark', 'rr3': 'noisy', 10: 'pop'}, {7: (0.003, click())})]
        w, ev = os.path.join(cls.tmp, 'faults.wav'), os.path.join(cls.tmp, 'faults.txt')
        render(spec, w, ev)
        cls.faults = tc.check_wav(w, ev, patch='Synth')
        # keys 53-59, 4 attacks each, key 56 dark in every round robin: a key outlier, no note flag
        spec = [(p, 80, 4, {'rr%d' % r: 'dark' for r in range(1, 7)} if p == 56 else {}, {}) for p in range(53, 60)]
        w, ev = os.path.join(cls.tmp, 'keys.wav'), os.path.join(cls.tmp, 'keys.txt')
        render(spec, w, ev)
        cls.keys = tc.check_wav(w, ev, patch='Synth')

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def flagged(self, res, key, vel):
        return [(n['rr'], n['why']) for n in res['notes'] if n['key'] == key and n['velocity'] == vel and n['why']]

    def test_narrow_round_robin_refound(self):
        for p in (47, 48):
            for v in (90, 99):
                self.assertEqual(self.flagged(self.rr, p, v), [], (p, v))
            for v in (100, 106, 116):
                got = self.flagged(self.rr, p, v)
                self.assertEqual([g[0] for g in got], [5, 11], (p, v, got))
                self.assertTrue(all(g[1] == ['narrow tone'] for g in got), got)
        f = [x for x in self.rr['flags'] if 'key 48 v106' in x]
        self.assertEqual(len(f), 1, self.rr['flags'])
        self.assertIn('round robin 5 of 6 (attacks 5 and 11 of 12)', f[0])
        hz9 = 9 * hz(48)
        self.assertLess(abs(float(f[0].split(' at ')[-1].split()[0]) - hz9) / hz9, 0.04, f[0])
        for p in (45, 46, 49, 50):
            self.assertEqual(self.flagged(self.rr, p, 106), [], p)

    def test_avoid_proposal(self):
        self.assertEqual(self.rr['avoid'], {'Synth': [{'keys': [47, 48], 'band': [100, 116], 'velocity': 99}]})
        # (its schema is sso_bad_samples.json's "avoid")
        bad = json.load(open(os.path.join(HERE, 'sso_bad_samples.json')))['avoid']
        self.assertEqual(sorted(bad['Celli - Performance'][0]), sorted(self.rr['avoid']['Synth'][0]))

    def test_clean_notes_quiet(self):
        clean = [n['narrow'] for n in self.rr['notes'] if n['key'] in (45, 46, 49, 50)]
        self.assertLess(max(clean), 4.0)
        for n in self.rr['notes']:
            self.assertLess(n['clickOn'], 15, n)
            self.assertLess(n['clickOff'], 15, n)

    def test_dark_noisy_click_pop(self):
        got = {n['rr']: n['why'] for n in self.faults['notes'] if n['why']}
        self.assertIn('dark', got.get(2, []), got)
        self.assertIn('dark', got.get(8, []), got)
        self.assertIn('noisy', got.get(3, []), got)
        self.assertIn('noisy', got.get(9, []), got)
        self.assertIn('click at note-on', got.get(7, []), got)
        self.assertIn('pop at note-off', got.get(10, []), got)
        for k in (1, 4, 5, 6, 11, 12):
            self.assertNotIn(k, got, got)
        flags = ' | '.join(self.faults['flags'])
        self.assertIn('dark: round robin 2 of 6 (attacks 2 and 8 of 12)', flags)
        self.assertIn('noisy: round robin 3 of 6 (attacks 3 and 9 of 12)', flags)
        # one velocity measured: no clean velocity to play instead, no proposal
        self.assertEqual(self.faults['avoid'], {})

    def test_key_outlier(self):
        ko = self.keys['keys']
        self.assertEqual([k['key'] for k in ko], [56], ko)
        self.assertLess(ko[0]['dev']['centroid'], -300)
        self.assertFalse(any(n['why'] for n in self.keys['notes']))

    def test_avoid_merge(self):
        p = os.path.join(self.tmp, 'a.json')
        json.dump(self.rr, open(p, 'w'))
        self.assertEqual(tc.merge_avoid([p, p]), self.rr['avoid'])

    def test_cli(self):
        out = os.path.join(self.tmp, 'cli.json')
        tc.main([os.path.join(self.tmp, 'rr.wav'), os.path.join(self.tmp, 'rr.txt'), '--patch', 'Synth', '-o', out,
                 '-q'])
        self.assertEqual(json.load(open(out))['avoid'], self.rr['avoid'])
        self.assertNotIn('bands', json.load(open(out))['notes'][0])


class MeasurePatchTone(unittest.TestCase):
    """measure_patch.py plan --what level,tone --repeats 12: rendered by the synthetic instrument, analysed"""

    def test_work(self):
        import measure_patch as mp
        import soundfile as sf
        work = tempfile.mkdtemp(prefix='tone-mp-')
        try:
            mp.main(['plan', '--patch', 'Synth', '--out', work, '--what', 'level,tone', '--pitches', '48',
                     '--velocity', '99,106', '--lengths', '500', '--repeats', '12', '--repeat-gap', '0.4',
                     '--takes', '2', '--no-warm'])
            meta = json.load(open(os.path.join(work, 'plan.json')))
            self.assertEqual(len(meta['cells'][0]['notes']), 12)
            for fi in meta['files']:
                n = int((fi['seconds'] + 2) * SR)
                out = np.zeros(n)
                for c in meta['cells']:
                    if c['file'] != fi['name']:
                        continue
                    for k, note in enumerate(c['notes']):
                        rr = k % 6 + 1
                        x = tone(note[1], note[2], note[4] - note[3], rr,
                                 'narrow' if rr == 5 and note[2] >= 100 else None, seed=note[1])
                        i = int(round(note[3] * SR))
                        out[i:i + len(x)] += x
                w = os.path.join(work, fi['name'] + '.wav')
                sf.write(w, np.stack([out, out], axis=1).astype(np.float32), SR, subtype='FLOAT')

                class A:
                    level_window = 400.0
                    gain = 0.0
                mp.analyze_file(work, fi['name'], w, A())

            class B:
                level_window = 400.0
                gain = 0.0
                o = None
            B.work = work
            r = mp.analyze(B())
            flags = r['tone']['flags']
            self.assertEqual(len(flags), 1, flags)
            self.assertIn('v106', flags[0])
            self.assertIn('round robin 5 of 6 (attacks 5 and 11 of 12)', flags[0])
            self.assertEqual(r['tone']['avoid'], {'Synth': [{'keys': [48, 48], 'band': [106, 106], 'velocity': 99}]})
            self.assertTrue(os.path.exists(os.path.join(work, 'tone.json')))
            self.assertIn('x12', r['cells'][0]['label'])
            # tone_check.py WORK gives the same
            self.assertEqual(tc.check_work(work)['flags'], flags)
        finally:
            shutil.rmtree(work, ignore_errors=True)


@unittest.skipUnless(os.environ.get('MS_TONE_REAL'), 'MS_TONE_REAL="<wav>|<kthost events>" not set')
class RealRender(unittest.TestCase):
    """a real render of clean isolated attacks (e.g. SSO's string Performance patches at 64 / 122, kthost): no flags"""

    def test_no_flags(self):
        for pair in os.environ['MS_TONE_REAL'].split(';'):
            wav, ev = pair.split('|')
            r = tc.check_wav(wav, ev)
            self.assertEqual(r['flags'], [], (os.path.basename(wav), r['flags']))


if __name__ == '__main__':
    unittest.main()
