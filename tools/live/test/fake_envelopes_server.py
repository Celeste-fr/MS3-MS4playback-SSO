"""A stand-in for Live with the MuseScore Envelopes script, on its real UDP port (9005): the script's own protocol code
(tools/live/MuseScoreEnvelopes/core.py) over the stand-in Live of test_envelopes.py, for a real MuseScore with
fake_live_server.js --edit-clip <track> --session: every track index is the edited clip's track, its first session slot
the clip; the track's devices as fake_live_server.js has them: MuseScore Link (no parameters), the Synth Rack (Cutoff
0-1, Drive 0-2). The clip starts with a Drive envelope (1 from beat 0, a ramp to 2 over beats 4-8); --live-change-at:
a Cutoff breakpoint drawn "in Live" then (a conflict).

    python3 fake_envelopes_server.py [--quit-at <s>] [--live-change-at <s>]

Prints each message and, at the end, the clip's envelopes ("ENVELOPES " + JSON)."""

import json
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import test_envelopes as T  # noqa: E402
import core  # noqa: E402  (test_envelopes put its folder on the path)

quit_at = live_change_at = -1.0
a = sys.argv[1:]
for i, x in enumerate(a):
    if x == "--quit-at":
        quit_at = float(a[i + 1])
    if x == "--live-change-at":
        live_change_at = float(a[i + 1])

cutoff, drive = T.Param("Cutoff"), T.Param("Drive", 0.0, 2.0)
clip = T.Clip(length=8.0)
track = T.Obj(clip_slots=[T.Slot(clip)], devices=[T.Obj(parameters=[]), T.Obj(parameters=[cutoff, drive])],
              mixer_device=T.Obj(volume=T.Param("Track Volume"), panning=T.Param("Track Panning", -1.0, 1.0)))


class Tracks(object):
    def __iter__(self):
        return iter([track] * 16)


song = T.Obj(tracks=Tracks())
env = clip.create_automation_envelope(drive)
for t, v in [(0.0, 1.0), (4.0, 1.0), (8.0, 2.0)]:
    env.create_event(T.EE(t, v))

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", core.PORT))
sock.settimeout(0.1)
t0 = time.time()


def send(d, addr):
    print("%.3f -> %s" % (time.time() - t0, core.parse(d)), flush=True)
    sock.sendto(d, addr)


h = core.Handler(song, send, T.EE, lambda *x: print("log", *x, flush=True))
checked = 0
changed = False
while quit_at < 0 or time.time() - t0 < quit_at:
    try:
        d, addr = sock.recvfrom(65536)
        print("%.3f <- %s" % (time.time() - t0, core.parse(d)), flush=True)
        h.handle(d, addr)
    except socket.timeout:
        pass
    if live_change_at >= 0 and not changed and time.time() - t0 >= live_change_at:
        changed = True
        clip.automation_envelope(cutoff) or clip.create_automation_envelope(cutoff)
        clip.automation_envelope(cutoff).create_event(T.EE(2.0, 0.9))
        print("%.3f changed in Live: Cutoff 0.9 at beat 2" % (time.time() - t0), flush=True)
    if time.time() - checked >= 1.0:
        checked = time.time()
        h.check()
out = {p.name: [(t, round(p.unstore(v), 4)) for t, v in e.ev] for p in (cutoff, drive)
       for e in [clip.automation_envelope(p)] if e is not None}
print("ENVELOPES " + json.dumps(out), flush=True)
