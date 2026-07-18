@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ============================================================
echo SmartParallel complete benchmark and decision-quality run
echo ============================================================

echo.
echo [1/3] Core, hardening, and OpenCV regression
call benchmarks\opencv\scripts\run_full_regression.bat
if errorlevel 1 exit /b 1

echo.
echo [2/3] Scientific benchmark suite
call benchmarks\scientific\scripts\run_scientific_suite.bat
if errorlevel 1 exit /b 1

echo.
echo [3/3] Forced Sequential vs forced oneTBB vs adaptive audit
call benchmarks\decision_quality\scripts\run_decision_quality_audit.bat
if errorlevel 1 exit /b 1

echo.
echo ============================================================
echo ALL BENCHMARKS COMPLETED SUCCESSFULLY
echo Upload CSV files from validation\output\
echo Most important file:
echo validation\output\all_benchmarks_decision_quality.csv
echo ============================================================
endlocal
