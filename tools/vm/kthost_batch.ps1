# kthost_batch.ps1 -run <run folder> [-secs 300] [-tail 3] [-wait 60] [-kthost C:\claude\kt\kthost.exe] [-opts "..."]
# The kthost 3-step's VM step (docs/TOOLS.md › Conventions) for tools/vm/vmjob.py: each "<name>|<patch>|<probe pitch>" row of
# <run>\list.txt rendered from <run>\<name>.txt to <run>\wav\<name>.wav through kthost + Kontakt 8, with the VM MuseScore's
# own resaved setup of the patch (copied read-only into <run>\st: the owner's setups are never written), a watchdog of
# -secs s per render (killed after), a line per render in <run>\run.out. Writes only <run>.
#   e.g. vmjob.py --name accent --fetch wav --kill kthost.exe <work> -- powershell -NoProfile -ExecutionPolicy Bypass -File kthost_batch.ps1 -run {run}
param([string]$run, [int]$secs = 300, [int]$tail = 3, [int]$wait = 60, [string]$kthost = "C:\claude\kt\kthost.exe", [string]$opts = "")
if (-not $run -or -not (Test-Path "$run\list.txt")) { "usage: kthost_batch.ps1 -run <folder with list.txt>"; exit 2 }
$src = "$env:LOCALAPPDATA\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
$kontakt = "C:\Program Files\Common Files\VST3\Kontakt 8.vst3"
New-Item -ItemType Directory -Force "$run\st", "$run\wav" | Out-Null
if (Test-Path C:\claude\share.txt) {
  $pw = Get-Content C:\claude\share.txt -TotalCount 1
  net use \\VOLENS\VMShare /user:VOLENS\vmshare $pw *> $null
}
$fail = 0
foreach ($ln in Get-Content "$run\list.txt") {
  if (-not $ln.Trim()) { continue }
  $r, $patch, $probe = $ln.Split('|')
  if (-not (Test-Path "$run\st\$patch.vst3state")) { Copy-Item "$src\$patch.vst3state" "$run\st\" }
  $t0 = Get-Date
  $args_ = "`"$kontakt`" `"$run\st\$patch.vst3state`" `"$run\$r.txt`" `"$run\wav\$r.wav`" --tail $tail --wait $wait --probe-pitch $probe $opts"
  $p = Start-Process -FilePath $kthost -ArgumentList $args_ -NoNewWindow -PassThru -RedirectStandardOutput "$run\wav\$r.log" -RedirectStandardError "$run\wav\$r.err"
  $h = $p.Handle
  while (-not $p.HasExited -and ((Get-Date) - $t0).TotalSeconds -lt $secs) { Start-Sleep 1 }
  if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; $st = "KILLED (watchdog)"; $fail = 1 } else { $st = "exit $($p.ExitCode)"; if ($p.ExitCode -ne 0) { $fail = 1 } }
  "{0} {1} {2:N1} s" -f $r, $st, ((Get-Date) - $t0).TotalSeconds | Out-File -Append "$run\run.out"
}
if (Test-Path C:\claude\share.txt) { net use \\VOLENS\VMShare /delete /y *> $null }
Get-Content "$run\run.out"
"KT DONE"
exit $fail
