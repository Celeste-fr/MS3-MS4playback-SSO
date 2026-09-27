#!/usr/bin/env python3
"""Extract what a Kontakt sound library's files tell, without Kontakt: the other half of
"Extract plug-in data" (which asks the running plug-in; see CLAUDE.md).

On the owner's Windows machine (Python 3.8+, standard library only; or the .exe the
workflow "Tool: Extract library files" builds):

    python extract_library_files.py                      find the library (registry, Native
                                                          Access) and extract everything
    python extract_library_files.py "D:\\Spitfire Symphony Orchestra" [more folders or files]
    python extract_library_files.py --match "spitfire|symphon" --name "Spitfire Symphony Orchestra"
    python extract_library_files.py --report "<output folder or .zip>"   print the summary again

It writes Documents/MuseScore Sound Library Check/<name> files <date>/ and a .zip of it next to
it (the one to hand back). No sample audio is copied.

What it reads (formats as documented by ConvertWithMoss, github.com/git-moss/ConvertWithMoss,
LGPL: nicontainer/, kontakt/type/kontakt5/; and PresetConverter, github.com/perivar/PresetConverter:
NKS.cs, NICNT.cs; NKSF by github.com/jhorology/gulp-nks-rewrite-meta; all re-written here):

- Kontakt 5+ instruments, multis, banks, snapshots (.nki .nkm .nkb .nksn .nkp: an NI container,
  "hsin" items): the authoring application and version, the library it belongs to (authorization
  SNPIDs), the Soundinfo (name, author, vendor, description, tags, attributes, properties), the
  container tree, and when the preset data is not encrypted: each program (name, volume, tune,
  key and velocity range, default keyswitch, library id, credits, URL, categories), its groups
  (name, volume, pan, tune, release trigger, voice group, MIDI channel), its zones (key and
  velocity range, root key, sample, rate, channels, length, loops) and its sample file list.
- NI file containers ("/\\ NI FC MTD  /\\": .nicnt, monolith .nki, some .nkr .nkx): the table of
  contents; a .nicnt's ProductHints XML (library name, SNPID, RegKey, company, version); data
  files inside (.nka, .xml, .txt outside a scripts folder; pictures with --images).
- The older NKS archives (.nkx sample monoliths, .nkr resources, .nkc caches): the directory tree,
  i.e. the name of every sample and resource inside, and the size of those stored plainly.
- NKS presets (.nksf .nksn .nksfx: RIFF "NIKS"): summary (NISI), the controller pages Komplete
  Kontrol shows (NICA: knob names and the parameter each drives), the plug-in id (PLID).
- Kontakt 1-4 files and anything else: zlib streams inside (Kontakt 2-4 keep XML there).
- WAV samples lying loose: format, length, loops (smpl chunk). .nka arrays, XML, JSON, text.
- On Windows: Native Instruments' registry keys (ContentDir, versions), Native Access's
  Service Center XMLs, NI's databases (komplete.db3 …: the rows naming the library), Spitfire
  Audio's settings folders and Kontakt's user content (snapshots) that name the library.

What it does not do, on purpose: decrypt anything (encrypted preset data, samples and resources
stay closed; what their headers say in the clear is listed), or copy script source (KSP in
PAR_SCRIPT chunks or a Resources/scripts folder: listed with its size only). Values whose name
says serial, license, password, token, activation, email or key are left out; the user's home
folder is written as %USERPROFILE%.
"""

import argparse
import csv
import datetime
import json
import os
import re
import sqlite3
import struct
import sys
import xml.dom.minidom
import zipfile
import zlib
from collections import Counter

VERSION = 1

HOME = os.path.expanduser("~")
SENSITIVE = re.compile(r"serial|licen[cs]e|password|passwd|token|activation|e-?mail|secret|"
                       r"(^|[^a-z])key($|[^a-z])|jdx|^hu$|^ru$|machineid|cookie|credential|jwt|session", re.I)
PERSONAL_FILE = re.compile(r"auth|token|credential|cookie|session|login|account|user|licen[cs]e|serial", re.I)
SCRIPTS = re.compile(r"(^|[\\/])scripts?([\\/]|$)|\.ksp$", re.I)
TEXT_EXT = {".nka", ".xml", ".txt", ".json", ".ini", ".cfg", ".nfo", ".csv", ".plist", ".md"}
IMAGE_EXT = {".png", ".tga", ".jpg", ".jpeg", ".bmp", ".svg"}
HSIN_EXT = {".nki", ".nkm", ".nkb", ".nksn", ".nkp", ".nkg", ".nkz"}
MAX_TEXT = 4 * 1024 * 1024        # text files copied up to this size
MAX_READ = 64 * 1024 * 1024       # whole-file reads (containers, zlib scan) up to this size


def documents_folder():
    """The user's real Documents folder (on Windows it may be elsewhere, e.g. in OneDrive or on
    another drive: asked of the shell, else the registry), else ~/Documents."""
    if sys.platform == "win32":
        try:
            import ctypes
            import uuid
            fid = uuid.UUID("{FDD39AD0-238F-46AF-ADB4-6C85480369C7}")   # FOLDERID_Documents
            buf = ctypes.c_wchar_p()
            guid = (ctypes.c_byte * 16).from_buffer_copy(fid.bytes_le)
            if ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(guid), 0, None, ctypes.byref(buf)) == 0:
                path = buf.value
                ctypes.windll.ole32.CoTaskMemFree(buf)
                if path and os.path.isdir(path):
                    return path
        except Exception:
            pass
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER,
                                r"Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders") as k:
                path = os.path.expandvars(winreg.QueryValueEx(k, "Personal")[0])
                if os.path.isdir(path):
                    return path
        except Exception:
            pass
    return os.path.join(HOME, "Documents")


def private(path):
    """The path with the user's home folder hidden."""
    p = str(path)
    if HOME and p.lower().startswith(HOME.lower()):
        p = "%USERPROFILE%" + p[len(HOME):]
    return p


class Reader:
    """Little-endian reader over bytes."""

    def __init__(self, data, pos=0):
        self.d = data
        self.p = pos

    def left(self):
        return len(self.d) - self.p

    def take(self, n):
        if n < 0 or self.p + n > len(self.d):
            raise ValueError("read past the end (%d of %d at %d)" % (n, len(self.d), self.p))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def u8(self):
        return self.take(1)[0]

    def u16(self):
        return struct.unpack("<H", self.take(2))[0]

    def s16(self):
        return struct.unpack("<h", self.take(2))[0]

    def u32(self):
        return struct.unpack("<I", self.take(4))[0]

    def s32(self):
        return struct.unpack("<i", self.take(4))[0]

    def u64(self):
        return struct.unpack("<Q", self.take(8))[0]

    def f32(self):
        return round(struct.unpack("<f", self.take(4))[0], 6)

    def f64(self):
        return struct.unpack("<d", self.take(8))[0]

    def ascii(self, n):
        return self.take(n).decode("latin-1")

    def utf16(self):
        """UTF-16LE string with its length in characters first (NI's usual string)."""
        n = self.u32()
        if n > 1 << 20:
            raise ValueError("string of %d characters" % n)
        return self.take(2 * n).decode("utf-16-le", "replace")

    def block64(self):
        """A block that starts with its own size (8 bytes, the size included)."""
        n = self.u64()
        if n < 8 or n - 8 > self.left():
            raise ValueError("block of %d bytes, %d left" % (n, self.left()))
        return self.take(n - 8)


def hexs(b, limit=64):
    s = b[:limit].hex(" ")
    return s + (" … (%d bytes)" % len(b) if len(b) > limit else "")


# ---------------------------------------------------------------------------------------------
# FastLZ (levels 1 and 2), as NI compresses container sub-trees

