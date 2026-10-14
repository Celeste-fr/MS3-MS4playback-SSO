#!/usr/bin/env python3
"""Why the 40 ms correction (N) wobbled, a wobble measure checked against the owner's ear, and a smoother correction
rule (2026-10-14, branch even-rule). Written for this check, not taken from anywhere. Measurement and audio only: the
renderer doesn't read the result (sso_even_rule_check.json).

  even_rule_check.py wobble <tempo_line work dir>
        the heard mp3s (U / C / N) and the CC11 each version sent: wobble measures against the owner's verdicts
  even_rule_check.py cache <f180 work> <plays dir> [names]      every pass of every render, analysed once (npz)
  even_rule_check.py events <f180 work> <plays dir> <sim dir> <work>   the rule's kthost event files (list1.txt)
  even_rule_check.py real <f180 work> <work>                    the rule's renders against U / C / S, every pass
  even_rule_check.py clips <f180 work> <work> <mp3 dir> <key.json>     blind S / R pairs + notes.json
  (the simulation sweep: scratch driver, see "Simulation" below; its functions are here: calibrate, wiener,
   sent_for, response, audio_measures)

The owner (blind, db evenwindow1, 2026-10-13): N (even_window_check: the envelope flattened to each slur's median with
CC11 nodes every 40 ms) "horrible, it has a really obv wobbly sound", wrong in 6 of 6 passages; C (the job's one value
per note) fine 5 of 6; U wrong 4 of 6. Then (db evenfig1): S (C with the node mid-note) fine 2/2 at 250 ms where C was
wrong 2/2; at 180R both swell at note 3 (SSO's E3 -> Eb3 sample: note3_check.py).

1. What made N wobble (the heard mp3s, Zwicker's time-varying loudness, ISO 532-1, MOSQITO 1.2.1; digital full scale
   = 1 Pa). The measure everything was judged by (loudness.py's perceived envelope: 46 ms frames, 22 / 50 ms
   smoothing) can't see modulation above ~10 Hz, and N cancelled what it could see: N's total-loudness fluctuation
   is the LOWEST of the three in every passage (FS-weighted depth 0.42-0.63 dB against C 0.66-1.04, U 0.76-1.44).
   Per critical band it is the other way round: N's fluctuation strength (Zwicker & Fastl eq. 10.2 per 0.1 Bark,
   excitation level from N' ~ E^0.23) rises in the UPPER bands, 12-24 Bark (above ~1.7 kHz: bow noise, upper
   partials): 136R U .214 C .175 N .240, 136L .203 .218 .247, 180R .207 .221 .288, 180L .211 .216 .239, 250R .184 .184
   .211, 250L .206 .195 .236 (N highest in 6 of 6, 11-30 % above the higher of U and C), while it lowers the low
   bands (0-12 Bark), where the swell is. CC11 is a broadband gain (C - U per Bark region against the C curve sent:
   gain 1.24-1.40 between 3 and 20 Bark, r 0.93-0.98), so cancelling the low bands' swell puts the same movement on
   bands that didn't swell. Not noise-chasing: the swell is repeatable at every modulation frequency up to 64 Hz
   (180R, 18 plays: the Wiener weight below 0.92-1.0 above 1 Hz), and not mistiming (CC11: K 1.048, one pole 40 ms,
   lag 4 ms, r 0.994 on the 18 C against 18 U plays).
2. The wobble measure: upper-band (12-24 Bark) fluctuation strength ranks N worst in 6 of 6 as the owner did; the
   whole-spectrum sum (0-24 Bark) agrees with 9 of the 10 verdict pairs of that test (misses 180L: N < C) and has S <
   C at 250R (the owner: S fine, C wrong).
3. The rule R: CC11 cancels the repeatable part of each slur's loudness swell, at the strength that minimises the
   sound's fluctuation strength summed over the critical bands. Swell = the perceived envelope less a continuous
   target (each slur's median at the slur's middle, joined linearly; faded in / out over one IOI: a step target
   becomes a spike once the smoothing is inverted); repeatable = the passes' mean, each third-octave modulation band
   weighted max(0, 1 - noise / power) (Wiener; noise = the passes' spread / n); sent = the wanted level change through
   the inverted CC11 response (G + tau G', lag earlier, / K, 7 bits, <= 127); strength alpha in {.25 .5 .75 1}, the
   one with the least fluctuation strength (0-24 Bark) in the simulation. No per-note case, no list, no new constant:
   the trade-off (low bands' swell against upper bands' added movement) is decided by Zwicker & Fastl's model.
4. Simulation (audio domain: each held-out uncorrected play times the level change, analysed as the cache; validated:
   C's and S's simulated upper-band FS .235 / .228 against the real renders' .236 / .227, N worst). Fluctuation
   strength 0-24 Bark at alpha .25 / .5 / .75 / 1: 180R .649 .638 .646 .671 (U .680, C .681), 250R .716 .704 .713 .745,
   250L .854 .839 .835 .839, 136R .576 .544 .535 .548, 136L .671 .672 .677 .685, 180L .762 .757 .757 .763: chosen
   180R .5, 250R .5, 250L .75, 136R .75, 136L .25, 180L .75 (a tie with .5).
5. Rendered (VM, kthost + Kontakt 8 + SSO, 2026-10-14; 180R / 250R / 250L 3 takes, the others 1; every pass; U / C / S
   the earlier renders): swell p75 | upper-band FS (12-24 Bark) | FS 0-24 Bark; 180R / 250R by slur position
            U                    C                    S                    R
     180R   2.77 .213 .680      2.81 .236 .683      2.45 .227 .676      1.65 .210 .642
            .65 2.00 3.50 1.78  2.08 1.41 3.19 .85  1.26 1.69 3.07 1.33  .59 1.29 1.84 1.20
     250R   3.48 .195 .752      3.08 .193 .743      2.54 .177 .723      2.08 .179 .709
            .89 1.54 5.85 1.22  1.53 .89 4.21 2.08  1.23 1.48 3.14 1.85  .51 1.20 2.98 .77
     250L   2.41 .209 .890      1.98 .200 .818      2.09 .194 .855      1.13 .209 .843
     136R   2.02 .218 .625      1.70 .175 .569      1.57 .196 .593      1.05 .194 .584
     136L   1.47 .216 .674      1.44 .231 .686      1.33 .237 .691      1.21 .215 .664
     180L   1.96 .217 .772      1.88 .226 .745      1.83 .231 .780      1.11 .214 .762
   R has the least swell in all six; its upper-band FS is at or below U's in all six (C's above U's in three), above
   C's at 250L (+.009) and 136R (+.019). 180R note 3 (SSO's E3 -> Eb3): 3.50 U, 3.19 C, 3.07 S, 1.84 R. The real
   renders do better than the simulation (180R note 3 2.69 simulated): not explained here. Blind clips S / R: 180R,
   250L, 250R, and 180R again with letters swapped. Measures are not the owner's ear.
"""
import argparse
import json
import os
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
import slow_slur_test as sst  # noqa: E402

