# score.py <out dir>: the slur-rhythm grid (2026-10-16, branch slur-rhythm), measured as MuseScore plays it: a score
# from the repository's String Orchestra template (no owner material) whose five string parts play the same slurs at
# once, each at its own grid pitches, every slur after silence (a smooth attack, [slurs] smoothStart) and followed by
# silence (the rest of its measure and a whole measure's rest):
#   first note L1 in 16th, eighth, quarter x notes in the slur n in 2, 3, 4 x tempo 80, 110, 160 bpm x dynamic pp, mp,
#   f x 3 pitches of the section (performance_context/grid_score.py's: lowest open string + 2, the map's calibration
#   pitch, + 12) = 243 slurs per part. The notes after the first: a 16th after a 16th or an eighth, an eighth after a
#   quarter (a slur's usual shapes: Whence's eighth + two 16ths, a 16th run, a quarter + eighths). Intervals by shape
#   (semitones from the first note): up [0 2 4 5], turn [0 -2 -1 1], wave [0 1 3 1], the shape cycling with (L1, n).
#   Order: a fixed shuffle (random.Random(seed), take A 16, take B 17) so that neighbouring slurs differ (round robins,
#   drift). usage: score.py <out dir> [seed]
# Writes <out>/grid.mscx (its playbackSettings metaTag: Recommended, Playback::presets(), written out, with even levels
# off: no even values are measured for the grid, and they only flatten levels inside a slur) and <out>/index.json
# (every slur of every part in time order: L1, n, bpm, dynamic, pitch index, its notes' pitches and written seconds).
import json, os, random, sys
from fractions import Fraction as F

D = os.path.dirname(os.path.abspath(__file__))
T = os.path.join(D, '..', '..', '..', 'share', 'templates', '08-Orchestral', '03-String_Orchestra.mscx')
SEC = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
PITCHES = {'Violins 1': [57, 76, 88], 'Violins 2': [57, 74, 86], 'Violas': [50, 67, 79], 'Celli': [38, 59, 71],
           'Basses': [30, 39, 51]}
L1S = [('16th', F(1, 16)), ('eighth', F(1, 8)), ('quarter', F(1, 4))]
NS = [2, 3, 4]
BPMS = [80, 110, 160]
DYN = [('pp', 33, 32), ('mp', 64, 64), ('f', 96, 96)]  # subtype, MS3 velocity, MS4 CC1
SHAPES = [[0, 2, 4, 5], [0, -2, -1, 1], [0, 1, 3, 1]]
RECOMMENDED = [('heldNotes/early', 100), ('heldNotes/byPitch', 0), ('levels/calibrated', 2), ('slurs/quick', 2),
               ('slurs/quickLevel', 1), ('slurs/performance', 1), ('slurs/transitions', 1), ('slurs/smoothStart', 1),
               ('slurs/transitionsEarly', 1), ('levels/context', 1), ('levels/sectionBalance', 2),
               ('levels/realRange', 2), ('levels/evenLevels', 0), ('levels/micCompensation', 1)]
TPC = {0: 14, 1: 21, 2: 16, 3: 23, 4: 18, 5: 13, 6: 20, 7: 15, 8: 22, 9: 17, 10: 24, 11: 19}
DT = {F(1, 16): '16th', F(1, 8): 'eighth', F(1, 4): 'quarter', F(1, 2): 'half'}


def settings(extra=''):
    return ';'.join(f'{k}={v}' for k, v in RECOMMENDED) + extra


def note(p):
    return f'<Note><pitch>{p}</pitch><tpc>{TPC[p % 12]}</tpc></Note>'


def rests(left):
    out = ''
    for d in (F(1, 2), F(1, 4), F(1, 8), F(1, 16)):
        while left >= d:
            out += f'<Rest><durationType>{DT[d]}</durationType></Rest>'
            left -= d
    return out


def slurs():
    cells = [(l1, n, bpm, dyn, pi) for l1 in range(3) for n in NS for bpm in BPMS for dyn in range(3) for pi in range(3)]
    random.Random(SEED).shuffle(cells)
    return cells


def build():
    ms, idx, t = [], {s: [] for s in SEC}, 0.0
    for l1, n, bpm, dyn, pi in slurs():
        name, d1 = L1S[l1]
        df = F(1, 8) if d1 == F(1, 4) else F(1, 16)
        shape = SHAPES[(l1 + n) % 3]
        lens = [d1] + [df] * (n - 1)
        dist = sum(lens[:-1])
        v = {}
        for s in SEC:
            p0 = PITCHES[s][pi]
            x = f'<Dynamic><subtype>{DYN[dyn][0]}</subtype><velocity>{DYN[dyn][1]}</velocity></Dynamic>'
            for k in range(n):
                sp = ''
                if k == 0:
                    sp = (f'<Spanner type="Slur"><Slur></Slur><next><location><fractions>{dist.numerator}/{dist.denominator}'
                          '</fractions></location></next></Spanner>')
                elif k == n - 1:
                    sp = (f'<Spanner type="Slur"><prev><location><fractions>-{dist.numerator}/{dist.denominator}'
                          '</fractions></location></prev></Spanner>')
                x += f'<Chord><durationType>{DT[lens[k]]}</durationType>{sp}{note(p0 + shape[k])}</Chord>'
            v[s] = x + rests(F(1) - sum(lens))
        beat = 60.0 / bpm
        for s in SEC:
            on, notes = t, []
            for k in range(n):
                notes.append(dict(pitch=PITCHES[s][pi] + shape[k], on=round(on, 6), len=round(float(lens[k]) * 4 * beat, 6)))
                on += float(lens[k]) * 4 * beat
            idx[s].append(dict(l1=name, n=n, bpm=bpm, dyn=DYN[dyn][2], pi=pi, notes=notes))
        tempo = f'<Tempo><tempo>{bpm / 60:.6f}</tempo><followText>1</followText><text>q = {bpm}</text></Tempo>'
        ms.append((tempo, v))
        ms.append(('', {s: '<Rest><durationType>measure</durationType><duration>4/4</duration></Rest>' for s in SEC}))
        t += 8 * beat
    return ms, idx, t


def write(path, ms, extra=''):
    src = open(T, encoding='utf-8').read()
    head = src[:src.index('    <Staff id="1">\n      <VBox>')]
    head = head.replace('    <metaTag name="arranger"></metaTag>',
                        f'    <metaTag name="arranger"></metaTag>\n    <metaTag name="playbackSettings">{settings(extra)}</metaTag>', 1)
    head = head.replace('<showMargins>0</showMargins>', '<showMargins>0</showMargins>\n    <layoutMode>line</layoutMode>', 1)
    out = [head]
    for i, s in enumerate(SEC):
        out.append(f'    <Staff id="{i + 1}">')
        for k, (t, v) in enumerate(ms):
            pre = '<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>' if k == 0 else ''
            out.append(f'      <Measure><voice>{pre}{t if i == 0 else ""}{v[s]}</voice></Measure>')
        out.append('      </Staff>')
    open(path, 'w', encoding='utf-8').write('\n'.join(out) + '\n    </Score>\n  </museScore>\n')


SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 16

if __name__ == "__main__":
    O = sys.argv[1]
    os.makedirs(O, exist_ok=True)
    ms, idx, t = build()
    write(f'{O}/grid.mscx', ms)
    json.dump(dict(sections=SEC, pitches=PITCHES, seconds=round(t, 3), parts=idx), open(f'{O}/index.json', 'w'))
    print('grid.mscx', len(ms), 'measures,', round(t / 60, 1), 'min;', {s: len(v) for s, v in idx.items()})
