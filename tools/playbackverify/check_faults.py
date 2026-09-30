#!/usr/bin/env python3
"""The test synth runs of --verify-playback (try_with_testsynth.sh): set-up, and the check.

  check_faults.py --setup <work dir>   the map VerifyTest.xml and the patches' setups for the
                                       MS Test Synth (under <work dir>/home)
  check_faults.py <out dir>            each run's faults (MS_VERIFY_FAULT_LOG) against its report:
                                       found, missed, and findings no fault explains

A fault log has one line per injected fault, "<kind> <seconds> <slot> <pitch>", and "begin" at
each export (the verification renders a score with several parts once mixed, then each part
alone; a score of one part once). A dropped note-on is expected as a missing attack or note when it
left its strike (the notes starting together) with no note (one dropped from a chord is counted
apart: found when its own partials show it); a truncated note as cut short when it is held
(a long or legato articulation), long enough for the check (0.3 s, cut before 70 % of it) and has
partials no other note has (notes.tsv cutBins).
"""
import csv
import glob
import json
import os
import struct
import sys

MAP = """<?xml version="1.0" encoding="UTF-8"?>
<!-- the MS Test Synth as a sound library, for try_with_testsynth.sh -->
<SoundLibrary name="VerifyTest">
  <Plugin files="mstestsynth.vst3"/>
  <Switch type="cc" number="32"/>
  <Dynamics cc="1"/>
  <Instrument name="Piano" ids="piano grand-piano">
    <Articulation name="Long" value="1" techniques="long legato short staccato staccatissimo"/>
  </Instrument>
  <Instrument name="Violin" ids="violin violins">
    <Articulation name="Long" value="1" techniques="long legato"/>
    <Articulation name="Short" value="42" techniques="short staccato staccatissimo spiccato"/>
    <Articulation name="Pizzicato" value="56" techniques="pizzicato"/>
  </Instrument>
  <Instrument name="Cello" ids="violoncello cello violoncellos">
    <Articulation name="Long" value="2" techniques="long legato"/>
    <Articulation name="Short" value="43" techniques="short staccato staccatissimo spiccato"/>
    <Articulation name="Pizzicato" value="57" techniques="pizzicato"/>
  </Instrument>
</SoundLibrary>
"""


def qstring(s):
    b = s.encode('utf-16-be')
    return struct.pack('>I', len(b)) + b


def qbytes(b):
    return struct.pack('>I', len(b)) + b


def setup(work):
    with open(os.path.join(work, 'VerifyTest.xml'), 'w') as f:
        f.write(MAP)
    folder = os.path.join(work, 'home', '.local', 'share', 'MuseScore', 'MuseScore3Evo', 'soundlibraries', 'VerifyTest')
    os.makedirs(folder, exist_ok=True)
    for name, art in (('Piano', 1), ('Violin', 1), ('Cello', 2)):
        component = struct.pack('<dd', art / 127.0, 1.0)   # the test synth's state: articulation, level
        state = b'MSV3' + struct.pack('>I', 1) + qstring('MS Test Synth') + qbytes(component) + qbytes(b'')
        with open(os.path.join(folder, name + '.vst3state'), 'wb') as f:
            f.write(state)


def renders(log):
    """the faults per export: [[(kind, t, slot, pitch)], …]"""
    out = []
    if not os.path.exists(log):
        return out
    for line in open(log):
        p = line.split()
        if p[0] == 'begin':
            out.append([])
        elif out:
            out[-1].append((p[0], float(p[1]), int(p[2]), int(p[3])))
    return out


def notes_of(folder, prefix):
    path = os.path.join(folder, prefix + ' notes.tsv')
    return list(csv.DictReader(open(path, encoding='utf-8'), delimiter='\t')) if os.path.exists(path) else []


def strikes(folder, prefix):
    path = os.path.join(folder, prefix + ' strikes.tsv')
    return list(csv.DictReader(open(path, encoding='utf-8'), delimiter='\t')) if os.path.exists(path) else []


NAMES = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B']


