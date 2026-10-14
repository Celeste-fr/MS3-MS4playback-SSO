#!/usr/bin/env python3
"""Stage B, option 2 (the owner, 2026-10-14: "test option 2"): a dynamic tilt EQ on top of SSO's CC1, so that the
strings' tone changes from ppp to fff as a real player's does, as a blind listening test. Test only: MuseScore does
not read anything here.

Why bands, not sharpness: DIN 45692 sharpness barely moves for SSO's upper strings over CC1 16 -> 127 (0.1-0.4 %,
sso_brightness.json), yet the owner heard CC1 127 brighter in 10 of 10 blind upper-string clips (listening page db
`stageb1`, 2026-10-14), where the amplitude spectral centroid moved 20-80 %. So the working measure here is the level
per octave band relative to the note's total level (its tone, loudness removed), compared as a CHANGE (ff - pp, CC1 127
- 16): absolute values differ by microphones and room (TU Berlin: one player, anechoic; SSO: a section in a hall).

    tilt_eq.py real-bands <instrument> <out.jsonl> --pitches P ...
        TU Berlin's pp / ff notes (real_brightness.py's source, read member by member over HTTP), per note: octave-band
        power of the steady part (all 32 microphones, power average) and of the quietest 0.25 s outside it (the
        chamber's noise), dB re 20 uPa
    tilt_eq.py sso-events <out.txt> <out.notes.json> --pitches P ...
        a kthost score: each pitch at CC1 16 and 127 (velocity = CC1, as MuseScore sends a Long), 2 repeats (SSO
        alternates two round robins), 3 s notes, 1.5 s apart (sso_cc1_sweep.make_events)
    tilt_eq.py sso-bands <render.wav> <notes.json> -o <out.json>
        per note: octave-band power of 1.5-3.0 s after the note-on (sso_cc1_sweep's sustain window), left / right
        power-averaged
    tilt_eq.py fit <real.jsonl> <sso.json> --section NAME [--ff-only-clean] -o <part.json>
        per pitch, bands used: those whose centre is at least the note's f0 / sqrt(2) (below it a string sounds
        nothing) where the real pp's band is >= CLEAN_DB above the chamber noise (noise then adds < 0.43 dB) and the
        ff's too; each spectrum normalised to its total over those bands (tone only); D_p(b) = real (ff - pp) - SSO
        (127 - 16). D(b) = median over pitches (bands with >= 3 pitches). Fit, least squares: TILT, D(b) = c + s *
        log2(f_b / 1 kHz) (dB per octave through all bands), and PIVOT, D(b) = c + s * max(0, log2(f_b / f_p)) (flat
        below a pivot f_p, a band centre, s dB per octave above): the one with the smaller residual rms is kept
        (both have one more free parameter than the data's level offset, which the loudness compensation removes).
        Outside the measured bands the curve holds the nearest measured band's value: TU Berlin's pp is at or under the
        anechoic chamber's noise above ~2.8 kHz (violin, viola) and ~0.7-1.4 kHz (double bass), so nothing is known
        there and nothing is extrapolated
    tilt_eq.py score <out.mscx>
        the test phrase (public domain, written here): Violins 1, Violas, Violoncellos, Contrabasses, q = 160, a scale
        up an octave and down again, one whole note (1.5 s) per written dynamic ppp pp p mp mf f ff fff ff f mf mp p
        pp ppp, no slurs (MuseScore plays them on All techniques Long); rendered by tst_liveequivalence dumpEvents
        (Recommended: the defaults) to the events MuseScore sends, replayed on the VM by kthost
    tilt_eq.py apply <render.wav> <route events.txt> <fit.json> --factor F -o <out.wav>
        the EQ at each marking k (0 = ppp ... 7 = fff): the fitted curve times F * (k - 3) / 7 (rule D's equal steps,
        mp = no EQ; F = 0 none, 0.5 half, 1 full), realised as a cascade of RBJ shelving biquads (minimum phase) at
        the octave-band edges whose gains follow the curve's step from band to band; each marking's filtered version
        is level-compensated (K-weighted power of the marking's notes equal to the unfiltered render's: the EQ changes
        tone, realRange keeps the loudness), then the versions are crossfaded at the marking changes (the note-ons,
        100 ms raised cosine)
    tilt_eq.py measure <clip.wav> <route events.txt> -o <out.json>
        per marking ppp / mp / fff (the up-going notes): octave-band levels relative to the note's total and the
        amplitude spectral centroid (brightness.centroids) of each note's 0.3-1.3 s
"""
import argparse
import json
import math
import os
import statistics
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

