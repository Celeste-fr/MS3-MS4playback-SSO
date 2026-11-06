#!/usr/bin/env python3
"""percussion_levels.py: the producer of sso_percussion_levels.json (read by section_balance_cl.py and spheres_timpani.py:
the harp's, timpani's, percussion's and Grand Piano's sectionLevel2 / realSteps), re-runnable.

    percussion_levels.py events <work> [--only Harp,Timpani,...]
    (VM)  kthost renders of <work>/list.txt with MuseScore's resaved setups, e.g. cp tools/vm/kthost_batch.ps1 <work>/ &&
          tools/vm/vmjob.py --name perc --fetch wav --kill kthost.exe <work> -- powershell -NoProfile -ExecutionPolicy Bypass \\
          -File kthost_batch.ps1 -run {run} -secs 4000 -tail 8        (the harp's render is ~60 min of audio)
    percussion_levels.py analyse <wav dir> <work> <out.json> [--method-from tools/soundlibraries/sso_percussion_levels.json]
    percussion_levels.py check-events [<json>]

The method of 2026-10-15 (balance-refine iterations 6 / 7, and perc-keys-levels for the Grand Piano; their private scripts
nmb-br-mkperc.py / anaperc.py / mkpiano.py / mergepiano.py, folded in here; the harp / timpani / hits event maker was not
kept and is rebuilt from the file's "method"): every note through sso_cc1_sweep.make_events (CC32 the switch, CC11 127,
CC1 = velocity 1 s before each note-on, a warm-up note first), velocity = CC1 16 .. 127 (126 on the metal patches,
maxVelocity) or the patch's list, held 7 s; per note loudDb (sso_cc1_sweep.loudest50 over the note and 0.5 s more),
cumE (both channels' mean square through the IEC 61672 fast weighting, 125 ms, summed from 0.25 s before the note-on,
every 0.25 s up to 8.5 s; dB re 1 FS^2 s) and, on the timpani, f0Hz (the strongest spectral peak 30 .. 400 Hz in 0.3 ..
1.5 s). The VM renders were not kept: check-events checks that the plan below makes exactly the committed notes (pitch,
velocity, repeat, in order) of every patch (2026-10-26: all eight identical); the levels themselves need the VM.
Assumed in the rebuild (not recorded): the timpani's and hits' switch CC32 1 (as the harp's and piano's).
"""
import argparse
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sso_cc1_sweep as sw  # noqa: E402

CCS = [16, 32, 48, 64, 80, 96, 112, 127]
METAL = [16, 32, 48, 64, 80, 96, 112, 126]
# json name -> (patch, make_events arguments)
PLAN = {
    "Harp": ("Harp", dict(low=24, high=96, step=6, ccs=CCS, repeats=4, note=7.0, gap=1.5)),
    "Timpani": ("Timpani", dict(low=46, high=62, step=1, ccs=[32, 64, 80, 112], repeats=2, note=7.0, gap=1.5)),
    "Bass Drum": ("Percussion - Drums - Low - Bass Drum", dict(low=48, high=48, ccs=CCS, repeats=4, note=7.0, gap=6.0)),
    "Piatti": ("Percussion - Unpitched - Metal - Piatti", dict(low=48, high=48, ccs=CCS, repeats=4, note=7.0, gap=6.0)),
    "Snare 1": ("Percussion - Drums - High - Snare 1", dict(low=48, high=48, ccs=CCS, repeats=4, note=7.0, gap=6.0)),
    "Tam Tam": ("Percussion - Unpitched - Metal - Tam Tam", dict(low=48, high=48, ccs=METAL, repeats=4, note=7.0, gap=6.0)),
    "Triangle 1": ("Percussion - Unpitched - Metal - Triangle 1",
                   dict(low=48, high=48, ccs=METAL, repeats=4, note=7.0, gap=6.0)),
    "Grand Piano": ("Grand Piano", dict(low=21, high=105, step=6, extra=(108,), ccs=[32, 48, 64, 80, 96, 112], repeats=2,
                                         note=7.0, gap=1.5, warm_pitch=60)),
}
CUM_T = [round(0.25 * k, 2) for k in range(1, 35)]


def key_of(name):
    return name.lower().replace(" ", "_")


