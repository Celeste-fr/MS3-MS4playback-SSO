#!/usr/bin/env python3
"""Make MuseScore's sound-library setups (.vst3state) for Kontakt patches, from their .nki files,
instead of loading each patch by hand in the plug-in's window.

    python make_setups.py                          what it would do (the setups folder, the library,
                                                   the templates found, the patches without a setup)
    python make_setups.py --only "Violins 1 - Core techniques" [--only …]
    python make_setups.py --all                    every patch of the library that has no setup yet
    python make_setups.py --only X --overwrite     also over an existing setup (kept as .bak)

What a setup is (found on SSO, 2026-09-27; see CLAUDE.md › Extract plug-in data): MuseScore's
.vst3state ("MSV3", Vst3Plugin::state) holds Kontakt's own state, an NI container (like an .nki)
whose preset data is a Kontakt multi: a BANK whose SLOT_LIST holds the patch's program, plus the
bank's sample list (FILENAME_LIST_EX). The program in it is the .nki's program, as Kontakt 8 saves
it again, and the patch's script keeps its settings (UACC & UI only …) as named values after its
code. So a setup is made from:
  - a template: a setup made by hand (any patch), for everything around the program (the multi,
    its outputs, Kontakt's browser state, the authorization);
  - the patch's .nki: its program, and its sample list with the paths made absolute (the .nki's
    are relative to where it lies);
  - the settings the owner made in the templates: the script values that differ between a
    template's own .nki and its setup (same slot, same name, same length), set in the new program
    where its script has the same value.
The script code itself is not read or written out: it is copied as it is, byte for byte.

Where: the setups folder is MuseScore's <dataPath>/soundlibraries/<library>/ (found by its .vst3state
files under %LOCALAPPDATA% / %APPDATA%, or --setups); the library by the registry (ContentDir) or
--library. A patch the map has is written under the map's name (MAP_NKI below), any other under
the .nki's name and added to addedpatches.json, so Check articulations lists it. Every setup made
is listed in generated.json there (to tell them from those made by hand).

The preset data is compressed as Kontakt does (FastLZ level 1, fastlz_compress), not as well:
Violins 1 - All techniques is a 1.9 MB setup made by hand, 2.x MB made here.
"""

import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import extract_library_files as E  # noqa: E402

LIBRARY = "Spitfire Symphony Orchestra"
MATCH = re.compile(r"spitfire symphony orchestra", re.I)

# The map's patches (share/soundlibraries/Spitfire Symphony Orchestra.xml) whose .nki isn't found
# by the rules in nki_for(): name -> the .nki's name
MAP_ALIASES = {
    "Celeste": "Tuned - Celeste", "Crotales": "Tuned - Crotales", "Desk Bells": "Tuned - Desk Bells",
    "Glockenspiel": "Tuned - Glockenspiel", "Marimba": "Tuned - Marimba", "Timpani": "Tuned - Timpani",
    "Tubular Bells": "Tuned - Tubular Bells", "Vibraphone": "Tuned - Vibraphone",
    "Xylophone": "Tuned - Xylophone", "Contrabass Trombone": "Contrabass Trombone Solo - All techniques",
    "Contrabass Tuba": "Contrabass Tuba Solo - All techniques", "Contrabassoon": "ContraBassoon - All techniques",
    "Piccolo": "Piccolo Flute - All techniques", "Solo Violin 1": "Solo Violin - All techniques",
    "Strings Ensemble": "Ensembles - All techniques",
}


def map_names():
    """The map's patch names, from the map in this repository (next to the tool when frozen)."""
    here = os.path.dirname(os.path.abspath(__file__))
    for p in (os.path.join(here, "..", "..", "share", "soundlibraries", LIBRARY + ".xml"),
              os.path.join(here, LIBRARY + ".xml"),
              os.path.join(getattr(sys, "_MEIPASS", here), LIBRARY + ".xml")):
        if os.path.isfile(p):
            with open(p, encoding="utf-8") as f:
                text = f.read()
            names = []
            for m in re.finditer(r"<Instrument\s+([^>]*)>", text):
                a = m.group(1)
                n = re.search(r'name="([^"]*)"', a)
                if n and 'kit="1"' not in a:
                    names.append(n.group(1).replace("&amp;", "&"))
            return names
    return []


def nki_for(name, nkis):
    """The .nki (a path from nkis, by its name) of a map patch."""
    for cand in (MAP_ALIASES.get(name), name, name + " - All techniques", "Other - " + name,
                 name.replace("Motif ", "") + " - All techniques"):
        if cand and cand.lower() in nkis:
            return nkis[cand.lower()]
    return None


