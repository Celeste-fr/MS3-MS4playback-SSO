@echo off
rem lag probe: the probe's own config folder (-c), its setups the main folder's (a junction), the task
cd /d C:\claude\lagprobe
if exist whence.mscz ren whence.mscz Whence.mscz
if not exist cfg\soundlibraries mklink /J cfg\soundlibraries "%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries"
copy /y "%LOCALAPPDATA%\MuseScore\MuseScore3Evo\playback.ini" cfg\ >nul
copy /y "%APPDATA%\MuseScore\MuseScore3Evo.ini" cfg\MuseScore\ >nul
powershell -NoProfile -c "$l = Get-Content C:\claude\verify.cmd -TotalCount 3; $l + @('call C:\claude\lagprobe\probe.cmd','net use \\VOLENS\VMShare /delete /y >nul 2>&1') | Set-Content C:\claude\lagprobe\task.cmd"
schtasks /Create /TN claude-lagprobe /TR C:\claude\lagprobe\task.cmd /SC ONCE /ST 23:59 /IT /F
dir /b cfg cfg\MuseScore
