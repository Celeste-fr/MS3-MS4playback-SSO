@echo off
rem The heard check's renders on the Windows VM (VERIFY.md > Heard check): heard_check_vm.cmd <run>
rem C:\claude\heardcheck\<run>\list.txt ("route|patch|probe pitch") and <route>.txt (kthost events) -> <run>\wav\<route>.wav, each
rem route through kthost with MuseScore's own setup of its patch (copied from MuseScore's setups folder). Only
rem C:\claude\heardcheck is written. One share login for the whole batch (the PC caps SMB sessions). kthost waits at
rem most 60 s for its probe note (the route's first pitch) to pass -60 dBFS: the Basses' never does at their setup's
rem levels (their renders sound; 2026-10-16), and a resaved setup's samples load in seconds; the analysis lists routes
rem whose probe stayed silent (<route>.log).
setlocal
if "%~1"=="" (echo usage: heard_check_vm.cmd ^<run^> & exit /b 2)
set "HC=C:\claude\heardcheck"
set "RUN=%HC%\%~1"
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist "%RUN%\list.txt" (echo no %RUN%\list.txt & exit /b 2)
if not exist "%RUN%\st" mkdir "%RUN%\st"
if not exist "%RUN%\wav" mkdir "%RUN%\wav"
for /f "usebackq tokens=1,2 delims=|" %%a in ("%RUN%\list.txt") do if not exist "%RUN%\st\%%b.vst3state" copy /y "%SRC%\%%b.vst3state" "%RUN%\st\" >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
echo start %date% %time%>"%RUN%\run.out"
for /f "usebackq tokens=1,2,3 delims=|" %%a in ("%RUN%\list.txt") do (
  call :stamp "%%a %%b start"
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "%RUN%\st\%%b.vst3state" "%RUN%\%%a.txt" "%RUN%\wav\%%a.wav" --tail 3 --wait 60 --probe-pitch %%c > "%RUN%\wav\%%a.log" 2>&1
  call :stamp "%%a done"
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo end %date% %time%>>"%RUN%\run.out"
echo HEARD CHECK RENDERS DONE
endlocal
exit /b 0

:stamp
echo %~1 %time%>>"%RUN%\run.out"
exit /b 0
