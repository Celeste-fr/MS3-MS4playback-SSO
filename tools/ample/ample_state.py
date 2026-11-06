#!/usr/bin/env python3
"""Read, diff and edit an Ample plug-in's component state (the same names as audio/vst3/pluginstate.h).

    ample_state.py xml <state>                 the state's XML
    ample_state.py names <state>               every settable name and its value, one per line
    ample_state.py diff <a> <b>                what differs (Parameter by Name, other elements by path + attribute)
    ample_state.py set <in> <out> name=value…  names: "Elem" (first <Elem Value>), "Parameter:Title",
                                               "Elem[Key=k]" / "Elem[Key=k]@Attr" (added when missing), "Elem@Attr"
    ample_state.py summary <state>             the facts gen_ample.py needs: DBName, version, the Parameter names, the
                                               Control (MIDI-learn) map, the Layer thresholds (JSON)

Layout (Ample 4.x, JUCE): "VstW" VST 2 chunk wrapper, an FXB "CcnK"/"FBCh" bank whose chunk starts at byte 176 with
"VC2!", the XML's length (little-endian 32 bit), the UTF-8 XML <AAG_STATE …>, JUCE's private tail. A changed length
sets the bank's three sizes again. States are the plug-in vendor's data: keep them outside the repository.
"""

import json
import re
import struct
import sys

CHUNK = 176


def load(path):
    d = open(path, "rb").read()
    if d[:4] != b"VstW" or d[CHUNK:CHUNK + 4] != b"VC2!":
        raise SystemExit("%s: not an Ample (JUCE VST 2 chunk) state" % path)
    n = struct.unpack("<I", d[CHUNK + 4:CHUNK + 8])[0]
    return d, d[CHUNK + 8:CHUNK + 8 + n].decode("utf-8"), d[CHUNK + 8 + n:]


def save(path, d, xml, rest):
    xb = xml.encode("utf-8")
    c = b"VC2!" + struct.pack("<I", len(xb)) + xb + rest
    head = bytearray(d[:CHUNK])
    head[172:176] = struct.pack(">I", len(c))
    head[20:24] = struct.pack(">I", CHUNK - 24 + len(c))
    with open(path, "wb") as f:
        f.write(bytes(head) + c)


TAG = re.compile(r"<(/?)([A-Za-z_][\w.-]*)([^>]*?)(/?)>")
ATTR = re.compile(r'([\w:.-]+)="([^"]*)"')


def items(xml):
    out, stack, cnt = {}, [], {}
    for m in TAG.finditer(xml):
        close, tag, attrs, selfc = m.groups()
        if close:
            if stack:
                stack.pop()
            continue
        a = dict(ATTR.findall(attrs))
        if tag == "Parameter" and "Name" in a:
            out["Parameter:" + a["Name"]] = a.get("Value")
        else:
            k = "/".join(stack + [tag])
            cnt[k] = cnt.get(k, 0) + 1
            if cnt[k] > 1:
                k += "[%d]" % cnt[k]
            for an, av in a.items():
                out[k + "@" + an] = av
            if not a:
                out[k] = ""
        if not selfc and not tag.startswith("?"):
            stack.append(tag)
    return out


def set_value(xml, name, value):
    esc = re.escape
    if name.startswith("Parameter:"):
        xml, n = re.subn(r'(<Parameter Name="%s" Value=")[^"]*' % esc(name[10:]), lambda m: m.group(1) + value, xml)
    elif re.match(r"^[\w.-]+\[\w+=[^\]]*\](@\w+)?$", name):
        m = re.match(r"^([\w.-]+)\[(\w+)=([^\]]*)\](?:@(\w+))?$", name)
        el, key, kv, attr = m.groups()
        attr = attr or "Value"
        pat = r'(<%s\b(?=[^>]*\b%s="%s")[^>]*?\b%s=")[^"]*' % (esc(el), esc(key), esc(kv), esc(attr))
        xml, n = re.subn(pat, lambda mm: mm.group(1) + value, xml, count=1)
        if not n:
            new = '<%s %s="%s" %s="%s"/>' % (el, key, kv, attr, value)
            last = None
            for mm in re.finditer(r"<%s\b[^>]*/>" % esc(el), xml):
                last = mm
            if last:
                xml = xml[:last.end()] + new + xml[last.end():]
            else:
                end = xml.rfind("</")
                xml = xml[:end] + new + xml[end:]
            n = 1
    elif "@" in name:
        el, attr = name.split("@")
        xml, n = re.subn(r'(<%s\b[^>]*?\b%s=")[^"]*' % (esc(el), esc(attr)), lambda m: m.group(1) + value, xml, count=1)
    else:
        xml, n = re.subn(r'(<%s Value=")[^"]*' % esc(name), lambda m: m.group(1) + value, xml, count=1)
    if not n:
        raise SystemExit("not in the state: " + name)
    return xml


def summary(xml):
    root = re.search(r"<AAG_STATE\b([^>]*)>", xml)
    ra = dict(ATTR.findall(root.group(1))) if root else {}
    params = [(a, v, i) for a, v, i in re.findall(r'<Parameter Name="([^"]*)" Value="([^"]*)"(?: Index="(\d+)")?', xml)]
    db = re.search(r'<DBName Value="([^"]*)"', xml)
    controls = {c: p for c, p in re.findall(r'<Control ID="(\d+)" Parameter="([^"]*)"', xml)}
    layers = {i: v for i, v in re.findall(r'<Layer ID="(\d+)" Value="([^"]*)"', xml)}
    return {"root": ra, "dbName": db.group(1) if db else None,
            "parameters": [{"name": a, "value": v, "index": int(i) if i else None} for a, v, i in params],
            "controls": controls, "layers": layers}


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    cmd = argv[1]
    if cmd == "xml":
        print(load(argv[2])[1])
    elif cmd == "names":
        for k, v in items(load(argv[2])[1]).items():
            print("%s=%s" % (k, v))
    elif cmd == "diff":
        a, b = load(argv[2]), load(argv[3])
        A, B = items(a[1]), items(b[1])
        for k in sorted(set(A) | set(B)):
            if A.get(k) != B.get(k):
                print("%s: %r -> %r" % (k, A.get(k), B.get(k)))
        if a[2] != b[2]:
            print("(JUCE private tail differs: %d vs %d bytes)" % (len(a[2]), len(b[2])))
    elif cmd == "set":
        d, xml, rest = load(argv[2])
        for kv in argv[4:]:
            m = re.match(r"^((?:[^=\[]|\[[^\]]*\])+)=(.*)$", kv)
            k, v = m.group(1), m.group(2)
            xml = set_value(xml, k, v)
        save(argv[3], d, xml, rest)
    elif cmd == "summary":
        print(json.dumps(summary(load(argv[2])[1]), indent=1))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
