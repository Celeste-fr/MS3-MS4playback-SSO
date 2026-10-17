# ablana.py <work> <bar0> <bar1> [out.json]: abl.py's mixes measured over bars bar0..bar1 by the heard check's analysis
# (heard_check.analyse on each <work>/v/<variant>_p<pass>/: ARRIVAL, RHYTHM, LEVEL, UNHEARD), plus per note:
#   cents   the f0 (YIN, legato_glide/f0.py) over the note's last 60 % up to the next note-on, median, against the written
#           pitch (quarter tones included); |cents| median over the notes, and the share within 30 cents;
#   timing  |heard - written| median (ms; the heard check's heard time);
#   dip     at each join (a note starting within 40 ms of the end of the one before, written): the perceived envelope's
#           lowest point between the two heard onsets, under the lower of the two notes' loudest (dB; negative: a dip);
#   loud    the passage's level (perceived envelope, median over the notes' loudest, dB) for the loudness-matched copies.
# Prints a table, variant x metric (both passes: the mean, and the two values for the main ones).
import json, os, sys
import numpy as np, soundfile as sf
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'playbackverify'))
sys.path.insert(0, os.path.join(HERE, '..', 'legato_glide'))
import heard_check as hc
from loudness import perceived_envelope
from f0 import yin_track, hz

W, B0, B1 = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])


def is_stacc(r):
    t = (r.get('techniques') or '') + ',' + (r.get('articulation') or '')
    return any(w in t.lower() for w in ('staccat', 'spiccato'))


