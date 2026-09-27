#!/usr/bin/env python3
"""Tests for extract_library_files.py, on files built here in the layouts it reads.

    python3 tools/soundlibraries/test_extract_library_files.py [-v]

KONTAKT_NKI=<a Kontakt 5+ .nki> also reads a real file (e.g. ConvertWithMoss's
src/main/resources/de/mossgrabers/convertwithmoss/templates/nki/Kontakt680Template.nki: one program,
one group "61 samples", 61 zones, 61 sample paths, five script slots).
"""

import json
import os
import re
import sqlite3
import struct
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_library_files as E  # noqa: E402

u8 = lambda v: struct.pack("<B", v)
u16 = lambda v: struct.pack("<H", v)
s16 = lambda v: struct.pack("<h", v)
u32 = lambda v: struct.pack("<I", v)
s32 = lambda v: struct.pack("<i", v)
u64 = lambda v: struct.pack("<Q", v)
f32 = lambda v: struct.pack("<f", v)
f64 = lambda v: struct.pack("<d", v)


def utf16(s):
    return u32(len(s)) + s.encode("utf-16-le")


def block64(b):
    return u64(len(b) + 8) + b


# --- NI container ---------------------------------------------------------------------------

def stack(chunks):
    """chunks: [(domain, type id, body)], ending with the terminator."""
    domain, tid, body = chunks[0]
    head = domain.encode() + u32(tid) + u32(1)
    if tid == 1:
        return head + body
    return head + block64(stack(chunks[1:])) + body


def item(chunks, children=(), version=1):
    b = u32(1) + b"hsin" + u32(1) + u32(0) + bytes(16) + block64(stack(chunks + [("DSIN", 1, b"")]))
    b += u32(version) + u32(len(children))
    for i, (domain, tid, child) in enumerate(children):
        b += u32(i) + domain.encode() + u32(tid) + child
    return block64(b)


def fastlz_literal(data):
    """Level-1 FastLZ with literal runs only (valid input for any FastLZ decoder)."""
    out = b""
    for i in range(0, len(data), 32):
        run = data[i:i + 32]
        out += u8(len(run) - 1) + run
    return out


def soundinfo(name, tags=(), attributes=(), props=None, description=""):
    b = u32(1) + u32(2) + u32(1) + u32(0) + utf16(name) + utf16("Spitfire Audio") + utf16("Spitfire Audio")
    b += utf16(description) + bytes(4) + b"\xff" * 8 + bytes(16) + u32(1) + u32(1)
    b += u32(len(tags)) + b"".join(utf16(t) for t in tags)
    b += u32(len(attributes)) + b"".join(utf16(t) for t in attributes) + u32(0)
    props = props or {}
    b += u32(len(props)) + b"".join(utf16(k) + utf16(v) for k, v in props.items())
    return b


def app(version="7.10.5"):
    return u32(1) + u8(0) + u32(2) + u32(1) + utf16(version)


def authorization(snpids):
    if not snpids:
        return u32(1) + u32(2)
    return u32(1) + u32(1) + u32(1) + u32(len(snpids)) + b"".join(utf16(s) for s in snpids) + bytes(8) + u32(0x8565620D)


def preset_chunk_item(data):
    return u32(1) + u32(1) + u32(1) + u32(len(data)) + u32(0) + data + u32(0) + u32(0x8565620D)


def subtree(inner, compressed=True, garbage=False):
    if garbage:
        packed = b"\xff" + os.urandom(40)
        return u32(1) + u8(1) + u32(1000) + u32(len(packed)) + packed
    if not compressed:
        return u32(1) + u8(0) + inner
    packed = fastlz_literal(inner)
    return u32(1) + u8(1) + u32(len(inner)) + u32(len(packed)) + packed


