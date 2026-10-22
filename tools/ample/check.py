#!/usr/bin/env python3
"""The Ample check: a Windows build of MuseScore renders the check scores through the real plug-in on the VM; numbers
decide PASS / FAIL; report.md + results.json. Generalises the private checks of builds #453 - #457 (2026-10-20 / 21).

    check.py --product AGM --build 'C:\\claude\\bpt\\ms' --out <private folder> [--ref <earlier results.json>]
             [--map <map.xml>] [--cases a,b,…] [--repeat 3] [--retries 2] [--no-live]

Needs AMPLE_VM (amplevm.py). --build: an installed Windows build (bin\\MuseScore3Evo.exe). --map copies the product's
map into the build's share\\soundlibraries first (a new guitar's map before a build carries it). --ref: an earlier
run's results.json: every render's hash is compared (bit-identical = PASS, else the levels are compared). Each run goes
under a watchdog (secs) and is retried on a crash (AGM's load crash, OPEN_ITEMS.md 12: a non-zero exit without a
report), up to --retries times; crashes are counted in the report. Safety: the plug-in's global settings file
(Documents\\Ample Sound\\<code>\\Configs\\<code>_info.xml) is hashed before and after (FAIL if it changed), and MuseScore's
`parameter values.json` files are restored if a run changed them. The scores: check_scores.py (made here, converted
by the build itself). Everything written (renders, logs, the VM folder's files) holds the vendor's audio: keep <out>
outside the repository.

Cases (score, playback settings, lanes; what is checked):
  tech-on     techniques, strums / softStrums / pinch 1, MS_VERIFY_ROUTES=1, --repeat runs: every run bit-identical;
              one route per sample set used (Finger, Pick, Strum); bar 21 (capo 2) G3 sounds A3 (220 Hz), bar 22 G3
  tech-off    techniques, the guitar settings 0: renders
  state0      state score, no lanes (the reference)
  velsens0    Vel Sensitivity 0: ppp G3 over 8 dB louder than state0's (the soft layers still play: not as loud as ff)
  layers      velocity layers 20 / 15 / 10: differs from state0
  voices1     Max Voices 1: the chord loses notes (its level below state0's)
  soft0/1/2   soft, strums 1, softStrums 0 / 1 / 2: the ppp strum at 7.0 s sounds (> -45 dB)
  vol         soft, softStrums 0, a volume lane 96 -> 48 -> 80: bar 3 (4-6 s) about 20 log10(80/96) from soft0's
  capostart   capostart: notes 1-4 A3 (220 Hz), 5-8 G3 (196 Hz)
  live        Create Live Set of vol, then Update twice: the second update writes nothing (the set's hash kept)
"""

import argparse
import json
import os
import re
import shutil
import statistics
import sys
import tempfile
import time
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import amplevm  # noqa: E402
import audio  # noqa: E402
import check_scores  # noqa: E402
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "soundlibraries"))
import ample_products  # noqa: E402

GUITAR_ON = "guitar/strums=1;guitar/softStrums=1;guitar/pinchHarmonics=1"
GUITAR_OFF = "guitar/strums=0;guitar/softStrums=0;guitar/pinchHarmonics=0"
VOL_LANE = [{"target": "volume", "points": [[0, 0.7559, "step"], [1920, 0.7559, "linear"], [3840, 0.378, "step"],
                                            [3840, 0.6299, "step"]]}]


def lane(target, value):
    return [{"target": target, "points": [[0, value, "step"]]}]


CASES = [
    # name, score, playback settings, lanes, routes
    ("tech-on", "techniques", GUITAR_ON, None, True),
    ("tech-off", "techniques", GUITAR_OFF, None, False),
    ("state0", "state", "", None, False),
    ("velsens0", "state", "", lane("vel-sensitivity", 0), False),
    ("layers", "state", "", lane("velocity-layer-1", 0.150794) + lane("velocity-layer-2", 0.111111)
     + lane("velocity-layer-3", 0.071429), False),
    ("voices1", "state", "", lane("max-voices", 0), False),
    ("soft0", "soft", "guitar/strums=1;guitar/softStrums=0", None, False),
    ("soft1", "soft", "guitar/strums=1;guitar/softStrums=1", None, False),
    ("soft2", "soft", "guitar/strums=1;guitar/softStrums=2", None, False),
    ("vol", "soft", "guitar/strums=1;guitar/softStrums=0", VOL_LANE, False),
    ("capostart", "capostart", "", None, False),
]


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace('"', "&quot;")


