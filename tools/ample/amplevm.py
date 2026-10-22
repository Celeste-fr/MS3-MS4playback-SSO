#!/usr/bin/env python3
"""The Windows VM where the Ample plug-in is installed: ssh / scp, the shared quiet.lock, amphost runs with a watchdog.

Configuration (environment; nothing personal is in the repository):
  AMPLE_VM      user@host of the VM (required; the owner's dev machine has it in its notes)
  AMPLE_VM_KEY  ssh key (default ~/.ssh/id_ed25519_winvm)
  AMPLE_VM_DIR  work folder on the VM (default C:\\claude\\apipe); amphost.exe is copied there
  AMPLE_VM_LOCK the lock file other sessions honour (default C:\\claude\\quiet.lock); AMPLE_LOCK_NAME its text
  AMPLE_PAUSE   a local file written while the lock is held (default none)

Lock rule: wait until the lock file doesn't exist, create it with our name, hold it only while rendering, remove it only
when it still holds our name. Never delete another session's lock.
"""

import os
import shlex
import subprocess
import sys
import time

VM = os.environ.get("AMPLE_VM", "")
KEY = os.path.expanduser(os.environ.get("AMPLE_VM_KEY", "~/.ssh/id_ed25519_winvm"))
DIR = os.environ.get("AMPLE_VM_DIR", r"C:\claude\apipe")
LOCK = os.environ.get("AMPLE_VM_LOCK", r"C:\claude\quiet.lock")
LOCK_NAME = os.environ.get("AMPLE_LOCK_NAME", "ample-pipeline")
PAUSE = os.environ.get("AMPLE_PAUSE", "")
TMP = os.environ.get("AMPLE_VM_TMP", r"C:\claude")


def need_vm():
    if not VM:
        raise SystemExit("set AMPLE_VM=user@host (the Windows VM with the plug-in)")


def ps(script, check=True, timeout=None):
    """run a PowerShell script on the VM (copied as a .ps1 and run with -File: stdin mode runs line by line, which breaks
    multi-line blocks), return its output"""
    need_vm()
    import tempfile
    name = "amplevm_%d_%d.ps1" % (os.getpid(), int(time.time() * 1000))
    with tempfile.NamedTemporaryFile("w", suffix=".ps1", delete=False, encoding="utf-8-sig") as f:
        f.write(script)
        local = f.name
    try:
        remote = "%s\\%s" % (TMP, name)
        subprocess.run(["scp", "-q", "-i", KEY, local, "%s:%s" % (VM, remote.replace("\\", "/"))], check=True)
    finally:
        os.unlink(local)
    r = subprocess.run(["ssh", "-o", "ConnectTimeout=15", "-i", KEY, VM,
                        "powershell -NoProfile -NonInteractive -ExecutionPolicy Bypass -File %s & del %s" % (remote, remote)],
                       capture_output=True, text=True, timeout=timeout)
    if check and r.returncode != 0:
        raise RuntimeError("VM script failed (%d): %s" % (r.returncode, (r.stderr + r.stdout)[-2000:]))
    return r.stdout


def win(path):
    return path.replace("/", "\\")


def put(local, remote):
    need_vm()
    subprocess.run(["scp", "-q", "-i", KEY, local, "%s:%s" % (VM, remote.replace("\\", "/"))], check=True)


def get(remote, local):
    need_vm()
    subprocess.run(["scp", "-q", "-i", KEY, "%s:%s" % (VM, remote.replace("\\", "/")), local], check=True)


def mkdir(remote):
    ps("New-Item -ItemType Directory -Force '%s' | Out-Null" % win(remote))


class Lock:
    """with Lock(): ... holds the VM's quiet.lock (waits for another session's to go)"""

    def __enter__(self):
        while True:
            out = ps("if (Test-Path '%s') { 'BUSY ' + (Get-Content '%s' -Raw) } else { Set-Content '%s' '%s'; 'TAKEN' }"
                     % (LOCK, LOCK, LOCK, LOCK_NAME))
            if "TAKEN" in out:
                break
            print("VM busy (%s), waiting" % out.strip(), file=sys.stderr)
            time.sleep(60)
        if PAUSE:
            try:
                with open(PAUSE, "w") as f:
                    f.write(LOCK_NAME + "\n")
            except OSError:
                self.__exit__()                # (never leave the VM's lock behind)
                raise
        return self

    def __exit__(self, *a):
        ps("if ((Test-Path '{0}') -and ((Get-Content '{0}' -Raw).Trim() -eq '{1}')) {{ Remove-Item '{0}' }}"
           .format(LOCK, LOCK_NAME), check=False)
        if PAUSE and os.path.exists(PAUSE):
            os.unlink(PAUSE)


def file_hash(remote):
    return ps("(Get-FileHash '%s').Hash" % win(remote)).strip()


def run_watched(exe, args, log, secs=180, env=None):
    """run exe with args on the VM under a watchdog; returns (state, seconds): state 'exit N' or 'KILLED'"""
    envs = "".join("$env:%s = '%s'; " % (k, v) for k, v in (env or {}).items())
    script = r"""
%s$t0 = Get-Date
$p = Start-Process -FilePath '%s' -ArgumentList '%s' -NoNewWindow -PassThru -RedirectStandardOutput '%s.out' -RedirectStandardError '%s.err'
$h = $p.Handle
while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt %d) { Start-Sleep -Milliseconds 300 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep 1; $s = 'KILLED' } else { $s = 'exit ' + $p.ExitCode }
'{0} {1:N1}' -f $s, ((Get-Date) - $t0).TotalSeconds
""" % (envs, win(exe), args.replace("'", "''"), win(log), win(log), secs)
    out = ps(script).strip().splitlines()[-1]
    state, t = out.rsplit(" ", 1)
    return state, float(t.replace(",", "."))


def quote(path):
    return '"%s"' % win(path)


if __name__ == "__main__":
    # amplevm.py ps <script file> | lock-test
    if sys.argv[1:2] == ["ps"]:
        print(ps(open(sys.argv[2]).read()))
    else:
        print(__doc__)
