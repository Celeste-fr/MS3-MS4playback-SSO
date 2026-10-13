#!/usr/bin/env python3
"""The real players' brightness at pp and ff, per pitch, from TU Berlin's anechoic recordings (stage B of "Dynamics like
a real player", HANDOFF.md).

Source: Weinzierl, Vorlaender, Ackermann, Behler, Brinkmann, von Coler, Detzner, Kraemer, Lindau, Pollow, Schulz &
Shabtai (2024), "A Database of Anechoic Microphone Array Measurements of Musical Instruments", version 3, DepositOnce
(TU Berlin), doi 10.14279/depositonce-19858 (handle 11303/21058), CC BY-NC-SA 4.0; the recordings of Weinzierl, Lepa,
Schultz, Detzner, von Coler & Behler (2018), "Sound power and timbre as cues for the dynamic strength of orchestral
instruments", JASA 144(3), 1347-1355. Files "2_<Instrument>_modern_recordings.zip": one SOFA file per note and dynamic
(pp = "as softly as possible", ff = "as loudly as possible", chromatic over the range), 32 microphones on a sphere
around the player in the anechoic chamber, calibrated in pascals, compensated for the array's response, stored as
one-sided spectra (documentation eq. 1: interior bins doubled), with the manually set steady part (SteadyPart, samples)
and the tuning (SourceTuningFrequency: 443 Hz). Only derived numbers are kept; the zips are read member by member over
HTTP range requests (httpzip.py), nothing is stored.

Per note: the steady part of all 32 microphones -> brightness.measure_signal (power average over the microphones; the
power spectrum of the quietest 0.25 s of the file outside the steady part subtracted with a 3 dB gate:
chamber and microphone noise, ~30 dB SPL per microphone, only ~9 dB under a violin's pp A4), f0 from the MIDI note and
the tuning. Also the levels of the steady part and the noise (snrDb) and, for the CSV's columns, the principal
microphone's (the one loudest in the steady part most often over the instrument's notes, as the paper chose it)
measures.

    python3 real_brightness.py fetch <instrument> <out.jsonl>     (resumable: notes already in out.jsonl are skipped)
    python3 real_brightness.py merge <in.jsonl> ... -o tools/soundlibraries/real_brightness.json
"""
import argparse
import io
import json
import os
import statistics
import sys
import zipfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import brightness  # noqa: E402
import httpzip  # noqa: E402

API = "https://api-depositonce.tu-berlin.de/server/api/core/bitstreams/%s/content"
ZIPS = {  # bitstream ids in the version 3 item (handle 11303/21058), listed 2026-10-12
    "Violin": "32adecd2-1e53-47eb-ad31-61b864fcb432",
    "Viola": "f584155c-1272-49cf-ab3e-06486eb8a1f4",
    "Cello": "162ffe06-eec0-46f5-81ac-da6eadc7a592",
    "Double_bass": "ea938c7d-68f5-4166-91a6-1238ace231ae",
}
SECTIONS = {"Violin": ["Violins 1", "Violins 2"], "Viola": ["Violas"], "Cello": ["Celli"], "Double_bass": ["Basses"]}
NOISE_LEN = 0.25


def signals(sofa_bytes):
    import h5py
    with h5py.File(io.BytesIO(sofa_bytes), "r") as f:
        X = f["Data.Real"][0] + 1j * f["Data.Imag"][0]
        steady = [int(v) for v in f["SteadyPart"][0]]
        midi = int(round(float(f["MIDINote"][0])))
        tuning = float(f["SourceTuningFrequency"][0])
        desc = f.attrs["Description"].decode()
    K = X.shape[1]
    N = 2 * (K - 1)
    Y = X.copy()
    Y[:, 1:-1] *= 0.5           # the documentation's eq. 1: the stored one-sided spectrum has its interior doubled
    x = np.fft.irfft(Y, n=N, axis=1).T   # samples x 32, pascals
    return x, steady, midi, tuning, desc


def quietest(x, sr, steady):
    """the quietest NOISE_LEN s of the file outside the steady part (power over every microphone, 50 ms hop), after
    the first 0.05 s: the noise without the note (some notes start within the file's first 0.3 s)"""
    n, hop = int(NOISE_LEN * sr), int(0.05 * sr)
    p = (x ** 2).mean(axis=1)
    c = np.concatenate([[0.0], np.cumsum(p)])
    best = None
    for s in range(hop, len(p) - n, hop):
        if s + n > steady[0] and s < steady[1]:
            continue
        e = c[s + n] - c[s]
        if best is None or e < best[0]:
            best = (e, s)
    s = best[1] if best else 0
    return x[s:s + n]


def note_measures(x, steady, midi, tuning, sr=44100):
    f0 = tuning * 2 ** ((midi - 69) / 12)
    a, b = steady
    seg = x[a:b]
    noise = quietest(x, sr, steady)
    m = brightness.measure_signal(seg, sr, f0, noise=noise)
    raw = brightness.measure_signal(seg, sr, f0, sharp=False)
    lp = 10 * np.log10((seg ** 2).mean(axis=0) / brightness.P0 ** 2)
    ln = 10 * np.log10((noise ** 2).mean(axis=0) / brightness.P0 ** 2)
    m["snrDb"] = round(float(10 * np.log10(np.mean(10 ** (lp / 10))) - 10 * np.log10(np.mean(10 ** (ln / 10)))), 2)
    m["scPowNoNoiseSub"] = raw["scPow"]
    m["scAmpNoNoiseSub"] = raw["scAmp"]
    m["lw"] = round(float(10 * np.log10(np.mean(10 ** (0.1 * lp))) + 10 * np.log10(54.63)), 2)  # documentation eq. 2-4
    m["loudestMic"] = int(np.argmax(lp)) + 1
    return f0, m


