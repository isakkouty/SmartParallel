@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

set "INTEGRATION=%~1"
if not defined INTEGRATION goto :usage
set "REPETITIONS=%~2"
if not defined REPETITIONS set "REPETITIONS=15"
set "PRESET=%~3"
if not defined PRESET set "PRESET=all"
set "MODE=%~4"
if not defined MODE set "MODE=all"
set "BACKEND=%~5"
if not defined BACKEND set "BACKEND=all"
set "TRACE=%~6"

if not "%SMARTPARALLEL_REAL_WORLD_SKIP_BUILD%"=="1" (
  call scripts\benchmarks\build_real_world_benchmarks.bat reuse
  if errorlevel 1 exit /b 1
)

set "TARGET="
if /I "%INTEGRATION%"=="opencv" set "TARGET=smartparallel_real_world_opencv"
if /I "%INTEGRATION%"=="lz4" set "TARGET=smartparallel_real_world_lz4"
if /I "%INTEGRATION%"=="bvh" set "TARGET=smartparallel_real_world_bvh"
if /I "%INTEGRATION%"=="particles" set "TARGET=smartparallel_real_world_particles"
if not defined TARGET goto :usage

set "EXE=%REPO_ROOT%\build\real_world_release\benchmarks\v1.1.0\real_world\%TARGET%.exe"
if not exist "%EXE%" (
  echo ERROR: benchmark executable not found: %EXE%
  exit /b 1
)
set "TRACE_ARG="
if /I "%TRACE%"=="trace" set "TRACE_ARG=--trace"

"%EXE%" --preset "%PRESET%" --mode "%MODE%" --backend "%BACKEND%" ^
  --repetitions "%REPETITIONS%" --warmups 3 --workers 4 ^
  --seed 1511505647 --output-dir "%REPO_ROOT%\validation\output\real_world" %TRACE_ARG%
exit /b %errorlevel%

:usage
echo Usage:
echo   scripts\benchmarks\run_real_world_integration.bat ^<opencv^|lz4^|bvh^|particles^> [repetitions] [preset] [mode] [backend] [trace]
exit /b 2