def nki(preset=None, snpids=(), encrypted=False, name="Violins 1 - All techniques"):
    info = item([("DSIN", 108, soundinfo(name, tags=["Strings"], attributes=["Ensemble"],
                                         props={"\\@verl": "1.7.14"}, description="All the techniques."))])
    if encrypted:
        body = item([("DSIN", 116, u32(1) + u8(1)), ("DSIN", 115, subtree(b"", garbage=True))])
    else:
        inner = item([("DSIN", 109, preset_chunk_item(preset))])
        body = item([("DSIN", 116, u32(1) + u8(0)), ("DSIN", 115, subtree(inner))])
    sound = item([("4KIN", 3, b"\x00\x00"), ("4KIN", 101, app()), ("4KIN", 106, authorization([]))],
                 [("DSIN", 108, info), ("DSIN", 116, body)])
    root = item([("DSIN", 118, u32(1) + u32(1 << 20 | 7 << 12 | 14) + u32(0) + u32(1) + bytes(42)),
                 ("DSIN", 106, authorization(list(snpids)))],
                [("4KIN", 3, sound)])
    return root


# --- Kontakt preset data --------------------------------------------------------------------

def pchunk(cid, body):
    return u16(cid) + u32(len(body)) + body


def pstruct(version, public, children=b"", private=b""):
    return u8(1) + u16(version) + u32(len(private)) + private + u32(len(public)) + public + u32(len(children)) + children


def program_data(name):
    b = utf16(name) + f64(1e9) + u8(0) + f32(0.5) + f32(0) + f32(1) + bytes([1, 127, 24, 96])
    b += u16(0) + u32(61440) + u32(4242) + u32(0) + u32(32) + u8(0) + u32(22)
    b += utf16("Spitfire") + utf16("Spitfire Audio") + utf16("https://www.spitfireaudio.com")
    b += u16(1) + u16(2) + u16(3) + b"\xff" * 4 + bytes(12) + b"\xff" * 4
    return b


def group_data(name, release=False):
    return (utf16(name) + f32(1) + f32(0) + f32(1) + bytes([1, 0, 1 if release else 0, 0]) + s32(0)
            + s16(-1) + s32(-1) + s32(0) + u8(0) + u8(0) + s32(0))


def zone_data(key_low, key_high, vel_low, vel_high, root, file_id):
    b = u32(0) + u32(0) + b"\xff" * 4 + u16(vel_low) + u16(vel_high) + u16(key_low) + u16(key_high)
    b += bytes(8) + u16(root) + f32(1) + f32(0) + f32(1) + u8(0) + u8(1) + b"\xff" * 4
    b += s32(file_id) + u32(3) + u32(48000) + u8(2) + u32(96000) + u32(44) + u32(root) + f32(1) + u8(0) + u32(576000)
    return b


def path_segments(folder, name):
    return u32(2) + u8(2) + utf16(folder) + u8(4) + utf16(name)


def kontakt_preset():
    groups = [group_data("Long mf RR1"), group_data("Long mf RR2"), group_data("Staccato release", True)]
    group_list = pchunk(0x33, u32(len(groups)) + b"".join(pstruct(0x9C, g) for g in groups))
    zones = [(0, 48, 59, 1, 127, 55, 0), (1, 48, 59, 1, 127, 55, 1), (2, 60, 72, 1, 64, 66, 2)]
    zone_list = pchunk(0x34, u32(len(zones)) + b"".join(u32(z[0]) + pstruct(0x9A, zone_data(*z[1:])) for z in zones))
    script = pchunk(0x06, pstruct(0x10, b"on init\n  message(\"secret\")\nend on"))
    program = pchunk(0x28, pstruct(0xAE, program_data("Violins 1 - All techniques"), group_list + zone_list + script))
    names = ["vln1_long_mf_rr1_C3.ncw", "vln1_long_mf_rr2_C3.ncw", "vln1_stac_rel_C4.ncw"]
    files = pchunk(0x3D, s32(0) + s32(len(names)) + b"".join(path_segments("Samples", n) for n in names)
                   + bytes(8 * len(names)) + u32(0))
    return program + files


# --- other containers -----------------------------------------------------------------------

