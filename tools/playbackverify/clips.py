#!/usr/bin/env python3
"""clips.py: listening clips from heard-check renders: one section (or several) alone, with a metronome click on every
written beat, A/B variants at one gain, long rests cut out, and a JSON of every note's written and heard time.

The owner judges timing by one section alone with a click (2026-10-21, Whence bars 5-14). This makes such clips from the
heard check's own export renders (heard_check.sh with HC_KEEP=1: <work>/wav/out/<score…>/Playback verify */<score>
route <code>.wav), so the clip is exactly what the heard check measured.

  clips.py make --variant A=<hc work> [--variant B=<hc work> ...] --bars a-b -o <dir>
                [--parts "Violins 1,Violas"] [--mix] [--override "Violins 1:A=<hc work>"] [--name whence]
                [--count-in 1] [--pre 0.3] [--beats N] [--no-cut] [--max-len 20] [--min-cut-bars 1]
                [--shared-gain part|all|none] [--click-db -12] [--accent-db -8] [--wav] [--no-mp3]
  clips.py measure <dir> [--clip name ...] [--dump <hc work>]
                each clip's transitions read in the clip itself (the click subtracted): leave (+23.5 ms) and arrival,
                next to the heard check's own readings; -> <dir>/measured.json (for checking the heard check against
                what a listener heard)

make: per part (default: every library part with renders; --mix: the parts together in one clip) and variant, the part's
routes added up on score time (each export run's start lead taken out, as soloclips.py did), bars a-b with --count-in
bars of clicks before (the music before the cut sounds under it), 0.3 s of pre-roll. Clicks: 8 ms sine bursts, 1.5 kHz
on each bar's first beat (--accent-db under the comparison's peak), 1 kHz on the others (--click-db); beats from the
dump's bars.txt and the notes' written beats (per bar: its length / the beat's length; --beats N for every bar).
Long rests (the owner, 2026-10-21: "so much silence"): whole bars where the clip's part(s) play nothing are cut out, from
the first bar line at or after the first beat after the last note's written end (its tail sounds at least to that beat)
to the bar line that leaves a bar of clicks (less one beat: a pickup's own bar) before the next note sounds (its early
start included), when that removes at least --min-cut-bars bars (--cut-beats: on beats, one bar of clicks before); the same at the clip's start
(the lead-in bar replaces the count-in) and end. Each cut joins with a 10 ms crossfade; the clicks are laid on the joined
timeline, so the grid stays whole. A clip longer than --max-len s after the cuts is split at bar lines into parts (each
with its own count-in). One gain per comparison: --shared-gain part (default: a part's variants), all, none; the loudest
peak at -1 dBFS. MP3 320k by ffmpeg (--wav keeps the WAV).

Writes <dir>/<name>_<part>_<variant>[_pN].mp3, <dir>/clips.json and <dir>/index.html (a review page skeleton: the
clips, what to listen for, the largest A/B differences). clips.json, per clip: score bars, clipStartScoreS, the cuts
(clipS where it joins, scoreFrom, scoreTo, removedS: score second t >= clipStartScoreS maps to clip second
t - clipStartScoreS - the removedS of every cut with scoreTo <= t), beats (bar, beat, clipS), gainDb, and notes (part,
bar, beat, pitch, cents, patch, articulation, kind, transition, fromPitch, scoreS, writtenS / onS / heardS in clip
seconds, sentMs, predictedMs, heardMs, leaveMs, arriveMs, how; heard values from the variant's report.json).
MP3 decoders may add ~26 ms of delay: clicks and music move together (measure aligns on the clicks).
"""
import argparse
import csv
import glob
import json
import math
import os
import subprocess
import sys
import tempfile

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

XFADE_S = 0.010
CLICK_S = 0.008
LEAVE_HEARD_MS = 23.5


# ---------------------------------------------------------------------------------------------------------------- input

def read_tsv(path):
    with open(path, encoding="utf-8") as f:
        return list(csv.DictReader(f, delimiter="\t"))


