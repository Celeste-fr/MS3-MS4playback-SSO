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


def hand(trigger, xf=0.0):
    """a <group> header: the hand-over (see DESIGN). xf 0 (round 1): the old voice ends at its own note-off
    (MuseScore's overlap) with ampeg_release REL_MS; a transition region fades in over XF_MS, an attack over 2 ms.
    xf > 0 (ms, round 2): an equal-power crossfade: the old voice's release T = EP_T_PER_F * xf from its note-off,
    sent with the new note-on (sfizz's release falls 80 dB over T, exponentially: measured with sfzrender), the new
    region's fade-in sqrt(1 - 10^(-8 t / T)) baked into its sample (ep_fade)"""
    if xf:
        return '<group> group=1 ampeg_attack=0 ampeg_release=%s trigger=%s' % (EP_T_PER_F * xf / 1000, trigger)
    return '<group> group=1 ampeg_attack=%s ampeg_release=%s trigger=%s' % (
        (XF_MS if trigger == 'legato' else 2.0) / 1000, REL_MS / 1000, trigger)


EP_T_PER_F = 6.0      # round 2: the old voice's release T = 6 F: 13.3 dB down at F, the new region 0.2 dB under full


def ep_fade(xf):
    """the new region's equal-power fade-in against sfizz's exponential release of the old voice (T = EP_T_PER_F xf)"""
    T = EP_T_PER_F * xf / 1000
    return lambda t: np.sqrt(np.clip(1 - 10 ** (-8 * np.minimum(t, T) / T), 0, 1))


# SSO's sample sets by the time since the previous legato note-on (read from the patch's script, the legato-switch
# branch: bands 115 / 350 ms; runs only for intervals of 1-3 semitones) as sfizz's lotimer / hitimer (s since the
# previous voice start in the group: checked with sfzrender). A pair without a set plays the nearest set it has.
SETS = {'slow': dict(lotimer=0.35), 'fast': dict(lotimer=0.115, hitimer=0.35), 'runs': dict(hitimer=0.115)}
SET_FALLBACK = {'slow': ['slow', 'fast', 'runs'], 'fast': ['fast', 'runs', 'slow'], 'runs': ['runs', 'fast', 'slow']}


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

    def cut(self, file, t0, t1, name, gain=None):
        """the recording's [t0, t1] s as samples/<name>.flac; gain: a function of the time from the cut's start (s,
        numpy array) giving an amplitude factor, baked in (the hand-over's fade-in, the level match)"""
        p = os.path.join(self.out, 'samples', name + '.flac')
        if self.reuse and os.path.exists(p):
            self.bytes += os.path.getsize(p)
            return 'samples/%s.flac' % name, sf.info(p).frames / SR
        x = self.x(file)
        a, b = max(0, int(round(t0 * SR))), min(len(x), int(round(t1 * SR)))
        y = x[a:b]
        if gain is not None:
            y = y * gain(np.arange(len(y)) / SR)[:, None]
        sf.write(p, y, SR, subtype='PCM_24')
        self.bytes += os.path.getsize(p)
        return 'samples/%s.flac' % name, (b - a) / SR


LONG_TAIL_S = 0.3     # round 2: a fast / runs take is used only with this much of the new note recorded after its
                      # arrival (record.py fast's held notes, a run's last note): a run's inner notes end at the next
                      # note-on, too soon for a note held longer
LEVEL_MATCH_MS = 15.0 # round 2: the level at a join, RMS of the region's first LEVEL_MATCH_MS
LEVEL_MATCH_MAX_DB = 6.0
HEAD_MS = 30.0        # round 2 --head-check: the region's first HEAD_MS must be the note before at its pitch ...
HEAD_SHARE = 0.7      # ... in this share of pYIN's frames (a fast / runs group without such a take falls back to a set
                      # with one; a slow group keeps its best)
# round 2 --vel-sets: the set by velocity (MuseScore picks it by the note before's length: slow from 350 ms), each type's
# band of SSO's split in two; runs / fast by sfizz's timer (lotimer / hitimer 0.115 s) inside the fast band
VEL_SETS = {('portamento', 'slow'): (1, 9), ('portamento', 'fast'): (10, 19), ('fingered', 'slow'): (20, 52),
            ('fingered', 'fast'): (53, 84), ('bowed', 'slow'): (85, 97), ('bowed', 'fast'): (98, 110)}


