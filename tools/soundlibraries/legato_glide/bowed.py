# bowed.py <bowed-timing work dir> [out.json]: when SSO's BOWED legato transition is heard, for the five string
# sections' Performance patches ([slurs] plainAuto 1 sends a new bow stroke, a note touching the note before outside a
# slur, as the bowed transition: velocity 93, Violas' legatoVelocity 100). Reads the measure_patch.py work folders
# <work>/{vn1,vn2,va,vc,cb} (plan.json, res/*.json), planned 2026-10-27 as
#   measure_patch.py plan --patch "<section> - Performance" --what transition --pitches <three registers, gen.py's>
#     --intervals=-12,-5,-2,-1,1,2,5,12 --before attack,transition --before-len 150,600 --ioi 600
#     --attack-velocity 5 --transition-velocity 93 (Violas 100) --overlap 0 --cc1 64 --takes 2
# (A after silence on the smooth attack 5, as plainAuto plays a note after a rest; "transition": Z smooth, A by bowed
# transition, B by bowed transition, a detache run; the note before ends at B's note-on, as plain notes touch) and
# rendered by kthost (build 485) + Kontakt 8 + SSO on the Windows test VM with MuseScore's setups.
# Measured on B (measure_patch.py's rules): f0 leave / mid / arrive (0.1 / 0.5 / 0.9 of the way, YIN), heardRef = leave
# + 23.5 ms (the owner's perceptual reference, 2026-10-21; Polfreman 2013), ms after B's note-on. A reading counts when
# the f0 was still at A's pitch at B's note-on (leave > -5 ms: else A had not sounded yet, the window's start) and B
# arrived within its own 600 ms (arrive < 550; a later "arrival" at the window's end is a misread, mostly octaves:
# B's 2nd harmonic is A). Per patch and note-before length: medians, quartiles, n, over intervals, registers, both
# figures and takes; per interval too; and without the octaves (+-12: YIN confuses B with its 2nd harmonic, +12
# never read, -12 reads early), the proposal's figure. proposal: median heardRef - the patch's plainShift
# (sso_plain_performance_shift.json), how much earlier than today a plainAuto bowed note would be heard on time.
import glob, json, os, statistics as st, sys

W = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'sso_legato_glide_bowed.json')
SHIFT = json.load(open(os.path.join(HERE, '..', 'sso_plain_performance_shift.json')))


def q(xs):
    if not xs:
        return None
    s = sorted(xs)
    qs = st.quantiles(s, n=4) if len(s) > 1 else [s[0]] * 3
    return dict(median=round(st.median(s), 1), q1=round(qs[0], 1), q3=round(qs[2], 1), n=len(s))


out = {}
for sec in ('vn1', 'vn2', 'va', 'vc', 'cb'):
    plan = json.load(open(f'{W}/{sec}/plan.json'))
    cells = {c['id']: c for c in plan['cells']}
    rows = []
    for f in sorted(glob.glob(f'{W}/{sec}/res/*.json')):
        for r in json.load(open(f))['rows']:
            c, m = cells[r['id']], r['m']
            rows.append([c['beforeLenMs'], c['before'], c['interval'], c['pitch'], r['take'],
                         m.get('leave'), m.get('mid'), m.get('arrive')])
    ok = [r for r in rows if r[5] is not None and r[7] is not None and r[5] > -5 and r[7] < 550]
    shift = SHIFT[plan['patch']]['shiftMs']
    by = {}
    for L in sorted({r[0] for r in rows}):
        g = [r for r in ok if r[0] == L]
        hr = [r[5] + 23.5 for r in g]
        by[str(L)] = dict(leave=q([r[5] for r in g]), mid=q([r[6] for r in g]), arrive=q([r[7] for r in g]),
                          heardRef=q(hr), total=len([r for r in rows if r[0] == L]),
                          proposalMs=round(st.median(hr) - shift, 1) if hr else None,
                          byInterval={str(i): q([r[5] + 23.5 for r in g if r[2] == i])
                                      for i in sorted({r[2] for r in rows})},
                          heardRefNoOctaves=q([r[5] + 23.5 for r in g if abs(r[2]) != 12]),
                          proposalNoOctavesMs=round(st.median([r[5] + 23.5 for r in g if abs(r[2]) != 12]) - shift, 1),
                          byBefore={b: q([r[5] + 23.5 for r in g if r[1] == b]) for b in ('attack', 'transition')})
    hr = [r[5] + 23.5 for r in ok]
    hn = [r[5] + 23.5 for r in ok if abs(r[2]) != 12]
    out[plan['patch']] = dict(heardRefNoOctaves=q(hn), proposalNoOctavesMs=round(st.median(hn) - shift, 1),
                              transitionVelocity=plan['cells'][0]['transitionVelocity'], plainShiftMs=shift,
                              byBeforeLen=by, heardRef=q(hr),
                              proposalMs=round(st.median(hr) - shift, 1) if hr else None,
                              rows=rows)
out['_about'] = ('SSO string-section Performance BOWED legato transitions (velocity 93, Violas 100: [slurs] plainAuto 1\'s '
                 'new bow stroke), CC1 64, the note before ending at B\'s note-on, IOI before 150 / 600 ms, intervals '
                 '-12 -5 -2 -1 1 2 5 12, three registers, after a smooth attack or a bowed transition, 2 takes; kthost '
                 '(build 485) + Kontakt 8 + SSO on the Windows test VM, 2026-10-27. ms after B\'s note-on: f0 leave / mid '
                 '/ arrive (YIN, measure_patch.py), heardRef = leave + 23.5 ms; valid readings only (leave > -5, arrive '
                 '< 550); *NoOctaves: without the +-12 rows (YIN octave confusion). proposalMs = median heardRef - plainShift (sso_plain_performance_shift.json). rows: [beforeLenMs, '
                 'before, interval, pitch, take, leave, mid, arrive]. Producer: legato_glide/bowed.py')
json.dump(out, open(OUT, 'w'), indent=0, sort_keys=True)
for k, v in out.items():
    if k.startswith('_'):
        continue
    print(k, 'shift', v['plainShiftMs'], 'all', v['heardRef'], 'proposal', v['proposalMs'])
    for L, b in v['byBeforeLen'].items():
        print('  L', L, 'leave', b['leave'], 'arrive', b['arrive'], 'heardRef', b['heardRef'], 'of', b['total'],
              'proposal', b['proposalMs'])
        print('    by interval', {i: (x['median'], x['n']) if x else None for i, x in b['byInterval'].items()})
        print('    by before', {i: (x['median'], x['n']) if x else None for i, x in b['byBefore'].items()})
