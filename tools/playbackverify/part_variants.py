#!/usr/bin/env python3
"""Copies of a score (the owner's scores stay out of the repository: write them to a temporary folder) with
[tuning] hostedTuning set in its playbackSettings metaTag, and (per part) every other part played by MS4's built-in
sounds (metaTag partPlayback), so a hosted render (--verify-playback --verify-wav) loads only that part's Kontakt
instances: the VM's memory took Whence's 20 instances in one render only without Live running (2026-10-16, load
profile). part_variants.py <unzipped .mscz folder> <outdir>: Whence-<k>-slots.mscz / Whence-<k>-vari.mscz for each part
k (PARTS: Whence's string parts, in score order), and Whence-slots / Whence-vari with every part. Compare the renders
with compare_renders.py."""
import json, re, sys, zipfile, os
src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
files = []
for root, _, names in os.walk(src):
    for n in names:
        p = os.path.join(root, n)
        files.append((os.path.relpath(p, src), p))
PARTS = ["Violins 1", "Violins 2", "Violas", "Violoncellos", "Contrabasses"]

def write(name, value, only):
    with zipfile.ZipFile(os.path.join(out, name), "w", zipfile.ZIP_DEFLATED) as o:
        for rel, p in sorted(files, key=lambda f: (not f[0].startswith("META-INF"), f[0])):
            data = open(p, "rb").read()
            if rel.endswith(".mscx"):
                text = data.decode("utf-8")
                tag = re.search(r'<metaTag name="playbackSettings">([^<]*)</metaTag>', text)
                assert tag, "no playbackSettings metaTag"
                items = [i for i in tag.group(1).split(";") if i and not i.startswith("tuning/hostedTuning")]
                items.append("tuning/hostedTuning=%d" % value)
                text = text[:tag.start(1)] + ";".join(items) + text[tag.end(1):]
                assert 'name="partPlayback"' not in text
                if only is not None:
                    modes = [{"part": i, "name": n, "mode": "library" if i == only else "ms4"} for i, n in enumerate(PARTS)]
                    end = text.index("</metaTag>", text.index('<metaTag name="playbackSettings">')) + len("</metaTag>")
                    text = text[:end] + '\n    <metaTag name="partPlayback">' + json.dumps(modes) + "</metaTag>" + text[end:]
                data = text.encode("utf-8")
            o.writestr(rel, data)
    print(name)

for value, kind in ((1, "slots"), (0, "vari")):
    write("Whence-%s.mscz" % kind, value, None)
    for k in range(5):
        write("Whence-%d-%s.mscz" % (k, kind), value, k)
