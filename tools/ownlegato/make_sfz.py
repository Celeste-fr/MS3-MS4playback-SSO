#!/usr/bin/env python3
"""The own-legato pilot's SFZ instrument (sfizz), cut from our own recordings of SSO's Violas - Performance (record.py ->
run_vm.cmd -> measure.py), so that a legato transition ARRIVES a fixed, known time after its note-on.

    make_sfz.py <index.json> [<index.json> ...] --audio <dir>[,<dir>...] --out <dir> [--lt ms] [--la ms] [--name violas_performance]
               [--reuse]

Writes <out>/<name>.sfz, <out>/samples/*.flac (24-bit) and <out>/<name>.json (the cut: every region's measured leave /
arrival / onset, the latencies, what was left out). The SFZ names its samples relative to itself: <out> is the owner's
(the recordings are the owner's, never committed).

Why (the owner, 2026-10-16): SSO's Performance Legato decides a transition's timing itself (its script): arrivals
scatter about +-100 ms by key and interval and jump with the send time, so MuseScore can't place them. Recorded once
and cut here, each transition's arrival is known to the sample, so the patch can put it where it is asked.

DESIGN
  Measured (measure.py, the heard check's own rules): a transition's LEAVE (last pYIN frame within 30 cents of the note
  before) and ARRIVAL (first of 3 frames within 30 cents of the new pitch); an attack's ONSET (perceived envelope, -15 dB
  under its peak over 550 ms, less 30 ms). SSO's recordings show the transition as: the note before held on in the
  transition sample for ~150-300 ms after the note-on, then a short glide (the leave -> arrival time D).

  Two fixed latencies (the map would carry them; MuseScore sends every note-on that much before its beat):
    L_t  a TRANSITION (a note-on while the note before is held: trigger=legato) arrives L_t after its note-on.
         Each transition region's sample is cut CUT_PRE_MS before its arrival and played from offset CUT_PRE_MS - L_t,
         so the arrival lands at exactly L_t. L_t = the largest D + PRE_MS over the recordings (--lt overrides): every region
         then begins with at least PRE_MS of the note before, still settled, which is where the hand-over happens.
    L_a  an ATTACK (a note after silence: trigger=first) is heard (its onset) L_a after its note-on. Each attack region
         starts at its recorded note-on and waits delay = L_a - onset (L_a = the largest onset; --la overrides).
  Hand-over: the new note's region fades in over XF_MS (ampeg_attack) from its note-on; the old voice plays on until
    its own note-off (MuseScore's overlap, 24 ms later) and fades over REL_MS (ampeg_release): a crossfade between two
    takes of the note before's settled sustain. (First built with off_by / off_mode=time, off_time 15 ms: the old voice
    fell faster than the new one rose, an 8-11 dB dip at every hand-over.)
  No ghost releases in legato: the old note's note-off (MuseScore's overlap, 24 ms after the new note-on) must not play
    its release. sfizz's rt_dead=0 rule doesn't stop it (tried: the release still played with the note-off 100 ms after
    the hand-over), so the release regions play only with CC68 (the legato footswitch) up: MuseScore sends CC68 127
    before a transition's note-on and CC68 0 just before the last note-off of a legato line (score_events.py).
  The transition by the note before: sw_previous=<its key> (sfizz: the last note-on's key), the type by velocity as SSO's
    (the map's transitions=: portamento 1-19, fingered 20-84, bowed 85-127); attacks by velocity as SSO's (attacks=:
    smooth 1-9, spiccato 10-116, accented 117-127).
  Sustain: each region loops its own settled part (loop_continuous from the arrival + LOOP_FROM_MS (an attack: + its
    onset) to its end, loop_crossfade up to 0.25 s), so a note held longer than the recording (1.5 s) goes on.
  Dynamics: one layer per recorded CC1 value, crossfaded by CC1 (xfin / xfout, power curve): at a recorded value exactly
    one layer plays (Whence's Performance notes: CC1 64). CC11 is amplitude (amplitude_oncc11=100: amplitude = CC11 /
    127, SSO's measured law within ~1-2 dB, probe P5); velocity selects, never scales (amp_veltrack=0).
  Round robins: the takes of a transition (record.py --rr) as seq_length / seq_position (sfizz counts a sequence per
    region by the key's note-ons: a key's takes alternate). SSO itself picks its transitions at random (probe: no two of
    34 takes of one transition null below -40 dB, not even across processes); its attacks and portamento are
    deterministic (one sample each).
  Releases: trigger=release regions per key and CC1 layer, the recorded sound after the note-off of a held smooth note
    (3 s), fading in over REL_MS while the sustain fades out over REL_MS (ampeg_release).
  Microtones: pitch bend +-100 cents (SSO's patch: 99.6), applied to every voice; MuseScore bends a quarter tone.
  Gaps: a previous key never recorded before a key plays that key's smooth attack, delayed or cut so its onset lands at
    L_t (a re-attack in time, not a transition); a missing type of a recorded pair plays a recorded type of that pair.

sfizz opcodes used (all in sfizz 1.2.3's supported list, checked in its source): sample, key, pitch_keycenter, trigger
(first / legato / release), sw_previous, lovel / hivel, seq_length / seq_position, xfin_locc1 / xfin_hicc1 / xfout_locc1 /
xfout_hicc1, xf_cccurve, amplitude_oncc11, amp_veltrack, delay, offset, loop_mode, loop_start, loop_end, loop_crossfade,
ampeg_attack, ampeg_release, group, hicc68, bend_up, bend_down, default_path.
"""
import argparse
import json
import math
import os
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import SR, stereo, load_json  # noqa: E402

