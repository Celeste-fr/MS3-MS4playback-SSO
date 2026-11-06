#!/usr/bin/env python3
"""Layer isolation (2026-10-13, branch layer-isolation): does Kontakt's output for SSO's Celli - Performance equal the
sum of its sample groups played separately? Written for this check, not taken from anywhere. Why: slurred notes'
levels in the Performance legato swell by context (sso_even_levels_test.json); an overlap model of whole transitions
(superposition_scan.py) reached r 0.65 against r 0.97 measuring in place. If the patch mixes its groups linearly, each
layer family (a transition kind, the sustains, the releases) can be measured and modelled on its own.
Measurement only: the renderer doesn't read the result (sso_layer_isolation.json).

  layer_isolation.py setups <setup.vst3state> <out folder>
      measurement-only copies of the setup (never MuseScore's setups folder): one per layer family, every zone of the
      other families at zone volume 0 (kept loaded and played, so the script, voice allocation and every modulation
      behave the same; the script sets group volumes from its mic faders, Map__Volume, and reads no zone parameter,
      so zone volume is the one gain the script can't undo), plus 'silent' (every zone at 0) and 'identity' (none:
      the rewrite alone); partition.json. --det 1|2: random sources (|pitch LFOs) at intensity 0 (file prefix d1_/d2_);
      --fxgain TYPE:FAMILY, --zero TARGETREGEX:FAMILY: the probes
  layer_isolation.py events <dump folder> <work>      one kthost event file <work>/ev.txt and <work>/meta.json
  layer_isolation.py analyze <work> <wav folder> [-o sso_layer_isolation.json]

  layer_isolation.py probes <work> <wav folder>       the group-data probes (below) against their family's render
  layer_isolation.py bands <work> <wav folder>        band powers (loudness.py's model) of the mix and each family
  layer_isolation.py model <work> | modelbands <work> per-layer prediction, even phrases -> odd phrases
  layer_isolation.py summary <work> [-o sso_layer_isolation.json]

Method (Windows test VM, kthost + Kontakt 8 + SSO; analysis on Linux): copies of MuseScore's resaved Celli -
Performance setup, built by this tool in a scratch folder (deleted after), never in MuseScore's setups folder. Families
by group name over all five mic blocks: FING, PORTAMENTO FAST (::int:: without suffix too), RUNS, RUNS RT, FAST LEGATO,
sustains (Dest-nt_Sus, long, Across Strings, Spicc), releases (RLS, Rls, GHOST), other (DETACHED, PORT, headers); a
second partition by mic block. Every group stays loaded and played; only its zones' volume is 0 ('silent', every zone
at 0, renders exact zeros). The shipped patch is not deterministic: two renders differ at -7.5 dB (from the second
note on: random modulation sources, type 10 RVU -> fm7_level1 and a random pitch per legato note, and the pitch LFOs);
'd2' copies set those 400 modulators' intensity to 0 (two renders: -99.5 dB, max 6e-7 against a 0.04 peak; same
overall level as shipped, per-note swell |diff| median 0.3 dB, like two shipped takes' 0.2). One event file (152 s):
MuseScore's Whence 12-TET bars 1-4 Celli events (tst_liveequivalence dumpEvents, Recommended) and run_phrases('vc')
2-29 (a spiccato note then four slurs of four: bowed 93 + fingered 69; 20 phrases at 136 ms, 4 at 250, 4 at 500).

Result (2026-10-13; VM 21 min, 27 renders):
- Linear. Sum of the 8 family renders - the d2 mix: -96.8 dB overall, per note (heard window) median -105.8, max
  -99.8 dB; sum of the 5 mic renders -99.5 dB; the mix's own take-to-take baseline -99.5 / per note -108.4. Perceived
  swell of the sum = the mix's (max |diff| 0.001). No voice-limit, choke, script-volume or bus effect breaks additivity.
  Mics: the Tree block sounds (Leader block -26 dB re the mix); Close, Ambient, Outrigger silent.
  RUNS (Celli -14.5 dB zone gain) never sounds; RUNS RT does.
- Layers (heard window, dB re the mix, median): 136 ms runs: FING -3.3, PORT FAST -8.7, FAST LEGATO -10.7, sustains
  -12.0, other (DETACHED on the bowed 93 slur starts) -14.3, RUNS RT -14.0; 500 ms: FING -1.3, PORT FAST -12.4, FAST
  LEGATO -12.9, sustains -27.2 (still fading in), RUNS RT silent. FING carries the note; the fast-run surplus is the
  other transition layers and the sustains.
- Prediction (fit on even phrases, scored on odd, shapes within slurs; bar r ~0.97, range <= ~0.7 dB): the layers'
  measured band powers summed in power then compressed reach r 0.959, range 2.80 -> 0.86 dB (cross terms between
  layers, ~0.4 dB SD, are lost in a power sum: a per-layer power model can't pass the bar even with perfect layers);
  measured heard-window energies alone r 0.79 (energy is not the perceived swell). Predicted from context (pitch,
  intervals, velocities, gaps of notes -2..+2): per-layer band models r 0.51 (136 ms) / 0.29 (all), per-layer energy
  models r 0.32, one direct model r 0.50, against shipped take-to-take r 0.93-0.95. Per-layer shapes are predictable
  for FING / PORT FAST / sustains (r ~0.85), not RUNS RT / FAST LEGATO (0.16 / 0.43). 10 training phrases are few.
- Group data probes (one copy each, family isolated): the header float of FX type 9 (PORT FAST, +10.67 dB) and type 3
  (FING, -2.16 dB) is a static output gain: setting it to 1.0 moves the layer by exactly -10.67 / +2.16 dB at every
  note and time; type 4 (sustains, -3.16..+4.82 dB per group) by -0.5 dB overall, consistent. fm7_levelN / fm7_timeN
  modulations act on the layer's volume envelope levels: off in FING -1.6 dB at 136 ms, -1.1 at 250 (-2.5..-3.3 dB
  50-200 ms after the note-on), ~0 at 500 ms (so CC119 speed raises FING's envelope when fast); off in PORT FAST
  +11.2 / +12.9 / +13.2 dB (136 / 250 / 500 ms): its speed-dependent attenuation sits in these envelope modulations.
  Which of a group's flex envelopes an fm7 target names is still not decoded from the bytes.
"""
import argparse
import json
import math
import os
import re
import statistics as st
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))

