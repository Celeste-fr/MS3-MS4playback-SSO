#!/usr/bin/env python3
"""The onset detectors behind the SSO timing numbers (sso_long_onset_fit.json, sso_rachm_onset_fit.json, the map's
Long early starts), moved here unchanged from the scratch scripts they were run from (tmp-liveref/fam11.py `measure`
and `measure2`, fam9.py `isomeasure`; 2026-10-07..09). Tested against synthetic notes with a known onset by
test_onset_detect.py; its results: onset_detect_accuracy.json.

    onset_detect.py loudness  <meta.json> <wavA> <wavB> <out.json>   late_S_<code>.json (fam11.py measure)
    onset_detect.py harmonics <meta.json> <wavA> <wavB> <out.json>   hlate_S_<code>.json (fam11.py measure2)

meta.json: one row per note, {i, written, on, off, pitch, pos, inst} (seconds; inst 0 / 1: the note is on wavA / wavB,
odd and even notes of a passage on two instances so that the note before on the same instance is two back; pos 0:
the first note of a phrase, not read). Out: {i: arrival - written, ms} for the notes read; a note without a reading
is left out.

Detectors
  loudness_arrivals   "loudness": the perceived loudness (tools/playbackverify/loudness.py perceived_envelope, the
                      port of ArticulationCheck's perceivedEnvelope) from the note-on to 0.2 s after its note-off; the
                      arrival is the first value after the last one below (the window's peak - 15 dB) before the
                      peak, minus LATENCY_MS. No value below that before the peak (the note two back still rings within
                      15 dB): no reading.
  harmonic_arrivals   "harmonics": the power in the new pitch's own harmonics (those not shared with the note two
                      back on its instance, analyze_sweep.unique_harmonics), each harmonic the loudest bin within
                      1.5 % of it, summed, dB; Hann frames every 5 ms, read at the frame centres; the arrival is the
                      first of HARM_RUN frames in a row within 15 dB of the peak. No reading when the note two back
                      has the same pitch, or the first 5 frames' median is already within 15 dB of the peak.
  isolated_onset      fam9's isolated reading (one note, silence before): the first envelope value within 15 dB of
                      the peak in the 1.4 s from the note-on, minus LATENCY_MS, not below 0.

Constants (the owner's rule: every number sourced, measured or a stated choice)
  PEAK_DROP_DB 15     sourced: Vos & Rasch 1981 ("The perceptual onset of musical tones", Perception &
                      Psychophysics 29(4) 323-335): the perceptual onset is where the envelope reaches a threshold
                      relative to the tone's peak; 15 dB under it fitted their listeners. (Also ArticulationCheck's
                      onset.) Their threshold is on the physical envelope; here it is applied to a smoothed one.
  LATENCY_MS 30       CHOSEN (2026-10-07): the analysis's delay, from plucked notes (whose physical onset is
                      immediate) reading 28-53 ms late. test_onset_detect.py measures what it should be against
                      synthetic notes: onset_detect_accuracy.json "latency_replacement_ms": 23.6 ms (SD 3.5) for a
                      note rising from silence (15.8 at a 25 ms attack, 22-26 at 70-865 ms), but about 3 ms for a
                      note rising out of the tail of the note two back (slurred, slow run: arrival - 30 reads 27 ms
                      before the 15 dB onset at every attack): the smoothing starts from the tail's level, not
                      from silence. Synthetic signals, not SSO; the shipped value is unchanged.
  HARM_RUN 4          CHOSEN: 4 frames (20 ms) within 15 dB, so that one frame's flicker isn't an arrival.
  HOP_S 0.005         as perceived_envelope's hop (ArticulationCheck's 5 ms windows).
  N (harmonics frame) CHOSEN by the lowest pitch of the passage: 8192 under MIDI 40, 4096 under 60, else 2048, so
                      that adjacent harmonics of the lowest note fall in different bins (a Hann main lobe is 4 bins).
  BIN_TOL 0.015       CHOSEN: half of unique_harmonics' 3 % separation, so a harmonic's band never reaches another's.
  TOP_HZ 8000         CHOSEN (as analyze_sweep.arrival): harmonics above aren't used; also at most 0.45 sr.
  LOBE_BINS 3.0       as analyze_sweep.arrival: a harmonic within 3 bins of the other note's is shared.
  FALLBACK_HARMS 8    CHOSEN: with no note two back, the pitch's first 8 harmonics.
  AFTER_OFF_S 0.2, HARM_BEFORE_S 0.05, ISO_SPAN_S 1.4, HARM_MIN_FRAMES 10, HARM_PRE_FRAMES 5   CHOSEN window edges.
"""
import json
import os
import statistics as st
import sys

