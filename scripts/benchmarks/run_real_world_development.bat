@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"
set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=15"

call scripts\benchmarks\build_real_world_benchmarks.bat
if errorlevel 1 exit /b 1

rmdir /s /q "validation\output\real_world" 2>nul
set "SMARTPARALLEL_REAL_WORLD_SKIP_BUILD=1"

call scripts\benchmarks\run_real_world_integration.bat opencv %REPETITIONS% tiny,one_large,many_medium,mixed_sizes core all trace
if errorlevel 1 exit /b 1
call scripts\benchmarks\run_real_world_integration.bat lz4 %REPETITIONS% all core all trace
if errorlevel 1 exit /b 1
call scripts\benchmarks\run_real_world_integration.bat bvh %REPETITIONS% small_uniform,uniform,clustered,highly_unbalanced core all trace
if errorlevel 1 exit /b 1
call scripts\benchmarks\run_real_world_integration.bat particles %REPETITIONS% tiny,uniform,clustered,sudden_count_change core all trace
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\benchmarks\compare_real_world_results.ps1 validation\output\real_world
if errorlevel 1 exit /b 1

echo Real-world development matrix completed successfully.
exit /b 0