def fc_container(files):
    """NI file container: [(name, data)]."""
    b = E.FC_MTD + bytes(248) + b"\xF0" * 8 + u64(len(files)) + u64(sum(len(d) for _, d in files))
    b += E.FC_TOC + bytes(600)
    end = 0
    for i, (name, data) in enumerate(files):
        end += len(data)
        b += u64(i + 1) + bytes(16) + name.encode("utf-16-le").ljust(600, b"\x00") + u64(0) + u64(end)
    b += b"\xF1" * 8 + bytes(16) + E.FC_TOC + bytes(592)
    return b + b"".join(d for _, d in files)


PRODUCT_HINTS = (b'<?xml version="1.0" encoding="UTF-8"?><ProductHints spec="1.0"><Product version="1">'
                 b'<Name>Spitfire Symphony Orchestra</Name><SNPID>K42</SNPID><RegKey>Spitfire Symphony Orchestra</RegKey>'
                 b'<SerialNumber>1234-5678</SerialNumber><Company>Spitfire Audio</Company></Product></ProductHints>')


def nicnt(files):
    head = bytearray(E.FC_MTD + bytes(512 - 16))
    head[66:66 + 10] = "1.0.2".encode("utf-16-le")
    start = 512 + len(PRODUCT_HINTS)
    head[144:148] = u32(start)
    body = bytes(head[:256]) + PRODUCT_HINTS + b"\x00"
    body = body.ljust(start + 256, b"\x00")
    return body + fc_container(files)


def nks_archive():
    """Directory v0x0110: a folder with an encrypted sample and a plain file, and a content file."""
    def entry(off, typ, name):
        return u16(0) + u32(off) + u16(typ) + name.encode("utf-16-le") + b"\x00\x00"

    def directory(entries):
        return u32(E.NKS_DIR) + u16(0x0110) + u32(0) + bytes(4) + u32(len(entries)) + bytes(4)

    root_entries = [("Samples", 1), ("wallpaper.tga", 4)]
    sub_entries = [("vln1_long_C4_mf_RR1.ncw", 2), ("info.dat", 3)]
    # offsets: root dir, sub dir, content file header, file header
    root_len = 22 + sum(8 + 2 * len(n) + 2 for n, _ in root_entries)
    sub_off = root_len
    sub_len = 22 + sum(8 + 2 * len(n) + 2 for n, _ in sub_entries)
    content_off = sub_off + sub_len
    file_off = content_off + 30
    root = directory(root_entries) + entry(sub_off, 1, "Samples") + entry(content_off, 4, "wallpaper.tga")
    sub = directory(sub_entries) + entry(0x12345678, 2, "vln1_long_C4_mf_RR1.ncw") + entry(file_off, 3, "info.dat")
    content = u32(E.NKS_CONTENT) + u16(0x0110) + u32(0) + u32(0) + u32(777) + bytes(12)
    plain = u32(E.NKS_FILE) + u16(0x0110) + bytes(13) + u32(4096) + bytes(9)
    return root + sub + content + plain


def msgpack(v):
    if isinstance(v, str):
        b = v.encode()
        return (u8(0xA0 | len(b)) if len(b) < 32 else b"\xd9" + u8(len(b))) + b
    if isinstance(v, bool):
        return b"\xc3" if v else b"\xc2"
    if isinstance(v, int):
        return u8(v) if 0 <= v < 128 else b"\xce" + struct.pack(">I", v)
    if isinstance(v, list):
        return u8(0x90 | len(v)) + b"".join(msgpack(x) for x in v)
    if isinstance(v, dict):
        return u8(0x80 | len(v)) + b"".join(msgpack(k) + msgpack(x) for k, x in v.items())
    raise TypeError(v)


def nksf():
    chunks = [(b"NISI", u32(1) + msgpack({"name": "Violins 1 Long", "vendor": "Spitfire Audio", "types": [["Strings"]]})),
              (b"NICA", u32(1) + msgpack({"ni8": [[{"id": 1, "name": "Dynamics", "section": "Main"},
                                                   {"id": 2, "name": "Expression"}]]})),
              (b"PLID", u32(1) + msgpack({"VST.magic": 1315523403})),
              (b"PCHK", u32(1) + b"opaque plugin state")]
    body = b"NIKS"
    for cid, data in chunks:
        body += cid + u32(len(data)) + data + (b"\x00" if len(data) & 1 else b"")
    return b"RIFF" + u32(len(body)) + body