import numpy as np

_HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(_HERE, '..', 'playbackverify'))
from loudness import perceived_envelope  # noqa: E402

PEAK_DROP_DB = 15
LATENCY_MS = 30
HARM_RUN = 4
HOP_S = 0.005
BIN_TOL = 0.015
TOP_HZ = 8000.0
LOBE_BINS = 3.0
FALLBACK_HARMS = 8
AFTER_OFF_S = 0.2
HARM_BEFORE_S = 0.05
ISO_SPAN_S = 1.4
HARM_MIN_FRAMES = 10
HARM_PRE_FRAMES = 5


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12)


def unique_harmonics(f, other, top, min_hz=0.0):
    """f's harmonics at least 3 % and min_hz (the window's main lobe) from any of other's (= analyze_sweep's)"""
    hs = [h * f for h in range(1, 41) if h * f < top]
    os_ = [h * other for h in range(1, 81) if h * other < top * 1.1]
    return [x for x in hs if all(abs(x / o - 1) > 0.03 and abs(x - o) >= min_hz for o in os_)][:12]


def _mono(x):
    x = np.asarray(x, dtype=np.float64)
    return x.mean(axis=1) if x.ndim == 2 else x


def envelopes(signals, sr):
    """perceived_envelope of each instance's signal: [(env, first_ms, hop_ms)]"""
    out = []
    for x in signals:
        env, first, hop = perceived_envelope(x, sr)
        out.append((np.asarray(env), first, hop))
    return out


def loudness_arrivals(rows, envs, latency_ms=LATENCY_MS):
    """{i: arrival - written, ms}, total read (fam11.measure)"""
    late, total = {}, 0
    for r in rows:
        if r['pos'] == 0:
            continue
        total += 1
        env, first, hop = envs[r['inst']]
        idx = lambda t: int(round((t * 1000 - first) / hop))  # noqa: E731
        a, b = max(0, idx(r['on'])), min(len(env), idx(r['off'] + AFTER_OFF_S))
        if b - a < 4:
            continue
        seg = env[a:b]
        k = int(np.argmax(seg))
        thr = seg[k] - PEAK_DROP_DB
        below = np.nonzero(seg[:k + 1] < thr)[0]
        if not len(below):
            continue            # the note two back still rings within 15 dB: no clean rise
        c = int(below[-1]) + 1
        t = (first + hop * (a + c)) / 1000
        late[r['i']] = (t - r['written']) * 1000 - latency_ms
    return late, total


