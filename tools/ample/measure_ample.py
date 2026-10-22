#!/usr/bin/env python3
"""Measure an Ample guitar plug-in on the Windows VM with amphost; write the numbers gen_ample.py needs.

    measure_ample.py --product <AGM|AGSJ|AG12|AGL|AGT> --out <private folder> [--keep-wav] <experiment>…
    measure_ample.py --product AGM --out ~/MuseScore/ms3fork/private/ample-pipeline/agm all

Needs AMPLE_VM (tools/ample/amplevm.py), the plug-in installed and activated on the VM, amphost.exe built
(tools/soundlibraries/amphost/build.sh; --amphost <exe>, default /tmp/amphost-build/amphost.exe, copied to the VM).
Every render goes under the VM's quiet.lock. Results: <out>/<experiment>.json (numbers only) and a line per finding on
stdout; `merge` writes the numbers gen_ample.py reads into tools/soundlibraries/ample_<product>.json (only measured keys;
the rest stay as they are: docs/AMPLE_PIPELINE.md says which carry over).
The folder <out> holds the plug-in's states and dumps (the vendor's data): keep it outside the repository.

Experiments (each says what it fills in ample_<product>.json):
  dump        amphost info + the default state: parameters (the generator's input), DBName, version, state names,
              MIDI-learn map, velocity layers                                          -> dump.txt, default.state, facts
  sets        each sample set's state (DBName codes from products.py, or --sets); a set that loads silent is reported
                                                                                       -> sampleSets
  keys        every key 0-127 tapped before a test note, against the plain note: which keys change the sound
              (keyswitch zone, slide guitar key)                                       -> keyswitches (check)
  range       each articulation's playable notes (28-100): silent / sounding           -> ranges
  velocity    velocity 1-127 on a test note: level curve, the velocity where the timbre jumps (Pop)  -> maxVelocity
  cc          CC1 / CC7 / CC11 / CC64 and M-Master Vol on a held note; CC11 re-mapped by MIDI-learn
                                                                                       -> cc (finding: CC11)
  strum       strum keys' velocity bands and the CC7 at which the recorded strum equals a six-string roll, per set
                                                                                       -> strumLevels
  softroll    single-string-key rolls at v20 / v40 and 10 / 15 ms against the CC7-lowered strum  -> softRoll
  capo        M-Capo change at offsets before a note, block sizes 64-2048              -> capoLead
  bgload      a note after pre-rolls of 0-8 s after setState, against a settled render, per set  -> backgroundLoad
  demo15      strums repeated for 70 s in variants (period, tempo, transport, pacing, network): the quiet / dark strums
  crash       N loads of the plug-in (amphost), network allowed / blocked by a firewall rule on amphost.exe
  stateclamp  every state setting written past its assumed ends, loaded, saved: what the plug-in keeps -> stateRanges
"""

import argparse
import json
import os
import statistics
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amplevm  # noqa: E402
import ample_state  # noqa: E402
import audio  # noqa: E402
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "soundlibraries"))
import ample_products as products  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))


class Ctx:
    def __init__(self, a):
        self.product = products.get(a.product)
        self.out = os.path.abspath(os.path.expanduser(a.out))
        os.makedirs(self.out, exist_ok=True)
        self.keep = a.keep_wav
        self.amphost = a.amphost
        self.vmdir = amplevm.DIR + "\\" + self.product["code"]
        self.plugin = a.plugin or self.product["vst3Path"]
        self.n = a.n
        self.merge_to = a.merge_to
        self.results = {}
        ss = self.product["sampleSets"]
        self.sets = ss if isinstance(ss, list) else self.product["sampleSetsGuess"]
        self.ready = False

    def setup(self):
        if self.ready:
            return
        amplevm.mkdir(self.vmdir)
        amplevm.put(self.amphost, amplevm.DIR + "\\amphost.exe")
        self.ready = True

    def local(self, name):
        return os.path.join(self.out, name)

    def remote(self, name):
        return self.vmdir + "\\" + name

    def save(self, exp, data):
        with open(self.local(exp + ".json"), "w") as f:
            json.dump(data, f, indent=1, sort_keys=True, default=lambda o: o.item() if hasattr(o, "item") else str(o))
        self.results[exp] = data


def amp(ctx, args):
    return '"%s" %s' % (ctx.plugin, args)


def render(ctx, jobs, secs=240):
    """jobs: [(name, events list, options)] -> {name: (sr, x)} (renders under the lock, one VM call)"""
    ctx.setup()
    with tempfile.TemporaryDirectory() as tmp:
        for name, evs, _ in jobs:
            p = os.path.join(tmp, name + ".txt")
            with open(p, "w") as f:
                f.write("\n".join(evs) + "\n")
            amplevm.put(p, ctx.remote(name + ".txt"))
        lines = []
        for name, _, opts in jobs:
            args = amp(ctx, 'render "%s" "%s" %s' % (ctx.remote(name + ".txt"), ctx.remote(name + ".wav"), opts))
            lines.append(r"$r = & {%s}; '%s ' + $r" % (watch_script(amplevm.DIR + "\\amphost.exe", args,
                                                                     ctx.remote(name), secs), name))
        with amplevm.Lock():
            out = amplevm.ps("\n".join(lines))
        status = dict(l.split(" ", 1) for l in out.strip().splitlines() if l.strip())
        res = {}
        for name, _, _ in jobs:
            st = status.get(name, "?")
            if not st.startswith("exit 0"):
                print("  %s: %s" % (name, st), file=sys.stderr)
                res[name] = None
                continue
            lp = os.path.join(tmp, name + ".wav")
            amplevm.get(ctx.remote(name + ".wav"), lp)
            res[name] = audio.load(lp)
            if ctx.keep:
                os.replace(lp, ctx.local(name + ".wav"))
        amplevm.ps("Remove-Item '%s\\*.wav' -ea 0" % ctx.vmdir, check=False)
    return res


