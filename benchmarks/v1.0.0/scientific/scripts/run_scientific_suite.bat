@echo off
setlocal EnableExtensions

echo ============================================================
echo SmartParallel Scientific Benchmark Suite
echo ============================================================
echo(

call "%~dp0run_scientific_test1.bat" "%~1"
if errorlevel 1 exit /b 1

echo(
call "%~dp0run_scientific_test2.bat" "%~1"
if errorlevel 1 exit /b 1

echo(
call "%~dp0run_scientific_test3.bat" "%~1"
if errorlevel 1 exit /b 1

echo(
echo ============================================================
echo Scientific benchmark suite completed successfully.
echo Results are in validation\output
echo ============================================================
exit /b 0
