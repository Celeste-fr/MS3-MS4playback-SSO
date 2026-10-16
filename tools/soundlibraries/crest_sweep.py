#!/usr/bin/env python3
"""SSO's string notes: how far a note's sample peak lies above its loudest 50 ms (sso_crest.json), for the master trim.

    crest_sweep.py events <folder>             kthost event files + manifests (<code>.txt, <code>.json, list.txt)
    crest_sweep.py analyze <folder>            on the VM, after kthost: <code>.wav -> results.json
    crest_sweep.py summary <results.json> [-o sso_crest.json]

Why (2026-10-12, branch utility-headroom): MuseScore's per-patch gain moved into a Utility after each Kontakt (no
more "everything lower so the loudest fader fits Live's +6 dB"); one trim per score keeps the score's loudest moment
from clipping the master instead (contextlevel.h › Master trim). The trim adds up each sounding note's level as the
dynamics calibration has it (dynamics.json: the loudest 50 ms, RMS) and needs how far a note's sample peak lies above
that: the crest, measured here.

Method (as string_dynamics_sweep.py, whose plan it reuses): kthost on the Windows VM, one Kontakt 8 instance with the
patch's MuseScore setup (library defaults), 44.1 kHz, offline. The five string sections' All techniques patches: Long
and the map's played velocity techniques, at the check's pitch; their Performance patches' Legato (one note, its
attack, no transition). Each note: CC32 (All techniques), CC1, CC11 127 PRE s before, velocity = CC1 = x for x in XS,
REPEATS in a row; held 1.0 s with 0.5 s tail (velocity techniques 0.5 s and 0.2 s). Measures over note-on .. note-off
+ tail + 0.2 s: loud50 = the loudest 50 ms (stereo mean square, hop 25 ms, dB: the calibration's 'curve'); peak = the
largest absolute sample of either channel, dBFS; crest = peak - loud50.
"""
import argparse
import json
import os
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import string_dynamics_sweep as sds          # noqa: E402  (its plan: patches, pitches, played techniques)

XS = [32, 64, 96, 127]
REPEATS = 2
PRE, START, SLACK = 1.0, 2.0, 0.2
PERFORMANCE_LEGATO = 20                      # the map's Performance Legato articulation value


def events(folder):
    os.makedirs(folder, exist_ok=True)
    with open(sds.SHIPPED) as f:
        shipped = json.load(f)
    plans = sds.plan(shipped, sds.played_values(sds.MAP))
    listing = []
    for code, (patch, pitch, techs) in plans.items():
        for perf in (False, True):
            name = patch + (" - Performance" if perf else "")
            fcode = code + ("p" if perf else "")
            lines = ["0.200000 cc 11 127", "0.200000 cc 1 64"]
            if not perf:
                lines.append("0.200000 cc 32 %d" % sds.LONG)
            lines += ["1.000000 on %d 64" % pitch, "2.000000 off %d 0" % pitch]
            man = []
            t = START + 3.0
            plan = [("Legato", PERFORMANCE_LEGATO, pitch, False)] if perf else techs
            for tname, value, p, on_velocity in plan:
                hold, tail = (0.5, 0.2) if on_velocity else (1.0, 0.5)
                gap = sds.GAP_SHORT if on_velocity else sds.GAP_LONG
                for x in XS:
                    for r in range(REPEATS):
                        if not perf:
                            lines.append("%.6f cc 32 %d" % (t - PRE, value))
                        lines += ["%.6f cc 1 %d" % (t - PRE, x), "%.6f cc 11 127" % (t - PRE),
                                  "%.6f on %d %d" % (t, p, x), "%.6f off %d 0" % (t + hold, p)]
                        man.append({"name": tname, "value": value, "pitch": p, "x": x, "repeat": r,
                                    "time": round(t, 6), "hold": hold, "tail": tail})
                        t += hold + gap
            lines.sort(key=lambda ln: (float(ln.split()[0]), {"cc": 0, "off": 1, "on": 2}[ln.split()[1]]))
            with open(os.path.join(folder, fcode + ".txt"), "w") as f:
                f.write("\n".join(lines) + "\n")
            with open(os.path.join(folder, fcode + ".json"), "w") as f:
                json.dump({"patch": name, "pitch": pitch, "notes": man}, f)
            listing.append("%s|%s\n" % (fcode, name))
            print("%s %s: %d notes, %.0f s" % (fcode, name, len(man), t))
    with open(os.path.join(folder, "list.txt"), "w") as f:
        f.write("".join(listing))


