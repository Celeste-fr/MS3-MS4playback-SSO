#!/usr/bin/env python3
"""Stage B of "Dynamics like a real player" (HANDOFF.md): which CC1 each written marking would need for SSO's
brightness change to match the real player's, per string section and measure.

    python3 brightness_plan.py merge-sso <part.json> ... --method <method.json> -o sso_brightness.json
        part files from `sso_cc1_sweep.py analyze --brightness --name "<section>|<technique>"`
    python3 brightness_plan.py plan real_brightness.json sso_brightness.json      (writes "plan" into sso_brightness.json)

The rule (an algorithm, not a measurement; HANDOFF.md › Dynamics like a real player › Stage B):
- CHANGES only: TU Berlin is one player, anechoic, 32 microphones around him; SSO a section in a hall through its mics.
  So the real side gives a ratio per pitch, R(p) = B_ff / B_pp for a measure B, and SSO a ratio against its own mp,
  q(p, cc) = B(p, cc) / B(p, 64) (relative64; mp is CC1 64, where the section balance M was set and stage A's realDb is 0).
- Rule D (the owner, 2026-10-12): TU's pp / ff are the player's ppp / fff, the markings between in 7 equal steps. Here
  in brightness: equal steps of log B, so marking i (ppp 0 … fff 7, mp 3) has the real brightness B_pp * R^(i / 7), and
  against mp R^((i - 3) / 7). (Only pp and ff are measured: whether brightness grows evenly in log B with the
  markings is not known; it is the same reading of "equal steps" as stage A's in dB.)
- R(p): the median of log R over the TU notes within +-2 semitones of p that have both pp and ff, each at least 3 dB
  over its noise (a 5-semitone running median against one player's note-to-note scatter).
- SSO: log q(cc) made non-decreasing in CC1 (pool-adjacent-violators, weights equal) and inverted by straight lines
  between the measured CC1 values; a target beyond q(16) or q(127) is out of reach: the end value, flagged.
- Per section and marking: the median over pitches of the CC1 found (and the pitches out of reach).
- Loudness after: CC11 would then carry the whole real change: stage A's realDb = steps x span / 7 - long(CC1, p), now
  with SSO's Long level at the NEW CC1 (rmsDb from the same sweep, relative to CC1 64): reported as longDb per marking.
"""
import argparse
import json
import math
import os
import statistics
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MARKINGS = ["ppp", "pp", "p", "mp", "mf", "f", "ff", "fff"]
TODAY = [16, 32, 48, 64, 80, 96, 112, 127]       # MuseScore's CC1 for ppp … fff today (Ms4::expressionLevel)
MEASURES = ["sharp", "hcPow", "hcAmp"]          # brightness measures planned (scX / hcX give the same ratios)
INSTRUMENT = {"Violins 1": "Violin", "Violins 2": "Violin", "Violas": "Viola", "Celli": "Cello", "Basses": "Double_bass"}
HALF = 2                                          # semitones each side for the real running median
MIN_SNR = 3.0     # dB: a TU note counts only when its steady part is 3 dB over its noise (the noise gate's 3 dB);
                  # one file (Violin ff B5) reads -1 dB: its steady part is quieter than the rest of the file


def real_ratio(real, inst, measure, p):
    notes = real["instruments"][inst]["notes"]
    v = []
    for q in range(p - HALF, p + HALF + 1):
        n = notes.get(str(q))
        if n and "pp" in n and "ff" in n and n["pp"].get(measure, 0) > 0 and n["ff"].get(measure, 0) > 0 \
                and min(n["pp"].get("snrDb", 99), n["ff"].get("snrDb", 99)) >= MIN_SNR:
            v.append(math.log(n["ff"][measure] / n["pp"][measure]))
    return (statistics.median(v), len(v)) if v else (None, 0)


def pav(y):
    """pool-adjacent-violators: the non-decreasing least-squares fit of y (equal weights)"""
    blocks = [[v, 1] for v in y]
    i = 0
    while i < len(blocks) - 1:
        if blocks[i][0] > blocks[i + 1][0] + 1e-12:
            v = (blocks[i][0] * blocks[i][1] + blocks[i + 1][0] * blocks[i + 1][1]) / (blocks[i][1] + blocks[i + 1][1])
            blocks[i] = [v, blocks[i][1] + blocks[i + 1][1]]
            del blocks[i + 1]
            i = max(i - 1, 0)
        else:
            i += 1
    out = []
    for v, n in blocks:
        out += [v] * n
    return out


