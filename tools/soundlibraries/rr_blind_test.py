#!/usr/bin/env python3
"""Blind-test clips: SSO's two alternating round robins as they are (OFF) against evened out by a per-note CC11 (ON).

SSO's strings play two round robins per key, alternating from a transport start (take A on a key's 1st, 3rd ... press,
B on its 2nd, 4th ...; offline a round robin's takes are identical). ON sets each note's CC11 10 ms before its note-on so
that its take plays at the mean of the two: CC11 = base * 10^(offset / 20) (SSO's CC11 is a plain gain, 20 log10: measured
2026-10-14 with this script, within 0.02 dB). OFF sends the base CC11 throughout.

Where the offsets come from:
  sustain (default): sso_cc1_curves.json's per-take levels (rmsDbTakes, even = A, odd = B; 1.5-3.0 s into 3 s notes).
      Measured 2026-10-14: too weak for these short notes (Basses' two takes have different attack envelopes; the
      takes differ by up to ~5 dB more in the first 0.55 s than in the sustain).
  --from <levels.json> --version off|on: a render's own measured levels (best case): per pitch with both takes in the
      phrase, each take moves to the mean of the two takes' measured means, added to the offsets that render had
      (one refinement pass: --version on). Pitches heard on one take only keep offset 0. If a CC11 would pass 127 the
      item's offsets all move down together (a plain gain: the clips are loudness-matched anyway).

    rr_blind_test.py events <items.json> <outdir> [--from <levels.json> --version off|on]
        kthost event files <item>_off.txt / _on.txt, <item>.notes.json (the ON plan), list.txt
    rr_blind_test.py analyze <items.json> <evdir> <wavdir> -o <levels.json>   per-note levels of both renders, spreads
    rr_blind_test.py clips <levels.json> <wavdir> <audiodir> --key <key.json> [--items a b ...] [--seed N]

items.json: {"<item>": {"section": "Basses", "pitch": 28, "cc": 80, "line": [28, 31, ...], "warm": 48}, ...}
Phrase: 8 quarter notes on the pitch at q = 96 (each 90 % long), a half rest, the 8-note line, all at one CC1
(velocity = CC1, as MuseScore sends a Long). A warm-up note on another key (not in the clip) first, as the sweep did;
render with kthost --probe-pitch <warm> so the readiness probe hits no key of the clip.
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
BASE = 90            # CC11 for every note OFF, the centre ON
WINDOW = (0.10, 0.55)  # per-note level window after the note-on, s (note-off at 0.5625)
NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def name(p):
    return f"{NAMES[p % 12]}{p // 12 - 1}"


def phrase(item):
    """[(on, pitch)] of the 16 notes"""
    p = item["pitch"]
    ons = [(START + i * Q, p) for i in range(8)]
    t = START + 10 * Q  # 8 quarters + half rest
    ons += [(t + i * Q, q) for i, q in enumerate(item["line"])]
    return ons


def sustain_offsets(item, curves):
    out = {}
    for _, p in phrase(item):
        row = next(r for r in curves["sections"][item["section"]]["points"][str(p)] if r["cc"] == item["cc"])
        t = row["rmsDbTakes"]
        a, b = st.mean(t[0::2]), st.mean(t[1::2])
        out[p] = {"A": (a + b) / 2 - a, "B": (a + b) / 2 - b}
    return out


def measured_offsets(rec, version):
    """offsets from a render's measured levels, on top of the offsets it had"""
    notes, lv = rec["notes"], rec[version]["levelsDb"]
    had = rec["notes"] if version == "on" else None
    by = {}
    for n, l in zip(notes, lv):
        prev = n["offsetDb"] if had else 0.0
        by.setdefault(n["pitch"], {}).setdefault(n["take"], []).append((l, prev))
    out = {}
    for p, t in by.items():
        if "A" in t and "B" in t:
            m = {k: st.mean(x[0] for x in v) for k, v in t.items()}
            prev = {k: st.mean(x[1] for x in v) for k, v in t.items()}
            target = (m["A"] + m["B"]) / 2
            out[p] = {k: prev[k] + target - m[k] for k in ("A", "B")}
    return out


