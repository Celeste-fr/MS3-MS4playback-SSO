# lag probe: every 15 s while the probe's MuseScore runs, the other sessions' heavy processes and their CPU
param([string]$out)
$mine = $null
for ($i = 0; $i -lt 120 -and -not $mine; $i++) {
  Start-Sleep -Seconds 1
  $mine = Get-CimInstance Win32_Process -Filter "Name='MuseScore3Evo.exe'" | Where-Object { $_.CommandLine -like '*lagprobe\cfg*' } | Select-Object -First 1
}
if (-not $mine) { "no probe MuseScore found" | Out-File -Append $out; exit }
"mine $($mine.ProcessId)" | Out-File -Append $out
$prev = @{}
while (Get-Process -Id $mine.ProcessId -ErrorAction SilentlyContinue) {
  $t = Get-Date -Format "HH:mm:ss"
  $ps = Get-Process | Where-Object { $_.Name -match 'kthost|ktload|Ableton|MuseScore3Evo|Kontakt|python|ffmpeg' }
  $line = @()
  foreach ($p in $ps) {
    $c = [double]$p.CPU
    $d = if ($prev.ContainsKey($p.Id)) { $c - $prev[$p.Id] } else { 0 }
    $prev[$p.Id] = $c
    $tag = if ($p.Id -eq $mine.ProcessId) { 'MINE' } else { 'other' }
    $line += "$($p.Name):$($p.Id):${tag}:cpu+$([math]::Round($d,1))s:ws$([math]::Round($p.WorkingSet64/1MB))MB"
  }
  $os = Get-CimInstance Win32_OperatingSystem
  "$t free$([math]::Round($os.FreePhysicalMemory/1024))MB $($line -join ' ')" | Out-File -Append $out
  Start-Sleep -Seconds 15
}
"probe exited" | Out-File -Append $out
