#!/usr/bin/env python3
"""sfz150: the 150 ms class layer instrument (iso10_build.py's model) as an SFZ for sfizz, plus t9's events for it.

    sfz150_build.py <iso dir> <out dir> [--inc own|prev]

Writes <out>/samples/*.flac (24-bit cuts of the key-solo family-layer recordings, the instrument's audio: stays outside
the repository), <out>/ol150.sfz and <out>/t9_sfz.txt (t9.txt with the two position controllers added). Render:
    sfzrender <out>/ol150.sfz <out>/t9_sfz.txt <wav> --voices 512 --tail 4
with sfzrender linked against sfizz 1.2.3 + sfizz-agefix.patch (stock 1.2.3 repeats a sample at the end of every voice's
first block: the null against the model is then -19 dB; patched -138 dB). Null: sfz150_null.py.

Design (one region per family layer of a note, silent layers left out; every region key=, amp_veltrack=0, no pitch
tracking, a flex EG as its amplitude envelope; group 1 / off_by 1 on every note region, so a new note's regions choke
the older note's (sfizz 1.2.3 Voice::checkOffGroup skips regions of the same note number)):
  ATK   the phrase's first note (trigger=first): its K-solo 'atkheld' recording; the flex EG cuts it to 0 in one frame
        when choked by the 2nd note (or at its own note-off).
  TAIL  the attack note's voices after the 2nd note-on (iso10): key N, sw_previous=K, trigger=legato, CC20=1; the
        K-solo 'atkfull' recording of K -> another 2nd note (leave-one-out, as iso10_build --atk loo), from that
        note-on on (the 302 ms attack class); group 2 (nothing chokes it), one_shot.
  MID   a transition note that will be followed (CC21=0): key b, sw_previous=a, trigger=legato, CC20=1 (2nd note, layer
        'atk') or 2 (later, layer 'run'); its 'mid' layer, one_shot; choked by the next note -> the flex EG's release
        stages are the old-note gain R_f(t) (E8 at 150 ms, one 10 ms step per stage pair, zero-time jumps; 2nd note:
        E8's 'all', later notes: by the interval into the note, --inc own; prev = iso10_build's default, which needs
        the note before's interval: not expressible with sw_previous, refused). Gains > 1 (FAST LEGATO to 1.7): EG
        levels / gmax and volume +20 log10 gmax (sfizz clamps EG levels to [-1, 1]).
  END   the phrase's last note (CC21=1): its 'end' layer (held 86 blocks then released, recorded), one_shot.
CC20 / CC21 are sent by the player before each note-on (the score knows the position: 1 = 2nd note, 2 = later; 21:
1 = last note of the phrase). The release of the last note and the attack tail are recorded for t9's hold classes
(1.0 s / 302 ms); other holds need their own tails (phase 1's hold probe).
"""
import json, os, sys
import numpy as np
import soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import stereo, SR  # noqa
import iso10_build as M  # noqa

FAMS = M.FAMS
W = M.W
SFIZZ_GAIN_DB = 11.522749  # sfizz plays a sample at -11.522749 dB at its defaults (make_sfz3.py, measured)
STEP = (W - 0.5) / SR      # an EG hold stage: sfizz ends a stage on the first frame with time >= its length: 441 frames


