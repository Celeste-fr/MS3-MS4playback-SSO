# heard.py <dump dir> <wav dir> <prefix> <out.json> [part,part,...] [first bar] [last bar]: when every library note of
# a dumpEvents dump is heard in its kthost renders (events.py's <prefix>_<route>.wav, <prefix>.shift), by the rules of
# the 2026-10-16 Violas investigation (anactx.py, job vla912), for every part:
#   a slur's first note (Performance route, an attack: trace performance 1): its route's perceived envelope
#     (tools/playbackverify/loudness.py) against the slur's own top (the max from its note-on to the slur's written
#     end): heard = the first value within 15 dB of it, minus 30 ms (onset_detect.py's LATENCY_MS);
#   a transition (trace performance 2): pYIN f0 of its route (librosa, hop 128, frame 2048; 4096 under 100 Hz) after
#     the note-on, from when it leaves the note before's pitch (more than 30 cents from it, or unvoiced): arrival = the
#     first of 3 frames in a row within 30 cents of the new pitch (its cents included); also, as the map's bendGlide is
#     measured (legato_glide/glide.py: YIN, 2.5 ms steps, 30 ms frames, 5-frame median, phi = the share of the way from
#     the note before's pitch to the new one), leave = phi >= 0.1 and glideArrive = phi >= 0.9 (ms after the note-on);
#   any other note (All techniques routes: staccatos, held notes): the sum of the part's non-Performance routes'
#     envelope; in [note-on - 10, + 200 ms] the peak; heard = the first value after the last one under peak - 15 dB
#     before it, minus 30 ms (None: no 15 dB dip, the tail of the note before).
# Rows: part, bar, beat, pitch, slur, k (position in the slur, 0 first; -1 none), kind, written s, sent ms (note-on -
# written), heard ms (- written; None: not found), heardAfterOn (heard - note-on), lenBefore (ms from the note before's
# note-on as sent, same route: its played length up to this note-on), before ('attack' / 'transition'), model (the
# trace's glide arrival after the note-on), velocity.
import json, os, sys
import numpy as np, soundfile as sf, librosa
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'playbackverify'))
from loudness import perceived_envelope  # noqa: E402
sys.path.insert(0, os.path.join(HERE, '..', 'legato_glide'))
from f0 import yin_track, hz  # noqa: E402
from scipy.signal import medfilt  # noqa: E402

dump, wavd, prefix, outf = sys.argv[1:5]
PARTS = set(sys.argv[5].split(',')) if len(sys.argv) > 5 and sys.argv[5] else None
A = int(sys.argv[6]) if len(sys.argv) > 6 else 1
B = int(sys.argv[7]) if len(sys.argv) > 7 else 0
LAT = 30.0
shift, lead = [float(x) for x in open(os.path.join(wavd, prefix + '.shift')).read().split()]
OFF = lead - shift + 1.0          # render seconds = written seconds + OFF (sent times: dump time - shift + 1.0)
bars = [float(x) for x in open(os.path.join(dump, 'bars.txt')).read().split()]
t0, t1 = bars[A - 1], (bars[B] if 0 < B < len(bars) else 1e9)
bpm = {}
routes = {}
for ln in open(os.path.join(dump, 'routemap.txt')).read().splitlines():
    r, part, patch, lane, cents, bend = ln.split('|')
    if (not PARTS or part in PARTS) and os.path.exists(os.path.join(wavd, '%s_%s.wav' % (prefix, r))):
        routes[r] = (part, patch)
ons = []
for r, (part, patch) in routes.items():
    for e in open(os.path.join(dump, r + ' museScore.txt')).read().splitlines():
        x = e.split()
        if len(x) >= 4 and x[1] == 'on':
            ons.append(dict(t=float(x[0]) - lead, key=int(x[2]), vel=int(x[3]), route=r, patch=patch, part=part))
trace = {}
for ln in open(os.path.join(dump, 'trace.txt')).read().splitlines():
    x = ln.split('|')
    trace[(x[0], round(float(x[1]), 4), int(x[2]))] = (x[3], int(x[4]), float(x[5]), float(x[6]))
