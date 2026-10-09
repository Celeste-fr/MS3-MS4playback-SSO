#!/usr/bin/env python3
"""Held-out test scores for the Long (Rachm.) swap (`[slurs] quick` 2): slurred string lines that are NOT the standard
test score, to check the map's Long (Rachm.) onsets (sso_rachm_onset_fit.json) and quickLevel (sso_rachm_levels.json)
away from the passage they were fitted on. Violas and Celli were fitted (commit bdaaed4aa7) on Whence's own lines and
checked on the same lines; Violins 1 / 2 on the violas passage moved by octaves at x4 (onset_fit_from_split.py).

Usage: make_rachm_holdout.py <out folder> [instrument ...]
Writes "Rachm holdout <instrument>.musicxml" and ".notes.json" (every written note: time, length, pitch, line,
figure, slur position, dynamic, whether the swap should take it, whether Whence has the pitch on Rachm.). Convert with
MuseScore 3.6.2 (`mscore -o X.mscx X.musicxml`), then tools/playbackverify/fix_sweep_ids.py X.mscx (the MusicXML
import names the violins "violin"; the map matches "violins"). The .mscx files are committed next to this script in
rachm_holdout/. Deterministic: no randomness, same output every run.

Every parameter and where it comes from:
- Tempo 110 bpm: Whence's own tempo (the passage the fit used). A 32nd is 60/110/8 = 68.18 ms.
- Swap threshold per instrument: the map's Long `peak` (chooseSlurred in libmscore/soundlibrary.cpp swaps a slurred
  note whose written length, the whole tie chain, is under it), read from share/soundlibraries/Spitfire Symphony
  Orchestra.xml: Violins 1 1063, Violins 2 1088, Violas 773, Celli 1193, Basses 1363 ms.
- Length grid, in 32nds at 110: 1, 2, 4, 8 (32nd to quarter), 12 when under the threshold, then k_swap = the longest
  whole number of 32nds under the threshold and k_ctrl = the shortest over it (the control: written just above the
  threshold, so not swapped; one within 5 ms of the threshold is passed over as too close to call). Violins 1 15 / 16
  (1023 / 1091 ms), Violins 2 15 / 17 (1023 / 1159: 16 is 2.9 ms over), Violas 11 / 12 (750 / 818), Celli 17 / 18
  (1159 / 1227), Basses 19 / 21 (1295 / 1432: 20 is 0.6 ms over).
  Lengths that aren't one note value are tied notes (the swap reads the tie chain).
- Pitch range: the keys of sso_rachm_onset_fit.json (where Long (Rachm.) sounds; Violins 2's 98 is silent and left out
  there): Violins 1 / 2 55-97, Violas 48-90, Celli 36-82. Basses have no Long (Rachm.) in All techniques (their
  slurred notes stay on Long): the range of their Long onset table in the map, 24-54.
- Pitches on Rachm. in Whence (dump of the standard test score with the Recommended preset, 2026-10-09; the swapped
  notes of each route): Violins 1 70 72 73 75 76 78 79 82 84 85, Violas 49 51 52 54 55 57 58 60 61 63 64 66 67, Celli
  46 48 49 51 52 54 55 58 60 61; Violins 2 and Basses none. Every other pitch is held out.
- Lines (each starts with a plain Long reference, an unslurred quarter on the beat at the line's first pitch, then a
  quarter rest; a half rest and the next bar between lines; mf unless the line says otherwise):
  A  chromatic over the whole range, up then down, slurs of 4: in 32nds, 16ths and 8ths.
  B  the length axis: X X+2 X+4 X+2 slurred, at every grid length, from starts every 6 semitones of the range.
  C  intervals (3rd m/M, 4th, 5th, octave = 3 4 5 7 12 semitones): X X+i X+2i X+i (arpeggio; X X+i X X+i, a leap,
     where X+2i is out of range), 16ths, from starts every 6 semitones.
  D  the bar-7 figure X X+1 X X-2 (Whence's violas, bar 7), 16ths, slurs of 4, X every 3 semitones.
  E  slur lengths: chromatic 16ths up in slurs of 2 and of 8; then 8 slurred 16ths ending on a note of k_ctrl
     (the slur ends on a note that stays Long), from starts every 6 semitones.
  F  dynamics: the chromatic 16ths up at p and at f, and once more p < f (a crescendo hairpin over the line).
- Starts every 6 / 3 semitones: half / quarter octave, so that 12-TET's pitch classes are each covered within lines B-D.
Render length at 110 bpm: about 4-5 minutes per instrument (printed).

Results: sso_rachm_holdout.json (2026-10-09, real SSO in Kontakt 8 on the Windows test VM, kthost offline renders of
these scores' dumpEvents with the Recommended preset and with quick 0 as the reference; odd / even notes rendered
separately for the per-note loudness detector; method in the file's "method"). Arrival by both detectors (harmonics,
loudness) per instrument, in-Whence against held-out pitches, by line and by pitch, Rasch counts, notes failing both,
a cross-validated per-pitch offset model, and levels against plain Long in context (dB, by length / dynamic; Violas
48-58 and 59-90 apart).
"""
import json
import os
import re
import sys
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
MAP = os.path.join(REPO, "share", "soundlibraries", "Spitfire Symphony Orchestra.xml")
RACHM_FIT = os.path.join(HERE, "sso_rachm_onset_fit.json")

