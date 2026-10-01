# Research prototype (not installed by MuseScore): a Live 12 Control Surface script that writes a curve into the
# ARRANGEMENT automation of an open set, which neither Max for Live's Live Object Model nor any other API can
# write directly in Live 12.2. LIVE.md › Automation lanes › Writing Live's own automation.
from .muse import MuseScoreAuto


def create_instance(c_instance):
    return MuseScoreAuto(c_instance)
