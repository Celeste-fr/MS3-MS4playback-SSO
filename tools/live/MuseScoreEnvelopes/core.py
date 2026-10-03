"""MuseScore Envelopes, the part without Live: OSC, the protocol, the clip envelopes read and written.

A Live Control Surface script (LIVE.md › Editing Live clips in MuseScore › Automation lanes in a clip tab). The owner,
2026-10-02: automation lanes drawn in an Edit-in-MuseScore clip tab are written into the Live clip's own envelopes.
Max for Live's Object Model has no envelopes (Live 12.4.6: a Clip offers has_envelopes, clear_envelope and
clear_all_envelopes only; calling automation_envelope from a device fails), Live's Python API has them:
Clip.create_automation_envelope / automation_envelope (Session clips only: None or an error for an Arrangement
clip, still in 12.4.6), Envelope.create_event (Live 12.4: an EnvelopeEvent(time, value)), insert_step,
events_in_range, delete_events_in_range, value_at_time. Tried on the test VM (Live 12.4.6, 2026-10-02):
- create_event's value is the parameter's value (min … max, as value_at_time gives it); events_in_range gives Live's
  internal value, which differs for some parameters (a Volume: 0.25 is stored as 0.0363), so values are read with
  value_at_time;
- two events at one time are a jump (the later one holds from then: value_at_time at that time is the later one);
- an event's control coefficients (a curve) are ignored by create_event (always read back 0.5, the segment straight):
  MuseScore sends a curved segment as short straight ones;
- delete_events_in_range(a, b) includes both ends.
Nothing here needs more than Live 12.4.

The protocol (OSC over UDP on 127.0.0.1, this script listening on PORT; answers go to the sender's address):
  MuseScore -> script
    /ms/env/hello                                     -> /live/env/hello version:i
    /ms/env/read  key:s track:i slot:i                 the clip in that track's session slot (slot -1: an
                                                       arrangement clip: answered "arrangement")
        -> /live/env/begin key:s status:s hash:i lanes:i   (status "ok", "arrangement", "gone", "error: …")
        -> /live/env/lane  key:s d:i p:i chunk:i chunks:i (tick:i value:f) × n    (each envelope; values 0-1 of the
                                                       parameter's range, ticks 480 a beat of clip time)
    /ms/env/write key:s write:i track:i slot:i hash:i lanes:i     (hash: the envelopes as MuseScore knows them)
    /ms/env/lane  key:s write:i d:i p:i chunk:i chunks:i (tick:i value:f) × n   (n 0: the envelope cleared)
        -> /live/env/written key:s write:i status:s hash:i     ("ok", "conflict", "gone", "error: …"), once every
                                                       lane's chunks are in; a write sent again is applied once
    /ms/env/close key:s
  script -> MuseScore (checked every second for each clip read here)
    /live/env/conflict key:s hash:i                    the envelopes changed in Live
    /live/env/gone key:s
A parameter: d >= 0: the track's devices[d].parameters[p]; d -1: the mixer (p 0 volume, 1 panning).
"""

import struct

VERSION = 1
PORT = 9005
TICKS = 480
PAIRS_PER_PACKET = 100
EPS = 1e-6


# ---------------------------------------------------------------- OSC 1.0: i, f, s

def _pad(b):
    return b + b"\0" * (4 - len(b) % 4)


def osc(address, *args):
    tags = ","
    data = b""
    for a in args:
        if isinstance(a, bool) or isinstance(a, int):
            tags += "i"
            data += struct.pack(">i", int(a))
        elif isinstance(a, float):
            tags += "f"
            data += struct.pack(">f", a)
        else:
            tags += "s"
            data += _pad(str(a).encode("utf-8"))
    return _pad(address.encode("utf-8")) + _pad(tags.encode("utf-8")) + data


