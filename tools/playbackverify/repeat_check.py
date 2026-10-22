#!/usr/bin/env python3
"""repeat_check.py: render one score N times and report any difference between the renders, by route / slot and note.

Renders must repeat: with a byte-identical score, every A/B, null test and heard check assumes the same events. On build
#458 Whence's Violins 1 chord voices went onto different slots from one export to the next (92 notes on one slot in one
take, 59/23/10 in another, the Linux dump 63/18/10/1: OPEN_ITEMS.md item 1); this finds such a thing.

  repeat_check.py linux <score> [--settings recommended|library|"id=v;..."] [--n 5] [--tool audit|dump] [--chunked]
                  [--map <xml>] [--calibration <json>] --run <wrapper> -o <dir>
      N renders in fresh processes through <wrapper> (a script running a command string in the build dir, e.g.
      ~/MuseScore/ms3fork/ovl-<branch>/<xx>.sh): --tool audit (default): tst_playbackaudit auditScore with MS_AUDIT_DUMP
      (presets as the heard check plays them); dump: tst_liveequivalence dumpEvents (MS_DUMP_SETTINGS a metaTag; a preset
      id is expanded from libmscore/playbacksettings.cpp; --chunked as playback renders it, 10-measure chunks; also
      live.txt). Each in <dir>/run-<i>; then every run compared with run-1 (dump_compare.py: routes by part, patch and
      lane, and by route code) and each run's note-ons per lane. --free-memory-mb N pins MS_FREE_MEMORY_MB (the free
      memory the tuning / voice copies count, SoundLib::memoryMaxLanes: OPEN_ITEMS item 1's cause) in both modes.
  repeat_check.py vm <score.mscz|.mscx> [--settings ...] [--part "Violins 1"] [--n 3] [--fixed-transitions]
                  [--exe C:\\claude\\bpt\\ms\\bin\\MuseScore3Evo.exe] [--library "Spitfire Symphony Orchestra"] -o <dir>
      the score's export copy (heard_check.py score: its playbackSettings as the dump plays them; --part: only that part
      on the library) rendered N times on the VM in one locked job (tools/vm/vmjob.py: --verify-playback --verify-wav,
      MS_VERIFY_ROUTES=1, each route alone as File > Export audio renders it), each WAV's SHA-256 taken on the VM and
      the WAVs deleted there; fetched: each run's events.tsv and summary. Compared: note-ons per slot, the event lists,
      the WAV hashes per route. --fixed-transitions adds slurs/fixedTransitions=1 so that Performance legato can null.
  repeat_check.py compare <dir>        the comparison again from an earlier run's folder (linux or vm)

Report: <dir>/repeat.txt and repeat.json: per run against run-1 IDENTICAL or the first differing event per route /
slot, the per-run lane or slot counts (DIFFERS marked). Exit 0 when every run matches run-1, 1 when any differs, 2 on
a setup error. (An ASan / valgrind run needs a build of its own: give its wrapper with --run.)
"""
import argparse
import collections
import glob
import json
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "vm"))
import dump_compare as DC  # noqa: E402

MAP = os.path.join(ROOT, "share", "soundlibraries", "Spitfire Symphony Orchestra.xml")
CAL = os.path.join(ROOT, "share", "soundlibraries", "Spitfire Symphony Orchestra.dynamics.json")


def expand(settings):
    if "=" in settings:
        return settings
    import heard_check as H
    return ";".join(H.preset_values(settings, ROOT))


# ---------------------------------------------------------------------------------------------------------------- linux

