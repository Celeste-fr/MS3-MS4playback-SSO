#!/usr/bin/env python3
"""Recording-only copies of the own-legato recording setup with layer families muted (owner 2026-10-17: group
muting / soloing allowed for recordings on the Windows VM only, never saved as a MuseScore setup, never used in
playback; the copies are deleted from the VM after each batch). Method: tools/soundlibraries/layer_isolation.py's
(2026-10-13): every zone of the other families at zone volume 0, so every group stays loaded and played and the
script, voice allocation and modulation behave the same (the script sets group volumes, never zone volumes).

    mute_setups.py <setup.vst3state> <out dir> [--det] [--only fam,fam]
      writes <out>/<prefix><fam>.vst3state for each family (layer_isolation.family()) plus 'all' (the rewrite alone)
      and 'silent'; --det: layer_isolation's d1 (random modulation sources at intensity 0) in every copy, prefix d1_
    --solo-keys-split name:k1+k2,...   zone re-split key-solo (ownlegato-study STUDY.md 3.4): every narrow zone (<= WIDE
      keys) holding a solo key is narrowed to that key alone (key range at the zone record's public offset +16 / +18)
      and the rest of its range goes to the adjacent zone of the same group and velocity range (widened, muted), so a
      semitone neighbour still makes its (silent) voice in that group but never sounds in this note's layer; then only
      the narrowed zones are unmuted. Keys with no adjacent zone to go to (a range edge, or the neighbour holds another
      solo key) lose that group: listed as orphan_ranges, they must not be played keys. Wide zones (> WIDE keys: the GHOST / 0-127 zones) are muted and left alone.
    --not-keys name:k1+k2      every zone except the narrow zones holding a key (the identity test's 'rest')
    --shared-keys name:k+k/k+k the narrow zones holding a key of both sets (identity test: the double-counted zones)
    --wide-only name           the wide zones alone
"""
import argparse, json, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'soundlibraries'))
import make_setups as M  # noqa: E402
import layer_isolation as LI  # noqa: E402

WIDE = 12                      # a zone wider than this many keys is 'wide' (0-127 GHOST / release zones)
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
    ap.add_argument('--solo-keys-split', help='name:k1+k2,...: zone re-split key-solo (see the module doc)')
    ap.add_argument('--not-keys'); ap.add_argument('--shared-keys'); ap.add_argument('--wide-only')
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
    vels = [struct.unpack_from('<2H', prog, o - 30 + 12) for g, o in zones]
    wide = [kh - kl + 1 > WIDE for kl, kh in keys]
    spec = {}                                  # name -> (mode, keys or (keys, keys))
    for opt, mode in ((a.solo_keys_split, 'split'), (a.not_keys, 'not'), (a.shared_keys, 'shared')):
        for e in (opt.split(',') if opt else []):
            n, ks = e.split(':')
            if mode == 'shared':
                p, q = ks.split('/')
                spec['solo_' + n] = (mode, ([int(k) for k in p.split('+')], [int(k) for k in q.split('+')]))
            else:
                spec['solo_' + n] = (mode, [int(k) for k in ks.split('+')])
    if a.wide_only:
        spec['solo_' + a.wide_only] = ('wide', None)
    if spec:
        todo = list(solo) + list(spec)
    info = {}
    for f in todo:
        b = bytearray(prog)
        nd = LI.deterministic(b, mods, 1) if a.det else 0
        nz = 0
        if f in spec:
            nz, extra = special(b, zones, keys, vels, wide, *spec[f], fam=sub)
            c = M.make_component(comp, bytes(b), None)
            fn = '%s %s%s.vst3state' % (a.name, pre, f)
            open(os.path.join(a.out, fn), 'wb').write(M.write_vst3state(name, c, ctrl))
            info[f] = dict(muted_zones=nz, det_mods=nd, **extra)
            print(fn, info[f])
            continue
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


def special(b, zones, keys, vels, wide, mode, ks, fam=None):
    """the re-split / identity masks in place on b; returns (muted zones, info)"""
    holds = lambda i, kk: (not wide[i]) and any(keys[i][0] <= k <= keys[i][1] for k in kk)  # noqa: E731
    nz, info = 0, {}
    if mode == 'split':
        rng = list(keys)
        lane = {}                                  # (group, vel range) -> zone indices sorted by key
        for i, (g, o) in enumerate(zones):
            lane.setdefault((g, vels[i]), []).append(i)
        S = [i for i in range(len(zones)) if holds(i, ks)]
        Sset = set(S)
        narrowed = orphan = widened = 0
        orphans = {}
        for i in S:
            kl, kh = keys[i]
            k = [x for x in ks if kl <= x <= kh]
            assert len(k) == 1, ('two solo keys in one zone', i, keys[i], k)
            k = k[0]
            if (kl, kh) == (k, k):
                continue
            narrowed += 1
            rng[i] = (k, k)
            L = sorted(lane[(zones[i][0], vels[i])], key=lambda j: keys[j][0])
            pos = L.index(i)
            for lo, hi, side in ((kl, k - 1, -1), (k + 1, kh, 1)):
                if lo > hi:
                    continue
                j = L[pos + side] if 0 <= pos + side < len(L) else None
                if j is not None and j not in Sset and (keys[j][1] == lo - 1 if side < 0 else keys[j][0] == hi + 1):
                    rng[j] = (rng[j][0], hi) if side < 0 else (lo, rng[j][1])
                    widened += 1
                else:
                    orphan += 1
                    t = '%s %d-%d' % (fam[zones[i][0]] if fam else '', lo, hi)
                    orphans[t] = orphans.get(t, 0) + 1
        # no two zones of a lane may overlap after the edit
        for L in lane.values():
            r = sorted(rng[j] for j in L)
            assert all(r[x][1] < r[x + 1][0] for x in range(len(r) - 1)), ('overlap', r)
        for i, (g, o) in enumerate(zones):
            if rng[i] != keys[i]:
                struct.pack_into('<2H', b, o - 30 + 16, *rng[i])
            if i not in Sset:
                struct.pack_into('<f', b, o, 0.0); nz += 1
        info = dict(narrowed=narrowed, widened=widened, orphan_ranges=orphans, solo_zones=len(S))
    else:
        for i, (g, o) in enumerate(zones):
            if mode == 'not':
                keep = not holds(i, ks)
            elif mode == 'shared':
                keep = holds(i, ks[0]) and holds(i, ks[1])
            else:
                keep = wide[i]
            if not keep:
                struct.pack_into('<f', b, o, 0.0); nz += 1
        info = dict(kept=len(zones) - nz)
    return nz, info


if __name__ == '__main__':
    main()