def patch_mscz(src, out, settings, lanes):
    """every arpeggio but the wavy one (bar 7) straight (strum lines), capo fretId on "capo N" texts, the parts on the
    library, the playback settings and lanes as metaTags"""
    def patch(text):
        n = [0]

        def arp(m):
            i = n[0]
            n[0] += 1
            body = m.group(0)
            return body if i == 4 and "techniques" in src else re.sub(
                r"<subtype>([12])</subtype>", lambda s: "<subtype>%d</subtype>" % (int(s.group(1)) + 3), body)
        text = re.sub(r"<Arpeggio>.*?</Arpeggio>", arp, text, flags=re.S)
        text = re.sub(r"(<StaffText>\s*(?:<[^>]*>\s*)*?<text>capo (\d+)</text>)",
                      lambda m: m.group(1) + '\n          <capo fretId="%d"/>' % (int(m.group(2)) + 1), text)
        names = [re.search(r"<trackName>([^<]*)</trackName>", p).group(1) for p in re.findall(r"<Part[ >].*?</Part>", text, re.S)]
        tags = {"partPlayback": json.dumps([{"part": i, "name": nm, "mode": "library"} for i, nm in enumerate(names)])}
        if settings:
            tags["playbackSettings"] = settings
        if lanes:
            tags["automation"] = json.dumps([{"part": 0, "name": names[0], "lanes": lanes}], separators=(",", ":"))
        text = re.sub(r'\s*<metaTag name="(partPlayback|playbackSettings|automation)">[^<]*</metaTag>', "", text)
        m = re.search(r'\n(\s*)<metaTag ', text)
        at = m.start() + 1
        return text[:at] + "".join('%s<metaTag name="%s">%s</metaTag>\n' % (m.group(1), k, esc(v)) for k, v in tags.items()) + text[at:]
    with zipfile.ZipFile(src) as z, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as o:
        for info in z.infolist():
            data = z.read(info.filename)
            if info.filename.endswith(".mscx"):
                data = patch(data.decode("utf-8")).encode("utf-8")
            o.writestr(info, data)


WATCH = r"""
function Run([string]$exe, [string]$argstr, [string]$log, [int]$secs, [string]$envs) {
  foreach ($kv in ($envs -split ';')) { if ($kv -match '^([^=]+)=(.*)$') { Set-Item -Path ("env:" + $matches[1]) -Value $matches[2] } }
  $t0 = Get-Date
  $p = Start-Process -FilePath $exe -ArgumentList $argstr -NoNewWindow -PassThru -RedirectStandardOutput "$log.out" -RedirectStandardError "$log.err"
  $h = $p.Handle
  while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt $secs) { Start-Sleep -Milliseconds 500 }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Start-Sleep 2; $s = "KILLED" } else { $s = "exit " + $p.ExitCode }
  foreach ($kv in ($envs -split ';')) { if ($kv -match '^([^=]+)=') { Remove-Item -Path ("env:" + $matches[1]) -ea 0 } }
  "{0} {1:N1}" -f $s, ((Get-Date) - $t0).TotalSeconds
}
"""


