#!/usr/bin/env python3
"""Compare two hosted renders of the same part (MuseScore --verify-playback --verify-wav: "<part> library.wav" and the
score's events.tsv), note by note and as a null test.

cmp.py <dirA> <dirB> <part name> <notes.txt of the part's dump> [--json out]

dirA / dirB: a "Playback verify <date>" folder. Per note (the events' note-ons of the part, matched in order with the
dump's notes: written pitch + cents): f0 (pYIN, median over the note's steady part) in cents from the written tuning,
level (RMS dB), onset (5 ms RMS envelope first within 20 dB of the note's peak, ms after its note-on), spectrum (mean
log-power spectrum 100 Hz-12 kHz, the RMS of A - B in dB, and the spectral centroid's difference in cents). Null test:
the whole part, B - A, its energy against A's (dB), after the best lag within +-5 ms (reported).
"""
import glob, json, math, os, sys
import numpy as np
import soundfile as sf
import librosa
from scipy.signal import welch

def load(d, part):
    w = glob.glob(os.path.join(d, "*%s library.wav" % part))
    w = [x for x in w if os.path.basename(x).count(" library.wav") == 1]
    assert w, "no %s library.wav in %s" % (part, d)
    a, sr = sf.read(sorted(w, key=len)[0], dtype="float64")
    if a.ndim > 1:
        a = a.mean(axis=1)
    ev = glob.glob(os.path.join(d, "* events.tsv"))[0]
    ons = []
    held = {}
    for line in open(ev):
        c = line.rstrip("\n").split("\t")
        if c[0] == "time":
            continue
        t, slot, ty = float(c[0]), int(c[2]), c[3]
        if ty in ("on", "off"):
            p, v = int(c[4]), int(c[5])
            if ty == "on" and v > 0:
                held[(slot, p)] = len(ons)
                ons.append([t, p, slot, None])
            else:
                i = held.pop((slot, p), None)
                if i is not None:
                    ons[i][3] = t
    for o in ons:
        if o[3] is None:
            o[3] = o[0] + 0.5
    ons.sort(key=lambda o: (o[0], o[1]))
    return a, sr, ons

def notes(path, part):
    out = []
    for line in open(path):
        c = line.strip().split("|")
        if c[0] == part:
            out.append((float(c[1]), float(c[2]), int(c[3]), float(c[6])))
    return sorted(out, key=lambda n: (n[0], n[2]))

def rms_db(x):
    return 10 * math.log10(max(np.mean(x * x), 1e-20))

