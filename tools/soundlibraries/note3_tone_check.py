#!/usr/bin/env python3
"""Is the swell of note 3 in the 180 ms figure loudness or tone? (2026-10-14, branch note3-tone.) Written for this
check, not taken from anywhere. Measurement only: the renderer doesn't read the result (sso_note3_tone_check.json).

  note3_tone_check.py <n3 work> <rule mp3 dir> <g180 mp3 dir> <out.json>

The owner (blind, db evenrule1): R (even_rule_check.py: CC11 cancelling the repeatable swell) cut note 3's loudness
rise 3.07 -> 1.84 dB, yet in both 180R pairs "both swell at 3", as with C and S. note3_check.py: the swell is SSO's
E3 -> Eb3 transition sample (Celli - Performance, fingered 69), wherever it sits; it rises within ~55 ms at 180 ms.

Question: does the sound change only in level during that rise (which a broadband gain, CC11, can undo: measured
near-flat across bands, even_rule_check.py), or in spectrum (tone), which no gain can undo?

Method, per note, in its note-on span [on, on + IOI) of every pass of every take:
  - Zwicker's time-varying loudness (ISO 532-1, MOSQITO 1.2.1; digital full scale = 1 Pa, 48 kHz): the dip and the
    peak of the loudness level after it (the largest rise, as note3_check.py's swell, on the loudness level
    40 + 33.22 log10 N phon);
  - between the dip and the peak, each critical-band region's level change (0-6, 6-12, 12-18, 18-24 Bark; specific
    loudness summed per region, as an excitation-level change 10 log10(ratio) / 0.23, Zwicker's N' ~ E^0.23). A gain
    raises every region by the same dB; tone is the regions' spread (top region minus bottom region, dB);
  - sharpness (DIN 45692, from the same loudness, acum) and the spectral centroid (power spectrum, 21 ms Hann frames,
    Hz) at the dip and at the peak: a gain changes neither;
  - f0 and harmonics-to-noise ratio (Boersma 1993: normalised autocorrelation peak r in a 40 ms Hann frame, 100-260
    Hz lags, HNR = 10 log10(r / (1 - r))): the pitch glide of the transition and the bow noise at the dip and peak.
Each figure: the median over passes and takes of the notes at that slur position. Zwicker's loudness runs ~24 ms
behind the 21 ms frames (its temporal weighting): the frame measures are read at loudness time - lag, the lag measured
per file (best correlation of the two, each less its 100 ms moving average; fig180 t1: 24 ms, r 0.75).

Result (Windows test VM renders, kthost + Kontakt 8 + SSO, uncorrected, 3 takes x 3 passes; and the mp3s the owner
heard; Linux, MOSQITO 1.2.1):
  Both: a level rise AND a change of tone, and the tone part is what a gain can't touch. Uncorrected (fig180), note 3
  from its dip (75 ms after the note-on) to its peak (134 ms): loudness +5.0 phon; the band levels rise unevenly:
  below ~400 Hz (0-4 Bark: the fundamental and 2nd partial) +12.1 dB, 4-16 Bark +3.0-4.6, 16-20 +2.0, 20-25 +0.3
  (loudness-weighted spread around the mean, 'tone', 4.17 dB, against 1.96 / 2.24 for notes 1 / 2); the spectral
  centroid falls 45 % (darker: the body comes in), the harmonics-to-noise ratio rises 7.1 dB (from bow noise to a
  pitched tone), while f0 is still on the way down (163.0 -> 162.1 Hz; Eb3 is 155.6): the swell happens during the
  slide, before the pitch arrives. DIN sharpness hardly moves (+0.06 acum): it weights > 3 kHz, the change is at the
  bottom; the centroid sees it.
  In the heard clips, note 3, S (one level per note) / C / R (the rule): the band-weighted level change 6.52 / 5.77 /
  1.91 dB: R removed most of the level; the tone stays: spread 4.40 / 4.20 / 3.67 dB, the lowest band still +8.0 dB
  against the mid bands' -1.0..+0.3 in R, centroid -46 / -45 / -44 %, HNR +7.2 / +6.9 / +6.5 dB. The owner heard
  "both swell at 3" in R as in S: what is left is the tone.
  It follows the sample, as note3_check.py found for the level: E3 -> Eb3 elsewhere: as note 1 (rot1) tone 5.03,
  centroid -49 %, HNR +5.2; in the line (note 3) 3.89, -37 %, +8.6; the same semitone down into F3 (up2, note 3) 2.69,
  -35 %, +2.4; into C3 (dn3) 2.08, +1 %, +2.6. Note 4 (Eb3 -> C#3) changes as much in tone (4.38, -43 %, +6.0) but
  from its note-on on (dip at 2 ms: an onset, not a dip in the middle of the note); the owner hears note 3, whose
  body comes in after a thin, noisy dip mid-note.
  CC11 is a broadband gain (near-flat across bands, even_rule_check.py): it can cancel the level part (R did) but not a
  +8-12 dB rise of the lowest band against the rest, nor noise turning into tone; flattening the low band with a gain
  moves every other band instead (what made the 40 ms correction wobble). So the remaining swell is SSO's sample:
  nothing on the levels side removes it.
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REGIONS = [(0, 4), (4, 8), (8, 12), (12, 16), (16, 20), (20, 25)]
FS = 48000


def load(path):
    import soundfile as sf
    from scipy.signal import resample_poly
    if path.endswith('.mp3'):
        import subprocess
        raw = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'f32le', '-ac', '1', '-ar', str(FS), '-'],
                             capture_output=True, check=True).stdout
        return np.frombuffer(raw, dtype=np.float32).astype(float)
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1)
    if sr != FS:
        g = np.gcd(FS, sr)
        x = resample_poly(x, FS // g, sr // g)
    return x


def analyse(x):
    """time axis (s), loudness level (phon), region levels (dB, arbitrary origin), sharpness, centroid, f0, HNR"""
    from mosqito.sq_metrics import loudness_zwtv
    from mosqito.sq_metrics.sharpness.sharpness_din import sharpness_din_from_loudness
    N, Ns, bark, t = loudness_zwtv(x, FS, field_type='free')
    N = np.maximum(N, 1e-6)
    LN = np.where(N >= 1, 40 + 33.22 * np.log10(N), 40 * (N + 0.0005) ** 0.35)
    share = np.array([Ns[(bark >= a) & (bark < b)].sum(axis=0) for a, b in REGIONS])   # each region's loudness
    S = sharpness_din_from_loudness(N, Ns, weighting='din')
    # band levels (physical dB per Bark region, Traunmüller 1990's z(f)), centroid, f0 / HNR on their own frames
    hop = 96
    win = 1024
    w = np.hanning(win)
    nfr = (len(x) - win) // hop
    fr_t = (np.arange(nfr) * hop + win / 2) / FS
    freqs = np.fft.rfftfreq(win, 1 / FS)
    z = 26.81 * freqs / (1960 + freqs) - 0.53
    masks = [(z >= a) & (z < b) for a, b in REGIONS]
    cent = np.empty(nfr)
    lev = np.empty((len(REGIONS), nfr))
    for i in range(nfr):
        P = np.abs(np.fft.rfft(x[i * hop:i * hop + win] * w)) ** 2
        cent[i] = (P * freqs).sum() / max(P.sum(), 1e-20)
        for r, mk in enumerate(masks):
            lev[r, i] = 10 * np.log10(max(P[mk].sum(), 1e-20))
    # Zwicker's loudness (temporal weighting) runs behind the 21 ms frames: the lag of best correlation of the two,
    # each less its 100 ms moving average, read the frame measures at loudness time - lag (fig180: 24 ms, r 0.75)
    t = np.asarray(t)
    tot = 10 * np.log10(np.sum(10 ** (lev / 10), axis=0))
    hp = lambda v: v - np.convolve(v, np.ones(50) / 50, 'same')
    A = hp(np.interp(t, fr_t, tot))
    B = hp(LN)
    sel = LN > np.percentile(LN, 30)
    lags = np.arange(0, 31)                                   # 0-60 ms in 2 ms steps
    rs = [np.corrcoef(A[:len(A) - L][sel[L:]], B[L:][sel[L:]])[0, 1] for L in lags]   # B later than A by L
    lag = float(lags[int(np.argmax(rs))] * (t[1] - t[0]))
    tq = t - lag
    reg = np.array([np.interp(tq, fr_t, lev[r]) for r in range(len(REGIONS))])
    w2 = int(0.040 * FS)
    h2 = int(0.005 * FS)
    hw = np.hanning(w2)
    rw = np.correlate(hw, hw, 'full')[w2 - 1:]
    lo, hi = int(FS / 260), int(FS / 100)
    n2 = (len(x) - w2) // h2
    f0 = np.full(n2, np.nan)
    hnr = np.full(n2, np.nan)
    for i in range(n2):
        s = x[i * h2:i * h2 + w2]
        s = (s - s.mean()) * hw
        e = (s * s).sum()
        if e < 1e-10:
            continue
        sp = np.fft.rfft(s, 2 * w2)
        ac = np.fft.irfft(np.abs(sp) ** 2)[:w2]
        with np.errstate(divide='ignore', invalid='ignore'):
            ac = (ac / ac[0]) / (rw / rw[0])                 # Boersma 1993: normalised, the window's own divided out
        k = lo + int(np.argmax(ac[lo:hi]))
        r = float(min(ac[k], 0.9999))
        if 0 < k < len(ac) - 1:                              # parabolic peak for the lag
            a, b, c = ac[k - 1], ac[k], ac[k + 1]
            d = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0
        else:
            d = 0
        f0[i] = FS / (k + d)
        hnr[i] = 10 * np.log10(r / (1 - r)) if r > 0 else np.nan
    t2 = (np.arange(n2) * h2 + w2 / 2) / FS
    ok = ~np.isnan(f0) & ~np.isnan(hnr)
    return dict(t=t, LN=LN, reg=reg, share=share, S=np.asarray(S), cent=np.interp(tq, fr_t, cent), lag=lag,
                lagR=float(max(rs)), f0=np.interp(tq, t2[ok], f0[ok]), hnr=np.interp(tq, t2[ok], hnr[ok]))


def note(a, on, ioi):
    t = a['t']
    idx = np.where((t >= on) & (t < on + ioi))[0]
    x = a['LN'][idx]
    mn = np.minimum.accumulate(x)
    k = int(np.argmax(x - mn))
    j = int(np.argmin(x[:k + 1]))
    J, K = idx[j], idx[k]
    dr = a['reg'][:, K] - a['reg'][:, J]
    wgt = 0.5 * (a['share'][:, J] + a['share'][:, K])
    wgt = wgt / wgt.sum()
    mean = float((wgt * dr).sum())
    return dict(rise=float(x[k] - x[j]), dipMs=1000 * (t[J] - on), peakMs=1000 * (t[K] - on),
                regions=[float(v) for v in dr], share=[float(v) for v in wgt], gain=mean,
                tone=float(np.sqrt((wgt * (dr - mean) ** 2).sum())),
                sharp=[float(a['S'][J]), float(a['S'][K])], cent=[float(a['cent'][J]), float(a['cent'][K])],
                f0=[float(a['f0'][J]), float(a['f0'][K])], hnr=[float(a['hnr'][J]), float(a['hnr'][K])])


def summary(rows):
    def med(f):
        return round(float(np.median([f(r) for r in rows])), 2)
    return dict(n=len(rows), rise=med(lambda r: r['rise']), dipMs=med(lambda r: r['dipMs']),
                peakMs=med(lambda r: r['peakMs']),
                regions=[med(lambda r, i=i: r['regions'][i]) for i in range(len(REGIONS))],
                share=[med(lambda r, i=i: r['share'][i]) for i in range(len(REGIONS))],
                gain=med(lambda r: r['gain']), tone=med(lambda r: r['tone']),
                sharpDip=med(lambda r: r['sharp'][0]), sharpPeak=med(lambda r: r['sharp'][1]),
                dSharp=med(lambda r: r['sharp'][1] - r['sharp'][0]),
                centDip=med(lambda r: r['cent'][0]), centPeak=med(lambda r: r['cent'][1]),
                dCentPct=med(lambda r: 100 * (r['cent'][1] / r['cent'][0] - 1)),
                f0Dip=med(lambda r: r['f0'][0]), f0Peak=med(lambda r: r['f0'][1]),
                dHnr=med(lambda r: r['hnr'][1] - r['hnr'][0]))


def positions(m):
    """slur position (1-4, 0 the lead) of every note in meta"""
    out = []
    for k, n in enumerate(m['notes']):
        j = k
        while j > 0 and m['notes'][j - 1]['slur'] == n['slur']:
            j -= 1
        out.append(0 if k == 0 else k - j + 1)
    return out


def cell_file(job):
    """one render: (cell, rows by slur position)"""
    work, name, f = job
    m = json.load(open(os.path.join(work, 'meta.json')))[name]
    pos = positions(m)
    ioi = m['ioi'] / 1000
    a = analyse(load(os.path.join(work, 'wav', f)))
    rows = {}
    for p0 in m['passes']:
        for k, n in enumerate(m['notes']):
            if pos[k] and n['on'] + ioi + p0 < a['t'][-1]:
                rows.setdefault(pos[k], []).append(note(a, n['on'] + p0, ioi))
    print(name, f, flush=True)
    return name, rows


def cells(work, names, pool):
    jobs = [(work, name, f) for name in names for f in sorted(os.listdir(os.path.join(work, 'wav')))
            if f.startswith(name + '_t') and f.endswith('.wav')]
    rows = {}
    for name, r in pool.imap_unordered(cell_file, jobs):
        for p, v in r.items():
            rows.setdefault(name, {}).setdefault(p, []).extend(v)
    meta = json.load(open(os.path.join(work, 'meta.json')))
    return {name: dict({str(p): summary(v) for p, v in sorted(rows[name].items())}, ioi=meta[name]['ioi'])
            for name in names if name in rows}


def clip_file(job):
    """one heard mp3: notes.json gives [on, arrival, name, bowed, position] in clip seconds"""
    path, notes_key, ioi, label = job
    N = json.load(open(os.path.join(os.path.dirname(path), 'notes.json')))[notes_key]
    a = analyse(load(path))
    rows = {}
    for n in N:
        if n[4] and n[0] + ioi < a['t'][-1]:
            rows.setdefault(n[4], []).append(note(a, n[0], ioi))
    print(label, flush=True)
    return label, {str(p): summary(v) for p, v in sorted(rows.items())}


def main():
    from multiprocessing import Pool
    work, rule_dir, g180_dir, out_path = sys.argv[1:5]
    clips = [(os.path.join(rule_dir, 'p1a.mp3'), '180R', 0.18, '180R R (rule p1a)'),
             (os.path.join(rule_dir, 'p1b.mp3'), '180R', 0.18, '180R S (rule p1b)'),
             (os.path.join(rule_dir, 'p3b.mp3'), '250R', 0.25, '250R R (rule p3b)'),
             (os.path.join(rule_dir, 'p3a.mp3'), '250R', 0.25, '250R S (rule p3a)'),
             (os.path.join(g180_dir, 'p1a.mp3'), '180', 0.18, '180R S (g180 p1a)'),
             (os.path.join(g180_dir, 'p1b.mp3'), '180', 0.18, '180R C (g180 p1b)')]
    with Pool(4) as pool:
        heard = dict(pool.map(clip_file, clips))
        res = dict(cells=cells(work, ['fig180', 'rot1_180', 'rot2_180', 'rot4_180', 'up2_180', 'dn3_180', 'line180',
                                      'fig250', 'iso600'], pool), heard=heard)
    res['_about'] = __doc__.split('\n\n')[0]
    res['regionsBark'] = REGIONS
    json.dump(res, open(out_path, 'w'), indent=1)


if __name__ == '__main__':
    main()
