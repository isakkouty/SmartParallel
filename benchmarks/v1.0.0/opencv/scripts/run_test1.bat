@echo off
call "%~dp0run_common.bat" build_opencv_test1 smartparallel_opencv_test1 SMARTPARALLEL_BUILD_OPENCV_TEST1 validation\output\opencv_test1_threshold.csv "OpenCV Test 1" "%~1"
exit /b %errorlevel%
