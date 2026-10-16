#!/bin/sh
# The heard check end to end from Linux (VERIFY.md › Heard check):
#   tools/playbackverify/heard_check.sh <score> [preset or playbackSettings metaTag] [parts a,b,...]
# 1. the events as playback renders them and the audit's predictions (tst_playbackaudit auditScore, MS_AUDIT_DUMP)
# 2. one kthost event file per route (heard_check.py events)
# 3. copied to the Windows VM, rendered there through Kontakt + the library (heard_check_vm.cmd), fetched
# 4. the renders deleted on the VM (a guarded literal path)
# 5. measured and judged (heard_check.py analyse): <work>/report.txt, report.json; exit 1 on a FAIL
# Environment:
#   HC_RUN    a wrapper that runs a command string in the build dir (e.g. ~/MuseScore/ms3fork/run-<name>.sh); else
#   HC_BUILD  the build dir itself (default: build.dir next to this repo's tools)
#   HC_VM     ssh target (default user@192.168.1.45), HC_KEY its key (default ~/.ssh/id_ed25519_winvm)
#   HC_WORK   local work folder (default ~/.cache/heardcheck/<run>); HC_KEEP=1 keeps the renders
#   HC_VMDIR  the VM's base folder (default heardcheck: C:\claude\heardcheck; a name of letters, digits, _ and -, or
#             two such names with a backslash: a session's own folder, e.g. slurshift\\hc)
# The VM side writes only C:\claude\<HC_VMDIR>\<run>. Never commit a report of the owner's score.
set -u
[ $# -ge 1 ] || { sed -n '2,15p' "$0"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCORE=$(readlink -f "$1")
PRESET=${2:-recommended}
PARTS=${3:-}
[ -f "$SCORE" ] || { echo "no score $SCORE"; exit 2; }
VM=${HC_VM:-user@192.168.1.45}
KEY=${HC_KEY:-$HOME/.ssh/id_ed25519_winvm}
NAME=hc-$(date +%Y%m%d-%H%M%S)-$$
case "$NAME" in hc-[0-9]*-[0-9]*-[0-9]*) ;; *) echo "bad run name '$NAME'"; exit 2;; esac
WORK=${HC_WORK:-$HOME/.cache/heardcheck/$NAME}
mkdir -p "$WORK/dump" "$WORK/ev" "$WORK/wav"
VMDIR=${HC_VMDIR:-heardcheck}
case "$VMDIR" in ''|*[!A-Za-z0-9_\\-]*|\\*|*\\) echo "bad HC_VMDIR '$VMDIR'"; exit 2;; esac
VMB="C:\\claude\\$VMDIR"
VMS="C:/claude/$(printf %s "$VMDIR" | tr '\\' /)"
SSH="ssh -o BatchMode=yes -i $KEY $VM"
SCP="scp -q -o BatchMode=yes -i $KEY"
T0=$(date +%s)

echo "== 1. dump ($PRESET) -> $WORK/dump"
# (a script file: the wrappers pass their command through two levels of quoting)
case "$WORK" in *[!A-Za-z0-9/._-]*) echo "HC_WORK must have no spaces or quotes: $WORK"; exit 2;; esac
cat > "$WORK/dump.sh" <<EOF
cd mtest/libmscore/playbackaudit || exit 2
MS_AUDIT_SCORE='$SCORE' MS_AUDIT_SETTINGS='$PRESET' MS_AUDIT_DUMP='$WORK/dump' MS_AUDIT_OUT='$WORK/audit.txt' \\
  QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore > '$WORK/dump.log' 2>&1
EOF
if [ -n "${HC_RUN:-}" ]; then "$HC_RUN" "sh $WORK/dump.sh"; else (cd "${HC_BUILD:-$ROOT/build.dir}" && sh "$WORK/dump.sh"); fi
[ -f "$WORK/dump/predicted.tsv" ] || { echo "no dump (see $WORK/dump.log)"; exit 2; }

echo "== 2. events"
python3 "$HERE/heard_check.py" events "$WORK/dump" "$WORK/ev" ${PARTS:+--parts "$PARTS"} > "$WORK/events.txt" || exit 2
N=$(grep -c . "$WORK/ev/list.txt")
echo "$N routes"
sed 's/$/\r/' "$HERE/heard_check_vm.cmd" > "$WORK/ev/heard_check_vm.cmd"

printf "== 3. render on the VM (%s)\n" "$VMS/$NAME"
$SSH "if not exist $VMB mkdir $VMB" || exit 2
$SCP -r "$WORK/ev" "$VM:$VMS/$NAME" || exit 2
T1=$(date +%s)
$SSH "$VMB\\$NAME\\heard_check_vm.cmd $NAME $VMB" | tail -1
T2=$(date +%s)
$SCP "$VM:$VMS/$NAME/wav/*.wav" "$WORK/wav/" || echo "(some renders missing)"
$SCP "$VM:$VMS/$NAME/wav/*.log" "$WORK/wav/" 2>/dev/null
$SCP "$VM:$VMS/$NAME/run.out" "$WORK/run.out" 2>/dev/null

echo "== 4. delete the VM's renders"
# (a literal path: the run name checked above, never empty)
$SSH "dir /b $VMB\\$NAME\\wav | find /c \".wav\" & if exist $VMB\\$NAME\\list.txt rmdir /s /q $VMB\\$NAME"

echo "== 5. analyse"
python3 "$HERE/heard_check.py" analyse "$WORK/dump" "$WORK/wav" --report "$WORK/report.txt" --json "$WORK/report.json" \
  ${PARTS:+--parts "$PARTS"} --title "Heard check of $(basename "$SCORE") ($PRESET; $N routes; VM render $((T2 - T1)) s)" > /dev/null
RC=$?
cat "$WORK/report.txt" | sed -n '1,40p'
echo "(full report: $WORK/report.txt, $WORK/report.json; total $(( $(date +%s) - T0 )) s)"
[ "${HC_KEEP:-0}" = 1 ] || rm -f "$WORK"/wav/*.wav
exit $RC