TEMPO = 110
T32 = 60.0 / TEMPO / 8          # seconds per 32nd
DIV = 8                          # MusicXML divisions per quarter: a 32nd is 1
BAR = 32                         # 4/4 in 32nds

# (part name, MusicXML sound, MuseScore id, clef)
INSTRUMENTS = [
    ("Violins 1", "strings.violin", "violins", ("G", 2)),
    ("Violins 2", "strings.violin", "violins", ("G", 2)),
    ("Violas", "strings.viola", "violas", ("C", 3)),
    ("Celli", "strings.cello", "violoncellos", ("F", 4)),
    ("Basses", "strings.contrabass", "contrabasses", ("F", 4)),
]
WHENCE = {"Violins 1": {70, 72, 73, 75, 76, 78, 79, 82, 84, 85},
          "Violas": {49, 51, 52, 54, 55, 57, 58, 60, 61, 63, 64, 66, 67},
          "Celli": {46, 48, 49, 51, 52, 54, 55, 58, 60, 61}}
INTERVALS = [3, 4, 5, 7, 12]
NAMES = ["C", "C", "D", "D", "E", "F", "F", "G", "G", "A", "A", "B"]
ALTER = [0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0]
# note values in 32nds: (type, dotted)
VALUES = [(32, "whole", 0), (24, "half", 1), (16, "half", 0), (12, "quarter", 1), (8, "quarter", 0),
          (6, "eighth", 1), (4, "eighth", 0), (3, "16th", 1), (2, "16th", 0), (1, "32nd", 0)]


def map_info(name):
    """(Long peak ms, Long onset keys, has Long (Rachm.)) of the map's All techniques instrument"""
    root = ET.parse(MAP).getroot()
    for ins in root.iter("Instrument"):
        if ins.get("name") == name:
            peak, keys, rachm = None, [], False
            for a in ins.iter("Articulation"):
                if a.get("name") == "Long":
                    peak = float(a.get("peak"))
                    keys = [int(p.split(":")[0]) for p in a.get("onset").split()]
                if a.get("name") == "Long (Rachm.)":
                    rachm = True
            return peak, keys, rachm
    raise SystemExit(f"{name} not in the map")


def grid(peak):
    k_swap = max(k for k in range(1, 64) if k * T32 * 1000 < peak)
    k_ctrl = min(k for k in range(1, 64) if k * T32 * 1000 > peak)
    if k_ctrl * T32 * 1000 - peak < 5:      # within 5 ms: take the next one
        k_ctrl += 1
    lengths = [k for k in (1, 2, 4, 8, 12) if k < k_swap]
    return lengths + [k_swap, k_ctrl], k_swap, k_ctrl


