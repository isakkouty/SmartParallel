@echo off
call "%~dp0run_common.bat" build_opencv_stress smartparallel_opencv_stress SMARTPARALLEL_BUILD_OPENCV_STRESS validation\output\opencv_stress_suite.csv "OpenCV Stress Suite" "%~1"
exit /b %errorlevel%