def analyze(folder):
    import numpy as np
    from scipy.io import wavfile

    def db(p):
        return float(10 * np.log10(p)) if p > 0 else -200.0

    out = {}
    with open(os.path.join(folder, "list.txt")) as f:
        codes = [ln.split("|")[0] for ln in f.read().splitlines() if ln]
    for code in codes:
        wav = os.path.join(folder, code + ".wav")
        if not os.path.exists(wav):
            continue
        with open(os.path.join(folder, code + ".json")) as f:
            man = json.load(f)
        sr, x = wavfile.read(wav)
        x = x.astype(np.float64) / (32768.0 if x.dtype.kind == "i" else 1.0)
        if x.ndim == 1:
            x = np.stack([x, x], axis=1)
        win = int(round(sr * 0.05))
        res = []
        for n in man["notes"]:
            a = int(round(n["time"] * sr))
            clip = x[a:int(round((n["time"] + n["hold"] + n["tail"] + SLACK) * sr))]
            p = (clip ** 2).mean(axis=1)
            c = np.concatenate([[0.0], np.cumsum(p)])
            loud = max((c[i + win] - c[i]) / win for i in range(0, len(p) - win + 1, win // 2))
            peak = float(np.abs(clip).max())
            r = dict(n)
            r.update({"loud50": round(db(loud), 3), "peak": round(db(peak * peak), 3)})
            r["crest"] = round(r["peak"] - r["loud50"], 3)
            res.append(r)
        out[code] = {"patch": man["patch"], "pitch": man["pitch"], "sr": sr, "notes": res}
        print(code, man["patch"], len(res), "notes, crest max %.2f" % max(r["crest"] for r in res))
    with open(os.path.join(folder, "results.json"), "w") as f:
        json.dump(out, f)


def summary(results):
    patches = {}
    every = []
    for code, d in results.items():
        by = {}
        for n in d["notes"]:
            if n["loud50"] < -90:               # (silent: no level to compare with)
                continue
            by.setdefault(n["name"], []).append(n)
            every.append(n["crest"])
        patches[d["patch"]] = {name: {"value": ns[0]["value"], "pitch": ns[0]["pitch"],
                                      "points": [[n["x"], n["loud50"], n["peak"], n["crest"]] for n in ns],
                                      "crestMax": max(n["crest"] for n in ns)}
                               for name, ns in by.items()}
    return {
        "about": "SSO's five string sections, the sample peak of a note against its loudest 50 ms (crest_sweep.py has "
                 "the method), 2026-10-12, branch utility-headroom. Windows test VM, the real Spitfire Symphony "
                 "Orchestra in Kontakt Player 8 (VST 3; MuseScore's resaved setups, library defaults), offline through "
                 "kthost, 44.1 kHz. points: [x (velocity = CC1), loud50 dB, peak dBFS, crest dB], %d repeats each. "
                 "crestMax: the largest crest of the patch's technique; crest: over every note measured (max, median). "
                 "MuseScore's master trim adds crest.max to each note's calibrated level (contextlevel.h › Master trim)."
                 % REPEATS,
        "crest": {"max": max(every), "median": round(statistics.median(every), 3), "notes": len(every)},
        "patches": {p: patches[p] for p in sorted(patches)},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["events", "analyze", "summary"])
    ap.add_argument("path")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "sso_crest.json"))
    a = ap.parse_args()
    if a.command == "events":
        events(a.path)
    elif a.command == "analyze":
        analyze(a.path)
    else:
        with open(a.path) as f:
            doc = summary(json.load(f))
        with open(a.out, "w") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
        print("written", a.out, doc["crest"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