class Work:
    """one heard-check work dir"""

    def __init__(self, path):
        self.path = path
        d = os.path.join(path, "dump")
        if not os.path.exists(os.path.join(d, "predicted.tsv")):
            raise SystemExit("%s: no dump/predicted.tsv (not a heard-check work dir)" % path)
        self.rows = read_tsv(os.path.join(d, "predicted.tsv"))
        for r in self.rows:
            for k in ("written", "writtenEnd", "sentMs", "beat", "cents", "fromWritten", "arrivalMs"):
                r[k] = float(r[k])
            for k in ("bar", "pitch", "fromPitch", "transition", "hasArrival", "slur"):
                r[k] = int(r[k])
        with open(os.path.join(d, "bars.txt"), encoding="utf-8") as f:
            self.bars = [float(x) for x in f if x.strip()]
        self.routemap = {}
        if os.path.exists(os.path.join(d, "routemap.txt")):
            with open(os.path.join(d, "routemap.txt"), encoding="utf-8") as f:
                for ln in f:
                    p = ln.rstrip("\n").split("|")
                    if len(p) >= 3:
                        self.routemap[p[0]] = p[1]
        self.parts_txt = []
        if os.path.exists(os.path.join(path, "parts.txt")):
            with open(os.path.join(path, "parts.txt"), encoding="utf-8") as f:
                self.parts_txt = [x.strip() for x in f if x.strip()]
        self.report = {}
        rp = os.path.join(path, "report.json")
        if os.path.exists(rp):
            with open(rp, encoding="utf-8") as f:
                for n in json.load(f).get("notes", []):
                    self.report[(n["part"], round(n["written"], 3), n["pitch"])] = n

    def parts(self):
        seen = []
        for r in self.rows:
            if r["part"] not in seen:
                seen.append(r["part"])
        return seen

    def folder(self, part):
        """(export folder with the part's route WAVs, split run or not)"""
        out = os.path.join(self.path, "wav", "out")
        if self.parts_txt and part in self.parts_txt:
            g = glob.glob(os.path.join(out, "score-%d" % (self.parts_txt.index(part) + 1), "Playback verify *"))
            if g:
                return g[0], True
        g = glob.glob(os.path.join(out, "score", "Playback verify *")) or glob.glob(os.path.join(out, "*", "Playback verify *"))
        if not g:
            raise SystemExit("%s: no export renders for %s (heard_check.sh with HC_KEEP=1 keeps them)" % (self.path, part))
        return g[0], False

    def audio(self, parts, t0, t1):
        """the parts' routes added up, score time t0..t1 (t0 may be < 0); (mono-or-stereo array, sr, routes)"""
        import soundfile as sf
        mix, sr, n_routes = None, 44100, 0
        for part in parts:
            d, split = self.folder(part)
            evf = glob.glob(os.path.join(d, "*events.tsv"))
            ev = [r for r in read_tsv(evf[0]) if r["type"] == "on"] if evf else []
            rows = [r for r in self.rows if r["part"] == part] if split else self.rows
            lead = (min(float(r["time"]) for r in ev) - min(r["written"] + r["sentMs"] / 1000 for r in rows)) if ev and rows else 0.0
            for w in sorted(glob.glob(os.path.join(d, "* route *.wav"))):
                code = w.rsplit(" route ", 1)[1][:-4]
                if not split and self.routemap and self.routemap.get(code) not in (None, part):
                    continue
                x, sr = sf.read(w, dtype="float32", always_2d=True)
                n = int(round((t1 - t0) * sr))
                s0 = int(round((t0 + lead) * sr))
                y = np.zeros((n, x.shape[1]), np.float32)
                a, b = max(0, s0), min(len(x), s0 + n)
                if b > a:
                    y[a - s0:b - s0] = x[a:b]
                mix = y if mix is None else mix + y
                n_routes += 1
        if mix is None:
            raise SystemExit("%s: no route WAVs for %s" % (self.path, ", ".join(parts)))
        return mix, sr, n_routes


# ----------------------------------------------------------------------------------------------------------- the grid

def beat_grid(work, beats_override=None):
    """[(bar, beat, score s)] for every bar of bars.txt (and one bar more), beats per bar from the notes"""
    bars = work.bars + [work.bars[-1] + (work.bars[-1] - work.bars[-2] if len(work.bars) > 1 else 2.0)]
    beat_len = {}
    by = {}
    for r in work.rows:
        if str(r.get("grace", "0")) not in ("0", ""):
            continue
        by.setdefault(r["bar"], {})[round(r["beat"], 4)] = r["written"]
    for bar, d in by.items():
        b = sorted(d)
        ls = [(d[y] - d[x]) / (y - x) for x, y in zip(b, b[1:]) if y - x > 1e-6]
        if ls:
            beat_len[bar] = float(np.median(ls))
    grid = []
    last_n = 4
    for i in range(len(bars) - 1):
        bar = i + 1
        dur = bars[i + 1] - bars[i]
        if beats_override:
            n = beats_override
        elif bar in beat_len and beat_len[bar] > 0:
            n = max(1, int(round(dur / beat_len[bar])))
        else:
            near = min(beat_len, key=lambda k: abs(k - bar)) if beat_len else None
            n = max(1, int(round(dur / beat_len[near]))) if near else last_n
        last_n = n
        for k in range(n):
            grid.append((bar, k + 1, bars[i] + k * dur / n))
    return grid, bars


