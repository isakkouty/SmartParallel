@echo off
call "%~dp0run_common.bat" build_opencv_test3 smartparallel_opencv_test3 SMARTPARALLEL_BUILD_OPENCV_TEST3 validation\output\opencv_test3_sobel.csv "OpenCV Test 3" "%~1"
exit /b %errorlevel%
