#!/usr/bin/env python3
"""The room SSO's recordings carry (AIR Lyndhurst Hall), per mic, measured from SSO's own notes (2026-10-24, branch
research-reverb; docs/RESEARCH.md › 3.5 Room and reverb › Measured).

    sso_room.py events <work>                     kthost event files + jobs.json (one render per patch x mic config)
    sso_room.py run <work> [--only tag,...]       render on the Windows VM (tools/vm/vmlib.py lock "reverb", batches of
                                                  --batch-min minutes), fetch, analyse each WAV into <work>/meas/<tag>.json,
                                                  delete the WAV once its patch's deconvolution is done
    sso_room.py analyze <work> [--wav-dir d]      (re)analyse the WAVs present
    sso_room.py summary <work> [-o tools/soundlibraries/sso_room.json] [--musereverb <ir.f32>]
                                                  the derived numbers: medians per patch x mic x band, then per family

Renders (Windows test VM, kthost of build #474: Kontakt 8 + SSO, offline, 44.1 kHz, one process per render, transport
restarted; copies of MuseScore's made setups: the strings' from MuseScore's setups folder, the winds' and brass' from the
dynamics check's; read only). Each render: the mic levels set by param events at 0.1 s ("Mic N level": the mic alone at
1.0, the others 0, as mic_levels.py did; strings N 1-5 Close, Tree, Ambient, Outrigger, Leader; winds and brass 1-4),
CC11 127, CC1 100; a warm-up short; then
  short   SSO's shortest articulation (strings Spiccato UACC 42, winds / brass "staccato" 40), velocity 100, 0.15 s keys,
          3 pitches (the quartiles of the patch's CC1-sweep pitches, mic_levels.pitches) x 3 takes, 6 s apart;
  pizz    (strings) Pizzicato UACC 56, the same pitches x 2 takes, 6 s apart;
  long    Long UACC 1, velocity 100, 2.5 s held, 7 s after the note-off, 3 pitches x 2 takes: the release tail.
Configurations: each mic alone (m1 .. m5) at the patch's Release default (0.4961: MuseScore never changes it), and
the Tree alone with Release at 1.0 ("tr1": how much the Release control shortens the tail).

Analysis per note (both channels' power summed, L^2 + R^2):
  - octave bands 125 Hz .. 8 kHz (4th-order Butterworth band-pass, zero phase) and broadband;
  - the decay from its start (a short / pizz: the envelope's peak; a long: the note-off) to the next note: noise floor =
    the mean power of the window's last 0.4 s, the decay curve cut where the 20 ms envelope first falls to the floor
    + 5 dB, the floor subtracted, Schroeder backward integration (ISO 3382-1; for an interrupted note the decay curve of
    the interrupted-noise method, also exponential with the same slope); EDT (0 .. -10 dB) x 6, T20 (-5 .. -25) x 3, T30
    (-5 .. -35) x 2, each only where the curve's usable range reaches 10 dB below the fit's end;
  - level of the 10 ms envelope 0.2 / 0.5 / 1.0 s after the decay start against its peak (broadband);
  - onset: the first 1 ms where the 2 ms envelope reaches the note's peak - 20 dB, after the written note-on.
Per patch: each mic's relative impulse response against the Close mic, h_m = mic m / Close, by regularised
deconvolution over the short and pizz notes (Close itself is not dry: h is the room as heard by mic m minus what the
Close mic already carries, an approximation): its peak lag (time of flight Close -> mic m, if Spitfire kept the mics'
natural delays), DRR (+-2.5 ms around the peak against the rest), C50 / C80 (ISO 3382-1, from the peak - 2.5 ms),
EDT / T20 / T30 of h by band. (The first reflection is not resolvable this way: the notes' own 2-20 ms structure fills
the bins next to the direct peak; firstReflMs in the per-patch files always reads the first bin.)
MuseScore 4's reverb (effects/musereverb, the fork's copy of MS4.7.5's): its impulse response from
tools/soundlibraries/room/musereverb_ir (wet only, the default parameters; the send 0.3 in MuseReverb) analysed with the
same decay code.
No audio or SSO file is kept: WAVs are deleted once measured; only numbers go in the JSON.
"""
import argparse
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'vm'))