def main():
    d, out = sys.argv[1], sys.argv[2]
    imode = sys.argv[sys.argv.index('--inc') + 1] if '--inc' in sys.argv else 'own'
    if imode != 'own':
        sys.exit('--inc prev picks R_f by the interval before the previous note: no sfz region can see that')
    B9 = d + '/b9'
    os.makedirs(out + '/samples', exist_ok=True)
    R = M.e8_gains(d)
    C = {}

    def get(n):
        if n not in C:
            C.clear()
            C[n] = stereo('%s/audio/b_9/%s.flac' % (d, n))
        return C[n]
    written = {}

    def sample(name, x):
        if not np.any(x):
            return None
        if name not in written:
            sf.write('%s/samples/%s.flac' % (out, name), x, SR, subtype='PCM_24')
            written[name] = len(x)
        return name + '.flac'

    def rcurve(f, inc):
        Rc = R[f].get(inc) if R[f].get(inc) is not None else R[f].get('all')
        return np.zeros(1) if Rc is None else Rc

    def eg_cut():
        return 'eg1_ampeg=1 eg1_sustain=0 eg1_time0=0 eg1_level0=1 eg1_time1=0 eg1_level1=0 eg1_time2=0.01 eg1_level2=0'

    def eg_hold():
        return 'eg1_ampeg=1 eg1_sustain=0 eg1_time0=0 eg1_level0=1'

    def eg_r(Rc):
        g = Rc[::W] if len(Rc) > 1 else np.zeros(1)
        gmax = max(1.0, float(np.abs(g).max()))
        ops = ['eg1_ampeg=1', 'eg1_sustain=0', 'eg1_time0=0', 'eg1_level0=%.9g' % (1 / gmax)]
        p = 1
        for j, v in enumerate(g):
            last = j == len(g) - 1
            ops += ['eg1_time%d=0' % p, 'eg1_level%d=%.9g' % (p, v / gmax),
                    'eg1_time%d=%.9g' % (p + 1, 10.0 if last else STEP), 'eg1_level%d=%.9g' % (p + 1, v / gmax)]
            p += 2
        vol = SFIZZ_GAIN_DB + 20 * np.log10(gmax)    # a region's volume replaces the <global> one
        return ' '.join(ops), vol

    regions = []
    common = 'amp_veltrack=0 pitch_keytrack=0 loop_mode=one_shot'
    # attack layers
    full = {}
    for fn in sorted(os.listdir(B9)):
        if not (fn.startswith('a9_k') and fn.endswith('.json')):
            continue
        name = fn[:-5]
        segs = json.load(open(os.path.join(B9, fn)))['segments']
        k = segs[0]['notes'][0]['key']
        for f in FAMS:
            y = get('solo_%s_%s__%s' % (name, f, name))
            held = [s for s in segs if s['kind'] == 'atkheld'][0]
            i0 = int(round(held['notes'][0]['on'] * SR))
            sm = sample('atk_k%d_%s' % (k, f), y[i0:i0 + int(3.5 * SR)])
            if sm:
                regions.append('<region> sample=%s key=%d pitch_keycenter=%d trigger=first group=1 off_by=1 %s %s'
                               % (sm, k, k, common.replace(' loop_mode=one_shot', ''), eg_cut()))
            for s in segs:
                if s['kind'] != 'atkfull':
                    continue
                j0 = int(round(s['notes'][0]['on'] * SR))
                t1 = int(round(s['notes'][1]['on'] * SR)) - j0
                full[(k, s['b'], f)] = (y[j0 + t1:j0 + int(3.5 * SR)].copy(), t1)
    # transition layers
    lay = {}
    for fn in sorted(os.listdir(B9)):
        if not (fn.startswith('l9_b') and fn.endswith('.json')):
            continue
        name = fn[:-5]
        for f in FAMS:
            y = get('solo_%s_%s__%s' % (name, f, name))
            for s in json.load(open(os.path.join(B9, fn)))['segments']:
                i = int(round(s['notes'][-1]['on'] * SR))
                lay[(s['kind'], s['a'], s['b'], s['var'], f)] = y[i:i + int(3.2 * SR)].copy()
    pairs = sorted({(kd, a, b) for (kd, a, b, v, f) in lay})
    for kd, a, b in pairs:
        pos = 1 if kd == 'atk' else 2
        cond = 'key=%d pitch_keycenter=%d sw_previous=%d trigger=legato locc20=%d hicc20=%d' % (b, b, a, pos, pos)
        for f in FAMS:
            sm = sample('%s_%d_%d_mid_%s' % (kd, a, b, f), lay[(kd, a, b, 'mid', f)])
            if sm:
                eg, vol = eg_r(rcurve(f, 'all' if kd == 'atk' else abs(b - a)))
                regions.append('<region> sample=%s %s locc21=0 hicc21=0 group=1 off_by=1 volume=%.9g %s %s'
                               % (sm, cond, vol, common, eg))
            sm = sample('%s_%d_%d_end_%s' % (kd, a, b, f), lay[(kd, a, b, 'end', f)])
            if sm:
                regions.append('<region> sample=%s %s locc21=1 hicc21=1 group=1 off_by=1 %s %s'
                               % (sm, cond, common, eg_hold()))
        if kd == 'atk':
            for f in FAMS:
                alt = [n for (k2, n, f2) in full if k2 == a and f2 == f and n != b]
                n = alt[0] if alt else b
                x, t1 = full[(a, n, f)]
                sm = sample('tail_k%d_from%d_%s' % (a, n, f), x)
                if sm:
                    regions.append('<region> sample=%s key=%d pitch_keycenter=%d sw_previous=%d trigger=legato '
                                   'locc20=1 hicc20=1 group=2 %s %s' % (sm, b, b, a, common, eg_hold()))
    hdr = ['// own legato, 150 ms class (sfz150_build.py; generated: the samples stay outside the repository)',
           '<control> default_path=samples/',
           '<global> polyphony=512 volume=%.6f sample_quality=1' % SFIZZ_GAIN_DB]   # sfizz's default level / exact copy: make_sfz3.py
    open(out + '/ol150.sfz', 'w').write('\n'.join(hdr + regions) + '\n')
    # events: CC20 / CC21 before each note-on
    T = json.load(open(B9 + '/t9.json'))
    pos = {}
    for s in T['segments']:
        ns = s['notes']
        for i, q in enumerate(ns):
            pos[round(q['on'], 6)] = (0 if i == 0 else 1 if i == 1 else 2, 1 if i == len(ns) - 1 and i else 0)
        assert int(round(ns[1]['on'] * SR)) - int(round(ns[0]['on'] * SR)) == 13312
    ev = []
    for line in open(B9 + '/t9.txt'):
        p = line.split()
        if len(p) >= 3 and p[1] == 'on':
            c20, c21 = pos[round(float(p[0]), 6)]
            ev += ['%s cc 20 %d' % (p[0], c20), '%s cc 21 %d' % (p[0], c21)]
        ev.append(line.rstrip('\n'))
    open(out + '/t9_sfz.txt', 'w').write('\n'.join(ev) + '\n')
    tot = sum(written.values())
    print('%d regions, %d samples (%.1f s audio); wrote %s/ol150.sfz, t9_sfz.txt' % (len(regions), len(written), tot / SR, out))


if __name__ == '__main__':
    main()
