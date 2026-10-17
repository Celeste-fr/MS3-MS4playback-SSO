# arrive.py: per diag phrase, each transition's pitch arrival (f0 past the midpoint of the interval) against the written
# note-on, SSO take 1 vs ours: offset and spread (lumpy timing)
import numpy as np, sys, json
sys.path.insert(0, '/home/volensia/MuseScore/ms3fork/ms37/.claude/worktrees/agent-a7732e538165920fb/tools/ownlegato')
from common import *
C = '/home/volensia/.cache/ownlegato/'
J = json.load(open(C + 'diag/diag.json'))
X = {k: mono(C + f).astype(float) for k, f in (('sso1', 'audio/diag/diag_p1.wav'), ('ours', 'dg/ours.wav'))}
for si, s in enumerate(J['segments']):
    ns = s['notes']
    if len(ns) < 2:
        continue
    lo = min(n['key'] for n in ns) - 2; hi = max(n['key'] for n in ns) + 2
    res = {}
    for k, x in X.items():
        a = ns[0]['on'] - 0.1; b = ns[-1]['off']
        t, m = f0(x[int(a * SR):int(b * SR)], lo, hi, resolution=0.005); t = t + a
        arr = []
        for i in range(1, len(ns)):
            ka, kb = ns[i - 1]['key'], ns[i]['key']
            if ka == kb:
                arr.append(np.nan); continue
            mid = (ka + kb) / 2; w = (t > ns[i]['on'] - 0.12) & (t < ns[i]['on'] + min(0.5, ns[i]['on'] - ns[i-1]['on'] + 0.1))
            tt, mm = t[w], m[w]
            c = np.where(np.isfinite(mm) & ((mm - mid) * np.sign(kb - ka) > 0))[0]
            arr.append((tt[c[0]] - ns[i]['on']) * 1000 if len(c) else np.nan)
        res[k] = np.array(arr)
    d = res['ours'] - res['sso1']; print(s['name'], 'found', {k: int(np.isfinite(v).sum()) for k, v in res.items()}, 'of', len(ns) - 1, [None if np.isnan(v) else int(v) for v in res['ours']][:8], [None if np.isnan(v) else int(v) for v in res['sso1']][:8])
    print('%-12s ioi %.3f ours-sso %+.0f sd %.0f max %.0f | sso med %.0f sd %.0f | ours med %.0f sd %.0f' % (
        s['name'], s['ioi'], np.nanmedian(d), np.nanstd(d), np.nanmax(np.abs(d)), np.nanmedian(res['sso1']), np.nanstd(res['sso1']), np.nanmedian(res['ours']), np.nanstd(res['ours'])))
