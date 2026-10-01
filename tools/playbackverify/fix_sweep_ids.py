#!/usr/bin/env python3
"""Sets the MuseScore instrument id of a sweep score converted to .mscx (make_sweep_scores.py): the MusicXML import
takes the first instrument with the part's sound ("violin" for the violins), and the map matches section strings by
their own ids ("violins", "violas", "violoncellos"). Usage: fix_sweep_ids.py <Sweep X.mscx> ... (next to its .notes.json)"""
import json
import re
import sys

for path in sys.argv[1:]:
    meta = json.load(open(path[:-5] + ".notes.json", encoding="utf-8"))
    text = open(path, encoding="utf-8").read()
    text, n = re.subn(r'<Instrument id="[^"]*">', f'<Instrument id="{meta["id"]}">', text, count=1)
    open(path, "w", encoding="utf-8").write(text)
    print(path, meta["id"], n)
