#!/usr/bin/env python3
"""Reads sweep renders (make_sweep_scores.py's scores through MuseScore --verify-playback --verify-wav with SSO):
when each note is heard against its written time.

    analyze_sweep.py <Sweep X.notes.json> <the part's "library.wav"> [--json out.json] [--label name]

  L (slurred, not the first of its slur)  arriveMs: when the new pitch takes over from the one before (after
        the old one led by 3 dB for 20 ms, the energy of the harmonics only the new pitch has passes that of those only
        the old one has, for 20 ms; 4096-point frames every 5 ms, their centres; 8192 under 120 Hz; an octave: the lower pitch's own harmonics against the
        shared ones cross the midpoint of their levels before and after), from the written time
  H, S and a slur's first note  onsetMs: the first time the perceived loudness (loudness.perceived_envelope) is
        within 15 dB of the note's peak (and onset10Ms / onset12Ms / onset20Ms); S also endMs: the last time within 10 / 15 dB
        of the peak before the next note (from the written time; against the written length)
Prints per section, register and length the median and the 10-90 % range; --json keeps every note.
"""
import argparse
import json
import statistics

import numpy as np
import soundfile as sf

from loudness import perceived_envelope


HOP = [5.0]   # ms per envelope value (perceived_envelope's hop_ms)


def env_at(env, first, t):
    return int(round((t * 1000 - first) / HOP[0]))


def at_ms(first, i):
    return (first + HOP[0] * i) / 1000


def onset_of(env, first, t0, t1):
    a, b = max(0, env_at(env, first, t0)), min(len(env), env_at(env, first, t1))
    if b <= a:
        return None
    seg = env[a:b]
    k = int(np.argmax(seg))
    peak = seg[k]
    out = {}
    for d in (20, 15, 12, 10):
        i = int(np.argmax(seg[:k + 1] >= peak - d))
        out[d] = at_ms(first, a + i)
    return peak, at_ms(first, a + k), out


def end_of(env, first, peak_t, peak, t1, drops=(10, 15)):
    a, b = env_at(env, first, peak_t), min(len(env), env_at(env, first, t1))
    out = {}
    for d in drops:
        idx = np.nonzero(env[a:b] >= peak - d)[0]
        out[d] = at_ms(first, a + idx[-1] + 1) if len(idx) else None
    return out


def unique_harmonics(f, other, top, min_hz=0.0):
    """f's harmonics at least 3 % and min_hz (the window's main lobe) from any of other's"""
    hs = [h * f for h in range(1, 41) if h * f < top]
    os_ = [h * other for h in range(1, 81) if h * other < top * 1.1]
    return [x for x in hs if all(abs(x / o - 1) > 0.03 and abs(x - o) >= min_hz for o in os_)][:12]


