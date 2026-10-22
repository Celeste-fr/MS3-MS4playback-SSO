# desk_run.ps1 (tools/live/realcheck): one MuseScore command line in the desktop session (gui.ps1 runs it), for the
# jobs that hang headless: --verify-playback of a score with SSO's solo strings waited without end over ssh (session 0)
# on build #464 (2026-10-22, 0 % CPU, no instance loaded), and finished in 19 s in the desktop session.
#   desk_run.ps1 -job <job.json>   { exe, args: [..], env: {K: V}, stdout, done, timeoutS }
param([string]$job)
$J = Get-Content $job -Raw | ConvertFrom-Json
foreach ($p in $J.env.PSObject.Properties) { Set-Item -Path ("env:" + $p.Name) -Value $p.Value }
$p = Start-Process -FilePath $J.exe -ArgumentList $J.args -PassThru -WindowStyle Minimized -RedirectStandardOutput $J.stdout -RedirectStandardError ($J.stdout + '.err')
$null = $p.Handle
if (-not $p.WaitForExit([int]$J.timeoutS * 1000)) { Stop-Process -Id $p.Id -Force; $code = 124 } else { $code = $p.ExitCode }
Set-Content -Encoding ascii $J.done "$code"