def file_name(s):
    """MuseScore's setup file name for a patch (SoundLibraryHost::setupFile)."""
    return re.sub(r'[\\/:*?"<>|]', "_", s)


# ---------------------------------------------------------------------------------------------
# MuseScore's .vst3state (QDataStream, big-endian): "MSV3", quint32 1, QString name,
# QByteArray component state, QByteArray controller state

def read_vst3state(data):
    if data[:4] != b"MSV3":
        raise ValueError("not a MuseScore setup")
    p = 4
    version = struct.unpack(">I", data[p:p + 4])[0]
    p += 4
    n = struct.unpack(">I", data[p:p + 4])[0]
    p += 4
    name = "" if n == 0xFFFFFFFF else data[p:p + n].decode("utf-16-be")
    p += 0 if n == 0xFFFFFFFF else n
    parts = []
    for _ in range(2):
        n = struct.unpack(">I", data[p:p + 4])[0]
        p += 4
        if n == 0xFFFFFFFF:
            parts.append(b"")
        else:
            parts.append(data[p:p + n])
            p += n
    return version, name, parts[0], parts[1]


def write_vst3state(name, component, controller):
    n = name.encode("utf-16-be")
    return (b"MSV3" + struct.pack(">I", 1) + struct.pack(">I", len(n)) + n
            + struct.pack(">I", len(component)) + component + struct.pack(">I", len(controller)) + controller)


# ---------------------------------------------------------------------------------------------
# NI container, kept byte for byte where it isn't changed

class Item:
    """An "hsin" item: its header (after its size), data chunks, version and children."""

    def __init__(self, data):
        r = E.Reader(data)
        body = r.block64()
        self.size = r.p
        b = E.Reader(body)
        self.head = b.take(4 + 4 + 4 + 4 + 16)
        if self.head[4:8] != b"hsin":
            raise ValueError("not an hsin item")
        self.chunks = []
        stack = E.Reader(b.block64())
        while True:
            c = {"domain": stack.take(4), "type": stack.u32(), "version": stack.u32()}
            if c["type"] == 1:
                c["data"] = stack.take(stack.left())
                self.chunks.append(c)
                break
            nxt = stack.block64()
            c["data"] = stack.take(stack.left())
            self.chunks.append(c)
            stack = E.Reader(nxt)
        self.version = b.u32()
        self.children = []
        for _ in range(b.u32()):
            index, domain, typ = b.u32(), b.take(4), b.u32()
            child = Item(b.d[b.p:])
            b.p += child.size
            self.children.append([index, domain, typ, child])

    def to_bytes(self):
        stack = b""
        for c in reversed(self.chunks):
            head = c["domain"] + struct.pack("<II", c["type"], c["version"])
            stack = head + c["data"] if c["type"] == 1 else head + block64(stack) + c["data"]
        body = self.head + block64(stack) + struct.pack("<II", self.version, len(self.children))
        for index, domain, typ, child in self.children:
            body += struct.pack("<I", index) + domain + struct.pack("<I", typ) + child.to_bytes()
        return block64(body)

    def find(self, typ):
        """[(chunk, owner item)] of a type, into sub-trees (parsed on the way) and children."""
        out = []
        for c in self.chunks:
            if c["type"] == typ:
                out.append((c, self))
            if c["type"] == 115:
                sub = subtree(c)
                if sub is not None:
                    out += sub.find(typ)
        for ch in self.children:
            out += ch[3].find(typ)
        return out


def block64(b):
    return struct.pack("<Q", len(b) + 8) + b


def subtree(c):
    """A Subtree Item chunk's item (parsed once and kept in the chunk; None if encrypted)."""
    if "item" not in c:
        d = c["data"]
        compressed = d[4] > 0
        if compressed:
            usize, csize = struct.unpack("<II", d[5:13])
            try:
                inner = E.fastlz_decompress(d[13:13 + csize], usize)
            except ValueError:
                c["item"] = None
                return None
        else:
            inner = d[5:]
        c["item"] = Item(inner)
        c["compressed"] = compressed
    return c["item"]


