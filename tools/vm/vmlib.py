#!/usr/bin/env python3
"""vmlib.py: the Windows test VM from Linux, in ONE place: its address, the lock protocol, guarded deletes.

Every tool that renders on the VM (heard_check.sh, vmjob.py, repeat_check.py --vm, ...) uses this module, so the VM's
address and the lock rules live here only (docs/TOOLS.md › VM tools).

Configuration (first found wins, per key):
  environment   MS_VM (ssh target, user@host), MS_VM_KEY (ssh key), MS_VM_PAUSE (the local pause file),
                MS_VM_LOCK (the VM's lock file), MS_VM_ROOT (the VM's work root)
  file          $MS_VM_CONF, else ~/.config/ms3fork/vm.conf: "key = value" lines (host, key, pause, lock, root), '#'
                comments; see tools/vm/vm.conf.example. The file is outside the repository (the address is the owner's
                network's).
  defaults      key ~/.ssh/id_ed25519_winvm, lock C:\\claude\\quiet.lock, root C:\\claude,
                pause ~/MuseScore/ms3fork/private/VM_PAUSE; no default host (a stale default sent jobs to a dead address).

Lock protocol (the sessions sharing the VM, TOOLS.md › Conventions):
  - wait while C:\\claude\\quiet.lock exists or the local pause file exists, unless it holds this job's own name
    (re-entrant: a job that already holds the lock under its name may call tools that lock again under that name;
    MS_VM_JOB passes the name down);
  - take: write the job's name into the lock and the pause file, read the lock back (twice, 2 s apart) to see that it
    is ours (two sessions starting together: the later writer wins, the other waits again);
  - release between batches (a tool locks only around its VM work, not its Linux analysis);
  - never delete another job's lock: the release deletes the lock only when it holds exactly this name.

Command line:
  vmlib.py conf [--shell]            the configuration (--shell: MS_VM=... lines to eval in sh)
  vmlib.py status                    who holds the VM lock and the local pause file, free disk on C:
  vmlib.py lock <name> [--max-wait s] [--poll s] (default 20 s: a job that releases and re-takes between its batches
                                     leaves short gaps)
                                     wait, then take the lock; exit 0 taken, 3 already held under <name> (nested: the
                                     caller must not unlock), 4 timed out
  vmlib.py unlock <name>             release (only if held under <name>)
  vmlib.py run <name> -- <cmd...>    hold the lock while a local command runs (MS_VM_JOB=<name> in its environment)
  vmlib.py ssh <command>             one command on the VM (the configured target and key)
  vmlib.py mem [--min-gb 1]          local MemAvailable; exit 1 under the minimum
"""
import argparse
import base64
import os
import re
import shlex
import subprocess
import sys
import time

DEFAULTS = {
    "key": "~/.ssh/id_ed25519_winvm",
    "lock": r"C:\claude\quiet.lock",
    "root": r"C:\claude",
    "pause": "~/MuseScore/ms3fork/private/VM_PAUSE",
    "host": "",
}
ENV = {"host": "MS_VM", "key": "MS_VM_KEY", "pause": "MS_VM_PAUSE", "lock": "MS_VM_LOCK", "root": "MS_VM_ROOT"}
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$")
# a folder the tools may delete: at least two levels under the work root, plain names only
VMDIR_RE = re.compile(r"^[A-Za-z]:\\claude(\\[A-Za-z0-9_][A-Za-z0-9_.-]*){2,}$")
# Windows crash exit codes as cmd's %ERRORLEVEL% shows them (signed), and unsigned
CRASH_CODES = {
    -1073741819: "0xC0000005 access violation",
    -1073740791: "0xC0000409 stack buffer overrun",
    -1073741571: "0xC00000FD stack overflow",
    -1073741811: "0xC000000D invalid parameter",
    -1073740940: "0xC0000374 heap corruption",
    -1073741795: "0xC000001D illegal instruction",
}
for _k, _v in list(CRASH_CODES.items()):
    CRASH_CODES[_k & 0xFFFFFFFF] = _v


class VMError(RuntimeError):
    pass


def conf_path():
    return os.environ.get("MS_VM_CONF") or os.path.expanduser("~/.config/ms3fork/vm.conf")


def read_conf(path=None, env=None):
    """the configuration: env over file over defaults; paths with ~ expanded (local ones)"""
    env = os.environ if env is None else env
    c = dict(DEFAULTS)
    path = path or conf_path()
    if path and os.path.exists(path):
        for ln in open(path, encoding="utf-8"):
            ln = ln.split("#", 1)[0].strip()
            if not ln or "=" not in ln:
                continue
            k, v = (x.strip() for x in ln.split("=", 1))
            if k in c:
                c[k] = v
    for k, e in ENV.items():
        if env.get(e):
            c[k] = env[e]
    c["key"] = os.path.expanduser(c["key"])
    c["pause"] = os.path.expanduser(c["pause"])
    c["file"] = path if path and os.path.exists(path) else None
    return c


def check_name(name):
    if not NAME_RE.match(name or ""):
        raise VMError("bad job name %r (letters, digits, _ . -; at most 64)" % (name,))
    return name


