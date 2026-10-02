#!/usr/bin/env python3
"""Reads renders of make_fastrun_scores.py's scores (MuseScore --verify-playback --verify-wav with SSO): when each
slurred fast note is heard against its written time, and how even the notes of a slur are.

    analyze_fastruns.py <Fast X.notes.json> <render folder (holds "<part> library.wav")> [--json out.json]
    analyze_fastruns.py --whence <render folder> [--json out.json]   (the owner's Whence: part "Violoncellos" or
                        "cello", bars 1-4, sixteenths at 110 in 4-note slurs, read from "<part> strikes.tsv")

Per note whose pitch differs from the note before (a slur's first note too, against the slur before's last note):
  arriveMs  when the new pitch takes over (analyze_sweep.arrival: its unique harmonics pass the old pitch's for 20 ms)
  levelDb   the loudest perceived loudness over its written length (from 30 ms after its written time; loudness.py)
Prints per part and tempo: transitions' arrival median and 10-90 % (and the slurs' first notes'), and the level by
position in the slur (mean dB) with its spread (max - min).
"""
import argparse
import csv
import glob
import json
import os
import statistics

import numpy as np
import soundfile as sf

from analyze_sweep import arrival, env_at, hz
from loudness import perceived_envelope

NAMES = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(name):
    n = NAMES[name[0]]
    i = 1
    while name[i] in "#b":
        n += 1 if name[i] == "#" else -1
        i += 1
    return n + 12 * (int(name[i:]) + 1)


def whence_notes(folder):
    path = [f for f in glob.glob(os.path.join(folder, "**", "*strikes.tsv"), recursive=True)
            if "cello" in os.path.basename(f).lower()][0]
    beat = 60 / 110
    rows = []
    for r in csv.DictReader(open(path, encoding="utf-8"), delimiter="\t"):
        m, b = int(r["measure"]), float(r["beat"])
        if m > 4:
            continue
        t = ((m - 1) * 4 + b - 1) * beat
        k = round((t / beat) * 4)
        rows.append(dict(time=t, seconds=beat / 4, pitch=midi(r["pitches"].split()[0]), tempo=110, size=4, pos=k % 4,
                         first=(k % 4 == 0), group=f"110-4-{k // 4}"))
    return path.replace(" strikes.tsv", " library.wav"), rows


def played_ons(notes, played):
    """each note's note-on as played (the part's notes.tsv next to its wav), greedily in order"""
    ons = []
    for r in csv.DictReader(open(played, encoding="utf-8"), delimiter="\t"):
        ons.append((float(r["time"]), midi(r["pitch"])))
    used, out = set(), []
    for n in notes:
        hit = None
        for k, (t, p) in enumerate(ons):
            if k not in used and p == n["pitch"] and n["time"] - 0.6 <= t <= n["time"] + 0.02:
                hit = k
                break
        if hit is not None:
            used.add(hit)
        out.append(None if hit is None else ons[hit][0])
    return out


def analyse(notes, wav):
    ev = wav.replace(" library.wav", " notes.tsv")
    ons = played_ons(notes, ev) if os.path.exists(ev) else [None] * len(notes)
    x, sr = sf.read(wav, always_2d=True)
    m = x.mean(axis=1)
    env, first, hop = perceived_envelope(x, sr)
    out = []
    for i, n in enumerate(notes):
        t, L = n["time"], n["seconds"]
        r = dict(n)
        if ons[i] is not None:
            r["onMs"] = round((ons[i] - t) * 1000)
            if i and ons[i - 1] is not None:
                r["ioiMs"] = round((ons[i] - ons[i - 1]) * 1000)
        lo, hi = max(0, int(round((t + 0.03) * 1000 - first) / hop)), min(len(env), int(round(((t + L) * 1000 - first) / hop)))
        r["levelDb"] = round(float(env[lo:hi].max()), 1) if hi > lo else None
        prev = notes[i - 1] if i else None
        if prev and prev["pitch"] != n["pitch"] and t - (prev["time"] + prev["seconds"]) < 1e-3:
            a = arrival(m, sr, t, hz(prev["pitch"]), hz(n["pitch"]), prev["time"] - 0.05, t + L + 0.15)
            r["arriveMs"] = None if a is None else round(a * 1000)
            if a is not None and "onMs" in r:
                r["lagMs"] = r["arriveMs"] - r["onMs"]
        out.append(r)
    return out


def q(xs, f):
    xs = sorted(xs)
    return xs[min(len(xs) - 1, int(f * len(xs)))]


def summary(part, rows):
    lines = []
    for tempo in sorted({r["tempo"] for r in rows}):
        for size in sorted({r["size"] for r in rows if r["tempo"] == tempo}):
            rs = [r for r in rows if r["tempo"] == tempo and r["size"] == size]
            tr = [r["arriveMs"] for r in rs if not r["first"] and r.get("arriveMs") is not None]
            fi = [r["arriveMs"] for r in rs if r["first"] and r.get("arriveMs") is not None]
            lv = {p: [r["levelDb"] for r in rs if r["pos"] == p and r["levelDb"] is not None] for p in range(size)}
            means = [statistics.mean(lv[p]) for p in range(size) if lv[p]]
            s = f"{part:12s} {tempo:3d} x{size}  "
            s += (f"trans {statistics.median(tr):+4.0f} ({q(tr, .1):+4.0f}..{q(tr, .9):+4.0f}) n={len(tr):2d}" if tr else "trans -")
            s += (f"  first {statistics.median(fi):+4.0f}" if fi else "  first -")
            lag = [r["lagMs"] for r in rs if not r["first"] and "lagMs" in r]
            on = [r["onMs"] for r in rs if not r["first"] and "onMs" in r]
            s += (f"  on {statistics.median(on):+4.0f} lag {statistics.median(lag):4.0f}" if lag and on else "")
            s += "  lvl " + "/".join(f"{x:.1f}" for x in means) + f"  spread {max(means) - min(means):.1f}"
            lines.append(s)
    return "\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("notes", nargs="?")
    ap.add_argument("folder")
    ap.add_argument("--whence", action="store_true")
    ap.add_argument("--json")
    a = ap.parse_args()
    result = {}
    if a.whence:
        wav, notes = whence_notes(a.folder)
        result["Celli (Whence)"] = analyse(notes, wav)
    else:
        meta = json.load(open(a.notes, encoding="utf-8"))
        for p in meta["parts"]:
            wavs = glob.glob(os.path.join(a.folder, "**", "*" + p["part"] + " library.wav"), recursive=True)
            if wavs:
                result[p["part"]] = analyse(p["notes"], sorted(wavs)[-1])
    for part, rows in result.items():
        print(summary(part, rows))
    if a.json:
        json.dump(result, open(a.json, "w"), indent=0)


if __name__ == "__main__":
    main()