def watch_script(exe, args, log, secs):
    return r"""$t0 = Get-Date
$p = Start-Process -FilePath '%s' -ArgumentList '%s' -NoNewWindow -PassThru -RedirectStandardError '%s.err'
$h = $p.Handle
while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt %d) { Start-Sleep -Milliseconds 200 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep 1; 'KILLED' } else { 'exit ' + $p.ExitCode + (' {0:N1}' -f ((Get-Date) - $t0).TotalSeconds) }""" % (
        exe, args.replace("'", "''"), log, secs)


def state_path(ctx, set_name):
    return ctx.remote("set_%s.state" % set_name)


# ---- events ----

def ev(t, kind, a, b=0):
    return "%.5f %s %s %s" % (t, kind, a, b)


def tap(t, key, vel=100):
    return [ev(t, "on", key, vel), ev(t, "off", key, 0)]


def strum_events(ctx, t, chord=(40, 44, 47), vel=110, cc7=None, length=0.4979, key=None):
    g = ctx.product["guitar"]
    out = []
    if cc7 is not None:
        out.append(ev(t, "cc", 7, cc7))
    out += tap(t, g["sustainKey"])
    out += tap(t, g["detect"], g["detectVelocity"]) + tap(t, g["strumMode"], g["strumOn"])
    out += [ev(t, "on", n, 100) for n in chord] + [ev(t, "on", key or g["strumKeys"][0], vel)]
    e = t + length
    out += [ev(e, "off", n, 0) for n in chord] + [ev(e, "off", key or g["strumKeys"][0], 0)]
    out += tap(e, g["strumMode"], g["strumOff"])
    return out


def note(t, pitch, vel=100, length=1.0):
    return [ev(t, "on", pitch, vel), ev(t + length, "off", pitch, 0)]


# ---- experiments ----

def x_dump(ctx):
    ctx.setup()
    with amplevm.Lock():
        st, t = amplevm.run_watched(amplevm.DIR + "\\amphost.exe",
                                    amp(ctx, 'info "%s" "%s"' % (ctx.remote("dump.txt"), ctx.remote("default.state"))),
                                    ctx.remote("dump"), 180)
    if st != "exit 0":
        raise SystemExit("dump: " + st)
    amplevm.get(ctx.remote("dump.txt"), ctx.local("dump.txt"))
    amplevm.get(ctx.remote("default.state"), ctx.local("default.state"))
    s = ample_state.summary(ample_state.load(ctx.local("default.state"))[1])
    params = [l for l in open(ctx.local("dump.txt"), encoding="utf-8") if "\tid=" in l]
    real = [l for l in params if "\tMIDI CC " not in l]
    facts = {"version": s["root"].get("Ver"), "dbName": s["dbName"], "controls": s["controls"], "layers": s["layers"],
             "stateParameters": len(s["parameters"]), "vst3Parameters": len(real),
             "stateOnly": sorted(p["name"] for p in s["parameters"]
                                 if p["name"] not in set(l.split("\t")[2] for l in real))}
    ctx.save("dump", facts)
    print("dump: version %s, %d VST 3 parameters (no MIDI CC proxies), %d state parameters (%d state only), DBName %s"
          % (facts["version"], len(real), facts["stateParameters"], len(facts["stateOnly"]), facts["dbName"]))


def x_sets(ctx):
    """a state per sample set (DBName); the first note's level tells a set that loads"""
    if not os.path.exists(ctx.local("default.state")):
        x_dump(ctx)
    jobs = []
    for name, db in ctx.sets:
        p = ctx.local("set_%s.state" % name)
        d, xml, rest = ample_state.load(ctx.local("default.state"))
        ample_state.save(p, d, ample_state.set_value(xml, "DBName", db), rest)
        ctx.setup()
        amplevm.put(p, state_path(ctx, name))
        jobs.append(("sets_" + name, note(0.5, 52, 100, 1.5), "--pre 10 --tail 1 --state \"%s\"" % state_path(ctx, name)))
    res = render(ctx, jobs)
    out = {}
    for name, db in ctx.sets:
        r = res.get("sets_" + name)
        lv = audio.loud50(audio.mono(r[1]), r[0], 0.5, 1.0) if r else None
        out[name] = {"dbName": db, "level": lv, "loads": lv is not None and lv > -80}
        print("sets: %s (%s) E3 v100 %s" % (name, db, "%.1f dB" % lv if lv is not None else "render failed"))
    ctx.save("sets", out)


def need_sets(ctx):
    if not all(os.path.exists(ctx.local("set_%s.state" % n)) for n, _ in ctx.sets):
        x_sets(ctx)
    ctx.setup()


def first_set(ctx):
    return ctx.sets[0][0]


def strum_set(ctx):
    for n, _ in ctx.sets:
        if n == "Strum":
            return n
    return first_set(ctx)


