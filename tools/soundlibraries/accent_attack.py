#!/usr/bin/env python3
"""accent_attack.py: the producer of sso_accent_attack.json (gen_spitfire_sso.py -> accentedLevel=), re-runnable.

How much louder SSO's Performance legato plays its accented attack (velocity 122) than its spiccato attack (64), per
played pitch and note length, at the real players' level. The method of 2026-10-21 (branch accent-level; private
scripts mkfull.py / anafull2.py, folded in here):

  accent_attack.py events <work> [--sections v1,v2,va,vc,cb] [--rounds 2] [--cc11 cb=64 ...]
      kthost event files <work>/<sec>.txt (CC1 64, CC11 the section's value, then every semitone of the section's range x
      lengths 100 150 211 300 450 ms x rounds, ABBA: 64 122 / 122 64, 1.5 s of silence between notes), <work>/list.txt
      "<sec>|<patch>|<probe pitch>" and <work>/plan.tsv (every note's time, pitch, velocity, round, length)
  (VM) kthost renders of each list.txt row with MuseScore's resaved setup of its patch, e.g. through tools/vm/vmjob.py:
      cp tools/vm/kthost_batch.ps1 <work>/ && tools/vm/vmjob.py --name accent --fetch wav --kill kthost.exe <work> -- \\
          powershell -NoProfile -ExecutionPolicy Bypass -File kthost_batch.ps1 -run {run}
      (renders <work>/wav/<sec>.wav; then fetched into <work>.out/wav)
  accent_attack.py analyse <wav dir> <work> <out.json> [--target 59.3] [--about-from tools/soundlibraries/sso_accent_attack.json]
      per section: the render scaled by a gain that puts the 64's median perceived peak at 211 ms at --target (59.3
      loudness units: the median of the real players' unaccented neighbours at f, Spheres K. 550), found by iteration
      within 0.2; per pitch and length the rounds' mean of 122 - 64 (perceived peak, tools/playbackverify/loudness.py, in
      the 0.4 s after the note-on), level64 the 64's mean; a key where any note is 30 units under target dropped (outside
      the patch's samples). Writes sso_accent_attack.json's format: {"_about", "sections": {patch: {cc11, gain, byPitch:
      {pitch: {ms: {db, level64}}}}}} (the _about copied from --about-from when given)
"""
import argparse
import json
import os
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "playbackverify"))

# (key, patch, CC11 (Whence bar 8's staccato value; the Basses 64 as in the shipped json), the range measured)
SECTIONS = [("v1", "Violins 1 - Performance", 41, range(55, 99)),
            ("v2", "Violins 2 - Performance", 37, range(55, 97)),
            ("va", "Violas - Performance", 86, range(48, 89)),
            ("vc", "Celli - Performance", 70, range(36, 80)),
            ("cb", "Basses - Performance", 64, range(24, 61))]
LENS_MS = (100, 150, 211, 300, 450)
GAP_S = 1.5
TARGET = 59.3