def bar_start(bars, bar):
    return bars[min(bar - 1, len(bars) - 1)]


def plan_cuts(spans, beats, t0, t1, bar_beats, min_cut_bars=1, barlines=None):
    """cuts [(from, to)] of score time inside [t0, t1): spans = sounding [(start, end)] of the clip's notes; beats = the
    grid's beat times; bar_beats(t) = beats per bar at t. A gap: from the first beat after a note's end (or t0) to one
    bar before the next note's start (or t1), at least min_cut_bars bars long. With barlines (the default in make: whole
    bars, as the owner trimmed Whence's violins by hand, 2026-10-21: [11.209, 17.755) s of the clip, 3 bars at 110 bpm):
    the cut starts at the first bar line at or after that beat and ends at the last bar line that leaves at least a bar
    less one beat of clicks before the next note (a downbeat entry: one whole bar; a pickup on beat 4: its own bar)"""
    spans = sorted(s for s in spans if s[1] > t0 and s[0] < t1)
    bt = list(beats)
    bl = sorted(barlines) if barlines else None
    cuts = []
    cursor = t0                                   # sound (or the clip start) up to here
    edges = []
    for s, e in spans:
        if s > cursor + 1e-6:
            edges.append((cursor, s))
        cursor = max(cursor, e)
    edges.append((cursor, None))                  # after the last note: to the clip's end
    for i, (a, b) in enumerate(edges):
        start_of_clip = i == 0 and a <= t0 + 1e-6 and spans
        # cut start: the clip start itself, else the first beat after the note's end (whole bars: the bar line after)
        ca = t0 if start_of_clip else next((x for x in bt if x > a + 1e-6), None)
        if ca is None:
            continue
        if b is None:
            # the end: keep up to the first beat after the last end, drop the rest
            if t1 - ca > 1e-6:
                cuts.append((ca, t1))
            continue
        if bl and not start_of_clip:
            ca = next((x for x in bl if x >= ca - 1e-6), None)
            if ca is None:
                continue
        on_beat = max([x for x in bt if x <= b + 1e-6] or [t0])
        k = bt.index(on_beat) if on_beat in bt else 0
        n = bar_beats(on_beat)
        if bl:
            lim = bt[k - (n - 1)] if k - (n - 1) >= 0 else t0
            cb = max([x for x in bl if x <= lim + 1e-6] or [t0])
        else:
            cb = bt[k - n] if k - n >= 0 else t0
        if cb - ca < 1e-6:
            continue
        if bl:
            enough = sum(1 for x in bl if ca - 1e-6 <= x < cb - 1e-6) >= min_cut_bars
        else:
            enough = sum(1 for x in bt if ca - 1e-6 <= x < cb - 1e-6) >= min_cut_bars * bar_beats(ca)
        if enough:
            cuts.append((ca, cb))
    return cuts


def join(segments, sr):
    """segments of audio joined with XFADE_S crossfades centred on each join: (audio, [join sample])"""
    nx = int(round(XFADE_S * sr))
    h = nx // 2
    out = segments[0].copy()
    joins = []
    for seg in segments[1:]:
        j = len(out)
        fo = np.cos(np.linspace(0, np.pi / 2, nx))[:, None]
        fi = np.sin(np.linspace(0, np.pi / 2, nx))[:, None]
        # (out's last h samples and seg's first h: seg was cut h samples early / out h samples late, see make_clip)
        tail = out[j - nx:] * fo if j >= nx else out[:0]
        head = seg[:nx] * fi
        if len(tail) == nx and len(head) == nx:
            out[j - nx:] = tail + head
            out = np.concatenate([out, seg[nx:]])
        else:
            out = np.concatenate([out, seg])
        joins.append(j - h)
    return out, joins


def click(sr, f, amp):
    n = int(CLICK_S * sr)
    t = np.arange(n) / sr
    env = np.minimum(1, t / 0.0005) * np.exp(-t / 0.0025)
    return (amp * env * np.sin(2 * np.pi * f * t)).astype(np.float32)


# --------------------------------------------------------------------------------------------------------------- make

def note_kind(r):
    perf = r["patch"].endswith("Performance")
    if perf and r["transition"]:
        return "slurred transition" if r.get("slurredBefore") == "1" else "transition (touching, not slurred: fingered)"
    if perf:
        return "fresh attack"
    return "All techniques (%s)" % r["articulation"]