def x_demo15(ctx):
    """the quiet / dark strums at multiples of 15 s of render time (ample-wincheck FIX2): which clock drives them"""
    need_sets(ctx)
    s = state_path(ctx, first_set(ctx))
    variants = [
        ("d_every2", 2.0, "--pre 15"),
        ("d_every3", 3.0, "--pre 15"),
        ("d_every1_5", 1.5, "--pre 15"),
        ("d_pre3", 2.0, "--pre 3"),
        ("d_pre17", 2.0, "--pre 17"),
        ("d_tempo90", 2.0, "--pre 15 --tempo 90"),
        ("d_stopped", 2.0, "--pre 15 --stopped"),
        ("d_rt", 2.0, "--pre 15 --rt"),
        ("d_odd", 2.0, "--pre 15 --odd"),
    ]
    jobs = []
    times = {}
    for name, period, opts in variants:
        start = 1.0 if "--odd" in opts else 0.0
        opts = opts.replace("--odd", "")
        ts = [start + k * period for k in range(int(70 / period) + 1) if start + k * period <= 70]
        evs = []
        for t in ts:
            evs += strum_events(ctx, t, cc7=36)
        times[name] = ts
        jobs.append((name, evs, "%s --tail 2 --state \"%s\"" % (opts, s)))
    res = render(ctx, jobs, secs=400)
    out = {}
    for name, _, _ in jobs:
        r = res.get(name)
        if not r:
            continue
        sr, x = r[0], audio.mono(r[1])
        rows = [(t, audio.loud50(x, sr, t, 0.5), audio.centroid(x, sr, t, 0.3)) for t in times[name]]
        med = statistics.median(l for _, l, _ in rows)
        quiet = [(t, round(l - med, 1), round(c)) for t, l, c in rows if l < med - 3]
        out[name] = {"median": round(med, 1), "quiet": quiet}
        print("demo15 %s: median %.1f dB; quiet: %s" % (name, med, ", ".join("%.1f s %+.1f dB %d Hz" % q for q in quiet)
                                                         or "none"))
    ctx.save("demo15", out)


def firewall(block, program):
    name = "ample-pipeline block"
    if block:
        amplevm.ps("New-NetFirewallRule -DisplayName '%s' -Direction Outbound -Program '%s' -Action Block | Out-Null"
                   % (name, program))
    else:
        amplevm.ps("Remove-NetFirewallRule -DisplayName '%s' -ea 0" % name, check=False)


def x_crash(ctx):
    """N loads (amphost load with the first set's state) with the network allowed, then blocked for amphost.exe"""
    need_sets(ctx)
    s = state_path(ctx, first_set(ctx))
    exe = amplevm.DIR + "\\amphost.exe"
    out = {}
    for cond in ("network", "blocked"):
        lines = []
        for i in range(ctx.n):
            lines.append(r"$r = & {%s}; 'load%d ' + $r" % (watch_script(exe, amp(ctx, 'load --state "%s"' % s),
                                                                         ctx.remote("crash%d" % i), 120), i))
        with amplevm.Lock():
            try:
                if cond == "blocked":
                    firewall(True, exe)
                txt = amplevm.ps("\n".join(lines))
            finally:
                firewall(False, exe)
        rows = [l.split(" ", 1)[1] for l in txt.strip().splitlines() if l.startswith("load")]
        bad = [r for r in rows if not r.startswith("exit 0")]
        secs = [float(r.split()[-1].replace(",", ".")) for r in rows if r.startswith("exit 0")]
        out[cond] = {"runs": len(rows), "failed": len(bad), "failures": bad,
                     "seconds_median": round(statistics.median(secs), 2) if secs else None}
        print("crash %s: %d of %d loads failed %s; median %.2f s" % (cond, len(bad), len(rows), bad[:5],
                                                                     out[cond]["seconds_median"] or 0))
    ctx.save("crash", out)


def x_cc(ctx):
    """CC1 / 7 / 11 / 64 on a held E3, and CC11 mapped by MIDI-learn (Control ID 5 + 11 = 16) onto M-Master Vol"""
    need_sets(ctx)
    base = ctx.local("set_%s.state" % first_set(ctx))
    d, xml, rest = ample_state.load(base)
    mapped = ctx.local("cc11_learn.state")
    ample_state.save(mapped, d, ample_state.set_value(xml, "Control[ID=16]@Parameter", "M-Master Vol"), rest)
    amplevm.put(mapped, ctx.remote("cc11_learn.state"))
    vals = [127, 64, 32, 8]
    rows = []
    evs = []
    t = 0.5
    for cc in (7, 11, 1, 64):
        for v in vals:
            evs += [ev(t - 0.3, "cc", cc, v)] + note(t, 52, 100, 1.2) + [ev(t + 1.6, "cc", cc, 64 if cc == 7 else
                                                                          (127 if cc == 11 else 0))]
            rows.append((cc, v, t))
            t += 2.0
    jobs = [("cc_plain", evs, "--pre 10 --tail 1 --state \"%s\"" % state_path(ctx, first_set(ctx))),
            ("cc_learn", evs, "--pre 10 --tail 1 --state \"%s\"" % ctx.remote("cc11_learn.state"))]
    res = render(ctx, jobs)
    out = {}
    for name in ("cc_plain", "cc_learn"):
        r = res.get(name)
        if not r:
            continue
        sr, x = r[0], audio.mono(r[1])
        lv = {}
        for cc, v, t0 in rows:
            lv.setdefault(str(cc), {})[str(v)] = {"loud50": round(audio.loud50(x, sr, t0, 1.0), 2),
                                                  "late": round(audio.rms(x, sr, t0 + 0.6, 0.4), 2)}
        out[name] = lv
        for cc in lv:
            print("cc %s CC%s: %s" % (name, cc, ", ".join("%s: %.1f" % (v, lv[cc][v]["loud50"]) for v in lv[cc])))
    ctx.save("cc", out)