def fastlz_compress(data):
    """FastLZ level 1 (the format NI's containers use): greedy, a match found by its first 4
    bytes (the last place they were seen), up to 264 bytes long and 8192 back."""
    out = bytearray()
    n = len(data)
    table = {}
    i = lit = 0

    def literals(a, b):
        while a < b:
            run = min(32, b - a)
            out.append(run - 1)
            out.extend(data[a:a + run])
            a += run

    while i < n - 3:
        key = data[i:i + 4]
        cand = table.get(key)
        table[key] = i
        if cand is not None and i - cand <= 8192:
            # the match's length: whole 264 at once where it can, else halving
            limit = min(264, n - i)
            if data[cand:cand + limit] == data[i:i + limit]:
                length = limit
            else:
                lo, hi = 4, limit          # data[cand:cand+lo] matches, +hi doesn't
                while hi - lo > 1:
                    mid = (lo + hi) // 2
                    if data[cand:cand + mid] == data[i:i + mid]:
                        lo = mid
                    else:
                        hi = mid
                length = lo
            literals(lit, i)
            d = i - cand - 1
            if length <= 8:
                out.append(((length - 2) << 5) | (d >> 8))
            else:
                out.append((7 << 5) | (d >> 8))
                out.append(length - 9)
            out.append(d & 255)
            i += length
            lit = i
            if i - 1 < n - 3:
                table[data[i - 1:i + 3]] = i - 1
        else:
            i += 1
    literals(lit, n)
    if not out or out[0] >> 5:           # (a stream starts with literals: level 1)
        return fastlz_literal(data)
    return bytes(out)


def fastlz_literal(data):
    """FastLZ level 1 with literal runs only: valid for any FastLZ decoder, not smaller."""
    out = bytearray()
    for i in range(0, len(data), 32):
        run = data[i:i + 32]
        out.append(len(run) - 1)
        out += run
    return bytes(out)


def rebuild_subtrees(item):
    """Write every changed sub-tree back into its chunk (inner first)."""
    for c in item.chunks:
        if c["type"] == 115 and c.get("item") is not None and c.get("changed"):
            rebuild_subtrees(c["item"])
            inner = c["item"].to_bytes()
            if c["compressed"]:
                packed = fastlz_compress(inner)
                c["data"] = c["data"][:5] + struct.pack("<II", len(inner), len(packed)) + packed
            else:
                c["data"] = c["data"][:5] + inner
    for ch in item.children:
        rebuild_subtrees(ch[3])


def preset_of(root):
    """The Preset Chunk Item's preset data, and a function that sets it (marking the sub-trees
    on the way as changed)."""
    path = []

    def walk(item):
        for c in item.chunks:
            if c["type"] == 109:
                return c
            if c["type"] == 115 and subtree(c) is not None:
                path.append(c)
                found = walk(c["item"])
                if found:
                    return found
                path.pop()
        for ch in item.children:
            found = walk(ch[3])
            if found:
                return found
        return None

    chunk = walk(root)
    if chunk is None:
        raise ValueError("no preset data (encrypted?)")
    d = chunk["data"]
    size = struct.unpack("<I", d[12:16])[0]
    data = d[20:20 + size]

    def setter(new, tail=None):
        # (tail: what follows the data, its last 4 bytes a marker, not a checksum: a7636734 with a
        # program, as in an .nki, 8565620d in Kontakt with nothing loaded, which Kontakt refuses
        # around a program: "The project could not be recalled for unknown reasons")
        chunk["data"] = d[:12] + struct.pack("<I", len(new)) + d[16:20] + new + (d[20 + size:] if tail is None else tail)
        for c in path:
            c["changed"] = True
    return data, setter


# ---------------------------------------------------------------------------------------------
# Kontakt preset chunks, raw: [(id, body)]

def chunks(data):
    out, p = [], 0
    while p < len(data):
        cid, n = struct.unpack("<HI", data[p:p + 6])
        out.append([cid, data[p + 6:p + 6 + n]])
        p += 6 + n
    return out


def join(cs):
    return b"".join(struct.pack("<HI", cid, len(body)) + body for cid, body in cs)


def struct_parts(body):
    """A structured chunk's body: (version, private, public, children bytes)."""
    if body[0] != 1:
        raise ValueError("not structured")
    p = 1
    version = struct.unpack("<H", body[p:p + 2])[0]
    p += 2
    n = struct.unpack("<I", body[p:p + 4])[0]
    private = body[p + 4:p + 4 + n]
    p += 4 + n
    n = struct.unpack("<I", body[p:p + 4])[0]
    public = body[p + 4:p + 4 + n]
    p += 4 + n
    n = struct.unpack("<I", body[p:p + 4])[0]
    kids = body[p + 4:p + 4 + n]
    return version, private, public, kids


