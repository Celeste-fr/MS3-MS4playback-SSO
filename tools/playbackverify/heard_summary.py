#!/usr/bin/env python3
"""heard_summary.py: whole-score summary, regression and drift report of heard-check runs (heard_check.sh's report.json).

  heard_summary.py [label=]report.json ... [--baseline [label=]report.json ...] [--by section,kind] [--held 0.4]
                   [--worst 20] -o <dir>

Each argument is one run (one take); a label defaults to the report's folder name. Next to a report.json it reads, when
there: report.txt (the title: score name and settings), settings.txt, dump/predicted.tsv (each transition's note before,
for "transition after held"). Writes <dir>/summary.md and <dir>/summary.json:

  counts       per run ARRIVAL / RHYTHM (slurs, runs) / LEVEL / UNHEARD fail and warn, notes measured, drifting cells;
               with --baseline the difference from the baseline runs' median
  readings     every transition is shown two ways: "arrival" (f0 within 30 cents of the new pitch, the heard check's
               default judgement) and "leave" (the f0 leaving the note before + 23.5 ms, Polfreman 2013; the owner's ears
               2026-10-21 suggest the ear places a legato transition near the leave). Attacks are the same in both. A
               report made before the leave reading existed (no leaveMs) has arrival only; its judgement is the report's
               own (transitionRule)
  spread       per written time with notes in >= 2 sections: max - min of heard - written (median, p90, max) and the share
               of notes within +-30 ms of their beat's median, per reading
  medians      heard - written per --by keys (section, kind, tempo, bar, patch, situation; default section,kind):
               median [p10 .. p90] n, per run and reading
  drift        heard - predicted (the audit's model) per patch x situation, per run and reading: n, median, p10-p90;
               DRIFT when |median| > 20 ms, SIGN when the median's sign differs between scores
  takes        runs of one score and settings: per note the range over the takes; per patch x kind the median range and
               the largest, n notes heard in >= 2 takes
  worst        the --worst notes by |heard - written| (arrival reading), bar / beat / part / kind

kinds: Performance patches: "first after silence", "attack joined", "staccato", "transition after attack", "transition
after held" (the note before at least --held s long as written, from predicted.tsv), "transition after transition";
All techniques patches: "staccato" or "other attack". tempo: per bar, from the notes' written times and beats.
Exit 0; 2 on bad input.
"""
import argparse
import collections
import csv
import json
import math
import os
import re
import sys

LEAVE_HEARD_MS = 23.5
DRIFT_MS = 20.0
WITHIN_MS = 30.0
KIND_ORDER = ("first after silence", "attack joined", "staccato", "transition after attack", "transition after held",
              "transition after transition", "other attack")
READINGS = ("arrival", "leave")


def q(a, f):
    a = sorted(a)
    if not a:
        return None
    k = (len(a) - 1) * f
    i = int(math.floor(k))
    r = k - i
    return a[i] + (a[min(i + 1, len(a) - 1)] - a[i]) * r


def med(a):
    return q(a, 0.5)


# ---------------------------------------------------------------------------------------------------------------- input

class Run:
    def __init__(self, path, label=None):
        self.path = path
        self.dir = os.path.dirname(os.path.abspath(path))
        self.label = label or os.path.basename(self.dir) or path
        with open(path, encoding="utf-8") as f:
            self.res = json.load(f)
        self.notes = self.res.get("notes", [])
        self.rule = self.res.get("transitionRule", "arrival")
        title = ""
        rt = os.path.join(self.dir, "report.txt")
        if os.path.exists(rt):
            with open(rt, encoding="utf-8") as f:
                title = f.readline().strip()
        m = re.match(r"Heard check of (.+?) \((.*)\)$", title)
        self.score = m.group(1) if m else "?"
        st = os.path.join(self.dir, "settings.txt")
        if os.path.exists(st):
            with open(st, encoding="utf-8") as f:
                self.settings = f.read().strip()
        else:
            self.settings = m.group(2).split("; ")[0] if m else "?"
        self.pred = {}
        pt = os.path.join(self.dir, "dump", "predicted.tsv")
        if os.path.exists(pt):
            with open(pt, encoding="utf-8") as f:
                for r in csv.DictReader(f, delimiter="\t"):
                    self.pred[(r["part"], round(float(r["written"]), 3), int(r["pitch"]))] = r

    def note_key(self, n):
        return (n["part"], round(n["written"], 3), n["pitch"])


