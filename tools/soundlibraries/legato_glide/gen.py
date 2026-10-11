# gen.py <work dir>: kthost event files for the legato-glide and bend-smoothing measurement of SSO's five string
# sections' Performance patches (Kontakt 8 + SSO on the Windows VM; render each with
#   kthost.exe "Kontakt 8.vst3" "<patch>.vst3state" <name>.txt <name>.wav --tail 2
# the patch's state as MuseScore made it). Writes <work>/ev/*.txt and <work>/index.json.
# - runs_<sec>: slurred runs as MuseScore plays Whence (Recommended): attack 112, transitions 93 (Violas 100: the
#   map's legatoVelocity; 93 is the bowed transition, performancetechnique.h), the note before off 24 ms after the
#   next note-on (the map's overlap), CC1 64, CC11 127, no pitch bend. A run: p, p+i, p, p+i, p, p+i at IOI 136 ms
#   (Whence's sixteenths at 110 bpm), 300 and 600 ms, i in -2 -1 +1 +2, p three registers per section (the
#   performance-context study's, ../performance_context/gen.py), 1.5 s between runs.
# - probe_<sec>: one held note; the bend stepped 0 -> +50 -> 0 -> -50 -> 0 cents 1.5 s apart, then six full-range
#   moves (full range until a one pole of 71.35 ms, the 2026-10-10 measurement, would reach the target, then the
#   target), for the patch's bend smoothing (fit.py).
import json, math, os, sys

W = sys.argv[1] if len(sys.argv) > 1 else '.'
SEC = {'vn1': ('Violins 1 - Performance', 93, (57, 76, 88), 69, 99.1),
       'vn2': ('Violins 2 - Performance', 93, (57, 74, 86), 69, 99.7),
       'va': ('Violas - Performance', 100, (50, 67, 79), 57, 99.6),
       'vc': ('Celli - Performance', 93, (38, 59, 71), 48, 99.8),
       'cb': ('Basses - Performance', 93, (30, 39, 51), 40, 99.1)}
IOIS = (0.136, 0.300, 0.600)
PROBE_RANGE = 99.6          # (the probes' bend values were made with the Violas' range for every patch; fit.py reads
                            # each with its own patch's range)
os.makedirs(f'{W}/ev', exist_ok=True)


def bend(c, R=PROBE_RANGE):
    return int(round(8192 + c / R * (8192 if c < 0 else 8191)))


idx = {}
for s, (patch, tv, regs, probePitch, rng) in SEC.items():
    t = 0.5; ev = [(0, '0.000000 cc 1 64'), (0, '0.000000 cc 11 127')]; notes = []
    for ioi in IOIS:
        for r in regs:
            for i in (-2, -1, 1, 2):
                ps = [r, r + i, r, r + i, r, r + i]
                for k, p in enumerate(ps):
                    on = t + k * ioi
                    off = on + ioi + 0.024 if k + 1 < len(ps) else on + ioi
                    ev += [(on, f'{on:.6f} on {p} {112 if k == 0 else tv}'), (off + 1e-7, f'{off:.6f} off {p} 0')]
                    notes.append(dict(on=on, off=off, p=p, prev=ps[k - 1] if k else None, ioi=ioi, reg=r,
                                      interval=(p - ps[k - 1]) if k else 0, k=k))
                t += len(ps) * ioi + 1.5
    open(f'{W}/ev/runs_{s}.txt', 'w').write('\r\n'.join(l for _, l in sorted(ev, key=lambda e: e[0])) + '\r\n')
    tau = 0.07135
    pe = ['0.000000 cc 1 64', '0.000000 cc 11 127', '0.000000 pb 8192 0', f'0.500000 on {probePitch} 112']
    for tt, c in ((2.0, 50), (3.5, 0), (5.0, -50), (6.5, 0)):
        pe.append(f'{tt:.6f} pb {bend(c)} 0')
    for tt, y0, y1 in ((8.0, 0, 50), (9.5, 50, 0), (11.0, 0, -50), (12.5, -50, 0), (14.0, 0, 50), (15.5, 50, -50)):
        u = PROBE_RANGE if y1 > y0 else -PROBE_RANGE
        T = tau * math.log((u - y0) / (u - y1))
        pe += [f'{tt:.6f} pb {min(16383, bend(u))} 0', f'{tt + T:.6f} pb {bend(y1)} 0']
    pe.append(f'17.500000 off {probePitch} 0')
    open(f'{W}/ev/probe_{s}.txt', 'w').write('\r\n'.join(pe) + '\r\n')
    idx[s] = dict(patch=patch, range=rng, probePitch=probePitch, notes=notes, end=t)
json.dump(idx, open(f'{W}/index.json', 'w'))
