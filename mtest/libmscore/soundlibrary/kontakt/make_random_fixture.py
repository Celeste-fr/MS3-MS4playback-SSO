#!/usr/bin/env python3
# Writes tst_soundlibrary::kontaktRandomSources' .nki: a made-up "Violas - Performance" whose two groups carry
# modulators as SSO's Performance patches keep them ([slurs] fixedTransitions; audio/vst3/kontaktsetup.cpp › random
# sources): group 0 a 0x3B list (version 0x13) with an external modulator from the random generator ("RVU_TIME1", type 10,
# on fm7_level1, intensity 1), an empty slot, an external one from a controller ("CC_TIME1", type 4, 0.5) and an internal
# one; group 1 a 0x3C list (version 0x10) with another "RVU_TIME1" at 0.75. Checked against layer_isolation.py's
# modulators() (the walk the C++ follows). Run from this folder: python3 make_random_fixture.py
import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
tools = os.path.join(here, "..", "..", "..", "..", "tools", "soundlibraries")
sys.path.insert(0, tools)
from test_make_setups import VALUES, file_list, script, voice_groups  # noqa: E402
from test_extract_library_files import PROGRAM_MARKER, group_data, nki, pchunk, program_data, pstruct, u16, u32, zone_data  # noqa: E402


def f32(v):
    return struct.pack("<f", v)


def ext(target, source, stype, intensity):
    t = target.encode()
    s = source.encode()
    tail = f32(intensity) + b"\xff\xff\x10\x0a\x00" + u32(6) + b"<none>" + u32(4) + u32(len(s)) + s + u32(1) + u32(stype) \
        + bytes(8)
    rec = bytes([1, 5, 1]) + u32(4 + 4 + len(t) + len(tail)) + u32(1) + u32(len(t)) + t + tail
    return b"\x01" + pchunk(0x0C, rec)


def internal(target, kind, intensity):
    t = target.encode()
    k = kind.encode()
    tail = f32(intensity) + b"\xff\xff\x10\x00\x00" + u32(len(k)) + k + bytes(8)
    rec = bytes([1, 5, 1]) + u32(4 + 4 + len(t) + len(tail)) + u32(1) + u32(len(t)) + t + tail
    return b"\x01" + pchunk(0x0D, rec)


g0 = pchunk(0x3B, b"\x00" + u16(0x13) + u32(0) + ext("fm7_level1", "RVU_TIME1", 10, 1.0) + b"\x00"
            + ext("fm7_level1", "CC_TIME1", 4, 0.5) + internal("pitch", "LFO_SINE_PITCH", 0.25))
g1 = pchunk(0x3C, b"\x00" + u16(0x10) + ext("fm7_level1", "RVU_TIME1", 10, 0.75))
groups = pchunk(0x33, u32(2) + pstruct(0x9C, group_data("Minor 2nd PORTAMENTO FAST"), g0)
                + pstruct(0x9C, group_data("Minor 2nd RUNS RT"), g1))
zl = pchunk(0x34, u32(1) + u32(0) + pstruct(0x9A, zone_data(48, 72, 1, 127, 60, 0)))
name = "Violas - Performance"
prog = pstruct(0xAF, program_data(name), script([]) + script(VALUES) + voice_groups() + groups + zl)

if __name__ == "__main__":
    import layer_isolation as LI
    mods = LI.modulators(prog)
    got = [(m["kind"], m.get("source"), m["target"], round(struct.unpack_from("<f", prog, m["at"])[0], 3)) for m in mods]
    assert got == [("ext", "RVU_TIME1", "fm7_level1", 1.0), ("ext", "CC_TIME1", "fm7_level1", 0.5),
                   ("int", None, "pitch", 0.25), ("ext", "RVU_TIME1", "fm7_level1", 0.75)], got
    preset = pchunk(0x28, prog) + pchunk(0x47, bytes(17)) + pchunk(0x4B, file_list(["va_C4.ncw"], name + ".nki"))
    with open(os.path.join(here, "Violas - Performance (random sources).nki"), "wb") as f:
        f.write(nki(preset, snpids=[], name=name, marker=PROGRAM_MARKER))
    print(got)
