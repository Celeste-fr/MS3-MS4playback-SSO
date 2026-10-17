#!/usr/bin/env python3
"""Every sound of the own-legato recordings measured (record.py's files rendered by run_vm.cmd): the index make_sfz.py
cuts the SFZ instrument from.

    measure.py <rec dir> <audio dir> -o index.json [--files rec00,rec01,...]

<rec dir>: record.py's <name>.json logs; <audio dir>: the renders <name>.flac (or .wav), 44.1 kHz stereo.

By the heard check's rules (tools/playbackverify/heard_check.py, so our patch is cut to land where the heard check will
read it):
  transition  pYIN f0 (heard_check.track: librosa.pyin, hop 128) over the note before's note-on .. 1.2 s after the new
              note-on: the ARRIVAL is the first of 3 frames in a row within 30 cents of the new pitch after f0 left the note
              before (heard_check.transition_time, the note before taken as heard 0.5 s after its own note-on: settled);
              the LEAVE the last frame within 30 cents of the note before ahead of it (common.leave_arrive). Times in ms
              after the new note-on. Not found: arrive None (the sound is left out of the instrument).
  attack      the perceived envelope (loudness.perceived_envelope): heard_check.attack_time over its first ATTACK_SPAN_MS
              (a slur's first note's peak looked for over its slur in the heard check; Whence's Violas slurs are 545 ms
              long), so -15 dB under that peak less 30 ms; ms after the note-on.
  level       each transition's B: the loudest perceived level from its arrival to 400 ms later; each attack: over its
              span. dB of the envelope (33.2 log10 of summed specific loudness): for level steps between sounds.
"""
import argparse
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
from common import SR, stereo, leave_arrive, load_json  # noqa: E402
import heard_check as hc  # noqa: E402
from loudness import perceived_envelope  # noqa: E402

ATTACK_SPAN_MS = 550.0
RANGE = (49, 73)    # pYIN's range (--range): the recorded keys +-3, fixed, as the heard check tracks a whole route
                    # between its lowest and highest pitch +-3 (a narrow range loses the voicing in a glide: 60 -> 62 read
                    # 328 ms with 57-65, 235 ms with 50-72, the pitch touching 62 at 235 and sagging to 61.5 until 330)


def audio_path(d, name):
    for ext in ('.flac', '.wav'):
        p = os.path.join(d, name + ext)
        if os.path.exists(p):
            return p
    return None


def measure_file(log, x):
    mono = x.mean(1)
    env, first, hop = perceived_envelope(x, SR)
    out = []
    for si, s in enumerate(log['segments']):
        notes = s['notes']
        if s['kind'] == 'cc11':
            continue
        # the attack (every segment starts with one)
        n0 = notes[0]
        on = n0['on'] * 1000
        h, masked, pk = hc.attack_time(env, first, hop, on, on - 1000.0, on + ATTACK_SPAN_MS)
        out.append(dict(kind='attack', file=log['name'], seg=si, attack=s.get('attack', 'smooth'), key=n0['key'],
                        vel=n0['vel'], cc1=s['cc1'], on=n0['on'], off=n0['off'], onset=None if h is None else round(h - on, 1),
                        level=None if pk is None else round(pk, 2), chainStart=len(notes) > 1, rep=s.get('rep')))
        # transitions
        for i in range(1, len(notes)):
            A, B = notes[i - 1], notes[i]
            w0, w1 = A['on'], B['on'] + 1.2
            seg = mono[int(w0 * SR):int(w1 * SR)].astype(np.float64)
            lo, hi = RANGE
            f0, fh = hc.track(seg, SR, lo, hi)
            t = np.arange(len(f0)) * fh
            ta, why = hc.transition_time(f0, fh, B['on'] - w0, A['key'], B['key'], after_s=A['on'] - w0 + 0.5,
                                         until_s=None)
            lv = None
            if ta is not None:
                lv, _ = leave_arrive(t, f0, A['key'], B['key'], ta - 1e-6)
            rec = dict(kind='transition', file=log['name'], seg=si, idx=i, type=s.get('type', ''), a=A['key'], b=B['key'],
                       vel=B['vel'], cc1=s['cc1'], rep=s.get('rep'), on=B['on'], aOn=A['on'], off=B['off'],
                       next=notes[i + 1]['on'] if i + 1 < len(notes) else None,
                       arrive=None if ta is None else round((ta + w0 - B['on']) * 1000, 1),
                       leave=None if lv is None else round((lv + w0 - B['on']) * 1000, 1), why=why)
            if ta is not None:
                ia = int(round(((ta + w0) * 1000 + hc.LATENCY_MS - first) / hop))
                ib = int(round(((ta + w0) * 1000 + 400 + hc.LATENCY_MS - first) / hop))
                rec['level'] = round(float(env[max(0, ia):max(ia + 1, ib)].max()), 2)
            out.append(rec)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rec')
    ap.add_argument('audio')
    ap.add_argument('-o', required=True)
    ap.add_argument('--files')
    ap.add_argument('--range', default='49-73')
    a = ap.parse_args()
    global RANGE
    RANGE = tuple(int(v) for v in a.range.split('-'))
    names = sorted(f[:-5] for f in os.listdir(a.rec) if f.endswith('.json') and not f.startswith('index'))
    if a.files:
        names = [n for n in names if n in a.files.split(',')]
    idx = []
    for n in names:
        p = audio_path(a.audio, n)
        if not p:
            print('no audio for', n, file=sys.stderr)
            continue
        log = load_json(os.path.join(a.rec, n + '.json'))
        r = measure_file(log, stereo(p))
        tr = [q for q in r if q['kind'] == 'transition']
        ok = [q for q in tr if q['arrive'] is not None]
        print('%s: %d attacks, %d transitions, %d arrivals found' % (n, len(r) - len(tr), len(tr), len(ok)), file=sys.stderr)
        idx += r
    with open(a.o, 'w') as f:
        json.dump(dict(source=os.path.basename(os.path.normpath(a.rec)), items=idx), f, indent=0)


if __name__ == '__main__':
    main()