PATCH = 'Celli - Performance'
FAMILIES = ('fing', 'portFast', 'runs', 'runsRT', 'fastLegato', 'sustain', 'release', 'other')
MICS = ('Close', 'Tree', 'Ambient', 'Outrigger', 'Leader')


# --- setups ---------------------------------------------------------------------------------------------------------

def family(name):
    """the layer family of a group, from its name (any mic)"""
    u = name.upper()
    if 'RUNS RT' in u:
        return 'runsRT'
    if 'RUNS' in u:
        return 'runs'
    if 'FAST LEGATO' in u:
        return 'fastLegato'
    if '::INT' in u:
        if 'FING' in u:
            return 'fing'
        if 'DETACHED' in u or re.search(r'::\s*PORT\s*$', u):
            return 'other'
        return 'portFast'
    if 'RLS' in u or 'GHOST' in u:
        return 'release'
    if 'DEST-NT_SUS' in u or u.startswith('LONG ') or 'ACROSS STRINGS' in u or 'SPICC' in u:
        return 'sustain'
    return 'other'


def _pstruct_span(b, p):
    """(public offset, public length, end) of a structured record at p"""
    if b[p] != 1:
        raise ValueError('unstructured record')
    p += 3
    n = struct.unpack_from('<I', b, p)[0]
    p += 4 + n
    n = struct.unpack_from('<I', b, p)[0]
    pub = p + 4
    p += 4 + n
    k = struct.unpack_from('<I', b, p)[0]
    return pub, n, p + 4 + k


def program_layout(prog):
    """groups [(name, mic)] and zones [(group, offset of the zone volume in prog)]"""
    import make_setups as M
    ver, priv, pub, kids = M.struct_parts(prog)
    base = 1 + 2 + 4 + len(priv) + 4 + len(pub) + 4      # kids start in prog
    groups, zones = [], []
    p = 0
    while p < len(kids):
        cid, n = struct.unpack_from('<HI', kids, p)
        body = base + p + 6
        if cid == 0x33:
            q = body + 4
            mic = None
            for _ in range(struct.unpack_from('<I', prog, body)[0]):
                po, pn, q2 = _pstruct_span(prog, q)
                ln = struct.unpack_from('<I', prog, po)[0]
                name = prog[po + 4:po + 4 + 2 * ln].decode('utf-16-le', 'replace').strip()
                m = re.match(r'^#+\s*(\w+)\s*#+$', name)
                if m:
                    mic = m.group(1)
                groups.append((name, mic))
                q = q2
        elif cid == 0x34:
            q = body + 4
            for _ in range(struct.unpack_from('<I', prog, body)[0]):
                g = struct.unpack_from('<I', prog, q)[0]
                po, pn, q2 = _pstruct_span(prog, q + 4)
                zones.append((g, po + 30))
                q = q2
        p += 6 + n
    return groups, zones


SOURCE = re.compile(rb'([\x03-\x1f])\x00\x00\x00([A-Z][A-Z0-9_]{2,30})\x01\x00\x00\x00([\x00-\x20])\x00\x00\x00')


def modulators(prog):
    """every modulator of every group: dict(group, kind 'ext' / 'int', target, offset of its intensity, sourceType
    (ext), type (int))"""
    import make_setups as M
    ver, priv, pub, kids = M.struct_parts(prog)
    base = 1 + 2 + 4 + len(priv) + 4 + len(pub) + 4
    out = []
    p = 0
    while p < len(kids):
        cid, n = struct.unpack_from('<HI', kids, p)
        body = base + p + 6
        if cid == 0x33:
            q = body + 4
            for gi in range(struct.unpack_from('<I', prog, body)[0]):
                po, pn, q2 = _pstruct_span(prog, q)
                k = po + pn + 4
                while k < q2:
                    aid, an = struct.unpack_from('<HI', prog, k)
                    a = k + 6
                    if aid in (0x3B, 0x3C):
                        aver = struct.unpack_from('<H', prog, a + 1)[0]
                        r = a + 3 + (4 if aver >= 0x13 else 0)
                        while r < a + an:
                            present = prog[r]
                            r += 1
                            if not present:
                                continue
                            mid, ml = struct.unpack_from('<HI', prog, r)
                            mo = r + 6
                            tl = struct.unpack_from('<I', prog, mo + 11)[0]
                            d = dict(group=gi, target=prog[mo + 15:mo + 15 + tl].decode('latin-1'), at=mo + 15 + tl)
                            rec = prog[mo:mo + ml]
                            if mid == 0x0C:
                                m = list(SOURCE.finditer(rec))
                                d.update(kind='ext', source=m[-1].group(2).decode() if m else None,
                                         sourceType=m[-1].group(3)[0] if m else None)
                            elif mid == 0x0D:
                                m = re.search(rb'([\x03-\x1f])\x00\x00\x00((?:LFO|ENV|SB)_[A-Z0-9_]*)', rec[15 + tl:])
                                d.update(kind='int', type=m.group(2).decode() if m else None)
                            out.append(d)
                            r = mo + ml
                    k = a + an
                q = q2
        p += 6 + n
    return out