def plan(item, offsets):
    """per note: on, pitch, press count on its key, take, offset dB, CC11 ON"""
    count = {}
    out = []
    for on, p in phrase(item):
        k = count.get(p, 0)
        count[p] = k + 1
        take = "A" if k % 2 == 0 else "B"
        out.append({"on": round(on, 4), "pitch": p, "press": k, "take": take,
                    "wantDb": offsets.get(p, {}).get(take, 0.0)})
    top = max(n["wantDb"] for n in out)
    shift = min(0.0, 20 * math.log10(127 / BASE) - top)  # keep the loudest under 127
    for n in out:
        n["offsetDb"] = round(n["wantDb"] + shift, 2)
        n["cc11"] = max(1, min(127, int(round(BASE * 10 ** (n["offsetDb"] / 20)))))
        n["cc11Db"] = round(20 * math.log10(n["cc11"] / BASE), 2)
        n["wantDb"] = round(n["wantDb"], 2)
    return out, round(shift, 2)


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


def level(x, sr, on, w=WINDOW):
    i0, i1 = int(round((on + w[0]) * sr)), int(round((on + w[1]) * sr))
    seg = x[i0:i1].astype("float64")
    return 10 * math.log10(float((seg ** 2).mean()))


def spread(vs):
    return {"maxMinusMin": round(max(vs) - min(vs), 2), "sd": round(st.pstdev(vs), 2)}