def kind_of(run, n, held_s):
    perf = n["patch"].endswith("Performance")
    sit = n.get("situation")
    if not perf:
        return "staccato" if sit == "staccato" else "other attack"
    if n["transition"]:
        if sit == "transition after transition":
            return sit
        p = run.pred.get(run.note_key(n))
        if p is not None:
            src = run.pred.get((n["part"], round(float(p["fromWritten"]), 3), int(p["fromPitch"])))
            if src is not None and float(src["writtenEnd"]) - float(src["written"]) >= held_s - 1e-6:
                return "transition after held"
            if src is not None and src.get("transition") == "1":
                return "transition after transition"
        return "transition after attack"
    if sit in ("first after silence", "attack joined", "staccato"):
        return sit
    return "attack joined"


def value(n, reading):
    """heard - written (ms) by a reading; None if not read"""
    if not n["transition"]:
        return n.get("heardMs")
    if reading == "arrival":
        if "arriveMs" in n:
            return n["arriveMs"]
        return n.get("heardMs")                     # (an older report: heardMs is the arrival)
    lv = n.get("leaveMs")
    return None if lv is None else lv + LEAVE_HEARD_MS


def bar_tempi(notes):
    """bpm per bar from (beat, written) pairs of its notes (the beat's own unit)"""
    by = collections.defaultdict(dict)
    for n in notes:
        by[n["bar"]][round(n["beat"], 4)] = n["written"]
    out = {}
    for bar, d in by.items():
        b = sorted(d)
        ls = [(d[y] - d[x]) / (y - x) for x, y in zip(b, b[1:]) if y - x > 1e-6]
        if ls:
            m = med(ls)
            if m and m > 0:
                out[bar] = int(round(60.0 / m))
    return out


# ------------------------------------------------------------------------------------------------------------- analysis

def enrich(run, held_s):
    tempi = bar_tempi(run.notes)
    for n in run.notes:
        n["_kind"] = kind_of(run, n, held_s)
        n["_tempo"] = tempi.get(n["bar"])
        n["_v"] = {r: value(n, r) for r in READINGS}


def key_of(n, by):
    out = []
    for k in by:
        out.append({"section": n["part"], "part": n["part"], "kind": n["_kind"], "tempo": n["_tempo"], "bar": n["bar"],
                    "patch": n["patch"], "situation": n.get("situation")}[k])
    return tuple(out)


def counts(run):
    f = run.res.get("findings", [])
    c = collections.OrderedDict()
    for k, sub in (("ARRIVAL", None), ("RHYTHM", "slur"), ("RHYTHM", "run"), ("LEVEL", None), ("UNHEARD", None)):
        name = k + (" " + sub + "s" if sub else "")
        sel = [x for x in f if x["kind"] == k and (sub is None or x.get("group") == sub)]
        c[name + " fail"] = sum(1 for x in sel if x["severity"] == "FAIL")
        c[name + " warn"] = sum(1 for x in sel if x["severity"] == "WARN")
    c["notes"] = len(run.notes)
    c["measured"] = sum(1 for n in run.notes if n.get("heardMs") is not None)
    c["transitions leave read"] = sum(1 for n in run.notes if n["transition"] and n.get("leaveMs") is not None)
    c["drifting cells"] = sum(1 for d in run.res.get("drift", []) if d.get("drifting"))
    return c


def spread(run, reading):
    byt = collections.defaultdict(list)
    for n in run.notes:
        v = n["_v"][reading]
        if v is not None:
            byt[round(n["written"], 3)].append((n, v))
    sp, within, total = [], 0, 0
    for t, g in byt.items():
        if len({n["part"] for n, _ in g}) < 2:
            continue
        vals = [v for _, v in g]
        sp.append(max(vals) - min(vals))
        m = med(vals)
        for v in vals:
            total += 1
            within += abs(v - m) <= WITHIN_MS
    if not sp:
        return None
    return dict(beats=len(sp), medianMs=round(med(sp), 1), p90Ms=round(q(sp, 0.9), 1), maxMs=round(max(sp), 1),
                withinShare=round(within / total, 3) if total else None)


def stats(v):
    return dict(n=len(v), medianMs=round(med(v), 1), p10Ms=round(q(v, 0.1), 1), p90Ms=round(q(v, 0.9), 1)) if v else None


def medians(run, by, reading):
    g = collections.defaultdict(list)
    for n in run.notes:
        v = n["_v"][reading]
        if v is not None:
            g[key_of(n, by)].append(v)
    return {k: stats(v) for k, v in g.items()}


def drift(run, reading):
    g = collections.defaultdict(list)
    for n in run.notes:
        v = n["_v"][reading]
        if v is None or n.get("predictedMs") is None:
            continue
        g[(n["patch"], n["_kind"])].append(v - n["predictedMs"])
    out = {}
    for k, v in g.items():
        s = stats(v)
        s["drifting"] = abs(s["medianMs"]) > DRIFT_MS
        out[k] = s
    return out