def group_fx(prog):
    """every group insert-FX slot: dict(group, slot, fxType, bypass, gain (the header float), at (its offset))"""
    import make_setups as M
    ver, priv, pub, kids = M.struct_parts(prog)
    base = 1 + 2 + 4 + len(priv) + 4 + len(pub) + 4
    out = []
    p = 0
    while p < len(kids):
        cid, n = struct.unpack_from('<HI', kids, p)
        body = base + p + 6
        if cid == 0x33:
            q = body + 4
            for gi in range(struct.unpack_from('<I', prog, body)[0]):
                po, pn, q2 = _pstruct_span(prog, q)
                pv0 = q + 7
                pv1 = pv0 + struct.unpack_from('<I', prog, q + 3)[0]
                i = prog.find(b'\x00\x13\x00\x08\x00\x00\x00', pv0, pv1)
                if i >= 0:
                    r = i + 7
                    try:
                        for slot in range(8):
                            if not prog[r]:
                                r += 1
                                continue
                            cid2, sz = struct.unpack_from('<HI', prog, r + 1)
                            if cid2 != 0x25:
                                break
                            fb = r + 7                  # PAR_FX body: u8 1, u16 version, u32 private length, private
                            fp = fb + 7
                            out.append(dict(group=gi, slot=slot, fxType=struct.unpack_from('<I', prog, fp)[0],
                                            bypass=prog[fp + 9], gain=struct.unpack_from('<f', prog, fp + 10)[0],
                                            at=fp + 10))
                            r = fb + sz
                    except struct.error:
                        pass
                q = q2
        p += 6 + n
    return out


def deterministic(b, mods, level):
    """intensity 0 on the random modulation sources (level >= 1) and the pitch LFOs (level >= 2); count"""
    n = 0
    for m in mods:
        if (level >= 1 and m['kind'] == 'ext' and m['sourceType'] == 10) or \
           (level >= 2 and m['kind'] == 'int' and (m['type'] or '').startswith('LFO')):
            struct.pack_into('<f', b, m['at'], 0.0)
            n += 1
    return n


def setups(src, out, det=0, only=None, fxgain=None, zero=None):
    import make_setups as M
    os.makedirs(out, exist_ok=True)
    v, name, comp, ctrl = M.read_vst3state(open(src, 'rb').read())
    prog = M.setup_parts(comp)
    groups, zones = program_layout(prog)
    fam = [family(n) for n, m in groups]
    mic = [m or 'Close' for n, m in groups]
    vols = [struct.unpack_from('<f', prog, o)[0] for g, o in zones]
    print(len(groups), 'groups,', len(zones), 'zones; zone volume range', min(vols), max(vols))
    counts = {f: dict(groups=sum(1 for x in fam if x == f), zones=sum(1 for g, o in zones if fam[g] == f))
              for f in FAMILIES}
    counts.update({'mic_' + f: dict(groups=sum(1 for x in mic if x == f), zones=sum(1 for g, o in zones if mic[g] == f))
                   for f in MICS})
    print(counts)
    mods = modulators(prog)
    import collections
    print(len(mods), 'modulators;', collections.Counter((m['kind'], m.get('sourceType'), m.get('type')) for m in mods))
    for f in (only or FAMILIES + tuple('mic_' + m for m in MICS) + ('silent', 'identity')):
        b = bytearray(prog)
        if det:
            print('deterministic', det, ':', deterministic(b, mods, det), 'intensities set to 0')
        if fxgain:                  # 'type:family': that FX type's header float set to 1.0 in that family's groups
            t, fm = fxgain.split(':')
            hit = [x for x in group_fx(prog) if x['fxType'] == int(t) and fam[x['group']] == fm]
            for x in hit:
                struct.pack_into('<f', b, x['at'], 1.0)
            print('fx type', t, 'in', fm, ':', len(hit), 'slots, gains', sorted(set(round(20 * math.log10(x['gain']), 2) for x in hit)))
        if zero:                    # 'regex:family': intensity 0 on the external modulators with a matching target
            t, fm = zero.split(':')
            hit = [m for m in mods if m['kind'] == 'ext' and re.match(t, m['target']) and fam[m['group']] == fm]
            for m in hit:
                struct.pack_into('<f', b, m['at'], 0.0)
            print('zeroed', len(hit), 'modulators', sorted(set(m['target'] for m in hit)))
        for g, o in zones:
            if f != 'identity' and fam[g] != f and 'mic_' + mic[g] != f:
                struct.pack_into('<f', b, o, 0.0)
        c = M.make_component(comp, bytes(b), None)
        fn = (('d%d_' % det) if det else '') + (('fx%s_' % fxgain.split(':')[0]) if fxgain else '') + \
            ('z_' if zero else '') + f + '.vst3state'
        open(os.path.join(out, fn), 'wb').write(M.write_vst3state(name, c, ctrl))
        print(f, 'written')
    json.dump(dict(counts=counts, family=fam, mic=mic), open(os.path.join(out, 'partition.json'), 'w'))


# --- events ---------------------------------------------------------------------------------------------------------

WHENCE_T0 = 1.0
GAP = 1.5
KINDS = (('fast', 0.136, range(2, 22)), ('med', 0.25, range(22, 26)), ('slow', 0.5, range(26, 30)))


