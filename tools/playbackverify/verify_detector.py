#!/usr/bin/env python3
"""A Python replica of --verify-playback's detector (audio/vst3/playbackverify.cpp): the audio's offset against the notes
and the missing-note check, with the fixes of OPEN_ITEMS.md › Missing tools #8 (item 32) switchable one by one, so a
change of the detector can be tried on a verify folder's renders before it goes into C++, and so the regression set
(tools/playbackverify/regression/) can be checked without audio.

What it replicates (the C++ is the reference; numbers agree to ~0.1 dB / one 5 ms offset step, float32 against float64):
  - Spectrogram (1024-point Hann, 10 ms hop, log(1 + G |X|), 1024 samples of silence first), the broadband onset flux;
  - the offset: bestOffset's flux sum over the strikes' onsets for -50 … 500 ms (5 ms steps), its margin (the best sum
    against the best one >= 50 ms away), "offset ambiguous" (margin < 3 %), and the expected-onset alignment (each
    strike at its note-on + its expected latency, a residual search of +-60 ms);
  - per note (with the reference, the built-in synth's render): its fundamental and 2nd harmonic on the fine spectrum
    (4096 points), those another sounding note has left out; its rise at the onset and its level after it (a legato
    note: the mean power from 40 to 70 % of its length, or of the two ends; a short legato note, < 0.25 s: the peak
    over [on + 0.05, off + 0.25]);
    the level against the reference's, against its register's (notes within 3 semitones of its group and kind, at
    least 12); missing: 20 dB under (legato, or not rising where the reference rises) or 30 dB under. Fine bins at the
    note's cents (fractional pitches) or on 12-TET keys.
Not replicated: the strike (missing-attack) check, cut short, silence, drift, clipping.

Commands
  curve <wav> <notes.tsv>                       the onset-flux curve against the offset (was private offset_curve.py)
  analyse <verify folder> <part> [--extra <tsv>] [--rules old|new|<list>] [--json]
                                                the missing-note check of one part from "<score> <part> library.wav",
                                                "… built-in.wav" and "… notes.tsv"; --extra: per note cents and expected
                                                latency (columns time, cents, expectedMs; matched by time)
  levels <verify folder> <part> [--extra <tsv>] each note's held level and deficit, 12-TET and tuned bins, old and new
                                                window (was private legato_level.py)
  features <verify folder> <part> --extra <tsv> --notes <t1,t2,…> --out <json>
                                                the regression features: the part's offset curves and, for the listed
                                                notes, each level variant's deficit and rises at every offset
                                                (no audio, no note list of the score)
  judge <features.json> [--rules …]             the decision from the features alone (what the regression test runs)
  relevel <notes.tsv> [--extra <tsv>] [--patch n] [--rules …]
                                                the register step alone on a verify notes.tsv's measured levels (no
                                                audio: a run whose renders weren't kept)

Rules (--rules new = all five, as the C++'s defaults; old = none, the detector before 2026-10-23): margin (offset margin +
ambiguous: a legato missing note on an ambiguous offset is "unclear", not flagged), expected (expected-onset alignment
+-60 ms where the notes carry an expected latency), shortpeak (a short legato note's peak window), cents (fractional
bins), heldmean (a legato note's held level the mean power from 40 to 70 % of it every 20 ms, not two points: found
on the regression set, Whence's Celli F#3 at 32.758 s read 21 dB under at one vibrato dip), groups (a note's register
level among the notes measured the same way, a short legato note's peak apart from held ones, and a small group's from
its patch's before all groups': build #474's Celli bar 13 Ab3 on its voice copy, 4 notes, was held against the main
slot's short notes' peaks, "21 dB under", heard the loudest of its chord).

Needs numpy and soundfile.
"""
import argparse
import json
import math
import os
import sys

import numpy as np

GAIN = 6.0
RULES = ("margin", "expected", "shortpeak", "cents", "heldmean", "groups")
SETTINGS = dict(offset_from=-0.05, offset_to=0.5, step=0.005, ambiguous_margin=0.03, rival_distance=0.05,
                expected_window=0.06, legato_short=0.25, short_from=0.05, short_after=0.25, short_step=0.01, held_step=0.02,
                missing_note_db=20.0, held_missing_db=12.0, rise_min=6.0, rise_margin=10.0, ref_rise_min=10.0, absent_db=30.0)


def lround(x):
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


def db(p):
    return 10 * math.log10(max(p, 1e-20))