def x_bgload(ctx):
    """a note after a pre-roll of p s (real-time paced) from setState, against p = 10: how long the set's load takes"""
    need_sets(ctx)
    pres = [0.05, 0.25, 0.5, 1.0, 1.5, 2.0, 3.0, 5.0, 10.0]
    out = {}
    for name, _ in ctx.sets:
        jobs = [("bg_%s_%g" % (name, p), note(0.0, 55, 100, 1.0), "--pre %g --tail 0.5 --state \"%s\"" %
                 (p, state_path(ctx, name))) for p in pres]
        res = render(ctx, jobs)
        ref = res.get("bg_%s_10" % name)
        rows = {}
        for p in pres:
            r = res.get("bg_%s_%g" % (name, p))
            if not r or not ref:
                continue
            rows[str(p)] = {"loud50": round(audio.loud50(audio.mono(r[1]), r[0], 0, 1), 2),
                            "null": round(audio.null_db(audio.mono(ref[1]), audio.mono(r[1])), 1)}
        settled = [p for p in pres if str(p) in rows and rows[str(p)]["null"] < -60]
        out[name] = {"rows": rows, "settled_after": settled[0] if settled else None}
        print("bgload %s: %s; settled from %s s" % (name, ", ".join("%g s %.1f dB null %.0f" % (float(p), v["loud50"],
                                                                                              v["null"])
                                                                      for p, v in rows.items()), out[name]["settled_after"]))
    ctx.save("bgload", out)


def assumed_settings():
    sys.path.insert(0, os.path.join(REPO, "tools", "soundlibraries"))
    import gen_ample
    return gen_ample.state_settings()


def x_stateclamp(ctx):
    """every state setting at its assumed ends (the values the map's lanes can write), then past them; each probe state
    loaded and saved again (amphost info): whether the plug-in loads it (a crash is a finding: bisected to the settings
    that cause it) and what it keeps"""
    need_sets(ctx)
    rows = assumed_settings()
    d, xml, rest = ample_state.load(ctx.local("set_%s.state" % first_set(ctx)))
    have = ample_state.items(xml)
    probes = {"min": {}, "max": {}, "below": {}, "above": {}}
    for name, cid, shown, kind, spec, default in rows:
        key = "Parameter:" + name
        if key not in have:
            continue
        if kind == "list":
            nums = [float(w) for w, _ in spec]
            lo, hi, step = min(nums), max(nums), 1
        else:
            lo, hi, dec = spec
            step = 10 ** -dec if dec else 1
        probes["min"][key] = fmtnum(lo)
        probes["max"][key] = fmtnum(hi)
        probes["below"][key] = fmtnum(lo - step)
        probes["above"][key] = fmtnum(hi + step)
    exe = amplevm.DIR + "\\amphost.exe"
    counter = [0]

    def load(vals):
        """(loaded, kept values) of a state with vals set"""
        counter[0] += 1
        n = "clamp%d" % counter[0]
        x2 = xml
        for k, v in vals.items():
            x2 = ample_state.set_value(x2, k, v)
        lp = ctx.local(n + ".state")
        ample_state.save(lp, d, x2, rest)
        amplevm.put(lp, ctx.remote(n + ".state"))
        st = amplevm.ps(r"$r = & {%s}; $r" % watch_script(exe, amp(ctx, 'info "%s" "%s" --state "%s"' % (
            ctx.remote(n + ".txt"), ctx.remote(n + "_out.state"), ctx.remote(n + ".state"))), ctx.remote(n), 120)).strip()
        os.unlink(lp)
        if not st.startswith("exit 0"):
            return False, {}
        lo = ctx.local(n + "_out.state")
        amplevm.get(ctx.remote(n + "_out.state"), lo)
        got = ample_state.items(ample_state.load(lo)[1])
        os.unlink(lo)
        return True, got

    def bisect(vals, out, probe):
        ok, got = load(vals)
        if ok:
            for k, v in vals.items():
                out.setdefault(k[10:], {})[probe] = {"written": v, "kept": got.get(k)}
            return
        if len(vals) == 1:
            k, v = next(iter(vals.items()))
            out.setdefault(k[10:], {})[probe] = {"written": v, "kept": None, "crash": True}
            return
        items = sorted(vals.items())
        half = len(items) // 2
        bisect(dict(items[:half]), out, probe)
        bisect(dict(items[half:]), out, probe)

    out = {}
    with amplevm.Lock():
        for probe in ("min", "max", "below", "above"):
            bisect(probes[probe], out, probe)
    crash = {k: [p for p in v if v[p].get("crash")] for k, v in out.items() if any(v[p].get("crash") for p in v)}
    changed = {k: {p: v[p]["kept"] for p in v} for k, v in out.items()
               if any(not v[p].get("crash") and v[p]["kept"] != v[p]["written"] for p in v)}
    ends_ok = [k for k, v in out.items() if all(not v.get(p, {}).get("crash") for p in ("min", "max"))]
    print("stateclamp: %d settings; at the assumed ends %d load; crashes: %s; kept differently: %s; %d loads" % (
        len(out), len(ends_ok), crash or "none", changed or "none", counter[0]))
    ctx.save("stateclamp", {"settings": out, "crash": crash, "changed": changed, "loads": counter[0]})


