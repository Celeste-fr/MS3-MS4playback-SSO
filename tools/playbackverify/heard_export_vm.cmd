@echo off
rem The heard check's export renders on the Windows VM (VERIFY.md > Heard check, HC_RENDER=export):
rem   heard_export_vm.cmd <run folder> <MuseScore3Evo.exe> convert     <run>\in.musicxml -> <run>\conv.mscz (the fork's import)
rem   heard_export_vm.cmd <run folder> <MuseScore3Evo.exe>             <run>\score.mscz rendered as File > Export audio renders
rem it (--verify-playback --verify-wav, MS_VERIFY_ROUTES=1): <run>\out\Playback verify <date>\score route <code>.wav, one per
rem library route, and score events.tsv. Writes only <run>. One share login for the run (the PC caps SMB sessions).
setlocal
if "%~2"=="" (echo usage: heard_export_vm.cmd ^<run folder^> ^<MuseScore3Evo.exe^> [convert] & exit /b 2)
set "RUN=%~1"
set "EXE=%~2"
if "%~3"=="convert" (
  "%EXE%" -o "%RUN%\conv.mscz" "%RUN%\in.musicxml" > "%RUN%\convert.txt" 2>&1
  if exist "%RUN%\conv.mscz" (echo HEARD CHECK CONVERT DONE) else (echo HEARD CHECK CONVERT FAILED)
  exit /b 0
)
if not exist "%RUN%\score.mscz" (echo no %RUN%\score.mscz & exit /b 2)
if not exist "%RUN%\out" mkdir "%RUN%\out"
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
set MS_VERIFY_ROUTES=1
echo start %date% %time%>"%RUN%\run.out"
"%EXE%" --verify-library "Spitfire Symphony Orchestra" --verify-playback "%RUN%\score.mscz" --verify-out "%RUN%\out" --verify-wav > "%RUN%\stdout.txt" 2> "%RUN%\stderr.txt"
echo exit %ERRORLEVEL% %date% %time%>>"%RUN%\run.out"
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo HEARD CHECK EXPORT DONE
endlocal
exit /b 0
