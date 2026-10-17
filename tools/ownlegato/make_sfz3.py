#!/usr/bin/env python3
"""Round 3's SFZ (sfizz) from r3.py's recordings: every transition region is SSO's own sound from the new note-on, so
the instrument keeps SSO's timing per take (each take's arrival is measured, not moved) and a join is the hand-over
from the note before's region to the new region while both still sound the note before (SSO stays the note before for
~70 ms after a note-on: round 2).

    make_sfz3.py --rec <dir>[,<dir>] --audio <dir>[,<dir>] --out <dir> [--files p3port,p3atk] [--xf 0.6]
                 [--share 0.9] [--fade ep|lin] [--name violas_r3] [--reuse]

Audio: <name>_p<n>.flac (pass n = take n; run_vm.cmd's names) or <name>.flac (one pass).

DESIGN
  Regions per transition (a -> b): key b, sw_previous a, the velocity band of its type (portamento 1-19, normal 20-84;
    never 85-127: the owner rejects bowed / detached slurs), the CC119 band it was recorded in (MuseScore sends CC119
    from the written spacing, [slurs] speedFromWritten; Violas group start criteria): slow 0-63 (dt > 500 ms release
    groups on), 64-94, RUNS RT 95-113, RUNS 114-127; portamento recorded slow only, used for 0-94 (its groups have no
    500 ms gate). Takes = passes, rotated by seq_length / seq_position.
  The hand-over: the old voice plays to its note-off (MuseScore's overlap, OVERLAP_S after the new note-on), then
    falls by sfizz's release (exponential, -78 dB at T = --xf: sfizz 1.2.3 ADSREnvelope, exp(-9 t / T)); the new
    region's sample is silent until OVERLAP_S and then rises as the power complement sqrt(1 - exp(-18 t' / T)) (--fade
    ep) or the amplitude complement 1 - exp(-9 t' / T) (lin), baked into the sample.
  A note's sample goes on EXT_S past the next recorded note-on (SSO's own continuation, heard while the old voice
    releases), looped (loop_sustain) only before it.
  --choke T: the old voice is choked at the new note-on instead (sfizz off_by: its release exp(-9 t / T) from the
    note-on), the new region rising from its note-on as the complement: the new region carries SSO's own fading note
    before from its first sample, so a short hand-over swaps our note before for SSO's.
  Sustain shared across takes from --share s (the destination sustain is deterministic from ~0.9 s: the twin-pass null
    -58 to -74 dB): each take's sample ends at share + XF_LIN_S with a linear fade-out baked in; one shared region per
    transition (the take with the longest note) is heard from share (its sample silent before) with the matching linear fade-in, and
    loops its settled part for notes held longer than recorded.
  First notes (trigger=first): the smooth attacks (p3atk) from their note-on; releases (trigger=release,
    sw_previous = own key: none inside a legato line) from their note-off.
  Exact copy: sample_quality=1 (sfizz's default 2 leaves a -59 dB residual at pitch ratio 1, 1 is exact: -140 dB,
    measured); with bends (microtones) it is linear interpolation.
  Exact level: sfizz plays a sample at -11.522749 dB at its defaults (CC7 100, CC10 64; measured with sfzrender on noise,
    residual -144 dB): global volume puts it back. Its voices also start 1 sample late and ramp in over ~300 samples
    (measured): every cut starts LAG samples after the recorded note-on, at kthost's frame (floor of the written time
    x SR); render ours with sfzrender --floor so both place a note-on alike.
"""
import argparse
import json
import os
import sys

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from common import SR, stereo  # noqa: E402

OVERLAP_S = 0.024
LOOP_PAST_S = 0.06    # the loop ends this long after the next recorded note-on (SSO still plays the note before there:
LOOP_XF_S = 0.03      # ~70 ms) and crossfades over its last LOOP_XF_S: so a note handed over at or before its recorded
                      # length (its note-off OVERLAP_S after the new note-on) plays its recording exactly
EXT_S = 0.3           # a note's sample goes on this long past the next recorded note-on (SSO's own continuation: the
                      # old voice's release plays it), looped only before it (loop_sustain)