def attack(x, sr, notes, pitch):
    """per take on the repeated pitch: mean level 0.15-0.30 s and 0.40-0.55 s after the note-on (dB) and the rise"""
    out = {}
    for t in ("A", "B"):
        e = [level(x, sr, n["on"], (0.15, 0.30)) for n in notes[:8] if n["take"] == t]
        l = [level(x, sr, n["on"], (0.40, 0.55)) for n in notes[:8] if n["take"] == t]
        out[t] = {"early": round(st.mean(e), 2), "late": round(st.mean(l), 2), "riseDb": round(st.mean(l) - st.mean(e), 2)}
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events")
    e.add_argument("items")
    e.add_argument("outdir")
    e.add_argument("--from", dest="src")
    e.add_argument("--version", choices=["off", "on"], default="off")
    a = sub.add_parser("analyze")
    a.add_argument("items")
    a.add_argument("evdir")
    a.add_argument("wavdir")
    a.add_argument("-o", "--out", required=True)
    c = sub.add_parser("clips")
    c.add_argument("levels")
    c.add_argument("wavdir")
    c.add_argument("audiodir")
    c.add_argument("--key", required=True)
    c.add_argument("--items", nargs="*")
    c.add_argument("--seed", type=int)
    args = ap.parse_args(argv)
    if args.cmd == "events":
        with open(args.items) as f:
            items = json.load(f)
        src = None
        if args.src:
            with open(args.src) as f:
                src = json.load(f)
        else:
            with open(CURVES) as f:
                curves = json.load(f)
        lst = []
        for k, it in items.items():
            offs = measured_offsets(src[k], args.version) if src else sustain_offsets(it, curves)
            notes, shift = plan(it, offs)
            for v in ("off", "on"):
                with open(os.path.join(args.outdir, f"{k}_{v}.txt"), "w", newline="\r\n") as f:
                    f.write(events(it, notes, v == "on"))
                lst.append(f"{k}_{v}|{it['section']}|{it['warm']}")
            with open(os.path.join(args.outdir, f"{k}.notes.json"), "w") as f:
                json.dump({"shiftDb": shift, "source": f"{args.src}:{args.version}" if src else "sustain", "notes": notes}, f, indent=1)
            print(k, it["section"], name(it["pitch"]), "CC1", it["cc"], "shift", shift, "cc11", [n["cc11"] for n in notes])
        with open(os.path.join(args.outdir, "list.txt"), "w", newline="\r\n") as f:
            f.write("\n".join(lst) + "\n")
    elif args.cmd == "analyze":
        import soundfile as sf
        with open(args.items) as f:
            items = json.load(f)
        res = {}
        for k, it in items.items():
            with open(os.path.join(args.evdir, f"{k}.notes.json")) as f:
                pl = json.load(f)
            notes = pl["notes"]
            r = {"item": it, "notes": notes, "shiftDb": pl["shiftDb"], "source": pl["source"]}
            for v in ("off", "on"):
                x, sr = sf.read(os.path.join(args.wavdir, f"{k}_{v}.wav"), dtype="float32", always_2d=True)
                lv = [round(level(x, sr, n["on"]), 2) for n in notes]
                ret = [l for l, n in zip(lv, notes) if n["pitch"] == it["pitch"]]
                r[v] = {"levelsDb": lv, "repeated": spread(lv[:8]), "returns": spread(ret),
                        "takeDiffDb": round(st.mean(lv[:8][0::2]) - st.mean(lv[:8][1::2]), 2),
                        "attack": attack(x, sr, notes, it["pitch"])}
            r["halved"] = r["on"]["repeated"]["maxMinusMin"] <= r["off"]["repeated"]["maxMinusMin"] / 2
            res[k] = r
            print(f"{k:8s} {it['section']:6s} {name(it['pitch']):4s} CC1 {it['cc']:3d}  A-B OFF {r['off']['takeDiffDb']:+.2f}"
                  f" ON {r['on']['takeDiffDb']:+.2f}  repeated OFF {r['off']['repeated']} ON {r['on']['repeated']}"
                  f"  returns OFF {r['off']['returns']['maxMinusMin']} ON {r['on']['returns']['maxMinusMin']}"
                  f"  halved {r['halved']}")
        with open(args.out, "w") as f:
            json.dump(res, f, indent=1)
    else:
        import numpy as np
        import soundfile as sf
        import pyloudnorm
        with open(args.levels) as f:
            res = json.load(f)
        rng = random.Random(args.seed)
        key = {"what": "blind A/B: SSO All techniques Long (Celli / Basses), the two alternating round robins as they are "
                       "(off) or evened out by a per-note CC11 (on, best case: offsets from the clip's own measured per-take "
                       "levels, refined once); q = 96, 8 repeated quarters (90 % long), a half rest, an 8-note line",
               "rendered": "2026-10-14, Windows test VM, Kontakt Player 8 + SSO, kthost offline (probe on the warm-up key), "
                           "MuseScore's unedited setups",
               "matching": "each clip gained to -20 LUFS integrated (ITU-R BS.1770-4, pyloudnorm), mp3 192 kb/s, "
                           f"{LEAD} s lead before the first note", "ccBase": BASE, "cc11Law": "dB = 20 log10(cc11 / 90)",
               "levelWindow": f"per note: mean power of both channels {WINDOW[0]}-{WINDOW[1]} s after the note-on, dB FS, before the gain",
               "attack": "per take on the repeated pitch, OFF: mean level 0.15-0.30 s and 0.40-0.55 s after the note-on; "
                         "the takes' attack shapes differ and ON leaves them as they are (one gain per note)",
               "items": {}}
        os.makedirs(args.audiodir, exist_ok=True)
        for k, r in res.items():
            if args.items and k not in args.items:
                continue
            it, notes = r["item"], r["notes"]
            first = notes[0]["on"]
            t0 = first - LEAD
            t1 = notes[-1]["on"] + LEN + TAIL
            letters = ["a", "b"]
            rng.shuffle(letters)
            ver = dict(zip(letters, ["off", "on"]))
            ent = {"section": it["section"], "patch": it["section"] + " (All techniques), Long (UACC 1)", "pitch": it["pitch"],
                   "pitchName": name(it["pitch"]), "cc1": it["cc"], "velocity": it["cc"], "line": it["line"],
                   "lineNames": [name(p) for p in it["line"]], "letters": ver,
                   "takeLevelsOffDb": {t: round(st.mean(l for l, n in zip(r["off"]["levelsDb"][:8], notes[:8]) if n["take"] == t), 2)
                                       for t in ("A", "B")},
                   "attackOff": r["off"]["attack"], "attackOn": r["on"]["attack"],
                   "noteOnsS": [round(n["on"] - t0, 4) for n in notes],
                   "repeatedS": [round(notes[0]["on"] - t0, 4), round(notes[7]["on"] + LEN - t0, 4)],
                   "lineS": [round(notes[8]["on"] - t0, 4), round(notes[15]["on"] + LEN - t0, 4)],
                   "onShiftDb": r["shiftDb"], "offsetSource": r["source"],
                   "notes": [{"pitch": n["pitch"], "name": name(n["pitch"]), "press": n["press"], "take": n["take"],
                              "cc11On": n["cc11"], "offsetDb": n["offsetDb"]} for n in notes],
                   "clips": {}}
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
                                        "returnsSpread": r[v]["returns"], "takeDiffDb": r[v]["takeDiffDb"]}
            key["items"][k] = ent
            print(k, ver)
        with open(args.key, "w") as f:
            json.dump(key, f, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
