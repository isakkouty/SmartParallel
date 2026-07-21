@echo off
call "%~dp0run_common.bat" build_scientific_test3 smartparallel_scientific_test3 SMARTPARALLEL_BUILD_SCIENTIFIC_TEST3 validation\output\scientific_test3_irregular_particles.csv "Scientific Test 3 - Irregular Particles" "%~1"
exit /b %errorlevel%