def events(dump, work):
    import transition_level_scan as tls
    import even_levels_test as elt
    os.makedirs(work, exist_ok=True)
    info = tls.map_info(tls.MAP)[PATCH]
    bt = [float(x) for x in open(os.path.join(dump, 'bars.txt')).read().split()]
    wn, wev = elt.read_dump(os.path.join(dump, '0-7 museScore.txt'), bt[4])
    wn = elt.windows(wn, info)
    ev = [(t + WHENCE_T0, k, a, b) for t, k, a, b in wev]
    notes = []
    for n in wn:
        notes.append(dict(on=round(n['on'] + WHENCE_T0, 5), off=round(n['off'] + WHENCE_T0, 5), p=n['p'], v=n['v'],
                          d=n['d'], slur=n['slur'], phrase=0, kind='whence'))
    t = max(e[0] for e in ev) + GAP
    ev += [(t - 0.5, 'cc', 1, 64), (t - 0.5, 'cc', 11, 127)]
    phrases = tls.run_phrases('vc')
    ph, slur = 1, notes[-1]['slur'] + 1
    for kind, ioi, idx in KINDS:
        for i in idx:
            ps = phrases[i]
            vs = [tls.avoid('vc', ps[0], tls.SPICCATO)] + [tls.BOWED if k % 4 == 0 else tls.FINGERED
                                                            for k in range(len(ps) - 1)]
            for k, (p, v) in enumerate(zip(ps, vs)):
                last = k == len(ps) - 1
                on, off = t + k * ioi, t + (k + 1) * ioi + (0 if last else tls.OVERLAP)
                ev += [(on, 'on', p, v), (off, 'off', p, 0)]
                if k and (k - 1) % 4 == 0:
                    slur += 1
                d = (tls.legato_delay(info, p - ps[k - 1]) if k else (tls._interp(info['onset'], p) or 60.0)) / 1000
                notes.append(dict(on=round(on, 5), off=round(off, 5), p=p, v=v, d=round(d, 4),
                                  slur=slur if k else -1, phrase=ph, kind=kind, ioi=ioi))
            t += len(ps) * ioi + GAP
            ph += 1
            slur += 1
    elt.write_events(os.path.join(work, 'ev.txt'), ev)
    json.dump(dict(patch=PATCH, end=t, notes=notes), open(os.path.join(work, 'meta.json'), 'w'))
    print(len(notes), 'notes,', ph, 'phrases,', round(t, 1), 's')


# --- analysis (Linux) -----------------------------------------------------------------------------------------------

WIN = 0.1364


def db10(x):
    return 10 * math.log10(max(x, 1e-30))


def read_wav(path):
    import soundfile as sf
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    return x, sr


def note_windows(meta, sr, n_samples):
    """per note: (heard window i0, i1) = the swell window, (whole i0, i1) = note-on to the next note-on / + 0.5 s"""
    notes = meta['notes']
    out = []
    for k, n in enumerate(notes):
        a = n['on'] + n['d']
        nxt = notes[k + 1]['on'] if k + 1 < len(notes) and notes[k + 1]['phrase'] == n['phrase'] else n['off'] + 0.5
        out.append(((int(a * sr), min(n_samples, int((a + WIN) * sr))), (int(n['on'] * sr), min(n_samples, int(nxt * sr)))))
    return out


def energies(y, wins):
    """K-weighted energy in each heard window and each whole window"""
    import numpy as np
    c = np.concatenate([[0.0], np.cumsum(y * y)])
    return ([c[b] - c[a] for (a, b), _ in wins], [c[b] - c[a] for _, (a, b) in wins])


def swell(env, first, hop, meta):
    import numpy as np
    t = (first + np.arange(len(env)) * hop) / 1000.0
    out = []
    for n in meta['notes']:
        a = n['on'] + n['d']
        m = (t >= a) & (t < a + WIN)
        out.append(float(np.mean(env[m])))
    return out


REF, REFB = 'd2_identityA', 'd2_identityB'


def analyze(work, wavs, out_json):
    """renders: d2_identityA/B (the patch without its random modulation, twice), d2_<family>, d2_mic_<mic>, origA/B
    (the setup as shipped, twice)"""
    import numpy as np
    from loudness import k_weighted, perceived_envelope
    meta = json.load(open(os.path.join(work, 'meta.json')))
    names = sorted(f[:-4] for f in os.listdir(wavs) if f.endswith('.wav'))
    X, sr = {}, None
    for nm in names:
        X[nm], sr = read_wav(os.path.join(wavs, nm + '.wav'))
    L = min(len(v) for v in X.values())
    X = {k: v[:L] for k, v in X.items()}
    wins = note_windows(meta, sr, L)
    res = dict(sr=sr, seconds=round(L / sr, 2))

    def rel_db(a, b):        # energy of a relative to b (raw samples, both channels)
        return round(db10(float(np.sum(a * a))) - db10(float(np.sum(b * b))), 2)

    fams = [f for f in FAMILIES if 'd2_' + f in X]
    mics = [m for m in MICS if 'd2_mic_' + m in X]
    X['sumFam'] = sum(X['d2_' + f] for f in fams)
    X['sumMic'] = sum(X['d2_mic_' + m] for m in mics)
    ref = X[REF]
    b = res['baseline'] = dict(
        families=fams, mics=mics,
        refBminusA_db=rel_db(X[REFB] - ref, ref), refMaxAbsDiff=float(np.max(np.abs(X[REFB] - ref))),
        refPeak=float(np.max(np.abs(ref))),
        sumFamMinusRef_db=rel_db(X['sumFam'] - ref, ref), sumFamMaxAbsDiff=float(np.max(np.abs(X['sumFam'] - ref))),
        sumMicMinusRef_db=rel_db(X['sumMic'] - ref, ref), sumMicMaxAbsDiff=float(np.max(np.abs(X['sumMic'] - ref))),
        shippedBminusA_db=rel_db(X['origB'] - X['origA'], X['origA']) if 'origB' in X else None,
        refMinusShipped_db=rel_db(ref - X['origA'], X['origA']) if 'origA' in X else None,
        refLevelVsShipped_db=rel_db(ref, X['origA']) if 'origA' in X else None)
    if 'd2_fingB' in X:
        b['fingBminusA_db'] = rel_db(X['d2_fingB'] - X['d2_fing'], X['d2_fing'])
    b['familyLevel_db'] = {f: rel_db(X['d2_' + f], ref) for f in fams}
    b['micLevel_db'] = {m: rel_db(X['d2_mic_' + m], ref) for m in mics}
    K = {k: k_weighted(v, sr) for k, v in X.items() if k in (REF, REFB, 'sumFam', 'sumMic') or k.startswith('d2_')}
    K['resFam'] = k_weighted(X['sumFam'] - ref, sr)
    K['resMic'] = k_weighted(X['sumMic'] - ref, sr)
    K['resBase'] = k_weighted(X[REFB] - ref, sr)
    E = {k: energies(v, wins) for k, v in K.items()}
    env = {}
    for k in (REF, REFB, 'sumFam', 'sumMic', 'origA', 'origB'):
        if k in X:
            e, first, hop = perceived_envelope(X[k], sr)
            env[k] = swell(e, first, hop, meta)
    per = []
    for i, n in enumerate(meta['notes']):
        r = dict(i=i, kind=n['kind'], phrase=n['phrase'], slur=n['slur'], p=n['p'], v=n['v'], on=n['on'],
                 ioi=n.get('ioi'), d=n['d'])
        o, ow = E[REF][0][i], E[REF][1][i]
        r['heardDb'] = round(db10(o), 2)
        for k in ('resFam', 'resMic', 'resBase'):
            r[k + 'Heard'] = round(db10(E[k][0][i]) - db10(o), 2)
            r[k + 'Whole'] = round(db10(E[k][1][i]) - db10(ow), 2)
        for f in fams:
            r['share_' + f] = round(db10(E['d2_' + f][0][i]) - db10(o), 2)
        for m in mics:
            r['mic_' + m] = round(db10(E['d2_mic_' + m][0][i]) - db10(o), 2)
        for k, v in env.items():
            r['swell_' + k] = round(v[i], 3)
        per.append(r)
    res['notes'] = per
    json.dump(res, open(os.path.join(work, 'analysis.json'), 'w'))
    print(json.dumps(res['baseline'], indent=1))
    summarize(res)


