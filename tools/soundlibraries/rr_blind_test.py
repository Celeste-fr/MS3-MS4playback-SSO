#!/usr/bin/env python3
"""Blind-test clips: SSO's two alternating round robins as they are (OFF) against evened out by a per-note CC11 (ON).

SSO's strings play two round robins per key, alternating from a transport start (take A on a key's 1st, 3rd ... press,
B on its 2nd, 4th ...; offline a round robin's takes are identical). sso_cc1_curves.json has each take's level (rmsDbTakes,
even = A, odd = B) per pitch and CC1 for Celli and Basses. ON sets each note's CC11 just before its note-on so that its
predicted take plays at the mean of the two: offset = (A + B) / 2 - take dB, CC11 = base * 10^(offset / 20). SSO's CC11 is
a plain gain, dB = 20 log10(cc11 / 127) (measured 2026-10-14 with this script, within 0.02 dB; a first run with 40 log10
corrected only half). OFF sends the same base CC11 throughout.

Measured 2026-10-14 (VM, Kontakt 8 + SSO, kthost): the alternation is predicted exactly, but in these short notes (q = 96,
level 0.10-0.55 s after the note-on) the takes differ by up to ~5 dB more than in the sweep's sustain (1.5-3.0 s): Basses'
two takes have different attack envelopes, so sustain levels don't predict short notes.

    rr_blind_test.py events <items.json> <outdir>       kthost event files <item>_off.txt / _on.txt and <item>.notes.json
    rr_blind_test.py analyze <items.json> <wavdir> -o <levels.json>   per-note levels of both renders, spreads
    rr_blind_test.py clips <levels.json> <wavdir> <audiodir> --key <key.json> [--seed N]   blind mp3s and the key

items.json: {"<item>": {"section": "Basses", "pitch": 28, "cc": 80, "line": [28, 31, ...], "warm": 48}, ...}
Phrase: 8 quarter notes on the pitch at q = 96 (each 90 % long), a half rest, the 8-note line, all at one CC1
(velocity = CC1, as MuseScore sends a Long). A warm-up note on another key (not in the clip) first, as the sweep did.
"""
import argparse
import json
import math
import os
import random
import statistics as st
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CURVES = os.path.join(HERE, "sso_cc1_curves.json")
Q = 60.0 / 96
LEN = 0.9 * Q
START = 5.0          # first phrase note-on, s
LEAD = 0.5           # clip lead before it
TAIL = 2.5           # after the last note-off
BASE = 90            # CC11 for every note OFF, the centre ON (room for +3 dB under 127)
WINDOW = (0.10, 0.55)  # per-note level window after the note-on, s (note-off at 0.5625)
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def name(p):
    return f"{NAMES[p % 12]}{p // 12 - 1}"


def takes(section, pitch, cc, curves):
    row = next(r for r in curves["sections"][section]["points"][str(pitch)] if r["cc"] == cc)
    t = row["rmsDbTakes"]
    return st.mean(t[0::2]), st.mean(t[1::2])


def phrase(item):
    """[(on, pitch)] of the 16 notes"""
    p = item["pitch"]
    ons = [(START + i * Q, p) for i in range(8)]
    t = START + 10 * Q  # 8 quarters + half rest
    ons += [(t + i * Q, q) for i, q in enumerate(item["line"])]
    return ons


def plan(item, curves):
    """per note: on, pitch, press count on its key, predicted take, offset dB, CC11 ON"""
    count = {}
    out = []
    for on, p in phrase(item):
        k = count.get(p, 0)
        count[p] = k + 1
        a, b = takes(item["section"], p, item["cc"], curves)
        take = "A" if k % 2 == 0 else "B"
        off = (a + b) / 2 - (a if take == "A" else b)
        cc11 = max(1, min(127, int(round(BASE * 10 ** (off / 20)))))
        out.append({"on": round(on, 4), "pitch": p, "press": k, "take": take, "takeA": round(a, 2), "takeB": round(b, 2),
                    "offsetDb": round(off, 2), "cc11": cc11, "cc11Db": round(20 * math.log10(cc11 / BASE), 2)})
    return out


