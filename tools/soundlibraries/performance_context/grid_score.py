# The level rule's grid, measured as MuseScore plays it (2026-10-15): scores for MuseScore3Evo.exe --verify-playback
# --verify-wav (each part rendered alone through the hosted Kontakt + SSO, its notes.tsv), from the repository's
# String Orchestra template (no owner material). The five string parts play the same figures at once, each at its own
# grid pitches:
#   for each dynamic d (pp mp f ff = CC1 32 64 96 112, MS4's levels) and each grid pitch p of the section:
#     held: four half notes [p, p-2, p+2, p] at q = 60 (2 s each, 2 s rest): All techniques Long, CC1 = d;
#     slurred, for each IOI (100 136 200 300 500 1000 ms, a tempo per figure): two touching slurs of four sixteenths,
#       [p, p-2, p, p+1] [p, p+2, p, p-1] (each median p), then a half rest and a measure's rest at q = 60 (4 s):
#       Performance Legato (a smooth first note, fingered within a slur, bowed into the next, as Recommended plays).
# Take B is take A in reverse order (round robins, drift). Writes <out>/gridA.mscx, gridB.mscx, calib.mscx and
# index.json (every note of every part in time order: kind, d, ioi, p).
#   calib.mscx: the 2026-10-15 check's calibration score: 8 held halves at mp, two 8-quarter slurs at q = 60 (IOI 1 s),
#   8 held halves at f, then the same slurs at f (the check had mp slurs only).
# The playbackSettings metaTag: Recommended's values (Playback::presets()) written out; MEASURE (the grid) also turns off
# what plays alike on both techniques of a section and so cancels in held - slurred (sectionBalance, realRange,
# micCompensation) and evenLevels: the grid sets a slur's overall level (its median), even levels then flattens the
# notes inside it around that median by a CC11 glide (EvenLevels: to each slur's median), so the two stack without
# counting anything twice; measured with even levels on, the gain would depend on even values measured at the old gains.
# The map's contextLevel is zeroed for the grid's renders (the VM's copy of the build only), so CC1 = the written
# dynamic and no gain: held - slurred is the gain each cell needs.
import json, os, re, sys
D = os.path.dirname(os.path.abspath(__file__))
T = os.path.join(D, '..', '..', '..', 'share', 'templates', '08-Orchestral', '03-String_Orchestra.mscx')
SEC = ['Violins 1', 'Violins 2', 'Violas', 'Celli', 'Basses']
PITCHES = {'Violins 1': [57, 76, 88], 'Violins 2': [57, 74, 86], 'Violas': [50, 67, 79], 'Celli': [38, 59, 71],
           'Basses': [30, 39, 51]}  # the 2026-10-10 grid's: lowest open string + 2, the map's calibration pitch, + 12
DYN = [('pp', 33, 32), ('mp', 64, 64), ('f', 96, 96), ('ff', 112, 112)]  # subtype, MS3 velocity, MS4 CC1
IOIS = [100, 136, 200, 300, 500, 1000]
RECOMMENDED = [('heldNotes/early', 100), ('heldNotes/byPitch', 0), ('levels/calibrated', 1), ('slurs/quick', 2),
               ('slurs/quickLevel', 1), ('slurs/performance', 1), ('slurs/transitions', 1), ('slurs/smoothStart', 1),
               ('slurs/transitionsEarly', 1), ('levels/context', 1), ('levels/sectionBalance', 2),
               ('levels/realRange', 2), ('levels/evenLevels', 1), ('levels/micCompensation', 1)]
MEASURE = {'levels/sectionBalance': 0, 'levels/realRange': 0, 'levels/micCompensation': 0, 'levels/evenLevels': 0}
TPC = {0: 14, 1: 21, 2: 16, 3: 23, 4: 18, 5: 13, 6: 20, 7: 15, 8: 22, 9: 17, 10: 24, 11: 19}


def settings(measure):
    return ';'.join(f'{k}={MEASURE.get(k, v) if measure else v}' for k, v in RECOMMENDED)


def note(p):
    return f'<Note><pitch>{p}</pitch><tpc>{TPC[p % 12]}</tpc></Note>'


def chord(dt, p, extra=''):
    return f'<Chord><durationType>{dt}</durationType>{extra}{note(p)}</Chord>'


def rest(dt):
    return f'<Rest><durationType>{dt}</durationType>' + ('<duration>4/4</duration>' if dt == 'measure' else '') + '</Rest>'


def dyn(s, v):
    return f'<Dynamic><subtype>{s}</subtype><velocity>{v}</velocity></Dynamic>'


def tempo(bpm):
    return f'<Tempo><tempo>{bpm / 60:.6f}</tempo><followText>1</followText><text>q = {bpm:g}</text></Tempo>'


def slur(n, frac):  # a slur from this chord over the next n - 1 chords of length frac each (same measure)
    a = f'<Spanner type="Slur"><Slur></Slur><next><location><fractions>{(n - 1)}/{frac}</fractions></location></next></Spanner>'
    b = f'<Spanner type="Slur"><prev><location><fractions>-{(n - 1)}/{frac}</fractions></location></prev></Spanner>'
    return a, b


