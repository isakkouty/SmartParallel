@echo off
setlocal EnableExtensions
set "TOOLCHAIN=%~1"
call "%~dp0run_test1.bat" "%TOOLCHAIN%"
if errorlevel 1 exit /b 1
call "%~dp0run_test2.bat" "%TOOLCHAIN%"
if errorlevel 1 exit /b 1
call "%~dp0run_test3.bat" "%TOOLCHAIN%"
if errorlevel 1 exit /b 1
call "%~dp0run_stress.bat" "%TOOLCHAIN%"
if errorlevel 1 exit /b 1

echo(
echo ============================================================
echo All OpenCV benchmarks completed successfully.
echo Results are in validation\output\opencv_*.csv
echo ============================================================
