@echo off
rem The heard check's export renders on the Windows VM (VERIFY.md > Heard check, HC_RENDER=export):
rem   heard_export_vm.cmd <run folder> <MuseScore3Evo.exe> convert     <run>\in.musicxml -> <run>\conv.mscz (the fork's import)
rem   heard_export_vm.cmd <run folder> <MuseScore3Evo.exe>             each <run>\score*.mscz rendered as File > Export audio
rem renders it (--verify-playback --verify-wav, MS_VERIFY_ROUTES=1) into <run>\out\<score>\Playback verify <date>\: <score>
rem route <code>.wav, one per library route, and <score> events.tsv (HC_SPLIT=parts: score-1.mscz, score-2.mscz ..., each
rem with one part on the library, one MuseScore each). Writes only <run>. One share login for the run (the PC caps SMB
rem sessions).
setlocal
if "%~2"=="" (echo usage: heard_export_vm.cmd ^<run folder^> ^<MuseScore3Evo.exe^> [convert] & exit /b 2)
set "RUN=%~1"
set "EXE=%~2"
if "%~3"=="convert" (
  "%EXE%" -o "%RUN%\conv.mscz" "%RUN%\in.musicxml" > "%RUN%\convert.txt" 2>&1
  if exist "%RUN%\conv.mscz" (echo HEARD CHECK CONVERT DONE) else (echo HEARD CHECK CONVERT FAILED)
  exit /b 0
)
if not exist "%RUN%\score*.mscz" (echo no %RUN%\score*.mscz & exit /b 2)
if not exist "%RUN%\out" mkdir "%RUN%\out"
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
set MS_VERIFY_ROUTES=1
rem (the free memory for the copies pinned as in the dump: heard_check.sh writes its MEM here; builds before voice-determinism ignore it)
set MS_FREE_MEMORY_MB=16384
echo start %date% %time%>"%RUN%\run.out"
for %%s in ("%RUN%\score*.mscz") do call :one "%%~ns"
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo end %date% %time%>>"%RUN%\run.out"
echo HEARD CHECK EXPORT DONE
endlocal
exit /b 0

:one
if not exist "%RUN%\out\%~1" mkdir "%RUN%\out\%~1"
echo %~1 start %time%>>"%RUN%\run.out"
"%EXE%" --verify-library "Spitfire Symphony Orchestra" --verify-playback "%RUN%\%~1.mscz" --verify-out "%RUN%\out\%~1" --verify-wav > "%RUN%\out\%~1\stdout.txt" 2> "%RUN%\out\%~1\stderr.txt"
echo %~1 exit %ERRORLEVEL% %time%>>"%RUN%\run.out"
exit /b 0
