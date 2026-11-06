"""In-context level of every part at its written dynamic, from anechoic orchestra recordings.

balance.py <config.json>   (config: piece, readings tsv, envelope npz, track map, time signature, ...)

1. readings (bar:beat rows, FORMAT.txt) -> per part: notes spans, marking timeline, changing spans (hairpins,
   cresc./dim.), accents.
2. score activity S[part, position] (1/8 bar grid) against audio activity A[part, frame] (50 ms frames, a frame active
   when its envelope is over noise + 10 dB and within 30 dB of the track's 99th percentile); a monotonic path
   (DTW, steps (1,1) (1,2) (2,1), open end on the score axis) maps frames to score positions.
3. steady segments: positions where the part plays (notes span), its marking is constant, no hairpin / cresc. / dim.
   span covers it, more than 0.25 bar from an accent and from the segment's ends; level = power mean of the part's
   active frames there (noise power subtracted), one value per track, the tracks of one part (repeated passes)
   power-averaged.
Output: rows (part, segment start/end bar, marking, frames, level dB re full scale (+ calibration), tracks).
"""
import sys, json, re, numpy as np

ORDER = ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff']


def norm_marking(v):
    v = v.strip().lower().replace('più', 'piu').replace('sempre', '').replace('.', ' ').strip()
    v = re.sub(r'\s+', ' ', v)
    for m in ['fff', 'ff', 'mf', 'mp', 'ppp', 'pp', 'p', 'f']:
        if v == m or v.startswith(m + ' '):
            return m
    return None


def pos(s, beats):
    """'12:2.5' -> 11 + 1.5 / beats (bars from 0)"""
    s = s.strip()
    if not s:
        return None
    if ':' in s:
        b, t = s.split(':', 1)
        return float(b) - 1 + (float(t) - 1) / beats(float(b))
    return float(s) - 1


def read(path, beats, endPad=0.0):
    parts = {}
    for line in open(path, encoding='utf-8'):
        if line.startswith('#') or not line.strip():
            continue
        c = line.rstrip('\n').split('\t')
        c += [''] * (6 - len(c))
        part, typ, a, b, val, com = [x.strip() for x in c[:6]]
        if typ not in ('notes', 'dyn', 'accent', 'cresc', 'dim', 'text'):
            continue
        p = parts.setdefault(part, {'notes': [], 'dyn': [], 'accent': [], 'change': [], 'text': [], 'uncertain': []})
        try:
            pa = pos(a, beats)
            pb = pos(b, beats) if b else None
        except ValueError:
            continue
        if 'uncertain' in com.lower():
            p['uncertain'].append((pa, typ, val))
        if typ == 'notes':
            # a span ends at the end of its last note: "end" given as that note's bar:beat start; allow one beat
            p['notes'].append((pa, (pb + endPad) if pb is not None else pa + 1.0 / beats(float(a.split(':')[0]))))
        elif typ == 'dyn':
            m = norm_marking(val)
            if m:
                p['dyn'].append((pa, m))
            else:
                p['accent'].append(pa)      # sfp, fp etc. written as dyn
        elif typ == 'accent':
            if val.strip() not in ('>', '^'):
                p['accent'].append(pa)
        elif typ in ('cresc', 'dim'):
            p['change'].append((pa, pb))
        else:
            p['text'].append((pa, val))
    for p in parts.values():
        p['dyn'].sort()
        ch = []
        for a, b in p['change']:
            if b is None:      # a cresc./dim. text: until the next dynamic, at most 4 bars
                nxt = [x for x, _ in p['dyn'] if x > a + 1e-6]
                b = min(nxt[0], a + 4.0) if nxt else a + 4.0
            ch.append((a, b))
        p['change'] = ch
    return parts


TECH_ON = ('gest', 'mit dämpfer', 'dämpfer auf', 'pizz', 'con sord', 'col legno', 'sul pont', 'sul tasto')
TECH_OFF = ('offen', 'dämpfer ab', 'arco', 'ohne dämpfer', 'senza sord', 'ordin', 'nat')