class Part:
    def __init__(self, name, peak, rachm, whence):
        self.measures = [[]]
        self.pos = 0            # 32nds into the current bar
        self.t = 0              # 32nds from the start
        self.notes = []
        self.name, self.peak, self.rachm, self.whence = name, peak, rachm, whence
        self.dyn = None

    def _room(self):
        if self.pos == BAR:
            self.measures.append([])
            self.pos = 0

    def direction(self, xml):
        self._room()
        self.measures[-1].append(f'<direction placement="below"><direction-type>{xml}</direction-type></direction>')

    def dynamic(self, d):
        self.dyn = d
        self.direction(f"<dynamics><{d}/></dynamics>")

    def _pieces(self, dur):
        """split a length at bar lines and into note values: [(32nds, type, dotted)]"""
        out, pos = [], self.pos
        while dur > 0:
            if pos == BAR:
                pos = 0
            room = BAR - pos
            for v, typ, dot in VALUES:
                if v <= min(dur, room):
                    out.append((v, typ, dot))
                    pos += v
                    dur -= v
                    break
        return out

    def note(self, pitch, dur, slur=None, **info):
        p = (f"<pitch><step>{NAMES[pitch % 12]}</step>{'<alter>1</alter>' if ALTER[pitch % 12] else ''}"
             f"<octave>{pitch // 12 - 1}</octave></pitch>")
        pieces = self._pieces(dur)
        slurred = info.get("slurLen", 0) > 0
        ms = dur * T32 * 1000
        self.notes.append(dict(time=round(self.t * T32, 5), seconds=round(ms / 1000, 5), pitch=pitch,
                               bar=len(self.measures) + (1 if self.pos == BAR else 0), dynamic=self.dyn,
                               swap=bool(slurred and self.rachm and ms < self.peak),
                               inWhence=pitch in self.whence, **info))
        for i, (v, typ, dot) in enumerate(pieces):
            tie = ""
            nots = []
            if len(pieces) > 1:
                if i > 0:
                    tie += '<tie type="stop"/>'
                    nots.append('<tied type="stop"/>')
                if i < len(pieces) - 1:
                    tie += '<tie type="start"/>'
                    nots.append('<tied type="start"/>')
            if slur == "start" and i == 0:
                nots.append('<slur type="start" number="1"/>')
            if slur == "stop" and i == len(pieces) - 1:
                nots.append('<slur type="stop" number="1"/>')
            n = "<notations>" + "".join(nots) + "</notations>" if nots else ""
            self._room()
            self.measures[-1].append(f"<note>{p}<duration>{v}</duration>{tie}<type>{typ}</type>"
                                     f"{'<dot/>' if dot else ''}{n}</note>")
            self.pos += v
            self.t += v

    def rest(self, dur):
        for v, typ, dot in self._pieces(dur):
            self._room()
            self.measures[-1].append(f"<note><rest/><duration>{v}</duration><type>{typ}</type>{'<dot/>' if dot else ''}</note>")
            self.pos += v
            self.t += v

    def to_bar(self):
        if 0 < self.pos < BAR:
            self.rest(BAR - self.pos)

    # a line: reference Long, quarter rest, the slurred groups, then a half rest and the next bar
    def line(self, name, groups, dyn="mf", hairpin=None):
        self.to_bar()
        if dyn != self.dyn:
            self.dynamic(dyn)
        first = groups[0][0][0][0]
        self.note(first, 8, line=name, figure="reference", slurPos=0, slurLen=0, reference=True)
        self.rest(8)
        if hairpin:
            self.direction('<wedge type="crescendo"/>')
        for g, figure in groups:
            for i, (pitch, dur) in enumerate(g):
                slur = "start" if i == 0 else "stop" if i == len(g) - 1 else None
                if len(g) == 1:
                    slur = None
                self.note(pitch, dur, slur=slur, line=name, figure=figure, slurPos=i + 1, slurLen=len(g) if len(g) > 1 else 0,
                          reference=False)
        if hairpin:
            self.direction('<wedge type="stop"/>')
            self.dynamic(hairpin)
        self.rest(16)


def chunks(seq, n):
    return [seq[i:i + n] for i in range(0, len(seq), n) if len(seq[i:i + n]) >= 2]


