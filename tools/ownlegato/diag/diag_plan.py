# diag_plan.py <out dir>: designed 12-TET phrases for the defect hunt (SSO via kthost, ours via sfizz with the same notes)
import sys, os
sys.path.insert(0, '/home/volensia/MuseScore/ms3fork/ms37/.claude/worktrees/agent-a7732e538165920fb/tools/ownlegato')
import record as R
out = sys.argv[1]
os.makedirs(out, exist_ok=True)
f = R.File('diag')
P = []
def ph(name, keys, ioi, vel, cc1, last=1.5, first=None):
    s = f.chain(keys, 5, vel, cc1, kind='diag', hold=ioi, attack_hold=first or ioi, holds=[ioi] * (len(keys) - 2) + [last],
                name=name, ioi=ioi, cc1v=cc1)
    P.append(name)
for cc1 in (64, 32, 100, 48):
    ph('slow_up_%d' % cc1, [57, 59, 61, 62, 64, 66], 0.6, 69, cc1)
ph('slow_down', [66, 64, 62, 61, 59, 57], 0.6, 69, 64)
ph('fast_scale', [57, 59, 61, 62, 64, 66, 64, 62, 61, 59, 57], 0.136, 69, 64, first=0.273)
ph('fast_turns', [61, 62, 61, 59, 61, 62, 64, 62, 61], 0.136, 69, 64, first=0.273)
ph('eighths', [57, 61, 64, 62, 59, 62, 57], 0.273, 69, 64)
ph('leaps', [57, 64, 55, 62, 69, 60], 0.6, 69, 64)
ph('bowed_slow', [59, 61, 62, 64, 62], 0.6, 100, 64)
ph('bowed_fast', [59, 61, 62, 64, 62, 61, 59], 0.136, 100, 64, first=0.273)
ph('repeated', [62, 62, 62, 64, 64, 62], 0.4, 69, 64)
ph('long_hold', [60, 62], 0.6, 69, 64, last=4.0)
ph('single', [62], 3.0, 69, 64)
ph('quiet_fast', [57, 59, 61, 62, 64, 62, 61, 59], 0.136, 69, 32, first=0.273)
ph('loud_slow', [62, 64, 66, 67, 64], 0.6, 69, 100)
total = f.write(out)
open(os.path.join(out, 'list.txt'), 'w').write('diag|Violas - Performance\n')
print(len(P), 'phrases', total, 's')