def q(v, f):
    v = sorted(v)
    return v[min(len(v) - 1, int(f * len(v)))] if v else None


def summarize(res):
    notes = [n for n in res['notes'] if n['heardDb'] > -200]
    for k in ('resFamHeard', 'resMicHeard', 'resBaseHeard', 'resFamWhole', 'resMicWhole', 'resBaseWhole'):
        v = [n[k] for n in notes if k in n]
        print(k, 'median', round(st.median(v), 1), 'p90', round(q(v, 0.9), 1), 'max', round(max(v), 1))
    for a, b in (('swell_sumFam', 'swell_' + REF), ('swell_sumMic', 'swell_' + REF), ('swell_' + REFB, 'swell_' + REF),
                 ('swell_origB', 'swell_origA'), ('swell_' + REF, 'swell_origA')):
        v = [abs(n[a] - n[b]) for n in notes if a in n and b in n]
        if v:
            print(a, '-', b, '|diff| median', round(st.median(v), 3), 'p90', round(q(v, 0.9), 3), 'max', round(max(v), 3))
    fams = res['baseline']['families']
    for kind in ('whence', 'fast', 'med', 'slow'):
        ns = [n for n in notes if n['kind'] == kind and n['slur'] >= 0]
        print(kind, len(ns), 'notes; share of the heard window (dB re all), median [p10, p90]')
        for f in fams:
            v = [n['share_' + f] for n in ns]
            print('   %-11s %7.1f [%6.1f, %6.1f]' % (f, st.median(v), q(v, 0.1), q(v, 0.9)))


# --- per-layer prediction (Linux) -----------------------------------------------------------------------------------

def features(notes, i):
    """a note's context: its own and its neighbours' pitch, interval, velocity and gaps (0 outside the phrase)"""
    n = notes[i]

    def nb(k):
        j = i + k
        return notes[j] if 0 <= j < len(notes) and notes[j]['phrase'] == n['phrase'] else None
    f = []
    for k in (-2, -1, 0, 1, 2):
        m, mp = nb(k), nb(k - 1)
        if m is None:
            f += [0, 0, 0, 0, 0, 0]
            continue
        f += [1, m['p'], (m['p'] - mp['p']) if mp else 0, abs(m['p'] - mp['p']) if mp else 0, m['v'],
              (m['on'] - mp['on']) if mp else 2.0]
    k = sum(1 for j in range(i) if notes[j]['phrase'] == n['phrase'])
    f += [k, n['slur'] >= 0 and (k - 1) % 4 if k else -1]
    return f


def shapes(notes, vals):
    """values minus their slur's mean, over notes in slurs of four (the phrases' first, spiccato note left out)"""
    by = {}
    for n, v in zip(notes, vals):
        if n['slur'] >= 0 and n['kind'] != 'whence':
            by.setdefault((n['phrase'], n['slur']), []).append(v)
    mean = {k: sum(v) / len(v) for k, v in by.items()}
    return [v - mean[(n['phrase'], n['slur'])] if n['slur'] >= 0 and n['kind'] != 'whence' else None
            for n, v in zip(notes, vals)]


def scores(notes, meas, pred, k=1.0):
    import numpy as np
    sm, sp = shapes(notes, meas), shapes(notes, pred)
    idx = [i for i, v in enumerate(sm) if v is not None]
    a, b = np.array([sm[i] for i in idx]), np.array([sp[i] for i in idx])
    by = {}
    for i in idx:
        by.setdefault((notes[i]['phrase'], notes[i]['slur']), []).append(i)
    rng_unc = [max(sm[i] for i in g) - min(sm[i] for i in g) for g in by.values()]
    rng_cor = [max(sm[i] - k * sp[i] for i in g) - min(sm[i] - k * sp[i] for i in g) for g in by.values()]
    return dict(n=len(idx), r=round(float(np.corrcoef(a, b)[0, 1]), 3), residSd=round(float(np.std(a - k * b)), 2),
                measSd=round(float(np.std(a)), 2), rangeUnc=round(st.median(rng_unc), 2),
                rangeCorr=round(st.median(rng_cor), 2), k=round(k, 2))


def fit_k(notes, meas, pred):
    import numpy as np
    sm, sp = shapes(notes, meas), shapes(notes, pred)
    idx = [i for i, v in enumerate(sm) if v is not None]
    a, b = np.array([sm[i] for i in idx]), np.array([sp[i] for i in idx])
    return float(np.dot(a, b) / max(np.dot(b, b), 1e-9))