OUT_JSON = os.path.join(HERE, 'sso_even_rule_check.json')
NAMES = [f'{i}{k}' for i in (136, 180, 250) for k in 'RL']
# the owner, blind (db evenwindow1, 2026-10-13): 1 = sounds wrong
VERDICT_UCN = {'136R': dict(U=1, C=0, N=1), '136L': dict(U=0, C=0, N=1), '180R': dict(U=1, C=1, N=1),
               '180L': dict(U=0, C=0, N=1), '250R': dict(U=1, C=0, N=1), '250L': dict(U=1, C=0, N=1)}
FS_MAX_HZ = 20.0      # Zwicker & Fastl, Psychoacoustics (3rd ed.) ch. 10/11: fluctuation strength below ~20 Hz,
                      # roughness above (15-300 Hz)
PA_FULL_SCALE = 1.0   # calibration: digital full scale = 1 Pa (94 dB SPL), the usual sound-level-meter convention
REGIONS = ((0, 4), (4, 8), (8, 12), (12, 16), (16, 20), (20, 24), (0, 12), (12, 24))   # Bark


def jload(*p):
    return json.load(open(os.path.join(*p)))


def decode(path, sr=48000, channels=1):
    r = subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', path, '-f', 'f32le', '-ac', str(channels), '-ar', str(sr), '-'],
                       capture_output=True, check=True)
    return np.frombuffer(r.stdout, dtype=np.float32).astype(np.float64)


def fs_weight(f):
    """Zwicker & Fastl's fluctuation-strength dependence on modulation frequency: 1 / ((f / 4 Hz) + (4 Hz / f))"""
    f = np.asarray(f, dtype=float)
    with np.errstate(divide='ignore'):
        return np.where(f > 0, 1.0 / (f / 4.0 + 4.0 / np.maximum(f, 1e-9)), 0.0)


def mod_depth(x, dt, fmax=FS_MAX_HZ, weighted=True):
    """an envelope's (dB) modulation as an equivalent peak-to-peak depth: 2 sqrt(2) x the rms of its spectrum up to
    fmax (a sine of depth D has rms D / (2 sqrt 2)), each frequency weighted by fs_weight if asked (the mean removed,
    a Hann window, its power corrected)"""
    x = np.asarray(x, dtype=float) - np.mean(x)
    w = np.hanning(len(x))
    X = np.fft.rfft(x * w)
    f = np.fft.rfftfreq(len(x), dt)
    p = 2 * np.abs(X) ** 2 / (len(x) * np.sum(w ** 2))   # mean square per bin (Parseval, window power restored)
    sel = (f > 0) & (f <= fmax)
    g = fs_weight(f[sel]) ** 2 if weighted else 1.0
    return float(2 * np.sqrt(2) * np.sqrt(np.sum(p[sel] * g)))


def band_power(x, dt, edges):
    x = np.asarray(x, dtype=float) - np.mean(x)
    w = np.hanning(len(x))
    p = np.abs(np.fft.rfft(x * w)) ** 2
    f = np.fft.rfftfreq(len(x), dt)
    tot = p[f > 0].sum()
    return [float(p[(f > a) & (f <= b)].sum() / tot) for a, b in zip(edges[:-1], edges[1:])]


