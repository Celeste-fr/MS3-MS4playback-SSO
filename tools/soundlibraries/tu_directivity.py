#!/usr/bin/env python3
"""Directivity index in the loudest direction, per instrument and dynamic, from TU Berlin's directivity database
(Ackermann, Boehm, Brinkmann & Weinzierl, "A Database with Directivities of Musical Instruments", J. Audio Eng. Soc.
72(3), 2024; DepositOnce, doi 10.14279/depositonce-19858, version 3, 2024-02-12; CC BY-NC-SA 4.0: the files are not
in the repository, only derived numbers). For section_balance_cl.py's sound power (Lw = Lp + 20 log10(10 m) + 11 -
DI_max): Clark & Luce measured each instrument at 10 m in its loudest direction.

    python3 -I tu_directivity.py singletones <1a_DirectivitiesSingleTones_SOFA dir> <out.json>
    python3 -I tu_directivity.py recordings <out.json> <TU label> <2_<label>_recordings.zip URL or path> ...

singletones (1a: ff only, every file "dynamic = ff", whatever Table 1 of the documentation says): per note, the
receiver's pressure squared = the sum of its partials' RMS pressures squared (Data.Real, M x R x N).
recordings (2, both dynamics: "<label>_<dynamic>_<note>_recordings.sofa", a one-sided spectrum per channel, calibrated
in Pa): per note and dynamic, each channel's mean square over the steady part (SteadyPart, samples; the two-sided
spectrum of the documentation's equation (1): the bins between DC and Nyquist halved), by Parseval. A zip given as a
URL is read by HTTP range requests, member by member (nothing stored).

Both: DI_r = 10 log10(p_r^2 / sum_q w_q p_q^2), w the 32 receivers' spherical Voronoi areas over 4 pi (the 2.06 m
sphere's even coverage); per instrument and dynamic, the receiver with the highest mean DI over the notes is the loudest
direction, diMax its mean. "pp", "ff": the same receiver as each dynamic's own choice; "ppAtFf": pp's DI at ff's receiver
(Clark & Luce kept one microphone direction per instrument for every dynamic).
"""
import io
import json
import math
import os
import re
import sys
import zipfile

import h5py
import numpy as np
from scipy.spatial import SphericalVoronoi

NOTE = {'c': 0, 'd': 2, 'e': 4, 'f': 5, 'g': 7, 'a': 9, 'b': 11}


def weights(pos):
    """spherical Voronoi areas / 4 pi of the receivers (azimuth, elevation in degrees)"""
    az, el = np.radians(pos[:, 0]), np.radians(pos[:, 1])
    pts = np.stack([np.cos(el) * np.cos(az), np.cos(el) * np.sin(az), np.sin(el)], axis=1)
    sv = SphericalVoronoi(pts, 1.0, np.zeros(3))
    sv.sort_vertices_of_regions()
    a = sv.calculate_areas()
    return a / a.sum()


def di(p2, w):
    return 10 * np.log10(p2 / float((w * p2).sum()))


def loudest(rows):
    """rows: [DI per receiver] per note -> (receiver, its mean DI, notes)"""
    m = np.mean(np.array(rows), axis=0)
    r = int(np.argmax(m))
    return r, float(m[r]), len(rows)


def singletones(root, outPath):
    out = {}
    for inst in sorted(os.listdir(root)):
        rows, w = [], None
        for fn in sorted(os.listdir(os.path.join(root, inst))):
            f = h5py.File(os.path.join(root, inst, fn), 'r')
            if w is None:
                w = weights(f['ReceiverPosition'][:])
            p2 = (f['Data.Real'][0] ** 2).sum(axis=1)
            rows.append(di(p2, w))
        r, v, n = loudest(rows)
        out[inst] = {'ff': {'receiver': r, 'diMax': round(v, 2), 'notes': n}}
        print('%-32s ff receiver %2d diMax %.2f (%d notes)' % (inst, r, v, n))
    json.dump(out, open(outPath, 'w'), indent=1)


