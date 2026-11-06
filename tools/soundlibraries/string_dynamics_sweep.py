#!/usr/bin/env python3
"""The string sections' Long and velocity techniques measured again, with repeats (sso_string_dynamics.json).

    string_dynamics_sweep.py events <folder>             kthost event files + manifests (<code>.txt, <code>.json, list.txt)
    string_dynamics_sweep.py analyze <folder>            on the VM, after kthost: <code>.wav -> results.json
    string_dynamics_sweep.py curves <results.json> [-o sso_string_dynamics.json]

Why (2026-10-12): the shipped calibration (sso_sound_dynamics.json, the owner's dynamics check: one note per point) has
Celli's All techniques Long "neither" and nearly flat (perceived 44.1 .. 55.1, not monotonic), so [levels] calibrated
played every Celli short at velocity ~96-127. The check plays each point once; SSO's string Longs alternate round
robins up to 4 dB apart at soft CC1 (sso_cc1_curves.json, Celli 59: repeat spread 3.8-4.1 dB at CC1 16-48), and it
sends CC1 0.1 s before the note-on. This measures the five sections' All techniques patches again, Long and the
techniques the calibration compares with it (the map's played articulations with a technique of <Dynamics velocity>), at the
same pitch and with the same measures as the check, every point REPEATS times.

Method (as Check articulations > Dynamics plays a curve, ArticulationCheck::dynamics, Player::play): kthost on the
Windows VM, one Kontakt 8 instance with the patch's MuseScore setup (All techniques, library defaults), 44.1 kHz.
CC11 127 first and a warm-up Long (not measured). Each note: CC32 (the technique), CC1 and CC11 PRE s before the
note-on, velocity = CC1 = x, held 1.0 s with 0.5 s of tail measured (a technique on velocity: 0.5 s and 0.2 s, as the
check), a note every NOTE + GAP s; the REPEATS notes of a point in a row (SSO's strings alternate round robins: an even
count plays each equally). Per technique: the curve at x = 16, 32 .. 112, 127; the check's classification (velocity 32
CC1 127, velocity 127 CC1 32: drivenBy, 3 dB from x 32); Long also the expression curve (CC11 = 16 .. 112 with velocity
= CC1 = 80, the check's). PRE is 1.0 s (CC1 settled, as sso_cc1_sweep.py and rachm_register_sweep.py); Long is also
played once more as the check plays it (CC1 0.1 s before, the check's order of points: "replay", two passes; not
used in the curves, it shows what the check's timing reads).
Measures, of note-on .. note-off + tail (+ 0.2 s slack: kthost's audio starts up to 0.09 s after the event):
loud50 = the loudest 50 ms (stereo mean square, windows hopped 25 ms), dB, the check's 'curve'; perc = the peak of
the perceived loudness envelope (tools/playbackverify/loudness.py, ArticulationCheck::perceivedLoudnessDb in numpy),
the check's 'perceived'. Per point the median of the repeats and their spread (max - min).
"""
import argparse
import collections
import json
import os
import statistics
import sys
import types
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
MAP = os.path.join(ROOT, "share", "soundlibraries", "Spitfire Symphony Orchestra.xml")
SHIPPED = os.path.join(HERE, "sso_sound_dynamics.json")
XS = [16, 32, 48, 64, 80, 96, 112, 127]               # ArticulationCheck::CURVE_POINTS
EXPRESSION = [16, 32, 48, 64, 80, 96, 112]             # ArticulationCheck::EXPRESSION_POINTS
SECTIONS = {"v1": "Violins 1", "v2": "Violins 2", "va": "Violas", "vc": "Celli", "cb": "Basses"}
LONG = 1
REPEATS = 4
PRE, START, SLACK = 1.0, 2.0, 0.2
GAP_LONG, GAP_SHORT = 3.0, 2.5
REPLAY = [(32, 32), (32, 127), (127, 32)] + [(x, x) for x in XS if x != 32]   # the check's order (velocity, CC1)


def played_values(map_path):
    """{patch: {value: on velocity}} of the map's Articulations with techniques: on velocity when one of its techniques
    is in <Dynamics velocity> (the techniques [levels] calibrated plays by velocity, rendermidi.cpp libVelocity)"""
    root = ET.parse(map_path).getroot()
    on_velocity = set()
    for d in root.iter("Dynamics"):
        on_velocity |= set((d.get("velocity") or "").split())
    out = {}
    for ins in root.iter("Instrument"):
        out[ins.get("name")] = {int(a.get("value")): bool(on_velocity & set(a.get("techniques").split()))
                                for a in ins.findall("Articulation") if a.get("techniques")}
    return out


