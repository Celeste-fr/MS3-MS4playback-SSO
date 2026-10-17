@echo off
rem sh.cmd <command…>: run a command with the owner's share logged in (SSO's samples stream from it), then log out.
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1
set PW=
%*
net use \\VOLENS\VMShare /delete /y >nul 2>&1
