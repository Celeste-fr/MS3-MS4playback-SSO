# ana.py <work dir> [out.json]: per transition of gen.py's runs (<work>/r/<name>.wav, <work>/index.json) the f0 (YIN as
# legato_glide/f0.py, 2.5 ms steps, 30 ms frames, 5-frame median) against the WRITTEN pitch (quarter tones included):
#   best  the closest approach (cents, |.|) between the note-on and the next note-on, as the heard check's UNHEARD rule
#         (3 frames within 30 cents counts as reached);
#   end   the median error over the note's last 40 % (signed toward the move: - short of the written pitch, + beyond);
#   hit   reached by the heard check's rule (3 consecutive frames within 30 cents before the next note-on).
# Prints per name x IOI: the medians of best and |end| and the share reached; writes out.json with every row.
import json, os, sys
import numpy as np, soundfile as sf
from scipy.signal import medfilt
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'legato_glide'))
from f0 import yin_track, hz

W = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else f'{W}/ana.json'
idx = json.load(open(f'{W}/index.json'))
rows = {}
for name, v in sorted(idx.items()):
    fn = f'{W}/r/{name}.wav'
    if not os.path.exists(fn):
        print(name, 'missing'); continue
    a, sr = sf.read(fn); a = a.mean(1) if a.ndim > 1 else a
    rs = []
    for n in v['notes']:
        if n['k'] == 0:
            continue
        lo, hi = min(n['target'], n['prev']) - 2.5, max(n['target'], n['prev']) + 2.5
        t, f = yin_track(a, sr, n['on'], n['next'], hz(lo), hz(hi), step=0.0025, win=0.030)
        c = 1200 * np.log2(f / hz(n['target'])); ok = ~np.isnan(c)
        if ok.sum() < 8:
            rs.append(dict(ioi=n['ioi'], take=n['take'], k=n['k'], best=None, end=None, hit=False)); continue
        c = medfilt(np.interp(t, t[ok], c[ok]), 5)
        within = np.abs(c) <= 30
        hit = any(within[i:i + 3].all() for i in range(len(c) - 2))
        tail = c[int(len(c) * 0.6):]
        rs.append(dict(ioi=n['ioi'], take=n['take'], k=n['k'], best=round(float(np.min(np.abs(c))), 1),
                       end=round(float(np.median(tail) * np.sign(n['target'] - n['prev'])), 1), hit=bool(hit)))
    rows[name] = rs
    for ioi in sorted({r['ioi'] for r in rs}):
        g = [r for r in rs if r['ioi'] == ioi and r['best'] is not None]
        if not g:
            continue
        print(f"{name:8s} {int(ioi * 1000):4d} ms  best {np.median([r['best'] for r in g]):6.1f}  |end| "
              f"{np.median([abs(r['end']) for r in g]):6.1f}  end {np.median([r['end'] for r in g]):+7.1f}  "
              f"reached {sum(r['hit'] for r in g)}/{len(g)}")
json.dump(rows, open(OUT, 'w'))
