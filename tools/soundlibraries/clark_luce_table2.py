#!/usr/bin/env python3
"""Clark & Luce 1965, Table II: each player's scales as parabolas, level = a + b n + c n^2 (dB SPL at 10 m, n the note
number counted from the lowest note played); used to check Fig. 2 and to replace the French horn's Fig. 2 levels.

    python3 -I clark_luce_table2.py <clark_luce_levels.json> <out clark_luce_table2.json>

Fig. 2 (clark_luce_levels.json `levels`) is the mean over "all notes of all scales played at a particular dynamic
marking whether with or without vibrato and whether by one or the other of the two performers". Check: the mean in dB of
Table II's parabolas over n = 0 .. N (N the one unknown: how many notes each player played) reproduces Fig. 2 for all
twelve other instruments within 0.05 .. 1.05 dB rms (ten within 0.5; N 33 .. 45), but not the French horn (best 3.3 dB
rms, pp / mf / ff -1.7 / +4.3 / -3.4 dB at N 41): its two players differ by 14 .. 22 dB (Table III: the worst player
agreement of the study, 9.7 dB), and
player 2's parabolas start at 15 (pp) / 34 (mf) dB SPL at 10 m, 20 .. 40 dB under player 1 and under every other wind
instrument's lowest notes. Fig. 2's horn follows player 2 (mf 57.8 / ff 71.0 against player 2's 58.1 / 70.8, power mean
over n = 0 .. 30).
The horn's balance therefore takes player 1 (vibrato pp and mf: the scales he played; he has no ff), by the same mean
over n = 0 .. N with N the median of the other instruments' fitted N, the spread over their N its uncertainty. Player 1
against the trumpets (Fig. 2) at pp, as sound power with TU Berlin's DI_max: -6.2 dB; TU Berlin's single notes -0.3,
Meyer 2009's in-play pp -3 (Fig. 2's horn: -16.1). His pp -> mf, 10.7 dB, is three of the five equal steps pp -> ff of
[levels] realRange 2, i.e. a pp -> ff of 17.8 dB, Table I's horn span, which stays.
playerWeight (since 2026-10-15, iteration 3): Fig. 2 weighs every scale alike, so a player who played both styles
counts twice; per instrument and marking, the mean of the two players' means less the mean of the scales (Table II, the
same N), which section_balance_cl.py adds to Fig. 2: the tuba +1.4 dB at mp (its quieter player 2 played two scales,
player 1 one; TU Berlin, Meyer and Spheres all put the tuba higher than Fig. 2), the others -0.7 .. +0.5.
Numbers only (the coefficients as printed, J. Audio Eng. Soc. 13(2) 151-157, pp. 154-155); read from the scan,
review page https://claude.ai/artifact/5W2xDamjK9vLRcJ4S6cFC2 (the owner to check).
"""
import json
import math
import statistics
import sys

