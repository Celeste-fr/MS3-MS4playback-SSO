#!/usr/bin/env python3
"""Positive controls for the heard check (VERIFY.md › Positive controls; OPEN_ITEMS.md › tools, 13): a known
fault planted in a heard-check run that is already on disk, the analysis rerun, and the findings compared with the
unplanted run's. A check that can't be shown to catch a fault of known size proves nothing.

  heard_control.py <dump> <wavs> [--export] [--patch P] [--json f]
        <dump>: tst_playbackaudit auditScore's MS_AUDIT_DUMP folder (predicted.tsv ...); <wavs>: the renders
        (kthost: <wavs>/<route>.wav; --export: MuseScore's --verify-playback folder, "<score> route <code>.wav" and its
        events.tsv). Prints each control's result; exit 0 when every control passes, 1 when one fails, 2 when a control
        found nothing to plant on.

The plants (each against the same unplanted run, the null):
  model     the audit's predicted heard time (predicted.tsv's arrivalMs) of every legato transition of one patch
            (--patch, default: the patch with the most transitions measured) moved later by MODEL_SHIFTS_MS (+40, +80).
            The sound is untouched, so: that patch's transition situations' model drift (median, p10, p90, the
            Kaplan-Meier median) moves by exactly -shift (within DRIFT_TOL_MS, the report's rounding); every other drift
            row, every note's heard time and every finding (ARRIVAL, RHYTHM, LEVEL, UNHEARD: none reads the model)
            stay the same.
  level     one route's sound LEVEL_DB (3 dB) quieter from LEVEL_LEAD_MS before one note's heard time to its end (a step
            down, not a bump: a level is a span's loudest value, so only the planted note's step can change; pick_level_note
            has why not up), the note a legato transition read by pitch whose step from the note before is judged (the
            audit's LEVEL pairs). Expected: that step -3 dB off the null's within LEVEL_TOL_DB (1 dB), flagged LEVEL where
            it then lies clearly over 0.69 dB; every note heard after the cut on the route 3 dB quieter, the others as in
            the null (levels within LEVEL_OTHER_DB, the note before within LEVEL_NEAR_DB; heard times within
            HEARD_TOL_MS, the planted note and the one before within HEARD_NEAR_MS); every other finding as in the null.
The ARRIVAL check against a note heard late (the sound moved, not the model) has its controls on synthetic audio in
test_heard_check.py (PlantedControls, KnownAnswer): a real render can't be retimed in one place without a seam.

Known answer on a kept run (2026-11-01): K. 550 i bars 1-9, the five string sections, build 524's export on real SSO
(the score and renders private, ~/MuseScore/ms3fork/private/positive-controls/k550A): VERIFY.md › Positive controls.
"""
import argparse
import glob
import json
import os
import shutil
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import heard_check as H  # noqa: E402

MODEL_SHIFTS_MS = (40.0, 80.0)
DRIFT_TOL_MS = 0.15             # the report rounds to 0.1 ms
LEVEL_DB = 3.0                  # planted as a step down (pick_level_note: a step up can move the readings before it)
PEAK_MARGIN_DB = 1.0
LEVEL_LEAD_MS = 120.0           # the level cut this long before the planted note's heard time: inside its glide, so the
                                # whole note is lowered (its level is its span's loudest value, at its arrival on K. 550: a cut
                                # 45-90 ms after it changed nothing; a cut at or just after it moved pYIN's reading 3-6 ms;
                                # 60 ms before: the envelope's 50 ms release still 0.6 dB short at the span's start)
LEVEL_RAMP_MS = 5.0
LEVEL_TOL_DB = 1.0
LEVEL_OTHER_DB = 0.05
LEVEL_NEAR_DB = 1.0             # the note before: its span's last ~140 ms see the cut through the envelope's window and
                                # release (K. 550: -0.67 dB; the synthetic flat slurs -0.21)
HEARD_TOL_MS = 0.5
HEARD_NEAR_MS = 10.0            # the planted note and the note before: the level step 60 ms before the arrival moves their
                                # readings a few ms (K. 550: the attack before 5 ms; pYIN and the -15 dB rule see the edge),
                                # a third of ARRIVAL's 30 ms warning
FINDING_TOL = dict(ARRIVAL=HEARD_NEAR_MS * 2, RHYTHM=0.02, LEVEL=LEVEL_OTHER_DB * 2, UNHEARD=1e9)
TRANSITION_SITUATIONS = ("transition after attack", "transition after transition")


