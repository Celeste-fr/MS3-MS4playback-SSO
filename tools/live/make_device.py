#!/usr/bin/env python3
"""Builds the MuseScore Link Max for Live MIDI effect from MuseScoreLink.js (LIVE.md › Live plays the score).

Writes, next to this file:
  MuseScoreLink.maxpat   the patcher as JSON (Max's own format), the script embedded in its v8 box
  MuseScore Link.amxd    the same patcher as a Live device

The .amxd container (no official documentation; the layout of js2max's writer, github.com/ktamas77/js2max,
src/amxd/writer.ts, MIT, whose devices load in Live 12 with Max 9, and of Max 9's own files): chunks of a
4-byte ASCII tag and a little-endian uint32 length:
  "ampf" 4 "mmmm"               the device type (mmmm: MIDI effect, aaaa: audio effect, iiii: instrument)
  "meta" 4 <uint32 LE 7>
  "ptch" <n> <mx@c container>
the mx@c container: "mx@c", then big-endian uint32 16, 0 and (16 + JSON size + 2), the patcher JSON, "\\n\\0",
then a directory "dlst" > "dire" of big-endian tag-length-value entries (type "JSON", fnam <file name>,
sz32 <JSON size + 2>, of32 16, vers 0, flag 0x11, mdat 0), each length counting its own 8-byte header.

The MIDI path is plain Max objects in the scheduler thread (the script is never in it):
  midiin -> midiparse; notes -> route 127 … 116 (the carrier keys, liveclips.h's table):
    a carrier's note-on (velocity v > 0) -> sel 0 -> the value -> prepend 176 <cc> -> iter -> midiout
    (a control change on channel 1), its note-off dropped. The value (liveclips.h carrierValue): UACC (key 127)
    "- 1" (v - 1); the others "expr $i1*($i1>1)" (v, and 1 as 0: 127 exact);
  route 115 / 114 (the pitch bend's upper / lower 7 bits, liveclips.h BEND_MSB / BEND_LSB): note-on -> sel 0 ->
    the value (as the controllers') -> t b i -> into pack 224 0 64 (the centre until the first; inlet 2: upper,
    inlet 1: lower), then a bang to its left inlet:
    the whole bend (status 224 = pitch bend on channel 1, lower, upper) -> iter -> midiout. Either half sends it
    with the other's last value, so a pair chased in any order ends right; note-offs dropped;
    any other note -> midiformat (with the rest of midiparse's messages and its channel) -> midiout.
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CARRIER_CCS = [32, 1, 11, 64, 2, 4, 21, 5, 65, 66, 67, 68]     # pitch 127 - i -> CARRIER_CCS[i] (liveclips.cpp)
BEND_MSB, BEND_LSB = 115, 114                                   # the pitch bend's carriers (liveclips.h)
DEVICE_WIDTH = 330


class Patch:
    def __init__(self):
        self.boxes = []
        self.lines = []
        self.n = 0

    def box(self, maxclass, text=None, inlets=1, outlets=1, rect=(0, 0, 100, 22), **extra):
        self.n += 1
        b = {"id": f"obj-{self.n}", "maxclass": maxclass, "numinlets": inlets, "numoutlets": outlets,
             "patching_rect": list(rect)}
        if text is not None:
            b["text"] = text
        if outlets:
            b["outlettype"] = extra.pop("outlettype", [""] * outlets)
        b.update(extra)
        self.boxes.append({"box": b})
        return b["id"]

    def obj(self, text, inlets=1, outlets=1, x=0, y=0, w=None, **extra):
        return self.box("newobj", text, inlets, outlets, (x, y, w or max(40, 7 * len(text) + 16), 22), **extra)

    def connect(self, a, ao, b, bi):
        self.lines.append({"patchline": {"source": [a, ao], "destination": [b, bi]}})

    def patcher(self):
        return {"patcher": {
            "fileversion": 1,
            "appversion": {"major": 9, "minor": 0, "revision": 7, "architecture": "x64", "modernui": 1},
            "classnamespace": "box",
            "rect": [100, 100, 900, 700],
            "openinpresentation": 1,
            "default_fontsize": 10.0, "default_fontface": 0, "default_fontname": "Arial",
            "gridonopen": 1, "gridsize": [15.0, 15.0], "gridsnaponopen": 1, "objectsnaponopen": 1,
            "statusbarvisible": 2, "toolbarvisible": 1,
            "boxanimatetime": 200, "enablehscroll": 1, "enablevscroll": 1,
            "devicewidth": DEVICE_WIDTH,
            "description": "MuseScore Link: the score's parts as clips, kept up to date by MuseScore; "
                           "turns their controller notes into MIDI controllers. Put it before the instrument.",
            "digest": "", "tags": "", "style": "", "subpatcher_template": "",
            "boxes": self.boxes, "lines": self.lines,
            "dependency_cache": [], "autosave": 0,
        }}


def build(script):
    p = Patch()
    # --- MIDI: carriers to controllers, everything else through
    midiin = p.obj("midiin", 1, 1, 30, 30, outlettype=["int"])
    parse = p.obj("midiparse", 1, 8, 30, 70, outlettype=["", "", "", "int", "int", "", "int", ""])
    keys = " ".join(str(127 - i) for i in range(len(CARRIER_CCS))) + f" {BEND_MSB} {BEND_LSB}"
    n = len(CARRIER_CCS)
    route = p.obj(f"route {keys}", 1, n + 3, 30, 110, w=400)
    fmt = p.obj("midiformat", 7, 2, 30, 330, outlettype=["int", ""])
    it = p.obj("iter", 1, 1, 420, 330, outlettype=[""])
    midiout = p.obj("midiout", 1, 0, 30, 370)

    def value(x):                          # a continuous controller's or a bend half's value: v, 1 as 0
        return p.obj("expr $i1*($i1>1)", 1, 1, x, 200, w=110, outlettype=[""])
    p.connect(midiin, 0, parse, 0)
    p.connect(parse, 0, route, 0)
    for i, cc in enumerate(CARRIER_CCS):
        x = 30 + i * 60
        sel = p.obj("sel 0", 2, 2, x, 160, outlettype=["bang", ""])
        minus = p.obj("- 1", 2, 1, x, 200, outlettype=["int"]) if cc == 32 else value(x)
        pre = p.obj(f"prepend 176 {cc}", 1, 1, x, 240, w=100)
        p.connect(route, i, sel, 0)
        p.connect(sel, 1, minus, 0)
        p.connect(minus, 0, pre, 0)
        p.connect(pre, 0, it, 0)
    # the pitch bend: upper half into pack's inlet 2, lower into 1, each then bangs it out
    bend = p.obj("pack 224 0 64", 3, 1, 30 + (n + 1) * 60, 280, w=90, outlettype=[""])
    for k, inlet in ((n, 2), (n + 1, 1)):
        x = 30 + (k + (0 if inlet == 2 else 1)) * 60
        sel = p.obj("sel 0", 2, 2, x, 160, outlettype=["bang", ""])
        minus = value(x)
        trig = p.obj("t b i", 1, 2, x, 240, outlettype=["bang", "int"])
        p.connect(route, k, sel, 0)
        p.connect(sel, 1, minus, 0)
        p.connect(minus, 0, trig, 0)
        p.connect(trig, 1, bend, inlet)
        p.connect(trig, 0, bend, 0)
    p.connect(bend, 0, it, 0)
    p.connect(route, n + 2, fmt, 0)
    for k in range(1, 7):
        p.connect(parse, k, fmt, k)
    p.connect(fmt, 0, midiout, 0)
    p.connect(it, 0, midiout, 0)

    # --- the script: the hub's work (clips, locators, transport), the status line
    dev = p.obj("live.thisdevice", 1, 3, 500, 30, outlettype=["bang", "int", "int"])
    js = p.box("newobj", "v8", 1, 3, (500, 450, 120, 22), outlettype=["", "", ""],
               saved_object_attributes={"parameter_enable": 0},
               textfile={"text": script, "filename": "none", "flags": 0, "embed": 1, "autowatch": 1})
    send = p.obj("udpsend 127.0.0.1 9002", 1, 0, 500, 500)
    p.connect(dev, 0, js, 0)
    p.connect(js, 0, send, 0)
    p.connect(js, 1, send, 0)

    # --- the face: title, port, Resync, status
    p.box("comment", "MuseScore Link", 1, 0, (650, 30, 150, 20), presentation=1,
          presentation_rect=[6, 4, 150, 20], fontsize=12.0, fontface=1)
    # a Float parameter shown as a whole number (unitstyle 0): Live's Int parameters have 256 steps
    # (0-255 from the minimum), so an Int port from 1024 could reach 1279 at most -- 9001 became 1279 and
    # the device never heard MuseScore (seen in Live 12.2 on the test VM, 2026-09-30)
    port = p.box("live.numbox", None, 1, 2, (650, 60, 50, 15), outlettype=["", "float"],
                 presentation=1, presentation_rect=[160, 6, 50, 15], varname="Port",
                 parameter_enable=1,
                 saved_attribute_attributes={"valueof": {
                     "parameter_longname": "Port", "parameter_shortname": "Port", "parameter_type": 0,
                     "parameter_mmin": 1024, "parameter_mmax": 65000, "parameter_initial_enable": 1,
                     "parameter_initial": [9001], "parameter_unitstyle": 0, "parameter_invisible": 1}})
    p.box("comment", "UDP port", 1, 0, (705, 60, 60, 18), presentation=1, presentation_rect=[212, 5, 60, 18])
    pre = p.obj("prepend port", 1, 1, 650, 90)
    p.connect(port, 0, pre, 0)
    p.connect(pre, 0, js, 0)
    button = p.box("live.text", "Resync", 1, 2, (650, 120, 60, 18), outlettype=["", ""],
                   presentation=1, presentation_rect=[270, 5, 55, 18], mode=0, texton="Resync",
                   varname="Resync", parameter_enable=1,
                   saved_attribute_attributes={"valueof": {
                       "parameter_longname": "Resync", "parameter_shortname": "Resync", "parameter_type": 2,
                       "parameter_enum": ["off", "on"], "parameter_mmax": 1, "parameter_invisible": 2}})
    trig = p.obj("t resync", 1, 1, 650, 150)
    p.connect(button, 0, trig, 0)
    p.connect(trig, 0, js, 0)
    status = p.box("comment", "MuseScore Link: loading", 1, 0, (650, 200, 320, 40), presentation=1,
                   presentation_rect=[6, 26, 320, 40], linecount=3)
    p.connect(js, 2, status, 0)             # (the script sends "set <text>")
    p.box("comment", "MuseScore owns its \"MuseScore:\" clips: edits to them here are overwritten. Keep this device "
          "before the instrument.", 1, 0, (650, 250, 320, 30), presentation=1, presentation_rect=[6, 68, 320, 28],
          linecount=2, fontsize=9.0, textcolor=[0.6, 0.6, 0.6, 1.0])
    # --- Edit in MuseScore: the clip in Live's Clip View opens as a score in MuseScore (LIVE.md › Editing Live clips)
    edit = p.box("live.text", "Edit in MuseScore", 1, 2, (650, 290, 110, 18), outlettype=["", ""],
                 presentation=1, presentation_rect=[6, 100, 110, 18], mode=0, texton="Edit in MuseScore",
                 varname="Edit in MuseScore", parameter_enable=1,
                 saved_attribute_attributes={"valueof": {
                     "parameter_longname": "Edit in MuseScore", "parameter_shortname": "Edit",
                     "parameter_type": 2, "parameter_enum": ["off", "on"], "parameter_mmax": 1,
                     "parameter_invisible": 2}})
    tedit = p.obj("t edit", 1, 1, 650, 320)
    p.connect(edit, 0, tedit, 0)
    p.connect(tedit, 0, js, 0)
    p.box("comment", "the MIDI clip shown in Live's Clip View, as notation; edits go back to it", 1, 0,
          (770, 290, 200, 30), presentation=1, presentation_rect=[120, 99, 205, 28], linecount=2, fontsize=9.0,
          textcolor=[0.6, 0.6, 0.6, 1.0])
    return p.patcher()


def tlv(tag, data):
    return tag.encode("ascii") + struct.pack(">I", 8 + len(data)) + data


def tlv_str(tag, s):
    b = s.encode("ascii")
    b += b"\0" * ((4 - len(b) % 4) % 4)
    return tlv(tag, b)


def amxd(patcher, filename, kind=b"mmmm"):
    data = json.dumps(patcher, indent="\t").encode("utf-8")
    dire = (tlv_str("type", "JSON") + tlv_str("fnam", filename) + tlv("sz32", struct.pack(">I", len(data) + 2))
            + tlv("of32", struct.pack(">I", 16)) + tlv("vers", struct.pack(">I", 0)) + tlv("flag", struct.pack(">I", 0x11))
            + tlv("mdat", struct.pack(">I", 0)))
    dlst = tlv("dlst", tlv("dire", dire))
    body = b"\n\0"
    mx = b"mx@c" + struct.pack(">III", 16, 0, len(data) + len(body) + 16)
    ptch = mx + data + body + dlst
    return (b"ampf" + struct.pack("<I", 4) + kind + b"meta" + struct.pack("<I", 4) + struct.pack("<I", 7)
            + b"ptch" + struct.pack("<I", len(ptch)) + ptch)


def main():
    with open(os.path.join(HERE, "MuseScoreLink.js"), encoding="utf-8") as f:
        script = f.read()
    patcher = build(script)
    out = sys.argv[1] if len(sys.argv) > 1 else HERE
    with open(os.path.join(out, "MuseScoreLink.maxpat"), "w", encoding="utf-8") as f:
        json.dump(patcher, f, indent="\t")
        f.write("\n")
    name = "MuseScore Link.amxd"
    with open(os.path.join(out, name), "wb") as f:
        f.write(amxd(patcher, name))
    print("wrote", os.path.join(out, "MuseScoreLink.maxpat"), "and", os.path.join(out, name))


if __name__ == "__main__":
    main()
