#!/usr/bin/env python3
"""A dynamics ladder for the five string sections ([levels] realRange, contextlevel.h › Real range).

    python3 -I make_dynamics_ladder.py <out.mscx> [playbackSettings metaTag, e.g. "levels/realRange=1"]

Violins 1, Violins 2, Violas, Violoncellos, Contrabasses (MuseScore's section instruments: SSO's All techniques
patches), 60 bpm, a bar's rest, then for each written dynamic ppp pp p mp mf f ff fff: a whole note held (bar a) and
four staccato quarters (bar b), at each section's dynamics-check pitch (E5, E5, A4, B3, Eb2: the map's <Instrument
pitch>). Rendered by --verify-playback (each part alone too), the held notes' sustain gives each section's loudness per
dynamic: its spread ppp -> fff against rule D's target (the player's measured ff - pp at that pitch), the staccatos the
shorts' against the held notes'. Written for MuseScore's playback verification; public domain (CC0).
"""
import sys
from xml.sax.saxutils import escape

PARTS = [('Violins 1', 'Vlns. 1', 'violins', 76, 'G', 2), ('Violins 2', 'Vlns. 2', 'violins', 76, 'G', 2),
         ('Violas', 'Vlas.', 'violas', 69, 'C', 3), ('Violoncellos', 'Vcs.', 'violoncellos', 59, 'F', 4),
         ('Contrabasses', 'Cbs.', 'contrabasses', 39, 'F', 4)]
DYN = ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff']
VELO = {'ppp': 16, 'pp': 33, 'p': 49, 'mp': 64, 'mf': 80, 'f': 96, 'ff': 112, 'fff': 126}
CLEF = {'G': 'G', 'C': 'C3', 'F': 'F'}
TPC = {76: 18, 69: 17, 59: 19, 39: 11}           # E, A, B, Eb (tonal pitch class)


def note(pitch, dur, stacc=False):
    a = ('<Articulation><subtype>articStaccatoAbove</subtype></Articulation>' if stacc else '')
    return (f'<Chord><durationType>{dur}</durationType>{a}<Note><pitch>{pitch}</pitch><tpc>{TPC[pitch]}</tpc></Note></Chord>')


def staff(i, pitch, clef):
    out = [f'<Staff id="{i + 1}">']
    first = (f'<Clef><concertClefType>{CLEF[clef]}</concertClefType><transposingClefType>{CLEF[clef]}</transposingClefType></Clef>'
             '<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>')
    if i == 0:
        first += '<Tempo><tempo>1</tempo><followText>1</followText><text>q = 60</text></Tempo>'
    out.append(f'<Measure><voice>{first}<Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure>')
    for d in DYN:
        dyn = f'<Dynamic><subtype>{d}</subtype><velocity>{VELO[d]}</velocity></Dynamic>'
        out.append(f'<Measure><voice>{dyn}{note(pitch, "whole")}</voice></Measure>')
        out.append('<Measure><voice>' + ''.join(note(pitch, 'quarter', True) for _ in range(4)) + '</voice></Measure>')
    out.append('</Staff>')
    return '\n'.join(out)


def part(name, short, iid, clef):
    return (f'<Part><Staff id="1"><StaffType group="pitched"><name>stdNormal</name></StaffType>'
            f'<defaultClef>{CLEF[clef]}</defaultClef></Staff><trackName>{escape(name)}</trackName>'
            f'<Instrument id="{iid}"><longName>{escape(name)}</longName><shortName>{escape(short)}</shortName>'
            f'<trackName>{escape(name)}</trackName><instrumentId>strings.group</instrumentId>'
            f'<Channel name="arco"><program value="48"/><synti>Fluid</synti></Channel></Instrument></Part>')


def main():
    out = sys.argv[1]
    settings = sys.argv[2] if len(sys.argv) > 2 else ''
    parts = '\n'.join(part(n, s, iid, c) for n, s, iid, _, c, _ in PARTS)
    staves = '\n'.join(staff(i, p, c) for i, (_, _, _, p, c, _) in enumerate(PARTS))
    meta = f'<metaTag name="playbackSettings">{escape(settings)}</metaTag>' if settings else ''
    text = (f'<?xml version="1.0" encoding="UTF-8"?>\n<museScore version="3.02"><programVersion>3.6.2</programVersion>'
            f'<Score><Division>480</Division>{meta}<metaTag name="workTitle">Dynamics ladder</metaTag>\n'
            f'{parts}\n{staves}\n</Score></museScore>\n')
    with open(out, 'w', encoding='utf-8') as f:
        f.write(text)


if __name__ == '__main__':
    main()
