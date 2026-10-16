# fit.py <out.json> [sweep=]<index.json>:<heard rows.json>[,<heard rows.json> …] …: the map's slurOnset,
# slurOnsetLength, slurOnsetDynamic, bendGlideAttack and bendGlideSlur from heard.py's rows of the slur-rhythm grid
# (score.py) and pitch sweep (sweep.py, its arguments marked sweep=), every take (index and rows: take A, take B …)
# pooled, per section's Performance patch:
#   slurOnset: a slur's first note (the smooth attack after silence) heard -- 15 dB under the slur's own top -- this long
#     after its note-on (heard.py's 'first' rows, heardAfterOn). By pitch from the sweep (every semitone, one shape: an
#     eighth + two sixteenths at 110 bpm, mp; the median of each pitch's slurs, 2 per take): the onset is a property of
#     the key (SSO's sample zones: Violas 66-170 ms between neighbouring keys, repeats within 6 ms), so the grid's 3
#     pitches can't give it; median: the sweep's over every slur. Then the grid's rows less the sweep's onset at their
#     pitch (linear between the swept pitches, as the renderer reads it), the residuals' median by the first note's
#     written length (ms; 16th / eighth / quarter at 80 / 110 / 160 bpm: 93.8 … 750 ms) and by the dynamic (MS4's CC1:
#     pp 32, mp 64, f 96), each less that of the sweep's own (273 ms, CC1 64): slurOnsetLength / slurOnsetDynamic, the
#     change to add (an additive model; the residual at the sweep's own cell is kept as refResidual, a check);
#     byLength / byDynamic / byPitchIndex: the grid's plain medians, for reference;
#   bendGlideAttack: the transition out of that first note (heard.py's 'transition' rows with k = 1), by the first note's
#     PLAYED length (its note-on to the 2nd note-on, as sent): in bins of played length (BINS), keyed by the bin's median
#     length, the median arrival as heard (the first of 3 pYIN frames within 30 cents of the new pitch: the heard check's
#     rule, VERIFY.md › Heard check) and the median leave as the map's bendGlide is measured (glide.py's phi 0.1); a bin
#     with fewer than MIN_N heard arrivals is left out; arrive kept >= leave;
#   bendGlideSlur: the same for the transitions after a transition (k >= 2), by the note before's played length;
#   beside each bin glide.py's phi-0.9 arrival (the bendGlide definition), for comparison.
import json, sys, statistics as st
import numpy as np

BINS = [0, 175, 250, 325, 450, 600, 2000]
MIN_N = 8
ABOUT = ("In-context timing of the five string sections' Performance Legato as MuseScore plays slurs (2026-10-16, branch "
         "slur-rhythm, tools/soundlibraries/slur_rhythm/: score.py, events.py, run.cmd, heard.py, fit.py; their headers hold "
         "the rules). 243 slurs per section (first note 16th / eighth / quarter x 2-4 notes x 80 / 110 / 160 bpm x pp / mp / f "
         "x 3 pitches) x 2 takes (shuffles 16, 17), and a pitch sweep (sweep.py: every semitone of each section's range, an eighth + "
         "two sixteenths at 110 bpm, mp, 2 repeats x 2 takes, shuffles 21, 22), dumped by tst_liveequivalence dumpEvents (Recommended, even levels off), "
         "rendered per route with kthost + Kontakt 8 + SSO on the Windows test VM at CC11 127 (the patch's own level: the "
         "perceived envelope is not level-invariant). slurOnset: a slur's first note (smooth attack after silence) heard "
         "-15 dB under the slur's own top, less 30 ms, ms after its note-on: by pitch (the sweep; median: the sweep's), and the "
         "change by written length (lengthDms) and by dynamic (dynamicDms, MS4 CC1): the grid's medians of (heard - the "
         "sweep's onset at its pitch), less the sweep cell's (273 ms, CC1 64); byLength / byDynamic / byPitchIndex the grid's "
         "plain medians. glideAttack / "
         "glideSlur: the transition out of that first note / after a transition, by the note before's played length (ms), "
         "leave (glide.py phi 0.1) and arrive (heard: the first of 3 pYIN frames within 30 cents of the new pitch), medians "
         "per bin, n, IQR, phi9Arrive (glide.py's 9/10 crossing, the bendGlide definition) beside. rows: [kind, k, pitch, "
         "written ms, sent ms (- written), lenBefore, heardAfterOn, leave, glideArrive] (the grid's). gen_spitfire_sso.py writes "
         "slurOnset= (by pitch), slurOnsetLength=, slurOnsetDynamic=, "
         "bendGlideAttack=, bendGlideSlur=; the renderer reads them with [slurs] slurOnset / transitionModel.")
