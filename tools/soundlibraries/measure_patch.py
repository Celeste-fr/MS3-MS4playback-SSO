#!/usr/bin/env python3
"""measure_patch.py: measure any patch of a VST 3 instrument (SSO in Kontakt, or any plug-in with a state) with one
command: a sweep is planned as kthost event files, rendered on the Windows test VM (or locally), analysed and summed
up as a JSON the generators can read. docs/MEASURING.md is the guide; this header is the reference.

    measure_patch.py plan    --patch NAME --out WORK [grid options]       event files, list.txt, plan.json
    measure_patch.py run     WORK [--vm user@host | --local] [...]        renders (VM locks, resumable), analysed,
                                                                          WAVs deleted
    measure_patch.py analyze WORK [-o out.json] [--at-level …]            the cells' medians, SD, 95 % CI
    measure_patch.py report  WORK|out.json                                 a short text report (and PNGs with --png)
    measure_patch.py merge   result.json... --by k1,k2 -o out.json        several patches' results grouped (pooled takes)
    measure_patch.py all     --patch NAME --out WORK [grid] [run options] [-o out.json]   the four in one go

PLAN (what is played). A cell is one measured note in a stated context; every cell is played --takes times. The order
is ABBA over the whole plan: take 1 plays the cells in plan order, take 2 in reverse, take 3 forward …, so a slow drift
(a warming cache, a round robin cycle) falls equally on every cell. Two kinds of cells:

  note        (--what includes level, onset, release or tone) a note after silence: every --pitches x --velocity x --cc1
              x --cc11 x --lengths (ms). Measured: level, onset, release. --repeats N plays it N times in a row,
              --repeat-gap s apart (the measures above on the first; tone on every one: a patch's round robins in turn).
  transition  (--what includes transition) a figure ending on a target note B at pitch + interval, each
              --pitches x --intervals x --before x --before-len (ms, the note before's IOI) x --ioi (ms, B's own
              length, the IOI after it). --before:
                attack      A (pitch, --attack-velocity) after silence, B --transition-velocity at A's start + before-len,
                            A released --overlap ms after B's note-on (a legato transition after a fresh attack;
                            a long before-len is "after a held note")
                transition  Z (pitch - interval) attacked, A by transition after before-len, B by transition after another
                            before-len: B's transition after a transition, in one direction (a scale run: Z A B)
                turn        the same with Z on B's pitch: B turns back (an alternating figure, Z A B = B A B)
                staccato-attack  Z a staccato (before-len - --staccato-gap ms), A attacked after it, B by transition
                staccato    A short (before-len - --staccato-gap ms), then B after the gap: an attack after a staccato
              --after transition: a note C after B by transition at B's written end (sent as early as B; a run: B + 2
              the same way, a turn: back to A's pitch), so that B glides into the next note as in music. --intervals may
              be quarter tones (1.5): spelled and bent as [slurs] microtoneBends 1 sends them (the map's bend=,
              bendGlideFingered=, bendSmoothing=: rendermidi.cpp's microtoneSpelling and the uncut fingered glide).
              Measured on B: the f0 leave / mid / arrive times (YIN, as legato_glide/glide.py), its level, and A's.
Controllers: CC1 / CC11 / the articulation's switch are sent --pre s before each cell, during the silence. The switch:
--articulation NAME with --library MAP.xml (the map's <Switch>, the Articulation's value), or --switch cc:N:V /
key:K[:V] / none. --gap s of silence after each cell (default 1.5: sso_even_gap_check.json, 1-1.5 s keeps a Performance
patch's state as in music; use more for --what release). A warm-up note (not measured) starts each file. Files are cut
at --chunk-seconds of audio, so a failed render loses little.

RUN. Each event file through kthost with the patch's state: on the VM the setup MuseScore made
(%LOCALAPPDATA%\\MuseScore\\MuseScore3Evo\\soundlibraries\\<library>\\<patch>.vst3state, copied read-only into the VM work
folder; never written) or --state FILE (uploaded). kthost options: --kthost-opts (default "--tail 3 --wait 120
--max-voices 512", as MuseScore loads a Kontakt patch; the probe at the plan's middle pitch). The VM protocol
(CLAUDE.md, TOOLS.md › VM etiquette): wait until C:\\claude\\quiet.lock is gone, write it with --lock-tag and the local
private/VM_PAUSE, hold both only while a batch of --batch files renders, release between batches; one render at a time;
a watchdog per render (--timeout s), a crash or a timeout retried --retries times. Each WAV is fetched, analysed into
WORK/res/<file>.json and deleted (on the VM and here) unless --keep-wavs. Resumable: a file with its res/*.json is
skipped; --redo re-renders. --local renders on this machine (--kthost, --plugin, --state: the Linux test synth).

ANALYZE (per cell, per take; then over takes). Envelopes: tools/playbackverify/loudness.py perceived_envelope (loudness
units, about dB) and power_envelope (dBFS, 5 ms).
  peak        perceived peak in [on, on + --level-window ms] (default 400; the accent study's measure)
  mean        perceived mean over B's/the note's middle (on + min(150, len/2) .. off)
  loud50      the loudest 50 ms of power (dBFS; the dynamics check's measure)
  onset15     the heard check's / the map's onset rule: the first envelope value after the last one under (peak - 15
              dB) before the peak, minus 30 ms latency (Vos & Rasch 1981; onset_detect.py LATENCY_MS), ms after note-on
  onset6      the same at -6 dB (the heard check's heardAltMs)
  onsetIso    onset_detect.isolated_onset (the existing detector for a note after silence)
  release40   ms after the note-off until the power envelope stays 40 dB under the note's loud50 (release20: 20 dB)
  leave / mid / arrive   transition cells: ms after B's note-on where the f0 has gone 0.1 / 0.5 / 0.9 of the way from
              A's pitch to B's (glide.py's rule: a crossing counts when the next 15 ms stay within 0.25 of it)
  heardRefWritten / arriveWritten  the same against B's written time (note-on + bEarlyMs)
  heardRef    leave + 23.5 ms: THE PERCEPTUAL REFERENCE of a transition. The owner, 2026-10-21 (Whence bars 5-14, each
              section alone with a click): transitions that the f0 shows arriving 130-200 ms late sounded on time, so the
              ear places a legato transition near where the pitch LEAVES the note before (Polfreman 2013: leave +
              23.5 ms). Arrival is reported but is not the heard time; don't model timing on arrival alone. The
              23.5 ms is Polfreman's, not measured on SSO (an assumption until a tap test).
  levelB / levelA  transition cells: perceived peak of B over [B on, B on + min(ioi, 400)], of A over its own span
  bOnset15    transition cells whose B is an attack (before staccato): B's onset15; bMasked 1 when A's release stays
              within 15 dB of B's peak (no reading, as the heard check's "masked"; its tail extrapolation isn't done here)
  tone        (--what includes tone) every note of every cell through tone_check.py (spectral centroid, tilt,
              harmonics-to-noise, noise floor, clicks at note-on / off, 1/12-octave bands), kept in res/*.json's rows;
              analyze compares each note with its group (cell and note, over takes and repeats) and writes WORK/tone.json
              and result.json's "tone" (flags, key outliers, the proposed `avoid`). tone_check.py WORK does the same.
Over the takes: n, median, mean, SD, 95 % CI of the mean (Student's t), and every take. analyze --reanalyze WAVDIR
--at-level T --at-level-cells "velocity=64,lengthMs=211" (renders kept by run --keep-wavs): every render scaled by one gain
so that those cells' median peak is T loudness units before measuring, as the accent study did (the perceived envelope
depends on level).
Output (-o, default WORK/result.json): {"_about", "method" (plan, kthost, rules), "patch", "cells": [{"key": {...},
"n", "measures": {name: {"median", "mean", "sd", "ci95", "takes"}}}], "byPitch": {pitch: {cell label: {measure:
median}}}}. The label: "v64 cc1=64 cc11=127 211ms" (notes), "attack 250ms +2 ioi300" (transitions).

Tests: tools/soundlibraries/test_measure_patch.py (synthetic renders with known onsets, levels and glides; and, with
MS_KTHOST + MS_TESTSYNTH set, plan -> kthost -> analyze on the test synth).
"""
import argparse
import json
import math
import os
import re
import shlex
import statistics as st
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'playbackverify'))
sys.path.insert(0, os.path.join(HERE, 'legato_glide'))

VERSION = 1
LATENCY_MS = 30.0           # onset_detect.py LATENCY_MS (the map's onset rule)
POLFREMAN_MS = 23.5         # Polfreman 2013: a legato transition is heard 23.5 ms after the pitch leaves the note before
DEFAULT_KTHOST_OPTS = "--tail 3 --wait 120 --max-voices 512"
VM_DEFAULT = "user@172.23.177.178"
VM_KEY = os.path.expanduser("~/.ssh/id_ed25519_winvm")
VM_ROOT = r"C:\claude\ps"
VM_KTHOST = r"C:\claude\ps\kt\kthost.exe"
VM_PLUGIN = r"C:\Program Files\Common Files\VST3\Kontakt 8.vst3"
VM_SETUPS = r"%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries"
PAUSE_FILE = os.path.expanduser("~/MuseScore/ms3fork/private/VM_PAUSE")


