#!/usr/bin/env python3
"""summarize.py <work folder> [out.json]: the groups of tools/soundlibraries/staccato_lengths.json from the per-note
files of tub8_ii.py, tub8_ii_rests.py, tub8_iv.py and spheres_k550.py, with SSO's own short lengths
(sso_short_lengths.json) and MS4.7.5's rule (ms4tables.h PROFILE: staccato 50 %, staccatissimo 25 %) beside them.

Per group: notes, tracks, the median IOI to the next onset, the written value (s, at the played tempo), and per measure
(power to -6 / -10 / -20 dB under the note's peak, perceived to -10) the median and IQR in ms, the median as a share of
the written value, and the share of notes still within that many dB at the next onset ("joined": their length is only a
lower bound, so a median is given only while fewer than half are joined). Uncertainty between players: the range of the
per-track medians (tracks: one player's pass; Spheres: one section).
"""
import json, os, random, statistics, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
MEASURES = (('e20', 'joined20'), ('e10', 'joined10'), ('e6', None), ('p10', 'pJoined10'))


def group(rows, written, trackKey):
    out = {'notes': len(rows), 'tracks': len({r[trackKey] for r in rows}),
           'ioiMs': round(1000 * statistics.median(r['ioi'] for r in rows)), 'writtenMs': round(1000 * written)}
    # e20's 90 % interval: a two-level bootstrap (tracks, then each track's notes), 4000 draws, seed 1; with one track
    # only the notes' spread (the players' then unknown)
    rng = random.Random(1)
    tracks = sorted({r[trackKey] for r in rows})
    by = {t: [r['e20'] for r in rows if r[trackKey] == t] for t in tracks}
    boots = []
    for _ in range(4000):
        v = []
        for t in (rng.choice(tracks) for _ in tracks):
            v += rng.choices(by[t], k=len(by[t]))
        boots.append(statistics.median(v))
    boots.sort()
    out['e20Interval90Ms'] = [round(1000 * boots[200]), round(1000 * boots[3799])]
    if 'dipDb' in rows[0]:
        out['dipDbMedian'] = round(statistics.median(r['dipDb'] for r in rows), 1)
    for k, j in MEASURES:
        v = np.array([r[k] for r in rows])
        jn = sum(r[j] for r in rows) / len(rows) if j else 0.0
        q = np.percentile(v, [25, 50, 75])
        per = [statistics.median(r[k] for r in rows if r[trackKey] == t) for t in sorted({r[trackKey] for r in rows})]
        out[k] = {'medianMs': round(1000 * q[1]) if jn < 0.5 else None, 'iqrMs': [round(1000 * q[0]), round(1000 * q[2])],
                  'ofWritten': round(q[1] / written, 2) if jn < 0.5 else None, 'joined': round(jn, 2),
                  'trackMediansMs': [round(1000 * min(per)), round(1000 * max(per))]}
    return out