PN = {'Violins I': 'Violins 1', 'Violins II': 'Violins 2', 'Violas': 'Violas', 'Violoncellos': 'Celli', 'Contrabasses': 'Basses'}


def binned(rows):
    out = []
    for a, b in zip(BINS, BINS[1:]):
        h = [r for r in rows if a <= r['lenBefore'] < b and r.get('heardAfterOn') is not None]
        lv = [r['leave'] for r in rows if a <= r['lenBefore'] < b and r.get('leave') is not None]
        g9 = [r['glideArrive'] for r in rows if a <= r['lenBefore'] < b and r.get('glideArrive') is not None]
        if len(h) >= MIN_N and lv:
            arrive = st.median(r['heardAfterOn'] for r in h)
            leave = min(st.median(lv), arrive)
            out.append(dict(len=round(st.median(r['lenBefore'] for r in h)), leave=round(leave), arrive=round(arrive), n=len(h),
                            iqrArrive=[round(float(np.percentile([r['heardAfterOn'] for r in h], q)), 1) for q in (25, 75)],
                            phi9Arrive=round(st.median(g9), 1) if g9 else None, nPhi9=len(g9)))
    return out


def at(table, key):
    """linear between the keys, the nearest end beyond (SoundLib's keyedMsAt)"""
    ks = sorted(table)
    if key <= ks[0]:
        return table[ks[0]]
    if key >= ks[-1]:
        return table[ks[-1]]
    for a, b in zip(ks, ks[1:]):
        if a <= key <= b:
            return table[a] + (table[b] - table[a]) * (key - a) / (b - a)