def arrival(x, sr, t_written, f_old, f_new, t_from, t_to):
    N = 8192 if min(f_old, f_new) < 120 else 4096
    hop = int(sr * 0.005)
    top = min(8000.0, sr * 0.45)
    lobe = 3.0 * sr / N
    hn, ho = unique_harmonics(f_new, f_old, top, lobe), unique_harmonics(f_old, f_new, top, lobe)
    freqs = np.fft.rfftfreq(N, 1 / sr)
    def bins(hs):
        return [np.nonzero(np.abs(freqs / h - 1) <= 0.015)[0] for h in hs]
    bn, bo = bins(hn), bins(ho)
    win = np.hanning(N)
    start = max(0, int(t_from * sr) - N // 2)
    stop = min(len(x) - N, int(t_to * sr) - N // 2)
    if not hn or not ho:
        # an octave (or twelfth …): one pitch has no harmonics of its own. The lower one's own harmonics against
        # those both share, in dB: from its level under the old note to that under the new one, the crossing of
        # the midpoint (for 20 ms)
        low, high = (f_old, f_new) if f_old < f_new else (f_new, f_old)
        own = bins(unique_harmonics(low, high, top, lobe))
        shared = bins([h * high for h in range(1, 13) if h * high < top])
        if not own or not shared:
            return None
        ts, rs = [], []
        for s in range(start, stop, hop):
            p = np.abs(np.fft.rfft(x[s:s + N] * win)) ** 2
            eo = sum(p[b].max() for b in own if len(b))
            es = sum(p[b].max() for b in shared if len(b))
            ts.append((s + N // 2) / sr)
            rs.append(10 * np.log10(max(eo, 1e-30) / max(es, 1e-30)))
        if len(rs) < 30:
            return None
        before, after = np.median(rs[:10]), np.median(rs[-20:])
        if abs(before - after) < 6:
            return None
        mid = 0.5 * (before + after)
        run = 0
        for t, r in zip(ts, rs):
            if (r > mid) == (after > before):
                run += 1
                if run == 4:
                    return t - 3 * hop / sr - t_written
            else:
                run = 0
        return None
    run = 0
    old = 0       # (the old pitch heard first: frames before it are another note's)
    for s in range(start, stop, hop):
        p = np.abs(np.fft.rfft(x[s:s + N] * win)) ** 2
        en = sum(p[b].max() for b in bn if len(b))
        eo = sum(p[b].max() for b in bo if len(b))
        if old < 4:
            old = old + 1 if eo > 2 * en else 0
            continue
        if en > eo:
            run += 1
            if run == 4:
                return (s - 3 * hop + N // 2) / sr - t_written
        else:
            run = 0
    return None


def arrival_template(x, sr, t_written, f_old, f_new, a_from, a_to, b_from, b_to, t_from, t_to):
    """when the new note's share of the power first passes half (for 30 ms), each frame's magnitude spectrum fitted
    as a A + b B (a, b >= 0), A / B the old / new note's own spectra averaged over [a_from, a_to] / [b_from, b_to]
    (seconds in the render); 50 Hz-8 kHz; 4096-point frames (8192 under 120 Hz) every 10 ms. Right for octaves,
    where the harmonics' ratio (arrival) is not: octave_synth_check.py"""
    N = 8192 if min(f_old, f_new) < 120 else 4096
    if a_to - a_from < 0.05 or b_to - b_from < 0.05:
        return None
    freqs = np.fft.rfftfreq(N, 1 / sr)
    band = (freqs > 50) & (freqs < 8000)
    win = np.hanning(N)
    def mag(c):
        s0 = int(c * sr) - N // 2
        if s0 < 0 or s0 + N > len(x):
            return None
        return np.abs(np.fft.rfft(x[s0:s0 + N] * win))[band]
    def avg(t0, t1):
        ms = [m for m in (mag(t) for t in np.arange(t0, t1 + 1e-9, 0.01)) if m is not None]
        return np.sqrt(np.mean(np.square(ms), axis=0)) if ms else None
    A, B = avg(a_from, a_to), avg(b_from, b_to)
    if A is None or B is None:
        return None
    aa, bb, ab = A @ A, B @ B, A @ B
    det = aa * bb - ab * ab
    if det <= 1e-12 * aa * bb:
        return None
    run = 0
    for t in np.arange(t_from, t_to, 0.01):
        m = mag(t)
        if m is None:
            break
        am, bm = A @ m, B @ m
        a, b = (bb * am - ab * bm) / det, (aa * bm - ab * am) / det
        if a < 0:
            a, b = 0.0, max(0.0, bm / bb)
        elif b < 0:
            a, b = max(0.0, am / aa), 0.0
        share = b * b * bb / max(a * a * aa + b * b * bb, 1e-30)
        run = run + 1 if share >= 0.5 else 0
        if run == 3:
            return t - 0.02 - t_written
    return None


def hz(p):
    return 440.0 * 2 ** ((p - 69) / 12)


def analyse(meta, wav):
    x, sr = sf.read(wav, always_2d=True)
    m = x.mean(axis=1)
    env, first, HOP[0] = perceived_envelope(x, sr)
    notes = meta["notes"]
    rows = []
    for i, n in enumerate(notes):
        t = n["time"]
        nxt = notes[i + 1]["time"] if i + 1 < len(notes) else t + n["seconds"] + 4
        prev = notes[i - 1] if i else None
        r = dict(n)
        if n["section"] == "L" and not n["first"]:
            a = arrival(m, sr, t, hz(prev["pitch"]), hz(n["pitch"]), max(t - 0.45, prev["time"] - 0.15), t + min(0.9, n["seconds"] + 0.6))
            r["arriveMs"] = None if a is None else round(a * 1000)
            # its level: the loudest perceived loudness over its written length (from 30 ms after its written time)
            lo_, hi_ = max(0, env_at(env, first, t + 0.03)), min(len(env), env_at(env, first, t + n["seconds"]))
            r["levelDb"] = round(float(env[lo_:hi_].max()), 1) if hi_ > lo_ else None
            r["interval"] = n["pitch"] - prev["pitch"]
            # by templates (the old note from 0.2 s after its written time to 80 ms before the new one's, the new note
            # likewise; notes of 0.4 s and more)
            at = arrival_template(m, sr, t, hz(prev["pitch"]), hz(n["pitch"]), prev["time"] + 0.2, t - 0.08,
                                  t + 0.2, t + n["seconds"] - 0.08, max(t - 0.45, prev["time"] + 0.1), t + min(0.9, n["seconds"]))
            r["arriveTMs"] = None if at is None else round(at * 1000)
            r["before"] = prev["seconds"]
        else:
            o = onset_of(env, first, t - 0.4, min(nxt, t + 1.5))
            if o:
                peak, peak_t, on = o
                r["onsetMs"] = round((on[15] - t) * 1000)
                r["onset10Ms"] = round((on[10] - t) * 1000)
                r["onset12Ms"] = round((on[12] - t) * 1000)
                r["onset20Ms"] = round((on[20] - t) * 1000)
                r["peakMs"] = round((peak_t - t) * 1000)
                if n["section"] == "S":
                    e = end_of(env, first, peak_t, peak, nxt - 0.05)
                    r["end10Ms"] = None if e[10] is None else round((e[10] - t) * 1000)
                    r["end15Ms"] = None if e[15] is None else round((e[15] - t) * 1000)
        rows.append(r)
    return rows


def summary(rows, label=""):
    def stat(xs):
        xs = sorted(x for x in xs if x is not None)
        if not xs:
            return "-"
        q = lambda f: xs[min(len(xs) - 1, int(f * len(xs)))]
        return f"{statistics.median(xs):+.0f} ({q(0.1):+.0f}..{q(0.9):+.0f}, n={len(xs)})"
    groups = {}
    for r in rows:
        if r["section"] == "L":
            key = ("L arrive", r["register"], f"{r['seconds']:g}s" + (" first(onset)" if r["first"] else ""))
            val = r.get("onsetMs") if r["first"] else r.get("arriveMs")
        elif r["section"] == "H":
            key = ("H onset-15", r["register"], f"{r['dynamic']} {r['technique']}".strip())
            val = r.get("onsetMs")
        else:
            key = ("S onset-15", r["register"], f"{r.get('mark')} {r['seconds']:g}s")
            val = r.get("onsetMs")
        groups.setdefault(key, []).append(val)
        if r["section"] == "S":
            groups.setdefault(("S end-10 minus written", r["register"], f"{r.get('mark')} {r['seconds']:g}s"), []).append(
                None if r.get("end10Ms") is None else r["end10Ms"] - r["seconds"] * 1000)
    lines = []
    for key in sorted(groups):
        lines.append(f"{label}  {key[0]:24s} {key[1]:5s} {key[2]:22s} {stat(groups[key])}")
    missing = sum(1 for r in rows if r["section"] == "L" and not r["first"] and r.get("arriveMs") is None)
    lines.append(f"{label}  L transitions not found: {missing}")
    # evenness of slurred groups: the levels of a group's inner notes (not its first or last), max - min
    for sec in (0.25, 0.5):
        spreads = level_spreads(rows, sec)
        if spreads:
            lines.append(f"{label}  L {sec:g}s groups' level spread (dB): " + ", ".join(f"{x:.1f}" for x in spreads))
    return "\n".join(lines)


def level_spreads(rows, sec):
    """per slurred group of notes sec long: max - min of its inner notes' levels (dB)"""
    spreads, group = [], []
    for r in rows + [dict(section="", first=True)]:
        if r["section"] == "L" and not r["first"] and r["seconds"] == sec:
            group.append(r.get("levelDb"))
        elif group:
            inner = [x for x in group[:-1] if x is not None]
            if len(inner) >= 3:
                spreads.append(round(max(inner) - min(inner), 1))
            group = []
    return spreads


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("notes")
    ap.add_argument("wav")
    ap.add_argument("--json")
    ap.add_argument("--label", default="")
    a = ap.parse_args()
    meta = json.load(open(a.notes, encoding="utf-8"))
    rows = analyse(meta, a.wav)
    print(summary(rows, a.label or meta["part"]))
    if a.json:
        json.dump(dict(part=meta["part"], instrument=meta["instrument"], notes=rows), open(a.json, "w"), indent=0)


if __name__ == "__main__":
    main()