def median(v):
    v = sorted(v)
    return v[len(v) // 2] if v else 0.0          # (nth_element at n / 2, as the C++)


class Spectrogram:
    def __init__(self, mono, rate):
        self.rate = float(rate)
        self.size = 2048 if rate > 50000 else 1024
        self.hop = max(1, lround(rate * 0.01))
        self.pad = self.size
        mono = np.asarray(mono, dtype=np.float64)
        self.peak = float(np.max(np.abs(mono))) if len(mono) else 0.0
        norm = 1.0 / self.peak if self.peak > 0 else 1.0
        self.audio = mono * norm
        x = np.concatenate([np.zeros(self.pad), self.audio])
        n = (len(x) - self.size) // self.hop + 1 if len(x) >= self.size else 0
        w = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(self.size) / (self.size - 1))
        fr = np.lib.stride_tricks.sliding_window_view(x, self.size)[::self.hop][:n] * w
        L = np.log1p(GAIN * 1024.0 / self.size * np.abs(np.fft.rfft(fr, axis=1)))
        self.flux = np.concatenate([[0.0], np.maximum(0, np.diff(L, axis=0)).sum(axis=1)]) if n else np.zeros(0)
        self.frames = n
        self.fine = self.size * 4
        self._win = 0.5 - 0.5 * np.cos(2 * np.pi * np.arange(self.fine) / (self.fine - 1))
        self._cache = {}

    def frame_at(self, s):
        return lround((s * self.rate + self.pad - self.size / 2.0) / self.hop)

    def fine_bins(self, pitches, harmonics, spread):
        n = self.fine
        bins = set()
        for p in pitches:
            f0 = 440.0 * 2 ** ((p - 69) / 12.0)
            for k in range(1, harmonics + 1):
                f = k * f0
                if f > self.rate * 0.45:
                    break
                b = lround(f * n / self.rate)
                for d in range(-spread, spread + 1):
                    if 0 < b + d < n // 2:
                        bins.add(b + d)
        return sorted(bins)

    def spectrum(self, seconds):
        start = lround(seconds * self.rate) - self.fine // 2
        if start in self._cache:
            return self._cache[start]
        seg = np.zeros(self.fine)
        a, b = max(0, start), min(len(self.audio), start + self.fine)
        if b > a:
            seg[a - start:b - start] = self.audio[a:b]
        # (bins under 1024 only, float32: the 2nd harmonic of C8 is bin 780; a bounded cache)
        X = None if not np.any(seg) else (np.abs(np.fft.rfft(seg * self._win)[:1024]) ** 2).astype(np.float32)
        if len(self._cache) > 60000:
            self._cache.clear()
        self._cache[start] = X
        return X

    def fine_power(self, bins, seconds):
        X = self.spectrum(seconds)
        if X is None:
            return 0.0
        return float(sum(float(X[b]) for b in bins if b < len(X)))


# ---------------------------------------------------------------- notes, strikes, offset

def read_notes(path):
    """a verify folder's notes.tsv: time, length, pitch (name), slot, articulation, legato, sustained, …"""
    rows = [l.rstrip("\n").split("\t") for l in open(path, encoding="utf-8")]
    head, rows = rows[0], rows[1:]
    col = {h: i for i, h in enumerate(head)}
    notes = []
    for r in rows:
        if len(r) < len(head):
            continue
        on = float(r[col["time"]])
        num = lambda name, d: float(r[col[name]]) if name in col and r[col[name]] != "" else d
        notes.append(dict(on=on, off=on + float(r[col["length"]]), key=midi(r[col["pitch"]]), cents=num("cents", 0.0),
                          expected=num("expectedMs", -1.0) / 1000 if num("expectedMs", -1.0) >= 0 else -1.0,
                          group=1000 + int(r[col["slot"]]), patch=int(num("patch", -1)), legato=r[col["legato"]] == "1",
                          after=num("afterDb", 0.0), refAfter=num("refAfterDb", 0.0), rise=num("riseDb", 0.0),
                          refRise=num("refRiseDb", 0.0), reported=float(r[col["deficitDb"]]), flagged=r[col["flagged"]] == "1"))
    return notes


def midi(name):
    P = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    k, i = P[name[0]], 1
    while i < len(name) and name[i] in "#b":
        k += 1 if name[i] == "#" else -1
        i += 1
    return k + 12 * (int(name[i:]) + 1)


