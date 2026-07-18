@echo off
rem SmartParallel parallel_for validation and profiling-overhead runner.
rem Optional argument: full path to the vcpkg CMake toolchain file.

setlocal EnableExtensions
cd /d "%~dp0\..\..\.."

set "BUILD_DIR=build_parallel_for_validation"
set "VALIDATION_TARGET=smartparallel_parallel_for_validation"
set "OVERHEAD_TARGET=smartparallel_parallel_for_overhead"
set "CSV_PATH=validation\output\parallel_for_overhead.csv"
set "TOOLCHAIN=%~1"

if not defined TOOLCHAIN if defined VCPKG_ROOT set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN set "TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"

if not exist "%TOOLCHAIN%" (
    echo ERROR: vcpkg toolchain not found:
    echo "%TOOLCHAIN%"
    echo.
    echo Set VCPKG_ROOT or pass the toolchain path as the first argument.
    exit /b 1
)

echo ==== [0/4] Normalize project timestamps ====
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$cutoff=(Get-Date).AddMinutes(-5); Get-ChildItem -LiteralPath . -Recurse -File | Where-Object { $_.FullName -notmatch '\\build[^\\]*\\' -and $_.FullName -notmatch '\\.git\\' } | ForEach-Object { $_.LastWriteTime=$cutoff }"
if errorlevel 1 exit /b 1

echo.
echo ==== [1/4] Configure parallel_for validation ====
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"

cmake -S . -B "%BUILD_DIR%" ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DSMARTPARALLEL_BUILD_PARALLEL_FOR_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_PARALLEL_FOR_OVERHEAD=ON
if errorlevel 1 exit /b 1

echo.
echo ==== [2/4] Build parallel_for validation ====
cmake --build "%BUILD_DIR%" --target "%VALIDATION_TARGET%" "%OVERHEAD_TARGET%"
if errorlevel 1 exit /b 1

echo.
echo ==== [3/4] Run parallel_for correctness tests ====
ctest ^
  --test-dir "%BUILD_DIR%" ^
  -C Release ^
  --output-on-failure
if errorlevel 1 exit /b 1

echo.
echo ==== [4/4] Run parallel_for overhead benchmark ====
if not exist "validation\output" mkdir "validation\output"

"%BUILD_DIR%\%OVERHEAD_TARGET%.exe" "%CSV_PATH%"
if errorlevel 1 exit /b 1

echo.
echo ============================================================
echo Parallel For Validation completed successfully.
echo Results: %CSV_PATH%
echo ============================================================

exit /b 0