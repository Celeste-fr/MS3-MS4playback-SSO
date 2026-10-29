# vm_run.ps1: the real-Live check's VM side, one run folder (tools/live/realcheck/realcheck.py copies it there through
# tools/vm/vmjob.py, which holds C:\claude\quiet.lock for the whole run).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File vm_run.ps1 -run <run folder>
#
# The run folder holds score.mscz (the score with its playbackSettings) and score-ms.mscz (the same, for MuseScore's
# export: Create Live Set and Update Live Set save score.mscz with the set's record, liveTracks, and --verify-playback
# of such a score waited without end on build #464, 2026-10-22: 0 % CPU, no route WAV), variant(-ms).mscz, job.json ({ exe, library, checks: [...],
# tailS, variant: "variant.mscz" or "" }) and these scripts. Steps (each skipped when its "done\<step>" marker exists, so
# a run started again after a crash or a timeout goes on where it stopped):
#   env       the scheduled tasks (claude-rc-gui: this user's desktop; claude-rc-tscon: SYSTEM, puts the desktop on
#             the console), backups of what the run changes and restores at the end: Live's Preferences.cfg, the User
#             Library's MuseScore Link.amxd (the build's copied over it: the set and the device must match), the hash of
#             Ample's AGM_info.xml (must stay unchanged)
#   create    --create-live-set set\RC.als score.mscz (also saves the score: its liveTracks record)
#   ms        MuseScore's own export: --verify-playback --verify-wav with MS_VERIFY_ROUTES=1 (each route alone, as
#             File > Export audio renders it) -> ms\
#   msb       (check "floor") the same export again -> msb\: compare1 nulls each route against it (the floor: on
#             build #464 with SSO, -148 / -150 dB, bit-identical)
#   live1     Live opens set\RC.als, exports every track (live1\), saves the set as saved\RCsaved.als, quits
#   compare1  compare.py live1 against ms; alsset.py diff written -> saved
#   readback  --live-set-readback of the written and the saved set (Kontakt's state as Live saved it)
#   update0   --update-live-set on a copy of Live's saved set with the unchanged score: must write nothing
#   update1   (variant) --update-live-set on another copy with variant.mscz (the score edited), -> upd1\RCupd.als;
#             (liveEdits: the copy edited first as a user would in Live, alsset.py liveedit)
#             ms2: MuseScore's export of the variant; live2: Live opens the updated set, exports, saves as; compare2
#   cleanup   every WAV deleted (unless keepWavs), the backups put back
# Results: results\ (result.json per step, reports, the sets, screenshots); WAVs never leave the VM.
param([string]$run)
$ErrorActionPreference = 'Continue'
Set-Location $run
$J = Get-Content (Join-Path $run 'job.json') -Raw | ConvertFrom-Json
$res = Join-Path $run 'results'; $done = Join-Path $run 'done'
New-Item -ItemType Directory -Force $res, $done | Out-Null
$PY = 'C:\Program Files\Python312\python.exe'
$log = Join-Path $res 'vm_run.log'
# (Write-Host, not the pipeline: a function's output is its return value. And no function may be named as an alias:
# PowerShell runs an alias first, so a function "Compare" ran Compare-Object)
function L($s) { $line = "$(Get-Date -Format 'HH:mm:ss') $s"; Add-Content -Encoding utf8 $log $line; Write-Host $line }
function Done($s) { Test-Path (Join-Path $done $s) }
function Mark($s, $text) { Set-Content -Encoding utf8 (Join-Path $done $s) $text }
function Want($c) { ($J.checks -contains $c) -or ($J.checks -contains 'all') }
function MS([string[]]$a, [string]$outTxt, [int]$timeoutS = 3600) {
  $p = Start-Process -FilePath $J.exe -ArgumentList $a -NoNewWindow -PassThru -RedirectStandardOutput $outTxt -RedirectStandardError ($outTxt + '.err')
  $null = $p.Handle   # (Start-Process: ExitCode stays empty unless the handle was taken)
  if (-not $p.WaitForExit($timeoutS * 1000)) { Stop-Process -Id $p.Id -Force; L "MuseScore timed out: $($a -join ' ')"; return 124 }
  return $p.ExitCode }
