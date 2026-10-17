# The level rule ([levels] context, the owner, 2026-10-10: the same written dynamic sounds equally loud in every
# string part and technique, judged in the music): the gain in dB a slur on a section's Performance Legato needs over
# the patch at CC1 = the written dynamic's value, for the slur's dynamic, median note spacing (IOI) and median pitch.
#
# Since 2026-10-15 (re-measured with the slur settings of today's Recommended: transitions, smoothStart,
# transitionsEarly 1; the 2026-10-10 grid had none of them and judged the slurs against the staccatos around them):
#   gain(s, d, ioi, p) = held(s, d, p) - slurred(s, d, ioi, p)
# held: the section's All techniques Long at CC1 = d, four half notes around p (median); slurred: the Performance
# Legato at CC1 = d, no gain (the map's contextLevel zeroed for the renders), two touching slurs of four notes at the
# IOI (median of the eight); both as MuseScore 3.7 Recommended plays them (MuseScore3Evo --verify-playback
# --verify-wav, build 435 with the score's playbackSettings; grid_score.py, grid_ana.py), the takes A and B (reverse
# order) averaged. Metric P: ArticulationCheck::perceivedLoudnessDb (perceived.py), the measure of the calibration
# curves (the Long's level a velocity technique is calibrated to) and of the check that found the old grid out of date;
# L (loudest 50 ms RMS, the 2026-10-10 grid's) is in the JSON beside it.
# What both techniques of a section play alike cancels in held - slurred, so it was off in the renders and stacks on
# top in MuseScore: the section balance and real range (patch volumes, CC11 per note by the written CC1 and pitch),
# mic compensation; even levels (off) flattens inside a slur around the slur's median, which this gain sets.
# Writes the "rule" key of sso_performance_context.json (gen_spitfire_sso.py puts it into the map as
# <Articulation contextLevel>).
import json, os
import numpy as np
D = os.path.dirname(os.path.abspath(__file__))
J = os.path.join(D, '..', 'sso_performance_context.json')
j = json.load(open(J))
G = j['grid']; DS = G['dynamic']; IOIS = G['ioiMs']; SEC = list(j['sections'])
M = 'P'


def mean(c):
    return float(np.mean(list(c.values())))


rule = dict(metric=M, relation='gain = held - slurred',
            note='gain dB over the Performance Legato at CC1 = d for a slur of median IOI ioiMs and median pitch p: the '
                 "section's All techniques Long held at CC1 = d less the slurred figure at CC1 = d (metric P); key \"d|ioiMs|p\"",
            sections={})
noise = []
print(f'gain dB ({M}); rows d, columns IOI {IOIS} ms; low / mid / high register')
for s in SEC:
    m = j['sections'][s][M]; regs = G['pitches'][s]; cells = {}
    for d in DS:
        for p in regs:
            h = m['held'][f'{d}|{p}']
            noise.append(h['A'] - h['B'])
            for i in IOIS:
                sl = m['slur'][f'{d}|{i}|{p}']
                noise.append(sl['A'] - sl['B'])
                cells[f'{d}|{i}|{p}'] = round(mean(h) - mean(sl), 1)
    rule['sections'][s] = cells
    print(s)
    for d in DS:
        print(f'  d {d:3d}  ' + '   '.join(' '.join(f'{cells[f"{d}|{i}|{p}"]:5.1f}' for i in IOIS) for p in regs))
print(f'take A - B, every held and slurred figure: sd {np.std(noise):.2f}, max |{np.max(np.abs(noise)):.2f}| dB '
      f'(a cell is the mean of both: its noise about sd / 2 per figure)')
j['rule'] = rule

# [slurs] plainPerformance (2026-10-17, branch plain-levels): every note of the part on the plain Performance legato
# (velocity 64), measured on the same grid (grid_score.py today: "plain" of the JSON): per figure kind, slur (two touching
# slurs), stac (staccatos: a spiccato attack each), single (notes outside a slur, touching), gain = held - figure. The
# staccatos' D ([levels] calibrated 2: real players' staccato against the held note, technique_offsets.json) is not in
# the cells: the renderer adds the D of the technique the note would play otherwise (the Spiccato's heldOffset), as for
# a velocity technique. The map's contextLevelPlain / contextLevelStaccato / contextLevelSingle.
if 'plain' in j:
    pj = j['plain']; PG = pj['grid']
    prule = dict(metric=M, relation='gain = held - figure',
                 note='gain dB over the plain Performance legato at CC1 = d for a phrase of median IOI ioiMs and median pitch p '
                      '(a staccato or a note outside a slur: the note alone, its length as its IOI): the section\'s All techniques '
                      'Long held at CC1 = d less the figure at CC1 = d (metric P); per kind, key "d|ioiMs|p"', kinds={})
    noise = []
    for kind in ('slur', 'stac', 'single'):
        prule['kinds'][kind] = {}
        print(f'plain {kind}: gain dB ({M}); rows d, columns IOI {PG["ioiMs"]} ms; low / mid / high register')
        for s in SEC:
            m = pj['sections'][s][M]; regs = PG['pitches'][s]; cells = {}
            for d in PG['dynamic']:
                for p in regs:
                    h = m['held'][f'{d}|{p}']
                    if kind == 'slur':
                        noise.append(h['A'] - h['B'])
                    for i in PG['ioiMs']:
                        f = m[kind][f'{d}|{i}|{p}']
                        noise.append(f['A'] - f['B'])
                        cells[f'{d}|{i}|{p}'] = round(mean(h) - mean(f), 1)
            prule['kinds'][kind][s] = cells
            print(s)
            for d in PG['dynamic']:
                print(f'  d {d:3d}  ' + '   '.join(' '.join(f'{cells[f"{d}|{i}|{p}"]:5.1f}' for i in PG['ioiMs']) for p in regs))
    print(f'plain: take A - B, every figure: sd {np.std(noise):.2f}, max |{np.max(np.abs(noise)):.2f}| dB')
    pj['rule'] = prule
open(J, 'w').write(json.dumps(j, indent=1) + '\n')