def cc11_db(ev_path, m):
    """the CC11 curve of pass 1 in dB (20 log10(v / 127)) every 5 ms, as Kontakt would hear it after its smoothing
    (one pole, 35 ms: even_window_check's fit, lag 10-15 ms), on the passage's own time (note-on 0 = first note)"""
    p0 = m['passes'][0]
    pts = []
    for line in open(ev_path):
        a = line.split()
        if len(a) == 4 and a[1] == 'cc' and a[2] == '11':
            pts.append((float(a[0]) - p0, int(a[3])))
    t = np.arange(m['notes'][1]['on'], m['notes'][-1]['on'], sst.STEP)
    v = np.full(len(t), 127.0)
    for tt, val in sorted(pts):
        v[t >= tt] = val
    return t, sst.smooth(20 * np.log10(np.maximum(v, 1) / 127.0))


def zwicker(x, sr=48000):
    from mosqito.sq_metrics import loudness_zwtv
    N, Ns, bark, tz = loudness_zwtv(x * PA_FULL_SCALE, sr, field_type='free')
    return np.asarray(N), np.asarray(Ns), np.asarray(bark), np.asarray(tz)


def fs_index(N, Ns, bark, tz, a, b):
    """the fluctuation strength formula of Zwicker & Fastl (eq. 10.2: F = 0.008 * integral over z of
    dL / ((fmod / 4 Hz) + (4 Hz / fmod)), vacil), applied to a non-periodic sound: per critical band (0.1 Bark), dL the
    weighted modulation depth (mod_depth) of the band's excitation level 10 log10 E = (10 / 0.23) log10 N' (N' ~ E^0.23,
    Zwicker & Fastl eq. 8.6), over [a, b) s; bands with N' = 0 anywhere in the span (below threshold) left out. Also
    the same for the total loudness level 33.2 log10 N (10 phon per doubling)."""
    sel = (tz >= a) & (tz < b)
    dt = float(tz[1] - tz[0])
    dz = float(bark[1] - bark[0])
    spec = Ns[:, sel]
    per = np.zeros(len(bark))
    for i in range(len(bark)):
        if np.all(spec[i] > 0):
            per[i] = 0.008 * mod_depth((10 / 0.23) * np.log10(spec[i]), dt)
    tot = mod_depth(33.2 * np.log10(np.maximum(N[sel], 1e-6)), dt)
    fast = np.zeros(len(bark))
    for i in range(len(bark)):
        if np.all(spec[i] > 0):
            fast[i] = mod_depth(highpass((10 / 0.23) * np.log10(spec[i]), dt, 8.0), dt, 50.0, weighted=False)
    regions = {f'{lo}-{hi}': float(np.sum(per[(bark >= lo) & (bark < hi)]) * dz) for lo, hi in REGIONS}
    return dict(fsBands=float(np.sum(per) * dz), fsTotalDepth=tot, perBand=per, regions=regions,
                fastBands=float(np.mean(fast[fast > 0])))


def highpass(x, dt, fc):
    X = np.fft.rfft(np.asarray(x) - np.mean(x))
    f = np.fft.rfftfreq(len(x), dt)
    X[f < fc] = 0
    return np.fft.irfft(X, len(x))


def wobble(work_dir):
    meta = jload(work_dir, 'work', 'meta.json')
    summ = jload(work_dir, 'summary.json') if os.path.exists(os.path.join(work_dir, 'summary.json')) else {}
    out = {}
    for name in NAMES:
        m = meta[name]
        res = {}
        for ver in 'UCN':
            # the CC11 that version sent (N: the round heard, the kept one; even_window_check's summary)
            if ver == 'N':
                rnd = summ[name]['correctionN']['keptRound']
                evp = os.path.join(work_dir, 'work', 'ev', f'{name}_N{rnd}.txt')
            else:
                evp = os.path.join(work_dir, 'work', 'ev', f'{name}_{ver}.txt')
            t, g = cc11_db(evp, m)
            dg = np.diff(g)
            ccr = dict(rangeDb=round(float(g.max() - g.min()), 2),
                       depth4_20=round(mod_depth(g, sst.STEP), 2),
                       maxStepDbPer40ms=round(float(np.max(np.abs(g[8:] - g[:-8]))), 2),
                       share=dict(zip(['0-2', '2-8', '8-20', '20-100'],
                                      [round(x, 3) for x in band_power(g, sst.STEP, [0, 2, 8, 20, 100])])) if np.std(g) > 1e-6 else None,
                       rmsSlope=round(float(np.sqrt(np.mean((dg / sst.STEP) ** 2))), 2))
            # the heard audio
            mp3 = os.path.join(work_dir, 'audio', f'{name[:-1]}_{name[-1]}_{ver}.mp3')
            x = decode(mp3)
            N, Ns, bark, tz = zwicker(x)
            off = sst.LEAD - m['first']    # mp3 time of the passage's time 0 (its first note-on at LEAD)
            a, b = m['notes'][1]['on'] + off, m['notes'][-1]['on'] + off
            fz = fs_index(N, Ns, bark, tz, a, b)
            res[ver] = dict(cc11=ccr, fsBands=round(fz['fsBands'], 3), fsTotalDepth=round(fz['fsTotalDepth'], 2),
                            fastBands=round(fz['fastBands'], 2), fsRegions={k: round(v, 4) for k, v in fz['regions'].items()})
            print(name, ver, 'cc11', ccr['rangeDb'], ccr['depth4_20'], ccr['maxStepDbPer40ms'],
                  '| FS bands', res[ver]['fsBands'], 'total depth', res[ver]['fsTotalDepth'],
                  '| FS 0-12', round(fz['regions']['0-12'], 3), '12-24', round(fz['regions']['12-24'], 3), flush=True)
        out[name] = res
    return out


