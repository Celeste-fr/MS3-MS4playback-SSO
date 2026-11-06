#!/usr/bin/env python3
"""Legato switch probe (2026-10-16, branch legato-switch): what makes SSO's Performance Legato transitions arrive at times
that jump (Whence's Violas: one note-on 1 ms earlier, heard arrival 58 -> 116 ms early). Hand-made kthost event files
(Kontakt 8 + SSO on the Windows test VM, MuseScore's setup of each "<section> - Performance" patch), every cell after
1.6 s of silence (the script's own reset is 1.5 s) and a transport restart ("tr": round robins line up):
  pair  : A (smooth attack, velocity 5) -> B (transition, velocity 69 = fingered), B's note-on dt ms after A's, A off
          24 ms after B on (the map's overlap); dt 60 … 500 step 5; intervals +2, -1, +4, -7 at the mid key, +2 at the low
  chain : A smooth -> B (+2, 400 ms after A) -> C dt after B (+2 / -1), dt 60 … 500 step 10
  vel   : pair, dt 136 / 273, B's velocity 20 … 127
  cc1   : pair, dt 136 / 273, CC1 20 / 40 / 64 / 90 / 120
  gap   : pair, dt 136 / 273, A's note-off -39 … +150 ms from B's note-on (negative: a gap)
  fine  : pair, dt lo … hi step 1 (--fine lo:hi[,lo:hi…] --fine-int i[,i…]): around a jump
CC1 64 (except cc1), CC11 127 (the patch's own level), B (C) held 0.7 s.

  probe.py events <out dir> <take seed> [--sections a,b] [--only pair,chain,…] [--fine …] [--per 160]
      <out>/<name>.txt (kthost events: "seconds on|off|cc|tr a b"), <name>.json (the cells), list.txt "name|patch"
  probe.py analyse <out dir> <flac dir> <rows.json>
      per cell, the transition note's arrival (the heard check's rule, slur_rhythm/heard.py: librosa pYIN, hop 128, the
      first of 3 frames within 30 cents of the new pitch after it leaves the old one) and leave / glideArrive (YIN, the
      map's bendGlide phi 0.1 / 0.9), ms after its note-on
Written for this check (no Spitfire material); the measured rows are in sso_legato_switch.json.

Stopped 2026-10-16 after take A of Violins 1 (615 cells) and half of Violins 2 (320): the own-legato pilot found SSO
picks fingered / bowed transitions at random, and the owner chose an own sample-based patch. What was found, for it:
- The patch (behaviour read from its script and sample groups, nothing copied): a legato transition is a note-on while
  the note before still sounds or ended < 40 ms before; the transition set is chosen by the time since the previous
  legato note-on (ms), against per-articulation bands (5 edges, the script's fallback 115 / 350 ms, the patch's own in
  its group data), the script also writing that time to CC119 (126 - ms x 127 / 1000); slow / fast / runs sets (runs:
  minor 2nd, major 2nd, minor 3rd only); velocity within a set: 1-19 portamento, 20-84 fingered, 85-127 bowed; 1.5 s
  of silence resets the voice; a trill path (160 ms) is off. No CC68 / rebow control.
- Measured: velocity 20-84 arrive within 3 ms of each other, a step at 85 (bowed) of 6-15 ms; from a smooth attack the
  arrival (200-300 ms after the note-on) shows no step with dt 60-500 ms, scatter p10-p90 15-60 ms by interval / key;
  after a transition a minor 2nd down arrives much sooner when dt < 150 ms (Violins 1 69 ms against 165-185, the runs
  set), a tone up 202 / 226 / 246 ms for dt < 150 / 150-300 / >= 300; CC1 20 arrives ~100 ms sooner than 40-120 (flat);
  the note before ending exactly at the note-on (0 / -1 ms) arrives ~80 ms later than an overlap of 10-150 ms or a gap
  of 10-39 ms.
"""
import json, os, random, sys

SEC = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
KEYS = {'Violins 1': (76, 62), 'Violins 2': (74, 60), 'Violas': (67, 55), 'Celli': (59, 45), 'Basses': (39, 33)}
GAP = 1.6
HOLD = 0.7
OVER = 0.024


