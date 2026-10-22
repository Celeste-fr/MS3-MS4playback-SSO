#!/usr/bin/env python3
"""vmjob.py: run one job on the Windows test VM with the lock protocol, a watchdog, crash retries and cleanup.

  vmjob.py [options] <job dir> -- <VM command>

Steps: (1) local MemAvailable >= --min-mem GB (waits); (2) the VM lock (tools/vm/vmlib.py: wait while another job holds
C:\\claude\\quiet.lock or the local VM_PAUSE, take both under --name, re-entrant through MS_VM_JOB); (3) the job dir
copied to C:\\claude\\<--vmdir>\\<name> (with a marker file, so only this run's folder can be deleted); (4) the command
run there through a run.cmd (cwd = the run folder; "{run}" in it = the run folder's path) under a watchdog (--timeout s:
then --kill <image>'s processes naming the run folder are killed); its %ERRORLEVEL% read back; a crash (0xC0000005, 0xC0000409, 0xC00000FD, 0xC000000D,
0xC0000374, 0xC000001D or --retry-code), a timeout, or a missing --expect file is retried up to --retries times, each
attempt logged; (5) the --fetch paths (relative to the run folder) copied back into --out; (6) the run folder deleted
on the VM (unless --keep), WAVs with it; (7) the lock released (also on an error or Ctrl-C).
Exit status: the command's last exit code (0..255 truncated; a crash 128), 124 on timeout, 2 on a setup error.
The log: <out>/vmjob.log (one line per step and attempt) and <out>/vmjob.json.

Examples:
  vmjob.py --name myrun --fetch out --kill MuseScore3Evo.exe --timeout 1800 work/ -- \\
      'C:\\claude\\bpt\\ms\\bin\\MuseScore3Evo.exe --verify-library "Spitfire Symphony Orchestra" --verify-playback
       {run}\\score.mscz --verify-out {run}\\out --verify-wav'
  vmjob.py --name smoke --retries 1 empty/ -- 'echo hello'
Environment variables set for the command on the VM: none by default; --env K=V (repeatable) adds "set K=V" lines.
"""
import argparse
import json
import os
import re
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vmlib  # noqa: E402

MARKER = ".vmjob-run"


def run_cmd_text(command, env):
    lines = ["@echo off", "cd /d \"%~dp0\""]
    for k, v in env:
        lines.append("set \"%s=%s\"" % (k, v))
    lines += ["call %s" % command, "(echo %ERRORLEVEL%)> \"%~dp0.vmjob.rc\"", "exit /b 0"]
    return "\r\n".join(lines) + "\r\n"


def classify(rc, timed_out, missing, retry_codes):
    """why an attempt should be retried (None: done)"""
    if timed_out:
        return "timeout"
    if rc is None:
        return "no exit code read back"
    if rc in vmlib.CRASH_CODES:
        return "crash " + vmlib.CRASH_CODES[rc]
    if rc in retry_codes:
        return "exit %d (--retry-code)" % rc
    if missing:
        return "missing " + ", ".join(missing)
    return None


def run_job(vm, a, log, lock_factory=vmlib.Lock, sleep=time.sleep):
    """the job's steps (vm: vmlib.VM or a fake with the same methods); returns (exit status, attempts list)"""
    name = vmlib.check_name(a.name)
    vmdir = a.vmdir
    if not re.match(r"^[A-Za-z0-9_-]+$", vmdir):
        raise vmlib.VMError("bad --vmdir %r" % vmdir)
    root = vm.conf["root"].rstrip("\\")
    run = vmlib.check_vmdir("%s\\%s\\%s" % (root, vmdir, name))
    attempts = []
    while True:
        m = vmlib.mem_available_gb()
        if m is None or m >= a.min_mem:
            break
        log("MemAvailable %.2f GB < %.1f: waiting" % (m, a.min_mem))
        sleep(30)
    L = lock_factory(vm, name, log=log, poll=a.poll)
    if not L.acquire(a.max_wait):
        log("lock not taken")
        return 2, attempts
    status = 2
    try:
        vm.ssh('if not exist "%s\\%s" mkdir "%s\\%s"' % (root, vmdir, root, vmdir), check=True)
        if vm.exists(run):
            raise vmlib.VMError("the run folder %s exists already (another run of this name?)" % run)
        with tempfile.TemporaryDirectory() as t:
            stage = os.path.join(t, name)
            os.makedirs(stage)
            open(os.path.join(stage, MARKER), "w").write(name + "\n")
            command = a.command.replace("{run}", run)
            open(os.path.join(stage, "run.cmd"), "w", newline="").write(run_cmd_text(command, a.env))
            items = [os.path.join(a.job, x) for x in sorted(os.listdir(a.job))] if a.job else []
            vm.scp_to([stage], "%s\\%s\\" % (root, vmdir), recursive=True)
            if items:
                vm.scp_to(items, run + "\\", recursive=True)
        log("copied %d item(s) to %s" % (len(items), run))
        for i in range(a.retries + 1):
            vm.ssh('if exist "%s\\.vmjob.rc" del "%s\\.vmjob.rc"' % (run, run))
            t0 = time.time()
            rc, out = vm.ssh('"%s\\run.cmd"' % run, timeout=a.timeout)
            timed_out = rc is None
            if timed_out and a.kill:
                for im in a.kill:
                    vm.kill_image(im, run)
            txt = vm.read(run + "\\.vmjob.rc")
            code = int(txt) if txt and re.match(r"^-?\d+$", txt) else None
            missing = [e for e in a.expect if not vm.exists(run + "\\" + e.replace("/", "\\"))]
            why = classify(code, timed_out, missing, a.retry_code)
            attempts.append(dict(attempt=i + 1, exit=code, seconds=round(time.time() - t0, 1), timedOut=timed_out,
                                 retry=why, stdout=(out or "")[-2000:]))
            log("attempt %d: exit %s in %.0f s%s" % (i + 1, code, time.time() - t0, "; " + why if why else ""))
            if out:
                sys.stdout.write(out)
            if not why or i == a.retries:
                break
            log("retrying (%s)" % why)
        last = attempts[-1]
        if last["timedOut"]:
            status = 124
        elif last["exit"] is None:
            status = 2
        elif last["exit"] in vmlib.CRASH_CODES:
            status = 128
        else:
            status = last["exit"] & 0xFF
        os.makedirs(a.out, exist_ok=True)
        for f in a.fetch:
            ok = vm.scp_from(run + "\\" + f.replace("/", "\\"), a.out, recursive=True)
            log("fetched %s: %s" % (f, "ok" if ok else "FAILED"))
        if not a.keep:
            gone = vm.rmdir_guarded(run, MARKER)
            log("deleted %s: %s" % (run, "ok" if gone else "NOT deleted"))
        else:
            log("kept %s (--keep)" % run)
    except vmlib.VMError as e:
        log("error: %s" % e)
        status = 2
    except KeyboardInterrupt:
        # (stopped by hand: the VM's process and run folder go too, then the lock)
        log("interrupted: killing %s, deleting %s" % (", ".join(a.kill) or "nothing", run))
        try:
            for im in a.kill:
                vm.kill_image(im, run)
            if not a.keep:
                vm.rmdir_guarded(run, MARKER)
        finally:
            L.release()
        raise
    finally:
        L.release()
    return status, attempts


