"""Release re-measure with a long tail (numbers-measured, 2026-10-03), on the Windows VM: for each sound of a patch, as
the rest check plays it (its newest rest result: value / drum key, the hold `seconds`, the pitches of its range), each
note at mf (velocity = CC1 = 80, CC11 127) held that long, then TAIL s of silence, through kthost (offline Kontakt 8).
Release as ArticulationCheck::rest measures it: power in 5 ms windows; `before` = the loudest window in the 100 ms before
the note-off; the release = from the note-off to the end of the last window within D dB of `before` (D = 30; 15 and
60 too, for the threshold's sensitivity); `sustains`: before >= the note's peak - 20 dB (else no release). -1: the
last window within D dB is the render's last (the tail too short).

  release_long_tail.py <patch> <out json> [--tail 25] [--sounds a,b] [--every n]     ("~" in an argument: a space)
Needs: kthost (an offline VST 3 host, LIVE.md › Measured with SSO; on the VM C:\claude\nm\kthost.exe), the rest
check's setups and results (Documents\MuseScore Sound Library Check), sso_sound_range.json next to it (the pitches).
releases_from_long_tail.py puts the results into sso_sound_range.json.
Writes {sound: {"value", "key", "seconds", "rows": [[pitch, sustains, r15, r30, r60, peakDb, beforeDb], ...]}}."""
import glob, io, json, os, subprocess, sys, zipfile
import numpy as np
import soundfile as sf

BASE = r"C:\Users\user\Documents\MuseScore Sound Library Check"
SETUPS = BASE + r"\background rest check setups\Spitfire Symphony Orchestra"
KT = r"C:\claude\nm\kthost.exe"
PLUGIN = r"C:\Program Files\Common Files\VST3\Kontakt 8.vst3"
KEYSWITCHED = {'Timpani', 'Celeste', 'Glockenspiel', 'Marimba', 'Vibraphone', 'Tubular Bells', 'Other - Harp glissandi'}
WORK = r"C:\claude\nm\rel"

args = sys.argv[1:]
args = [a.replace('~', ' ') for a in args]          # ('~' for a space: a batch file's argument)
patch, out = args[0], args[1]
tail = float(args[args.index('--tail') + 1]) if '--tail' in args else 25.0
only = args[args.index('--sounds') + 1].split(',') if '--sounds' in args else None
every = int(args[args.index('--every') + 1]) if '--every' in args else 1
os.makedirs(WORK, exist_ok=True)


def rest_results(name):
    found = None
    for z in sorted(glob.glob(BASE + r"\*.zip")):
        try:
            zf = zipfile.ZipFile(z)
        except Exception:
            continue
        stack = [zf]
        while stack:
            f = stack.pop()
            for n in f.namelist():
                if n.endswith('.zip'):
                    stack.append(zipfile.ZipFile(io.BytesIO(f.read(n))))
                elif n.endswith('results.json'):
                    try:
                        d = json.loads(f.read(n))
                    except Exception:
                        continue
                    for p in d.get('patches') or []:
                        if (p.get('name') or p.get('patch')) == name and p.get('rest'):
                            # (zips sorted by date: the newest wins, sound by sound; one with its range over one without)
                            for r in p['rest']:
                                k = (r.get('drum'), r.get('key'), tuple(r.get('names') or []), r.get('value'))
                                if found is None:
                                    found = {}
                                if r.get('range') or k not in found:
                                    found[k] = r
    return list(found.values()) if found else None


def sound_name(r):
    if r.get('drum'):
        return f"{r['drum']} (key {r['key']})"
    return (r.get('names') or ['?'])[0]


def render(r, pitches, hold):
    ev, notes, t = [(0.0, 'cc', 1, 80), (0.0, 'cc', 11, 127)], [], 0.3
    for p in pitches:
        if r.get('value', -1) >= 0 and not r.get('drum'):
            if patch in KEYSWITCHED:
                ev += [(t, 'on', r['value'], 100), (t + 0.05, 'off', r['value'], 0)]
            else:
                ev.append((t, 'cc', 32, r['value']))
        t += 0.2
        ev += [(t, 'on', p, 80), (t + hold, 'off', p, 0)]
        notes.append((p, t, t + hold))
        t += hold + tail
    evf = os.path.join(WORK, 'rel.txt')
    with open(evf, 'w') as f:
        for e in sorted(ev, key=lambda e: e[0]):
            f.write(f'{e[0]:.6f} {e[1]} {e[2]} {e[3]}\n')
    wav = os.path.join(WORK, 'rel.wav')
    subprocess.run([KT, PLUGIN, os.path.join(SETUPS, patch + '.vst3state'), evf, wav, '--tail', str(tail), '--wait', '20'],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if not os.path.exists(wav):
        print(patch, 'render failed', pitches, flush=True)
        return []
    snd = sf.SoundFile(wav)
    sr, total = snd.samplerate, snd.frames
    win = int(sr * 0.005)
    rows = []
    for k, (p, on, off) in enumerate(notes):
        end = notes[k + 1][1] - 0.2 if k + 1 < len(notes) else total / sr
        snd.seek(int(on * sr))
        seg = snd.read(int(end * sr) - int(on * sr), dtype='float32', always_2d=True)
        n = len(seg) // win
        env = 10 * np.log10(np.mean(seg[:n * win].reshape(n, win, -1) ** 2, axis=(1, 2)) + 1e-30)
        offw = int(round((off - on) / 0.005))
        peak = float(env[:offw].max())
        before = float(env[max(0, offw - 20):offw].max())
        sustains = before >= peak - 20
        rel = []
        for d in (15, 30, 60):
            idx = np.nonzero(env[offw:] >= before - d)[0]
            last = offw + (idx[-1] if len(idx) else -1)
            rel.append(-1 if last + 1 >= len(env) else int(max(0, last + 1 - offw) * 5))
        rows.append([p, bool(sustains), *rel, round(peak, 1), round(before, 1)])
    snd.close()
    os.remove(wav)
    return rows


RANGE = json.load(open(r"C:\claude\nm\sso_sound_range.json", encoding='utf-8'))
rest = rest_results(patch)
if not rest:
    print('no rest results for', patch); sys.exit(1)
res = json.load(open(out)) if os.path.exists(out) else {}
for r in rest:
    if r.get('silent') or r.get('pitch', -1) < 0:
        continue
    name = sound_name(r)
    if only and name not in only and r.get('drum', '') not in only:
        continue
    if name in res:
        continue
    # (the pitches: the rest check's range rows, sso_sound_range.json, else this result's own)
    known = RANGE.get(patch, {}).get(name, {}).get('range') if isinstance(RANGE.get(patch, {}).get(name), dict) else None
    pitches = [row[0] for row in (known or r.get('range') or [])] if not r.get('drum') else [r['key']]
    pitches = pitches[::every] if pitches else [r['pitch']]
    hold = float(r.get('seconds') or 1.5)
    rows = []
    for c in range(0, len(pitches), 12):          # (12 notes a render: kthost holds its render in memory)
        rows += render(r, pitches[c:c + 12], hold)
    res[name] = dict(value=r.get('value'), key=r.get('key'), seconds=hold, tail=tail, rows=rows)
    json.dump(res, open(out, 'w'), indent=0)
    mx = max((row[3] for row in rows if row[1]), default=None)
    print(patch, '|', name, '| notes', len(rows), '| max release 30 dB', mx, flush=True)