# ---------------------------------------------------------------------------------------------------------- grid specs

def parse_list(spec, cast=float):
    """'48:72:2' (inclusive), '48,50,60', '64' -> list"""
    if spec is None:
        return []
    out = []
    for part in str(spec).split(','):
        part = part.strip()
        if not part:
            continue
        if ':' in part:
            a = [cast(x) for x in part.split(':')]
            lo, hi, step = a[0], a[1], (a[2] if len(a) > 2 else 1)
            v = lo
            while (v <= hi + 1e-9) if step > 0 else (v >= hi - 1e-9):
                out.append(cast(v))
                v += step
        else:
            out.append(cast(part))
    return out


def parse_switch(spec):
    """'cc:32:20' / 'key:24' / 'key:24:100' / 'none' -> dict or None"""
    if not spec or spec == 'none':
        return None
    p = spec.split(':')
    if p[0] == 'cc' and len(p) == 3:
        return {'type': 'cc', 'number': int(p[1]), 'value': int(p[2])}
    if p[0] == 'key' and len(p) in (2, 3):
        return {'type': 'key', 'key': int(p[1]), 'velocity': int(p[2]) if len(p) == 3 else 100}
    raise SystemExit('bad --switch %r (cc:N:V, key:K[:V], none)' % spec)


def map_switch(library, patch, articulation):
    """the switch an articulation of a patch plays by, from a sound library map (<Switch>, <Articulation value>)"""
    root = ET.parse(library).getroot()
    lib_sw = root.find('Switch')
    inst = next((i for i in root.iter('Instrument') if i.get('name') == patch), None)
    if inst is None:
        raise SystemExit('no Instrument %r in %s' % (patch, library))
    sw = inst.find('Switch') if inst.find('Switch') is not None else lib_sw
    art = next((a for a in inst.iter('Articulation') if a.get('name') == articulation), None)
    if art is None:
        names = [a.get('name') for a in inst.iter('Articulation')]
        raise SystemExit('no Articulation %r in %s (%s)' % (articulation, patch, ', '.join(names)))
    if sw is None or sw.get('type') in (None, 'none'):
        return None
    if sw.get('type') == 'cc':
        return {'type': 'cc', 'number': int(sw.get('number')), 'value': int(art.get('value'))}
    if sw.get('type') == 'key':
        return {'type': 'key', 'key': int(art.get('value')), 'velocity': 100}
    raise SystemExit('switch type %r not handled' % sw.get('type'))


# ---------------------------------------------------------------------------------------------------------------- plan

def cell_label(c):
    if c['kind'] == 'note':
        return 'v%d cc1=%d cc11=%d %dms%s' % (c['velocity'], c['cc1'], c['cc11'], c['lengthMs'],
                                              ' x%d' % c['repeats'] if c.get('repeats', 1) > 1 else '')
    return '%s %dms %+g ioi%d v%d/%d cc1=%d %s early%d%s' % (c['before'], c['beforeLenMs'], c['interval'], c['ioiMs'],
                                                           c['attackVelocity'], c['transitionVelocity'], c['cc1'],
                                                           c.get('speed', 'played'), c.get('bEarlyMs', 0),
                                                           ' after-' + c['after'] if c.get('after', 'none') != 'none' else '')


def key_of(c):
    return {k: v for k, v in c.items() if k not in ('take', 'id', 'file', 't', 'notes', 'span', 'label')}


def public_command(argv):
    """the command line as a result may record it: no home folder (the repo is public; CLAUDE.md)"""
    home = os.path.expanduser('~')
    out = [os.path.basename(argv[0])] + [x.replace(home, '~') for x in argv[1:]]
    return ' '.join(shlex.quote(x) for x in out)


def build_cells(a):
    what = set(a.what.split(','))
    pitches = parse_list(a.pitches, int)
    if not pitches:
        raise SystemExit('--pitches is needed (e.g. 48:72:3)')
    cc1s = parse_list(a.cc1, int) or [64]
    cc11s = parse_list(a.cc11, int) or [127]
    cells = []
    if what & {'level', 'onset', 'release', 'tone'}:
        for p in pitches:
            for v in parse_list(a.velocity, int) or [64]:
                for c1 in cc1s:
                    for c11 in cc11s:
                        for ln in parse_list(a.lengths, int) or [1000]:
                            cells.append({'kind': 'note', 'pitch': p, 'velocity': v, 'cc1': c1, 'cc11': c11,
                                          'lengthMs': ln})
                            if a.repeats > 1:
                                cells[-1]['repeats'] = a.repeats
    if 'transition' in what:
        for p in pitches:
            for i in parse_list(a.intervals, float) or [2]:
                i = int(i) if float(i).is_integer() else i       # (quarter tones: 1.5 = three quarter tones)
                for b in a.before.split(','):
                    if b not in ('attack', 'transition', 'turn', 'staccato', 'staccato-attack'):
                        raise SystemExit('--before: attack, transition, turn, staccato, staccato-attack (not %r)' % b)
                    for bl in parse_list(a.before_len, int) or [300]:
                        for ioi in parse_list(a.ioi, int) or [300]:
                            for c1 in cc1s:
                                for sp in a.speed.split(','):
                                    if sp not in ('played', 'written'):
                                        raise SystemExit('--speed: played, written (not %r)' % sp)
                                    for early in parse_list(a.b_early, int) or [0]:
                                        if early and early > bl - a.min_spacing:
                                            continue      # (B can't go before A plus SSO's 15 ms: DROPPED)
                                        cells.append({'kind': 'transition', 'pitch': p, 'interval': i, 'before': b,
                                                      'beforeLenMs': bl, 'ioiMs': ioi, 'cc1': c1, 'cc11': cc11s[0],
                                                      'attackVelocity': a.attack_velocity,
                                                      'transitionVelocity': a.transition_velocity,
                                                      'speed': sp, 'bEarlyMs': early, 'after': a.after})
    for c in cells:
        c['label'] = cell_label(c)
    return cells


def cell_notes(c, t, a):
    """the cell's notes from t (s): [(name, pitch, velocity, on, off)] (pitch: sounding, in semitones, a quarter tone
    x.5), the measured note B; with after = 'transition' a note C after it (B's pitch + 2 semitones the same way, by
    transition at B's written end, sent as early as B: B's own spacing as written), so that B's glide runs into the next
    note as in a run of the music, not into a release"""
    if c['kind'] == 'note':
        # (--repeats: the note again and again, --repeat-gap s apart: R2, R3 ... after N, so that a patch's round robins
        # play in turn, for tone_check.py)
        L, step = c['lengthMs'] / 1000, c['lengthMs'] / 1000 + getattr(a, 'repeat_gap', 0.4)
        return [('N' if i == 0 else 'R%d' % (i + 1), c['pitch'], c['velocity'], t + i * step, t + i * step + L)
                for i in range(c.get('repeats', 1))]
    L, ioi, ov = c['beforeLenMs'] / 1000, c['ioiMs'] / 1000, a.overlap / 1000
    e = c.get('bEarlyMs', 0) / 1000          # (B's note-on this much before its written time, as MuseScore starts a
    p, q = c['pitch'], c['pitch'] + c['interval']   # transition early; its written time stays)
    av, tv = c['attackVelocity'], c['transitionVelocity']
    after = c.get('after', 'none') == 'transition'
    turn = c['before'] == 'turn'
    cpitch = p if turn else q + (2 if c['interval'] > 0 else -2)      # (a turn goes on alternating, as a trill figure)
    def tail(bon, bwritten):
        if not after:
            return [('B', q, tv, bon, bwritten + ioi)]
        con = bwritten + ioi - e
        return [('B', q, tv, bon, con + ov), ('C', cpitch, tv, con, con + a.after_len / 1000)]
    if c['before'] == 'attack':
        return [('A', p, av, t, t + L - e + ov)] + tail(t + L - e, t + L)
    if c['before'] in ('transition', 'turn'):
        # transition: Z A B in one direction (a run); turn: Z on B's pitch, A, then B back (the f0 may not have reached
        # A yet when B starts: Whence's alternating quarter-tone figures)
        z = q if turn else p - c['interval']
        return [('Z', z, av, t, t + L + ov), ('A', p, tv, t + L, t + 2 * L - e + ov)] + tail(t + 2 * L - e, t + 2 * L)
    gap = a.staccato_gap / 1000
    if c['before'] == 'staccato-attack':
        # Z a staccato (plain Performance: a spiccato attack --staccato-gap ms clear of the next note), A attacked after
        # it, B by transition from A: a slur's first transition after a staccato
        return [('Z', p - c['interval'], av, t, t + max(0.02, L - gap)), ('A', p, av, t + L, t + 2 * L - e + ov)] + \
            tail(t + 2 * L - e, t + 2 * L)
    return [('A', p, av, t, t + max(0.02, L - gap)), ('B', q, av, t + L - e, t + L + ioi)]


