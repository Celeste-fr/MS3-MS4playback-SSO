#!/usr/bin/env python3
"""alsset.py: read a Live set (.als, gzip XML) the way the real-Live check needs it, stdlib only (runs on the VM's
Python too).

  alsset.py tracks <set.als>                 the tracks in Live's order: kind, name, MuseScore key, group, devices,
                                             Track Delay, the export file name Live gives it ("<base> <name>[-n].wav")
  alsset.py length <set.als> [tail s]       the arrangement's end, tempo range, bars to render (renderBars)
  alsset.py variant <in.mscz> <out.mscz>     the score with its first note a whole tone higher (Update Live Set's
                                             edit; made on the VM from the score Create Live Set saved: its liveTracks)
  alsset.py liveedit <in.als> <out.als> [beats]  what a user does in Live, as text edits on a set Live saved (Update
                                             Live Set's end-to-end test): one note of a technique clip (the second
                                             part's technique track with notes, its third note) moved later by beats
                                             (default 0.0625), one configured Kontakt panel parameter without
                                             automation (Configure, Manual) turned by 0.25; prints the edits (json)
  alsset.py diff <written.als> <saved.als>   does what MuseScore wrote survive Live's save? Every element and attribute
                                             of the written set must come back (numbers within float32 rounding);
                                             what Live adds is listed, not judged; a known list of view / session
                                             state is ignored (IGNORE). Exit 1 on a lost or changed value.

The diff is generic on purpose: it needs no knowledge of which part of the set MuseScore owns, so a new feature (a
device store, a new clip envelope) is covered the day it is written. Tracks are matched by their MuseScore key (the
Info text "MuseScore: …", Name/Annotation), else by name; lists of like elements by position.
"""
import collections
import gzip
import json
import math
import re
import sys
import xml.etree.ElementTree as ET

# Elements whose value Live changes on every save or by the GUI run itself (view, selection, transport, ids it
# renumbers): never a finding. Matched against the element's tag (anywhere) or its tag path's tail.
IGNORE_TAGS = {
    "LomId", "LomIdView", "OverwriteProtectionNumber", "ViewData", "IsContentSelectedInDocument", "SelectedDocumentViewInMainWindow",
    "HighlightedTrackIndex", "SequencerNavigator", "TakeCounter", "NeedRefreeze", "NeedArrangerRefreeze",
    "SourceHint", "NoteAlgorithms", "TimeSelection", "ScrollerTimePreserver", "SessionScrollPos", "ViewStateFxSlotCount",
    "ViewStateSessionMixerVolumeSectionHeight", "ViewStateArrangerMixerVolumeSectionHeight", "ClientSize",
    "AutomationEnvelopesListWrapper", "DetailClipKeyMidis", "SelectedBreakpointValue", "IsContentSplitterOpen",
    "IsExpressionSplitterOpen", "ContentLanes", "ExpressionLanes", "LastPresetRef", "LastSelectedTimeableIndex",
    "LastSelectedClipEnvelopeIndex", "ModulationSourceCount", "PreHearTrack", "GroovePool", "IsFolded",
    "TrackUnfolded", "ShowAutomationLanes", "Selection", "ChooserBar", "AnnotationNewName",
}
IGNORE_PATHS = ("Transport/CurrentTime", "Transport/LoopStart", "Transport/LoopLength", "Transport/LoopOn",
                "Transport/LoopIsSongStart", "Transport/CurrentTime", "LiveSet/Locators")
IGNORE_ATTRS = {"SelectedToolPanel", "SelectedTransformationName", "SelectedGeneratorName"}
IGNORE_ROOT_ATTRS = {"MinorVersion", "SchemaChangeCount", "Creator", "Revision", "MajorVersion"}
NUM = re.compile(r"^-?(\d+\.?\d*|\.\d+)([eE][-+]?\d+)?$")


def load(path):
    data = open(path, "rb").read()
    if data[:2] == b"\x1f\x8b":
        data = gzip.decompress(data)
    return ET.fromstring(data)


def same_value(a, b):
    if a == b:
        return True
    if a is None or b is None:
        return False
    if NUM.match(a) and NUM.match(b):
        x, y = float(a), float(b)
        # Live keeps many values as float32 (Track Delay -9.8 -> -9.80000019) or prints doubles with more digits
        return math.isclose(x, y, rel_tol=2e-7, abs_tol=1e-6)
    return False


