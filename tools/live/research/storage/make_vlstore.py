#!/usr/bin/env python3
"""VLStore, a research device (clip-velocity-lane, 2026-10-03; LIVE.md › Kept without Live undo steps): which Max for Live
store persists with the set without a Live undo step. Stores A-G (vlstore.js has them and its OSC); writes VLStore.amxd
next to this file. Not shipped."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
from make_device import Patch, amxd  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))


def param(name, ptype, invisible, undoable=None, **more):
    v = {"parameter_longname": name, "parameter_shortname": name, "parameter_type": ptype,
         "parameter_invisible": invisible, "parameter_initial_enable": 0}
    if undoable is not None:
        v["parameter_visible_undoable"] = undoable
    v.update(more)
    return {"valueof": v}


def build(script):
    p = Patch()
    rcv = p.obj("udpreceive 9011", 1, 1, 30, 30)
    js = p.box("newobj", "v8", 1, 3, (30, 80, 120, 22), outlettype=["", "", ""],
               saved_object_attributes={"parameter_enable": 0},
               textfile={"text": script, "filename": "none", "flags": 0, "embed": 1, "autowatch": 1})
    snd = p.obj("udpsend 127.0.0.1 9002", 1, 0, 30, 130)
    p.connect(rcv, 0, js, 0)
    p.connect(js, 0, snd, 0)
    route = p.obj("route A B C D E F G", 1, 8, 200, 130, w=200)
    p.connect(js, 1, route, 0)

    def back(src, k, x):
        pre = p.obj(f"prepend back {k}", 1, 1, x, 220)
        p.connect(src, 0, pre, 0)
        p.connect(pre, 0, js, 0)

    a = p.box("newobj", "pattr vlA", 1, 3, (200, 170, 80, 22), outlettype=["", "", ""], varname="vlA",
              parameter_enable=1, saved_object_attributes={"parameter_enable": 1},
              saved_attribute_attributes=param("vlA", 3, 1))
    b = p.box("newobj", "pattr vlB", 1, 3, (290, 170, 80, 22), outlettype=["", "", ""], varname="vlB",
              parameter_enable=1, saved_object_attributes={"parameter_enable": 1},
              saved_attribute_attributes=param("vlB", 3, 1, 0))
    c = p.box("newobj", "dict vlC", 2, 5, (380, 170, 80, 22), outlettype=["dictionary", "", "", "", ""],
              varname="vlC", parameter_enable=1, saved_object_attributes={"parameter_enable": 1, "embed": 0},
              saved_attribute_attributes=param("vlC", 3, 1, 0))
    d = p.box("newobj", "dict vlD @embed 1", 2, 5, (470, 170, 110, 22), outlettype=["dictionary", "", "", "", ""],
              saved_object_attributes={"embed": 1, "parameter_enable": 0})
    e = p.box("newobj", "coll vlE @embed 1", 1, 4, (590, 170, 110, 22), outlettype=["", "", "", "bang"],
              saved_object_attributes={"embed": 1, "precision": 6})
    f = p.box("live.numbox", None, 1, 2, (710, 170, 50, 15), outlettype=["", "float"], varname="vlF",
              parameter_enable=1,
              saved_attribute_attributes=param("vlF", 0, 3, 0, parameter_mmin=0.0, parameter_mmax=16777216.0,
                                               parameter_unitstyle=0))
    g = p.box("newobj", "pattr vlG", 1, 3, (770, 170, 80, 22), outlettype=["", "", ""], varname="vlG",
              parameter_enable=1, saved_object_attributes={"parameter_enable": 1},
              saved_attribute_attributes=param("vlG", 0, 1, 0, parameter_mmin=0.0, parameter_mmax=16777216.0))
    for i, (box, k) in enumerate(((a, "A"), (b, "B"), (c, "C"), (d, "D"), (e, "E"), (f, "F"), (g, "G"))):
        p.connect(route, i, box, 0)
        back(box, k, 200 + i * 90)
    p.box("comment", "VLStore (research)", 1, 0, (30, 300, 150, 20), presentation=1,
          presentation_rect=[6, 4, 150, 20])
    pat = p.patcher()
    pat["patcher"]["devicewidth"] = 200
    return pat


def main():
    with open(os.path.join(HERE, "vlstore.js"), encoding="utf-8") as fh:
        script = fh.read()
    pat = build(script)
    with open(os.path.join(HERE, "VLStore.amxd"), "wb") as fh:
        fh.write(amxd(pat, "VLStore.amxd"))
    print("wrote VLStore.amxd")


if __name__ == "__main__":
    main()
