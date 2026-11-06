@echo off
rem lag probe: unpack build 442, point the probe's ini at its SSO map, hosted
cd /d C:\claude\lagprobe
if not exist b mkdir b
tar xzf b.tgz -C b
del b.tgz
powershell -NoProfile -c "$f='C:\claude\lagprobe\cfg\MuseScore\MuseScore3Evo.ini'; $t = Get-Content $f; $t = $t -replace '^soundLibrary=.*','soundLibrary=C:/claude/lagprobe/b/soundlibraries/Spitfire Symphony Orchestra.xml' -replace '^soundLibraryLast=.*','soundLibraryLast=C:/claude/lagprobe/b/soundlibraries/Spitfire Symphony Orchestra.xml' -replace '^soundLibraryOutput=.*','soundLibraryOutput=plugin'; Set-Content $f $t"
findstr /i "soundLibrary inspector" cfg\MuseScore\MuseScore3Evo.ini
dir /b b\bin\MuseScore3Evo.exe
tasklist /v /fo csv | findstr /i "MuseScore Ableton kthost ktload"
