#!/usr/bin/env python3
"""The real player's dynamic range against SSO's, for [levels] realRange (libmscore/contextlevel.h › Real range).

    python3 -I real_range.py [--rule-d] <3_Audio_Features.csv> <sso_cc1_curves.json> <out sso_real_range.json> [<sso_winds_cc1_curves.json>
                             [<sso_gap_cc1_curves.json>]]
    (since 2026-10-28: sso_winds_cc1_curves.json,sso_winds_cc1_semitone.json sso_gap_cc1_curves.json,sso_gap_cc1_semitone.json:
    a comma joins files, a later one's sections win: the winds' longLevel every semitone)

--rule-d: every instrument by rule D (the comparison; the map then gets no realPlay). Both rules' levels at each entry's
middle pitch are written either way ("marksMid", "realDbMid").

With the winds' curves (2026-10-14) the woodwinds' and brass' main patches (WINDS) are written too, under
"instruments", the same way: their own k by leave-one-out pooled over their twelve database instruments, SSO's Long
from the winds' sweep (sso_winds_cc1_curves.json, same measure). The strings' "sections" don't change.

With the gap patches' curves (2026-10-15, docs/BALANCE_LOG.md › Iteration 4) the 15 patches that had no section level
before iteration 2 (section_balance_cl.py GAP) are written under "instruments" too: those with their own database
instrument (GAP_OWN: the solo strings, the Motif brass; a Motif a3 / a4 / a5 one player's span) as the winds; those
without (GAP_FAMILY) with their family's Clark & Luce span only (an ASSUMPTION, not a measurement: plays like its
family) and no span ("realSpan"): realRange 2 plays them, realRange 1 (the Meyer rule, which needs the database's
span) leaves them at the library's level. SSO's Long from sso_gap_cc1_curves.json, the same measure.

Stage A of the dynamics plan (HANDOFF.md › Dynamics like a real player; the owner, 2026-10-12): each note of SSO's five
string sections as loud as a real player at its written dynamic, the player's full range, by CC11 and the patches'
volumes; CC1 (the recording, mostly tone) stays MuseScore's.

Real side: Weinzierl, Lepa, Schultz, Detzner, von Coler & Behler 2018, "Sound power and timbre as cues for the dynamic
strength of orchestral instruments", JASA 144(3) (TU Berlin database v3, 3_Audio_Features.csv: column "SoundPower in
dB" of single tones, modern instruments, chromatic "_et_" files, pp and ff; the mean where a pitch has several files).
Violin for Violins 1 and 2, viola, cello, double bass. The CSV is CC BY-NC-SA 4.0 and not in the repository: only
derived numbers are written.

The Meyer rule (the owner, 2026-10-14: "replace rule D with this, this is clearly better"): the database's pp and ff
("as softly / loudly as possible") are still the player's ppp and fff, the softest and loudest single note. Written pp
and ff are what players produce in normal playing: Juergen Meyer, Acoustics and the Performance of Music (Springer 2009),
chapter 3, gives per instrument the sound power of single-note extremes X (lowest pp, highest ff) and of rapid scales P
(pp, ff, each note sounded). Per instrument a = (P_pp - X_lo) / (X_hi - X_lo) and b = (P_ff - X_lo) / (X_hi - X_lo) place
pp and ff inside each pitch's span: pp(p) = tuPp(p) + a span(p), ff(p) = tuPp(p) + b span(p) (TU Berlin's per-pitch
shape kept); p, mp, mf, f in equal dB steps between pp and ff (Meyer's own method for his forte: equal steps from pp to
ff), 5 steps of (b - a) span / 5; ppp -> pp and ff -> fff one step each. Relative to mp as before (the section balance M
unchanged); piecewise linear in marking steps, so continuous in CC1 through hairpins. Rule D is the case a = 1/7,
b = 6/7. Instruments without Meyer extremes keep rule D (MEYER_NONE), except the bass clarinet and the bass trombone,
which take their sibling's fractions (SIBLING: the clarinet's, the tenor trombone's; the owner, 2026-10-14: an assumption,
"plays like its family", not a measurement); Cor Anglais and Contrabassoon keep rule D. Only section numbers and figures are used here
(the book is copyrighted).

Clark & Luce spans ([levels] realRange 2, the owner, 2026-10-14: the section data where reliable, one source for every
instrument): each entry with one (SPANS below: Clark & Luce 1965, Table I) gets "sectionSpan" {db S, source}: written markings in equal steps of S / 5 around mp
(pp -2 S / 5 .. ff +3 S / 5, ppp / fff one step more), whatever the single-note span; gen_spitfire_sso.py writes it as
realSteps="S" and MuseScore plays it with realRange 2 (realRange 1, the Meyer rule, unchanged). marksMid / realDbMid
"spans" give it at the middle pitch.

Rule D (the owner, 2026-10-12; kept where Meyer has no extremes, and by --rule-d): the database's pp and ff ("as softly / loudly as possible") are the player's ppp and
fff; the markings between them in 7 equal dB steps of (ff - pp) / 7 at each pitch, relative to mp (where the section
balance M was set). So only the span ff - pp per pitch matters. The span is smoothed over pitch (single takes: a
semitone's span jumps by several dB): a local linear least-squares fit over the pitches within +-k semitones of each
pitch (uniform weights), k chosen by leave-one-out cross-validation (the k with the least squared error predicting
each measured span from the others, pooled over the four instruments, k = 1 .. 12). Written at every semitone of the
database's range per instrument; MuseScore takes the nearest end's beyond it.

SSO side: sso_cc1_curves.json (Windows test VM, Kontakt 8 + SSO, 2026-10-11; Celli and Basses again 2026-10-14):
All techniques Long at CC1 = velocity 16 .. 127, median of 4 (two round robins, each twice), Violins and Violas every 3
semitones, Celli and Basses every semitone (between the old points the 3-semitone grid's interpolation was up to 2.2 /
7.3 dB off). The measure: rmsDb, the mean power of the sustain 1.5-3.0 s after the note-on (like for like with the
database's sound power of a held tone; loudDb, the loudest 50 ms, is the dynamics check's peak measure). Written as
each measured pitch's level less its level at CC1 64 (mp), unsmoothed (offline a round robin's takes are identical, so
the median of 4 is the two round robins' mean; they differ by ~1 dB, Basses up to 5.5; between pitches MuseScore
interpolates linearly).
"""
import csv
import json
import sys