class Clip:
    pass


def make_clip(work, parts, bar_lo, bar_hi, a):
    """the score-time plan and the music of one clip: (Clip)"""
    grid, bars = beat_grid(work, a.beats)
    first = bar_start(bars, bar_lo)
    cnt_bar = bar_lo - a.count_in
    if cnt_bar >= 1:
        cnt = bar_start(bars, cnt_bar)
    else:
        L = bars[1] - bars[0] if len(bars) > 1 else 2.0
        cnt = first - a.count_in * L
    end = bar_start(bars, bar_hi + 1)
    beats = [g[2] for g in grid if cnt - 1e-6 <= g[2] < end - 1e-6]
    if cnt < bars[0] - 1e-6:                           # (a count-in before bar 1: its own beats)
        n = [g for g in grid if g[0] == 1]
        L = bars[1] - bars[0] if len(bars) > 1 else 2.0
        k = len(n) or 4
        beats = [cnt + i * L / k for i in range(int(round((first - cnt) / (L / k))))] + beats
    beats = sorted(set(round(b, 6) for b in beats))

    def bar_beats(t):
        g = [x for x in grid if x[2] <= t + 1e-6]
        bar = g[-1][0] if g else 1
        return sum(1 for x in grid if x[0] == bar) or 4
    rows = [r for r in work.rows if r["part"] in parts]
    spans = [(min(r["written"], r["written"] + r["sentMs"] / 1000), r["writtenEnd"]) for r in rows]
    barlines = None if getattr(a, "cut_beats", False) else [x for x in bars if first - 1e-6 <= x <= end + 1e-6]
    cuts = plan_cuts(spans, beats, first, end, bar_beats, a.min_cut_bars, barlines) if a.cut else []
    t0 = cnt - a.pre
    # the end: the last cut reaching `end` shortens the clip
    t1 = end
    if cuts and abs(cuts[-1][1] - end) < 1e-6:
        t1 = cuts[-1][0] + 0.3                        # (0.3 s of release after the kept beat)
        cuts = cuts[:-1]
    if cuts and abs(cuts[0][0] - first) < 1e-6:
        # the start: the music before the lead-in bar is cut, the count-in's clicks with it
        c0 = cuts.pop(0)
        cnt_new = c0[1]
        t0 = cnt_new - a.pre
        cnt = cnt_new
        beats = [b for b in beats if b >= cnt - 1e-6]
    c = Clip()
    c.work, c.parts, c.bar_lo, c.bar_hi = work, parts, bar_lo, bar_hi
    c.t0, c.t1, c.cnt, c.first, c.cuts = t0, t1, cnt, first, cuts
    c.beats = [b for b in beats if b < t1 - 1e-6 and not any(x <= b < y - 1e-6 for x, y in cuts)]
    c.grid = grid
    return c


def score_to_clip(c, t):
    """score second -> clip second (None inside a cut)"""
    rem = 0.0
    for x, y in c.cuts:
        if x - 1e-9 <= t < y - 1e-9:
            return None
        if t >= y - 1e-9:
            rem += y - x
    return t - c.t0 - rem


def render(c, mono=False):
    x, sr, nr = c.work.audio(c.parts, c.t0, c.t1)
    h = int(round(XFADE_S * sr / 2))
    segs, s = [], c.t0
    for a, b in c.cuts:
        i0, i1 = int(round((s - c.t0) * sr)), int(round((a - c.t0) * sr))
        segs.append(x[max(0, i0 - (h if segs else 0)):i1 + h])
        s = b
    segs.append(x[max(0, int(round((s - c.t0) * sr)) - (h if segs else 0)):])
    y, joins = join(segs, sr)
    nf = int(0.02 * sr)
    y[:nf] *= np.linspace(0, 1, nf)[:, None]
    nl = min(len(y), int(0.3 * sr))
    y[-nl:] *= np.linspace(1, 0, nl)[:, None]
    return y, sr, nr


