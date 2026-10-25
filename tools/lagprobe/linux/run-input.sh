#!/bin/sh
# the lag probe with window-system input (phases l / r / g): as run-linux.sh, and input.py sending wheel steps meanwhile
# (cwd = build.rel): ../.lagprobe/run-input.sh <score> <log> [env assignments...]; then input.py analyze <log>.sends <log>
S="$1"; LOG="$2"; shift 2
H=$(cd .. && pwd)/.lagprobe/home
export HOME=$H XDG_RUNTIME_DIR=$H/xdg
mkdir -p $XDG_RUNTIME_DIR && chmod 700 $XDG_RUNTIME_DIR
Xvfb :57 -screen 0 1600x1000x24 >/dev/null 2>&1 &
XP=$!
pulseaudio -n --load=module-null-sink --load=module-native-protocol-unix --exit-idle-time=-1 --daemonize=no >/dev/null 2>&1 &
PP=$!
sleep 2
python3 -I $(cd .. && pwd)/.lagprobe/input.py send :57 "$LOG.sends" --every-ms 100 --seconds 900 &
IP=$!
env DISPLAY=:57 MS_LAG_PROBE="$LOG" "$@" timeout 1200 ./main/mscore "$S" > "$LOG.stdout" 2>&1
echo "mscore exit $?"
kill $IP $XP $PP 2>/dev/null
