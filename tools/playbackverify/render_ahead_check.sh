#!/bin/sh
# Render ahead's null and stall tests on Linux (render-ahead, 2026-10-31; docs/PLAYBACK_SETTINGS.md › Render ahead):
# MuseScore under Xvfb (offscreen aborts: no OpenGL context) on the null audio driver (`-a null`: the same 512-frame blocks every run), the test
# synth as the library (tools/lagprobe/linux/mkenv.py), the score played once from its start per run (MS_PLAY_CAPTURE),
# what was heard compared with render_ahead_null.py.
#   usage: render_ahead_check.sh <build dir> <score.mscz> <out dir> [runs...]
#   a run: <name>:<renderAhead ms>[:<stall s>:<stall ms>]   (default: the set below)
# RA_DRIVER=portaudio (default null): a real play through PortAudio into a PulseAudio null sink (started here), with
# MS_AUDIO_TIMING=<out dir>/<run>.timing (its line per play: callbacks against their budget).
# MSCORE=<binary> (default <build dir>/main/mscore): a build dir's binary runs only through tools/roundtrip/portable_fork.sh
# (MuseScore finds its share folder next to it). Extra environment (MS_VST3_HOST_PROCESSES=0 …) passes through. Every run's .txt has its late-audio line.
B=$(cd "$1" && pwd); S="$2"; O="$3"; shift 3
M=${MSCORE:-$B/main/mscore}
R=$(cd "$(dirname "$0")/../.." && pwd)
mkdir -p "$O"
H="$O/home"
python3 "$R/tools/lagprobe/linux/mkenv.py" "$R" "$H" "$B" >/dev/null
mkdir -p "$H/xdg" && chmod 700 "$H/xdg"
[ $# -gt 0 ] || set -- ref0:0 ra500:500 ref0b:0 ra500b:500 ra50:50 stall300:500:3:300 stall900:500:3:900 direct300:0:3:300
D=${RA_DISPLAY:-:58}
Xvfb $D -screen 0 1600x1000x24 >/dev/null 2>&1 &
XP=$!
DRV=${RA_DRIVER:-null}
PP=""
if [ "$DRV" != null ]; then
      printf 'pcm.!default { type pulse }\nctl.!default { type pulse }\n' > "$H/.asoundrc"   # (PortAudio's ALSA into PulseAudio)
      env HOME="$H" XDG_RUNTIME_DIR="$H/xdg" DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent pulseaudio -n --load=module-null-sink --load=module-native-protocol-unix \
          --exit-idle-time=-1 --daemonize=no >/dev/null 2>&1 &
      PP=$!
      fi
sleep 2
for run in "$@"; do
      name=${run%%:*}; rest=${run#*:}
      ms=${rest%%:*}; stall=""
      case "$rest" in *:*) stall=${rest#*:} ;; esac
      rm -f "$H/.local/share/MuseScore/MuseScore3Evo/session"
      env HOME="$H" XDG_RUNTIME_DIR="$H/xdg" DISPLAY=$D MS_RENDER_AHEAD="$ms" \
          MS_PLAY_CAPTURE="$O/$name.wav" ${stall:+MS_AUDIO_STALL=$stall} \
          $( [ "$DRV" != null ] && echo MS_AUDIO_TIMING="$O/$name.timing" ) timeout 900 "$M" -a "$DRV" "$S" > "$O/$name.log" 2>&1
      echo "$name (renderAhead $ms${stall:+, stall $stall}): exit $? | $(grep -h '^late\|^result' "$O/$name.wav.txt" 2>/dev/null | tr '\n' ' ')"
done
kill $XP $PP 2>/dev/null