CLICK_SPAN_S = 0.5    # round 2 --head-check also drops a take with a click in its first L + CLICK_SPAN_S: spectral flux
CLICK_Z = 6.0         # more than CLICK_Z SDs over its median (SSO's faulty samples: 54 -> 58 bowed's both takes 8.7-9.2;
                      # clean takes ~2-3)


def click_z(x):
    """the largest spectral-flux z of x (1024-point Hann, hop 128) against its own median and SD"""
    N, H = 1024, 128
    if len(x) < 4 * N:
        return 0.0
    fr = np.array([x[k:k + N] * np.hanning(N) for k in range(0, len(x) - N, H)])
    S = np.log(np.abs(np.fft.rfft(fr, axis=1)) + 1e-9)
    fl = np.maximum(0, np.diff(S, axis=0)).sum(1)
    return float((fl.max() - np.median(fl)) / (fl.std() + 1e-9))


def lat(a, q):
    """the latency a take is cut for: L_slow for the slow set with --vel-sets, else L_t"""
    return (a.lt_slow or a.lt) if (a.vel_sets and q.get('set', 'slow') == 'slow') else a.lt


def round2(a, tr, lt, la, lay, c, lines, regions, atk):
    """Round 2's transition regions (see DESIGN, round 2): each take cut from L_t before its arrival (no offset: the fade
    is baked at the cut's start), sets by timers, the level matched at the join, re-attacks; returns pair_types (the
    gap rule's recorded pairs) and the count of heads shorter than PRE_MS."""
    rng = tuple(int(v) for v in a.keys.split('-')) if a.keys else None
    types = dict(TYPES)
    if a.reattack:
        types['bowed'] = (85, 110)
    use = []
    for q in tr:
        q.setdefault('set', 'slow')
        if rng and not rng[0] <= q['b'] <= rng[1]:
            continue
        if not a.sets and q['set'] != 'slow':
            continue
        end = (q['next'] - 0.005) if q['next'] else q['off']
        if q['set'] != 'slow' and end - (q['on'] + q['arrive'] / 1000) < LONG_TAIL_S:
            continue
        L = lat(a, q)
        if q['on'] + (q['arrive'] - L) / 1000 < q['aOn'] + 0.05:   # the cut would start in the note before's attack
            continue
        use.append((q, end))
    # the head: the note before must sound at its pitch where the region starts (its first HEAD_MS within 30 cents of
    # a in HEAD_SHARE of pYIN's frames), or the hand-over plays the recording's own unsettled context (a scoop: a fast
    # take whose note before was cut short, a slow glide longer than L): such takes are dropped while their group has
    # another
    if a.head_check:
        from common import f0 as pyin
        ok, nclick = [], 0
        for q, end in use:
            x = c.x(q['file'])
            t0 = q['on'] + (q['arrive'] - lat(a, q)) / 1000
            seg = x[int(t0 * SR):int((t0 + HEAD_MS / 1000) * SR) + 2048].mean(1).astype(np.float64)
            _, m = pyin(seg, min(q['a'], q['b']) - 3, max(q['a'], q['b']) + 3)
            m = m[:max(1, int(HEAD_MS / 1000 * SR / 128))]
            q['headShare'] = float(np.mean(np.abs(np.nan_to_num(m, nan=0) - q['a']) <= 0.30))
            q['clickZ'] = click_z(x[int(t0 * SR):int((t0 + lat(a, q) / 1000 + CLICK_SPAN_S) * SR)].mean(1))
            if q['clickZ'] > CLICK_Z:
                nclick += 1
                continue
            ok.append((q, end))
        best = {}
        for q, end in ok:
            k = (q['a'], q['b'], q['type'], q['cc1'], q['set'])
            best[k] = max(best.get(k, 0), q['headShare'])
        use = [(q, end) for q, end in ok if q['headShare'] >= HEAD_SHARE
               or (q['set'] == 'slow' and best[(q['a'], q['b'], q['type'], q['cc1'], q['set'])] < HEAD_SHARE)]
        print('head check: %d of %d takes kept (%d dropped for a click)' % (len(use), len(ok) + nclick, nclick))
    # the level at each join: the recording's RMS over the cut's first LEVEL_MATCH_MS; the reference for the note before
    # (key a): the median over every take leaving a, so every region starts at the level a typical one has there
    pre = {}
    for q, end in use:
        x = c.x(q['file'])
        t0 = q['on'] + (q['arrive'] - lat(a, q)) / 1000
        w = x[int(t0 * SR):int((t0 + LEVEL_MATCH_MS / 1000) * SR)]
        pre[id(q)] = 10 * np.log10(np.mean(w ** 2) + 1e-20)
    ref = {}
    for q, end in use:
        ref.setdefault((q['a'], q['cc1']), []).append(pre[id(q)])
    ref = {k: float(np.median(v)) for k, v in ref.items()}
    fade = ep_fade(a.xf) if a.xf else None
    groups = {}
    for q, end in use:
        groups.setdefault((q['a'], q['b'], q['type'], q['cc1'], q['set']), []).append((q, end))
    for qs in groups.values():
        qs.sort(key=lambda p: (p[0]['rep'] if p[0]['rep'] is not None else 0, p[0]['file'], p[0]['seg']))
    cut = {}
    late = [0]

    def cutg(key, Lms):
        """the takes of group key cut for latency Lms (cached)"""
        if (key, Lms) in cut:
            return cut[(key, Lms)]
        ka, kb, ty, cc1, st = key
        out = []
        for i, (q, end) in enumerate(groups[key]):
            t0 = q['on'] + (q['arrive'] - Lms) / 1000
            if t0 < q['aOn'] + 0.05:
                continue
            if q['arrive'] - q['leave'] + PRE_MS > Lms:
                late[0] += 1
            g0 = float(np.clip(ref[(ka, cc1)] - pre[id(q)], -LEVEL_MATCH_MAX_DB, LEVEL_MATCH_MAX_DB)) \
                if a.level_match and Lms == lat(a, q) else 0.0
            L = Lms / 1000

            def gain(t, g0=g0, L=L):
                g = 10 ** ((g0 * np.clip(1 - t / L, 0, 1)) / 20)
                return g * (fade(t) if fade else 1.0)
            name = 't2_%d_%d_%s_%d_%s_%d_L%d' % (ka, kb, ty[:4], cc1, st, i, Lms)
            path, dur = c.cut(q['file'], t0, end, name, gain if (fade or g0) else None)
            out.append(dict(sample=path, dur=dur, L=L))
            regions.append(dict(kind='transition', set=st, a=ka, b=kb, type=ty, cc1=cc1, take=i, sample=path, L=Lms,
                                leave=q['leave'], arrive=q['arrive'], D=round(q['arrive'] - q['leave'], 1),
                                levelMatchDb=round(g0, 2), headShare=q.get('headShare'),
                                src='%s/%d/%d' % (q['file'], q['seg'], q['idx'])))
        cut[(key, Lms)] = out
        return out
    lines.append(hand('legato', a.xf))
    pair_types = {}
    for (ka, kb, ty, cc1, st) in groups:
        pair_types.setdefault((ka, kb), set()).add(ty)
    centre = {t: (lo + hi) / 2 for t, (lo, hi) in types.items()}
    cc1s = sorted({k[3] for k in groups})
    emitted = 0
    have_at = {}
    for (ka, kb, ty, cc1, st) in groups:
        have_at.setdefault((ka, kb, cc1), set()).add(ty)
    for (ka, kb), have in sorted(pair_types.items()):
        for ty in types:
            for cc1 in cc1s:
                # a pair without takes at this CC1 layer: the nearest layer's, playing in this one
                src_cc1 = min((c for c in cc1s if (ka, kb, c) in have_at), key=lambda c: abs(c - cc1))
                hv = have_at[(ka, kb, src_cc1)]
                src_ty = ty if ty in hv else min(hv, key=lambda h: abs(centre[h] - centre[ty]))
                bands = ['slow', 'fast', 'runs'] if a.sets else ['slow']
                if abs(ka - kb) > 3 and a.sets:
                    bands = ['slow', 'fast']     # no runs set: the fast set below 115 ms too
                for band in bands:
                    Lb = (a.lt_slow or lt) if (band == 'slow' and a.vel_sets) else lt
                    src = next((s for s in SET_FALLBACK[band] if (ka, kb, src_ty, src_cc1, s) in groups
                                and cutg((ka, kb, src_ty, src_cc1, s), Lb)), None)
                    if src is None:
                        continue
                    if a.vel_sets:
                        # the set by velocity (MuseScore knows the note before's length), runs / fast by timer
                        vlo, vhi = VEL_SETS[(ty, 'slow' if band == 'slow' else 'fast')]
                        if ty == 'bowed' and band != 'slow' and not a.reattack:
                            vhi = 127
                        timer = {'runs': dict(hitimer=0.115), 'fast': dict(lotimer=0.115) if abs(ka - kb) <= 3 else {},
                                 'slow': {}}[band]
                    else:
                        vlo, vhi = types[ty]
                        timer = dict(SETS[band]) if a.sets else {}
                        if band == 'fast' and abs(ka - kb) > 3:
                            timer.pop('lotimer', None)
                    takes = cutg((ka, kb, src_ty, src_cc1, src), Lb)
                    for i, r in enumerate(takes):
                        reg = dict(sample=r['sample'], key=kb, pitch_keycenter=kb, sw_previous=ka, lovel=vlo, hivel=vhi)
                        reg.update(timer)
                        if len(takes) > 1:
                            reg.update(seq_length=len(takes), seq_position=i + 1)
                        reg.update(lay[cc1])
                        ls, le = r['L'] + LOOP_FROM_MS / 1000, r['dur'] - 0.03
                        if le - ls > 0.2:
                            reg.update(loop_mode='loop_continuous', loop_start=int(ls * SR), loop_end=int(le * SR),
                                       loop_crossfade=round(min(0.25, (le - ls) / 2), 3))
                        note = '' if (src_ty == ty and src == band and src_cc1 == cc1) else \
                            '  // %s %s cc1 %d from %s %s cc1 %d' % (ty, band, cc1, src_ty, src, src_cc1)
                        lines.append('<region> ' + opc(reg) + note)
                        emitted += 1
    if a.reattack:
        # a re-attack straight after a legato note: the spiccato / accented attack (record.py's atk) as a legato
        # region, its onset at L_t (delay L_t - onset); MuseScore sends the note before's note-off at the onset
        # (score_events.py --reattack), so the old voice hands over to the attack itself, no gap
        for q in atk:
            if q['attack'] not in ('spiccato', 'accented'):
                continue
            path = 'samples/a_%s_%d_%d.flac' % (q['attack'], q['key'], q['cc1'])
            c.cut(q['file'], q['on'], q['off'], 'a_%s_%d_%d' % (q['attack'], q['key'], q['cc1']))
            lo, hi = (111, 116) if q['attack'] == 'spiccato' else (117, 127)
            reg = dict(sample=path, key=q['key'], pitch_keycenter=q['key'], lovel=lo, hivel=hi,
                       delay=round(max(0.0, lt - q['onset']) / 1000, 4))
            dur = q['off'] - q['on']
            ls, le = max(0.6, q['onset'] / 1000 + 0.4), dur - 0.03
            if le - ls > 0.2:
                reg.update(loop_mode='loop_continuous', loop_start=int(ls * SR), loop_end=int(le * SR),
                           loop_crossfade=round(min(0.25, (le - ls) / 2), 3))
            lines.append('<region> ' + opc(reg) + '  // re-attack')
    print('round 2: %d takes used (%d groups), %d regions' % (len(use), len(cut), emitted))
    return dict(pair_types=pair_types, late=late[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('index', nargs='+')
    ap.add_argument('--audio', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--lt', type=float)
    ap.add_argument('--la', type=float)
    ap.add_argument('--name', default='violas_performance')
    ap.add_argument('--reuse', action='store_true', help='keep samples already cut (same cut, another --lt / --la)')
    ap.add_argument('--xf', type=float, default=0.0, help='round 2: equal-power hand-over of this many ms (0: round 1)')
    ap.add_argument('--sets', action='store_true', help='round 2: slow / fast / runs sets by sfizz lotimer / hitimer')
    ap.add_argument('--release', choices=['cc68', 'sw'], default='cc68',
                    help='sw: release regions with sw_previous = their own key (none in legato, no CC68)')
    ap.add_argument('--level-match', action='store_true', help='round 2: each region at its join to the median level')
    ap.add_argument('--reattack', action='store_true',
                    help='round 2: legato regions at velocity 111-116 / 117-127 play the spiccato / accented attack')
    ap.add_argument('--vel-sets', action='store_true', help='round 2: slow / fast set by velocity, own latency each')
    ap.add_argument('--lt-slow', type=float, help='round 2 --vel-sets: the slow set latency (ms)')
    ap.add_argument('--head-check', action='store_true', help='round 2: drop takes whose head is not the note before')
    ap.add_argument('--keys', help='only transitions into these keys, lo-hi (a quicker cut for comparisons)')
    ap.add_argument('--attack-adjust', help='json {key: ms}: added to each smooth attack region delay')
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
    r2on = bool(a.xf or a.sets or a.head_check or a.vel_sets or a.level_match or a.reattack)
    if r2on:
        r2 = round2(a, tr, lt, la, lay, c, lines, regions, atk)
        late = r2['late']
    # transitions, grouped by (a, b, type, cc1): their takes are the round robins
    groups = {}
    for q in (tr if not r2on else []):
        groups.setdefault((q['a'], q['b'], q['type'], q['cc1']), []).append(q)
    lines.append(hand('legato', a.xf))
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
    if r2on:
        pair_types = r2['pair_types']
    adjust = {int(k): v for k, v in load_json(a.attack_adjust).items()} if a.attack_adjust else {}
    # attacks after silence (trigger=first) and their releases
    lines.append(hand('first', a.xf))
    smooth_by = {}
    for q in atk:
        name = 'a_%s_%d_%d' % (q['attack'], q['key'], q['cc1'])
        path, dur = c.cut(q['file'], q['on'], q['off'], name)
        reg = dict(sample=path, key=q['key'], pitch_keycenter=q['key'], lovel=ATTACKS[q['attack']][0], hivel=ATTACKS[q['attack']][1])
        if q['attack'] == 'smooth':
            reg.update(lay[q['cc1']])
            reg['delay'] = round(max(0.0, (la - q['onset'] + adjust.get(q['key'], 0.0)) / 1000), 4)
            smooth_by[(q['key'], q['cc1'])] = (path, q['onset'], dur)
        ls, le = 2.0 if dur > 3 else max(0.6, q['onset'] / 1000 + 0.4), dur - 0.03
        if le - ls > 0.2:
            reg.update(loop_mode='loop_continuous', loop_start=int(ls * SR), loop_end=int(le * SR),
                       loop_crossfade=round(min(0.25, (le - ls) / 2), 3))
        lines.append('<region> ' + opc(reg))
        regions.append(dict(kind='attack', attack=q['attack'], key=q['key'], cc1=q['cc1'], sample=path, onset=q['onset'],
                            level=q.get('level')))
    if a.release == 'sw':
        # sfizz's sw_previous is the last note-on's key, also for a release trigger: in legato the note before's note-off
        # comes after the new note-on, so its release doesn't play; a line's last note-off plays it (checked with
        # sfzrender: a 1 kHz release region heard only at the last note-off). No CC68 needed.
        lines.append('<group> group=2 trigger=release ampeg_attack=%s ampeg_release=0.3'
                     % ((a.xf if a.xf else REL_MS) / 1000))
    else:
        lines.append('<group> group=2 trigger=release hicc68=63 ampeg_attack=%s ampeg_release=0.3' % (REL_MS / 1000))
    for q in [q for q in atk if q['attack'] == 'smooth']:
        name = 'r_%d_%d' % (q['key'], q['cc1'])
        path, dur = c.cut(q['file'], q['off'], q['off'] + REL_LEN_S, name)
        rr = dict(sample=path, key=q['key'], pitch_keycenter=q['key'])
        if a.release == 'sw':
            rr['sw_previous'] = q['key']
        lines.append('<region> ' + opc(rr) + ' ' + opc(lay[q['cc1']]))
    # gaps: a previous key never recorded before key b: b's smooth attack, its onset at L_t (delay or cut)
    lines.append(hand('legato', a.xf))
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
                if a.reattack:
                    reg['hivel'] = 110
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