def bands(work, wavs):
    """the perceived-loudness model's band powers of every family render, power-summed, compressed and smoothed as
    loudness.py does: each note's swell from the layers' sum in power (no cross terms) against the mix's own; and the
    band powers kept (log10, float16) in <work>/bands.npz for the models"""
    import numpy as np
    from superposition_scan import band_powers, compress
    res = json.load(open(os.path.join(work, 'analysis.json')))
    meta = json.load(open(os.path.join(work, 'meta.json')))
    fams = [f for f in res['baseline']['families'] if res['baseline']['familyLevel_db'][f] > -200]
    B = {}
    for k in [REF] + ['d2_' + f for f in fams]:
        x, sr = read_wav(os.path.join(wavs, k + '.wav'))
        B[k], first, hop = band_powers(x, sr)
    P = sum(B['d2_' + f] for f in fams)
    env_sum, env_ref = compress(P, hop), compress(B[REF], hop)
    s_sum, s_ref = swell(env_sum, first, hop, meta), swell(env_ref, first, hop, meta)
    np.savez_compressed(os.path.join(work, 'bands.npz'), first=first, hop=hop,
                        **{k: np.log10(np.maximum(v, 1e-12)).astype(np.float16) for k, v in B.items()})
    notes = res['notes']
    sel = [i for i, n in enumerate(notes) if n['kind'] != 'whence']
    tn = [notes[i] for i in sel]
    out = dict(compressRef=scores(tn, [notes[i]['swell_' + REF] for i in sel], [s_ref[i] for i in sel]),
               powerSumOfLayers=scores(tn, [notes[i]['swell_' + REF] for i in sel], [s_sum[i] for i in sel]))
    print(json.dumps(out, indent=1))
    json.dump(out, open(os.path.join(work, 'bands.json'), 'w'))
    return out


def window_loudness(E):
    """loudness.py's compression of one band-power vector (no smoothing), ~dB"""
    import numpy as np
    a = 1e-3
    return 33.2 * np.log10(np.maximum(((E + a) ** 0.3 - a ** 0.3).sum(axis=-1), 1e-12))


def model_bands(work):
    """per-layer models in the loudness domain: each layer's mean band powers over a note's heard window (log10,
    every band) predicted from the note's context (random forest, even phrases), the layers summed in power and
    compressed; scored on the odd phrases against the perceived swell. Ceilings: the measured window vectors so summed,
    and the mix's own window vector."""
    import numpy as np
    from sklearn.ensemble import RandomForestRegressor
    res = json.load(open(os.path.join(work, 'analysis.json')))
    notes = res['notes']
    z = np.load(os.path.join(work, 'bands.npz'))
    first, hop = float(z['first']) / 1000, float(z['hop']) / 1000
    keys = [k for k in z.files if k.startswith('d2_') and k != REF]
    W = {}
    for k in keys + [REF]:
        Bk = 10.0 ** z[k].astype(np.float64)
        rows = []
        for n in notes:
            a = n['on'] + n['d']
            i0 = int(round((a - first) / hop))
            rows.append(Bk[i0:i0 + int(round(WIN / hop))].mean(axis=0))
        W[k] = np.array(rows)
    X = np.array([features(notes, i) for i in range(len(notes))], dtype=float)
    meas = [n['swell_' + REF] for n in notes]
    out = {}
    for kind in ('fast', 'all'):
        sel = [i for i, n in enumerate(notes) if n['kind'] != 'whence' and (kind == 'all' or n['kind'] == kind)]
        train = [i for i in sel if notes[i]['phrase'] % 2 == 0]
        test = [i for i in sel if notes[i]['phrase'] % 2 == 1]
        tn, trn = [notes[i] for i in test], [notes[i] for i in train]
        o = out[kind] = {}
        mix = window_loudness(W[REF])
        lay = window_loudness(sum(W[k] for k in keys))
        o['ceilingMixWindow'] = scores(tn, [meas[i] for i in test], [mix[i] for i in test])
        o['ceilingLayersWindow'] = scores(tn, [meas[i] for i in test], [lay[i] for i in test])
        P = 0
        for k in keys:
            Y = np.log10(np.maximum(W[k], 1e-12))
            rf = RandomForestRegressor(n_estimators=300, min_samples_leaf=2, random_state=1).fit(X[train], Y[train])
            Pk = 10.0 ** rf.predict(X)
            P = P + Pk
            lm, lp = window_loudness(W[k]), window_loudness(Pk)
            o.setdefault('layerShape', {})[k[3:]] = scores(tn, [lm[i] for i in test], [lp[i] for i in test])
        pred = window_loudness(P)
        kk = fit_k(trn, [meas[i] for i in train], [pred[i] for i in train])
        o['perLayerBands'] = scores(tn, [meas[i] for i in test], [pred[i] for i in test], kk)
        rf = RandomForestRegressor(n_estimators=300, min_samples_leaf=2, random_state=1).fit(X[train], np.array(meas)[train])
        d = rf.predict(X)
        kk = fit_k(trn, [meas[i] for i in train], [d[i] for i in train])
        o['directForest'] = scores(tn, [meas[i] for i in test], [d[i] for i in test], kk)
        print(kind, json.dumps(o))
    json.dump(out, open(os.path.join(work, 'model_bands.json'), 'w'))
    return out


