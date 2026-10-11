#!/bin/sh
# fetch each finished render, measure it, delete the local wav (the VM keeps its copy until the end)
P=${PCL_WORK:-.}
K=${VM_KEY:?ssh key of the Windows VM}; VM=${VM_HOST:?user@host of the Windows VM}
while :; do
  for n in $(grep ' done' $P/run.out | cut -d' ' -f1); do
    [ -n "$n" ] || continue
    [ -f "$P/r/$n.lev.json" ] && continue
    scp -q -i $K "$VM:C:/claude/pcl/$n.wav" "$VM:C:/claude/pcl/$n.log" "$P/r/" || continue
    PCL_WORK="$P" python3 "$(dirname "$0")/ana.py" "$n" && rm -f -- "$P/r/$n.wav"
  done
  grep -q 'ALL DONE\|EXIT' $P/run.out && [ "$(ls $P/r/*.lev.json | wc -l)" -ge 20 ] && break
  grep -q 'EXIT' $P/run.out && ! grep -q 'ALL DONE' $P/run.out && { echo RUN FAILED; break; }
  sleep 30
done
echo FETCH DONE
