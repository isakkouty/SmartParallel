@echo off
call "%~dp0run_common.bat" build_scientific_test2 smartparallel_scientific_test2 SMARTPARALLEL_BUILD_SCIENTIFIC_TEST2 validation\output\scientific_test2_heat_diffusion.csv "Scientific Test 2 - 2D Heat Diffusion" "%~1"
exit /b %errorlevel%
