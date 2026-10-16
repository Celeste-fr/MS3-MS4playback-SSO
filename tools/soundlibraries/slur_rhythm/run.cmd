@echo off
rem run.cmd <work folder> <batch>: kthost renders (Kontakt 8 + SSO, offline) of events.py's files in <work>\<batch>\
rem (list.txt "code|patch"), each from MuseScore's resaved setup of its patch (copied to <work>\st once), into
rem <work>\wav\<batch>\. One share login per batch (the owner's PC caps SMB sessions). Log: <work>\run.out
rem (--wait 60: kthost waits that long for its probe note; the Basses' never passes -60 dBFS at their setups' levels, they render all the same)
set "W=%~1"
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist "%W%\st" mkdir "%W%\st"
if not exist "%W%\wav\%2" mkdir "%W%\wav\%2"
for /f "usebackq tokens=1,2 delims=|" %%a in ("%W%\%2\list.txt") do if not exist "%W%\st\%%b.vst3state" copy /y "%SRC%\%%b.vst3state*" "%W%\st\" >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("%W%\%2\list.txt") do (
  echo %%a start !time!>>"%W%\run.out"
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "%W%\st\%%b.vst3state" "%W%\%2\%%a.txt" "%W%\wav\%2\%%a.wav" --tail 3 --wait 60 > "%W%\wav\%2\%%a.log" 2>&1
  echo %%a done !time!>>"%W%\run.out"
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo BATCH %2 DONE>>"%W%\run.out"
