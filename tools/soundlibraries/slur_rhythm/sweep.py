# sweep.py <out dir> [seed]: the slur-rhythm pitch sweep (2026-10-16): the grid (score.py) has 3 pitches per section, and a
# check of Whence's Violas slur (eighth + two sixteenths at 110 bpm, mp) at 9 pitches found the first note's onset
# a property of the key (66-170 ms, repeats within 6 ms: SSO's samples by key zone), so the map's slurOnset is by pitch:
# here every semitone of each section's usual range (RANGES), that one shape (up a tone, then a semitone: [0 2 3],
# Whence's bars 9-11), mp, 2 repeats, a fixed shuffle (random.Random(seed): take A 21, take B 22). Each section plays
# its own pitch list (cycled where it is shorter than the longest). Writes <out>/grid.mscx and <out>/index.json as
# score.py does (its playbackSettings: Recommended, even levels off).
import json, os, random, sys
from fractions import Fraction as F
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import score as S  # noqa: E402

RANGES = {'Violins 1': (55, 91), 'Violins 2': (55, 91), 'Violas': (48, 81), 'Celli': (36, 69), 'Basses': (28, 55)}
SEED = int(sys.argv[2]) if len(sys.argv) > 2 else 21
N = max(b - a + 1 for a, b in RANGES.values())
S.PITCHES = {s: [RANGES[s][0] + i % (RANGES[s][1] - RANGES[s][0] + 1) for i in range(N)] for s in S.SEC}
S.SHAPES = [[0, 2, 3, 5]] * 3
cells = [(1, 3, 110, 1, pi) for pi in range(N)] * 2
random.Random(SEED).shuffle(cells)
S.slurs = lambda: cells

if __name__ == '__main__':
    O = sys.argv[1]
    os.makedirs(O, exist_ok=True)
    ms, idx, t = S.build()
    S.write(f'{O}/grid.mscx', ms)
    json.dump(dict(sections=S.SEC, pitches=S.PITCHES, seconds=round(t, 3), parts=idx), open(f'{O}/index.json', 'w'))
    print('sweep', len(cells), 'slurs,', round(t / 60, 1), 'min')
