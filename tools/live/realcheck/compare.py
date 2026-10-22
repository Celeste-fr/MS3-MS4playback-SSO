#!/usr/bin/env python3
"""compare.py: Live's own export of a plain set against MuseScore's own export of the same score, per Kontakt track
(patch), per part and for the whole set. numpy + scipy + soundfile only, so it runs on the VM's Python (the WAVs never
leave the VM).

  compare.py --live <dir> --base <name> --set <set.als> --ms <Playback verify folder> [--json out] [--report out]
             [--max-lag-ms 400] [--null-pass -30] [--level-pass 0.5] [--onset-pass 5]

--live: the folder Live's *Export Audio/Video* wrote with Rendered Track = All Individual Tracks: "<base>.wav" (Main)
and "<base> <track>.wav" per track that makes sound (alsset.tracks' exportName: a name met before gets "-1", "-2").
--set: the set Live exported (the track list, each Kontakt track's patch from the .als.txt report next to the set
Create Live Set wrote, else from the plug-in's track name). --ms: MuseScore's `--verify-playback --verify-wav` folder
with MS_VERIFY_ROUTES=1: "<score> route <code>.wav" per route, report.json (route -> patch), "<score> events.tsv"
(note-ons per slot, slot = 16 port + channel).

Per Kontakt track (all MuseScore routes of its patch added up: its tuning / voice copies are slots of that one Kontakt):
  lag       the shift of Live's audio against MuseScore's (ms, + = Live later), cross-correlation of the 10 ms
            envelopes first (+-max-lag), then of the samples within +-3 ms of it
  corr      correlation of the two signals at that lag
  null      residual (Live - MuseScore at the lag) energy against MuseScore's, dB (-inf: identical)
  gainDb    the level difference, least squares (Live / MuseScore), and rmsDb (Live RMS - MuseScore RMS)
  edges     (--edges t1,t2,…: a lane's steps, in seconds) when each level step lands in Live's export and in
            MuseScore's (halfway between the levels before and after, both at the global lag), and the difference
  notes     per note-on of its routes (events.tsv): levelDb = Live - MuseScore over [on, on + 250 ms] (both at the
            global lag), onsetMs = the local lag (cross-correlation within +-40 ms over [on - 20, on + 200 ms]); the
            median and the largest |.| are reported; notes under 30 dB below the route's peak are skipped
Verdict per track: PASS when null <= --null-pass dB (default -30, `--live-equivalence --live-equivalence-strict`'s
residual) or, for Kontakt's own randomness (SSO round robins / modulators), every note within --level-pass x 6 dB and
--onset-pass ms and the median note within --level-pass dB: "PASS (notes)"; else DIFFERS. The lag itself is reported,
never judged here (the start lead and Track Delays are the caller's checks).
"""
import argparse
import glob
import json
import math
import os
import re
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import alsset  # noqa: E402

ENV_HOP = 0.010


def mono(x):
    return x.mean(axis=1) if x.ndim > 1 else x


def read(path):
    x, sr = sf.read(path, dtype="float64", always_2d=True)
    return x, sr


def db(v):
    return 10 * math.log10(v) if v > 0 else float("-inf")


def envelope(x, sr, hop=ENV_HOP):
    n = int(hop * sr)
    m = len(x) // n
    if m == 0:
        return np.zeros(1)
    return np.sqrt((x[: m * n].reshape(m, n) ** 2).mean(axis=1) + 1e-20)


def xcorr_lag(a, b, max_lag):
    """the lag (in samples of these arrays) maximising sum a[t] b[t - lag], |lag| <= max_lag: + = a later than b"""
    n = len(a) + len(b)
    nfft = 1 << (n - 1).bit_length()
    fa = np.fft.rfft(a, nfft)
    fb = np.fft.rfft(b, nfft)
    c = np.fft.irfft(fa * np.conj(fb), nfft)
    lags = np.concatenate([np.arange(0, max_lag + 1), np.arange(-max_lag, 0)])
    vals = np.concatenate([c[: max_lag + 1], c[nfft - max_lag:]])
    return int(lags[int(np.argmax(vals))])


