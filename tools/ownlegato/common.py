"""Shared measurement code of the own-legato pilot (tools/ownlegato/): pYIN pitch, a transition's leave and arrival by the
heard check's rule (tools/playbackverify/heard_check.py: 3 frames in a row within 30 cents of the new pitch), audio I/O."""
import json
import numpy as np
import soundfile as sf

SR = 44100
HOP = 128           # the heard check's pYIN hop
TOL = 0.30          # semitones: the heard check's 30 cents


def mono(path):
    x, sr = sf.read(path, dtype='float32')
    assert sr == SR, (path, sr)
    return x.mean(1) if x.ndim == 2 else x


def stereo(path):
    x, sr = sf.read(path, dtype='float32')
    assert sr == SR, (path, sr)
    return x if x.ndim == 2 else np.stack([x, x], 1)


def f0(x, lo=48, hi=76, sr=SR, hop=HOP, resolution=0.1):
    """pYIN (librosa) in MIDI numbers (nan unvoiced) and the frames' centre times (s from x's start)."""
    import librosa
    f, v, _ = librosa.pyin(np.asarray(x, dtype=np.float64), fmin=librosa.midi_to_hz(lo), fmax=librosa.midi_to_hz(hi),
                           sr=sr, frame_length=2048, hop_length=hop, center=True,
                           resolution=resolution)
    m = librosa.hz_to_midi(np.where(v, f, np.nan))
    return np.arange(len(m)) * hop / sr, m


def leave_arrive(t, m, a, b, t_from, t_to=None, tol=TOL):
    """A transition from pitch a to b (MIDI numbers, cents as fractions) in a pYIN track: the arrival is the first of 3
    frames in a row within tol of b at or after t_from (and before t_to); the leave the last frame within tol of a before
    the arrival. (None, None) when b isn't reached."""
    nb = np.abs(m - b) <= tol
    arr = None
    for i in range(len(m) - 2):
        if t[i] < t_from:
            continue
        if t_to is not None and t[i] >= t_to:
            break
        if nb[i] and nb[i + 1] and nb[i + 2]:
            arr = i
            break
    if arr is None:
        return None, None
    na = np.abs(m - a) <= tol
    lv = next((i for i in range(arr, -1, -1) if na[i]), None)
    return (None if lv is None else float(t[lv])), float(t[arr])


def env_db(x, sr=SR, win=0.010):
    """RMS envelope in dB, 10 ms windows, hop 5 ms; times = window centres."""
    n, h = int(win * sr), int(win * sr / 2)
    k = max(0, (len(x) - n) // h)
    e = np.array([np.sqrt(np.mean(x[i * h:i * h + n] ** 2) + 1e-20) for i in range(k)])
    return (np.arange(k) * h + n / 2) / sr, 20 * np.log10(e)


def load_json(p):
    with open(p) as f:
        return json.load(f)