class HttpFile(io.RawIOBase):
    """a read-only, seekable file over HTTP range requests"""

    def __init__(self, url):
        import urllib.request
        self.url, self.pos, self.req = url, 0, urllib.request
        with self.req.urlopen(self.req.Request(url, headers={'Range': 'bytes=0-0'}), timeout=120) as r:
            self.url = r.geturl()
            self.size = int(r.headers['Content-Range'].rsplit('/', 1)[1])

    def seekable(self):
        return True

    def readable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def readinto(self, b):
        n = min(len(b), self.size - self.pos)
        if n <= 0:
            return 0
        for attempt in range(5):
            try:
                rq = self.req.Request(self.url, headers={'Range': 'bytes=%d-%d' % (self.pos, self.pos + n - 1)})
                with self.req.urlopen(rq, timeout=120) as r:
                    data = r.read()
                break
            except OSError:
                if attempt == 4:
                    raise
        b[:len(data)] = data
        self.pos += len(data)
        return len(data)


def channelPower(f):
    """each channel's mean square over the steady part, from the one-sided spectrum (documentation eq. 1)"""
    X = f['Data.Real'][0] + 1j * f['Data.Imag'][0]          # R x K, K = N/2 + 1
    K = X.shape[1]
    N = 2 * (K - 1)
    h = np.full(K, 0.5)
    h[0] = h[-1] = 1.0
    x = np.fft.irfft(X * h, n=N, axis=1)                     # (the TF is the DFT: numpy's irfft is its inverse, in Pa)
    s = f['SteadyPart'][:].ravel() if 'SteadyPart' in f else None
    a, b = (int(s[0]), int(s[1])) if s is not None and len(s) >= 2 else (0, N)
    a, b = max(0, a - 1), min(N, b)                          # (1-based, inclusive, as MATLAB)
    return (x[:, a:b] ** 2).mean(axis=1)


def recordings(outPath, pairs):
    out = json.load(open(outPath)) if os.path.exists(outPath) else {}
    for label, src in pairs:
        if label in out and out[label].get('complete'):
            continue
        fh = HttpFile(src) if src.startswith('http') else open(src, 'rb')
        z = zipfile.ZipFile(io.BufferedReader(fh, buffer_size=1 << 20) if src.startswith('http') else fh)
        rows, w, notes = {'pp': [], 'ff': []}, None, {'pp': [], 'ff': []}
        for info in z.infolist():
            m = re.search(r'_(pp|ff)_([a-g]k?\d)_recordings\.sofa$', info.filename)
            if not m:
                continue
            f = h5py.File(io.BytesIO(z.read(info)), 'r')
            if w is None:
                w = weights(f['ReceiverPosition'][:])
            d = f.attrs['Description']
            d = d.decode() if isinstance(d, bytes) else d
            dyn = d.split('dynamic =')[-1].strip()
            if dyn != m.group(1):
                print('  %s: name %s, description %s' % (info.filename, m.group(1), dyn))
            rows[dyn].append(di(channelPower(f), w))
            notes[dyn].append(int(f['MIDINote'][0]))
        e = {'source': src.rsplit('/', 3)[-1] if not src.startswith('http') else src, 'complete': True}
        for dyn in ('pp', 'ff'):
            r, v, n = loudest(rows[dyn])
            e[dyn] = {'receiver': r, 'diMax': round(v, 2), 'notes': n, 'pitches': [min(notes[dyn]), max(notes[dyn])]}
        rf = e['ff']['receiver']
        e['ppAtFf'] = round(float(np.mean(np.array(rows['pp']), axis=0)[rf]), 2)
        e['ppReceiverMeanDb'] = [round(float(v), 2) for v in np.mean(np.array(rows['pp']), axis=0)]
        e['ffReceiverMeanDb'] = [round(float(v), 2) for v in np.mean(np.array(rows['ff']), axis=0)]
        out[label] = e
        json.dump(out, open(outPath, 'w'), indent=1)
        print('%-28s ff r%-2d %.2f (%d)  pp r%-2d %.2f (%d)  pp at ff\'s %.2f' % (
            label, rf, e['ff']['diMax'], e['ff']['notes'], e['pp']['receiver'], e['pp']['diMax'], e['pp']['notes'],
            e['ppAtFf']), flush=True)


if __name__ == '__main__':
    if sys.argv[1] == 'singletones':
        singletones(sys.argv[2], sys.argv[3])
    else:
        a = sys.argv[3:]
        recordings(sys.argv[2], list(zip(a[0::2], a[1::2])))
