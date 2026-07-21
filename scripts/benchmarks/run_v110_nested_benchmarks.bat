@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
call "%REPO_ROOT%\benchmarks\v1.1.0\scripts\run_nested_execution_benchmarks.bat" "%~1" "%~2"
exit /b %errorlevel%