XF_LIN_S = 0.05
SFIZZ_GAIN_DB = 11.522749
LAG = 1
CHOKE = ' off_by=1 off_mode=time off_time=%s'
TYPES = {5: (1, 19), 64: (20, 84)}


def kframe(t):
    """the frame kthost plays an event at: floor of its time as written in the event file (%.6f) x SR"""
    return int(float('%.6f' % t) * SR)


# CC119 zones: inside one the sound changes little or smoothly; across a bound it steps (r3.py probe2 / probe3: Violas
# portamento identical for CC119 0-56, then changing; normal within the take spread for 0-63; RUNS RT 95-113 and RUNS
# 114-127 from the group start criteria)
ZONES = {5: [(0, 56), (57, 94), (95, 113), (114, 127)], 64: [(0, 63), (64, 94), (95, 113), (114, 127)]}


def cc_ranges(vel, values):
    """each recorded CC119 value's range: the values of a zone split it at their midpoints; a zone without a
    recorded value goes to the nearest one"""
    out = {v: [] for v in values}
    for lo, hi in ZONES[vel]:
        inz = sorted(v for v in values if lo <= v <= hi)
        if not inz:
            v = min(values, key=lambda v: min(abs(v - lo), abs(v - hi)))
            out[v].append((lo, hi))
            continue
        cuts = [lo] + [(a + b) // 2 + 1 for a, b in zip(inz[:-1], inz[1:])] + [hi + 1]
        for v, a, b in zip(inz, cuts[:-1], cuts[1:]):
            out[v].append((a, b - 1))
    # merge adjacent ranges
    for v in out:
        r = sorted(out[v])
        m = [list(r[0])]
        for a, b in r[1:]:
            if a == m[-1][1] + 1:
                m[-1][1] = b
            else:
                m.append([a, b])
        out[v] = [tuple(x) for x in m]
    return out


def band_of(cc):
    return cc


def opc(d):
    return ' '.join('%s=%s' % (k, (('%.6f' % v).rstrip('0').rstrip('.') if isinstance(v, float) else v)) for k, v in d.items())


class Audio:
    """frames [a, b) of a recording, read from its file (FLAC seeks; the whole renders don't fit in memory)"""
    def __init__(self, dirs):
        self.dirs = dirs

    def seg(self, name, a, b):
        p = next((os.path.join(d, name + e) for d in self.dirs for e in ('.flac', '.wav')
                  if os.path.exists(os.path.join(d, name + e))), None)
        x, sr = sf.read(p, start=a, stop=b, dtype='float32', always_2d=True)
        assert sr == SR
        return x


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rec', required=True)
    ap.add_argument('--audio', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--files')
    ap.add_argument('--passes', type=int, default=0, help='takes: _p1.._pN (0: every pass found)')
    ap.add_argument('--xf', type=float, default=0.6, help='old voice release T (s, -78 dB); round 3: 0.04 / 0.12 / 0.3 / 0.6 tried, 0.6 brings the hand-over features inside SSO\'s spread')
    ap.add_argument('--fade', choices=['ep', 'lin'], default='ep', help='ep for normal (takes uncorrelated), lin for portamento')
    ap.add_argument('--share', type=float, default=0.9)
    ap.add_argument('--no-share', action='store_true')
    ap.add_argument('--overlap', type=float, default=0.001, help='the old note\'s note-off after the new note-on (s): '
                    'the new region rises from there')
    ap.add_argument('--choke', type=float, default=0.0,
                    help='> 0: the old voice is choked at the new note-on (off_by, off_mode=time, off_time = this many s) '
                    'instead of released at its note-off, and the new region rises from its note-on over the same time (round 3: '
                    'sfizz then sends the old note a note-off and its release region plays: worse, not used)')
    ap.add_argument('--name', default='violas_r3')
    ap.add_argument('--reuse', action='store_true')
    a = ap.parse_args()
    os.makedirs(os.path.join(a.out, 'samples'), exist_ok=True)
    A = Audio(a.audio.split(','))
    logs = {}
    for d in a.rec.split(','):
        for f in os.listdir(d):
            if f.endswith('.json') and (not a.files or f[:-5] in a.files.split(',')):
                J = json.load(open(os.path.join(d, f)))
                if 'segments' in J:
                    logs[f[:-5]] = J
    T = a.choke if a.choke else a.xf
    if a.choke:
        a.overlap = 0.0
    fadein = (lambda t: np.sqrt(np.clip(1 - np.exp(-18 * t / T), 0, 1))) if a.fade == 'ep' else \
        (lambda t: np.clip(1 - np.exp(-9 * t / T), 0, 1))
    nbytes = [0]

    def write(name, y):
        p = os.path.join(a.out, 'samples', name + '.flac')
        if not (a.reuse and os.path.exists(p)):
            sf.write(p, y.astype(np.float32), SR, subtype='PCM_24')
        nbytes[0] += os.path.getsize(p)
        return 'samples/%s.flac' % name

    # every transition note with a long note after it, per (a, b, vel, band): its takes
    groups, attacks = {}, []
    for name, J in sorted(logs.items()):
        passes = []
        for d in a.audio.split(','):
            passes += [int(f[len(name) + 2:-5]) for f in os.listdir(d) if f.startswith(name + '_p') and f.endswith('.flac')]
            if os.path.exists(os.path.join(d, name + '.flac')):
                passes.append(0)
        passes = sorted(set(passes))
        if a.passes:
            passes = [p for p in passes if p <= a.passes]
        for p in passes:
            fname = name if p == 0 else '%s_p%d' % (name, p)
            for si, s in enumerate(J['segments']):
                ns = s['notes']
                if s.get('attack') == 'smooth' and len(ns) == 1:
                    attacks.append(dict(file=fname, key=ns[0]['key'], on=ns[0]['on'], off=ns[0]['off']))
                    continue
                roles = s.get('roles')
                for i in range(1, len(ns)):
                    if roles and roles[i] != 'b':
                        continue
                    end = ns[i + 1]['on'] if i + 1 < len(ns) else ns[i]['off']
                    if end - ns[i]['on'] < 1.0:
                        continue
                    k = (ns[i - 1]['key'], ns[i]['key'], ns[i]['vel'], band_of(ns[i]['cc119']))
                    groups.setdefault(k, []).append(dict(file=fname, on=ns[i]['on'], end=end, seg=si, i=i, cc119=ns[i]['cc119'],
                                                         ext=EXT_S if i + 1 < len(ns) else 0.0))
    lines = ['// own legato round 3: Violas - Performance (tools/ownlegato/make_sfz3.py; design in its header)',
             '<control> default_path=./',
             '<global> volume=%.6f amp_veltrack=0 amplitude_oncc11=100 bend_up=100 bend_down=-100 sample_quality=1' % SFIZZ_GAIN_DB,
             '<group> group=1 trigger=legato ampeg_attack=0 ampeg_release=%s%s' % (a.xf, CHOKE % T if a.choke else '')]
    regions = []
    share_at = None if a.no_share else a.share
    recorded = {}
    for (ka, kb, vel, band) in groups:
        recorded.setdefault((ka, kb, vel), set()).add(band)
    cc_by = {k: cc_ranges(k[2], sorted(v)) for k, v in recorded.items()}
    for (ka, kb, vel, band), takes in sorted(groups.items()):
        takes.sort(key=lambda q: (q['file'], q['seg'], q['i']))
        lo, hi = TYPES[vel]
        rngs = cc_by[(ka, kb, vel)][band]
        if len(rngs) != 1:
            print('note: %d -> %d vel %d CC119 %d covers %s' % (ka, kb, vel, band, rngs))
        cond = dict(key=kb, pitch_keycenter=kb, sw_previous=ka, lovel=lo, hivel=hi, locc119=rngs[0][0], hicc119=rngs[0][1])
        for ti, q in enumerate(takes):
            s0 = kframe(q['on']) + LAG
            stop = q['end'] + q['ext'] if share_at is None else min(q['end'] + q['ext'], q['on'] + share_at + XF_LIN_S)
            y = A.seg(q['file'], s0, kframe(stop) + LAG).astype(np.float64)
            t = np.arange(len(y)) / SR
            g = np.where(t < a.overlap, 0.0, fadein(np.maximum(t - a.overlap, 0)))
            if share_at is not None and stop < q['end']:
                g = g * np.clip((share_at + XF_LIN_S - t) / XF_LIN_S, 0, 1)
            else:
                g = g * np.clip((len(y) / SR - t) / 0.005, 0, 1)
            nm = 'r3_%d_%d_%d_%s_%d' % (ka, kb, vel, band, ti)
            path = write(nm, y * g[:, None])
            reg = dict(sample=path, **cond)
            if len(takes) > 1:
                reg.update(seq_length=len(takes), seq_position=ti + 1)
            le = q['end'] - q['on'] + (LOOP_PAST_S if q['ext'] else -0.01)
            if share_at is None and le > 0.9:
                reg.update(loop_mode='loop_sustain', loop_start=int(0.6 * SR), loop_end=int(le * SR),
                           loop_crossfade=LOOP_XF_S)
            lines.append('<region> ' + opc(reg))
            regions.append(dict(a=ka, b=kb, vel=vel, band=band, take=ti, cc119=q['cc119'], sample=path,
                                src='%s/%d/%d' % (q['file'], q['seg'], q['i'])))
        if share_at is not None:
            q = max(takes, key=lambda q: q['end'] - q['on'])
            s0 = kframe(q['on']) + int(round(share_at * SR)) + LAG
            y = A.seg(q['file'], s0, kframe(q['end'] + q['ext']) + LAG).astype(np.float64)
            t = np.arange(len(y)) / SR
            y = y * (np.clip(t / XF_LIN_S, 0, 1) * np.clip((len(y) / SR - t) / 0.005, 0, 1))[:, None]
            # (silence ahead instead of sfizz's delay=, which starts the voice ~170 samples late: measured; FLAC keeps
            # digital silence in a few bytes)
            y = np.concatenate([np.zeros((int(round(share_at * SR)), 2)), y])
            path = write('r3s_%d_%d_%d_%s' % (ka, kb, vel, band), y)
            reg = dict(sample=path, **cond)
            ls, le = share_at + XF_LIN_S + 0.05, q['end'] - q['on'] + (LOOP_PAST_S if q['ext'] else -0.01)
            if le - ls > 0.2:
                reg.update(loop_mode='loop_sustain', loop_start=int(ls * SR), loop_end=int(le * SR),
                           loop_crossfade=LOOP_XF_S)
            lines.append('<region> ' + opc(reg) + '  // shared sustain')
    # first notes and releases
    lines.append('<group> group=1 trigger=first ampeg_attack=0 ampeg_release=%s%s' % (a.xf, CHOKE % T if a.choke else ''))
    # (an attack's sample: to its note-off, looped while held; its release is the release region's)
    seen = {}
    for q in attacks:
        if q['key'] in seen:
            continue
        s0 = kframe(q['on']) + LAG
        y = A.seg(q['file'], s0, kframe(q['off']) + LAG)
        path = write('r3a_%d' % q['key'], y)
        seen[q['key']] = (q, path)
        reg = dict(sample=path, key=q['key'], pitch_keycenter=q['key'], loop_mode='loop_continuous', loop_start=int(2.0 * SR),
                   loop_end=len(y) - int(0.03 * SR), loop_crossfade=0.25)
        lines.append('<region> ' + opc(reg))
    lines.append('<group> group=2 trigger=release ampeg_attack=%s ampeg_release=0.3' % (T / 3))
    for k, (q, _) in sorted(seen.items()):
        s0 = kframe(q['off']) + LAG
        path = write('r3r_%d' % k, A.seg(q['file'], s0, s0 + 3 * SR))
        lines.append('<region> ' + opc(dict(sample=path, key=k, pitch_keycenter=k, sw_previous=k)))
    with open(os.path.join(a.out, a.name + '.sfz'), 'w') as f:
        f.write('\n'.join(lines) + '\n')
    with open(os.path.join(a.out, a.name + '.json'), 'w') as f:
        json.dump(dict(xf=T, fade=a.fade, share=share_at, groups=len(groups), regions=regions, attacks=len(seen),
                       sampleBytes=nbytes[0]), f, indent=0)
    print('%d transition groups, %d take regions, %d attacks; samples %.1f MB' % (len(groups), len(regions), len(seen),
                                                                                nbytes[0] / 1e6))


if __name__ == '__main__':
    main()
