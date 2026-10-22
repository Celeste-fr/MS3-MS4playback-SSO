#!/bin/sh
# The heard check end to end from Linux (VERIFY.md › Heard check):
#   tools/playbackverify/heard_check.sh <score> [preset or playbackSettings metaTag] [parts a,b,...]
# 1. the events as playback renders them and the audit's predictions (tst_playbackaudit auditScore, MS_AUDIT_DUMP)
# 2. HC_RENDER=kthost: one kthost event file per route (heard_check.py events); HC_RENDER=export: a copy of the score with
#    the dump's settings as its playbackSettings metaTag (heard_check.py score; a MusicXML score imported by the VM's
#    MuseScore first, and the dump made from that copy too)
# 3. copied to the Windows VM and rendered there through Kontakt + the library, fetched: kthost (heard_check_vm.cmd: each
#    route alone, MuseScore's setup of its patch) or MuseScore's own export (heard_export_vm.cmd: --verify-playback
#    --verify-wav with MS_VERIFY_ROUTES=1, each route alone as File › Export audio renders it: tuning slots, the gain stage,
#    flat lanes' plug-in parameters)
# 4. the renders deleted on the VM (a guarded literal path)
# 5. measured and judged (heard_check.py analyse [--export]): <work>/report.txt, report.json; exit 1 on a FAIL
# Environment:
#   HC_RENDER export (default) or kthost
#   HC_SPLIT  (export) parts (default): one MuseScore run per library part, the other parts on MS4's sounds (each run loads
#             only its part's instances; a run that crashes loses one part, 2026-10-16: --verify-wav of the whole passages
#             crashed after 24 of 34 routes); whole: one run of the whole score
#   HC_EXE    (export) the VM's MuseScore3Evo.exe, build 443 or later (default C:\claude\heardcheck2\b443\bin\MuseScore3Evo.exe)
#   HC_RUN    a wrapper that runs a command string in the build dir (e.g. ~/MuseScore/ms3fork/run-<name>.sh); else
#   HC_BUILD  the build dir itself (default: build.dir next to this repo's tools)
#   HC_VM     ssh target (default user@192.168.1.45), HC_KEY its key (default ~/.ssh/id_ed25519_winvm)
#   HC_VMDIR  the VM's folder under C:\claude (default heardcheck)
#   HC_WORK   local work folder (default ~/.cache/heardcheck/<run>); HC_KEEP=1 keeps the renders
# The VM side writes only C:\claude\<HC_VMDIR>\<run>. Never commit a report of the owner's score.
set -u
[ $# -ge 1 ] || { sed -n '2,26p' "$0"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
SCORE=$(readlink -f "$1")
PRESET=${2:-recommended}
PARTS=${3:-}
[ -f "$SCORE" ] || { echo "no score $SCORE"; exit 2; }
RENDER=${HC_RENDER:-export}
case "$RENDER" in export|kthost) ;; *) echo "HC_RENDER must be export or kthost"; exit 2;; esac
SPLIT=${HC_SPLIT:-parts}
case "$SPLIT" in parts|whole) ;; *) echo "HC_SPLIT must be parts or whole"; exit 2;; esac
EXE=${HC_EXE:-C:\\claude\\heardcheck2\\b443\\bin\\MuseScore3Evo.exe}
VM=${HC_VM:-user@192.168.1.45}
KEY=${HC_KEY:-$HOME/.ssh/id_ed25519_winvm}
VMDIR=${HC_VMDIR:-heardcheck}
case "$VMDIR" in *[!A-Za-z0-9_-]*|"") echo "bad HC_VMDIR '$VMDIR'"; exit 2;; esac
NAME=hc-$(date +%Y%m%d-%H%M%S)-$$
case "$NAME" in hc-[0-9]*-[0-9]*-[0-9]*) ;; *) echo "bad run name '$NAME'"; exit 2;; esac
WORK=${HC_WORK:-$HOME/.cache/heardcheck/$NAME}
case "$WORK" in *[!A-Za-z0-9/._-]*) echo "HC_WORK must have no spaces or quotes: $WORK"; exit 2;; esac
mkdir -p "$WORK/dump" "$WORK/ev" "$WORK/wav"
SSH="ssh -o BatchMode=yes -i $KEY $VM"
SCP="scp -q -o BatchMode=yes -i $KEY"
VMRUN="C:\\claude\\$VMDIR\\$NAME"
VMRUN_SCP="C:/claude/$VMDIR/$NAME"
T0=$(date +%s)
# the free memory both renders count (MS_FREE_MEMORY_MB, branch voice-determinism 2026-10-21): pinned, so that the
# dump and the VM's export gave the notes the same copies whatever either machine had free (the test VM on dynamic
# memory: 1.9 GB free when idle, Violins 1's line on 1, 2 or 4 copies between runs). Since branch all-copies (the owner,
# 2026-10-21: every copy the music needs, a warning when memory is short) the memory no longer decides the copies: the
# pin only sets the memory warning's figure, and is needed only against builds before all-copies. 16384: the VM's
# installed memory
MEM=${HC_FREE_MEMORY_MB:-16384}
case "$MEM" in *[!0-9]*|"") echo "HC_FREE_MEMORY_MB must be a number of MB"; exit 2;; esac
$SSH "if not exist C:\\claude\\$VMDIR mkdir C:\\claude\\$VMDIR" || exit 2