# instrument: [(style, marking, player, a, b, c)], as printed (vibrato / white; a blank row: the scale wasn't played)
TABLE2 = {
    'Violin': [('v', 'pp', 1, 47.6, -0.170, 0.0035), ('v', 'mf', 1, 54.1, -0.270, 0.0050), ('v', 'ff', 1, 56.9, -0.150, 0.0019),
               ('w', 'pp', 2, 42.0, 0.496, -0.0122), ('w', 'mf', 2, 50.9, -0.148, 0.0045), ('w', 'ff', 2, 65.1, -0.041, 0.0005)],
    'Viola': [('v', 'pp', 2, 40.1, -0.077, 0.0031), ('v', 'mf', 1, 48.0, 0.084, 0.0006), ('v', 'mf', 2, 44.4, 0.140, -0.0011),
              ('v', 'ff', 1, 56.1, 0.106, -0.0023), ('v', 'ff', 2, 56.6, 0.050, -0.0008),
              ('w', 'pp', 1, 44.4, -0.151, 0.0042), ('w', 'mf', 1, 50.0, 0.183, -0.0050), ('w', 'ff', 1, 61.3, -0.284, 0.0064)],
    'Cello': [('v', 'pp', 1, 51.4, -0.179, 0.0014), ('v', 'pp', 2, 56.8, -0.193, -0.0001), ('v', 'mf', 1, 60.3, -0.138, 0.0003),
              ('v', 'mf', 2, 64.3, -0.413, 0.0042), ('v', 'ff', 1, 60.5, -0.242, 0.0038), ('v', 'ff', 2, 69.3, -0.306, -0.0002),
              ('w', 'pp', 2, 48.4, -0.100, -0.0000), ('w', 'mf', 2, 57.6, -0.032, -0.0020), ('w', 'ff', 2, 70.4, -0.547, 0.0060)],
    'Double bass': [('v', 'pp', 1, 54.4, -0.303, 0.0028), ('v', 'pp', 2, 50.8, 0.255, -0.0111), ('v', 'mf', 1, 63.7, -0.225, -0.0003),
                    ('v', 'mf', 2, 58.8, 0.070, -0.0110), ('v', 'ff', 1, 73.4, -0.508, 0.0027), ('v', 'ff', 2, 68.3, -0.339, -0.0015),
                    ('w', 'pp', 1, 54.0, -0.306, 0.0017), ('w', 'mf', 1, 62.6, -0.284, 0.0021), ('w', 'ff', 1, 71.6, -0.386, 0.0011)],
    'Flute': [('v', 'pp', 1, 31.9, 1.627, -0.0312), ('v', 'pp', 2, 22.9, 2.298, -0.0345), ('v', 'mf', 1, 43.6, 0.809, -0.0109),
              ('v', 'mf', 2, 37.9, 1.009, -0.0055), ('v', 'ff', 1, 50.3, 0.304, -0.0001), ('v', 'ff', 2, 45.9, 0.555, 0.0019),
              ('w', 'pp', 1, 35.8, 0.959, -0.0049), ('w', 'mf', 1, 41.9, 0.817, -0.0064), ('w', 'ff', 1, 46.9, 0.563, 0.0031)],
    'Clarinet': [('v', 'pp', 1, 50.1, 0.366, -0.0051), ('v', 'pp', 2, 45.6, -0.120, 0.0080), ('v', 'mf', 1, 54.9, 0.140, 0.0016),
                 ('v', 'mf', 2, 50.4, 0.052, 0.0035), ('v', 'ff', 1, 58.1, 0.019, 0.0031), ('v', 'ff', 2, 52.8, 0.022, 0.0058),
                 ('w', 'pp', 1, 47.8, 0.229, -0.0012), ('w', 'mf', 1, 53.9, 0.017, 0.0036), ('w', 'ff', 1, 60.5, -0.143, 0.0070)],
    'Oboe': [('v', 'pp', 1, 55.3, 0.182, -0.0040), ('v', 'pp', 2, 48.3, -0.376, 0.0197), ('v', 'mf', 1, 59.2, -0.026, 0.0029),
             ('v', 'mf', 2, 61.4, -0.442, 0.0136), ('v', 'ff', 1, 59.4, 0.101, -0.0007), ('v', 'ff', 2, 61.7, -0.267, 0.0105),
             ('w', 'pp', 1, 54.6, -0.217, 0.0084), ('w', 'pp', 2, 53.4, -0.010, 0.0047), ('w', 'mf', 1, 58.4, 0.013, 0.0034),
             ('w', 'mf', 2, 61.9, -0.364, 0.0120), ('w', 'ff', 1, 60.7, 0.082, 0.0006), ('w', 'ff', 2, 62.0, -0.176, 0.0072)],
    'English horn': [('v', 'pp', 1, 54.8, -0.608, 0.0171), ('v', 'pp', 2, 53.0, 0.158, -0.0047), ('v', 'mf', 2, 55.2, 0.113, -0.0032),
                     ('v', 'ff', 1, 53.1, 0.260, -0.0056), ('v', 'ff', 2, 55.8, 0.231, -0.0068),
                     ('w', 'pp', 1, 55.0, -0.911, 0.0288), ('w', 'pp', 2, 52.5, -0.150, 0.0038), ('w', 'mf', 1, 52.0, 0.181, -0.0027),
                     ('w', 'mf', 2, 55.1, 0.252, -0.0047), ('w', 'ff', 1, 53.7, 0.435, -0.0116), ('w', 'ff', 2, 57.7, 0.044, -0.0006)],
    'Bassoon': [('v', 'pp', 1, 54.8, -0.505, 0.0115), ('v', 'pp', 2, 54.5, -0.224, 0.0034), ('v', 'mf', 1, 55.1, -0.199, 0.0060),
                ('v', 'mf', 2, 55.0, 0.072, 0.0002), ('v', 'ff', 1, 59.1, -0.352, 0.0095), ('v', 'ff', 2, 60.7, 0.122, -0.0025),
                ('w', 'pp', 1, 54.9, -0.513, 0.0106), ('w', 'pp', 2, 51.5, 0.374, -0.0256), ('w', 'mf', 1, 55.2, -0.338, 0.0086),
                ('w', 'mf', 2, 52.5, 0.684, -0.0180), ('w', 'ff', 1, 59.7, -0.321, 0.0093), ('w', 'ff', 2, 59.3, 0.395, -0.0085)],
    'Trumpet': [('v', 'pp', 1, 63.5, 0.250, 0.0002), ('v', 'pp', 2, 61.5, 0.098, 0.0062), ('v', 'mf', 1, 68.6, 0.453, -0.0059),
                ('v', 'mf', 2, 67.3, 0.551, -0.0078), ('v', 'ff', 1, 71.3, 0.208, 0.0012), ('v', 'ff', 2, 71.1, 0.212, -0.0005),
                ('w', 'pp', 1, 65.0, 0.213, 0.0016), ('w', 'pp', 2, 58.1, 0.131, 0.0111), ('w', 'mf', 1, 68.1, 0.619, -0.0086),
                ('w', 'mf', 2, 68.2, 0.317, -0.0008), ('w', 'ff', 1, 72.8, 0.293, -0.0013), ('w', 'ff', 2, 70.5, 0.383, -0.0058)],
    'Trombone': [('v', 'pp', 1, 57.7, -1.026, 0.0401), ('v', 'pp', 2, 56.6, -0.047, 0.0051), ('v', 'mf', 1, 65.3, 0.275, -0.0012),
                 ('v', 'mf', 2, 63.4, -0.073, 0.0032), ('v', 'ff', 1, 67.6, 0.362, -0.0036), ('v', 'ff', 2, 69.0, 0.203, -0.0019),
                 ('w', 'pp', 2, 48.0, 0.119, 0.0097), ('w', 'mf', 2, 63.6, 0.121, 0.0015), ('w', 'ff', 2, 71.2, 0.118, 0.0015)],
    'Tuba': [('v', 'pp', 2, 60.4, 0.152, 0.0003), ('v', 'mf', 2, 65.2, 0.009, 0.0041), ('v', 'ff', 2, 70.1, 0.209, -0.0026),
             ('w', 'pp', 1, 68.0, -0.141, 0.0094), ('w', 'pp', 2, 58.3, 0.011, 0.0013), ('w', 'mf', 1, 73.7, 0.178, -0.0048),
             ('w', 'mf', 2, 61.3, 0.297, -0.0022), ('w', 'ff', 1, 77.6, 0.569, -0.0183), ('w', 'ff', 2, 69.9, 0.426, -0.0075)],
    'French horn': [('v', 'pp', 1, 55.5, 0.679, -0.0117), ('v', 'mf', 1, 66.7, 0.427, -0.0020),
                    ('v', 'pp', 2, 15.4, 1.438, -0.0034), ('v', 'mf', 2, 33.7, 0.861, 0.0039), ('v', 'ff', 2, 46.7, 1.076, -0.0063),
                    ('w', 'pp', 2, 26.9, 0.880, 0.0064), ('w', 'mf', 2, 38.1, 1.090, -0.0035), ('w', 'ff', 2, 46.1, 1.703, -0.0194)],
}
MARKS = ('pp', 'mf', 'ff')
NS = range(10, 49)