CENTRES = [125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0]
CLEAN_DB = 10.0     # a band counts when the pp is this far above the chamber's noise: noise adds < 10*log10(1.1) dB
SR = 44100
DYN = ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff']
VELO = {'ppp': 16, 'pp': 33, 'p': 49, 'mp': 64, 'mf': 80, 'f': 96, 'ff': 112, 'fff': 126}
ARCH = [0, 1, 2, 3, 4, 5, 6, 7, 6, 5, 4, 3, 2, 1, 0]          # the phrase's markings, as indices into DYN


def band_powers(f, P):
    out = []
    for c in CENTRES:
        sel = (f >= c / math.sqrt(2)) & (f < c * math.sqrt(2))
        out.append(float(P[sel].sum()))
    return out


def db(p):
    return 10 * math.log10(p) if p > 0 else -200.0


# ---- real players ----------------------------------------------------------------------------------------------

def real_bands(instrument, out, pitches):
    import zipfile
    import brightness
    import httpzip
    import real_brightness as rb
    done = set()
    if os.path.exists(out):
        with open(out) as fh:
            done = {json.loads(line)["file"] for line in fh if line.strip()}
    z = zipfile.ZipFile(httpzip.HttpFile(rb.API % rb.ZIPS[instrument]))
    names = [i.filename for i in z.infolist() if i.filename.endswith("_recordings.sofa")]
    rows = {}
    jl = f"/tmp/sb/real/{instrument}.jsonl"          # real_brightness.py's index of the same files (midi per file)
    if os.path.exists(jl):
        with open(jl) as fh:
            for line in fh:
                r = json.loads(line)
                rows[r["file"]] = r["midi"]
    for n in sorted(names):
        base = n.rsplit("/", 1)[-1]
        if base in done:
            continue
        if rows and rows.get(base) not in pitches:
            continue
        x, steady, midi, tuning, desc = rb.signals(z.read(n))
        if midi not in pitches:
            continue
        dyn = "ff" if "dynamic = ff" in desc else "pp" if "dynamic = pp" in desc else desc
        a, b = steady
        f, P = brightness.power_spectrum(x[a:b], SR)
        noise = rb.quietest(x, SR, steady)
        fn, Pn = brightness.power_spectrum(noise, SR, nfft=min(brightness.NFFT, len(noise)))
        if len(fn) != len(f):
            Pn = np.interp(f, fn, Pn) * ((f[1] - f[0]) / (fn[1] - fn[0]))
        row = {"file": base, "instrument": instrument, "dynamic": dyn, "midi": midi, "tuning": tuning,
               "f0": round(tuning * 2 ** ((midi - 69) / 12), 3), "centres": CENTRES,
               "sigDb": [round(db(p / brightness.P0 ** 2), 3) for p in band_powers(f, P)],
               "noiseDb": [round(db(p / brightness.P0 ** 2), 3) for p in band_powers(f, Pn)]}
        with open(out, "a") as fh:
            fh.write(json.dumps(row) + "\n")
        print(base, dyn, midi, row["sigDb"], flush=True)


# ---- SSO -------------------------------------------------------------------------------------------------------

def sso_events(out, notes_out, pitches):
    import sso_cc1_sweep as sw
    text, notes = sw.make_events(min(pitches), max(pitches), step=999, extra=pitches, ccs=[16, 127], repeats=2,
                                 note=3.0, gap=1.5)
    open(out, "w").write(text)
    json.dump(notes, open(notes_out, "w"))
    print(len(notes["notes"]), "notes,", notes["notes"][-1]["off"] + 2, "s")


def read_wav(path):
    import soundfile as sf
    x, sr = sf.read(path, dtype="float64", always_2d=True)
    assert sr == SR, sr
    return x


