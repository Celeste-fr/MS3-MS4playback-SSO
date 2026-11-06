#!/usr/bin/env python3
"""spheres_k550_staccato.py: the level side of spheres_k550_staccato.json (technique_offsets.py -> technique_offsets.json ->
the map's heldOffset), re-runnable from the public Spheres stems.

    spheres_fetch.py get ...                 the close stems Violin_1_Violin_1.flac, Cello_Cello.flac, Bass_1_Bass.flac
    spheres_k550_staccato.py envelopes <stem dir>       each stem's perceived envelope -> <stem>_stl.npz next to it
    spheres_k550_staccato.py measure <npz dir> <out.json> [--times-from spheres_k550_staccato.json]
    spheres_k550_staccato.py compare <npz dir> [--json spheres_k550_staccato.json]

What is a reading and what is measured: each note's start t0 and length dur are readings (the owner confirmed or
corrected them on 2026-10-15, review page https://claude.ai/artifact/8FoTWaNAb3Ekf2zFEDqWg8: which notes are single held
notes, which staccatos, double stops left out), kept in the json itself (--times-from); 'stl' is measured: the maximum of
tools/playbackverify/loudness.py's perceived envelope (ArticulationCheck's STL, 5 ms hop) of the section's stem over
[t0, t0 + dur].

compare, 2026-10-26, with the envelopes of the accent-spheres study (2026-10-20, the same stems at 48 kHz): the held,
quarter and whole-note passages within 0.06 dB median (max 0.33 except two whole notes, 3.7 / 2.2), the staccato passages
0.05 (Violins 1), 1.07 (Celli), 0.94 (Basses) dB higher in the median, up to 5.7 on single notes. So the committed
staccato levels were not taken over [t0, t0 + dur] of this envelope (their 2026-10-15 script is lost; the window or the
segmentation it used is unknown). technique_offsets.py on the re-measured values gives the bowed-staccato D -2.6 dB
[-4.7, -0.5] instead of the committed -3.4 [-5.7, -0.9] (inside its interval); the committed file is left as it is
(an owner-reviewed value; the difference is reported in docs/OPEN_ITEMS.md item 36).
"""
import argparse
import copy
import json
import os
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "playbackverify"))
STEMS = {"Violin_1": "Violin_1_Violin_1", "Cello": "Cello_Cello", "Bass_1": "Bass_1_Bass"}
JSON = os.path.join(HERE, "spheres_k550_staccato.json")


def envelopes(stem_dir):
    import numpy as np
    import soundfile as sf
    import loudness
    for f in STEMS.values():
        x, sr = sf.read(os.path.join(stem_dir, f + ".flac"), dtype="float64", always_2d=True)
        env, first, hop = loudness.perceived_envelope(x[:, 0] if x.shape[1] == 1 else x, sr)
        np.savez(os.path.join(stem_dir, f + "_stl.npz"), env=env.astype(np.float32), first=first, hop=hop)
        print(f, len(env), first, hop)


def measured(npz_dir, times):
    import numpy as np
    d = copy.deepcopy(times)
    for sec, groups in d["sections"].items():
        z = np.load(os.path.join(npz_dir, STEMS[sec] + "_stl.npz"))
        env, first, hop = z["env"], float(z["first"]), float(z["hop"])
        for notes in groups.values():
            for n in notes:
                i = int((n["t0"] * 1000 - first) / hop)
                j = int(((n["t0"] + n["dur"]) * 1000 - first) / hop)
                n["stl"] = round(float(env[max(i, 0):max(j, i + 1)].max()), 2)
    return d


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("envelopes"); e.add_argument("stems")
    m = sub.add_parser("measure"); m.add_argument("npz"); m.add_argument("out"); m.add_argument("--times-from", default=JSON)
    c = sub.add_parser("compare"); c.add_argument("npz"); c.add_argument("--json", default=JSON)
    a = ap.parse_args()
    if a.cmd == "envelopes":
        envelopes(a.stems)
    elif a.cmd == "measure":
        d = measured(a.npz, json.load(open(a.times_from, encoding="utf-8")))
        with open(a.out, "w", encoding="utf-8") as f:
            json.dump(d, f, indent=1, ensure_ascii=False)
            f.write("\n")
    else:
        old = json.load(open(a.json, encoding="utf-8"))
        new = measured(a.npz, old)
        for sec, groups in old["sections"].items():
            for g, notes in groups.items():
                dd = [m["stl"] - n["stl"] for n, m in zip(notes, new["sections"][sec][g])]
                print(f"{sec:9s} {g:38s} n {len(dd):3d}  median {st.median(dd):+.2f}  max |d| {max(map(abs, dd)):.2f}")


if __name__ == "__main__":
    main()
