@echo off
rem run.cmd <batch>: renders C:\claude\legatoswitch\<batch>\list.txt ("name|patch") with kthost (Kontakt 8 + SSO, MuseScore's
rem setups copied to st\ once), each wav converted to mono FLAC (conv.py) and the wav deleted (literal path); only
rem C:\claude\legatoswitch is touched. A stop.txt there ends the batch after the current file. Log: run.out
set "W=C:\claude\legatoswitch"
set "SRC=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra"
if not exist "%W%\st" mkdir "%W%\st"
if not exist "%W%\%1\r" mkdir "%W%\%1\r"
for /f "usebackq tokens=1,2 delims=|" %%a in ("%W%\%1\list.txt") do if not exist "%W%\st\%%b.vst3state" copy /y "%SRC%\%%b.vst3state" "%W%\st\" >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
setlocal EnableDelayedExpansion
for /f "usebackq tokens=1,2 delims=|" %%a in ("%W%\%1\list.txt") do (
  if exist "%W%\stop.txt" goto :end
  if not exist "%W%\%1\r\%%a.flac" (
    echo %%a start !date! !time!>>"%W%\run.out"
    set X=
    if "%%b"=="Basses - Performance" set X=--probe-pitch 40
    C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "%W%\st\%%b.vst3state" "%W%\%1\%%a.txt" "%W%\%1\r\%%a.wav" --tail 3 --wait 120 !X! > "%W%\%1\r\%%a.log" 2>&1
    "C:\Program Files\Python312\python.exe" "%W%\conv.py" "%W%\%1\r\%%a.wav" "%W%\%1\r\%%a.flac" >> "%W%\%1\r\%%a.log" 2>&1
    if exist "%W%\%1\r\%%a.flac" del "%W%\%1\r\%%a.wav"
    echo %%a done !date! !time!>>"%W%\run.out"
  )
)
:end
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo BATCH %1 DONE>>"%W%\run.out"
