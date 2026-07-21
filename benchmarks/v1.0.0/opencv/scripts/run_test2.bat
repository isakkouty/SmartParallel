@echo off
call "%~dp0run_common.bat" build_opencv_test2 smartparallel_opencv_test2 SMARTPARALLEL_BUILD_OPENCV_TEST2 validation\output\opencv_test2_convolution.csv "OpenCV Test 2" "%~1"
exit /b %errorlevel%
