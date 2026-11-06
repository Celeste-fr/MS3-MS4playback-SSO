#!/bin/sh
# MuseScore closed (Ctrl+Q) while the score's patches load: the time to exit and the exit code, a screenshot
# /tmp/close-test-1.png just after (cwd = build.rel): ../.lagprobe/close-test.sh <score> <seconds before Ctrl+Q> [env...]
# (KILL_AFTER: seconds before it is killed). A modified score asks to save: [levels] evenLevels writes values, so set it
# to 0 in the probe home's playback.ini for this test
S="$1"; WAIT="$2"; shift 2
H=$(cd .. && pwd)/.lagprobe/home
export HOME=$H XDG_RUNTIME_DIR=$H/xdg
rm -f $H/.local/share/MuseScore/MuseScore3Evo/session
Xvfb :58 -screen 0 1600x1000x24 >/dev/null 2>&1 &
XP=$!
pulseaudio -n --load=module-null-sink --load=module-native-protocol-unix --exit-idle-time=-1 --daemonize=no >/dev/null 2>&1 &
PP=$!
sleep 2
env DISPLAY=:58 "$@" timeout 300 ./main/mscore "$S" > /tmp/close-test.out 2>&1 &
MP=$!
sleep $WAIT
grep -c "preloaded" /tmp/close-test.out | sed 's/^/preload steps before the close: /'
T0=$(python3 -c "import time; print(time.time() + 0.3)")   # (the Discard click: Ctrl+Q, 1 s, then the click)
DISPLAY=:58 xdotool mousemove 800 450 click 1 sleep 0.3 key ctrl+q
sleep 1.5
DISPLAY=:58 import -window root /tmp/close-test-1.png
for i in $(seq 1 ${KILL_AFTER:-300}); do kill -0 $MP 2>/dev/null || break; sleep 1; done
kill $MP 2>/dev/null
wait $MP
RC=$?
T1=$(date +%s.%N)
echo "exit code $RC, $(python3 -c "print(round($T1 - $T0, 2))") s after Ctrl+Q"
grep -c "preloaded" /tmp/close-test.out | sed 's/^/preload steps in all: /'
grep -i "segm\|abort\|fatal" /tmp/close-test.out | head -3
kill $XP $PP 2>/dev/null