def fastlz_decompress(src, size):
    level = (src[0] >> 5) + 1
    if level not in (1, 2):
        raise ValueError("FastLZ level %d" % level)
    out = bytearray()
    ip = 1
    ctrl = src[0] & 31
    n = len(src)
    while True:
        if ctrl >= 32:
            length = (ctrl >> 5) - 1
            ofs = (ctrl & 31) << 8
            ref = len(out) - ofs - 1
            if length == 6:
                if level == 1:
                    length += src[ip]
                    ip += 1
                else:
                    while True:
                        code = src[ip]
                        ip += 1
                        length += code
                        if code != 255:
                            break
            code = src[ip]
            ip += 1
            ref -= code
            if level == 2 and code == 255 and ofs == 31 << 8:
                ofs = (src[ip] << 8) | src[ip + 1]
                ip += 2
                ref = len(out) - ofs - 8191 - 1
            length += 3
            if ref < 0 or len(out) + length > size:
                raise ValueError("FastLZ: bad reference")
            for _ in range(length):
                out.append(out[ref])
                ref += 1
        else:
            ctrl += 1
            if ip + ctrl > n or len(out) + ctrl > size:
                raise ValueError("FastLZ: bad literal run")
            out += src[ip:ip + ctrl]
            ip += ctrl
        if ip >= n:
            break
        ctrl = src[ip]
        ip += 1
    if len(out) != size:
        raise ValueError("FastLZ: %d bytes, expected %d" % (len(out), size))
    return bytes(out)


# ---------------------------------------------------------------------------------------------
# MessagePack (NKSF chunks)

def msgpack_decode(data):
    r = Reader(data)

    def obj():
        t = r.u8()
        if t <= 0x7F:
            return t
        if 0x80 <= t <= 0x8F:
            return {str(obj()): obj() for _ in range(t & 15)}
        if 0x90 <= t <= 0x9F:
            return [obj() for _ in range(t & 15)]
        if 0xA0 <= t <= 0xBF:
            return r.take(t & 31).decode("utf-8", "replace")
        if t >= 0xE0:
            return t - 256
        be = lambda fmt, n: struct.unpack(">" + fmt, r.take(n))[0]
        simple = {0xC0: None, 0xC2: False, 0xC3: True}
        if t in simple:
            return simple[t]
        if t in (0xC4, 0xC5, 0xC6):
            n = be("BHI"[t - 0xC4], 1 << (t - 0xC4))
            return "<%d bytes: %s>" % (n, hexs(r.take(n), 32))
        if t in (0xC7, 0xC8, 0xC9):
            n = be("BHI"[t - 0xC7], 1 << (t - 0xC7))
            r.u8()
            return "<ext %d bytes>" % len(r.take(n))
        if t == 0xCA:
            return be("f", 4)
        if t == 0xCB:
            return be("d", 8)
        if 0xCC <= t <= 0xCF:
            return be("BHIQ"[t - 0xCC], 1 << (t - 0xCC))
        if 0xD0 <= t <= 0xD3:
            return be("bhiq"[t - 0xD0], 1 << (t - 0xD0))
        if 0xD4 <= t <= 0xD8:
            r.u8()
            return "<fixext %d bytes>" % len(r.take(1 << (t - 0xD4)))
        if t in (0xD9, 0xDA, 0xDB):
            n = be("BHI"[t - 0xD9], 1 << (t - 0xD9))
            return r.take(n).decode("utf-8", "replace")
        if t in (0xDC, 0xDD):
            n = be("HI"[t - 0xDC], 2 << (t - 0xDC))
            return [obj() for _ in range(n)]
        if t in (0xDE, 0xDF):
            n = be("HI"[t - 0xDE], 2 << (t - 0xDE))
            return {str(obj()): obj() for _ in range(n)}
        raise ValueError("msgpack type 0x%02X" % t)

    return obj()


# ---------------------------------------------------------------------------------------------
# NI container (Kontakt 5+: "hsin" items)

CHUNK_TYPES = {
    1: "Terminator", 3: "BNI Sound Preset", 4: "BNI Sound Header", 100: "Bank",
    101: "Authoring Application", 102: "Bank Container", 103: "Preset Container",
    104: "Binary Chunk Item", 106: "Authorization", 108: "Soundinfo Item", 109: "Preset Chunk Item",
    110: "External File Reference", 111: "Resources", 112: "Audio Sample Item",
    113: "Internal Resource Reference Item", 114: "Picture Item", 115: "Subtree Item",
    116: "Encryption Item", 117: "App Specific", 118: "Container Root",
    120: "Automation Parameters", 121: "Controller Assignments", 122: "Module", 123: "Module Bank",
}
APPLICATIONS = {1: "Guitar Rig", 2: "Kontakt", 3: "Kore", 4: "Reaktor", 5: "Maschine", 6: "Absynth",
                7: "Massive", 8: "FM8", 9: "Battery", 10: "Komplete Kontrol", 11: "SC", 31: "Traktor"}


def parse_hsin_item(data):
    """One container item: its data chunks (a linked list) and its child items."""
    r = Reader(data)
    item_data = r.block64()
    ir = Reader(item_data)
    header_version = ir.u32()
    magic = ir.ascii(4)
    if header_version != 1 or magic != "hsin":
        raise ValueError("not an hsin item (%d %r)" % (header_version, magic))
    ir.u32()
    ir.u32()
    uuid = ir.take(16).hex()
    chunks = parse_chunk_stack(ir.block64())
    version = ir.u32()
    children = []
    for _ in range(ir.u32()):
        index = ir.u32()
        domain = ir.ascii(4)
        type_id = ir.u32()
        rest = ir.d[ir.p:]
        child, used = parse_hsin_item(rest)
        ir.p += used
        children.append({"index": index, "domain": domain, "type": CHUNK_TYPES.get(type_id, type_id),
                         "item": child})
    return {"uuid": uuid, "version": version, "chunks": chunks, "children": children}, r.p


def parse_chunk_stack(data):
    """The chunks of an item; each holds the next one before its own data."""
    chunks = []
    r = Reader(data)
    while True:
        domain = r.ascii(4)
        type_id = r.u32()
        version = r.u32()
        name = CHUNK_TYPES.get(type_id, "Unknown %d" % type_id)
        chunk = {"domain": domain, "type": name, "typeId": type_id, "version": version}
        if type_id == 1:
            chunk["size"] = r.left()
            chunks.append(chunk)
            break
        inner = r.block64()
        # the rest of this block is this chunk's own data
        chunk["size"] = r.left()
        body = r.take(r.left())
        try:
            parse_chunk_data(chunk, body)
        except Exception as e:  # keep going: an unknown layout is itself a finding
            chunk["error"] = str(e)
            chunk["head"] = hexs(body)
        chunks.append(chunk)
        r = Reader(inner)
    return chunks


