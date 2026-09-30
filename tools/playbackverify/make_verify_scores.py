#!/usr/bin/env python3
"""Writes the scores MuseScore --verify-playback checks by default (share/verifyplayback/).

Written for the verification, not taken from anywhere: each passage is a situation where a
hosted sample library has dropped or cut notes before, or might (VERIFY.md):
  Piano pedal chords  chords at pedal changes (SSO's Grand Piano dropped them, 2026-09-28), a
                      chord struck while the same notes ring under the pedal, soft chords, a
                      crescendo, staccato, a pedal that ends on a chord, a pedal at a repeat
  Strings             two parts (each is also rendered alone): long notes through a crescendo,
                      slurred (legato) lines, staccato, pizz./arco, accents at pp and ff

Usage: make_verify_scores.py [folder]   (default: share/verifyplayback next to this repo's tools)
"""
import os
import sys

DIV = 2   # divisions per quarter: an eighth is 1

STEPS = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
TYPES = {1: 'eighth', 2: 'quarter', 3: 'quarter', 4: 'half', 6: 'half', 8: 'whole'}


def pitch_xml(p):
    step, rest = p[0], p[1:]
    alter = 0
    while rest and rest[0] in '#b':
        alter += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    a = f'<alter>{alter}</alter>' if alter else ''
    return f'<pitch><step>{step}</step>{a}<octave>{rest}</octave></pitch>'


def note(pitches, dur, marks=(), slur=None):
    """pitches: 'C4' or 'C4 E4 G4' or 'r'; dur in eighths"""
    typ = TYPES[dur]
    dot = '<dot/>' if dur in (3, 6) else ''
    out = []
    for i, p in enumerate(pitches.split()):
        chord = '<chord/>' if i else ''
        body = '<rest/>' if p == 'r' else pitch_xml(p)
        nots = ''
        if i == 0:
            arts = ''.join(f'<{m}/>' for m in marks)
            s = f'<slur type="{slur}"/>' if slur else ''
            if arts or s:
                nots = '<notations>' + s + (f'<articulations>{arts}</articulations>' if arts else '') + '</notations>'
        out.append(f'<note>{chord}{body}<duration>{dur}</duration><type>{typ}</type>{dot}{nots}</note>')
    return ''.join(out)


def direction(inner, placement='below', sound=''):
    return f'<direction placement="{placement}"><direction-type>{inner}</direction-type>{sound}</direction>'


def dyn(d):
    return direction(f'<dynamics><{d}/></dynamics>')


def pedal(t):
    return direction(f'<pedal type="{t}" line="yes"/>')


def words(w):
    return direction(f'<words>{w}</words>', 'above')


def wedge(t):
    return direction(f'<wedge type="{t}"/>')


def score(title, parts, tempo):
    head = ['<?xml version="1.0" encoding="UTF-8"?>',
            '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">',
            '<score-partwise version="3.1">',
            f'<work><work-title>{title}</work-title></work>',
            '<identification><rights>Written for MuseScore\'s playback verification; public domain (CC0)</rights></identification>',
            '<part-list>']
    for i, (name, sound, clef, measures) in enumerate(parts, 1):
        head.append(f'<score-part id="P{i}"><part-name>{name}</part-name><score-instrument id="P{i}-I1">'
                    f'<instrument-name>{name}</instrument-name><instrument-sound>{sound}</instrument-sound>'
                    f'</score-instrument></score-part>')
    head.append('</part-list>')
    body = []
    for i, (name, sound, clef, measures) in enumerate(parts, 1):
        body.append(f'<part id="P{i}">')
        for n, m in enumerate(measures, 1):
            attrs = ''
            if n == 1:
                sign, line = clef
                attrs = (f'<attributes><divisions>{DIV}</divisions><key><fifths>0</fifths></key><time><beats>4</beats>'
                         f'<beat-type>4</beat-type></time><clef><sign>{sign}</sign><line>{line}</line></clef></attributes>')
                if i == 1:
                    attrs += direction(f'<metronome><beat-unit>quarter</beat-unit><per-minute>{tempo}</per-minute></metronome>',
                                       'above', f'<sound tempo="{tempo}"/>')
            body.append(f'<measure number="{n}">{attrs}{"".join(m)}</measure>')
        body.append('</part>')
    return '\n'.join(head + body + ['</score-partwise>']) + '\n'