def sso_bands(wav, notes_path, out):
    import brightness
    x = read_wav(wav)
    notes = json.load(open(notes_path))
    rows = []
    for n in notes["notes"]:
        a, b = int((n["on"] + 1.5) * SR), int((n["on"] + 3.0) * SR)
        f, P = brightness.power_spectrum(x[a:b], SR)
        rows.append({"pitch": n["pitch"], "cc": n["cc"], "repeat": n["repeat"],
                     "bandDb": [round(db(p), 3) for p in band_powers(f, P)]})
    json.dump({"centres": CENTRES, "notes": rows}, open(out, "w"), indent=1)
    print(len(rows), "notes")


# ---- the fit ---------------------------------------------------------------------------------------------------

def tone(levels_db, use):
    """band levels minus the total over the bands in use (dB): the spectrum's shape, its loudness removed"""
    tot = db(sum(10 ** (levels_db[i] / 10) for i in use))
    return [levels_db[i] - tot if i in use else None for i in range(len(levels_db))]


def power_mean_db(vals):
    return db(sum(10 ** (v / 10) for v in vals) / len(vals))


def fit(real_jsonl, sso_json, section, out):
    real = [json.loads(line) for line in open(real_jsonl) if line.strip()]
    sso = json.load(open(sso_json))
    by = {}
    for r in real:
        by.setdefault(r["midi"], {})[r["dynamic"]] = r
    sso_by = {}
    for n in sso["notes"]:
        sso_by.setdefault((n["pitch"], n["cc"]), []).append(n["bandDb"])
    per_pitch = {}
    for midi, d in sorted(by.items()):
        if "pp" not in d or "ff" not in d or (midi, 16) not in sso_by or (midi, 127) not in sso_by:
            continue
        f0 = d["pp"]["f0"]
        use = [i for i, c in enumerate(CENTRES)
               if c >= f0 / math.sqrt(2)
               and d["pp"]["sigDb"][i] - d["pp"]["noiseDb"][i] >= CLEAN_DB
               and d["ff"]["sigDb"][i] - d["ff"]["noiseDb"][i] >= CLEAN_DB]
        if len(use) < 3:
            continue
        # the signal's own power: the noise's mean power taken off (it is >= CLEAN_DB under in every band used)
        def clean(r):
            return [db(10 ** (s / 10) - 10 ** (n / 10)) for s, n in zip(r["sigDb"], r["noiseDb"])]
        rp, rf = tone(clean(d["pp"]), use), tone(clean(d["ff"]), use)
        s16 = [power_mean_db([v[i] for v in sso_by[(midi, 16)]]) for i in range(len(CENTRES))]
        s127 = [power_mean_db([v[i] for v in sso_by[(midi, 127)]]) for i in range(len(CENTRES))]
        sp, sf_ = tone(s16, use), tone(s127, use)
        per_pitch[midi] = {"bands": use,
                           "real": [round(rf[i] - rp[i], 2) if i in use else None for i in range(len(CENTRES))],
                           "sso": [round(sf_[i] - sp[i], 2) if i in use else None for i in range(len(CENTRES))]}
    med = {}
    for key in ("real", "sso"):
        med[key] = []
        for i in range(len(CENTRES)):
            v = [p[key][i] for p in per_pitch.values() if p[key][i] is not None]
            med[key].append(round(statistics.median(v), 2) if len(v) >= 3 else None)
    dvals = []
    for i in range(len(CENTRES)):
        v = [p["real"][i] - p["sso"][i] for p in per_pitch.values() if p["real"][i] is not None]
        dvals.append(round(statistics.median(v), 2) if len(v) >= 3 else None)
    npitch = [sum(1 for p in per_pitch.values() if p["real"][i] is not None) for i in range(len(CENTRES))]
    idx = [i for i, v in enumerate(dvals) if v is not None]
    xs = np.array([math.log2(CENTRES[i] / 1000.0) for i in idx])
    ys = np.array([dvals[i] for i in idx])
    models = {}
    A = np.vstack([np.ones_like(xs), xs]).T
    coef, *_ = np.linalg.lstsq(A, ys, rcond=None)
    res = ys - A @ coef
    models["tilt"] = {"c": float(coef[0]), "slope": float(coef[1]), "pivot": None,
                      "residualRmsDb": float(math.sqrt((res ** 2).mean()))}
    best = None
    for j in idx[:-1]:
        fp = CENTRES[j]
        xp = np.maximum(0.0, np.array([math.log2(CENTRES[i] / fp) for i in idx]))
        A2 = np.vstack([np.ones_like(xp), xp]).T
        c2, *_ = np.linalg.lstsq(A2, ys, rcond=None)
        r2 = ys - A2 @ c2
        rms = float(math.sqrt((r2 ** 2).mean()))
        if best is None or rms < best["residualRmsDb"]:
            best = {"c": float(c2[0]), "slope": float(c2[1]), "pivot": fp, "residualRmsDb": rms}
    models["pivot"] = best
    keep = min(models, key=lambda k: models[k]["residualRmsDb"])
    m = models[keep]
    # the curve over the measured bands only; outside them it holds the nearest measured band's value: the real pp is
    # under the chamber's noise there (no measurement), so no change is put in (nothing extrapolated)
    curve = []
    lo_i, hi_i = idx[0], idx[-1]
    for i, c in enumerate(CENTRES):
        cc = CENTRES[min(max(i, lo_i), hi_i)]
        x = math.log2(cc / 1000.0) if keep == "tilt" else max(0.0, math.log2(cc / m["pivot"]))
        curve.append(round(m["c"] + m["slope"] * x, 3))
    result = {"section": section, "centres": CENTRES, "cleanDb": CLEAN_DB, "pitches": sorted(per_pitch),
              "perPitch": {str(k): v for k, v in per_pitch.items()}, "realMedian": med["real"],
              "ssoMedian": med["sso"], "difference": dvals, "pitchesPerBand": npitch,
              "models": {k: {kk: (round(vv, 3) if isinstance(vv, float) else vv) for kk, vv in v.items()} for k, v in models.items()},
              "kept": keep, "curve": curve,
              "curveLevelFree": [round(v - curve[3], 3) for v in curve]}   # (relative to 1 kHz: what is heard)
    json.dump(result, open(out, "w"), indent=1)
    print(section, "real", med["real"], "sso", med["sso"], "D", dvals, "n", npitch)
    print("  ", keep, result["models"][keep], "curve", curve)


