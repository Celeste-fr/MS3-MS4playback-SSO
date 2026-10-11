import json, os
W = os.environ.get('PCL_WORK', '.')  # work folder: ev/, r/, index.json
idx = json.load(open(W + '/index.json'))
with open(W + '/list.txt', 'w', newline='') as f:
    for n, v in idx.items():
        f.write(f"{n}|{v['patch']}\r\n")
cmd = r'''@echo off
rem performance-context study: renders list.txt (name|state) with kthost, one share login
set S=%LOCALAPPDATA%\MuseScore\MuseScore3Evo\soundlibraries\Spitfire Symphony Orchestra
if not exist C:\claude\pcl\st mkdir C:\claude\pcl\st
for %%f in ("Violins 1" "Violins 2" Violas Celli Basses "Violins 1 - Performance" "Violins 2 - Performance" "Violas - Performance" "Celli - Performance" "Basses - Performance") do copy /y "%S%\%%~f.vst3state" C:\claude\pcl\st\ >nul
set /p PW=<C:\claude\share.txt
net use \\VOLENS\VMShare /user:VOLENS\vmshare %PW% >nul 2>&1 || (echo share login failed & exit /b 1)
for /f "usebackq tokens=1,2 delims=|" %%a in ("C:\claude\pcl\list.txt") do (
  C:\claude\kt\kthost.exe "C:\Program Files\Common Files\VST3\Kontakt 8.vst3" "C:\claude\pcl\st\%%b.vst3state" "C:\claude\pcl\%%a.txt" "C:\claude\pcl\%%a.wav" --tail 2 > "C:\claude\pcl\%%a.log" 2>&1
  echo %%a done
)
net use \\VOLENS\VMShare /delete /y >nul 2>&1
echo ALL DONE
'''
open(W + '/pcl-run.cmd', 'w', newline='').write(cmd.replace('\n', '\r\n'))
