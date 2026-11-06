# gui.ps1: the real-Live check's runner in the desktop session (scheduled task claude-rc-gui, "run only when the user
# is logged on"): runs the command line in C:\claude\rc\gui-job.txt (vm_run.ps1 writes it: live_drive.ps1 and its
# arguments), its output to C:\claude\rc\gui.out.
$ErrorActionPreference = 'Continue'
$cmd = (Get-Content C:\claude\rc\gui-job.txt -Raw).Trim()
"start $(Get-Date -Format o) $cmd" | Set-Content -Encoding utf8 C:\claude\rc\gui.out
try { Invoke-Expression "& $cmd" *>&1 | Out-File -Append -Encoding utf8 C:\claude\rc\gui.out } catch { "ERROR $_" | Add-Content -Encoding utf8 C:\claude\rc\gui.out }
"finished $(Get-Date -Format o)" | Add-Content -Encoding utf8 C:\claude\rc\gui.out