def take_spread(runs, reading):
    """per group of runs (score, settings) with >= 2 runs: per patch x kind the per-note range over the takes"""
    groups = collections.OrderedDict()
    for r in runs:
        groups.setdefault((r.score, r.settings), []).append(r)
    out = []
    for (score, settings), rs in groups.items():
        if len(rs) < 2:
            continue
        per = collections.defaultdict(list)
        meta = {}
        for r in rs:
            for n in r.notes:
                v = n["_v"][reading]
                if v is None:
                    continue
                k = (n["part"], n["bar"], round(n["beat"], 3), n["pitch"])
                per[k].append(v)
                meta[k] = (n["patch"], n["_kind"])
        cells = collections.defaultdict(list)
        for k, v in per.items():
            if len(v) >= 2:
                cells[meta[k]].append(max(v) - min(v))
        out.append(dict(score=score, takes=[r.label for r in rs], cells={
            "%s / %s" % k: dict(n=len(v), medianRangeMs=round(med(v), 1), maxRangeMs=round(max(v), 1))
            for k, v in sorted(cells.items())}))
    return out


def summarise(runs, baseline=(), by=("section", "kind"), held_s=0.4, worst=20):
    for r in list(runs) + list(baseline):
        enrich(r, held_s)
    res = dict(about="heard_summary.py: see its --help", by=list(by), heldS=held_s, leaveHeardMs=LEAVE_HEARD_MS,
               runs=[], baseline=[r.label for r in baseline])
    base_counts = [counts(r) for r in baseline]
    for r in runs:
        c = counts(r)
        d = None
        if base_counts:
            d = {k: v - med([b[k] for b in base_counts]) for k, v in c.items()}
        res["runs"].append(dict(
            label=r.label, path=r.path, score=r.score, settings=r.settings, transitionRule=r.rule, counts=c, delta=d,
            spread={rd: spread(r, rd) for rd in READINGS},
            medians={rd: {" | ".join(str(x) for x in k): v for k, v in sorted(medians(r, by, rd).items(), key=lambda kv: [str(x) for x in kv[0]])}
                     for rd in READINGS},
            drift={rd: {"%s / %s" % k: v for k, v in sorted(drift(r, rd).items())} for rd in READINGS}))
    # (a cell's median sign between scores)
    signs = collections.defaultdict(lambda: collections.defaultdict(set))
    for r, out in zip(runs, res["runs"]):
        for rd in READINGS:
            for k, v in out["drift"][rd].items():
                if abs(v["medianMs"]) > DRIFT_MS:
                    signs[rd][k].add((r.score, v["medianMs"] > 0))
    res["signDiffers"] = {rd: sorted(k for k, s in d.items() if len({x[1] for x in s}) > 1 and len({x[0] for x in s}) > 1)
                          for rd, d in signs.items()}
    res["takes"] = {rd: take_spread(runs, rd) for rd in READINGS}
    allnotes = [(r.label, n) for r in runs for n in r.notes if n["_v"]["arrival"] is not None]
    allnotes.sort(key=lambda x: -abs(x[1]["_v"]["arrival"]))
    res["worst"] = [dict(run=lab, part=n["part"], bar=n["bar"], beat=n["beat"], pitch=n["pitch"], kind=n["_kind"],
                         patch=n["patch"], heardMinusWrittenMs=n["_v"]["arrival"], leavePlus23Ms=n["_v"]["leave"],
                         predictedMs=n.get("predictedMs"), how=n.get("how")) for lab, n in allnotes[:worst]]
    return res


# ------------------------------------------------------------------------------------------------------------- markdown

def fmt(s):
    if not s:
        return "-"
    return "%+.0f [%+.0f..%+.0f] n%d" % (s["medianMs"], s["p10Ms"], s["p90Ms"], s["n"])


