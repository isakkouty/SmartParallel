@echo off
setlocal
set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=31"
call "%~dp0run_real_world_complete.bat" "%REPETITIONS%"
exit /b %errorlevel%
