"""The Control Surface around core.Handler: a non-blocking UDP socket polled on Live's main thread (update_display,
about 10 times a second; every Live API call happens there), the envelopes of the clips read checked every second."""

import os
import socket
import time
import traceback

import Live
from _Framework.ControlSurface import ControlSurface

from . import core

LOG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "MuseScoreEnvelopes.log")


def log(*a):
    try:
        with open(LOG, "a") as f:
            f.write(time.strftime("%H:%M:%S ") + " ".join(str(x) for x in a) + "\n")
    except Exception:
        pass


class MuseScoreEnvelopes(ControlSurface):
    def __init__(self, c_instance):
        ControlSurface.__init__(self, c_instance)
        self._sock = None
        self._checked = 0.0
        try:
            if os.path.exists(LOG) and os.path.getsize(LOG) > 1000000:
                os.remove(LOG)
            self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self._sock.setblocking(False)
            self._sock.bind(("127.0.0.1", core.PORT))
            app = Live.Application.get_application()
            log("listening on", core.PORT, "in Live", app.get_major_version(), app.get_minor_version(),
                app.get_bugfix_version())
        except Exception:
            log(traceback.format_exc())
            self._sock = None
        self._handler = core.Handler(self.song(), self._send, Live.Envelope.EnvelopeEvent, log)

    def _send(self, data, addr):
        try:
            self._sock.sendto(data, addr)
        except Exception:
            log(traceback.format_exc())

    def update_display(self):
        ControlSurface.update_display(self)
        if self._sock is None:
            return
        for _ in range(200):
            try:
                data, addr = self._sock.recvfrom(65536)
            except BlockingIOError:
                break
            except OSError:
                continue          # (Windows: an earlier answer's port was closed, WSAECONNRESET)
            self._handler.handle(data, addr)
        t = time.time()
        if t - self._checked >= 1.0:
            self._checked = t
            try:
                self._handler.check()
            except Exception:
                log(traceback.format_exc())

    def disconnect(self):
        try:
            if self._sock is not None:
                self._sock.close()
        except Exception:
            pass
        self._sock = None
        ControlSurface.disconnect(self)
