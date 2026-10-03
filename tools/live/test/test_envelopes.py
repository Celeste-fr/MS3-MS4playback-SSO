"""MuseScore Envelopes (tools/live/MuseScoreEnvelopes/core.py) against a stand-in Live with clip envelopes that
behaves as Live 12.4.6 did on the test VM (core.py's header): create_event takes the parameter's value, events_in_range
gives the stored one (a Volume stores another), two events at one time are a jump, delete_events_in_range includes
both ends, Arrangement clips have no envelopes.  Run: python3 tools/live/test/test_envelopes.py"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "MuseScoreEnvelopes"))
import core  # noqa: E402


class Param(object):
    def __init__(self, name, lo=0.0, hi=1.0, quantized=False, stored=None):
        self.name, self.min, self.max, self.is_quantized = name, lo, hi, quantized
        # (stored value <-> value: a Volume keeps another number than it shows)
        self.store, self.unstore = stored or ((lambda v: v), (lambda s: s))


class EE(object):
    def __init__(self, time, value):
        self.time, self.value = time, value


class Envelope(object):
    def __init__(self, par):
        self.par = par
        self.ev = []              # (time, stored value), in order of insertion within one time
        self.creates = 0

    def create_event(self, e):
        self.creates += 1
        i = len(self.ev)
        while i > 0 and self.ev[i - 1][0] > e.time:
            i -= 1
        self.ev.insert(i, (e.time, self.par.store(e.value)))

    def delete_events_in_range(self, a, b):
        self.ev = [x for x in self.ev if not (a <= x[0] <= b)]

    def events_in_range(self, a, b):
        return [EE(t, v) for t, v in self.ev if a <= t < b]

    def value_at_time(self, t):
        if not self.ev:
            return 0.0
        if t <= self.ev[0][0]:
            return self.par.unstore(self.ev[0][1])
        for et, ev in self.ev:        # (at a jump's time: the earlier value, as Live 12.4.6 gives it)
            if et == t:
                return self.par.unstore(ev)
        last = None
        for i, (et, ev) in enumerate(self.ev):
            if et <= t:
                last = i
        if last == len(self.ev) - 1:
            return self.par.unstore(self.ev[-1][1])
        (t0, v0), (t1, v1) = self.ev[last], self.ev[last + 1]
        s = v0 if t1 == t0 else v0 + (v1 - v0) * (t - t0) / (t1 - t0)
        return self.par.unstore(s)


class Clip(object):
    def __init__(self, arrangement=False, length=8.0):
        self.arrangement = arrangement
        self.length = self.end_marker = self.loop_end = length
        self.envs = {}

    def automation_envelope(self, par):
        return None if self.arrangement else self.envs.get(id(par))

    def create_automation_envelope(self, par):
        if self.arrangement:
            raise RuntimeError("Not a session clip or parameter belongs to another track.")
        self.envs[id(par)] = Envelope(par)
        return self.envs[id(par)]

    def clear_envelope(self, par):
        self.envs.pop(id(par), None)


class Slot(object):
    def __init__(self, clip=None):
        self.clip = clip

    @property
    def has_clip(self):
        return self.clip is not None


class Obj(object):
    def __init__(self, **kw):
        self.__dict__.update(kw)


def volume_like():
    return Param("Volume", stored=((lambda v: v ** 3), (lambda s: s ** (1.0 / 3))))


class Live(object):
    def __init__(self):
        self.transpose = Param("Transpose", -48.0, 48.0)
        self.algorithm = Param("Algorithm", 0.0, 10.0, quantized=True)
        self.volume = volume_like()
        self.vol = Param("Track Volume")
        self.pan = Param("Track Panning", -1.0, 1.0)
        self.clip = Clip()
        self.track = Obj(clip_slots=[Slot(self.clip), Slot()], devices=[
            Obj(parameters=[Param("Device On", quantized=True), self.algorithm, self.transpose, self.volume])],
            mixer_device=Obj(volume=self.vol, panning=self.pan))
        self.song = Obj(tracks=[Obj(clip_slots=[], devices=[], mixer_device=Obj(volume=Param("v"), panning=Param("p"))),
                                self.track])
        self.sent = []
        self.h = core.Handler(self.song, lambda d, a: self.sent.append((core.parse(d), a)), EE)

    def msg(self, address, *args):
        self.h.handle(core.osc(address, *args), ("127.0.0.1", 9002))

    def got(self, address=None):
        out = [m for (m, a) in self.sent if address is None or m[0] == address]
        return out

    def write(self, key, write, base, lanes):
        """lanes: [(d, p, [(tick, v01)…])]"""
        self.msg("/ms/env/write", key, write, 1, 0, base, len(lanes))
        for d, p, pts in lanes:
            chunks = max(1, (len(pts) + 99) // 100)
            for c in range(chunks):
                args = []
                for t, v in pts[c * 100:(c + 1) * 100]:
                    args += [t, float(v)]
                self.msg("/ms/env/lane", key, write, d, p, c, chunks, *args)


def lane_of(live, d, p):
    out = []
    for m in live.got("/live/env/lane"):
        if m[1][1] == d and m[1][2] == p:
            a = m[1][5:]
            out += [(a[i], round(a[i + 1], 4)) for i in range(0, len(a), 2)]
    return out


class Test(unittest.TestCase):
    def test_hello(self):
        L = Live()
        L.msg("/ms/env/hello")
        self.assertEqual(L.got("/live/env/hello")[0][1], [core.VERSION])

    def test_read_empty_and_write_back(self):
        L = Live()
        L.msg("/ms/env/read", "c1", 1, 0)
        begin = L.got("/live/env/begin")[0][1]
        self.assertEqual(begin[:2], ["c1", "ok"])
        self.assertEqual(begin[3], 0)
        h0 = begin[2]
        # Transpose: 0 -> 40 ramp over beat 0-4, a jump to -20 at beat 4, held
        L.write("c1", 1, h0, [(0, 2, [(0, 0.5), (1920, (40 + 48) / 96.0), (1920, (28) / 96.0)])])
        w = L.got("/live/env/written")[-1][1]
        self.assertEqual(w[:3], ["c1", 1, "ok"])
        env = L.clip.envs[id(L.transpose)]
        self.assertEqual([(t, round(v, 4)) for t, v in env.ev], [(0.0, 0.0), (4.0, 40.0), (4.0, -20.0)])
        self.assertAlmostEqual(env.value_at_time(2.0), 20.0, places=4)
        self.assertAlmostEqual(env.value_at_time(4.001), -20.0, places=4)      # (of two at one time the later holds)
        # read back: the same breakpoints
        L.sent = []
        L.msg("/ms/env/read", "c1", 1, 0)
        self.assertEqual(L.got("/live/env/begin")[0][1][2], w[3])
        self.assertEqual(lane_of(L, 0, 2), [(0, 0.5), (1920, round(88 / 96.0, 4)), (1920, round(28 / 96.0, 4))])

    def test_values_read_as_the_parameter_has_them(self):
        L = Live()
        env = L.clip.create_automation_envelope(L.volume)
        for t, v in [(0.0, 0.2), (2.0, 0.2), (2.0, 0.8), (6.0, 0.4)]:
            env.create_event(EE(t, v))
        self.assertNotAlmostEqual(env.ev[0][1], 0.2)                 # (stored otherwise)
        L.msg("/ms/env/read", "c1", 1, 0)
        self.assertEqual(lane_of(L, 0, 3), [(0, 0.2), (960, 0.2), (960, 0.8), (2880, 0.4)])

    def test_mixer_and_quantized(self):
        L = Live()
        L.msg("/ms/env/read", "c1", 1, 0)
        h = L.got("/live/env/begin")[0][1][2]
        L.write("c1", 1, h, [(-1, 1, [(0, 0.0), (480, 1.0)]), (0, 1, [(0, 0.33)])])
        self.assertEqual(L.got("/live/env/written")[-1][1][2], "ok")
        self.assertEqual([v for t, v in L.clip.envs[id(L.pan)].ev], [-1.0, 1.0])
        self.assertEqual([v for t, v in L.clip.envs[id(L.algorithm)].ev], [3.0])     # 3.3 -> 3

    def test_clear(self):
        L = Live()
        env = L.clip.create_automation_envelope(L.transpose)
        env.create_event(EE(0.0, 5.0))
        L.msg("/ms/env/read", "c1", 1, 0)
        h = L.got("/live/env/begin")[0][1][2]
        L.write("c1", 2, h, [(0, 2, [])])
        self.assertEqual(L.got("/live/env/written")[-1][1][2], "ok")
        self.assertNotIn(id(L.transpose), L.clip.envs)

    def test_rewrite_replaces_and_once(self):
        L = Live()
        L.msg("/ms/env/read", "c1", 1, 0)
        h = L.got("/live/env/begin")[0][1][2]
        L.write("c1", 1, h, [(0, 2, [(0, 0.5), (960, 0.75)])])
        h1 = L.got("/live/env/written")[-1][1][3]
        L.write("c1", 2, h1, [(0, 2, [(480, 0.25)])])
        env = L.clip.envs[id(L.transpose)]
        self.assertEqual(env.ev, [(1.0, -24.0)])
        n = env.creates
        L.write("c1", 2, h1, [(0, 2, [(480, 0.25)])])                 # sent again: applied once
        self.assertEqual(env.creates, n)
        self.assertEqual(L.got("/live/env/written")[-1][1][:3], ["c1", 2, "ok"])

    def test_chunks(self):
        L = Live()
        L.msg("/ms/env/read", "c1", 1, 0)
        h = L.got("/live/env/begin")[0][1][2]
        pts = [(i * 10, (i % 50) / 50.0) for i in range(250)]
        L.write("c1", 1, h, [(0, 2, pts)])
        self.assertEqual(L.got("/live/env/written")[-1][1][2], "ok")
        self.assertEqual(len(L.clip.envs[id(L.transpose)].ev), 250)
        L.sent = []
        L.msg("/ms/env/read", "c1", 1, 0)
        self.assertEqual(len(L.got("/live/env/lane")), 3)
        self.assertEqual(len(lane_of(L, 0, 2)), 250)

    def test_conflict(self):
        L = Live()
        L.msg("/ms/env/read", "c1", 1, 0)
        h = L.got("/live/env/begin")[0][1][2]
        L.h.check()
        self.assertEqual(L.got("/live/env/conflict"), [])
        L.clip.create_automation_envelope(L.transpose).create_event(EE(1.0, 3.0))     # drawn in Live
        L.h.check()
        self.assertEqual(len(L.got("/live/env/conflict")), 1)
        L.h.check()
        self.assertEqual(len(L.got("/live/env/conflict")), 1)          # (said once)
        L.write("c1", 1, h, [(0, 2, [(0, 0.5)])])                      # MuseScore's base is older
        self.assertEqual(L.got("/live/env/written")[-1][1][2], "conflict")
        self.assertEqual(L.clip.envs[id(L.transpose)].ev, [(1.0, 3.0)])

    def test_arrangement_and_gone(self):
        L = Live()
        L.msg("/ms/env/read", "a1", 1, -1)
        self.assertEqual(L.got("/live/env/begin")[-1][1][1], "arrangement")
        L.msg("/ms/env/read", "c2", 1, 1)
        self.assertEqual(L.got("/live/env/begin")[-1][1][1], "gone")
        L.msg("/ms/env/read", "c1", 1, 0)
        L.track.clip_slots[0].clip = None
        L.h.check()
        self.assertEqual(L.got("/live/env/gone")[-1][1], ["c1"])

    def test_osc_roundtrip(self):
        d = core.osc("/x", "abc", 7, 0.5, -3)
        self.assertEqual(core.parse(d), ("/x", ["abc", 7, 0.5, -3]))


if __name__ == "__main__":
    unittest.main()
