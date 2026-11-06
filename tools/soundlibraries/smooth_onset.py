#!/usr/bin/env python3
"""smooth_onset.py: the producer of sso_smooth_onset.json (gen_spitfire_sso.py: <Articulation attackOnset> on the five string
sections' Performance patches, [slurs] smoothStart), re-runnable.

    smooth_onset.py events <work> [--pitches-from tools/soundlibraries/sso_smooth_onset.json]
    (VM)  kthost renders of <work>/list.txt, e.g. cp tools/vm/kthost_batch.ps1 <work>/ && tools/vm/vmjob.py --name smooth \\
          --fetch wav --kill kthost.exe <work> -- powershell -NoProfile -ExecutionPolicy Bypass -File kthost_batch.ps1 \\
          -run {run} -secs 600        (<work>/wav/<sec>.wav, fetched into <work>.out/wav)
    smooth_onset.py analyse <wav dir> <work> <out.json> [--about-from tools/soundlibraries/sso_smooth_onset.json]
    smooth_onset.py summary <json> [--check]

The method of 2026-10-12 (branch smooth-onset; its scripts were not kept, rebuilt from the file's _about): each section's
Performance patch from MuseScore's resaved setup (library defaults), CC1 64, CC11 127, 5 pitches over its range x 2 repeats
x 3 velocities (5 smooth, 112 spiccato, 80 the rest check's mf), each note held 3 s with 3.5 s of silence after (the
patch's round robins reset: every note a first note). Per note (rows: [pitch, velocity, repeat, onset ms, onset over a
2.9 s window, perceived peak ms after the note-on, peak level]): the onset as the map's (onset_detect.py isolated_onset:
the perceived-loudness envelope first within 15 dB of its peak in the 1.4 s from the note-on, minus 30 ms), the same over
2.9 s, the peak in those 2.9 s. summary: median per attack over all rows, byPitch the mean of the repeats (rounded 0.1).
2026-10-26: summary on the committed rows reproduces its median and byPitch exactly; events / analyse are untested
against the VM (the committed rows' pitches are the default plan).
"""
import argparse
import json
import os
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "playbackverify"))
sys.path.insert(0, HERE)
SECTIONS = [("v1", "Violins 1 - Performance"), ("v2", "Violins 2 - Performance"), ("va", "Violas - Performance"),
            ("vc", "Celli - Performance"), ("cb", "Basses - Performance")]
VELOCITIES = {"smooth": 5, "spiccato": 112, "mf": 80}
HOLD_S, GAP_S, LEAD_S, REPEATS = 3.0, 3.5, 1.0, 2


def plan(pitches):
    t, notes = LEAD_S, []
    for p in pitches:
        for r in range(REPEATS):
            for v in VELOCITIES.values():
                notes.append((round(t, 6), p, v, r))
                t += HOLD_S + GAP_S
    return notes


def events(work, pitches_from):
    os.makedirs(work, exist_ok=True)
    src = json.load(open(pitches_from, encoding="utf-8"))["sections"]
    lst = []
    for key, patch in SECTIONS:
        pitches = sorted({r[0] for r in src[patch]["rows"]})
        ev = ["0.000000 cc 1 64", "0.000000 cc 11 127"]
        for t, p, v, r in plan(pitches):
            ev += ["%.6f on %d %d" % (t, p, v), "%.6f off %d 0" % (t + HOLD_S, p)]
        with open(os.path.join(work, key + ".txt"), "w", newline="\r\n") as f:
            f.write("\n".join(ev) + "\n")
        lst.append("%s|%s|%d" % (key, patch, pitches[len(pitches) // 2]))
        json.dump({"pitches": pitches}, open(os.path.join(work, key + ".json"), "w"))
    with open(os.path.join(work, "list.txt"), "w", newline="\r\n") as f:
        f.write("\n".join(lst) + "\n")


def onset(env, first, hop, t, span):
    import numpy as np
    from onset_detect import PEAK_DROP_DB, LATENCY_MS
    a = max(0, int(round((t * 1000 - first) / hop)))
    b = min(len(env), int(round(((t + span) * 1000 - first) / hop)))
    seg = env[a:b]
    k = int(np.argmax(seg))
    i = int(np.argmax(seg[:k + 1] >= seg[k] - PEAK_DROP_DB))
    on = max(0.0, ((first + hop * (a + i)) / 1000 - t) * 1000 - LATENCY_MS)
    return on, (first + hop * (a + k)) / 1000 * 1000 - t * 1000, float(seg[k])


def analyse(wavs, work, out, about_from=None):
    import soundfile as sf
    from loudness import perceived_envelope
    from onset_detect import ISO_SPAN_S
    res = {"_about": json.load(open(about_from, encoding="utf-8"))["_about"] if about_from else "", "sections": {}}
    for key, patch in SECTIONS:
        wav = os.path.join(wavs, key + ".wav")
        if not os.path.exists(wav):
            continue
        x, sr = sf.read(wav, always_2d=True)
        env, first, hop = perceived_envelope(x, sr)
        rows = []
        for t, p, v, r in plan(json.load(open(os.path.join(work, key + ".json")))["pitches"]):
            on, _, _ = onset(env, first, hop, t, ISO_SPAN_S)
            on29, peak_ms, peak = onset(env, first, hop, t, 2.9)
            rows.append([p, v, r, round(on, 1), round(on29, 1), round(peak_ms, 1), round(peak, 1)])
        res["sections"][patch] = {"rows": rows}
    summarise(res)
    with open(out, "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1, ensure_ascii=False)
        f.write("\n")


def summary_of(rows):
    med = {k: round(st.median([r[3] for r in rows if r[1] == v]), 1) for k, v in VELOCITIES.items()}
    med["smoothMinusSpiccato"] = round(med["smooth"] - med["spiccato"], 1)
    pitches = sorted({r[0] for r in rows})
    by = {k: {str(p): round(st.mean([r[3] for r in rows if r[1] == v and r[0] == p]), 1) for p in pitches}
          for k, v in VELOCITIES.items()}
    return med, by


def summarise(d):
    for patch, s in d["sections"].items():
        med, by = summary_of(s["rows"])
        d["sections"][patch] = {"median": med, "byPitch": by, "rows": s["rows"]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events"); e.add_argument("work")
    e.add_argument("--pitches-from", default=os.path.join(HERE, "sso_smooth_onset.json"))
    a_ = sub.add_parser("analyse"); a_.add_argument("wavs"); a_.add_argument("work"); a_.add_argument("out")
    a_.add_argument("--about-from")
    s = sub.add_parser("summary"); s.add_argument("json"); s.add_argument("--check", action="store_true")
    a = ap.parse_args()
    if a.cmd == "events":
        events(a.work, a.pitches_from)
    elif a.cmd == "analyse":
        analyse(a.wavs, a.work, a.out, a.about_from)
    else:
        d = json.load(open(a.json, encoding="utf-8"))
        bad = 0
        for patch, sec in d["sections"].items():
            med, by = summary_of(sec["rows"])
            same = med == sec.get("median") and by == sec.get("byPitch")
            bad += not same
            print(f"{patch}: median {med}{'' if same else '  DIFFERS from the file'}")
        sys.exit(1 if a.check and bad else 0)


if __name__ == "__main__":
    main()