# --- plays: each pass of each render, as analysed once --------------------------------------------------------------

LUFS = -13.0   # every play matched to the integrated loudness the owner's clips had (EBU R128, pyloudnorm)


def play_window(m, p0):
    """a pass's span in the render: 0.5 s (sst.LEAD) before its first note-on to 1.0 s after the passage's end"""
    return p0 + m['first'] - sst.LEAD, p0 + m['end'] + 1.0


def analyse_play(args):
    wav, m, p0, out = args
    if os.path.exists(out):
        return out
    import soundfile as sf
    import pyloudnorm
    from loudness import perceived_envelope
    from scipy.signal import resample_poly
    x, sr = sf.read(wav, always_2d=True)
    a, b = play_window(m, p0)
    seg = x[int(a * sr):int(b * sr)]
    gain = 10 ** ((LUFS - pyloudnorm.Meter(sr).integrated_loudness(seg)) / 20)
    seg = seg * gain
    env, first, hop = perceived_envelope(seg, sr)
    mono = seg.mean(axis=1)
    m48 = resample_poly(mono, 160, 147) if sr == 44100 else mono
    N, Ns, bark, tz = zwicker(m48, 48000)
    from mosqito.sq_metrics.loudness.loudness_zwtv._third_octave_levels import _third_octave_levels
    lv, t3, _ = _third_octave_levels(m48 * PA_FULL_SCALE, 48000)      # ISO 532-1 6.3: dB per 1/3 octave, 0.5 ms
    lv = np.asarray(lv)[:, ::4]
    t3 = np.asarray(t3)[::4]
    t0 = m['first'] - sst.LEAD    # passage time of the window's start
    np.savez_compressed(out, gain=gain, env=env.astype(np.float32), third=lv.astype(np.float32),
                        t3=(t3 + t0).astype(np.float32),
                        tenv=((first + np.arange(len(env)) * hop) / 1000.0 + t0).astype(np.float32),
                        N=N.astype(np.float32), Ns=Ns.astype(np.float32), bark=bark.astype(np.float32),
                        tz=(tz + t0).astype(np.float32))
    return out


def cache(f180, out_dir, names, workers):
    from multiprocessing import Pool
    meta = jload(f180, 'meta.json')
    os.makedirs(out_dir, exist_ok=True)
    jobs = []
    for f in sorted(os.listdir(os.path.join(f180, 'wav'))):
        if not f.endswith('.wav'):
            continue
        stem = f[:-4]
        name = stem.split('_')[0]
        if names and not any(stem.startswith(n) for n in names):
            continue
        m = meta[name]
        for i, p0 in enumerate(m['passes']):
            jobs.append((os.path.join(f180, 'wav', f), m, p0, os.path.join(out_dir, f'{stem}_p{i + 1}.npz')))
    with Pool(workers) as pool:
        for k, r in enumerate(pool.imap_unordered(analyse_play, jobs)):
            print(k + 1, '/', len(jobs), os.path.basename(r), flush=True)


# --- simulation -----------------------------------------------------------------------------------------------------

DT = 0.002                                         # zwtv's and the cache's step
THIRD_FC = np.array([25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000,
                     2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500], dtype=float)   # ISO 532-1's 28 bands


def bark_of(f):
    """Zwicker & Terhardt (1980): z = 13 atan(0.00076 f) + 3.5 atan((f / 7500)^2)"""
    return 13 * np.arctan(0.00076 * f) + 3.5 * np.arctan((f / 7500.0) ** 2)


def span(m):
    ioi = m['ioi'] / 1000
    return np.arange(m['notes'][1]['on'] - 0.3, m['notes'][-1]['on'] + ioi + 0.2, DT)


class Play:
    def __init__(self, path, m):
        d = np.load(path)
        self.m, self.t = m, span(m)
        self.env = np.interp(self.t, d['tenv'], d['env'])
        self.third = np.array([np.interp(self.t, d['t3'], b) for b in d['third']])
        self.tz, self.Ns, self.bark = d['tz'].astype(float), d['Ns'].astype(float), d['bark'].astype(float)
        self.name = os.path.basename(path)[:-4]


def lowpass(x, tau):
    if tau <= 0:
        return np.array(x, dtype=float)
    a = 1 - np.exp(-DT / tau)
    out = np.empty(len(x))
    s = x[0]
    for i, v in enumerate(x):
        s += a * (v - s)
        out[i] = s
    return out