def struct_body(version, private, public, kids):
    return (b"\x01" + struct.pack("<H", version) + struct.pack("<I", len(private)) + private
            + struct.pack("<I", len(public)) + public + struct.pack("<I", len(kids)) + kids)


# ---------------------------------------------------------------------------------------------
# Script values: after a PAR_SCRIPT's code, "<u32 length><name value>" entries

def script_values(public):
    """{name: (offset in public, value bytes)} of a script slot's saved values, or {}."""
    if len(public) < 6:
        return {}
    n = struct.unpack("<I", public[2:6])[0]
    start = 6 + n
    if start > len(public):
        return {}
    out = {}
    p = start
    tail = public
    while True:
        m = re.compile(rb"([$%@!~?][A-Za-z0-9_]{1,40}) ").search(tail, p)
        if not m:
            break
        s = m.start()
        if s >= 4:
            length = struct.unpack("<I", tail[s - 4:s])[0]
            if 0 < length <= 400 and s + length <= len(tail):
                name = m.group(1).decode("latin-1")
                value = tail[m.end():s + length]
                if name not in out:
                    out[name] = (m.end(), value)
                p = s + length
                continue
        p = m.end()
    return out


def program_scripts(program_struct_body):
    """[(child index, public)] of a program's PAR_SCRIPT children."""
    version, private, public, kids = struct_parts(program_struct_body)
    out = []
    for i, (cid, body) in enumerate(chunks(kids)):
        if cid == 0x06 and body and body[0] == 0:
            out.append((i, body[1:]))
    return out


def settings_of(nki_program, setup_program):
    """The script values the owner changed: {slot: {name: value}} (same length only)."""
    a = dict(program_scripts(nki_program))
    b = dict(program_scripts(setup_program))
    edits = {}
    for slot in a:
        if slot not in b:
            continue
        va, vb = script_values(a[slot]), script_values(b[slot])
        for name, (_, value) in vb.items():
            if name in va and va[name][1] != value and len(va[name][1]) == len(value):
                edits.setdefault(slot, {})[name] = value
    return edits


def apply_settings(program_body, edits):
    """The program with the script values set; how many were set."""
    version, private, public, kids = struct_parts(program_body)
    cs = chunks(kids)
    count = 0
    for slot, values in edits.items():
        if slot >= len(cs) or cs[slot][0] != 0x06 or not cs[slot][1] or cs[slot][1][0] != 0:
            continue
        pub = bytearray(cs[slot][1][1:])
        have = script_values(bytes(pub))
        for name, value in values.items():
            if name in have and len(have[name][1]) == len(value):
                off = have[name][0]
                pub[off:off + len(value)] = value
                count += 1
        cs[slot][1] = b"\x00" + bytes(pub)
    return struct_body(version, private, public, join(cs)), count


# ---------------------------------------------------------------------------------------------
# Sample lists (FILENAME_LIST_EX version 2, as in an .nki): the paths made absolute

def read_entry(b, p):
    """One path: (segments [(type, bytes of the segment)], end)."""
    n = struct.unpack("<I", b[p:p + 4])[0]
    p += 4
    segs = []
    for _ in range(n):
        t = b[p]
        s = p
        p += 1
        if t == 0:
            p += 2
        elif t in (1, 2, 4, 8, 9):
            k = struct.unpack("<I", b[p:p + 4])[0]
            p += 4 + 2 * k
        elif t in (3, 6):
            pass
        else:
            raise ValueError("path segment type %d" % t)
        segs.append((t, b[s:p]))
    return segs, p


def seg(t, text):
    s = text.encode("utf-16-le")
    return bytes([t]) + struct.pack("<I", len(text)) + s


def absolute(segs, base):
    """A relative path made absolute against base (a Windows folder, the .nki's)."""
    if not segs or segs[0][0] in (0, 1):
        return segs
    parts = [x for x in re.split(r"[\\/]+", base) if x]
    drive = parts[0].rstrip(":") if parts and parts[0].endswith(":") else ""
    dirs = parts[1:] if drive else parts
    rest = list(segs)
    while rest and rest[0][0] == 3:
        rest.pop(0)
        if dirs:
            dirs.pop()
    while rest and rest[0][0] == 6:
        rest.pop(0)
    return [(1, seg(1, drive))] + [(2, seg(2, d)) for d in dirs] + rest


