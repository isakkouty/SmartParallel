@echo off
call "%~dp0run_common.bat" build_scientific_test1 smartparallel_scientific_test1 SMARTPARALLEL_BUILD_SCIENTIFIC_TEST1 validation\output\scientific_test1_numerical_integration.csv "Scientific Test 1 - Numerical Integration" "%~1"
exit /b %errorlevel%