def cells(only, fine, fine_int):
    out = []
    if 'pair' in only:
        for dt in range(60, 501, 5):
            for iv in (2, -1, 4, -7):
                out.append(dict(kind='pair', dt=dt, iv=iv, key='mid'))
            out.append(dict(kind='pair', dt=dt, iv=2, key='low'))
    if 'chain' in only:
        for dt in range(60, 501, 10):
            for iv in (2, -1):
                out.append(dict(kind='chain', dt=dt, iv=iv, key='mid'))
    if 'vel' in only:
        for dt in (136, 273):
            for v in list(range(20, 85, 4)) + list(range(85, 128, 6)):
                out.append(dict(kind='vel', dt=dt, iv=2, key='mid', vel=v))
    if 'cc1' in only:
        for dt in (136, 273):
            for c in (20, 40, 64, 90, 120):
                out.append(dict(kind='cc1', dt=dt, iv=2, key='mid', cc1=c))
    if 'gap' in only:
        for dt in (136, 273):
            for g in (-39, -30, -20, -10, -1, 0, 10, 24, 60, 150):
                out.append(dict(kind='gap', dt=dt, iv=2, key='mid', off=g))
    if 'fine' in only:
        for rng in fine:
            lo, hi = [int(x) for x in rng.split(':')]
            for dt in range(lo, hi + 1):
                for iv in fine_int:
                    out.append(dict(kind='fine', dt=dt, iv=iv, key='mid'))
    return out


def events(outd, seed, sections, only, fine, fine_int, per):
    os.makedirs(outd, exist_ok=True)
    lst = []
    for sec in sections:
        cs = cells(only, fine, fine_int)
        random.Random(f'{seed} {sec}').shuffle(cs)
        for n in range(0, len(cs), per):
            t, ev, lay = 1.0, [], []
            for c in cs[n:n + per]:
                k0 = KEYS[sec][0 if c['key'] == 'mid' else 1]
                cc1 = c.get('cc1', 64)
                vel = c.get('vel', 69)
                ev += [(t - 0.05, 'cc', 1, cc1), (t - 0.05, 'cc', 11, 127), (t - 0.002, 'tr', 0, 0)]
                dt = c['dt'] / 1000
                if c['kind'] == 'chain':
                    kb, kc = k0 + 2, k0 + 2 + c['iv']
                    tb = t + 0.4
                    ev += [(t, 'on', k0, 5), (tb + OVER, 'off', k0, 0), (tb, 'on', kb, 69),
                           (tb + dt + OVER, 'off', kb, 0), (tb + dt, 'on', kc, vel), (tb + dt + HOLD, 'off', kc, 0)]
                    c2 = dict(c, start=round(t, 5), prevKey=kb, key0=k0, on=round(tb + dt, 6), target=kc, prevOn=round(tb, 6))
                    t = tb + dt + HOLD + GAP
                else:
                    kb = k0 + c['iv']
                    off = (c['off'] if 'off' in c else OVER * 1000) / 1000
                    ev += [(t, 'on', k0, 5), (t + dt + off, 'off', k0, 0), (t + dt, 'on', kb, vel),
                           (t + dt + HOLD, 'off', kb, 0)]
                    c2 = dict(c, start=round(t, 5), prevKey=k0, key0=k0, on=round(t + dt, 6), target=kb, prevOn=round(t, 6))
                    t = t + dt + HOLD + GAP
                lay.append(c2)
            name = f'{seed}_{sec.replace(" ", "")}_{n // per + 1}'
            write_events(os.path.join(outd, name + '.txt'), ev)
            json.dump(dict(name=name, section=sec, patch=sec + ' - Performance', seed=seed, cells=lay, seconds=round(t, 3)),
                      open(os.path.join(outd, name + '.json'), 'w'))
            lst.append(f'{name}|{sec} - Performance')
            print(name, len(lay), 'cells', round(t / 60, 1), 'min')
    with open(os.path.join(outd, 'list.txt'), 'a', newline='\r\n') as f:
        f.write(''.join(x + '\n' for x in lst))


def write_events(path, ev):
    order = {'tr': -1, 'cc': 0, 'off': 1, 'on': 2}
    ev = sorted(ev, key=lambda e: (round(e[0], 6), order[e[1]]))
    with open(path, 'w', newline='\r\n') as f:
        for e in ev:
            f.write(f'{e[0]:.6f} {e[1]} {e[2]} {e[3]}\n')


