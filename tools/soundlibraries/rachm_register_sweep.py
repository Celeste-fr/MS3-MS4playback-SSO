#!/usr/bin/env python3
"""Long (Rachm.)'s loudness curve along CC1 at several pitches per section (sso_rachm_register_curves.json).

    rachm_register_sweep.py events <folder>              kthost event files + manifests (<code>.txt, <code>.json, list.txt)
    rachm_register_sweep.py analyze <folder>             on the VM, after kthost: <code>.wav -> results.json
    rachm_register_sweep.py curves <results.json> [-o sso_rachm_register_curves.json]

The shipped dynamics calibration measured each technique at one pitch (sso_sound_dynamics.json: Violins 76,
Violas 69, Celli 59). Long (Rachm.)'s curve changes shape with the register (Violas 69 dips between CC1 64 and 89,
55 rises), so quickLevel ([slurs] quickLevel: a swapped note as loud as Long) converted at the wrong curve. This
measures it at:
  - the centre floor((lo + hi) / 2) of each constant quickLevel band of sso_rachm_levels.json,
  - the lowest and highest Long (Rachm.) key (sso_sound_range.json),
  - the pitches 4f0e4d93f1's sweep measured (sso_rachm_cc1_sweep.json: Violas 50 55 69 79, Violins 1 60 69 76 88),
  - the dynamics check's pitch (the shipped curve's).
Basses: no Long (Rachm.) in their All techniques patch (bdaaed4aa7), so none.

Method (as Check articulations > Dynamics plays a curve and as 4f0e4d93f1's sweep): kthost on the Windows VM, one
Kontakt 8 instance with the patch's MuseScore setup (All techniques, library defaults), 44.1 kHz. CC32 16 (the
switch) and CC11 127 first; each note: CC1 = x 1.0 s before the note-on, velocity x, held 1.0 s, a note every
3.5 s; x = 16, 20 .. 124 and 127. Then the dynamics pitch again at x 48, 80, 112: the round-robin check.
Measures, of note-on .. note-off + 0.5 s (+ 0.2 s slack: kthost's audio starts up to 0.09 s after the event):
loud50 = the loudest 50 ms (stereo mean square, windows hopped 25 ms), dB, the shipped curve's measure
(ArticulationCheck Player::play); perc = the peak of the perceived loudness envelope (tools/playbackverify/
loudness.py), the shipped 'perceived'.
"""
import argparse
import collections
import json
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
XS = list(range(16, 125, 4)) + [127]
SECTIONS = {   # code: (patch, pitches, the dynamics check's pitch)
    "v1": ("Violins 1", [55, 60, 65, 69, 76, 78, 88, 97], 76),
    "v2": ("Violins 2", [55, 65, 76, 78, 88, 97], 76),
    "va": ("Violas", [48, 50, 53, 55, 69, 74, 79, 90], 69),
    "vc": ("Celli", [36, 42, 51, 54, 59, 69, 82], 59),
}
SPACING, HOLD, PRE, START, TAIL, SLACK = 3.5, 1.0, 1.0, 2.0, 0.5, 0.2
REPEATS = (48, 80, 112)


def events(folder):
    os.makedirs(folder, exist_ok=True)
    for code, (patch, pitches, ref) in SECTIONS.items():
        notes = [(p, x, False) for p in pitches for x in XS] + [(ref, x, True) for x in REPEATS]
        lines = ["0.200000 cc 32 16", "0.200000 cc 11 127"]
        man = []
        for i, (p, x, rep) in enumerate(notes):
            t = START + SPACING * i
            lines += ["%.6f cc 1 %d" % (t - PRE, x), "%.6f on %d %d" % (t, p, x), "%.6f off %d 0" % (t + HOLD, p)]
            man.append({"pitch": p, "x": x, "time": t, "repeat": rep})
        with open(os.path.join(folder, code + ".txt"), "w") as f:
            f.write("\n".join(lines) + "\n")
        with open(os.path.join(folder, code + ".json"), "w") as f:
            json.dump({"patch": patch, "hold": HOLD, "notes": man}, f)
    with open(os.path.join(folder, "list.txt"), "w") as f:
        f.write("".join("%s|%s\n" % (c, v[0]) for c, v in SECTIONS.items()))