def grid_blocks(reverse):
    blocks = [(d, pi, ioi) for d in DYN for pi in range(3) for ioi in [None] + IOIS]
    return blocks[::-1] if reverse else blocks


def grid(reverse):
    """measures: a list of (tempo marking or '', {section: voice xml}), and every part's notes in time order"""
    ms, notes, last = [], {s: [] for s in SEC}, None
    for d, pi, ioi in grid_blocks(reverse):
        mark = (d, pi) != last  # a dynamic at every new (d, pitch) block
        last = (d, pi)
        if ioi is None:
            for k, off in enumerate([0, -2, 2, 0]):
                v = {}
                for s in SEC:
                    p = PITCHES[s][pi] + off
                    v[s] = (dyn(d[0], d[1]) if mark and k == 0 else '') + chord('half', p) + rest('half')
                    notes[s].append(dict(kind='held', d=d[2], p=PITCHES[s][pi], pitch=p))
                ms.append((tempo(60) if k == 0 else '', v))
        else:
            v = {}
            a, b = slur(4, 16)
            for s in SEC:
                p0 = PITCHES[s][pi]; x = dyn(d[0], d[1]) if mark else ''
                for si, offs in enumerate([[0, -2, 0, 1], [0, 2, 0, -1]]):
                    for k, off in enumerate(offs):
                        x += chord('16th', p0 + off, a if k == 0 else b if k == 3 else '')
                        notes[s].append(dict(kind='slur', d=d[2], ioi=ioi, p=p0, pitch=p0 + off, slur=si, k=k))
                v[s] = x + rest('half')
            ms.append((tempo(15000 / ioi), v))
            ms.append((tempo(60), {s: rest('measure') for s in SEC}))
    ms.append((tempo(60), {s: rest('measure') for s in SEC}))
    return ms, notes


def calib():
    ms, notes = [], {s: [] for s in SEC}
    SC = {'Violins 1': 67, 'Violins 2': 67, 'Violas': 60, 'Celli': 48, 'Basses': 36}  # the check's: 8 white keys up

    def scale(lo):
        out, p = [], lo
        while len(out) < 8:
            if p % 12 in (0, 2, 4, 5, 7, 9, 11):
                out.append(p)
            p += 1
        return out

    for dn, vel, cc in (('mp', 64, 64), ('f', 96, 96)):
        for k in range(8):
            ms.append((tempo(60) if not ms else '', {s: (dyn(dn, vel) if k == 0 else '') + chord('half', scale(SC[s])[k]) + rest('half') for s in SEC}))
            for s in SEC:
                notes[s].append(dict(kind='held', d=cc, pitch=scale(SC[s])[k]))
        for m in range(4):
            v = {s: '' for s in SEC}
            for b in range(4):
                j = m * 4 + b
                for s in SEC:
                    q = scale(SC[s]) + scale(SC[s])[::-1]
                    sp = ('<Spanner type="Slur"><Slur></Slur><next><location><measures>1</measures><fractions>3/4</fractions></location></next></Spanner>'
                          if j % 8 == 0 else
                          '<Spanner type="Slur"><prev><location><measures>-1</measures><fractions>-3/4</fractions></location></prev></Spanner>'
                          if j % 8 == 7 else '')
                    v[s] += chord('quarter', q[j], sp)
                    notes[s].append(dict(kind='slur', d=cc, ioi=1000, pitch=q[j], slur=j // 8, k=j % 8))
            ms.append(('', v))
        ms.append(('', {s: rest('measure') for s in SEC}))
    ms.append(('', {s: rest('measure') for s in SEC}))
    return ms, notes


def write(path, ms, measure):
    src = open(T, encoding='utf-8').read()
    head = src[:src.index('    <Staff id="1">\n      <VBox>')]
    head = head.replace('    <metaTag name="arranger"></metaTag>',
                        f'    <metaTag name="arranger"></metaTag>\n    <metaTag name="playbackSettings">{settings(measure)}</metaTag>', 1)
    head = head.replace('<showMargins>0</showMargins>', '<showMargins>0</showMargins>\n    <layoutMode>line</layoutMode>', 1)
    out = [head]
    for i, s in enumerate(SEC):
        out.append(f'    <Staff id="{i + 1}">')
        for n, (t, v) in enumerate(ms):
            pre = '<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>' if n == 0 else ''
            out.append(f'      <Measure><voice>{pre}{t if i == 0 else ""}{v[s]}</voice></Measure>')
        out.append('      </Staff>')
    open(path, 'w', encoding='utf-8').write('\n'.join(out) + '\n    </Score>\n  </museScore>\n')


if __name__ == '__main__':
    O = sys.argv[1] if len(sys.argv) > 1 else '.'
    os.makedirs(O, exist_ok=True)
    idx = {}
    for name, rev in (('gridA', False), ('gridB', True)):
        ms, notes = grid(rev)
        write(f'{O}/{name}.mscx', ms, True); idx[name] = notes
    ms, notes = calib()
    write(f'{O}/calib.mscx', ms, False); idx['calib'] = notes
    json.dump(dict(sections=SEC, pitches=PITCHES, dynamics=[d[2] for d in DYN], ioiMs=IOIS, scores=idx),
              open(f'{O}/index.json', 'w'))
    for n, v in idx.items():
        print(n, {s: len(x) for s, x in v.items()})
