#!/usr/bin/env python3
# A UACC-style test instrument for sfizz (open-source SFZ sampler, VST 3): one region per
# UACC value on CC32, like a Spitfire patch set to UACC. Each articulation's sample is a
# C4 sine plus one "fingerprint" harmonic, so fingerprint.py can tell from the audio which
# articulation played. Values: the Spitfire map's Solo Violin 1 (+ tremolo/trills with --ornaments).
import math, os, struct, sys, wave
out = sys.argv[1] if len(sys.argv) > 1 else "uaccsfz"
ART = {1: 2, 40: 3, 42: 4, 56: 5, 10: 6, 61: 7}          # UACC -> harmonic
if "--ornaments" in sys.argv:
    ART.update({11: 8, 70: 9, 71: 10})
os.makedirs(out + "/samples", exist_ok=True)
sr, f0 = 48000, 261.6256
for v, k in ART.items():
    with wave.open(f"{out}/samples/uacc{v}.wav", "w") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(sr)
        w.writeframes(b"".join(struct.pack("<h", int(min(1, i / 200) * (0.25 * math.sin(2 * math.pi * f0 * i / sr)
                      + 0.25 * math.sin(2 * math.pi * f0 * k * i / sr)) * 32767)) for i in range(sr * 4)))
sfz = ["<control> default_path=samples/",
       "<global> lokey=0 hikey=127 pitch_keycenter=60 ampeg_release=0.03 amplitude_oncc1=100 amplitude_curvecc1=0"]
sfz += [f"<region> sample=uacc{v}.wav locc32={v} hicc32={v}" for v in ART]
open(f"{out}/uacc-test.sfz", "w").write("\n".join(sfz) + "\n")