def read_extra(path, notes):
    """per note cents and expected latency (ms after its note-on its sound is expected), matched by time and key"""
    rows = [l.rstrip("\n").split("\t") for l in open(path, encoding="utf-8") if l.strip()]
    head, rows = rows[0], rows[1:]
    col = {h: i for i, h in enumerate(head)}
    extra = [(float(r[col["time"]]), int(r[col["key"]]) if "key" in col else None, float(r[col["cents"]]),
              float(r[col["expectedMs"]]), int(r[col["patch"]]) if "patch" in col else None) for r in rows]
    for n in notes:
        best = None
        for t, k, c, e, p in extra:
            if k is not None and k != n["key"]:
                continue
            d = abs(t - n["on"])
            if d < 0.002 and (best is None or d < best[0]):
                best = (d, c, e, p)
        if best:
            n["cents"], n["expected"] = best[1], best[2] / 1000 if best[2] >= 0 else -1.0
            if best[3] is not None:
                n["patch"] = best[3]
    return notes


def strikes(notes, tolerance=0.005):
    order = sorted(range(len(notes)), key=lambda i: notes[i]["on"])
    out = []
    for i in order:
        if not out or notes[i]["on"] - out[-1]["time"] > tolerance:
            out.append(dict(time=notes[i]["on"], notes=[]))
        out[-1]["notes"].append(i)
    return out


def flux_sum(spec, times, offset):
    s = 0.0
    for t in times:
        f = spec.frame_at(t + offset)
        if 0 <= f < spec.frames:
            s += spec.flux[f]
    return s


def offset_curve(spec, times, lo, hi, step):
    """(offset, flux sum) at lo + k * step up to hi (PlaybackVerify::offsetCurve)"""
    n = int(math.floor((hi - lo) / step + 1e-6))
    return [(round(lo + k * step, 6), flux_sum(spec, times, lo + k * step)) for k in range(n + 1)]


def margin_of(curve, distance):
    """(best offset, its sum, the best offset >= distance away, its sum, margin = best / rival - 1)"""
    best = max(curve, key=lambda c: c[1])          # (the first of equal sums, as the C++'s strict >)
    for c in curve:
        if c[1] == best[1]:
            best = c
            break
    far = [c for c in curve if abs(c[0] - best[0]) >= distance - 1e-9]
    if not far:
        return best[0], best[1], None, 0.0, float("inf")
    rival = max(far, key=lambda c: c[1])
    return best[0], best[1], rival[0], rival[1], (best[1] / rival[1] - 1) if rival[1] > 0 else float("inf")


def choose_offset(spec, notes, rules, S=SETTINGS):
    """the offset and how it was found: mode "search" (-50 … 500 ms) or "expected" (each strike at its expected latency,
    +-60 ms); margin / ambiguous from the wide curve (search mode)"""
    ks = strikes(notes)
    times = [k["time"] for k in ks]
    wide = offset_curve(spec, times, S["offset_from"], S["offset_to"], S["step"])
    b, bs, rv, rs, m = margin_of(wide, S["rival_distance"])
    info = dict(searchOffset=b, rival=rv, margin=m, wide=wide)
    exp = [min(notes[i]["expected"] for i in k["notes"]) for k in ks]
    known = [e for e in exp if e >= 0]
    if "expected" in rules and known and len(known) * 2 >= len(ks):
        E = median(known)
        shifted = [t + ((e if e >= 0 else E) - E) for t, e in zip(times, exp)]
        lo, hi = E - S["expected_window"], E + S["expected_window"]
        near = offset_curve(spec, shifted, lo, hi, S["step"])
        best = near[0]
        for c in near:
            if c[1] > best[1]:
                best = c
        info.update(mode="expected", expected=E, offset=best[0], near=near,
                    edge=abs(best[0] - E) >= S["expected_window"] - S["step"] - 1e-9, ambiguous=False)
    else:
        info.update(mode="search", offset=b, ambiguous="margin" in rules and m < S["ambiguous_margin"])
    return info


# ---------------------------------------------------------------- the missing-note check

def tuned(n, rules):
    return n["key"] + (n["cents"] / 100.0 if "cents" in rules else 0.0)


