@echo off
setlocal EnableExtensions
cd /d "%~dp0\..\..\.."

call benchmarks\scientific\scripts\run_common.bat ^
  build_decision_quality_audit ^
  smartparallel_decision_quality_audit ^
  SMARTPARALLEL_BUILD_DECISION_QUALITY_AUDIT ^
  validation\output\all_benchmarks_decision_quality.csv ^
  "Decision-quality audit" ^
  "%~1"

exit /b %errorlevel%