def main(work, out):
    W = lambda f: json.load(open(os.path.join(work, f)))['notes']
    G = {}
    tw = W('tub8_ii_winds.json')
    # the owner's review, 2026-10-24: bars 1-2 confirmed (R02-R05); bars 3-8, 18-22 "can't tell" (R06-R11): kept apart,
    # marked unconfirmed, not used for the map's values
    for inst in ('oboe', 'clarinet', 'bassoon', 'horn'):
        for label, wins, ok in (('b1-2 pp', ('b1-2 pp',), True), ('b3-8 pp', ('b3-8 pp',), False),
                                ('b18-22 cresc. to f', ('b18-19 cresc', 'b20-22 f'), False)):
            r = [x for x in tw if x['instrument'] == inst and x['window'] in wins]
            if r:
                G[f'B8/II {inst} staccato 16ths {label}'] = dict(group(r, statistics.median(x['ioi'] for x in r), 'track'),
                    source='TU B8 II ' + label.split()[0], family='winds', articulation='staccato (dots, pp sempre staccato)',
                    readingsConfirmed=ok)
    r = [x for x in tw if x['window'] == 'b1-2 pp']
    G['B8/II winds staccato 16ths b1-2 pp, all'] = dict(group(r, statistics.median(x['ioi'] for x in r), 'track'),
        source='TU B8 II b1-2', family='winds', articulation='staccato', readingsConfirmed=True)
    tr = W('tub8_ii_rests.json')
    six = statistics.median(x['ioi'] for x in tw)   # the played sixteenth
    for fam, insts, art in (('winds', ('oboe', 'clarinet', 'bassoon', 'horn'), 'no mark printed (whether bar 1\'s sempre staccato still holds: unconfirmed)'),
                            ('cello', ('cello',), 'unmarked, before a rest'), ('double bass', ('double bass',), 'unmarked, before a rest')):
        r = [x for x in tr if x['instrument'] in insts]
        G[f'B8/II {fam} eighths before a rest'] = dict(group(r, 2 * six, 'track'), source='TU B8 II b13-17', readingsConfirmed=True,
                                                         family='strings' if fam != 'winds' else 'winds', articulation=art)
    tv = W('tub8_iv_vl1.json')
    q = statistics.median(x['ioi'] for x in tv if 'staccato' in x['label'])
    for lab, art in (('staccato', 'staccato (dots)'), ('b9 unmarked', 'unmarked, in a line of quarters'),
                     ('b10 unmarked before rest', 'unmarked, before a rest')):
        r = [x for x in tv if lab in x['label']]
        G[f'B8/IV violin 1 quarters, {art}'] = dict(group(r, q, 'track'), source='TU B8 IV b6-10', family='strings', readingsConfirmed=True,
                                                    articulation=art)
    sp = W('spheres_k550.json')
    for stem, name in (('Violin_1', 'violins 1'), ('Cello', 'celli'), ('Bass_1', 'basses')):
        for kind in ('staccato', 'unmarked before rests', 'held'):
            r = [x for x in sp if x['stem'] == stem and x['kind'] == kind]
            if not r:
                continue
            written = 0.30 if kind == 'unmarked before rests' else statistics.median(x['ioi'] for x in r)
            G[f'K550/I {name} {kind} ({r[0]["written"]})'] = dict(group(r, written, 'passage'), source='Spheres K550 I', readingsConfirmed=True,
                                                                   family='strings (section)', articulation=kind)
    sso = json.load(open(os.path.join(HERE, '..', 'sso_short_lengths.json')))
    S = {}
    for patch, arts in (('Violins 1', ('Spiccato', 'Short 0.5')), ('Violas', ('Spiccato', 'Short 0.5')),
                        ('Celli', ('Spiccato', 'Short 0.5')), ('Basses', ('Spiccato', 'Short 0.5')),
                        ('Flute Solo', ('staccato',)), ('Oboe Solo', ('staccato',)), ('Clarinet Solo', ('staccato',)),
                        ('Bassoon Solo', ('staccato',)), ('Horn Solo', ('staccato',)), ('Trumpet Solo', ('staccato',))):
        for a in arts:
            rows = [r for r in sso[patch][a] if r[1] in (100, 250)]
            S[f'{patch} / {a}'] = {'p10Ms': round(statistics.median(r[3][1] for r in rows)),
                                   'e20Ms': round(statistics.median(r[4][3] for r in rows))}
    doc = {
        'about': 'How long real orchestral players sound short notes (docs/RESEARCH.md 2.5a). Written by '
                 'staccato_lengths/summarize.py from the per-note measurements of tub8_ii.py, tub8_ii_rests.py, tub8_iv.py, '
                 'spheres_k550.py (audio deleted after measuring). Times in ms from the start of the rise that carries the '
                 "note's peak to the end of the last 5 ms window within X dB of the peak before the next onset. e20 / e10 / "
                 'e6: power; p10: perceived (ArticulationCheck\'s perceivedEnvelope, the measure of SSO\'s p10). '
                 'ofWritten: median / written value at the played tempo. joined: share of notes still within X dB at the '
                 'next onset (a lower bound; no median when >= 0.5). Readings of the articulation marks: '
                 'staccato_lengths/readings_articulation.tsv; the TU ones are an agent\'s, NOT reviewed by the owner '
                 '(the K. 550 ones are the owner-confirmed readings of 2026-10-15); the Beethoven 8 ones reviewed by the owner '
                 '2026-10-24 (https://claude.ai/artifact/GNwFDt1PcNHJ1UWoWugvDc): R01-R05, R12-R17, R19-R24 right, R06-R11 and R18 '
                 '"can not tell": groups with readingsConfirmed false rest on those and are not used for the map '
                 '(staccatoLength=, gen_spitfire_sso.py).',
        'groups': G,
        'sso': {'about': 'SSO shorts held 100 / 250 ms at mf (sso_short_lengths.json, the VM rest check, Tree mic: the hall '
                         'is in it), median over the three pitches: p10 and e20 in ms', 'patches': S},
        'ms4': {'about': 'MS4.7.5 note-off as a share of the written value (ms4tables.h PROFILE, every family): what '
                         'MuseScore plays on the built-in sounds and uses as the meant length when choosing an SSO short '
                         '(shorts/staccato etc.)', 'staccato': 0.50, 'staccatissimo': 0.25, 'portato': 0.745,
                'tenuto': 0.99, 'unmarked': 0.99},
    }
    json.dump(doc, open(out, 'w'), indent=1, ensure_ascii=False)
    for k, v in S.items():
        print('SSO', k, v)
    for k, g in G.items():
        f = lambda m: '%s (%s) j%.2f' % (g[m]['medianMs'], g[m]['ofWritten'], g[m]['joined'])
        print('%-62s n%4d ioi%5d wr%5d | e20 %-18s e10 %-18s p10 %-18s trk e20 %s' % (
            k, g['notes'], g['ioiMs'], g['writtenMs'], f('e20'), f('e10'), f('p10'), g['e20']['trackMediansMs']))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'staccato_lengths_groups.json'))