def technique_spans(p, end=1e9):
    """spans where a non-ordinary technique (stopped horn, mute, pizz., ...) is in force: excluded"""
    out, on = [], None
    for x, v in sorted(p['text'], key=lambda t: t[0]):
        t = v.lower()
        if any(k in t for k in TECH_OFF):
            if on is not None:
                out.append((on, x)); on = None
        elif any(k in t for k in TECH_ON):
            if on is None:
                on = x
    if on is not None:
        out.append((on, end))
    return out


def marking_at(p, x):
    m = None
    for a, v in p['dyn']:
        if a <= x + 1e-6:
            m = v
    return m


def activity(env, floor=None):
    """active frames: over the noise + 10 dB and within 30 dB of the 99th percentile"""
    e = env[env > -150]
    if len(e) < 10:
        return np.zeros(len(env), bool), -200.0
    nf = floor if floor is not None else np.percentile(e, 5)
    top = np.percentile(e, 99)
    return (env > max(nf + 10, top - 30)), nf


def dtw_open_end(C, band=None):
    """C[s, f] cost; path from (0,0) to (any s, F-1); steps (1,1) (1,0)->(s+1,f) (0,1)->(s,f+1) with penalty"""
    S, F = C.shape
    D = np.full((S, F), np.inf)
    P = np.zeros((S, F), np.int8)
    D[0, 0] = C[0, 0]
    pen = 0.5 * float(np.median(C))
    for f in range(F):
        for s in range(S):
            if s == 0 and f == 0:
                continue
            best, arg = np.inf, 0
            if s > 0 and f > 0 and D[s - 1, f - 1] < best:
                best, arg = D[s - 1, f - 1], 1
            if s > 0 and D[s - 1, f] + pen < best:
                best, arg = D[s - 1, f] + pen, 2
            if f > 0 and D[s, f - 1] + pen < best:
                best, arg = D[s, f - 1] + pen, 3
            D[s, f] = best + C[s, f]
            P[s, f] = arg
    s = int(np.argmin(D[:, F - 1] / (np.arange(S) + F)))
    f = F - 1
    path = [(s, f)]
    while (s, f) != (0, 0):
        a = P[s, f]
        if a == 1:
            s, f = s - 1, f - 1
        elif a == 2:
            s -= 1
        else:
            f -= 1
        path.append((s, f))
    return path[::-1]


def phenicx_map(annot_dir):
    """PHENICX-Anechoic: each family's score MIDI times (_o.txt) paired note by note with the hand-aligned times
    (.txt) -> a monotone piecewise-linear map score-MIDI seconds -> audio seconds"""
    import glob
    pairs = []
    for f in glob.glob(annot_dir + '/*_o.txt'):
        o = [l.split(',') for l in open(f)]
        a = [l.split(',') for l in open(f.replace('_o.txt', '.txt'))]
        if len(o) != len(a):
            continue
        for x, y in zip(o, a):
            if x[2].strip() == y[2].strip():
                pairs.append((float(x[0]), float(y[0])))
    p = np.array(sorted(pairs))
    xs = np.unique(p[:, 0])
    ys = np.array([np.median(p[p[:, 0] == x, 1]) for x in xs])
    ys = np.maximum.accumulate(ys)          # monotone
    return xs, ys


FRAME = 0.05


STAT = 'power'


def seg_level(env, nf, sel):
    """power mean of the selected active frames, the noise power (5th percentile) subtracted"""
    e = env[sel]
    if STAT == 'dbmean':
        return float(np.mean(e))
    pw = np.mean(10 ** (e / 10)) - 10 ** (nf / 10)
    return 10 * np.log10(pw) if pw > 0 else None


