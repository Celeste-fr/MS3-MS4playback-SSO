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
"""
import argparse
import json
import os
import statistics

PATCH = {'vln1': 'Violins 1', 'vln2': 'Violins 2', 'vla': 'Violas', 'vc': 'Celli', 'cb': 'Basses', 'fl': 'Flute Solo', 'ob': 'Oboe Solo',
         'cl': 'Clarinet Solo', 'bn': 'Bassoon Solo', 'hn': 'Horn Solo', 'tpt': 'Trumpet Solo',
         'tbn': 'Tenor Trombone Solo', 'tba': 'Tuba Solo'}
FIT = ['vln1', 'vla', 'fl', 'ob', 'cl', 'tba']
SHIFT = ['tbn', 'cb']
# --sound: the articulation fitted, its patches (fitted per pitch / shifted) and its file
SOUNDS = {'Long': (FIT, SHIFT, 'sso_long_onset_fit.json'),
          'Long (Rachm.)': (['vln1', 'vln2'], [], 'sso_rachm_onset_fit.json')}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isolated', required=True)
    ap.add_argument('--split', required=True, help='folder with meta_Q_<code>.json and late_S_<code>.json')
    ap.add_argument('--smooth', type=int, default=1, help='median of the isolated medians within this many semitones')
    ap.add_argument('--sound', default='Long', choices=sorted(SOUNDS), help='the articulation fitted')
    ap.add_argument('--out', help='default: the sound\'s file next to this script')
    a = ap.parse_args()
    fit, shift, name = SOUNDS[a.sound]
    out_path = a.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), name)
    iso = json.load(open(a.isolated))
    out = {}
    for code in fit + shift:
        table = {int(p): v['median'] for p, v in iso[code].items()}
        table = {p: statistics.median(table[q] for q in table if abs(q - p) <= a.smooth) for p in table}
        meta = {str(x['i']): x for x in json.load(open(os.path.join(a.split, f'meta_Q_{code}.json')))}
        late = json.load(open(os.path.join(a.split, f'late_S_{code}.json')))
        if code in shift:
            out[PATCH[code]] = {'shift': round(statistics.median(late.values()))}
            continue
        nearest = lambda p: table[min(table, key=lambda k: abs(k - p))]
        off = statistics.median((meta[i]['written'] - meta[i]['on']) * 1000 + ms - nearest(meta[i]['pitch'])
                                for i, ms in late.items())
        out[PATCH[code]] = {'onsets': {str(p): max(0, round(table[p] + off)) for p in sorted(table)}}
    with open(out_path, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write('\n')


if __name__ == '__main__':
    main()
