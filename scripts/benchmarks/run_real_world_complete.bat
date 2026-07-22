@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"
set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=31"

call scripts\benchmarks\build_real_world_benchmarks.bat
if errorlevel 1 exit /b 1

rmdir /s /q "validation\output\real_world" 2>nul
set "SMARTPARALLEL_REAL_WORLD_SKIP_BUILD=1"

call scripts\benchmarks\run_real_world_integration.bat opencv %REPETITIONS% all all all trace
if errorlevel 1 exit /b 1
call scripts\benchmarks\run_real_world_integration.bat lz4 %REPETITIONS% all all all trace
if errorlevel 1 exit /b 1
call scripts\benchmarks\run_real_world_integration.bat bvh %REPETITIONS% all all all trace
if errorlevel 1 exit /b 1
call scripts\benchmarks\run_real_world_integration.bat particles %REPETITIONS% all all all trace
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\benchmarks\compare_real_world_results.ps1 validation\output\real_world
if errorlevel 1 exit /b 1
if not exist "validation\output\real_world\v1.1.0_real_world_analysis.md" (
  echo ERROR: final Markdown analysis was not generated.
  exit /b 1
)
for %%I in ("validation\output\real_world\v1.1.0_real_world_analysis.md") do if %%~zI LEQ 0 (
  echo ERROR: final Markdown analysis is empty.
  exit /b 1
)

echo(
echo ============================================================
echo COMPLETE REAL-WORLD SUITE PASSED
echo Results: validation\output\real_world
echo Comparison: validation\output\real_world\v1.1.0_real_world_comparison.csv
echo Analysis: validation\output\real_world\v1.1.0_real_world_analysis.md
echo ============================================================
exit /b 0