notes = []
for ln in open(os.path.join(dump, 'notes.txt')).read().splitlines():
    x = ln.split('|')
    if (not PARTS or x[0] in PARTS) and t0 - 0.3 <= float(x[1]) < t1:
        notes.append(dict(part=x[0], w=float(x[1]), end=float(x[2]), pitch=int(x[3]), slur=int(x[5]), cents=float(x[6])))
notes.sort(key=lambda n: (n['part'], n['w'], n['pitch']))
used = set()
for n in notes:
    tr = trace.get((n['part'], round(n['w'], 4), n['pitch']))
    n['perf'] = tr[1] if tr else 0
    n['model'] = tr[3] if tr else -1
    exp = 0.02 if not tr else tr[3] / 1000 if tr[1] == 2 else 0.2 if tr[1] == 1 else 0.05
    cand = [i for i, o in enumerate(ons) if o['part'] == n['part'] and o['key'] == n['pitch'] and i not in used
            and -0.6 <= o['t'] - n['w'] <= 0.15 and (not tr or o['patch'] == tr[0])]
    n['on'] = None
    if cand:
        i = min(cand, key=lambda i: abs(ons[i]['t'] - n['w'] + exp))
        used.add(i)
        n.update(on=ons[i]['t'], route=ons[i]['route'], patch=ons[i]['patch'], vel=ons[i]['vel'])
wav = {}


def load(r):
    if r not in wav:
        x, sr = sf.read(os.path.join(wavd, '%s_%s.wav' % (prefix, r)))
        wav[r] = (x, sr)
    return wav[r]


env = {}


def envelope(key, rs):
    if key not in env:
        xs, sr = None, None
        for r in rs:
            x, sr = load(r)
            if xs is None:
                xs = x.copy()
            else:
                y = np.zeros((max(len(x), len(xs)),) + x.shape[1:])
                y[:len(xs)] += xs
                y[:len(x)] += x
                xs = y
        env[key] = perceived_envelope(xs, sr)
    return env[key]


def idx(t_ms, first, hop):
    return max(0, int(round((t_ms - first) / hop)))


def arrival(r, on, prev, target):
    x, sr = load(r)
    m = x.mean(axis=1) if x.ndim == 2 else x
    lo, hi = min(prev, target) - 3, max(prev, target) + 3
    fmin, fmax = librosa.midi_to_hz(lo), librosa.midi_to_hz(hi)
    fl = 2048 if fmin >= 100 else 4096
    a = int((on - 0.05) * sr)
    seg = m[max(0, a):a + int(0.85 * sr)]
    f0, vf, vp = librosa.pyin(seg, fmin=fmin, fmax=fmax, sr=sr, frame_length=fl, hop_length=128)
    fh = 128 / sr
    base = max(0, a) / sr
    i0 = int(round((on - base) / fh))
    for i in range(i0, len(f0)):
        if np.isnan(f0[i]) or abs(12 * np.log2(f0[i] / 440) + 69 - prev) > 0.30:
            i0 = i
            break
    for i in range(i0, len(f0) - 3):
        if all(not np.isnan(f0[j]) and abs(12 * np.log2(f0[j] / 440) + 69 - target) < 0.30 for j in range(i, i + 3)):
            return base + i * fh
    return None


def glide(r, on, prev, target, ioi):
    x, sr = load(r)
    a = x.mean(axis=1) if x.ndim == 2 else x
    lo, hi = min(prev, target) - 2, max(prev, target) + 2
    t, f = yin_track(a, sr, on - 0.03, on + max(ioi, 0.3) + 0.15, hz(lo), hz(hi), step=0.0025, win=0.030)
    c = 1200 * np.log2(f / hz(target)); ok = ~np.isnan(c)
    out = {'leave': None, 'glideArrive': None}
    if ok.sum() >= 10:
        c = medfilt(np.interp(t, t[ok], c[ok]), 5)
        c0 = (prev - target) * 100.0
        if abs(c0) < 1:
            return out
        phi = (c - c0) / (0 - c0)
        for xx, key in ((0.1, 'leave'), (0.9, 'glideArrive')):
            for k in range(len(t)):
                if t[k] >= on - 0.01 and phi[k] >= xx and np.min(phi[k:k + 6]) >= xx - 0.25:
                    out[key] = round((t[k] - on) * 1000, 1)
                    break
    return out