def run(cfg):
    beats = lambda b: cfg.get('beats', 4)
    parts = read(cfg['readings'], beats, cfg.get('notesEndPad', 0.0))
    env = np.load(cfg['env'])
    tracks = cfg['tracks']
    # frame -> score position (bars from 0)
    F = max(len(env[t]) for ts in tracks.values() for t in ts if t in env.files)
    times = (np.arange(F) + 0.5) * FRAME
    tm = cfg['timing']
    if tm['type'] == 'phenicx':
        xs, ys = phenicx_map(tm['dir'])
        t_o = np.interp(times - tm.get('audioOffset', 0.0), ys, xs, left=np.nan, right=np.nan)
        # piecewise bars: list of [bar0_seconds_in_midi, seconds_per_bar] or a constant
        posn = (t_o - tm['bar1']) / tm['barlen']
    else:
        posn = np.interp(times, tm['times'], tm['bars'], left=np.nan, right=np.nan)
    act, nfl = {}, {}
    for p, ts in tracks.items():
        for t in ts:
            if t in env.files:
                a, nf = activity(env[t])
                act[t] = np.pad(a, (0, F - len(a))); nfl[t] = nf
    out = {'segments': [], 'check': {}}
    for p, ts in tracks.items():
        rp = cfg.get('readAs', {}).get(p, p)
        if rp not in parts:
            continue
        P = parts[rp]
        # score activity per frame
        sc = np.zeros(F, bool)
        for a, b in P['notes']:
            sc |= (posn >= a) & (posn < b)
        au = np.zeros(F, bool)
        for t in ts:
            if t in act:
                au |= act[t]
        valid = ~np.isnan(posn)
        out['check'][p] = {'scoreActiveHeard': round(float(au[sc & valid].mean()), 2) if (sc & valid).any() else None,
                           'heardInScore': round(float(sc[au & valid].mean()), 2) if (au & valid).any() else None}
        # steady segments
        trim = cfg.get('trim', 0.25)
        for a, b in P['notes']:
            cuts = sorted(set([a, b] + [x for x, _ in P['dyn'] if a < x < b]))
            for i in range(len(cuts) - 1):
                s0, s1 = cuts[i], cuts[i + 1]
                m = marking_at(P, s0 + 1e-4)
                if m is None:
                    continue
                tr = min(trim, 0.25 * (s1 - s0))
                sel = valid & (posn >= s0 + tr) & (posn < s1 - tr)
                for c0, c1 in P['change']:
                    sel &= ~((posn >= c0 - trim) & (posn < c1 + trim))
                for c0, c1 in technique_spans(P):
                    sel &= ~((posn >= c0 - 0.05) & (posn < c1))
                for x in P['accent']:
                    sel &= ~((posn >= x - cfg.get('accentBefore', 0.05)) & (posn < x + cfg.get('accentAfter', 0.25)))
                lv, nfr = [], []
                for t in ts:
                    if t not in act:
                        continue
                    s = sel & act[t]
                    if s.sum() < cfg.get('minFrames', 10):
                        continue
                    L = seg_level(np.pad(env[t], (0, F - len(env[t])), constant_values=-200), nfl[t], s)
                    if L is not None:
                        lv.append(L); nfr.append(int(s.sum()))
                if lv:
                    L = 10 * np.log10(np.mean(10 ** (np.array(lv) / 10)))
                    unc = any(s0 - 0.5 <= u[0] <= s1 + 0.5 for u in P['uncertain'] if u[0] is not None)
                    out['segments'].append({'part': p, 'from': round(s0 + 1, 2), 'to': round(s1 + 1, 2), 'marking': m,
                                            'frames': int(np.mean(nfr)), 'tracks': len(lv), 'levelDbFs': round(L, 2),
                                            'trackSpreadDb': round(float(np.ptp(lv)), 2), 'uncertainReading': unc})
    return out


if __name__ == '__main__':
    cfg = json.load(open(sys.argv[1]))
    if len(sys.argv) > 2:
        STAT = sys.argv[2]
        cfg['out'] = cfg['out'].replace('.json', '_' + STAT + '.json')
    res = run(cfg)
    json.dump(res, open(cfg['out'], 'w'), indent=1)
    for p, c in res['check'].items():
        print('check', p, c)
    for s in res['segments']:
        print(s)