def shift(x, lag):
    """x moved earlier by lag samples (lag > 0: x was late)"""
    if lag > 0:
        return np.concatenate([x[lag:], np.zeros((lag,) + x.shape[1:])])
    if lag < 0:
        return np.concatenate([np.zeros((-lag,) + x.shape[1:]), x[:lag]])
    return x


def fit_length(a, b):
    n = max(len(a), len(b))
    pad = lambda x: np.concatenate([x, np.zeros((n - len(x),) + x.shape[1:])]) if len(x) < n else x
    return pad(a), pad(b)


def align(live, ms, sr, max_lag_ms):
    """(lag in samples, Live shifted onto MuseScore)"""
    la, lb = mono(live), mono(ms)
    ea, eb = np.log(envelope(la, sr) + 1e-6), np.log(envelope(lb, sr) + 1e-6)
    ea, eb = ea - ea.mean(), eb - eb.mean()
    coarse = xcorr_lag(ea, eb, max(1, int(max_lag_ms / 1000 / ENV_HOP)))
    lag = int(round(coarse * ENV_HOP * sr))
    w = int(0.003 * sr) + int(ENV_HOP * sr)
    a = shift(la, lag)
    a, b = fit_length(a, lb)
    fine = xcorr_lag(a, b, w)
    return lag + fine


def metrics(live, ms, sr, lag):
    a = shift(live, lag)
    a, b = fit_length(a, ms)
    am, bm = mono(a), mono(b)
    eb = float((b ** 2).sum())
    ea = float((a ** 2).sum())
    res = float(((a - b) ** 2).sum())
    corr = float((am * bm).sum() / math.sqrt(max((am ** 2).sum() * (bm ** 2).sum(), 1e-30)))
    g = float((am * bm).sum() / max((bm ** 2).sum(), 1e-30))
    return {
        "lagMs": round(lag / sr * 1000, 3), "corr": round(corr, 6),
        "nullDb": round(db(res / eb), 2) if eb > 0 else None,
        "gainDb": round(20 * math.log10(abs(g)), 3) if g != 0 else None,
        "rmsDb": round(db(ea / eb), 3) if eb > 0 and ea > 0 else None,
        "liveSilent": ea < 1e-12, "msSilent": eb < 1e-12,
    }, a, b


def fine_envelope(m, sr, hop_s=0.001, win_s=0.005):
    hop, win = int(hop_s * sr), int(win_s * sr)
    n = (len(m) - win) // hop
    if n <= 0:
        return np.zeros(1)
    idx = np.arange(n)[:, None] * hop + np.arange(0, win, 2)[None, :]
    return np.sqrt((m[idx] ** 2).mean(axis=1) + 1e-20)