def analyse(outd, flacd, rowsf):
    import numpy as np, soundfile as sf, librosa
    from scipy.signal import medfilt
    here = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(here, '..', 'legato_glide'))
    from f0 import yin_track, hz
    rows = json.load(open(rowsf)) if os.path.exists(rowsf) else []
    done = {r['file'] for r in rows}
    for fn in sorted(os.listdir(flacd)):
        if not fn.endswith('.flac') or fn[:-5] in done:
            continue
        meta = json.load(open(os.path.join(outd, fn[:-5] + '.json')))
        x, sr = sf.read(os.path.join(flacd, fn))
        m = x.mean(axis=1) if x.ndim == 2 else x
        for c in meta['cells']:
            on, prev, tgt = c['on'], c['prevKey'], c['target']
            lo, hi = min(prev, tgt) - 3, max(prev, tgt) + 3
            fmin, fmax = librosa.midi_to_hz(lo), librosa.midi_to_hz(hi)
            fl = 2048 if fmin >= 100 else 4096
            a = int((on - 0.05) * sr)
            seg = m[max(0, a):a + int(0.85 * sr)]
            f0, vf, vp = librosa.pyin(seg, fmin=fmin, fmax=fmax, sr=sr, frame_length=fl, hop_length=128)
            fh = 128 / sr
            base = max(0, a) / sr
            i0 = int(round((on - base) / fh))
            arr = None
            for i in range(i0, len(f0)):
                if np.isnan(f0[i]) or abs(12 * np.log2(f0[i] / 440) + 69 - prev) > 0.30:
                    i0 = i
                    break
            for i in range(i0, len(f0) - 3):
                if all(not np.isnan(f0[j]) and abs(12 * np.log2(f0[j] / 440) + 69 - tgt) < 0.30 for j in range(i, i + 3)):
                    arr = round((base + i * fh - on) * 1000, 1)
                    break
            g = {'leave': None, 'glideArrive': None}
            t, f = yin_track(m, sr, on - 0.03, on + 0.6, hz(lo + 1), hz(hi - 1), step=0.0025, win=0.030)
            cc = 1200 * np.log2(f / hz(tgt)); ok = ~np.isnan(cc)
            if ok.sum() >= 10:
                cc = medfilt(np.interp(t, t[ok], cc[ok]), 5)
                c0 = (prev - tgt) * 100.0
                phi = (cc - c0) / (0 - c0)
                for xx, key in ((0.1, 'leave'), (0.9, 'glideArrive')):
                    for k in range(len(t)):
                        if t[k] >= on - 0.01 and phi[k] >= xx and np.min(phi[k:k + 6]) >= xx - 0.25:
                            g[key] = round((t[k] - on) * 1000, 1)
                            break
            # level: the loudest 50 ms in [on, on + 0.6 s] (dBFS, mono), a cue for which sample set played
            s2 = m[int(on * sr):int((on + 0.6) * sr)]
            w = int(0.05 * sr)
            lev = None
            if len(s2) > w:
                p = np.convolve(s2 ** 2, np.ones(w) / w, 'valid')
                lev = round(float(10 * np.log10(p.max() + 1e-12)), 2)
            r = {k: v for k, v in c.items()}
            r.update(file=fn[:-5], section=meta['section'], seed=meta['seed'], arrive=arr, level=lev, **g)
            rows.append(r)
        json.dump(rows, open(rowsf, 'w'))
        print(fn, len(meta['cells']), 'cells;', sum(1 for r in rows if r['file'] == fn[:-5] and r['arrive'] is None), 'not found')


if __name__ == '__main__':
    a = sys.argv[1:]
    def opt(name, default):
        if name in a:
            i = a.index(name); v = a[i + 1]; del a[i:i + 2]; return v
        return default
    if a[0] == 'events':
        sections = opt('--sections', ','.join(SEC)).split(',')
        only = opt('--only', 'pair,chain,vel,cc1,gap').split(',')
        fine = opt('--fine', '').split(',') if '--fine' in a else []
        fine_int = [int(x) for x in opt('--fine-int', '2').split(',')]
        per = int(opt('--per', '160'))
        events(a[1], a[2], sections, only, [f for f in fine if f], fine_int, per)
    elif a[0] == 'analyse':
        analyse(a[1], a[2], a[3])