def fmtnum(v):
    return ("%d" % v) if float(v).is_integer() else ("%g" % v)


def x_demofine(ctx):
    """demo15's follow-up: how exactly a strum must start on a multiple of 7.5 s, block size, the strummer's humanize
    parameters at 0, plain chords (no strummer) at those times"""
    need_sets(ctx)
    s = state_path(ctx, first_set(ctx))
    offs = [0.0, 0.001, 0.005, 0.02, 0.1, -0.001, -0.02]
    ts = []
    for k, base in enumerate((7.5, 15.0, 22.5, 30.0, 37.5, 45.0, 52.5)):
        ts.append(base + offs[k])
    ctrl = [b + 1.5 for b in (7.5, 15.0, 22.5, 30.0)]          # (control strums off the grid)
    evs = []
    for t in ts + ctrl:
        evs += strum_events(ctx, t, cc7=36)
    plain = []
    for t in (7.5, 15.0, 22.5, 30.0, 9.0, 16.5):
        plain += [ev(t, "on", n, 100) for n in (40, 47, 52, 56, 59, 64)] + [ev(t + 0.5, "off", n, 0) for n in (40, 47, 52, 56, 59, 64)]
    hum = []
    dump = open(ctx.local("dump.txt"), encoding="utf-8").read().splitlines()
    for l in dump:
        f = l.split("\t")
        if len(f) > 2 and f[2] in ("S-Hum ST All", "S-Hum ST Each", "S-Hum Strum Vel"):
            hum.append("--set %s=0" % f[1].split("=")[1])
    jobs = [("f_offsets", evs, "--pre 5 --tail 2 --state \"%s\"" % s),
            ("f_block64", evs, "--pre 5 --tail 2 --block 64 --state \"%s\"" % s),
            ("f_nohum", evs, "--pre 5 --tail 2 %s --state \"%s\"" % (" ".join(hum), s)),
            ("f_plain", plain, "--pre 5 --tail 2 --state \"%s\"" % s)]
    res = render(ctx, jobs, secs=400)
    out = {}
    for name, _, _ in jobs:
        r = res.get(name)
        if not r:
            continue
        sr, x = r[0], audio.mono(r[1])
        when = (ts + ctrl) if name != "f_plain" else [7.5, 15.0, 22.5, 30.0, 9.0, 16.5]
        rows = [(round(t, 3), round(audio.loud50(x, sr, t, 0.5), 1), round(audio.centroid(x, sr, t, 0.3))) for t in when]
        out[name] = rows
        print("demofine %s: %s" % (name, "; ".join("%.3f %.1f dB %d Hz" % r for r in rows)))
    out["humanizeSet"] = hum
    ctx.save("demofine", out)


def param_id(ctx, title):
    if not os.path.exists(ctx.local("dump.txt")):
        x_dump(ctx)
    for l in open(ctx.local("dump.txt"), encoding="utf-8"):
        f = l.rstrip("\n").split("\t")
        if len(f) > 2 and f[2] == title:
            return int(f[1].split("=")[1])
    return None


def lsd(x, sr, t, y, sr2, u, d=0.6):
    """log-spectral distance (dB) of two notes, 1/6 octave bands 80 Hz - 8 kHz, peak-normalised (meas2 B)"""
    import numpy as np
    def bands(z, rate, t0):
        f, S = audio.spectrum(z, rate, t0, d)
        edges = 80 * 2 ** (np.arange(0, 40) / 6.0)
        edges = edges[edges < 8000]
        b = np.array([S[(f >= lo) & (f < hi)].sum() + 1e-12 for lo, hi in zip(edges[:-1], edges[1:])])
        b = 20 * np.log10(b)
        return b - b.max()
    a, b = bands(x, sr, t), bands(y, sr2, u)
    return float(np.sqrt(np.mean((a - b) ** 2)))


def x_keys(ctx):
    """each key outside the playing range tapped (after a Sustain tap) 0.1 s before G3: does the note change?"""
    need_sets(ctx)
    g = ctx.product["guitar"]
    keys = [k for k in range(0, 128) if k < 40 or k > 88]
    evs, at = [], {}
    t = 1.0
    for rep in range(2):                     # two plain references (the round robins alternate)
        evs += tap(t - 0.2, g["sustainKey"]) + note(t, 55, 100, 0.9)
        at["ref%d" % rep] = t
        t += 1.5
    for k in keys:
        evs += tap(t - 0.2, g["sustainKey"]) + tap(t - 0.1, k) + note(t, 55, 100, 0.9)
        at[k] = t
        t += 1.5
    jobs = [("keys", evs, "--pre 5 --tail 1 --state \"%s\"" % state_path(ctx, first_set(ctx)))]
    r = render(ctx, jobs, secs=600)["keys"]
    sr, x = r[0], audio.mono(r[1])
    refs = [at["ref0"], at["ref1"]]
    ref_l = [audio.loud50(x, sr, u, 0.9) for u in refs]
    out = {}
    for k in keys:
        u = at[k]
        l = audio.loud50(x, sr, u, 0.9)
        pre = audio.rms(x, sr, u - 0.1, 0.09)
        d = min(lsd(x, sr, u, x, sr, v) for v in refs)
        late = audio.rms(x, sr, u + 0.5, 0.4) - l
        changed = d > 4 or abs(l - statistics.mean(ref_l)) > 3 or pre > -60
        out[str(k)] = {"loud50": round(l, 1), "lsd": round(d, 1), "tapSound": round(pre, 1), "decay": round(late, 1),
                       "changed": bool(changed)}
    ch = [k for k in keys if out[str(k)]["changed"]]
    print("keys: changed the note or sounded: %s" % ch)
    ctx.save("keys", {"keys": out, "changed": ch, "refLoud50": [round(v, 1) for v in ref_l]})