OUT = sys.argv[4] if len(sys.argv) > 4 else f'{W}/ablana.json'
res = {}
for vd in sorted(os.listdir(f'{W}/v')):
    dump, wavs = f'{W}/v/{vd}/dump', f'{W}/v/{vd}/wav'
    a = hc.analyse(dump, wavs)
    notes = [n for n in a['notes'] if B0 <= n['bar'] <= B1]
    fs = [f for f in a['findings'] if B0 <= f['bar'] <= B1]
    cnt = lambda k, s=None: sum(1 for f in fs if f['kind'] == k and (s is None or f['severity'] == s))
    grp = [g for g in a['groups'] if g['maxDeviation'] is not None]
    x, sr = sf.read(f'{wavs}/mix.wav', always_2d=True)
    mono = x.mean(1)
    env, first, hop = perceived_envelope(x, sr)
    rows = hc.read_rows(dump)
    lead = hc.read_lead_ms(dump)
    rows = sorted([r for r in rows if B0 <= r['bar'] <= B1], key=lambda r: r['written'])
    allrows = sorted(hc.read_rows(dump), key=lambda r: r['written'])
    on = lambda r: (r['written'] + hc.OFFSET_S) * 1000 + lead + r['sentMs']
    cents, joins = [], []
    heard = {round(n['written'], 4): n for n in a['notes']}
    for r in rows:
        nxt = [q for q in allrows if q['written'] > r['written'] + 1e-4]
        t0 = on(r) / 1000
        t1 = min(on(nxt[0]) / 1000 if nxt else t0 + 0.3, t0 + (r['writtenEnd'] - r['written']) + 0.15)
        t0 = t0 + 0.4 * (t1 - t0)
        tp = r['pitch'] + r['cents'] / 100
        t, f = yin_track(mono, sr, t0, t1, hz(tp - 3), hz(tp + 3), step=0.0025, win=0.030)
        c = 1200 * np.log2(f / hz(tp))
        c = c[~np.isnan(c)]
        if len(c) >= 4:
            cents.append(float(np.median(c)))
        # a join: written start within 40 ms of the note before's written end
        prev = [q for q in allrows if q['written'] < r['written'] - 1e-4]
        if prev and abs(prev[-1]['writtenEnd'] - r['written']) <= 0.040:
            p = prev[-1]
            hp, hn = heard.get(round(p['written'], 4)), heard.get(round(r['written'], 4))
            if hp and hn and hp['heardMs'] is not None and hn['heardMs'] is not None and hp['levelDb'] is not None and hn['levelDb'] is not None:
                ta = (p['written'] + hc.OFFSET_S) * 1000 + lead + hp['heardMs']
                tb = (r['written'] + hc.OFFSET_S) * 1000 + lead + hn['heardMs']
                i0, i1 = int((ta - first) / hop), int((tb - first) / hop)
                if i1 > i0 + 1:
                    joins.append(float(env[i0:i1 + 1].min() - min(hp['levelDb'], hn['levelDb'])))
    # relative timing: each note's heard - written after the passage's median; staccatos and the rest (slurred notes,
    # longs) apart; the attacks' and transitions' own medians (absolute: the latency a constant shift would take out)
    rowsw = {(round(r['written'], 4), r['pitch']): r for r in rows}
    st, sl, att, tra = [], [], [], []
    for n in notes:
        r = rowsw.get((round(n['written'], 4), n['pitch']))
        if n['heardMs'] is None or r is None:
            continue
        (st if is_stacc(r) else sl).append(n['heardMs'])
        (tra if n['transition'] else att).append(n['heardMs'])
    allh = st + sl
    med = float(np.median(allh)) if allh else 0.0

    def spread(v):
        if not v:
            return None
        d = np.array(v) - med
        return dict(n=len(v), p10=round(float(np.percentile(d, 10)), 1), p90=round(float(np.percentile(d, 90)), 1),
                    maxAbs=round(float(np.abs(d).max()), 1), median=round(float(np.median(d)), 1))
    rel = dict(passageMedianMs=round(med, 1), staccatos=spread(st), slurred=spread(sl),
               staccMinusSlurMs=round(float(np.median(st) - np.median(sl)), 1) if st and sl else None,
               attackMedianMs=round(float(np.median(att)), 1) if att else None,
               attackP10P90=[round(float(np.percentile(att, q)), 1) for q in (10, 90)] if att else None,
               transitionMedianMs=round(float(np.median(tra)), 1) if tra else None,
               transitionP10P90=[round(float(np.percentile(tra, q)), 1) for q in (10, 90)] if tra else None)
    lv = [n['levelDb'] for n in notes if n['levelDb'] is not None]
    tm = [abs(n['heardMs']) for n in notes if n['heardMs'] is not None]
    res[vd] = dict(arrivalFail=cnt('ARRIVAL', 'FAIL'), arrivalWarn=cnt('ARRIVAL', 'WARN'),
                   rhythmFail=cnt('RHYTHM', 'FAIL'), rhythmWarn=cnt('RHYTHM', 'WARN'), level=cnt('LEVEL'), unheard=cnt('UNHEARD'),
                   centsAbs=round(float(np.median(np.abs(cents))), 1) if cents else None,
                   within30=round(float(np.mean(np.abs(cents) <= 30)), 2) if cents else None, nCents=len(cents),
                   timingMs=round(float(np.median(tm)), 1) if tm else None, nTimed=len(tm), n=len(notes),
                   dipDb=round(float(np.median(joins)), 2) if joins else None,
                   dipWorstDb=round(float(min(joins)), 2) if joins else None, nJoins=len(joins),
                   loudDb=round(float(np.median(lv)), 2) if lv else None,
                   levelSpreadDb=round(float(np.percentile(lv, 90) - np.percentile(lv, 10)), 2) if lv else None,
                   relative=rel,
                   relP10P90=round(float(np.percentile(np.array(allh) - med, 90) - np.percentile(np.array(allh) - med, 10)), 1) if allh else None,
                   staccMinusSlurMs=rel['staccMinusSlurMs'],
                   findings=[dict(kind=f['kind'], severity=f['severity'], bar=f['bar'], beat=f['beat'], value=f['value'])
                             for f in fs])
json.dump(res, open(OUT, 'w'), indent=1)
keys = ['arrivalFail', 'arrivalWarn', 'rhythmFail', 'rhythmWarn', 'level', 'unheard', 'centsAbs', 'within30', 'timingMs',
        'relP10P90', 'staccMinusSlurMs', 'dipDb', 'dipWorstDb', 'levelSpreadDb']
print('%-12s' % 'variant' + ''.join('%9s' % k[:9] for k in keys))
for vd, v in res.items():
    print('%-12s' % vd + ''.join('%9s' % ('-' if v[k] is None else v[k]) for k in keys))
