#!/bin/sh
# Even levels off against on in the real sound (even_need.py's header; the owner, 2026-10-16):
#   tools/playbackverify/even_need.sh <score with even values> <takes> [parts a,b,...]
# 1. two dumps (tst_playbackaudit auditScore, MS_AUDIT_DUMP): Recommended with [levels] evenLevels 0 and 1
# 2. kthost event files of the Performance legato routes only (heard_check.py events, list.txt filtered)
# 3. one VM batch: <takes> renders of every route for each condition (heard_check_vm.cmd per take: kthost + Kontakt +
#    the library), fetched, the VM's folders deleted (guarded literal paths)
# 4. even_need.py: steps in fast slurs per section -> <work>/even_need.json
# An EN_WORK of an earlier run is reused: its dumps, event files and fetched takes. Environment: HC_RUN (a wrapper running a command in the build dir; required), HC_VM, HC_KEY, EN_WORK (default
# ~/.cache/evenneed/<run>), EN_SETTINGS (the Recommended keys as a playbackSettings metaTag, evenLevels left out: it is
# appended). The VM side writes only C:\claude\evenneed\<run>-<cond><take>. Never commit the owner's score's results.
set -u
[ $# -ge 2 ] || { sed -n '2,13p' "$0"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
SCORE=$(readlink -f "$1")
TAKES=$2
PARTS=${3:-}
ROOT=$(cd "$HERE/../.." && pwd)
. "$ROOT/tools/vm/vm.sh"
VM=${HC_VM:-$MS_VM}
KEY=${HC_KEY:-$MS_VM_KEY}
[ -n "$VM" ] || { echo "no VM address: set HC_VM or MS_VM, or write ~/.config/ms3fork/vm.conf (tools/vm/vm.conf.example)"; exit 2; }
REC=${EN_SETTINGS:-heldNotes/early=100;heldNotes/byPitch=0;levels/calibrated=2;slurs/quick=2;slurs/quickLevel=1;slurs/performance=1;slurs/transitions=1;slurs/smoothStart=1;slurs/transitionsEarly=1;levels/context=1;levels/sectionBalance=2;levels/realRange=2;levels/micCompensation=1}
NAME=en-$(date +%Y%m%d-%H%M%S)-$$
case "$NAME" in en-[0-9]*-[0-9]*-[0-9]*) ;; *) echo "bad run name '$NAME'"; exit 2;; esac
WORK=${EN_WORK:-$HOME/.cache/evenneed/$NAME}
case "$WORK" in *[!A-Za-z0-9/._-]*) echo "EN_WORK must have no spaces or quotes: $WORK"; exit 2;; esac
SSH="ssh -o BatchMode=yes -i $KEY $VM"
SCP="scp -q -o BatchMode=yes -i $KEY"
mkdir -p "$WORK"
for C in off on; do
  V=0; [ $C = on ] && V=1
  mkdir -p "$WORK/$C/dump" "$WORK/$C/ev"
  [ -f "$WORK/$C/ev/list.txt" ] && continue          # (EN_WORK of an earlier run: its dumps and event files reused)
  cat > "$WORK/$C/dump.sh" <<EOF
cd mtest/libmscore/playbackaudit || exit 2
MS_AUDIT_SCORE='$SCORE' MS_AUDIT_SETTINGS='$REC;levels/evenLevels=$V' MS_AUDIT_DUMP='$WORK/$C/dump' MS_AUDIT_OUT='$WORK/$C/audit.txt' \\
  QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore > '$WORK/$C/dump.log' 2>&1
EOF
  "$HC_RUN" "sh $WORK/$C/dump.sh"
  [ -f "$WORK/$C/dump/predicted.tsv" ] || { echo "no dump $C (see $WORK/$C/dump.log)"; exit 2; }
  python3 "$HERE/heard_check.py" events "$WORK/$C/dump" "$WORK/$C/ev" ${PARTS:+--parts "$PARTS"} > /dev/null || exit 2
  grep -- "- Performance|" "$WORK/$C/ev/list.txt" > "$WORK/$C/ev/l" ; mv "$WORK/$C/ev/l" "$WORK/$C/ev/list.txt"
  for f in "$WORK/$C/ev/"*.txt; do r=$(basename "$f" .txt); [ "$r" = list ] || grep -q "^$r|" "$WORK/$C/ev/list.txt" || rm -f "$f"; done
  sed 's/$/\r/' "$HERE/heard_check_vm.cmd" | sed 's/^set "HC=C:\\claude\\heardcheck"/set "HC=C:\\claude\\evenneed"/' > "$WORK/$C/ev/heard_check_vm.cmd"
done
echo "$(grep -c . "$WORK/off/ev/list.txt") routes; CC11 events off / on: $(cat "$WORK/off/dump/"*museScore.txt | grep -c ' cc 11 ') / $(cat "$WORK/on/dump/"*museScore.txt | grep -c ' cc 11 ')"
# the VM batch: every take of both conditions in one ssh session
B="$WORK/batch.cmd"
: > "$B"
T=1
while [ $T -le "$TAKES" ]; do
  for C in off on; do
    R=$NAME-$C$T
    [ -d "$WORK/$C/wav$T" ] && continue                 # (a take an earlier run fetched)
    $SCP -r "$WORK/$C/ev" "$VM:C:/claude/evenneed/$R" || exit 2
    printf 'call C:\\claude\\evenneed\\%s\\heard_check_vm.cmd %s\r\n' "$R" "$R" >> "$B"
  done
  T=$((T + 1))
done
$SCP "$B" "$VM:C:/claude/evenneed/$NAME-batch.cmd" || exit 2
$SSH "C:\\claude\\evenneed\\$NAME-batch.cmd" | grep -c "RENDERS DONE"
T=1
while [ $T -le "$TAKES" ]; do
  for C in off on; do
    R=$NAME-$C$T
    grep -q "heard_check_vm.cmd $R" "$B" || continue
    mkdir -p "$WORK/$C/wav$T.part"
    # (the VM's folder deleted only once every route's render arrived: a failed copy keeps it for a second try)
    if $SCP "$VM:C:/claude/evenneed/$R/wav/*" "$WORK/$C/wav$T.part/" \
       && [ "$(ls "$WORK/$C/wav$T.part/"*.wav | wc -l)" -eq "$(grep -c . "$WORK/$C/ev/list.txt")" ]; then
      mv "$WORK/$C/wav$T.part" "$WORK/$C/wav$T"
      $SSH "if exist C:\\claude\\evenneed\\$R\\list.txt rmdir /s /q C:\\claude\\evenneed\\$R"
    else
      echo "(renders of $R incomplete: kept on the VM in C:/claude/evenneed/$R)"; exit 1
    fi
  done
  T=$((T + 1))
done
$SSH "if exist C:\\claude\\evenneed\\$NAME-batch.cmd del C:\\claude\\evenneed\\$NAME-batch.cmd"
A=""
T=1
while [ $T -le "$TAKES" ]; do A="$A --take off=$WORK/off/wav$T --take on=$WORK/on/wav$T"; T=$((T + 1)); done
python3 "$HERE/even_need.py" "$WORK/off/dump" "$WORK/even_need.json" --dump-on "$WORK/on/dump" $A ${PARTS:+--parts "$PARTS"}
echo "(work: $WORK)"