def x_range(ctx):
    """each articulation key's playable notes (MIDI 28-100): sounding (loud50 > -70 dB) or not"""
    need_sets(ctx)
    g = ctx.product["guitar"]
    arts = [("sustain", g["sustainKey"]), ("harmonic", g["harmonic"]), ("palmMute", g["palmMute"])]
    if ctx.product["slideGuitar"]:
        arts.append(("slideGuitar", g["slideGuitarKey"]))
    jobs = []
    pitches = list(range(28, 101))
    for name, key in arts:
        evs = []
        for i, p in enumerate(pitches):
            t = 0.5 + i * 0.7
            evs += tap(t - 0.1, key) + note(t, p, 100, 0.5)
        jobs.append(("range_" + name, evs, "--pre 5 --tail 1 --state \"%s\"" % state_path(ctx, first_set(ctx))))
    res = render(ctx, jobs, secs=600)
    out = {}
    for name, _ in arts:
        r = res.get("range_" + name)
        if not r:
            continue
        sr, x = r[0], audio.mono(r[1])
        lv = {p: audio.loud50(x, sr, 0.5 + i * 0.7, 0.6) for i, p in enumerate(pitches)}
        sounding = [p for p in pitches if lv[p] > -70]
        out[name] = {"lowest": min(sounding) if sounding else None, "highest": max(sounding) if sounding else None,
                     "levels": {str(p): round(v, 1) for p, v in lv.items()}}
        print("range %s: sounds %s-%s" % (name, out[name]["lowest"], out[name]["highest"]))
    ctx.save("range", out)


def x_velocity(ctx):
    """G3 at velocities 1-127: level, centroid; the top velocity's timbre jump (Pop): maxVelocity"""
    need_sets(ctx)
    g = ctx.product["guitar"]
    vels = [1, 8, 16, 24, 32, 40, 48, 56, 64, 72, 80, 88, 96, 104, 112, 118, 122, 124, 125, 126, 127]
    evs = []
    for i, v in enumerate(vels):
        for rep in range(2):
            t = 0.5 + (2 * i + rep) * 1.2
            evs += tap(t - 0.1, g["sustainKey"]) + note(t, 55, v, 1.0)
    res = render(ctx, [("velocity", evs, "--pre 5 --tail 1 --state \"%s\"" % state_path(ctx, first_set(ctx)))])
    r = res["velocity"]
    sr, x = r[0], audio.mono(r[1])
    rows = {}
    for i, v in enumerate(vels):
        ts = [0.5 + (2 * i + rep) * 1.2 for rep in range(2)]
        rows[str(v)] = {"loud50": round(statistics.mean(audio.loud50(x, sr, t, 1.0) for t in ts), 1),
                        "centroid": round(statistics.mean(audio.centroid(x, sr, t, 0.3) for t in ts))}
    c126, c127 = rows["126"]["centroid"], rows["127"]["centroid"]
    jump = c127 > 1.5 * c126
    out = {"rows": rows, "popAt127": bool(jump), "maxVelocity": 126 if jump else 127}
    print("velocity: %s; centroid 126 %d Hz, 127 %d Hz -> maxVelocity %d" % (
        ", ".join("%s %.1f" % (v, rows[v]["loud50"]) for v in rows), c126, c127, out["maxVelocity"]))
    ctx.save("velocity", out)


def roll(t, keys, vel, gap, length=0.5):
    evs = []
    for i, k in enumerate(keys):
        evs.append(ev(t + i * gap, "on", k, vel))
    for k in keys:
        evs.append(ev(t + length, "off", k, 0))
    return evs


def strum_mode_on(ctx, t, chord=(40, 44, 47)):
    g = ctx.product["guitar"]
    return (tap(t, g["sustainKey"]) + tap(t, g["detect"], g["detectVelocity"]) + tap(t, g["strumMode"], g["strumOn"])
            + [ev(t, "on", n, 100) for n in chord])


def strum_mode_off(ctx, t, chord=(40, 44, 47)):
    g = ctx.product["guitar"]
    return [ev(t, "off", n, 0) for n in chord] + tap(t, g["strumMode"], g["strumOff"])


