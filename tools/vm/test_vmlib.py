#!/usr/bin/env python3
"""Tests for vmlib.py (configuration, lock protocol, guards) and vmjob.py (retries, cleanup), on a fake VM: no ssh.

    python3 -I tools/vm/test_vmlib.py [-v]
"""
import os
import sys
import tempfile
import types
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vmlib  # noqa: E402
import vmjob  # noqa: E402


class FakeVM(vmlib.VM):
    """files in a dict; ssh understands only what vmjob sends; run.cmd's results scripted"""

    def __init__(self, pause, results=()):
        self.conf = dict(vmlib.DEFAULTS, host="user@fake", pause=pause, key="k")
        self.files = {}
        self.results = list(results)      # per run: (exit code or None = timeout, files written)
        self.commands = []
        self.killed = []
        self.on_poll = None

    def exists(self, path):
        return path in self.files or any(f.startswith(path + "\\") for f in self.files)

    def read(self, path):
        return self.files.get(path)

    def write(self, path, text):
        self.files[path] = text

    def delete_if(self, path, text):
        if self.files.get(path) == text:
            del self.files[path]

    def rmdir_guarded(self, path, marker):
        vmlib.check_vmdir(path)
        if path + "\\" + marker in self.files:
            for f in [f for f in self.files if f.startswith(path + "\\")]:
                del self.files[f]
        return not self.exists(path)

    def kill_image(self, image, within=None):
        self.killed.append(image)

    def scp_to(self, local_paths, remote, recursive=False):
        remote = remote.rstrip("\\")
        for p in local_paths:
            base = remote + "\\" + os.path.basename(p)
            if os.path.isdir(p):
                for d, _, fs in os.walk(p):
                    for f in fs:
                        rel = os.path.relpath(os.path.join(d, f), p).replace("/", "\\")
                        self.files[base + "\\" + rel] = open(os.path.join(d, f)).read().strip()
            else:
                self.files[base] = open(p).read().strip()

    def scp_from(self, remote, local, recursive=False):
        hit = [f for f in self.files if f == remote or f.startswith(remote + "\\")]
        for f in hit:
            rel = remote.split("\\")[-1] + f[len(remote):].replace("\\", "/")
            os.makedirs(os.path.dirname(os.path.join(local, rel)), exist_ok=True)
            open(os.path.join(local, rel), "w").write(self.files[f])
        return bool(hit)

    def ssh(self, command, timeout=None, check=False):
        self.commands.append(command)
        if command.endswith('\\run.cmd"'):
            run = command.strip('"')[:-len("\\run.cmd")]
            code, written = self.results.pop(0)
            for f in written:
                self.files[run + "\\" + f] = "x"
            if code is None:
                return None, ""
            self.files[run + "\\.vmjob.rc"] = str(code)
            return 0, "ran\n"
        if command.startswith("if exist") and ".vmjob.rc" in command:
            self.files.pop(command.split('"')[1], None)
        return 0, ""


def args(**kw):
    a = dict(name="t1", vmdir="vmjob", job=None, out=None, fetch=[], expect=[], retries=2, retry_code=[], timeout=10,
             kill=[], env=[], keep=False, min_mem=0.0, max_wait=None, poll=1.0, command="x.exe {run}\\in")
    a.update(kw)
    return types.SimpleNamespace(**a)


