#!/usr/bin/env python3
# Writes the KontaktSetup test's files (tst_soundlibrary::kontaktSetup) with the builders of
# tools/soundlibraries/test_make_setups.py: an .nki of a made-up patch (its program, two script slots,
# the second with saved values, and a sample list relative to it) and Kontakt's state with nothing
# loaded (a multi with no slot used). Run from this folder: python3 make_fixtures.py
import os
import sys

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "..", "..", "..", "tools", "soundlibraries"))
from test_make_setups import VALUES, nki_file, setup_component_raw  # noqa: E402
from test_extract_library_files import pchunk, pstruct, u16  # noqa: E402

with open(os.path.join(here, "Violins 2 - All techniques.nki"), "wb") as f:
    f.write(nki_file("Violins 2 - All techniques", VALUES, ["v2_C3.ncw", "v2_C4.ncw"]))
bank = pchunk(0x03, pstruct(0x77, bytes(42), pchunk(0x47, bytes(17)) + pchunk(0x37, bytes(8))
                            + pchunk(0x48, b"\x01" + bytes(63)), private=b"uuid"))
with open(os.path.join(here, "empty.bin"), "wb") as f:
    f.write(setup_component_raw(bank + pchunk(0xF02, b"browser") + pchunk(0x4B, u16(3) + b"x")))