def check_vmdir(path):
    if not VMDIR_RE.match(path or ""):
        raise VMError("refusing VM folder %r: must be a plain path at least two levels under C:\\claude" % (path,))
    return path


def mem_available_gb():
    try:
        for ln in open("/proc/meminfo"):
            if ln.startswith("MemAvailable:"):
                return int(ln.split()[1]) / 1024.0 / 1024.0
    except OSError:
        pass
    return None


class VM:
    """the real VM over ssh / scp. The lock logic (Lock) only uses exists / read / write / delete_if / sleep, so a test
    can give it a fake (test_vmlib.py)."""

    def __init__(self, conf=None):
        self.conf = conf or read_conf()
        if not self.conf["host"]:
            raise VMError("no VM address: set MS_VM=user@host or write host = user@host into %s (tools/vm/vm.conf.example)"
                          % conf_path())

    def _ssh_base(self):
        return ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=20", "-i", self.conf["key"], self.conf["host"]]

    def ssh(self, command, timeout=None, check=False):
        """(exit code, stdout text); exit 255 = ssh itself failed. timeout: s, then the ssh client is killed (the remote
        command may go on: callers kill their image after a timeout)"""
        try:
            p = subprocess.run(self._ssh_base() + [command], capture_output=True, text=True, timeout=timeout,
                               encoding="utf-8", errors="replace")
        except subprocess.TimeoutExpired:
            return None, ""
        if check and p.returncode != 0:
            raise VMError("ssh %r: exit %d %s" % (command, p.returncode, p.stderr.strip()[:300]))
        return p.returncode, p.stdout

    def scp_to(self, local_paths, remote, recursive=False):
        a = ["scp", "-q", "-o", "BatchMode=yes", "-i", self.conf["key"]] + (["-r"] if recursive else [])
        r = subprocess.run(a + list(local_paths) + ["%s:%s" % (self.conf["host"], remote.replace("\\", "/"))])
        if r.returncode:
            raise VMError("scp to %s failed (%d)" % (remote, r.returncode))

    def scp_from(self, remote, local, recursive=False):
        a = ["scp", "-q", "-o", "BatchMode=yes", "-i", self.conf["key"]] + (["-r"] if recursive else [])
        return subprocess.run(a + ["%s:%s" % (self.conf["host"], remote.replace("\\", "/")), local]).returncode == 0

    # (the lock's primitives)
    def exists(self, path):
        rc, out = self.ssh('if exist "%s" (echo YES) else (echo NO)' % path)
        if rc is None or rc == 255:
            raise VMError("the VM does not answer (%s)" % self.conf["host"])
        return "YES" in out

    def read(self, path):
        rc, out = self.ssh('if exist "%s" type "%s"' % (path, path))
        return out.strip() if rc == 0 and out.strip() else None

    def write(self, path, text):
        check_name(text)
        # ("(echo x)>": a name ending in a digit would read as a handle in "echo x1> f")
        self.ssh('(echo %s)> "%s"' % (text, path), check=True)

    def delete_if(self, path, text):
        """delete <path> only if it holds exactly <text>"""
        check_name(text)
        self.ssh('findstr /x /c:"%s" "%s" >nul 2>&1 && del "%s"' % (text, path, path))

    def rmdir_guarded(self, path, marker):
        """remove a run folder: a literal path (checked), only when it holds the run's own marker file"""
        check_vmdir(path)
        if not re.match(r"^[A-Za-z0-9_.-]+$", marker or ""):
            raise VMError("bad marker %r" % (marker,))
        self.ssh('if exist "%s\\%s" rmdir /s /q "%s"' % (path, marker, path))
        return not self.exists(path)

    def free_gb(self, drive="C:"):
        rc, out = self.ssh("dir %s\\ | find \"bytes free\"" % drive)
        m = re.search(r"([\d,.]+) bytes free", out or "")
        return int(re.sub(r"[,.]", "", m.group(1))) / 1e9 if m else None

    def kill_image(self, image, within=None):
        """kill <image>'s processes; with within (a run folder): only those whose command line names it (another
        session's processes of the same image are left alone)"""
        if not re.match(r"^[A-Za-z0-9_.-]+\.exe$", image):
            raise VMError("bad image %r" % image)
        if within is None:
            self.ssh("taskkill /f /im %s >nul 2>&1" % image)
            return
        check_vmdir(within)
        ps = ("Get-CimInstance Win32_Process -Filter \"Name='%s'\" | Where-Object { $_.CommandLine -like '*%s*' } | "
              "ForEach-Object { Stop-Process -Id $_.ProcessId -Force }" % (image, within))
        self.ssh("powershell -NoProfile -EncodedCommand " + base64.b64encode(ps.encode("utf-16-le")).decode())