def wav_with_loop():
    fmt = u16(1) + u16(2) + u32(48000) + u32(48000 * 6) + u16(6) + u16(24)
    smpl = bytes(12) + u32(60) + u32(0) + bytes(8) + u32(1) + u32(0) + u32(0) + u32(0) + u32(1000) + u32(47999) + u32(0) + u32(0)
    data = bytes(6 * 48000)
    body = b"WAVE" + b"fmt " + u32(len(fmt)) + fmt + b"smpl" + u32(len(smpl)) + smpl + b"data" + u32(len(data)) + data
    return b"RIFF" + u32(len(body)) + body


class Tests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = self.tmp.name
        self.opts = E.Options(os.path.join(self.dir, "out"), False)
        self.opts.match = re.compile("spitfire|symphon", re.I)

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, name, data):
        p = os.path.join(self.dir, "lib", name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "wb") as f:
            f.write(data)
        return p

    def test_fastlz(self):
        self.assertEqual(E.fastlz_decompress(fastlz_literal(b"x" * 100), 100), b"x" * 100)
        # "abcab" + a back reference of 5 bytes at distance 3: "abcab" + "cabca"
        packed = u8(4) + b"abcab" + u8(3 << 5 | 0) + u8(2)
        self.assertEqual(E.fastlz_decompress(packed, 10), b"abcabcabca")
        with self.assertRaises(ValueError):
            E.fastlz_decompress(b"\xff" + bytes(10), 100)

    def test_msgpack(self):
        v = {"a": [1, "two", True, None], "b": {"c": -3}}
        self.assertEqual(E.msgpack_decode(u8(0x82) + msgpack("a") + u8(0x94) + u8(1) + msgpack("two") + b"\xc3\xc0"
                                          + msgpack("b") + u8(0x81) + msgpack("c") + u8(0xFD)), v)

    def test_readable_nki(self):
        p = self.write("Instruments/Violins 1.nki", nki(kontakt_preset(), snpids=[]))
        r = E.examine(p, self.opts)
        self.assertEqual(r["format"], "NI container")
        self.assertNotIn("error", r)
        self.assertEqual(r["application"], "Kontakt 7.10.5")
        self.assertEqual(r["soundinfo"]["name"], "Violins 1 - All techniques")
        self.assertEqual(r["soundinfo"]["tags"], ["Strings"])
        self.assertEqual(r["soundinfo"]["attributes"], ["Ensemble"])
        self.assertEqual(r["soundinfo"]["properties"], {"\\@verl": "1.7.14"})
        self.assertFalse(r.get("encrypted"))
        k = r["kontakt"][0]
        prog = k["programs"][0]
        self.assertEqual(prog["name"], "Violins 1 - All techniques")
        self.assertEqual(prog["keyRange"], [24, 96])
        self.assertEqual(prog["libraryId"], 4242)
        self.assertEqual(prog["defaultKeyswitch"], 0)
        self.assertEqual(prog["url"], "https://www.spitfireaudio.com")
        self.assertEqual([g["name"] for g in prog["groups"]], ["Long mf RR1", "Long mf RR2", "Staccato release"])
        self.assertTrue(prog["groups"][2]["releaseTrigger"])
        zones = [dict(zip(prog["zoneFields"], z)) for z in prog["zones"]]
        self.assertEqual([(z["group"], z["keyLow"], z["keyHigh"], z["velHigh"], z["root"], z["file"]) for z in zones],
                         [(0, 48, 59, 127, 55, 0), (1, 48, 59, 127, 55, 1), (2, 60, 72, 64, 66, 2)])
        self.assertEqual((zones[0]["rate"], zones[0]["channels"], zones[0]["frames"]), (48000, 2, 96000))
        self.assertEqual(k["files"], ["Samples/vln1_long_mf_rr1_C3.ncw", "Samples/vln1_long_mf_rr2_C3.ncw",
                                      "Samples/vln1_stac_rel_C4.ncw"])
        # the script is counted, its text is nowhere in the output
        self.assertEqual(len(k["scriptChunks"]), 1)
        self.assertNotIn("secret", json.dumps(r, default=str))

    def test_encrypted_nki(self):
        p = self.write("Instruments/Celli.nki", nki(encrypted=True, snpids=["K42"], name="Celli - All techniques"))
        r = E.examine(p, self.opts)
        self.assertNotIn("error", r)
        self.assertTrue(r["encrypted"])
        self.assertEqual(r["snpids"], ["K42"])
        self.assertEqual(r["soundinfo"]["name"], "Celli - All techniques")
        self.assertNotIn("kontakt", r)

    def test_nicnt(self):
        p = self.write("Spitfire Symphony Orchestra.nicnt", nicnt([
            ("ContentVersion.txt", b"1.0.2"),
            ("Resources/data/uacc.nka", b"%uacc\n1\n40\n"),
            ("Resources/scripts/main.txt", b"on init end on"),
            ("Resources/pictures/bg.png", b"\x89PNG....")]))
        r = E.examine(p, self.opts)
        self.assertNotIn("error", r)
        self.assertEqual(r["contentVersion"], "1.0.2")
        self.assertEqual(r["productHints"]["/ProductHints/Product/Name"], "Spitfire Symphony Orchestra")
        self.assertEqual(r["productHints"]["/ProductHints/Product/SNPID"], "K42")
        self.assertEqual(r["productHints"]["/ProductHints/Product/RegKey"], "Spitfire Symphony Orchestra")
        self.assertEqual(r["productHints"]["/ProductHints/Product/SerialNumber"], "(left out)")
        self.assertEqual([e["name"] for e in r["entries"]][:2], ["ContentVersion.txt", "Resources/data/uacc.nka"])
        self.assertEqual(r["scriptsNotExtracted"], [{"name": "Resources/scripts/main.txt", "size": 14}])
        saved = " ".join(self.opts.saved)
        self.assertIn("uacc.nka", saved)
        self.assertNotIn("main.txt", saved)
        self.assertNotIn("bg.png", saved)

    def test_monolith(self):
        p = self.write("Violins mono.nki", fc_container([("Violins.nki", nki(kontakt_preset())),
                                                        ("Samples/a.ncw", b"\x01\xa8\x9e\xd6" + bytes(60))]))
        r = E.examine(p, self.opts)
        self.assertEqual(r["format"], "NI file container")
        self.assertEqual(r["instruments"][0]["kontakt"][0]["programs"][0]["name"], "Violins 1 - All techniques")
        self.assertEqual(r["samples"], 1)

    def test_nks_archive(self):
        p = self.write("Samples/violins.nkx", nks_archive())
        r = E.examine(p, self.opts)
        self.assertEqual(r.get("format"), "NKS archive", r)
        names = r["_names"]
        self.assertEqual(names, [("/Samples/vln1_long_C4_mf_RR1.ncw", "encrypted file", None),
                                 ("/Samples/info.dat", "file", 4096),
                                 ("/wallpaper.tga", "content file", 777)])

    def test_nksf(self):
        p = self.write("Presets/Violins 1 Long.nksf", nksf())
        r = E.examine(p, self.opts)
        self.assertEqual(r["format"], "NKS preset (NIKS)")
        self.assertEqual(r["chunks"]["NISI"]["data"]["name"], "Violins 1 Long")
        self.assertEqual(r["chunks"]["NICA"]["data"]["ni8"][0][1]["name"], "Expression")
        self.assertEqual(r["chunks"]["PLID"]["data"], {"VST.magic": 1315523403})
        self.assertEqual(r["chunks"]["PCHK"]["size"], 23)

    def test_wav(self):
        r = E.examine(self.write("loose.wav", wav_with_loop()), self.opts)
        self.assertEqual(r["wav"]["rate"], 48000)
        self.assertEqual(r["wav"]["frames"], 48000)
        self.assertEqual(r["wav"]["root"], 60)
        self.assertEqual(r["wav"]["loops"], [[0, 1000, 47999]])

    def test_sqlite(self):
        p = os.path.join(self.dir, "lib", "komplete.db3")
        os.makedirs(os.path.dirname(p), exist_ok=True)
        con = sqlite3.connect(p)
        con.execute("create table k_sound_info (id, name, vendor, serial_number)")
        con.executemany("insert into k_sound_info values (?,?,?,?)",
                        [(1, "Violins 1", "Spitfire Audio", "SECRET"), (2, "Grand", "Other", "X")])
        con.commit()
        con.close()
        r = E.examine(p, self.opts)
        t = r["database"]["tables"]["k_sound_info"]
        self.assertEqual(t["rows"], 2)
        self.assertEqual(t["matching"], [{"id": 1, "name": "Violins 1", "vendor": "Spitfire Audio"}])

    def test_run_and_report(self):
        self.write("Spitfire Symphony Orchestra.nicnt", nicnt([("Resources/data/uacc.nka", b"%uacc\n1\n")]))
        self.write("Instruments/Violins 1.nki", nki(kontakt_preset()))
        self.write("Instruments/Celli.nki", nki(encrypted=True, snpids=["K42"], name="Celli - All techniques"))
        self.write("Samples/violins.nkx", nks_archive())
        self.write("settings.json", b'{"email": "someone@example.com", "theme": "dark"}')
        out = os.path.join(self.dir, "results")
        outdir = E.run(E_args([os.path.join(self.dir, "lib"), "--out", out, "--no-system"]))
        self.assertTrue(os.path.isfile(outdir + ".zip"))
        with open(os.path.join(outdir, "summary.txt"), encoding="utf-8") as f:
            summary = f.read()
        self.assertIn("Kontakt presets readable: 1, encrypted: 1", summary)
        self.assertIn("Library SNPIDs: K42 (1 files)", summary)
        self.assertIn("groups: Long mf RR1, Long mf RR2, Staccato release", summary)
        self.assertIn("/ProductHints/Product/Name: Spitfire Symphony Orchestra", summary)
        with open(os.path.join(outdir, "archives", "violins.nkx.txt"), encoding="utf-8") as f:
            self.assertIn("vln1_long_C4_mf_RR1.ncw", f.read())
        with open(os.path.join(outdir, "extracted", "settings.json", "settings.json"), encoding="utf-8") as f:
            self.assertEqual(json.load(f), {"email": "(left out)", "theme": "dark"})
        with zipfile.ZipFile(outdir + ".zip") as z:
            self.assertTrue(any(n.endswith("library.json") for n in z.namelist()))
        with open(os.path.join(outdir, "library.json"), encoding="utf-8") as f:
            lib = json.load(f)
        self.assertEqual(E.summarize(lib).splitlines()[1:6], summary.splitlines()[1:6])

    def test_real_nki(self):
        path = os.environ.get("KONTAKT_NKI")
        if not path:
            self.skipTest("KONTAKT_NKI not set")
        r = E.examine(path, self.opts)
        self.assertNotIn("error", r)
        progs = r["kontakt"][0]["programs"]
        self.assertTrue(progs and progs[0]["zones"], r)
        if os.path.basename(path) == "Kontakt680Template.nki":
            self.assertEqual(r["application"], "Kontakt 6.7.1.0")
            self.assertEqual(len(progs[0]["zones"]), 61)
            self.assertEqual(len(r["kontakt"][0]["files"]), 61)
            self.assertEqual(len(r["kontakt"][0]["scriptChunks"]), 5)


def E_args(argv):
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="*")
    ap.add_argument("--match", default=r"spitfire|symphon")
    ap.add_argument("--name", default="Spitfire Symphony Orchestra")
    ap.add_argument("--out")
    ap.add_argument("--images", action="store_true")
    ap.add_argument("--no-system", action="store_true")
    return ap.parse_args(argv)


if __name__ == "__main__":
    unittest.main()