SECTIONS = {'Violins 1': 'Violin modern', 'Violins 2': 'Violin modern', 'Violas': 'Viola modern',
            'Celli': 'Cello modern', 'Basses': 'Double bass modern'}
# woodwinds and brass (2026-10-14): SSO's main patch (solo, a2, a6) -> the database's instrument; the a2 / a6 patches take
# one player's span (rule D is per player: a section's dynamic range is the player's). SSO's patches the database lacks
# stay at the library's default: Piccolo, Alto Flute, Bass Flute, Contrabass Clarinet, Cimbasso Solo / Cimbassi a2,
# Contrabass Trombone, Contrabass Tuba (its "Tuba modern" is a bass tuba), and the Motif a3 / a4 / a5 patches (not measured)
WINDS = {'Flute Solo': 'Transverse flute modern', 'Flutes a2': 'Transverse flute modern',
         'Oboe Solo': 'Oboe modern', 'Oboes a2': 'Oboe modern', 'Cor Anglais': 'English horn modern',
         'Clarinet Solo': 'Clarinet modern', 'Clarinets a2': 'Clarinet modern', 'Bass Clarinet': 'Bass clarinet modern',
         'Bassoon Solo': 'Bassoon modern', 'Bassoons a2': 'Bassoon modern', 'Contrabassoon': 'Contrabassoon modern',
         'Horn Solo': 'French horn modern', 'Horns a2': 'French horn modern', 'Horns a6': 'French horn modern',
         'Trumpet Solo': 'Trumpet modern', 'Trumpets a2': 'Trumpet modern', 'Trumpets a6': 'Trumpet modern',
         'Tenor Trombone Solo': 'Tenor trombone modern', 'Tenor Trombones a2': 'Tenor trombone modern',
         'Trombones a6': 'Tenor trombone modern', 'Bass Trombone Solo': 'Bass trombone modern',
         'Bass Trombones a2': 'Bass trombone modern', 'Tuba Solo': 'Tuba modern'}