# ------------------------------------------------------------------------------------------------------------- loading

def loader(wavs, export, plant=None):
    """load(route) -> (x, sr) as analyse's own loader reads the renders; plant(route, x, sr) -> x changes one"""
    import soundfile as sf
    where = H.read_export_ons(wavs)[1] if export else {}

    def load(route):
        if export:
            f = glob.glob(os.path.join(where.get(route, wavs), "* route %s.wav" % route.split("/")[-1]))
            f = f[0] if f else ""
        else:
            f = os.path.join(wavs, route + ".wav")
        if not os.path.exists(f):
            return None
        x, sr = sf.read(f, always_2d=False)
        if plant:
            x = plant(route, x, sr)
        return x, sr
    return load


def analyse(dump, wavs, export, plant=None):
    return H.analyse(dump, wavs, load=loader(wavs, export, plant), export=export)


# -------------------------------------------------------------------------------------------------------------- plants

def plant_model(dump, out, patch, shift_ms):
    """a copy of the dump with every legato transition of patch predicted shift_ms later (arrivalMs); the rows changed"""
    if os.path.exists(out):
        shutil.rmtree(out)
    shutil.copytree(dump, out)
    f = os.path.join(out, "predicted.tsv")
    lines = H._read(f).splitlines()
    head = lines[0].split("\t")
    ip, it, ia, ih = (head.index(k) for k in ("patch", "transition", "arrivalMs", "hasArrival"))
    changed = 0
    for i in range(1, len(lines)):
        c = lines[i].split("\t")
        if c[ip] == patch and int(c[it]) and int(c[ih]):
            c[ia] = "%.3f" % (float(c[ia]) + shift_ms)
            lines[i] = "\t".join(c)
            changed += 1
    H._write(f, "\n".join(lines) + "\n")
    return changed


def gain_plant(route, cut_ms, db, ramp_ms=LEVEL_RAMP_MS):
    """plant(route, x, sr): the route's sound db louder from cut_ms (render ms) to its end, after a ramp_ms ramp
    (linear in dB); one level change, not a bump, so nothing after it changes but its level"""
    def plant(r, x, sr):
        if r != route:
            return x
        t = np.arange(len(x)) / sr * 1000.0
        g = 10 ** (db * np.clip((t - cut_ms) / ramp_ms, 0.0, 1.0) / 20)
        return x * (g[:, None] if x.ndim == 2 else g)
    return plant


# ------------------------------------------------------------------------------------------------------------- compare

def note_key(n):
    return (n["part"], n["written"], n["pitch"])


def finding_key(f):
    return (f["kind"], f["severity"], f["part"], f["bar"], f["beat"], f["pitch"], f.get("group"), f["value"])


def same_findings(a, b):
    """failures where finding lists a and b differ: each of a matched in b by kind, severity, place and group, its
    value within FINDING_TOL"""
    def k(f):
        return finding_key(f)[:-1]
    left = list(b)
    bad = []
    for f in a:
        m = [g for g in left if k(g) == k(f) and abs(g["value"] - f["value"]) <= FINDING_TOL[f["kind"]]]
        if m:
            left.remove(m[0])
        else:
            bad.append("a finding gone or changed: %s" % (finding_key(f),))
    bad += ["a new finding: %s" % (finding_key(g),) for g in left]
    return bad


def drift_rows(res):
    return {(d["patch"], d["situation"]): d for d in res["drift"]}


def render_ms(res, n):
    """a note's heard time on its render's timeline (ms), as analyse placed it"""
    offset = H.EXPORT_OFFSET_S if res["renderer"] == "export" else H.OFFSET_S
    shifts = (res.get("pairing") or {}).get("shiftsMs") or [0.0]
    return n["heardMs"] + (n["written"] + offset) * 1000 + res["lead"] + (shifts[0] if len(shifts) == 1 else 0.0)


