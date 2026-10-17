@echo off
rem run.cmd <folder>: renders <folder>\ev\<name>.txt for each line "name|patch|probe pitch" of <folder>\ev\list.txt through
rem kthost with MuseScore's own setup of the patch -> <folder>\r\<name>.wav. Writes only <folder>.
setlocal
if "%~1"=="" (echo usage: run.cmd ^<folder^> & exit /b 2)
set "RUN=%~1"
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist "%RUN%\ev\list.txt" (echo no %RUN%\ev\list.txt & exit /b 2)
if not exist "%RUN%\st" mkdir "%RUN%\st"
if not exist "%RUN%\r" mkdir "%RUN%\r"
for /f "usebackq tokens=1,2 delims=|" %%a in ("%RUN%\ev\list.txt") do if not exist "%RUN%\st\%%b.vst3state" copy /y "%SRC%\%%b.vst3state" "%RUN%\st\" >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
for /f "usebackq tokens=1,2,3 delims=|" %%a in ("%RUN%\ev\list.txt") do (
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "%RUN%\st\%%b.vst3state" "%RUN%\ev\%%a.txt" "%RUN%\r\%%a.wav" --tail 2 --wait 60 --probe-pitch %%c > "%RUN%\r\%%a.log" 2>&1
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo QTB RENDERS DONE
endlocal
exit /b 0
