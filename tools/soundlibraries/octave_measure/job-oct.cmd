@echo off
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set MS_EXTRACT_HANG_MINUTES=60
set MS_EXTRACT_REDO=1
for %%V in (D E F G) do (
  echo oct %%V start %DATE% %TIME%
  "C:\claude\oct\%%V\bin\MuseScore3Evo.exe" --extract-library "Spitfire Symphony Orchestra" --check-rest --rest-parts legatopitches --extract-patches C:\claude\oct\oct-patches.txt
  echo oct %%V exit %ERRORLEVEL% %DATE% %TIME%
)
echo all done %DATE% %TIME%
