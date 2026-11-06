#!/usr/bin/env python3
"""dump_compare.py: compare two (or more) dumpEvents / audit-dump folders event by event, routes matched by meaning.

  dump_compare.py <dumpA> <dumpB> [--route-by part,patch,lane | code] [--only notes,cc,pb] [--kind museScore,live]
                  [--tol-ms 0] [--max-lines 20] [--json out.json]
  dump_compare.py --slots <dump>...          note-ons per (part, patch) and lane in each dump (the chord-voice lanes:
                                             a lane split that differs between renders shows here)

A dump folder: tst_liveequivalence dumpEvents' MS_DUMP_OUT, or tst_playbackaudit auditScore's MS_AUDIT_DUMP (the heard
check's dump): "<route> museScore.txt" (and "<route> live.txt") event lists "seconds on|off|cc|pb ...", routemap.txt
"route|part|patch|lane|cents|bend", and notes.txt, trace.txt, tunings.txt, gains.txt, volumes.txt, headroom.txt,
instances.txt, bars.txt, predicted.tsv when there.

Routes are matched by (part, patch, lane) from routemap.txt (the route numbers shift when a part gets more copies);
--route-by code matches by route code. Per matched route and kind: identical or not; the first differing event; counts of
note-ons only in A / only in B (by key at the same time within --tol-ms), note-ons whose velocity changed, CCs / pitch
bends that differ; per note the timing delta of note-ons moved by less than 50 ms. Then the other files, line by line.
Exit 0 when everything compared is identical, 1 when anything differs, 2 on bad input.
"""
import argparse
import collections
import json
import os
import sys

FILES = ("notes.txt", "trace.txt", "tunings.txt", "gains.txt", "volumes.txt", "headroom.txt", "instances.txt", "bars.txt",
         "predicted.tsv", "lead.txt")
ROUTED = ("tunings.txt", "volumes.txt")         # (their first field is a route code)


def routemap(d):
    m = {}
    p = os.path.join(d, "routemap.txt")
    if os.path.exists(p):
        with open(p, encoding="utf-8") as f:
            for ln in f:
                x = ln.rstrip("\n").split("|")
                if len(x) >= 4:
                    m[x[0]] = dict(part=x[1], patch=x[2], lane=x[3], cents=x[4] if len(x) > 4 else "",
                                   bend=x[5] if len(x) > 5 else "")
    return m


def route_key(code, m, by):
    if by == ["code"] or code not in m:
        return code if code in m or by == ["code"] else "unmapped " + code
    return " | ".join(m[code][k] for k in by)


def parse_events(path):
    out = []
    with open(path, encoding="utf-8") as f:
        for ln in f:
            p = ln.split()
            if not p or p[0].startswith("#"):
                continue
            try:
                t = float(p[0])
            except ValueError:
                continue
            out.append((t, p[1], tuple(p[2:])))
    return out


def event_files(d, m, by, kinds):
    out = {}
    for n in sorted(os.listdir(d)):
        for kind in kinds:
            suf = " %s.txt" % kind
            if n.endswith(suf):
                code = n[:-len(suf)]
                out[(route_key(code, m, by), kind)] = (code, parse_events(os.path.join(d, n)))
    return out


def norm_kind(k):
    return {"on": "notes", "off": "notes", "cc": "cc", "pb": "pb"}.get(k, k)