STRINGS = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
WINDS = ['Flute Solo', 'Oboe Solo', 'Horn Solo', 'Trumpet Solo', 'Tenor Trombone Solo']
MICS = ['Close', 'Tree', 'Ambient', 'Outrigger', 'Leader']
BANDS = [125, 250, 500, 1000, 2000, 4000, 8000]
SR = 44100
KTHOST = r'C:\claude\b474\ms\bin\kthost.exe'
PLUGIN = r'C:\Program Files\Common Files\VST3\Kontakt 8.vst3'
SETUPS_STRINGS = r'%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra'
SETUPS_WINDS = r'C:\Users\user\Documents\MuseScore Sound Library Check\background dynamics check setups\Spitfire Symphony Orchestra'
VMDIR = r'C:\claude\ps\reverb'
SHORT_LEN, SHORT_GAP, LONG_LEN, LONG_GAP = 0.15, 6.0, 2.5, 7.0
KINDS = ('short', 'shortheld', 'pizz', 'pizzheld', 'long')
FADE_S = 0.35        # (the samples' end fade, seen on Violins 1: -60 dB -> silence in ~0.3 s)


def short(patch):
    return ''.join(ch for ch in patch.lower() if ch.isalnum())[:10]


def nmics(patch):
    return 5 if patch in STRINGS else 4


def configs(patch):
    n = nmics(patch)
    out = [('m%d' % i, {'Mic %d level' % j: (1.0 if j == i else 0.0) for j in range(1, n + 1)}) for i in range(1, n + 1)]
    out.append(('tr1', {**{'Mic %d level' % j: (1.0 if j == 2 else 0.0) for j in range(1, n + 1)}, 'Release': 1.0}))
    return out


def plan_events(patch, cfg):
    import mic_levels
    ps = mic_levels.pitches()[patch]
    strings = patch in STRINGS
    sw_short = 42 if strings else 40
    ev = [(0.1, 'param %s %.6f' % (k, v)) for k, v in cfg.items()]
    ev += [(0.2, 'cc 11 127'), (0.2, 'cc 1 100'), (0.2, 'cc 32 %d' % sw_short),
           (1.0, 'on %d 100' % ps[1]), (1.0 + SHORT_LEN, 'off %d 0' % ps[1])]
    notes = []
    t = 5.0

    def add(kind, p, k, length, after):
        nonlocal t
        ev.extend([(t, 'on %d 100' % p), (t + length, 'off %d 0' % p)])
        notes.append({'kind': kind, 'pitch': p, 'take': k, 'on': round(t, 4), 'off': round(t + length, 4)})
        t = round(t + length + after if kind == 'long' else t + after, 4)

    for p in ps:
        for k in range(3):
            add('short', p, k, SHORT_LEN, SHORT_GAP)
    for p in ps:            # (the key held: the sample's own tail, without Kontakt's release envelope at the key-up)
        for k in range(2):
            add('shortheld', p, k, SHORT_GAP - 0.5, SHORT_GAP)
    if strings:
        ev.append((t - 1.0, 'cc 32 56'))
        for p in ps:
            for k in range(2):
                add('pizz', p, k, SHORT_LEN, SHORT_GAP)
        for p in ps:
            add('pizzheld', p, 0, SHORT_GAP - 0.5, SHORT_GAP)
    ev.append((t - 1.0, 'cc 32 1'))
    for p in ps:
        for k in range(2):
            add('long', p, k, LONG_LEN, LONG_GAP)
    order = {'param': 0, 'cc': 1, 'off': 2, 'on': 3}
    ev.sort(key=lambda e: (e[0], order[e[1].split()[0]]))
    return ''.join('%.4f %s\n' % e for e in ev), {'patch': patch, 'pitches': ps, 'cfg': cfg, 'notes': notes, 'end': t}