def invert(ccs, logq, target):
    """the CC1 where the monotone fit of log q reaches target; (cc, flag) flag -1 / +1 when out of reach"""
    fit = pav(logq)
    if target <= fit[0]:
        return float(ccs[0]), (-1 if target < fit[0] - 1e-9 else 0)
    if target >= fit[-1]:
        return float(ccs[-1]), (1 if target > fit[-1] + 1e-9 else 0)
    for i in range(len(ccs) - 1):
        a, b = fit[i], fit[i + 1]
        if a <= target <= b:
            if b - a < 1e-12:
                return float(ccs[i]), 0
            return ccs[i] + (target - a) / (b - a) * (ccs[i + 1] - ccs[i]), 0
    return float(ccs[-1]), 0


def section_plan(real, part, section, measure):
    inst = INSTRUMENT[section]
    rows = []
    for p, pts in part["points"].items():
        p = int(p)
        by = {r["cc"]: r for r in pts}
        if 64 not in by or measure not in by[64] or by[64][measure] <= 0:
            continue
        ccs = sorted(c for c in by if by[c].get(measure, 0) > 0)
        logq = [math.log(by[c][measure] / by[64][measure]) for c in ccs]
        lr, n = real_ratio(real, inst, measure, p)
        if lr is None or len(ccs) < 2:
            continue
        cc_i, flags = [], []
        for i in range(8):
            if i == 3:                  # mp is the anchor: CC1 64 by definition (the fit may pool 64 with a neighbour)
                cc_i.append(64.0)
                flags.append(0)
                continue
            c, fl = invert(ccs, logq, lr * (i - 3) / 7)
            cc_i.append(round(c, 1))
            flags.append(fl)
        rows.append({"pitch": p, "realRatio": round(math.exp(lr), 4), "realNotes": n,
                     "ssoRatio16": round(math.exp(logq[0]), 4), "ssoRatio127": round(math.exp(logq[-1]), 4),
                     "cc": cc_i, "outOfReach": flags})
    if not rows:
        return None
    med = [round(statistics.median(r["cc"][i] for r in rows)) for i in range(8)]
    reach = {"below16": [sum(1 for r in rows if r["outOfReach"][i] < 0) for i in range(8)],
             "above127": [sum(1 for r in rows if r["outOfReach"][i] > 0) for i in range(8)]}
    real_all = statistics.median(r["realRatio"] for r in rows)
    sso_span = statistics.median(r["ssoRatio127"] / r["ssoRatio16"] for r in rows)
    # coverage: how much of the real player's pp -> ff change (in log B) SSO's CC1 16 -> 127 gives, median over pitches
    cov = statistics.median(math.log(r["ssoRatio127"] / r["ssoRatio16"]) / math.log(r["realRatio"])
                            for r in rows if r["realRatio"] != 1)
    return {"cc": med, "pitches": len(rows), "reach": reach, "realRatioMedian": round(real_all, 4),
            "ssoRatio127over16Median": round(sso_span, 4), "coverage": round(cov, 3), "perPitch": rows}


def long_db(long_part, cc_values):
    """median over pitches of SSO's Long rmsDb at each CC1 (relative to 64), by straight lines between measured CC1"""
    out = []
    for c in cc_values:
        v = []
        for p, pts in long_part["points"].items():
            by = {r["cc"]: r["rmsDb"] for r in pts}
            if 64 not in by:
                continue
            ks = sorted(by)
            for a, b in zip(ks, ks[1:]):
                if a <= c <= b:
                    v.append(by[a] + (c - a) / (b - a) * (by[b] - by[a]) - by[64])
                    break
        out.append(round(statistics.median(v), 2) if v else None)
    return out


def spearman(a, b):
    def ranks(x):
        o = sorted(range(len(x)), key=lambda i: x[i])
        r = [0.0] * len(x)
        i = 0
        while i < len(o):
            j = i
            while j + 1 < len(o) and x[o[j + 1]] == x[o[i]]:
                j += 1
            for k in range(i, j + 1):
                r[o[k]] = (i + j) / 2
            i = j + 1
        return r
    ra, rb = ranks(a), ranks(b)
    ma, mb = statistics.mean(ra), statistics.mean(rb)
    num = sum((x - ma) * (y - mb) for x, y in zip(ra, rb))
    den = math.sqrt(sum((x - ma) ** 2 for x in ra) * sum((y - mb) ** 2 for y in rb))
    return num / den if den else float("nan")