def track_key(t):
    ann = t.find("Name/Annotation")
    if ann is not None and ann.get("Value", "").startswith("MuseScore:"):
        return ann.get("Value")
    n = t.find("Name/EffectiveName")
    return "%s:%s" % (t.tag, n.get("Value") if n is not None else t.get("Id"))


def tracks(root):
    """the set's tracks in order, with what the check needs"""
    ls = root.find("LiveSet")
    out = []
    groups = {}
    for t in list(ls.find("Tracks")):
        name = t.find("Name/EffectiveName").get("Value")
        devs = t.find("DeviceChain/DeviceChain/Devices")
        devices = []
        for d in (list(devs) if devs is not None else []):
            e = {"type": d.tag}
            if d.tag == "PluginDevice":
                n = d.find(".//PluginDesc//Name")
                e["plugin"] = n.get("Value") if n is not None else ""
            if d.tag == "StereoGain":
                g = d.find("Gain/Manual")
                u = d.find("UserName")
                e["gain"] = float(g.get("Value")) if g is not None else None
                e["name"] = u.get("Value") if u is not None else ""
            if d.tag.startswith("MxDevice"):
                f = d.find(".//MxPatchRef/FileRef/RelativePath")
                p = d.find(".//MxPatchRef/FileRef/Path")
                e["patch"] = (p.get("Value") if p is not None else "") or (f.get("Value") if f is not None else "")
            devices.append(e)
        td = t.find("TrackDelay/Value")
        gid = t.find("TrackGroupId")
        mo = t.find("DeviceChain/MidiOutputRouting/Target")
        vol = t.find("DeviceChain/Mixer/Volume/Manual")
        act = t.find("DeviceChain/Mixer/Speaker/Manual")
        e = {
            "id": t.get("Id"), "kind": t.tag, "name": name, "key": track_key(t),
            "group": gid.get("Value") if gid is not None else "-1",
            "trackDelayMs": float(td.get("Value")) if td is not None else 0.0,
            "devices": devices,
            "midiTo": mo.get("Value") if mo is not None else "",
            "volume": float(vol.get("Value")) if vol is not None else None,
            "active": (act.get("Value") == "true") if act is not None else True,
        }
        out.append(e)
        if t.tag == "GroupTrack":
            groups[t.get("Id")] = e
    # Live's file names for "All Individual Tracks": "<base> <name>.wav", a name met before gets "-1", "-2" … in track
    # order (Live 12.4.6, the VM 2026-10-22: group "Violin" and its Kontakt "Violin" -> "Violin", "Violin-1").
    seen = collections.Counter()
    for e in out:
        n = e["name"]
        e["exportName"] = n if seen[n] == 0 else "%s-%d" % (n, seen[n])
        seen[n] += 1
        e["renders"] = e["kind"] in ("GroupTrack", "AudioTrack", "ReturnTrack") or any(
            d["type"] in ("PluginDevice", "InstrumentGroupDevice") or d["type"].startswith("MxDeviceInstrument")
            for d in e["devices"])
    for e in out:
        e["children"] = [c["id"] for c in out if c["group"] == e["id"]]
    return out


def length(root, tail_s=6.0):
    """the arrangement's end (beats, the last clip's end), the tempo range and the bars to render so that tail_s
    seconds of release follow the last clip at the fastest tempo"""
    ls = root.find("LiveSet")
    ends = [float(c.find("CurrentEnd").get("Value")) for c in ls.iter("MidiClip") if c.find("CurrentEnd") is not None]
    ends += [float(c.find("CurrentEnd").get("Value")) for c in ls.iter("AudioClip") if c.find("CurrentEnd") is not None]
    mt = ls.find("MainTrack")
    tempo = mt.find(".//Tempo")
    t0 = float(tempo.find("Manual").get("Value"))
    tid = tempo.find("AutomationTarget").get("Id")
    tempos = [t0]
    for ae in mt.iter("AutomationEnvelope"):
        if ae.find("EnvelopeTarget/PointeeId").get("Value") == tid:
            tempos += [float(e.get("Value")) for e in ae.iter("FloatEvent")]
    ts = int(float(mt.find(".//TimeSignature/Manual").get("Value")))
    num = ts % 99 + 1
    end = max(ends) if ends else 0.0
    bars = int(math.ceil((end + tail_s * max(tempos) / 60.0) / num)) + 1
    return {"endBeats": end, "tempoMin": min(tempos), "tempoMax": max(tempos), "beatsPerBar": num, "renderBars": bars}