def absolute_file_list(public, base):
    """An .nki's FILENAME_LIST_EX (version 2) with every path absolute; other bytes kept."""
    version = struct.unpack("<H", public[:2])[0]
    if version != 2:
        raise ValueError("sample list version %d" % version)
    out = bytearray(public[:2])
    p = 2
    for _ in range(2):                           # the special files, the samples
        count = struct.unpack("<i", public[p:p + 4])[0]
        out += public[p:p + 4]
        p += 4
        for _ in range(count):
            segs, p = read_entry(public, p)
            segs = absolute(segs, base) if segs else segs
            out += struct.pack("<I", len(segs)) + b"".join(s for _, s in segs)
        samples = count
    meta = samples * (4 + 4) + samples * 4       # dates, then one number per sample
    out += public[p:p + meta]
    p += meta
    if p < len(public):                          # the other files
        count = struct.unpack("<i", public[p:p + 4])[0]
        out += public[p:p + 4]
        p += 4
        for _ in range(count):
            segs, p = read_entry(public, p)
            segs = absolute(segs, base) if segs else segs
            out += struct.pack("<I", len(segs)) + b"".join(s for _, s in segs)
    out += public[p:]
    return bytes(out)


def list_paths(public):
    """The paths of a version 2 sample list, as text (for checks)."""
    p = 2
    paths = []
    for _ in range(2):
        count = struct.unpack("<i", public[p:p + 4])[0]
        p += 4
        for _ in range(count):
            segs, p = read_entry(public, p)
            text = ""
            for t, s in segs:
                if t == 0:
                    text += s[1:3].decode("latin-1").strip() + ":/"
                elif t == 1:
                    text += s[5:].decode("utf-16-le") + ":/"
                elif t == 2:
                    text += s[5:].decode("utf-16-le") + "/"
                elif t == 3:
                    text += "../"
                elif t in (4, 8, 9):
                    text += s[5:].decode("utf-16-le")
            paths.append(text)
    return paths


# ---------------------------------------------------------------------------------------------
# A setup

def nki_parts(path):
    """An .nki's program (structured chunk body) and sample list (FILENAME_LIST_EX public)."""
    with open(path, "rb") as f:
        root = Item(f.read())
    data, _ = preset_of(root)
    program = files = None
    for cid, body in chunks(data):
        if cid == 0x28:
            program = body
        elif cid in (0x4B, 0x3D):
            files = body
    if program is None:
        raise ValueError("no program in %s" % path)
    return program, files


def setup_parts(component):
    """A setup's program (in the multi's first slot)."""
    root = Item(component)
    data, _ = preset_of(root)
    for cid, body in chunks(data):
        if cid == 0x03:
            version, private, public, kids = struct_parts(body)
            for kid, kbody in chunks(kids):
                if kid == 0x37:                         # 8 bytes of used slots, then the first
                    container = chunks(kbody[8:])[0]
                    cv, cpriv, cpub, ckids = struct_parts(container[1])
                    for pid, pbody in chunks(ckids):
                        if pid == 0x36:                 # count, then the programs
                            return pbody[4:]
    raise ValueError("no program in the setup")


def make_component(template_component, program, files):
    """Kontakt's state: the template's, with its first slot's program and its sample list replaced."""
    root = Item(template_component)
    data, setter = preset_of(root)
    top = chunks(data)
    for t in top:
        if t[0] == 0x03:
            version, private, public, kids = struct_parts(t[1])
            kcs = chunks(kids)
            for k in kcs:
                if k[0] == 0x37:
                    slots = k[1]
                    container = chunks(slots[8:])
                    cv, cpriv, cpub, ckids = struct_parts(container[0][1])
                    ccs = chunks(ckids)
                    for c in ccs:
                        if c[0] == 0x36:
                            c[1] = struct.pack("<I", 1) + program
                    container[0][1] = struct_body(cv, cpriv, cpub, join(ccs))
                    k[1] = slots[:8] + join(container)
            t[1] = struct_body(version, private, public, join(kcs))
        elif t[0] in (0x4B, 0x3D) and files is not None:
            t[0] = 0x4B
            t[1] = files
    setter(join(top))
    rebuild_subtrees(root)
    return root.to_bytes()


# ---------------------------------------------------------------------------------------------
# A setup from an empty Kontakt (its state with nothing loaded) and the .nki alone

# The first slot's container as Kontakt 8.9 writes it (Violins 1's setup, 2026-09-27): PROGRAM_CONTAINER
# (version 81, its private and public data), 0x2B and SAVE_SETTINGS before the PROGRAM_LIST
SLOT_CONTAINER = (81, bytes.fromhex("01000000000000000000000000000000 0200".replace(" ", "")),
                  bytes.fromhex("00000000000000 3f00000000".replace(" ", "")))