def note_bins(spec, notes, i, rules):
    """its fundamental and 2nd harmonic bins (those another note sounding then has left out, unless both are) and kind"""
    n = notes[i]
    p = tuned(n, rules)
    others = [tuned(m, rules) for j, m in enumerate(notes)
              if j != i and m["on"] <= n["on"] + 0.15 and m["off"] + 0.3 >= n["on"] - 0.1 and abs(tuned(m, rules) - p) > 1e-6]
    shared = set(spec.fine_bins(others, 8, 2))
    bins, kind = [], 0
    for h in (1, 2):
        hb = spec.fine_bins([p], h, 1)
        prev = set(spec.fine_bins([p], h - 1, 1)) if h > 1 else set()
        only = [b for b in hb if b not in prev]
        if all(b not in shared for b in only):
            bins += only
            kind |= h
    if not bins:
        bins = spec.fine_bins([p], 2, 1)
    return sorted(set(bins)), kind


def level_and_rise(spec, n, bins, offset, rules, S=SETTINGS):
    on = n["on"] + offset
    before = spec.fine_power(bins, on - 0.06)
    after = max(spec.fine_power(bins, on + 0.05), spec.fine_power(bins, on + 0.1))
    length = n["off"] - n["on"]
    if n["legato"] and "shortpeak" in rules and length < S["legato_short"]:
        # a short legato note: its peak over [on + 0.05, off + 0.25] (SSO's transition lands after a short note's end)
        t, best = S["short_from"], 0.0
        while t <= length + S["short_after"] + 1e-9:
            best = max(best, spec.fine_power(bins, on + t))
            t += S["short_step"]
        level = db(best)
    elif n["legato"]:
        t1 = min(max(0.2, 0.4 * length), max(0.05, length - 0.02))
        t2 = min(max(0.3, 0.7 * length), max(0.05, length - 0.02))
        if "heldmean" in rules:
            # the mean power over [t1, t2] every 20 ms (two points read a vibrato's or a beat's dip as a missing note)
            k = max(1, lround((t2 - t1) / S["held_step"]))
            level = db(sum(spec.fine_power(bins, on + t1 + (t2 - t1) * j / k) for j in range(k + 1)) / (k + 1))
        else:
            level = db(0.5 * (spec.fine_power(bins, on + t1) + spec.fine_power(bins, on + t2)))
    else:
        level = db(after)
    return level, db(after) - db(before)


def note_diffs(lib, ref, notes, lib_offset, ref_offset, rules, bins_kind=None):
    """each note's (after, refAfter, rise, refRise, kind)"""
    out = []
    for i, n in enumerate(notes):
        bins, kind = bins_kind[i] if bins_kind else note_bins(lib, notes, i, rules)
        la, rise = level_and_rise(lib, n, bins, lib_offset, rules)
        ra, rrise = level_and_rise(ref, n, bins, ref_offset, rules)
        out.append(dict(after=max(-120.0, la), refAfter=max(-120.0, ra), rise=max(-99.0, min(99.0, rise)),
                        refRise=max(-99.0, min(99.0, rrise)), kind=kind))
    return out


def method_of(n, rules, S=SETTINGS):
    """how its level is read (NoteCheck::Method): attack, held, or a short legato note's peak"""
    if not n["legato"]:
        return 0
    length = n["length"] if "length" in n else n["off"] - n["on"]
    return 2 if "shortpeak" in rules and length < S["legato_short"] else 1


def group_key(notes, c, i, level, rules):
    """(level 0 group / 1 patch / 2 all, its id, the method with "groups" (else legato or not), kind)"""
    n = notes[i]
    ident = n["group"] if level == 0 else n.get("patch", -1) if level == 1 else 0
    how = method_of(n, rules) if "groups" in rules else (1 if n["legato"] else 0)
    return (level, ident, how, c[i]["kind"])


def register_bases(notes, c, rules=()):
    """each note's register base (the median diff of the notes within 3 … 96 semitones of its key, at least 12) and
    the key it is taken in: its group's; with "groups" its patch's when its group has fewer than 6 (Note::patch), then
    all groups'; with "groups" among the notes measured the same way (a short legato note's peak apart from held ones)"""
    diff = [x["after"] - x["refAfter"] for x in c]
    levels = (0, 1, 2) if "groups" in rules else (0, 2)
    by = {}
    for i, n in enumerate(notes):
        for level in levels:
            if level == 1 and n.get("patch", -1) < 0:
                continue
            by.setdefault(group_key(notes, c, i, level, rules), {}).setdefault(n["key"], []).append(diff[i])
    count = {k: sum(len(v) for v in d.values()) for k, d in by.items()}
    base = []
    for i, n in enumerate(notes):
        k = group_key(notes, c, i, 2, rules)
        for level in levels[:-1]:
            if level == 1 and n.get("patch", -1) < 0:
                continue
            if count.get(group_key(notes, c, i, level, rules), 0) >= 6:
                k = group_key(notes, c, i, level, rules)
                break
        d = by[k]
        w = 3
        while True:
            near = [x for q in range(n["key"] - w, n["key"] + w + 1) for x in d.get(q, [])]
            if len(near) >= 12 or w * 2 > 128:
                break
            w *= 2
        base.append(median(near))
    return diff, base