def main():
    da, db, part, ntxt = sys.argv[1:5]
    out = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    a, sr, onsA = load(da, part)
    b, srb, onsB = load(db, part)
    assert sr == srb
    written = notes(ntxt, part)
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    # null test: best lag within +-5 ms
    best = None
    for lag in range(-int(0.005 * sr), int(0.005 * sr) + 1, 4):
        bb = np.roll(b, lag)
        r = 10 * math.log10(np.sum((bb - a) ** 2) / np.sum(a * a))
        if best is None or r < best[0]:
            best = (r, lag)
    null = {"residualDb": best[0], "lagMs": 1000 * best[1] / sr, "levelA": rms_db(a), "levelB": rms_db(b)}
    rows = []
    m = min(len(onsA), len(onsB), len(written))
    for i in range(m):
        ta, p, _, offa = onsA[i]
        tb, pb, _, offb = onsB[i]
        w = written[i]
        dur = min(offa - ta, 0.6)
        if dur < 0.12 or p != pb or p != w[2]:
            rows.append({"i": i, "t": ta, "pitch": p, "skipped": "short or unmatched"})
            continue
        # polyphony within the part: another note sounding in the window
        poly = any(j != i and onsA[j][0] < ta + dur and onsA[j][3] > ta + 0.05 for j in range(len(onsA)))
        row = {"i": i, "t": round(ta, 3), "pitch": p, "cents": w[3], "poly": poly}
        for key, x, t0 in (("A", a, ta), ("B", b, tb)):
            s0, s1 = int((t0 + 0.08) * sr), int((t0 + dur) * sr)
            seg = x[s0:s1]
            row["level" + key] = rms_db(seg)
            # onset: 5 ms RMS envelope, first within 20 dB of the peak in [-50, +400] ms
            e0, e1 = int((t0 - 0.05) * sr), int((t0 + 0.4) * sr)
            env = x[max(0, e0):e1] ** 2
            k = int(0.005 * sr)
            env = np.sqrt(np.convolve(env, np.ones(k) / k, mode="same"))
            thr = env.max() * 0.1
            idx = int(np.argmax(env >= thr)) if env.max() > 0 else 0
            row["onset" + key] = 1000 * ((max(0, e0) + idx) / sr - t0)
            # spectrum: 1/3-octave bands 100 Hz - 12.5 kHz, each band's share of the note's power (dB)
            f, pxx = welch(seg, sr, nperseg=4096) if len(seg) >= 4096 else welch(seg, sr, nperseg=len(seg))
            edges = 100 * 2 ** (np.arange(0, 22) / 3)
            bands = np.array([np.sum(pxx[(f >= lo) & (f < hi)]) for lo, hi in zip(edges[:-1], edges[1:])])
            row["_bands" + key] = 10 * np.log10(bands / max(np.sum(bands), 1e-30) + 1e-12)
            band = (f >= 100) & (f <= 12000)
            row["centroid" + key] = float(np.sum(f[band] * pxx[band]) / max(np.sum(pxx[band]), 1e-20))
            # f0: YIN (parabolic interpolation), median of the frames within 20 dB of the note's loudest
            if not poly:
                fw = 440 * 2 ** ((p - 69 + w[3] / 100) / 12)
                fl = 4096 if fw < 150 else 2048
                f0 = librosa.yin(seg.astype(np.float32), fmin=fw * 2 ** (-4 / 12), fmax=fw * 2 ** (4 / 12), sr=sr, frame_length=fl)
                r = librosa.feature.rms(y=seg.astype(np.float32), frame_length=fl, hop_length=fl // 4)[0][:len(f0)]
                keep = r >= r.max() * 0.1
                row["f0" + key] = float(1200 * math.log2(np.median(f0[keep]) / fw)) if np.sum(keep) >= 3 else None
        ba = row.pop("_bandsA")
        bb = row.pop("_bandsB")
        ok = (ba > -40) | (bb > -40)            # (the bands that carry the note)
        row["specDiffDb"] = float(np.sqrt(np.mean((bb[ok] - ba[ok]) ** 2))) if np.any(ok) else None
        row["centroidCents"] = 1200 * math.log2(row["centroidB"] / row["centroidA"])
        s0, s1 = int((ta + 0.0) * sr), int((ta + dur) * sr)
        sa_, sb_ = a[s0:s1], b[s0:s1]
        row["nullDb"] = 10 * math.log10(max(np.sum((sb_ - sa_) ** 2), 1e-30) / max(np.sum(sa_ * sa_), 1e-30))
        row["levelDiff"] = row["levelB"] - row["levelA"]
        row["onsetDiff"] = row["onsetB"] - row["onsetA"]
        if row.get("f0A") is not None and row.get("f0B") is not None:
            row["f0Diff"] = row["f0B"] - row["f0A"]
        rows.append(row)
    res = {"part": part, "notes": len(written), "eventsA": len(onsA), "eventsB": len(onsB), "null": null, "rows": rows}
    def stats(k):
        v = [r[k] for r in rows if k in r and r[k] is not None]
        if not v:
            return None
        v = np.array(v)
        return {"n": len(v), "median": float(np.median(v)), "meanAbs": float(np.mean(np.abs(v))), "maxAbs": float(np.max(np.abs(v)))}
    keys = ("nullDb", "f0A", "f0B", "f0Diff", "levelDiff", "onsetDiff", "specDiffDb", "centroidCents")
    res["summary"] = {k: stats(k) for k in keys}
    allRows = rows
    for name, sel in (("tuned", lambda r: r.get("cents", 0) != 0), ("untuned", lambda r: r.get("cents", 0) == 0)):
        rows = [r for r in allRows if "cents" in r and sel(r)]
        res["summary_" + name] = {k: stats(k) for k in keys}
    rows = allRows
    print(json.dumps({"part": part, "null": null, "summary": res["summary"], "tuned": res["summary_tuned"], "untuned": res["summary_untuned"], "notes": len(written), "events": [len(onsA), len(onsB)]}, indent=1))
    if out:
        json.dump(res, open(out, "w"), indent=1)

main()
