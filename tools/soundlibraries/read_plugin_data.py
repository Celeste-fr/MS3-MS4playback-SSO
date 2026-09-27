#!/usr/bin/env python3
"""Read an "Extract plug-in data" folder (or its .zip) from the Check Articulations dialog.

    python3 tools/soundlibraries/read_plugin_data.py "<library> extract <date>[.zip]" [--full]

What MuseScore wrote (mscore/soundlibrarycheck.h, Extract):
  plugin.json                 Vst3Plugin::describe() of the plug-in with nothing loaded
  <patch>.json                the same with the patch's setup ("describe"), and with "Try every
                              controller": "controllers", "parameters", "switches"
                              (audio/vst3/pluginextract.h)
  <patch> component.bin       the plug-in's own state (what a DAW keeps in a project)
  <patch> controller.bin
  <patch> controllers.png     what each controller or parameter shows in the window (0 | 127)
  <patch> (window).png
  summary.txt

This prints (and writes to <folder>/report.txt):
  - the plug-in: vendor, version, classes, buses, interfaces, editor, what it asked of MuseScore
  - its parameters: how many, the families of placeholders (Kontakt's "#001" …), the others
    with the text of their values
  - the MIDI mapping (which controllers reach it and as which parameters)
  - programs, pitch names, keyswitches, note expressions
  - per patch: what differs from the empty plug-in, what each controller and parameter does
  - the state blobs: size, entropy, zlib streams inside (decoded to <folder>/_decoded/) and
    the readable strings (file paths of .nki, instrument names …) found in them
"""

import json
import math
import os
import re
import sys
import tempfile
import zipfile
import zlib
from collections import Counter

FULL = "--full" in sys.argv


def out(line=""):
    REPORT.append(line)
    print(line)


REPORT = []


def load_folder(path):
    if path.endswith(".zip"):
        tmp = tempfile.mkdtemp(prefix="plugin-data-")
        with zipfile.ZipFile(path) as z:
            z.extractall(tmp)
        subs = [os.path.join(tmp, d) for d in os.listdir(tmp)]
        return subs[0] if len(subs) == 1 and os.path.isdir(subs[0]) else tmp
    return path


def family(title):
    return re.sub(r"\d+", "#", title).strip()


def entropy(data):
    if not data:
        return 0.0
    counts = Counter(data)
    n = len(data)
    return -sum(c / n * math.log2(c / n) for c in counts.values())


def strings(data, minimum=6):
    """ASCII and UTF-16LE strings, with their offsets."""
    found = []
    for m in re.finditer(rb"[\x20-\x7e]{%d,}" % minimum, data):
        found.append((m.start(), m.group().decode("ascii")))
    for m in re.finditer(rb"(?:[\x20-\x7e]\x00){%d,}" % minimum, data):
        found.append((m.start(), m.group().decode("utf-16le") + "  [utf-16]"))
    found.sort()
    return found


def zlib_streams(data):
    """Every zlib stream in data: (offset, decoded bytes)."""
    streams = []
    i = 0
    while i < len(data) - 2:
        if data[i] == 0x78 and data[i + 1] in (0x01, 0x5E, 0x9C, 0xDA):
            try:
                d = zlib.decompressobj()
                decoded = d.decompress(data[i:])
                if decoded and d.eof:
                    used = len(data) - i - len(d.unused_data)
                    streams.append((i, decoded))
                    i += max(used, 2)
                    continue
            except zlib.error:
                pass
        i += 1
    return streams


def interesting(s):
    return re.search(r"\.nki|\.nkm|\.nkx|\.nkc|\.nkr|Spitfire|Kontakt|Instrument|Library|[A-Z]:\\|/Users/|Samples", s, re.I)