def compare_events(a, b, only, tol_s):
    """a, b: [(t, kind, args)]; the differences as a dict"""
    a = [e for e in a if norm_kind(e[1]) in only]
    b = [e for e in b if norm_kind(e[1]) in only]
    res = dict(identical=a == b, eventsA=len(a), eventsB=len(b))
    if a == b:
        return res
    first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))
    res["first"] = dict(index=first, A=fmt(a[first]) if first < len(a) else None, B=fmt(b[first]) if first < len(b) else None)
    # note-ons by key (velocity > 0)
    ons = lambda ev: [(t, int(x[0]), int(x[1])) for t, k, x in ev if k == "on" and len(x) >= 2 and int(x[1]) > 0]
    oa, ob = ons(a), ons(b)
    used = set()
    moved, vel, only_a = [], 0, []
    bykey = collections.defaultdict(list)
    for j, (t, k, v) in enumerate(ob):
        bykey[k].append(j)
    for t, k, v in oa:
        cand = [j for j in bykey[k] if j not in used and abs(ob[j][0] - t) <= max(tol_s, 0.05)]
        if not cand:
            only_a.append((t, k, v))
            continue
        j = min(cand, key=lambda j: abs(ob[j][0] - t))
        used.add(j)
        dt = ob[j][0] - t
        if abs(dt) > tol_s:
            moved.append(dict(t=round(t, 6), key=k, deltaMs=round(1000 * dt, 3)))
        if ob[j][2] != v:
            vel += 1
    only_b = [ob[j] for j in range(len(ob)) if j not in used]
    ctl = lambda ev, kk: collections.Counter((round(t, 6), x) for t, k, x in ev if k == kk)
    res.update(noteOnsA=len(oa), noteOnsB=len(ob), onlyA=len(only_a), onlyB=len(only_b), velocityChanged=vel,
               moved=len(moved), movedMaxMs=max((abs(x["deltaMs"]) for x in moved), default=0.0),
               onlyAFirst=[dict(t=round(t, 6), key=k, velocity=v) for t, k, v in only_a[:5]],
               onlyBFirst=[dict(t=round(t, 6), key=k, velocity=v) for t, k, v in only_b[:5]],
               movedFirst=moved[:5])
    for kk in ("cc", "pb"):
        if kk in only:
            ca, cb = ctl(a, kk), ctl(b, kk)
            res[kk + "Differ"] = sum(((ca - cb) + (cb - ca)).values())
    return res


def fmt(e):
    return "%.6f %s %s" % (e[0], e[1], " ".join(e[2]))


def read_lines(d, name, m, by):
    p = os.path.join(d, name)
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8") as f:
        rows = f.read().splitlines()
    if name in ROUTED:
        out = []
        for ln in rows:
            x = ln.split("|")
            if x and x[0] in m:
                x[0] = route_key(x[0], m, by)
            out.append("|".join(x))
        return sorted(out)
    return rows


def compare(da, db, by=("part", "patch", "lane"), only=("notes", "cc", "pb"), kinds=("museScore", "live"), tol_ms=0.0):
    by = list(by)
    ma, mb = routemap(da), routemap(db)
    fa, fb = event_files(da, ma, by, kinds), event_files(db, mb, by, kinds)
    routes = []
    for k in sorted(set(fa) | set(fb)):
        x, y = fa.get(k), fb.get(k)
        r = dict(route=k[0], kind=k[1], codeA=x[0] if x else None, codeB=y[0] if y else None)
        if x is None or y is None:
            r.update(identical=False, missing="A" if x is None else "B")
        else:
            r.update(compare_events(x[1], y[1], set(only), tol_ms / 1000.0))
        routes.append(r)
    files = []
    for name in FILES:
        x, y = read_lines(da, name, ma, by), read_lines(db, name, mb, by)
        if x is None and y is None:
            continue
        e = dict(file=name, identical=x == y)
        if x != y:
            ca, cb = collections.Counter(x or []), collections.Counter(y or [])
            e.update(onlyA=sum((ca - cb).values()), onlyB=sum((cb - ca).values()),
                     firstA=sorted((ca - cb).elements())[:5], firstB=sorted((cb - ca).elements())[:5])
        files.append(e)
    return dict(A=da, B=db, routeBy=by, routes=routes, files=files,
                identical=all(r["identical"] for r in routes) and all(f["identical"] for f in files))


def slots(d):
    """{(part, patch): {lane: note-ons}} from museScore.txt and routemap.txt"""
    m = routemap(d)
    out = collections.defaultdict(collections.Counter)
    for n in os.listdir(d):
        if n.endswith(" museScore.txt"):
            code = n[:-len(" museScore.txt")]
            info = m.get(code, dict(part="?", patch=code, lane="?"))
            k = sum(1 for t, kk, x in parse_events(os.path.join(d, n)) if kk == "on" and len(x) >= 2 and int(x[1]) > 0)
            out[(info["part"], info["patch"])][info["lane"]] += k
    return out