def note_metrics(a, b, sr, ons, max_local_ms=40):
    """per note-on (seconds), on the already aligned a (Live) and b (MuseScore): the level over [on, on + 250 ms] and
    the local lag of the two onset functions over [on - 30, on + 200 ms]: the rises of the 1 ms log envelopes (floored
    50 dB under the peak), max(0, d/dt): what moves is the attack, not the waveform (a tone's waveform correlates at
    every period) or a held level"""
    am, bm = mono(a), mono(b)
    peak = max(float(np.abs(bm).max()), 1e-9)
    fl = peak * 10 ** (-50 / 20)
    ea = np.maximum(0, np.diff(np.log(np.maximum(fine_envelope(am, sr), fl))))
    eb = np.maximum(0, np.diff(np.log(np.maximum(fine_envelope(bm, sr), fl))))
    rows = []
    for t in ons:
        i0, i1 = int(t * sr), int((t + 0.25) * sr)
        if i1 > len(bm):
            continue
        rb = math.sqrt(float((bm[i0:i1] ** 2).mean()) + 1e-30)
        ra = math.sqrt(float((am[i0:i1] ** 2).mean()) + 1e-30)
        if 20 * math.log10(rb / peak) < -30:
            # a soft attack: its level over the first half second instead
            i1 = min(len(bm), int((t + 0.5) * sr))
            rb = math.sqrt(float((bm[i0:i1] ** 2).mean()) + 1e-30)
            ra = math.sqrt(float((am[i0:i1] ** 2).mean()) + 1e-30)
            if 20 * math.log10(rb / peak) < -40:
                continue
        j0, j1 = max(0, int((t - 0.03) * 1000)), int((t + 0.2) * 1000)
        if j1 > min(len(ea), len(eb)):
            continue
        x, y = ea[j0:j1], eb[j0:j1]
        best, lag = None, 0
        for L in range(-max_local_ms, max_local_ms + 1):
            if L >= 0:
                v = float((x[L:] * y[:len(y) - L]).sum()) if L < len(x) else -1e9
            else:
                v = float((x[:L] * y[-L:]).sum())
            if best is None or v > best:
                best, lag = v, L
        rows.append({"t": round(t, 4), "levelDb": round(20 * math.log10(ra / rb), 2), "onsetMs": float(lag)})
    return rows


def spans(a, b, sr, win_s=0.25, worse_db=-20.0, quiet_db=-60.0):
    """where the aligned a (Live) and b (MuseScore) differ: the 250 ms windows (b's level within 60 dB of its peak)
    whose null is above -20 dB, merged into spans (s, on b's timeline: MuseScore's), each with Live's level against
    MuseScore's: a note Live doesn't play shows as a span far below 0 dB"""
    am, bm = mono(a), mono(b)
    n = min(len(am), len(bm))
    w = int(win_s * sr)
    peak = max(float(np.abs(bm[:n]).max()) if n else 0.0, 1e-9)
    out = []
    for i in range(0, n - w + 1, w):
        x, y = am[i:i + w], bm[i:i + w]
        ey = float((y ** 2).mean())
        if ey <= 0 or 10 * math.log10(ey) - 20 * math.log10(peak) < quiet_db:
            continue
        nd = 10 * math.log10(float(((x - y) ** 2).mean()) / ey + 1e-30)
        if nd <= worse_db:
            continue
        lv = 10 * math.log10((float((x ** 2).mean()) + 1e-30) / ey)
        if out and abs(out[-1]["end"] - i / sr) < 1e-9:
            o = out[-1]
            o["end"] = (i + w) / sr
            o["worstNullDb"] = max(o["worstNullDb"], round(nd, 1))
            o["levelDb"] = round(min(o["levelDb"], lv), 1) if lv < 0 else round(max(o["levelDb"], lv), 1)
        else:
            out.append({"start": i / sr, "end": (i + w) / sr, "worstNullDb": round(nd, 1), "levelDb": round(lv, 1)})
    for o in out:
        # (MuseScore's file ends: its render stops at the last note's tail, Live's export runs on to the render length)
        o["atEnd"] = o["end"] >= n / sr - 1.0
        o["start"], o["end"] = round(o["start"], 3), round(o["end"], 3)
    return out


def null_outside(a, b, sr, sp):
    """the null (dB) with the spans left out: how the rest of the track nulls"""
    am, bm = mono(a), mono(b)
    n = min(len(am), len(bm))
    keep = np.ones(n, dtype=bool)
    for x in sp:
        keep[int(x["start"] * sr): int(x["end"] * sr)] = False
    eb = float((bm[:n][keep] ** 2).sum())
    return round(db(float(((am[:n] - bm[:n])[keep] ** 2).sum()) / eb), 2) if eb > 0 else None