def make(name):
    patch, kw = PLAN[name]
    return sw.make_events(switch=1, **kw)


def events(work, only=None):
    os.makedirs(work, exist_ok=True)
    lst = []
    for name in PLAN:
        if only and name not in only:
            continue
        text, notes = make(name)
        k = key_of(name)
        open(os.path.join(work, k + ".txt"), "w", newline="\r\n").write(text)
        notes["name"], notes["patch"] = name, PLAN[name][0]
        json.dump(notes, open(os.path.join(work, k + ".notes.json"), "w"))
        lst.append("%s|%s|%d" % (k, PLAN[name][0], notes["warmPitch"]))
    open(os.path.join(work, "list.txt"), "w", newline="\r\n").write("\n".join(lst) + "\n")


def analyse_wav(wav, notes, timpani):
    import numpy as np
    import soundfile as sf
    from scipy.signal import lfilter
    x, sr = sf.read(wav, dtype="float32", always_2d=True)
    p2 = (x.astype("float64") ** 2).mean(axis=1)
    a = 1 - math.exp(-1 / (0.125 * sr))
    fast = lfilter([a], [1, a - 1], p2)
    out = []
    for n in notes["notes"]:
        i0 = int(round((n["on"] - 0.25) * sr))
        cum = np.cumsum(fast[i0:i0 + int(8.75 * sr)]) / sr
        e = [round(10 * math.log10(max(float(cum[min(len(cum) - 1, int((t + 0.25) * sr) - 1)]), 1e-30)), 3) for t in CUM_T]
        r = {"pitch": n["pitch"], "velocity": n["velocity"], "repeat": n["repeat"],
             "loudDb": round(sw.loudest50(x[int(n["on"] * sr):int((n["off"] + 0.5) * sr)], sr), 2), "cumE": e}
        if timpani:
            s = x[int((n["on"] + 0.3) * sr):int((n["on"] + 1.5) * sr)].astype("float64").mean(axis=1)
            sp = np.abs(np.fft.rfft(s * np.hanning(len(s)), n=1 << 18))
            f = np.fft.rfftfreq(1 << 18, 1 / sr)
            m = (f >= 30) & (f <= 400)
            r["f0Hz"] = round(float(f[m][np.argmax(sp[m])]), 2)
        out.append(r)
    return out


def analyse(wavs, work, out_path, method_from=None):
    method = json.load(open(method_from, encoding="utf-8"))["method"] if method_from else {}
    res = {"method": method, "patches": {}}
    for name in PLAN:
        k = key_of(name)
        wav = os.path.join(wavs, k + ".wav")
        if not os.path.exists(wav):
            continue
        notes = json.load(open(os.path.join(work, k + ".notes.json")))
        res["patches"][name] = {"patch": PLAN[name][0], "cumT": CUM_T,
                                "notes": analyse_wav(wav, notes, name == "Timpani")}
        print(name, len(res["patches"][name]["notes"]), "notes")
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(res, f, indent=0, separators=(",", ":"))
        f.write("\n")


def check_events(path):
    d = json.load(open(path, encoding="utf-8"))["patches"]
    bad = 0
    for name, v in d.items():
        want = [(n["pitch"], n["velocity"], n["repeat"]) for n in v["notes"]]
        got = [(n["pitch"], n["velocity"], n["repeat"]) for n in make(name)[1]["notes"]]
        same = want == got and v["patch"] == PLAN[name][0] and v["cumT"] == CUM_T
        bad += not same
        print(f"{name}: {len(got)} notes, {'identical' if same else 'DIFFERS'}")
    return bad == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events"); e.add_argument("work"); e.add_argument("--only")
    a_ = sub.add_parser("analyse"); a_.add_argument("wavs"); a_.add_argument("work"); a_.add_argument("out")
    a_.add_argument("--method-from")
    c = sub.add_parser("check-events"); c.add_argument("json", nargs="?", default=os.path.join(HERE, "sso_percussion_levels.json"))
    a = ap.parse_args()
    if a.cmd == "events":
        events(a.work, a.only.split(",") if a.only else None)
    elif a.cmd == "analyse":
        analyse(a.wavs, a.work, a.out, a.method_from)
    else:
        sys.exit(0 if check_events(a.json) else 1)


if __name__ == "__main__":
    main()