NO_DATA = ['Piccolo', 'Alto Flute', 'Bass Flute', 'Contrabass Clarinet', 'Cimbasso Solo', 'Cimbassi a2',
           'Contrabass Trombone', 'Contrabass Tuba', 'Motif Horns a4', 'Motif Trumpets a3', 'Motif Trombones a5']
# The patches without a section level before 2026-10-15 (section_balance_cl.py GAP), their Long swept 2026-10-15
# (sso_gap_cc1_curves.json). Their own database instrument (one player's span, as the a2 / a6 patches):
GAP_OWN = {'Solo Violin 1': 'Violin modern', 'Solo Violin 2': 'Violin modern', 'Solo Viola': 'Viola modern',
           'Solo Cello': 'Cello modern', 'Motif Horns a4': 'French horn modern', 'Motif Trumpets a3': 'Trumpet modern',
           'Motif Trombones a5': 'Tenor trombone modern'}
# No database instrument and no Clark & Luce span of their own: their family's span (an ASSUMPTION, not a measurement:
# plays like its family; the same families as their section level, section_balance_cl.py GAP); no realSpan
GAP_FAMILY = {'Piccolo': 'Transverse flute modern', 'Alto Flute': 'Transverse flute modern',
              'Bass Flute': 'Transverse flute modern', 'Contrabass Clarinet': 'Clarinet modern',
              'Contrabass Trombone': 'Tenor trombone modern', 'Cimbasso Solo': 'Tenor trombone modern',
              'Cimbassi a2': 'Tenor trombone modern', 'Contrabass Tuba': 'Tuba modern'}