def name(p):
    return NAMES[p % 12] + str(p // 12 - 1)


def check(out):
    total_missed = total_extra = 0
    for run in sorted(os.listdir(out)):
        rdir = os.path.join(out, run)
        if not os.path.isdir(rdir):
            continue
        reports = sorted(glob.glob(os.path.join(rdir, 'Playback verify *', 'report.json')))
        if not reports:
            print(f'{run}: no report')
            continue
        folder = os.path.dirname(reports[-1])
        rep = json.load(open(reports[-1]))
        exports = renders(os.path.join(out, run + ' faults.txt'))
        k = 0
        lines = []
        found = expected = injected = other = other_found = 0
        for sc in rep['scores']:
            parts = sc.get('parts', [])
            base = os.path.splitext(sc['file'])[0]
            single = len(parts) == 1
            mix = exports[k] if k < len(exports) else []
            k += 1
            for pi, part in enumerate(parts):
                faults = mix if single else (exports[k] if k < len(exports) else [])
                if not single:
                    k += 1
                prefix = base if single else base + ' ' + part['name']
                st = strikes(folder, prefix)
                byTime = {}
                for s in st:
                    byTime.setdefault(round(float(s['time']), 2), s)
                findings = part['findings']
                used = set()
                # the faults of a strike together (a chord's notes dropped at once)
                groups = {}
                for f in faults:
                    groups.setdefault((f[0], round(f[1], 2)), []).append(f)
                notes = {}
                for n in notes_of(folder, prefix):
                    notes.setdefault((round(float(n['time']), 2), n['pitch']), n)
                for (kind, t), fs in sorted(groups.items(), key=lambda x: x[0][1]):
                    injected += len(fs)
                    s = byTime.get(t) or byTime.get(round(t - 0.01, 2)) or byTime.get(round(t + 0.01, 2))
                    pitches = s['pitches'].split() if s else []
                    if kind in ('drop', 'pedal-drop'):
                        want = ('missing-attack', 'missing-note')
                        detectable = s is not None and len(set(name(f[3]) for f in fs)) >= len(set(pitches))
                    else:
                        want = ('cut-short',)
                        n = notes.get((t, name(fs[0][3]))) or notes.get((round(t - 0.01, 2), name(fs[0][3]))) or {}
                        length = float(n.get('length', 0))
                        cut = 0.12
                        detectable = (length >= 0.3 and 0.7 * length - 0.05 > cut + 0.05 and n.get('sustained') == '1'
                                      and int(n.get('cutBins', 0)) >= 3)
                    hit = [i for i, x in enumerate(findings) if x['kind'] in want and abs(x['time'] - t) < 0.04]
                    used.update(hit)
                    if detectable:
                        expected += 1
                        found += bool(hit)
                        if not hit:
                            lines.append(f'   MISSED {kind} {t:.3f} s {part["name"]} {" ".join(name(f[3]) for f in fs)}')
                            total_missed += 1
                    else:
                        other += 1
                        other_found += bool(hit)
                # a silence where a dropped or cut note should sound is the same fault
                for i, x in enumerate(findings):
                    if x['kind'] == 'silence' and any(x['time'] - 1.0 <= f[1] <= x['time'] + x.get('length', 0) for f in faults):
                        used.add(i)
                for i, x in enumerate(findings):
                    if i not in used:
                        lines.append(f'   EXTRA {x["kind"]} {x["time"]:.3f} s m. {x.get("measure")} {part["name"]} '
                                     f'{x.get("pitchNames", "")}: {x["text"]}')
                        total_extra += 1
        print(f'{run}: {injected} faults injected; {expected} detectable, {found} found; {other} others '
              f'(a note of a chord dropped, a note too short or with no partials of its own), {other_found} of them found; '
              f'{rep["findingCount"]} findings in all ({rep["verdict"]})')
        for l in lines:
            print(l)
    print(f'missed {total_missed}, findings no fault explains {total_extra}')
    return total_missed == 0 and total_extra == 0


if __name__ == '__main__':
    if len(sys.argv) == 3 and sys.argv[1] == '--setup':
        setup(sys.argv[2])
    elif len(sys.argv) == 2:
        sys.exit(0 if check(sys.argv[1]) else 1)
    else:
        print(__doc__)
        sys.exit(2)