SLOT_0x2B = bytes.fromhex("0060000000000001000140000000 0a000000ffffffff".replace(" ", ""))
SLOT_SAVE_SETTINGS = bytes.fromhex("0010000100 0000ffffffff00000000000001".replace(" ", ""))


def make_component_from_empty(empty_component, nki_path, settings=None):
    """Kontakt's state with the patch loaded, from Kontakt's state with nothing loaded: the patch's
    program in the first slot (with settings: {slot: {name: value}} set in its script), its
    samples' paths absolute, the library's authorization and the sound header's library fields
    from the .nki."""
    with open(nki_path, "rb") as f:
        nki = Item(f.read())
    program, files = nki_parts(nki_path)
    if settings:
        program, _ = apply_settings(program, settings)
    files = absolute_file_list(files, os.path.dirname(nki_path)) if files else None
    root = Item(empty_component)

    # the BNI sound preset item (at any depth): its authorization and sound header from the .nki's
    def bni(item):
        for ch in item.children:
            if ch[2] == 3:
                return ch[3]
        for c in item.chunks:
            if c["type"] == 115 and subtree(c) is not None:
                found = bni(c["item"])
                if found:
                    return found
        for ch in item.children:
            found = bni(ch[3])
            if found:
                return found
        return None

    def first(item, typ):
        for c, owner in item.find(typ):
            return c
        return None
    nki_data, _ = preset_of(nki)
    nki_chunk = next(c for c, owner in nki.find(109))
    nki_tail = nki_chunk["data"][20 + len(nki_data):]
    target = bni(root)
    nki_bni = bni(nki)
    if target is None or nki_bni is None:
        raise ValueError("no sound preset item")
    for c in target.chunks:
        if c["type"] == 106:
            src = next(x for x in nki_bni.chunks if x["type"] == 106)
            c["data"] = src["data"]
    header = first(target, 4)
    nki_header = first(nki_bni, 4)
    if header is not None and nki_header is not None and len(header["data"]) >= 178 <= len(nki_header["data"]):
        h = bytearray(header["data"])
        h[36] = 1                                       # (a patch loaded)
        h[156:178] = nki_header["data"][156:178]        # the library's id, flags and the patch's id
        header["data"] = bytes(h)

    def mark(item):                                     # every sub-tree written again
        for c in item.chunks:
            if c["type"] == 115 and c.get("item") is not None:
                c["changed"] = True
                mark(c["item"])
        for ch in item.children:
            mark(ch[3])
    mark(root)

    data, setter = preset_of(root)
    top = chunks(data)
    for t in top:
        if t[0] == 0x03:
            version, private, public, kids = struct_parts(t[1])
            kcs = chunks(kids)
            for k in kcs:
                if k[0] == 0x37:
                    container = struct_body(SLOT_CONTAINER[0], SLOT_CONTAINER[1], SLOT_CONTAINER[2],
                                            join([[0x2B, SLOT_0x2B], [0x47, SLOT_SAVE_SETTINGS],
                                                  [0x36, struct.pack("<I", 1) + program]]))
                    k[1] = b"\x01" + bytes(7) + join([[0x29, container]])
                elif k[0] == 0x48 and k[1]:
                    k[1] = b"\x00" + k[1][1:]                # (1 with nothing loaded, 0 with a patch)
            t[1] = struct_body(version, private, public, join(kcs))
        elif t[0] in (0x4B, 0x3D) and files is not None:
            t[0] = 0x4B
            t[1] = files
    setter(join(top), nki_tail)                         # (a program's marker, the .nki's)
    rebuild_subtrees(root)
    return root.to_bytes()


# ---------------------------------------------------------------------------------------------
# Where things are

def find_setups_folder():
    roots = [os.environ.get("LOCALAPPDATA", ""), os.environ.get("APPDATA", ""),
             os.path.join(os.path.expanduser("~"), ".local", "share")]
    found = []
    for r in roots:
        if not r or not os.path.isdir(r):
            continue
        for dirpath, dirnames, filenames in os.walk(r):
            depth = dirpath[len(r):].count(os.sep)
            if depth > 5:
                dirnames[:] = []
                continue
            if os.path.basename(dirpath) == file_name(LIBRARY) and \
               os.path.basename(os.path.dirname(dirpath)) == "soundlibraries":
                n = sum(1 for f in filenames if f.endswith(".vst3state"))
                found.append((n, dirpath))
    found.sort(reverse=True)
    return found[0][1] if found else None


