@echo off
rem run_vm.cmd <work> <batch> [passes]: the own-legato recordings on the Windows VM (tools/ownlegato/record.py's files in
rem <work>\<batch>\, list.txt "name|patch"), each through kthost (Kontakt 8 + SSO, offline; one fresh plug-in instance per
rem file and pass, so the round robins start alike) from MuseScore's own setup of the patch (copied once to <work>\st),
rem into <work>\wav\<batch>\<name>[_p<n>].wav. Writes only <work>. One share login per batch (the PC caps SMB sessions).
setlocal EnableDelayedExpansion
if "%~2"=="" (echo usage: run_vm.cmd ^<work^> ^<batch^> [passes] & exit /b 2)
set "W=%~1"
set "P=%~3"
if "%P%"=="" set "P=1"
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist "%W%\%2\list.txt" (echo no %W%\%2\list.txt & exit /b 2)
if not exist "%W%\st" mkdir "%W%\st"
if not exist "%W%\wav\%2" mkdir "%W%\wav\%2"
for /f "usebackq tokens=1,2 delims=|" %%a in ("%W%\%2\list.txt") do if not exist "%W%\st\%%b.vst3state" copy /y "%SRC%\%%b.vst3state" "%W%\st\" >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
echo batch %2 start %date% %time%>>"%W%\run.out"
for /f "usebackq tokens=1,2 delims=|" %%a in ("%W%\%2\list.txt") do (
  for /l %%p in (1,1,%P%) do (
    set "O=%%a"
    if not "%P%"=="1" set "O=%%a_p%%p"
    echo !O! start !time!>>"%W%\run.out"
    C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "%W%\st\%%b.vst3state" "%W%\%2\%%a.txt" "%W%\wav\%2\!O!.wav" --tail 4 --wait 60 > "%W%\wav\%2\!O!.log" 2>&1
    echo !O! done !time!>>"%W%\run.out"
  )
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo BATCH %2 DONE %time%>>"%W%\run.out"
echo BATCH %2 DONE
endlocal
