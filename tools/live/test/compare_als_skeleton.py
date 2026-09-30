#!/usr/bin/env python3
"""Compare the element tree of a Live Set MuseScore wrote (Create Live Set…, libmscore/livesetwriter.h)
with a set Ableton Live saved itself.

    compare_als_skeleton.py <generated.als|.xml> <saved-by-live.als|.xml> [...more saved by Live]

For every kind of element the generated set has (its path from the root, list positions and ids left
out), its attributes' names and its children, in order, must be those Live writes for that element in one of the reference sets
(a run of the same child, like 8 ClipSlots or 128 PluginFloatParameters, counts as one). Elements only
the references have (clips, return tracks, envelopes …) are content the generated set leaves out and are
not compared; elements only the generated set has are listed (no reference to compare with). An element
the generated set leaves empty where Live's has items (SendsPre and Sends without return tracks, an empty
SourceContext or LastPresetRef value, as Live writes them for a track's mixer) and the Tracks list (fewer
kinds of track) are content left out: listed apart, not a difference.

The reference sets are the owner's own and are never put in the repository: without them (the file
missing) this exits 0 with "skipped". Exit 1 when a compared element's children differ.
"""
import gzip
import os
import re
import sys
import xml.etree.ElementTree as ET


def load(path):
    data = open(path, 'rb').read()
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    return ET.fromstring(data)


# list items whose tag carries a number (ControllerTargets.5, Fields.2) count as one kind
def norm(tag):
    return re.sub(r'\.\d+$', '.n', tag)


def children(e):
    out = ['@' + ','.join(sorted(e.attrib))] if e.attrib else []
    for c in e:
        t = norm(c.tag)
        if out and out[-1] == t:
            continue
        out.append(t)
    return tuple(out)


def signatures(root):
    sig = {}

    def walk(e, path):
        p = path + '/' + norm(e.tag)
        sig.setdefault(p, set()).add(children(e))
        for c in e:
            walk(c, p)
    walk(root, '')
    return sig


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    gen_path, refs = sys.argv[1], sys.argv[2:]
    refs = [r for r in refs if os.path.exists(r)]
    if not refs:
        print('skipped: no reference set (the owner\'s, kept outside the repository)')
        return 0
    gen = signatures(load(gen_path))
    ref = {}
    for r in refs:
        for k, v in signatures(load(r)).items():
            ref.setdefault(k, set()).update(v)
    compared = differ = 0
    only = []
    left_out = []
    for path in sorted(gen):
        if path not in ref:
            only.append(path)
            continue
        compared += 1
        for kids in gen[path]:
            if kids in ref[path]:
                continue
            empty = not [k for k in kids if not k.startswith('@')] and any(
                [k for k in kids if k.startswith('@')] == [k for k in rk if k.startswith('@')] and len(rk) > len(kids)
                for rk in ref[path])
            fewer_tracks = path.endswith('/LiveSet/Tracks') and any(set(kids) <= set(rk) for rk in ref[path])
            if empty or fewer_tracks:
                left_out.append((path, ' '.join(kids) or '(empty)', ' | '.join(' '.join(rk) for rk in sorted(ref[path]))))
                continue
            differ += 1
            print('DIFFERS', path)
            print('   generated:', ' '.join(kids))
            for rk in sorted(ref[path]):
                print('   Live     :', ' '.join(rk))
    print('%d kinds of element compared, %d differ; %d only in the generated set; %d with content left out'
          % (compared, differ, len(only), len(left_out)))
    for p, g, r in left_out:
        print('   content left out:', p, '| generated:', g, '| Live:', r)
    for p in only:
        print('   only generated:', p)
    return 1 if differ else 0


if __name__ == '__main__':
    sys.exit(main())
