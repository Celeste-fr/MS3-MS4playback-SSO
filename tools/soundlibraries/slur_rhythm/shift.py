# shift.py <sso_slur_rhythm.json> <out sso_slur_shift.json> [verify.json]: the map's slurShift (2026-10-16, branch slur-shift),
# one amount per string section's Performance patch by which every note of a slur is sent later ([slurs] slurShift), from the
# slur-rhythm grid's renders as integrate-1012's Recommended plays it (A: no slurOnset, no transitionModel; the rows of
# sso_slur_rhythm.json, the grid's 243 slurs x 2 takes, shuffles 16 / 17 of score.py, rendered by kthost + Kontakt 8 + SSO on
# the Windows test VM at CC11 127).
# A note's error e = heard - written (sent ms + heardAfterOn: a slur's first note 15 dB under the slur's own top less 30 ms,
# a transition at 3 pYIN frames within 30 cents; the heard check's rules). Shifting a whole slur by D adds D to each of its
# notes' errors and leaves the differences between them (SSO's timing inside the slur) as they were (checked on the VM:
# verify.json, below). Criterion: each slur's worst note |e + D| is smallest at D_s = -(max e + min e) / 2 (its minimax
# centre); D = the median of D_s over the section's slurs (robust: a misread note moves one slur's centre, not the median;
# the mean of the worst notes, minimised directly, lands within 1.5 ms of it in every section). Readings |e| > OUTLIER_MS
# are taken for misreadings (pYIN octave errors, a lost track: Basses up to 484 ms) and left out; slurs with fewer than 2
# notes read are left out. The ARRIVAL limit (every note of a slur within 50 ms) can't be met by any D in half or more of
# the slurs (their notes spread more than 100 ms: SSO's transitions vary by key and interval), so it is reported, not the
# criterion. D is used only when its bootstrap 90 % interval lies above 0 (the slur later, as asked): a fit around or below 0
# means the section's slurs are centred already (moving them earlier gains under 1 ms of the median slur's worst note and
# would push the first note against the note before's keepMs); used = round(D) then, else 0.
# By dynamic, tempo, the first note's length and the notes per slur: each group's own D lowers the mean worst note by 0-4.7 ms, within the
# spread of that note (IQR ~60 ms): not used; listed (byGroup).
import json, os, sys
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_argv, sys.argv = sys.argv, sys.argv[:1]     # (score.py reads its seed from the command line when imported)
import score as S  # noqa: E402
sys.argv = _argv

OUTLIER_MS = 250.0
DS = np.arange(-100, 200.5, 0.5)
SEEDS = (16, 17)


def grid_slurs(rows_by_section):
    """the grid's slurs per section: their cell (from score.py's order for each take's seed) and notes' errors"""
    meta = {s: [] for s in S.SEC}
    for seed in SEEDS:
        S.SEED = seed
        _, idx, _ = S.build()
        for s in S.SEC:
            meta[s] += idx[s]
    out = {}
    for patch, rows in rows_by_section.items():
        sec = patch.replace(' - Performance', '')
        slurs, cur = [], None
        for r in rows:
            if r[1] == 0:
                cur = []
                slurs.append(cur)
            cur.append(r)
        assert len(slurs) == len(meta[sec]), (patch, len(slurs))
        o = []
        for t, (m, sl) in enumerate(zip(meta[sec], slurs)):
            assert len(sl) == m['n'] and [r[2] for r in sl] == [n['pitch'] for n in m['notes']], (patch, t)
            o.append(dict(take=0 if t < len(slurs) // 2 else 1, l1=m['l1'], n=m['n'], bpm=m['bpm'],
                          dyn=m['dyn'], e=[(r[4] + r[6]) if r[6] is not None else None for r in sl]))
        out[patch] = o
    return out


def usable(slurs):
    x = [np.array([v for v in s['e'] if v is not None and abs(v) <= OUTLIER_MS]) for s in slurs]
    return [v for v in x if len(v) >= 2]


def fit(slurs):
    sl = usable(slurs)
    centres = np.array([-(x.max() + x.min()) / 2 for x in sl])
    worst = np.array([[np.max(np.abs(x + D)) for x in sl] for D in DS])     # D x slur
    allv = np.concatenate(sl)
    rng = np.random.default_rng(1)
    boot = [np.median(rng.choice(centres, len(centres))) for _ in range(1000)]
    D = float(np.median(centres))
    return dict(slurs=len(sl), notes=int(len(allv)), fitMs=round(D, 1), ci90=[round(float(np.percentile(boot, q)), 1) for q in (5, 95)],
                minimaxMeanMs=float(DS[np.argmin(worst.mean(axis=1))]), rmsMs=round(float(-allv.mean()), 1),
                feasible50=round(float(np.mean([x.max() - x.min() <= 100 for x in sl])), 3)), sl, worst


