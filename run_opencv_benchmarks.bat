@echo off
call "%~dp0benchmarks\opencv\scripts\run_suite.bat" "%~1"
exit /b %errorlevel%