def build(spec):
    name, sound, mid, clef = spec
    peak, long_keys, rachm = map_info(name)
    if rachm:
        keys = sorted(int(k) for k in json.load(open(RACHM_FIT, encoding="utf-8"))[name]["onsets"])
    else:
        keys = long_keys
    lo, hi = min(keys), max(keys)
    lengths, k_swap, k_ctrl = grid(peak)
    part = Part(name, peak, rachm, WHENCE.get(name, set()))
    part.dynamic("mf")
    part.rest(16)                     # lead-in: room for early starts
    part.to_bar()
    up = list(range(lo, hi + 1))
    updown = up + up[-2::-1]
    starts6 = list(range(lo, hi + 1, 6))
    starts3 = list(range(lo, hi + 1, 3))

    def fig(groups, figure):
        return [[g, figure] for g in groups]
    # A: chromatic up and down, slurs of 4
    for dur, label in ((1, "32nd"), (2, "16th"), (4, "8th")):
        part.line(f"A {label}", fig(chunks([(p, dur) for p in updown], 4), "chromatic"))
    # B: the length axis
    for k in lengths:
        groups = []
        for s in starts6:
            ps = [s, s + 2, s + 4, s + 2]
            if ps[2] > hi:
                ps = [s, s - 2, s - 4, s - 2]
            groups.append([(p, k) for p in ps])
        part.line(f"B {k}", fig(groups, f"length {k}"))
    # C: intervals
    for iv in INTERVALS:
        groups = []
        for s in starts6:
            if s + iv > hi:
                continue
            if s + 2 * iv <= hi:
                groups.append([[(s, 2), (s + iv, 2), (s + 2 * iv, 2), (s + iv, 2)], f"arpeggio {iv}"])
            else:
                groups.append([[(s, 2), (s + iv, 2), (s, 2), (s + iv, 2)], f"leap {iv}"])
        part.line(f"C {iv}", groups)
    # D: X X+1 X X-2
    groups = [[(x, 2), (x + 1, 2), (x, 2), (x - 2, 2)] for x in starts3 if lo <= x - 2 and x + 1 <= hi]
    part.line("D", fig(groups, "bar 7"))
    # E: slur lengths
    for n in (2, 8):
        part.line(f"E {n}", fig(chunks([(p, 2) for p in up], n), f"slur {n}"))
    groups = []
    for s in starts6:
        ps = [p for p in range(s, s + 8)] if s + 8 <= hi else [p for p in range(s, s - 8, -1)]
        last = ps[-1] + (1 if ps[-1] > ps[0] else -1)
        groups.append([(p, 2) for p in ps] + [(last, k_ctrl)])
    part.line("E long end", fig(groups, "long end"))
    # F: dynamics
    line = chunks([(p, 2) for p in up], 4)
    part.line("F p", fig(line, "chromatic"), dyn="p")
    part.line("F f", fig(line, "chromatic"), dyn="f")
    part.line("F cresc", fig(line, "chromatic"), dyn="p", hairpin="f")
    part.to_bar()
    part.rest(BAR)

    xml = ['<?xml version="1.0" encoding="UTF-8"?>',
           '<!DOCTYPE score-partwise PUBLIC "-//Recordare//DTD MusicXML 3.1 Partwise//EN" "http://www.musicxml.org/dtds/partwise.dtd">',
           '<score-partwise version="3.1">', f"<work><work-title>Rachm holdout {name}</work-title></work>",
           "<identification><rights>Written for MuseScore's playback checks; public domain (CC0)</rights></identification>",
           f'<part-list><score-part id="P1"><part-name>{name}</part-name><score-instrument id="P1-I1"><instrument-name>{name}'
           f"</instrument-name><instrument-sound>{sound}</instrument-sound></score-instrument></score-part></part-list>",
           '<part id="P1">']
    for n, m in enumerate(part.measures, 1):
        attrs = ""
        if n == 1:
            attrs = (f"<attributes><divisions>{DIV}</divisions><key><fifths>0</fifths></key><time><beats>4</beats>"
                     f"<beat-type>4</beat-type></time><clef><sign>{clef[0]}</sign><line>{clef[1]}</line></clef></attributes>"
                     '<direction placement="above"><direction-type><metronome><beat-unit>quarter</beat-unit>'
                     f'<per-minute>{TEMPO}</per-minute></metronome></direction-type><sound tempo="{TEMPO}"/></direction>')
        xml.append(f'<measure number="{n}">{attrs}{"".join(m)}</measure>')
    xml += ["</part>", "</score-partwise>"]
    meta = dict(part=name, id=mid, tempo=TEMPO, peakMs=peak, range=[lo, hi], rachm=rachm, lengths32=lengths,
                kSwap=k_swap, kCtrl=k_ctrl, whence=sorted(part.whence), seconds=round(part.t * T32, 3), notes=part.notes)
    return "\n".join(xml) + "\n", meta


def main():
    out = sys.argv[1]
    want = sys.argv[2:]
    os.makedirs(out, exist_ok=True)
    for spec in INSTRUMENTS:
        if want and spec[0] not in want:
            continue
        xml, meta = build(spec)
        base = os.path.join(out, "Rachm holdout " + spec[0])
        open(base + ".musicxml", "w", encoding="utf-8").write(xml)
        json.dump(meta, open(base + ".notes.json", "w", encoding="utf-8"), indent=0)
        sw = sum(n["swap"] for n in meta["notes"])
        held = sum(n["swap"] and not n["inWhence"] for n in meta["notes"])
        print(f"{base}: {len(meta['notes'])} notes, {sw} to swap ({held} held out), {meta['seconds']:.0f} s, "
              f"lengths {meta['lengths32']} (32nds)")


if __name__ == "__main__":
    main()
