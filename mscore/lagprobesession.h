//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  LagProbeSession: the lag probe's scripted session (the owner, 2026-10-16: "MuseScore already feels kinda laggy with
//  all the SSO patches loaded and the measurement going in the background"). Off unless MS_LAG_PROBE=<log file>
//  (libmscore/lagprobe.h); MuseScore is started as usual, in the real GUI, with the score on its command line.
//
//    measured    the event loop's latency (a 10 ms precise QTimer: each firing's lateness), the main thread's busy
//                stretches (a firing more than 30 ms late: the thread held from its due time) with the probe's scopes
//                in them, every scope (with the scope it ran in and the outermost one: what calls a render) (libmscore/lagprobe.h: commands, layout, even levels, the real range's and
//                master trim's renders, paints, the host's idle timer …), the audio callback's time against its period,
//                and the CPU of every thread of the process by name (Windows: GetThreadTimes, the thread's description
//                or the module its start address lies in; Linux: /proc/self/task)
//    script      once the score is shown, in Continuous View: phase o (opening: no input, until the library's preload
//                and the even levels' first measurement are done), then for each phase of MS_LAG_PROBE_PHASES (default
//                "o,b,c,d,a,e"), stopped and then playing, MS_LAG_PROBE_REPS (20) rounds of: select a note (as a click:
//                its preview plays), pitch-up, pitch-down, add-staccato, add-staccato again (removes it), undo, redo,
//                scroll the view (a wheel event), the Inspector toggled twice; MS_LAG_PROBE_GAP_MS (800) after each
//                (more than the even levels' 600 ms debounce: the measurement runs between edits). The rounds leave the
//                score as it was.
//                  b  [levels] evenLevels 1 (as the score / ini has it), the measurement running after each edit
//                  c  the same once the measurement is done, then paused (EvenAutoMeasurer::setProbePaused)
//                  d  c with every part's automation lanes open
//                  a  evenLevels 0 (the score's own playback setting, in memory)
//                  e  MS4 mode (no library), the baseline
//                  l  loading: every instance released and loaded again as at score open; while it loads, rounds of
//                     scroll, select, pitch-up, undo, Play pressed (the action, as the user presses it: "play enabled",
//                     "playing" in its note), stop; then Play once all are loaded
//                  r  reloading after a part's change: the first part to MS4 (Play, stop) and back to the library (as
//                     the Mixer does it), Play right after, then l's rounds while its patches load
//                  g  a change while playing that needs patches not loaded (the first part to MS4 and back): playback
//                     stops, the patches load in the background, playback goes on once they are (GoOnPlaying)
//                  i  the audio device changed in Preferences › I/O (the boxes set, OK clicked) for each step of
//                     MS_LAG_PROBE_IO ("pa:<host API>/<device>", "alsa:<rate>"), Play after each: the driver's, MScore's,
//                     the synthesizers' and each hosted instance's sample rate, the audio callbacks while playing
//                In l / r / g the window system's input reaching MuseScore is logged ("input"): tools/lagprobe/linux/input.py
//                sends wheel steps with xdotool and gives each one's delay
//    each action its synchronous time (the command, layout, endCmd and what it calls), its response (to the event loop's
//                next idle: the paint included) and its window (to the next action: the largest event-loop lateness
//                and the scopes run in it, the debounced even levels' detect among them)
//    log         JSON lines (tools/lagprobe/read_lag_log.py makes the tables); then MuseScore quits without saving
//                (MS_LAG_PROBE_KEEP=1: stays open). The playback-mode preference is put back as it was.
//
//  Nothing is played or drawn differently while the probe is off; with it on, only what the script does.
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __LAGPROBESESSION_H__
#define __LAGPROBESESSION_H__

namespace Ms {

class LagProbeSession {
   public:
      static void startIfRequested();     // after the command line's scores are loaded
      };

} // namespace Ms
#endif
