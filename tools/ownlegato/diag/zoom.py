# zoom.py <out.png> <t_written> [half_width] : SSO take 1 at the written note-on vs ours (sent 80 ms early): waveform,
# spectrogram to 16 kHz, 2.5 ms RMS envelope
import sys, numpy as np
sys.path.insert(0, '/home/volensia/MuseScore/ms3fork/ms37/.claude/worktrees/agent-a7732e538165920fb/tools/ownlegato')
from common import *
import matplotlib; matplotlib.use('Agg'); import matplotlib.pyplot as plt
C = '/home/volensia/.cache/ownlegato/'
t = float(sys.argv[2]); hw = float(sys.argv[3]) if len(sys.argv) > 3 else 0.25
W = [('SSO 1', stereo(C + 'audio/diag/diag_p1.wav')), ('ours', stereo(C + 'dg/ours.wav'))]
fig, axs = plt.subplots(3, 2, figsize=(16, 9))
for c, (name, x) in enumerate(W):
    a, b = int((t - hw) * SR), int((t + hw) * SR)
    s = x[a:b]; tt = np.arange(len(s)) / SR + t - hw
    axs[0][c].plot(tt, s[:, 0], lw=0.4); axs[0][c].plot(tt, s[:, 1], lw=0.4, alpha=0.6); axs[0][c].set_title(name)
    axs[0][c].axvline(t, color='g'); axs[0][c].axvline(t - 0.08, color='r')
    axs[1][c].specgram(s.mean(1), NFFT=512, Fs=SR, noverlap=448, cmap='magma', vmin=-140, xextent=(t - hw, t + hw))
    axs[1][c].set_ylim(0, 16000)
    m = s.mean(1); n = int(0.0025 * SR)
    e = 20 * np.log10(np.sqrt(np.convolve(m ** 2, np.ones(n) / n, 'same')) + 1e-9)
    axs[2][c].plot(tt, e, lw=0.6); axs[2][c].set_ylim(e.max() - 30, e.max() + 2); axs[2][c].axvline(t, color='g'); axs[2][c].axvline(t - 0.08, color='r')
plt.tight_layout(); plt.savefig(sys.argv[1], dpi=60)