KMAX = 12
# Meyer 2009, chapter 3: database instrument -> (section, X single-note extremes (lo, hi), P rapid scales (pp, ff)), dB
# sound power. Viola's X_lo: 63 on the D / A strings (67 low); clarinet's X_lo: 57 in the fifth octave (65 typical);
# flute's X: pp 67 low register, ff 100 highest octave; the trumpet's text says "sound pressure level", read as power like
# the rest of its figures
MEYER = {
    'Violin modern': ('3.3.1.2', (58, 99), (74, 94)),
    'Viola modern': ('3.3.2.2', (63, 95), (73, 91)),
    'Cello modern': ('3.3.3.2', (63, 98), (74, 96)),
    'Double bass modern': ('3.3.4.2', (66, 100), (79, 96)),
    'Transverse flute modern': ('3.2.1.2', (67, 100), (82, 94)),
    'Oboe modern': ('3.2.2.2', (70, 103), (83, 95)),
    'Clarinet modern': ('3.2.3.2', (57, 106), (77, 97)),
    'Bassoon modern': ('3.2.4.2', (72, 102), (81, 96)),
    'French horn modern': ('3.1.1.2', (65, 117), (86, 107)),
    'Trumpet modern': ('3.1.2.2', (78, 111), (89, 104)),
    'Tenor trombone modern': ('3.1.3.2', (73, 113), (89, 105)),
    'Tuba modern': ('3.1.4.2', (77, 112), (93, 108)),
}
# Meyer gives no single-note extremes against in-play levels: rule D. Piccolo (3.2.1.4: limits only, about 15 dB) has no
# database data anyway
MEYER_NONE = {
    'English horn modern': '3.2.2.4: only the limits pp 79 / ff 94 (narrower than the oboe), no scales against single notes',
    'Bass clarinet modern': '3.2.3.4: only single notes (pp 59 low register, ff about 97), no in-play levels',
    'Contrabassoon modern': '3.2.4.6: in-play pp 86 / ff 96, single notes only as a range ("up to 15 dB"), no levels',
    'Bass trombone modern': '3.1.3.4: tone colour only, no levels',
}
# An ASSUMPTION, not a measurement (the owner, 2026-10-14, "sure": "plays like its family"): these take their sibling's
# Meyer fractions, Meyer giving them no levels to place pp / ff by (MEYER_NONE above). Cor Anglais and Contrabassoon were
# not asked about: rule D
SIBLING = {
    'Bass clarinet modern': 'Clarinet modern',
    'Bass trombone modern': 'Tenor trombone modern',
}
# Clark & Luce spans ([levels] realRange 2; the owner, 2026-10-14: "let's use the section data wherever it's reliable",
# then one source for every instrument, not mixed): an instrument's written pp -> ff in dB, S; the markings in equal steps
# of S / 5 around mp, ppp / fff one step more (the map's realSteps, contextlevel.cpp realRangeDb). Source: M. Clark,
# D. Luce, "Intensities of Orchestral Instrument Scales Played at Prescribed Dynamic Markings", J. Audio Eng. Soc. 13(2),
# 1965, 151-157, Table I "Dynamic Range" (printed): two players per instrument, chromatic scales at pp, mf and ff as they
# would play those markings in orchestral music; the average ff scale less the average pp scale. Cross-check, not used:
# the Spheres dataset (Colibri Ensemble; arXiv 2511.21247, Zenodo 17347681), whole sections' scales, where a section
# recorded 3+ dynamics: Violin 2 13.7, Cello 15.8, Trumpet 10.6, Trombone 14.3 dB, within ~2 dB of these. Family (an
# ASSUMPTION, not a measurement: plays like its family): bass clarinet the clarinet's, contrabassoon the bassoon's, bass
# trombone the tenor trombone's.
CL = 'Clark & Luce 1965, Table I'
SPANS = {
    'Violin modern': (13.8, CL),
    'Viola modern': (15.9, CL),
    'Cello modern': (14.1, CL),
    'Double bass modern': (14.1, CL),
    'Transverse flute modern': (7.5, CL),
    'Oboe modern': (7.4, CL),
    'English horn modern': (5.2, CL),
    'Clarinet modern': (8.3, CL),
    'Bassoon modern': (10.2, CL),
    'French horn modern': (17.7, CL),
    'Trumpet modern': (8.8, CL),
    'Tenor trombone modern': (17.0, CL),
    'Tuba modern': (12.5, CL),
}
SPAN_FAMILY = {
    'Bass clarinet modern': 'Clarinet modern',
    'Contrabassoon modern': 'Bassoon modern',
    'Bass trombone modern': 'Tenor trombone modern',
}


def sectionSpan(lab):
    """(S dB, source) of the instrument's written pp -> ff, None: none (rule 1 then)"""
    if lab in SPANS:
        return SPANS[lab]
    if lab in SPAN_FAMILY:
        s, src = SPANS[SPAN_FAMILY[lab]]
        return s, src + "; " + SPAN_FAMILY[lab] + "'s, an assumption (plays like its family)"
    return None


MARKS = ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff']
MARK_CC = [16, 32, 48, 64, 80, 96, 112, 127]      # Ms4::expressionLevel; marking steps from mp -3 .. +4
RULE_D = (1 / 7, 6 / 7)


def fractions(lab):
    """(a, b): written pp and ff as fractions of the span above the single-note pp; None: rule D"""
    m = MEYER.get(lab) or MEYER.get(SIBLING.get(lab))
    if not m:
        return None
    (xlo, xhi), (ppp, fff) = m[1], m[2]
    return ((ppp - xlo) / (xhi - xlo), (fff - xlo) / (xhi - xlo))