if [ "$RENDER" = export ]; then
  echo "== 0. the export's copy of the score ($PRESET)"
  SRC=$SCORE
  case "$SCORE" in
    *.musicxml|*.xml|*.mxl)
      # (imported by the VM's MuseScore, so the dump and the export read the same score)
      $SSH "mkdir $VMRUN" || exit 2
      $SCP "$SCORE" "$VM:$VMRUN_SCP/in.musicxml" || exit 2
      sed "s/^set MS_FREE_MEMORY_MB=16384/set MS_FREE_MEMORY_MB=$MEM/; s/\$/\r/" "$HERE/heard_export_vm.cmd" > "$WORK/heard_export_vm.cmd"
      $SCP "$WORK/heard_export_vm.cmd" "$VM:$VMRUN_SCP/heard_export_vm.cmd" || exit 2
      $SSH "$VMRUN\\heard_export_vm.cmd $VMRUN $EXE convert" | tail -1
      $SCP "$VM:$VMRUN_SCP/conv.mscz" "$WORK/conv.mscz" || { echo "the VM's import failed"; exit 2; }
      SRC=$WORK/conv.mscz;;
  esac
  python3 "$HERE/heard_check.py" score "$SRC" "$WORK/score.mscz" "$PRESET" > "$WORK/settings.txt" || exit 2
  # (the musicxml path left the VM's folder with in.musicxml; the export's run uses it again)
  DUMPSCORE=$WORK/score.mscz
else
  DUMPSCORE=$SCORE
fi

echo "== 1. dump ($PRESET) -> $WORK/dump"
# (a script file: the wrappers pass their command through two levels of quoting)
cat > "$WORK/dump.sh" <<EOF
cd mtest/libmscore/playbackaudit || exit 2
MS_FREE_MEMORY_MB=$MEM MS_AUDIT_SCORE='$DUMPSCORE' MS_AUDIT_SETTINGS='$PRESET' MS_AUDIT_DUMP='$WORK/dump' MS_AUDIT_OUT='$WORK/audit.txt' \\
  QT_QPA_PLATFORM=offscreen ./tst_playbackaudit auditScore > '$WORK/dump.log' 2>&1
EOF
if [ -n "${HC_RUN:-}" ]; then "$HC_RUN" "sh $WORK/dump.sh"; else (cd "${HC_BUILD:-$ROOT/build.dir}" && sh "$WORK/dump.sh"); fi
[ -f "$WORK/dump/predicted.tsv" ] || { echo "no dump (see $WORK/dump.log)"; exit 2; }

echo "== 2. events"
python3 "$HERE/heard_check.py" events "$WORK/dump" "$WORK/ev" ${PARTS:+--parts "$PARTS"} > "$WORK/events.txt" || exit 2
N=$(grep -c . "$WORK/ev/list.txt")
echo "$N routes"