def model(work):
    """per-layer models of each note's heard-window level (K-weighted energy, dB), fitted on the even phrases,
    combined as a power sum, scored on the odd ones against the perceived swell (shapes within slurs)"""
    import numpy as np
    from sklearn.ensemble import GradientBoostingRegressor
    res = json.load(open(os.path.join(work, 'analysis.json')))
    notes = res['notes']
    fams = res['baseline']['families']
    X = np.array([features(notes, i) for i in range(len(notes))], dtype=float)
    meas = [n['swell_' + REF] for n in notes]
    tot = [n['heardDb'] for n in notes]
    # a layer's level, floored 60 dB under the note's total (silent layers: that floor); layers silent throughout left out
    layer = {f: [n['heardDb'] + max(n['share_' + f], -60.0) for n in notes] for f in fams}
    fams = [f for f in fams if max(n['share_' + f] for n in notes) > -60]
    out = {}
    for kind in ('fast', 'all'):
        sel = [i for i, n in enumerate(notes) if n['kind'] != 'whence' and (kind == 'all' or n['kind'] == kind)]
        train = [i for i in sel if notes[i]['phrase'] % 2 == 0]
        test = [i for i in sel if notes[i]['phrase'] % 2 == 1]
        tn = [notes[i] for i in test]
        trn = [notes[i] for i in train]
        o = out[kind] = {}
        # upper bounds: the measured layers' power sum, the measured total energy
        psum = [10 * math.log10(sum(10 ** (layer[f][i] / 10) for f in fams)) for i in range(len(notes))]
        o['layersMeasuredPowerSum'] = scores(tn, [meas[i] for i in test], [psum[i] for i in test])
        o['totalEnergyMeasured'] = scores(tn, [meas[i] for i in test], [tot[i] for i in test])
        # per-layer models
        pred_l = {}
        for f in fams:
            y = np.array(layer[f])
            g = GradientBoostingRegressor(n_estimators=300, max_depth=3, learning_rate=0.05, subsample=0.8,
                                          random_state=1).fit(X[train], y[train])
            pred_l[f] = g.predict(X)
            o.setdefault('layerR', {})[f] = round(float(np.corrcoef(y[test], pred_l[f][test])[0, 1]), 3)
        comb = [10 * math.log10(sum(10 ** (pred_l[f][i] / 10) for f in fams)) for i in range(len(notes))]
        k = fit_k(trn, [meas[i] for i in train], [comb[i] for i in train])
        o['perLayer'] = scores(tn, [meas[i] for i in test], [comb[i] for i in test], k)
        o['perLayerUnscaled'] = scores(tn, [meas[i] for i in test], [comb[i] for i in test])
        # one model of the perceived swell itself, same features
        g = GradientBoostingRegressor(n_estimators=300, max_depth=3, learning_rate=0.05, subsample=0.8,
                                      random_state=1).fit(X[train], np.array(meas)[train])
        direct = g.predict(X)
        k = fit_k(trn, [meas[i] for i in train], [direct[i] for i in train])
        o['direct'] = scores(tn, [meas[i] for i in test], [direct[i] for i in test], k)
        if 'swell_origA' in notes[0] and 'swell_origB' in notes[0]:
            o['shippedTakeVsTake'] = scores(tn, [notes[i]['swell_origA'] for i in test],
                                            [notes[i]['swell_origB'] for i in test])
        print(kind, json.dumps(o, indent=1))
    json.dump(out, open(os.path.join(work, 'model.json'), 'w'))
    return out


# --- probes of the group data (Linux) -------------------------------------------------------------------------------

PROBES = (('d2_fx9_portFast', 'd2_portFast', 'fx type 9 header float 10.67 dB -> 0 dB in PORTAMENTO FAST'),
          ('d2_fx3_fing', 'd2_fing', 'fx type 3 header float -2.16 dB -> 0 dB in FING'),
          ('d2_fx4_sustain', 'd2_sustain', 'fx type 4 header float (-3.16 .. +4.82 dB by group) -> 0 dB in sustains'),
          ('d2_z_fing', 'd2_fing', 'every fm7_* modulation of FING at intensity 0'),
          ('d2_z_portFast', 'd2_portFast', 'every fm7_* modulation of PORTAMENTO FAST at intensity 0'))


def frame_db(y, sr, hop=0.005):
    import numpy as np
    n = int(sr * hop)
    m = len(y) // n
    return 10 * np.log10(np.maximum((y[:m * n] ** 2).reshape(m, n).sum(axis=1), 1e-20))


def probes(work, wavs):
    """each probe render against its family's render: level change overall, per note (heard window) by IOI, and the
    mean envelope after the note-on (5 ms frames) by IOI"""
    import numpy as np
    from loudness import k_weighted
    meta = json.load(open(os.path.join(work, 'meta.json')))
    notes = meta['notes']
    out = {}
    for a, b, what in PROBES:
        if not (os.path.exists(os.path.join(wavs, a + '.wav')) and os.path.exists(os.path.join(wavs, b + '.wav'))):
            continue
        xa, sr = read_wav(os.path.join(wavs, a + '.wav'))
        xb, _ = read_wav(os.path.join(wavs, b + '.wav'))
        L = min(len(xa), len(xb))
        ya, yb = k_weighted(xa[:L], sr), k_weighted(xb[:L], sr)
        wins = note_windows(meta, sr, L)
        ea, eb = energies(ya, wins), energies(yb, wins)
        fa, fb = frame_db(ya, sr), frame_db(yb, sr)
        o = out[a] = dict(what=what, overallDb=round(db10(float(np.sum(ya ** 2))) - db10(float(np.sum(yb ** 2))), 2))
        for ioi in (0.136, 0.25, 0.5):
            idx = [i for i, n in enumerate(notes) if n.get('ioi') == ioi and n['slur'] >= 0 and eb[0][i] > 0]
            d = [db10(ea[0][i]) - db10(eb[0][i]) for i in idx]
            env_a = np.median([fa[int(notes[i]['on'] / 0.005):int(notes[i]['on'] / 0.005) + 100] for i in idx], axis=0)
            env_b = np.median([fb[int(notes[i]['on'] / 0.005):int(notes[i]['on'] / 0.005) + 100] for i in idx], axis=0)
            o['ioi%d' % round(ioi * 1000)] = dict(
                heardMedianDb=round(st.median(d), 2), heardP10=round(q(d, 0.1), 2), heardP90=round(q(d, 0.9), 2),
                envDiffDbEvery25ms=[round(float(x), 1) for x in (env_a - env_b)[::5]],
                envProbeDbEvery25ms=[round(float(x), 1) for x in env_a[::5]],
                envBaseDbEvery25ms=[round(float(x), 1) for x in env_b[::5]])
        print(a, json.dumps(o))
    json.dump(out, open(os.path.join(work, 'probes.json'), 'w'))
    return out


