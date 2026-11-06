#!/usr/bin/env python3
"""spheres_fetch.py: one file out of a remote zip by HTTP range requests (zip64 aware), so a stem of the Spheres dataset
(Zenodo 17347681, 10+ GB zips) comes without the whole archive. Standard library only.

    spheres_fetch.py list <url>
    spheres_fetch.py get <url> <path in the zip> <out file>

e.g. the Mozart K. 550 close stems spheres_k550_staccato.py measures:
    U="https://zenodo.org/records/17347681/files/TheSpheresDataset-Multichannel.zip?download=1"
    B=TheSpheresDataset-Multichannel/TheSpheres_data/Mozart
    for p in Violin_1/Violin_1 Cello/Cello Bass_1/Bass; do
        python3 -I tools/soundlibraries/spheres_fetch.py get "$U" "$B/$p.flac" "<dir>/$(echo $p | tr / _).flac"; done
(the private accent-spheres / research-staccato rzip.py + get.py, 2026-10-20, folded in.) The dataset is CC BY-SA 4.0:
keep the audio outside the repo; derived numbers only.
"""
import struct, sys, zlib, urllib.request, json
def rng(url, a, b):
    req = urllib.request.Request(url, headers={'Range': f'bytes={a}-{b}'})
    for i in range(5):
        try:
            return urllib.request.urlopen(req, timeout=120).read()
        except Exception as e:
            err = e
    raise err
def size(url):
    req = urllib.request.Request(url, method='HEAD')
    return int(urllib.request.urlopen(req, timeout=60).headers['Content-Length'])
def listing(url):
    n = size(url)
    tail = rng(url, n - 65536 - 22, n - 1)
    i = tail.rfind(b'PK\x05\x06')
    cd_size, cd_off = struct.unpack('<II', tail[i+12:i+20])
    j = tail.rfind(b'PK\x06\x06')
    if j >= 0:
        cd_size, cd_off = struct.unpack('<QQ', tail[j+40:j+56])
    cd = rng(url, cd_off, cd_off + cd_size - 1)
    out, p = [], 0
    while p < len(cd) and cd[p:p+4] == b'PK\x01\x02':
        meth, = struct.unpack('<H', cd[p+10:p+12])
        csz, usz = struct.unpack('<II', cd[p+20:p+28])
        nl, el, cl = struct.unpack('<HHH', cd[p+28:p+34])
        off, = struct.unpack('<I', cd[p+42:p+46])
        name = cd[p+46:p+46+nl].decode('utf-8', 'replace')
        ex = cd[p+46+nl:p+46+nl+el]
        q = 0
        while q < len(ex):
            hid, hl = struct.unpack('<HH', ex[q:q+4])
            if hid == 1:
                vals = ex[q+4:q+4+hl]; k = 0
                if usz == 0xFFFFFFFF: usz, = struct.unpack('<Q', vals[k:k+8]); k += 8
                if csz == 0xFFFFFFFF: csz, = struct.unpack('<Q', vals[k:k+8]); k += 8
                if off == 0xFFFFFFFF: off, = struct.unpack('<Q', vals[k:k+8]); k += 8
            q += 4 + hl
        out.append((name, meth, csz, usz, off))
        p += 46 + nl + el + cl
    return out
def fetch(url, entry, dst):
    name, meth, csz, usz, off = entry
    h = rng(url, off, off + 29)
    nl, el = struct.unpack('<HH', h[26:30])
    start = off + 30 + nl + el
    data = b''
    step = 32 << 20
    with open(dst, 'wb') as f:
        d = zlib.decompressobj(-15) if meth == 8 else None
        a = start
        while a < start + csz:
            b = min(start + csz, a + step) - 1
            chunk = rng(url, a, b)
            f.write(d.decompress(chunk) if d else chunk)
            a = b + 1
        if d: f.write(d.flush())


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == "list":
        for e in listing(sys.argv[2]):
            print(e[0], e[1], e[2], e[3], e[4])
    elif len(sys.argv) == 5 and sys.argv[1] == "get":
        url, want, dst = sys.argv[2:5]
        for e in listing(url):
            if e[0] == want:
                fetch(url, e, dst)
                print("ok", dst)
                return
        sys.exit("missing: " + want)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
