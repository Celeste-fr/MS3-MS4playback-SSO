#!/bin/sh
# the lag probe on Linux: GUI under Xvfb + PulseAudio null sink, the test synth as SSO.
# Run inside lp-ninja.sh (cwd = build.rel): ../.lagprobe/run-linux.sh <score> <log> [env assignments...]
S="$1"; LOG="$2"; shift 2
H=$(cd .. && pwd)/.lagprobe/home
export HOME=$H XDG_RUNTIME_DIR=$H/xdg
mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR
Xvfb :57 -screen 0 1600x1000x24 >/dev/null 2>&1 &
XP=$!
pulseaudio -n --load=module-null-sink --load=module-native-protocol-unix --exit-idle-time=-1 --daemonize=no >/dev/null 2>&1 &
PP=$!
sleep 2
env DISPLAY=:57 MS_LAG_PROBE="$LOG" "$@" timeout 3600 ./main/mscore "$S" > "$LOG.stdout" 2>&1
echo "mscore exit $?"
kill $XP $PP 2>/dev/null