def linux_script(a, i, out):
    score = os.path.abspath(a.score)
    mem = "MS_FREE_MEMORY_MB=%d " % a.free_memory_mb if a.free_memory_mb else ""
    if a.tool == "audit":
        env = "MS_AUDIT_SCORE=%s MS_AUDIT_SETTINGS=%s MS_AUDIT_DUMP=%s MS_AUDIT_OUT=%s" % (
            shlex.quote(score), shlex.quote(a.settings), shlex.quote(out), shlex.quote(os.path.join(out, "audit.txt")))
        if a.map:
            env += " MS_AUDIT_MAP=%s" % shlex.quote(a.map)
        return "cd mtest/libmscore/playbackaudit || exit 2\n" + mem + "%s QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore > %s 2>&1\n" % (
            env, shlex.quote(os.path.join(out, "run.log")))
    env = "MS_DUMP_SCORE=%s MS_DUMP_MAP=%s MS_DUMP_OUT=%s MS_DUMP_SETTINGS=%s MS_DUMP_CALIBRATION=%s" % (
        shlex.quote(score), shlex.quote(a.map or MAP), shlex.quote(out), shlex.quote(expand(a.settings)),
        shlex.quote(a.calibration or CAL))
    if a.chunked:
        env += " MS_DUMP_CHUNKED=1"
    return "cd mtest/libmscore/liveequivalence || exit 2\n" + mem + "%s QT_QPA_PLATFORM=offscreen ./tst_liveequivalence dumpEvents > %s 2>&1\n" % (
        env, shlex.quote(os.path.join(out, "run.log")))


def cmd_linux(a):
    os.makedirs(a.out, exist_ok=True)
    for i in range(1, a.n + 1):
        d = os.path.join(a.out, "run-%d" % i)
        os.makedirs(d, exist_ok=True)
        sp = os.path.join(a.out, "run-%d.sh" % i)
        with open(sp, "w") as f:
            f.write(linux_script(a, i, d))
        print("== run %d of %d" % (i, a.n), flush=True)
        subprocess.call([a.run, "sh %s" % sp])
        if not glob.glob(os.path.join(d, "* museScore.txt")):
            print("run %d wrote no events (see %s)" % (i, os.path.join(d, "run.log")))
            return 2
    with open(os.path.join(a.out, "repeat-run.json"), "w") as f:
        json.dump(dict(mode="linux", score=a.score, settings=a.settings, tool=a.tool, n=a.n), f)
    return compare_dir(a.out)


# ------------------------------------------------------------------------------------------------------------------- vm

VM_CMD = r"""@echo off
rem repeat_check.py's VM side: N renders of score.mscz, WAV hashes, WAVs deleted
setlocal
set "RUN=%~dp0"
set "RUN=%RUN:~0,-1%"
rem (one share login for the run, as heard_export_vm.cmd: the library's samples)
if exist C:\claude\share.txt (
  set /p PW=<C:\claude\share.txt
  call net use \\VOLENS\VMShare /user:VOLENS\vmshare %%PW%% >nul 2>&1
  set PW=
)
set MS_VERIFY_ROUTES=1
{memory}for /l %%i in (1,1,{n}) do call :one %%i
if exist C:\claude\share.txt net use \\VOLENS\VMShare /delete /y >nul 2>&1
endlocal
exit /b 0
:one
mkdir "%RUN%\out\run-%1"
echo run %1 start %time%>>"%RUN%\run.out"
"{exe}" --verify-library "{library}" --verify-playback "%RUN%\score.mscz" --verify-out "%RUN%\out\run-%1" --verify-wav > "%RUN%\out\run-%1\stdout.txt" 2> "%RUN%\out\run-%1\stderr.txt"
echo run %1 exit %ERRORLEVEL% %time%>>"%RUN%\run.out"
for /r "%RUN%\out\run-%1" %%w in (*.wav) do (certutil -hashfile "%%w" SHA256 | findstr /v /c:"hash" /c:"CertUtil" > "%%w.sha256" & del "%%w")
exit /b 0
"""