def level_pairs(dump, res):
    """the audit's LEVEL pairs as analyse judges them: (note before, note) for each slurred step at an unchanged
    dynamic, top notes of each line's chords, both levels measured; notes as in res"""
    rows = H.read_rows(dump)
    notes = {note_key(n): n for n in res["notes"]}
    lines = {}
    for r in rows:
        if r["grace"] or r["sub"]:
            continue
        lines.setdefault((r["part"], r["track"]), {}).setdefault(round(r["written"], 6), []).append(r)
    out = []
    for g in lines.values():
        seq = [g[t] for t in sorted(g)]
        for a, b in zip(seq, seq[1:]):
            ta, tb = max(a, key=lambda r: r["pitch"]), max(b, key=lambda r: r["pitch"])
            if not tb["slurredBefore"] or tb["dynamicBefore"]:
                continue
            na = notes.get((ta["part"], round(ta["written"], 4), ta["pitch"]))
            nb = notes.get((tb["part"], round(tb["written"], 4), tb["pitch"]))
            if na and nb and na["levelDb"] is not None and nb["levelDb"] is not None:
                out.append((na, nb))
    return out


# ------------------------------------------------------------------------------------------------------------ controls

def pick_patch(res):
    """the patch with the most transitions measured (drift rows' n)"""
    count = {}
    for d in res["drift"]:
        if d["situation"] in TRANSITION_SITUATIONS:
            count[d["patch"]] = count.get(d["patch"], 0) + d["n"]
    return max(sorted(count), key=lambda p: count[p]) if count else None


def check_model(null, planted, patch, shift):
    """the model control's failures (empty: passed)"""
    bad = []
    dn, dp = drift_rows(null), drift_rows(planted)
    if set(dn) != set(dp):
        bad.append("drift rows differ: %s" % sorted(set(dn) ^ set(dp)))
    hit = 0
    for k in sorted(set(dn) & set(dp)):
        a, b = dn[k], dp[k]
        if k[0] == patch and k[1] in TRANSITION_SITUATIONS:
            hit += 1
            for f in ("medianMs", "p10Ms", "p90Ms", "kmMedianMs"):
                if (a[f] is None) != (b[f] is None):
                    bad.append("%s %s: %s %s -> %s" % (k, f, "set" if a[f] is not None else "None", a[f], b[f]))
                elif a[f] is not None and abs((b[f] - a[f]) + shift) > DRIFT_TOL_MS:
                    bad.append("%s %s moved %+.1f ms, not %+.1f" % (k, f, b[f] - a[f], -shift))
            if (a["n"], a["censored"]) != (b["n"], b["censored"]):
                bad.append("%s n / censored changed" % (k,))
        elif a != b:
            bad.append("an unplanted drift row changed: %s %s -> %s" % (k, a, b))
    if not hit:
        bad.append("no transition drift row of %s" % patch)
    if sorted(map(finding_key, null["findings"])) != sorted(map(finding_key, planted["findings"])):
        bad.append("the findings changed (none reads the model)")
    hn = {note_key(n): n["heardMs"] for n in null["notes"]}
    if any(hn.get(note_key(n)) != n["heardMs"] for n in planted["notes"]):
        bad.append("a heard time changed")
    return bad, hit


def pick_level_note(dump, null, db=-LEVEL_DB):
    """(note, the note before, the cut in render ms): a legato transition read by pitch (its heard time can't move with
    the level), its step from the note before judged (level_pairs), the cut LEVEL_LEAD_MS before its heard time (inside
    its glide). Only a step down (db < 0): a level is the loudest value of a span and an attack is read -15 dB under its
    slur's loudest value, so a step up could move readings before the cut (on synthetic flat slurs it moved the slur's
    attack 5 ms and put the step on the note before); a step down can't raise what is before it. The note before must
    not be read from the lowered stretch: a transition read by pitch, or an attack whose own span's loudest value lies
    PEAK_MARGIN_DB above the planted note's. Preferred: the null has no LEVEL finding there, then the fewest notes after it
    on its route (the fewest steps the plant moves as a whole), then the earliest"""
    if db >= 0:
        raise ValueError("the level control plants a step down (db < 0)")
    pairs = level_pairs(dump, null)
    flagged = {(f["part"], f["bar"], f["beat"], f["pitch"]) for f in null["findings"] if f["kind"] == "LEVEL"}
    best = None
    for a, b in pairs:
        if not b["transition"] or b["how"] != "pitch":
            continue
        if not (a["transition"] and a["how"] == "pitch") and a["levelDb"] < b["levelDb"] + PEAK_MARGIN_DB:
            continue
        h = render_ms(null, b)
        later = sum(1 for n in null["notes"] if n["route"] == b["route"] and n["heardMs"] is not None and render_ms(null, n) > h + 0.5)
        rank = ((b["part"], b["bar"], b["beat"], b["pitch"]) in flagged, later, h)
        if best is None or rank < best[0]:
            best = (rank, (b, a, h - LEVEL_LEAD_MS))
    return best[1] if best else None


