#!/usr/bin/env python3
"""Perceptual onsets, short notes' real lengths and legato after first notes of each length, from background rest checks
run with the later parts (MuseScore --extract-library ... --check-rest --rest-parts onset,shorts,legatolengths;
ArticulationCheck::rest, articulationcheck.h; 2026-10-01, for the legato-timing fixes).

    onset_from_check.py <check folder or zip> ... [-d tools/soundlibraries]

Reads each results.json (folders, zips, or zips of zips), keeps per patch and part the newest result and writes only
derived numbers, keyed by patch then sound ("<articulation name>", a drum hit "<name> (key n)"); times in ms from the
note-on, -1: never (silent, or not within that many dB):

  sso_sound_onset.json      every semitone that sounds at mf, pp / mf / ff (velocity = CC1 = 32 / 80 / 112), each held
                            1.5 s: [[pitch, [pp t20, t15, t12, t10], [mf ...], [ff ...], [pp e20, e15, e12, e10], [mf ...],
                            [ff ...], [pp peak, mf peak, ff peak]], ...]. t: the first time the short-term perceived
                            loudness (perceivedEnvelope: ERB filters, attack 22 / release 50 ms, every 5 ms, the 2048-point
                            window's centre) is within 20 / 15 / 12 / 10 dB of its peak in the first 1.5 s; e: the same on
                            the power in 5 ms windows; peak: when the perceived peak is. A silent pp / ff: null
  sso_short_lengths.json    mf, held 50 / 100 / 250 / 500 / 1000 / 2000 ms at the test pitch and an octave (else a fifth)
                            under and over it: [[pitch, heldMs, perceived peak ms, [p6, p10, p15, p20], [e6, e10, e15, e20],
                            loudDb], ...]; p: the last time the perceived loudness is within 6 / 10 / 15 / 20 dB of its peak
                            (the end of that window), e: the same on power
  sso_legato_lengths.json   per legato sound: [[firstMs, interval, leaveMs, arriveMs, dipDb], ...]: two notes slurred
                            (velocity 64, 30 ms overlap), the first held 100 / 200 / 300 / 500 / 1000 ms before the second
                            note-on; leave / arrive: as sso_legato_grid.json (from the second note-on)
  sso_legato_grid_pitches.json  per legato sound: "range": [low, high] (probes every 3 semitones at mf), "rows":
                            [[startPitch, interval, leaveMs, midMs, arriveMs, dipDb], ...]: from 5 starting pitches (10 /
                            30 / 50 / 70 / 90 % of the range), intervals -12 -7 -5 -3 -2 -1 1 2 3 5 7 12, velocity = CC1 =
                            80, the first note held 1.2 s; timed by harmonics (legatoHarmonic: octaves too): the first
                            time the second pitch's own harmonics against the first's are 10 / 50 / 90 % of the way from
                            their level on the first note to that on the second (FFT frames' centres, ms after the second
                            note-on; -1: not reached)
Prints a summary.
"""
import argparse
import os
import statistics
import sys

from rest_from_check import results, notes, sound_name, write


