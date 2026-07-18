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

set "BUILD_DIR=build_v1_opencv_test1"
echo ==== [0/3] Normalize project timestamps ====
powershell -NoProfile -ExecutionPolicy Bypass -Command "$cutoff=(Get-Date).AddMinutes(-5); Get-ChildItem -LiteralPath . -Recurse -File | Where-Object { $_.FullName -notmatch '\\build[^\\]*\\' -and $_.FullName -notmatch '\\.git\\' } | ForEach-Object { $_.LastWriteTime=$cutoff }"
if errorlevel 1 (
    echo ERROR: failed to normalize project timestamps.
    exit /b 1
)

echo.
echo ==== [1/3] Configure OpenCV Test 1 ====
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

cmake -S . -B "%BUILD_DIR%" ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DSMARTPARALLEL_BUILD_OPENCV_TEST1=ON
if errorlevel 1 exit /b 1

echo.
echo ==== [2/3] Build OpenCV Test 1 ====
cmake --build "%BUILD_DIR%" --target smartparallel_opencv_test1
if errorlevel 1 exit /b 1

echo.
echo ==== [3/3] Run OpenCV Test 1 ====
if not exist validation\output mkdir validation\output
"%BUILD_DIR%\smartparallel_opencv_test1.exe" "validation\output\opencv_test1_threshold.csv"
if errorlevel 1 exit /b 1

echo.
echo ============================================================
echo OpenCV Test 1 completed.
echo Results: validation\output\opencv_test1_threshold.csv
echo ============================================================