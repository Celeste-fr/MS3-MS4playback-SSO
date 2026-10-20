#!/usr/bin/env python3
"""Recording-only copies of the own-legato recording setup with layer families muted (owner 2026-10-17: group
muting / soloing allowed for recordings on the Windows VM only, never saved as a MuseScore setup, never used in
playback; the copies are deleted from the VM after each batch). Method: tools/soundlibraries/layer_isolation.py's
(2026-10-13): every zone of the other families at zone volume 0, so every group stays loaded and played and the
script, voice allocation and modulation behave the same (the script sets group volumes, never zone volumes).

    mute_setups.py <setup.vst3state> <out dir> [--det] [--only fam,fam]
      writes <out>/<prefix><fam>.vst3state for each family (layer_isolation.family()) plus 'all' (the rewrite alone)
      and 'silent'; --det: layer_isolation's d1 (random modulation sources at intensity 0) in every copy, prefix d1_
"""
import argparse, json, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'soundlibraries'))
import make_setups as M  # noqa: E402
import layer_isolation as LI  # noqa: E402

FAMS = ('fing', 'portFast', 'runs', 'runsRT', 'fastLegato', 'sustain', 'release', 'other')


def subfamily(name):
    """the 'sustain' family split by group kind (iso10): sus.long, sus.dest, sus.spicc, sus.across, else the family"""
    f = LI.family(name)
    if f != 'sustain':
        return f
    u = name.upper()
    for tag, sub in (('DEST-NT_SUS', 'dest'), ('ACROSS STRINGS', 'across'), ('SPICC', 'spicc'), ('LONG ', 'long')):
        if tag in u:
            return 'sus.' + sub
    return 'sus.other'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('src'); ap.add_argument('out'); ap.add_argument('--det', action='store_true')
    ap.add_argument('--only'); ap.add_argument('--name', default='Violas mute')
    ap.add_argument('--solo-keys', help='name:k1+k2,name:k1+k2...: one copy per entry keeping only the zones whose key '
                    'range holds one of the keys (every family): the voices of one note (its sustain keys, its transition '
                    'zones at key +-24)')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    v, name, comp, ctrl = M.read_vst3state(open(a.src, 'rb').read())
    prog = M.setup_parts(comp)
    groups, zones = LI.program_layout(prog)
    fam = [LI.family(n) for n, m in groups]
    sub = [subfamily(n) for n, m in groups]
    mods = LI.modulators(prog)
    pre = 'd1_' if a.det else ''
    todo = a.only.split(',') if a.only else list(FAMS) + ['all', 'silent']
    solo = {}
    if a.solo_keys:
        for e in a.solo_keys.split(','):
            n, ks = e.split(':')
            fl = None
            if '/' in n:                            # name/family or name/-family: that family alone / all but it
                n, fl = n.split('/')
            solo['solo_' + n + ('_' + fl.replace('-', 'no').replace('.', '') if fl else '')] = ([int(k) for k in ks.split('+')], fl)
        todo = list(solo)
    keys = [struct.unpack_from('<2H', prog, o - 30 + 16) for g, o in zones]
    info = {}
    for f in todo:
        b = bytearray(prog)
        nd = LI.deterministic(b, mods, 1) if a.det else 0
        nz = 0
        for (g, o), (kl, kh) in zip(zones, keys):
            if f in solo:
                ks, fl = solo[f]
                mute = not any(kl <= k <= kh for k in ks)
                if fl and fl.startswith('-'):
                    mute = mute or fam[g] == fl[1:]
                elif fl and fl.startswith('sus.'):     # a sustain sub-family (iso10)
                    mute = mute or sub[g] != fl
                elif fl:
                    mute = mute or fam[g] != fl
            elif f == 'ghost':                     # the GHOST groups alone (one zone over all keys each)
                mute = 'GHOST' not in groups[g][0].upper()
            else:
                mute = f == 'silent' or (f != 'all' and fam[g] != f)
            if mute:
                struct.pack_into('<f', b, o, 0.0); nz += 1
        c = M.make_component(comp, bytes(b), None)
        fn = '%s %s%s.vst3state' % (a.name, pre, f)
        open(os.path.join(a.out, fn), 'wb').write(M.write_vst3state(name, c, ctrl))
        info[f] = dict(muted_zones=nz, det_mods=nd, groups=sum(1 for x in fam if x == f))
        print(fn, info[f])
    json.dump(dict(info=info, family=fam, names=[n for n, m in groups]), open(os.path.join(a.out, 'partition.json'), 'w'))


if __name__ == '__main__':
    main()