def delay(t, x, lag):
    return np.interp(t - lag, t, x)


def sent_db(ev_path, m, t):
    """the CC11 a version sent in pass 1, as 20 log10(v / 127), on passage time t"""
    p0 = m['passes'][0]
    pts = [(float(a[0]) - p0, int(a[3])) for a in (l.split() for l in open(ev_path))
           if len(a) == 4 and a[1] == 'cc' and a[2] == '11']
    v = np.full(len(t), float(pts[0][1]) if pts else 127.0)
    for tt, val in sorted(pts):
        v[t >= tt] = val
    return 20 * np.log10(np.maximum(v, 1) / 127.0)


def response(s, t, cal):
    """the level change Kontakt makes of a sent CC11 curve (dB): K x one pole (tau) of the curve lag later"""
    g = cal['k'] * lowpass(delay(t, s, cal['lag']), cal['tau'])
    return g - g.mean()


def calibrate(U, C, ev_c, m):
    """K, tau and lag of CC11 from the real renders: mean C envelope - mean U envelope against the C curve sent"""
    t = U[0].t
    sel = (t >= m['notes'][1]['on']) & (t < m['notes'][-1]['on'])
    d = np.mean([p.env for p in C], axis=0) - np.mean([p.env for p in U], axis=0)
    s = sent_db(ev_c, m, t)
    best = None
    for tau in np.arange(0.0, 0.1001, 0.005):
        lp = lowpass(s, tau)
        for lag in np.arange(-0.02, 0.0601, 0.002):
            x = delay(t, lp, lag)[sel]
            x0, y0 = x - x.mean(), d[sel] - d[sel].mean()
            k = float(x0 @ y0 / (x0 @ x0))
            r = float(np.sqrt(np.mean((y0 - k * x0) ** 2)))
            if best is None or r < best['rms']:
                best = dict(k=round(k, 3), tau=round(float(tau), 3), lag=round(float(lag), 3), rms=round(r, 3),
                            r=round(float(x0 @ y0 / np.sqrt((x0 @ x0) * (y0 @ y0))), 3))
    return best


def band_mean(p, valid, w):
    return (p.third[valid] * w[:, None]).sum(axis=0) / w.sum()


def valid_bands(plays, m):
    """the third-octave bands audible throughout: zwtv's specific loudness > 0 over the band's Bark interval at every
    step of the passage in every play; weights: each band's width in Bark"""
    lo, hi = bark_of(THIRD_FC * 2 ** (-1 / 6)), bark_of(THIRD_FC * 2 ** (1 / 6))
    ok = np.ones(len(THIRD_FC), dtype=bool)
    for p in plays:
        sel = (p.tz >= m['notes'][1]['on']) & (p.tz < m['notes'][-1]['on'])
        for i in range(len(THIRD_FC)):
            zb = (p.bark >= lo[i]) & (p.bark < hi[i])
            if not zb.any() or not np.all(p.Ns[zb][:, sel] > 0):
                ok[i] = False
    return ok, (hi - lo)[ok]


def deviation(m, t, x):
    """x less a continuous target: each slur's median (ewc.target's span: from its first note-on to the next slur's)
    placed at the span's middle, joined linearly (held before the first / after the last); faded in and out with a
    raised cosine over one IOI at the passage's second / last note-on (a step there, or a step target at each slur
    start, becomes a spike once CC11's smoothing is inverted)"""
    import even_window_check as ewc
    T = ewc.target(m, t, x)
    starts = {}
    for n in m['notes'][1:]:
        starts.setdefault(n['slur'], n['on'])
    sl = sorted(starts.values())
    ends = sl[1:] + [m['notes'][-1]['on']]
    mids = [(a + b) / 2 for a, b in zip(sl, ends)]
    meds = [float(np.median(x[(t >= a) & (t < b)])) for a, b in zip(sl, ends)]
    Tc = np.interp(t, mids, meds)
    ioi = m['ioi'] / 1000
    a, b = m['notes'][1]['on'], m['notes'][-1]['on']
    w = np.clip(np.minimum((t - a) / ioi, (b - t) / ioi), 0, 1)
    w = 0.5 - 0.5 * np.cos(np.pi * w)
    return w * (x - Tc)


MOD_FC = 2.0 ** (np.arange(-6, 22) / 3)            # third-octave modulation bands, 0.25 .. 128 Hz (constant-Q, as
                                                   # modulation filterbanks: Dau, Kollmeier & Kohlrausch 1997)