rows = []
byslur = {}
for n in notes:
    if n['slur'] >= 0:
        byslur.setdefault((n['part'], n['slur']), []).append(n)
for n in notes:
    if n['w'] < t0 - 1e-6 or n['on'] is None:
        continue
    bar = max(i for i, b in enumerate(bars) if b <= n['w'] + 1e-6)
    group = byslur.get((n['part'], n['slur']), [n]) if n['slur'] >= 0 else [n]
    k = group.index(n) if n['slur'] >= 0 else -1
    res = dict(part=n['part'], route=n['route'], bar=bar + 1, w=round(n['w'], 6), pitch=n['pitch'], cents=n['cents'], slur=n['slur'], k=k,
               sent=round((n['on'] - n['w']) * 1000, 1), vel=n['vel'], patch=n['patch'], len=round((n['end'] - n['w']) * 1000, 1))
    same = [m for m in notes if m['part'] == n['part'] and m.get('route') == n['route'] and m['on'] is not None
            and m['on'] < n['on'] - 1e-6]
    if same:
        pb = max(same, key=lambda m: m['on'])
        res['lenBefore'] = round((n['on'] - pb['on']) * 1000, 1)
        res['before'] = 'transition' if pb['perf'] == 2 else 'attack' if pb['perf'] == 1 else 'other'
    heard = None
    on_ms = (n['on'] + OFF) * 1000
    if 'Performance' not in n['patch']:
        rs = [r for r, (p, pa) in routes.items() if p == n['part'] and 'Performance' not in pa]
        e, first, hop = envelope(('all', n['part']), rs)
        a, b = idx(on_ms - 10, first, hop), idx(on_ms + 200, first, hop)
        seg = e[a:b]
        if len(seg):
            kk = int(np.argmax(seg))
            below = np.where(seg[:kk + 1] < seg[kk] - 15)[0]
            if len(below):
                heard = first + hop * (a + below[-1] + 1) - LAT
        res['kind'] = 'other'
    elif n['perf'] == 1 or n['perf'] == 0:
        e, first, hop = envelope(('r', n['route']), [n['route']])
        endw = max(g['end'] for g in group)
        a, b = idx(on_ms, first, hop), idx((endw + OFF) * 1000, first, hop)
        seg = e[a:max(b, a + 1)]
        top = seg.max()
        i = int(np.argmax(seg >= top - 15))
        heard = first + hop * (a + i) - LAT
        res['kind'] = 'first'
        res['topMs'] = round(first + hop * (a + int(np.argmax(seg))) - LAT - on_ms, 1)
    else:
        pv = group[k - 1] if k > 0 else None
        prev = (pv['pitch'] + pv['cents'] / 100) if pv else n['pitch']
        arr = arrival(n['route'], n['on'] + OFF, prev, n['pitch'] + n['cents'] / 100)
        heard = arr * 1000 - LAT * 0 if arr is not None else None
        res['kind'] = 'transition'
        res['model'] = n['model']
        res.update(glide(n['route'], n['on'] + OFF, prev, n['pitch'] + n['cents'] / 100, res.get('lenBefore', 300) / 1000))
    if heard is not None:
        res['heard'] = round(heard - (n['w'] + OFF) * 1000, 1)
        res['heardAfterOn'] = round(heard - on_ms, 1)
    else:
        res['heard'] = res['heardAfterOn'] = None
    rows.append(res)
json.dump(rows, open(outf, 'w'), indent=0)
print(outf, len(rows), 'notes;', sum(1 for r in rows if r['heard'] is None), 'not found')