def agreement_sso(part, ms=("sharp", "hcPow", "hcAmp", "sone")):
    """Spearman correlation between the measures' log changes against CC1 64, over every pitch x CC1 point"""
    vals = {m: [] for m in ms}
    for p, pts in part["points"].items():
        by = {r["cc"]: r for r in pts}
        if 64 not in by:
            continue
        for c, r in by.items():
            if c == 64 or any(r.get(m, 0) <= 0 or by[64].get(m, 0) <= 0 for m in ms):
                continue
            for m in ms:
                vals[m].append(math.log(r[m] / by[64][m]))
    out = {}
    for i, a in enumerate(ms):
        for b in ms[i + 1:]:
            out[f"{a}~{b}"] = round(spearman(vals[a], vals[b]), 3) if len(vals[a]) > 3 else None
    return out


def agreement_real(real, inst, ms=("sharp", "hcPow", "hcAmp", "sone")):
    vals = {m: [] for m in ms}
    for p, n in real["instruments"][inst]["notes"].items():
        if "pp" in n and "ff" in n and all(n["pp"].get(m, 0) > 0 and n["ff"].get(m, 0) > 0 for m in ms):
            for m in ms:
                vals[m].append(math.log(n["ff"][m] / n["pp"][m]))
    out = {}
    for i, a in enumerate(ms):
        for b in ms[i + 1:]:
            out[f"{a}~{b}"] = round(spearman(vals[a], vals[b]), 3) if len(vals[a]) > 3 else None
    return out


def plan(real_path, sso_path):
    with open(real_path) as f:
        real = json.load(f)
    with open(sso_path) as f:
        sso = json.load(f)
    res = {"method": __doc__, "markings": MARKINGS, "todayCC1": TODAY, "sections": {}, "agreement": {}}
    for section, techs in sso["sections"].items():
        sec = {}
        for tech, part in techs.items():
            t = {}
            for m in MEASURES:
                sp = section_plan(real, part, section, m)
                if sp:
                    t[m] = sp
            if "Long" in techs and t:
                for m in t:
                    t[m]["longDbAtCC1"] = long_db(techs["Long"], t[m]["cc"])
                t["longDbToday"] = long_db(techs["Long"], TODAY)
            sec[tech] = t
            res["agreement"][f"{section}|{tech}"] = agreement_sso(part)
        res["sections"][section] = sec
    for inst in real["instruments"]:
        res["agreement"][f"real {inst}"] = agreement_real(real, inst)
    sso["plan"] = res
    with open(sso_path, "w") as f:
        f.write(dump(sso) + "\n")
    for section, sec in res["sections"].items():
        for tech, t in sec.items():
            for m in MEASURES:
                if m in t:
                    print(f"{section:10s} {tech:20s} {m:6s} CC1 {t[m]['cc']} cover {t[m]['coverage']:.2f} real ff/pp {t[m]['realRatioMedian']}"
                          f"  SSO 127/16 {t[m]['ssoRatio127over16Median']}  >127 {t[m]['reach']['above127']}"
                          f"  <16 {t[m]['reach']['below16']}")


def merge_sso(parts, method, out):
    res = {"method": {}, "sections": {}}
    if method:
        with open(method) as f:
            res["method"] = json.load(f)
    for p in parts:
        with open(p) as f:
            d = json.load(f)
        section, tech = d.pop("name").split("|")
        d.pop("relative64", None)
        res["sections"].setdefault(section, {})[tech] = d
        with open(p) as f:
            res["sections"][section][tech]["relative64"] = json.load(f)["relative64"]
    with open(out, "w") as f:
        f.write(dump(res) + "\n")


def dump(obj, indent=0):
    pad = " " * indent
    if isinstance(obj, dict) and obj and not all(isinstance(v, (int, float, str, type(None))) for v in obj.values()):
        items = [f'{pad} {json.dumps(k)}: {dump(v, indent + 1).lstrip()}' for k, v in obj.items()]
        return pad + "{\n" + ",\n".join(items) + "\n" + pad + "}"
    if isinstance(obj, list) and obj and isinstance(obj[0], dict):
        return pad + "[\n" + ",\n".join(dump(v, indent + 1) for v in obj) + "\n" + pad + "]"
    return pad + json.dumps(obj)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("merge-sso")
    m.add_argument("parts", nargs="+")
    m.add_argument("--method")
    m.add_argument("-o", "--out", required=True)
    p = sub.add_parser("plan")
    p.add_argument("real")
    p.add_argument("sso")
    a = ap.parse_args(argv)
    if a.cmd == "merge-sso":
        merge_sso(a.parts, a.method, a.out)
    else:
        plan(a.real, a.sso)


if __name__ == "__main__":
    main()