def notes_of(c, variant_work):
    out = []
    for r in variant_work.rows:
        if r["part"] not in c.parts or not (c.bar_lo <= r["bar"] <= c.bar_hi):
            continue
        w = score_to_clip(c, r["written"])
        if w is None:
            continue
        rep = variant_work.report.get((r["part"], round(r["written"], 3), r["pitch"]), {})
        h = rep.get("heardMs")
        src = None
        if r["transition"]:
            src = next((q for q in variant_work.rows if q["part"] == r["part"] and abs(q["written"] - r["fromWritten"]) < 1e-4
                        and q["pitch"] == r["fromPitch"]), None)
        out.append(dict(part=r["part"], bar=r["bar"], beat=round(r["beat"], 4), pitch=r["pitch"], cents=r["cents"],
                        patch=r["patch"], articulation=r["articulation"], kind=note_kind(r), transition=bool(r["transition"]),
                        situation=rep.get("situation"), slur=r["slur"], route=r["route"],
                        fromPitch=r["fromPitch"] if r["transition"] else None,
                        fromCents=(src["cents"] if src else 0.0) if r["transition"] else None,
                        scoreS=round(r["written"], 4), writtenS=round(w, 4),
                        onS=round(w + r["sentMs"] / 1000, 4), sentMs=round(r["sentMs"], 1),
                        predictedMs=rep.get("predictedMs"), heardMs=h, heardS=None if h is None else round(w + h / 1000, 4),
                        leaveMs=rep.get("leaveMs"), arriveMs=rep.get("arriveMs", h if r["transition"] else None),
                        how=rep.get("how")))
    out.sort(key=lambda n: (n["writtenS"], n["pitch"]))
    return out


def split_ranges(work, parts, bar_lo, bar_hi, a):
    """[(lo, hi)] bar ranges whose clips (after the cuts) are at most --max-len s, about equally long"""
    def length(lo, hi):
        c = make_clip(work, parts, lo, hi, a)
        return c.t1 - c.t0 - sum(y - x for x, y in c.cuts)
    if not a.max_len:
        return [(bar_lo, bar_hi)]
    total = length(bar_lo, bar_hi)
    if total <= a.max_len:
        return [(bar_lo, bar_hi)]
    k = int(math.ceil(total / a.max_len))
    target = total / k
    out, lo = [], bar_lo
    while lo <= bar_hi:
        hi = lo
        while hi < bar_hi:
            L = length(lo, hi + 1)
            if L > a.max_len or (L > target * 1.1 and len(out) < k - 1):
                break
            hi += 1
        out.append((lo, hi))
        lo = hi + 1
    return out


def safe(s):
    return "".join(ch.lower() if ch.isalnum() else "" for ch in s) or "x"


def encode(path_wav, mp3):
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", path_wav, "-codec:a", "libmp3lame", "-b:a", "320k", mp3], check=True)