def _path_ignored(path):
    tags = [p.split("[")[0] for p in path.split("/") if p]
    if any(t in IGNORE_TAGS for t in tags):
        return True
    plain = "/".join(tags)
    return any(plain.endswith(p) for p in IGNORE_PATHS)


def diff(written, saved):
    """(lost, changed, added): lists of (path, written value, saved value); paths with track keys"""
    lost, changed, added = [], [], []

    def walk(x, y, path):
        if _path_ignored(path):
            return
        for k in set(x.attrib) | set(y.attrib):
            if (path == "" and k in IGNORE_ROOT_ATTRS) or k in IGNORE_ATTRS:
                continue
            if k == "Id" and x.tag not in ("Tracks",):
                # element ids Live may renumber are compared through their pointee references only when equal;
                # a changed pointee id is not a lost value
                if x.get(k) != y.get(k) and x.get(k) is not None and y.get(k) is not None:
                    continue
            a, b = x.get(k), y.get(k)
            if a is None:
                added.append((path + "@" + k, None, b))
            elif b is None:
                lost.append((path + "@" + k, a, None))
            elif not same_value(a, b):
                changed.append((path + "@" + k, a, b))
        xc, yc = list(x), list(y)
        if x.tag == "Tracks" or x.tag == "ReturnTracks":
            ym = {track_key(c): c for c in yc}
            for c in xc:
                k = track_key(c)
                seg = "/" + k.replace("/", "\u203a")
                if k in ym:
                    walk(c, ym.pop(k), path + seg)
                else:
                    lost.append((path + seg, "track", None))
            for k in ym:
                added.append((path + "/" + k.replace("/", "\u203a"), None, "track"))
            return
        byx = collections.defaultdict(list)
        byy = collections.defaultdict(list)
        for c in xc:
            byx[c.tag].append(c)
        for c in yc:
            byy[c.tag].append(c)
        for tag, xs in byx.items():
            ys = byy.get(tag, [])
            for i, c in enumerate(xs):
                p = "%s/%s[%d]" % (path, tag, i) if len(xs) > 1 or len(ys) > 1 else "%s/%s" % (path, tag)
                if i < len(ys):
                    walk(c, ys[i], p)
                elif not _path_ignored(p):
                    lost.append((p, "element", None))
            for i in range(len(xs), len(ys)):
                p = "%s/%s[%d]" % (path, tag, i)
                if not _path_ignored(p):
                    added.append((p, None, "element"))
        for tag in byy:
            if tag not in byx:
                p = "%s/%s" % (path, tag)
                if not _path_ignored(p):
                    added.append((p, None, "element"))

    walk(written, saved, "")
    return lost, changed, added


def summarize(items, n=40):
    """group paths by their shape (indices and track keys dropped) with a count and one example"""
    g = collections.OrderedDict()
    for p, a, b in items:
        shape = re.sub(r"\[\d+\]", "", p)
        shape = re.sub(r"/(MuseScore|MidiTrack|AudioTrack|GroupTrack|ReturnTrack):[^/]*", "/<track>", shape)
        if shape not in g:
            g[shape] = [0, p, a, b]
        g[shape][0] += 1
    return [{"shape": s, "count": v[0], "example": v[1], "written": v[2], "saved": v[3]} for s, v in list(g.items())[:n]]


def diff_report(written_path, saved_path):
    lost, changed, added = diff(load(written_path), load(saved_path))
    return {
        "verdict": "PASS" if not lost and not changed else "FAIL",
        "lost": len(lost), "changed": len(changed), "added": len(added),
        "lostShapes": summarize(lost), "changedShapes": summarize(changed), "addedShapes": summarize(added, 25),
    }


# ------------------------------------------------------------------- the score's variant (Update Live Set's test)

def rewrite_mscz(src, out, fn):
    """a copy of a .mscz with fn applied to its .mscx text"""
    import zipfile
    with zipfile.ZipFile(src) as z, zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as o:
        for info in z.infolist():
            data = z.read(info.filename)
            if info.filename.endswith(".mscx"):
                data = fn(data.decode("utf-8")).encode("utf-8")
            o.writestr(info, data)