def judge_note(n, c, deficit, ambiguous, rules=(), S=SETTINGS):
    """"missing", "unclear" (missing, but on an ambiguous offset: a legato note), or "" """
    no_rise = c["rise"] < S["rise_min"] or c["rise"] < c["refRise"] - S["rise_margin"]
    absent = deficit <= -S["absent_db"]
    # (with "groups" a held legato note, against held notes only, by heldMissingDb)
    limit = S["held_missing_db"] if "groups" in rules and method_of(n, rules) == 1 else S["missing_note_db"]
    under = deficit <= -limit and (n["legato"] or (no_rise and c["refRise"] >= S["ref_rise_min"]))
    if not (under or absent):
        return ""
    return "unclear" if ambiguous and n["legato"] else "missing"


def analyse(lib, ref, notes, rules, S=SETTINGS):
    off = choose_offset(lib, notes, rules, S)
    ref_ks = [k["time"] for k in strikes(notes)]
    rb = margin_of(offset_curve(ref, ref_ks, S["offset_from"], S["offset_to"], S["step"]), S["rival_distance"])
    ref_offset = rb[0]
    c = note_diffs(lib, ref, notes, off["offset"], ref_offset, rules)
    diff, base = register_bases(notes, c, rules)
    out = []
    for i, n in enumerate(notes):
        d = diff[i] - base[i]
        if c[i]["after"] <= -100 and c[i]["refAfter"] > -60:
            d = -99.0
        out.append(dict(time=n["on"], key=n["key"], cents=n["cents"], length=round(n["off"] - n["on"], 3), legato=n["legato"],
                        deficit=round(d, 1), verdict=judge_note(n, c[i], d, off["ambiguous"], rules)))
    return off, ref_offset, out


def relevel(notes, rules, S=SETTINGS):
    """each note's deficit and verdict from its measured levels (a notes.tsv's afterDb / refAfterDb / riseDb / refRiseDb)
    by the register step alone (kinds from the notes' partials; the offset rules don't apply)"""
    spec = Spectrogram(np.zeros(8192), 44100)
    c = []
    for i, n in enumerate(notes):
        _, kind = note_bins(spec, notes, i, rules)
        c.append(dict(after=n["after"], refAfter=n["refAfter"], rise=n["rise"], refRise=n["refRise"], kind=kind))
    diff, base = register_bases(notes, c, rules)
    out = []
    for i, n in enumerate(notes):
        d = diff[i] - base[i]
        if c[i]["after"] <= -100 and c[i]["refAfter"] > -60:
            d = -99.0
        out.append(dict(time=n["on"], key=n["key"], deficit=round(d, 1), verdict=judge_note(n, c[i], d, False, rules)))
    return out


def level_features(notes, picks):
    """for a run whose renders weren't kept: the picked notes' measured levels and, under each level variant, their
    register base (from every note of the part, as relevel; no other note's data is kept)"""
    spec = Spectrogram(np.zeros(8192), 44100)
    out = {}
    idx = [min((i for i in range(len(notes)) if notes[i]["key"] == k), key=lambda i: abs(notes[i]["on"] - t)) for t, k in picks]
    for name, rules in VARIANTS:
        c = []
        for i, n in enumerate(notes):
            _, kind = note_bins(spec, notes, i, rules)
            c.append(dict(after=n["after"], refAfter=n["refAfter"], rise=n["rise"], refRise=n["refRise"], kind=kind))
        _, base = register_bases(notes, c, rules)
        for i in idx:
            n = notes[i]
            e = out.setdefault(i, dict(time=round(n["on"], 3), key=n["key"], length=round(n["off"] - n["on"], 3), legato=n["legato"],
                                       after=n["after"], refAfter=n["refAfter"], rise=n["rise"], refRise=n["refRise"], base={}))
            e["base"][name] = round(base[i], 2)
    return [out[i] for i in idx]