def cmd_vm(a):
    import heard_check as H
    import vmjob
    import vmlib
    os.makedirs(a.out, exist_ok=True)
    job = os.path.join(a.out, "job")
    os.makedirs(job, exist_ok=True)
    settings = a.settings
    if a.fixed_transitions:
        settings = expand(settings) + ";slurs/fixedTransitions=1"
    H.make_score(a.score, os.path.join(job, "score.mscz"), settings, a.part)
    for c in ('"', "%", "&", "|", "<", ">", "^"):
        if c in a.exe or c in a.library:
            raise SystemExit("bad character %r in --exe / --library" % c)
    with open(os.path.join(job, "repeat_vm.cmd"), "w", newline="") as f:
        mem = "set MS_FREE_MEMORY_MB=%d\n" % a.free_memory_mb if a.free_memory_mb else ""
        f.write(VM_CMD.replace("{n}", str(a.n)).replace("{exe}", a.exe).replace("{library}", a.library)
                .replace("{memory}", mem).replace("\n", "\r\n"))
    import re
    name = a.name or ("repeat-" + re.sub(r"[^A-Za-z0-9_.-]", "", os.path.basename(os.path.abspath(a.out))))[:64]
    argv = ["--name", name, "--vmdir", "repeatcheck", "--out", os.path.join(a.out, "fetched"), "--fetch", "out", "--fetch",
            "run.out", "--retries", "0", "--timeout", str(a.timeout), "--kill", os.path.basename(a.exe.replace("\\", "/")),
            job, "--", "repeat_vm.cmd"]
    rc = vmjob.main(argv)
    if rc not in (0,):
        print("vm job exit %d (see %s)" % (rc, os.path.join(a.out, "fetched", "vmjob.log")))
    with open(os.path.join(a.out, "repeat-run.json"), "w") as f:
        json.dump(dict(mode="vm", score=a.score, settings=settings, part=a.part, n=a.n, exe=a.exe), f)
    if not glob.glob(os.path.join(a.out, "fetched", "out", "run-*")):
        return 2
    return compare_dir(a.out)


def vm_run(d):
    """one fetched VM run: events (slot -> [(t, type, a, b)]), {route code: sha256}"""
    ev = collections.defaultdict(list)
    for f in glob.glob(os.path.join(d, "**", "* events.tsv"), recursive=True):
        rows = DC_tsv(f)
        for r in rows:
            ev[r["slot"]].append((r["time"], r["type"], r["a"], r["b"]))
    hashes = {}
    for f in glob.glob(os.path.join(d, "**", "*.wav.sha256"), recursive=True):
        code = os.path.basename(f)[:-len(".wav.sha256")].rsplit(" route ", 1)[-1]
        with open(f, encoding="utf-8", errors="replace") as h:
            hashes[code] = "".join(h.read().split()).lower()
    return ev, hashes


def DC_tsv(path):
    import csv
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


# ------------------------------------------------------------------------------------------------------------- compare