def describe_plugin(d, title):
    out(f"# {title}")
    m = d.get("module", {})
    f = m.get("factory", {})
    out(f"  vendor: {f.get('vendor')}  {f.get('url')}  {f.get('email')}  flags: {', '.join(f.get('flags', []))}")
    for c in m.get("classes", []):
        out(f"  class: {c.get('name')} [{c.get('category')}] {c.get('subCategories')} v{c.get('version')} "
            f"(SDK {c.get('sdkVersion')}) cid {c.get('cid')}")
        if c.get("compatibility"):
            out(f"    compatibility: {c['compatibility']}")
    if m.get("moduleInfo"):
        info = m["moduleInfo"]
        out(f"  moduleinfo.json: {info.get('Name')} {info.get('Version')} factory {info.get('Factory Info', {})}")
    elif m.get("moduleInfoText"):
        out(f"  moduleinfo.json (not strict JSON): {len(m['moduleInfoText'])} characters")
    ifs = d.get("interfaces", {})
    out(f"  component interfaces: {', '.join(ifs.get('component', []))}")
    out(f"  controller interfaces: {', '.join(ifs.get('controller', []))}")
    comp = d.get("component", {})
    for b in comp.get("buses", []):
        out(f"  bus: {b['media']} {b['direction']} {b['index']} \"{b['name']}\" {b['type']} {b['channels']} ch "
            f"{b.get('arrangement', '')} {'(used)' if b.get('usedByMuseScore') else ''}")
    out(f"  latency {comp.get('latencySamples')} samples, tail {comp.get('tailSamples')}, 64-bit {comp.get('canProcess64bit')}, "
        f"prefetchable {comp.get('prefetchable')}, needs: {', '.join(comp.get('processContextRequirements', []))}")
    ed = d.get("editor")
    if ed:
        out(f"  editor: {ed.get('size')} resizable {ed.get('canResize')} platforms {', '.join(ed.get('platforms', []))}")
    if d.get("parameterFunctions"):
        out(f"  parameter functions: {d['parameterFunctions']}")
    if d.get("xmlRepresentation"):
        out(f"  XML representation: {len(d['xmlRepresentation'])} characters")
    hq = d.get("hostQueries", {})
    if hq:
        out("  asked of MuseScore: " + "; ".join(f"{k} {v}" for k, v in sorted(hq.items())))