class Check:
    def __init__(self, a):
        self.p = ample_products.get(a.product)
        self.lib = self.p["name"]
        self.build = a.build.rstrip("\\")
        self.exe = self.build + "\\bin\\MuseScore3Evo.exe"
        self.out = os.path.abspath(os.path.expanduser(a.out))
        os.makedirs(self.out, exist_ok=True)
        self.vm = amplevm.DIR + "\\check-" + self.p["code"]
        self.a = a
        self.rows = []
        self.results = {"product": self.p["code"], "build": self.build, "cases": {}, "crashes": 0}
        # (renders on /tmp, deleted after the analysis unless --keep-wav: the dev machine's disk is tight)
        self.wavdir = os.path.join(self.out, "wav") if a.keep_wav else tempfile.mkdtemp(prefix="amplecheck-")
        self.settings_file = "C:\\Users\\user\\Documents\\" + self.p["settingsFile"].replace("/", "\\")

    def row(self, item, ok, numbers):
        if not isinstance(ok, str):
            ok = bool(ok)                 # (numpy's bools are no True / False)
        self.rows.append((item, "PASS" if ok is True else ("FAIL" if ok is False else str(ok)), numbers))
        print("%-28s %-6s %s" % (item, self.rows[-1][1], numbers))

    def ps(self, body):
        return amplevm.ps(WATCH + body)

    def run(self, argstr, log, secs=240, envs=""):
        out = self.ps("Run '%s' '%s' '%s' %d '%s'" % (self.exe, argstr.replace("'", "''"), log, secs, envs)).strip()
        st, t = out.splitlines()[-1].rsplit(" ", 1)
        return st, float(t.replace(",", "."))

    # ---- scores ----
    def scores(self):
        with tempfile.TemporaryDirectory() as tmp:
            check_scores.main(["check_scores", tmp])
            amplevm.mkdir(self.vm + "\\sc")
            for n in ("techniques", "soft", "capostart", "state"):
                amplevm.put(os.path.join(tmp, n + ".musicxml"), self.vm + "\\sc\\%s.musicxml" % n)
                st, _ = self.run("-o %s\\sc\\%s.mscz %s\\sc\\%s.musicxml" % (self.vm, n, self.vm, n), self.vm + "\\sc\\conv-" + n, 180)
                if st != "exit 0":
                    raise SystemExit("converting %s: %s" % (n, st))
                amplevm.get(self.vm + "\\sc\\%s.mscz" % n, os.path.join(self.out, n + ".mscz"))
            for name, score, settings, lanes, _ in self.cases():
                lp = os.path.join(self.out, "case-%s.mscz" % name)
                patch_mscz(os.path.join(self.out, score + ".mscz"), lp, settings, lanes)
                amplevm.put(lp, self.vm + "\\sc\\%s.mscz" % name)

    def cases(self):
        want = self.a.cases.split(",") if self.a.cases else None
        return [c for c in CASES if not want or c[0] in want]

    # ---- renders ----
    def verify(self, name, routes, tag=""):
        o = "%s\\out\\%s%s" % (self.vm, name, tag)
        envs = "MS_VERIFY_ROUTES=%d" % (1 if routes else 0)
        crashes = 0
        for attempt in range(self.a.retries + 1):
            self.ps("if (Test-Path '%s') { Remove-Item -Recurse -Force '%s' }; New-Item -ItemType Directory -Force '%s' | Out-Null"
                    % (o, o, o))
            st, secs = self.run('--verify-library "%s" --verify-playback %s\\sc\\%s.mscz --verify-out %s --verify-wav'
                                % (self.lib, self.vm, name, o), o + "\\run", 300, envs)
            listing = self.ps("Get-ChildItem '%s' -Recurse -Filter *.wav | ? { $_.DirectoryName -notmatch 'clips' } | sort Name | "
                              "%% { $_.Name + '|' + (Get-FileHash $_.FullName -Algorithm MD5).Hash + '|' + $_.FullName }" % o)
            wavs = [l.split("|") for l in listing.strip().splitlines() if "|" in l]
            if st == "exit 0" and wavs:
                break
            crashes += 1
            print("   %s%s: %s (attempt %d)" % (name, tag, st, attempt + 1), file=sys.stderr)
        self.results["crashes"] += crashes
        res = {"state": st, "seconds": secs, "crashes": crashes, "wavs": {}}
        for wname, h, full in wavs:
            if "built-in" in wname:
                continue
            lp = os.path.join(self.wavdir, "%s%s %s" % (name, tag, wname))
            os.makedirs(os.path.dirname(lp), exist_ok=True)
            amplevm.get(full, lp)
            res["wavs"][wname.replace(name + " ", "")] = {"md5": h, "file": lp}
        return res

    # ---- analyses ----
    @staticmethod
    def lib_wav(res):
        for k, v in res["wavs"].items():
            if k.startswith("library"):
                return audio.load(v["file"])
        k = sorted(res["wavs"])[0] if res["wavs"] else None
        return audio.load(res["wavs"][k]["file"]) if k else None

    def analyse(self, done):
        sets = self.p["sampleSets"] if isinstance(self.p["sampleSets"], list) else self.p["sampleSetsGuess"]
        if "tech-on" in done:
            runs = done["tech-on"]
            hashes = [tuple(sorted((k, v["md5"]) for k, v in r["wavs"].items())) for r in runs]
            self.row("tech-on deterministic", len(set(hashes)) == 1 and all(hashes),
                     "%d runs, %d distinct; routes %s" % (len(runs), len(set(hashes)), sorted(runs[0]["wavs"])))
            self.row("tech-on routes = sets", len(runs[0]["wavs"]) == len(sets), "%d routes, %d sets" % (len(runs[0]["wavs"]), len(sets)))
            # bar 21 capo 2 (40 s), bar 22 capo 0 (42 s): the Finger route (route 0-1)
            first = sorted(runs[0]["wavs"])[0]
            sr, x = audio.load(runs[0]["wavs"][first]["file"])
            x = audio.mono(x)
            f21 = audio.f0(x, sr, 40.1, 0.5, 150, 300)
            f22 = audio.f0(x, sr, 42.1, 0.5, 150, 300)
            self.row("capo 2 / capo 0 (bars 21-22)", abs(f21 - 220) < 6 and abs(f22 - 196) < 6, "%.1f / %.1f Hz" % (f21, f22))
            q16, c16 = audio.loud50(x, sr, 30.0, 0.5), audio.centroid(x, sr, 30.0, 0.3)
            q1, c1 = audio.loud50(x, sr, 0.0, 0.5), audio.centroid(x, sr, 0.0, 0.3)
            # a strum whose mode / Detect / chord keys share the strum key's block plays dark and partial (the chord's
            # bass strings, ~300-600 Hz), and on multiples of 7.5 s of render time quiet too; with them a block earlier
            # bright (~1200 Hz): measure_ample.py strumtiming / strumlead, 2026-10-21
            self.row("strum bright (bar 1, set-up lead)", c1 > 900, "centroid %.0f Hz, %.1f dB" % (c1, q1))
            self.row("bar 16 strum (30.0 s) not quiet", abs(q16 - q1) < 4, "%.1f dB %.0f Hz vs bar 1 %.1f dB" % (q16, c16, q1))
        if "tech-off" in done:
            r = done["tech-off"][0]
            self.row("tech-off renders", bool(r["wavs"]), r["state"])
        if "state0" in done:
            sr, x = self.lib_wav(done["state0"][0])
            x = audio.mono(x)
            ff0, ppp0 = audio.loud50(x, sr, 0, 0.5), audio.loud50(x, sr, 1.0, 0.5)
            chord0 = audio.loud50(x, sr, 4.0, 1.0)
            self.row("state0 ff / ppp", ff0 - ppp0 > 10, "%.1f / %.1f dB" % (ff0, ppp0))
            h0 = self.lib_md5(done["state0"][0])
            if "velsens0" in done:
                sr, y = self.lib_wav(done["velsens0"][0])
                y = audio.mono(y)
                ff, ppp = audio.loud50(y, sr, 0, 0.5), audio.loud50(y, sr, 1.0, 0.5)
                # (Vel Sensitivity 0 flattens the level, but the soft layers still play: check455 st1x -14.4 / -24.6)
                self.row("Vel Sensitivity 0 lane", ppp - ppp0 > 8, "ff %.1f / ppp %.1f dB (state0's ppp %.1f)" % (ff, ppp, ppp0))
            if "layers" in done:
                self.row("velocity layers lane", self.lib_md5(done["layers"][0]) != h0, "differs from state0")
            if "voices1" in done:
                sr, y = self.lib_wav(done["voices1"][0])
                c = audio.loud50(audio.mono(y), sr, 4.0, 1.0)
                self.row("Max Voices 1 lane", c < chord0 - 2, "chord %.1f vs %.1f dB" % (c, chord0))
        for n in ("soft0", "soft1", "soft2"):
            if n in done:
                sr, y = self.lib_wav(done[n][0])
                lv = audio.loud50(audio.mono(y), sr, 7.0, 0.5)
                self.row("%s ppp strum (7.0 s)" % n, lv > -45, "%.1f dB" % lv)
        if "vol" in done and "soft0" in done:
            sr, y = self.lib_wav(done["vol"][0])
            sr, z = self.lib_wav(done["soft0"][0])
            d = audio.rms(audio.mono(y), sr, 4.0, 2.0) - audio.rms(audio.mono(z), sr, 4.0, 2.0)
            exp = 20 * __import__("math").log10(80 / 64.0)
            self.row("volume lane (bar 3)", abs(d - exp) < 1.5, "%+.2f dB vs soft0 (lane 80 = %+.2f)" % (d, exp))
        if "capostart" in done:
            sr, y = self.lib_wav(done["capostart"][0])
            y = audio.mono(y)
            fs = [audio.f0(y, sr, 0.05 + i * 0.5 + 0.05, 0.35, 150, 300) for i in range(8)]
            ok = all(abs(f - 220) < 6 for f in fs[:4]) and all(abs(f - 196) < 6 for f in fs[4:])
            self.row("capo from the start", ok, " ".join("%.0f" % f for f in fs))

    @staticmethod
    def lib_md5(res):
        for k, v in res["wavs"].items():
            if k.startswith("library"):
                return v["md5"]
        return None

    def compare_ref(self, done):
        if not self.a.ref:
            return
        ref = json.load(open(self.a.ref))
        for name, runs in done.items():
            r0 = ref["cases"].get(name)
            if not r0:
                continue
            mine = {k: v["md5"] for k, v in runs[0]["wavs"].items()}
            theirs = {k: v["md5"] for k, v in r0[0]["wavs"].items()}
            same = mine == theirs
            self.row("%s = reference" % name, True if same else "DIFF", "bit-identical" if same else
                     "differs: %s" % sorted(k for k in mine if mine.get(k) != theirs.get(k)))

    def live(self):
        d = self.vm + "\\live"
        self.ps("New-Item -ItemType Directory -Force '%s' | Out-Null; Copy-Item '%s\\sc\\vol.mscz' '%s\\v.mscz' -Force" % (d, self.vm, d))
        st, _ = self.run('--create-live-set %s\\v.als %s\\v.mscz --verify-library "%s"' % (d, d, self.lib), d + "\\create", 240)
        self.row("Create Live Set", st == "exit 0", st)
        hs = []
        for k in (1, 2):
            st, _ = self.run('--update-live-set %s\\v.als %s\\v.mscz --verify-library "%s"' % (d, d, self.lib), d + "\\upd%d" % k, 240)
            h = self.ps("(Get-FileHash '%s\\v.als').Hash" % d).strip()
            msg = self.ps("Get-Content '%s\\upd%d.err' -ea 0 | Select-String 'Updated|othing|imported' | %% { $_.Line }" % (d, k)).strip()
            hs.append(h)
            self.row("Update Live Set %d" % k, st == "exit 0" if k == 1 else (st == "exit 0" and "othing" in msg),
                     "%s; %s" % (st, msg[:160].replace("\n", " | ")))
        amplevm.get(d + "\\v.als", os.path.join(self.out, "v.als"))

    def main(self):
        amplevm.mkdir(self.vm + "\\out")
        pv = ["C:\\Users\\user\\AppData\\Local\\MuseScore\\MuseScore3Evo\\soundlibraries\\%s\\parameter values.json" % self.lib,
              "C:\\Users\\user\\Documents\\MuseScore Sound Library Check\\background verify setups\\%s\\parameter values.json" % self.lib]
        with amplevm.Lock():
            h0 = self.ps("(Get-FileHash '%s' -ea 0).Hash" % self.settings_file).strip()
            self.ps("".join("if (Test-Path '%s') { Copy-Item '%s' '%s\\pv%d.json' -Force }; " % (p, p, self.vm, i) for i, p in enumerate(pv)))
            if self.a.map:
                amplevm.put(self.a.map, self.build + "\\share\\soundlibraries\\%s.xml" % self.lib)
            self.scores()
            if self.a.block_network:
                self.ps("New-NetFirewallRule -DisplayName 'ample-check block' -Direction Outbound -Program '%s' -Action Block | Out-Null" % self.exe)
            done = {}
            for name, _, _, _, routes in self.cases():
                n = self.a.repeat if name == "tech-on" else 1
                done[name] = [self.verify(name, routes, "" if k == 0 else "-%d" % k) for k in range(n)]
                self.results["cases"][name] = [{"state": r["state"], "seconds": r["seconds"], "crashes": r["crashes"],
                                                "wavs": {k: {"md5": v["md5"]} for k, v in r["wavs"].items()}} for r in done[name]]
            if not self.a.no_live and (not self.a.cases or "live" in self.a.cases.split(",")) and "vol" in [c[0] for c in self.cases()]:
                self.live()
            if self.a.block_network:
                self.ps("Remove-NetFirewallRule -DisplayName 'ample-check block' -ea 0")
            h1 = self.ps("(Get-FileHash '%s' -ea 0).Hash" % self.settings_file).strip()
            restored = self.ps("".join("if ((Test-Path '%s\\pv%d.json') -and ((Get-FileHash '%s').Hash -ne (Get-FileHash '%s\\pv%d.json').Hash)) "
                                       "{ Copy-Item '%s\\pv%d.json' '%s' -Force; 'restored %d' }; " % (self.vm, i, p, self.vm, i, self.vm, i, p, i)
                                       for i, p in enumerate(pv))).strip()
            self.ps("Remove-Item -Recurse -Force '%s\\out' -ea 0" % self.vm)
        self.row("plug-in settings file unchanged", h0 == h1, "%s… before and after" % h0[:12])
        if restored:
            self.row("parameter values.json", "RESTORED", restored)
        self.analyse(done)
        self.compare_ref(done)
        self.row("load crashes (retried)", "INFO", "%d" % self.results["crashes"])
        self.results["rows"] = self.rows
        with open(os.path.join(self.out, "results.json"), "w") as f:
            json.dump(self.results, f, indent=1)
        fails = [r for r in self.rows if r[1] == "FAIL"]
        with open(os.path.join(self.out, "report.md"), "w") as f:
            f.write("# Ample check: %s, build %s, %s\n\n" % (self.p["code"], self.build, time.strftime("%Y-%m-%d %H:%M")))
            f.write("**%s**: %d PASS, %d FAIL, %d other. Private: the renders are the vendor's audio.\n\n" % (
                "FAIL" if fails else "PASS", sum(r[1] == "PASS" for r in self.rows), len(fails),
                sum(r[1] not in ("PASS", "FAIL") for r in self.rows)))
            f.write("| Item | Result | Numbers |\n|---|---|---|\n")
            for r in self.rows:
                f.write("| %s | %s | %s |\n" % (r[0], r[1], str(r[2]).replace("|", "/")))
        print("report: %s" % os.path.join(self.out, "report.md"))
        if not self.a.keep_wav:
            shutil.rmtree(self.wavdir, ignore_errors=True)
        return 1 if fails else 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--product", required=True)
    ap.add_argument("--build", required=True, help="the installed build on the VM, e.g. C:\\claude\\bpt\\ms")
    ap.add_argument("--out", required=True)
    ap.add_argument("--ref")
    ap.add_argument("--map")
    ap.add_argument("--cases")
    ap.add_argument("--repeat", type=int, default=3)
    ap.add_argument("--retries", type=int, default=2)
    ap.add_argument("--no-live", action="store_true")
    ap.add_argument("--block-network", action="store_true", help="a firewall rule blocks MuseScore's outbound traffic "
                    "while the renders run (the load-crash question: is it the plug-in's licence check?)")
    ap.add_argument("--keep-wav", action="store_true")
    return Check(ap.parse_args(argv[1:])).main()


if __name__ == "__main__":
    sys.exit(main(sys.argv))
