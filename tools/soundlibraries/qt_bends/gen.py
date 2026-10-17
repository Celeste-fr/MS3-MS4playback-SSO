# gen.py <work dir>: kthost event files separating why SSO's Performance patches miss Whence's quarter tones in fast slurs
# (Kontakt 8 + SSO on the Windows VM; render each with
#   kthost.exe "Kontakt 8.vst3" "<patch>.vst3state" <name>.txt <name>.wav --tail 2
# the patch's state as MuseScore made it). Writes <work>/ev/*.txt, <work>/list.txt and <work>/index.json.
# Runs of 8 slurred notes alternating two written pitches (an attack at 112, then transitions at the map's
# legatoVelocity, the note before off the map's overlap 24 ms after the next note-on, CC1 64, CC11 127), at IOI 136 ms
# (Whence's sixteenths), 200, 300 and 600, each twice (round robins), 2 s of silence between runs (SSO's legato resets
# after 1.5 s). Variants of the same written pitches:
#   N  the spelled keys, no bend (SSO's own transitions, 12-TET);
#   K  the spelled keys, bends as MuseScore sends them (libraryPitchBends: the measured bendGlide / bendSmoothing,
#      reimplemented here);
#   S  the spelled keys, each note's bend as a step at its note-on;
#   C  keys chosen so that every note of the run takes one bend (a -50 note one key down at +50), held constant;
#   T  C's keys, no bend (C's transitions in 12-TET).
import json, math, os, sys

W = sys.argv[1] if len(sys.argv) > 1 else '.'
# patch, legatoVelocity, range (cents), tau (s, bendSmoothing), bendGlide [(lenBefore ms, leave, arrive)]
PATCH = {'va': ('Violas - Performance', 100, 99.6, 0.0731, [(136, -8, 130), (300, 52, 182), (600, 130, 288)]),
         'vc': ('Celli - Performance', 93, 99.8, 0.0719, [(136, -9, 128), (300, 69, 228), (600, 110, 282)])}
# pairs of written pitches (MIDI key + cents as spelled)
PAIRS = {'va8': ('va', [(62, -50), (64, 50)]),      # Whence Violas bar 8: D quarter-flat / E quarter-sharp (61.5 / 64.5)
         'vc1': ('vc', [(47, -50), (50, -50)]),     # Whence Celli bar 1: 46.5 / 49.5 (one bend already)
         'vc2': ('vc', [(52, 50), (50, -50)]),      # Whence Celli bar 2: 52.5 / 49.5
         # Whence's figures as written: Violas bar 8 (63, 61.5, 63, 64.5 ...), Celli bar 1 (48, 46.5, 48, 49.5 ...)
         'va8w': ('va', [(63, 0), (62, -50), (63, 0), (64, 50)]),
         'vc1w': ('vc', [(48, 0), (47, -50), (48, 0), (50, -50)])}
IOIS = (0.136, 0.200, 0.300, 0.600)
TAKES = 2
N = 8
os.makedirs(f'{W}/ev', exist_ok=True)


def unit(c, R):
    return max(0, min(16383, int(round(8192 + c / R * (8192 if c < 0 else 8191)))))


def glide_at(gl, ms):
    if ms <= gl[0][0]:
        return gl[0][1], gl[0][2]
    for a, b in zip(gl, gl[1:]):
        if a[0] <= ms <= b[0]:
            f = (ms - a[0]) / (b[0] - a[0])
            return a[1] + (b[1] - a[1]) * f, a[2] + (b[2] - a[2]) * f
    return gl[-1][1], gl[-1][2]


def common(notes):
    """one bend for every note where possible: a -50 note one key down at +50 when another note is at +50"""
    cs = {c for _, c in notes}
    if len(cs) == 1:
        return notes
    if cs == {-50, 50}:
        return [(k - 1, 50) if c == -50 else (k, c) for k, c in notes]
    return notes


def bends_k(ons, cents, R, tau, gl, ioi):
    """MuseScore's libraryPitchBends for one run on one route (bendAtArrival 1, measured glide, smoothing)"""
    out = []
    st = dict(heard=cents[0], heardAt=ons[0], sent=cents[0])

    def hear(t):
        if t > st['heardAt']:
            st['heard'] = st['sent'] + (st['heard'] - st['sent']) * math.exp(-(t - st['heardAt']) / tau)
            st['heardAt'] = t

    def send(t, c):
        hear(t)
        out.append((t, unit(c, R)))
        st['sent'] = c

    send(ons[0], cents[0])
    for k in range(1, len(ons)):
        on = ons[k]
        nxt = ons[k + 1] if k + 1 < len(ons) else None
        leave, arrive = glide_at(gl, ioi * 1000)
        t0 = on + max(0.0, leave) / 1000
        t1 = max(t0 + 0.0005, on + max(0.0, arrive) / 1000)
        if nxt is not None and nxt > t0:
            t1 = min(t1, nxt)
        if t0 > on:
            send(on, cents[k - 1])
        hear(t0)
        e = math.exp(-(t1 - t0) / tau)
        u = max(-R, min(R, (cents[k] - st['heard'] * e) / (1 - e)))
        send(t0, u)
        if nxt is None or t1 < nxt:
            send(t1, cents[k])
    return out


idx = {}
for name, (pk, pair) in PAIRS.items():
    patch, tv, R, tau, gl = PATCH[pk]
    for var in 'NKSCT':
        if var in 'CT' and common(pair) == pair:
            continue
        t = 0.5
        ev = [(0, '0.000000 cc 1 64'), (0, '0.000000 cc 11 127'), (0, '0.000000 pb 8192 0')]
        notes = []
        for ioi in IOIS:
            for take in range(TAKES):
                seq = [pair[k % len(pair)] for k in range(N)]
                play = common(seq) if var in 'CT' else seq
                keys = [k for k, _ in play]
                cents = [c for _, c in play]
                ons = [t + k * ioi for k in range(N)]
                if var == 'K':
                    for tt, v in bends_k(ons, cents, R, tau, gl, ioi):
                        ev.append((tt - 1e-7, f'{tt:.6f} pb {v} 0'))
                elif var in 'SC':
                    for k, on in enumerate(ons):
                        if var == 'S' or k == 0:
                            ev.append((on - 1e-7, f'{on:.6f} pb {unit(cents[k], R)} 0'))
                for k, on in enumerate(ons):
                    off = on + ioi + 0.024 if k + 1 < N else on + ioi
                    ev += [(on, f'{on:.6f} on {keys[k]} {112 if k == 0 else tv}'), (off + 1e-7, f'{off:.6f} off {keys[k]} 0')]
                    notes.append(dict(on=on, next=on + ioi, key=keys[k], target=seq[k][0] + seq[k][1] / 100.0,
                                      prev=(seq[k - 1][0] + seq[k - 1][1] / 100.0) if k else None, ioi=ioi, k=k, take=take))
                t += N * ioi + 2.0
                ev.append((t - 0.3, f'{t - 0.3:.6f} pb 8192 0'))
        fn = f'{name}_{var}'
        open(f'{W}/ev/{fn}.txt', 'w').write('\r\n'.join(l for _, l in sorted(ev, key=lambda e: e[0])) + '\r\n')
        idx[fn] = dict(patch=patch, pair=name, variant=var, notes=notes, end=t, probe=pair[0][0])
json.dump(idx, open(f'{W}/index.json', 'w'), indent=0)
open(f'{W}/ev/list.txt', 'w').write(''.join(f"{k}|{v['patch']}|{v['probe']}\r\n" for k, v in sorted(idx.items())))
print(' '.join(sorted(idx)))