def level(steps, ab):
    """the fraction of the span at a marking step (ppp -3 .. fff +4), piecewise linear (contextlevel.cpp realFraction)"""
    a, b = ab
    if steps <= -2:
        return max(0.0, (steps + 3) * a)
    if steps <= 3:
        return a + (steps + 2) * (b - a) / 5
    return min(1.0, b + (steps - 3) * (1 - b))


def between(m, x):
    """linear between the nearest keys, the nearest end's beyond (contextlevel.cpp between)"""
    ks = sorted(m)
    if x <= ks[0]:
        return m[ks[0]]
    if x >= ks[-1]:
        return m[ks[-1]]
    hi = next(k for k in ks if k >= x)
    if hi == x:
        return m[hi]
    lo = ks[ks.index(hi) - 1]
    return m[lo] + (x - lo) / (hi - lo) * (m[hi] - m[lo])


def marks(e, ab, sectionDb=None):
    """({marking: the player's dB from mp}, {marking: realDb}) at the entry's middle pitch; sectionDb: the section
    spans' rule (steps x S / 5) instead of fractions ab of the span"""
    p = e['midPitch']
    span = e['span'][str(p)] if sectionDb is None else None
    longs = {int(q): {int(c): v for c, v in per.items()} for q, per in e['long'].items()}
    player, real = {}, {}
    for i, (mk, cc) in enumerate(zip(MARKS, MARK_CC)):
        if sectionDb is not None:
            player[mk] = round((i - 3) * sectionDb / 5, 2)
        else:
            player[mk] = round((level(i - 3, ab) - level(0, ab)) * span, 2)
        real[mk] = round(player[mk] - between({q: between(per, cc) for q, per in longs.items()}, p), 2)
    return player, real


def addRule(e, ruleD):
    lab = e['instrument']
    ab = None if ruleD else fractions(lab)
    e['rule'] = 'meyer' if ab else 'D'
    if ab:
        e['fractions'] = {'pp': round(ab[0], 4), 'ff': round(ab[1], 4)}
    if lab in MEYER:
        sec, x, pl = MEYER[lab]
        e['meyer'] = {'section': sec, 'extremes': list(x), 'inPlay': list(pl)}
    else:
        e['meyerNone'] = MEYER_NONE.get(lab, 'not in Meyer chapter 3')
    if lab in SIBLING:
        sec, x, pl = MEYER[SIBLING[lab]]
        e['meyerSibling'] = {'instrument': SIBLING[lab], 'section': sec, 'extremes': list(x), 'inPlay': list(pl),
                             'why': 'an assumption, not a measurement: plays like its family (the owner, 2026-10-14)'}
    md, rd = marks(e, RULE_D)
    e['marksMid'], e['realDbMid'] = {'D': md}, {'D': rd}
    if fractions(lab):
        e['marksMid']['meyer'], e['realDbMid']['meyer'] = marks(e, fractions(lab))
    ss = sectionSpan(lab)
    if ss:
        e['sectionSpan'] = {'db': ss[0], 'source': ss[1]}
        e['marksMid']['spans'], e['realDbMid']['spans'] = marks(e, None, ss[0])
    return e


def spans(path, labels=None):
    """{database label: {midi: ff - pp}} (pitches with both)"""
    pc = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
    rows = list(csv.reader(open(path, encoding='utf-8')))
    head = rows[1]
    col = head.index('SoundPower in dB')
    out = {}
    for lab in set(labels or SECTIONS.values()):
        lv = {'pp': {}, 'ff': {}}
        for r in rows[2:]:
            if len(r) > col and r[1] == lab and '_et_' in r[0] and r[4] in lv:
                name = r[5]                     # e.g. "a3", "ak3" (k: sharp)
                m = 12 * (int(name[-1]) + 1) + pc[name[0]] + (1 if name[1] == 'k' else 0)
                lv[r[4]].setdefault(m, []).append(float(r[col]))
        mean = {d: {p: sum(v) / len(v) for p, v in t.items()} for d, t in lv.items()}
        out[lab] = {p: mean['ff'][p] - mean['pp'][p] for p in sorted(set(mean['pp']) & set(mean['ff']))}
    return out


