@echo off
setlocal EnableExtensions
for %%I in ("%~dp0\..\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

set "BUILD_DIR=build_v110_nested_benchmarks"
set "TARGET=smartparallel_v110_nested_benchmarks"
set "CSV=validation\output\v1.1.0_nested_execution_benchmarks.csv"
set "TOOLCHAIN=%~1"
if not defined TOOLCHAIN if defined VCPKG_ROOT set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN set "TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
if not exist "%TOOLCHAIN%" (
  echo ERROR: vcpkg toolchain not found: "%TOOLCHAIN%"
  echo Set VCPKG_ROOT or pass the toolchain path as the first argument.
  exit /b 1
)

set "REPETITIONS=%~2"
if not defined REPETITIONS set "REPETITIONS=7"

echo ==== [1/3] Configure v1.1.0 nested benchmarks ====
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
cmake -S . -B "%BUILD_DIR%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DSMARTPARALLEL_BUILD_V110_NESTED_BENCHMARKS=ON
if errorlevel 1 exit /b 1

echo(
echo ==== [2/3] Build v1.1.0 nested benchmarks ====
cmake --build "%BUILD_DIR%" --target "%TARGET%"
if errorlevel 1 exit /b 1

echo(
echo ==== [3/3] Run v1.1.0 nested benchmarks ====
"%BUILD_DIR%\benchmarks\v1.1.0\%TARGET%.exe" "%CSV%" "%REPETITIONS%"
if errorlevel 1 exit /b 1

echo(
echo ============================================================
echo v1.1.0 nested benchmarks completed successfully.
echo CSV: %CSV%
echo ============================================================
endlocal
