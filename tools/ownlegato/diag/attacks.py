# attacks.py: each diag phrase's first note (after silence): onset (envelope first within 20 dB of its 0.1-0.4 s level)
# against the written note-on, and level over 0.1-0.4 s, SSO take 1 vs ours
import numpy as np, sys, json
sys.path.insert(0, '/home/volensia/MuseScore/ms3fork/ms37/.claude/worktrees/agent-a7732e538165920fb/tools/ownlegato')
from common import *
C = '/home/volensia/.cache/ownlegato/'
J = json.load(open(C + 'diag/diag.json'))
X = {k: mono(C + f).astype(float) for k, f in (('sso1', 'audio/diag/diag_p1.wav'), ('ours', 'dg/ours.wav'))}
n5 = int(0.005 * SR)
for s in J['segments']:
    n = s['notes'][0]; out = []
    for k, x in X.items():
        sh = 0.21 if k == 'sso1' else 0.0   # SSO heard L_a after its note-on: compare at the same heard time
        a = int((n['on'] + sh - 0.3) * SR)
        e = 10 * np.log10(np.convolve(x[a:a + int(0.8 * SR)] ** 2, np.ones(n5) / n5, 'same') + 1e-20)
        end = min(0.4, (s['notes'][1]['on'] - n['on']) if len(s['notes']) > 1 else 0.4)
        lv = 10 * np.log10(np.mean(x[int((n['on'] + sh + 0.1) * SR):int((n['on'] + sh + end) * SR)] ** 2))
        on = (np.argmax(e > lv - 20) / SR - 0.3) * 1000
        out.append('%s onset %+4.0f ms level %.1f' % (k, on, lv))
    print('%-12s' % s['name'], ' | '.join(out))