def parse_chunk_data(chunk, body):
    t = chunk["typeId"]
    r = Reader(body)
    if t == 101:
        r.u32()
        chunk["compressed"] = r.u8() > 0
        app = r.u32()
        chunk["application"] = APPLICATIONS.get(app, app)
        r.u32()
        chunk["applicationVersion"] = r.utf16()
    elif t == 106:
        r.u32()
        kind = r.u32()
        chunk["pidContent"] = kind
        if kind == 1:
            r.u32()
            chunk["snpids"] = [s for s in (r.utf16() for _ in range(r.u32())) if s.strip()]
    elif t == 108:
        r.u32()
        chunk["soundinfoVersion"] = "%d.%d.%d" % (r.u32(), r.u32(), r.u32())
        for k in ("name", "author", "vendor", "description"):
            chunk[k] = r.utf16()
        r.take(4 + 8 + 8 + 8 + 4 + 4)
        chunk["tags"] = [r.utf16() for _ in range(r.u32())]
        chunk["attributes"] = [r.utf16() for _ in range(r.u32())]
        r.u32()
        props = {}
        for _ in range(r.u32()):
            k = r.utf16()
            props[k] = r.utf16()
        chunk["properties"] = props
    elif t == 118:
        r.u32()
        v = r.u32()
        chunk["niSoundVersion"] = "%d.%d.%d" % (v >> 20 & 0xFF, v >> 12 & 0xFF, v & 0xFFF)
        chunk["repositoryMagic"] = r.u32()
        chunk["repositoryType"] = r.u32()
    elif t == 109:
        r.u32()
        chunk["dictionaryType"] = r.u32()
        count = r.u32()
        size = r.u32()
        r.u32()
        if count == 1 and size <= r.left():
            chunk["_preset"] = r.take(size)
        chunk["items"] = count
    elif t == 115:
        r.u32()
        compressed = r.u8() > 0
        chunk["compressed"] = compressed
        if compressed:
            usize = r.u32()
            csize = r.u32()
            packed = r.take(csize)
            try:
                inner = fastlz_decompress(packed, usize)
            except Exception as e:
                chunk["encrypted"] = True
                chunk["note"] = "not FastLZ (%s): encrypted, left closed" % e
                return
        else:
            inner = r.take(r.left())
        chunk["subtree"], _ = parse_hsin_item(inner)
    elif t == 121:
        r.u32()
        rest = r.take(r.left())
        chunk["data"] = hexs(rest, 256)
        chunk["strings"] = strings_in(rest)[:100]
    elif t == 116:
        chunk["data"] = hexs(body, 32)
    elif t in (3, 4):
        chunk["data"] = hexs(body, 64)
    else:
        if body:
            chunk["head"] = hexs(body, 48)
            s = strings_in(body)
            if s:
                chunk["strings"] = s[:50]


def walk_chunks(item):
    for c in item["chunks"]:
        yield c
        if "subtree" in c:
            yield from walk_chunks(c["subtree"])
    for ch in item["children"]:
        yield from walk_chunks(ch["item"])


def read_hsin(data, out):
    item, _ = parse_hsin_item(data)
    chunks = list(walk_chunks(item))
    for c in chunks:
        if c["typeId"] == 101:
            out["application"] = "%s %s" % (c.get("application"), c.get("applicationVersion"))
        elif c["typeId"] == 106 and c.get("snpids"):
            out.setdefault("snpids", []).extend(c["snpids"])
        elif c["typeId"] == 108:
            out["soundinfo"] = {k: c[k] for k in ("name", "author", "vendor", "description", "tags",
                                                  "attributes", "properties") if c.get(k)}
        elif c.get("encrypted"):
            out["encrypted"] = True
    presets = [c.pop("_preset") for c in chunks if "_preset" in c]
    out["container"] = item
    if presets:
        out["kontakt"] = [read_kontakt_preset(p) for p in presets]
    elif out.get("snpids") or out.get("encrypted"):
        out["encrypted"] = True
        out["note"] = ("the preset data is encrypted (library %s): only the metadata above is in "
                       "the clear" % ", ".join(out.get("snpids", [])))


# ---------------------------------------------------------------------------------------------
# Kontakt 5+ preset data (the Preset Chunk Item)

PRESET_IDS = {
    0x00: "PAR_MOD_BASE", 0x03: "BANK", 0x04: "GROUP", 0x06: "PAR_SCRIPT", 0x0C: "PAR_EXTERNAL_MOD",
    0x0D: "PAR_INTERNAL_MOD", 0x17: "PAR_FX_SEND_LEVELS", 0x25: "PAR_FX", 0x28: "PROGRAM",
    0x29: "PROGRAM_CONTAINER", 0x2C: "ZONE", 0x32: "VOICE_GROUPS", 0x33: "GROUP_LIST",
    0x34: "ZONE_LIST", 0x35: "PRIVATE_RAW_OBJECT", 0x36: "PROGRAM_LIST", 0x37: "SLOT_LIST",
    0x38: "STAR_CRIT_LIST", 0x39: "LOOP_ARRAY", 0x3A: "PARAMETER_ARRAY_8", 0x3B: "PARAMETER_ARRAY_16",
    0x3C: "PARAMETER_ARRAY_32", 0x3D: "FILENAME_LIST", 0x3E: "OUTPUT_CONFIGURATION", 0x45: "INSERT_BUS",
    0x47: "SAVE_SETTINGS", 0x48: "MULTI_CONFIGURATION", 0x4A: "PAR_GROUP_DYNAMICS",
    0x4B: "FILENAME_LIST_EX", 0x4E: "QUICK_BROWSE_DATA",
}
STRUCTURED = {0x03, 0x29, 0x28, 0x06, 0x17, 0x32, 0x3A, 0x45, 0x47, 0x4E}


class PChunk:
    __slots__ = ("id", "version", "private", "public", "children")

    def __init__(self, cid=-1):
        self.id = cid
        self.version = -1
        self.private = b""
        self.public = b""
        self.children = []


def read_pchunk(r):
    c = PChunk(r.u16())
    size = r.u32()
    body = Reader(r.take(size))
    if c.id == 0x33:
        read_parray(c, body, False)
        for ch in c.children:
            ch.id = 0x04
    elif c.id == 0x34:
        read_parray(c, body, True)
    elif c.id in STRUCTURED:
        read_pstruct(c, body, size)
    elif c.id in (0x3B, 0x3C):
        if body.u8() != 0:
            raise ValueError("fixed array")
        c.version = body.u16()
        for _ in range(16 if c.id == 0x3B else 32):
            if body.u8():
                ch = PChunk(body.u16())
                ch.public = body.take(body.u32())
                c.children.append(ch)
    else:
        c.public = body.take(size)
    return c


def read_parray(c, r, with_ref):
    for _ in range(r.u32()):
        ch = PChunk(r.u32() if with_ref else -1)
        read_pstruct(ch, r, None)
        c.children.append(ch)


def read_pstruct(c, r, size):
    if r.u8() == 0:
        if size:
            c.public = r.take(size - 1)
        return
    c.version = r.u16()
    c.private = r.take(r.u32())
    c.public = r.take(r.u32())
    kids = Reader(r.take(r.u32()))
    while kids.left() > 0:
        c.children.append(read_pchunk(kids))


def read_kontakt_preset(data):
    out = {"size": len(data)}
    top = []
    r = Reader(data)
    try:
        while r.left() > 0:
            top.append(read_pchunk(r))
    except Exception as e:
        out["error"] = "preset chunks: %s" % e
    out["tree"] = [tree_summary(c) for c in top]
    programs = []
    for p in find_pchunks(top, 0x28):
        try:
            programs.append(read_program(p))
        except Exception as e:
            programs.append({"error": str(e)})
    for slot in find_pchunks(top, 0x37):
        try:
            programs.extend(read_slot_list(slot))
        except Exception as e:
            programs.append({"error": "slot list: %s" % e})
    out["programs"] = programs
    for fl in find_pchunks(top, 0x3D) + find_pchunks(top, 0x4B):
        try:
            out["files"] = read_file_list(fl)
        except Exception as e:
            out["filesError"] = str(e)
    scripts = find_pchunks(top, 0x06)
    if scripts:
        out["scriptChunks"] = [{"version": s.version, "bytes": len(s.private) + len(s.public),
                                "children": len(s.children)} for s in scripts]
    return out


def find_pchunks(chunks, cid):
    found = []
    for c in chunks:
        if c.id == cid:
            found.append(c)
        else:
            found.extend(find_pchunks(c.children, cid))
    return found