def mean(inst, mark, n, player=None):
    """the mean in dB over n = 0 .. N of the scales at this marking (one player's, or all)"""
    v = [a + b * k + c * k * k for _, m, p, a, b, c in TABLE2[inst] if m == mark and player in (None, p) for k in range(n + 1)]
    return sum(v) / len(v) if v else None


def fit(inst, fig2):
    """the N whose means reproduce Fig. 2 best, and the rms error"""
    best = None
    for n in NS:
        e = [mean(inst, m, n) - fig2[m] for m in MARKS]
        r = math.sqrt(sum(x * x for x in e) / 3)
        if best is None or r < best[1]:
            best = (n, r, e)
    return best


def main():
    fig2 = json.load(open(sys.argv[1], encoding='utf-8'))['levels']
    out = {'method': __doc__.strip().split('\n\n', 1)[1], 'fig2Check': {}}
    for inst in TABLE2:
        n, r, e = fit(inst, fig2[inst])
        out['fig2Check'][inst] = {'N': n, 'rms': round(r, 2), 'error': {m: round(x, 2) for m, x in zip(MARKS, e)}}
        print('%-13s N %2d  rms %.2f  (%s)' % (inst, n, r, ' '.join('%s %+.1f' % (m, x) for m, x in zip(MARKS, e))))
    # each player once: Fig. 2 weighs every scale alike, so a player with two styles counts twice (tuba: the quieter
    # player 2 two scales, player 1 one); playerWeight = the mean of the players' means - the mean of the scales, at N
    out['playerWeight'] = {}
    for inst, v in out['fig2Check'].items():
        if inst == 'French horn':
            continue
        n = v['N']
        delta = {}
        for m in MARKS:
            ps = [x for x in (mean(inst, m, n, 1), mean(inst, m, n, 2)) if x is not None]
            delta[m] = round(sum(ps) / len(ps) - mean(inst, m, n), 2)
        out['playerWeight'][inst] = delta
    others = sorted(v['N'] for k, v in out['fig2Check'].items() if k != 'French horn')
    n = int(statistics.median(others))
    lv = {m: mean('French horn', m, n, 1) for m in ('pp', 'mf')}
    spread = [mean('French horn', 'pp', k, 1) + 2 / 3 * (mean('French horn', 'mf', k, 1) - mean('French horn', 'pp', k, 1))
              for k in (others[0], others[-1])]
    out['French horn'] = {'player': 1, 'N': n, 'Nothers': others, 'pp': round(lv['pp'], 2), 'mf': round(lv['mf'], 2),
                          'mpRange': [round(min(spread), 2), round(max(spread), 2)],
                          'player2': {m: round(mean('French horn', m, n, 2), 2) for m in MARKS}}
    print('French horn, player 1, N %d: pp %.2f mf %.2f (mp %.2f .. %.2f over N %d .. %d); player 2: %s' % (
        n, lv['pp'], lv['mf'], min(spread), max(spread), others[0], others[-1], out['French horn']['player2']))
    with open(sys.argv[2], 'w', encoding='utf-8') as fh:
        json.dump(out, fh, indent=1)
        fh.write('\n')


if __name__ == '__main__':
    main()