def find_empty_state():
    """Kontakt's state with nothing loaded: "plugin component.bin" of the newest Extract plug-in
    data folder (Documents/MuseScore Sound Library Check/<library> extract <date>/)."""
    base = os.path.join(E.documents_folder(), "MuseScore Sound Library Check")
    found = []
    if os.path.isdir(base):
        for d in os.listdir(base):
            p = os.path.join(base, d, "plugin component.bin")
            if " extract " in d and os.path.isfile(p):
                found.append((os.path.getmtime(p), p))
    found.sort(reverse=True)
    return found[0][1] if found else None


def find_library():
    try:
        import winreg
    except ImportError:
        return None
    for hive in (winreg.HKEY_LOCAL_MACHINE, winreg.HKEY_CURRENT_USER):
        for base in (r"SOFTWARE\Native Instruments", r"SOFTWARE\WOW6432Node\Native Instruments"):
            try:
                with winreg.OpenKey(hive, base + "\\" + LIBRARY) as k:
                    d = winreg.QueryValueEx(k, "ContentDir")[0]
                    if os.path.isdir(d):
                        return d
            except OSError:
                pass
    return None


def library_nkis(library):
    out = {}
    for dirpath, _, filenames in os.walk(library):
        for fn in filenames:
            if fn.lower().endswith(".nki"):
                out[os.path.splitext(fn)[0].lower()] = os.path.join(dirpath, fn)
    return out