def tree_summary(c, depth=0):
    """The chunk tree for later reverse engineering: ids, versions, sizes; big lists counted."""
    name = PRESET_IDS.get(c.id, "0x%X" % c.id) if c.id >= 0 else "item"
    node = {"id": name, "version": c.version, "private": len(c.private), "public": len(c.public)}
    if c.id == 0x06:
        node["note"] = "script: content not extracted"
    elif c.public and len(c.public) <= 64:
        node["publicHex"] = c.public.hex(" ")
    if c.children:
        if len(c.children) > 8 or depth > 6:
            node["childCount"] = len(c.children)
            node["childIds"] = dict(Counter(PRESET_IDS.get(k.id, "0x%X" % k.id) if k.id >= 0 else "item"
                                            for k in c.children))
            node["first"] = tree_summary(c.children[0], depth + 1)
        else:
            node["children"] = [tree_summary(k, depth + 1) for k in c.children]
    return node


ICONS = ["Organ", "Cello", "Drum Kit", "Bell", "Trumpet", "Guitar", "Piano", "Marimba",
         "Record Player", "E-Piano", "Drum Pads", "Bass Guitar", "Electric Guitar", "Wave", "Asian Symbol",
         "Flute", "Speaker", "Score", "Conga", "Pipe Organ", "FX", "Computer", "Violin", "Surround",
         "Synthesizer", "Microphone", "Oboe", "Saxophone", "New"]


def read_program(c):
    r = Reader(c.public)
    p = {"name": r.utf16(), "sizeOfSamples": r.f64()}
    t = r.u8()
    p["midiTranspose"] = t - 256 if t > 128 else t
    p["volume"], p["pan"], p["tune"] = r.f32(), r.f32(), r.f32()
    p["velocityRange"] = [r.u8(), r.u8()]
    p["keyRange"] = [r.u8(), r.u8()]
    ks = r.u16()
    p["defaultKeyswitch"] = None if ks == 0xFFFF else ks
    p["dfdPreload"] = r.u32()
    p["libraryId"] = r.u32()
    p["fingerprint"] = r.u32()
    p["loadingFlags"] = r.u32()
    p["groupSolo"] = r.u8() > 0
    icon = r.u32()
    p["icon"] = ICONS[icon] if icon < len(ICONS) else icon
    p["credits"] = r.utf16()
    p["author"] = r.utf16()
    p["url"] = r.utf16()
    p["categories"] = [r.u16(), r.u16(), r.u16()]
    p["more"] = hexs(r.take(r.left()))
    groups, zones = [], []
    other = Counter()
    for ch in c.children:
        if ch.id == 0x33:
            for g in ch.children:
                groups.append(read_group(g))
        elif ch.id == 0x34:
            for z in ch.children:
                zones.append(read_zone(z))
        else:
            other[PRESET_IDS.get(ch.id, "0x%X" % ch.id)] += 1
    p["groups"] = groups
    p["zoneFields"] = ZONE_FIELDS
    p["zones"] = zones
    p["otherChunks"] = dict(other)
    return p


def read_slot_list(c):
    r = Reader(c.public)
    flags = int.from_bytes(r.take(8), "little")
    programs = []
    for slot in range(64):
        if not flags >> slot & 1:
            continue
        container = read_pchunk(r)
        for ch in container.children:
            if ch.id != 0x36:
                continue
            holder = PChunk()
            read_parray(holder, Reader(ch.public), False)
            for pc in holder.children:
                prog = read_program(pc)
                prog["slot"] = slot
                programs.append(prog)
    return programs


def read_group(c):
    r = Reader(c.public)
    g = {"name": r.utf16(), "volume": r.f32(), "pan": r.f32(), "tune": r.f32(),
         "keyTracking": r.u8() > 0, "reverse": r.u8() > 0, "releaseTrigger": r.u8() > 0,
         "releaseTriggerNoteMonophonic": r.u8() > 0, "releaseTriggerCounter": r.s32(),
         "midiChannel": r.s16(), "voiceGroup": r.s32(), "fxIdxAmpSplitPoint": r.s32(),
         "muted": r.u8() > 0, "soloed": r.u8() > 0, "interpolation": r.s32(), "version": c.version}
    mods = Counter()
    for ch in c.children:
        for k in ch.children:
            mods[PRESET_IDS.get(k.id, "0x%X" % k.id)] += 1
    if mods:
        g["modulators"] = dict(mods)
    return g


ZONE_FIELDS = ["group", "keyLow", "keyHigh", "velLow", "velHigh", "root", "fadeKeyLow", "fadeKeyHigh",
               "fadeVelLow", "fadeVelHigh", "volume", "pan", "tune", "sampleStart", "sampleEnd", "file",
               "bits", "rate", "channels", "frames", "loops"]


def read_zone(c):
    r = Reader(c.public)
    v = c.version
    start, end = r.u32(), r.u32()
    r.u32()
    vl, vh, kl, kh = r.u16(), r.u16(), r.u16(), r.u16()
    fvl, fvh, fkl, fkh = r.u16(), r.u16(), r.u16(), r.u16()
    root = r.u16()
    vol, pan, tune = r.f32(), r.f32(), r.f32()
    fid = bits = rate = chans = frames = None
    if v >= 0x9A:
        r.take(6)
    if r.left() >= 17:
        fid, bits, rate, chans, frames = r.s32(), r.u32(), r.u32(), r.u8(), r.u32()
    loops = []
    for ch in c.children:
        if ch.id == 0x39 and len(ch.public) >= 2:
            lr = Reader(ch.public)
            enabled = lr.u16()
            for i in range(8):
                if enabled >> i & 1 and lr.left() >= 23:
                    lr.u16()
                    loops.append([lr.s32(), lr.u32(), lr.u32(), lr.u32(), lr.u8(), lr.f32(), lr.u32()])
                    if lr.left():
                        lr.u8()
    return [c.id, kl, kh, vl, vh, root, fkl, fkh, fvl, fvh, vol, pan, tune, start, end, fid,
            bits, rate, chans, frames, loops or None]


def read_file_list(c):
    r = Reader(c.public)
    if c.id == 0x4B:
        version = r.u16()
        if version == 3:
            n = r.s32()
            r.take(8)
            files = []
            for i in range(max(0, n - 1)):
                files.append(read_path(r))
                if r.left():
                    r.take(4 + 4 + 20)
            return [f for f in files if f]
    special = [read_path(r) for _ in range(r.s32())] if r.left() else []
    samples = [read_path(r) for _ in range(r.s32())] if r.left() else []
    return [f for f in special + samples if f]


def read_path(r):
    parts = []
    for _ in range(r.u32()):
        t = r.u8()
        if t == 0:
            d = r.ascii(2).strip()
            parts.append(d + ":/" if d else "/")
        elif t == 1:
            d = r.utf16()
            parts.append(d + ":/" if d else "/")
        elif t == 2:
            parts.append(r.utf16() + "/")
        elif t == 3:
            parts.append("../")
        elif t in (4, 8, 9):
            parts.append(r.utf16())
        elif t == 6:
            pass
        else:
            raise ValueError("path segment type %d" % t)
    return "".join(parts)


# ---------------------------------------------------------------------------------------------
# NI file container ("/\ NI FC MTD  /\")

FC_MTD = b"/\\ NI FC MTD  /\\"
FC_TOC = b"/\\ NI FC TOC  /\\"


def read_fc_toc(f, base=0):
    """Table of contents: [(index, name, start, end)] with absolute offsets."""
    f.seek(base)
    head = f.read(16 + 248 + 8 + 16)
    if head[:16] != FC_MTD or head[264:272] != b"\xF0" * 8:
        raise ValueError("not an NI file container")
    count, total = struct.unpack("<QQ", head[272:288])
    if count > 100000:
        raise ValueError("%d files" % count)
    toc = f.read(16 + 600)
    if toc[:16] != FC_TOC:
        raise ValueError("no table of contents")
    entries = []
    for _ in range(count):
        e = f.read(8 + 16 + 600 + 8 + 8)
        index = struct.unpack("<Q", e[:8])[0]
        name = e[24:624].decode("utf-16-le", "replace").split("\x00")[0].strip()
        end = struct.unpack("<Q", e[632:640])[0]
        entries.append((index, name, end))
    tail = f.read(8 + 16 + 16 + 592)
    if tail[:8] != b"\xF1" * 8 or tail[24:40] != FC_TOC:
        raise ValueError("table of contents does not end as expected")
    data_start = f.tell()
    out, prev = [], 0
    for index, name, end in sorted(entries):
        out.append((index, name, data_start + prev, data_start + end))
        prev = end
    return out, total