def analyze(folder):
    import numpy as np
    from scipy.io import wavfile
    sys.modules.setdefault("soundfile", types.ModuleType("soundfile"))
    sys.path.insert(0, os.path.join(os.path.dirname(HERE), "playbackverify"))
    sys.path.insert(0, folder)
    from loudness import perceived_envelope

    def db(p):
        return float(10 * np.log10(p)) if p > 0 else -200.0

    out = {}
    for code in SECTIONS:
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
            clip = x[a:int(round((n["time"] + man["hold"] + TAIL + SLACK) * sr))]
            p = (clip ** 2).mean(axis=1)
            c = np.concatenate([[0.0], np.cumsum(p)])
            loud = max((c[i + win] - c[i]) / win for i in range(0, len(p) - win + 1, win // 2))
            env, _, _ = perceived_envelope(clip, sr)
            nz = np.nonzero(np.abs(clip).max(axis=1) > 1e-3)[0]
            pre = x[max(0, a - int(0.3 * sr)):a]
            res.append({"pitch": n["pitch"], "x": n["x"], "repeat": n["repeat"], "loud50": round(db(loud), 3),
                        "perc": round(float(env.max()), 3), "lag": round(float(nz[0]) / sr, 4) if len(nz) else None,
                        "pre": round(db(float((pre ** 2).mean())), 1)})
        out[code] = {"patch": man["patch"], "sr": sr, "notes": res}
    with open(os.path.join(folder, "results.json"), "w") as f:
        json.dump(out, f)


def curves(results):
    patches, checks = {}, {}
    for code, d in results.items():
        by = collections.defaultdict(dict)
        for n in d["notes"]:
            if not n["repeat"]:
                by[n["pitch"]][n["x"]] = n
        rr = [max(abs(n["loud50"] - by[n["pitch"]][n["x"]]["loud50"]), abs(n["perc"] - by[n["pitch"]][n["x"]]["perc"]))
              for n in d["notes"] if n["repeat"]]
        checks[d["patch"]] = {"roundRobinMaxDb": round(max(rr), 3),
                              "preMaxDb": max(n["pre"] for n in d["notes"]),
                              "lagMaxS": max(n["lag"] for n in d["notes"] if n["lag"] is not None)}
        patches[d["patch"]] = {"Long (Rachm.)": {"value": 16, "pitches": {
            str(p): {"curve": [[x, round(by[p][x]["loud50"], 2)] for x in XS],
                     "perceived": [[x, round(by[p][x]["perc"], 2)] for x in XS]} for p in sorted(by)}}}
    return {
        "about": "Long (Rachm.)'s loudness along CC1 = velocity = x at several pitches per section, 2026-10-09 "
                 "(branch rachm-register-curves; tools/soundlibraries/rachm_register_sweep.py has the method and the "
                 "pitch rule). Windows test VM, real SSO in Kontakt 8 (All techniques patches at library defaults, "
                 "MuseScore's resaved setups), offline through kthost. 'curve': loud50 (the shipped curve's measure), "
                 "'perceived': the perceived loudness peak (the shipped 'perceived'). Checks per patch: the "
                 "round-robin repeats' largest difference (one round robin: 0), the loudest 0.3 s before a note-on, "
                 "the latest audio start after its note-on. Agrees with sso_rachm_cc1_sweep.json (4f0e4d93f1) "
                 "within 0.01 dB loud50 / 0.06 perceived where both measured. Basses: no Long (Rachm.) in their "
                 "All techniques patch. calibration_from_sound_dynamics.py puts these into the shipped "
                 "dynamics.json as each entry's 'pitches'.",
        "checks": checks,
        "patches": {p: patches[p] for p in sorted(patches)},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["events", "analyze", "curves"])
    ap.add_argument("path")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "sso_rachm_register_curves.json"))
    a = ap.parse_args()
    if a.command == "events":
        events(a.path)
    elif a.command == "analyze":
        analyze(a.path)
    else:
        with open(a.path) as f:
            doc = curves(json.load(f))
        with open(a.out, "w") as f:
            json.dump(doc, f, indent=1)
            f.write("\n")
        print("written", a.out, doc["checks"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