def check_level(dump, null, planted, note, before, cut, db):
    """the level control's failures (empty: passed) and the planted note's step in the null and planted. Expected: on
    the planted route every note heard after the cut db louder, every other note as in the null (heard times within
    HEARD_TOL_MS, levels within LEVEL_OTHER_DB); each judged step moved by db x (its note after the cut - the note
    before after the cut) within LEVEL_TOL_DB (on one route: only the planted note's); LEVEL flagged where the
    expected step lies clearly (LEVEL_TOL_DB) over the threshold, not where clearly under; every other finding as in
    the null"""
    bad = []
    pn = {note_key(n): n for n in planted["notes"]}

    def after(n):
        return n["route"] == note["route"] and n["heardMs"] is not None and render_ms(null, n) > cut + 1.0
    near = {note_key(note), note_key(before)}
    for n in null["notes"]:
        q = pn[note_key(n)]
        tol = HEARD_NEAR_MS if note_key(n) in near else HEARD_TOL_MS
        if (n["heardMs"] is None) != (q["heardMs"] is None) or (n["heardMs"] is not None and abs(n["heardMs"] - q["heardMs"]) > tol):
            bad.append("a heard time changed: %s %s -> %s" % (note_key(n), n["heardMs"], q["heardMs"]))
            continue
        want = db if after(n) else 0.0
        ltol = LEVEL_TOL_DB if want else (LEVEL_NEAR_DB if note_key(n) == note_key(before) else LEVEL_OTHER_DB)
        if (n["levelDb"] is None) != (q["levelDb"] is None) or (
                n["levelDb"] is not None and abs(q["levelDb"] - n["levelDb"] - want) > ltol):
            bad.append("level %s %s -> %s, expected %+.1f" % (note_key(n), n["levelDb"], q["levelDb"], want))
    moved = {}
    step0 = step = None
    for a, b in level_pairs(dump, null):
        qa, qb = pn[note_key(a)], pn[note_key(b)]
        if qa["levelDb"] is None or qb["levelDb"] is None:
            continue
        d = db * (after(b) - after(a))
        s0, s1 = b["levelDb"] - a["levelDb"], qb["levelDb"] - qa["levelDb"]
        if note_key(b) == note_key(note):
            step0, step = s0, s1
        tol = LEVEL_TOL_DB if d else (LEVEL_NEAR_DB + LEVEL_OTHER_DB if note_key(b) == note_key(before) else 2 * LEVEL_OTHER_DB)
        if abs(s1 - s0 - d) > tol:
            bad.append("the step at %s %+.2f -> %+.2f dB, expected %+.1f" % (note_key(b), s0, s1, d))
        if d:
            moved[(b["part"], b["bar"], b["beat"], b["pitch"])] = s0 + d
    if step is None:
        bad.append("the planted note's step not measured")
    loc = lambda f: (f["part"], f["bar"], f["beat"], f["pitch"])  # noqa: E731

    def rest(res):
        return sorted((f for f in res["findings"] if not (f["kind"] == "LEVEL" and loc(f) in moved)), key=finding_key)
    bad += same_findings(rest(null), rest(planted))
    for k, want in moved.items():
        hit = any(f["kind"] == "LEVEL" and loc(f) == k for f in planted["findings"])
        if abs(abs(want) - H.LEVEL_DL_DB) > LEVEL_TOL_DB and (abs(want) > H.LEVEL_DL_DB) != hit:
            bad.append("LEVEL at %s %s, its step expected about %+.2f dB" % (k, "flagged" if hit else "not flagged", want))
    return bad, step0, step