def markdown(res):
    o = ["# Heard-check summary", ""]
    o.append("Runs: " + ", ".join("%s (%s; rule %s)" % (r["label"], r["score"], r["transitionRule"]) for r in res["runs"]))
    if res["baseline"]:
        o.append("Baseline: " + ", ".join(res["baseline"]))
    o += ["", "Transitions in two readings: **arrival** (f0 within 30 cents) and **leave** (f0 leaves the note before, "
          "+%.1f ms). Attacks are the same in both." % res["leaveHeardMs"], "", "## Counts", ""]
    keys = list(res["runs"][0]["counts"]) if res["runs"] else []
    o.append("| run | " + " | ".join(keys) + " |")
    o.append("|---" * (len(keys) + 1) + "|")
    for r in res["runs"]:
        cells = []
        for k in keys:
            v = r["counts"][k]
            d = r["delta"][k] if r["delta"] else None
            cells.append("%d%s" % (v, "" if d is None else " (%+g)" % d))
        o.append("| %s | %s |" % (r["label"], " | ".join(cells)))
    o += ["", "## Beat spread across sections (ms)", "", "| run | reading | beats | median | p90 | max | within ±30 ms |",
          "|---|---|---|---|---|---|---|"]
    for r in res["runs"]:
        for rd in READINGS:
            s = r["spread"][rd]
            if s:
                o.append("| %s | %s | %d | %.0f | %.0f | %.0f | %s |" % (r["label"], rd, s["beats"], s["medianMs"], s["p90Ms"],
                                                                       s["maxMs"], "%.0f %%" % (100 * s["withinShare"])))
    o += ["", "## Heard − written by %s (ms: median [p10..p90] n)" % ", ".join(res["by"]), ""]
    for r in res["runs"]:
        o += ["### " + r["label"], "", "| %s | arrival | leave + %.1f |" % (" | ".join(res["by"]), res["leaveHeardMs"]),
              "|---" * (len(res["by"]) + 2) + "|"]
        ks = list(r["medians"]["arrival"]) + [k for k in r["medians"]["leave"] if k not in r["medians"]["arrival"]]
        for k in ks:
            o.append("| %s | %s | %s |" % (k, fmt(r["medians"]["arrival"].get(k)), fmt(r["medians"]["leave"].get(k))))
        o.append("")
    o += ["## Model drift: heard − predicted per patch × kind (ms)", ""]
    for r in res["runs"]:
        o += ["### " + r["label"], "", "| patch / kind | arrival | leave + %.1f |" % res["leaveHeardMs"], "|---|---|---|"]
        ks = list(r["drift"]["arrival"]) + [k for k in r["drift"]["leave"] if k not in r["drift"]["arrival"]]
        for k in ks:
            a, lv = r["drift"]["arrival"].get(k), r["drift"]["leave"].get(k)
            o.append("| %s | %s%s | %s%s |" % (k, fmt(a), " DRIFT" if a and a["drifting"] else "", fmt(lv),
                                               " DRIFT" if lv and lv["drifting"] else ""))
        o.append("")
    for rd in READINGS:
        if res["signDiffers"].get(rd):
            o.append("SIGN (%s): the drift's sign differs between scores: %s" % (rd, "; ".join(res["signDiffers"][rd])))
    for rd in READINGS:
        for t in res["takes"][rd]:
            o += ["", "## Take-to-take range, %s, %s (%s)" % (rd, t["score"], ", ".join(t["takes"])), "",
                  "| patch / kind | notes | median range | max range |", "|---|---|---|---|"]
            for k, v in t["cells"].items():
                o.append("| %s | %d | %.0f | %.0f |" % (k, v["n"], v["medianRangeMs"], v["maxRangeMs"]))
    o += ["", "## Worst notes (|heard − written|, arrival reading)", "",
          "| run | part | bar | beat | pitch | kind | heard − written | leave + 23.5 | predicted | how |",
          "|---|---|---|---|---|---|---|---|---|---|"]
    for w in res["worst"]:
        o.append("| %s | %s | %d | %.2f | %d | %s | %+.0f | %s | %s | %s |" % (
            w["run"], w["part"], w["bar"], w["beat"], w["pitch"], w["kind"], w["heardMinusWrittenMs"],
            "-" if w["leavePlus23Ms"] is None else "%+.0f" % w["leavePlus23Ms"],
            "-" if w["predictedMs"] is None else "%+.0f" % w["predictedMs"], w["how"]))
    return "\n".join(o) + "\n"


def load_runs(args):
    out = []
    for a in args:
        lab, path = (a.split("=", 1) if "=" in a and not os.path.exists(a) else (None, a))
        if not os.path.exists(path):
            raise SystemExit("no report %s" % path)
        out.append(Run(path, lab))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reports", nargs="+")
    ap.add_argument("--baseline", nargs="+", default=[])
    ap.add_argument("--by", default="section,kind")
    ap.add_argument("--held", type=float, default=0.4, help="a note before at least this long (s): 'transition after held'")
    ap.add_argument("--worst", type=int, default=20)
    ap.add_argument("-o", "--out", required=True)
    a = ap.parse_args(argv)
    by = [x.strip() for x in a.by.split(",") if x.strip()]
    bad = [x for x in by if x not in ("section", "part", "kind", "tempo", "bar", "patch", "situation")]
    if bad:
        ap.error("unknown --by key(s): %s" % ", ".join(bad))
    runs, base = load_runs(a.reports), load_runs(a.baseline)
    res = summarise(runs, base, by, a.held, a.worst)
    os.makedirs(a.out, exist_ok=True)
    md = markdown(res)
    with open(os.path.join(a.out, "summary.md"), "w", encoding="utf-8") as f:
        f.write(md)
    with open(os.path.join(a.out, "summary.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
    sys.stdout.write(md)
    return 0


if __name__ == "__main__":
    sys.exit(main())