def main():
    res = {}
    byPart = {}
    sweepPart = {}
    for arg in sys.argv[2:]:
        sweep = arg.startswith('sweep=')
        if sweep:
            arg = arg[len('sweep='):]
        ixf, files = arg.split(':', 1)
        idx = json.load(open(ixf))
        for f in files.split(','):
            rows = json.load(open(f))
            sec = PN[rows[0]['part']]
            sl = {s['notes'][0]['on']: s for s in idx['parts'][sec]}
            for r in rows:
                s = sl[min(sl, key=lambda t: abs(t - r['w']))] if r['kind'] == 'first' else None
                if s:
                    r.update(dyn=s['dyn'], pi=s['pi'], n=s['n'], bpm=s['bpm'], l1=s['l1'])
                r['take'] = ixf
            (sweepPart if sweep else byPart).setdefault(sec, []).extend(rows)
    for sec, rows in byPart.items():
        patch = sec + ' - Performance'
        first = [r for r in rows if r['kind'] == 'first' and r['heardAfterOn'] is not None]
        bylen = {}
        for r in first:
            bylen.setdefault(round(r['len']), []).append(r['heardAfterOn'])
        by = lambda key: {str(k): round(st.median(v), 1) for k, v in sorted(
            ((k, [r['heardAfterOn'] for r in first if r[key] == k]) for k in sorted({r[key] for r in first})))}
        # (the heard check's rule: a transition arrives before the next note-on on its route, else it never did; an
        # arrival found later, pYIN locking onto a later note of the same pitch, is left out. One route per part here)
        ons = {}
        for r in rows:
            ons.setdefault((r['take'], r.get('route', '')), []).append(r['w'] + r['sent'] / 1000)
        for v in ons.values():
            v.sort()
        late = 0
        for r in rows:
            if r['kind'] == 'transition' and r.get('heardAfterOn') is not None:
                t = r['w'] + r['sent'] / 1000
                nxt = [x for x in ons[(r['take'], r.get('route', ''))] if x > t + 1e-6]
                if nxt and r['heardAfterOn'] > (nxt[0] - t) * 1000:
                    r['heardAfterOn'] = None
                    late += 1
        # the sweep: the onset by pitch; the grid's residuals by length and dynamic
        sw = [r for r in sweepPart.get(sec, []) if r['kind'] == 'first' and r['heardAfterOn'] is not None]
        bp = {}
        for r in sw:
            bp.setdefault(r['pitch'], []).append(r['heardAfterOn'])
        byPitch = {p: round(st.median(v), 1) for p, v in sorted(bp.items())}
        lengthD = dynamicD = refRes = None
        if byPitch:
            for r in first:
                r['res'] = r['heardAfterOn'] - at(byPitch, r['pitch'])
            ref_len = min({round(r['len']) for r in first}, key=lambda x: abs(x - 272.7))
            med = lambda f: st.median(r['res'] for r in first if f(r))
            refL = med(lambda r: round(r['len']) == ref_len)
            refD = med(lambda r: r['dyn'] == 64)
            refRes = round(med(lambda r: round(r['len']) == ref_len and r['dyn'] == 64), 1)
            lengthD = {str(k): round(med(lambda r, k=k: round(r['len']) == k) - refL, 1) for k in sorted({round(r['len']) for r in first})}
            dynamicD = {str(k): round(med(lambda r, k=k: r['dyn'] == k) - refD, 1) for k in sorted({r['dyn'] for r in first})}
        swt = {}
        for kind, cond in (('afterAttack', lambda r: r['k'] == 1), ('afterTransition', lambda r: r['k'] >= 2)):
            v = [r for r in sweepPart.get(sec, []) if r['kind'] == 'transition' and cond(r) and r.get('heardAfterOn') is not None]
            if v:
                swt[kind] = dict(n=len(v), lenBefore=round(st.median(r['lenBefore'] for r in v)),
                                 arrive=round(st.median(r['heardAfterOn'] for r in v), 1),
                                 iqr=[round(float(np.percentile([r['heardAfterOn'] for r in v], q)), 1) for q in (25, 75)],
                                 byRegister={k: round(st.median(r['heardAfterOn'] for r in v if lo <= r['pitch'] < hi), 1)
                                             for k, lo, hi in (('low', 0, 60), ('mid', 60, 72), ('high', 72, 200))
                                             if any(lo <= r['pitch'] < hi for r in v)},
                                 # by the new note's pitch (out of the attack: up a tone; after a transition: up a semitone):
                                 # the median of its 2-4 slurs (not in the map: open, docs/PLAYBACK_SETTINGS.md › Slur rhythm)
                                 byPitch={str(pp): round(st.median(r['heardAfterOn'] for r in v if r['pitch'] == pp), 1)
                                          for pp in sorted({r['pitch'] for r in v})})
        tr1 = [r for r in rows if r['kind'] == 'transition' and r['k'] == 1]
        tr2 = [r for r in rows if r['kind'] == 'transition' and r['k'] >= 2]
        res[patch] = dict(
            slurOnset=dict(median=round(st.median(r['heardAfterOn'] for r in sw), 1) if sw else
                           round(st.median(r['heardAfterOn'] for r in first), 1),
                           byPitch={str(k): v for k, v in byPitch.items()}, nByPitch={str(k): len(v) for k, v in sorted(bp.items())},
                           lengthDms=lengthD, dynamicDms=dynamicD, refResidual=refRes,
                           gridMedian=round(st.median(r['heardAfterOn'] for r in first), 1),
                           byLength={str(k): round(st.median(v), 1) for k, v in sorted(bylen.items())},
                           nByLength={str(k): len(v) for k, v in sorted(bylen.items())},
                           iqr=[round(float(np.percentile([r['heardAfterOn'] for r in first], q)), 1) for q in (25, 75)],
                           byDynamic=by('dyn'), byPitchIndex=by('pi'), bySlurLength=by('n'), byTempo=by('bpm'),
                           sentMedian=round(st.median(r['sent'] for r in first), 1), n=len(first)),
            glideAttack=binned(tr1), glideSlur=binned(tr2), sweepTransitions=swt,
            transitions=dict(afterAttack=len(tr1), afterTransition=len(tr2),
                             foundAfterAttack=sum(1 for r in tr1 if r.get('glideArrive') is not None),
                             foundAfterTransition=sum(1 for r in tr2 if r.get('glideArrive') is not None), arrivedAfterNextOn=late),
            rows=[[r['kind'], r['k'], r['pitch'], round(r['len']), r['sent'], r.get('lenBefore'), r['heardAfterOn'],
                   r.get('leave'), r.get('glideArrive')] for r in rows])
        s = res[patch]
        print(patch, 'slurOnset', s['slurOnset']['median'], 'grid', s['slurOnset']['gridMedian'], s['slurOnset']['byLength'])
        print('   byPitch', byPitch)
        print('   lengthDms', lengthD, 'dynamicDms', dynamicD, 'refResidual', refRes)
        print('   sweep transitions', swt)
        print('   attack:    ', ' '.join('%d:%d:%d(n%d, phi9 %s)' % (g['len'], g['leave'], g['arrive'], g['n'], g['phi9Arrive']) for g in s['glideAttack']))
        print('   transition:', ' '.join('%d:%d:%d(n%d, phi9 %s)' % (g['len'], g['leave'], g['arrive'], g['n'], g['phi9Arrive']) for g in s['glideSlur']))
        # take to take: each take's slurOnset median and attack arrivals
        for tk in sorted({r['take'] for r in rows}):
            f1 = [r['heardAfterOn'] for r in first if r['take'] == tk]
            print('   take', tk, 'slurOnset median %.1f (n%d)' % (st.median(f1), len(f1)))
    json.dump(dict(_about=ABOUT, sections=res), open(sys.argv[1], "w"), indent=0)


if __name__ == '__main__':
    main()
