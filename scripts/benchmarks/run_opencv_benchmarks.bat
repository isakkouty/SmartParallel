@echo off
setlocal EnableExtensions
for %%I in ("%~dp0..\..") do set "REPO_ROOT=%%~fI"
call "%REPO_ROOT%\benchmarks\opencv\scripts\run_suite.bat" "%~1"
exit /b %errorlevel%