def wiener(D):
    """the repeatable part of n plays' deviations: their mean, each third-octave modulation band scaled by
    max(0, 1 - noise / mean power), noise = the plays' spread about the mean / n (its power in the mean)"""
    D = np.asarray(D)
    n = len(D)
    mean = D.mean(axis=0)
    w = np.hanning(len(mean))
    F = np.fft.rfft(mean * w)
    f = np.fft.rfftfreq(len(mean), DT)
    Sn = np.sum(np.abs(np.fft.rfft((D - mean) * w, axis=1)) ** 2, axis=0) / (n * (n - 1))
    Sm = np.abs(F) ** 2
    H = np.zeros(len(f))
    for fc in MOD_FC:
        b = (f >= fc * 2 ** (-1 / 6)) & (f < fc * 2 ** (1 / 6))
        if b.any() and Sm[b].sum() > 0:
            H[b] = max(0.0, 1 - Sn[b].sum() / Sm[b].sum())
    H[0] = 0.0
    out = np.fft.irfft(np.fft.rfft(mean) * H, len(mean))
    return out, dict(zip([round(float(x), 2) for x in MOD_FC], [round(float(H[(f >= fc * 2 ** (-1 / 6)) & (f < fc * 2 ** (1 / 6))].mean()), 2) if ((f >= fc * 2 ** (-1 / 6)) & (f < fc * 2 ** (1 / 6))).any() else None for fc in MOD_FC]))


def sent_for(G, t, cal):
    """the CC11 curve (dB, quantised to 7 bits, <= 127) that makes the level change G: the one pole inverted
    (G + tau G'), lag earlier, / K"""
    x = (G + cal['tau'] * np.gradient(G, DT)) / cal['k']
    s = delay(t, x, -cal['lag'])
    s -= max(0.0, float(s.max()))
    v = np.clip(np.round(127 * 10 ** (s / 20)), 1, 127)
    return 20 * np.log10(v / 127.0)


def apply_measures(p, G):
    """the measures of play p with an extra level change G (dB, on p.t): swell by note (the perceived envelope:
    ewc.note_shapes), and the fluctuation strength by Bark region (specific loudness scaled by 10^(0.023 G):
    N' ~ E^0.23)"""
    import even_window_check as ewc
    m = p.m
    env = p.env + G
    sh = ewc.note_shapes(m, p.t, env)
    sw = [q['swell'] for q in sh]
    Gz = np.interp(p.tz, p.t, G, left=0.0, right=0.0)
    Ns = p.Ns * 10 ** (0.023 * Gz)[None, :]
    N = np.trapezoid(Ns, dx=float(p.bark[1] - p.bark[0]), axis=0) if hasattr(np, 'trapezoid') else Ns.sum(axis=0) * 0.1
    fz = fs_index(N, Ns, p.bark, p.tz, m['notes'][1]['on'], m['notes'][-1]['on'])
    out = dict(swellP75=float(np.percentile(sw[1:-1], 75)), fsLow=fz['regions']['0-12'], fsHigh=fz['regions']['12-24'],
               fs=fz['fsBands'])
    if m['key'] == 'figure':
        out['pos'] = ewc.by_position(m, sw)
    return out


def audio_measures(args):
    """the measures of a play with a level change G (dB on passage time t) applied to its audio (CC11 acts as a
    broadband gain: calibrate's r 0.99), loudness-matched and analysed as the cache does"""
    wav, m, p0, t, G = args
    import soundfile as sf
    import pyloudnorm
    from loudness import perceived_envelope
    from scipy.signal import resample_poly
    import even_window_check as ewc
    x, sr = sf.read(wav, always_2d=True)
    a, b = play_window(m, p0)
    seg = x[int(a * sr):int(b * sr)]
    ts = m['first'] - sst.LEAD + np.arange(len(seg)) / sr          # passage time of each sample
    seg = seg * (10 ** (np.interp(ts, t, G, left=0.0, right=0.0) / 20))[:, None]
    seg = seg * 10 ** ((LUFS - pyloudnorm.Meter(sr).integrated_loudness(seg)) / 20)
    env, first, hop = perceived_envelope(seg, sr)
    tenv = (first + np.arange(len(env)) * hop) / 1000.0 + m['first'] - sst.LEAD
    m48 = resample_poly(seg.mean(axis=1), 160, 147) if sr == 44100 else seg.mean(axis=1)
    N, Ns, bark, tz = zwicker(m48, 48000)
    tz = tz + m['first'] - sst.LEAD
    sw = [q['swell'] for q in ewc.note_shapes(m, t, np.interp(t, tenv, env))]
    fz = fs_index(N, Ns, bark, tz, m['notes'][1]['on'], m['notes'][-1]['on'])
    out = dict(swellP75=float(np.percentile(sw[1:-1], 75)), fsLow=fz['regions']['0-12'], fsHigh=fz['regions']['12-24'],
               fs=fz['fsBands'], swells=[float(s) for s in sw])
    if m['key'] == 'figure':
        out['pos'] = ewc.by_position(m, sw)
    return out


def play_source(f180, play_name, meta):
    """(wav, m, p0) of a cached play name <passage>_<version>[_t<k>]_p<i>"""
    stem, pi = play_name.rsplit('_p', 1)
    m = meta[stem.split('_')[0]]
    return os.path.join(f180, 'wav', stem + '.wav'), m, m['passes'][int(pi) - 1]


# --- the rule, rendered ---------------------------------------------------------------------------------------------

RULE_TAKES = {'180R': 3, '250R': 3, '250L': 3, '136R': 1, '136L': 1, '180L': 1}