def barline(loc, direction_):
    return f'<barline location="{loc}"><bar-style>{"heavy-light" if loc == "left" else "light-heavy"}</bar-style><repeat direction="{direction_}"/></barline>'


def piano():
    prog = [('C3 E4 G4 C5', 'A2 E4 A4 C5'), ('F2 F4 A4 C5', 'G2 D4 G4 B4'),
            ('E3 E4 G4 B4', 'A2 C4 E4 A4'), ('D3 D4 F4 A4', 'G2 B3 D4 G4')]
    ms = []
    # 1-8: half-note chords, the pedal changed at every chord (legato pedalling)
    for k in range(8):
        a, b = prog[k % 4]
        m = []
        if k == 0:
            m += [dyn('mf'), pedal('start')]
        m += [note(a, 4)]
        m += [pedal('change'), note(b, 4)]
        if k < 7:
            m += [pedal('change')]
        else:
            m += [pedal('stop')]
        ms.append(m)
    # 9-12: the same chord struck again while it rings under one pedal, p
    for k in range(4):
        m = []
        if k == 0:
            m += [dyn('p'), pedal('start')]
        m += [note('C3 E4 G4 C5', 2) for _ in range(4)]
        if k == 3:
            m += [pedal('stop')]
        ms.append(m)
    # 13-16: pp chords and a melody, crescendo to f
    mel = ['E5', 'D5', 'C5', 'D5', 'E5', 'F5', 'G5', 'A5', 'G5', 'F5', 'E5', 'D5', 'C5', 'D5', 'E5', 'C5']
    for k in range(4):
        m = []
        if k == 0:
            m += [dyn('pp'), wedge('crescendo')]
        for j in range(4):
            m.append(note(('C3 ' if j == 0 else '') + 'E4 ' + mel[4 * k + j] if j == 0 else mel[4 * k + j], 2))
        if k == 3:
            m += [wedge('stop'), dyn('f')]
        ms.append(m)
    # 17-20: staccato eighths
    run = ['C4', 'E4', 'G4', 'C5', 'G4', 'E4', 'C4', 'G3']
    for k in range(4):
        m = [dyn('mp')] if k == 0 else []
        m += [note(p, 1, ('staccato',)) for p in run]
        ms.append(m)
    # 21-24: ff chords, the pedal changed at each
    for k in range(4):
        a, b = prog[k % 4]
        m = []
        if k == 0:
            m += [dyn('ff'), pedal('start')]
        m += [note(a, 4), pedal('change'), note(b, 4)]
        m += [pedal('change') if k < 3 else pedal('stop')]
        ms.append(m)
    # 25-26: a pedal that ends where a chord starts (it is not taken again)
    ms.append([dyn('mf'), pedal('start'), note('C3 G3 E4', 4), note('F3 A3 C4', 4)])
    ms.append([pedal('stop'), note('G2 G3 B3 D4', 4), note('C3 G3 C4 E4', 4)])
    # 27-28: under a pedal, repeated (a pedal at a repeat's jump)
    ms.append([barline('left', 'forward'), pedal('start'), note('A2 E4 A4 C5', 4), note('D3 F4 A4 D5', 4)])
    ms.append([note('G2 D4 G4 B4', 4), note('C3 E4 G4 C5', 4), pedal('stop'), barline('right', 'backward')])
    # 29-30: the end
    ms.append([pedal('start'), note('F2 C4 F4 A4', 8)])
    ms.append([note('C2 G3 C4 E4 G4 C5', 8), pedal('stop'),
               '<barline location="right"><bar-style>light-heavy</bar-style></barline>'])
    return score('Piano pedal chords', [('Piano', 'keyboard.piano', ('G', 2), ms)], 100)


