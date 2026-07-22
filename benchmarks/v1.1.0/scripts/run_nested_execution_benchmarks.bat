@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..\..") do set "REPO_ROOT=%%~fI"
call "%REPO_ROOT%\scripts\validation\run_nested_release_validation.bat" "%~1" "%~2"
exit /b %errorlevel%