def parse(argv):
    if "--" not in argv:
        raise SystemExit("usage: vmjob.py [options] <job dir> -- <VM command>   (vmjob.py --help)")
    i = argv.index("--")
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("job", nargs="?", help="local folder copied into the run folder (may be empty)")
    ap.add_argument("--name", help="job / run name (default: vmjob-<time>)")
    ap.add_argument("--vmdir", default="vmjob", help="folder under C:\\claude (default vmjob)")
    ap.add_argument("--out", help="local folder for fetched files and the log (default <job>.out)")
    ap.add_argument("--fetch", action="append", default=[], help="path in the run folder to fetch (repeatable)")
    ap.add_argument("--expect", action="append", default=[], help="a file the command must write (else retried)")
    ap.add_argument("--retries", type=int, default=2)
    ap.add_argument("--retry-code", type=int, action="append", default=[])
    ap.add_argument("--timeout", type=float, default=3600.0)
    ap.add_argument("--kill", action="append", default=[], help="image to kill after a timeout, e.g. MuseScore3Evo.exe")
    ap.add_argument("--env", action="append", default=[], help="K=V set for the command (repeatable)")
    ap.add_argument("--keep", action="store_true", help="leave the run folder on the VM")
    ap.add_argument("--min-mem", type=float, default=1.0, help="local MemAvailable GB to wait for (default 1)")
    ap.add_argument("--max-wait", type=float, help="give up waiting for the lock after s")
    ap.add_argument("--poll", type=float, default=20.0)
    a = ap.parse_args(argv[:i])
    a.command = " ".join(argv[i + 1:])
    if not a.command:
        ap.error("no VM command after --")
    a.name = a.name or os.environ.get("MS_VM_JOB") or time.strftime("vmjob-%Y%m%d-%H%M%S")
    env = []
    for kv in a.env:
        k, _, v = kv.partition("=")
        if not re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", k) or any(c in v for c in '"%&|<>^'):
            ap.error("bad --env %r" % kv)
        env.append((k, v))
    a.env = env
    if a.job and not os.path.isdir(a.job):
        ap.error("no job dir %s" % a.job)
    a.out = a.out or ((a.job.rstrip("/") + ".out") if a.job else a.name + ".out")
    return a


def main(argv=None):
    a = parse(sys.argv[1:] if argv is None else argv)
    os.makedirs(a.out, exist_ok=True)
    logf = open(os.path.join(a.out, "vmjob.log"), "a", encoding="utf-8")

    def log(s):
        line = "%s %s" % (time.strftime("%H:%M:%S"), s)
        print(line, file=sys.stderr, flush=True)
        logf.write(line + "\n")
        logf.flush()
    try:
        vm = vmlib.VM()
    except vmlib.VMError as e:
        log(str(e))
        return 2
    status, attempts = run_job(vm, a, log)
    json.dump(dict(name=a.name, command=a.command, status=status, attempts=attempts), open(os.path.join(a.out, "vmjob.json"), "w"),
              indent=1)
    return status


if __name__ == "__main__":
    sys.exit(main())