def parse(data):
    def string(i):
        j = data.index(b"\0", i)
        return data[i:j].decode("utf-8", "replace"), (j // 4 + 1) * 4
    address, i = string(0)
    tags, i = string(i)
    args = []
    for t in tags[1:]:
        if t == "i":
            args.append(struct.unpack(">i", data[i:i + 4])[0])
            i += 4
        elif t == "f":
            args.append(struct.unpack(">f", data[i:i + 4])[0])
            i += 4
        elif t == "s":
            s, i = string(i)
            args.append(s)
        else:
            raise ValueError("OSC type " + t)
    return address, args


def int32(h):
    return h - (1 << 32) if h >= (1 << 31) else h


# ---------------------------------------------------------------- the clip and its parameters

def clip_at(song, track, slot):
    """the session clip, None when gone; raises LookupError for an arrangement clip (slot -1)"""
    if slot < 0:
        raise LookupError("arrangement")
    tracks = list(song.tracks)
    if track < 0 or track >= len(tracks):
        return None
    slots = list(tracks[track].clip_slots)
    if slot >= len(slots) or not slots[slot].has_clip:
        return None
    return slots[slot].clip


def parameters(track):
    """(d, p, parameter) of everything a clip envelope can automate on the track"""
    out = [(-1, 0, track.mixer_device.volume), (-1, 1, track.mixer_device.panning)]
    for d, dev in enumerate(track.devices):
        for p, par in enumerate(dev.parameters):
            out.append((d, p, par))
    return out


def parameter(track, d, p):
    if d < 0:
        return track.mixer_device.volume if p == 0 else track.mixer_device.panning
    return list(list(track.devices)[d].parameters)[p]


def track_of(song, clip_track):
    return list(song.tracks)[clip_track]


def to01(par, v):
    span = par.max - par.min
    return 0.0 if span <= 0 else min(1.0, max(0.0, (v - par.min) / span))


def from01(par, v):
    x = par.min + max(0.0, min(1.0, v)) * (par.max - par.min)
    return float(round(x)) if par.is_quantized else x


def envelopes(clip, track):
    """[(d, p, parameter, envelope)] the clip has"""
    out = []
    for d, p, par in parameters(track):
        env = clip.automation_envelope(par)
        if env is not None:
            out.append((d, p, par, env))
    return out


def events(env, end):
    return sorted(((e.time, e.value) for e in env.events_in_range(-1.0, end)), key=lambda x: x[0])


def horizon(clip):
    return max(float(clip.length), float(clip.end_marker), float(clip.loop_end)) + 100000.0


def fnv(h, x):
    for b in struct.pack(">q", x):
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def hash_envelopes(clip, track):
    """Live's events of every envelope of the clip (FNV-1a), as Live stores them"""
    h = 2166136261
    end = horizon(clip)
    for d, p, par, env in envelopes(clip, track):
        ev = events(env, end)
        if not ev:
            continue
        h = fnv(h, d)
        h = fnv(h, p)
        for t, v in ev:
            h = fnv(h, int(round(t * TICKS * 64)))
            h = fnv(h, int(round(v * 1e6)))
    return int32(h)


def read_lane(env, par, end):
    """the envelope as (tick, value 0-1) breakpoints in order: two at one tick are a jump"""
    ev = events(env, end)
    if not ev:
        return []
    # (a parameter whose stored value is its value: read as it is; else each value as value_at_time has it, the
    # earlier of two at one time just before that time)
    same = all(abs(env.value_at_time(t) - v) < 1e-6 for i, (t, v) in enumerate(ev)
               if (i + 1 == len(ev) or ev[i + 1][0] != t) and (i == 0 or ev[i - 1][0] != t))
    # (value_at_time at a jump's time gives the earlier value: the later one is read just after it)
    out = []
    for i, (t, v) in enumerate(ev):
        later = i + 1 < len(ev) and abs(ev[i + 1][0] - t) < EPS
        earlier = i > 0 and abs(ev[i - 1][0] - t) < EPS
        if later and t <= EPS:
            continue              # (the first of a jump at the clip's start: never in force)
        if not same:
            v = env.value_at_time(t + EPS if earlier else t - EPS if later else t)
        out.append((int(round(t * TICKS)), to01(par, v)))
    return out


# ---------------------------------------------------------------- the protocol

class Handler(object):
    """the protocol over a Live song; send(datagram, addr) answers; EnvelopeEvent: Live.Envelope.EnvelopeEvent"""

    def __init__(self, song, send, EnvelopeEvent, log=lambda *a: None):
        self.song = song
        self.send = send
        self.EE = EnvelopeEvent
        self.log = log
        self.edits = {}           # key -> {track, slot, hash, addr, write, reply, incoming}

    def handle(self, data, addr):
        try:
            address, a = parse(data)
        except Exception as e:
            self.log("bad datagram", repr(e))
            return
        try:
            if address == "/ms/env/hello":
                self.send(osc("/live/env/hello", VERSION), addr)
            elif address == "/ms/env/read":
                self.read(str(a[0]), int(a[1]), int(a[2]), addr)
            elif address == "/ms/env/write":
                self.begin_write(str(a[0]), int(a[1]), int(a[2]), int(a[3]), int(a[4]), int(a[5]), addr)
            elif address == "/ms/env/lane":
                self.lane(str(a[0]), int(a[1]), int(a[2]), int(a[3]), int(a[4]), int(a[5]), a[6:], addr)
            elif address == "/ms/env/close":
                self.edits.pop(str(a[0]), None)
        except Exception as e:
            import traceback
            self.log(traceback.format_exc())
            key = str(a[0]) if a else ""
            if address == "/ms/env/read":
                self.send(osc("/live/env/begin", key, "error: " + str(e), 0, 0), addr)
            elif address in ("/ms/env/write", "/ms/env/lane"):
                self.send(osc("/live/env/written", key, int(a[1]), "error: " + str(e), 0), addr)

    def edit(self, key, track, slot, addr):
        e = self.edits.get(key)
        if e is None:
            e = self.edits[key] = {"write": 0, "reply": None, "incoming": None, "hash": 0}
        e.update(track=track, slot=slot, addr=addr)
        return e

    def read(self, key, track, slot, addr):
        e = self.edit(key, track, slot, addr)
        try:
            clip = clip_at(self.song, track, slot)
        except LookupError:
            self.send(osc("/live/env/begin", key, "arrangement", 0, 0), addr)
            self.edits.pop(key, None)
            return
        if clip is None:
            self.send(osc("/live/env/begin", key, "gone", 0, 0), addr)
            self.edits.pop(key, None)
            return
        tr = track_of(self.song, track)
        end = horizon(clip)
        lanes = [(d, p, read_lane(env, par, end)) for d, p, par, env in envelopes(clip, tr)]
        lanes = [x for x in lanes if x[2]]
        e["hash"] = hash_envelopes(clip, tr)
        self.send(osc("/live/env/begin", key, "ok", e["hash"], len(lanes)), addr)
        for d, p, pts in lanes:
            chunks = max(1, (len(pts) + PAIRS_PER_PACKET - 1) // PAIRS_PER_PACKET)
            for c in range(chunks):
                args = [key, d, p, c, chunks]
                for t, v in pts[c * PAIRS_PER_PACKET:(c + 1) * PAIRS_PER_PACKET]:
                    args += [int(t), float(v)]
                self.send(osc("/live/env/lane", *args), addr)

    def begin_write(self, key, write, track, slot, base, lanes, addr):
        e = self.edit(key, track, slot, addr)
        if write == e["write"] and e["reply"] is not None:      # sent again: applied once, answered again
            self.send(e["reply"], addr)
            return
        e["incoming"] = {"write": write, "lanes": lanes, "base": base, "got": {}, "done": 0}
        if lanes == 0:
            self.apply(key, e)

    def lane(self, key, write, d, p, chunk, chunks, pairs, addr):
        e = self.edits.get(key)
        if e is None or e["incoming"] is None or e["incoming"]["write"] != write:
            return
        inc = e["incoming"]
        got = inc["got"].setdefault((d, p), {"chunks": chunks, "parts": {}})
        got["parts"][chunk] = [(int(pairs[i]), float(pairs[i + 1])) for i in range(0, len(pairs) - 1, 2)]
        if len(got["parts"]) == got["chunks"] and not got.get("complete"):
            got["complete"] = True
            inc["done"] += 1
        if inc["done"] == inc["lanes"]:
            self.apply(key, e)

    def reply(self, e, key, write, status, h):
        e["reply"] = osc("/live/env/written", key, write, status, h)
        e["write"] = write
        self.send(e["reply"], e["addr"])

    def apply(self, key, e):
        inc = e["incoming"]
        e["incoming"] = None
        try:
            clip = clip_at(self.song, e["track"], e["slot"])
        except LookupError:
            return self.reply(e, key, inc["write"], "error: an arrangement clip has no envelopes in Live's API", 0)
        if clip is None:
            return self.reply(e, key, inc["write"], "gone", 0)
        tr = track_of(self.song, e["track"])
        now = hash_envelopes(clip, tr)
        if now != inc["base"]:
            e["hash"] = now
            return self.reply(e, key, inc["write"], "conflict", now)
        end = horizon(clip)
        for (d, p), got in sorted(inc["got"].items()):
            par = parameter(tr, d, p)
            pts = []
            for c in range(got["chunks"]):
                pts += got["parts"].get(c, [])
            env = clip.automation_envelope(par)
            if not pts:
                if env is not None:
                    clip.clear_envelope(par)
                continue
            if env is None:
                env = clip.create_automation_envelope(par)
            else:
                env.delete_events_in_range(-1.0, end)
            for t, v in pts:          # (in order: of two at one time the later holds)
                env.create_event(self.EE(t / float(TICKS), from01(par, v)))
        e["hash"] = hash_envelopes(clip, tr)
        self.log("wrote", key, len(inc["got"]), "lane(s)")
        self.reply(e, key, inc["write"], "ok", e["hash"])

    def check(self):
        """every second: a clip read here whose envelopes changed in Live (or that is gone)"""
        for key in list(self.edits):
            e = self.edits[key]
            if e["incoming"] is not None:
                continue
            try:
                clip = clip_at(self.song, e["track"], e["slot"])
            except LookupError:
                continue
            if clip is None:
                self.send(osc("/live/env/gone", key), e["addr"])
                self.edits.pop(key, None)
                continue
            h = hash_envelopes(clip, track_of(self.song, e["track"]))
            if h != e["hash"]:
                e["hash"] = h
                self.send(osc("/live/env/conflict", key, h), e["addr"])
