#!/usr/bin/env python3
"""The All techniques patches' Long onset, fitted so that the sections' slurred notes arrive together
(sso_long_onset_fit.json, read by gen_spitfire_sso.py).

The owner, 2026-10-07: plain Long for every slurred note, the sections within Rasch 1979's 30-50 ms between
players. Measured on the Windows test VM (real SSO in Kontakt 8, offline renders through kthost; branch
legato-pair-delays):
- isolated: every semitone of each patch's Long (CC32 1), four times at velocity 64, CC1 64, 1.5 s apart; the
  onset the map's way (15 dB under the perceived-loudness peak, less 30 ms): --isolated, {code: {pitch: {median}}}
- split: Whence's violas, bars 3-7 (80 slurred sixteenths at 110 bpm, three passes; slow: every duration x4), moved
  by octaves into each patch's range, every note started early by the map's onset, odd notes on one instance and
  even on another (so no note's attack is masked by the one before); each note's arrival on its own instance the
  same way as isolated: --split, meta_Q_<code>.json (note times) and late_S_<code>.json (arrival - written, ms)

A note's true onset is its early start plus its measured arrival. For FIT, onset(pitch) = the isolated median at
that pitch + the section's offset (median over the passage of true onset - isolated): it follows the pitch where the
map's smoothed table didn't (the arrival's SD followed pitch: Oboe r 0.94, Tuba 0.90, Violins 1 0.83). Taken only
where both detectors (loudness, and the new pitch's own harmonics) gave a smaller SD with it: replaying the passage's
notes, pairs of the 12 sections within 50 ms went 40 -> 55 of 66 (loudness), 58 -> 59 (harmonics), the fast run
15 of 15 (the pairs sharing 30 or more readable notes). For SHIFT only the offset (the median arrival, early under
both detectors on the slow run): the map's own table moved by it. Basses' isolated onsets vary 0-305 ms (SD 37 over
four repeats) and made the fit worse.

--smooth 1: each pitch's isolated median is the median of those within a semitone. The isolated repeats of one
pitch alternate between round-robin variants (Oboe 76: 140/251/158/250 ms), so a neighbour's reading steadies it.
Replayed from the split run: slow 55 of 66 pairs within 50 ms (loudness), 62 of 66 (harmonics, was 59), fast 15 of
15; +-2 and +-3 were worse (loudness 46 and 39, fast 14 of 15).
Batch 6 (the unsmoothed fit rendered on the VM) matched its replay: slow 56 of 66 (loudness, median SD 29 ms),
59 of 66 (harmonics, 28), fast 16 of 17 (25).

--sound "Long (Rachm.)" (2026-10-08, sso_rachm_onset_fit.json): the swap `[slurs] quick` makes for slurred notes
shorter than Long's peak, on Violins 1 and Violins 2 (All techniques patches, CC32 16). Measured the same way (VM,
real SSO in Kontakt 8, kthost offline): isolated every semitone 55-97 / 55-98, four repeats (repeat SD median 2 ms
on both, where Violins 2's Long alternates between two round robins about 60 ms apart, SD 18); split: the violas
passage slow, early by Long's map median (Violins 1 54.5, Violins 2 152.5). Violins 2 at 98 is silent in both
Long and Rachm. (no onset readable): left out. Section offsets: the passage's true onsets (early + arrival) have
median 66 ms (Violins 1) and 60 ms (Violins 2) under the loudness detector. Replayed (arrival - fitted onset over
the 180 passage notes): per pitch SD 5 ms (Violins 1) / 19 (Violins 2) under loudness, 12 / 22 under harmonics;
the fit's median at every pitch 25 / 32 and 31 / 30, so per pitch is kept. The same fit of Violins 2's Long
replayed worse per pitch (35 vs 24 loudness, 57 vs 41 harmonics): that patch's Long isn't fitted here.
Rendered: Whence's Violins 1 line, bars 5-16, three passes, on each patch (slurred on Rachm., odd/even notes on two
instances; arrival SD of the readable slurred notes, loudness / harmonics): early by Long's map median 21.6 / 22.5
(Violins 1), and Violins 2 arrives 97 ms early; per pitch 10.8 / 12.6 and 11.4 / 22.3; the fit's median
22.0 / 22.2 and 22.6 / 15.9. Plain Long with its fit (reference) 10.4 / 41.9 and 11.8 / 25.9. Per pitch arrives
15 ms (loudness) to 30 ms (harmonics) before the reference on Violins 1, level with it on Violins 2.
sso_rachm_levels.json: Rachm. minus Long, perceived-loudness peak in the first second, by pitch and held length
(ms), mean of two repeats, same VM renders.

--sound "Long (Rachm.)" --context (2026-10-09): Violas and Celli (vla, vc; the other entries of the file are kept:
a patch not in --isolated isn't refitted). Same VM, real SSO, kthost. Isolated every semitone 48-90 / 36-82 as above
(repeat SD median 2 / 3 ms), less the readings whose start the note before still masked (its tail within 15 dB of
this note's peak, 3 / 8 of 172 / 188; Celli 44 has no reading). The slow split run (violas passage x4, early by Long's
map median 59 / 117.5) didn't carry over to the score's tempo: its true onsets (violas 35-189 ms by pitch) were 2-3x
what the same pitches showed at tempo, and per pitch against the fit's median it was better under one detector and
worse under the other (violas 43.5 vs 52.2 loudness, 57.9 vs 49.9 harmonics). So --context: --split holds the
patch's own line of the standard test score at its tempo (the dumped route as MuseScore plays it with [slurs] quick
2, odd / even notes on two instances, the swapped notes only, three renders with different early starts: the per
note spread of the readings median 13 / 5 ms), each pitch the median of its readings within a semitone under both
detectors, a reading before the note starts dropped (the overlap in bar 7); pitches the line doesn't reach: isolated
+ the offset (median of at tempo - isolated). Violas 33-129 ms (median 66), Celli 7-161 (median 107). Rendered again
with them (the same line): the violas' Rachm. notes arrive -5 ms (loudness, SD 10; harmonics +3, SD 56), were -78
(SD 19) and -60 (SD 50); the other notes -10. Celli: loudness reads 2 of 120, harmonics +11 (SD 32), was -11 (SD 30).

The arrivals (late_S_<code>.json: loudness, hlate_S_<code>.json: harmonics) come from onset_detect.py (moved there
from the scratch fam11.py unchanged, 2026-10-09; its accuracy against synthetic notes: onset_detect_accuracy.json).
--wavs <folder>: measure them here first from <folder>/fam_AQ_<code>.wav and fam_BQ_<code>.wav (the two instances'
renders of meta_Q_<code>.json) and write them into --split.
"""
import argparse
import json
import os
import statistics