def read_fc(path, f, out, opts):
    f.seek(0)
    head = f.read(512)
    base = 0
    if head[264:272] != b"\xF0" * 8:
        # a .nicnt: its version and ProductHints XML first, the file container after
        out["contentVersion"] = head[66:132].decode("utf-16-le", "replace").split("\x00")[0]
        start = struct.unpack("<I", head[144:148])[0]
        f.seek(256)
        raw = f.read(max(0, min(start, 16 << 20))).split(b"\x00")[0]
        if raw.lstrip().startswith(b"<"):
            opts.save_text(path, "ProductHints.xml", raw)
            out["productHints"] = xml_fields(raw)
        base = start + 256
    entries, total = read_fc_toc(f, base)
    out["format"] = "NI file container"
    out["totalSize"] = total
    out["entries"] = [{"name": n, "size": e - s} for _, n, s, e in entries]
    for index, name, s, e in entries:
        ext = os.path.splitext(name.lower())[1]
        size = e - s
        if ext in HSIN_EXT and size <= MAX_READ:
            f.seek(s)
            sub = {"name": name}
            try:
                read_hsin(f.read(size), sub)
            except Exception as ex:
                sub["error"] = str(ex)
            out.setdefault("instruments", []).append(sub)
        elif SCRIPTS.search(name):
            out.setdefault("scriptsNotExtracted", []).append({"name": name, "size": size})
        elif ext in TEXT_EXT and size <= MAX_TEXT:
            f.seek(s)
            opts.save_text(path, name, f.read(size))
        elif ext in IMAGE_EXT and opts.images and size <= MAX_TEXT:
            f.seek(s)
            opts.save_file(path, name, f.read(size))
        elif ext in (".wav", ".ncw"):
            out["samples"] = out.get("samples", 0) + 1


def xml_fields(raw):
    """The leaf elements of an XML document as {path: text}, sensitive names left out."""
    try:
        doc = xml.dom.minidom.parseString(raw)
    except Exception as e:
        return {"error": str(e)}
    fields = {}

    def walk(node, prefix):
        for ch in node.childNodes:
            if ch.nodeType != ch.ELEMENT_NODE:
                continue
            path = prefix + "/" + ch.tagName
            if SENSITIVE.search(ch.tagName):
                fields[path] = "(left out)"
                continue
            elems = [k for k in ch.childNodes if k.nodeType == k.ELEMENT_NODE]
            if elems:
                walk(ch, path)
            else:
                text = "".join(k.data for k in ch.childNodes if k.nodeType == k.TEXT_NODE).strip()
                if len(text) > 200:
                    text = text[:200] + "… (%d chars)" % len(text)
                key = path
                i = 2
                while key in fields:
                    key = "%s[%d]" % (path, i)
                    i += 1
                fields[key] = text
    walk(doc, "")
    return fields


# ---------------------------------------------------------------------------------------------
# NKS archives (Kontakt 2-4 era containers still used by .nkx / .nkr / .nkc)

NKS_DIR, NKS_ENC, NKS_FILE, NKS_CONTENT, NKS_NKI = 0x5E70AC54, 0x16CCF80A, 0x4916E63C, 0x2AE905FA, 0x7FA89012
NKS_TYPES = {1: "directory", 2: "encrypted file", 3: "file", 4: "content file"}


def read_nks_archive(f, out, size):
    f.seek(0)
    magic = struct.unpack("<I", f.read(4))[0]
    start = 0
    if magic == NKS_NKI:
        start = 4 + 218
    out["format"] = "NKS archive"
    counts = Counter()
    names = []
    tree = read_nks_dir(f, start, "", names, counts, size, 0)
    out["directory"] = tree
    out["entryTypes"] = dict(counts)
    out["_names"] = names


def read_nks_dir(f, offset, path, names, counts, size, depth):
    f.seek(offset)
    h = f.read(22)
    magic, version, set_id = struct.unpack("<IHI", h[:10])
    if magic != NKS_DIR:
        raise ValueError("no directory at %d" % offset)
    count = struct.unpack("<I", h[14:18])[0]
    node = {"path": path or "/", "version": "0x%04X" % version, "setId": set_id, "entries": count}
    if count > 1000000 or depth > 32:
        raise ValueError("implausible directory")
    entries = []
    for _ in range(count):
        if version == 0x0100:
            e = f.read(136)
            name = e[:129].split(b"\x00")[0].decode("latin-1")
            off, typ = struct.unpack("<IH", e[130:136])
        elif version in (0x0110, 0x0111):
            e = f.read(8)
            off, typ = struct.unpack("<IH", e[2:8])
            raw = bytearray()
            while True:
                ch = f.read(2)
                if len(ch) < 2 or ch == b"\x00\x00":
                    break
                raw += ch
            name = raw.decode("utf-16-le", "replace")
        else:
            raise ValueError("directory version 0x%04X" % version)
        entries.append((name, off, typ))
    subdirs = []
    for name, off, typ in entries:
        kind = NKS_TYPES.get(typ, "type %d" % typ)
        counts[kind] += 1
        full = path + "/" + name
        if typ == 1 and off < size:
            try:
                subdirs.append(read_nks_dir(f, off, full, names, counts, size, depth + 1))
            except Exception as e:
                subdirs.append({"path": full, "error": str(e)})
            continue
        entry_size = None
        if typ in (3, 4) and off + 30 < size:
            # plain headers: content file (magic, version, set id, key index, size),
            # file (magic, version, 13 bytes, size)
            f.seek(off)
            eh = f.read(32)
            m = struct.unpack("<I", eh[:4])[0]
            if m == NKS_CONTENT:
                entry_size = struct.unpack("<I", eh[14:18])[0]
            elif m == NKS_FILE:
                entry_size = struct.unpack("<I", eh[19:23])[0]
        names.append((full, kind, entry_size))
    if subdirs:
        node["subdirectories"] = subdirs
    return node


# ---------------------------------------------------------------------------------------------
# NKS presets (RIFF NIKS)

def read_nksf(data, out, opts, path):
    r = Reader(data)
    if r.ascii(4) != "RIFF":
        raise ValueError("not RIFF")
    r.u32()
    out["format"] = "NKS preset (%s)" % r.ascii(4)
    chunks = {}
    while r.left() >= 8:
        cid = r.ascii(4)
        n = r.u32()
        body = r.take(min(n, r.left()))
        if n & 1 and r.left():
            r.u8()
        if cid in ("NISI", "NICA", "PLID"):
            try:
                chunks[cid] = {"version": struct.unpack("<I", body[:4])[0], "data": msgpack_decode(body[4:])}
            except Exception as e:
                chunks[cid] = {"error": str(e), "head": hexs(body)}
        elif cid == "PCHK":
            info = {"size": len(body)}
            inner = body[4:]
            if len(inner) > 16 and inner[12:16] == b"hsin":
                sub = {}
                try:
                    read_hsin(inner, sub)
                except Exception as e:
                    sub["error"] = str(e)
                info["kontakt"] = sub
            else:
                s = strings_in(inner)
                if s:
                    info["strings"] = s[:100]
            chunks[cid] = info
        else:
            chunks[cid] = {"size": len(body)}
    out["chunks"] = chunks


# ---------------------------------------------------------------------------------------------
# Other files

