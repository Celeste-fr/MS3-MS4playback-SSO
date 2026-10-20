#!/usr/bin/env python3
"""Blind clips, round 5 (150 ms class, held-out t9 phrases): ours vs SSO, the same phrases, levels matched, names random.

    blind5_prep.py <ours.wav> <t9.json> <out dir> <answers.json> <sso_pass1> <sso_pass2> <sso_pass3> [--seed N]

Per phrase: ours plus two of SSO's three passes (chosen at random), cut as round 3 (/data/abviola/tools/prep3.py): first
note-on - 0.1 s to the last note-off + 2.62 s (Violas - Performance release), 10 ms fade-in, 0.5 s fade-out; every
clip to one integrated loudness (pyloudnorm, ITU-R BS.1770), the loudest common level with all peaks <= -1 dBFS,
at most -16 LUFS; 16-bit FLAC + 320k MP3 named by a random 12-hex id.
<out>/trials.json (publishable): trials in random order, each {"trial", "A", "B", "ref"}: A / B = ours and one SSO pass
in random order, ref = the other SSO pass (for an ABX or a same/different control). <answers.json> (keep private,
outside the published folder): which file is which, gains, loudness; next to it <answers>_clipmap.json, abgate.py's
map (private too: it names ours).
"""
import json, os, secrets, subprocess, sys
import numpy as np
import soundfile as sf
import pyloudnorm as pyln

SR = 44100
PRE, REL, FADE_OUT, FADE_IN = 0.1, 2.62, 0.5, 0.01


def main():
    a = sys.argv[1:]
    seed = int(a[a.index('--seed') + 1]) if '--seed' in a else 20261020
    ours, meta, out, ans = a[0], a[1], a[2], a[3]
    ssos = a[4:7]
    rnd = np.random.default_rng(seed)
    T = json.load(open(meta))
    src = {'ours': sf.read(ours, always_2d=True, dtype='float64')[0]}
    for i, p in enumerate(ssos):
        src['sso%d' % (i + 1)] = sf.read(p, always_2d=True, dtype='float64')[0]
    clips = {}
    for s in T['segments']:
        ph = s['phrase']
        b0, b1 = int(round((s['notes'][0]['on'] - PRE) * SR)), int(round((s['notes'][-1]['off'] + REL) * SR))
        two = sorted(rnd.choice([1, 2, 3], 2, replace=False).tolist())
        for k in ['ours'] + ['sso%d' % j for j in two]:
            x = src[k][b0:b1].copy()
            t = np.arange(len(x)) / SR
            x *= (np.clip(t / FADE_IN, 0, 1) * np.clip((len(x) / SR - t) / FADE_OUT, 0, 1))[:, None]
            clips[(ph, k)] = x
    meter = pyln.Meter(SR)
    L = {k: meter.integrated_loudness(x) for k, x in clips.items()}
    P = {k: 20 * np.log10(np.abs(x).max()) for k, x in clips.items()}
    target = min(-16.0, min(L[k] - P[k] for k in clips) - 1.0)
    # second pass at the target level: BS.1770's -70 LUFS absolute gate flips tail blocks when measured ~25 dB lower
    L = {k: L[k] + meter.integrated_loudness(x * 10 ** ((target - L[k]) / 20)) - target for k, x in clips.items()}
    os.makedirs(out + '/audio', exist_ok=True)
    names, info = {}, {}
    for (ph, k), x in clips.items():
        nm = secrets.token_hex(6)
        names[(ph, k)] = nm
        y = x * 10 ** ((target - L[(ph, k)]) / 20)
        tmp = out + '/tmp.wav'
        sf.write(tmp, y, SR, subtype='PCM_24')
        base = '%s/audio/%s' % (out, nm)
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', tmp, '-sample_fmt', 's16', base + '.flac'], check=True)
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-i', tmp, '-c:a', 'libmp3lame', '-b:a', '320k',
                        base + '.mp3'], check=True)
        os.remove(tmp)
        info.setdefault(str(ph), {})[k] = dict(file=nm, lufs_before=round(L[(ph, k)], 2),
                                              gain_db=round(target - L[(ph, k)], 2), dur=round(len(y) / SR, 3),
                                              lufs_after=round(meter.integrated_loudness(
                                                  sf.read(base + '.flac')[0]), 2))
    trials, key = [], []
    phs = sorted({ph for ph, _ in clips})
    order = rnd.permutation(len(phs))
    for t, i in enumerate(order):
        ph = phs[i]
        ss = sorted(k for (p, k) in clips if p == ph and k != 'ours')
        vs = ss[int(rnd.integers(2))]
        ref = [k for k in ss if k != vs][0]
        ab = ['ours', vs] if rnd.integers(2) else [vs, 'ours']
        trials.append(dict(trial=t + 1, A=names[(ph, ab[0])], B=names[(ph, ab[1])], ref=names[(ph, ref)]))
        key.append(dict(trial=t + 1, phrase=ph, A=ab[0], B=ab[1], ref=ref, ours='A' if ab[0] == 'ours' else 'B'))
    json.dump(dict(target_lufs=round(target, 2), clip_window=dict(pre=PRE, rel=REL, fade_in=FADE_IN, fade_out=FADE_OUT),
                   trials=trials), open(out + '/trials.json', 'w'), indent=1)
    cm = {str(ph): {'own': names[(ph, 'ours')], **{'sso%d' % (j + 1): names[(ph, k)] for j, k in enumerate(
        sorted(k for (p, k) in clips if p == ph and k != 'ours'))}} for ph in phs}
    json.dump(cm, open(os.path.splitext(ans)[0] + '_clipmap.json', 'w'), indent=1)
    json.dump(dict(seed=seed, ours=os.path.abspath(ours), sso=[os.path.abspath(p) for p in ssos], target_lufs=target,
                   key=key, clips=info), open(ans, 'w'), indent=1)
    print('%d trials, %d clips, target %.2f LUFS -> %s (answers: %s)' % (len(trials), len(clips), target, out, ans))


if __name__ == '__main__':
    main()