def judge_levels(rows, rules):
    """each note's deficit and verdict from level_features' rows (the offset as the run had it)"""
    variant = variant_of(rules)
    res = []
    for n in rows:
        d = n["after"] - n["refAfter"] - n["base"][variant]
        if n["after"] <= -100 and n["refAfter"] > -60:
            d = -99.0
        res.append(dict(time=n["time"], key=n["key"], deficit=round(d, 1), verdict=judge_note(n, n, d, False, rules)))
    return res


# ---------------------------------------------------------------- regression features and the judge

def offsets_grid(S=SETTINGS):
    n = lround((S["offset_to"] - S["offset_from"]) / S["step"])
    return [round(S["offset_from"] + k * S["step"], 3) for k in range(n + 1)]


LEVEL_RULES = ("cents", "groups", "heldmean", "shortpeak")
VARIANTS = [("old", ()), ("cents", ("cents",)), ("heldmean", ("heldmean",)), ("shortpeak", ("shortpeak",)),
            ("nogroups", ("cents", "heldmean", "shortpeak")), ("new", LEVEL_RULES)]


def variant_of(rules):
    r = tuple(sorted(x for x in rules if x in LEVEL_RULES))
    for name, v in VARIANTS:
        if tuple(sorted(v)) == r:
            return name
    raise ValueError("no feature variant for level rules %s (variants: %s)" % (", ".join(r), ", ".join(n for n, _ in VARIANTS)))


def features(lib, ref, notes, picks, S=SETTINGS):
    """what judge() needs, without the audio or the score's notes: the part's offset curves (search, and expected with
    the strikes at their expected latencies) and, for each picked note, under each level variant (VARIANTS), its
    deficit and rises at every offset of the grid (the reference's offset fixed)"""
    ks = strikes(notes)
    times = [k["time"] for k in ks]
    exp = [min(notes[i]["expected"] for i in k["notes"]) for k in ks]
    known = [e for e in exp if e >= 0]
    E = median(known) if known else None
    grid = offsets_grid(S)
    wide = offset_curve(lib, times, S["offset_from"], S["offset_to"], S["step"])
    shifted = [t + ((e if e >= 0 else E) - E) for t, e in zip(times, exp)] if E is not None else None
    expected_curve = offset_curve(lib, shifted, S["offset_from"], S["offset_to"], S["step"]) if shifted else None
    ref_offset = margin_of(offset_curve(ref, times, S["offset_from"], S["offset_to"], S["step"]), S["rival_distance"])[0]
    out = dict(strikes=len(ks), expectedKnown=len(known), expected=E, refOffset=ref_offset, grid=grid,
               wide=[round(s, 2) for _, s in wide], expectedCurve=[round(s, 2) for _, s in expected_curve] if expected_curve else None,
               notes=[])
    # picks: (time, key or None): the note starting nearest the time (of that key)
    idx = [min((i for i in range(len(notes)) if k is None or notes[i]["key"] == k), key=lambda i: abs(notes[i]["on"] - t))
           for t, k in picks]
    per = {i: dict(time=round(notes[i]["on"], 3), key=notes[i]["key"], length=round(notes[i]["off"] - notes[i]["on"], 3), legato=notes[i]["legato"],
                   cents=notes[i]["cents"], variants={}) for i in idx}
    for name, rules in VARIANTS:
        bins_kind = [note_bins(lib, notes, i, rules) for i in range(len(notes))]
        rows = {i: dict(deficit=[], rise=[], refRise=[]) for i in idx}
        for o in grid:
            c = note_diffs(lib, ref, notes, o, ref_offset, rules, bins_kind)
            diff, base = register_bases(notes, c, rules)
            for i in idx:
                r = rows[i]
                d = diff[i] - base[i]
                if c[i]["after"] <= -100 and c[i]["refAfter"] > -60:
                    d = -99.0
                r["deficit"].append(round(d, 1))
                r["rise"].append(round(c[i]["rise"], 1))
                r["refRise"].append(round(c[i]["refRise"], 1))
        for i in idx:
            per[i]["variants"][name] = rows[i]
    out["notes"] = [per[i] for i in idx]
    return out