def localLinear(points, p, k, leaveOut=None):
    """the least-squares line through the points within +-k of p (leaveOut excluded), at p; None: fewer than 2"""
    xs = [(x, y) for x, y in points.items() if abs(x - p) <= k and x != leaveOut]
    if len(xs) < 2:
        return None
    n = len(xs)
    mx = sum(x for x, _ in xs) / n
    my = sum(y for _, y in xs) / n
    sxx = sum((x - mx) ** 2 for x, _ in xs)
    if sxx == 0:
        return my
    b = sum((x - mx) * (y - my) for x, y in xs) / sxx
    return my + b * (p - mx)


def chooseK(data):
    best = None
    table = {}
    for k in range(1, KMAX + 1):
        err, n = 0.0, 0
        for pts in data.values():
            for p, y in pts.items():
                f = localLinear(pts, p, k, leaveOut=p)
                if f is not None:
                    err += (f - y) ** 2
                    n += 1
        table[k] = (err / n) ** 0.5
        if best is None or table[k] < table[best]:
            best = k
    return best, table


def curves(path):
    """the sections of a curves file, or of several separated by commas: a later file's section wins (2026-10-28: the
    winds' and gap patches' every-semitone sweeps over their 3-semitone ones, sso_*_cc1_semitone.json)"""
    out = {}
    for p in path.split(','):
        out.update(json.load(open(p, encoding='utf-8'))['sections'])
    return {'sections': out}


def sso(path, names=SECTIONS):
    d = curves(path)
    out = {}
    for s in names:
        pts = d['sections'][s]['points']
        per = {}
        for p, rows in pts.items():
            at = {r['cc']: r['rmsDb'] for r in rows if r.get('rmsDb') is not None}
            if 64 not in at:
                continue
            per[int(p)] = {cc: round(v - at[64], 2) for cc, v in sorted(at.items())}
        out[s] = dict(sorted(per.items()))
    return out