def harmonic_frames(n_fft, signals, sr):
    """[(power spectra per 5 ms frame, frame centres s, sr)] (fam11.measure2's spectra)"""
    specs = []
    for x in signals:
        m = _mono(x)
        hop = int(sr * HOP_S)
        win = np.hanning(n_fft)
        starts = np.arange(0, len(m) - n_fft, hop)
        P = np.empty((len(starts), n_fft // 2 + 1), dtype=np.float32)
        for j in range(0, len(starts), 512):
            fr = np.stack([m[s:s + n_fft] * win for s in starts[j:j + 512]])
            P[j:j + 512] = np.abs(np.fft.rfft(fr, axis=1)) ** 2
        specs.append((P, (starts + n_fft // 2) / sr, sr))
    return specs


def harmonic_n(rows):
    lo = min(r['pitch'] for r in rows)
    return 8192 if lo < 40 else 4096 if lo < 60 else 2048


def harmonic_arrivals(rows, signals, sr):
    """{i: arrival - written, ms}, total, notes at the pitch two back (fam11.measure2)"""
    N = harmonic_n(rows)
    specs = harmonic_frames(N, signals, sr)
    late, total, same = {}, 0, 0
    for r in rows:
        if r['pos'] == 0:
            continue
        total += 1
        prev = rows[r['i'] - 2]['pitch'] if r['i'] >= 2 else None
        if prev == r['pitch']:
            same += 1
            continue
        P, tc, sr_ = specs[r['inst']]
        freqs = np.fft.rfftfreq(N, 1 / sr_)
        top, lobe = min(TOP_HZ, sr_ * 0.45), LOBE_BINS * sr_ / N
        hs = (unique_harmonics(hz(r['pitch']), hz(prev), top, lobe) if prev
              else [hz(r['pitch']) * k for k in range(1, FALLBACK_HARMS + 1)])
        bs = [np.nonzero(np.abs(freqs / h - 1) <= BIN_TOL)[0] for h in hs]
        bs = [b for b in bs if len(b)]
        if not bs:
            continue
        a, b = np.searchsorted(tc, r['on'] - HARM_BEFORE_S), np.searchsorted(tc, r['off'] + AFTER_OFF_S)
        e = sum(P[a:b, bb].max(axis=1) for bb in bs)
        e = 10 * np.log10(np.maximum(e, 1e-30))
        k = int(np.argmax(e))
        thr = e[k] - PEAK_DROP_DB
        if len(e) < HARM_MIN_FRAMES or np.median(e[:HARM_PRE_FRAMES]) >= thr:
            continue            # its harmonics already sound when the note starts
        run = 0
        for j in range(k + 1):
            run = run + 1 if e[j] >= thr else 0
            if run == HARM_RUN:
                late[r['i']] = (tc[a + j - HARM_RUN + 1] - r['written']) * 1000
                break
    return late, total, same


def isolated_onset(env, first, hop, t, latency_ms=LATENCY_MS):
    """ms from t (a note-on with silence before) to its onset, or None (fam9.isomeasure with analyze_sweep.onset_of)"""
    a = max(0, int(round((t * 1000 - first) / hop)))
    b = min(len(env), int(round(((t + ISO_SPAN_S) * 1000 - first) / hop)))
    if b <= a:
        return None
    seg = env[a:b]
    k = int(np.argmax(seg))
    i = int(np.argmax(seg[:k + 1] >= seg[k] - PEAK_DROP_DB))
    return max(0.0, ((first + hop * (a + i)) / 1000 - t) * 1000 - latency_ms)


def _read(wavs):
    import soundfile as sf
    xs, sr = [], None
    for w in wavs:
        x, sr = sf.read(w, always_2d=True)
        xs.append(x)
    return xs, sr


def measure_files(kind, meta, wava, wavb, out):
    rows = json.load(open(meta))
    xs, sr = _read((wava, wavb))
    if kind == 'loudness':
        late, total = loudness_arrivals(rows, envelopes(xs, sr))
        head = f'heard {len(late)}/{total}'
    else:
        late, total, same = harmonic_arrivals(rows, xs, sr)
        head = f'heard {len(late)}/{total} ({same} at the pitch two back)'
    v = list(late.values())
    if v:
        print(f'{head}  median {st.median(v):.0f} ms  SD {st.pstdev(v):.0f} ms')
    else:
        print(head)
    json.dump(late, open(out, 'w'))
    return late


if __name__ == '__main__':
    if len(sys.argv) != 6 or sys.argv[1] not in ('loudness', 'harmonics'):
        sys.exit(__doc__.split('\n\n')[1])
    measure_files(*sys.argv[1:6])