def run(dump, wavs, export=False, patch=None, out=None, null=None, level_db=-LEVEL_DB):
    """every control; {"controls": [...], "passed": bool}. null: the unplanted analysis if already made"""
    out_json = dict(controls=[])
    null = null or analyse(dump, wavs, export)
    tmp = tempfile.mkdtemp(prefix="heard-control-")
    try:
        patch = patch or pick_patch(null)
        if patch is None:
            out_json["controls"].append(dict(control="model", passed=None, why="no transition measured"))
        for shift in MODEL_SHIFTS_MS:
            if patch is None:
                break
            d = os.path.join(tmp, "model%+d" % shift)
            changed = plant_model(dump, d, patch, shift)
            res = analyse(d, wavs, export)
            bad, hit = check_model(null, res, patch, shift)
            dp = {k: v for k, v in drift_rows(res).items() if k[0] == patch and k[1] in TRANSITION_SITUATIONS}
            dn = drift_rows(null)
            out_json["controls"].append(dict(
                control="model", patch=patch, shiftMs=shift, rowsChanged=changed, passed=not bad, failures=bad,
                drift=[dict(situation=k[1], n=v["n"], nullMedianMs=dn[k]["medianMs"], medianMs=v["medianMs"],
                            nullKmMs=dn[k]["kmMedianMs"], kmMs=v["kmMedianMs"], nullDrifting=dn[k]["drifting"],
                            drifting=v["drifting"]) for k, v in sorted(dp.items())]))
        pick = pick_level_note(dump, null, level_db)
        if pick is None:
            out_json["controls"].append(dict(control="level", passed=None, why="no slurred transition to plant on"))
        else:
            note, before, cut = pick
            res = analyse(dump, wavs, export, gain_plant(note["route"], cut, level_db))
            bad, step0, step = check_level(dump, null, res, note, before, cut, level_db)
            # (the findings not in the null within FINDING_TOL: the planted step's)
            left = list(res["findings"])
            for f in null["findings"]:
                m = [g for g in left if finding_key(g)[:-1] == finding_key(f)[:-1]
                     and abs(g["value"] - f["value"]) <= FINDING_TOL[f["kind"]]]
                if m:
                    left.remove(m[0])
            new = left
            out_json["controls"].append(dict(
                control="level", db=level_db, part=note["part"], bar=note["bar"], beat=note["beat"], pitch=note["pitch"],
                patch=note["patch"], route=note["route"], fromMs=round(cut, 1),
                nullStepDb=round(step0, 2) if step0 is not None else None, stepDb=round(step, 2) if step is not None else None, newFindings=[dict(kind=f["kind"], severity=f["severity"],
                value=f["value"], part=f["part"], bar=f["bar"], beat=f["beat"], pitch=f["pitch"]) for f in new],
                passed=not bad, failures=bad))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    out_json["nullFindings"] = len(null["findings"])
    out_json["passed"] = all(c["passed"] for c in out_json["controls"])
    if out:
        H._write(out, json.dumps(out_json, indent=1))
    return out_json


def text(res):
    out = []
    for c in res["controls"]:
        state = "PASS" if c["passed"] else ("FAIL" if c["passed"] is False else "NOTHING TO PLANT ON")
        if c["control"] == "model" and "patch" in c:
            out.append("%s model +%.0f ms on %s's transitions (%d rows):" % (state, c["shiftMs"], c["patch"], c["rowsChanged"]))
            for d in c["drift"]:
                km = lambda v: "higher" if v is None else "%+.1f" % v  # noqa: E731
                out.append("     %-28s n %3d  median %+7.1f -> %+7.1f  KM %s -> %s  %s -> %s" % (
                    d["situation"], d["n"], d["nullMedianMs"], d["medianMs"], km(d["nullKmMs"]), km(d["kmMs"]),
                    "DRIFT" if d["nullDrifting"] else "ok", "DRIFT" if d["drifting"] else "ok"))
        elif c["control"] == "level" and "part" in c:
            out.append("%s level %+.1f dB on %s bar %d beat %.2f pitch %d (%s, route %s from %.0f ms): step %s -> %s dB; new: %s" % (
                state, c["db"], c["part"], c["bar"], c["beat"], c["pitch"], c["patch"], c["route"], c["fromMs"],
                c["nullStepDb"], c["stepDb"], ", ".join("%s %s %+.2f bar %d beat %.2f" % (f["kind"], f["severity"], f["value"],
                                                                                          f["bar"], f["beat"]) for f in c["newFindings"]) or "none"))
        else:
            out.append("%s %s: %s" % (state, c["control"], c.get("why", "")))
        for b in c.get("failures", []):
            out.append("     " + b)
    out.append("%s (the null: %d findings)" % ("ALL CONTROLS PASS" if res["passed"] else "A CONTROL FAILED", res["nullFindings"]))
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("dump")
    ap.add_argument("wavs")
    ap.add_argument("--export", action="store_true")
    ap.add_argument("--patch")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    res = run(a.dump, a.wavs, a.export, a.patch, a.json)
    sys.stdout.write(text(res))
    if any(c["passed"] is None for c in res["controls"]):
        return 2
    return 0 if res["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