# ---- the test phrase -------------------------------------------------------------------------------------------

PHRASES = [('Violins 1', 'Vlns. 1', 'violins', 'G', [67, 69, 71, 72, 74, 76, 78, 79]),
           ('Violas', 'Vlas.', 'violas', 'C', [60, 62, 64, 65, 67, 69, 71, 72]),
           ('Violoncellos', 'Vcs.', 'violoncellos', 'F', [50, 52, 54, 55, 57, 59, 61, 62]),
           ('Contrabasses', 'Cbs.', 'contrabasses', 'F', [38, 40, 42, 43, 45, 47, 49, 50])]
TPC = {38: 16, 40: 18, 42: 20, 43: 15, 45: 17, 47: 19, 49: 21, 50: 16, 52: 18, 54: 20, 55: 15, 57: 17, 59: 19,
       60: 14, 61: 21, 62: 16, 64: 18, 65: 13, 67: 15, 69: 17, 71: 19, 72: 14, 74: 16, 76: 18, 78: 20, 79: 15}


def score(out):
    from xml.sax.saxutils import escape
    CLEF = {'G': 'G', 'C': 'C3', 'F': 'F'}
    seq_idx = list(range(8)) + list(range(6, -1, -1))        # up the scale, then down: 15 notes
    staves, parts = [], []
    for si, (name, short, iid, clef, scale) in enumerate(PHRASES):
        parts.append(f'<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType>'
                     f'<defaultClef>{CLEF[clef]}</defaultClef></Staff><trackName>{escape(name)}</trackName>'
                     f'<Instrument id="{iid}"><longName>{escape(name)}</longName><shortName>{escape(short)}</shortName>'
                     f'<trackName>{escape(name)}</trackName><instrumentId>strings.group</instrumentId>'
                     f'<Channel name="arco"><program value="48"/><synti>Fluid</synti></Channel></Instrument></Part>')
        o = [f'<Staff id="{si + 1}">']
        first = (f'<Clef><concertClefType>{CLEF[clef]}</concertClefType><transposingClefType>{CLEF[clef]}</transposingClefType></Clef>'
                 '<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>')
        if si == 0:
            first += '<Tempo><tempo>2.66667</tempo><followText>1</followText><text>q = 160</text></Tempo>'
        o.append(f'<Measure><voice>{first}<Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>')
        for k, (mi, pi) in enumerate(zip(ARCH, seq_idx)):
            d = DYN[mi]
            p = scale[pi]
            o.append(f'<Measure><voice><Dynamic><subtype>{d}</subtype><velocity>{VELO[d]}</velocity></Dynamic>'
                     f'<Chord><durationType>whole</durationType><Note><pitch>{p}</pitch><tpc>{TPC[p]}</tpc></Note></Chord></voice></Measure>')
        o.append('<Measure><voice><Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>')
        o.append('</Staff>')
        staves.append('\n'.join(o))
    text = (f'<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02"><programVersion>3.6.2</programVersion>'
            f'<Score><Division>480</Division><metaTag name="workTitle">Tone test phrase</metaTag>\n'
            + '\n'.join(parts) + '\n' + '\n'.join(staves) + '\n</Score></museScore>\n')
    open(out, 'w').write(text)
    print(out)


