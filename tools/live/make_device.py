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
  A clip tab playing through its track: the hub's udpreceive -> route /ms/midi (msl_in) -> forward to
    "msl_m<track id>" -> each copy's receive (named by the script) -> iter -> midiout; the rest -> deferlow -> the script.

Plug-in parameter lanes (MuseScoreLink.js › Parameter lanes), in the signal domain:
  phasor~ @frequency <PERIOD_TICKS> ticks @lock 1 (phase-locked to Live's transport: the song position over the period)
    -> *~ (right inlet from the script's outlet 4: PERIOD_QUARTERS x 60000 / tempo) = the song position in ms
    -> index~ ---mslp<k> (k = 0 … SLOTS-1; buffer~ ---mslp<k>: the value at each ms, step-hold) -> live.remote~ (left)
    and -> snapshot~ (banged by outlet 5; its value back as "posvalue <ms>", the "pos" probe);
  the script's outlet 3 "k id n" -> route 0 … 15 -> live.remote~ k's right inlet ("id n" takes parameter n, "id 0"
    lets it go); loadmess prefix ---mslp gives the script the buffers' resolved "---" prefix;
  receive msl_params -> deferlow -> prepend msl_params -> the script (the hub's word that a track's lanes changed).
"""

import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CARRIER_CCS = [32, 1, 11, 64, 2, 4, 21, 5, 65, 66, 67, 68]     # pitch 127 - i -> CARRIER_CCS[i] (liveclips.cpp)
BEND_MSB, BEND_LSB = 115, 114                                   # the pitch bend's carriers (liveclips.h)
DEVICE_WIDTH = 330
SLOTS = 16                                                      # parameters a track's device drives (MuseScoreLink.js SLOTS)
STORES = 4                                                      # [pattr Lanes] … kept in the set (MuseScoreLink.js STORES)
# the song-position phasor's period: 16384 quarter notes at Max's 480 ticks a quarter (MuseScoreLink.js PERIOD_QUARTERS);
# 2 h 16 min at 120 bpm before it wraps
PERIOD_TICKS = 16384 * 480
# the velocity shaper's ring of codes, one a tick (480 a beat) of song time: MuseScoreLink.js VEL_RING, derived the same
# way there (two windows of VEL_AHEAD_S 2 s at Live's fastest tempo, 999 BPM)
VEL_RING = 2 * math.ceil(2 * 999 / 60 * 480)
# a note-on's velocity v and the ring's code c (MuseScoreLink.js velShape): c 0 v; c >= 1 round(v (c - 1)) within 1-127;
# c < 0 -c; a velocity 0 (a note-off) as it is. Max's expr has no comma-free min / max, so the clamp is written out
# (R: the rounded product)
_R = "int($i1*($f2-1)+0.5)"
VEL_EXPR = ("expr ($i1>0)*(($f2==0)*$i1+($f2>=1)*(" + _R + "+(" + _R + "<1)*(1-" + _R + ")-(" + _R + ">127)*(" + _R
            + "-127))+($f2<0)*int(0.5-$f2))")
# the ring's cell for the song position: the phasor's phase (0-1 over PERIOD_TICKS ticks) plus the bias (ticks),
# rounded, modulo the ring (written without fmod's comma)
_X = "floor($f1*" + str(PERIOD_TICKS) + ".+$f2+0.5)"
VEL_INDEX = "expr " + _X + "-$f3*floor(" + _X + "/$f3)"


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
    # (any other note: through the velocity shaper, below, to midiformat)
    for k in range(1, 7):
        p.connect(parse, k, fmt, k)
    p.connect(fmt, 0, midiout, 0)
    p.connect(it, 0, midiout, 0)

    # --- a clip tab plays through its track (MuseScoreLink.js; protocol 4): the hub's udpreceive (made by the
    # script) goes into [route /ms/midi] (scripting name msl_in); "/ms/midi trackId status data1 data2" ->
    # t l l: first the track id -> sprintf msl_m%ld -> prepend send -> forward (its target), then the message's
    # bytes -> forward -> the [receive] of every copy on that track (named "msl_m<track id>" by the script's
    # outlet 7) -> iter -> midiout, before the instrument. Everything else -> deferlow -> the script.
    into = p.obj("route /ms/midi", 1, 2, 30, 560, w=100, varname="msl_in")
    dl_in = p.obj("deferlow", 1, 1, 150, 590)
    t_in = p.obj("t l l", 1, 2, 30, 590, outlettype=["", ""])
    first = p.obj("zl.slice 1", 2, 2, 90, 620, outlettype=["", ""])
    rest = p.obj("zl.slice 1", 2, 2, 30, 650, outlettype=["", ""])
    name = p.obj("sprintf msl_m%ld", 1, 1, 90, 650)
    pre_send = p.obj("prepend send", 1, 1, 90, 680)
    fwd = p.obj("forward", 1, 0, 30, 710)
    rcv_m = p.obj("receive", 1, 1, 420, 300, outlettype=[""])
    p.connect(into, 0, t_in, 0)
    p.connect(into, 1, dl_in, 0)
    p.connect(t_in, 1, first, 0)
    p.connect(first, 0, name, 0)
    p.connect(name, 0, pre_send, 0)
    p.connect(pre_send, 0, fwd, 0)
    p.connect(t_in, 0, rest, 0)
    p.connect(rest, 1, fwd, 0)
    p.connect(rcv_m, 0, it, 0)

    # --- the script: the hub's work (clips, locators, transport), the status line
    dev = p.obj("live.thisdevice", 1, 3, 500, 30, outlettype=["bang", "int", "int"])
    js = p.box("newobj", "v8", 1, 10, (500, 450, 120, 22), outlettype=[""] * 10,
               saved_object_attributes={"parameter_enable": 0},
               textfile={"text": script, "filename": "none", "flags": 0, "embed": 1, "autowatch": 1})
    # the track's parameter lanes kept in the Live Set: a [pattr] that is a Live parameter of type Blob, Stored Only
    # (in the device's MxDBlob as "Lanes", next to "Port"). The script sets it (outlet 6) when MuseScore's lanes
    # change; it gives the value back when the set opens (and on Live's undo). Tried in Live 12.2 (the test VM,
    # 2026-10-01): a duplicated device and Live's undo carry the [pattr]'s value as last set; the v8 box's own value
    # (getvalueof / notifyclients) stayed at the value the set was opened with, so the store is this [pattr]
    # STORES of them ("Lanes", "Lanes2" …): Live 12.2 crashed when one [pattr] was set to 34010 atoms, so the script
    # sends at most MuseScoreLink.js STORE_ATOMS to each ("k atoms…" -> route k)
    route_st = p.obj("route " + " ".join(str(k) for k in range(STORES)), 1, STORES + 1, 700, 420, w=120)
    p.connect(js, 6, route_st, 0)
    for k in range(STORES):
        name = "Lanes" if k == 0 else f"Lanes{k + 1}"
        st = p.box("newobj", f"pattr {name}", 1, 3, (700 + k * 110, 450, 100, 22),
                   outlettype=["", "", ""], varname=name,
                   parameter_enable=1, saved_object_attributes={"parameter_enable": 1},
                   saved_attribute_attributes={"valueof": {
                       "parameter_longname": name, "parameter_shortname": name, "parameter_type": 3,
                       "parameter_invisible": 1, "parameter_initial_enable": 0}})
        pre_st = p.obj(f"prepend lanes {k}", 1, 1, 700 + k * 110, 480)
        p.connect(route_st, k, st, 0)
        p.connect(st, 0, pre_st, 0)
        p.connect(pre_st, 0, js, 0)
    send = p.obj("udpsend 127.0.0.1 9002", 1, 0, 500, 500)
    p.connect(js, 7, rcv_m, 0)              # ("set msl_m<track id>")
    p.connect(dl_in, 0, js, 0)
    p.connect(dev, 0, js, 0)
    p.connect(js, 0, send, 0)
    p.connect(js, 1, send, 0)

    # --- plug-in parameter lanes: the song position in ms, 16 tables, 16 live.remote~
    # (the period as the frequency attribute: "phasor~ 7864320 ticks" as arguments ran at 0 Hz in Live 12.2 / Max 9;
    # "@frequency 7864320 ticks" follows the song position, tried on the VM)
    phasor = p.obj(f"phasor~ @frequency {PERIOD_TICKS} ticks @lock 1", 2, 1, 30, 450, outlettype=["signal"])
    ms = p.obj("*~ 1.", 2, 1, 30, 490, outlettype=["signal"])
    p.connect(phasor, 0, ms, 0)
    p.connect(js, 4, ms, 1)
    # --- the velocity shaper (MuseScoreLink.js › velocity curves): each note that isn't a carrier, in the scheduler:
    # t l b 0: first 0 (the code reset: a note never takes an older one), then the song position (snapshot~ of the
    # phasor, banged) -> the ring's cell (VEL_INDEX; its bias and the ring's size in its right inlets) -> peek~ ---mslv
    # -> the code into VEL_EXPR's right inlet, then the note: unpack -> its velocity through VEL_EXPR -> pack with its
    # pitch -> midiformat. The script fills [buffer~ ---mslv] (all 0: every note as it is).
    shp = p.obj("t l b 0", 1, 3, 450, 150, outlettype=["", "bang", "int"])
    vsnap = p.obj("snapshot~", 2, 1, 450, 180, outlettype=["float"])
    vidx = p.obj(VEL_INDEX, 3, 1, 450, 210, w=360, outlettype=[""])
    vring = p.obj(f"loadmess {VEL_RING}", 1, 1, 820, 180, outlettype=[""])
    p.obj("buffer~ ---mslv", 1, 2, 820, 240, w=100, outlettype=["float", "bang"])
    vpeek = p.obj("peek~ ---mslv", 3, 1, 450, 240, w=100, outlettype=["float"])
    vexpr = p.obj(VEL_EXPR, 2, 1, 450, 270, w=360, outlettype=[""])
    vunp = p.obj("unpack 0 0", 1, 2, 30, 270, outlettype=["int", "int"])
    vpack = p.obj("pack 0 0", 2, 1, 30, 300, outlettype=[""])
    p.connect(route, n + 2, shp, 0)
    p.connect(shp, 2, vexpr, 1)
    p.connect(phasor, 0, vsnap, 0)
    p.connect(shp, 1, vsnap, 0)
    p.connect(vsnap, 0, vidx, 0)
    p.connect(vring, 0, vidx, 2)
    p.connect(vidx, 0, vpeek, 0)
    p.connect(vpeek, 0, vexpr, 1)
    p.connect(shp, 0, vunp, 0)
    p.connect(vunp, 1, vexpr, 0)
    p.connect(vexpr, 0, vpack, 1)
    p.connect(vunp, 0, vpack, 0)
    p.connect(vpack, 0, fmt, 0)
    # the script's outlet 9: "bias <ticks>" -> VEL_INDEX's bias; "log 0/1" opens the log: each note as "vlog <pitch>
    # <phase> <velocity in> <velocity out>" -> deferlow -> the script (the "vlog" probe)
    # snapshot~ reports "the sample value in the most recently received signal vector" (Max 9's snapshot~ reference): one
    # signal vector before the note (measured: every note 64 samples, the vector, early, 2026-10-03 on the test VM), so
    # the script adds one vector (the sample rate and vector size from [dspstate~], asked when the device loads) as the
    # index's bias
    dsp = p.obj("dspstate~", 1, 4, 820, 60, outlettype=["int", "float", "int", "int"])
    p.connect(dev, 0, dsp, 0)
    for k, word in ((1, "dspsr"), (2, "dspvs")):
        pre_d = p.obj(f"prepend {word}", 1, 1, 820 + (k - 1) * 90, 90)
        p.connect(dsp, k, pre_d, 0)
        p.connect(pre_d, 0, js, 0)
    vroute = p.obj("route bias log", 1, 3, 820, 120, outlettype=["", "", ""])
    p.connect(js, 9, vroute, 0)
    p.connect(vroute, 0, vidx, 1)
    vlog = p.obj("pack 0 0. 0 0", 4, 1, 30, 330, outlettype=[""])
    p.connect(vunp, 0, vlog, 0)
    p.connect(vsnap, 0, vlog, 1)
    p.connect(vunp, 1, vlog, 2)
    p.connect(vexpr, 0, vlog, 3)
    vgate = p.obj("gate 1", 2, 1, 30, 360, outlettype=[""])
    p.connect(vroute, 1, vgate, 0)
    p.connect(vlog, 0, vgate, 1)
    vpre = p.obj("prepend vlog", 1, 1, 30, 390)
    vdl = p.obj("deferlow", 1, 1, 30, 420)
    p.connect(vgate, 0, vpre, 0)
    p.connect(vpre, 0, vdl, 0)
    p.connect(vdl, 0, js, 0)
    # outlet 8: what is kept in the set, to the MuseScore Envelopes script; its word that a track's values came, and a
    # velocity curve changed (the hub's messnamed), through deferlow
    p.connect(js, 8, p.obj("udpsend 127.0.0.1 9005", 1, 0, 650, 560), 0)
    for name in ("msl_keep", "msl_vel"):
        r = p.obj(f"receive {name}", 0, 1, 650, 590 if name == "msl_keep" else 650, outlettype=[""])
        d = p.obj("deferlow", 1, 1, 650, 615 if name == "msl_keep" else 675)
        pr = p.obj(f"prepend {name}", 1, 1, 750, 615 if name == "msl_keep" else 675)
        p.connect(r, 0, d, 0)
        p.connect(d, 0, pr, 0)
        p.connect(pr, 0, js, 0)
    snap = p.obj("snapshot~", 2, 1, 200, 490, outlettype=["float"])
    pos = p.obj("prepend posvalue", 1, 1, 200, 520)
    p.connect(ms, 0, snap, 0)
    p.connect(js, 5, snap, 0)
    p.connect(snap, 0, pos, 0)
    p.connect(pos, 0, js, 0)
    slots = p.obj("route " + " ".join(str(k) for k in range(SLOTS)), 1, SLOTS + 1, 30, 650, w=300)
    p.connect(js, 3, slots, 0)
    for k in range(SLOTS):
        x = 30 + k * 70
        p.obj(f"buffer~ ---mslp{k}", 1, 2, x, 690, w=65, outlettype=["float", "bang"])
        idx = p.obj(f"index~ ---mslp{k}", 2, 1, x, 720, w=65, outlettype=["signal"])
        remote = p.obj("live.remote~", 2, 0, x, 750, w=65)
        p.connect(ms, 0, idx, 0)
        p.connect(idx, 0, remote, 0)
        p.connect(slots, k, remote, 1)
    prefix = p.obj("loadmess prefix ---mslp", 1, 1, 650, 450, outlettype=[""])
    p.connect(prefix, 0, js, 0)
    rcv = p.obj("receive msl_params", 0, 1, 650, 480, outlettype=[""])
    dl = p.obj("deferlow", 1, 1, 650, 505)
    pre_p = p.obj("prepend msl_params", 1, 1, 650, 530)
    p.connect(rcv, 0, dl, 0)
    p.connect(dl, 0, pre_p, 0)
    p.connect(pre_p, 0, js, 0)

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
