@echo off
rem lag probe: go.cmd <tag> [phases] [reps] [modes] (lists with + : o+b+e), writes tag.txt / env.cmd, starts the task
cd /d C:\claude\lagprobe
set P=%~2
set M=%~4
echo %1> tag.txt
echo set MS_LAG_PROBE_PHASES=%P:+=,%> env.cmd
>>env.cmd echo set MS_LAG_PROBE_REPS=%~3
if not "%M%"=="" echo set MS_LAG_PROBE_MODES=%M:+=,%>> env.cmd
type env.cmd
schtasks /Run /TN claude-lagprobe