# ---- the EQ ----------------------------------------------------------------------------------------------------

def shelf(fc, gain_db, sr=SR):
    """RBJ cookbook high shelf, S = 1 (minimum phase): b, a"""
    A = 10 ** (gain_db / 40)
    w0 = 2 * math.pi * fc / sr
    alpha = math.sin(w0) / 2 * math.sqrt(2)
    cw = math.cos(w0)
    b0 = A * ((A + 1) + (A - 1) * cw + 2 * math.sqrt(A) * alpha)
    b1 = -2 * A * ((A - 1) + (A + 1) * cw)
    b2 = A * ((A + 1) + (A - 1) * cw - 2 * math.sqrt(A) * alpha)
    a0 = (A + 1) - (A - 1) * cw + 2 * math.sqrt(A) * alpha
    a1 = 2 * ((A - 1) - (A + 1) * cw)
    a2 = (A + 1) - (A - 1) * cw - 2 * math.sqrt(A) * alpha
    return np.array([b0, b1, b2]) / a0, np.array([1.0, a1 / a0, a2 / a0])


def eq_filter(x, curve, scale):
    """the curve (dB per octave band, level-free) times scale, as a cascade of high shelves at the band edges, each
    stepping by the curve's change from the band below; the lowest band's level is the reference (0 dB)"""
    from scipy.signal import lfilter
    y = x.copy()
    for i in range(1, len(CENTRES)):
        g = (curve[i] - curve[i - 1]) * scale
        if abs(g) < 1e-6:
            continue
        b, a = shelf(CENTRES[i] / math.sqrt(2), g)
        y = lfilter(b, a, y, axis=0)
    return y


def kweighted_power(x):
    import pyloudnorm as pyln
    meter = pyln.Meter(SR)
    y = x.copy()
    from scipy.signal import lfilter
    for f in meter._filters.values():
        y = lfilter(f.b, f.a, y, axis=0)
    return float((y ** 2).mean())


def note_ons(events_path):
    ons = []
    for line in open(events_path):
        p = line.split()
        if len(p) >= 4 and p[1] == "on" and int(p[3]) > 0:
            ons.append(float(p[0]))
    return sorted(ons)