def fetch(instrument, out):
    done = set()
    if os.path.exists(out):
        with open(out) as f:
            done = {json.loads(line)["file"] for line in f if line.strip()}
    z = zipfile.ZipFile(httpzip.HttpFile(API % ZIPS[instrument]))
    names = [i.filename for i in z.infolist() if i.filename.endswith("_recordings.sofa")]
    for n in sorted(names):
        base = n.rsplit("/", 1)[-1]
        if base in done:
            continue
        x, steady, midi, tuning, desc = signals(z.read(n))
        f0, m = note_measures(x, steady, midi, tuning)
        dyn = "ff" if "dynamic = ff" in desc else "pp" if "dynamic = pp" in desc else desc
        # every microphone's steady stretch kept as a level and an amplitude centroid, for the principal channel
        a, b = steady
        per_mic = []
        for ch in range(x.shape[1]):
            fq, P = brightness.power_spectrum(x[a:b, ch], 44100)
            c = brightness.centroids(fq, P, f0)
            per_mic.append([round(brightness.level_db(P), 2), round(c["scAmp"], 1), round(c["scPow"], 1)])
        row = {"file": base, "instrument": instrument, "dynamic": dyn, "midi": midi, "tuning": tuning,
               "f0": round(f0, 3), "steady": steady, "measures": m, "perMic": per_mic}
        with open(out, "a") as f:
            f.write(json.dumps(row) + "\n")
        print(base, dyn, midi, m["scPow"], m["sharp"], m["snrDb"], flush=True)


def merge(paths, out):
    rows = []
    for p in paths:
        with open(p) as f:
            rows += [json.loads(line) for line in f if line.strip()]
    res = {"method": {
        "source": "Weinzierl et al. (2024), A Database of Anechoic Microphone Array Measurements of Musical Instruments, "
                  "v3, DepositOnce doi 10.14279/depositonce-19858, CC BY-NC-SA 4.0 (recordings of Weinzierl et al. "
                  "2018, JASA 144(3)); files 2_<Instrument>_modern_recordings.zip, one player each",
        "dynamics": "pp and ff as recorded (the players' softest / loudest); rule D (HANDOFF.md) reads them as ppp / fff",
        "stretch": "the database's SteadyPart of each note, all 32 microphones, power average of their spectra",
        "noise": "power spectrum of the quietest 0.25 s of the same file outside the steady part (50 ms grid) subtracted, bins under 3 dB above it "
                 "dropped; snrDb = steady level - noise level (microphone power averages)",
        "measures": brightness.__doc__,
        "principal": "principalMic: the microphone loudest in the steady part most often over the instrument's notes "
                     "(the paper's rule for its features); perMic: [level dB, scAmp Hz, scPow Hz] of each microphone, "
                     "no noise subtraction, for comparison with 3_Audio_Features.csv",
        "tool": "tools/soundlibraries/real_brightness.py (fetch, merge), brightness.py, httpzip.py",
        "measured": "2026-10-12 on the Linux dev VM from the published recordings"}, "instruments": {}}
    for inst in sorted({r["instrument"] for r in rows}):
        rs = [r for r in rows if r["instrument"] == inst]
        mics = [r["measures"]["loudestMic"] for r in rs]
        principal = statistics.mode(mics)
        notes = {}
        for r in sorted(rs, key=lambda r: (r["midi"], r["dynamic"])):
            m = dict(r["measures"])
            pm = r["perMic"][principal - 1]
            m["principalScAmp"], m["principalScPow"] = pm[1], pm[2]
            notes.setdefault(str(r["midi"]), {})[r["dynamic"]] = m
        res["instruments"][inst] = {"sections": SECTIONS[inst], "principalMic": principal,
                                    "tuning": rs[0]["tuning"], "notes": notes}
    with open(out, "w") as f:
        f.write(dump(res) + "\n")


def dump(obj, indent=0):
    pad = " " * indent
    if isinstance(obj, dict) and obj and not all(isinstance(v, (int, float, str)) for v in obj.values()):
        items = [f'{pad} {json.dumps(k)}: {dump(v, indent + 1).lstrip()}' for k, v in obj.items()]
        return pad + "{\n" + ",\n".join(items) + "\n" + pad + "}"
    return pad + json.dumps(obj)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("fetch")
    a.add_argument("instrument", choices=sorted(ZIPS))
    a.add_argument("out")
    m = sub.add_parser("merge")
    m.add_argument("inputs", nargs="+")
    m.add_argument("-o", "--out", required=True)
    args = ap.parse_args(argv)
    if args.cmd == "fetch":
        fetch(args.instrument, args.out)
    else:
        merge(args.inputs, args.out)


if __name__ == "__main__":
    main()