PRE_MS = 30.0         # at least this much of the note before (settled) ahead of a transition's leave in each region
CUT_PRE_MS = 300.0    # each transition sample is cut this long before its arrival; the region's offset skips to L_t
                      # before it (so L_t is a text change of the SFZ, --lt)
XF_MS = 30.0          # the hand-over: the new region's fade-in
REL_MS = 50.0         # the old voice's release at its note-off (24 ms after the new note-on) and the sustain ->
                      # release crossfade
LOOP_FROM_MS = 350.0  # a transition's loop starts this long after its arrival (the glide's overshoot settled)
REL_LEN_S = 3.0
SFIZZ_GAIN_DB = 11.52 # sfizz 1.2.3 plays a sample 11.52 dB under its file at its defaults (CC7 100, CC10 64; measured
                      # with sfzrender: a smooth attack's sustain against its sample, both channels): global volume= puts
                      # it back, so our patch plays at the recordings' (SSO's) level
TYPES = {'portamento': (1, 19), 'fingered': (20, 84), 'bowed': (85, 127)}
ATTACKS = {'smooth': (1, 9), 'spiccato': (10, 116), 'accented': (117, 127)}
KEY_LO, KEY_HI = 40, 90   # previous keys covered by the gap rule


def hand(trigger):
    """a <group> header: the hand-over (see DESIGN). The old voice ends at its own note-off (MuseScore's overlap) with
    ampeg_release REL_MS; a transition region fades in over XF_MS, an attack over 2 ms"""
    return '<group> group=1 ampeg_attack=%s ampeg_release=%s trigger=%s' % (
        (XF_MS if trigger == 'legato' else 2.0) / 1000, REL_MS / 1000, trigger)


def layers(cc1s):
    """CC1 crossfade ranges per recorded layer: full below the lowest / above the highest, linear between neighbours"""
    cc1s = sorted(cc1s)
    out = {}
    for i, c in enumerate(cc1s):
        o = {}
        if i > 0:
            o.update(xfin_locc1=cc1s[i - 1], xfin_hicc1=c)
        if i + 1 < len(cc1s):
            o.update(xfout_locc1=c, xfout_hicc1=cc1s[i + 1])
        out[c] = o
    return out


def opc(d):
    return ' '.join('%s=%s' % (k, (('%.6f' % v).rstrip('0').rstrip('.') if isinstance(v, float) else v)) for k, v in d.items())


