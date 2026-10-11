# the fit at Whence's figures (D 64, the slurs' median IOI and pitch), linear in pitch between the registers and in
# log IOI between the grid; 'need' = staccatos - slurs + 1.3 (dB over the calibrated CC1)
import json, os, sys
import numpy as np
W = os.environ.get('PCL_WORK', '.'); sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen import SEC, IOIS
M = sys.argv[1] if len(sys.argv) > 1 else 'L'
F = json.load(open(f'{W}/fit_{M}.json'))


def pred(sec, d, ioi, p, key='need'):
    regs = SEC[sec][3:]
    def at_r(r):
        return np.interp(np.log(ioi), np.log(IOIS), [F[sec][f'{d}|{i}|{r}'][key] for i in IOIS])
    return np.interp(p, regs, [at_r(r) for r in regs])


def need_a(sec, d, ioi, p):  # slurs from silence (attack, k 0-3) only
    return pred(sec, d, ioi, p, 'st') - pred(sec, d, ioi, p, 'a') + 1.3


cases = [  # part, bars, median pitch of the slurred notes, median IOI, MuseScore's own Whence st-sl (kthost render)
    ('Violins 1', '13-16', 75.5, 0.136, 6.5), ('Violas', '9-12', 63.5, 0.136, 5.0), ('Violas', '13-16', 63.5, 0.136, 3.4)]
print('same-part context: MuseScore Recommended Whence (st - sl) against the fit (st - sl = need - 1.3)')
for sec, bars, p, ioi, ms in cases:
    a = need_a(sec, 64, ioi, p) - 1.3; f = pred(sec, 64, ioi, p) - 1.3
    print(f'  {sec:9s} {bars:5s}: Whence {ms:4.1f}  fit (slurs from silence) {a:4.1f}  resid {ms - a:+4.1f}   fit (all) {f:4.1f}')
own = [('Violins 1', '1-12', 76, 10.4), ('Violins 1', '13-16', 75.5, 3.9), ('Violas', '3-12', 57, 4.8), ('Celli', '1-8', 53, 6.2)]
print("the owner's R (beyond calibration, R-based) against the fit's need (D 64, IOI 136)")
for sec, bars, p, o in own:
    n = need_a(sec, 64, 0.136, p)
    print(f'  {sec:9s} {bars:5s}: owner {o:4.1f}  fit {n:4.1f}  resid {o - n:+4.1f}')
print("the owner's R faders (Performance minus All techniques, both at CC1 64) against the fit's at CC1 64 (D 64, IOI 136)")
for sec, bars, p, o in [('Violins 1', '1-12', 76, 13.1), ('Violins 1', '13-16', 75.5, 6.6), ('Violas', '3-12', 57, 8.6),
                        ('Violas', '9-12', 63.5, 8.6), ('Celli', '1-8', 53, 14.0)]:
    n = pred(sec, 64, 0.136, p, 'st') - pred(sec, 64, 0.136, p, 'a64') + 1.3
    g = pred(sec, 64, 0.136, p, 'a') - pred(sec, 64, 0.136, p, 'a64')
    print(f'  {sec:9s} {bars:5s}: owner {o:4.1f}  fit {n:4.1f}  resid {o - n:+4.1f}   (in context CC1 64 -> calibrated: {g:+.1f} dB)')
