#!/usr/bin/env python3
"""The heard check: each library note's heard time in the REAL sound (Kontakt + the library on the Windows VM), against
its written time and against what the playback audit predicts (VERIFY.md › Heard check).

Why (the owner, 2026-10-16, after finding a slur rhythm problem by ear): the playback audit (libmscore/playbackaudit.*)
checks the rendered events against the renderer's own model (the map's onsets, the modelled transition arrivals), so a
wrong model passes it: it reported ARRIVAL 0 fail while Whence's Violas slurs (an eighth + two sixteenths, 273 / 136 /
136 ms) sounded about 200 / 210 / 105 ms, a triplet. This measures the sound.

  heard_check.py events <dump> <out> [--parts a,b]   kthost event files, one per route (<dump>: tst_playbackaudit
                                                     auditScore's MS_AUDIT_DUMP folder; times + OFFSET_S), list.txt
                                                     "route|patch|probe pitch"
  heard_check.py analyse <dump> <wavs> [--report f] [--json f] [--parts a,b] [--title t] [--export]
                                                     the renders (<wavs>/<code>.wav, code = route "0-11") measured;
                                                     the report printed (and written); exit 1 on any FAIL. --export:
                                                     <wavs> is MuseScore's --verify-playback folder (MS_VERIFY_ROUTES=1:
                                                     "<score> route <code>.wav", its events.tsv)
  heard_check.py score <in.mscz|.mscx> <out> <preset or metaTag> [--part name]
                                                     the export renderer's copy of the score: its playbackSettings
                                                     metaTag set as the dump plays it (settings_tag); --part: only that
                                                     part on the library (partPlayback metaTag)
  heard_check.py parts <score> <dump>                the library parts, one per line (HC_SPLIT=parts)

Inputs (PlaybackAudit::writeHeardInputs): per route "<route> museScore.txt" (seconds = score time + the start lead),
routemap.txt, predicted.tsv (every library note-on: written time, route, patch, articulation, sent ms, the audit's
predicted heard time `arrivalMs`, its line / slur), lead.txt (ms), bars.txt.

Each route is rendered alone through its patch (kthost: one Kontakt instance with MuseScore's setup of that patch;
the events from OFFSET_S on so an early note-on fits). Times below are ms against the written time.

Measures (b):
  attack       (not a legato transition: a staccato, a held note, a slur's first note): the perceived envelope of its
               route (loudness.py, as ArticulationCheck's) over the note's own span (its note-on to the next note-on on
               the route; a slur's first note followed by its transitions: to the slur's written end; at most 1.5 s):
               heard = the first value after the last one under (its peak - 15 dB) before the
               peak, less 30 ms of analysis latency (the map's onsets' own rule: docs/HISTORY.md, "the mf -15 dB time on
               the perceived envelope less 30 ms of analysis latency"; Vos & Rasch 1981's -15 dB perceptual onset).
               (a hard step onset reads ~24 ms early by this rule; the map's onsets were measured by it, so the
               renderer's early starts aim at this reading). Where the note before on the route keeps the level within
               15 dB ("masked"), its tail (extrapolated linearly in dB from 75-25 ms before the note-on) taken off
               in loudness and the -15 dB rule applied to what is left (marked "masked" in the report); no rise of
               1 dB: not measured
  transition   (a legato transition, LibTrace::from): pYIN f0 of its whole route (librosa.pyin, hop 128, the route's
               pitches +-3 semitones): f0 at the note before's pitch (within 30 cents) after that note's heard time,
               then leaving it (more than 30 cents, or unvoiced; not before 50 ms ahead of its note-on), then the first
               of 3 (before the next note-on on the route)
               frames in a row within 30 cents of the new pitch (its playback tuning's cents: quarter tones included).
               A chord on the route (two notes at once): not measured (pYIN is monophonic)
Report (c): per note heard - written; per slur and per run of notes (a line's chords joined without a rest) the heard
  inter-onset intervals against the written ones.
Flags (d):
  ARRIVAL   FAIL: heard - written differs from the line's note before (joined, as the audit's ARRIVAL) by more than
            50 ms; WARN 30-50 ms (Rasch 1979, "Synchronization in performed ensemble music", Acustica 43: 30-50 ms
            asynchrony is normal ensemble playing)
  RHYTHM    FAIL (approved by the owner 2026-10-16, "sure, go with your rec"): in a window of 3 IOIs (4 onsets; a slur of 3 notes: its 2) the heard
            proportions p_i = IOI_i / sum differ from the written ones by more than RHYTHM_FAIL = 0.10 in any interval.
            Derived (not a published figure): Desain & Honing 2003 ("The formation of rhythmic categories and metric
            priming", Perception 32, 341-365; 3-interval rhythms in a 1 s bar): listeners' category means for 2:1 scatter
            with SD ~47 ms (0.047 of the bar; as tabled by Sadakata, Desain & Honing 2006, Music Perception 23(3)),
            2 SD ~0.094; performers' largest SD there ~37 ms (Repp's data, same table), 2 SD 0.074, so ordinary playing
            stays under 0.10. Above RHYTHM_WARN = 0.05 a WARN (audible, not categorical: Friberg & Sundberg's JND,
            ~10 ms below ~240 ms IOI, ~5 % above, as their 1993 ASA abstract states). The Violas' 200 / 210 / 105 for
            273 / 136 / 136: max |dp| 0.158
  LEVEL     WARN: under one slur at an unchanged written dynamic (the audit's LEVEL_STEP pairs), the change of the
            notes' loudest perceived level (envelope max over each note's heard span) above the audit's proposed
            0.69 dB (Jesteadt, Wier & Green 1977 at 60 dB SL; owner to approve)
  UNHEARD   WARN: a legato transition whose pitch is never within 30 cents before the next note-on on its route (a
            skipped note, a microtone not reached, or pYIN lost it: listen)
Model drift (e): measured - predicted heard time (the audit's arrivalMs: sent + onset, or sent + glideArriveMs) per
  patch x situation (first after silence, attack joined to the note before, staccato, transition after a fresh attack,
  transition after a transition): median and spread (10th-90th percentile); |median| > 20 ms flagged DRIFT.
  A transition's model is the bendGlide's arrival (9/10 of the way, legato_glide/glide.py) where this takes 30 cents:
  for a semitone 30 cents is 7/10 of the way, so a small negative drift there is the definitions' difference.
"""
import argparse
import json
import glob
import math
import os
import re
import sys
import zipfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