def program_name(program_body):
    version, private, public, kids = struct_parts(program_body)
    return E.Reader(public).utf16()


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--setups", help="MuseScore's setups folder (…/soundlibraries/%s)" % LIBRARY)
    ap.add_argument("--library", help="the library's folder (with Instruments and Samples)")
    ap.add_argument("--only", action="append", default=[], help="a patch (map name or .nki name); repeatable")
    ap.add_argument("--all", action="store_true", help="every patch without a setup")
    ap.add_argument("--overwrite", action="store_true", help="also over existing setups (kept as .bak)")
    ap.add_argument("--empty", help="Kontakt's state with nothing loaded (default: plugin component.bin of "
                                    "the newest Extract plug-in data folder); without one, a template's")
    args = ap.parse_args(argv)
    for stream in (sys.stdout, sys.stderr):
        try:
            stream.reconfigure(errors="replace")
        except (AttributeError, ValueError):
            pass

    setups = args.setups or find_setups_folder()
    library = args.library or find_library()
    print("Setups folder:", setups or "not found (--setups)")
    print("Library:", library or "not found (--library)")
    if not setups or not library:
        return 1
    nkis = library_nkis(library)
    print("%d .nki files" % len(nkis))

    # patches: the map's by their map names, the others by their .nki names
    names = map_names()
    patch_nki = {}
    for n in names:
        p = nki_for(n, nkis)
        if p:
            patch_nki[n] = p
    mapped = {os.path.normcase(p) for p in patch_nki.values()}
    added = []
    for key, p in sorted(nkis.items()):
        if os.path.normcase(p) not in mapped:
            n = os.path.splitext(os.path.basename(p))[0]
            patch_nki[n] = p
            added.append(n)
    print("%d patches (%d in the map, %d not)" % (len(patch_nki), len(patch_nki) - len(added), len(added)))

    # templates: the setups made by hand, with their .nki
    generated_file = os.path.join(setups, "generated.json")
    try:
        with open(generated_file, encoding="utf-8") as f:
            generated = set(json.load(f))
    except (OSError, ValueError):
        generated = set()
    templates = []
    for name, path in patch_nki.items():
        sf = os.path.join(setups, file_name(name) + ".vst3state")
        if not os.path.isfile(sf) or name in generated:
            continue
        try:
            with open(sf, "rb") as f:
                version, pname, component, controller = read_vst3state(f.read())
            setup_program = setup_parts(component)
            nki_program, _ = nki_parts(path)
            edits = settings_of(nki_program, setup_program)
            templates.append({"name": name, "file": sf, "pluginName": pname, "component": component,
                              "controller": controller, "edits": edits,
                              "vars": {s: set(script_values(pub)) for s, pub in program_scripts(nki_program)}})
        except Exception as e:
            print("  template %s: %s" % (name, e))
    print("%d templates (setups made by hand)" % len(templates))
    # the settings they hold, grouped (written to learned_settings.json too)
    groups = {}
    learned = {}
    for t in templates:
        ed = {str(s): {k: v.decode("latin-1") for k, v in vs.items()} for s, vs in t["edits"].items()}
        learned[t["name"]] = ed
        groups.setdefault(json.dumps(ed, sort_keys=True), []).append(t["name"])
    for key, names_ in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        print("  settings %s: %d setups (%s)" % (key if key != "{}" else "none", len(names_), ", ".join(names_)))
    with open(os.path.join(setups, "learned_settings.json"), "w", encoding="utf-8") as f:
        json.dump(learned, f, indent=1, sort_keys=True)
    empty_path = args.empty or find_empty_state()
    empty = None
    if empty_path:
        with open(empty_path, "rb") as f:
            empty = f.read()
        if empty[:4] == b"MSV3":
            empty = read_vst3state(empty)[2]
    print("Empty Kontakt state:", empty_path or "none (setups made on a template)")
    if not templates:
        print("No template: set up one patch by hand first (Check articulations › Set up…).")
        return 1

    targets = []
    if args.only:
        for n in args.only:
            key = next((k for k in patch_nki if k.lower() == n.lower()), None)
            if key is None:
                print("No patch %r" % n)
                return 1
            targets.append(key)
    elif args.all:
        targets = [n for n in patch_nki if not os.path.isfile(os.path.join(setups, file_name(n) + ".vst3state"))]
    else:
        missing = [n for n in patch_nki if not os.path.isfile(os.path.join(setups, file_name(n) + ".vst3state"))]
        print("%d patches have no setup, e.g. %s" % (len(missing), ", ".join(missing[:8])))
        if not (getattr(sys, "frozen", False) and sys.stdin and sys.stdin.isatty()):
            print("Nothing written. --only \"<patch>\" makes one, --all all of them.")
            return 0
        # double-clicked: ask
        typed = input("\nA patch to make a setup for (its name, as above), \"all\", or Enter to stop: ").strip().strip('"')
        if not typed:
            return 0
        if typed.lower() == "all":
            targets = missing
        else:
            key = next((k for k in patch_nki if k.lower() == typed.lower()), None)
            if key is None:
                print("No patch %r" % typed)
                return 1
            targets = [key]

    made = []
    for n in targets:
        sf = os.path.join(setups, file_name(n) + ".vst3state")
        if os.path.isfile(sf) and not args.overwrite:
            print("  %s: has a setup (--overwrite)" % n)
            continue
        try:
            program, files = nki_parts(patch_nki[n])
            vars_ = {s: set(script_values(pub)) for s, pub in program_scripts(program)}

            def overlap(t):
                return sum(len(vars_.get(s, set()) & t["vars"].get(s, set())) for s in t["vars"])
            t = max(templates, key=overlap)
            if empty is not None:
                component = make_component_from_empty(empty, patch_nki[n], t["edits"])
                _, count = apply_settings(program, t["edits"])
            else:
                program2, count = apply_settings(program, t["edits"])
                base = os.path.dirname(patch_nki[n])
                files2 = absolute_file_list(files, base) if files else None
                component = make_component(t["component"], program2, files2)
            if os.path.isfile(sf):
                os.replace(sf, sf + ".bak")
            with open(sf, "wb") as f:
                f.write(write_vst3state(t["pluginName"], component, t["controller"]))
            generated.add(n)
            made.append(n)
            print("  %s: made from %s (%s, settings of %s: %d), %.1f MB"
                  % (n, os.path.basename(patch_nki[n]), "empty Kontakt" if empty is not None else "template",
                     t["name"], count, len(component) / 1e6))
        except Exception as e:
            print("  %s: %s: %s" % (n, type(e).__name__, e))
    with open(generated_file, "w", encoding="utf-8") as f:
        json.dump(sorted(generated), f, indent=1)
    # the patches not in the map, in Check articulations' list
    new_added = [n for n in made if n in added]
    if new_added:
        af = os.path.join(setups, "addedpatches.json")
        try:
            with open(af, encoding="utf-8") as f:
                have = json.load(f)
        except (OSError, ValueError):
            have = []
        have += [n for n in new_added if n not in have]
        with open(af, "w", encoding="utf-8") as f:
            json.dump(have, f, indent=1)
    print("%d setups made." % len(made))
    return 0


if __name__ == "__main__":
    code = main()
    if getattr(sys, "frozen", False) and sys.stdin and sys.stdin.isatty():
        input("\nPress Enter to close.")
    sys.exit(code)