class Conf(unittest.TestCase):
    def test_file_env_defaults(self):
        with tempfile.TemporaryDirectory() as t:
            p = os.path.join(t, "vm.conf")
            open(p, "w").write("# c\nhost = user@10.0.0.1\nkey=~/k  # mine\nbogus = 1\n")
            c = vmlib.read_conf(p, env={})
            self.assertEqual(c["host"], "user@10.0.0.1")
            self.assertEqual(c["key"], os.path.expanduser("~/k"))
            self.assertEqual(c["lock"], r"C:\claude\quiet.lock")
            c = vmlib.read_conf(p, env={"MS_VM": "user@10.0.0.2"})
            self.assertEqual(c["host"], "user@10.0.0.2")
            c = vmlib.read_conf(os.path.join(t, "none"), env={})
            self.assertEqual(c["host"], "")

    def test_no_host_refused(self):
        os.environ.pop("MS_VM", None)
        old = os.environ.get("MS_VM_CONF")
        os.environ["MS_VM_CONF"] = "/nonexistent/vm.conf"
        try:
            with self.assertRaises(vmlib.VMError):
                vmlib.VM()
        finally:
            if old is None:
                del os.environ["MS_VM_CONF"]
            else:
                os.environ["MS_VM_CONF"] = old

    def test_guards(self):
        for bad in ("", "a b", "x&del", "../x", "-x", "a" * 65):
            with self.assertRaises(vmlib.VMError):
                vmlib.check_name(bad)
        vmlib.check_name("solo-clicks_2.a")
        for bad in (r"C:\claude", r"C:\claude\heardcheck", r"C:\claude\a\..", r"C:\claude\a\b c", r"C:\Windows\a\b",
                    r"C:\claude\a\b&x", ""):
            with self.assertRaises(vmlib.VMError):
                vmlib.check_vmdir(bad)
        vmlib.check_vmdir(r"C:\claude\heardcheck\hc-20261021-1")