def cmd_make(a):
    import soundfile as sf
    variants = []
    for v in a.variant:
        if "=" not in v:
            raise SystemExit("--variant LABEL=<hc work>")
        lab, p = v.split("=", 1)
        variants.append((lab, Work(p)))
    over = {}
    for o in a.override:
        part, _, rest = o.partition(":")
        lab, _, p = rest.partition("=")
        over[(part, lab)] = Work(p)
    base = variants[0][1]
    lo, hi = (int(x) for x in a.bars.split("-"))
    parts = [p.strip() for p in a.parts.split(",")] if a.parts else base.parts()
    groups = [parts] if a.mix else [[p] for p in parts]
    os.makedirs(a.out, exist_ok=True)
    meta = {}
    pending = []                                  # (name, audio, sr, group key, meta)
    for g in groups:
        gname = "mix" if a.mix else safe(g[0])
        for k, (plo, phi) in enumerate(split_ranges(base, g, lo, hi, a)):
            suffix = "" if (plo, phi) == (lo, hi) else "_p%d" % (k + 1)
            for lab, w in variants:
                ws = [over.get((p, lab), w) for p in g]
                wk = ws[0]
                c = make_clip(base, g, plo, phi, a)          # (one plan for every variant: same cuts, same grid)
                c.work = wk
                y, sr, nr = render(c)
                name = "%s_%s_%s%s" % (a.name, gname, lab, suffix)
                m = dict(score=a.name, parts=g, variant=lab, source=os.path.basename(os.path.abspath(wk.path)),
                         bars="%d-%d" % (plo, phi), clipStartScoreS=round(c.t0, 4), scoreFrom=round(c.first, 4),
                         scoreTo=round(c.t1, 4), countInAtClipS=round(score_to_clip(c, c.cnt), 4),
                         firstBarAtClipS=round(score_to_clip(c, c.first), 4) if score_to_clip(c, c.first) is not None else None,
                         cuts=[dict(clipS=round(score_to_clip(c, y_), 4), scoreFrom=round(x, 4), scoreTo=round(y_, 4),
                                    removedS=round(y_ - x, 4)) for x, y_ in c.cuts],
                         lengthS=round(len(y) / sr, 3), routesRendered=nr,
                         beats=[], notes=notes_of(c, wk))
                gb = {round(t, 6): (bar, k) for bar, k, t in c.grid}
                for t in c.beats:
                    bar, k = gb.get(round(t, 6), (0, 0))
                    m["beats"].append(dict(bar=bar if bar >= plo else "count-in", beat=k, clipS=round(score_to_clip(c, t), 4)))
                pending.append((name, y, sr, (gname, suffix), m))
    # one gain per comparison
    peaks = {}
    for name, y, sr, key, m in pending:
        k = {"part": key, "all": "all", "none": name}[a.shared_gain]
        peaks[k] = max(peaks.get(k, 0.0), float(np.abs(y).max()))
    for name, y, sr, key, m in pending:
        k = {"part": key, "all": "all", "none": name}[a.shared_gain]
        pk = peaks[k] or 1.0
        z = y.copy()
        for b in m["beats"]:
            acc = b["beat"] == 1
            cl = click(sr, 1500 if acc else 1000, pk * 10 ** ((a.accent_db if acc else a.click_db) / 20))
            i = int(round(b["clipS"] * sr))
            j = min(len(z), i + len(cl))
            if 0 <= i < len(z):
                z[i:j] += cl[:j - i, None]
        peaks.setdefault(("mixed", k), 0.0)
        m["_z"] = z
        peaks[("mixed", k)] = max(peaks[("mixed", k)], float(np.abs(z).max()))
    for name, y, sr, key, m in pending:
        k = {"part": key, "all": "all", "none": name}[a.shared_gain]
        g = 10 ** (-1.0 / 20) / (peaks[("mixed", k)] or 1.0)
        z = m.pop("_z") * g
        m.update(gainDb=round(20 * math.log10(g), 2), peakDbfs=round(20 * math.log10(max(float(np.abs(z).max()), 1e-12)), 2),
                 clickPeakDb=a.click_db, accentPeakDb=a.accent_db, sharedGain=a.shared_gain)
        wav = os.path.join(a.out, name + ".wav")
        sf.write(wav, z, sr, subtype="FLOAT" if a.wav else "PCM_24")
        if not a.no_mp3:
            encode(wav, os.path.join(a.out, name + ".mp3"))
            m["file"] = name + ".mp3"
        if a.wav or a.no_mp3:
            m["wav"] = name + ".wav"
        else:
            os.remove(wav)
        meta[name] = m
    # (the largest A/B differences per part, notes heard in both)
    labs = [v[0] for v in variants]
    if len(labs) >= 2:
        for name, m in meta.items():
            if m["variant"] != labs[0]:
                continue
            other = meta.get(name.replace("_%s" % labs[0], "_%s" % labs[1], 1))
            if not other:
                continue
            key = lambda n: (n["part"], n["bar"], n["beat"], n["pitch"])
            bn = {key(n): n for n in other["notes"] if n["heardMs"] is not None}
            d = [dict(part=n["part"], bar=n["bar"], beat=n["beat"], pitch=n["pitch"], clipS=n["writtenS"], kind=n["kind"],
                      A_ms=n["heardMs"], B_ms=bn[key(n)]["heardMs"])
                 for n in m["notes"] if n["heardMs"] is not None and key(n) in bn]
            d.sort(key=lambda r: -abs(r["B_ms"] - r["A_ms"]))
            m["largestDifferences"] = other["largestDifferences"] = d[:8]
    about = __doc__.split("\n\n")[-1] if False else ("clips.py make; see its --help for every field. Variants: " +
                                                    ", ".join("%s = %s" % (lab, os.path.basename(os.path.abspath(w.path))) for lab, w in variants))
    with open(os.path.join(a.out, "clips.json"), "w", encoding="utf-8") as f:
        json.dump(dict(_about=about, **meta), f, indent=1)
    write_page(a.out, meta, labs)
    for name, m in meta.items():
        print("%-36s %6.1f s  cuts %d  gain %+.1f dB  notes %d  routes %d" % (name, m["lengthS"], len(m["cuts"]), m["gainDb"],
                                                                             len(m["notes"]), m["routesRendered"]))
    return 0