def cmd_events(a):
    os.makedirs(os.path.join(a.work, 'ev'), exist_ok=True)
    jobs = []
    for patch in STRINGS + WINDS:
        for name, cfg in configs(patch):
            tag = '%s_%s' % (short(patch), name)
            text, meta = plan_events(patch, cfg)
            with open(os.path.join(a.work, 'ev', tag + '.txt'), 'w', newline='\r\n') as f:
                f.write(text)
            meta['tag'], meta['config'] = tag, name
            json.dump(meta, open(os.path.join(a.work, 'ev', tag + '.json'), 'w'), indent=0)
            jobs.append({'tag': tag, 'patch': patch, 'config': name, 'probe': meta['pitches'][1], 'seconds': meta['end']})
    json.dump(jobs, open(os.path.join(a.work, 'jobs.json'), 'w'), indent=1)
    print(len(jobs), 'renders,', round(sum(j['seconds'] for j in jobs) / 60, 1), 'min of audio')


PS = r'''
$ErrorActionPreference = 'Continue'
$run = '%(run)s'; $name = '%(name)s'
New-Item -ItemType Directory -Force "$run\wav" | Out-Null
$made = $false
if ((Test-Path C:\claude\share.txt) -and -not (Test-Path \\VOLENS\VMShare)) {
  $pw = Get-Content C:\claude\share.txt -TotalCount 1
  net use \\VOLENS\VMShare /user:VOLENS\vmshare $pw *> $null; $made = $true }
$kargs = '"%(plugin)s" "%(state)s" "' + "$run\ev\$name.txt" + '" "' + "$run\wav\$name.wav" + '" --tail 6 --wait 120 --max-voices 512 --probe-pitch %(probe)d --quiet --report "' + "$run\wav\$name.json" + '"'
$code = 'none'
for ($try = 0; $try -le 1; $try++) {
  $t0 = Get-Date
  $p = Start-Process -FilePath '%(kthost)s' -ArgumentList $kargs -NoNewWindow -PassThru -RedirectStandardOutput "$run\wav\$name.log" -RedirectStandardError "$run\wav\$name.err"
  $h = $p.Handle
  while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt 900) { Start-Sleep -Milliseconds 500 }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $code = 'timeout' } else { $code = [string]$p.ExitCode }
  "{0} try {1} exit {2} {3:N1} s" -f $name, $try, $code, ((Get-Date) - $t0).TotalSeconds | Out-File -Append "$run\run.out"
  if ($code -eq '0') { break }
}
if ($made) { net use \\VOLENS\VMShare /delete /y *> $null }
"RESULT $code"
'''


def wav_dir(a):
    return a.wav_dir or os.path.join(os.environ.get('TMPDIR', '/tmp'), 'sso_room-wav')


def cmd_run(a):
    import base64
    import vmlib
    vm = vmlib.VM()
    alljobs = jobs = json.load(open(os.path.join(a.work, 'jobs.json')))
    if a.only:
        jobs = [j for j in jobs if j['tag'] in a.only.split(',') or j['patch'] in a.only.split(',')]
    os.makedirs(os.path.join(a.work, 'meas'), exist_ok=True)
    os.makedirs(wav_dir(a), exist_ok=True)
    todo = [j for j in jobs if not os.path.exists(os.path.join(a.work, 'meas', j['tag'] + '.json'))]
    print(len(todo), 'to render', flush=True)

    def ps(script, timeout):
        enc = base64.b64encode(script.encode('utf-16-le')).decode()
        return vm.ssh('powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand ' + enc, timeout=timeout)

    vm.ssh(r'if not exist "%s\ev" mkdir "%s\ev"' % (VMDIR, VMDIR))
    vm.ssh(r'if not exist "%s\st" mkdir "%s\st"' % (VMDIR, VMDIR))
    i = 0
    while i < len(todo):
        lock = vmlib.Lock(vm, 'reverb', poll=30)
        lock.acquire()
        t0 = time.time()
        fetched = []
        try:
            # (the lock only while rendering: the batch's WAVs are analysed after its release)
            while i < len(todo) and time.time() - t0 < a.batch_min * 60:
                j = todo[i]
                i += 1
                src = (SETUPS_STRINGS if j['patch'] in STRINGS else SETUPS_WINDS) + '\\' + j['patch'] + '.vst3state'
                st = VMDIR + '\\st\\' + short(j['patch']) + '.vst3state'
                vm.ssh('if not exist "%s" copy /y "%s" "%s"' % (st, src, st))
                vm.scp_to([os.path.join(a.work, 'ev', j['tag'] + '.txt')], VMDIR + '\\ev\\' + j['tag'] + '.txt')
                t1 = time.time()
                rc, out = ps(PS % dict(run=VMDIR, name=j['tag'], plugin=PLUGIN, state=st, probe=j['probe'], kthost=KTHOST), 2200)
                code = ([l.split()[1] for l in (out or '').splitlines() if l.startswith('RESULT')] or ['?'])[-1]
                local = os.path.join(wav_dir(a), j['tag'] + '.wav')
                if code != '0' or not vm.scp_from(VMDIR + '\\wav\\' + j['tag'] + '.wav', local):
                    print('%s: FAILED (%s)' % (j['tag'], code), flush=True)
                    continue
                vm.ssh(r'del /q "%s\wav\%s.wav"' % (VMDIR, j['tag']))
                fetched.append((j, local))
                print('%s: rendered in %.0f s (%d/%d)' % (j['tag'], time.time() - t1, i, len(todo)), flush=True)
        finally:
            lock.release()
        for j, local in fetched:
            analyze_one(a.work, j['tag'], local)
            print('%s: measured' % j['tag'], flush=True)
        cleanup_wavs(a, alljobs)
        if i < len(todo):
            time.sleep(60)       # (others get a turn)
    if a.rm_vm:
        vm.ssh(r'rmdir /s /q "%s"' % VMDIR)