def judge(part, rules, S=SETTINGS):
    """the offset and each note's verdict from a part's features"""
    grid = part["grid"]
    wide = list(zip(grid, part["wide"]))
    b, _, rv, _, m = margin_of(wide, S["rival_distance"])
    res = dict(searchOffset=b, rival=rv, margin=m)
    if "expected" in rules and part.get("expectedCurve") and part["expectedKnown"] * 2 >= part["strikes"]:
        E = part["expected"]
        near = [(o, s) for o, s in zip(grid, part["expectedCurve"]) if E - S["expected_window"] - 1e-9 <= o <= E + S["expected_window"] + 1e-9]
        best = near[0]
        for c in near:
            if c[1] > best[1]:
                best = c
        res.update(mode="expected", offset=best[0], ambiguous=False)
    else:
        res.update(mode="search", offset=b, ambiguous="margin" in rules and m < S["ambiguous_margin"])
    k = min(range(len(grid)), key=lambda j: abs(grid[j] - res["offset"]))
    variant = variant_of(rules)
    verdicts = []
    for n in part["notes"]:
        v = n["variants"][variant]
        c = dict(rise=v["rise"][k], refRise=v["refRise"][k])
        d = v["deficit"][k]
        verdicts.append(dict(time=n["time"], key=n["key"], deficit=d, verdict=judge_note(n, c, d, res["ambiguous"], rules)))
    res["notes"] = verdicts
    return res


# ---------------------------------------------------------------- command line

def load_wav(path):
    import soundfile as sf
    d, sr = sf.read(path, dtype="float64", always_2d=True)
    return d.mean(axis=1), sr


def part_files(folder, part):
    names = os.listdir(folder)
    lib = [x for x in names if x.endswith(" %s library.wav" % part)]
    if not lib:
        sys.exit("no '<score> %s library.wav' in %s" % (part, folder))
    base = lib[0][:-len(" library.wav")]
    return (os.path.join(folder, base + " library.wav"), os.path.join(folder, base + " built-in.wav"),
            os.path.join(folder, base + " notes.tsv"))