def written_ons(c, notes):
    """each note's written start (s): the measured note B (and C after it) as written, bEarlyMs later than sent"""
    e = c.get('bEarlyMs', 0) / 1000 if c['kind'] == 'transition' else 0
    return [n[3] + (e if n[0] in ('B', 'C') else 0) for n in notes]


def spell(notes, legato):
    """[(key, cents)] for sounding pitches as [slurs] microtoneBends 1 sends them (rendermidi.cpp microtoneSpelling): a
    microtonal note reached by or going on by a legato transition on the key on the side of the note before, so that
    the key's step and the bend move the same way; candidates the written key (here the lower one: a quarter-sharp
    spelling), floor and ceil; never the key of the note before or after (SSO plays no same-key transition); a point for
    each of the step in and out moving with the bend; the written key on a tie"""
    out = []
    for i, n in enumerate(notes):
        x = float(n[1])
        k0 = int(math.floor(x + 1e-9))
        cents = (x - k0) * 100
        if abs(cents) < 1.0:
            out.append((int(round(x)), 0.0))
            continue
        has_in = i > 0 and legato[i]
        has_out = i + 1 < len(notes) and legato[i + 1]
        if not (has_in or has_out):
            out.append((k0, round(cents, 3)))
            continue
        fk, fc = out[i - 1] if has_in else (-1, 0.0)
        if has_out:
            nx = float(notes[i + 1][1])
            nk = int(math.floor(nx + 1e-9))
            nc = (nx - nk) * 100 if abs(nx - round(nx)) > 0.01 else 0.0
            if nc == 0.0:
                nk = int(round(nx))
        else:
            nk, nc = -1, 0.0
        same = lambda dk, dc: abs(dc) < 0.5 or (dk > 0) == (dc > 0)
        best, bs = k0, -1
        for k in (k0, int(math.floor(x)), int(math.ceil(x))):
            c = (x - k) * 100
            if abs(c) > 100 or k == fk or k == nk:
                continue
            sc = int(fk >= 0 and same(k - fk, c - fc)) + int(nk >= 0 and same(nk - k, nc - c))
            if sc > bs:
                bs, best = sc, k
        out.append((best, round((x - best) * 100, 3)))
    return out


def glide_at(table, len_ms):
    """bendGlideFingered "len:leave:arrive …" at a note before this long (Articulation::bendGlideAt: linear, clamped)"""
    if not table:
        return None
    if len_ms <= table[0][0]:
        return table[0][1], table[0][2]
    if len_ms >= table[-1][0]:
        return table[-1][1], table[-1][2]
    for (l0, a0, b0), (l1, a1, b1) in zip(table, table[1:]):
        if l0 <= len_ms <= l1:
            f = (len_ms - l0) / (l1 - l0)
            return a0 + (a1 - a0) * f, b0 + (b1 - b0) * f


def bend_events(notes, keys, legato, wons, bend):
    """the pitch bends [slurs] microtoneBends 1 sends for these notes (rendermidi.cpp libraryPitchBends, the uncut
    fingered glide): at each note-on the bend in force; an attack its own tuning there; a transition from the bend in force
    until t0 = note-on + bendGlideFingered's leave (by the note before's written length), then one value u until t1 = its
    arrival such that the bend as heard (Kontakt's one-pole smoothing, bendSmoothing ms) reaches the tuning at t1, then
    the tuning; t1 no later than the next transition's t0 (no glide at all when t0 is past it)"""
    rng, tau, table = bend['range'], bend['tauMs'] / 1000, bend['glide']
    unit = lambda c: max(0, min(16383, int(round(8192 + c / rng * (8192 if c < 0 else 8191)))))
    cents_of = lambda v: (v - 8192) * rng / (8192.0 if v < 8192 else 8191.0)
    ev = []
    state = {'sent': 0.0, 'heard': 0.0, 't': 0.0}
    def hear(t):
        if t > state['t']:
            state['heard'] = state['sent'] + (state['heard'] - state['sent']) * math.exp(-(t - state['t']) / tau)
            state['t'] = t
    def send(t, c):
        hear(t)
        v = unit(c)
        ev.append((round(t, 6), 'pb', v, 0))
        state['sent'] = cents_of(v)
    glides = []
    for i, n in enumerate(notes):
        on, c = n[3], keys[i][1]
        if not legato[i]:
            glides.append(None)
            continue
        g = glide_at(table, (wons[i] - wons[i - 1]) * 1000)
        glides.append((on + g[0] / 1000, on + g[1] / 1000) if g else (on, on))
    at = 0.0
    pend = []
    for i, n in enumerate(notes):
        on, c = n[3], keys[i][1]
        for tt, cc in pend:
            if tt <= on:
                send(tt, cc)
                at = cc
        rest = [(tt, cc) for tt, cc in pend if tt > on]
        if glides[i] is None:
            send(on, c)
            at = c
            pend = []
            continue
        send(on, at)
        for tt, cc in rest:
            send(max(on, tt), cc)
            at = cc
        pend = []
        t0, t1 = glides[i]
        limit = glides[i + 1][0] if i + 1 < len(notes) and glides[i + 1] else (notes[i + 1][3] if i + 1 < len(notes) else 1e9)
        if t0 >= limit:
            continue
        t1 = min(limit, max(t0 + 0.001, t1))
        hear(t0)
        e = math.exp(-(t1 - t0) / tau)
        u = max(-rng, min(rng, (c - state['heard'] * e) / (1 - e)))
        nxt = notes[i + 1][3] if i + 1 < len(notes) else 1e9
        if t0 >= nxt:
            pend = [(t0, u), (t1, c)]
            continue
        send(t0, u)
        at = u
        if t1 >= nxt:
            pend = [(t1, c)]
        else:
            send(t1, c)
            at = c
    for tt, cc in pend:
        send(tt, cc)
    return ev


def written_on(c, notes):
    """the measured note's written start (s): its note-on + bEarlyMs"""
    b = next((n for n in notes if n[0] == 'B'), notes[-1])
    return b[3] + (c.get('bEarlyMs', 0) / 1000 if c['kind'] == 'transition' else 0)


def patch_bend(library, patch):
    """the patch's bend range, its fingered transition's glide and Kontakt's bend smoothing, from the map (bend=,
    bendGlideFingered=, bendSmoothing=): None for a patch that doesn't bend"""
    if not library:
        return None
    inst = next((x for x in ET.parse(library).getroot().iter('Instrument') if x.get('name') == patch), None)
    if inst is None or not inst.get('bend'):
        return None
    art = next((x for x in inst.iter('Articulation') if x.get('bendGlideFingered')), None)
    table = []
    if art is not None:
        for part in art.get('bendGlideFingered').split():
            l, lv, ar = part.split(':')
            table.append((float(l), float(lv), float(ar)))
    return {'range': float(inst.get('bend')), 'glide': table,
            'tauMs': float(art.get('bendSmoothing', 70)) if art is not None else 70.0}


