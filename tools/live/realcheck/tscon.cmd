@echo off
rem tscon.cmd (tools/live/realcheck): run by the scheduled task claude-rc-tscon (SYSTEM): when the desktop session of
rem the user named in C:\claude\rc\tscon.user (vm_run.ps1 writes it) is disconnected (an RDP client left), it is put
rem on the console, so that GUI work has a connected, unlocked desktop. A connected session (RDP or console) is left alone.
set /p U=<C:\claude\rc\tscon.user
for /f "tokens=1,2,3" %%a in ('query session %U% ^| findstr /i /c:"%U%" ^| findstr /i "Disc"') do (
  if /i "%%a"=="%U%" (tscon %%b /dest:console)
)
query session > C:\claude\rc\tscon.out 2>&1