def report_text(res, max_lines=20):
    o = []
    same = sum(1 for r in res["routes"] if r["identical"])
    o.append("A %s\nB %s" % (res["A"], res["B"]))
    o.append("routes x kinds: %d identical, %d differ (matched by %s)" % (same, len(res["routes"]) - same, ", ".join(res["routeBy"])))
    shown = 0
    for r in res["routes"]:
        if r["identical"]:
            continue
        if shown >= max_lines:
            o.append("  ... (more)")
            break
        shown += 1
        if r.get("missing"):
            o.append("  %-46s %-9s only in %s" % (r["route"], r["kind"], "B" if r["missing"] == "A" else "A"))
            continue
        o.append("  %-46s %-9s %s: first diff #%d  A: %s  B: %s" % (
            r["route"], r["kind"], "%s/%s" % (r["codeA"], r["codeB"]), r["first"]["index"], r["first"]["A"], r["first"]["B"]))
        if "noteOnsA" in r:
            o.append("      note-ons %d/%d, only A %d, only B %d, velocity changed %d, moved %d (max %.3f ms)%s%s" % (
                r["noteOnsA"], r["noteOnsB"], r["onlyA"], r["onlyB"], r["velocityChanged"], r["moved"], r["movedMaxMs"],
                ", cc differ %d" % r["ccDiffer"] if "ccDiffer" in r else "", ", pb differ %d" % r["pbDiffer"] if "pbDiffer" in r else ""))
    for f in res["files"]:
        o.append("%-14s %s" % (f["file"], "identical" if f["identical"] else "differs: %d lines only in A, %d only in B" % (f["onlyA"], f["onlyB"])))
        if not f["identical"]:
            for ln in f["firstA"][:3]:
                o.append("    - " + ln[:160])
            for ln in f["firstB"][:3]:
                o.append("    + " + ln[:160])
    o.append("IDENTICAL" if res["identical"] else "DIFFERENT")
    return "\n".join(o) + "\n"


def slots_text(dumps):
    per = [slots(d) for d in dumps]
    keys = sorted(set(k for p in per for k in p))
    o = []
    for k in keys:
        lanes = sorted(set(l for p in per for l in p.get(k, {})), key=lambda s: (len(s), s))
        if len(lanes) < 2 and all(len(p.get(k, {})) <= 1 for p in per):
            row = ["%d" % sum(p.get(k, {}).values()) for p in per]
        else:
            row = ["/".join("%d" % p.get(k, {}).get(l, 0) for l in lanes) for p in per]
        flag = "" if len(set(row)) == 1 else "   DIFFERS"
        o.append("%-14s %-34s %s%s" % (k[0], k[1], "  ".join(row), flag))
    return "\n".join(o) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dumps", nargs="+")
    ap.add_argument("--route-by", default="part,patch,lane")
    ap.add_argument("--only", default="notes,cc,pb")
    ap.add_argument("--kind", default="museScore,live")
    ap.add_argument("--tol-ms", type=float, default=0.0)
    ap.add_argument("--max-lines", type=int, default=20)
    ap.add_argument("--json")
    ap.add_argument("--slots", action="store_true", help="note-ons per lane in each dump")
    a = ap.parse_args(argv)
    for d in a.dumps:
        if not os.path.isdir(d):
            ap.error("no dump folder %s" % d)
    if a.slots:
        sys.stdout.write(slots_text(a.dumps))
        return 0
    if len(a.dumps) != 2:
        ap.error("two dumps to compare (or --slots with any number)")
    by = [x.strip() for x in a.route_by.split(",") if x.strip()]
    res = compare(a.dumps[0], a.dumps[1], by, [x.strip() for x in a.only.split(",")], [x.strip() for x in a.kind.split(",")],
                  a.tol_ms)
    sys.stdout.write(report_text(res, a.max_lines))
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(res, f, indent=1)
    return 0 if res["identical"] else 1


if __name__ == "__main__":
    sys.exit(main())