def strings_in(data, minlen=5):
    found = [m.group().decode("latin-1") for m in re.finditer(rb"[\x20-\x7E]{%d,}" % minlen, data)]
    found += [m.group().decode("utf-16-le") for m in re.finditer(rb"(?:[\x20-\x7E]\x00){%d,}" % minlen, data)]
    seen, out = set(), []
    for s in found:
        if s not in seen:
            seen.add(s)
            out.append(s)
    return out


def zlib_streams(data, limit=50):
    streams = []
    for m in re.finditer(rb"\x78[\x01\x5E\x9C\xDA]", data):
        try:
            d = zlib.decompressobj()
            raw = d.decompress(data[m.start():m.start() + (8 << 20)], 32 << 20)
        except zlib.error:
            continue
        if len(raw) >= 16:
            streams.append((m.start(), raw))
            if len(streams) >= limit:
                break
    return streams


def read_wav(f, out):
    f.seek(0)
    head = f.read(12)
    if head[:4] != b"RIFF" or head[8:12] != b"WAVE":
        return
    info = {}
    while True:
        h = f.read(8)
        if len(h) < 8:
            break
        cid, n = h[:4], struct.unpack("<I", h[4:])[0]
        if cid == b"fmt ":
            b = f.read(n)
            fmt, ch, rate = struct.unpack("<HHI", b[:8])
            info.update(format=fmt, channels=ch, rate=rate, bits=struct.unpack("<H", b[14:16])[0])
        elif cid == b"smpl":
            b = f.read(n)
            unity, frac = struct.unpack("<II", b[12:20])
            nloops = struct.unpack("<I", b[28:32])[0]
            info["root"] = unity
            info["loops"] = [list(struct.unpack("<IIIIII", b[36 + 24 * i:60 + 24 * i])[1:4])
                             for i in range(min(nloops, 16)) if 60 + 24 * i <= len(b)]
        elif cid == b"data":
            info["dataBytes"] = n
            f.seek(n + (n & 1), 1)
        else:
            f.seek(n + (n & 1), 1)
    if info.get("channels") and info.get("bits"):
        info["frames"] = info.get("dataBytes", 0) // (info["channels"] * info["bits"] // 8)
    out["wav"] = info


def scan_unknown(f, size, out, opts, path):
    """A Kontakt file of an older or unknown layout: its zlib streams (Kontakt 2-4 keep their
    XML there) or else its readable strings."""
    out.setdefault("format", "Kontakt (older or unknown layout)")
    f.seek(0)
    data = f.read(min(size, MAX_READ))
    streams = zlib_streams(data)
    if streams:
        out["zlibStreams"] = []
        for off, raw in streams:
            entry = {"offset": off, "size": len(raw)}
            if raw.lstrip()[:1] == b"<":
                opts.save_text(path, "zlib@%d.xml" % off, raw)
                entry["xml"] = True
            else:
                entry["strings"] = strings_in(raw)[:50]
            out["zlibStreams"].append(entry)
    else:
        out["strings"] = strings_in(data[:1 << 20])[:200]


class Options:
    def __init__(self, outdir, images):
        self.outdir = outdir
        self.images = images
        self.saved = []

    def _target(self, source, name):
        base = re.sub(r"[^\w .-]+", "_", os.path.basename(source))
        inner = re.sub(r"[^\w .-]+", "_", name.replace("\\", "/")).strip("_") or "data"
        path = os.path.join(self.outdir, "extracted", base, inner)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        return path

    def save_text(self, source, name, data):
        path = self._target(source, name)
        with open(path, "wb") as fh:
            fh.write(data)
        self.saved.append(os.path.relpath(path, self.outdir))

    def save_file(self, source, name, data):
        self.save_text(source, name, data)


def examine(path, opts):
    """What one file tells."""
    try:
        st = os.stat(path)
    except OSError as e:
        return {"path": private(path), "error": str(e)}
    out = {"path": private(path), "size": st.st_size,
           "modified": datetime.datetime.fromtimestamp(st.st_mtime).isoformat(timespec="seconds")}
    ext = os.path.splitext(path)[1].lower()
    try:
        with open(path, "rb") as f:
            head = f.read(64)
            out["magic"] = head[:16].hex(" ")
            m32 = struct.unpack("<I", head[:4])[0] if len(head) >= 4 else 0
            if head[:16] == FC_MTD:
                read_fc(path, f, out, opts)
            elif len(head) >= 16 and head[12:16] == b"hsin":
                out["format"] = "NI container"
                if st.st_size <= MAX_READ:
                    f.seek(0)
                    read_hsin(f.read(), out)
            elif m32 == NKS_DIR or (m32 == NKS_NKI and ext not in HSIN_EXT):
                try:
                    read_nks_archive(f, out, st.st_size)
                except Exception as e:
                    out["archiveError"] = str(e)
                    scan_unknown(f, st.st_size, out, opts, path)
            elif head[:4] == b"RIFF" and head[8:12] == b"NIKS":
                f.seek(0)
                read_nksf(f.read(min(st.st_size, MAX_READ)), out, opts, path)
            elif head[:4] == b"RIFF" and head[8:12] == b"WAVE":
                out["format"] = "WAV"
                read_wav(f, out)
            elif ext in TEXT_EXT:
                out["format"] = "text"
                if SCRIPTS.search(path):
                    out["note"] = "script: content not extracted"
                elif st.st_size <= MAX_TEXT:
                    f.seek(0)
                    opts.save_text(path, os.path.basename(path), f.read())
            elif ext in IMAGE_EXT:
                out["format"] = "image"
                if opts.images and st.st_size <= MAX_TEXT:
                    f.seek(0)
                    opts.save_file(path, os.path.basename(path), f.read())
            elif ext in (".db3", ".db", ".sqlite"):
                out["format"] = "SQLite"
                out["database"] = read_sqlite(path, opts)
            elif ext in (".ncw",):
                out["format"] = "NCW sample"
            elif ext in HSIN_EXT or ext in (".nkx", ".nkr", ".nkc", ".nicnt"):
                scan_unknown(f, st.st_size, out, opts, path)
    except Exception as e:
        out["error"] = "%s: %s" % (type(e).__name__, e)
    return out


def read_sqlite(path, opts):
    info = {"tables": {}}
    try:
        con = sqlite3.connect("file:%s?mode=ro&immutable=1" % path.replace("\\", "/"), uri=True)
    except Exception as e:
        return {"error": str(e)}
    try:
        tables = [r[0] for r in con.execute("select name from sqlite_master where type='table'")]
        for t in tables:
            try:
                cols = [r[1] for r in con.execute('pragma table_info("%s")' % t)]
                n = con.execute('select count(*) from "%s"' % t).fetchone()[0]
                rows = []
                safe = [c for c in cols if not SENSITIVE.search(c)]
                if safe:
                    for row in con.execute('select %s from "%s"' % (",".join('"%s"' % c for c in safe), t)):
                        text = " ".join(str(v) for v in row if isinstance(v, str))
                        if opts.match.search(text):
                            rows.append({c: (v if not isinstance(v, bytes) else "<%d bytes>" % len(v))
                                         for c, v in zip(safe, row)})
                            if len(rows) >= 20000:
                                break
                info["tables"][t] = {"columns": cols, "rows": n, "matching": rows}
            except Exception as e:
                info["tables"][t] = {"error": str(e)}
    finally:
        con.close()
    return info


# ---------------------------------------------------------------------------------------------
# Windows: where the library and NI's own data are