def strings():
    vn, vc = [], []
    # 1-4: long notes through a crescendo
    for k, (a, b) in enumerate([('G4', 'C3'), ('A4', 'F2'), ('B4', 'G2'), ('C5', 'C3')]):
        vn.append(([dyn('p'), wedge('crescendo')] if k == 0 else []) + [note(a, 8)] + ([wedge('stop'), dyn('f')] if k == 3 else []))
        vc.append(([dyn('p'), wedge('crescendo')] if k == 0 else []) + [note(b, 8)] + ([wedge('stop'), dyn('f')] if k == 3 else []))
    # 5-8: slurred lines (legato)
    line_vn = ['E5', 'D5', 'C5', 'B4', 'A4', 'B4', 'C5', 'D5', 'E5', 'F5', 'G5', 'E5', 'D5', 'C5', 'B4', 'C5']
    line_vc = ['C3', 'D3', 'E3', 'F3', 'G3', 'F3', 'E3', 'D3', 'C3', 'B2', 'A2', 'G2', 'F2', 'G2', 'A2', 'C3']
    for k in range(4):
        for part, line in ((vn, line_vn), (vc, line_vc)):
            m = [dyn('mf')] if k == 0 else []
            for j in range(4):
                s = 'start' if j == 0 else 'stop' if j == 3 else None
                m.append(note(line[4 * k + j], 2, slur=s))
            part.append(m)
    # 9-12: staccato eighths
    for k in range(4):
        vn.append(([dyn('mp')] if k == 0 else []) + [note(p, 1, ('staccato',)) for p in ['G4', 'A4', 'B4', 'C5', 'D5', 'C5', 'B4', 'A4']])
        vc.append(([dyn('mp')] if k == 0 else []) + [note(p, 2, ('staccato',)) for p in ['C3', 'G2', 'C3', 'G2']])
    # 13-14: pizz., 15: arco
    for k in range(2):
        vn.append(([words('pizz.')] if k == 0 else []) + [note(p, 2) for p in ['C5', 'E5', 'G5', 'E5']])
        vc.append(([words('pizz.')] if k == 0 else []) + [note(p, 2) for p in ['C3', 'E3', 'G3', 'E3']])
    vn.append([words('arco'), note('D5', 8)])
    vc.append([words('arco'), note('G2', 8)])
    # 16-19: accents at ff and pp, held halves
    for k, d in enumerate(['ff', 'ff', 'pp', 'pp']):
        vn.append([dyn(d)] + [note(p, 4, ('accent',)) for p in (['E5', 'D5'] if k % 2 == 0 else ['C5', 'B4'])])
        vc.append([dyn(d)] + [note(p, 4, ('accent',)) for p in (['C3', 'G2'] if k % 2 == 0 else ['A2', 'G2'])])
    # 20-22: tenuto quarters, then a long last note
    for k in range(2):
        vn.append(([dyn('mf')] if k == 0 else []) + [note(p, 2, ('tenuto',)) for p in ['C5', 'D5', 'E5', 'F5']])
        vc.append(([dyn('mf')] if k == 0 else []) + [note(p, 2, ('tenuto',)) for p in ['A2', 'B2', 'C3', 'D3']])
    end = '<barline location="right"><bar-style>light-heavy</bar-style></barline>'
    vn.append([note('E5', 8), end])
    vc.append([note('C3', 8), end])
    return score('Strings', [('Violin', 'strings.violin', ('G', 2), vn), ('Violoncello', 'strings.cello', ('F', 4), vc)], 90)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', '..', 'share', 'verifyplayback')
    os.makedirs(out, exist_ok=True)
    for name, text in (('Piano pedal chords.musicxml', piano()), ('Strings.musicxml', strings())):
        with open(os.path.join(out, name), 'w', encoding='utf-8') as f:
            f.write(text)
        print(os.path.join(out, name))


if __name__ == '__main__':
    main()
