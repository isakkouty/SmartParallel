@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "TOOLCHAIN=%~1"
if not defined TOOLCHAIN if defined VCPKG_ROOT set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN set "TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"

if not exist "%TOOLCHAIN%" (
    echo ERROR: vcpkg toolchain not found: "%TOOLCHAIN%"
    echo Set VCPKG_ROOT or pass the toolchain path as the first argument.
    exit /b 1
)

call run_v1_opencv_test1.bat "%TOOLCHAIN%"
if errorlevel 1 exit /b 1
call run_v1_opencv_test2.bat "%TOOLCHAIN%"
if errorlevel 1 exit /b 1
call run_v1_opencv_test3.bat "%TOOLCHAIN%"
if errorlevel 1 exit /b 1

echo.
echo ============================================================
echo All OpenCV integration tests completed successfully.
echo Results are in validation\output\opencv_test*.csv
echo ============================================================