def x_strum(ctx):
    """per set: the strum key's velocity bands (level, centroid), the CC7 law of the recorded strum (v110), and the CC7 at
    which it equals a six-string key roll (15 ms) at v20..120: strumLevels"""
    need_sets(ctx)
    g = ctx.product["guitar"]
    ladder = [64, 54, 45, 38, 32, 27, 23, 19, 16, 14, 11, 10, 8, 7, 6, 5, 4, 3, 2]
    bandv = [1, 16, 31, 32, 48, 63, 64, 80, 95, 96, 110, 126, 127]
    rollv = [20, 40, 60, 80, 100, 120]
    out = {}
    for set_name, db in ctx.sets:
        evs, at = [], {}
        t = 0.5
        for v in bandv:
            evs += strum_events(ctx, t, vel=v, cc7=64)
            at["band%d" % v] = t
            t += 2.5
        for cc in ladder:
            evs += strum_events(ctx, t, vel=g.get("strumVelocity", 110), cc7=cc)
            at["cc%d" % cc] = t
            t += 2.5
        evs.append(ev(t - 0.5, "cc", 7, 64))
        for v in rollv:
            for rep in range(2):
                evs += strum_mode_on(ctx, t) + roll(t + 0.05, g["rollKeys"], v, 0.015, 1.0) + strum_mode_off(ctx, t + 1.2)
                at["roll%d_%d" % (v, rep)] = t + 0.05
                t += 2.5
        res = render(ctx, [("strum_" + set_name, evs, "--pre 10 --tail 1 --state \"%s\"" % state_path(ctx, set_name))],
                     secs=600)
        r = res.get("strum_" + set_name)
        if not r:
            continue
        sr, x = r[0], audio.mono(r[1])
        L = {k: audio.loud50(x, sr, u, 1.0) for k, u in at.items()}
        C = {k: audio.centroid(x, sr, u, 0.3) for k, u in at.items()}
        import numpy as np
        ccs = np.array(ladder, float)
        ls = np.array([L["cc%d" % c] for c in ladder])
        unity = float(np.median(ls - 20 * np.log10(ccs / 64)))
        resid = float(np.std(ls - 20 * np.log10(ccs / 64) - unity))
        levels = []
        for v in rollv:
            lr = statistics.mean(L["roll%d_%d" % (v, rep)] for rep in range(2))
            cc = int(round(64 * 10 ** ((lr - unity) / 20)))
            levels.append("%d:%d" % (v, max(1, min(127, cc))))
        out[set_name] = {"dbName": db, "bands": {str(v): [round(L["band%d" % v], 1), round(C["band%d" % v])] for v in bandv},
                         "unity": round(unity, 1), "gainResidualSd": round(resid, 2), "strumLevels": " ".join(levels),
                         "rolls": {str(v): round(statistics.mean(L["roll%d_%d" % (v, rep)] for rep in range(2)), 1)
                                   for v in rollv}}
        print("strum %s: unity %.1f dB (sd %.2f), levels %s; bands %s" % (set_name, unity, resid, " ".join(levels),
                                                                        out[set_name]["bands"]))
    ctx.save("strum", out)


def x_softroll(ctx):
    """key rolls at v20 / v40, gaps 5-25 ms, against the recorded strum at the set's StrumLevel CC7 (strum's result):
    the gap whose roll is as loud (softRoll pairs)"""
    need_sets(ctx)
    g = ctx.product["guitar"]
    st = ctx.results.get("strum") or (json.load(open(ctx.local("strum.json"))) if os.path.exists(ctx.local("strum.json"))
                                      else None)
    if not st:
        raise SystemExit("softroll needs strum first")
    name = strum_set(ctx)
    lv = dict(p.split(":") for p in st[name]["strumLevels"].split())
    gaps = [0.005, 0.010, 0.015, 0.020, 0.025]
    evs, at = [], {}
    t = 0.5
    for v in (20, 40):
        for rep in range(2):
            evs += strum_events(ctx, t, vel=g.get("strumVelocity", 110), cc7=int(lv[str(v)]))
            at["strum%d_%d" % (v, rep)] = t
            t += 2.5
        evs.append(ev(t - 0.5, "cc", 7, 64))
        for gp in gaps:
            for rep in range(2):
                evs += strum_mode_on(ctx, t) + roll(t + 0.05, g["rollKeys"], v, gp, 1.0) + strum_mode_off(ctx, t + 1.2)
                at["roll%d_%g_%d" % (v, gp, rep)] = t + 0.05
                t += 2.5
    r = render(ctx, [("softroll", evs, "--pre 10 --tail 1 --state \"%s\"" % state_path(ctx, name))], secs=600)["softroll"]
    sr, x = r[0], audio.mono(r[1])
    L = {k: audio.loud50(x, sr, u, 1.0) for k, u in at.items()}
    out = {}
    pairs = []
    for v in (20, 40):
        target = statistics.mean(L["strum%d_%d" % (v, rep)] for rep in range(2))
        rows = {str(int(gp * 1000)): round(statistics.mean(L["roll%d_%g_%d" % (v, gp, rep)] for rep in range(2)), 1)
                for gp in gaps}
        best = min(rows, key=lambda k: abs(rows[k] - target))
        out[str(v)] = {"strum": round(target, 1), "rolls": rows, "gap": int(best)}
        pairs.append("%d:%s" % (v, best))
    out["softRoll"] = " ".join(pairs)
    print("softroll (%s): %s; %s" % (name, out["softRoll"], {v: out[v] for v in ("20", "40")}))
    ctx.save("softroll", out)