printf "== 3. render on the VM (%s, %s)\n" "$RENDER" "$VMRUN_SCP"
if [ "$RENDER" = export ]; then
  $SSH "if not exist $VMRUN mkdir $VMRUN" || exit 2
  sed "s/^set MS_FREE_MEMORY_MB=16384/set MS_FREE_MEMORY_MB=$MEM/; s/\$/\r/" "$HERE/heard_export_vm.cmd" > "$WORK/heard_export_vm.cmd"
  if [ "$SPLIT" = parts ]; then
    # (one copy per library part; the parts asked for only)
    mkdir -p "$WORK/split"
    I=0
    python3 "$HERE/heard_check.py" parts "$WORK/score.mscz" "$WORK/dump" > "$WORK/parts.txt" || exit 2
    while IFS= read -r P; do
      I=$((I + 1))
      if [ -n "$PARTS" ]; then case ",$PARTS," in *",$P,"*) ;; *) continue;; esac; fi
      python3 "$HERE/heard_check.py" score "$SRC" "$WORK/split/score-$I.mscz" "$PRESET" --part "$P" > /dev/null || exit 2
    done < "$WORK/parts.txt"
    $SCP "$WORK/split/"score-*.mscz "$WORK/heard_export_vm.cmd" "$VM:$VMRUN_SCP/" || exit 2
  else
    $SCP "$WORK/score.mscz" "$WORK/heard_export_vm.cmd" "$VM:$VMRUN_SCP/" || exit 2
  fi
  T1=$(date +%s)
  $SSH "$VMRUN\\heard_export_vm.cmd $VMRUN $EXE" | tail -1
  T2=$(date +%s)
  $SCP -r "$VM:$VMRUN_SCP/out" "$WORK/wav/" || echo "(the export's folder not fetched)"
  $SCP "$VM:$VMRUN_SCP/run.out" "$WORK/" 2>/dev/null
  WAVS=$WORK/wav/out
  [ -n "$(find "$WAVS" -name '* events.tsv' 2>/dev/null)" ] || { echo "no export (see $WORK/run.out, $WAVS; VM folder kept: $VMRUN_SCP)"; exit 2; }
  echo "$(find "$WAVS" -name '* route *.wav' | wc -l) route renders; $(grep -c exit "$WORK/run.out" 2>/dev/null) runs:" \
    "$(grep exit "$WORK/run.out" | tr '\r\n' '  ')"
  # (no route render: the export ran but measured nothing, a failure, not an all-zero pass: 2026-10-16 .. 18 every
  # split copy rendered no route, crash-fixes 7f51a91979, and the report said 0 fail of 0)
  if [ "$(find "$WAVS" -name '* route *.wav' | wc -l)" -eq 0 ]; then
    echo "HEARD CHECK FAILED: the export wrote no route WAV, nothing to measure (MS_VERIFY_ROUTES pass skipped or failed;"
    echo "  see $WORK/run.out and each part's summary.txt / stdout.txt under $WAVS; VM folder kept: $VMRUN_SCP)"
    exit 2
  fi
  DEL="if exist $VMRUN\\heard_export_vm.cmd rmdir /s /q $VMRUN"
else
  sed 's/$/\r/' "$HERE/heard_check_vm.cmd" | sed "s/^set \"HC=C:\\\\claude\\\\heardcheck\"/set \"HC=C:\\\\claude\\\\$VMDIR\"/" > "$WORK/ev/heard_check_vm.cmd"
  $SCP -r "$WORK/ev" "$VM:$VMRUN_SCP" || exit 2
  T1=$(date +%s)
  $SSH "$VMRUN\\heard_check_vm.cmd $NAME" | tail -1
  T2=$(date +%s)
  $SCP "$VM:$VMRUN_SCP/wav/*.wav" "$WORK/wav/" || echo "(some renders missing)"
  $SCP "$VM:$VMRUN_SCP/wav/*.log" "$WORK/wav/" 2>/dev/null
  $SCP "$VM:$VMRUN_SCP/run.out" "$WORK/run.out" 2>/dev/null
  WAVS=$WORK/wav
  DEL="dir /b $VMRUN\\wav | find /c \".wav\" & if exist $VMRUN\\list.txt rmdir /s /q $VMRUN"
fi

echo "== 4. delete the VM's renders"
# (a literal path: the run name checked above, never empty; only a folder holding this run's own input file)
$SSH "$DEL"

echo "== 5. analyse"
EXP=""; [ "$RENDER" = export ] && EXP=--export
python3 "$HERE/heard_check.py" analyse "$WORK/dump" "$WAVS" $EXP --report "$WORK/report.txt" --json "$WORK/report.json" \
  ${PARTS:+--parts "$PARTS"} --title "Heard check of $(basename "$SCORE") ($PRESET; $RENDER; $N routes; VM render $((T2 - T1)) s)" > /dev/null
RC=$?
sed -n '1,40p' "$WORK/report.txt"
echo "(full report: $WORK/report.txt, $WORK/report.json; total $(( $(date +%s) - T0 )) s)"
[ "${HC_KEEP:-0}" = 1 ] || find "$WORK/wav" -name '*.wav' -delete
exit $RC