def write_page(out, meta, labs):
    """a review page skeleton (HTML, theme tokens, audio players) for an artifact"""
    rows = []
    for name, m in meta.items():
        cut = "; ".join("%.2f s: score %.2f-%.2f removed" % (c["clipS"], c["scoreFrom"], c["scoreTo"]) for c in m["cuts"]) or "none"
        src = m.get("file") or m.get("wav")
        rows.append("<section><h3>%s</h3><p>%s, bars %s, variant %s (%s); %.1f s; cuts: %s</p>"
                    "<audio controls preload=\"none\" src=\"%s\"></audio></section>" % (
                        name, ", ".join(m["parts"]), m["bars"], m["variant"], m["source"], m["lengthS"], cut, src))
    html = """<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Section Click Clips</title><style>
:root{--bg:#fafaf8;--fg:#1d1d1b;--muted:#6b6b66;--line:#ddd}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#1b1b1a;--fg:#ecebe6;--muted:#a3a29b;--line:#3a3a37}}
:root[data-theme="dark"]{--bg:#1b1b1a;--fg:#ecebe6;--muted:#a3a29b;--line:#3a3a37}
body{background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,sans-serif;margin:0 auto;max-width:860px;padding:16px}
section{border-top:1px solid var(--line);padding:8px 0}p{color:var(--muted)}audio{width:100%%}
</style></head><body><h1>Section click clips</h1>
<h2>What to check</h2><ul><li>Each section alone with a click on every written beat (accent on beat 1).</li>
<li>Compare %s at one gain: which places the notes on the beat?</li><li>Long rests are cut (crossfades); a bar of clicks leads back in.</li></ul>
%s</body></html>""" % (" / ".join(labs), "\n".join(rows))
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(html)


# ------------------------------------------------------------------------------------------------------------ measure

def align_clicks(x, sr, beats, f_acc=1500.0, f=1000.0, search_s=0.06):
    """the clip's delay (s) against its JSON (an MP3 decoder's), from the clicks: the median lag of each click's matched
    filter peak; and each click's fitted amplitude (to subtract it)"""
    lags, fits = [], []
    for b in beats:
        tpl = click(sr, f_acc if b["beat"] == 1 else f, 1.0)
        i = int(round(b["clipS"] * sr))
        lo, hi = max(0, i - int(search_s * sr)), min(len(x), i + int(search_s * sr) + len(tpl))
        seg = x[lo:hi]
        if len(seg) < len(tpl) + 2:
            continue
        cc = np.correlate(seg, tpl, mode="valid")
        k = int(np.argmax(np.abs(cc)))
        lags.append((lo + k - i) / sr)
        fits.append((lo + k, float(cc[k] / np.dot(tpl, tpl)), b["beat"] == 1))
    return (float(np.median(lags)) if lags else 0.0), fits


def normalise(n, m=None):
    """a note of clips.json, or of soloclips.py's solo_clicks.json (heardMinusWrittenMs, kind text only)"""
    n = dict(n)
    if "part" not in n and m is not None:
        n["part"] = m.get("section") or (m.get("parts") or ["?"])[0]
    if "transition" not in n:
        n["transition"] = "transition" in (n.get("kind") or "")
    if "heardMs" not in n:
        n["heardMs"] = n.get("heardMinusWrittenMs")
    if n["transition"] and "arriveMs" not in n:
        n["arriveMs"] = n["heardMs"]
    return n