def cleanup_wavs(a, jobs):
    """a patch's WAVs go once every config of it is measured and its deconvolution done"""
    if a.keep_wavs:
        return
    by = {}
    for j in jobs:
        by.setdefault(j['patch'], []).append(j['tag'])
    for patch, tags in by.items():
        if all(os.path.exists(os.path.join(a.work, 'meas', t + '.json')) for t in tags):
            dfile = os.path.join(a.work, 'meas', short(patch) + '.deconv.json')
            if not os.path.exists(dfile):
                deconvolve_patch(a.work, patch, wav_dir(a))
            if os.path.exists(dfile):
                for t in tags:
                    w = os.path.join(wav_dir(a), t + '.wav')
                    if os.path.exists(w):
                        os.remove(w)


# ---------------------------------------------------------------------------------------------------------- analysis

def db(x):
    return 10 * math.log10(x) if x > 0 else -200.0


_sos = {}


def bandpass(x, fc, sr):
    from scipy.signal import butter, sosfiltfilt
    if fc not in _sos:
        lo, hi = fc / math.sqrt(2), min(fc * math.sqrt(2), sr / 2 * 0.95)
        _sos[fc] = butter(4, [lo, hi], btype='band', fs=sr, output='sos')
    return sosfiltfilt(_sos[fc], x, axis=0)


def envelope(p, sr, win):
    """mean power in windows of win s (p: power per sample)"""
    import numpy as np
    n = max(1, int(win * sr))
    m = len(p) // n
    return p[:m * n].reshape(m, n).mean(axis=1)