import onset_detect

PATCH = {'vln1': 'Violins 1', 'vln2': 'Violins 2', 'vla': 'Violas', 'vc': 'Celli', 'cb': 'Basses', 'fl': 'Flute Solo', 'ob': 'Oboe Solo',
         'cl': 'Clarinet Solo', 'bn': 'Bassoon Solo', 'hn': 'Horn Solo', 'tpt': 'Trumpet Solo',
         'tbn': 'Tenor Trombone Solo', 'tba': 'Tuba Solo'}
FIT = ['vln1', 'vla', 'fl', 'ob', 'cl', 'tba']
SHIFT = ['tbn', 'cb']
# --sound: the articulation fitted, its patches (fitted per pitch / shifted) and its file
SOUNDS = {'Long': (FIT, SHIFT, 'sso_long_onset_fit.json'),
          'Long (Rachm.)': (['vln1', 'vln2', 'vla', 'vc'], [], 'sso_rachm_onset_fit.json')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isolated', required=True)
    ap.add_argument('--split', required=True, help='folder with meta_Q_<code>.json and late_S_<code>.json')
    ap.add_argument('--smooth', type=int, default=1, help='median of the isolated medians within this many semitones')
    ap.add_argument('--sound', default='Long', choices=sorted(SOUNDS), help='the articulation fitted')
    ap.add_argument('--out', help='default: the sound\'s file next to this script')
    ap.add_argument('--context', action='store_true',
                    help='--split is a run at the score\'s own tempo: each pitch takes the median true onset of the '
                         'run\'s notes within --smooth semitones (late_S and, when there, hlate_S: both detectors; a '
                         'reading before the note starts is dropped); pitches without: isolated + offset')
    ap.add_argument('--wavs', help='folder with fam_AQ_<code>.wav / fam_BQ_<code>.wav: measure late_S / hlate_S '
                                   'into --split with onset_detect.py first')
    a = ap.parse_args()
    fit, shift, name = SOUNDS[a.sound]
    out_path = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), name)
    iso = json.load(open(a.isolated))
    # a patch not in --isolated keeps its entry in the output file (measured in an earlier run)
    out = json.load(open(out_path)) if os.path.exists(out_path) else {}
    for code in fit + shift:
        if code not in iso:
            continue
        table = {int(p): v['median'] for p, v in iso[code].items()}
        table = {p: statistics.median(table[q] for q in table if abs(q - p) <= a.smooth) for p in table}
        if a.wavs:
            args = (os.path.join(a.split, f'meta_Q_{code}.json'), os.path.join(a.wavs, f'fam_AQ_{code}.wav'),
                    os.path.join(a.wavs, f'fam_BQ_{code}.wav'))
            onset_detect.measure_files('loudness', *args, os.path.join(a.split, f'late_S_{code}.json'))
            onset_detect.measure_files('harmonics', *args, os.path.join(a.split, f'hlate_S_{code}.json'))
        meta = {str(x['i']): x for x in json.load(open(os.path.join(a.split, f'meta_Q_{code}.json')))}
        late = json.load(open(os.path.join(a.split, f'late_S_{code}.json')))
        if code in shift:
            out[PATCH[code]] = {'shift': round(statistics.median(late.values()))}
            continue
        nearest = lambda p: table[min(table, key=lambda k: abs(k - p))]
        if a.context:
            hpath = os.path.join(a.split, f'hlate_S_{code}.json')
            reads = [(meta[i]['pitch'], (meta[i]['written'] - meta[i]['on']) * 1000 + ms)
                     for d in (late, json.load(open(hpath)) if os.path.exists(hpath) else {}) for i, ms in d.items()]
            reads = [(p, t) for p, t in reads if t >= 0]
            off = statistics.median(t - nearest(p) for p, t in reads)
            own = {p: statistics.median(t for q, t in reads if abs(q - p) <= a.smooth) for p in table
                   if any(abs(q - p) <= a.smooth for q, _ in reads)}
            out[PATCH[code]] = {'onsets': {str(p): max(0, round(own[p] if p in own else table[p] + off))
                                           for p in sorted(table)}}
            continue
        off = statistics.median((meta[i]['written'] - meta[i]['on']) * 1000 + ms - nearest(meta[i]['pitch'])
                                for i, ms in late.items())
        out[PATCH[code]] = {'onsets': {str(p): max(0, round(table[p] + off)) for p in sorted(table)}}
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write('\n')


if __name__ == '__main__':
    main()
