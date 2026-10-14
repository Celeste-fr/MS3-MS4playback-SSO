#!/usr/bin/env python3
"""A held technique's level against CC1, per pitch, from offline renders of one Kontakt instance (2026-10-11: the string
sections' All techniques Long for the section-balance model; the dynamics check measures one pitch with 1 s notes, the
rest check only CC1 32 / 80 / 112).

    sso_cc1_sweep.py events <out.txt> <out.notes.json> --low L --high H [--step 3] [--extra P ...] [--switch 1]
                     [--cc 16 32 48 64 80 96 112 127] [--repeats 4] [--note 3.0] [--gap 1.5] [--velocity V]
    sso_cc1_sweep.py analyze <render.wav> <notes.json> [--name NAME] [--window A B] [--brightness] -o <part.json>
    sso_cc1_sweep.py merge <part.json> ... -o tools/soundlibraries/sso_cc1_curves.json

events: a score of MIDI events for the VM's offline host (kthost: "time on|off|cc a b"): the switch (CC32, UACC) and CC11
127 once, a warm-up note (not measured), then every pitch from low to high in steps (and the extras), at each CC1 value
`repeats` notes in a row (SSO's strings alternate two round robins: an even count plays each equally), held `note` s,
`gap` s apart. Velocity = CC1 (--velocity V: always V, for the Performance patches, where velocity picks the attack),
as MuseScore sends a Long (velocity and CC1 both expressionLevel of the dynamic). CC1 is
sent 1 s before its note-on, during the previous note's release.

analyze, per note (stereo float WAV, times from the notes file):
  rmsDb     mean power of both channels from SUSTAIN[0] to SUSTAIN[1] s after the note-on, dB full scale
  lufs      ITU-R BS.1770-4 integrated loudness (pyloudnorm: K-weighting, 400 ms blocks, gates) of the note-on to the
            note-off, LUFS
  sones     ISO 532-1 (Zwicker) stationary loudness (mosqito loudness_zwst, free field) of the mid channel over the
            sustain window; samples read as pascals (full scale 1 Pa = 94 dB SPL): only ratios mean anything
  loudDb    the dynamics check's measure: the loudest 50 ms (25 ms hop) of the whole note and 0.5 s of its release, dB
  loud1sDb  the same over its first 1.0 s and 0.5 s more (the dynamics check's note length: comparable with
            sso_sound_dynamics.json; the rest check holds pp 1.0 s, mf 1.5 s)
  --window A B   the sustain window, s after the note-on (default 1.5 3.0; a short: e.g. 0 0.35), for rmsDb, sones and
            the brightness measures
  --brightness  also brightness.py's measures of the window (stage B, 2026-10-12): scAmp / scPow (spectral centroid
            of the amplitude / power spectrum, Hz), hcAmp / hcPow (the same / f0, f0 = 440 Hz equal temperament),
            sharp (DIN 45692 sharpness at 60 dB SPL), sharpNative, sone, levelDb; left / right power-averaged
Per point: the median of the repeats and their spread (max - min). Per section: each measure minus its value at CC1 64
at the same pitch, the median over pitches ("relative64").
"""
import argparse
import json
import statistics
import sys

SUSTAIN = (1.5, 3.0)
CCS = [16, 32, 48, 64, 80, 96, 112, 127]
MEASURES = ["rmsDb", "lufs", "sones", "loudDb", "loud1sDb"]
BRIGHT = ["scAmp", "scPow", "hcAmp", "hcPow", "sharp", "sharpNative", "sone", "levelDb"]
RATIO = {"sones", "scAmp", "scPow", "hcAmp", "hcPow", "sharp", "sharpNative", "sone"}   # relative64: value / at 64


