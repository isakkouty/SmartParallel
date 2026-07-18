@echo off
setlocal EnableExtensions
set "TOOLCHAIN=%~1"
call "%~dp0run_parallel_for_validation.bat" "%TOOLCHAIN%"
if errorlevel 1 exit /b 1
call "%~dp0run_suite.bat" "%TOOLCHAIN%"
if errorlevel 1 exit /b 1
echo.
echo ============================================================
echo SmartParallel full regression completed successfully.
echo Core profiling results: validation\output\parallel_for_overhead.csv
echo OpenCV results: validation\output\opencv_*.csv
echo ============================================================