def edge_times(x, sr, edges):
    """for each edge time (s): when the level crosses halfway between its level 50-250 ms before and after (ms against
    the edge; None: no step of at least 1 dB there)"""
    m = mono(x)
    hop, win = int(0.001 * sr), int(0.005 * sr)
    n = (len(m) - win) // hop
    if n <= 0:
        return [None for _ in edges]
    idx = np.arange(n)[:, None] * hop + np.arange(0, win, 4)[None, :]
    env = np.sqrt((m[idx] ** 2).mean(axis=1) + 1e-20)
    t = np.arange(n) * hop / sr + win / 2 / sr
    out = []
    for e in edges:
        pre = env[(t > e - 0.25) & (t < e - 0.05)]
        post = env[(t > e + 0.05) & (t < e + 0.25)]
        if not len(pre) or not len(post):
            out.append(None)
            continue
        a, b = float(np.median(pre)), float(np.median(post))
        if abs(20 * math.log10(b / a)) < 1:
            out.append(None)
            continue
        mid = (a + b) / 2
        sel = np.where((t > e - 0.15) & (t < e + 0.2))[0]
        hit = [t[i] for i in sel if (env[i] - mid) * (b - a) > 0]
        out.append(round((hit[0] - e) * 1000, 2) if hit else None)
    return out


def read_events(ms_dir):
    ev = glob.glob(os.path.join(ms_dir, "* events.tsv"))
    ons = {}
    if not ev:
        return ons
    for line in open(ev[0], encoding="utf-8"):
        c = line.rstrip("\n").split("\t")
        if c[0] == "time" or len(c) < 6:
            continue
        if c[3] == "on" and int(c[5]) > 0:
            ons.setdefault(int(c[2]), []).append(float(c[0]))
    return ons


def ms_routes(ms_dir):
    rep = json.load(open(os.path.join(ms_dir, "report.json"), encoding="utf-8"))
    routes = []
    for s in rep.get("scores", []):
        for r in s.get("routes", []):
            w = glob.glob(os.path.join(ms_dir, "* route %s.wav" % r["route"]))
            p, c = (int(v) for v in r["route"].split("-"))
            # (a route "p-c" is port p, MIDI channel c counted from 1; events.tsv's slot is 16 p + c - 1)
            routes.append({"route": r["route"], "patch": r.get("patch", ""), "slot": 16 * p + c - 1,
                           "wav": w[0] if w else None})
    return routes


def kontakt_patches(set_path, tracks):
    """Kontakt track id -> patch, from Create Live Set's report next to the set (<set>.txt: "<name> — Kontakt 8
    (<patch>)" lines, indented by group); a set Live saved has none: the caller passes the written set's report"""
    txt = set_path + ".txt"
    out = {}
    if not os.path.exists(txt):
        return out
    lines = [l.rstrip("\n") for l in open(txt, encoding="utf-8")]
    kont = [t for t in tracks if any(d["type"] == "PluginDevice" for d in t["devices"])]
    found = []
    # (the report names the Kontakt tracks as Create Live Set wrote them; a set Update Live Set changed keeps them)
    for l in lines:
        m = re.match(r"^\s+(.*?) \u2014 [^\u2014(]+\((.+?)\)( \u2014 |$)", l)
        if m and " \u2014 MIDI To " not in l:
            found.append((m.group(1).strip(), m.group(2)))
    # by name, in order (two Kontakt tracks of one name: the report lists them in track order)
    used = set()
    for name, patch in found:
        t = next((t for t in kont if t["name"] == name and t["id"] not in used), None)
        if t:
            used.add(t["id"])
            out[t["id"]] = patch
    return out


def add(signals):
    n = max(len(s) for s in signals)
    ch = max(s.shape[1] for s in signals)
    out = np.zeros((n, ch))
    for s in signals:
        out[: len(s), : s.shape[1]] += s
    return out