def parse_rules(s):
    if s == "old":
        return ()
    if s == "new":
        return RULES
    r = tuple(x for x in s.split(",") if x)
    bad = [x for x in r if x not in RULES]
    if bad:
        sys.exit("unknown rule(s): %s (rules: %s)" % (", ".join(bad), ", ".join(RULES)))
    return r


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("curve")
    c.add_argument("wav")
    c.add_argument("notes")
    for name in ("analyse", "levels", "features"):
        p = sub.add_parser(name)
        p.add_argument("folder")
        p.add_argument("part")
        p.add_argument("--extra")
        if name == "analyse":
            p.add_argument("--rules", default="new")
            p.add_argument("--json", action="store_true")
        if name == "features":
            p.add_argument("--notes", required=True, help="note-on times (s), comma-separated, each optionally :key")
            p.add_argument("--out", required=True)
    rl = sub.add_parser("relevel")
    rl.add_argument("notes")
    rl.add_argument("--extra")
    rl.add_argument("--rules", default="new")
    rl.add_argument("--patch", type=int, help="every note's patch (a notes.tsv before the patch column: one patch per part)")
    rl.add_argument("--features", help="write the listed notes' (--picks) register features to this JSON (adds a part)")
    rl.add_argument("--picks", help="time:key,… for --features")
    rl.add_argument("--part", help="the part's name in --features")
    rl.add_argument("--run", help="the run's name in --features (e.g. #474)")
    j = sub.add_parser("judge")
    j.add_argument("features")
    j.add_argument("--rules", default="new")
    a = ap.parse_args(argv)

    if a.cmd == "curve":
        x, sr = load_wav(a.wav)
        spec = Spectrogram(x, sr)
        notes = read_notes(a.notes)
        curve = offset_curve(spec, [k["time"] for k in strikes(notes)], SETTINGS["offset_from"], SETTINGS["offset_to"], SETTINGS["step"])
        b, bs, rv, rs, m = margin_of(curve, SETTINGS["rival_distance"])
        print("strikes %d best offset %.3f; best >= 50 ms away %.3f, margin %.1f %%%s"
              % (len(strikes(notes)), b, rv, 100 * m, " (offset ambiguous)" if m < SETTINGS["ambiguous_margin"] else ""))
        for o, s in curve[::2]:
            print("%6.3f %8.1f %s" % (o, s, "#" * int(60 * s / bs)))
        return 0
    if a.cmd == "relevel":
        # the register step alone, from a verify notes.tsv's levels (afterDb, refAfterDb as measured): no audio
        notes = read_notes(a.notes)
        if a.extra:
            read_extra(a.extra, notes)
        if a.patch is not None:
            for n in notes:
                n["patch"] = a.patch
        rules = parse_rules(a.rules)
        if a.features:
            f = level_features(notes, [(float(t.split(":")[0]), int(t.split(":")[1])) for t in a.picks.split(",") if t])
            data = json.load(open(a.features, encoding="utf-8")) if os.path.exists(a.features) else {}
            data.setdefault("runs", {}).setdefault(a.run or "run", {})[a.part or os.path.basename(a.notes)] = f
            with open(a.features, "w", encoding="utf-8") as fh:
                json.dump(data, fh, indent=1)
            print("%s: %d notes' register features into %s" % (a.part, len(f), a.features))
            return 0
        out = relevel(notes, rules)
        for n, o in zip(notes, out):
            if o["verdict"] or n["flagged"]:
                print("%7.3f  key %3d %+4.0f c  %.3f s  slot %d  reported %6.1f  now %6.1f  %s"
                      % (n["on"], n["key"], n["cents"], n["off"] - n["on"], n["group"] - 1000, n["reported"], o["deficit"],
                         o["verdict"] or "-"))
        print("missing %d" % sum(o["verdict"] == "missing" for o in out))
        return 0
    if a.cmd == "judge":
        data = json.load(open(a.features, encoding="utf-8"))
        rules = parse_rules(a.rules)
        for name, part in data["parts"].items():
            r = judge(part, rules)
            print("%s: offset %.3f (%s%s), margin %.1f %%" % (name, r["offset"], r["mode"], ", ambiguous" if r["ambiguous"] else "",
                                                             100 * r["margin"]))
            for n in r["notes"]:
                print("  %7.3f  key %3d  deficit %6.1f  %s" % (n["time"], n["key"], n["deficit"], n["verdict"] or "-"))
        return 0

    libp, refp, notesp = part_files(a.folder, a.part)
    notes = read_notes(notesp)
    if a.extra:
        read_extra(a.extra, notes)
    x, sr = load_wav(libp)
    y, sr2 = load_wav(refp)
    lib, ref = Spectrogram(x, sr), Spectrogram(y, sr2)
    if a.cmd == "analyse":
        off, ref_offset, out = analyse(lib, ref, notes, parse_rules(a.rules))
        if a.json:
            off = {k: (bool(v) if isinstance(v, (bool, np.bool_)) else v) for k, v in off.items() if k not in ("wide", "near")}
            print(json.dumps(dict(offset=off, refOffset=ref_offset, notes=out), indent=1))
            return 0
        print("%s: offset %.3f (%s%s); search best %.3f, best >= 50 ms away %s, margin %.1f %%; reference %.3f"
              % (a.part, off["offset"], off["mode"], ", ambiguous" if off["ambiguous"] else "", off["searchOffset"],
                 "%.3f" % off["rival"] if off["rival"] is not None else "-", 100 * off["margin"], ref_offset))
        for n in out:
            if n["verdict"]:
                print("  %7.3f  key %d %+4.0f c  %.3f s  deficit %6.1f  %s" % (n["time"], n["key"], n["cents"], n["length"], n["deficit"], n["verdict"]))
        print("  missing %d, unclear %d" % (sum(n["verdict"] == "missing" for n in out), sum(n["verdict"] == "unclear" for n in out)))
        return 0
    if a.cmd == "levels":
        print("time    key cents  length  reported  " + "  ".join("%9s" % v for v, _ in VARIANTS))
        offs = {}
        res = {}
        for name, rules in VARIANTS:
            off, ro, out = analyse(lib, ref, notes, rules + ("expected",) if a.extra else rules)
            offs[name] = off["offset"]
            res[name] = out
        for i, n in enumerate(notes):
            print("%7.3f %4d %+5.0f  %.3f  %6.1f  %s" % (n["on"], n["key"], n["cents"], n["off"] - n["on"], n["reported"],
                                                       "  ".join("%9.1f" % res[v][i]["deficit"] for v, _ in VARIANTS)))
        print("offsets: " + ", ".join("%s %.3f" % kv for kv in offs.items()))
        return 0
    if a.cmd == "features":
        picks = [(float(t.split(":")[0]), int(t.split(":")[1]) if ":" in t else None) for t in a.notes.split(",") if t]
        f = features(lib, ref, notes, picks)
        data = {}
        if os.path.exists(a.out):
            data = json.load(open(a.out, encoding="utf-8"))
        data.setdefault("parts", {})[a.part] = f
        with open(a.out, "w", encoding="utf-8") as fh:
            json.dump(data, fh, separators=(",", ":"))
        print("%s: %d notes' features into %s" % (a.part, len(f["notes"]), a.out))
        return 0
    return 1


if __name__ == "__main__":
    sys.exit(main())