OFFSET_S = 1.0              # kthost: the events from 1.0 s (a note-on before the score's 0 fits)
EXPORT_OFFSET_S = 0.0       # the export (--verify-playback --verify-wav): sample 0 is score time 0 (the start lead is
                            # in the events); measured against kthost on bit-exact routes (VERIFY.md › Heard check)
LATENCY_MS = 30.0           # the perceived envelope's analysis latency (docs/HISTORY.md, the map's onsets)
ONSET_DB = 15.0             # the -15 dB perceptual onset (Vos & Rasch 1981; the map's onset rule)
ALT_DB = 6.0                # a second reading of each attack, -6 dB: the Violas' slurs as the owner heard them (2026-10-16:
                            # ~200 / 210 / 105 ms) match it on the smooth first notes, not -15 (vla912 investigation's
                            # heard6 / heard15); reported beside, never judged
SPAN_MAX_S = 1.5            # a note's own span at most (the onset measurement's 1.5 s)
CENTS_TOL = 30.0            # a transition arrives within 30 cents of its pitch
ARRIVE_FRAMES = 3           # ... for 3 pYIN frames in a row
PYIN_HOP = 128
TRANSITION_SEARCH_S = 0.7   # its arrival looked for until 0.7 s after its note-on
ARRIVAL_WARN_MS, ARRIVAL_FAIL_MS = 30.0, 50.0       # Rasch 1979
RHYTHM_WARN, RHYTHM_FAIL = 0.05, 0.10               # approved by the owner 2026-10-16, see above
LEVEL_DL_DB = 0.69                                  # the audit's proposed (Jesteadt, Wier & Green 1977)
DRIFT_MS = 20.0
SHORT_BASES = {"spiccato", "staccatissimo", "staccato", "short", "tenuto", "pizzicato", "bartok", "collegno"}
SITUATIONS = ("first after silence", "attack joined", "staccato", "transition after attack", "transition after transition")


# ---------------------------------------------------------------------------------------------------------------- input

def _read(path):
    with open(path, encoding="utf-8") as f:
        return f.read()


def _write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def read_rows(dump):
    lines = _read(os.path.join(dump, "predicted.tsv")).splitlines()
    head = lines[0].split("\t")
    rows = []
    ints = {"track", "bar", "pitch", "velocity", "performance", "transition", "fromPitch", "hasArrival", "sub", "grace",
            "slur", "joinedBefore", "slurredBefore", "dynamicBefore"}
    for i, ln in enumerate(lines[1:]):
        x = dict(zip(head, ln.split("\t")))
        for k in list(x):
            if k in ints:
                x[k] = int(x[k])
            elif k not in ("part", "route", "patch", "articulation", "techniques"):
                x[k] = float(x[k])
        x["id"] = i
        rows.append(x)
    return rows


def read_lead_ms(dump):
    return float(_read(os.path.join(dump, "lead.txt")).split()[0])


def read_routemap(dump):
    out = {}
    for ln in _read(os.path.join(dump, "routemap.txt")).splitlines():
        if ln.strip():
            r, part, patch, lane, cents, bend = ln.split("|")
            out[r] = dict(part=part, patch=patch, lane=int(lane), cents=float(cents), bend=float(bend))
    return out


def read_bars(dump):
    return [float(x) for x in _read(os.path.join(dump, "bars.txt")).split()]


def make_events(dump, out, parts=None):
    """kthost event files: each route's events + OFFSET_S; list.txt 'route|patch|probe pitch'"""
    os.makedirs(out, exist_ok=True)
    lst = []
    for route, info in sorted(read_routemap(dump).items()):
        if parts and info["part"] not in parts:
            continue
        f = os.path.join(dump, route + " museScore.txt")
        if not os.path.exists(f):
            continue
        ev = [e for e in _read(f).splitlines() if e and not e.startswith("#")]
        if not any(e.split()[1] == "on" for e in ev):
            continue
        lines = ["%.6f %s" % (float(e.split()[0]) + OFFSET_S, " ".join(e.split()[1:])) for e in ev]
        _write(os.path.join(out, route + ".txt"), "\n".join(lines) + "\n")
        # (kthost's probe note, which it waits for until the samples sound: the route's first pitch, in the patch's range)
        probe = next(int(e.split()[2]) for e in ev if e.split()[1] == "on")
        lst.append("%s|%s|%d" % (route, info["patch"], probe))
    _write(os.path.join(out, "list.txt"), "\n".join(lst) + "\n")
    return lst


# ------------------------------------------------------------------------------------------------- the export renderer

def preset_values(preset, root=os.path.join(HERE, "..", "..")):
    """a preset's values (Playback::presets() in libmscore/playbacksettings.cpp), as "id=value" items"""
    src = _read(os.path.join(root, "libmscore", "playbacksettings.cpp"))
    m = re.search(r'\{\s*"%s",\s*"[^"]*",\s*\{(.*?)\}\s*\}\s*\}' % re.escape(preset), src, re.S)
    if not m:
        raise SystemExit("no preset %s in playbacksettings.cpp" % preset)
    return ["%s=%s" % kv for kv in re.findall(r'\{\s*"([^"]+)",\s*(-?[0-9.]+)', m.group(1))]


def settings_tag(settings, own):
    """the playbackSettings metaTag the export copy carries, so that it plays as the dump renders (tst_playbackaudit
    auditScore): a metaTag string replaces the score's own; a preset id is the preset's values (the dump's ini) with the
    score's own metaTag over them"""
    if "=" in settings:
        return settings
    items = dict(i.split("=", 1) for i in preset_values(settings))
    for i in (own or "").split(";"):
        if "=" in i:
            k, v = i.split("=", 1)
            items[k.strip()] = v.strip()
    return ";".join("%s=%s" % kv for kv in items.items())


def set_settings(text, settings):
    tag = re.search(r'<metaTag name="playbackSettings">([^<]*)</metaTag>', text)
    value = settings_tag(settings, tag.group(1) if tag else "")
    esc = value.replace("&", "&amp;").replace("<", "&lt;")
    if tag:
        return text[:tag.start(1)] + esc + text[tag.end(1):], value
    m = re.search(r'\n(\s*)<metaTag ', text)
    assert m, "no metaTag in the score"
    at = m.start() + 1
    return text[:at] + m.group(1) + '<metaTag name="playbackSettings">' + esc + "</metaTag>\n" + text[at:], value