def windows_sources(match):
    """(library folders, registry entries, other files worth examining)."""
    roots, registry, others = [], [], []
    try:
        import winreg
    except ImportError:
        return roots, registry, others
    for hive, hname in ((winreg.HKEY_LOCAL_MACHINE, "HKLM"), (winreg.HKEY_CURRENT_USER, "HKCU")):
        for base in (r"SOFTWARE\Native Instruments", r"SOFTWARE\WOW6432Node\Native Instruments"):
            try:
                key = winreg.OpenKey(hive, base)
            except OSError:
                continue
            i = 0
            while True:
                try:
                    sub = winreg.EnumKey(key, i)
                except OSError:
                    break
                i += 1
                try:
                    sk = winreg.OpenKey(key, sub)
                except OSError:
                    continue
                values = {}
                j = 0
                while True:
                    try:
                        name, value, _ = winreg.EnumValue(sk, j)
                    except OSError:
                        break
                    j += 1
                    if SENSITIVE.search(name):
                        values[name] = "(left out)"
                    elif isinstance(value, (str, int)):
                        values[name] = private(value) if isinstance(value, str) else value
                    else:
                        values[name] = "<%s>" % type(value).__name__
                named = match.search(sub) or any(match.search(str(v)) for v in values.values())
                registry.append({"key": "%s\\%s\\%s" % (hname, base, sub), "values": values,
                                 "matches": bool(named)})
                if named:
                    for vn in ("ContentDir", "InstallDir", "ContentDirectory"):
                        d = values.get(vn)
                        if isinstance(d, str):
                            d = d.replace("%USERPROFILE%", HOME)
                            if os.path.isdir(d) and d not in roots:
                                roots.append(d)
    env = os.environ.get
    candidates = [
        os.path.join(env("CommonProgramFiles", r"C:\Program Files\Common Files"), "Native Instruments", "Service Center"),
        os.path.join(env("LOCALAPPDATA", ""), "Native Instruments"),
        os.path.join(env("APPDATA", ""), "Native Instruments"),
        os.path.join(env("PROGRAMDATA", r"C:\ProgramData"), "Native Instruments"),
        os.path.join(env("APPDATA", ""), "Spitfire Audio"),
        os.path.join(env("LOCALAPPDATA", ""), "Spitfire Audio"),
        os.path.join(env("PROGRAMDATA", r"C:\ProgramData"), "Spitfire Audio"),
        os.path.join(documents_folder(), "Native Instruments", "User Content"),
        os.path.join(documents_folder(), "Spitfire Audio"),
    ]
    for folder in candidates:
        if not os.path.isdir(folder):
            continue
        for dirpath, dirnames, filenames in os.walk(folder):
            for fn in filenames:
                p = os.path.join(dirpath, fn)
                ext = os.path.splitext(fn)[1].lower()
                if ext in (".db3", ".db", ".sqlite"):
                    others.append({"path": p})      # only rows naming the library are kept
                    continue
                interesting = ext in TEXT_EXT or ext in (".nksf", ".nksn") or ext in HSIN_EXT
                if not interesting or PERSONAL_FILE.search(fn):
                    others.append({"path": private(p), "listed": True})
                    continue
                if match.search(p) or file_mentions(p, match):
                    others.append({"path": p})
    return roots, registry, others


def file_mentions(path, match):
    try:
        with open(path, "rb") as f:
            data = f.read(1 << 20)
    except OSError:
        return False
    return bool(match.search(data.decode("latin-1")) or match.search(data.decode("utf-16-le", "ignore")))


# ---------------------------------------------------------------------------------------------
# Output

def sanitize_saved_text(path):
    """Leave sensitive values out of copied XML/JSON/INI text."""
    try:
        with open(path, "rb") as f:
            raw = f.read()
        text = raw.decode("utf-8")
    except (OSError, UnicodeDecodeError):
        return
    words = r"(?:serial|licen[cs]e|password|token|activation|e-?mail|jdx|credential|cookie|session|jwt|secret)"
    pat = re.compile(r"(<(\w*" + words + r"\w*)[^>]*>)[^<]*(</\2>)|(\"\w*" + words + r"\w*\"\s*:\s*)\"[^\"]*\""
                     r"|^(\s*\w*" + words + r"\w*\s*=).*$", re.I | re.M)

    def repl(m):
        if m.group(1):
            return m.group(1) + "(left out)" + m.group(3)
        if m.group(4):
            return m.group(4) + '"(left out)"'
        return m.group(5) + " (left out)"

    new = pat.sub(repl, text)
    if new != text:
        with open(path, "w", encoding="utf-8") as f:
            f.write(new)