def x_capo(ctx):
    """M-Capo 0 -> 2 sent d before G3 (f0 196 -> 220 Hz) at blocks 64-2048: the lead that always works (capoLead)"""
    need_sets(ctx)
    pid = param_id(ctx, "M-Capo")
    if pid is None:
        print("capo: no M-Capo parameter")
        ctx.save("capo", {"param": None})
        return
    leads = [0.0, 0.002, 0.012, 0.024, 0.047, 0.050]
    blocks = [64, 512, 2048]
    capo_max = 11
    norm2 = 2.0 / capo_max
    jobs, at = [], {}
    for b in blocks:
        evs = []
        t = 0.5
        for d in leads:
            evs += [ev(t - 0.5, "param", pid, 0)] + note(t - 0.25, 55, 100, 0.2)       # (capo 0 in force)
            evs += [ev(t - d, "param", pid, norm2)] + note(t, 55, 100, 0.8)
            at[(b, d)] = t
            t += 1.5
        jobs.append(("capo_%d" % b, evs, "--pre 5 --tail 1 --block %d --state \"%s\"" % (b, state_path(ctx, first_set(ctx)))))
    res = render(ctx, jobs)
    out = {}
    works = {}
    for b in blocks:
        r = res.get("capo_%d" % b)
        if not r:
            continue
        sr, x = r[0], audio.mono(r[1])
        for d in leads:
            f = audio.f0(x, sr, at[(b, d)] + 0.05, 0.5, 150, 300)
            ok = abs(f - 220) < 6
            out["%d_%g" % (b, d)] = round(f, 1)
            works.setdefault(d, []).append(ok)
    lead = [d for d in leads if all(works.get(d, [False]))]
    res_ = {"param": pid, "f0": out, "leastLeadAllBlocks": min(lead) if lead else None,
            "capoLead": int(round(max(0.05, min(lead) if lead else 0.05) * 1000))}
    print("capo: f0 %s; least lead working at every block %s s" % (out, res_["leastLeadAllBlocks"]))
    ctx.save("capo", res_)


def x_merge(ctx):
    """the measured numbers into tools/soundlibraries/ample_<code>.json (keeps the keys not measured now)"""
    path = ctx.merge_to or os.path.join(REPO, "tools", "soundlibraries", "ample_%s.json" % ctx.product["code"])
    cur = json.load(open(path)) if os.path.exists(path) else {"_about": "Ample %s, measured by tools/ample/measure_ample.py"
                                                                         % ctx.product["code"], "_sources": {}}
    src = cur.setdefault("_sources", {})
    day = time.strftime("%Y-%m-%d")

    def have(name):
        p = ctx.local(name + ".json")
        return json.load(open(p)) if os.path.exists(p) else None

    def put(key, value, how):
        cur[key] = value
        src[key] = "measure_ample.py %s, %s" % (how, day)

    d = have("dump")
    if d:
        put("version", d["version"], "dump")
        put("vst3Parameters", d["vst3Parameters"], "dump")
        put("stateOnly", len(d["stateOnly"]), "dump")
        put("controlDefaults", sorted([[int(k), v] for k, v in d["controls"].items()]), "dump")
        put("velocityLayers", sorted([[int(k), int(v)] for k, v in d["layers"].items()]), "dump")
    s = have("sets")
    if s:
        put("sampleSets", [[n, v["dbName"]] for n, v in s.items() if v["loads"]], "sets")
    v = have("velocity")
    if v:
        put("maxVelocity", v["maxVelocity"], "velocity")
    st = have("strum")
    if st:
        put("strumLevels", [[st[n]["dbName"], st[n]["strumLevels"]] for n in st], "strum")
    sr = have("softroll")
    if sr:
        put("softRoll", sr["softRoll"], "softroll")
    c = have("capo")
    if c and c.get("param"):
        put("capoLead", c["capoLead"], "capo")
    r = have("range")
    if r and "harmonic" in r:
        put("nhRange", [r["harmonic"]["lowest"], r["harmonic"]["highest"]], "range")
    k = have("keys")
    if k:
        put("keyswitchesSounding", k["changed"], "keys")
    with open(path, "w") as f:
        json.dump(cur, f, indent=1)
        f.write("\n")
    print("merge: %s (%d keys)" % (path, len(cur)))


EXPERIMENTS = {"dump": x_dump, "sets": x_sets, "demo15": x_demo15, "crash": x_crash, "cc": x_cc, "bgload": x_bgload,
               "stateclamp": x_stateclamp, "demofine": x_demofine,
               "keys": x_keys, "range": x_range, "velocity": x_velocity, "strum": x_strum, "softroll": x_softroll,
               "capo": x_capo, "merge": x_merge}
# what `all` runs, in order (crash and the demo experiments are diagnostics: by name only)
ALL = ["dump", "sets", "keys", "range", "velocity", "cc", "strum", "softroll", "capo", "bgload", "stateclamp"]


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--product", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--plugin", help="the plug-in's path on the VM (default: products.py)")
    ap.add_argument("--amphost", default="/tmp/amphost-build/amphost.exe")
    ap.add_argument("--keep-wav", action="store_true")
    ap.add_argument("--merge-to", help="merge: this file instead of tools/soundlibraries/ample_<code>.json (a regression "
                    "run against the committed numbers)")
    ap.add_argument("-n", type=int, default=30, help="crash: loads per condition")
    ap.add_argument("experiments", nargs="+")
    a = ap.parse_args(argv[1:])
    ctx = Ctx(a)
    names = ALL if a.experiments == ["all"] else a.experiments
    for n in names:
        if n not in EXPERIMENTS:
            raise SystemExit("unknown experiment %s (%s)" % (n, ", ".join(EXPERIMENTS)))
    for n in names:
        t0 = time.time()
        EXPERIMENTS[n](ctx)
        print("  (%s: %.0f s)" % (n, time.time() - t0), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