def events(item, notes, on_version):
    c = item["cc"]
    ev = [(0.5, "cc", 32, 1), (0.5, "cc", 11, BASE), (0.5, "cc", 1, c), (1.0, "on", item["warm"], c), (2.0, "off", item["warm"], 0)]
    for n in notes:
        if on_version:
            ev.append((n["on"] - 0.01, "cc", 11, n["cc11"]))
        ev.append((n["on"], "on", n["pitch"], c))
        ev.append((n["on"] + LEN, "off", n["pitch"], 0))
    ev.sort(key=lambda e: (e[0], {"cc": 0, "off": 1, "on": 2}[e[1]]))
    return "".join(f"{e[0]:.4f} {e[1]} {e[2]} {e[3]}\n" for e in ev)


def level(x, sr, on):
    i0, i1 = int(round((on + WINDOW[0]) * sr)), int(round((on + WINDOW[1]) * sr))
    seg = x[i0:i1].astype("float64")
    return 10 * math.log10(float((seg ** 2).mean()))


def spread(vs):
    return {"maxMinusMin": round(max(vs) - min(vs), 2), "sd": round(st.pstdev(vs), 2)}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events")
    e.add_argument("items")
    e.add_argument("outdir")
    a = sub.add_parser("analyze")
    a.add_argument("items")
    a.add_argument("wavdir")
    a.add_argument("-o", "--out", required=True)
    c = sub.add_parser("clips")
    c.add_argument("levels")
    c.add_argument("wavdir")
    c.add_argument("audiodir")
    c.add_argument("--key", required=True)
    c.add_argument("--seed", type=int)
    args = ap.parse_args(argv)
    with open(CURVES) as f:
        curves = json.load(f)
    if args.cmd == "events":
        with open(args.items) as f:
            items = json.load(f)
        lst = []
        for k, it in items.items():
            notes = plan(it, curves)
            for v in ("off", "on"):
                with open(os.path.join(args.outdir, f"{k}_{v}.txt"), "w", newline="\r\n") as f:
                    f.write(events(it, notes, v == "on"))
                lst.append(f"{k}_{v}|{it['section']}")
            with open(os.path.join(args.outdir, f"{k}.notes.json"), "w") as f:
                json.dump(notes, f, indent=1)
            print(k, it["section"], name(it["pitch"]), "CC1", it["cc"], "cc11", [n["cc11"] for n in notes])
        with open(os.path.join(args.outdir, "list.txt"), "w", newline="\r\n") as f:
            f.write("\n".join(lst) + "\n")
    elif args.cmd == "analyze":
        import soundfile as sf
        with open(args.items) as f:
            items = json.load(f)
        res = {}
        for k, it in items.items():
            notes = plan(it, curves)
            r = {"item": it, "notes": notes}
            for v in ("off", "on"):
                x, sr = sf.read(os.path.join(args.wavdir, f"{k}_{v}.wav"), dtype="float32", always_2d=True)
                lv = [round(level(x, sr, n["on"]), 2) for n in notes]
                r[v] = {"levelsDb": lv, "repeated": spread(lv[:8]),
                        "returns": spread([l for l, n in zip(lv, notes) if n["pitch"] == it["pitch"]]),
                        # the line's notes measured against their own prediction (each pitch's two takes' mean + CC11)
                        "lineVsMean": [round(l - (n["takeA"] + n["takeB"]) / 2, 2) for l, n in zip(lv[8:], notes[8:])]}
            # how the short-note level difference of the takes compares with the sustain's (from OFF's repeats)
            offr = r["off"]["levelsDb"][:8]
            r["shortTakeDiffDb"] = round(st.mean(offr[0::2]) - st.mean(offr[1::2]), 2)
            r["sustainTakeDiffDb"] = round(notes[0]["takeA"] - notes[0]["takeB"], 2)
            res[k] = r
            print(f"{k:8s} {it['section']:6s} {name(it['pitch']):4s} CC1 {it['cc']:3d}  A-B sustain {r['sustainTakeDiffDb']:+.2f}"
                  f" short {r['shortTakeDiffDb']:+.2f}  repeated OFF {r['off']['repeated']} ON {r['on']['repeated']}"
                  f"  returns OFF {r['off']['returns']['maxMinusMin']} ON {r['on']['returns']['maxMinusMin']}")
        with open(args.out, "w") as f:
            json.dump(res, f, indent=1)
    else:
        import numpy as np
        import soundfile as sf
        import pyloudnorm
        with open(args.levels) as f:
            res = json.load(f)
        rng = random.Random(args.seed)
        key = {"what": "blind A/B: SSO All techniques Long, the two alternating round robins as they are (off) or evened out "
                       "by a per-note CC11 predicted from the per-take levels (on); q = 96, 8 repeated quarters then an 8-note line",
               "matching": "each clip gained to -20 LUFS integrated (ITU-R BS.1770-4, pyloudnorm), mp3 192 kb/s, "
                           f"{LEAD} s lead before the first note", "ccBase": BASE,
               "levelWindow": f"per note: mean power of both channels {WINDOW[0]}-{WINDOW[1]} s after the note-on, dB FS, before the gain",
               "items": {}}
        os.makedirs(args.audiodir, exist_ok=True)
        for k, r in res.items():
            it, notes = r["item"], r["notes"]
            first = notes[0]["on"]
            t0 = first - LEAD
            t1 = notes[-1]["on"] + LEN + TAIL
            letters = ["a", "b"]
            rng.shuffle(letters)
            ver = dict(zip(letters, ["off", "on"]))
            ent = {"section": it["section"], "patch": it["section"] + " (All techniques), Long", "pitch": it["pitch"],
                   "pitchName": name(it["pitch"]), "cc1": it["cc"], "line": it["line"],
                   "lineNames": [name(p) for p in it["line"]], "letters": ver,
                   "takeLevelsDb": {"A": notes[0]["takeA"], "B": notes[0]["takeB"]},
                   "noteOnsS": [round(n["on"] - t0, 4) for n in notes],
                   "repeatedS": [round(notes[0]["on"] - t0, 4), round(notes[7]["on"] + LEN - t0, 4)],
                   "lineS": [round(notes[8]["on"] - t0, 4), round(notes[15]["on"] + LEN - t0, 4)],
                   "notes": [{"pitch": n["pitch"], "press": n["press"], "take": n["take"], "cc11On": n["cc11"],
                              "offsetDb": n["offsetDb"]} for n in notes],
                   "shortTakeDiffDb": r["shortTakeDiffDb"], "sustainTakeDiffDb": r["sustainTakeDiffDb"], "clips": {}}
            for letter, v in ver.items():
                x, sr = sf.read(os.path.join(args.wavdir, f"{k}_{v}.wav"), dtype="float64", always_2d=True)
                y = x[int(round(t0 * sr)):int(round(t1 * sr))].copy()
                fade = int(0.3 * sr)
                y[-fade:] *= np.linspace(1, 0, fade)[:, None]
                lufs = pyloudnorm.Meter(sr).integrated_loudness(y)
                g = -20.0 - lufs
                y *= 10 ** (g / 20)
                tmp = os.path.join(args.audiodir, f".{k}_{letter}.wav")
                sf.write(tmp, y.astype("float32"), sr, subtype="FLOAT")
                out = os.path.join(args.audiodir, f"{k}_{letter}.mp3")
                subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", tmp, "-codec:a", "libmp3lame", "-b:a", "192k", out],
                               check=True)
                os.remove(tmp)
                ent["clips"][letter] = {"version": v, "file": f"rr/{k}_{letter}.mp3", "lufsBefore": round(lufs, 2),
                                        "gainDb": round(g, 2), "peakDb": round(20 * math.log10(float(np.abs(y).max())), 2),
                                        "levelsDb": r[v]["levelsDb"], "repeatedSpread": r[v]["repeated"],
                                        "returnsSpread": r[v]["returns"], "lineVsMeanDb": r[v]["lineVsMean"]}
            key["items"][k] = ent
            print(k, ver)
        with open(args.key, "w") as f:
            json.dump(key, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