class Locking(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.pause = os.path.join(self.t.name, "VM_PAUSE")
        self.vm = FakeVM(self.pause)
        self.logs = []

    def tearDown(self):
        self.t.cleanup()

    def lock(self, name, sleep=None):
        return vmlib.Lock(self.vm, name, log=self.logs.append, poll=1, sleep=sleep or (lambda s: None))

    def test_take_release(self):
        L = self.lock("jobA")
        self.assertTrue(L.acquire())
        self.assertTrue(L.taken)
        self.assertEqual(self.vm.files[r"C:\claude\quiet.lock"], "jobA")
        self.assertEqual(open(self.pause).read().strip(), "jobA")
        L.release()
        self.assertNotIn(r"C:\claude\quiet.lock", self.vm.files)
        self.assertFalse(os.path.exists(self.pause))

    def test_waits_for_other_and_never_deletes_it(self):
        self.vm.files[r"C:\claude\quiet.lock"] = "other"
        polls = []

        def sleep(s):
            polls.append(s)
            if len(polls) == 3:
                del self.vm.files[r"C:\claude\quiet.lock"]      # the other job releases
        L = self.lock("jobA", sleep)
        # a release by a job that doesn't hold it leaves the other's lock
        self.vm.files[r"C:\claude\quiet.lock"] = "other"
        M = self.lock("jobA")
        M.taken = True
        M.release()
        self.assertEqual(self.vm.files[r"C:\claude\quiet.lock"], "other")
        self.assertTrue(L.acquire())
        self.assertGreaterEqual(len(polls), 3)
        self.assertEqual(self.vm.files[r"C:\claude\quiet.lock"], "jobA")
        self.assertTrue(any("waiting for other" in x for x in self.logs))

    def test_local_pause_blocks(self):
        open(self.pause, "w").write("someone\n")
        t = [0.0]
        L = vmlib.Lock(self.vm, "jobA", log=self.logs.append, poll=10, sleep=lambda s: t.__setitem__(0, t[0] + s),
                       now=lambda: t[0])
        self.assertFalse(L.acquire(max_wait=30))
        self.assertNotIn(r"C:\claude\quiet.lock", self.vm.files)
        self.assertEqual(open(self.pause).read().strip(), "someone")

    def test_nested(self):
        A = self.lock("jobA")
        A.acquire()
        B = self.lock("jobA")
        self.assertTrue(B.acquire())
        self.assertTrue(B.nested)
        B.release()                                     # nested: leaves it
        self.assertEqual(self.vm.files[r"C:\claude\quiet.lock"], "jobA")
        A.release()
        self.assertNotIn(r"C:\claude\quiet.lock", self.vm.files)

    def test_lost_race(self):
        # someone writes the lock between our write and our read-back
        seq = []
        orig = self.vm.write

        def write(path, text):
            orig(path, text)
            if not seq:
                seq.append(1)
                self.vm.files[path] = "racer"
        self.vm.write = write
        n = [0]

        def sleep(s):
            n[0] += 1
            if n[0] == 4:
                del self.vm.files[r"C:\claude\quiet.lock"]
        L = self.lock("jobA", sleep)
        self.assertTrue(L.acquire())
        self.assertTrue(any("lost a race" in x for x in self.logs))
        self.assertEqual(self.vm.files[r"C:\claude\quiet.lock"], "jobA")


class Job(unittest.TestCase):
    def setUp(self):
        self.t = tempfile.TemporaryDirectory()
        self.pause = os.path.join(self.t.name, "VM_PAUSE")
        self.job = os.path.join(self.t.name, "job")
        os.makedirs(self.job)
        open(os.path.join(self.job, "in.txt"), "w").write("input")
        self.out = os.path.join(self.t.name, "out")
        self.logs = []

    def tearDown(self):
        self.t.cleanup()

    def run_job(self, vm, **kw):
        a = args(job=self.job, out=self.out, **kw)
        lf = lambda vm_, name, log, poll: vmlib.Lock(vm_, name, log=log, poll=poll, sleep=lambda s: None)
        return vmjob.run_job(vm, a, self.logs.append, lock_factory=lf, sleep=lambda s: None)

    def test_crash_retried_then_ok_fetch_and_delete(self):
        vm = FakeVM(self.pause, [(-1073741819, []), (0, ["out\\r.wav"])])
        st, att = self.run_job(vm, fetch=["out"], expect=["out\\r.wav"])
        self.assertEqual(st, 0)
        self.assertEqual([a["exit"] for a in att], [-1073741819, 0])
        self.assertIn("0xC0000005", att[0]["retry"])
        self.assertTrue(os.path.exists(os.path.join(self.out, "out", "r.wav")))
        self.assertFalse(any(f.startswith(r"C:\claude\vmjob\t1") for f in vm.files), vm.files)
        self.assertNotIn(r"C:\claude\quiet.lock", vm.files)
        rc = [c for c in vm.commands if c.endswith('run.cmd"')]
        self.assertEqual(len(rc), 2)

    def test_input_copied(self):
        vm = FakeVM(self.pause, [(0, [])])
        vm.rmdir_guarded = lambda p, m: True             # keep the files to look at
        self.run_job(vm)
        self.assertEqual(vm.files[r"C:\claude\vmjob\t1\in.txt"], "input")
        self.assertEqual(vm.files[r"C:\claude\vmjob\t1\.vmjob-run"], "t1")
        self.assertIn(r"x.exe C:\claude\vmjob\t1\in", vm.files[r"C:\claude\vmjob\t1\run.cmd"])

    def test_gives_up_after_retries(self):
        vm = FakeVM(self.pause, [(-1073740791, [])] * 3)
        st, att = self.run_job(vm, retries=2)
        self.assertEqual(st, 128)
        self.assertEqual(len(att), 3)
        self.assertNotIn(r"C:\claude\quiet.lock", vm.files)

    def test_timeout_kills_and_retries(self):
        vm = FakeVM(self.pause, [(None, []), (3, [])])
        st, att = self.run_job(vm, kill=["MuseScore3Evo.exe"])
        self.assertEqual(vm.killed, ["MuseScore3Evo.exe"])
        self.assertEqual(st, 3)                          # an ordinary exit code isn't retried
        self.assertEqual(len(att), 2)

    def test_missing_output_retried(self):
        vm = FakeVM(self.pause, [(0, []), (0, ["res.json"])])
        st, att = self.run_job(vm, expect=["res.json"])
        self.assertEqual(st, 0)
        self.assertIn("missing res.json", att[0]["retry"])

    def test_existing_run_folder_refused(self):
        vm = FakeVM(self.pause, [(0, [])])
        vm.files[r"C:\claude\vmjob\t1\old.txt"] = "x"
        st, att = self.run_job(vm)
        self.assertEqual(st, 2)
        self.assertEqual(att, [])
        self.assertEqual(vm.files[r"C:\claude\vmjob\t1\old.txt"], "x")
        self.assertNotIn(r"C:\claude\quiet.lock", vm.files)

    def test_run_cmd_text(self):
        s = vmjob.run_cmd_text("a.exe b", [("MS_VERIFY_ROUTES", "1")])
        self.assertIn('set "MS_VERIFY_ROUTES=1"\r\n', s)
        self.assertIn("call a.exe b\r\n", s)
        self.assertIn('(echo %ERRORLEVEL%)> "%~dp0.vmjob.rc"', s)


if __name__ == "__main__":
    unittest.main()
