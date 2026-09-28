#!/usr/bin/env python3
# Reads the patch's own articulation name under each value from the contact sheets of a
# "Check articulations" result (MuseScore: View > Sound Library > Check articulations) and
# compares it with the map's name.
#
#   python3 read_check_names.py "<unzipped result folder>" [--all]
#
# Prints the values whose name differs (or shows SSO's "None": the patch lacks the value), and
# with --all every value. Needs ImageMagick (convert) and tesseract. OCR misreads (Long ->
# "Large", Tenuto -> "Terait"): look at the sheet for anything it flags before changing the
# map. "AT LOAD": the picture most values showed (the sheet's last cell) is an articulation, not
# "None": the one selected when the patch loaded, which the scan can't find by picture (the
# values the patch lacks show it too); its UACC line gives its value.
# Made for SSO's window (the name is the third bright text line in its left panel).
import json, subprocess, sys, os, csv, io, difflib, re
folder = sys.argv[1]
show_all = '--all' in sys.argv[2:]
d = json.load(open(os.path.join(folder, 'results.json')))

def ocr(img, psm='7'):
    return subprocess.run(['tesseract', img, '-', '--psm', psm], capture_output=True, text=True).stdout.strip()

for p in d['patches']:
    if 'region' not in p: continue
    win = os.path.join(folder, p['patch'] + ' (window).png')
    raw = subprocess.run(['convert', win, '-crop', '200x420+375+230', '+repage', '-colorspace', 'gray', '-depth', '8', 'gray:-'],
                         capture_output=True).stdout
    W = 200
    rows = [sum(1 for x in range(W) if raw[y * W + x] > 200) for y in range(len(raw) // W)]
    runs, st = [], None
    for y, c in enumerate(rows + [0]):
        if c > 2 and st is None: st = y
        if c <= 2 and st is not None: runs.append((230 + st, 230 + y - 1)); st = None
    runs = [r for r in runs if r[0] >= 360 and r[1] - r[0] >= 3]
    if len(runs) < 3:
        print(f"{p['patch']}\t-\t-\t(name row not found {runs})\t-\t-"); continue
    uy = runs[2][1] + 12      # the UACC line, just under the name
    x0, y0, w, h = p['region']
    scale = min(1.0, 560.0 / w, 360.0 / h)
    cw, ch = int(w * scale), int(h * scale)
    cols = max(1, min(4, 1400 // (cw + 12)))
    sheet = os.path.join(folder, p['patch'] + '.png')
    byvalue = {a['value']: a for a in p['articulations']}
    order = p.get('sheet') or [a['value'] for a in p['articulations']]
    for i, v in enumerate(order):
        a = byvalue.get(v, {'value': v, 'names': [], 'sound': 'shows no articulation'})
        cx = 12 + (i % cols) * (cw + 12)
        cy = 64 + (i // cols) * (36 + ch + 12)
        def crop(wy0, wy1, out):
            sy = cy + 36 + int((wy0 - y0) * scale); sh = max(4, int((wy1 - wy0) * scale))
            subprocess.run(['convert', sheet, '-crop', f'{int(300*scale)}x{sh}+{cx}+{sy}', '+repage',
                            '-colorspace', 'gray', '-negate', '-resize', '500%', '-normalize', out])
        crop(uy - 34, uy - 4, '/tmp/_n.png')
        crop(uy - 3, uy + 16, '/tmp/_u.png')
        name = re.sub(r'^[|\s]+|[\s_\-—=:ai7|]+$', '', ocr('/tmp/_n.png'))
        norm = lambda t: re.sub(r'[^a-z0-9]', '', t.lower())
        ratio = max([difflib.SequenceMatcher(None, norm(n), norm(name)).ratio() for n in a['names']] or [0])
        none = re.match(r'n[oa]ne\b', name.lower()) is not None
        flag = 'NONE' if none else ('check' if ratio < 0.62 else '')
        if p.get('noneOnSheet') and i == len(order) - 1 and v == p.get('noneValueLike'):
            # the picture most values showed: "None", or the articulation selected at load,
            # whose own value (the UACC line) the scan could not tell from the values it lacks
            if none:
                flag = ''
            else:
                u = re.search(r'(\d+)', ocr('/tmp/_u.png'))
                a = dict(a, names=['(most values show this)'])
                flag = 'AT LOAD' + (f": UACC {u.group(1)}?" if u else '')
        if show_all or flag:
            print('\t'.join(str(x) for x in (p['patch'], a['value'], ' / '.join(a['names']) or '(not in map)', name,
                                              a['sound'] + (f" @{a['pitch']}" if 'pitch' in a else ''), flag)))