def speed_value(ms):
    """SSO's Performance legato speed controller (CC119) for a legato note-on ms after the line's previous note-on:
    126 - floor(ms x 127 / 1000) (SoundLib::speedValue; the script's formula, docs/SSO_SCRIPT_KNOWLEDGE.md)"""
    return 126 - min(126, max(0, int(round(ms))) * 127 // 1000)


def switch_events(sw, t):
    if not sw:
        return []
    if sw['type'] == 'cc':
        return [(t, 'cc', sw['number'], sw['value'])]
    return [(t, 'on', sw['key'], sw['velocity']), (t + 0.05, 'off', sw['key'], 0)]


def plan(a):
    sw = map_switch(a.library, a.patch, a.articulation) if a.articulation else parse_switch(a.switch)
    cells = build_cells(a)
    order = []
    for take in range(a.takes):
        seq = cells if take % 2 == 0 else list(reversed(cells))
        order += [(take, c) for c in seq]
    pitches = sorted({c['pitch'] for c in cells})
    probe = a.probe_pitch if a.probe_pitch is not None else pitches[len(pitches) // 2]
    work = a.out
    os.makedirs(os.path.join(work, 'ev'), exist_ok=True)
    speed_script, speed_cc = a.speed_script, a.speed_cc
    if a.library and not speed_script:
        inst = next((x for x in ET.parse(a.library).getroot().iter('Instrument') if x.get('name') == a.patch), None)
        if inst is not None and inst.get('speedScript'):
            speed_script, speed_cc = inst.get('speedScript'), int(inst.get('speedCC', speed_cc))
    if any(c.get('speed') == 'written' for c in cells) and not speed_script:
        raise SystemExit("--speed written needs the patch's speed script (--library with speedScript=, or --speed-script)")
    # cells of one speed mode share a file: 'written' loads the state with the script's own CC119 writing off, as
    # [slurs] speedFromWritten 1 does (--script-value <speedScript>=0), and sends CC119 from the written spacing right
    # before each legato note-on (MidiRenderer::librarySpeeds); 'played' leaves it to the script (the spacing as played)
    groups = []
    for mode in ('played', 'written'):
        g = [(tk, c) for tk, c in order if c.get('speed', 'played') == mode]
        if g:
            groups.append((mode, g))
    files, played = [], []
    nfile = 0
    rank = {'cc': 0, 'pb': 0, 'off': 1, 'on': 2}
    bend = patch_bend(a.library, a.patch)
    for mode, seq in groups:
        extra = ('--script-value %s=0' % speed_script) if mode == 'written' else ''
        i = 0
        while i < len(seq):
            name = 'f%03d' % nfile
            nfile += 1
            ev = [(0.0, 'cc', 11, seq[i][1]['cc11']), (0.0, 'cc', 1, seq[i][1]['cc1'])] + switch_events(sw, 0.0)
            t = 0.5
            if a.warm:
                ev += [(t, 'on', probe, 64), (t + 1.0, 'off', probe, 0)]
                t += 1.0 + a.gap
            t = max(t, a.pre + 0.1)
            start = t
            while i < len(seq) and (t - start < a.chunk_seconds or t == start):
                take, c = seq[i]
                ev += [(round(t - a.pre, 6), 'cc', 1, c['cc1']), (round(t - a.pre, 6), 'cc', 11, c['cc11'])]
                ev += switch_events(sw, round(t - a.pre, 6))
                notes = cell_notes(c, t, a)
                legato = [k > 0 and notes[k - 1][4] > notes[k][3] for k in range(len(notes))]
                wons = written_ons(c, notes)
                micro = any(abs(float(n[1]) - round(float(n[1]))) > 0.01 for n in notes)
                keys = spell(notes, legato) if micro and a.microtone_bends else \
                    [(int(round(float(n[1]))), round((float(n[1]) - round(float(n[1]))) * 100, 3)) for n in notes]
                if micro and bend:
                    ev += bend_events(notes, keys, legato, wons, bend)
                elif micro:
                    raise SystemExit('quarter tones need a patch that bends (--library with bend=): %s' % a.patch)
                cc119 = None
                for k, (nm, _, v, on, off) in enumerate(notes):
                    key = keys[k][0]
                    if legato[k]:
                        ms = (wons[k] - wons[k - 1]) if mode == 'written' else (on - notes[k - 1][3])
                        val = speed_value(ms * 1000)
                        if mode == 'written':
                            ev.append((round(on, 6), 'cc', speed_cc, val))
                        if nm == 'B':
                            cc119 = val
                    ev += [(round(on, 6), 'on', key, v), (round(off, 6), 'off', key, 0)]
                end = max(n[4] for n in notes)
                bi = next(k for k, n in enumerate(notes) if n[0] in ('B', 'N'))
                played.append(dict(c, take=take, file=name, t=round(t, 6), id=len(played), cc119=cc119,
                                   rrNote=bi,
                                   spacingMs=round((notes[bi][3] - notes[bi - 1][3]) * 1000, 3) if bi > 0 else None,
                                   notes=[[n[0], keys[k][0], n[2], round(n[3], 6), round(n[4], 6), float(n[1]), keys[k][1]]
                                          for k, n in enumerate(notes)],
                                   span=[round(t, 6), round(end + a.gap, 6)]))
                t = end + a.gap
                i += 1
            # (at one time: controllers first, then note-offs, then note-ons; stable, so CC119 stays before its note)
            ev.sort(key=lambda e: (e[0], rank[e[1]]))
            with open(os.path.join(work, 'ev', name + '.txt'), 'w', newline='\r\n') as f:
                f.write(''.join('%.6f %s %d %d\n' % e for e in ev))
            files.append({'name': name, 'seconds': round(t, 3), 'opts': extra, 'speed': mode})
    with open(os.path.join(work, 'list.txt'), 'w', newline='\r\n') as f:
        f.write(''.join('%s|%s|%d|%s\n' % (fi['name'], a.patch, probe, fi['opts']) for fi in files))
    meta = {'version': VERSION, 'patch': a.patch, 'library': a.library_name, 'articulation': a.articulation,
            'switch': sw, 'what': a.what, 'takes': a.takes, 'order': 'ABBA (take 1 forward, take 2 reversed, ...)',
            'gap': a.gap, 'pre': a.pre, 'overlap': a.overlap, 'staccatoGap': a.staccato_gap, 'warm': a.warm,
            'probePitch': probe, 'kthostOpts': a.kthost_opts, 'speedScript': speed_script, 'speedCC': speed_cc,
            'after': a.after, 'afterLen': a.after_len, 'repeats': a.repeats, 'repeatGap': a.repeat_gap,
            'microtoneBends': a.microtone_bends, 'bend': bend,
            'files': files, 'cells': played, 'command': public_command(sys.argv)}
    with open(os.path.join(work, 'plan.json'), 'w') as f:
        json.dump(meta, f, indent=0)
    total = sum(fi['seconds'] for fi in files)
    print('%d cells x %d takes = %d played, %d files, %.0f s of audio (%.1f min)' %
          (len(cells), a.takes, len(played), len(files), total, total / 60))
    return meta


# ------------------------------------------------------------------------------------------------------------- measure

def _env(x, sr):
    from loudness import perceived_envelope, power_envelope
    env, first, hop = perceived_envelope(x, sr)
    return env, first, hop, power_envelope(x, sr)


def _idx(t_ms, first, hop, n):
    return max(0, min(n, int(round((t_ms - first) / hop))))


def onset_rule(env, first, hop, t0_ms, t1_ms, drop):
    """ms after t0: the first value after the last one under (peak - drop) before the peak, minus LATENCY_MS"""
    import numpy as np
    a, b = _idx(t0_ms, first, hop, len(env)), _idx(t1_ms, first, hop, len(env))
    if b - a < 3:
        return None
    seg = env[a:b]
    k = int(np.argmax(seg))
    under = np.nonzero(seg[:k + 1] < seg[k] - drop)[0]
    if len(under) == 0:
        return None
    i = int(under[-1]) + 1
    return round(first + hop * (a + i) - t0_ms - LATENCY_MS, 1)


def peak_in(env, first, hop, t0_ms, t1_ms):
    a, b = _idx(t0_ms, first, hop, len(env)), _idx(t1_ms, first, hop, len(env))
    return round(float(env[a:b].max()), 2) if b > a else None


def mean_in(env, first, hop, t0_ms, t1_ms):
    a, b = _idx(t0_ms, first, hop, len(env)), _idx(t1_ms, first, hop, len(env))
    return round(float(env[a:b].mean()), 2) if b > a else None


def loud50(x, sr, t0, t1):
    import numpy as np
    seg = x[int(t0 * sr):int(t1 * sr)]
    if len(seg) == 0:
        return None
    p = (np.asarray(seg, dtype=np.float64) ** 2).mean(axis=1) if seg.ndim == 2 else seg ** 2
    w = int(0.05 * sr)
    if len(p) < w:
        return None
    c = np.concatenate([[0.0], np.cumsum(p)])
    s = np.arange(0, len(p) - w + 1, max(1, w // 2))
    m = float(((c[s + w] - c[s]) / w).max())
    return round(10 * math.log10(m), 2) if m > 0 else -200.0


def release_ms(power, sr, t_off, t_end, level, drop):
    """ms after t_off until the power envelope (power_envelope: windows of int(sr * 0.005) frames, 4.99 ms at 44.1 kHz)
    stays drop dB under level; None: it doesn't before t_end"""
    import numpy as np
    w = int(sr * 0.005) / sr
    a, b = int(t_off / w), min(len(power), int(t_end / w))
    if b <= a:
        return None
    seg = power[a:b]
    above = np.nonzero(seg > level - drop)[0]
    if len(above) == 0:
        return 0.0
    if above[-1] >= len(seg) - 1:
        return None                 # never fell that far before the next cell
    return round((above[-1] + 1) * w * 1000, 1)


def glide_times(mono, sr, prev_pitch, pitch, on, end, extra=()):
    """ms after on where the f0 has gone 0.1 / 0.5 / 0.9 of the way from prev_pitch to pitch (glide.py's rule); extra:
    other pitches the f0 may reach in the window (the note after), for the tracker's range"""
    import numpy as np
    from scipy.signal import medfilt
    from f0 import yin_track, hz
    if prev_pitch == pitch:
        return {}
    lo, hi = min([pitch, prev_pitch] + list(extra)) - 2, max([pitch, prev_pitch] + list(extra)) + 2
    t, f = yin_track(mono, sr, on - 0.03, end, hz(lo), hz(hi), step=0.0025, win=0.030)
    with np.errstate(invalid='ignore', divide='ignore'):
        c = 1200 * np.log2(f / hz(pitch))
    ok = ~np.isnan(c)
    if ok.sum() < 10:
        return {}
    c = medfilt(np.interp(t, t[ok], c[ok]), 5)
    c0 = (prev_pitch - pitch) * 100.0
    phi = (c - c0) / (0 - c0)
    out = {}
    for x, name in ((0.1, 'leave'), (0.5, 'mid'), (0.9, 'arrive')):
        for k in range(len(t)):
            if t[k] >= on - 0.01 and phi[k] >= x and np.min(phi[k:k + 6]) >= x - 0.25:
                out[name] = round((t[k] - on) * 1000, 1)
                break
    return out


def measure_cell(c, x, sr, envs, a):
    env, first, hop, power = envs
    import numpy as np
    out = {}
    notes = {n[0]: n for n in c['notes']}
    span_end = c['span'][1]
    if c['kind'] == 'note':
        _, p, v, on, off = notes['N'][:5]
        lw = a.level_window / 1000
        out['peak'] = peak_in(env, first, hop, on * 1000, (on + lw) * 1000)
        mid0 = on + min(0.15, (off - on) / 2)
        out['mean'] = mean_in(env, first, hop, mid0 * 1000, off * 1000)
        out['loud50'] = loud50(x, sr, on, off + 0.5)
        span = min(off + 0.2, on + 1.5)
        out['onset15'] = onset_rule(env, first, hop, on * 1000 - 200, span * 1000, 15)
        if out['onset15'] is not None:
            out['onset15'] = round(out['onset15'] - 200, 1)
        o6 = onset_rule(env, first, hop, on * 1000 - 200, span * 1000, 6)
        out['onset6'] = None if o6 is None else round(o6 - 200, 1)
        try:
            from onset_detect import isolated_onset
            iso = isolated_onset(env, first, hop, on)
            out['onsetIso'] = None if iso is None else round(iso, 1)
        except ImportError:
            pass
        if out['loud50'] is not None:
            out['release40'] = release_ms(power, sr, off, span_end, out['loud50'], 40)
            out['release20'] = release_ms(power, sr, off, span_end, out['loud50'], 20)
    else:
        mono = x.mean(axis=1) if x.ndim == 2 else x
        sounding = lambda n: float(n[5]) if len(n) > 5 else float(n[1])
        _, _, vb, onb, offb = notes['B'][:5]
        _, _, va, ona, offa = notes['A'][:5]
        pa, pb = sounding(notes['A']), sounding(notes['B'])
        extra = [sounding(notes['C'])] if 'C' in notes else []
        g = glide_times(mono, sr, pa, pb, onb, min(offb + 0.15, onb + 1.2), extra)
        for k in ('leave', 'mid', 'arrive'):
            out[k] = g.get(k)
        out['heardRef'] = None if out['leave'] is None else round(out['leave'] + POLFREMAN_MS, 1)
        e = c.get('bEarlyMs', 0)
        out['heardRefWritten'] = None if out['heardRef'] is None else round(out['heardRef'] - e, 1)
        out['arriveWritten'] = None if out['arrive'] is None else round(out['arrive'] - e, 1)
        lb = min(c['ioiMs'], a.level_window) / 1000
        out['levelB'] = peak_in(env, first, hop, onb * 1000, (onb + lb) * 1000)
        out['levelA'] = peak_in(env, first, hop, ona * 1000, onb * 1000)
        if out['levelA'] is not None and out['levelB'] is not None:
            out['stepAB'] = round(out['levelB'] - out['levelA'], 2)
        if c['before'] == 'staccato':
            o = onset_rule(env, first, hop, offa * 1000, min(offb + 0.2, onb + 1.5) * 1000, 15)
            out['bOnset15'] = None if o is None else round(o - (onb - offa) * 1000, 1)
            out['bMasked'] = 1 if o is None else 0          # (A's tail within 15 dB of B's peak: no reading)
        if 'Z' not in notes:
            o = onset_rule(env, first, hop, ona * 1000 - 200, onb * 1000, 15)
            out['aOnset15'] = None if o is None else round(o - 200, 1)
    return out


def analyze_file(work, name, wav, a, gain_db=0.0):
    """measure every cell of one rendered file -> res/<name>.json"""
    import soundfile as sf
    meta = json.load(open(os.path.join(work, 'plan.json')))
    x, sr = sf.read(wav, always_2d=True)
    if gain_db:
        x = x * 10 ** (gain_db / 20)
    envs = _env(x, sr)
    rows = []
    tone = 'tone' in meta['what'].split(',')
    if tone:
        import tone_check
        tnotes = {}
        for n in tone_check.read_plan(os.path.join(work, 'plan.json'), name):
            tnotes.setdefault(n['cellId'], []).append(n)
        trows = {}
        for n in tone_check.measure(x, sr, [n for v in tnotes.values() for n in v]):
            trows.setdefault(n['cellId'], []).append(n)
    for c in meta['cells']:
        if c['file'] != name:
            continue
        rows.append({'id': c['id'], 'take': c['take'], 'm': measure_cell(c, x, sr, envs, a)})
        if tone:
            rows[-1]['tone'] = trows.get(c['id'], [])
    res = {'file': name, 'gainDb': gain_db, 'sampleRate': sr, 'seconds': round(len(x) / sr, 3),
           'peakDbfs': round(20 * math.log10(max(1e-12, float(abs(x).max()))), 2), 'rows': rows}
    os.makedirs(os.path.join(work, 'res'), exist_ok=True)
    tmp = os.path.join(work, 'res', name + '.json.tmp')
    with open(tmp, 'w') as f:            # (written whole, then renamed: a full disk leaves no half result to skip)
        json.dump(res, f)
    os.replace(tmp, os.path.join(work, 'res', name + '.json'))
    return res


# ---------------------------------------------------------------------------------------------------------- summarise

def summarize(vals):
    v = [x for x in vals if x is not None]
    if not v:
        return {'n': 0, 'median': None, 'takes': vals}
    out = {'n': len(v), 'median': round(st.median(v), 2), 'mean': round(st.mean(v), 2), 'takes': vals}
    if len(v) > 1:
        sd = st.stdev(v)
        tq = {2: 12.71, 3: 4.30, 4: 3.18, 5: 2.78, 6: 2.57, 7: 2.45, 8: 2.36, 9: 2.31, 10: 2.26}.get(len(v), 1.96 + 2.5 / len(v))
        h = tq * sd / math.sqrt(len(v))
        out.update(sd=round(sd, 2), ci95=[round(out['mean'] - h, 2), round(out['mean'] + h, 2)])
    return out


def gather(work, a):
    meta = json.load(open(os.path.join(work, 'plan.json')))
    by_id = {c['id']: c for c in meta['cells']}
    got = {}
    missing = []
    for fi in meta['files']:
        p = os.path.join(work, 'res', fi['name'] + '.json')
        if not os.path.exists(p):
            missing.append(fi['name'])
            continue
        for r in json.load(open(p))['rows']:
            got[r['id']] = r['m']
    groups = {}
    for cid, c in by_id.items():
        k = json.dumps(key_of(c), sort_keys=True)
        groups.setdefault(k, []).append((c['take'], got.get(cid)))
    cells = []
    for k, takes in groups.items():
        takes.sort(key=lambda t: t[0])
        names = sorted({n for _, m in takes if m for n in m})
        key = json.loads(k)
        cells.append({'key': key, 'label': cell_label(key), 'n': sum(1 for _, m in takes if m),
                      'measures': {n: summarize([m.get(n) if m else None for _, m in takes]) for n in names}})
    cells.sort(key=lambda c: (c['key']['kind'], c['key']['pitch'], c['label']))
    return meta, cells, missing


def at_level_gain(work, wavdir, a):
    """the gain (dB) that puts the median peak of the cells matching a.at_level_cells ("velocity=64,lengthMs=211")
    at a.at_level loudness units, over the kept renders in wavdir (the perceived envelope depends on level: the accent
    study measured SSO's differences at the real players' level, 59.3 units); found by iteration (within 0.2 units)"""
    import soundfile as sf
    meta = json.load(open(os.path.join(work, 'plan.json')))
    sel = dict(kv.split('=') for kv in a.at_level_cells.split(',')) if a.at_level_cells else {}
    cells = [c for c in meta['cells'] if c['kind'] == 'note' and all(str(c.get(k)) == v for k, v in sel.items())]
    if not cells:
        raise SystemExit('--at-level-cells %r matches no note cell' % a.at_level_cells)
    clips = []
    for c in cells:
        w = os.path.join(wavdir, c['file'] + '.wav')
        if not os.path.exists(w):
            continue
        info = sf.info(w)
        on = c['notes'][-1][3]
        x, sr = sf.read(w, start=max(0, int((on - 0.3) * info.samplerate)), stop=int((on + 0.3 + a.level_window / 1000 + 0.1)
                                                                                    * info.samplerate), always_2d=True)
        clips.append((x, sr))
    if not clips:
        raise SystemExit('--at-level: no kept renders in %s (run with --keep-wavs)' % wavdir)
    from loudness import perceived_envelope
    g = 0.0
    m = None
    for it in range(8):
        peaks = []
        for x, sr in clips:
            env, first, hop = perceived_envelope(x * 10 ** (g / 20), sr)
            peaks.append(peak_in(env, first, hop, 300, 300 + a.level_window))
        m = st.median([p for p in peaks if p is not None])
        if abs(m - a.at_level) < 0.2:
            break
        g += (a.at_level - m) / (1.0 if it == 0 else 1.2)
    print('at level %.1f: gain %+.2f dB (median %.2f over %d cells)' % (a.at_level, g, m, len(clips)))
    return round(g, 2)


def analyze(a):
    work = a.work
    meta, cells, missing = gather(work, a)
    result = {'_about': ('measure_patch.py (tools/soundlibraries; docs/MEASURING.md): %s, %s. Rendered by kthost (%s) '
                         'from the patch\'s state; perceived levels by tools/playbackverify/loudness.py (loudness units, '
                         'about dB); onsets by the map\'s rule (-15 dB under the peak, minus 30 ms); transitions by '
                         'f0 (YIN): leave / mid / arrive = 0.1 / 0.5 / 0.9 of the way, ms after the note-on; heardRef = '
                         'leave + 23.5 ms is the perceptual reference (the owner by ear 2026-10-21; Polfreman 2013), '
                         'not arrive. Per cell: n, median, mean, SD, 95 %% CI of the mean, the takes (ABBA order).')
                        % (meta['patch'], meta['what'], meta['kthostOpts']),
              'method': {k: meta[k] for k in ('version', 'patch', 'library', 'articulation', 'switch', 'what', 'takes',
                                              'order', 'gap', 'pre', 'overlap', 'staccatoGap', 'warm', 'probePitch',
                                              'kthostOpts', 'command')},
              'rules': {'latencyMs': LATENCY_MS, 'polfremanMs': POLFREMAN_MS, 'levelWindowMs': a.level_window,
                        'perceptualReference': 'heardRef (leave + 23.5 ms) for transitions; onset15 for attacks'},
              'patch': meta['patch'], 'missingFiles': missing, 'cells': cells, 'byPitch': {}}
    for fi in meta['files']:
        p = os.path.join(work, 'res', fi['name'] + '.json')
        if os.path.exists(p):
            r = json.load(open(p))
            result.setdefault('renders', []).append({k: r[k] for k in ('file', 'gainDb', 'seconds', 'peakDbfs')})
    if 'tone' in meta['what'].split(','):
        import tone_check
        tr = tone_check.check_work(work)
        with open(os.path.join(work, 'tone.json'), 'w') as f:
            json.dump(tr, f, indent=1)
        result['tone'] = {k: tr[k] for k in ('flags', 'keys', 'avoid')}
        result['tone']['detail'] = 'tone.json (tone_check.py)'
        print('tone: %d flags -> %s' % (len(tr['flags']), os.path.join(work, 'tone.json')))
    for c in cells:
        result['byPitch'].setdefault(str(c['key']['pitch']), {})[c['label']] = \
            {n: m['median'] for n, m in c['measures'].items() if not n.startswith('_')}
    out = a.o or os.path.join(work, 'result.json')
    with open(out, 'w') as f:
        json.dump(result, f, indent=1, sort_keys=True)
    print('%d cells -> %s%s' % (len(cells), out, (' (missing files: %s)' % ', '.join(missing)) if missing else ''))
    return result


def report(a):
    p = a.what_path
    r = json.load(open(os.path.join(p, 'result.json') if os.path.isdir(p) else p))
    lines = ['%s: %d cells, kthost %s' % (r['patch'], len(r['cells']), r['method']['kthostOpts'])]
    if r.get('missingFiles'):
        lines.append('MISSING renders: ' + ', '.join(r['missingFiles']))
    names = {}
    for c in r['cells']:
        for n, m in c['measures'].items():
            names.setdefault((c['key']['kind'], n), []).append(m)
    lines.append('%-12s %-10s %6s %8s %8s %8s %6s' % ('kind', 'measure', 'cells', 'median', 'p10', 'p90', 'sd~'))
    for (kind, n), ms in sorted(names.items()):
        med = sorted(m['median'] for m in ms if m.get('median') is not None)
        sds = [m['sd'] for m in ms if m.get('sd') is not None]
        if not med:
            lines.append('%-12s %-10s %6d %8s' % (kind, n, len(ms), 'none'))
            continue
        q = lambda f: med[min(len(med) - 1, int(f * (len(med) - 1) + 0.5))]
        lines.append('%-12s %-10s %6d %8.1f %8.1f %8.1f %6s' % (kind, n, len(ms), q(0.5), q(0.1), q(0.9),
                                                             ('%.2f' % st.median(sds)) if sds else '-'))
    if any(c['key']['kind'] == 'transition' for c in r['cells']):
        lines.append('transitions: heardRef (leave + 23.5 ms) is the perceptual reference; arrive is NOT the heard time')
        by = {}
        for c in r['cells']:
            k = c['key']
            if k['kind'] != 'transition':
                continue
            m = c['measures']
            by.setdefault((k['before'], k['beforeLenMs']), []).append(
                (m.get('leave', {}).get('median'), m.get('mid', {}).get('median'), m.get('arrive', {}).get('median')))
        lines.append('%-11s %8s %8s %8s %8s %5s' % ('before', 'len ms', 'leave', 'mid', 'arrive', 'n'))
        for (b, bl), v in sorted(by.items()):
            med = lambda i: st.median([x[i] for x in v if x[i] is not None]) if any(x[i] is not None for x in v) else float('nan')
            lines.append('%-11s %8d %8.1f %8.1f %8.1f %5d' % (b, bl, med(0), med(1), med(2), len(v)))
    text = '\n'.join(lines) + '\n'
    print(text, end='')
    out = os.path.join(p, 'report.txt') if os.path.isdir(p) else os.path.splitext(p)[0] + '.report.txt'
    open(out, 'w').write(text)
    if a.png:
        plot(r, os.path.dirname(out) if not os.path.isdir(p) else p)
    return text


def merge(a):
    """several result.json (one per patch) -> one file: {"_about", "method" per patch, "sections": {patch: {"cells",
    "by": {group: {measure: {n, median, p10, p90, takeSd}}}}}}; group = the --by keys' values joined by '/', every take of
    every cell in the group pooled (so n counts takes); takeSd: the median over the group's cells of their take SD"""
    by = a.by.split(',')
    out = {'_about': a.about or ('measure_patch.py merge of %d results, grouped by %s; measures as in each method '
                                 '(docs/MEASURING.md). heardRef (f0 leave + 23.5 ms) is a transition\'s perceptual '
                                 'reference, arrive is not.' % (len(a.results), ','.join(by))),
           'by': by, 'sections': {}}
    for path in a.results:
        r = json.load(open(path))
        groups = {}
        for c in r['cells']:
            k = c['key']
            if a.kind and k['kind'] != a.kind:
                continue
            g = '/'.join(str(k.get(b)) for b in by)
            for n, m in c['measures'].items():
                e = groups.setdefault(g, {}).setdefault(n, {'v': [], 'sd': []})
                e['v'] += [x for x in m.get('takes', []) if x is not None]
                if m.get('sd') is not None:
                    e['sd'].append(m['sd'])
        summ = {}
        for g, ms in sorted(groups.items()):
            summ[g] = {}
            for n, e in ms.items():
                v = sorted(e['v'])
                if not v:
                    summ[g][n] = {'n': 0}
                    continue
                q = lambda f: v[min(len(v) - 1, int(f * (len(v) - 1) + 0.5))]
                summ[g][n] = {'n': len(v), 'median': round(st.median(v), 1), 'p10': round(q(0.1), 1),
                              'p90': round(q(0.9), 1), 'takeSd': round(st.median(e['sd']), 2) if e['sd'] else None}
        method = dict(r['method'])
        method['command'] = re.sub(r'\S*/(?=measure_patch\.py)', '', str(method.get('command', ''))).replace(os.path.expanduser('~'), '~')
        out['sections'][r['patch']] = {'method': method, 'by': summ, 'cells': r['cells'] if a.cells else None}
    with open(a.o, 'w') as f:
        json.dump(out, f, indent=1, sort_keys=True)
    print('%d results -> %s' % (len(a.results), a.o))
    return out


def plot(r, outdir):
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        print('(no matplotlib: no PNGs)')
        return
    names = sorted({(c['key']['kind'], n) for c in r['cells'] for n in c['measures']})
    for kind, n in names:
        fig, ax = plt.subplots(figsize=(8, 4))
        series = {}
        for c in r['cells']:
            if c['key']['kind'] != kind or n not in c['measures'] or c['measures'][n].get('median') is None:
                continue
            lab = re.sub(r'^\S+ ', '', c['label']) if kind == 'note' else c['label']
            series.setdefault(lab, []).append((c['key']['pitch'], c['measures'][n]['median']))
        for lab, pts in sorted(series.items())[:12]:
            pts.sort()
            ax.plot([p for p, _ in pts], [v for _, v in pts], marker='o', ms=3, label=lab)
        ax.set_xlabel('pitch')
        ax.set_ylabel(n)
        ax.set_title('%s: %s (%s)' % (r['patch'], n, kind))
        ax.legend(fontsize=6)
        fig.tight_layout()
        fig.savefig(os.path.join(outdir, '%s_%s.png' % (kind, n)), dpi=110)
        plt.close(fig)


# ------------------------------------------------------------------------------------------------------------- render

class VM:
    def __init__(self, a):
        self.host, self.key, self.tag = a.vm, a.key, a.lock_tag
        self.base = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=20', '-i', self.key, self.host]
        self.held = False

    def ssh(self, cmd, timeout=600, check=False):
        r = subprocess.run(self.base + [cmd], capture_output=True, text=True, timeout=timeout)
        if check and r.returncode != 0:
            raise RuntimeError('vm: %s -> %d %s %s' % (cmd, r.returncode, r.stdout, r.stderr))
        return r

    def ps(self, script, timeout=600, check=False):
        import base64
        enc = base64.b64encode(script.encode('utf-16-le')).decode()
        return self.ssh('powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand ' + enc, timeout, check)

    def scp_to(self, local, remote):
        subprocess.run(['scp', '-q', '-i', self.key, local, '%s:%s' % (self.host, remote.replace('\\', '/'))], check=True)

    def scp_from(self, remote, local):
        r = subprocess.run(['scp', '-q', '-i', self.key, '%s:%s' % (self.host, remote.replace('\\', '/')), local])
        return r.returncode == 0

    def lock(self, poll=60):
        while True:
            r = self.ssh(r'if exist C:\claude\quiet.lock (type C:\claude\quiet.lock & exit 1) else (exit 0)')
            if r.returncode == 0:
                self.ssh(r'echo %s> C:\claude\quiet.lock' % self.tag, check=True)
                r = self.ssh(r'type C:\claude\quiet.lock')
                if r.stdout.strip() == self.tag:
                    break
            print('%s VM busy (quiet.lock: %s), waiting' % (time.strftime('%H:%M:%S'), r.stdout.strip()[:60]), flush=True)
            time.sleep(poll)
        if os.path.isdir(os.path.dirname(PAUSE_FILE)):
            try:
                with open(PAUSE_FILE, 'w') as f:
                    f.write('%s %s\n' % (self.tag, time.ctime()))
            except OSError as e:            # (a full disk: the VM's lock is what counts)
                print('(VM_PAUSE not written: %s)' % e, flush=True)
        self.held = True

    def unlock(self):
        if not self.held:
            return
        r = self.ssh(r'type C:\claude\quiet.lock')
        if r.returncode == 0 and r.stdout.strip() == self.tag:        # (only our own)
            self.ssh(r'del C:\claude\quiet.lock')
        try:
            if open(PAUSE_FILE).read().startswith(self.tag):
                os.remove(PAUSE_FILE)
        except OSError:
            pass
        self.held = False


PS_RENDER = r'''
$ErrorActionPreference = 'Continue'
$run = '%(run)s'; $name = '%(name)s'
New-Item -ItemType Directory -Force "$run\wav" | Out-Null
$made = $false
if ((Test-Path C:\claude\share.txt) -and -not (Test-Path \\VOLENS\VMShare)) {
  $pw = Get-Content C:\claude\share.txt -TotalCount 1
  net use \\VOLENS\VMShare /user:VOLENS\vmshare $pw *> $null; $made = $true }
$kargs = '"%(plugin)s" "%(state)s" "' + "$run\ev\$name.txt" + '" "' + "$run\wav\$name.wav" + '" %(opts)s --report "' + "$run\wav\$name.json" + '"'
$code = 'none'
for ($try = 0; $try -le %(retries)d; $try++) {
  $t0 = Get-Date
  $p = Start-Process -FilePath '%(kthost)s' -ArgumentList $kargs -NoNewWindow -PassThru -RedirectStandardOutput "$run\wav\$name.log" -RedirectStandardError "$run\wav\$name.err"
  $h = $p.Handle
  while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt %(timeout)d) { Start-Sleep -Milliseconds 500 }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $code = 'timeout' } else { $code = [string]$p.ExitCode }
  "{0} try {1} exit {2} {3:N1} s" -f $name, $try, $code, ((Get-Date) - $t0).TotalSeconds | Out-File -Append "$run\run.out"
  if ($code -eq '0') { break }
}
if ($made) { net use \\VOLENS\VMShare /delete /y *> $null }
"RESULT $code"
'''


def wav_dir(a):
    """where fetched renders wait for their analysis: --wav-dir, else a folder on /tmp (RAM on the dev machine: the
    disk is tight; a 4-minute render is ~87 MB), WORK/wav with --keep-wavs"""
    if a.keep_wavs:
        return os.path.join(a.work, 'wav')
    return a.wav_dir or os.path.join(os.environ.get('TMPDIR', '/tmp'), 'measure_patch-' + os.path.basename(os.path.abspath(a.work)))


def run_vm(a, meta, todo):
    vm = VM(a)
    run = a.vm_dir or (VM_ROOT + '\\' + os.path.basename(os.path.abspath(a.work)))
    if not re.match(r'^C:\\claude\\ps\\[A-Za-z0-9_.-]+$', run):
        raise SystemExit('--vm-dir must be C:\\claude\\ps\\<name> (it is deleted at the end): %s' % run)
    patch = meta['patch']
    vm.ssh(r'if not exist "%s\ev" mkdir "%s\ev"' % (run, run), check=True)
    vm.ssh(r'if not exist "%s\st" mkdir "%s\st"' % (run, run), check=True)
    state_vm = run + '\\st\\state.vst3state'
    if a.state:
        vm.scp_to(a.state, state_vm)
    else:
        src = VM_SETUPS + '\\' + (a.library_name or meta.get('library') or 'Spitfire Symphony Orchestra') + '\\' + patch + '.vst3state'
        r = vm.ssh('copy /y "%s" "%s"' % (src, state_vm))
        if r.returncode != 0:
            raise SystemExit('no setup on the VM: %s (%s)' % (src, r.stdout + r.stderr))
    fopts = {fi['name']: fi.get('opts', '') for fi in meta['files']}
    done = []
    try:
        for b in range(0, len(todo), a.batch):
            batch = todo[b:b + a.batch]
            vm.lock()
            try:
                for name in batch:
                    vm.scp_to(os.path.join(a.work, 'ev', name + '.txt'), run + '\\ev\\' + name + '.txt')
                    r = vm.ps(PS_RENDER % dict(run=run, name=name, plugin=a.plugin or VM_PLUGIN, state=state_vm,
                                               opts=meta['kthostOpts'] + ' --probe-pitch %d %s' % (meta['probePitch'], fopts[name]),
                                               kthost=a.kthost or VM_KTHOST, timeout=a.timeout,
                                               retries=a.retries), timeout=a.timeout * (a.retries + 1) + 300)
                    code = (re.findall(r'RESULT (\S+)', r.stdout) or ['?'])[-1]
                    local = os.path.join(wav_dir(a), name + '.wav')
                    os.makedirs(os.path.dirname(local), exist_ok=True)
                    os.makedirs(os.path.join(a.work, 'wav'), exist_ok=True)
                    for ext in ('json', 'log'):
                        vm.scp_from(run + '\\wav\\%s.%s' % (name, ext), os.path.join(a.work, 'wav', name + '.' + ext))
                    if code != '0' or not vm.scp_from(run + '\\wav\\' + name + '.wav', local):
                        print('%s: FAILED (%s)' % (name, code), flush=True)
                        continue
                    vm.ssh(r'del /q "%s\wav\%s.wav"' % (run, name))
                    analyze_file(a.work, name, local, a, a.gain)
                    if not a.keep_wavs:
                        os.remove(local)
                    done.append(name)
                    print('%s: done (%d/%d)' % (name, len(done), len(todo)), flush=True)
            finally:
                vm.unlock()
    finally:
        if not a.keep_vm:
            vm.ssh(r'rmdir /s /q "%s"' % run)
    return done


def run_local(a, meta, todo):
    if not (a.kthost and a.plugin):
        raise SystemExit('--local needs --kthost and --plugin (and --state, or - for the plug-in\'s defaults)')
    fopts = {fi['name']: fi.get('opts', '') for fi in meta['files']}
    done = []
    os.makedirs(os.path.join(a.work, 'wav'), exist_ok=True)
    for name in todo:
        wav = os.path.join(a.work, 'wav', name + '.wav')
        code = None
        for _ in range(a.retries + 1):
            try:
                r = subprocess.run([a.kthost, a.plugin, a.state or '-', os.path.join(a.work, 'ev', name + '.txt'), wav]
                                   + shlex.split(meta['kthostOpts']) + ['--probe-pitch', str(meta['probePitch'])]
                                   + shlex.split(fopts[name])
                                   + ['--report', os.path.join(a.work, 'wav', name + '.json'), '--quiet'],
                                   timeout=a.timeout, capture_output=True, text=True)
                code = r.returncode
            except subprocess.TimeoutExpired:
                code = 'timeout'
            if code == 0:
                break
        if code != 0:
            print('%s: FAILED (%s)' % (name, code))
            continue
        analyze_file(a.work, name, wav, a, a.gain)
        if not a.keep_wavs:
            os.remove(wav)
        done.append(name)
    return done


def run(a):
    meta = json.load(open(os.path.join(a.work, 'plan.json')))
    todo = [fi['name'] for fi in meta['files']
            if a.redo or not os.path.exists(os.path.join(a.work, 'res', fi['name'] + '.json'))]
    if a.only:
        todo = [n for n in todo if n in a.only.split(',')]
    print('%d of %d files to render' % (len(todo), len(meta['files'])), flush=True)
    if not todo:
        return []
    return run_local(a, meta, todo) if a.local else run_vm(a, meta, todo)


# ---------------------------------------------------------------------------------------------------------------- main

def add_grid(p):
    p.add_argument('--patch', required=True, help='the patch (its setup file name without .vst3state)')
    p.add_argument('--out', required=True, help='the work folder')
    p.add_argument('--library', help='sound library map (.xml), for --articulation')
    p.add_argument('--library-name', default='Spitfire Symphony Orchestra', help='the setups folder on the VM')
    p.add_argument('--articulation', help='the articulation to switch to (from --library)')
    p.add_argument('--switch', default='none', help='cc:N:V, key:K[:V] or none (default) when no --articulation')
    p.add_argument('--what', default='level,onset', help='level,onset,release,transition,tone (tone: every note\'s '
                   'tone_check.py measures, kept in res/*.json; analyze adds the tone flags and WORK/tone.json)')
    p.add_argument('--repeats', type=int, default=1, help='note cells: the note this many times in a row (round robins '
                   'in turn, for tone; sso_bad_samples.json: 12)')
    p.add_argument('--repeat-gap', type=float, default=0.4, help='--repeats: silence between them, s (shorter than the '
                   'patch\'s round-robin reset: SSO Performance 1.5 s)')
    p.add_argument('--pitches', help='lo:hi[:step] or a,b,c')
    p.add_argument('--velocity', default='64')
    p.add_argument('--cc1', default='64')
    p.add_argument('--cc11', default='127')
    p.add_argument('--lengths', default='1000', help='note lengths, ms')
    p.add_argument('--intervals', default='2')
    p.add_argument('--ioi', default='300', help='transition cells: the target note\'s length (IOI after it), ms')
    p.add_argument('--before', default='attack', help='attack,transition,staccato')
    p.add_argument('--before-len', default='300', help='the note before\'s IOI, ms')
    p.add_argument('--speed', default='played', help='transition cells: played (the script writes CC119 from the spacing '
                   'as played) and/or written ([slurs] speedFromWritten 1: its writing off, CC119 from the written spacing)')
    p.add_argument('--b-early', default='0', help='transition cells: B\'s note-on this many ms before its written time')
    p.add_argument('--speed-script', help='the patch\'s speed script value (SSO: $vfm12; from --library otherwise)')
    p.add_argument('--speed-cc', type=int, default=119)
    p.add_argument('--attack-velocity', type=int, default=64)
    p.add_argument('--transition-velocity', type=int, default=64)
    p.add_argument('--overlap', type=float, default=24.0, help='legato: the note before ends this many ms after the next note-on')
    p.add_argument('--staccato-gap', type=float, default=45.0, help='before staccato: the silence before B, ms')
    p.add_argument('--after', default='none', choices=('none', 'transition'), help='transition cells: a note C after B '
                   '(B + 2 semitones the same way, by transition at B\'s written end, as early as B), so that B glides into '
                   'the next note as in a run, not into a release')
    p.add_argument('--after-len', type=float, default=300.0, help='C\'s length, ms')
    p.add_argument('--min-spacing', type=float, default=36.0, help='--b-early values leaving B closer than this to A '
                   '(ms) are skipped')
    p.add_argument('--microtone-bends', type=int, default=1, choices=(0, 1), help='quarter tones (--intervals 1.5): 1 '
                   'spelled and bent as [slurs] microtoneBends 1 sends them (the default, Recommended), 0 on the lower key')
    p.add_argument('--takes', type=int, default=2)
    p.add_argument('--gap', type=float, default=1.6, help='silence after each cell, s (>= 1.5: SSO Performance round robins reset)')
    p.add_argument('--pre', type=float, default=0.5, help='controllers this long before each cell, s')
    p.add_argument('--chunk-seconds', type=float, default=240)
    p.add_argument('--no-warm', dest='warm', action='store_false')
    p.add_argument('--probe-pitch', type=int)
    p.add_argument('--kthost-opts', default=DEFAULT_KTHOST_OPTS)


def add_run(p):
    p.add_argument('--local', action='store_true', help='render here (Linux test synth)')
    p.add_argument('--vm', default=VM_DEFAULT)
    p.add_argument('--key', default=VM_KEY)
    p.add_argument('--vm-dir', help='VM work folder (C:\\claude\\ps\\<name>, deleted at the end)')
    p.add_argument('--keep-vm', action='store_true')
    p.add_argument('--kthost', help='kthost (VM default %s)' % VM_KTHOST)
    p.add_argument('--plugin', help='the plug-in (VM default Kontakt 8)')
    p.add_argument('--state', help='a state file to use instead of the VM MuseScore\'s setup of the patch')
    p.add_argument('--lock-tag', default='measure-patch')
    p.add_argument('--batch', type=int, default=4, help='files per lock hold')
    p.add_argument('--timeout', type=int, default=900, help='watchdog per render, s')
    p.add_argument('--retries', type=int, default=1)
    p.add_argument('--redo', action='store_true')
    p.add_argument('--only', help='file names, a,b')
    p.add_argument('--keep-wavs', action='store_true', help='keep the renders in WORK/wav')
    p.add_argument('--wav-dir', help='where renders wait for their analysis (default /tmp/measure_patch-<work>)')


def add_analysis(p):
    p.add_argument('--level-window', type=float, default=400.0, help='ms after the note-on for peak')
    p.add_argument('--gain', type=float, default=0.0, help='dB applied to every render before measuring')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog='See the module header (measure_patch.py) and docs/MEASURING.md.')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('plan')
    add_grid(p)
    p = sub.add_parser('run')
    p.add_argument('work')
    add_run(p)
    add_analysis(p)
    p.add_argument('--library-name', default=None)
    p = sub.add_parser('analyze')
    p.add_argument('work')
    p.add_argument('-o')
    add_analysis(p)
    p.add_argument('--reanalyze', help='a folder of kept WAVs (<file>.wav) to measure again (run --keep-wavs)')
    p.add_argument('--at-level', type=float, help='with --reanalyze: scale so the --at-level-cells\' median peak is this')
    p.add_argument('--at-level-cells', help='e.g. velocity=64,lengthMs=211')
    p = sub.add_parser('report')
    p.add_argument('what_path', metavar='WORK|result.json')
    p.add_argument('--png', action='store_true')
    p = sub.add_parser('merge')
    p.add_argument('results', nargs='+')
    p.add_argument('--by', required=True, help='cell keys to group by, e.g. before,beforeLenMs,ioiMs,speed')
    p.add_argument('--kind', help='only note or transition cells')
    p.add_argument('--cells', action='store_true', help='also keep every cell')
    p.add_argument('--about')
    p.add_argument('-o', required=True)
    p = sub.add_parser('all')
    add_grid(p)
    add_run(p)
    add_analysis(p)
    p.add_argument('-o')
    p.add_argument('--png', action='store_true')
    a = ap.parse_args(argv)
    if a.cmd == 'plan':
        plan(a)
    elif a.cmd == 'run':
        run(a)
    elif a.cmd == 'analyze':
        if a.at_level is not None and not a.reanalyze:
            raise SystemExit('--at-level needs --reanalyze <folder of kept WAVs>')
        if a.reanalyze:
            meta = json.load(open(os.path.join(a.work, 'plan.json')))
            gain = a.gain + (at_level_gain(a.work, a.reanalyze, a) if a.at_level is not None else 0.0)
            for fi in meta['files']:
                w = os.path.join(a.reanalyze, fi['name'] + '.wav')
                if os.path.exists(w):
                    analyze_file(a.work, fi['name'], w, a, gain)
        analyze(a)
    elif a.cmd == 'report':
        report(a)
    elif a.cmd == 'merge':
        merge(a)
    else:
        plan(a)
        a.work = a.out
        run(a)
        analyze(a)
        a.what_path = a.o or os.path.join(a.work, 'result.json')
        report(a)


if __name__ == '__main__':
    main()