def apply(wav, events, fit_json, factor, out, offset=0.0):
    import soundfile as sf
    x = read_wav(wav)
    ft = json.load(open(fit_json))
    curve = ft["curveLevelFree"]
    ons = [t + offset for t in note_ons(events)]
    assert len(ons) == len(ARCH), (len(ons), len(ARCH))
    bounds = ons + [len(x) / SR]
    versions = {}
    gains = {}
    for k in sorted(set(ARCH)):
        scale = factor * (k - 3) / 7
        y = eq_filter(x, curve, scale) if scale != 0 else x.copy()
        # level compensation over this marking's notes (their note-on to the next note-on)
        segs = [(int(bounds[j] * SR), int(bounds[j + 1] * SR)) for j, m in enumerate(ARCH) if m == k]
        p0 = sum(kweighted_power(x[a:b]) for a, b in segs)
        p1 = sum(kweighted_power(y[a:b]) for a, b in segs)
        g = math.sqrt(p0 / p1) if p1 > 0 else 1.0
        versions[k] = y * g
        gains[k] = round(20 * math.log10(g), 3)
    # crossfade: before the first note-on the first marking's version; at each note-on a 100 ms raised cosine
    w = {k: np.zeros(len(x)) for k in versions}
    cur = ARCH[0]
    w[cur][:] = 1.0
    fade = int(0.1 * SR)
    for j in range(1, len(ARCH)):
        new = ARCH[j]
        if new == cur:
            continue
        c = int(ons[j] * SR) - fade // 2
        ramp = 0.5 - 0.5 * np.cos(np.linspace(0, math.pi, fade))
        for k in w:
            w[k][c:] = 0.0
        w[cur][c:c + fade] = 1.0 - ramp
        w[new][c:c + fade] = ramp
        w[new][c + fade:] = 1.0
        cur = new
    y = sum(versions[k] * w[k][:, None] for k in versions)
    sf.write(out, y, SR, subtype="FLOAT")
    print(out, "compensation dB per marking", gains)
    return gains


def measure(wav, events, out, offset=0.0):
    import brightness
    x = read_wav(wav)
    ons = [t + offset for t in note_ons(events)]
    res = {}
    for name, j in (("ppp", 0), ("mp", 3), ("fff", 7)):
        a, b = int((ons[j] + 0.3) * SR), int((ons[j] + 1.3) * SR)
        f, P = brightness.power_spectrum(x[a:b], SR)
        bp = [db(p) for p in band_powers(f, P)]
        tot = db(sum(10 ** (v / 10) for v in bp))
        res[name] = {"bandToneDb": [round(v - tot, 2) for v in bp], "scAmp": round(brightness.centroids(f, P)["scAmp"], 1)}
    json.dump(res, open(out, "w"), indent=1)
    return res


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("real-bands"); a.add_argument("instrument"); a.add_argument("out"); a.add_argument("--pitches", type=int, nargs="+", required=True)
    a = sub.add_parser("sso-events"); a.add_argument("out"); a.add_argument("notes"); a.add_argument("--pitches", type=int, nargs="+", required=True)
    a = sub.add_parser("sso-bands"); a.add_argument("wav"); a.add_argument("notes"); a.add_argument("-o", "--out", required=True)
    a = sub.add_parser("fit"); a.add_argument("real"); a.add_argument("sso"); a.add_argument("--section", required=True); a.add_argument("-o", "--out", required=True)
    a = sub.add_parser("score"); a.add_argument("out")
    a = sub.add_parser("apply"); a.add_argument("wav"); a.add_argument("events"); a.add_argument("fit"); a.add_argument("--factor", type=float, required=True)
    a.add_argument("--offset", type=float, default=0.0); a.add_argument("-o", "--out", required=True)
    a = sub.add_parser("measure"); a.add_argument("wav"); a.add_argument("events"); a.add_argument("--offset", type=float, default=0.0); a.add_argument("-o", "--out", required=True)
    args = ap.parse_args(argv)
    if args.cmd == "real-bands":
        real_bands(args.instrument, args.out, set(args.pitches))
    elif args.cmd == "sso-events":
        sso_events(args.out, args.notes, args.pitches)
    elif args.cmd == "sso-bands":
        sso_bands(args.wav, args.notes, args.out)
    elif args.cmd == "fit":
        fit(args.real, args.sso, args.section, args.out)
    elif args.cmd == "score":
        score(args.out)
    elif args.cmd == "apply":
        apply(args.wav, args.events, args.fit, args.factor, args.out, args.offset)
    elif args.cmd == "measure":
        measure(args.wav, args.events, args.out, args.offset)


if __name__ == "__main__":
    main()
