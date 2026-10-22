#!/usr/bin/env python3
"""realcheck.py: the real-Live check. A score's plain set, made by a Windows build of MuseScore, opened, exported and
saved by Ableton Live 12.4.6 on the test VM, against MuseScore's own export of the same score; the set Live saved
checked for what MuseScore wrote, Update Live Set and the read-back run on it (docs/TOOLS.md › 5; LIVE.md).

  realcheck.py run <score> --name <id> --out <dir> [--preset recommended] [--settings "k=v;…"]
               [--checks live,floor,readback,update] [--variant auto|none] [--lanes <json>|mic-steps]
               [--build C:\\claude\\b467\\ms] [--library "Spitfire Symphony Orchestra"] [--resume] [--timeout s]
      one VM job (tools/vm/vmjob.py: the VM lock C:\\claude\\quiet.lock for the whole run, a watchdog, the run folder
      C:\\claude\\rc\\lrc-<id>, kept until the run passed its steps so that --resume goes on where it stopped; the WAVs
      are deleted on the VM in any case). <score>: .mscz / .mscx, or MusicXML (converted by the build first). The
      score's playbackSettings metaTag = the preset's values with --settings over them (heard_check.make_score).
      --variant auto: a copy with one note a whole tone higher, for Update Live Set on the set Live saved (the update
      then has something to write). --lanes: the score's automation lanes (the metaTag's JSON list of
      {"target", "points": [[tick, value, "step"|"linear"]]} for the first part), or mic-steps: Mic 2 (Tree) 1 / 0.5
      every bar (parameter-lane edges in Live's export).
  realcheck.py report <dir>        the PASS / FAIL report again from a run's results (report.md, report.json)
  realcheck.py lanescore <out.musicxml>   the parameter-lane test score: a bar's rest, one violin note held over 8 bars, 120 bpm
                                   (run it with --lanes mic-steps)

Checks (--checks; "all" = every one):
  live      Live opens the set (dialogs handled: the trial notice; an unknown one escaped and reported), exports every
            track (File > Export Audio/Video, All Individual Tracks, 32-bit WAV), saves the set (Save Live Set As),
            quits; compare.py: per Kontakt track, per part and Main: lag, correlation, null, gain, per-note level and
            onset; alsset.py diff: every value MuseScore wrote is in the set Live saved
  floor     MuseScore exports the score a second time: each route's null against the first, the floor Live's null is
            read against (two renders of the same events don't null perfectly: Kontakt's own modulation, round robins)
  readback  --live-set-readback of the written and of the saved set: Kontakt's panel values as Live saved its state
  update    --update-live-set on (a copy of) the set Live saved: with the unchanged score it must write nothing; with
            the variant it updates, Live opens the updated set, exports it (against MuseScore's export of the variant)
            and saves it again
The features the set holds (written clips with Track Delays and MuseScore Link's timing, the gain-stage Utility,
tuning slots, voice copies, plainShift, the start lead, parameter automation, Ample) are listed from the written set,
each with the verdict of the tracks that carry it: confirmed, differs (with the numbers) or not in this set.

Private: the run's results (sets with Kontakt / Ample states, screenshots of Live, reports with the score's tracks)
go to --out, which must be outside the repository.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "tools", "vm"))
sys.path.insert(0, os.path.join(ROOT, "tools", "playbackverify"))
import alsset  # noqa: E402

VM_FILES = ["vm_run.ps1", "live_drive.ps1", "desk_run.ps1", "gui.ps1", "tscon.cmd", "compare.py", "alsset.py"]
DEFAULT_BUILD = r"C:\claude\b467\ms"
LIBRARY = "Spitfire Symphony Orchestra"


# ------------------------------------------------------------------------------------------------------ the score

def mscx_of(path):
    with zipfile.ZipFile(path) as z:
        n = next(n for n in z.namelist() if n.endswith(".mscx"))
        return n, z.read(n).decode("utf-8")


rewrite_mscz = alsset.rewrite_mscz


def set_meta(text, name, value):
    esc = value.replace("&", "&amp;").replace("<", "&lt;").replace('"', "&quot;")
    text = re.sub(r'\s*<metaTag name="%s">[^<]*</metaTag>' % re.escape(name), "", text)
    m = re.search(r'([ \t]*)<metaTag ', text)
    if not m:
        raise SystemExit("no metaTag in the score")
    return text[:m.start()] + '%s<metaTag name="%s">%s</metaTag>\n' % (m.group(1), name, esc) + text[m.start():]


def first_part_name(text):
    p = re.search(r"<Part[ >].*?</Part>", text, re.S)
    return re.search(r"<trackName>([^<]*)</trackName>", p.group(0)).group(1)


def mic_steps_lanes(text, bars=8, division=None):
    """Mic 2 (Tree) 1.0 / 0.5 alternating every bar from bar 3 (steps: edges to time in the export; the lane score's
    note starts at bar 2: a note at the song's start Live doesn't play when it starts early, LIVE.md › Real Live)"""
    div = division or int(re.search(r"<Division>(\d+)</Division>", text).group(1))
    bar = 4 * div
    pts = [[0, 1.0, "step"]]
    for b in range(2, bars + 1):
        pts.append([b * bar, 0.5 if b % 2 else 1.0, "step"])
    return [{"target": "mic2", "points": pts}]


def with_lanes(text, lanes):
    if lanes == "mic-steps":
        lanes = mic_steps_lanes(text)
    return set_meta(text, "automation", json.dumps([{"part": 0, "name": first_part_name(text), "lanes": lanes}],
                                                   separators=(",", ":")))


def lane_edge_seconds(text, lanes):
    """the lanes' steps (not the first point) in seconds, at the score's first tempo (a lane test score has one)"""
    if lanes == "mic-steps":
        lanes = mic_steps_lanes(text)
    div = int(re.search(r"<Division>(\d+)</Division>", text).group(1))
    m = re.search(r"<Tempo>.*?<tempo>([0-9.]+)</tempo>", text, re.S)
    bps = float(m.group(1)) if m else 2.0
    ticks = sorted({p[0] for l in lanes for p in l["points"][1:]})
    return [t / div / bps for t in ticks]


variant_text = alsset.variant_text


def settings_value(preset, extra):
    import heard_check as H
    items = dict(i.split("=", 1) for i in H.preset_values(preset, ROOT)) if preset and preset != "none" else {}
    for i in (extra or "").split(";"):
        if "=" in i:
            k, v = i.split("=", 1)
            items[k.strip()] = v.strip()
    return ";".join("%s=%s" % kv for kv in items.items())


def lane_score(path, bars=9, instrument="Violin", pitch=("A", 4)):
    """a MusicXML test score for parameter-lane edges: a bar's rest, then one note held over every other bar (tied), mf,
    120 bpm, 4/4 (not at the song's start: Live doesn't play a note there that starts early)"""
    notes = []
    for b in range(bars):
        ties = []
        if b > 1:
            ties.append("stop")
        if 0 < b < bars - 1:
            ties.append("start")
        tie = "".join('<tie type="%s"/>' % t for t in ties)
        tied = "".join('<tied type="%s"/>' % t for t in ties)
        attr = ('<attributes><divisions>1</divisions><key><fifths>0</fifths></key><time><beats>4</beats><beat-type>4'
                '</beat-type></time><clef><sign>G</sign><line>2</line></clef></attributes><direction placement="above">'
                '<direction-type><metronome><beat-unit>quarter</beat-unit><per-minute>120</per-minute></metronome>'
                '</direction-type><sound tempo="120"/></direction><direction><direction-type><dynamics><mf/></dynamics>'
                '</direction-type><sound dynamics="88"/></direction>') if b == 0 else ""
        if b == 0:
            notes.append('<measure number="1">%s<note><rest measure="yes"/><duration>4</duration></note></measure>' % attr)
            continue
        notes.append('<measure number="%d">%s<note><pitch><step>%s</step><octave>%d</octave></pitch><duration>4'
                     '</duration>%s<type>whole</type><notations>%s</notations></note></measure>'
                     % (b + 1, attr, pitch[0], pitch[1], tie, tied))
    xml = ('<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 '
           'Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">\n<score-partwise version="3.1"><work><work-title>'
           'Lane edges</work-title></work><part-list><score-part id="P1"><part-name>%s</part-name></score-part>'
           '</part-list><part id="P1">%s</part></score-partwise>\n' % (instrument, "".join(notes)))
    open(path, "w", encoding="utf-8").write(xml)


# --------------------------------------------------------------------------------------------------------- the VM

def vmjob(args, extra_env=None):
    cmd = [sys.executable, os.path.join(ROOT, "tools", "vm", "vmjob.py")] + args
    env = dict(os.environ, **(extra_env or {}))
    return subprocess.call(cmd, env=env)


def convert(score, work, name):
    """MusicXML -> .mscz by the build (the fork's import), one small VM job"""
    job = os.path.join(work, "convert-job")
    shutil.rmtree(job, ignore_errors=True)
    os.makedirs(job)
    shutil.copy(score, os.path.join(job, "in.musicxml"))
    out = os.path.join(work, "convert-out")
    shutil.rmtree(out, ignore_errors=True)
    rc = vmjob(["--name", "lrc-%s-conv" % name, "--vmdir", "rc", "--out", out, "--fetch", "conv.mscz", "--expect",
                "conv.mscz", "--kill", "MuseScore3Evo.exe", "--timeout", "600", "--retries", "1", "--poll", "5", job, "--",
                r'%s\bin\MuseScore3Evo.exe -o {run}\conv.mscz {run}\in.musicxml' % BUILD[0]])
    path = os.path.join(out, "conv.mscz")
    if rc != 0 or not os.path.exists(path):
        raise SystemExit("convert: failed (vmjob exit %s, %s)" % (rc, out))
    return path


BUILD = [DEFAULT_BUILD]


def cmd_run(a):
    BUILD[0] = a.build
    if os.path.abspath(a.out).startswith(ROOT + os.sep):
        raise SystemExit("--out must be outside the repository (the results hold the library's states and the score)")
    name = a.name
    if not re.match(r"^[A-Za-z0-9_-]{1,40}$", name):
        raise SystemExit("--name: letters, digits, _ and - only")
    os.makedirs(a.out, exist_ok=True)
    work = os.path.join(a.out, "work")
    job = os.path.join(work, "job")
    if not a.resume or not os.path.isdir(job):
        shutil.rmtree(job, ignore_errors=True)
        os.makedirs(job)
        src = a.score
        if src.lower().endswith((".musicxml", ".xml", ".mxl")):
            src = convert(src, work, name)
        if a.ample:
            # the Ample check's score edits (tools/ample/check.py patch_mscz: straight strum arpeggios, capo fret ids,
            # every part on the library)
            sys.path.insert(0, os.path.join(ROOT, "tools", "ample"))
            import check as ample_check
            patched = os.path.join(work, "ample-patched.mscz")
            ample_check.patch_mscz(src, patched, "", None)
            src = patched
        value = settings_value(a.preset, a.settings)
        lanes = None
        if a.lanes:
            lanes = a.lanes if a.lanes == "mic-steps" else json.load(open(a.lanes))

        def fix(text):
            text = set_meta(text, "playbackSettings", value) if value else text
            return with_lanes(text, lanes) if lanes else text
        if src.endswith(".mscx"):
            # an .mscx becomes a one-file .mscz (MuseScore reads either; the export copy is one file to move)
            with zipfile.ZipFile(os.path.join(job, "score.mscz"), "w", zipfile.ZIP_DEFLATED) as z:
                z.writestr("score.mscx", fix(open(src, encoding="utf-8").read()))
                z.writestr("META-INF/container.xml", '<?xml version="1.0" encoding="UTF-8"?>\n<container><rootfiles>'
                           '<rootfile full-path="score.mscx"/></rootfiles></container>\n')
        else:
            rewrite_mscz(src, os.path.join(job, "score.mscz"), fix)
        edges = ""
        if lanes:
            _, text = mscx_of(os.path.join(job, "score.mscz"))
            edges = ",".join("%.4f" % t for t in lane_edge_seconds(text, lanes))
        variant = ""
        if a.variant == "auto" and "update" in a.checks.replace("all", "update"):
            rewrite_mscz(os.path.join(job, "score.mscz"), os.path.join(job, "variant.mscz"), variant_text)
            variant = "variant.mscz"
            shutil.copy(os.path.join(job, "variant.mscz"), os.path.join(job, "variant-ms.mscz"))
        # MuseScore's export reads a copy: Create / Update Live Set save the score with the set's record (vm_run.ps1)
        shutil.copy(os.path.join(job, "score.mscz"), os.path.join(job, "score-ms.mscz"))
        for f in VM_FILES:
            shutil.copy(os.path.join(HERE, f), job)
        exe = a.build.rstrip("\\") + r"\bin\MuseScore3Evo.exe"
        json.dump({"exe": exe, "library": a.library, "checks": a.checks.split(","), "variant": variant,
                   "tailS": a.tail, "loadTimeoutS": a.load_timeout, "exportTimeoutS": a.export_timeout,
                   "settleS": a.settle, "edges": edges, "keepWavs": a.keep_wavs}, open(os.path.join(job, "job.json"), "w"), indent=1)
        json.dump({"score": os.path.basename(a.score), "settings": value, "lanes": lanes, "checks": a.checks,
                   "build": a.build, "started": time.strftime("%Y-%m-%d %H:%M:%S")},
                  open(os.path.join(a.out, "run.json"), "w"), indent=1)
    if a.resume:
        # the run folder keeps what the run made (the score Create Live Set saved, the sets); the scripts are this
        # checkout's: put them over the old ones (vmjob --resume copies only missing items)
        import vmlib
        vm = vmlib.VM()
        run = r"%s\rc\lrc-%s" % (vm.conf["root"].rstrip("\\"), name)
        for f in VM_FILES:
            shutil.copy(os.path.join(HERE, f), job)
        if vm.exists(run + "\\.vmjob-run"):
            vm.scp_to([os.path.join(HERE, f) for f in VM_FILES], run + "\\")
    out = os.path.join(a.out, "fetched")
    args = ["--name", "lrc-%s" % name, "--vmdir", "rc", "--out", out, "--fetch", "results", "--keep",
            "--kill", "Ableton Live 12 Trial.exe", "--kill", "MuseScore3Evo.exe", "--timeout", str(a.timeout),
            "--retries", "0", "--poll", "5"]
    if a.resume:
        args.append("--resume")
    if a.max_wait:
        args += ["--max-wait", str(a.max_wait)]
    shutil.rmtree(os.path.join(out, "results"), ignore_errors=True)
    rc = vmjob(args + [job, "--", r"powershell -NoProfile -ExecutionPolicy Bypass -File {run}\vm_run.ps1 -run {run}"])
    rep = report(a.out)
    if rc == 0 and not a.keep_wavs:
        import vmlib
        vm = vmlib.VM()
        run = r"%s\rc\lrc-%s" % (vm.conf["root"].rstrip("\\"), name)
        gone = vm.rmdir_guarded(run, ".vmjob-run")
        print("run folder %s: %s" % (run, "deleted" if gone else "NOT deleted"))
    else:
        print("vm_run exit %s: the run folder is kept; go on with --resume" % rc)
    return 0 if rc == 0 and rep["verdict"] == "PASS" else 1


# ------------------------------------------------------------------------------------------------------ the report

def load_json(path):
    try:
        return json.load(open(path, encoding="utf-8-sig"))
    except (OSError, ValueError):
        return None


def features(set_path, settings):
    """what the written set exercises, with the tracks that carry it"""
    root = alsset.load(set_path)
    tr = alsset.tracks(root)
    by_id = {t["id"]: t for t in tr}
    kont = [t for t in tr if any(d["type"] == "PluginDevice" for d in t["devices"])]

    def kontakt_of(t):
        m = re.search(r"Track\.(\d+)", t["midiTo"] or "")
        return by_id.get(m.group(1)) if m else None
    f = []

    def add(name, carriers, evidence):
        f.append({"feature": name, "present": bool(carriers), "tracks": sorted({c["name"] for c in carriers}),
                  "kontakts": sorted({c["key"] for c in carriers}), "evidence": evidence})
    tech = [t for t in tr if t["kind"] == "MidiTrack" and any(d["type"].startswith("MxDevice") for d in t["devices"])]
    delayed = [t for t in tech if abs(t["trackDelayMs"]) > 0]
    add("written clips: technique Track Delay + MuseScore Link per-note delays",
        [k for k in (kontakt_of(t) for t in delayed) if k],
        "%d technique tracks with MuseScore Link, %d with a Track Delay (%s ms)" % (
            len(tech), len(delayed), ", ".join("%+.1f" % t["trackDelayMs"] for t in delayed[:8])))
    util = [t for t in kont if any(d["type"] == "StereoGain" and d.get("gain") not in (None, 1.0) for d in t["devices"])]
    add("gain-stage Utility (MuseScore gain)", util, "; ".join(
        "%s %+.2f dB" % (t["name"], 20 * __import__("math").log10(max(d["gain"], 1e-9)))
        for t in util for d in t["devices"] if d["type"] == "StereoGain"))
    slots = [t for t in tr if re.search(r"DeviceIn\.0\.([1-9]\d*)", t["midiTo"] or "")]
    add("tuning / voice slots in a Kontakt multi (MIDI To a channel > 1)", [k for k in (kontakt_of(t) for t in slots) if k],
        "%d tracks to a slot > 1: %s" % (len(slots), ", ".join(t["name"] for t in slots[:6])))
    voices = [t for t in tr if "(voice" in t["name"]]
    add("voice copies (chords on mono patches)", [k for k in (kontakt_of(t) for t in voices) if k] or voices,
        ", ".join(t["name"] for t in voices[:6]))
    shift = [t for t in kont if abs(t["trackDelayMs"]) > 0]
    add("plainShift: a Kontakt track's own Track Delay", shift, ", ".join("%s %+.1f ms" % (t["name"], t["trackDelayMs"]) for t in shift))
    autos = []
    for t in root.find("LiveSet").find("Tracks"):
        env = t.find("AutomationEnvelopes/Envelopes")
        if env is not None and len(list(env)):
            autos.append(by_id[t.get("Id")])
    add("parameter automation on a Kontakt / plug-in track", [t for t in autos if t in kont],
        ", ".join("%s (%d envelopes)" % (t["name"], len(list(root.find("LiveSet").find("Tracks").find(
            "*[@Id='%s']/AutomationEnvelopes/Envelopes" % t["id"])))) for t in autos if t in kont))
    # the start lead: a technique track with a negative Track Delay and a note at the song's start (MuseScore plays it
    # early, before 0; the plain set by the Track Delay)
    first = []
    for t in root.find("LiveSet").find("Tracks"):
        if t.tag != "MidiTrack" or t.get("Id") not in by_id:
            continue
        me = by_id[t.get("Id")]
        if me["trackDelayMs"] < 0 and any(float(n.get("Time")) == 0.0 for n in t.iter("MidiNoteEvent")):
            k = kontakt_of(me)
            if k:
                first.append((me, k))
    f.append({"feature": "start lead: a note at the song's start played early (negative Track Delay)", "present": bool(first),
              "tracks": sorted({m["name"] for m, _ in first}), "kontakts": sorted({k["key"] for _, k in first}),
              "evidence": ", ".join("%s %+.1f ms" % (m["name"], m["trackDelayMs"]) for m, _ in first[:6]), "startLead": True})
    ample = [t for t in kont if any("AGM" in (d.get("plugin") or "") or "Ample" in (d.get("plugin") or "") for d in t["devices"])]
    add("Ample plug-in tracks", ample, ", ".join(t["name"] for t in ample))
    s = dict(i.split("=", 1) for i in (settings or "").split(";") if "=" in i)
    for k, label in (("slurs/fixedTransitions", "fixedTransitions' state (random modulators at 0) in the set"),
                     ("slurs/speedFromWritten", "speedFromWritten (CC119 carrier key 113)"),
                     ("slurs/plainPerformance", "plain Performance")):
        if s.get(k, "0") != "0":
            perf = [t for t in kont if "Performance" in t["name"] or "Performance" in t["key"]]
            add(label + " [setting %s=%s]" % (k, s[k]), perf or kont, "")
    return f


def verdict_of(compare, keys):
    rows = [t for t in (compare or {}).get("tracks", []) if t.get("key") in keys]
    if not rows:
        return "not compared", rows
    if all(r["verdict"].startswith("PASS") for r in rows):
        return "confirmed", rows
    if all(r["verdict"].startswith("NOT COMPARED") for r in rows):
        return "not compared", rows
    return "differs", rows


def row_numbers(r):
    if "lagMs" not in r:
        return r["verdict"]
    n = r.get("notes") or {}
    return ("%s: %s, lag %+.2f ms, corr %.5f, null %s dB, gain %+.2f dB, notes %d: level median %+.2f / max %.2f dB, "
            "onset median %+.2f / max %.2f ms%s" % (r["track"], r["verdict"], r["lagMs"], r["corr"], r["nullDb"],
                                                   r["gainDb"] or 0, n.get("n", 0), n.get("levelMedianDb", 0),
                                                   n.get("levelMaxAbsDb", 0), n.get("onsetMedianMs", 0),
                                                   n.get("onsetMaxAbsMs", 0),
                                                   ("; MuseScore against itself: null %s dB" % r["floorNullDb"] if "floorNullDb" in r else "")
                                                   + ("; differs at %s (null without them %s dB)" % (", ".join(
                                                       "%.2f-%.2f s Live %+.1f dB" % (x["start"], x["end"], x["levelDb"]) for x in r["spans"][:6]),
                                                       r.get("nullOutsideSpansDb")) if r.get("spans") else "")))


def report(outdir):
    res = os.path.join(outdir, "fetched", "results")
    run = load_json(os.path.join(outdir, "run.json")) or {}
    R = {"score": run.get("score"), "build": run.get("build"), "checks": [], "features": []}
    lv = os.path.join(res, "live-version.txt")
    R["live"] = open(lv, encoding="utf-8-sig").read().strip() if os.path.exists(lv) else ""

    def check(name, ok, detail):
        R["checks"].append({"check": name, "verdict": "PASS" if ok is True else ("FAIL" if ok is False else ok), "detail": detail})
    for n in ("live1", "live2"):
        d = load_json(os.path.join(res, n + ".json"))
        if d:
            check("Live %s: open, export, save, quit" % n[-1], d.get("verdict", "").startswith("PASS"),
                  "%s; load %s s, %s MB, export %s s, dialogs: %s%s" % (
                      d.get("verdict"), d.get("loadSeconds"), d.get("liveMemoryMB"), d.get("exportSeconds"),
                      ", ".join("%s (%s)" % (x.get("known") or "unknown %dx%d" % (x["w"], x["h"]), x.get("action")) for x in d.get("dialogs", [])) or "none",
                      "; " + d["failure"] if d.get("failure") else ""))
    for n in ("compare1", "compare2", "compare2u"):
        c = load_json(os.path.join(res, n + ".json"))
        if c:
            check({"compare1": "audio 1: Live's export of the written set against MuseScore's of the score",
                   "compare2": "audio 2: Live's export of the updated set against MuseScore's of the edited score",
                   "compare2u": "audio 2u: the same against MuseScore's of the score as Update Live Set left it"}[n],
                  c["verdict"] == "PASS", "; ".join(row_numbers(r) for r in c["tracks"]))
        x = load_json(os.path.join(res, n + "-xml.json"))
        if x:
            check("set %s survives Live's save" % ("written" if n == "compare1" else "updated"), x["verdict"] == "PASS",
                  "lost %d, changed %d (Live added %d): %s" % (x["lost"], x["changed"], x["added"], "; ".join(
                      "%s x%d (%s -> %s)" % (s["shape"], s["count"], s["written"], s["saved"]) for s in x["lostShapes"][:5] + x["changedShapes"][:5])))
    u0 = load_json(os.path.join(res, "update0.json"))
    if u0:
        check("Update Live Set, unchanged score, on the set Live saved: writes nothing", u0.get("exit") == 0 and u0.get("unchanged"),
              "exit %s, set unchanged %s, backups %s" % (u0.get("exit"), u0.get("unchanged"), u0.get("backups")))
    u1 = load_json(os.path.join(res, "update1.json"))
    if u1:
        txt = ""
        p = os.path.join(res, "update1.update.txt")
        if os.path.exists(p):
            txt = " ".join(open(p, encoding="utf-8", errors="replace").read().split()[:60])
        check("Update Live Set, edited score, on the set Live saved", u1.get("exit") == 0,
              "exit %s, backups %s; %s" % (u1.get("exit"), u1.get("backups"), txt))
    rw, rs = os.path.join(res, "readback-written.out.txt"), os.path.join(res, "readback-saved.out.txt")
    if os.path.exists(rw) and os.path.exists(rs):
        a_, b_ = open(rw, encoding="utf-8", errors="replace").read(), open(rs, encoding="utf-8", errors="replace").read()
        strip = lambda s: [l for l in s.splitlines() if not re.search(r"[A-Za-z]:\\|\.als", l)]
        same = strip(a_) == strip(b_)
        check("read-back: Kontakt's panel values in the saved set as in the written one", same,
              "identical" if same else "differs: %d / %d lines" % (len(strip(a_)), len(strip(b_))))
    c1 = load_json(os.path.join(res, "compare1.json"))
    written = os.path.join(res, "written.als")
    if os.path.exists(written):
        for f in features(written, run.get("settings")):
            if not f["present"]:
                f["result"] = "not in this set"
            elif f.get("startLead"):
                rows = [t for t in (c1 or {}).get("tracks", []) if t.get("key") in set(f["kontakts"]) and "spans" in t]
                lost = [(r, x) for r in rows for x in r["spans"] if x["start"] < 0.5 and x.get("firstDb", x["levelDb"]) < -10]
                f["result"] = ("differs: the first note not played" if lost else "confirmed") if rows else "not compared"
                f["numbers"] = ["%s: Live %+.1f dB at %.2f s" % (r["track"], x.get("firstDb", x["levelDb"]), x["start"]) for r, x in lost] or \
                    [row_numbers(r) for r in rows]
            else:
                v, rows = verdict_of(c1, set(f["kontakts"]))
                f["result"] = v
                f["numbers"] = [row_numbers(r) for r in rows]
            R["features"].append(f)
    if c1 and c1.get("tracks"):
        lags = [t["lagMs"] for t in c1["tracks"] if "lagMs" in t]
        R["startLead"] = {"lagsMs": lags, "note": "Live's export starts at song time 0; MuseScore's file is later by its start lead, so every track's lag is about minus the lead, and a note the lead moved before 0 is cut in Live"}
    bad = [c for c in R["checks"] if c["verdict"] == "FAIL"]
    R["verdict"] = "FAIL" if bad or not R["checks"] else "PASS"
    L = ["# Real-Live check: %s" % R["verdict"], "", "Score: %s; build: %s; Live %s; settings: %s" % (R["score"], R["build"], R["live"] or "?", run.get("settings")), "",
         "| Check | Verdict | Detail |", "|---|---|---|"]
    for c in R["checks"]:
        L.append("| %s | %s | %s |" % (c["check"], c["verdict"], c["detail"].replace("|", "/")))
    L += ["", "| Feature | Result | Where / numbers |", "|---|---|---|"]
    for f in R["features"]:
        L.append("| %s | %s | %s %s |" % (f["feature"], f["result"], f["evidence"].replace("|", "/"), " / ".join(f.get("numbers", [])).replace("|", "/")))
    open(os.path.join(outdir, "report.md"), "w", encoding="utf-8").write("\n".join(L) + "\n")
    json.dump(R, open(os.path.join(outdir, "report.json"), "w", encoding="utf-8"), indent=1)
    print("\n".join(L))
    return R


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd")
    r = sub.add_parser("run")
    r.add_argument("score")
    r.add_argument("--name", required=True)
    r.add_argument("--out", required=True)
    r.add_argument("--preset", default="recommended")
    r.add_argument("--settings", default="")
    r.add_argument("--checks", default="live,floor,readback,update")
    r.add_argument("--variant", default="auto", choices=["auto", "none"])
    r.add_argument("--lanes")
    r.add_argument("--build", default=DEFAULT_BUILD)
    r.add_argument("--ample", action="store_true", help="an Ample check score (tools/ample/check_scores.py): its edits")
    r.add_argument("--library", default=LIBRARY)
    r.add_argument("--resume", action="store_true")
    r.add_argument("--keep-wavs", action="store_true", help="leave the WAVs and the run folder on the VM (to debug "
                   "the comparison there; delete the folder by hand)")
    r.add_argument("--timeout", type=float, default=4 * 3600)
    r.add_argument("--max-wait", type=float)
    r.add_argument("--tail", type=float, default=6.0, help="seconds rendered after the last clip (default 6)")
    r.add_argument("--load-timeout", type=int, default=1500)
    r.add_argument("--export-timeout", type=int, default=3600)
    r.add_argument("--settle", type=int, default=20)
    ls = sub.add_parser("lanescore", help="write the parameter-lane test score (MusicXML)")
    ls.add_argument("out")
    p = sub.add_parser("report")
    p.add_argument("dir")
    a = ap.parse_args(argv)
    if a.cmd == "run":
        return cmd_run(a)
    if a.cmd == "lanescore":
        lane_score(a.out)
        return 0
    if a.cmd == "report":
        return 0 if report(a.dir)["verdict"] == "PASS" else 1
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main())