def judge(m, notes, a, sp=None):
    if m["msSilent"] and m["liveSilent"]:
        return "PASS (both silent)"
    if m["liveSilent"]:
        return "DIFFERS (Live silent)"
    if m["msSilent"]:
        return "DIFFERS (MuseScore silent)"
    if m["nullDb"] is not None and m["nullDb"] <= a.null_pass:
        return "PASS"
    sp = [x for x in sp or [] if not x.get("atEnd")]
    if any(x["levelDb"] < -10 for x in sp):
        return "DIFFERS (Live much quieter: %s)" % ", ".join("%.2f-%.2f s %+.1f dB" % (x["start"], x["end"], x["levelDb"])
                                                          for x in sp if x["levelDb"] < -10)
    if notes:
        lv = sorted(abs(n["levelDb"]) for n in notes)
        on = sorted(abs(n["onsetMs"]) for n in notes)
        med = sorted(n["levelDb"] for n in notes)[len(notes) // 2]
        if abs(med) <= a.level_pass and lv[-1] <= 6 * a.level_pass and on[-1] <= a.onset_pass:
            return "PASS (notes)"
    return "DIFFERS"


def summary_notes(notes):
    if not notes:
        return {}
    lv = sorted(n["levelDb"] for n in notes)
    on = sorted(n["onsetMs"] for n in notes)
    return {"n": len(notes), "levelMedianDb": lv[len(lv) // 2], "levelMaxAbsDb": max(abs(lv[0]), abs(lv[-1])),
            "onsetMedianMs": on[len(on) // 2], "onsetMaxAbsMs": max(abs(on[0]), abs(on[-1])),
            "worst": sorted(notes, key=lambda n: -abs(n["levelDb"]) - abs(n["onsetMs"]) / 5)[:5]}


def compare(a):
    tracks = alsset.tracks(alsset.load(a.set))
    patches = kontakt_patches(a.report_of or a.set, tracks)
    routes = ms_routes(a.ms)
    ons = read_events(a.ms)
    by_patch = {}
    for r in routes:
        by_patch.setdefault(r["patch"], []).append(r)
    cache = {}

    def ms_sig(rs):
        sigs = []
        sr = None
        for r in rs:
            if r["wav"] is None:
                continue
            if r["wav"] not in cache:
                cache[r["wav"]] = read(r["wav"])
            x, sr = cache[r["wav"]]
            sigs.append(x)
        return (add(sigs), sr) if sigs else (None, None)

    def live_wav(t):
        p = os.path.join(a.live, "%s %s.wav" % (a.base, t["exportName"]))
        return p if os.path.exists(p) else None

    out = {"tracks": [], "parts": [], "main": None, "missing": []}
    kont = [t for t in tracks if t["id"] in patches]
    patch_count = {}
    for t in kont:
        patch_count[patches[t["id"]]] = patch_count.get(patches[t["id"]], 0) + 1
    used = set()
    for t in kont:
        patch = patches[t["id"]]
        lw = live_wav(t)
        rs = by_patch.get(patch, [])
        row = {"track": t["name"], "key": t["key"], "patch": patch, "routes": [r["route"] for r in rs],
               "liveWav": os.path.basename(lw) if lw else None}
        if patch_count[patch] > 1:
            row["verdict"] = "NOT COMPARED (patch on %d Kontakt tracks: compared per part / Main only)" % patch_count[patch]
            out["tracks"].append(row)
            continue
        if lw and not rs:
            # a Kontakt MuseScore plays nothing on (a part's main patch when every note plays on another): silent?
            x, _ = read(lw)
            peak = float(np.abs(x).max()) if len(x) else 0.0
            row["liveSilent"] = peak < 1e-6
            row["verdict"] = "PASS (no notes: silent in both)" if row["liveSilent"] else \
                "DIFFERS (Live plays %.1f dBFS peak, MuseScore no route of this patch)" % (20 * math.log10(peak))
            out["tracks"].append(row)
            continue
        if not lw or not rs:
            row["verdict"] = "MISSING (%s)" % ("no Live WAV" if not lw else "no MuseScore route of this patch")
            out["tracks"].append(row)
            out["missing"].append(row["track"])
            continue
        used.update(r["route"] for r in rs)
        live, sr = read(lw)
        ms, sr2 = ms_sig(rs)
        if sr != sr2:
            row["verdict"] = "ERROR (sample rates %s / %s)" % (sr, sr2)
            out["tracks"].append(row)
            continue
        lag = align(live, ms, sr, a.max_lag_ms)
        m, al, bl = metrics(live, ms, sr, lag)
        t_ons = sorted(x for r in rs for x in ons.get(r["slot"], []))
        notes = note_metrics(al, bl, sr, t_ons)
        row.update(m)
        row["notes"] = summary_notes(notes)
        row["spans"] = spans(al, bl, sr)
        row["nullOutsideSpansDb"] = null_outside(al, bl, sr, row["spans"])
        row["verdict"] = judge(m, notes, a, row["spans"])
        if a.edges:
            el, em = edge_times(al, sr, a.edges), edge_times(bl, sr, a.edges)
            row["edges"] = [{"t": e, "liveMs": x, "museScoreMs": y, "diffMs": round(x - y, 2) if x is not None and y is not None else None}
                            for e, x, y in zip(a.edges, el, em)]
        out["tracks"].append(row)
    # parts: a part group's export against the routes of its Kontakt tracks' patches
    for g in [t for t in tracks if t["kind"] == "GroupTrack"]:
        kids = [t for t in kont if t["group"] == g["id"]]
        if not kids:
            continue
        ps = [patches[k["id"]] for k in kids]
        row = {"part": g["name"], "patches": ps}
        if any(patch_count[p] > 1 for p in ps):
            row["verdict"] = "NOT COMPARED (a patch shared with another part)"
            out["parts"].append(row)
            continue
        lw = live_wav(g)
        rs = [r for p in ps for r in by_patch.get(p, [])]
        if not lw or not rs:
            row["verdict"] = "MISSING"
            out["parts"].append(row)
            continue
        live, sr = read(lw)
        ms, _ = ms_sig(rs)
        lag = align(live, ms, sr, a.max_lag_ms)
        m, al, bl = metrics(live, ms, sr, lag)
        notes = note_metrics(al, bl, sr, sorted(x for r in rs for x in ons.get(r["slot"], [])))
        row.update(m)
        row["notes"] = summary_notes(notes)
        row["spans"] = spans(al, bl, sr)
        row["nullOutsideSpansDb"] = null_outside(al, bl, sr, row["spans"])
        row["verdict"] = judge(m, notes, a, row["spans"])
        out["parts"].append(row)
    main = os.path.join(a.live, "%s.wav" % a.base)
    if os.path.exists(main) and routes:
        live, sr = read(main)
        ms, _ = ms_sig(routes)
        lag = align(live, ms, sr, a.max_lag_ms)
        m, al, bl = metrics(live, ms, sr, lag)
        m["note"] = "Main includes the Main track's fader; MuseScore's routes have no master: gainDb shows it"
        out["main"] = m
    if a.floor:
        # MuseScore against itself (a second export of the same score): how far two renders of the same
        # events null at all, the floor a Live track can't be expected to beat
        fl = {r["route"]: r for r in ms_routes(a.floor)}
        rows = []
        for r in routes:
            f = fl.get(r["route"])
            if not r["wav"] or not f or not f["wav"]:
                continue
            x, sr = read(r["wav"])
            y, _ = read(f["wav"])
            lag = align(x, y, sr, a.max_lag_ms)
            m, _, _ = metrics(x, y, sr, lag)
            rows.append({"route": r["route"], "patch": r["patch"], "lagMs": m["lagMs"], "corr": m["corr"],
                         "nullDb": m["nullDb"], "gainDb": m["gainDb"]})
        out["floor"] = rows
        by = {}
        for f in rows:
            by.setdefault(f["patch"], []).append(f["nullDb"])
        for t in out["tracks"]:
            v = [x for x in by.get(t.get("patch"), []) if x is not None]
            if v:
                t["floorNullDb"] = max(v)
    out["unmatchedRoutes"] = [r["route"] for r in routes if r["route"] not in used]
    verdicts = [r["verdict"] for r in out["tracks"] + out["parts"]]
    out["verdict"] = "PASS" if verdicts and all(v.startswith("PASS") or v.startswith("NOT COMPARED") for v in verdicts) else "FAIL"
    return out


def report_text(r):
    L = ["Live export against MuseScore export: %s" % r["verdict"], ""]
    for t in r["tracks"]:
        if "lagMs" in t:
            n = t.get("notes") or {}
            L.append("  %-40s %-22s lag %+8.2f ms  corr %.5f  null %6s dB  gain %+.2f dB  notes %s  level med %+.2f max %.2f dB, onset med %+.2f max %.2f ms"
                     % (t["track"], t["verdict"], t["lagMs"], t["corr"], t["nullDb"], t["gainDb"] or 0, n.get("n", 0),
                        n.get("levelMedianDb", 0), n.get("levelMaxAbsDb", 0), n.get("onsetMedianMs", 0), n.get("onsetMaxAbsMs", 0)))
            if t.get("spans"):
                L.append("      without the spans below: null %s dB" % t.get("nullOutsideSpansDb"))
            for x in t.get("spans", [])[:12]:
                L.append("      differs %7.2f-%7.2f s (MuseScore's time): null up to %+.1f dB, Live %+.1f dB" % (
                    x["start"], x["end"], x["worstNullDb"], x["levelDb"]))
        else:
            L.append("  %-40s %s" % (t["track"], t["verdict"]))
    L.append("")
    for p in r["parts"]:
        if "lagMs" in p:
            L.append("  part %-35s %-22s lag %+8.2f ms  corr %.5f  null %6s dB  gain %+.2f dB" % (
                p["part"], p["verdict"], p["lagMs"], p["corr"], p["nullDb"], p["gainDb"] or 0))
        else:
            L.append("  part %-35s %s" % (p["part"], p["verdict"]))
    if r.get("main"):
        m = r["main"]
        L.append("  Main: lag %+.2f ms corr %.5f null %s dB gain %+.2f dB" % (m["lagMs"], m["corr"], m["nullDb"], m["gainDb"] or 0))
    if r.get("floor"):
        L.append("")
        L.append("  MuseScore against its own second export (the floor):")
        for f in r["floor"]:
            L.append("    route %-6s %-36s lag %+8.2f ms  corr %.5f  null %6s dB  gain %+.2f dB" % (
                f["route"], f["patch"], f["lagMs"], f["corr"], f["nullDb"], f["gainDb"] or 0))
    if r.get("unmatchedRoutes"):
        L.append("  MuseScore routes not compared per track: %s" % ", ".join(r["unmatchedRoutes"]))
    return "\n".join(L) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--live", required=True)
    ap.add_argument("--base", required=True)
    ap.add_argument("--set", required=True)
    ap.add_argument("--report-of", help="the written set whose .txt report names the Kontakt tracks' patches")
    ap.add_argument("--ms", required=True)
    ap.add_argument("--floor", help="a second MuseScore export of the same score: each route's null against it")
    ap.add_argument("--json")
    ap.add_argument("--report")
    ap.add_argument("--edges", type=lambda v: [float(x) for x in v.split(",") if x], default=[],
                    help="times (s) of parameter-lane steps: each track's level edge there, Live against MuseScore")
    ap.add_argument("--max-lag-ms", type=float, default=400)
    ap.add_argument("--null-pass", type=float, default=-30)
    ap.add_argument("--level-pass", type=float, default=0.5)
    ap.add_argument("--onset-pass", type=float, default=5)
    a = ap.parse_args(argv)
    r = compare(a)
    txt = report_text(r)
    if a.json:
        json.dump(r, open(a.json, "w", encoding="utf-8"), indent=1)
    if a.report:
        open(a.report, "w", encoding="utf-8").write(txt)
    sys.stdout.write(txt)
    return 0 if r["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
