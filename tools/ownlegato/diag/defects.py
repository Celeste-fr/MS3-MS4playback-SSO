# defects.py: per join of the diag phrases, SSO take 1 / take 2 / ours, the candidate defects at the splice (ours: our
# note-on = written - 80 ms; SSO: its note-on = written). Prints per phrase and a summary.
import sys, json, numpy as np
sys.path.insert(0, '/home/volensia/MuseScore/ms3fork/ms37/.claude/worktrees/agent-a7732e538165920fb/tools/ownlegato')
from common import *
from scipy.signal import butter, sosfilt
C = '/home/volensia/.cache/ownlegato/'
J = json.load(open(C + 'diag/diag.json'))
LT = float(sys.argv[1]) if len(sys.argv) > 1 else 80.0
OURS = sys.argv[2] if len(sys.argv) > 2 else C + 'dg/ours.wav'
W = {'sso1': stereo(C + 'audio/diag/diag_p1.wav'), 'sso2': stereo(C + 'audio/diag/diag_p2.wav'), 'ours': stereo(OURS)}
edges = 100 * 2 ** (np.arange(0, 22) / 3)          # 1/3-octave bands 100 Hz .. 16 kHz
def bands(x):
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), 8192)) ** 2
    f = np.fft.rfftfreq(8192, 1 / SR)
    return np.array([10 * np.log10(X[(f >= lo) & (f < lo * 2 ** (1 / 3))].sum() + 1e-20) for lo in edges])
def rms_db(x):
    return 10 * np.log10(np.mean(x ** 2) + 1e-20)
def win(x, t, a, b):
    return x[int((t + a) * SR):int((t + b) * SR)]
hp8 = {k: sosfilt(butter(4, 8000, 'highpass', fs=SR, output='sos'), v.mean(1)) for k, v in W.items()}
rows = []
for si, s in enumerate(J['segments']):
    ns = s['notes']
    for i in range(1, len(ns)):
        for k, x in W.items():
            t = ns[i]['on'] - (LT / 1000 if k == 'ours' else 0)
            m = x.mean(1)
            pre, post = win(m, t, -0.045, -0.005), win(m, t, 0.020, 0.060)
            lvl = rms_db(post) - rms_db(pre)
            bj = bands(post) - bands(pre); bj -= np.mean(bj)
            hf = rms_db(win(hp8[k], t, 0.020, 0.060)) - rms_db(win(hp8[k], t, -0.045, -0.005))
            # click: largest |2nd difference| within +-5 ms of the splice against its median over +-60 ms
            d2 = np.abs(np.diff(win(m, t, -0.06, 0.06), 2)); c = len(d2) // 2; n5 = int(0.005 * SR)
            click = 20 * np.log10(d2[c - n5:c + n5].max() / (np.median(d2) + 1e-12))
            # crossfade-window dip: min 5 ms RMS in [t, t+40] against the line from pre to post
            w5 = int(0.005 * SR)
            mid = [rms_db(m[j:j + w5]) for j in range(int(t * SR), int((t + 0.04) * SR), w5 // 2)]
            dip = min(mid) - (rms_db(pre) + rms_db(post)) / 2
            rows.append(dict(seg=si, name=s['name'], i=i, who=k, lvl=lvl, bandJump=float(np.sqrt(np.mean(bj ** 2))),
                             bandMax=float(np.abs(bj).max()), hf=hf, click=click, dip=dip))
json.dump(rows, open(OURS.rsplit('/', 1)[0] + '/defects.json', 'w'), default=float)
K = ['lvl', 'bandJump', 'bandMax', 'hf', 'click', 'dip']
print('%-6s' % 'who' + ''.join('%22s' % k for k in K))
for who in ('sso1', 'sso2', 'ours'):
    R = [r for r in rows if r['who'] == who]
    print('%-6s' % who + ''.join('%22s' % ('|%.1f| p90 %.1f max %.1f' % (np.median(np.abs([r[k] for r in R])),
          np.percentile(np.abs([r[k] for r in R]), 90), np.abs([r[k] for r in R]).max())) for k in K))