def compare_dir(out):
    info = {}
    p = os.path.join(out, "repeat-run.json")
    if os.path.exists(p):
        with open(p) as f:
            info = json.load(f)
    lines, res = [], dict(info=info, runs=[])
    any_diff = False
    if glob.glob(os.path.join(out, "run-*", "* museScore.txt")):
        runs = sorted((r for r in glob.glob(os.path.join(out, "run-*")) if os.path.isdir(r)), key=lambda s: int(s.rsplit("-", 1)[1]))
        lines.append("Linux renders: %d (%s)" % (len(runs), info.get("tool", "?")))
        lines.append("note-ons per (part, patch), lanes 0/1/...; one column per run:")
        lines.append(DC.slots_text(runs))
        for r in runs[1:]:
            for by in (["part", "patch", "lane"], ["code"]):
                c = DC.compare(runs[0], r, by)
                same = c["identical"]
                any_diff |= not same
                res["runs"].append(dict(run=os.path.basename(r), routeBy=by, identical=same,
                                        differing=[x for x in c["routes"] if not x["identical"]][:20],
                                        files=[f for f in c["files"] if not f["identical"]]))
                lines.append("%s vs %s by %s: %s" % (os.path.basename(r), os.path.basename(runs[0]), ",".join(by),
                                                    "IDENTICAL" if same else "DIFFERENT"))
                if not same:
                    lines.append(DC.report_text(c, 12))
    else:
        runs = sorted(glob.glob(os.path.join(out, "fetched", "out", "run-*")), key=lambda s: int(s.rsplit("-", 1)[1]))
        lines.append("VM renders: %d" % len(runs))
        data = [(os.path.basename(r),) + vm_run(r) for r in runs]
        slots = sorted({s for _, ev, _ in data for s in ev}, key=lambda s: int(s) if s.isdigit() else 0)
        lines.append("note-ons per slot (velocity > 0), one column per run:")
        counts = {}
        for s in slots:
            row = [sum(1 for e in ev.get(s, []) if e[1] == "on" and e[3] not in ("0", "")) for _, ev, _ in data]
            counts[s] = row
            lines.append("  slot %-4s %s%s" % (s, "  ".join("%4d" % x for x in row), "   DIFFERS" if len(set(row)) > 1 else ""))
        res["slotNoteOns"] = counts
        if data:
            r0, ev0, h0 = data[0]
            for rn, ev, h in data[1:]:
                diffs = []
                for s in slots:
                    a_, b_ = ev0.get(s, []), ev.get(s, [])
                    if a_ != b_:
                        i = next((k for k, (x, y) in enumerate(zip(a_, b_)) if x != y), min(len(a_), len(b_)))
                        diffs.append(dict(slot=s, index=i, A=a_[i] if i < len(a_) else None, B=b_[i] if i < len(b_) else None))
                hd = sorted(k for k in set(h0) | set(h) if h0.get(k) != h.get(k))
                same = not diffs and not hd
                any_diff |= not same
                res["runs"].append(dict(run=rn, identical=same, events=diffs, wavHashesDiffer=hd, wavs=len(h)))
                lines.append("%s vs %s: %s; events differ on %d slot(s), WAV hashes differ on %d of %d route(s)%s" % (
                    rn, r0, "IDENTICAL" if same else "DIFFERENT", len(diffs), len(hd), len(h),
                    (": " + ", ".join(hd[:12])) if hd else ""))
                for d in diffs[:12]:
                    lines.append("    slot %s first diff #%d: %s | %s" % (d["slot"], d["index"], d["A"], d["B"]))
    lines.append("ALL RUNS IDENTICAL" if not any_diff else "RUNS DIFFER")
    text = "\n".join(lines) + "\n"
    with open(os.path.join(out, "repeat.txt"), "w", encoding="utf-8") as f:
        f.write(text)
    with open(os.path.join(out, "repeat.json"), "w", encoding="utf-8") as f:
        json.dump(res, f, indent=1)
    sys.stdout.write(text)
    return 1 if any_diff else 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    li = sub.add_parser("linux")
    li.add_argument("score")
    li.add_argument("--settings", default="recommended")
    li.add_argument("--n", type=int, default=5)
    li.add_argument("--tool", choices=("audit", "dump"), default="audit")
    li.add_argument("--chunked", action="store_true")
    li.add_argument("--map")
    li.add_argument("--calibration")
    li.add_argument("--run", required=True, help="the build-dir wrapper (runs a command string)")
    li.add_argument("--free-memory-mb", type=int, help="pin the free memory the tuning / voice copies count (MS_FREE_MEMORY_MB)")
    li.add_argument("-o", "--out", required=True)
    vm = sub.add_parser("vm")
    vm.add_argument("score")
    vm.add_argument("--settings", default="recommended")
    vm.add_argument("--part")
    vm.add_argument("--n", type=int, default=3)
    vm.add_argument("--fixed-transitions", action="store_true")
    vm.add_argument("--exe", default=r"C:\claude\bpt\ms\bin\MuseScore3Evo.exe")
    vm.add_argument("--library", default="Spitfire Symphony Orchestra")
    vm.add_argument("--timeout", type=float, default=3600)
    vm.add_argument("--name")
    vm.add_argument("--free-memory-mb", type=int, help="pin MS_FREE_MEMORY_MB (builds from afd801287d); without it the "
                    "copies follow the VM's free memory, which is what OPEN_ITEMS item 1 was")
    vm.add_argument("-o", "--out", required=True)
    co = sub.add_parser("compare")
    co.add_argument("dir")
    a = ap.parse_args(argv)
    if a.cmd == "linux":
        return cmd_linux(a)
    if a.cmd == "vm":
        return cmd_vm(a)
    return compare_dir(a.dir)


if __name__ == "__main__":
    sys.exit(main())
