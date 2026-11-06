"""notes.py: the sounding length of each note in a single-player (or one-section) recording.

    from notes import load, envelopes, onsets, measure

The measure (the same windows as ArticulationCheck's, tools/playbackverify/loudness.py):
- power: the power in 5 ms windows (dB), after a 40 Hz high pass;
- perceived: perceivedEnvelope (ERB filters, attack 22 / release 50 ms, every 5 ms), the measure SSO's short lengths
  (sso_short_lengths.json, `p10`) were taken with.
A note runs from its onset (less 50 ms) to the next note's onset in the part (or a limit). Its peak is the envelope's
maximum in that span; its start at X dB is the first window of the rise that carries the peak still within X dB of it
(back from the peak), its end the end of the last window within X dB
of the peak before the next onset (minus a guard: the next attack's own rise). When the envelope is still within X dB at
the next onset, the note is "joined" at X: its length is censored (at least the span), never counted as a measured end.
"""
import os, subprocess, sys
import numpy as np
from scipy.signal import butter, sosfilt

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'playbackverify'))
import loudness  # noqa: E402

HOP = 0.005


def load(path, sr_out=None):
    """mono float64 and the sample rate (ffmpeg decodes anything)"""
    p = subprocess.run(['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries', 'stream=sample_rate',
                        '-of', 'csv=p=0', path], capture_output=True, text=True).stdout.strip().strip(',')
    sr = int(p)
    raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', '1', '-'],
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64), sr


def envelopes(x, sr, perceived=True):
    """(power dB every 5 ms from 0, perceived dB every 5 ms from its first, its first s)"""
    y = sosfilt(butter(4, 40, 'highpass', fs=sr, output='sos'), x)
    n = int(round(sr * HOP))
    m = len(y) // n
    pw = 10 * np.log10((y[:m * n].reshape(m, n) ** 2).mean(axis=1) + 1e-20)
    if not perceived:
        return pw, None, 0.0
    pe, first_ms, hop_ms = loudness.perceived_envelope(y, sr)
    assert abs(hop_ms / 1000 - HOP) < 2e-4, hop_ms
    return pw, np.asarray(pe), first_ms / 1000


def onsets(pw, t0, t1, rise_db=6.0, look=0.04, min_ioi=0.06, floor_db=None):
    """onset times (s) in [t0, t1]: where the power rises by rise_db within `look` s over the minimum of the 20 ms before
    (ignoring rises whose top stays under floor_db); the onset is the first window within 20 dB of the rise's peak (the
    next 80 ms). Re-armed only once the power has fallen rise_db under the running maximum since the onset (a
    re-articulation) and min_ioi has passed."""
    i0, i1 = max(int(t0 / HOP), 4), min(int(t1 / HOP), len(pw) - 20)
    k = int(look / HOP)
    out = []
    i = i0
    while i < i1:
        base = pw[i - 4:i + 1].min()
        top = pw[i + 1:i + 1 + k].max()
        if top - base >= rise_db and (floor_db is None or top >= floor_db):
            seg = pw[i:i + 17]
            P = seg.max()
            j = i + int(np.nonzero(seg >= P - 20)[0][0])
            out.append(j * HOP)
            # re-arm
            m = i + int(np.argmax(seg))
            run = pw[m]
            q = m
            while q < i1 and (pw[q] > run - rise_db or (q - j) * HOP < min_ioi):
                run = max(run, pw[q])
                q += 1
            i = q
            continue
        i += 1
    return out


def measure(env, first, t_on, t_next, levels=(6, 10, 20), guard=0.015, max_len=2.0, pre=0.05):
    """one note on an envelope (values at first + i * HOP): its onset t_on, the next onset t_next (None: max_len).
    Returns dict: peak dB, peak time, dip (dB from the peak down to the lowest window before the next onset), and per level X: start, end (s, absolute), joined (bool)"""
    stop = (t_next - guard) if t_next is not None else t_on + max_len
    a = max(int(round((t_on - pre - first) / HOP)), 0)
    b = min(int(round((stop - first) / HOP)), len(env) - 1)
    if b <= a + 2:
        return None
    seg = env[a:b + 1]
    ip = int(np.argmax(seg))
    peak = float(seg[ip])
    out = {'peak': peak, 'tPeak': first + (a + ip) * HOP, 'dip': float(peak - seg[ip:].min())}
    for X in levels:
        above = np.nonzero(seg >= peak - X)[0]
        e = above[-1]
        s = ip   # the start: back from the peak while within X dB (the rise that carries the peak)
        while s > 0 and seg[s - 1] >= peak - X:
            s -= 1
        joined = e >= len(seg) - 1
        out[X] = {'start': first + (a + s) * HOP, 'end': first + (a + e + 1) * HOP, 'joined': bool(joined)}
    return out