def plan(shipped, played):
    """per section code: (patch, test pitch, [(name, value, pitch, short note)]): Long and the played techniques on
    velocity (played_values); short note: the check's 0.5 s note for a technique it found on velocity alone"""
    out = {}
    for code, patch in SECTIONS.items():
        s = shipped[patch]
        techs = []
        for name, e in s.items():
            if not isinstance(e, dict) or e.get("value") not in played.get(patch, {}) or "curve" not in e:
                continue
            if e["value"] == LONG or played[patch][e["value"]]:
                techs.append((name, e["value"], e.get("pitch", s["pitch"]), e.get("drivenBy") == "velocity"))
        techs.sort(key=lambda t: (t[1] != LONG, t[1]))
        out[code] = (patch, s["pitch"], techs)
    return out


def events(folder, shipped_path=SHIPPED, map_path=MAP):
    os.makedirs(folder, exist_ok=True)
    with open(shipped_path) as f:
        shipped = json.load(f)
    for code, (patch, pitch, techs) in plan(shipped, played_values(map_path)).items():
        lines = ["0.200000 cc 11 127", "0.200000 cc 32 %d" % LONG, "0.200000 cc 1 64",
                 "1.000000 on %d 64" % pitch, "2.000000 off %d 0" % pitch]
        man = []
        t = START + 3.0

        def note(name, value, p, vel, cc, expr, hold, tail, kind, pre=PRE, gap=GAP_LONG, rep=0):
            nonlocal t
            lines.extend(["%.6f cc 32 %d" % (t - pre, value), "%.6f cc 1 %d" % (t - pre, cc),
                          "%.6f cc 11 %d" % (t - pre, expr),
                          "%.6f on %d %d" % (t, p, vel), "%.6f off %d 0" % (t + hold, p)])
            man.append({"name": name, "value": value, "pitch": p, "velocity": vel, "cc": cc, "expression": expr,
                        "kind": kind, "repeat": rep, "time": round(t, 6), "hold": hold, "tail": tail})
            t += hold + gap

        for name, value, p, on_velocity in techs:
            hold, tail = (0.5, 0.2) if on_velocity else (1.0, 0.5)
            gap = GAP_SHORT if on_velocity else GAP_LONG
            for x in XS:
                for r in range(REPEATS):
                    note(name, value, p, x, x, 127, hold, tail, "curve", gap=gap, rep=r)
            for vel, cc in ((32, 127), (127, 32)):
                for r in range(REPEATS):
                    note(name, value, p, vel, cc, 127, hold, tail, "probe", gap=gap, rep=r)
            if value == LONG:
                for e in EXPRESSION:
                    for r in range(REPEATS):
                        note(name, value, p, 80, 80, e, hold, tail, "expression", rep=r)
                for r in range(2):
                    for vel, cc in REPLAY:
                        note(name, value, p, vel, cc, 127, hold, tail, "replay", pre=0.1, gap=2.6, rep=r)
        lines.sort(key=lambda ln: (float(ln.split()[0]), {"cc": 0, "off": 1, "on": 2}[ln.split()[1]]))
        with open(os.path.join(folder, code + ".txt"), "w") as f:
            f.write("\n".join(lines) + "\n")
        with open(os.path.join(folder, code + ".json"), "w") as f:
            json.dump({"patch": patch, "pitch": pitch, "notes": man}, f)
        print("%s %s: %d notes, %.0f s" % (code, patch, len(man), t))
    with open(os.path.join(folder, "list.txt"), "w") as f:
        f.write("".join("%s|%s\n" % (c, p) for c, p in SECTIONS.items()))


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
            clip = x[a:int(round((n["time"] + n["hold"] + n["tail"] + SLACK) * sr))]
            p = (clip ** 2).mean(axis=1)
            c = np.concatenate([[0.0], np.cumsum(p)])
            loud = max((c[i + win] - c[i]) / win for i in range(0, len(p) - win + 1, win // 2))
            env, _, _ = perceived_envelope(clip, sr)
            nz = np.nonzero(np.abs(clip).max(axis=1) > 1e-3)[0]
            pre = x[max(0, a - int(0.3 * sr)):a]
            r = dict(n)
            r.update({"loud50": round(db(loud), 3), "perc": round(float(env.max()), 3),
                      "lag": round(float(nz[0]) / sr, 4) if len(nz) else None,
                      "pre": round(db(float((pre ** 2).mean())), 1)})
            res.append(r)
        out[code] = {"patch": man["patch"], "pitch": man["pitch"], "sr": sr, "notes": res}
        print(code, man["patch"], len(res), "notes")
    with open(os.path.join(folder, "results.json"), "w") as f:
        json.dump(out, f)


def med(v):
    return round(statistics.median(v), 2)


def spread(v):
    return round(max(v) - min(v), 2)


def curves(results):
    doc = {}
    for code, d in results.items():
        patch = d["patch"]
        by = collections.defaultdict(lambda: collections.defaultdict(list))
        for n in d["notes"]:
            by[(n["name"], n["value"], n["pitch"])][(n["kind"], n["velocity"], n["cc"], n["expression"])].append(n)
        sec = {"pitch": d["pitch"]}
        for (name, value, pitch), pts in by.items():
            def m(kind, vel, cc, expr=127, key="loud50"):
                return [n[key] for n in pts[(kind, vel, cc, expr)]]
            e = {"value": value}
            if pitch != d["pitch"]:
                e["pitch"] = pitch
            d32, v127, c127 = med(m("curve", 32, 32)), med(m("probe", 127, 32)), med(m("probe", 32, 127))
            v, c = v127 - d32, c127 - d32
            e["drivenBy"] = "both" if v >= 3 and c >= 3 else "velocity" if v >= 3 else "controller" if c >= 3 else "neither"
            e["curve"] = [[x, med(m("curve", x, x))] for x in XS]
            e["perceived"] = [[x, med(m("curve", x, x, key="perc"))] for x in XS]
            e["velocityDb"] = [d32, v127]
            e["controllerDb"] = [d32, c127]
            if value == LONG:
                e["expression"] = [[x, med(m("expression", 80, 80, x))] for x in EXPRESSION] + [[127, e["curve"][4][1]]]
                e["expressionPerceived"] = ([[x, med(m("expression", 80, 80, x, "perc"))] for x in EXPRESSION]
                                            + [[127, e["perceived"][4][1]]])
                rep = [n for k, v in pts.items() if k[0] == "replay" for n in v]
                e["replay"] = {"%d/%d" % (vel, cc): {"loud50": [n["loud50"] for n in rep if (n["velocity"], n["cc"]) == (vel, cc)],
                                                     "perc": [n["perc"] for n in rep if (n["velocity"], n["cc"]) == (vel, cc)]}
                               for vel, cc in REPLAY}
            e["spread"] = {"curve": [[x, spread(m("curve", x, x))] for x in XS],
                           "perceived": [[x, spread(m("curve", x, x, key="perc"))] for x in XS]}
            e["repeats"] = REPEATS
            sec[name] = e
        notes = d["notes"]
        sec["checks"] = {"preMaxDb": max(n["pre"] for n in notes),
                         "lagMaxS": max(n["lag"] for n in notes if n["lag"] is not None),
                         "silent": sum(1 for n in notes if n["lag"] is None)}
        doc[patch] = sec
    return {
        "about": "SSO's five string sections' All techniques patches: Long and the techniques measured on velocity "
                 "or both (the map's played ones), 2026-10-12 (branch long-calibration; "
                 "tools/soundlibraries/string_dynamics_sweep.py has the method). Windows test VM, the real Spitfire "
                 "Symphony Orchestra in Kontakt Player 8 (VST 3; MuseScore's resaved setups, library defaults), "
                 "offline through kthost, 44.1 kHz. At the check's pitch, with the check's measures and note "
                 "lengths, each point played %d times in a row, CC1 1.0 s before the note-on: 'curve' the loudest "
                 "50 ms, 'perceived' the perceived loudness peak, each the median of the repeats ('spread': their "
                 "max - min); 'drivenBy', 'velocityDb', 'controllerDb' as the check (medians); Long's 'expression' "
                 "(CC11 at velocity = CC1 = 80; 127: the curve's 80) and 'replay' (Long played as the check does: "
                 "CC1 0.1 s before, the check's order of points, two passes; each value). checks: the loudest 0.3 s "
                 "before a note-on, the latest audio start after a note-on, notes without sound. "
                 "calibration_from_sound_dynamics.py -s puts the entries into the shipped dynamics.json in place of "
                 "sso_sound_dynamics.json's." % REPEATS,
        "patches": {p: doc[p] for p in sorted(doc)},
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("command", choices=["events", "analyze", "curves"])
    ap.add_argument("path")
    ap.add_argument("-o", "--out", default=os.path.join(HERE, "sso_string_dynamics.json"))
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
        print("written", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