class Lock:
    """the VM lock plus the local pause file; re-entrant by name"""

    def __init__(self, vm, name, log=None, poll=20.0, sleep=time.sleep, now=time.time):
        self.vm, self.name = vm, check_name(name)
        self.lockpath, self.pause = vm.conf["lock"], vm.conf["pause"]
        self.log = log or (lambda s: print(s, file=sys.stderr, flush=True))
        self.poll, self.sleep, self.now = poll, sleep, now
        self.taken = False      # True: we created it (release deletes it); False after acquire: nested
        self.nested = False

    def _pause_holder(self):
        try:
            with open(self.pause, encoding="utf-8") as f:
                return f.read().strip() or "?"
        except OSError:
            return None

    def holders(self):
        return self.vm.read(self.lockpath) if self.vm.exists(self.lockpath) else None, self._pause_holder()

    def acquire(self, max_wait=None):
        t0 = self.now()
        last = None
        while True:
            vm_h, local_h = self.holders()
            if vm_h == self.name and local_h in (None, self.name):
                self.nested = True
                self.log("vm lock: already held as %s (nested)" % self.name)
                return True
            busy = [x for x in ((vm_h, "VM lock"), (local_h, "local pause")) if x[0] is not None and x[0] != self.name]
            if not busy and vm_h is None:
                self.vm.write(self.lockpath, self.name)
                self.sleep(2.0)
                if self.vm.read(self.lockpath) == self.name:
                    self.sleep(2.0)
                    if self.vm.read(self.lockpath) == self.name:
                        try:
                            os.makedirs(os.path.dirname(self.pause), exist_ok=True)
                            with open(self.pause, "w", encoding="utf-8") as f:
                                f.write(self.name + "\n")
                        except OSError as e:
                            self.log("vm lock: could not write the local pause file %s: %s" % (self.pause, e))
                        self.taken = True
                        self.log("vm lock: taken as %s" % self.name)
                        return True
                self.log("vm lock: lost a race, waiting")
                continue
            msg = "vm lock: waiting for " + ", ".join("%s (%s)" % (h, w) for h, w in busy or [(vm_h, "VM lock")])
            key = tuple((h or "").split()[:1][0] if h else "" for h, _ in busy)    # (a pause file's time stamp aside)
            if key != last:
                self.log(msg)
                last = key
            if max_wait is not None and self.now() - t0 >= max_wait:
                self.log("vm lock: gave up after %.0f s" % (self.now() - t0))
                return False
            self.sleep(self.poll)

    def release(self):
        if not self.taken:
            return
        self.vm.delete_if(self.lockpath, self.name)
        if self._pause_holder() == self.name:
            try:
                os.remove(self.pause)
            except OSError:
                pass
        self.taken = False
        self.log("vm lock: released %s" % self.name)

    def __enter__(self):
        if not self.acquire():
            raise VMError("vm lock not taken")
        return self

    def __exit__(self, *a):
        self.release()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("conf")
    c.add_argument("--shell", action="store_true")
    sub.add_parser("status")
    lk = sub.add_parser("lock")
    lk.add_argument("name")
    lk.add_argument("--max-wait", type=float)
    lk.add_argument("--poll", type=float, default=20.0)
    ul = sub.add_parser("unlock")
    ul.add_argument("name")
    rn = sub.add_parser("run")
    rn.add_argument("name")
    rn.add_argument("command", nargs=argparse.REMAINDER)
    sh = sub.add_parser("ssh")
    sh.add_argument("command")
    me = sub.add_parser("mem")
    me.add_argument("--min-gb", type=float, default=1.0)
    a = ap.parse_args(argv)
    if a.cmd == "conf":
        c = read_conf()
        if a.shell:
            for k, e in ENV.items():
                print("%s=%s" % (e, shlex.quote(c[k])))
        else:
            for k in ("host", "key", "lock", "root", "pause", "file"):
                print("%-6s %s" % (k, c[k]))
        return 0 if c["host"] else 1
    if a.cmd == "mem":
        m = mem_available_gb()
        print("MemAvailable %.2f GB" % (m or 0))
        return 0 if m is None or m >= a.min_gb else 1
    vm = VM()
    if a.cmd == "status":
        h = Lock(vm, "status").holders()
        print("VM lock (%s): %s" % (vm.conf["lock"], h[0] or "free"))
        print("local pause (%s): %s" % (vm.conf["pause"], h[1] or "none"))
        f = vm.free_gb()
        print("C: free %s GB" % ("?" if f is None else "%.1f" % f))
        return 0
    if a.cmd == "ssh":
        rc, out = vm.ssh(a.command)
        sys.stdout.write(out)
        return 255 if rc is None else rc
    if a.cmd == "lock":
        L = Lock(vm, a.name, poll=a.poll)
        if not L.acquire(a.max_wait):
            return 4
        return 3 if L.nested else 0
    if a.cmd == "unlock":
        L = Lock(vm, a.name)
        L.taken = True
        L.release()
        return 0
    if a.cmd == "run":
        cmd = a.command[1:] if a.command[:1] == ["--"] else a.command
        if not cmd:
            ap.error("run: no command")
        with Lock(vm, a.name):
            env = dict(os.environ, MS_VM_JOB=a.name)
            return subprocess.call(cmd, env=env)
    return 2


if __name__ == "__main__":
    sys.exit(main())