def rule_curve(m, U, cal, alpha):
    """the rule's CC11 (sent dB on span(m)): the plays' repeatable within-slur loudness deviation (wiener of the
    perceived envelope less each slur's median), times -alpha, through the inverted CC11 response"""
    t = U[0].t
    wv, H = wiener([deviation(m, t, p.env) for p in U])
    return t, sent_for(-alpha * wv, t, cal), H


def rule_events(f180, plays_dir, sim_dir, work):
    import glob
    meta = jload(f180, 'meta.json')
    cal = jload(sim_dir, 'cal180R.json')
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    shutil_meta = os.path.join(work, 'meta.json')
    json.dump(meta, open(shutil_meta, 'w'))
    lines, info = [], {}
    for name, n in RULE_TAKES.items():
        m = meta[name]
        U = [Play(f, m) for f in sorted(glob.glob(os.path.join(plays_dir, f'{name}_U*_p*.npz')))]
        rows = jload(sim_dir, f'{name}.json')['rows']
        alpha = min((float(k.split('=')[1]) for k in rows if k.startswith('E a=')), key=lambda a: rows[f'E a={a}']['fs'])
        t, s, H = rule_curve(m, U, cal, alpha)
        ts = np.arange(-sst.CC_LEAD, m['end'] + 0.3, sst.STEP)
        ss = np.interp(ts, t, s, left=s[0], right=s[-1])
        ev, last = [], None
        for tt, x in zip(ts, ss):
            y = int(max(1, min(127, round(127 * 10 ** (x / 20)))))
            if y != last:
                ev.append((round(float(tt), 5), 'cc', 11, y))
                last = y
        sst.write_events(os.path.join(work, 'ev', f'{name}_R.txt'), sst.passes(m, m['base'] + ev))
        lines += [f'{name}_R_t{i}|{name}_R|{m["patch"]}' for i in range(1, n + 1)]
        info[name] = dict(alpha=alpha, plays=len(U), cc=len(ev), sentRange=[round(float(s.min()), 2), round(float(s.max()), 2)],
                          maxStepDbPer40ms=round(float(np.max(np.abs(ss[8:] - ss[:-8]))), 2))
        print(name, info[name], flush=True)
    with open(os.path.join(work, 'list1.txt'), 'w', newline='\r\n') as f:
        f.write('\n'.join(lines) + '\n')
    import even_180_check as e180
    with open(os.path.join(work, 'run.cmd'), 'w', newline='\r\n') as f:
        f.write(e180.RUN_CMD.replace('f180', 'rule').replace('even 180 check (branch even-180)', 'even rule check (branch even-rule)'))
    json.dump(info, open(os.path.join(work, 'rule.json'), 'w'), indent=1)


S_KEPT = {'136R': 'S2', '136L': 'S1', '180R': 'S2', '180L': 'S2', '250R': 'S1', '250L': 'S1'}   # sso_even_180_check.json


def real(f180, work, out_json):
    """the rule's real renders against U / C / S (the earlier renders), every pass: the same measures"""
    from multiprocessing import Pool
    import glob
    meta = jload(f180, 'meta.json')
    jobs, labels = [], []
    for name in RULE_TAKES:
        m = meta[name]
        t = span(m)
        zero = np.zeros(len(t))
        srcs = {'U': glob.glob(os.path.join(f180, 'wav', f'{name}_U*.wav')),
                'C': glob.glob(os.path.join(f180, 'wav', f'{name}_C*.wav')),
                'S': [w for w in glob.glob(os.path.join(f180, 'wav', f'{name}_{S_KEPT[name]}*.wav'))],
                'R': glob.glob(os.path.join(work, 'wav', f'{name}_R_t*.wav'))}
        for ver, wavs in srcs.items():
            for w in sorted(wavs):
                for p0 in m['passes']:
                    jobs.append((w, m, p0, t, zero))
                    labels.append((name, ver, os.path.basename(w)))
    with Pool(8) as pool:
        res = pool.map(audio_measures, jobs)
    out = {}
    for (name, ver, w), r in zip(labels, res):
        out.setdefault(name, {}).setdefault(ver, []).append(r)
    summ = {}
    for name, vs in out.items():
        summ[name] = {}
        for ver, rs in vs.items():
            s = {k: round(float(np.mean([r[k] for r in rs])), 3) for k in ('swellP75', 'fsLow', 'fsHigh', 'fs')}
            s['n'] = len(rs)
            if 'pos' in rs[0]:
                s['pos'] = [round(float(x), 2) for x in np.mean([r['pos'] for r in rs], axis=0)]
            summ[name][ver] = s
            print(f'{name} {ver} n {s["n"]:2d} swell p75 {s["swellP75"]:.2f}  FS low {s["fsLow"]:.3f} high {s["fsHigh"]:.3f} '
                  f'all {s["fs"]:.3f}  pos {s.get("pos", "")}', flush=True)
    allj = jload(out_json) if os.path.exists(out_json) else {}
    allj['real'] = summ
    json.dump(allj, open(out_json, 'w'), indent=1)


CLIP_PAIRS = [('p1', '180R', 't1'), ('p2', '250L', 't1'), ('p3', '250R', 't1'), ('p4', '180R', 't1')]   # p4 repeats p1


