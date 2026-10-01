"""MuseScoreAuto: Live 12.2's Python API (Control Surface scripts) can write a Session clip's envelopes
(Clip.create_automation_envelope, Envelope.insert_step; Envelope.create_event only from Live 12.4) and copy a clip
into the Arrangement (Track.duplicate_clip_to_arrangement). A Session clip's envelope of a device or mixer
parameter, copied there, becomes the track's Arrangement automation of that parameter over the clip's span (the
other spans are left as they were), and stays when the copied clip is deleted again.

Install: copy this folder to <User Library>/Remote Scripts/MuseScoreAuto, then Settings › Link, Tempo & MIDI ›
Control Surface: MuseScoreAuto. It does nothing until a job file "trigger.json" appears next to this file:
  {"track": 0, "param": "volume" | "panning" | <a device parameter's name>, "start": <beats>, "length": <beats>,
   "points": [[beat, value 0-1], ...]}   (beats from "start"; each value holds until the next point)
It writes the points as steps (insert_step from one point to the next: Live 12.2 has no breakpoint insertion,
so a ramp is a staircase of the points MuseScore samples), copies the clip to "start" and deletes both clips.
Measured on the test VM (Live 12.2, 2026-10-01): the track's Pan then followed the points while Live played
(within 0.02 of the curve read back twice a second); 388 envelope events written in ~60 ms.
Caveat: the copy replaces whatever clip the track has over that span (a MuseScore clip there is cut in two), so a
real implementation copies the MuseScore clip itself (its notes and the envelopes) over its own span.
Log: muse.log next to this file.
"""
import json
import os
import time
import traceback

import Live
from _Framework.ControlSurface import ControlSurface

HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "muse.log")
TRIG = os.path.join(HERE, "trigger.json")


def log(*a):
    with open(LOG, "a") as f:
        f.write(time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a) + "\n")


class MuseScoreAuto(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        try:
            app = Live.Application.get_application()
            log("loaded in Live", app.get_major_version(), app.get_minor_version(), app.get_bugfix_version(),
                "Envelope:", [m for m in dir(Live.Envelope.Envelope) if not m.startswith("_")])
        except Exception:
            log(traceback.format_exc())

    def update_display(self):
        ControlSurface.update_display(self)
        if os.path.exists(TRIG):
            try:
                with open(TRIG) as f:
                    job = json.load(f)
                os.remove(TRIG)
                self.write(job)
            except Exception:
                log(traceback.format_exc())

    def parameter(self, track, name):
        if name in ("volume", "panning"):
            return getattr(track.mixer_device, name)
        for d in track.devices:
            for p in d.parameters:
                if name in (p.name, p.original_name):
                    return p
        raise RuntimeError("no parameter " + name + " on " + track.name)

    def write(self, job):
        song = self.song()
        track = song.tracks[int(job.get("track", 0))]
        p = self.parameter(track, job["param"])
        length = float(job["length"])
        points = sorted((float(t), float(v)) for t, v in job["points"])
        slot = next(s for s in track.clip_slots if not s.has_clip)      # a free Session slot
        t0 = time.time()
        slot.create_clip(length)
        env = slot.clip.create_automation_envelope(p)
        ends = [t for t, _ in points[1:]] + [length]
        for (t, v), end in zip(points, ends):
            if end > t:
                env.insert_step(t, end - t, p.min + v * (p.max - p.min))
        copy = track.duplicate_clip_to_arrangement(slot.clip, float(job.get("start", 0)))
        slot.delete_clip()
        track.delete_clip(copy)
        log("wrote", len(points), "points of", p.name, "on", track.name, "in", round((time.time() - t0) * 1000, 1),
            "ms; automation_state", p.automation_state)
