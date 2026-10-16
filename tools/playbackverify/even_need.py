#!/usr/bin/env python3
"""Does Even levels still matter? (the owner, 2026-10-16: "do we still need the background level evening now we've done
so much more research???"). The note-to-note level steps inside fast slurs, measured on the real sound, with
[levels] evenLevels off and on, several takes each:

  even_need.py <dump> <out.json> --take off=<wavdir> --take off=<wavdir> ... --take on=<wavdir> ... [--parts a,b]
               [--dump-on <dump>]

<dump>: tst_playbackaudit's MS_AUDIT_DUMP (heard_check.sh step 1) of the score with evenLevels off; --dump-on the same
with it on (the takes "on" are analysed against it; its rows are the same notes, only the CC11 differs). Each <wavdir>:
heard_check_vm.cmd's renders (kthost + Kontakt + the library, each route alone, the setups' own volumes: the gain stage
is a constant per patch and leaves steps within a route unchanged).

Steps: the heard check's LEVEL pairs (heard_check.py (d3): consecutive notes of a line, the second slurred to the first,
no written dynamic between, top notes of chords), on Performance legato routes, in a *fast* passage: the run of joined
notes it belongs to has a written inter-onset interval under EvenLevels::SLOW_IOI_S (350 ms; a passage whose every IOI
is >= 350 ms is skipped by Even levels and isn't counted here). Two level readings per note:
  A  the heard check's: the loudest perceived level (loudness.py's envelope) over the note's heard span (heard_check.py)
  B  independent of pitch tracking: the mean perceived level (dB) over [sent note-on, next sent note-on on the route)
     + the envelope's 30 ms latency, at most the note's written length and 1.5 s (the even-levels job's kind of window: by MuseScore's own times)
B leaves out the step from a slur's attacked first note (its window, from the sent note-on of an early attack, holds the
attack's rise: 15-25 dB lower, not a level). Per section and condition: |step| median / 90th percentile / max over every take's steps, the count above the heard
check's LEVEL limit (0.69 dB, proposed: Jesteadt, Wier & Green 1977) per take (mean), and the *two-pass floor*: for each
step, the difference between two takes of the same condition, / sqrt(2) (one take's step noise), its median / p90.
Also the steps of the takes' mean levels (the repeatable part: noise / sqrt(takes)).
"""
import argparse
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import heard_check as hc  # noqa: E402

SLOW_IOI_S = 0.35
LIMIT_DB = hc.LEVEL_DL_DB


def pairs_of(dump, parts=None):
    """the counted steps: (key, route, part, id_a, id_b) with the rows' ids, and the rows"""
    rows = hc.read_rows(dump)
    if parts:
        rows = [r for r in rows if r["part"] in parts]
    lines = {}
    for r in rows:
        if r["grace"] or r["sub"]:
            continue
        lines.setdefault((r["part"], r["track"]), {}).setdefault(round(r["written"], 6), []).append(r)
    out = []
    for (part, trk), gs in lines.items():
        seq = [gs[t] for t in sorted(gs)]
        tops = [max(g, key=lambda r: r["pitch"]) for g in seq]
        # passages: runs of joined notes (joinedBefore), fast when any written IOI < SLOW_IOI_S
        runs, cur = [], [0]
        for i in range(1, len(seq)):
            if any(r["joinedBefore"] for r in seq[i]):
                cur.append(i)
            else:
                runs.append(cur)
                cur = [i]
        runs.append(cur)
        fast_at = {}
        for run in runs:
            iois = [seq[b][0]["written"] - seq[a][0]["written"] for a, b in zip(run, run[1:])]
            fast = any(x < SLOW_IOI_S for x in iois)
            for i in run:
                fast_at[i] = fast
        for i in range(1, len(seq)):
            a, b = tops[i - 1], tops[i]
            if not b["slurredBefore"] or b["dynamicBefore"] or not b["performance"] or a["route"] != b["route"]:
                continue
            if "Performance" not in b["patch"] or not fast_at.get(i):
                continue
            out.append(dict(key="%s|%d|%.4f|%d" % (part, trk, b["written"], b["pitch"]), route=b["route"], part=part,
                            a=(part, round(a["written"], 4), a["pitch"]), b=(part, round(b["written"], 4), b["pitch"]),
                            bar=b["bar"], ioi=b["written"] - a["written"], aAttack=not a["transition"]))
    return out, rows


