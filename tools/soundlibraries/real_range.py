#!/usr/bin/env python3
"""The real player's dynamic range against SSO's, for [levels] realRange (libmscore/contextlevel.h › Real range).

    python3 -I real_range.py <3_Audio_Features.csv> <sso_cc1_curves.json> <out sso_real_range.json>

Stage A of the dynamics plan (HANDOFF.md › Dynamics like a real player; the owner, 2026-10-12): each note of SSO's five
string sections as loud as a real player at its written dynamic, the player's full range, by CC11 and the patches'
volumes; CC1 (the recording, mostly tone) stays MuseScore's.

Real side: Weinzierl, Lepa, Schultz, Detzner, von Coler & Behler 2018, "Sound power and timbre as cues for the dynamic
strength of orchestral instruments", JASA 144(3) (TU Berlin database v3, 3_Audio_Features.csv: column "SoundPower in
dB" of single tones, modern instruments, chromatic "_et_" files, pp and ff; the mean where a pitch has several files).
Violin for Violins 1 and 2, viola, cello, double bass. The CSV is CC BY-NC-SA 4.0 and not in the repository: only
derived numbers are written.

Rule D (the owner, 2026-10-12): the database's pp and ff ("as softly / loudly as possible") are the player's ppp and
fff; the markings between them in 7 equal dB steps of (ff - pp) / 7 at each pitch, relative to mp (where the section
balance M was set). So only the span ff - pp per pitch matters. The span is smoothed over pitch (single takes: a
semitone's span jumps by several dB): a local linear least-squares fit over the pitches within +-k semitones of each
pitch (uniform weights), k chosen by leave-one-out cross-validation (the k with the least squared error predicting
each measured span from the others, pooled over the four instruments, k = 1 .. 12). Written at every semitone of the
database's range per instrument; MuseScore takes the nearest end's beyond it.

SSO side: sso_cc1_curves.json (Windows test VM, Kontakt 8 + SSO, 2026-10-11): All techniques Long at CC1 = velocity
16 .. 127, every 3 semitones, median of 4. The measure: rmsDb, the mean power of the sustain 1.5-3.0 s after the
note-on (like for like with the database's sound power of a held tone; loudDb, the loudest 50 ms, is the dynamics
check's peak measure). Written as each measured pitch's level less its level at CC1 64 (mp), unsmoothed (the repeats'
spread, ~1 dB, is the noise; between pitches MuseScore interpolates linearly).
"""
import csv
import json
import sys

SECTIONS = {'Violins 1': 'Violin modern', 'Violins 2': 'Violin modern', 'Violas': 'Viola modern',
            'Celli': 'Cello modern', 'Basses': 'Double bass modern'}
KMAX = 12


def spans(path):
    """{database label: {midi: ff - pp}} (pitches with both)"""
    pc = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}
    rows = list(csv.reader(open(path, encoding='utf-8')))
    head = rows[1]
    col = head.index('SoundPower in dB')
    out = {}
    for lab in set(SECTIONS.values()):
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


def sso(path):
    d = json.load(open(path, encoding='utf-8'))
    out = {}
    for s in SECTIONS:
        pts = d['sections'][s]['points']
        per = {}
        for p, rows in pts.items():
            at = {r['cc']: r['rmsDb'] for r in rows if r.get('rmsDb') is not None}
            if 64 not in at:
                continue
            per[int(p)] = {cc: round(v - at[64], 2) for cc, v in sorted(at.items())}
        out[s] = dict(sorted(per.items()))
    return out


def main():
    csvPath, curvesPath, outPath = sys.argv[1:4]
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
    out = {
        'method': {
            'what': 'Rule D (the owner, 2026-10-12): the TU Berlin database\'s pp / ff are the player\'s ppp / fff, '
                    'the markings between in 7 equal dB steps of span / 7 at each pitch, relative to mp; span = ff - pp '
                    'of one player\'s sound power, smoothed; long = SSO\'s All techniques Long, rmsDb less CC1 64\'s',
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
    with open(outPath, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1)
        f.write('\n')
    print('k', k, 'cv', {i: round(v, 2) for i, v in cv.items()})
    for s, v in sections.items():
        print(s, 'mid', v['midPitch'], 'span', v['span'][str(v['midPitch'])], 'step', v['midStep'])


if __name__ == '__main__':
    main()