def onset_rows(s, fields):
    by = {}
    for n in notes(s.get("onset", []), fields):
        by.setdefault(n["pitch"], {})[n["level"]] = n
    rows = []
    for pitch in sorted(by):
        lv = by[pitch]
        mf = lv.get(80)
        if not mf or not mf["sounds"]:
            continue
        def t(level, prefix):
            n = lv.get(level)
            if not n or not n["sounds"]:
                return None
            return [n[f"{prefix}{d}Ms"] for d in (20, 15, 12, 10)]
        def peak(level):
            n = lv.get(level)
            return n["perceivedPeakMs"] if n and n["sounds"] else None
        rows.append([pitch, t(32, "onset"), t(80, "onset"), t(112, "onset"),
                     t(32, "energyOnset"), t(80, "energyOnset"), t(112, "energyOnset"), [peak(32), peak(80), peak(112)]])
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("paths", nargs="+")
    ap.add_argument("-d", "--dir", default=os.path.dirname(os.path.abspath(__file__)))
    a = ap.parse_args()
    newest = {}   # (patch, part) -> (date, patch result)
    for path in a.paths:
        for where, r in results(path):
            date = r.get("date", "")
            for p in r.get("patches", []):
                if not p.get("restOnly") or "rest" not in p:
                    continue
                for part in ("onset", "shorts", "legatolengths", "legatopitches"):
                    if part in p.get("restParts", "").split(","):
                        k = (p["patch"], part)
                        if k not in newest or date > newest[k][0]:
                            newest[k] = (date, p)
    onset, shorts, legato, pitches = {}, {}, {}, {}
    for (patch, part), (_, p) in newest.items():
        fields = p.get("restFields")
        for s in p.get("rest", []):
            name = sound_name(s)
            if part == "onset" and s.get("onset"):
                onset.setdefault(patch, {})[name] = onset_rows(s, fields)
            elif part == "shorts" and s.get("shorts"):
                shorts.setdefault(patch, {})[name] = sorted([[x[0], x[1], x[4], x[5], x[6], x[2]] for x in s["shorts"]],
                                                            key=lambda r: (r[0], r[1]))
            elif part == "legatopitches" and s.get("legatoPitches"):
                pitches.setdefault(patch, {})[name] = {
                    "range": s.get("legatoRange"),
                    "rows": [[l["start"], l["interval"], l["leaveMs"], l["midMs"], l["arriveMs"], l["dipDb"]] for l in s["legatoPitches"]]}
            elif part == "legatolengths" and s.get("legatoLengths"):
                legato.setdefault(patch, {})[name] = [[l["firstMs"], l["interval"], l["leaveMs"], l["arriveMs"], l["dipDb"]]
                                                      for l in s["legatoLengths"]]
    for f, d in (("sso_sound_onset.json", onset), ("sso_short_lengths.json", shorts), ("sso_legato_lengths.json", legato),
                 ("sso_legato_grid_pitches.json", pitches)):
        if d:
            write(os.path.join(a.dir, f), d)

    # summary: mf onsets (perceived, -10 dB) per patch, median over its sounds and pitches
    for patch, per in sorted(onset.items()):
        xs = [r[2][3] for rows in per.values() for r in rows if r[2] and r[2][3] >= 0]
        x20 = [r[2][0] for rows in per.values() for r in rows if r[2] and r[2][0] >= 0]
        if xs:
            print(f"onset {patch}: mf perceived within 10 dB after {statistics.median(xs):.0f} ms, 20 dB {statistics.median(x20):.0f} ms "
                  f"(median of {len(xs)} notes)")
    for patch, per in sorted(shorts.items()):
        for name, rows in sorted(per.items()):
            line = " ".join(f"{r[1]:.0f}:{r[3][1]:.0f}" for r in rows if r[0] == rows[0][0])
            print(f"short {patch} / {name}: held:perceived -10 dB end {line}")
    for patch, per in sorted(pitches.items()):
        for name, v in per.items():
            by = {}
            for start, interval, leave, mid, arr, dip in v["rows"]:
                if mid >= 0:
                    by.setdefault(start, []).append(mid)
            print(f"legato from pitches {patch} / {name}: range {v['range']}, mid " +
                  ", ".join(f"from {p}: {statistics.median(x):.0f}" for p, x in sorted(by.items())))
    for patch, per in sorted(legato.items()):
        for name, rows in per.items():
            by = {}
            for first, interval, leave, arr, dip in rows:
                if arr >= 0:
                    by.setdefault(first, []).append(arr)
            print(f"legato {patch} / {name}: arrives " + ", ".join(f"after {f:.0f} ms: {statistics.median(v):.0f}" for f, v in sorted(by.items())))


if __name__ == "__main__":
    main()