def make_events(low, high, step=3, extra=(), switch=1, ccs=CCS, repeats=4, note=3.0, gap=1.5, warm_pitch=None,
                velocity=None):
    pitches = sorted(set(list(range(low, high + 1, step)) + [p for p in extra if low <= p <= high]))
    ev = [(0.5, "cc", 32, switch), (0.5, "cc", 11, 127)]
    wp = warm_pitch if warm_pitch is not None else pitches[len(pitches) // 2]
    ev += [(0.5, "cc", 1, 64), (1.0, "on", wp, velocity or 64), (1.0 + note, "off", wp, 0)]
    t = 1.0 + note + gap
    notes = []
    for p in pitches:
        for c in ccs:
            for r in range(repeats):
                ev.append((round(t - 1.0, 4), "cc", 1, c))
                v = velocity or c
                ev.append((round(t, 4), "on", p, v))
                ev.append((round(t + note, 4), "off", p, 0))
                notes.append({"pitch": p, "cc": c, "velocity": v, "repeat": r, "on": round(t, 4), "off": round(t + note, 4)})
                t += note + gap
    ev.sort(key=lambda e: (e[0], {"cc": 0, "off": 1, "on": 2}[e[1]]))
    text = "".join(f"{e[0]:.4f} {e[1]} {e[2]} {e[3]}\n" for e in ev)
    return text, {"pitches": pitches, "ccs": list(ccs), "repeats": repeats, "note": note, "gap": gap,
                  "switch": switch, "warmPitch": wp, "velocity": velocity, "notes": notes}


def db(power):
    import math
    return 10 * math.log10(power) if power > 0 else -200.0


def loudest50(x, sr):
    import numpy as np
    p = (x.astype("float64") ** 2).mean(axis=1)
    win = int(round(0.05 * sr))
    if len(p) < win:
        return -200.0
    c = np.concatenate([[0.0], np.cumsum(p)])
    starts = np.arange(0, len(p) - win + 1, win // 2)
    return db(float(((c[starts + win] - c[starts]) / win).max()))


def measure(x, sr, on, off, meter=None, sones=True, window=SUSTAIN, bright=False, pitch=None):
    """the measures of one note in a stereo clip x (frames × 2), on / off in seconds"""
    import numpy as np
    i0, i1 = int(round(on * sr)), int(round(off * sr))
    s0, s1 = int(round((on + window[0]) * sr)), int(round((on + window[1]) * sr))
    seg = x[s0:s1].astype("float64")
    r = {"rmsDb": round(db(float((seg ** 2).mean())), 2)}
    if meter is not None and i1 - i0 > int(0.4 * sr):   # BS.1770's 400 ms block: a short note has no integrated LUFS
        v = meter.integrated_loudness(x[i0:i1].astype("float64"))
        r["lufs"] = round(float(v), 2) if np.isfinite(v) else -200.0
    if sones:
        from mosqito.sq_metrics import loudness_zwst
        n = loudness_zwst(seg.mean(axis=1), sr, field_type="free")[0]
        r["sones"] = round(float(n), 4)
    r["loudDb"] = round(loudest50(x[i0:min(len(x), i1 + int(0.5 * sr))], sr), 2)
    r["loud1sDb"] = round(loudest50(x[i0:min(len(x), i0 + int(1.5 * sr))], sr), 2)
    if bright:
        import brightness
        r.update(brightness.measure_signal(seg, sr, brightness.midi_f0(pitch)))
    return r


def analyze(wav, notes, name=None, sones=True, window=SUSTAIN, bright=False):
    import soundfile as sf
    import pyloudnorm
    x, sr = sf.read(wav, dtype="float32", always_2d=True)
    meter = pyloudnorm.Meter(sr)
    per = {}
    for n in notes["notes"]:
        m = measure(x, sr, n["on"], n["off"], meter, sones, window, bright, n["pitch"])
        per.setdefault((n["pitch"], n["cc"]), []).append(m)
    points = {}
    for (p, c), ms in sorted(per.items()):
        row = {"cc": c}
        for k in MEASURES + (BRIGHT if bright else []):
            vs = [m[k] for m in ms if k in m]
            if vs:
                row[k] = round(statistics.median(vs), 4 if k in RATIO else 2)
                row[k + "Spread"] = round(max(vs) - min(vs), 4 if k in RATIO else 2)
        points.setdefault(str(p), []).append(row)
    out = {"name": name, "sampleRate": sr, "pitches": notes["pitches"], "ccs": notes["ccs"],
           "repeats": notes["repeats"], "note": notes["note"], "switch": notes["switch"],
           "velocity": notes.get("velocity"), "window": list(window), "points": points}
    out["relative64"] = relative64(out, MEASURES + (BRIGHT if bright else []))
    return out


def relative64(sec, measures=MEASURES):
    """per measure, per CC1: [cc, median over pitches of (value - value at CC1 64 at that pitch; sones and the
    brightness measures but levelDb: value / value at CC1 64), its min, max, pitches, median repeat spread, largest
    repeat spread]"""
    rel = {}
    for k in measures:
        by_cc = {}
        spreads = {}
        for p, rows in sec["points"].items():
            ref = next((r for r in rows if r["cc"] == 64), None)
            if ref is None or k not in ref:
                continue
            for r in rows:
                if k in RATIO:
                    if ref[k] > 0 and r[k] > 0:
                        by_cc.setdefault(r["cc"], []).append(r[k] / ref[k])
                elif r[k] > -150 and ref[k] > -150:
                    by_cc.setdefault(r["cc"], []).append(r[k] - ref[k])
                spreads.setdefault(r["cc"], []).append(r[k + "Spread"])
        if by_cc:
            rel[k] = [[c, round(statistics.median(v), 3), round(min(v), 3), round(max(v), 3), len(v),
                       round(statistics.median(spreads[c]), 3), round(max(spreads[c]), 3)] for c, v in sorted(by_cc.items())]
    return rel


def dump(obj, indent=0):
    """JSON with a line per key down to the rows (lists of numbers on one line)"""
    pad = " " * indent
    if isinstance(obj, dict) and obj and not all(isinstance(v, (int, float, str)) for v in obj.values()):
        items = [f'{pad} {json.dumps(k)}: {dump(v, indent + 1).lstrip()}' for k, v in obj.items()]
        return pad + "{\n" + ",\n".join(items) + "\n" + pad + "}"
    if isinstance(obj, list) and obj and isinstance(obj[0], (list, dict)):
        return pad + "[\n" + ",\n".join(dump(v, indent + 1) for v in obj) + "\n" + pad + "]"
    return pad + json.dumps(obj)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events")
    e.add_argument("out")
    e.add_argument("notes")
    e.add_argument("--low", type=int, required=True)
    e.add_argument("--high", type=int, required=True)
    e.add_argument("--step", type=int, default=3)
    e.add_argument("--extra", type=int, nargs="*", default=[])
    e.add_argument("--switch", type=int, default=1)
    e.add_argument("--cc", type=int, nargs="*", default=CCS)
    e.add_argument("--repeats", type=int, default=4)
    e.add_argument("--note", type=float, default=3.0)
    e.add_argument("--gap", type=float, default=1.5)
    e.add_argument("--warm", type=int)
    e.add_argument("--velocity", type=int)
    a = sub.add_parser("analyze")
    a.add_argument("wav")
    a.add_argument("notes")
    a.add_argument("--name")
    a.add_argument("--window", type=float, nargs=2, default=list(SUSTAIN))
    a.add_argument("--brightness", action="store_true")
    a.add_argument("-o", "--out", required=True)
    m = sub.add_parser("merge")
    m.add_argument("parts", nargs="+")
    m.add_argument("-o", "--out", required=True)
    m.add_argument("--method", help="a JSON file with the method's description")
    args = ap.parse_args(argv)
    if args.cmd == "events":
        text, notes = make_events(args.low, args.high, args.step, args.extra, args.switch, args.cc, args.repeats,
                                  args.note, args.gap, args.warm, args.velocity)
        with open(args.out, "w", newline="\r\n") as f:
            f.write(text)
        with open(args.notes, "w") as f:
            json.dump(notes, f)
        print(f"{len(notes['notes'])} notes, {notes['notes'][-1]['off'] + args.gap:.0f} s, pitches {notes['pitches']}")
    elif args.cmd == "analyze":
        with open(args.notes) as f:
            notes = json.load(f)
        out = analyze(args.wav, notes, args.name, window=tuple(args.window), bright=args.brightness)
        with open(args.out, "w") as f:
            json.dump(out, f)
        for k, rows in out["relative64"].items():
            print(k, " ".join(f"{r[0]}:{r[1]:+.2f}" for r in rows))
    else:
        out = {}
        if args.method:
            with open(args.method) as f:
                out["method"] = json.load(f)
        out["sections"] = {}
        for p in args.parts:
            with open(p) as f:
                d = json.load(f)
            out["sections"][d.pop("name")] = d
        with open(args.out, "w") as f:
            f.write(dump(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
