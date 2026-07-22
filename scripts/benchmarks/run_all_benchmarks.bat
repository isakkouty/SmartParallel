@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

echo ============================================================
echo SmartParallel complete benchmark and decision-quality run
echo ============================================================

echo(
echo [1/4] Core, hardening, and OpenCV regression
call benchmarks\v1.0.0\opencv\scripts\run_full_regression.bat
if errorlevel 1 exit /b 1

echo(
echo [2/4] Scientific benchmark suite
call benchmarks\v1.0.0\scientific\scripts\run_scientific_suite.bat
if errorlevel 1 exit /b 1

echo(
echo [3/4] Forced Sequential vs forced oneTBB vs adaptive audit
call benchmarks\v1.0.0\decision_quality\scripts\run_decision_quality_audit.bat
if errorlevel 1 exit /b 1

echo(
echo [4/4] v1.1.0 nested execution benchmarks
call benchmarks\v1.1.0\scripts\run_nested_execution_benchmarks.bat
if errorlevel 1 exit /b 1

echo(
echo ============================================================
echo ALL BENCHMARKS COMPLETED SUCCESSFULLY
echo Upload CSV files from validation\output\
echo Most important file:
echo validation\output\all_benchmarks_decision_quality.csv
echo ============================================================
endlocal