def only_part(text, name):
    """the partPlayback metaTag (libmscore/partplayback.h): part name on the library, every other part on MS4's sounds
    (HC_SPLIT=parts: one export run per part loads only its instances)"""
    names = [re.search(r"<trackName>([^<]*)</trackName>", p).group(1) for p in re.findall(r"<Part[ >].*?</Part>", text, re.S)]
    if name not in names:
        raise SystemExit("no part %s in the score (%s)" % (name, ", ".join(names)))
    modes = [{"part": i, "name": n, "mode": "library" if n == name else "ms4"} for i, n in enumerate(names)]
    value = json.dumps(modes).replace("&", "&amp;").replace("<", "&lt;").replace('"', "&quot;")
    text = re.sub(r'\s*<metaTag name="partPlayback">[^<]*</metaTag>', "", text)
    m = re.search(r'\n(\s*)<metaTag ', text)
    at = m.start() + 1
    return text[:at] + m.group(1) + '<metaTag name="partPlayback">' + value + "</metaTag>\n" + text[at:]


def score_parts(src):
    """the library parts' names of a .mscz / .mscx, in score order"""
    text = _read(src) if src.endswith(".mscx") else next(
        zipfile.ZipFile(src).read(n).decode("utf-8") for n in zipfile.ZipFile(src).namelist() if n.endswith(".mscx"))
    return [re.search(r"<trackName>([^<]*)</trackName>", p).group(1) for p in re.findall(r"<Part[ >].*?</Part>", text, re.S)]


def make_score(src, out, settings, part=None):
    """a copy of a .mscz / .mscx (out: the same kind) with the playbackSettings metaTag of settings_tag (and with part:
    only that part on the library, only_part)"""
    if src.endswith(".mscx"):
        text, value = set_settings(_read(src), settings)
        if part:
            text = only_part(text, part)
        _write(out, text)
        return value
    value = None
    with zipfile.ZipFile(src) as z, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as o:
        for info in z.infolist():
            data = z.read(info.filename)
            if info.filename.endswith(".mscx"):
                text, value = set_settings(data.decode("utf-8"), settings)
                if part:
                    text = only_part(text, part)
                data = text.encode("utf-8")
            o.writestr(info, data)
    assert value is not None, "no .mscx in " + src
    return value


def export_folders(root):
    """the --verify-playback folders under root (each holding "<score> events.tsv"): one per run (HC_SPLIT=parts: one per
    part), sorted"""
    return sorted({os.path.dirname(f) for f in glob.glob(os.path.join(root, "**", "* events.tsv"), recursive=True)})


def route_key(folders, i, code):
    return code if len(folders) == 1 else "%d/%s" % (i, code)