def clips(f180, work, out_dir, key_path):
    """blind pairs S / R (the rule), pass 1, 0.5 s before the first note-on to 1.5 s after the last note-off, at one
    integrated loudness per pair (the highest at or below -13 LUFS with every sample peak <= -1 dBFS), LAME 192 kbps;
    p4 = p1's clips under swapped letters; notes.json: each note's note-on, arrival, name, bowed (velocity 93) and
    place in its slur (0: the lead-in), in clip seconds, per passage"""
    import random
    import soundfile as sf
    import pyloudnorm
    meta = jload(f180, 'meta.json')
    os.makedirs(out_dir, exist_ok=True)
    rnd = random.Random(20261014)
    key, notes = {}, {}
    for pid, name, take in CLIP_PAIRS:
        m = meta[name]
        p0 = m['passes'][0]
        a = p0 + m['first'] - sst.LEAD
        b = p0 + max(n['off'] for n in m['notes']) + sst.TAIL
        sv = S_KEPT[name]
        s_w = os.path.join(f180, 'wav', f'{name}_{sv}_{take}.wav')
        if not os.path.exists(s_w):
            s_w = os.path.join(f180, 'wav', f'{name}_{sv}.wav')
        srcs = {'S': s_w, 'R': os.path.join(work, 'wav', f'{name}_R_{take}.wav')}
        if pid == 'p4':
            letters = {'a': key['p1']['b'], 'b': key['p1']['a']}
        else:
            vs = ['S', 'R']
            rnd.shuffle(vs)
            letters = {'a': vs[0], 'b': vs[1]}
        segs = {}
        for v, w in srcs.items():
            x, sr = sf.read(w, always_2d=True)
            segs[v] = x[int(a * sr):int(b * sr)]
        meter = pyloudnorm.Meter(sr)
        lufs = {v: meter.integrated_loudness(s) for v, s in segs.items()}
        peak = {v: 20 * np.log10(np.max(np.abs(s))) for v, s in segs.items()}
        target = min(-13.0, min(lufs[v] + (-1.0 - peak[v]) for v in segs))
        files = {}
        for l, v in letters.items():
            y = segs[v] * 10 ** ((target - lufs[v]) / 20)
            tmp = os.path.join(out_dir, f'{pid}{l}.wav')
            sf.write(tmp, y, sr, subtype='FLOAT')
            mp3 = os.path.join(out_dir, f'{pid}{l}.mp3')
            subprocess.run(['ffmpeg', '-y', '-loglevel', 'error', '-i', tmp, '-c:a', 'libmp3lame', '-b:a', '192k', mp3], check=True)
            os.remove(tmp)
            dec = decode(mp3, sr, 2)
            files[l] = dict(version=v, source=os.path.basename(srcs[v]), lufs=round(target, 2),
                            peakDecoded=round(float(20 * np.log10(np.max(np.abs(dec)))), 2))
        key[pid] = dict(passage=name, a=letters['a'], b=letters['b'], files=files,
                        repeatOf='p1 (letters swapped)' if pid == 'p4' else None)
        if name not in notes:
            pos, last, rows = 0, None, []
            for n in m['notes']:
                pos = 0 if n['slur'] == 0 else (pos + 1 if n['slur'] == last else 1)
                last = n['slur']
                c0 = m['first'] - sst.LEAD
                rows.append([round(n['on'] - c0, 3), round(n['arr'] - c0, 3), n['name'], 1 if n['v'] == 93 else 0, pos])
            notes[name] = rows
        print(pid, name, letters, files, flush=True)
    json.dump(notes, open(os.path.join(out_dir, 'notes.json'), 'w'), separators=(',', ':'))
    json.dump(key, open(key_path, 'w'), indent=1)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('wobble')
    p.add_argument('work_dir')
    p = sub.add_parser('cache')
    p.add_argument('f180')
    p.add_argument('out_dir')
    p.add_argument('names', nargs='*')
    p.add_argument('--workers', type=int, default=6)
    p = sub.add_parser('events')
    p.add_argument('f180')
    p.add_argument('plays_dir')
    p.add_argument('sim_dir')
    p.add_argument('work')
    p = sub.add_parser('real')
    p.add_argument('f180')
    p.add_argument('work')
    p = sub.add_parser('clips')
    p.add_argument('f180')
    p.add_argument('work')
    p.add_argument('out_dir')
    p.add_argument('key_path')
    a = ap.parse_args()
    if a.cmd == 'real':
        real(a.f180, a.work, OUT_JSON)
        return
    if a.cmd == 'clips':
        clips(a.f180, a.work, a.out_dir, a.key_path)
        return
    if a.cmd == 'events':
        rule_events(a.f180, a.plays_dir, a.sim_dir, a.work)
        return
    if a.cmd == 'cache':
        cache(a.f180, a.out_dir, a.names, a.workers)
        return
    if a.cmd == 'wobble':
        r = wobble(a.work_dir)
        allj = jload(OUT_JSON) if os.path.exists(OUT_JSON) else {}
        allj['wobble'] = r
        json.dump(allj, open(OUT_JSON, 'w'), indent=1)


if __name__ == '__main__':
    main()
