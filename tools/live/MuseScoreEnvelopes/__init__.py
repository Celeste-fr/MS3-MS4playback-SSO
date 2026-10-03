"""MuseScore Envelopes: a Live Control Surface script that writes the automation lanes of MuseScore's clip tabs into
the Live clip's own envelopes, and reads them back (core.py has the protocol and what Live's API does; LIVE.md ›
Editing Live clips in MuseScore › Automation lanes in a clip tab).

Install (once): copy this folder to <Live's User Library>/Remote Scripts/MuseScoreEnvelopes (on Windows usually
Documents\\Ableton\\User Library\\Remote Scripts), restart Live, then Settings › Tempo & MIDI › Control Surface: pick
"MuseScoreEnvelopes" in a free row (Input and Output: None). It listens on UDP 127.0.0.1:9005 only.
"""

from .surface import MuseScoreEnvelopes


def create_instance(c_instance):
    return MuseScoreEnvelopes(c_instance)
