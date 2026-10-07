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
"""
import argparse
import json
import os
import statistics

PATCH = {'vln1': 'Violins 1', 'vla': 'Violas', 'vc': 'Celli', 'cb': 'Basses', 'fl': 'Flute Solo', 'ob': 'Oboe Solo',
         'cl': 'Clarinet Solo', 'bn': 'Bassoon Solo', 'hn': 'Horn Solo', 'tpt': 'Trumpet Solo',
         'tbn': 'Tenor Trombone Solo', 'tba': 'Tuba Solo'}
FIT = ['vln1', 'vla', 'fl', 'ob', 'cl', 'tba']
SHIFT = ['tbn', 'cb']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--isolated', required=True)
    ap.add_argument('--split', required=True, help='folder with meta_Q_<code>.json and late_S_<code>.json')
    ap.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), 'sso_long_onset_fit.json'))
    a = ap.parse_args()
    iso = json.load(open(a.isolated))
    out = {}
    for code in FIT + SHIFT:
        table = {int(p): v['median'] for p, v in iso[code].items()}
        meta = {str(x['i']): x for x in json.load(open(os.path.join(a.split, f'meta_Q_{code}.json')))}
        late = json.load(open(os.path.join(a.split, f'late_S_{code}.json')))
        if code in SHIFT:
            out[PATCH[code]] = {'shift': round(statistics.median(late.values()))}
            continue
        nearest = lambda p: table[min(table, key=lambda k: abs(k - p))]
        off = statistics.median((meta[i]['written'] - meta[i]['on']) * 1000 + ms - nearest(meta[i]['pitch'])
                                for i, ms in late.items())
        out[PATCH[code]] = {'onsets': {str(p): max(0, round(table[p] + off)) for p in sorted(table)}}
    with open(a.out, 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write('\n')


if __name__ == '__main__':
    main()
