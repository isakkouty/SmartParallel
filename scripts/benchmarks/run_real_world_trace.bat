@echo off
setlocal
if "%~1"=="" (
  echo Usage: scripts\benchmarks\run_real_world_trace.bat ^<opencv^|lz4^|bvh^|particles^> [preset]
  exit /b 2
)
set "PRESET=%~2"
if not defined PRESET set "PRESET=all"
call "%~dp0run_real_world_integration.bat" "%~1" 3 "%PRESET%" all all trace
exit /b %errorlevel%