def decay_params(p, sr, i0, i1, fade=FADE_S):
    """EDT / T20 / T30 of the power p (per sample) from sample i0 to i1: Lundeby, Vigran & Bietz (1995)'s iteration
    for the floor and the crossing point, the floor subtracted, Schroeder backward integration with the exponential
    tail past the crossing point added. SSO's samples end in digital silence after a fade (the sample's end): the
    analysis stops `fade` s before the last 20 ms bin within 100 dB of the peak, so the fade is not read as decay; the
    floor found is then the recording's own (its noise, the hall's ambience)."""
    import numpy as np
    seg = p[i0:i1].astype('float64')
    B = 0.02
    nb = int(B * sr)
    env = envelope(seg, sr, B)
    if len(env) < 20 or env.max() <= 0:
        return {}
    top = float(env[:5].max())
    alive = np.nonzero(env > top * 1e-10)[0]
    end_b = int(alive[-1]) + 1 - int(round(fade / B)) if len(alive) else len(env)
    out = {'endS': round((int(alive[-1]) + 1) * B, 2) if len(alive) else None}
    if end_b < 15:
        return out
    env = env[:end_b]
    Ldb = 10 * np.log10(np.maximum(env / top, 1e-30))
    t = (np.arange(len(env)) + 0.5) * B
    # (1) the floor from the last 10 %, if the curve has levelled off there (< 10 dB/s); a sample that ends while
    # still decaying (SSO's are cut ~2.2-3.1 s after the start) has no floor in it: none subtracted, the decay past
    # the end extrapolated from the fit
    n_last = max(5, len(env) // 10)
    floor = float(env[-n_last:].mean())
    tc = len(env) - 1
    k = 0.0
    tail_slope = np.polyfit(t[-n_last:], Ldb[-n_last:], 1)[0]
    plateau = tail_slope > -10.0
    out['plateau'] = bool(plateau)
    if not plateau:
        floor = 0.0
        m = (Ldb <= -5) & (Ldb >= Ldb[-1] + 3)
        m[:np.argmax(Ldb)] = False
        if m.sum() >= 5:
            k = np.polyfit(t[m], Ldb[m], 1)[0]
    for _ in range(5 if plateau else 0):
        fdb = 10 * math.log10(max(floor / top, 1e-30))
        m = (Ldb <= -1) & (Ldb >= fdb + 10)
        m[:np.argmax(Ldb)] = False
        if m.sum() < 5:
            break
        k, c = np.polyfit(t[m], Ldb[m], 1)
        if k >= 0:
            break
        tcs = (fdb - c) / k
        tc = int(min(len(env) - 1, max(5, tcs / B)))
        # the floor again from the bins from 5 dB of decay past the crossing point (at least 10 % of the segment)
        j = int(min(len(env) - n_last, tc + (-5.0 / k) / B))
        newf = float(env[j:].mean()) if j < len(env) else floor
        if abs(10 * math.log10(newf / floor)) < 0.2:
            floor = newf
            break
        floor = newf
    rng = -10 * math.log10(max(floor / top, 1e-30)) if plateau else -float(Ldb[-1])
    out['floorDb'] = round(-rng, 1)
    s = np.clip(seg[:(tc + 1) * nb] - floor, 0, None)
    if s.sum() <= 0:
        return out
    # the tail past the crossing point: exponential at the fitted slope k (dB/s), from the level at tc
    tail = max(float(env[tc] - floor) * sr / (-k * math.log(10) / 10), 0.0) if k < 0 else 0.0
    sch = np.cumsum(s[::-1])[::-1] + tail
    Ls = 10 * np.log10(np.maximum(sch / sch[0], 1e-30))
    ts = np.arange(len(Ls)) / sr
    i_pk = int(np.argmax(envelope(s, sr, 0.005))) * int(0.005 * sr) if len(s) > 0 else 0
    out['range'] = round(rng, 1)
    for name, a, b in (('edt', 0, -10), ('t20', -5, -25), ('t30', -5, -35)):
        if rng < -b + (10 if plateau else 5):    # (the fit's end 10 dB above the floor (ISO 3382-1), or 5 dB above the
            continue                            # sample's end, the rest extrapolated)
        m = (Ls <= a - 1e-9 if a < 0 else Ls <= 0) & (Ls >= b)
        m[:i_pk] = False
        if m.sum() < 20:
            continue
        slope = np.polyfit(ts[m], Ls[m], 1)[0]
        if slope < 0:
            out[name] = round(-60.0 / slope, 3)
    return out


def analyze_one(work, tag, wav):
    import numpy as np
    import soundfile as sf
    meta = json.load(open(os.path.join(work, 'ev', tag + '.json')))
    x, sr = sf.read(wav, dtype='float32', always_2d=True)
    x = x.astype('float64')
    bands = {'all': x}
    for fc in BANDS:
        bands[str(fc)] = bandpass(x, fc, sr)
    pw = {k: (v ** 2).sum(axis=1) for k, v in bands.items()}
    notes = meta['notes']
    res = []
    for n, nn in enumerate(notes):
        i_on = int(round(nn['on'] * sr))
        nxt = notes[n + 1]['on'] if n + 1 < len(notes) else nn['off'] + LONG_GAP
        i_end = min(len(x), int(round((nxt - 0.05) * sr)))
        i_off = int(round(nn['off'] * sr))
        P = pw['all']
        e2 = envelope(P[i_on:i_end], sr, 0.002)
        pk2 = float(e2.max()) if len(e2) else 0
        r = {k: nn[k] for k in ('kind', 'pitch', 'take', 'on')}
        if pk2 <= 0:
            r['silent'] = True
            res.append(r)
            continue
        r['peakDb'] = round(db(pk2), 2)
        r['onsetMs'] = round(float(np.nonzero(e2 >= pk2 / 100)[0][0]) * 2 + 1, 1)
        e10 = envelope(P[i_on:i_end], sr, 0.01)
        ipk = int(np.argmax(e10))
        start = i_on + ipk * int(0.01 * sr) if nn['kind'] != 'long' else i_off
        r['startS'] = round(start / sr - nn['on'], 3)
        es = envelope(P[start:i_end], sr, 0.01)
        ref = float(es[:3].max()) if nn['kind'] == 'long' else float(e10.max())
        for ms in (200, 500, 1000):
            k = ms // 10
            r['at%d' % ms] = round(db(float(es[k])) - db(ref), 1) if k < len(es) else None
        r['bands'] = {}
        for k, p in pw.items():
            d = decay_params(p, sr, start, i_end)
            d['levelDb'] = round(db(float(p[i_on:i_end].sum()) / sr), 2)
            r['bands'][k] = d
        res.append(r)
    out = {'tag': tag, 'patch': meta['patch'], 'config': meta['config'], 'notes': res}
    json.dump(out, open(os.path.join(work, 'meas', tag + '.json'), 'w'), indent=0)
    return out


def deconvolve_patch(work, patch, wdir):
    """each mic against Close over the short and pizz notes"""
    import numpy as np
    import soundfile as sf
    tag = lambda c: '%s_%s' % (short(patch), c)
    ref_wav = os.path.join(wdir, tag('m1') + '.wav')
    if not os.path.exists(ref_wav):
        return None
    meta = json.load(open(os.path.join(work, 'ev', tag('m1') + '.json')))
    X, sr = sf.read(ref_wav, dtype='float32', always_2d=True)
    xm = X.astype('float64').mean(axis=1)
    L = int(4.5 * sr)
    nfft = 1 << int(math.ceil(math.log2(2 * L)))
    segs = [n for n in meta['notes'] if n['kind'] in ('shortheld', 'pizzheld')]
    Xs = [np.fft.rfft(xm[int((n['on'] - 0.1) * sr):int((n['on'] - 0.1) * sr) + L], nfft) for n in segs]
    f = np.fft.rfftfreq(nfft, 1 / sr)
    band = (f > 80) & (f < 12000)
    out = {'patch': patch, 'mics': {}}
    for c, _ in configs(patch):
        w = os.path.join(wdir, tag(c) + '.wav')
        if not os.path.exists(w):
            continue
        Y, _ = sf.read(w, dtype='float32', always_2d=True)
        ym = Y.astype('float64').mean(axis=1)
        num = np.zeros(len(f), complex)
        den = np.zeros(len(f))
        for n, Xf in zip(segs, Xs):
            i = int((n['on'] - 0.1) * sr)
            Yf = np.fft.rfft(ym[i:i + L], nfft)
            num += Yf * np.conj(Xf)
            den += np.abs(Xf) ** 2
        H = num / (den + 1e-3 * den[band].mean())
        H[~band] = 0
        h = np.fft.irfft(H, nfft)
        h = np.roll(h, int(0.05 * sr))          # (acausal part visible: 50 ms before lag 0)
        h = h[:int(0.05 * sr) + int(3.5 * sr)]
        hp = h ** 2
        ipk = int(np.argmax(hp))
        lag = (ipk - int(0.05 * sr)) / sr * 1000
        d0, d1 = ipk - int(0.0025 * sr), ipk + int(0.0025 * sr)
        direct = hp[d0:d1].sum()
        rest = hp[d1:].sum()
        pre = hp[:d0].sum()
        e50 = hp[d0:ipk + int(0.05 * sr)].sum()
        e80 = hp[d0:ipk + int(0.08 * sr)].sum()
        tot = hp[d0:].sum()
        # the first reflection: after the direct window, the first 1 ms bin within 10 dB of the direct peak's bin
        bins = envelope(hp[d1:d1 + int(0.15 * sr)], sr, 0.001)
        pkbin = hp[ipk - 22:ipk + 22].mean()
        refl = np.nonzero(bins >= pkbin / 10)[0]
        r = {'lagMs': round(lag, 2), 'drrDb': round(db(direct) - db(rest), 2), 'preDb': round(db(pre) - db(tot), 2),
             'c50Db': round(db(e50) - db(tot - e50), 2), 'c80Db': round(db(e80) - db(tot - e80), 2),
             'firstReflMs': round(float(refl[0]) + 2.5, 1) if len(refl) else None, 'bands': {}}
        hb = {'all': h}
        for fc in BANDS:
            if fc < 12000 / math.sqrt(2):
                hb[str(fc)] = bandpass(h[:, None], fc, sr)[:, 0]
        for k, v in hb.items():
            r['bands'][k] = decay_params(v ** 2, sr, ipk, len(v))
            vv = v ** 2
            e80b = vv[d0:ipk + int(0.08 * sr)].sum()
            r['bands'][k]['c80Db'] = round(db(e80b) - db(vv[d0:].sum() - e80b), 2)
        out['mics'][c] = r
    json.dump(out, open(os.path.join(work, 'meas', short(patch) + '.deconv.json'), 'w'), indent=1)
    return out


def cmd_analyze(a):
    jobs = json.load(open(os.path.join(a.work, 'jobs.json')))
    os.makedirs(os.path.join(a.work, 'meas'), exist_ok=True)
    for j in jobs:
        w = os.path.join(wav_dir(a), j['tag'] + '.wav')
        if os.path.exists(w):
            analyze_one(a.work, j['tag'], w)
            print(j['tag'], flush=True)
    for p in STRINGS + WINDS:
        deconvolve_patch(a.work, p, wav_dir(a))


# ----------------------------------------------------------------------------------------------------------- summary

def med(v):
    v = [x for x in v if x is not None]
    return round(statistics.median(v), 3) if v else None


def iqr(v):
    v = sorted(x for x in v if x is not None)
    if len(v) < 4:
        return None
    q = statistics.quantiles(v, n=4)
    return [round(q[0], 3), round(q[2], 3)]


def cmd_summary(a):
    jobs = json.load(open(os.path.join(a.work, 'jobs.json')))
    cfgname = lambda patch, c: (MICS[int(c[1]) - 1] if c.startswith('m') else 'Tree, Release 1.0')
    per = {}
    for j in jobs:
        f = os.path.join(a.work, 'meas', j['tag'] + '.json')
        if not os.path.exists(f):
            continue
        m = json.load(open(f))
        row = {}
        for kind in KINDS:
            ns = [n for n in m['notes'] if n['kind'] == kind and not n.get('silent')]
            if not ns:
                continue
            k = {'n': len(ns), 'onsetMs': med([n['onsetMs'] for n in ns]),
                 'at200': med([n['at200'] for n in ns]), 'at500': med([n['at500'] for n in ns]),
                 'at1000': med([n['at1000'] for n in ns]), 'bands': {}}
            for b in ['all'] + [str(x) for x in BANDS]:
                bb = [n['bands'][b] for n in ns]
                k['bands'][b] = {q: med([x.get(q) for x in bb]) for q in ('edt', 't20', 't30', 'range')}
                k['bands'][b]['nT30'] = sum(1 for x in bb if x.get('t30'))
            row[kind] = k
        per.setdefault(j['patch'], {})[cfgname(j['patch'], j['config'])] = row
    dec = {}
    for p in STRINGS + WINDS:
        f = os.path.join(a.work, 'meas', short(p) + '.deconv.json')
        if os.path.exists(f):
            d = json.load(open(f))
            dec[p] = {cfgname(p, c): v for c, v in d['mics'].items()}
    # per family x mic: the median over patches and note kinds (short, pizz for the decay of a struck note; long for
    # the release); T30 where measured, else T20
    fam = {}
    for fname, ps in (('strings', STRINGS), ('winds and brass', WINDS)):
        fam[fname] = {}
        for mic in MICS + ['Tree, Release 1.0']:
            got = [per[p][mic] for p in ps if p in per and mic in per[p]]
            if not got:
                continue
            e = {}
            for kind in KINDS:
                ks = [g[kind] for g in got if kind in g]
                if not ks:
                    continue
                e[kind] = {'at200': med([k['at200'] for k in ks]), 'at500': med([k['at500'] for k in ks]),
                           'at1000': med([k['at1000'] for k in ks]), 'bands': {}}
                for b in ['all'] + [str(x) for x in BANDS]:
                    e[kind]['bands'][b] = {q: med([k['bands'][b][q] for k in ks]) for q in ('edt', 't20', 't30')}
            dd = [dec[p][mic] for p in ps if p in dec and mic in dec[p]]
            if dd and mic != 'Close':
                e['relativeIR'] = {q: med([d[q] for d in dd]) for q in ('lagMs', 'drrDb', 'c50Db', 'c80Db')}
                e['relativeIR']['bands'] = {b: {q: med([d['bands'].get(b, {}).get(q) for d in dd]) for q in ('edt', 't20', 't30', 'c80Db')}
                                            for b in ['all'] + [str(x) for x in BANDS]}
            fam[fname][mic] = e
    out = {'_about': __doc__.split('\n\n')[0] + ' Made by tools/soundlibraries/sso_room.py summary.',
           'method': {'kthost': KTHOST + ' (build #474)', 'rate': SR, 'bands': BANDS, 'shortLen': SHORT_LEN,
                      'shortGap': SHORT_GAP, 'longLen': LONG_LEN, 'longGap': LONG_GAP, 'velocity': 100, 'cc1': 100,
                      'cc11': 127, 'patches': STRINGS + WINDS},
           'families': fam, 'patches': per, 'relativeIR': dec}
    if a.musereverb:
        out['musereverb'] = musereverb_summary(a.musereverb)

    def compact(o):
        if isinstance(o, dict):
            return {k: compact(v) for k, v in o.items() if k not in ('endS', 'plateau', 'floorDb', 'firstReflMs', 'preDb')}
        if isinstance(o, list):
            return [compact(v) for v in o]
        return round(o, 3) if isinstance(o, float) else o
    out = compact(out)
    json.dump(out, open(a.o, 'w'), indent=1)
    print('wrote', a.o)


def musereverb_summary(path):
    """the MS4 reverb's impulse response (wet, stereo float32 interleaved, 44.1 kHz) by the same decay code"""
    import numpy as np
    x = np.fromfile(path, dtype='float32').reshape(-1, 2).astype('float64')
    sr = SR
    p = (x ** 2).sum(axis=1)
    ipk = int(np.argmax(envelope(p, sr, 0.001))) * int(0.001 * sr)
    first = int(np.nonzero(p > p.max() * 1e-4)[0][0])
    out = {'firstArrivalMs': round(first / sr * 1000, 2), 'peakMs': round(ipk / sr * 1000, 1), 'bands': {}}
    bands = {'all': x}
    for fc in BANDS:
        bands[str(fc)] = bandpass(x, fc, sr)
    for k, v in bands.items():
        pp = (v ** 2).sum(axis=1)
        out['bands'][k] = decay_params(pp, sr, first, len(pp))
    # the send 0.3 against the dry signal: the wet energy over the dry impulse's (1 + 1 for the two channels)
    out['wetToDryDbAtSend0.3'] = round(db(float(p.sum()) * 0.09 / 2.0), 2)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest='cmd', required=True)
    for c in ('events', 'run', 'analyze', 'summary'):
        s = sub.add_parser(c)
        s.add_argument('work')
        s.add_argument('--wav-dir')
        if c == 'run':
            s.add_argument('--only')
            s.add_argument('--batch-min', type=float, default=35)
            s.add_argument('--rm-vm', action='store_true')
            s.add_argument('--keep-wavs', action='store_true')
        if c == 'summary':
            s.add_argument('-o', default=os.path.join(HERE, 'sso_room.json'))
            s.add_argument('--musereverb')
    a = ap.parse_args()
    {'events': cmd_events, 'run': cmd_run, 'analyze': cmd_analyze, 'summary': cmd_summary}[a.cmd](a)


if __name__ == '__main__':
    main()
