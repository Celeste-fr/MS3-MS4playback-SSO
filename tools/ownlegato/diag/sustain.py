# sustain.py: per note of the diag phrases (settled part: written on + 0.25 s .. next written on - 0.02, notes >= 0.4 s
# only), SSO take 1 / 2 / ours: detune (median f0 - key, cents), vibrato extent (SD of detrended f0, cents), harmonic
# flutter (time-SD of harmonics 1-16's level, dB, 20 ms frames), inharmonic floor (energy between harmonics / on them, dB);
# each phrase's release: level at +0.1 / +0.3 / +0.6 / +1.0 s after the last note-off against the last 0.2 s before it.
import sys, json, numpy as np
sys.path.insert(0, '/home/volensia/MuseScore/ms3fork/ms37/.claude/worktrees/agent-a7732e538165920fb/tools/ownlegato')
from common import *
C = '/home/volensia/.cache/ownlegato/'
J = json.load(open(C + 'diag/diag.json'))
OURS = sys.argv[1] if len(sys.argv) > 1 else C + 'dg/ours.wav'
W = {'sso1': mono(C + 'audio/diag/diag_p1.wav').astype(float), 'sso2': mono(C + 'audio/diag/diag_p2.wav').astype(float),
     'ours': mono(OURS).astype(float)}
def harm(x, f0hz, nh=16):
    N = 2048; H = int(0.02 * SR)
    fr = np.array([x[k:k + N] * np.hanning(N) for k in range(0, len(x) - N, H)])
    S = np.abs(np.fft.rfft(fr, 8192, axis=1)) ** 2; f = np.fft.rfftfreq(8192, 1 / SR)
    hl, between = [], []
    for h in range(1, nh + 1):
        on = (f > f0hz * (h - 0.15)) & (f < f0hz * (h + 0.15))
        off = (f > f0hz * (h + 0.35)) & (f < f0hz * (h + 0.65))
        hl.append(10 * np.log10(S[:, on].sum(1) + 1e-20)); between.append(S[:, off].sum(1).mean() / (S[:, on].sum(1).mean() + 1e-20))
    hl = np.array(hl)
    return float(np.mean(np.std(hl, 1))), float(10 * np.log10(np.mean(between)))
out = []
for si, s in enumerate(J['segments']):
    ns = s['notes']
    for i, n in enumerate(ns):
        end = ns[i + 1]['on'] if i + 1 < len(ns) else n['off']
        if end - n['on'] < 0.4:
            continue
        for k, x in W.items():
            a, b = n['on'] + 0.25, end - 0.02
            seg = x[int(a * SR):int(b * SR)]
            t, m = f0(seg, n['key'] - 2, n['key'] + 3, resolution=0.02)
            m = m[np.isfinite(m)]
            if len(m) < 10:
                continue
            det = (np.median(m) - n['key']) * 100
            k5 = max(3, int(0.2 * SR / 128)) | 1
            trend = np.convolve(m, np.ones(k5) / k5, 'same')
            vib = float(np.std((m - trend)[k5:-k5]) * 100) if len(m) > 2 * k5 + 5 else float('nan')
            fl, flo = harm(seg, 440 * 2 ** ((n['key'] - 69) / 12))
            out.append(dict(seg=si, name=s['name'], i=i, who=k, dur=round(b - a, 2), det=det, vib=vib, flutter=fl, floor=flo))
    off = ns[-1]['off']
    for k, x in W.items():
        ref = 10 * np.log10(np.mean(x[int((off - 0.2) * SR):int(off * SR)] ** 2))
        dec = [10 * np.log10(np.mean(x[int((off + d) * SR):int((off + d + 0.05) * SR)] ** 2) + 1e-20) - ref for d in (0.1, 0.3, 0.6, 1.0)]
        out.append(dict(seg=si, name=s['name'], who=k, release=dec))
json.dump(out, open(C + 'dg/sustain.json', 'w'), default=float)
for k in ('sso1', 'sso2', 'ours'):
    R = [r for r in out if r['who'] == k and 'det' in r]
    Rr = [r for r in out if r['who'] == k and 'release' in r]
    print('%-5s notes %d | detune |med| %.1f max %.1f | vib SD %.1f | flutter %.2f dB (p90 %.2f) | floor %.1f dB | release +0.1/0.3/0.6/1.0 s %s'
          % (k, len(R), np.median([abs(r['det']) for r in R]), max(abs(r['det']) for r in R), np.nanmedian([r['vib'] for r in R]),
             np.median([r['flutter'] for r in R]), np.percentile([r['flutter'] for r in R], 90), np.median([r['floor'] for r in R]),
             ' '.join('%.1f' % v for v in np.median([r['release'] for r in Rr], 0))))
for nm in ('slow_up_64', 'slow_up_48', 'slow_up_32', 'slow_up_100', 'long_hold', 'single'):
    print(nm, ' '.join('%s flutter %.2f floor %.1f det %.1f' % (k, np.median([r['flutter'] for r in out if r.get('name') == nm and r['who'] == k and 'det' in r]),
                                                     np.median([r['floor'] for r in out if r.get('name') == nm and r['who'] == k and 'det' in r]),
                                                     np.median([r['det'] for r in out if r.get('name') == nm and r['who'] == k and 'det' in r])) for k in W))