def at(sl, D):
    w = np.array([np.max(np.abs(x + D)) for x in sl])
    allv = np.concatenate(sl) + D
    return dict(medianWorstMs=round(float(np.median(w)), 1), meanWorstMs=round(float(w.mean()), 1),
                notesWithin50=round(float((np.abs(allv) <= 50).mean()), 3), slursWithin50=round(float((w <= 50).mean()), 3))


def main():
    src = json.load(open(sys.argv[1], encoding='utf-8'))['sections']
    G = grid_slurs({p: v['rows'] for p, v in src.items()})
    res = {}
    for patch, slurs in G.items():
        r, sl, worst = fit(slurs)
        used = float(round(r['fitMs'])) if r['ci90'][0] > 0 else 0.0
        r['usedMs'] = used
        r['atZero'] = at(sl, 0.0)
        r['atUsed'] = at(sl, used)
        pos = {}
        for k in range(4):
            v = [s['e'][k] for s in slurs if len(s['e']) > k and s['e'][k] is not None and abs(s['e'][k]) <= OUTLIER_MS]
            if v:
                pos['k%d' % k] = dict(n=len(v), medianMs=round(float(np.median(v)), 1), withShiftMs=round(float(np.median(v)) + used, 1))
        r['byPosition'] = pos
        r['byTake'] = [fit([s for s in slurs if s['take'] == t])[0]['fitMs'] for t in (0, 1)]
        groups = {}
        for key in ('bpm', 'dyn', 'l1', 'n'):
            vals = sorted(set(s[key] for s in slurs), key=str)
            fits, tot, cnt = {}, 0.0, 0
            for v in vals:
                g, gsl, gw = fit([s for s in slurs if s[key] == v])
                fits[str(v)] = g['fitMs']
                tot += float(np.min(gw.mean(axis=1))) * len(gsl)
                cnt += len(gsl)
            groups[key] = dict(fitMs=fits, meanWorstSplitMs=round(tot / cnt, 1), meanWorstOneMs=round(float(np.min(worst.mean(axis=1))), 1))
        r['byGroup'] = groups
        res[patch] = r
        print('%-24s fit %6.1f (90%% %s, takes %s) used %4.0f  median worst %5.1f -> %5.1f  notes<=50 %.2f -> %.2f  slurs<=50 %.2f -> %.2f'
              % (patch, r['fitMs'], r['ci90'], r['byTake'], used, r['atZero']['medianWorstMs'], r['atUsed']['medianWorstMs'],
                 r['atZero']['notesWithin50'], r['atUsed']['notesWithin50'], r['atZero']['slursWithin50'], r['atUsed']['slursWithin50']))
    out = dict(_about=(
        "The map's slurShift (2026-10-16, branch slur-shift; tools/soundlibraries/slur_rhythm/shift.py, its header holds the "
        "rules): per string section's Performance patch, ms every note of a Performance slur is sent later with [slurs] "
        "slurShift. From sso_slur_rhythm.json's grid rows (integrate-1012 Recommended, the A renders: kthost + Kontakt 8 + SSO "
        "on the Windows test VM, CC11 127, 243 slurs x 2 takes per section): e = heard - written per note; fitMs the median "
        "over slurs of each slur's minimax centre -(max e + min e) / 2, |e| > 250 ms left out; ci90 a bootstrap of that "
        "median; minimaxMeanMs / rmsMs the other criteria; usedMs = round(fitMs) where ci90 lies above 0, else 0 (later only); atZero / atUsed the "
        "slurs' worst notes and the shares within 50 ms (ARRIVAL) without and with it; byPosition the median error of each "
        "slur position (k0 the first note); byGroup each group's own fit and the mean worst note with one D against a D per "
        "group. verify: the VM check that a whole-slur shift keeps the heard timing inside the slur."),
        sections=res)
    if len(sys.argv) > 3:
        out['verify'] = json.load(open(sys.argv[3], encoding='utf-8'))
    json.dump(out, open(sys.argv[2], 'w', encoding='utf-8'), indent=1)


if __name__ == '__main__':
    main()
