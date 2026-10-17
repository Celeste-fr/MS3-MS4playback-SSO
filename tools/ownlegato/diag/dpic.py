# dpic.py <out.png> <seg>[,<seg>] [fmax] : SSO take 1 / SSO take 2 / ours, per phrase, spectrogram + envelope + f0
import sys, json, numpy as np
sys.path.insert(0, '/home/volensia/MuseScore/ms3fork/ms37/.claude/worktrees/agent-a7732e538165920fb/tools/ownlegato')
from common import *
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
C = '/home/volensia/.cache/ownlegato/'
J = json.load(open(C + 'diag/diag.json'))
segs = [int(s) for s in sys.argv[2].split(',')]
fmax = float(sys.argv[3]) if len(sys.argv) > 3 else 5000
W = [('SSO 1', mono(C + 'audio/diag/diag_p1.wav')), ('SSO 2', mono(C + 'audio/diag/diag_p2.wav')), ('ours', mono(C + 'dg/ours.wav'))]
fig, axs = plt.subplots(len(W) * len(segs), 1, figsize=(16, 3.0 * len(W) * len(segs)))
k = 0
for sg in segs:
    s = J['segments'][sg]; ns = s['notes']
    t0, t1 = ns[0]['on'] - 0.4, ns[-1]['off'] + 1.2
    for name, x in W:
        ax = axs[k]; k += 1
        seg = x[int(t0 * SR):int(t1 * SR)]
        ax.specgram(seg, NFFT=2048, Fs=SR, noverlap=1792, cmap='magma', vmin=-130, xextent=(t0, t1))
        ax.set_ylim(0, fmax)
        te, e = env_db(seg); a3 = ax.twinx(); a3.plot(te + t0, e, 'w-', lw=1); a3.set_ylim(-90, -10)
        for n in ns: ax.axvline(n['on'], color='g', lw=0.7)
        ax.axvline(ns[-1]['off'], color='r', lw=0.7)
        ax.set_title('%s  %s  ioi %.3f cc1 %d' % (name, s['name'], s['ioi'], s['cc1']), fontsize=9)
plt.tight_layout(); plt.savefig(sys.argv[1], dpi=55)
