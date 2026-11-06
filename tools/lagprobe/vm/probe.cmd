rem lag probe run body: called by task.cmd after the share login. Reads tag.txt (the run's name) and env.cmd
cd /d C:\claude\lagprobe
set /p LP_TAG=<C:\claude\lagprobe\tag.txt
if exist C:\claude\lagprobe\env.cmd call C:\claude\lagprobe\env.cmd
if not exist out mkdir out
if exist cfg\session del cfg\session
if exist cfg\sc*.mscz del cfg\sc*.mscz
tasklist /fo csv > out\%LP_TAG%-tasks-start.csv
set MS_LAG_PROBE=C:\claude\lagprobe\out\%LP_TAG%.jsonl
start "" /b powershell -NoProfile -ExecutionPolicy Bypass -File C:\claude\lagprobe\monitor.ps1 -out C:\claude\lagprobe\out\%LP_TAG%-monitor.txt
echo start %date% %time% > out\%LP_TAG%.stdout.txt
C:\claude\lagprobe\b\bin\MuseScore3Evo.exe -c C:\claude\lagprobe\cfg C:\claude\lagprobe\Whence.mscz >> out\%LP_TAG%.stdout.txt 2>&1
echo exit %ERRORLEVEL% %date% %time% >> out\%LP_TAG%.stdout.txt
tasklist /fo csv > out\%LP_TAG%-tasks-end.csv
echo finished > out\%LP_TAG%.done