def levels_b(dump, wavs, rows):
    """reading B per note (part, written, pitch) -> dB"""
    import soundfile as sf
    from loudness import perceived_envelope
    lead = hc.read_lead_ms(dump)
    by_route = {}
    for r in rows:
        r["onMs"] = (r["written"] + hc.OFFSET_S) * 1000 + lead + r["sentMs"]
        by_route.setdefault(r["route"], []).append(r)
    out = {}
    for route, rr in by_route.items():
        f = os.path.join(wavs, route + ".wav")
        if not os.path.exists(f):
            continue
        x, sr = sf.read(f, always_2d=False)
        env, first, hop = perceived_envelope(x, sr)
        ons = sorted({r["onMs"] for r in rr})
        for r in rr:
            nxt = [t for t in ons if t > r["onMs"] + 0.5]
            # (to the next note-on, at most the note's written length and 1.5 s: never into a rest's silence)
            t1 = min(nxt[0] if nxt else r["onMs"] + 1500, r["onMs"] + 1000 * (r["writtenEnd"] - r["written"]),
                     r["onMs"] + 1500)
            a = max(0, int(round((r["onMs"] + hc.LATENCY_MS - first) / hop)))
            b = min(len(env), max(a + 1, int(round((t1 + hc.LATENCY_MS - first) / hop))))
            if b > a:
                out[(r["part"], round(r["written"], 4), r["pitch"])] = float(np.mean(env[a:b]))
    return out


def levels_a(dump, wavs, parts):
    res = hc.analyse(dump, wavs, parts)
    out = {}
    for n in res["notes"]:
        if n["levelDb"] is not None:
            out[(n["part"], round(n["written"], 4), n["pitch"])] = n["levelDb"]
    return out


def stats(v):
    v = [abs(x) for x in v]
    if not v:
        return None
    return dict(n=len(v), median=round(float(np.median(v)), 2), p90=round(float(np.percentile(v, 90)), 2),
                max=round(float(max(v)), 2))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("out")
    ap.add_argument("--dump-on")
    ap.add_argument("--take", action="append", default=[])
    ap.add_argument("--parts")
    args = ap.parse_args()
    parts = set(args.parts.split(",")) if args.parts else None
    pairs, _ = pairs_of(args.dump, parts)
    if args.dump_on:
        pon, _ = pairs_of(args.dump_on, parts)
        assert [p["key"] for p in pon] == [p["key"] for p in pairs], "the dumps' steps differ"
    takes = {}
    for t in args.take:
        cond, wavs = t.split("=", 1)
        dump = args.dump_on if cond == "on" and args.dump_on else args.dump
        _, rows = pairs_of(dump, parts)
        la = levels_a(dump, wavs, parts)
        lb = levels_b(dump, wavs, rows)
        takes.setdefault(cond, []).append(dict(wavs=os.path.basename(os.path.normpath(wavs)), A=la, B=lb))
        print("analysed", cond, wavs, file=sys.stderr)
    result = dict(limitDb=LIMIT_DB, slowIoiS=SLOW_IOI_S, steps=len(pairs), sections={})
    for part in sorted({p["part"] for p in pairs}):
        pp = [p for p in pairs if p["part"] == part]
        sec = dict(steps=len(pp), medianIoiMs=round(1000 * float(np.median([p["ioi"] for p in pp])), 1))
        for cond, tk in takes.items():
            for reading in ("A", "B"):
                per_take = []
                for t in tk:
                    L = t[reading]
                    per_take.append({p["key"]: L[p["b"]] - L[p["a"]] for p in pp if p["a"] in L and p["b"] in L
                                     and not (reading == "B" and p["aAttack"])})
                allv = [v for d in per_take for v in d.values()]
                common = [k for k in per_take[0] if all(k in d for d in per_take)] if per_take else []
                floor = []
                for i in range(len(per_take)):
                    for j in range(i + 1, len(per_take)):
                        floor += [(per_take[i][k] - per_take[j][k]) / math.sqrt(2) for k in common]
                mean_steps = [float(np.mean([d[k] for d in per_take])) for k in common]
                sec["%s %s" % (cond, reading)] = dict(
                    takes=len(per_take), all=stats(allv),
                    overLimitPerTake=round(float(np.mean([sum(1 for v in d.values() if abs(v) > LIMIT_DB)
                                                          for d in per_take])), 1) if per_take else None,
                    measuredPerTake=round(float(np.mean([len(d) for d in per_take])), 1) if per_take else None,
                    floor=stats(floor), meanOfTakes=stats(mean_steps),
                    meanOverLimit=sum(1 for v in mean_steps if abs(v) > LIMIT_DB))
        result["sections"][part] = sec
    with open(args.out, "w") as f:
        json.dump(result, f, indent=1)
    for part, sec in result["sections"].items():
        print("%s: %d steps (median IOI %.0f ms)" % (part, sec["steps"], sec["medianIoiMs"]))
        for k, v in sec.items():
            if isinstance(v, dict) and v.get("all"):
                print("  %-6s all %s | >%.2f per take %s of %s | floor %s | mean-of-takes %s (>lim %d)" % (
                    k, "%(median).2f/%(p90).2f/%(max).2f" % v["all"], LIMIT_DB, v["overLimitPerTake"], v["measuredPerTake"],
                    "%(median).2f/%(p90).2f" % v["floor"] if v["floor"] else "-",
                    "%(median).2f/%(p90).2f/%(max).2f" % v["meanOfTakes"] if v["meanOfTakes"] else "-", v["meanOverLimit"]))


if __name__ == "__main__":
    main()
