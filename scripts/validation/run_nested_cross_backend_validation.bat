@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"
set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=31"

call scripts\validation\run_nested_release_validation.bat %REPETITIONS%
if errorlevel 1 exit /b 1
call scripts\validation\run_nested_release_validation.bat 3 trace thread_pool reuse
if errorlevel 1 exit /b 1

call scripts\validation\run_nested_release_validation.bat %REPETITIONS% static reuse
if errorlevel 1 exit /b 1
call scripts\validation\run_nested_release_validation.bat 3 trace static reuse
if errorlevel 1 exit /b 1

call scripts\validation\run_nested_release_validation.bat %REPETITIONS% tbb
if errorlevel 1 exit /b 1
call scripts\validation\run_nested_release_validation.bat 3 trace tbb reuse
if errorlevel 1 exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\validation\compare_nested_backend_results.ps1 validation\output
if errorlevel 1 exit /b 1

echo Cross-backend nested validation completed successfully.
exit /b 0
