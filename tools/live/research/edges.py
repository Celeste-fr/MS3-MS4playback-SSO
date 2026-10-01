#!/usr/bin/env python3
# edges.py <wav> [bpm]: a lane stepping the level every beat (beats 1-15), then a ramp (16-24); prints when each step
# lands against its beat (halfway between the levels before and after) and the ramp levels. LIVE.md › Live's export and freeze.
import numpy as np, soundfile as sf, sys
x, sr = sf.read(sys.argv[1]); x = x.mean(axis=1) if x.ndim>1 else x
beat=60/float(sys.argv[2] if len(sys.argv) > 2 else 90)
# envelope via short RMS (1 ms hop, 2 ms window), sine at 440 Hz -> use a 5 ms window
hop=int(0.0005*sr); win=int(0.005*sr)
r=np.array([np.sqrt((x[i:i+win]**2).mean()) for i in range(0,len(x)-win,hop)]); t=np.arange(len(r))*hop/sr+win/2/sr
print('len %.2f s, peak %.3f' % (len(x)/sr, np.abs(x).max()))
errs=[]
for k in range(1,16):
    b=k*beat; pre=np.median(r[(t>b-0.25)&(t<b-0.05)]); post=np.median(r[(t>b+0.05)&(t<b+0.25)])
    mid=(pre+post)/2; sel=np.where((t>b-0.1)&(t<b+0.1))[0]
    cr=[t[i] for i in sel if (r[i]-mid)*(post-pre)>0]
    e=(cr[0]-b)*1000 if cr else float('nan'); errs.append(e if abs(np.log(post/pre))>0.5 else float('nan'))
    print('beat %2d: level %.4f -> %.4f (ratio %.2f), halfway at %+6.2f ms' % (k, pre, post, post/pre if pre else 0, e))
ratios=[]
e=np.array(errs); print('edges: mean %+.2f ms, sd %.2f ms, max |%.2f| ms' % (np.nanmean(e), np.nanstd(e), np.nanmax(np.abs(e))))
# the ramp 16-24 beats: level should rise monotonically
seg=[np.median(r[(t>(16+i)*beat+0.05)&(t<(17+i)*beat-0.05)]) for i in range(8)]
print('ramp levels per beat 16-24:', ' '.join('%.4f'%v for v in seg))