def describe_parameters(d):
    params = d.get("parameters", [])
    fams = Counter(family(p["title"]) for p in params)
    big = {f: n for f, n in fams.items() if n > 8}
    out(f"  parameters: {len(params)}")
    for f, n in sorted(big.items(), key=lambda x: -x[1]):
        ex = [p for p in params if family(p["title"]) == f]
        out(f"    family \"{f}\": {n} (ids {ex[0]['id']:.0f} … {ex[-1]['id']:.0f}; e.g. value text {ex[0].get('valueText')!r})")
    others = [p for p in params if family(p["title"]) not in big]
    for p in others if FULL or len(others) <= 80 else others[:80]:
        texts = p.get("texts", [])
        if not FULL and p["stepCount"] == 0 and len(texts) > 3:
            texts = [texts[0], texts[len(texts) // 2], texts[-1]]
        shown = ", ".join(f"{t[0]}={t[1]}" for t in texts[:8]) + (" …" if len(texts) > 8 else "")
        out(f"    {p['id']:.0f} \"{p['title']}\" [{p.get('units', '')}] steps {p['stepCount']} "
            f"value {p['value']:.3f} ({p.get('valueText')}) {','.join(p.get('flags', []))}  texts: {shown}")
    if not FULL and len(others) > 80:
        out(f"    … {len(others) - 80} more (--full)")
    return params


def describe_mapping(d):
    mm = d.get("midiMapping")
    if mm is None:
        out("  MIDI mapping: none (no IMidiMapping)")
        return
    for bus, chans in mm.items():
        counts = {ch: len(m) for ch, m in sorted(chans.items(), key=lambda x: channel_key(x[0]))}
        out(f"  MIDI mapping, {bus}: " + ", ".join(f"{ch.replace('channel ', 'ch ')}: {n}" for ch, n in counts.items()))
        ch1 = chans.get("channel 1", {})
        titles = [f"{cc}→{e['title']}" for cc, e in sorted(ch1.items(), key=lambda x: int(x[0]))]
        out("    channel 1: " + ("; ".join(titles) if FULL or len(titles) <= 40 else "; ".join(titles[:40]) + " …"))
    if d.get("midiMapping2"):
        for k, v in d["midiMapping2"].items():
            if v:
                out(f"  MIDI mapping 2 {k}: {len(v)}")


def describe_units(d):
    u = d.get("units")
    if not u:
        return
    out(f"  units: " + "; ".join(f"{x['id']} \"{x['name']}\" (parent {x['parent']}, programs {x['programListId']})" for x in u.get("units", [])))
    for l in u.get("programLists", []):
        progs = l.get("programs", [])
        names = [p.get("name", "") for p in progs]
        out(f"  program list {l['id']} \"{l['name']}\": {len(progs)} programs: " + ", ".join(names[:30]) + (" …" if len(names) > 30 else ""))
        for i, p in enumerate(progs):
            if p.get("pitchNames"):
                out(f"    program {i} \"{p.get('name')}\" pitch names: " + ", ".join(f"{k}={v}" for k, v in p["pitchNames"].items()))
            if p.get("info"):
                out(f"    program {i} info: {p['info']}")
    if u.get("unitData"):
        out(f"  unit data: {u['unitData']}")


def ranges(values):
    """[0, 1, 2, 5] -> 0-2, 5"""
    values = sorted(v for v in values if isinstance(v, int))
    parts = []
    i = 0
    while i < len(values):
        j = i
        while j + 1 < len(values) and values[j + 1] == values[j] + 1:
            j += 1
        parts.append(str(values[i]) if i == j else f"{values[i]}-{values[j]}")
        i = j + 1
    return ", ".join(parts) or "none"


def channel_key(name):
    return [int(x) for x in re.findall(r"\d+", name)]


def describe_channels(d):
    # channels that say the same, as one line
    groups = {}
    for ch, c in sorted(d.get("channels", {}).items(), key=lambda x: channel_key(x[0])):
        groups.setdefault(json.dumps(c, sort_keys=True), []).append(ch)
    for text, chans in groups.items():
        c = json.loads(text)
        nums = [channel_key(x)[1] for x in chans]
        ch = chans[0] if len(chans) == 1 else f"bus {channel_key(chans[0])[0]} channels {nums[0]}-{nums[-1]}" \
            if nums == list(range(nums[0], nums[-1] + 1)) else f"channels {', '.join(map(str, nums))}"
        if c.get("keyswitches"):
            out(f"  {ch} keyswitches: " + ", ".join(f"{k['title']} {k['keyMin']}-{k['keyMax']}" for k in c["keyswitches"]))
        if c.get("noteExpressions"):
            out(f"  {ch} note expressions: " + ", ".join(f"{n['title']} ({n['typeId']:.0f})" for n in c["noteExpressions"]))
        if c.get("physicalUI"):
            out(f"  {ch} physical UI: {c['physicalUI']}")
        if c.get("orchestralArticulations"):
            out(f"  {ch} orchestral articulations: {c['orchestralArticulations']}")


def compare(base, d):
    """What the patch changed against the empty plug-in."""
    bp = {p["id"]: p for p in base.get("parameters", [])}
    pp = {p["id"]: p for p in d.get("parameters", [])}
    added = [p for i, p in pp.items() if i not in bp]
    gone = [p for i, p in bp.items() if i not in pp]
    renamed = [(bp[i], p) for i, p in pp.items() if i in bp and bp[i]["title"] != p["title"]]
    changed = [(bp[i], p) for i, p in pp.items() if i in bp and abs(bp[i]["value"] - p["value"]) > 1e-6]
    out(f"  against the empty plug-in: {len(added)} parameters added, {len(gone)} gone, {len(renamed)} renamed, "
        f"{len(changed)} with another value")
    for a, b in renamed[:60]:
        out(f"    renamed {b['id']:.0f}: \"{a['title']}\" → \"{b['title']}\" ({b.get('valueText')})")
    for p in added[:60]:
        out(f"    added {p['id']:.0f} \"{p['title']}\" ({p.get('valueText')})")
    for a, b in changed[:60]:
        out(f"    value {b['id']:.0f} \"{b['title']}\": {a.get('valueText')} → {b.get('valueText')}")


def describe_tries(j):
    c = j.get("controllers")
    if c:
        out(f"  controllers tried: noise {c.get('windowNoisePixels')} px / {c.get('soundNoiseDb')} dB, note {c.get('noteLevelDb')} dB"
            + (f" — {c['warning']}" if c.get("warning") else ""))
        for e in c.get("effects", []):
            lv = e.get("levelDb", [])
            params = "; ".join(f"{x['title']}: {x['fromText']}→{x['toText']}" for x in e.get("parametersLowToHigh", []))
            out(f"    CC {e['cc']}{' (' + e['name'] + ')' if e.get('name') else ''}: {', '.join(e.get('changes', []))} · "
                f"level {lv[1] if len(lv) > 2 else '?'} → {lv[2] if len(lv) > 2 else '?'} dB · brightness {e.get('brightnessDb')} · "
                f"patch value {e.get('patchValue')} ({e.get('patchValueMatch', '')})"
                + (f" · region {e['region']}" if e.get("region") else "") + (f" · params: {params}" if params else ""))
            if e.get("reportedByPlugin"):
                out(f"      reported by the plug-in: {e['reportedByPlugin']}")
        out(f"    no effect: {ranges(c.get('noEffect', []))}  (128 channel pressure, 129 pitch bend)")
        if c.get("notMapped"):
            out(f"    not mapped (can't reach the plug-in): {ranges(c.get('notMapped'))}")
    p = j.get("parameters")
    if p:
        out(f"  parameters tried: placeholders {p.get('placeholders')}, controllers' {p.get('controllerParameters')}")
        for e in p.get("effects", []):
            out(f"    {e['id']:.0f} \"{e['title']}\" ({e.get('valueText')}): {', '.join(e.get('changes', []))} · level {e.get('levelDb')}"
                + (f" · region {e['region']}" if e.get("region") else ""))
        if p.get("notTried"):
            out(f"    not tried: {p['notTried']}")
    s = j.get("switches")
    if s:
        out(f"  articulation values that change a parameter: {s.get('valuesChangingParameters')} of {len(s.get('values', {}))}")
        for v, e in s.get("values", {}).items():
            if e:
                out(f"    {v}: " + "; ".join(f"{x['title']}: {x['fromText']}→{x['toText']}" for x in e.get("parameters", []))
                    + (f" reported {e['reportedByPlugin']}" if e.get("reportedByPlugin") else ""))


def describe_blob(folder, name):
    path = os.path.join(folder, name)
    data = open(path, "rb").read()
    if not data:
        out(f"  {name}: empty")
        return
    out(f"  {name}: {len(data)} bytes, entropy {entropy(data):.2f} bits/byte, starts {data[:16].hex(' ')} {data[:16]!r}")
    decoded_dir = os.path.join(folder, "_decoded")
    streams = zlib_streams(data)
    for off, dec in streams:
        os.makedirs(decoded_dir, exist_ok=True)
        dname = f"{name}.{off}.bin"
        open(os.path.join(decoded_dir, dname), "wb").write(dec)
        out(f"    zlib stream at {off}: {len(dec)} bytes decoded (_decoded/{dname}), entropy {entropy(dec):.2f}")
    sources = [("raw", data)] + [(f"zlib@{o}", d) for o, d in streams]
    for label, blob in sources:
        found = strings(blob)
        if not found:
            continue
        keep = [(o, s) for o, s in found if interesting(s)]
        out(f"    {label}: {len(found)} strings, {len(keep)} of note")
        for o, s in (found if FULL else keep)[:200]:
            out(f"      @{o}: {s[:200]}")
        with open(os.path.join(folder, f"_decoded/{name}.{label}.strings.txt") if os.path.isdir(decoded_dir)
                  else os.path.join(folder, f"{name}.strings.txt"), "w", encoding="utf-8") as f:
            for o, s in found:
                f.write(f"{o}\t{s}\n")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if not args:
        print(__doc__)
        sys.exit(1)
    folder = load_folder(args[0])
    base = None
    if os.path.exists(os.path.join(folder, "plugin.json")):
        base = json.load(open(os.path.join(folder, "plugin.json"), encoding="utf-8"))
        describe_plugin(base, "The plug-in, nothing loaded")
        describe_parameters(base)
        describe_mapping(base)
        describe_units(base)
        describe_channels(base)
        for name in ("plugin component.bin", "plugin controller.bin"):
            if os.path.exists(os.path.join(folder, name)):
                describe_blob(folder, name)
        out()
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".json") or name in ("plugin.json",):
            continue
        j = json.load(open(os.path.join(folder, name), encoding="utf-8"))
        if "patch" not in j:
            continue
        out(f"# {j['patch']} (pitch {j.get('pitch')}, {'sounds' if j.get('sounds') else 'played nothing'})")
        if j.get("error"):
            out(f"  error: {j['error']}")
            out()
            continue
        d = j.get("describe", {})
        if base:
            compare(base, d)
        if FULL or not base:
            describe_parameters(d)
            describe_mapping(d)
        describe_units(d)
        describe_channels(d)
        describe_tries(j)
        stem = name[:-5]
        for suffix in (" component.bin", " controller.bin"):
            if os.path.exists(os.path.join(folder, stem + suffix)):
                describe_blob(folder, stem + suffix)
        out()
    with open(os.path.join(folder, "report.txt"), "w", encoding="utf-8") as f:
        f.write("\n".join(REPORT) + "\n")
    print(f"(written to {os.path.join(folder, 'report.txt')})")


if __name__ == "__main__":
    main()