def cmd_measure(a):
    import soundfile as sf
    import heard_check as H
    with open(os.path.join(a.dir, "clips.json"), encoding="utf-8") as f:
        meta = json.load(f)
    dump_rows = {}
    if a.dump:
        for d in a.dump:
            lab, _, p = d.rpartition("=")
            w = Work(p)
            for r in w.rows:
                dump_rows.setdefault(lab, {})[(r["part"], r["bar"], round(r["beat"], 3), r["pitch"])] = r
    out = {}
    names = a.clip or [k for k in meta if not k.startswith("_")]
    for name in names:
        m = meta[name]
        f = os.path.join(a.dir, m.get("file") or m.get("wav") or name + ".mp3")
        x, sr = sf.read(f, dtype="float64", always_2d=True)
        mono = x.mean(axis=1)
        delay, fits = align_clicks(mono, sr, m["beats"])
        for pos, amp, acc in fits:                    # the clicks out
            tpl = click(sr, 1500 if acc else 1000, amp)
            mono[pos:pos + len(tpl)] -= tpl[:len(mono) - pos]
        notes = [normalise(n, m) for n in m["notes"]]
        lab = m.get("source")
        import re
        mm = re.match(r"heard check (\S+)", lab or "")      # (soloclips.py's clips: "heard check w0 (...)")
        if mm:
            lab = mm.group(1)
        for n in notes:
            if n.get("fromPitch") is None and n["transition"]:
                r = dump_rows.get(lab, dump_rows.get("", {})).get((n["part"], n["bar"], round(n["beat"], 3), n["pitch"]))
                if r is None and len(dump_rows) == 1:
                    r = list(dump_rows.values())[0].get((n["part"], n["bar"], round(n["beat"], 3), n["pitch"]))
                if r is not None:
                    n["fromPitch"] = r["fromPitch"]
                    n["onS"] = n.get("onS", n["writtenS"] + n["sentMs"] / 1000)
        tr = [n for n in notes if n["transition"] and n.get("fromPitch") is not None]
        res = []
        if tr:
            ps = [n["pitch"] + (n.get("cents") or 0) / 100 for n in notes] + [n["fromPitch"] for n in tr]
            f0, fh = H.track(mono.astype(np.float32), sr, min(ps) - 3, max(ps) + 3)
            ons = sorted({n.get("onS", n["writtenS"] + n["sentMs"] / 1000) for n in notes})
            for n in tr:
                on = n.get("onS", n["writtenS"] + n["sentMs"] / 1000) + delay
                # (a chord at its note-on or a note sounding over it from another line: f0 can't follow)
                chord = sum(1 for q in notes if abs(q["writtenS"] - n["writtenS"]) < 1e-3) > 1
                nxt = [t + delay for t in ons if t + delay > on + 0.0005]
                prev = [q for q in notes if q["writtenS"] < n["writtenS"] - 1e-3 and q.get("heardS") is not None]
                after = prev[-1]["heardS"] + delay if prev else None
                fp = n["fromPitch"] + (n.get("fromCents") or 0) / 100
                tp = n["pitch"] + (n.get("cents") or 0) / 100
                t, lv, why = H.transition_times(f0, fh, on, fp, tp, after, nxt[0] if nxt else None)
                base = n["writtenS"] + delay
                res.append(dict(part=n["part"], bar=n["bar"], beat=n["beat"], pitch=n["pitch"], kind=n["kind"], chord=chord,
                                clipArriveMs=None if t is None else round((t - base) * 1000, 1),
                                clipLeaveMs=None if lv is None else round((lv - base) * 1000, 1),
                                reportArriveMs=n.get("arriveMs"), reportLeaveMs=n.get("leaveMs"), why=why))
        out[name] = dict(delayMs=round(delay * 1000, 1), clicks=len(fits), transitions=res)
        a_ = [r for r in res if r["clipArriveMs"] is not None and r["reportArriveMs"] is not None]
        agree = float(np.median([abs(r["clipArriveMs"] - r["reportArriveMs"]) for r in a_])) if a_ else None
        out[name]["arrivalAgreementMs"] = None if agree is None else round(agree, 1)
        print("%-36s delay %+5.1f ms, %d transitions, %d leaves, %d arrivals (clip vs report median |d| %s ms)" % (
            name, delay * 1000, len(res), sum(1 for r in res if r["clipLeaveMs"] is not None),
            sum(1 for r in res if r["clipArriveMs"] is not None), "-" if agree is None else "%.1f" % agree))
    with open(os.path.join(a.dir, "measured.json"), "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1)
    return 0


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0], epilog=__doc__.split("\n", 2)[2],
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    m = sub.add_parser("make")
    m.add_argument("--variant", action="append", required=True, help="LABEL=<heard-check work dir> (repeatable)")
    m.add_argument("--override", action="append", default=[], help='"<part>:<LABEL>=<work dir>": that part from another run')
    m.add_argument("--bars", required=True, help="a-b")
    m.add_argument("--parts")
    m.add_argument("--mix", action="store_true", help="the parts together in one clip")
    m.add_argument("--name", default="clip")
    m.add_argument("--count-in", type=int, default=1)
    m.add_argument("--pre", type=float, default=0.3)
    m.add_argument("--beats", type=int, help="beats per bar for every bar (default: from the notes)")
    m.add_argument("--no-cut", dest="cut", action="store_false", help="keep the long rests")
    m.add_argument("--min-cut-bars", type=float, default=1.0)
    m.add_argument("--cut-beats", action="store_true", help="cut on beats, not whole bars")
    m.add_argument("--max-len", type=float, default=20.0, help="split clips longer than this (s; 0: never)")
    m.add_argument("--shared-gain", choices=("part", "all", "none"), default="part")
    m.add_argument("--click-db", type=float, default=-12.0)
    m.add_argument("--accent-db", type=float, default=-8.0)
    m.add_argument("--wav", action="store_true", help="keep the WAVs (float)")
    m.add_argument("--no-mp3", action="store_true")
    m.add_argument("-o", "--out", required=True)
    me = sub.add_parser("measure")
    me.add_argument("dir")
    me.add_argument("--clip", action="append")
    me.add_argument("--dump", action="append", help="[source=]<hc work dir>: fromPitch for clips.json files without it")
    a = ap.parse_args(argv)
    return cmd_make(a) if a.cmd == "make" else cmd_measure(a)


if __name__ == "__main__":
    sys.exit(main())