ABOUT = ('SSO Celli - Performance: does Kontakt\'s output equal the sum of its sample groups played separately? '
         'tools/soundlibraries/layer_isolation.py (its header has the method and result). Measured 2026-10-13 on the '
         'Windows test VM: kthost (offline VST 3 host), Kontakt 8 + SSO, measurement-only copies of MuseScore\'s resaved '
         'setup (zone volumes 0 outside a family; d2: random modulation sources and pitch LFOs at intensity 0 so two '
         'renders agree to -99.5 dB), 44.1 kHz; MuseScore\'s events for Whence 12-TET bars 1-4 (Celli) and '
         'transition_level_scan.py run_phrases (vc) 2-29: 20 phrases of 136 ms notes, 4 of 250 ms, 4 of 500 ms, '
         'CC1 64, CC11 127. Levels: K-weighted energy in each note\'s heard window (136.4 ms from note-on + the map\'s '
         'legatoDelay), dB re the whole mix; perceived: loudness.py\'s swell. Measurement only: the renderer does not '
         'read this file.')


def summary(work, out_json):
    res = json.load(open(os.path.join(work, 'analysis.json')))
    b = res['baseline']
    notes = res['notes']

    def dist(v):
        return dict(median=round(st.median(v), 1), p10=round(q(v, 0.1), 1), p90=round(q(v, 0.9), 1), max=round(max(v), 1))
    lin = dict(
        refTakeBminusA_db=b['refBminusA_db'], sumOfFamiliesMinusRef_db=b['sumFamMinusRef_db'],
        sumOfMicsMinusRef_db=b['sumMicMinusRef_db'], familyTakeBminusA_db=b.get('fingBminusA_db'),
        maxAbsDiff=dict(refTakes=b['refMaxAbsDiff'], sumFamilies=b['sumFamMaxAbsDiff'], sumMics=b['sumMicMaxAbsDiff'],
                        refPeak=b['refPeak']),
        perNoteHeardWindow_db=dict(sumFamilies=dist([n['resFamHeard'] for n in notes]),
                                   sumMics=dist([n['resMicHeard'] for n in notes]),
                                   refTakes=dist([n['resBaseHeard'] for n in notes])),
        perNoteSwellAbsDiff=dict(
            sumFamiliesVsRef=dist([abs(n['swell_sumFam'] - n['swell_' + REF]) for n in notes]),
            shippedTakeBvsA=dist([abs(n['swell_origB'] - n['swell_origA']) for n in notes]),
            refVsShipped=dist([abs(n['swell_' + REF] - n['swell_origA']) for n in notes])),
        shippedTakeBminusA_db=b['shippedBminusA_db'], refLevelVsShipped_db=b['refLevelVsShipped_db'])
    shares = {}
    for kind in ('whence', 'fast', 'med', 'slow'):
        ns = [n for n in notes if n['kind'] == kind and n['slur'] >= 0]
        shares[kind] = dict(notes=len(ns), **{f: dict(median=round(max(st.median([n['share_' + f] for n in ns]), -99), 1),
                                                       p10=round(max(q([n['share_' + f] for n in ns], 0.1), -99), 1),
                                                       p90=round(max(q([n['share_' + f] for n in ns], 0.9), -99), 1))
                                              for f in b['families']})
    out = dict(about=ABOUT, linearity=lin, mics=b['micLevel_db'], familyLevel_db=b['familyLevel_db'],
               layerShareOfHeardWindow_db=shares)
    for k, f in (('modelsEnergy', 'model.json'), ('modelsBands', 'model_bands.json'), ('probes', 'probes.json'),
                 ('powerSumCeiling', 'bands.json')):
        p = os.path.join(work, f)
        if os.path.exists(p):
            out[k] = json.load(open(p))
    if 'modelsBands' in out:
        for kind in out['modelsBands'].values():
            kind.pop('layerShape', None)
    json.dump(out, open(out_json, 'w'), indent=1)
    print('written', out_json)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest='cmd', required=True)
    a = sp.add_parser('setups')
    a.add_argument('src')
    a.add_argument('out')
    a.add_argument('--det', type=int, default=0)
    a.add_argument('--only', nargs='*')
    a.add_argument('--fxgain')
    a.add_argument('--zero')
    a = sp.add_parser('events')
    a.add_argument('dump')
    a.add_argument('work')
    a = sp.add_parser('probes')
    a.add_argument('work')
    a.add_argument('wavs')
    a = sp.add_parser('bands')
    a.add_argument('work')
    a.add_argument('wavs')
    a = sp.add_parser('modelbands')
    a.add_argument('work')
    a = sp.add_parser('summary')
    a.add_argument('work')
    a.add_argument('-o', default=os.path.join(HERE, 'sso_layer_isolation.json'))
    a = sp.add_parser('model')
    a.add_argument('work')
    a = sp.add_parser('analyze')
    a.add_argument('work')
    a.add_argument('wavs')
    a.add_argument('-o', default=None)
    args = ap.parse_args()
    if args.cmd == 'setups':
        setups(args.src, args.out, args.det, args.only, args.fxgain, args.zero)
    elif args.cmd == 'events':
        events(args.dump, args.work)
    elif args.cmd == 'probes':
        probes(args.work, args.wavs)
    elif args.cmd == 'bands':
        bands(args.work, args.wavs)
    elif args.cmd == 'modelbands':
        model_bands(args.work)
    elif args.cmd == 'summary':
        summary(args.work, args.o)
    elif args.cmd == 'model':
        model(args.work)
    else:
        analyze(args.work, args.wavs, args.o)



if __name__ == '__main__':
    main()
