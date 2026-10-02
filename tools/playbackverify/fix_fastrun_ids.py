#!/usr/bin/env python3
"""Sets the MuseScore instrument ids of a fast-run score converted to .mscx (make_fastrun_scores.py), part by part:
the MusicXML import takes the first instrument with the part's sound ("violin" for the violins), and the map matches
section strings by their own ids. Usage: fix_fastrun_ids.py <Fast X.mscx> ... (next to its .notes.json)"""
import json
import re
import sys

for path in sys.argv[1:]:
    meta = json.load(open(path[:-5] + ".notes.json", encoding="utf-8"))
    ids = iter(p["id"] for p in meta["parts"])
    text = open(path, encoding="utf-8").read()
    text, n = re.subn(r'<Instrument id="[^"]*">', lambda m: f'<Instrument id="{next(ids)}">', text)
    open(path, "w", encoding="utf-8").write(text)
    print(path, n)
