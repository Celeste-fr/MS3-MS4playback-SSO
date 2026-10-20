@echo off
rem run_vm_par.cmd <work> <batchA> <batchB> [wait]: run_vm.cmd on two batches side by side (two kthost processes), with
rem one share login in this session (an ssh session's SMB login isn't seen by other sessions; ownlegato-study/PILOT.md).
rem Returns when both batches are done (<work>\<batch>.done).
setlocal EnableDelayedExpansion
set "W=%~1"
set "WT=%~4"
if "%WT%"=="" set "WT=60"
del "%W%\%2.done" "%W%\%3.done" >nul 2>&1
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare !PW! >nul 2>&1
set PW=
start "" /b cmd /c ""%W%\run_vm.cmd" "%W%" %2 1 %WT% noshare > "%W%\%2.done""
start "" /b cmd /c ""%W%\run_vm.cmd" "%W%" %3 1 %WT% noshare > "%W%\%3.done""
:wait
ping -n 6 127.0.0.1 >nul
findstr /c:"DONE" "%W%\%2.done" >nul 2>&1 || goto wait
findstr /c:"DONE" "%W%\%3.done" >nul 2>&1 || goto wait
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo PAR %2 %3 DONE
endlocal