def read_export_ons(root):
    """the export's note-ons per route key (the route code "<port>-<channel + 1>", "<run>/<code>" with several runs) from
    each folder's "<score> events.tsv": {key: [(seconds, key number)]}, {key: folder}"""
    folders = export_folders(root)
    if not folders:
        raise SystemExit("no events.tsv under %s" % root)
    out, where = {}, {}
    for i, folder in enumerate(folders):
        ev = glob.glob(os.path.join(folder, "* events.tsv"))
        for ln in _read(ev[0]).splitlines()[1:]:
            c = ln.split("\t")
            if len(c) >= 6 and c[3] == "on" and int(c[5]) > 0:
                slot = int(c[2])
                k = route_key(folders, i, "%d-%d" % (slot // 16, slot % 16 + 1))
                out.setdefault(k, []).append((float(c[0]), int(c[4])))
                where[k] = folder
    for v in out.values():
        v.sort()
    return out, where


def read_dump_ons(dump, route):
    f = os.path.join(dump, route + " museScore.txt")
    if not os.path.exists(f):
        return []
    out = []
    for e in _read(f).splitlines():
        c = e.split()
        if len(c) >= 4 and c[1] == "on" and int(c[3]) > 0:
            out.append((float(c[0]), int(c[2])))
    return sorted(out)


# ------------------------------------------------------------------------------------------------------------- measures

def attack_time(env, first_ms, hop_ms, on_ms, prev_on_ms, end_ms, onset_db=ONSET_DB):
    """heard ms (render time) of an attack at note-on on_ms by the -15 dB rule, the note's span to end_ms; the envelope
    looked at back to the loudest value since prev_on_ms (the note-on before on its route). (ms, masked, peak dB) or
    (None, ...). A hard (step) onset reads ~24 ms before the step, a slow swell near its -15 dB time: the map's onsets were
    measured by the same rule, so the renderer's early starts aim at this reading (test_heard_check.py)"""
    def idx(t):
        return int(round((t - first_ms) / hop_ms))
    a0 = max(0, idx(max(prev_on_ms, on_ms - 1000.0)))
    i_on = max(a0, idx(on_ms))
    b = min(len(env), idx(end_ms) + 1)
    if b - i_on < 2:
        return None, False, None
    # (back to the note before's peak at most: its own rise is not this note's; looked for until 25 ms before this
    # note-on, where the analysis window starts to hear it)
    i_seen = max(a0, idx(on_ms - 25.0))
    a = a0 + int(np.argmax(env[a0:i_seen + 1])) if i_seen > a0 else a0
    k = i_on + int(np.argmax(env[i_on:b]))
    pk = float(env[k])
    seg = env[a:k + 1]
    below = np.where(seg < pk - onset_db)[0]
    if len(below):
        j = a + int(below[-1]) + 1
        return first_ms + hop_ms * j - LATENCY_MS, False, pk
    # masked: the note before keeps the level within 15 dB. Its tail, extrapolated linearly in dB from what the
    # envelope shows before it can hear this note-on (on - 75 .. on - 25 ms: the 2048-point window's half is 23 ms), is
    # taken off in loudness (the envelope is 33.2 log10 of summed specific loudness): this note's own envelope, and the
    # -15 dB rule on that
    t0, t1 = max(a, idx(on_ms - 75.0)), idx(on_ms - 25.0)
    if t1 - t0 < 3:
        return None, True, pk
    ts = np.arange(t0, t1 + 1)
    slope, icpt = np.polyfit(ts, env[t0:t1 + 1], 1)
    slope = min(slope, 0.0)                       # (a tail doesn't rise)
    span = np.arange(t1, k + 1)
    tail = env[t1] + slope * (span - t1)
    with np.errstate(divide="ignore", invalid="ignore"):
        own = 33.2 * np.log10(np.maximum(10 ** (env[t1:k + 1] / 33.2) - 10 ** (tail / 33.2), 1e-30))
    own_pk = float(own[-1])
    below = np.where(own < own_pk - onset_db)[0]
    if not len(below) or pk - float(env[t1]) < 1.0:
        return None, True, pk
    j = t1 + int(below[-1]) + 1
    return first_ms + hop_ms * j - LATENCY_MS, True, pk


def track(x, sr, lo, hi):
    """pYIN f0 (MIDI, NaN where unvoiced) of a whole route between MIDI pitches lo and hi, and its hop in s. A whole
    route at once: on short segments pYIN's voicing HMM drops the transitions themselves (Celli, 2026-10-16)"""
    import librosa
    fmin = 440.0 * 2 ** ((lo - 69) / 12)
    fmax = 440.0 * 2 ** ((hi - 69) / 12)
    frame = 2048 if fmin >= 2.0 * sr / 2048 else 4096
    f0, voiced, _ = librosa.pyin(x, fmin=fmin, fmax=fmax, sr=sr, frame_length=frame, hop_length=PYIN_HOP)
    with np.errstate(invalid="ignore", divide="ignore"):
        return np.where(voiced, 69 + 12 * np.log2(f0 / 440.0), np.nan), PYIN_HOP / sr


def transition_time(f0, fh, on_s, from_pitch, target_pitch, after_s=None, until_s=None):
    """heard s (render time) of a legato transition with note-on at on_s from from_pitch to target_pitch (fractional
    MIDI) on a route's f0 track (track()): f0 at the note before's pitch (within 30 cents) from after_s (the note
    before's heard time; else 300 ms before the note-on) on, then leaving it, then the first of 3 frames in a row within
    30 cents of the target, looked for until TRANSITION_SEARCH_S after the note-on or until_s (the next note-on on the
    route: a pitch reached after it may be that note's). (None, why): not found"""
    n = len(f0)
    tol = CENTS_TOL / 100
    end_s = on_s + TRANSITION_SEARCH_S if until_s is None else min(on_s + TRANSITION_SEARCH_S, until_s)
    i1 = min(n, int(round(end_s / fh)))
    i0 = max(0, int(round((on_s - 0.05) / fh)))
    if abs(from_pitch - target_pitch) > tol:
        ia = max(0, int(round(((after_s if after_s is not None else on_s - 0.3)) / fh)))
        at = next((i for i in range(ia, i1) if not np.isnan(f0[i]) and abs(f0[i] - from_pitch) <= tol), None)
        if at is None:
            return None, "the note before not heard at its pitch"
        i0 = max(i0, at)
        for i in range(i0, i1):
            if np.isnan(f0[i]) or abs(f0[i] - from_pitch) > tol:
                i0 = i
                break
        else:
            return None, "pitch not reached"
    for i in range(i0, i1 - ARRIVE_FRAMES + 1):
        w = f0[i:i + ARRIVE_FRAMES]
        if not np.isnan(w).any() and (np.abs(w - target_pitch) < tol).all():
            return i * fh, None
    return None, "pitch not reached"


def proportions(iois):
    s = sum(iois)
    return [v / s for v in iois] if s > 0 else None


def rhythm_deviation(written_onsets, heard_onsets):
    """max |p_heard - p_written| over the intervals of the onsets (seconds or ms; one unit)"""
    w = proportions([b - a for a, b in zip(written_onsets, written_onsets[1:])])
    h = proportions([b - a for a, b in zip(heard_onsets, heard_onsets[1:])])
    if not w or not h:
        return None
    return max(abs(a - b) for a, b in zip(w, h))


def situation(row, rows_by_note):
    if row["transition"]:
        src = rows_by_note.get((row["part"], round(row["fromWritten"], 4), row["fromPitch"]))
        return "transition after transition" if src and src["transition"] else "transition after attack"
    bases = set(row["techniques"].split(",")[:1]) if row["techniques"] else set()
    if bases & SHORT_BASES:
        return "staccato"
    return "attack joined" if row["joinedBefore"] else "first after silence"


def pct(v, q):
    return float(np.percentile(np.asarray(v, dtype=float), q))


# ------------------------------------------------------------------------------------------------------------- analysis

def analyse(dump, wavs, parts=None, load=None, export=False):
    """the measurements and findings (a dict, the JSON's content). load(route) -> (x, sr) or None (default: the wav).
    export: wavs is MuseScore's --verify-playback folder (MS_VERIFY_ROUTES: "<score> route <code>.wav", sample 0 = score
    time 0 + EXPORT_OFFSET_S) and each note's note-on is the export's own (its events.tsv, paired with the dump's)"""
    import soundfile as sf
    from loudness import perceived_envelope
    lead = read_lead_ms(dump)
    rows = read_rows(dump)
    routemap = read_routemap(dump)
    bars = read_bars(dump)
    if parts:
        rows = [r for r in rows if r["part"] in parts]
    rows_by_note = {(r["part"], round(r["written"], 4), r["pitch"]): r for r in rows}

    offset = EXPORT_OFFSET_S if export else OFFSET_S

    def load_wav(route):
        if export:
            folder = export_where.get(route, wavs)
            f = glob.glob(os.path.join(folder, "* route %s.wav" % route.split("/")[-1]))
            f = f[0] if f else ""
        else:
            f = os.path.join(wavs, route + ".wav")
        if not os.path.exists(f):
            return None
        x, sr = sf.read(f, always_2d=False)
        return x, sr
    export_where = {}
    load = load or load_wav
    by_route = {}
    for r in rows:
        r["onMs"] = (r["written"] + offset) * 1000 + lead + r["sentMs"]      # render time of its note-on
        by_route.setdefault(r["route"], []).append(r)
    # the export's own note-ons (what played): each row's note-on moved to its pair in events.tsv (its key, nearest in
    # time within 100 ms, any route: the export's routes are what played, so a row the dump put on another lane of its
    # patch is re-routed; seen once in 13 dumps of Whence, 2026-10-16: Violas - Performance on 2 lanes instead of 3)
    # (each export run's own start lead: with HC_SPLIT=parts a run plays one part, whose lead may differ from the whole
    # score's; its shift against the dump, the mode of the same-key differences within 0.5 s, is taken out first)
    pairing = {}
    if export:
        import bisect
        eons, export_where = read_export_ons(wavs)
        alle = sorted((t, k, route) for route, v in eons.items() for (t, k) in v)
        times = [e[0] for e in alle]
        dons = {route: read_dump_ons(dump, route) for route in by_route}
        alld = sorted((t, k) for v in dons.values() for (t, k) in v)
        dtimes = [d[0] for d in alld]
        shift = {}
        for folder in set(export_where.values()):
            hist = {}
            for (t, k, route) in alle:
                if export_where[route] != folder:
                    continue
                for j in range(bisect.bisect_left(dtimes, t - 0.5), bisect.bisect_right(dtimes, t + 0.5)):
                    if alld[j][1] == k:
                        b_ = int(round(1000 * (t - alld[j][0])))
                        hist[b_] = hist.get(b_, 0) + 1
            # (a run's shift only when most of its notes agree on it, at least 5: a whole run moved by its lead, not
            # single notes another build or setting plays at other times)
            n_run = sum(1 for e in alle if export_where[e[2]] == folder)
            # (1 ms bins: a shift on a bin's edge splits; the support within 2 ms of the mode, its mean)
            best = max(hist, key=lambda b_: (sum(hist.get(b_ + d, 0) for d in range(-2, 3)), -abs(b_))) if hist else 0
            near = {b_: hist.get(b_, 0) for b_ in range(best - 2, best + 3)}
            support = sum(near.values())
            mean = sum(b_ * c for b_, c in near.items()) / support if support else 0.0
            shift[folder] = mean / 1000 if hist and support >= max(5, 0.8 * n_run) else 0.0
        used, diffs, unpaired, rerouted = set(), [], 0, 0
        for r in sorted(rows, key=lambda r: r["onMs"]):
            t = r["onMs"] / 1000 - offset
            d = dons.get(r["route"], [])
            # (its event in the dump: at its time, the key nearest its pitch: a chord's notes share the time)
            i = min(range(len(d)), key=lambda i: (round(abs(d[i][0] - t), 3), abs(d[i][1] - r["pitch"] - r["cents"] / 100)),
                    default=None)
            if i is None or abs(d[i][0] - t) > 0.002:
                unpaired += 1
                continue
            key = d[i][1]
            lo, hi = bisect.bisect_left(times, t - 0.6), bisect.bisect_right(times, t + 0.6)
            cand = [j for j in range(lo, hi) if j not in used and alle[j][1] == key
                    and abs(alle[j][0] - shift[export_where[alle[j][2]]] - t) < 0.1]
            if not cand:
                unpaired += 1
                continue
            j = min(cand, key=lambda j: (round(abs(alle[j][0] - shift[export_where[alle[j][2]]] - t), 4),
                                         alle[j][2] != r["route"]))
            used.add(j)
            sh = shift[export_where[alle[j][2]]]
            diffs.append(1000 * (alle[j][0] - sh - t))
            if alle[j][2] != r["route"]:
                # (counted when the code differs: a split run numbers its own part's routes from 0-1, which isn't one)
                rerouted += alle[j][2].split("/")[-1] != r["route"] and len(set(export_where.values())) == 1
                r["dumpRoute"] = r["route"]
                r["route"] = alle[j][2]
            r["dumpOnMs"] = r["onMs"]
            r["exportRun"] = export_where[alle[j][2]]
            r["leadMs"] = lead + 1000 * sh
            r["onMs"] = (alle[j][0] + offset) * 1000
            r["sentMs"] = r["onMs"] - (r["written"] + offset) * 1000 - r["leadMs"]
        # (a shifted run's lead refined to the median of its notes' exact differences: the histogram's 1 ms bins)
        for folder in set(export_where.values()):
            rr = [r for r in rows if r.get("exportRun") == folder]
            if rr and shift[folder] != 0.0:
                dm = float(np.median([r["onMs"] - offset * 1000 - r["dumpOnMs"] + offset * 1000 for r in rr]))
                for r in rr:
                    r["leadMs"] = lead + dm
                    r["sentMs"] = r["onMs"] - (r["written"] + offset) * 1000 - r["leadMs"]
                shift[folder] = dm / 1000
        diffs = [r["onMs"] - r["dumpOnMs"] - (r["leadMs"] - lead) for r in rows if "dumpOnMs" in r]
        pairing = dict(rows=len(rows), paired=len(diffs), unpaired=unpaired, rerouted=rerouted, exportOns=len(alle),
                       runs=len(set(export_where.values())), shiftsMs=sorted({round(1000 * v, 1) for v in shift.values()}),
                       medianMs=round(float(np.median(diffs)), 3) if diffs else None,
                       maxAbsMs=round(float(max(abs(x) for x in diffs)), 3) if diffs else None)
        by_route = {}
        for r in rows:
            by_route.setdefault(r["route"], []).append(r)
    missing = []
    for route, rr in by_route.items():
        rr.sort(key=lambda r: r["onMs"])
        audio = load(route)
        if audio is None:
            missing.append(route)
            for r in rr:
                r["heard"] = None
                r["how"] = "no render"
            continue
        x, sr = audio
        mono = x.mean(axis=1) if x.ndim == 2 else x
        env, first, hop = perceived_envelope(x, sr)
        f0 = None
        if any(r["transition"] for r in rr):
            ps = [r["pitch"] + r["cents"] / 100 for r in rr] + [r["fromPitch"] for r in rr if r["transition"]]
            f0, fh = track(mono, sr, min(ps) - 3, max(ps) + 3)
        ons = sorted({round(r["onMs"], 3) for r in rr})
        for r in rr:
            on = r["onMs"]
            prev = [t for t in ons if t < on - 0.5]
            nxt = [t for t in ons if t > on + 0.5]
            prev_on = prev[-1] if prev else on - 1000.0
            end = min(nxt[0] if nxt else on + SPAN_MAX_S * 1000, on + SPAN_MAX_S * 1000)
            if not r["transition"] and r["slur"] >= 0:
                # a slur's first note: its peak looked for over the slur on its route (what it starts is one sound; the
                # map's onsets were measured on single notes against their own peak, the slur's level here)
                later = [q for q in rr if q["slur"] == r["slur"] and q["part"] == r["part"] and q["onMs"] > on + 0.5]
                if later and all(q["transition"] for q in later):
                    slur_end = (max(q["writtenEnd"] for q in later) + offset) * 1000 + r.get("leadMs", lead)
                    end = min(max(end, slur_end), on + SPAN_MAX_S * 1000)
            chord = sum(1 for q in rr if abs(q["onMs"] - on) < 0.5) > 1
            r["chordOnRoute"] = chord
            r["masked"] = False
            if r["transition"]:
                if chord:
                    r["heard"], r["how"] = None, "chord on its route (pitch not tracked)"
                    continue
                src = rows_by_note.get((r["part"], round(r["fromWritten"], 4), r["fromPitch"]))
                fp = r["fromPitch"] + (src["cents"] if src else 0.0) / 100
                tp = r["pitch"] + r["cents"] / 100
                before = [q for q in rr if q["onMs"] < on - 0.5 and q.get("heardRender") is not None]
                after = before[-1]["heardRender"] / 1000 if before else None
                t, why = transition_time(f0, fh, on / 1000, fp, tp, after, nxt[0] / 1000 if nxt else None)
                if t is None:
                    r["heard"], r["how"] = None, why
                    # (how near the sound came: the f0 closest to the target between its note-on and the next one)
                    i0, i1 = int(round(on / 1000 / fh)), int(round((nxt[0] if nxt else on + 700) / 1000 / fh))
                    w = f0[max(0, i0):max(i0 + 1, i1)]
                    w = w[~np.isnan(w)]
                    if len(w):
                        r["closestCents"] = float(100 * (w[np.argmin(np.abs(w - tp))] - tp))
                else:
                    r["heardRender"] = t * 1000
                    r["how"] = "pitch"
            else:
                t, masked, _ = attack_time(env, first, hop, on, prev_on, end)
                t6, _, _ = attack_time(env, first, hop, on, prev_on, end, ALT_DB)
                r["heardAlt"] = t6 - (r["written"] + offset) * 1000 - r.get("leadMs", lead) if t6 is not None else None
                r["masked"] = masked
                if t is None:
                    r["heard"], r["how"] = None, "no rise"
                else:
                    r["heardRender"] = t
                    r["how"] = "envelope (masked)" if masked else "envelope"
            if "heardRender" in r:
                r["heard"] = r["heardRender"] - (r["written"] + offset) * 1000 - r.get("leadMs", lead)
        # each note's loudest level over its heard span (to the next heard note on the route)
        heard_on = sorted((r["heardRender"], r["id"]) for r in rr if r.get("heardRender") is not None)
        for i, (t, rid) in enumerate(heard_on):
            r = next(q for q in rr if q["id"] == rid)
            t1 = heard_on[i + 1][0] if i + 1 < len(heard_on) else t + SPAN_MAX_S * 1000
            a = max(0, int(round((t + LATENCY_MS - first) / hop)))
            b = min(len(env), max(a + 1, int(round((min(t1, t + SPAN_MAX_S * 1000) + LATENCY_MS - first) / hop))))
            r["levelDb"] = float(env[a:b].max()) if b > a else None

    findings = []

    def where(r):
        return dict(part=r["part"], bar=r["bar"], beat=round(r["beat"], 2), pitch=r["pitch"], cents=r["cents"],
                    patch=r["patch"], articulation=r["articulation"])

    # (d0) a transition whose pitch is never reached before the next note-on on its route (skipped, or not tracked)
    for r in rows:
        if r["transition"] and r.get("how") == "pitch not reached" and not r["grace"] and not r["sub"]:
            findings.append(dict(kind="UNHEARD", severity="WARN", value=0.0, seconds=r["written"],
                                 text="its pitch (%+.0f cents) not reached within 30 cents before the next note-on on its route "
                                      "(%s; closest %s)" % (r["cents"], r["route"], "%+.0f cents" % r["closestCents"]
                                                            if r.get("closestCents") is not None else "unvoiced"), **where(r)))

    # (d1) heard against the note before in its line (the audit's ARRIVAL pairs: a chord's earliest)
    groups = {}
    for r in rows:
        if r["grace"] or r["sub"]:
            continue
        groups.setdefault((r["part"], r["track"]), {}).setdefault(round(r["written"], 6), []).append(r)

    def earliest(g):
        h = [r for r in g if r.get("heard") is not None]
        return min(h, key=lambda r: r["heard"]) if h else None
    lines = {}
    for key, gs in groups.items():
        seq = [gs[t] for t in sorted(gs)]
        lines[key] = seq
        for a, b in zip(seq, seq[1:]):
            if not any(r["joinedBefore"] for r in b):
                continue
            p, q = earliest(b), earliest(a)
            if not p or not q:
                continue
            d = p["heard"] - q["heard"]
            if abs(d) > ARRIVAL_WARN_MS:
                findings.append(dict(kind="ARRIVAL", severity="FAIL" if abs(d) > ARRIVAL_FAIL_MS else "WARN", value=round(d, 1),
                                     seconds=p["written"], text="heard %+.1f ms against the note before (this %+.1f ms %s, %s; "
                                     "before: pitch %d %s %+.1f ms %s)" % (d, p["heard"], p["how"], p["articulation"], q["pitch"],
                                                                           q["articulation"], q["heard"], q["how"]), **where(p)))

    # (c, d2) rhythm: per slur (its notes + the next onset joined to it) and per run (chords joined without a rest)
    groups_out = []
    alt = []                                # the slurs' max |dp| with the -6 dB reading of attacks

    def judge(kind, seq, label, key="heard"):
        """windows of 3 IOIs (a slur of 3 notes: its 2), each with every onset measured; a slur: its worst window; a run:
        the worst of each stretch of windows above RHYTHM_WARN"""
        onsets_w = [g[0]["written"] * 1000 for g in seq]
        hs = [earliest(g) for g in seq]
        if key != "heard":                  # (the -6 dB reading for attacks; transitions as measured)
            hs = [h if h is None or h["transition"] or h.get(key) is not None else None for h in hs]
        onsets_h = [h["written"] * 1000 + (h["heard"] if h["transition"] else h.get(key, h["heard"])) if h else None for h in hs]
        n_ioi = len(seq) - 1
        win = 3 if n_ioi >= 3 else n_ioi
        devs = []
        for i in range(0, n_ioi - win + 1):
            if any(v is None for v in onsets_h[i:i + win + 1]):
                devs.append(None)
                continue
            devs.append(rhythm_deviation(onsets_w[i:i + win + 1], onsets_h[i:i + win + 1]))
        measured = [d for d in devs if d is not None]
        if key != "heard":
            alt.append(max([d for d in devs if d is not None], default=None))
            return
        groups_out.append(dict(kind=kind, label=label, notes=len(seq), windows=len(devs), measured=len(measured),
                               maxDeviation=round(max(measured), 3) if measured else None,
                               writtenIoiMs=[round(b - a, 1) for a, b in zip(onsets_w, onsets_w[1:])],
                               heardIoiMs=[round(b - a, 1) if a is not None and b is not None else None
                                           for a, b in zip(onsets_h, onsets_h[1:])]))
        picks = []
        if kind == "slur":
            if measured:
                i = max((i for i, d in enumerate(devs) if d is not None), key=lambda i: devs[i])
                if devs[i] > RHYTHM_WARN:
                    picks.append(i)
        else:
            i = 0
            while i < len(devs):
                if devs[i] is not None and devs[i] > RHYTHM_WARN:
                    j = i
                    while j + 1 < len(devs) and devs[j + 1] is not None and devs[j + 1] > RHYTHM_WARN:
                        j += 1
                    picks.append(max(range(i, j + 1), key=lambda k: devs[k]))
                    i = j + 1
                else:
                    i += 1
        for i in picks:
            d = devs[i]
            first = hs[i]
            ih = [onsets_h[k + 1] - onsets_h[k] for k in range(i, i + win)]
            iw = [onsets_w[k + 1] - onsets_w[k] for k in range(i, i + win)]
            masked = any(h["masked"] for h in hs[i:i + win + 1])
            findings.append(dict(kind="RHYTHM", group=kind, severity="FAIL" if d > RHYTHM_FAIL else "WARN", value=round(d, 3),
                                 seconds=first["written"], text="%s: heard IOIs %s ms for written %s (max |dp| %.3f%s)" % (
                                     label, " / ".join("%.0f" % v for v in ih), " / ".join("%.0f" % v for v in iw), d,
                                     ", a masked onset" if masked else ""), **where(first)))
    for (part, trk), seq in lines.items():
        # slurs
        by_slur = {}
        for i, g in enumerate(seq):
            s = g[0]["slur"]
            if s >= 0:
                by_slur.setdefault(s, []).append(i)
        for s, idx in by_slur.items():
            sub = [seq[i] for i in idx]
            nxt = idx[-1] + 1
            if nxt < len(seq) and any(r["joinedBefore"] for r in seq[nxt]) and seq[nxt][0]["slur"] != s:
                sub.append(seq[nxt])
            if len(sub) >= 3:
                judge("slur", sub, "slur of %d from bar %d beat %.2f" % (len(idx), sub[0][0]["bar"], sub[0][0]["beat"]))
                judge("slur", sub, "", "heardAlt")
        # runs
        run = [seq[0]] if seq else []
        for g in seq[1:]:
            if any(r["joinedBefore"] for r in g):
                run.append(g)
            else:
                if len(run) >= 4:
                    judge("run", run, "run of %d from bar %d beat %.2f" % (len(run), run[0][0]["bar"], run[0][0]["beat"]))
                run = [g]
        if len(run) >= 4:
            judge("run", run, "run of %d from bar %d beat %.2f" % (len(run), run[0][0]["bar"], run[0][0]["beat"]))

    # (d3) level steps under a slur at an unchanged written dynamic (the audit's pairs: top notes)
    for key, seq in lines.items():
        for a, b in zip(seq, seq[1:]):
            tb = max(b, key=lambda r: r["pitch"])
            ta = max(a, key=lambda r: r["pitch"])
            if not tb["slurredBefore"] or tb["dynamicBefore"]:
                continue
            if ta.get("levelDb") is None or tb.get("levelDb") is None:
                continue
            step = tb["levelDb"] - ta["levelDb"]
            tb["levelStep"] = step
            if abs(step) > LEVEL_DL_DB:
                findings.append(dict(kind="LEVEL", severity="WARN", value=round(step, 2), seconds=tb["written"],
                                     text="%+.2f dB after the note before (pitch %d, %s)" % (step, ta["pitch"], ta["articulation"]),
                                     **where(tb)))

    # (e) model drift
    drift = {}
    for r in rows:
        if r.get("heard") is None or not r["hasArrival"] or r["grace"] or r["sub"]:
            continue
        r["situation"] = situation(r, rows_by_note)
        drift.setdefault((r["patch"], r["situation"]), []).append(r["heard"] - r["arrivalMs"])
    drift_out = []
    for (patch, sit), v in sorted(drift.items()):
        med = float(np.median(v))
        drift_out.append(dict(patch=patch, situation=sit, n=len(v), medianMs=round(med, 1), p10Ms=round(pct(v, 10), 1),
                              p90Ms=round(pct(v, 90), 1), drifting=abs(med) > DRIFT_MS))
    notes_out = []
    for r in sorted(rows, key=lambda r: (r["part"], r["written"], r["pitch"])):
        notes_out.append(dict(where(r), written=round(r["written"], 4), route=r["route"], sentMs=round(r["sentMs"], 1),
                              predictedMs=round(r["arrivalMs"], 1) if r["hasArrival"] else None,
                              heardMs=round(r["heard"], 1) if r.get("heard") is not None else None, how=r.get("how"),
                              heardAltMs=round(r["heardAlt"], 1) if r.get("heardAlt") is not None else None,
                              closestCents=round(r["closestCents"], 1) if r.get("closestCents") is not None else None,
                              transition=bool(r["transition"]), situation=r.get("situation"),
                              levelDb=round(r["levelDb"], 2) if r.get("levelDb") is not None else None))
    order = {"ARRIVAL": 0, "RHYTHM": 1, "LEVEL": 2, "UNHEARD": 3}
    findings.sort(key=lambda f: (order[f["kind"]], {"FAIL": 0, "WARN": 1}[f["severity"]], f["part"], f["seconds"]))
    silent = sorted(r for r in by_route if not export and os.path.exists(os.path.join(wavs, r + ".log"))
                    and "probe SILENT" in _read(os.path.join(wavs, r + ".log")))
    alt = [d for d in alt if d is not None]
    return dict(lead=lead, renderer="export" if export else "kthost", pairing=pairing, missingRenders=missing,
                probeSilent=silent,
                slursAlt6=dict(measured=len(alt), fail=sum(1 for d in alt if d > RHYTHM_FAIL),
                               warn=sum(1 for d in alt if RHYTHM_WARN < d <= RHYTHM_FAIL)), notes=notes_out, groups=groups_out, findings=findings, drift=drift_out,
                thresholds=dict(arrivalWarnMs=ARRIVAL_WARN_MS, arrivalFailMs=ARRIVAL_FAIL_MS, rhythmWarn=RHYTHM_WARN,
                                rhythmFail=RHYTHM_FAIL, rhythmProposed=False, levelDlDb=LEVEL_DL_DB, driftMs=DRIFT_MS))


def nothing_measured(res):
    """why the check measured nothing (a failure, not an empty pass), else None: no library note, no route's render found
    (2026-10-16 .. 18 the export renderer wrote no route WAV for a score of more than one part, crash-fixes 7f51a91979, and
    the check reported all zeros), or no note's heard time read"""
    n = res["notes"]
    routes = {x["route"] for x in n}
    if not n:
        return "no library note in the dump (the parts asked for: none on the library?)"
    if routes and set(res["missingRenders"]) >= routes:
        return "no render found for any of the %d routes (0 route WAVs): nothing measured, the renderer failed" % len(routes)
    if not any(x["heardMs"] is not None for x in n):
        return "0 of %d notes measured" % len(n)
    return None


def report_text(res, title=""):
    out = []
    if title:
        out.append(title)
    why = nothing_measured(res)
    if why:
        out.append("FAIL: NOTHING MEASURED: %s. The counts below are empty, not a pass." % why)
    n = res["notes"]
    meas = [x for x in n if x["heardMs"] is not None]
    masked = sum(1 for x in meas if x["how"] == "envelope (masked)")
    out.append("%d library notes, %d measured (%d of them masked attacks), %d not: %s" % (
        len(n), len(meas), masked, len(n) - len(meas),
        ", ".join("%s %d" % (k, sum(1 for x in n if x["heardMs"] is None and x["how"] == k))
                  for k in sorted({x["how"] for x in n if x["heardMs"] is None}))))
    pr = res.get("pairing") or {}
    if pr:
        out.append("export renderer: %d of %d notes paired with the export's note-ons (%d in all, %d run%s, lead shifts %s ms; "
                   "%d unpaired, %d re-routed; export - dump median %s ms, max |d| %s ms); times as the export played them" % (
                       pr["paired"], pr["rows"], pr["exportOns"], pr.get("runs", 1), "" if pr.get("runs", 1) == 1 else "s",
                       pr.get("shiftsMs", [0.0]), pr["unpaired"], pr["rerouted"], pr["medianMs"], pr["maxAbsMs"]))
    if res["missingRenders"]:
        out.append("no render for routes: " + ", ".join(res["missingRenders"]))
    if res.get("probeSilent"):
        out.append("kthost's probe note stayed silent (samples maybe not all loaded; check those routes): " + ", ".join(res["probeSilent"]))
    t = res["thresholds"]
    out.append("thresholds: ARRIVAL warn > %.0f, fail > %.0f ms (Rasch 1979); RHYTHM warn > %.2f, fail > %.2f max |dp| "
               "(approved 2026-10-16: Desain & Honing 2003 / Sadakata et al. 2006); LEVEL warn > %.2f dB (proposed, "
               "Jesteadt et al. 1977); DRIFT |median| > %.0f ms" % (t["arrivalWarnMs"], t["arrivalFailMs"], t["rhythmWarn"],
                                                                    t["rhythmFail"], t["levelDlDb"], t["driftMs"]))
    for k, sub in (("ARRIVAL", None), ("RHYTHM", "slur"), ("RHYTHM", "run"), ("LEVEL", None), ("UNHEARD", None)):
        f = [x for x in res["findings"] if x["kind"] == k and (sub is None or x.get("group") == sub)]
        out.append("  %-14s %d fail, %d warn" % (k + (" (%ss)" % sub if sub else ""), sum(1 for x in f if x["severity"] == "FAIL"),
                                                 sum(1 for x in f if x["severity"] == "WARN")))
    a = res.get("slursAlt6")
    if a:
        out.append("  (slurs with attacks read at -6 dB instead: %d fail, %d warn of %d measured; not judged)" % (a["fail"], a["warn"], a["measured"]))
    for kind in ("slur", "run"):
        g = [x for x in res["groups"] if x["kind"] == kind]
        out.append("  %ss: %d, %d of their %d windows measured" % (kind, len(g), sum(x["measured"] for x in g), sum(x["windows"] for x in g)))
    out.append("")
    out.append("== model drift (measured - the audit's predicted heard time, ms)")
    out.append("  %-26s %-28s %4s %8s %8s %8s" % ("patch", "situation", "n", "median", "p10", "p90"))
    for d in res["drift"]:
        out.append("  %-26s %-28s %4d %+8.1f %+8.1f %+8.1f%s" % (d["patch"], d["situation"], d["n"], d["medianMs"], d["p10Ms"],
                                                               d["p90Ms"], "  DRIFT" if d["drifting"] else ""))
    last = None
    for f in res["findings"]:
        if f["kind"] != last:
            out.append("")
            out.append("== " + f["kind"])
            last = f["kind"]
        out.append("%s %s bar %d beat %.2f pitch %d%s %s / %s: %s" % (
            f["severity"], f["part"], f["bar"], f["beat"], f["pitch"], " %+.0fc" % f["cents"] if f["cents"] else "", f["patch"],
            f["articulation"], f["text"]))
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("events")
    e.add_argument("dump")
    e.add_argument("out")
    e.add_argument("--parts")
    a = sub.add_parser("analyse")
    a.add_argument("dump")
    a.add_argument("wavs")
    a.add_argument("--report")
    a.add_argument("--json")
    a.add_argument("--parts")
    a.add_argument("--title", default="")
    a.add_argument("--export", action="store_true", help="wavs: MuseScore's --verify-playback folder (MS_VERIFY_ROUTES)")
    m = sub.add_parser("score")
    m.add_argument("src")
    m.add_argument("out")
    m.add_argument("settings")
    m.add_argument("--part", help="only this part on the library (the others on MS4's sounds)")
    pl = sub.add_parser("parts")
    pl.add_argument("score")
    pl.add_argument("dump")
    args = ap.parse_args(argv)
    if args.cmd == "score":
        print(make_score(args.src, args.out, args.settings, args.part))
        return 0
    if args.cmd == "parts":
        # (the parts with library routes in the dump, in score order: one export run each with HC_SPLIT=parts)
        lib = {v["part"] for v in read_routemap(args.dump).values()}
        for n in score_parts(args.score):
            if n in lib:
                print(n)
        return 0
    parts = set(args.parts.split(",")) if args.parts else None
    if args.cmd == "events":
        for ln in make_events(args.dump, args.out, parts):
            print(ln)
        return 0
    res = analyse(args.dump, args.wavs, parts, export=args.export)
    text = report_text(res, args.title)
    sys.stdout.write(text)
    if args.report:
        _write(args.report, text)
    if args.json:
        _write(args.json, json.dumps(res, indent=1))
    if nothing_measured(res):
        sys.stderr.write("heard check: FAIL: nothing measured: %s\n" % nothing_measured(res))
        return 1
    return 1 if any(f["severity"] == "FAIL" for f in res["findings"]) else 0


if __name__ == "__main__":
    sys.exit(main())