def events(work, sections=None, rounds=2, cc11=None):
    os.makedirs(work, exist_ok=True)
    cc11 = cc11 or {}
    lst, plan = [], ["sec\tpatch\tt\tpitch\tvelocity\tround\tms"]
    for key, patch, cc, pitches in SECTIONS:
        if sections and key not in sections:
            continue
        cc = cc11.get(key, cc)
        ev, t = ["0.000000 cc 1 64", "0.000000 cc 11 %d" % cc], 1.0
        for p in pitches:
            for ms in LENS_MS:
                for rnd in range(rounds):
                    for v in ((64, 122) if rnd % 2 == 0 else (122, 64)):
                        ev.append("%.6f on %d %d" % (t, p, v))
                        ev.append("%.6f off %d 0" % (t + ms / 1000, p))
                        plan.append("%s\t%s\t%.6f\t%d\t%d\t%d\t%d\t%d" % (key, patch, t, p, v, rnd, ms, cc))
                        t += ms / 1000 + GAP_S
        with open(os.path.join(work, key + ".txt"), "w", newline="\r\n") as f:
            f.write("\n".join(ev) + "\n")
        lst.append("%s|%s|%d" % (key, patch, pitches[len(pitches) // 2]))
    plan[0] += "\tcc11"
    with open(os.path.join(work, "list.txt"), "w", newline="\r\n") as f:
        f.write("\n".join(lst) + "\n")
    with open(os.path.join(work, "plan.tsv"), "w") as f:
        f.write("\n".join(plan) + "\n")
    return len(plan) - 1


def peaks(x, sr, rows, gain_db, envelope):
    out = {}
    g = 10 ** (gain_db / 20)
    for row in rows:
        t = float(row[2])
        s0, s1 = max(0, int((t - 0.3) * sr)), int((t + 0.5) * sr)
        env, first, hop = envelope(x[s0:s1] * g, sr)
        i0, i1 = int((300 - first) / hop), int((700 - first) / hop)
        out[tuple(row)] = float(env[max(0, i0):max(i0 + 1, i1)].max())
    return out


def analyse_section(x, sr, rows, target=TARGET, envelope=None):
    """(gain dB, {(pitch, ms): {db, level64, rounds}}, dropped keys)"""
    if envelope is None:
        from loudness import perceived_envelope as envelope
    probe = [r for r in rows if r[6] == "211" and r[4] == "64"]
    # (keys outside the patch's samples, near silence, left out of the gain's median)
    p0 = peaks(x, sr, probe, 0.0, envelope)
    top = max(p0.values()) if p0 else 0.0
    probe = [r for r in probe if p0[tuple(r)] > top - 30]
    g, m = 0.0, None
    for it in range(8):
        m = st.median(peaks(x, sr, probe, g, envelope).values())
        if abs(m - target) < 0.2:
            break
        g += (target - m) / (1.0 if it == 0 else 1.2)
    pk = peaks(x, sr, rows, g, envelope)
    res = {}
    for row, v in pk.items():
        res.setdefault((int(row[3]), int(row[6])), {}).setdefault(int(row[5]), {})[int(row[4])] = v
    out, dropped = {}, []
    for (p, ms), rr in sorted(res.items()):
        l64 = [r[64] for r in rr.values() if 64 in r]
        l122 = [r[122] for r in rr.values() if 122 in r]
        if not l64 or not l122 or min(l64 + l122) < target - 30:
            dropped.append((p, ms))
            continue
        dd = [r[122] - r[64] for r in rr.values() if 64 in r and 122 in r]
        out[(p, ms)] = dict(db=round(st.mean(dd), 2), level64=round(st.mean(l64), 2), rounds=[round(v, 2) for v in dd])
    return round(g, 2), out, dropped


def analyse(wavdir, work, out_json, target=TARGET, about_from=None):
    import soundfile as sf
    with open(os.path.join(work, "plan.tsv")) as f:
        rows = [ln.split("\t") for ln in f.read().splitlines()[1:]]
    res = {"_about": "", "sections": {}}
    if about_from and os.path.exists(about_from):
        with open(about_from) as f:
            res["_about"] = json.load(f).get("_about", "")
    for key in sorted({r[0] for r in rows}, key=lambda k: [s[0] for s in SECTIONS].index(k) if k in [s[0] for s in SECTIONS] else 9):
        sel = [r for r in rows if r[0] == key]
        x, sr = sf.read(os.path.join(wavdir, key + ".wav"))
        g, by, dropped = analyse_section(x, sr, sel, target)
        patch = sel[0][1]
        bp = {}
        for (p, ms), v in by.items():
            bp.setdefault(str(p), {})[str(ms)] = dict(db=v["db"], level64=v["level64"])
        res["sections"][patch] = dict(cc11=int(sel[0][7]) if len(sel[0]) > 7 else None, gain=g, byPitch=bp)
        print("%-26s gain %+.2f dB, %d keys, dropped %s" % (patch, g, len(bp), dropped))
    with open(out_json, "w") as f:
        json.dump(res, f, indent=1, sort_keys=True)
    return res


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events")
    e.add_argument("work")
    e.add_argument("--sections")
    e.add_argument("--rounds", type=int, default=2)
    e.add_argument("--cc11", action="append", default=[], help="sec=value")
    a_ = sub.add_parser("analyse")
    a_.add_argument("wavs")
    a_.add_argument("work")
    a_.add_argument("out")
    a_.add_argument("--target", type=float, default=TARGET)
    a_.add_argument("--about-from")
    a = ap.parse_args(argv)
    if a.cmd == "events":
        cc = dict((k, int(v)) for k, v in (x.split("=", 1) for x in a.cc11))
        n = events(a.work, a.sections.split(",") if a.sections else None, a.rounds, cc)
        print(n, "notes")
        return 0
    analyse(a.wavs, a.work, a.out, a.target, a.about_from)
    return 0


if __name__ == "__main__":
    sys.exit(main())
