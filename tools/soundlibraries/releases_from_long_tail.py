#!/usr/bin/env python3
"""Puts release_long_tail.py's re-measured releases into sso_sound_range.json (numbers-measured, 2026-10-03).

The rest check measured each semitone's release (to 30 dB under the level before the note-off) within a 6 s tail
(ArticulationCheck::TAIL_SECONDS then): 98 sounds came out at 4-6.1 s or -1 ("longer than the tail"), the timpani's
swells and rolls among them. release_long_tail.py measured those again with a 25 s tail, the same definition.
For each sound measured, each pitch's mfReleaseMs (range row index 9) becomes the new 30 dB value where the note
sustains (a note that doesn't sustain keeps what it had: its release is not used); a pitch still over the tail stays -1.

  releases_from_long_tail.py <release json> ... [-d tools/soundlibraries]   (file names: the patch, spaces as _)"""
import argparse
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('files', nargs='+')
    ap.add_argument('-d', default=HERE)
    a = ap.parse_args()
    path = os.path.join(a.d, 'sso_sound_range.json')
    R = json.load(open(path, encoding='utf-8'))
    changed = 0
    for f in a.files:
        patch = os.path.basename(f)[:-5].replace('_', ' ')
        for sound, v in json.load(open(f, encoding='utf-8')).items():
            entry = R.get(patch, {}).get(sound)
            if not isinstance(entry, dict) or 'range' not in entry:
                continue
            new = {row[0]: row[3] for row in v['rows'] if row[1]}
            for row in entry['range']:
                if row[0] in new and row[9] != new[row[0]]:
                    row[9] = new[row[0]]
                    changed += 1
    with open(path, 'w', encoding='utf-8') as out:          # (rest_from_check.py's layout: a patch a line)
        out.write("{\n" + ",\n".join(json.dumps(n, ensure_ascii=False) + ": " + json.dumps(v, separators=(",", ":"), ensure_ascii=False)
                                     for n, v in sorted(R.items()) if v) + "\n}\n")
    print(changed, 'releases changed')


if __name__ == '__main__':
    main()
