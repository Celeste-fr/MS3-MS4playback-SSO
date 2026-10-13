"""A seekable file over HTTP range requests, so zipfile can read single members of a remote zip without downloading
the whole archive (the TU Berlin database's recordings come as 1-3 GB zips per instrument; the dev VM has little disk).

    import zipfile, httpzip
    z = zipfile.ZipFile(httpzip.HttpFile(url))
"""
import io
import time
import urllib.request


class HttpFile(io.RawIOBase):
    def __init__(self, url, block=1 << 20, cached_blocks=8):
        self.url, self.pos, self.block, self.cached, self.cache = url, 0, block, cached_blocks, {}
        # DSpace's HEAD carries no Content-Length: a one-byte range answers with Content-Range "bytes 0-0/<size>"
        with urllib.request.urlopen(urllib.request.Request(url, headers={"Range": "bytes=0-0"}), timeout=60) as r:
            self.size = int(r.headers["Content-Range"].rsplit("/", 1)[1])

    def seekable(self):
        return True

    def readable(self):
        return True

    def tell(self):
        return self.pos

    def seek(self, off, whence=0):
        self.pos = off if whence == 0 else self.pos + off if whence == 1 else self.size + off
        return self.pos

    def _get(self, a, b):
        for attempt in range(5):
            try:
                req = urllib.request.Request(self.url, headers={"Range": f"bytes={a}-{b - 1}"})
                with urllib.request.urlopen(req, timeout=120) as r:
                    data = r.read()
                if len(data) == b - a:
                    return data
            except OSError:
                pass
            time.sleep(2 + 5 * attempt)
        raise OSError(f"range {a}-{b} of {self.url} failed")

    def read(self, n=-1):
        if n is None or n < 0:
            n = self.size - self.pos
        n = max(0, min(n, self.size - self.pos))
        if n == 0:
            return b""
        if n > 4 * self.block:
            d = self._get(self.pos, self.pos + n)
            self.pos += n
            return d
        out = bytearray()
        while n > 0:
            bi = self.pos // self.block
            if bi not in self.cache:
                if len(self.cache) >= self.cached:
                    self.cache.pop(next(iter(self.cache)))
                a = bi * self.block
                self.cache[bi] = self._get(a, min(self.size, a + self.block))
            blk = self.cache[bi]
            o = self.pos - bi * self.block
            d = blk[o:o + n]
            out += d
            self.pos += len(d)
            n -= len(d)
        return bytes(out)

    def readinto(self, b):
        d = self.read(len(b))
        b[:len(d)] = d
        return len(d)