def entry(lab, span, measured, long):
    mid = sorted(span)[len(span) // 2]
    return {
        'instrument': lab,
        'span': {str(p): v for p, v in span.items()},
        'spanMeasured': {str(p): round(v, 2) for p, v in measured.items()},
        'long': {str(p): {str(cc): v for cc, v in per.items()} for p, per in long.items()},
        'midPitch': mid,
        'midStep': round(span[mid] / 7, 2),
    }


def winds(csvPath, windsPath, k):
    """k: the strings' (their own leave-one-out picks k = 1 by 0.06 dB over k = 5, a flat curve, and leaves semitone
    jumps of the span up to 7.6 dB: Clarinet; at k = 5 at most 2.5 dB)"""
    data = spans(csvPath, set(WINDS.values()))
    kOwn, cv = chooseK(data)
    smooth = {lab: {p: round(localLinear(pts, p, k), 2) for p in range(min(pts), max(pts) + 1)}
              for lab, pts in data.items()}
    names = [n for n in WINDS if n in curves(windsPath)['sections']]
    ssoRel = sso(windsPath, names)
    inst = {n: entry(WINDS[n], smooth[WINDS[n]], data[WINDS[n]], ssoRel[n]) for n in names}
    method = {
        'what': 'as the sections: the Meyer rule (rule D without Meyer figures) with one player\'s span (an a2 / a6 patch takes its instrument\'s), long = SSO\'s '
                'All techniques Long (UACC 1), rmsDb less CC1 64\'s',
        'smoothing': 'local linear least squares over +-k semitones, k the strings\' (by their leave-one-out); the winds\' '
                     'own leave-one-out, pooled over their twelve instruments (k = 1 .. %d), picks kOwn by a flat curve '
                     '(cvRmsDb) and leaves semitone jumps of the span up to 7.6 dB (Clarinet): not used' % KMAX,
        'k': k,
        'kOwn': kOwn,
        'cvRmsDb': {str(i): round(v, 3) for i, v in cv.items()},
        'sso': ' + '.join(p.split('/')[-1] for p in windsPath.split(',')) + ' (file names, a later one\'s sections win: since 2026-10-28 '
               'every semitone, sso_winds_cc1_semitone.json), rmsDb (mean power 1.5-3.0 s after the note-on), each pitch '
               'less its CC1 64',
        'noData': NO_DATA,
    }
    return method, inst


def gap(csvPath, gapPath, k, ruleD):
    """the gap patches (GAP_OWN as the winds, GAP_FAMILY their family's Clark & Luce span only)"""
    swept = curves(gapPath)['sections']
    names = [n for n in list(GAP_OWN) + list(GAP_FAMILY) if n in swept]
    ssoRel = sso(gapPath, names)
    data = spans(csvPath, set(GAP_OWN.values()))
    smooth = {lab: {p: round(localLinear(pts, p, k), 2) for p in range(min(pts), max(pts) + 1)}
              for lab, pts in data.items()}
    inst = {}
    for n in names:
        if n in GAP_OWN:
            lab = GAP_OWN[n]
            inst[n] = addRule(entry(lab, smooth[lab], data[lab], ssoRel[n]), ruleD)
            continue
        lab = GAP_FAMILY[n]
        s, src = SPANS[lab]
        ps = sorted(ssoRel[n])
        e = {'instrument': None, 'family': lab,
             'long': {str(p): {str(cc): v for cc, v in per.items()} for p, per in ssoRel[n].items()},
             'midPitch': ps[len(ps) // 2], 'rule': None,
             'sectionSpan': {'db': s, 'source': src + "; the " + lab + "'s, an assumption, not a measurement (plays like "
                                                     "its family; the database has no such instrument)"}}
        md, rd = marks(e, None, s)
        e['marksMid'], e['realDbMid'] = {'spans': md}, {'spans': rd}
        inst[n] = e
    method = {
        'what': 'the 15 patches without a section level before 2026-10-15 (section_balance_cl.py GAP): GAP_OWN as the '
                'winds (their own database instrument, one player\'s span); GAP_FAMILY their family\'s Clark & Luce span '
                'only (an assumption: plays like its family), no realSpan (realRange 1 leaves them alone)',
        'own': GAP_OWN,
        'family': GAP_FAMILY,
        'sso': 'sso_gap_cc1_curves.json + sso_gap_cc1_semitone.json (since 2026-10-28 every semitone, Solo Violin 2 '
               'still every 3: the semitone sweep played Solo Violin 1\'s patch for it), rmsDb (mean power 1.5-3.0 s after '
               'the note-on), each pitch less its CC1 64',
    }
    return method, inst


def main():
    args = [a for a in sys.argv[1:] if a != '--rule-d']
    ruleD = len(args) != len(sys.argv) - 1
    csvPath, curvesPath, outPath = args[:3]
    windsPath = args[3] if len(args) > 3 else None
    gapPath = args[4] if len(args) > 4 else None
    data = spans(csvPath)
    k, cv = chooseK(data)
    smooth = {lab: {p: round(localLinear(pts, p, k), 2) for p in range(min(pts), max(pts) + 1)}
              for lab, pts in data.items()}
    ssoRel = sso(curvesPath)
    sections = {}
    for s, lab in SECTIONS.items():
        span = smooth[lab]
        mid = sorted(span)[len(span) // 2]
        sections[s] = {
            'instrument': lab,
            'span': {str(p): v for p, v in span.items()},
            'spanMeasured': {str(p): round(v, 2) for p, v in data[lab].items()},
            'long': {str(p): {str(cc): v for cc, v in per.items()} for p, per in ssoRel[s].items()},
            'midPitch': mid,
            'midStep': round(span[mid] / 7, 2),
        }
        addRule(sections[s], ruleD)
    out = {
        'method': {
            'what': ('Rule D for every instrument (--rule-d): ' if ruleD else
                     'The Meyer rule (the owner, 2026-10-14) where an entry has "fractions", else rule D: ') +
                    'the TU Berlin database\'s pp / ff (single notes as softly / loudly as possible) are the player\'s '
                    'ppp / fff. Meyer: written pp / ff at fractions a / b of each pitch\'s span above its pp, a = (P_pp - '
                    'X_lo) / (X_hi - X_lo), b = (P_ff - X_lo) / (X_hi - X_lo), X single-note extremes, P rapid two-octave '
                    'scales (J. Meyer, Acoustics and the Performance of Music, Springer 2009, chapter 3; each entry\'s '
                    '"meyer" has the section and figures), p .. f in 5 equal dB steps pp -> ff, ppp -> pp and ff -> fff '
                    'one step each. Rule D (the owner, 2026-10-12): 7 equal steps of span / 7 (a = 1/7, b = 6/7). Both '
                    'relative to mp; span = ff - pp of one player\'s sound power, smoothed; long = SSO\'s All techniques '
                    'Long, rmsDb less CC1 64\'s. marksMid: the player\'s dB from mp at midPitch, both rules; realDbMid: '
                    'less long at the marking\'s CC1 (16 .. 127): MuseScore\'s CC11 before the headroom',
            'meyerNone': MEYER_NONE,
            'meyerSibling': {k: v + "'s fractions: an assumption, not a measurement (the owner, 2026-10-14: plays like "
                                 "its family)" for k, v in SIBLING.items()},
            'real': 'Weinzierl et al. 2018, JASA 144(3), TU Berlin database v3, 3_Audio_Features.csv, SoundPower in dB, '
                    '_et_ files, mean over files per pitch',
            'smoothing': 'local linear least squares over +-k semitones, k by leave-one-out cross-validation pooled '
                         'over violin, viola, cello, double bass (k = 1 .. %d)' % KMAX,
            'k': k,
            'cvRmsDb': {str(i): round(v, 3) for i, v in cv.items()},
            'sso': 'sso_cc1_curves.json, rmsDb (mean power 1.5-3.0 s after the note-on), each pitch less its CC1 64',
        },
        'sections': sections,
    }
    if windsPath:
        wm, wi = winds(csvPath, windsPath, k)
        for e in wi.values():
            addRule(e, ruleD)
        out['methodInstruments'] = wm
        out['instruments'] = wi
        print('winds k', wm['k'], 'own', wm['kOwn'], 'cv', {i: round(float(v), 2) for i, v in wm['cvRmsDb'].items()})
        for n, v in wi.items():
            print(n, 'mid', v['midPitch'], 'span', v['span'][str(v['midPitch'])], 'rule', v['rule'], 'realDb D', v['realDbMid']['D'], 'meyer', v['realDbMid'].get('meyer'))
        if gapPath:
            gm, gi = gap(csvPath, gapPath, k, ruleD)
            out['methodGap'] = gm
            out['instruments'].update(gi)
            for n, v in gi.items():
                print(n, 'mid', v['midPitch'], 'rule', v['rule'], 'S', v['sectionSpan']['db'], 'realDb spans', v['realDbMid']['spans'])
    with open(outPath, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
        f.write('\n')
    print('k', k, 'cv', {i: round(v, 2) for i, v in cv.items()})
    for s, v in sections.items():
        print(s, 'mid', v['midPitch'], 'span', v['span'][str(v['midPitch'])], 'rule', v['rule'], 'realDb D', v['realDbMid']['D'], 'meyer', v['realDbMid'].get('meyer'))


if __name__ == '__main__':
    main()
