"""align_dtw.py <config.json> <secPerBar candidates, e.g. 0.7,0.8,0.9> : frames -> score bars by DTW of score activity
(readings' notes spans, every part) against audio activity (every track), writes config['timing'] = {type: table}.
Steps (1,1) (1,2) (2,1) (tempo within x0.5 .. x2 of the candidate), the audio fully matched, the score open-ended."""
import sys, json, numpy as np, librosa
sys.path.insert(0, __file__.rsplit('/', 1)[0])
import balance as B

cfg = json.load(open(sys.argv[1]))
parts = B.read(cfg['readings'], lambda b: cfg.get('beats', 4), cfg.get('notesEndPad', 0.0))
env = np.load(cfg['env'])
F = max(len(env[t]) for ts in cfg['tracks'].values() for t in ts)
plist = [p for p in cfg['tracks'] if cfg.get('readAs', {}).get(p, p) in parts]
A = np.zeros((len(plist), F))
for i, p in enumerate(plist):
    for t in cfg['tracks'][p]:
        a, _ = B.activity(env[t]); A[i, :len(a)] = np.maximum(A[i, :len(a)], a)
nbars = max(b for p in plist for a, b in parts[cfg.get('readAs', {}).get(p, p)]['notes'])
best = None
for spb in [float(x) for x in sys.argv[2].split(',')]:
    step = B.FRAME / spb                      # bars per score step
    G = np.arange(0, max(nbars, 1.3 * F * step), step)     # padded with silence past the readings' end
    S = np.zeros((len(plist), len(G)))
    for i, p in enumerate(plist):
        for a, b in parts[cfg.get('readAs', {}).get(p, p)]['notes']:
            S[i, (G >= a) & (G < b)] = 1
    # cost: fraction of parts disagreeing
    C = (np.abs(A.T[:, None, :] - S.T[None, :, :])).mean(axis=2) if len(G) * F < 4e6 else None
    if C is None:
        C = np.zeros((F, len(G)))
        for k in range(0, F, 200):
            C[k:k + 200] = np.abs(A.T[k:k + 200, None, :] - S.T[None, :, :]).mean(axis=2)
    D, wp = librosa.sequence.dtw(C=C, subseq=True, step_sizes_sigma=np.array([[1, 1], [1, 2], [2, 1]]))
    cost = D[-1, wp[0, 1]] / F
    wp = wp[::-1]
    print('secPerBar %.3f cost %.4f score bars %.1f..%.1f' % (spb, cost, G[wp[0, 1]], G[wp[-1, 1]]), flush=True)
    if best is None or cost < best[0]:
        best = (cost, spb, wp, G)
cost, spb, wp, G = best
fr, gi = wp[:, 0], wp[:, 1]
times = (fr + 0.5) * B.FRAME
bars = G[gi]
# one bar value per frame (median of the path's), smoothed monotone
u = np.unique(fr)
tb = np.array([np.median(bars[fr == x]) for x in u])
tb = np.maximum.accumulate(tb)
cfg['timing'] = {'type': 'table', 'times': list(np.round((u + 0.5) * B.FRAME, 3)), 'bars': list(np.round(tb, 4)),
                 'secPerBar': spb, 'cost': round(float(cost), 4)}
json.dump(cfg, open(sys.argv[1], 'w'), indent=1)
print('chosen', spb, cost, 'bars covered %.1f..%.1f' % (tb[0], tb[-1]))
