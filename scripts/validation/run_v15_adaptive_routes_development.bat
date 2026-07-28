@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
call "%REPO_ROOT%\scripts\validation\run_v15_adaptive_routes_release_validation.bat" 7 %1
exit /b %errorlevel%