def run(args):
    match = re.compile(args.match, re.I)
    stamp = datetime.datetime.now().strftime("%Y-%m-%d %H%M")
    base = args.out or os.path.join(documents_folder(), "MuseScore Sound Library Check")
    outdir = os.path.join(base, "%s files %s" % (args.name, stamp))
    os.makedirs(outdir, exist_ok=True)
    opts = Options(outdir, args.images)
    opts.match = match

    roots = list(args.paths)
    registry, extra = [], []
    if not args.no_system:
        found, registry, extra = windows_sources(match)
        if not roots:
            roots = found
    if not roots and sys.stdin and sys.stdin.isatty():
        print("The library's folder was not found. Drag its folder here and press Enter:")
        typed = input("> ").strip().strip('"')
        if typed:
            roots = [typed]
    if not roots and not extra:
        print("Nothing to read. Give the library's folder: extract_library_files.py \"D:\\...\\Spitfire Symphony Orchestra\"")
        return None

    files = []
    for root in roots:
        if os.path.isfile(root):
            files.append(root)
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames.sort()
            for fn in sorted(filenames):
                files.append(os.path.join(dirpath, fn))
    listed = [e for e in extra if e.get("listed")]
    files += [e["path"] for e in extra if not e.get("listed")]

    results = []
    sample_names = {}
    for i, p in enumerate(files):
        if i % 50 == 0:
            print("%d/%d %s" % (i + 1, len(files), private(p)[-90:]), flush=True)
        res = examine(p, opts)
        names = res.pop("_names", None)
        if names:
            sample_names[res["path"]] = names
            res["namesListed"] = len(names)
        results.append(res)
    for p in opts.saved:
        sanitize_saved_text(os.path.join(outdir, p))

    # names inside archives: one text file each, and all of them together
    os.makedirs(os.path.join(outdir, "archives"), exist_ok=True)
    for src, names in sample_names.items():
        fn = re.sub(r"[^\w .-]+", "_", os.path.basename(src)) + ".txt"
        with open(os.path.join(outdir, "archives", fn), "w", encoding="utf-8") as fh:
            for name, kind, size in names:
                fh.write("%s\t%s\t%s\n" % (name, kind, "" if size is None else size))

    library = {
        "tool": "extract_library_files.py", "toolVersion": VERSION, "name": args.name,
        "match": args.match, "date": stamp, "platform": sys.platform, "python": sys.version.split()[0],
        "roots": [private(r) for r in roots], "registry": registry,
        "otherFilesListed": [e["path"] for e in listed][:5000],
        "files": results, "extracted": opts.saved,
    }
    with open(os.path.join(outdir, "library.json"), "w", encoding="utf-8") as fh:
        json.dump(library, fh, ensure_ascii=False, indent=1, default=str)
    with open(os.path.join(outdir, "inventory.csv"), "w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh)
        w.writerow(["path", "size", "modified", "format", "encrypted", "error"])
        for r in results:
            w.writerow([r["path"], r.get("size"), r.get("modified"), r.get("format", ""),
                        "yes" if r.get("encrypted") else "", r.get("error", "")])
    sizes = []
    for dirpath, _, filenames in os.walk(outdir):
        for fn in filenames:
            full = os.path.join(dirpath, fn)
            sizes.append((os.path.getsize(full), os.path.relpath(full, outdir)))
    summary = summarize(library, sample_names)
    summary += "\nLargest files written: " + ", ".join(
        "%s %.1f MB" % (n, b / 1e6) for b, n in sorted(sizes, reverse=True)[:8]) + "\n"
    with open(os.path.join(outdir, "summary.txt"), "w", encoding="utf-8") as fh:
        fh.write(summary)
    zpath = outdir + ".zip"
    with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
        for dirpath, _, filenames in os.walk(outdir):
            for fn in filenames:
                full = os.path.join(dirpath, fn)
                z.write(full, os.path.join(os.path.basename(outdir), os.path.relpath(full, outdir)))
    print(summary)
    print("\nWritten: %s\nHand back: %s (%.1f MB)" % (outdir, zpath, os.path.getsize(zpath) / 1e6))
    return outdir


def summarize(lib, sample_names=None):
    L = []
    files = lib["files"]
    L.append("%s: library files, %s (extract_library_files.py v%s, %s)" % (
        lib["name"], lib["date"], lib.get("toolVersion"), lib.get("platform")))
    L.append("Folders: %s" % ("; ".join(lib["roots"]) or "none"))
    reg = [r for r in lib.get("registry", []) if r.get("matches")]
    for r in reg:
        L.append("Registry %s: %s" % (r["key"], ", ".join("%s=%s" % kv for kv in r["values"].items())))
    total = sum(r.get("size") or 0 for r in files)
    L.append("%d files, %.1f GB" % (len(files), total / 1e9))
    fmt = Counter(r.get("format", os.path.splitext(r["path"])[1].lower() or "?") for r in files)
    L.append("By format: " + ", ".join("%s %d" % kv for kv in fmt.most_common()))
    insts = []
    for r in files:
        if r.get("soundinfo") or r.get("kontakt") or r.get("snpids"):
            insts.append((r["path"], r))
        for sub in r.get("instruments", []):
            insts.append((r["path"] + " > " + sub.get("name", "?"), sub))
    enc = [r for _, r in insts if r.get("encrypted")]
    readable = [r for _, r in insts if r.get("kontakt")]
    L.append("Kontakt presets readable: %d, encrypted: %d" % (len(readable), len(enc)))
    snp = Counter(s for _, r in insts for s in set(r.get("snpids", [])))
    if snp:
        L.append("Library SNPIDs: " + ", ".join("%s (%d files)" % kv for kv in snp.most_common()))
    apps = Counter(r["application"] for _, r in insts if r.get("application"))
    if apps:
        L.append("Saved with: " + ", ".join("%s (%d)" % kv for kv in apps.most_common()))
    errors = [r for r in files if r.get("error")]
    if errors:
        L.append("Errors: %d (first: %s: %s)" % (len(errors), errors[0]["path"], errors[0]["error"]))

    for r in files:
        if r.get("productHints"):
            L.append("")
            L.append("ProductHints of %s:" % r["path"])
            for k, v in r["productHints"].items():
                if v:
                    L.append("  %s: %s" % (k, v))

    tags = Counter()
    for _, r in insts:
        si = r.get("soundinfo") or {}
        for t in si.get("tags", []) + si.get("attributes", []):
            tags[t] += 1
    if tags:
        L.append("")
        L.append("Soundinfo tags and attributes: " + ", ".join("%s (%d)" % kv for kv in tags.most_common(60)))

    L.append("")
    L.append("Instruments:")
    for where, r in insts:
        si = r.get("soundinfo")
        name = (si or {}).get("name") or os.path.basename(where)
        line = "  %s  [%s]" % (name, os.path.basename(where))
        if r.get("encrypted"):
            line += " encrypted"
        L.append(line)
        if si and si.get("description"):
            L.append("      " + si["description"][:300].replace("\n", " "))
        for k in r.get("kontakt", []):
            for p in k.get("programs", []):
                if "error" in p:
                    L.append("      program: error %s" % p["error"])
                    continue
                zones = p.get("zones", [])
                keys = [z[1] for z in zones] + [z[2] for z in zones]
                L.append("      program %r: %d groups, %d zones, keys %s-%s, default keyswitch %s, library id %s"
                         % (p["name"], len(p["groups"]), len(zones), min(keys) if keys else "-",
                            max(keys) if keys else "-", p.get("defaultKeyswitch"), p.get("libraryId")))
                gn = [g["name"] for g in p["groups"]]
                if gn:
                    L.append("        groups: " + ", ".join(gn[:80]) + (" …" if len(gn) > 80 else ""))
            if k.get("files"):
                L.append("      %d sample files, e.g. %s" % (len(k["files"]), k["files"][0]))
            if k.get("scriptChunks"):
                L.append("      %d script chunk(s) (not extracted)" % len(k["scriptChunks"]))

    nks = [r for r in files if isinstance(r.get("chunks"), dict)]
    if nks:
        L.append("")
        L.append("NKS presets:")
        for r in nks:
            ch = r["chunks"]
            nisi = (ch.get("NISI") or {}).get("data") or {}
            L.append("  %s: %s" % (r["path"], nisi.get("name") if isinstance(nisi, dict) else nisi))
            nica = (ch.get("NICA") or {}).get("data")
            if isinstance(nica, dict):
                for pages in nica.values():
                    if isinstance(pages, list):
                        for i, page in enumerate(pages):
                            knobs = ", ".join("%s(%s)" % (k.get("name"), k.get("id")) for k in page
                                              if isinstance(k, dict) and k.get("name"))
                            if knobs:
                                L.append("    page %d: %s" % (i + 1, knobs))

    archives = [r for r in files if r.get("format") == "NKS archive" or r.get("format") == "NI file container"]
    if archives:
        L.append("")
        L.append("Archives (sample monoliths, resources):")
        for r in archives:
            n = r.get("namesListed") or len(r.get("entries", []))
            L.append("  %s: %s, %d entries %s" % (r["path"], r["format"], n, r.get("entryTypes", "")))
            names = (sample_names or {}).get(r["path"])
            if names:
                for nm in names[:3]:
                    L.append("      " + nm[0])
                L.append("      … all in archives/%s.txt" % re.sub(r"[^\w .-]+", "_", os.path.basename(r["path"])))
            if r.get("scriptsNotExtracted"):
                L.append("      scripts (not extracted): %d" % len(r["scriptsNotExtracted"]))

    dbs = [r for r in files if r.get("format") == "SQLite"]
    for r in dbs:
        tabs = (r.get("database") or {}).get("tables", {})
        hits = {t: len(v.get("matching", [])) for t, v in tabs.items() if v.get("matching")}
        L.append("")
        L.append("Database %s: %d tables; rows naming the library: %s" % (r["path"], len(tabs), hits or "none"))

    if lib.get("extracted"):
        L.append("")
        L.append("Copied files (%d): %s" % (len(lib["extracted"]), ", ".join(lib["extracted"][:40])))
    return "\n".join(L) + "\n"


def report(target):
    if target.lower().endswith(".zip"):
        with zipfile.ZipFile(target) as z:
            name = next(n for n in z.namelist() if n.endswith("library.json"))
            lib = json.loads(z.read(name).decode("utf-8"))
    else:
        with open(os.path.join(target, "library.json"), encoding="utf-8") as fh:
            lib = json.load(fh)
    print(summarize(lib))


def main(argv=None):
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")  # a console in cp1252 and a name it can't show
        except (AttributeError, ValueError):
            pass
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("paths", nargs="*", help="library folders or files (default: found in the registry)")
    ap.add_argument("--match", default=r"spitfire|symphon", help="regular expression naming the library")
    ap.add_argument("--name", default="Spitfire Symphony Orchestra", help="name for the output folder")
    ap.add_argument("--out", help="folder to write into (default Documents/MuseScore Sound Library Check)")
    ap.add_argument("--images", action="store_true", help="also copy pictures (GUI, wallpapers)")
    ap.add_argument("--no-system", action="store_true", help="don't look at the registry and NI's folders")
    ap.add_argument("--report", metavar="FOLDER_OR_ZIP", help="print the summary of an earlier extraction")
    args = ap.parse_args(argv)
    if args.report:
        report(args.report)
        return 0
    outdir = run(args)
    if getattr(sys, "frozen", False) and sys.stdin and sys.stdin.isatty():
        input("\nPress Enter to close.")
    return 0 if outdir else 1


if __name__ == "__main__":
    sys.exit(main())