class Cutter:
    def __init__(self, audio, out):
        self.audio, self.out, self.cache = audio, out, {}
        os.makedirs(os.path.join(out, 'samples'), exist_ok=True)
        self.bytes = 0
        self.reuse = False

    def x(self, name):
        if name not in self.cache:
            if len(self.cache) > 40:      # (every render at once: ~3 GB of float32; the regions come in key order)
                self.cache.clear()
            cands = [os.path.join(d, name + e) for d in self.audio.split(',') for e in ('.flac', '.wav')]
            self.cache[name] = stereo(next(p for p in cands if os.path.exists(p)))
        return self.cache[name]

    def cut(self, file, t0, t1, name):
        p = os.path.join(self.out, 'samples', name + '.flac')
        if self.reuse and os.path.exists(p):
            self.bytes += os.path.getsize(p)
            return 'samples/%s.flac' % name, sf.info(p).frames / SR
        x = self.x(file)
        a, b = max(0, int(round(t0 * SR))), min(len(x), int(round(t1 * SR)))
        p = os.path.join(self.out, 'samples', name + '.flac')
        sf.write(p, x[a:b], SR, subtype='PCM_24')
        self.bytes += os.path.getsize(p)
        return 'samples/%s.flac' % name, (b - a) / SR


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('index', nargs='+')
    ap.add_argument('--audio', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--lt', type=float)
    ap.add_argument('--la', type=float)
    ap.add_argument('--name', default='violas_performance')
    ap.add_argument('--reuse', action='store_true', help='keep samples already cut (same cut, another --lt / --la)')
    a = ap.parse_args()
    items = []
    for p in a.index:
        items += load_json(p)['items']
    tr = [q for q in items if q['kind'] == 'transition' and q['arrive'] is not None and q['leave'] is not None]
    missing = [q for q in items if q['kind'] == 'transition' and (q['arrive'] is None or q['leave'] is None)]
    atk = [q for q in items if q['kind'] == 'attack' and q['file'] == 'atk' and q['onset'] is not None]
    D = [q['arrive'] - q['leave'] for q in tr]
    lt = a.lt if a.lt else math.ceil(max(D) + PRE_MS)
    sm = [q for q in atk if q['attack'] == 'smooth']
    la = a.la if a.la else math.ceil(max(q['onset'] for q in sm))
    cc1s = sorted({q['cc1'] for q in tr})
    lay = layers(cc1s)
    c = Cutter(a.audio, a.out)
    c.reuse = a.reuse
    lines = ['// own-legato pilot: Violas - Performance re-cut (tools/ownlegato/make_sfz.py; the design is in its header)',
             '// L_t = %d ms (a transition arrives L_t after its note-on), L_a = %d ms (an attack is heard L_a after it)'
             % (lt, la),
             '<control> default_path=./',
             '<global> volume=%.2f amp_veltrack=0 amplitude_oncc11=100 bend_up=100 bend_down=-100 xf_cccurve=power' % SFIZZ_GAIN_DB]
    regions, late = [], 0
    # transitions, grouped by (a, b, type, cc1): their takes are the round robins
    groups = {}
    for q in tr:
        groups.setdefault((q['a'], q['b'], q['type'], q['cc1']), []).append(q)
    lines.append(hand('legato'))
    pair_types = {}
    for (ka, kb, ty, cc1), qs in sorted(groups.items()):
        pair_types.setdefault((ka, kb), set()).add(ty)
        qs.sort(key=lambda q: (q['rep'] if q['rep'] is not None else 0, q['file'], q['seg']))
        for i, q in enumerate(qs):
            t_arr = q['on'] + q['arrive'] / 1000
            t0 = t_arr - CUT_PRE_MS / 1000
            head = (q['arrive'] - q['leave'])
            if head + PRE_MS > lt:
                late += 1
            t1 = (q['next'] - 0.005) if q['next'] else q['off']
            name = 't_%d_%d_%s_%d_%d' % (ka, kb, ty[:4], cc1, i)
            path, dur = c.cut(q['file'], t0, t1, name)
            ls = CUT_PRE_MS / 1000 + LOOP_FROM_MS / 1000
            le = dur - 0.03
            reg = dict(sample=path, key=kb, pitch_keycenter=kb, sw_previous=ka, lovel=TYPES[ty][0], hivel=TYPES[ty][1],
                       offset=int(round((CUT_PRE_MS - lt) / 1000 * SR)))
            if len(qs) > 1:
                reg.update(seq_length=len(qs), seq_position=i + 1)
            reg.update(lay[cc1])
            if le - ls > 0.2:
                reg.update(loop_mode='loop_continuous', loop_start=int(ls * SR), loop_end=int(le * SR),
                           loop_crossfade=round(min(0.25, (le - ls) / 2), 3))
            lines.append('<region> ' + opc(reg))
            regions.append(dict(kind='transition', a=ka, b=kb, type=ty, cc1=cc1, take=i, sample=path, leave=q['leave'],
                                arrive=q['arrive'], D=round(head, 1), level=q.get('level'), src='%s/%d/%d' % (q['file'], q['seg'], q['idx'])))
    # a missing type of a recorded pair: the nearest recorded type of that pair (by velocity band centre)
    centre = {t: (lo + hi) / 2 for t, (lo, hi) in TYPES.items()}
    for (ka, kb), have in sorted(pair_types.items()):
        for ty in TYPES:
            if ty in have:
                continue
            src = min(have, key=lambda h: abs(centre[h] - centre[ty]))
            for cc1 in cc1s:
                for r in [r for r in regions if r['kind'] == 'transition' and (r['a'], r['b'], r['type'], r['cc1']) == (ka, kb, src, cc1)]:
                    reg = dict(sample=r['sample'], key=kb, pitch_keycenter=kb, sw_previous=ka, lovel=TYPES[ty][0], hivel=TYPES[ty][1],
                               offset=int(round((CUT_PRE_MS - lt) / 1000 * SR)))
                    lines.append('<region> ' + opc(reg) + ' ' + opc(lay[cc1]) + '  // %s missing: %s' % (ty, src))
    # attacks after silence (trigger=first) and their releases
    lines.append(hand('first'))
    smooth_by = {}
    for q in atk:
        name = 'a_%s_%d_%d' % (q['attack'], q['key'], q['cc1'])
        path, dur = c.cut(q['file'], q['on'], q['off'], name)
        reg = dict(sample=path, key=q['key'], pitch_keycenter=q['key'], lovel=ATTACKS[q['attack']][0], hivel=ATTACKS[q['attack']][1])
        if q['attack'] == 'smooth':
            reg.update(lay[q['cc1']])
            reg['delay'] = round(max(0.0, (la - q['onset']) / 1000), 4)
            smooth_by[(q['key'], q['cc1'])] = (path, q['onset'], dur)
        ls, le = 2.0 if dur > 3 else max(0.6, q['onset'] / 1000 + 0.4), dur - 0.03
        if le - ls > 0.2:
            reg.update(loop_mode='loop_continuous', loop_start=int(ls * SR), loop_end=int(le * SR),
                       loop_crossfade=round(min(0.25, (le - ls) / 2), 3))
        lines.append('<region> ' + opc(reg))
        regions.append(dict(kind='attack', attack=q['attack'], key=q['key'], cc1=q['cc1'], sample=path, onset=q['onset'],
                            level=q.get('level')))
    lines.append('<group> group=2 trigger=release hicc68=63 ampeg_attack=%s ampeg_release=0.3' % (REL_MS / 1000))
    for q in [q for q in atk if q['attack'] == 'smooth']:
        name = 'r_%d_%d' % (q['key'], q['cc1'])
        path, dur = c.cut(q['file'], q['off'], q['off'] + REL_LEN_S, name)
        lines.append('<region> ' + opc(dict(sample=path, key=q['key'], pitch_keycenter=q['key'])) + ' ' + opc(lay[q['cc1']]))
    # gaps: a previous key never recorded before key b: b's smooth attack, its onset at L_t (delay or cut)
    lines.append(hand('legato'))
    keys = sorted({k for k, _ in smooth_by})
    gaps = 0
    for kb in keys:
        prev = {ka for (ka, b) in pair_types if b == kb}
        for ka in range(KEY_LO, KEY_HI + 1):
            if ka == kb or ka in prev:
                continue
            for cc1 in cc1s:
                if (kb, cc1) not in smooth_by:
                    continue
                path, onset, dur = smooth_by[(kb, cc1)]
                reg = dict(sample=path, key=kb, pitch_keycenter=kb, sw_previous=ka)
                if onset <= lt:
                    reg['delay'] = round((lt - onset) / 1000, 4)
                else:
                    reg['offset'] = int((onset - lt) / 1000 * SR)
                reg.update(lay[cc1])
                reg.update(loop_mode='loop_continuous', loop_start=int(2.0 * SR), loop_end=int((dur - 0.03) * SR),
                           loop_crossfade=0.25)
                lines.append('<region> ' + opc(reg))
                gaps += 1
    with open(os.path.join(a.out, a.name + '.sfz'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    summary = dict(lt=lt, la=la, preMs=PRE_MS, xfMs=XF_MS, relMs=REL_MS, cc1=cc1s, transitions=len(tr),
                   notMeasured=len(missing), lateHeads=late, gapRegions=gaps, attacks=len(atk), sampleBytes=c.bytes,
                   D=dict(median=float(np.median(D)), p90=float(np.percentile(D, 90)), max=float(max(D))),
                   regions=regions, missing=[dict(a=q['a'], b=q['b'], type=q['type'], cc1=q['cc1'], why=q['why']) for q in missing])
    with open(os.path.join(a.out, a.name + '.json'), 'w') as f:
        json.dump(summary, f, indent=0)
    print('L_t %d ms, L_a %d ms; %d transitions (%d not measured, %d with a head under %d ms), %d attacks, %d gap regions; '
          'samples %.1f MB' % (lt, la, len(tr), len(missing), late, PRE_MS, len(atk), gaps, c.bytes / 1e6))


if __name__ == '__main__':
    main()