function Q([string]$s) { '"' + $s + '"' }
# a MuseScore command line in the desktop session (desk_run.ps1 through the claude-rc-gui task), for what hangs headless
function DeskMS([string[]]$a, [string]$outTxt, [int]$timeoutS, $envs) {
  $jf = $outTxt + '.job.json'; $df = $outTxt + '.done'
  Remove-Item -Force -ErrorAction SilentlyContinue $df
  [ordered]@{ exe = $J.exe; args = $a; env = $envs; stdout = $outTxt; done = $df; timeoutS = $timeoutS } | ConvertTo-Json | Set-Content -Encoding utf8 $jf
  Set-Content -Encoding utf8 'C:\claude\rc\gui-job.txt' ("`"" + (Join-Path $run 'desk_run.ps1') + "`" -job `"$jf`"")
  schtasks /run /tn claude-rc-tscon | Out-Null; Start-Sleep 3
  schtasks /run /tn claude-rc-gui | Out-Null
  for ($i = 0; $i -lt $timeoutS + 120; $i += 3) { Start-Sleep 3; if (Test-Path $df) { return [int](Get-Content $df).Trim() } }
  return 124 }

# ---- env
# (the newest Live's preferences: Live updates itself, 12.4.6 -> 12.4.7 on the VM on 2026-10-22, and each version keeps
# its own folder)
$prefs = Get-ChildItem (Join-Path $env:APPDATA 'Ableton') -Directory -Filter 'Live 12*' -ErrorAction SilentlyContinue |
  ? { Test-Path (Join-Path $_.FullName 'Preferences\Preferences.cfg') } |
  Sort-Object { try { [version]($_.Name -replace '^Live ', '') } catch { [version]'0.0' } } | Select -Last 1 |
  % { Join-Path $_.FullName 'Preferences\Preferences.cfg' }
$liveExe = 'C:\ProgramData\Ableton\Live 12 Trial\Program\Ableton Live 12 Trial.exe'
$liveVersion = if (Test-Path $liveExe) { (Get-Item $liveExe).VersionInfo.ProductVersion } else { '' }
Set-Content -Encoding utf8 (Join-Path $res 'live-version.txt') $liveVersion
$dev = Join-Path $env:USERPROFILE 'Documents\Ableton\User Library\Presets\MIDI Effects\Max MIDI Effect\MuseScore Link.amxd'
$agm = Join-Path $env:USERPROFILE 'Documents\Ample Sound\AGM\Configs\AGM_info.xml'
$bk = Join-Path $run 'backup'
if (-not (Done 'env')) {
  New-Item -ItemType Directory -Force $bk | Out-Null
  if (-not (Test-Path (Join-Path $bk 'started'))) { Set-Content -Encoding ascii (Join-Path $bk 'started') (Get-Date -Format o) }
  if ($prefs -and (Test-Path $prefs)) { Copy-Item $prefs (Join-Path $bk 'Preferences.cfg') -Force; Set-Content -Encoding utf8 (Join-Path $bk 'prefs.path') $prefs }
  if (Test-Path $dev) { Copy-Item $dev (Join-Path $bk 'MuseScore Link.amxd') -Force }
  if (Test-Path $agm) { (Get-FileHash $agm).Hash | Set-Content (Join-Path $bk 'AGM_info.sha256') }
  $bdev = Join-Path (Split-Path $J.exe) 'MuseScore Link.amxd'
  if ((Test-Path $bdev) -and (Test-Path (Split-Path $dev))) { Copy-Item $bdev $dev -Force; L "MuseScore Link.amxd: the build's copied into the User Library (backup kept)" }
  $gui = 'C:\claude\rc'
  New-Item -ItemType Directory -Force $gui | Out-Null
  Copy-Item (Join-Path $run 'gui.ps1'), (Join-Path $run 'tscon.cmd') $gui -Force
  Set-Content -Encoding ascii (Join-Path $gui 'tscon.user') $env:USERNAME
  schtasks /query /tn claude-rc-gui *> $null
  if ($LASTEXITCODE -ne 0) { schtasks /create /f /tn claude-rc-gui /it /sc once /st 00:00 /tr "conhost.exe --headless powershell -NoProfile -ExecutionPolicy Bypass -File C:\claude\rc\gui.ps1" | Out-Null }
  schtasks /query /tn claude-rc-tscon *> $null
  if ($LASTEXITCODE -ne 0) { schtasks /create /f /tn claude-rc-tscon /ru SYSTEM /sc once /st 00:00 /tr C:\claude\rc\tscon.cmd | Out-Null }
  Mark 'env' 'ok'; L "env ready (Live $liveVersion)"
}

function Restore {
  # Live's crash recovery files from this run (a Live the driver had to stop leaves them; the next start would ask to
  # recover that set): the Crash folder's entries made since the run's env step
  if ($prefs -and (Test-Path (Join-Path $bk 'started'))) {
    $since = [datetime](Get-Content (Join-Path $bk 'started')).Trim()
    Get-ChildItem (Join-Path (Split-Path $prefs) 'Crash') -ErrorAction SilentlyContinue | ? { $_.LastWriteTime -ge $since -or $_.CreationTime -ge $since } |
      Remove-Item -Recurse -Force -ErrorAction SilentlyContinue }
  if (Test-Path (Join-Path $bk 'Preferences.cfg')) { Copy-Item (Join-Path $bk 'Preferences.cfg') "$(Get-Content (Join-Path $bk 'prefs.path'))".Trim() -Force }
  if (Test-Path (Join-Path $bk 'MuseScore Link.amxd')) { Copy-Item (Join-Path $bk 'MuseScore Link.amxd') $dev -Force }
  if (Test-Path (Join-Path $bk 'AGM_info.sha256')) {
    $was = (Get-Content (Join-Path $bk 'AGM_info.sha256')).Trim(); $now = (Get-FileHash $agm).Hash
    if ($was -ne $now) { L "AGM_info.xml CHANGED during the run"; Set-Content (Join-Path $res 'AGM_info.changed') "$was -> $now" } } }

# ---- Live through the desktop session
function RunLive([string]$name, [string]$set, [string]$exportDir, [string]$saveAs) {
  if (Done $name) { L "$name done before"; return $true }
  $len = & $PY (Join-Path $run 'alsset.py') length $set $J.tailS | ConvertFrom-Json
  $job = [ordered]@{ set = $set; exportDir = $exportDir; exportBase = 'L'; renderBars = $len.renderBars; saveAs = $saveAs
    loadTimeoutS = $J.loadTimeoutS; exportTimeoutS = $J.exportTimeoutS; settleS = $J.settleS; shots = (Join-Path $res "$name-shots") }
  $jf = Join-Path $run "$name-job.json"; $rf = Join-Path $res "$name.json"
  $job | ConvertTo-Json | Set-Content -Encoding utf8 $jf
  Remove-Item -Force -ErrorAction SilentlyContinue $rf
  Set-Content -Encoding utf8 'C:\claude\rc\gui-job.txt' ("`"" + (Join-Path $run 'live_drive.ps1') + "`" -job `"$jf`" -out `"$rf`"")
  schtasks /run /tn claude-rc-tscon | Out-Null; Start-Sleep 4
  schtasks /run /tn claude-rc-gui | Out-Null
  $limit = $J.loadTimeoutS + $J.exportTimeoutS + 600
  for ($i = 0; $i -lt $limit; $i += 5) {
    Start-Sleep 5
    if (Test-Path $rf) { try { $r = Get-Content $rf -Raw | ConvertFrom-Json; if ($r.verdict -ne 'RUNNING') { break } } catch { } }
    # the desktop runner ended without a verdict (the driver died): no use waiting
    $g = Get-Content 'C:\claude\rc\gui.out' -Raw -ErrorAction SilentlyContinue
    if ($i -gt 10 -and $g -and $g.Contains($jf) -and $g.Contains('finished ')) { Start-Sleep 3; break }
  }
  $r = if (Test-Path $rf) { Get-Content $rf -Raw | ConvertFrom-Json } else { $null }
  if (-not $r -or $r.verdict -eq 'RUNNING') {
    L "${name}: the driver did not finish in $limit s"
    if ($r -and $r.livePid) { Stop-Process -Id $r.livePid -Force -ErrorAction SilentlyContinue }
    return $false }
  L "${name}: $($r.verdict) $($r.failure) (load $($r.loadSeconds) s, export $($r.exportSeconds) s)"
  if ($r.verdict -like 'PASS*') { Mark $name $r.verdict; return $true }
  return $false
}

$score = Join-Path $run 'score.mscz'
$setDir = Join-Path $run 'set'; $set = Join-Path $setDir 'RC.als'
$rcExit = 0
try {
  # ---- create
  if (-not (Done 'create')) {
    New-Item -ItemType Directory -Force $setDir | Out-Null
    $c = DeskMS @('--create-live-set', (Q $set), (Q $score), '--verify-library', (Q $J.library)) (Join-Path $res 'create.txt') 900 @{}
    if ($c -ne 0 -or -not (Test-Path $set)) { L "create: exit $c"; $rcExit = 3; throw 'create' }
    Copy-Item $set (Join-Path $res 'written.als') -Force; Copy-Item "$set.txt" (Join-Path $res 'written.als.txt') -Force
    # the variant from the score as Create Live Set saved it (its liveTracks: the record of what it wrote, which Update
    # Live Set reads; a variant made before lacks it: every lane imported, "no record of the notes")
    if ($J.variant) { & $PY (Join-Path $run 'alsset.py') variant $score (Join-Path $run $J.variant) }
    Mark 'create' 'ok'; L 'create: ok' }
  # ---- MuseScore's export
  function RunExport([string]$name, [string]$sc, [int]$timeoutS = 5400, [bool]$must = $true) {
    if (Done $name) { return }
    $o = Join-Path $run $name
    Remove-Item -Recurse -Force -ErrorAction SilentlyContinue $o   # (an earlier attempt's folder: its WAVs are gone)
    New-Item -ItemType Directory -Force $o | Out-Null
    $c = DeskMS @('--verify-library', (Q $J.library), '--verify-playback', (Q $sc), '--verify-out', (Q $o), '--verify-wav') (Join-Path $res "$name.txt") $timeoutS @{ MS_VERIFY_ROUTES = '1' }
    $f = Get-ChildItem $o -Recurse -Filter report.json | Select -First 1
    if (-not $f -and -not $must) { L "${name}: exit $c, no report (left out)"; Mark $name ''; return }
    if (-not $f) { L "${name}: exit $c, no report"; $script:rcExit = 3; throw $name }
    Copy-Item $f.FullName (Join-Path $res "$name-report.json") -Force
    Get-ChildItem $f.DirectoryName -Filter '* events.tsv' | % { Copy-Item $_.FullName (Join-Path $res "$name-events.tsv") -Force }
    Mark $name $f.DirectoryName; L "${name}: exit $c, $($f.DirectoryName)" }
  function MsDir($name) { "$(Get-Content (Join-Path $done $name))".Trim() }
  RunExport 'ms' (Join-Path $run 'score-ms.mscz')
  if (Want 'floor') { RunExport 'msb' (Join-Path $run 'score-ms.mscz') }   # the same score again: the null floor

  function RunCompare($name, $liveDir, $msName, $written, $savedPattern, $reportOf = '') {
    if (-not $reportOf) { $reportOf = $written }
    if (Done $name) { return }
    $saved = Get-ChildItem (Join-Path $run $savedPattern) -Recurse -Filter '*.als' | ? { $_.DirectoryName -notlike '*Backup*' } | Select -First 1
    $ea = @(); if ($J.edges) { $ea = @('--edges', $J.edges) }
    if ($msName -eq 'ms' -and (Done 'msb')) { $ea += @('--floor', (MsDir 'msb')) }
    & $PY (Join-Path $run 'compare.py') --live $liveDir --base L --set $written --report-of $reportOf --ms (MsDir $msName) --json (Join-Path $res "$name.json") --report (Join-Path $res "$name.txt") @ea | Out-Null
    if ($saved) {
      Copy-Item $saved.FullName (Join-Path $res "$name-saved.als") -Force
      & $PY (Join-Path $run 'alsset.py') diff $written $saved.FullName | Set-Content -Encoding utf8 (Join-Path $res "$name-xml.json") }
    Mark $name 'ok'; L "${name}: compared" }

  if (Want 'live') {
    if (-not (RunLive 'live1' $set (Join-Path $run 'live1') (Join-Path $run 'saved\RCsaved.als'))) { $rcExit = 4; throw 'live1' }
    RunCompare 'compare1' (Join-Path $run 'live1') 'ms' $set 'saved'
  }
  $savedSet = Get-ChildItem (Join-Path $run 'saved') -Recurse -Filter 'RCsaved.als' -ErrorAction SilentlyContinue | ? { $_.DirectoryName -notlike '*Backup*' } | Select -First 1

  if ((Want 'readback') -and -not (Done 'readback')) {
    foreach ($s in @(@('written', $set), @('saved', $(if ($savedSet) { $savedSet.FullName } else { '' })))) {
      if (-not $s[1]) { continue }
      $c = DeskMS @('--live-set-readback', (Q $s[1]), '--verify-library', (Q $J.library)) (Join-Path $res "readback-$($s[0]).txt") 1800 @{}
      if (Test-Path "$($s[1]) readback.txt") { Copy-Item "$($s[1]) readback.txt" (Join-Path $res "readback-$($s[0]).out.txt") -Force }
      L "readback $($s[0]): exit $c" }
    Mark 'readback' 'ok' }

  if ((Want 'update') -and $savedSet -and -not (Done 'update0')) {
    $u = Join-Path $run 'upd0'; New-Item -ItemType Directory -Force $u | Out-Null
    $us = Join-Path $u 'RCsaved.als'; Copy-Item $savedSet.FullName $us -Force
    $h0 = (Get-FileHash $us).Hash
    $c = DeskMS @('--update-live-set', (Q $us), (Q $score), '--verify-library', (Q $J.library)) (Join-Path $res 'update0.txt') 1800 @{}
    $h1 = (Get-FileHash $us).Hash
    if (Test-Path "$us.update.txt") { Copy-Item "$us.update.txt" (Join-Path $res 'update0.update.txt') -Force }
    Set-Content -Encoding utf8 (Join-Path $res 'update0.json') (@{ exit = $c; unchanged = ($h0 -eq $h1); backups = @(Get-ChildItem $u -Recurse -Filter *.als | ? { $_.DirectoryName -like '*Backup*' } | % { $_.Name }) } | ConvertTo-Json)
    Mark 'update0' 'ok'; L "update0: exit $c, set unchanged: $($h0 -eq $h1)" }

  if ((Want 'update') -and $J.variant -and $savedSet) {
    $variant = Join-Path $run $J.variant
    if (-not (Done 'update1')) {
      $u = Join-Path $run 'upd1'; New-Item -ItemType Directory -Force $u | Out-Null
      $us = Join-Path $u 'RCupd.als'; Copy-Item $savedSet.FullName $us -Force
      # (liveEdits) what a user does in Live before the update: a note moved in a technique clip, a configured knob
      # turned (alsset.py liveedit); Update Live Set must take both back into the score and keep them in the set
      if ($J.liveEdits) { & $PY (Join-Path $run 'alsset.py') liveedit $us $us | Set-Content -Encoding utf8 (Join-Path $res 'liveedits.json') }
      $c = DeskMS @('--update-live-set', (Q $us), (Q $variant), '--verify-library', (Q $J.library)) (Join-Path $res 'update1.txt') 1800 @{}
      if (Test-Path "$us.update.txt") { Copy-Item "$us.update.txt" (Join-Path $res 'update1.update.txt') -Force }
      Copy-Item $us (Join-Path $res 'updated.als') -Force
      # the score as Update Live Set left it (it reads the set back first: lanes it imported play in MuseScore too)
      Copy-Item $variant (Join-Path $res 'variant-after-update.mscz') -Force
      Copy-Item $variant (Join-Path $run 'variant-upd-ms.mscz') -Force
      Set-Content -Encoding utf8 (Join-Path $res 'update1.json') (@{ exit = $c; backups = @(Get-ChildItem $u -Recurse -Filter *.als | ? { $_.DirectoryName -like '*Backup*' } | % { $_.Name }) } | ConvertTo-Json)
      Mark 'update1' 'ok'; L "update1: exit $c" }
    RunExport 'ms2' (Join-Path $run 'variant-ms.mscz')
    # MuseScore's export of the score as the update left it (a score with the set's liveTracks record: on build #464
    # --verify-playback of such a score could wait without end headless; here in the desktop session, 15 min at most)
    if (Test-Path (Join-Path $run 'variant-upd-ms.mscz')) { RunExport 'ms2u' (Join-Path $run 'variant-upd-ms.mscz') 900 $false }
    if (Want 'live') {
      $us = Join-Path $run 'upd1\RCupd.als'
      if (-not (RunLive 'live2' $us (Join-Path $run 'live2') (Join-Path $run 'saved2\RCupd2.als'))) { $rcExit = 4; throw 'live2' }
      RunCompare 'compare2' (Join-Path $run 'live2') 'ms2' $us 'saved2' $set
      if ((Done 'ms2u') -and (MsDir 'ms2u')) { RunCompare 'compare2u' (Join-Path $run 'live2') 'ms2u' $us 'saved2u' $set }
    }
  }
} catch {
  L "stopped at $_"
  if ($rcExit -eq 0) { $rcExit = 5 }
} finally {
  # ---- cleanup: WAVs go, backups back (also after a failure: the next start of the run renders them again)
  if (-not $J.keepWavs) { Get-ChildItem $run -Recurse -Filter *.wav -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue }
  else { L 'WAVs kept (keepWavs: delete the run folder by hand when done)' }
  Get-ChildItem $run -Recurse -Filter *.asd -ErrorAction SilentlyContinue | Remove-Item -Force -ErrorAction SilentlyContinue
  Restore
  # the WAVs are gone: a step whose WAVs were not compared yet must render again when the run is started again
  if ($J.keepWavs) { } elseif (-not (Done 'compare1')) { Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $done 'ms'), (Join-Path $done 'msb'), (Join-Path $done 'live1') }
  if (-not (Done 'compare2')) { Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $done 'ms2'), (Join-Path $done 'ms2u'), (Join-Path $done 'live2') }
  Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $done 'env')
  L "end (exit $rcExit)"
}
exit $rcExit