def variant_text(text):
    """one note a whole tone higher: the first <Note> of the music (pitch + 2, tpc + 2)"""
    start = text.rfind("</Part>")
    m = re.compile(r"<Note>.*?</Note>", re.S).search(text, start)
    if not m:
        raise SystemExit("variant: no note in the score")
    note = m.group(0)
    note2 = re.sub(r"<pitch>(\d+)</pitch>", lambda p: "<pitch>%d</pitch>" % (int(p.group(1)) + 2), note, count=1)
    note2 = re.sub(r"<tpc>(-?\d+)</tpc>", lambda p: "<tpc>%d</tpc>" % (int(p.group(1)) + 2), note2, count=1)
    return text[:m.start()] + note2 + text[m.end():]


# ------------------------------------------------------------------- a user's edits in Live (Update Live Set's test)

def live_edit(src, out, beats=0.0625):
    """text edits (the set's own formatting kept: Update Live Set edits the file as text) of what a user does in Live"""
    with gzip.open(src, "rb") as f:
        x = f.read().decode("utf-8")
    root = ET.fromstring(x)
    tr = tracks(root)
    edits = []
    # (1) a note moved in a technique clip: the first technique track (MuseScore Link on it) with 3+ notes whose part
    # group isn't the first part's (the variant edits the first part's first note)
    tech = [t for t in tr if t["kind"] == "MidiTrack" and any(d["type"].startswith("MxDevice") for d in t["devices"])]
    first_group = tech[0]["group"] if tech else None
    for t in tech:
        if t["group"] == first_group:
            continue
        m = re.search(r'<MidiTrack Id="%s"' % re.escape(t["id"]), x)
        end = x.find("</MidiTrack>", m.end())
        notes = list(re.finditer(r'<MidiNoteEvent Time="([0-9.eE+-]+)"', x[m.end():end]))
        if len(notes) < 3:
            continue
        n = notes[2]
        at = m.end() + n.start(1)
        old = float(n.group(1))
        x = x[:at] + repr(round(old + beats, 6)) + x[at + len(n.group(1)):]
        edits.append({"edit": "note moved", "track": t["name"], "key": t["key"], "noteIndex": 2, "from": old,
                      "to": round(old + beats, 6), "beats": beats})
        break
    # (2) a Configure slot's knob turned (no envelope on it): Manual +-0.25
    pointees = set(re.findall(r'<PointeeId Value="(\d+)" />', x))
    for m in re.finditer(r'<PluginFloatParameter Id="\d+">\s*<ParameterName Value="([^"]+)" />\s*<ParameterId Value="(\d+)" />'
                         r'(.*?)</PluginFloatParameter>', x, re.S):
        body = m.group(3)
        man = re.search(r'<Manual Value="([0-9.eE+-]+)" />', body)
        tgt = re.search(r'<AutomationTarget Id="(\d+)"', body)
        if not man or not tgt or tgt.group(1) in pointees:
            continue
        old = float(man.group(1))
        new = round(old + 0.25 if old < 0.5 else old - 0.25, 6)
        at = m.start(3) + man.start(1)
        # the track holding it
        tid = None
        for t in re.finditer(r'<(?:MidiTrack|AudioTrack) Id="(\d+)"', x[:at]):
            tid = t.group(1)
        name = next((t["name"] for t in tr if t["id"] == tid), "?")
        x = x[:at] + repr(new) + x[at + len(man.group(1)):]
        edits.append({"edit": "knob turned", "track": name, "parameter": m.group(1), "from": old, "to": new})
        break
    with gzip.open(out, "wb") as f:
        f.write(x.encode("utf-8"))
    return edits


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    if len(argv) >= 2 and argv[0] == "tracks":
        print(json.dumps(tracks(load(argv[1])), indent=1, ensure_ascii=False))
        return 0
    if len(argv) >= 2 and argv[0] == "length":
        print(json.dumps(length(load(argv[1]), float(argv[2]) if len(argv) > 2 else 6.0)))
        return 0
    if len(argv) >= 3 and argv[0] == "variant":
        rewrite_mscz(argv[1], argv[2], variant_text)
        return 0
    if len(argv) >= 3 and argv[0] == "liveedit":
        print(json.dumps(live_edit(argv[1], argv[2], float(argv[3]) if len(argv) > 3 else 0.0625), indent=1, ensure_ascii=False))
        return 0
    if len(argv) >= 3 and argv[0] == "diff":
        r = diff_report(argv[1], argv[2])
        print(json.dumps(r, indent=1, ensure_ascii=False))
        return 0 if r["verdict"] == "PASS" else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
