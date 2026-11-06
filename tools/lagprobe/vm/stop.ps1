# lag probe: end the task and kill only the probe's own processes (by command line)
schtasks /End /TN claude-lagprobe | Out-Null
Get-CimInstance Win32_Process | Where-Object { ($_.Name -eq 'MuseScore3Evo.exe' -and $_.CommandLine -like '*lagprobe\cfg*') -or ($_.Name -eq 'powershell.exe' -and $_.CommandLine -like '*lagprobe\monitor.ps1*') } | ForEach-Object { "kill $($_.ProcessId) $($_.Name)"; Stop-Process -Id $_.ProcessId -Force }
