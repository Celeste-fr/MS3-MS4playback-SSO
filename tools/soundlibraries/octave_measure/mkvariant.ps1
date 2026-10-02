param([string]$Name, [string]$Shares, [string]$Intervals)
$src = 'C:\claude\MuseScore-soundlibrary-win64-f0bef96'
$root = "C:\claude\oct\$Name"
if (Test-Path $root) { Remove-Item $root -Recurse -Force }
New-Item -ItemType Directory "$root\bin" | Out-Null
Get-ChildItem "$src\bin" -Recurse | ForEach-Object {
  $rel = $_.FullName.Substring("$src\bin".Length)
  if ($_.PSIsContainer) { New-Item -ItemType Directory "$root\bin$rel" -Force | Out-Null }
  elseif ($_.Name -ne 'MuseScore3Evo.exe') { cmd /c mklink /H "$root\bin$rel" "$($_.FullName)" | Out-Null }
}
Get-ChildItem $src -Directory | Where-Object Name -ne 'bin' | ForEach-Object { cmd /c mklink /J "$root\$($_.Name)" "$($_.FullName)" | Out-Null }
Get-ChildItem $src -File | ForEach-Object { cmd /c mklink /H "$root\$($_.Name)" "$($_.FullName)" | Out-Null }
$b = [IO.File]::ReadAllBytes("$src\bin\MuseScore3Evo.exe")
function Find($b, $pat) {
  $hits = @()
  $n = $pat.Length
  for ($i = 0; $i -le $b.Length - $n; $i++) {
    if ($b[$i] -eq $pat[0]) {
      $ok = $true
      for ($j = 1; $j -lt $n; $j++) { if ($b[$i + $j] -ne $pat[$j]) { $ok = $false; break } }
      if ($ok) { $hits += $i }
    }
  }
  return $hits
}
$old = @(0.1, 0.3, 0.5, 0.7, 0.9) | ForEach-Object { [BitConverter]::GetBytes([double]$_) } | ForEach-Object { $_ }
$h = Find $b ([byte[]]$old)
"shares hits: $($h -join ',')"
$oi = @(-12, -7, -5, -3, -2, -1, 1, 2, 3, 5, 7, 12) | ForEach-Object { [BitConverter]::GetBytes([int]$_) } | ForEach-Object { $_ }
$hi = Find $b ([byte[]]$oi)
"interval hits: $($hi -join ',')"
if ($h.Count -ne 1 -or $hi.Count -ne 1) { throw "pattern not unique" }
$ns = $Shares.Split(',') | ForEach-Object { [BitConverter]::GetBytes([double]$_) } | ForEach-Object { $_ }
for ($k = 0; $k -lt 40; $k++) { $b[$h[0] + $k] = $ns[$k] }
$iv = $Intervals.Split(',')
$ni = @()
for ($k = 0; $k -lt 12; $k++) { $v = if ($k -lt $iv.Count) { [int]$iv[$k] } else { 99 }; $ni += [BitConverter]::GetBytes($v) }
for ($k = 0; $k -lt 48; $k++) { $b[$hi[0] + $k] = $ni[$k] }
[IO.File]::WriteAllBytes("$root\bin\MuseScore3Evo.exe", $b)
"written $root\bin\MuseScore3Evo.exe"
