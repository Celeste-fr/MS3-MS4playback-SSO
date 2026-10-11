# Performance-in-context study: event files for kthost (one per section x patch x take) + index.json
# Rules copied from MuseScore's Recommended render of Whence (dump-rec 0-4 / 0-5): staccato = Spiccato (CC32 42),
# calibrated velocity, sent its map onset early, off at written + half its IOI; slur = Performance Legato: attack 112
# sent the Legato median onset early, transitions at written time (93, Violas 100), previous note off 24 ms after,
# re-attack 106 (same early), the note before it off 54 ms before the re-attack is sent (at least 1 ms long).
import json, os
W = os.environ.get('PCL_WORK', '.')  # work folder: ev/, r/, index.json
R = os.path.dirname(os.path.abspath(__file__)) + '/../../../share/soundlibraries/Spitfire Symphony Orchestra.dynamics.json'
DY = json.load(open(R))['patches']
SEC = {  # name: (perf median onset ms, spicc onset ms, transition vel, low, mid, high)
 'Violins 1': (50, 15, 93, 57, 76, 88), 'Violins 2': (40, 10, 93, 57, 74, 86), 'Violas': (87.5, 20, 100, 50, 67, 79),
 'Celli': (70, 20, 93, 38, 59, 71), 'Basses': (40, 15, 93, 30, 39, 51)}
SHORT = {'Violins 1': 'vn1', 'Violins 2': 'vn2', 'Violas': 'va', 'Celli': 'vc', 'Basses': 'cb'}
DS = [32, 64, 96, 112]; CCS = [16, 32, 48, 64, 80, 96, 112, 127]; IOIS = [0.100, 0.136, 0.200, 0.300, 0.500]


def at(c, x):
    if x <= c[0][0]:
        return c[0][1] + (c[1][1] - c[0][1]) / (c[1][0] - c[0][0]) * (x - c[0][0])
    for (a, ya), (b, yb) in zip(c, c[1:]):
        if x <= b:
            return ya + (yb - ya) * (x - a) / (b - a)
    return c[-1][1]


def inv(c, y):  # first crossing, linear between points, extrapolated under the first point
    if y <= c[0][1]:
        (a, ya), (b, yb) = c[0], c[1]
        return a + (y - ya) * (b - a) / (yb - ya) if yb != ya else a
    for (a, ya), (b, yb) in zip(c, c[1:]):
        if min(ya, yb) <= y <= max(ya, yb) and yb != ya:
            return a + (y - ya) * (b - a) / (yb - ya)
    return c[-1][0]


def cal(sec, val, d):
    perf = sec + ' - Performance' if val == '20' else sec
    ref = DY[sec]['1']['perceived']; c = DY[perf][val]['perceived']
    return max(1, min(127, round(inv(c, at(ref, d)))))


if __name__ == '__main__':
    idx = {}
    for sec, (pon, son, tv, *regs) in SEC.items():
        pon /= 1000; son /= 1000
        for take in 'AB':
            figs = [(d, ioi, r) for d in DS for ioi in IOIS for r in regs]
            if take == 'B': figs.reverse()
            t = 1.0; ev = [(0, '0.000000 cc 11 127'), (0, '0.000000 cc 32 42')]; notes = []
            for d, ioi, r in figs:
                v = cal(sec, '42', d); st = 2 * ioi
                ev.append((t - 0.5, f'{t-0.5:.6f} cc 1 {d}'))
                for k, p in enumerate([r, r - 2, r, r + 1, r, r - 2]):
                    w = t + k * st; on, off = w - son, w + st / 2
                    ev += [(on, f'{on:.6f} on {p} {v}'), (off, f'{off:.6f} off {p} 0')]
                    notes.append(dict(d=d, ioi=ioi, reg=r, k=k, on=on, off=off, vel=v))
                t += 6 * st + 1.5
            n = f'pcl_{SHORT[sec]}_st{take}'
            open(f'{W}/ev/{n}.txt', 'w').write('\r\n'.join(l for _, l in sorted(ev, key=lambda e: e[0])) + '\r\n')
            idx[n] = dict(sec=sec, patch=sec, kind='st', notes=notes, end=t)
            figs = [(c, ioi, r) for c in CCS for ioi in IOIS for r in regs]
            if take == 'B': figs.reverse()
            t = 1.0; ev = [(0, '0.000000 cc 11 127')]; notes = []
            for c, ioi, r in figs:
                ev.append((t - 0.6, f'{t-0.6:.6f} cc 1 {c}'))
                ps = [r, r - 2, r, r + 1, r + 2, r, r + 2, r + 3]; ws = [t + k * ioi for k in range(8)]
                ons = [ws[k] - pon if k in (0, 4) else ws[k] for k in range(8)]
                vels = [112 if k == 0 else 106 if k == 4 else tv for k in range(8)]
                offs = [t + 8 * ioi if k == 7 else max(ons[3] + 0.001, ons[4] - 0.054) if k == 3 else ons[k + 1] + 0.024
                        for k in range(8)]
                for k in range(8):
                    ev += [(ons[k], f'{ons[k]:.6f} on {ps[k]} {vels[k]}'), (offs[k] + 1e-7, f'{offs[k]:.6f} off {ps[k]} 0')]
                    notes.append(dict(cc=c, ioi=ioi, reg=r, k=k, w=ws[k], on=ons[k], off=offs[k], vel=vels[k]))
                t += 8 * ioi + 3.0
            n2 = f'pcl_{SHORT[sec]}_pf{take}'
            open(f'{W}/ev/{n2}.txt', 'w').write('\r\n'.join(l for _, l in sorted(ev, key=lambda e: e[0])) + '\r\n')
            idx[n2] = dict(sec=sec, patch=sec + ' - Performance', kind='pf', notes=notes, end=t)
    json.dump(idx, open(f'{W}/index.json', 'w'))
    tot = sum(v['end'] + 2 for v in idx.values())
    print(len(idx), 'renders', round(tot / 60, 1), 'min audio', round(tot * 0.35 / 1024, 2), 'GB')
    for s in SEC:
        print(s, 'spicc v', [cal(s, '42', d) for d in DS], 'perf cc', [cal(s, '20', d) for d in DS])
