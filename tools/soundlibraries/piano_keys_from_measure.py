#!/usr/bin/env python3
"""SSO's Grand Piano at every key, from a measure_patch.py run, for calibration_from_sound_dynamics.py -K.

    piano_keys_from_measure.py <result.json> [sso_piano_keys.json]

The run (2026-10-30, Windows test VM, kthost, the real library: `measure_patch.py plan --patch 'Grand Piano' --what
level,tone --pitches 21:108 --velocity 48,80,112 --lengths 1000 --repeats 4`): each key x velocity a cell of 4 repeated
notes; its loud50 (the loudest 50 ms of power, dBFS: the dynamics check's measure, sso_percussion_levels.json's loudDb)
is measured on one of them. Writes {velocities, loud50: {key: [dB at each velocity]}} (derived numbers only).
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VELOCITIES = (48, 80, 112)


def convert(result):
    by = result["byPitch"]
    out = {}
    for key in sorted(by, key=int):
        cells = by[key]
        row = []
        for v in VELOCITIES:
            c = cells.get("v%d cc1=64 cc11=127 1000ms x4" % v)
            if not c or c.get("loud50") is None:
                break
            row.append(round(c["loud50"], 2))
        else:
            out[key] = row
    return {"about": "SSO Grand Piano (Direct), every key: loud50 (dBFS, the loudest 50 ms of a 1 s note and 0.5 s more, one of "
                     "4 repeats) at velocity 48 / 80 / 112, CC1 64, CC11 127. Measured 2026-10-30 on the Windows test VM, "
                     "Kontakt Player 8 + SSO through kthost, by tools/soundlibraries/measure_patch.py (level,tone); written by "
                     "piano_keys_from_measure.py; read by calibration_from_sound_dynamics.py -K",
            "patch": "Grand Piano", "velocities": list(VELOCITIES), "loud50": out}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    with open(os.path.expanduser(sys.argv[1])) as f:
        doc = convert(json.load(f))
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "sso_piano_keys.json")
    with open(out, "w") as f:
        json.dump(doc, f, indent=0)
        f.write("\n")
    print("%d keys written to %s" % (len(doc["loud50"]), out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
