@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

where cmake >nul 2>nul || (
  echo ERROR: cmake was not found in PATH.
  exit /b 2
)
where ctest >nul 2>nul || (
  echo ERROR: ctest was not found in PATH.
  exit /b 2
)
where powershell >nul 2>nul || (
  echo ERROR: PowerShell was not found in PATH.
  exit /b 2
)

set "PYTHON_COMMAND="
where py >nul 2>nul && set "PYTHON_COMMAND=py -3"
if not defined PYTHON_COMMAND where python >nul 2>nul && set "PYTHON_COMMAND=python"
if not defined PYTHON_COMMAND (
  echo ERROR: neither the Python launcher "py" nor "python" was found in PATH.
  exit /b 2
)

where cl >nul 2>nul
if errorlevel 1 call :initialize_msvc
if errorlevel 1 (
  echo ERROR: MSVC compiler "cl" was not found.
  echo Install the Visual Studio 2022 Desktop development with C++ workload.
  exit /b 2
)

if not defined VCPKG_ROOT (
  echo ERROR: VCPKG_ROOT is not defined.
  echo Example:
  echo   set VCPKG_ROOT=D:\Tools\vcpkg
  exit /b 2
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: invalid VCPKG_ROOT: %VCPKG_ROOT%
  exit /b 2
)

set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=31"
for /f "delims=0123456789" %%A in ("%REPETITIONS%") do (
  echo ERROR: repetition count must be a positive odd integer.
  exit /b 2
)
set /a REPETITION_REMAINDER=%REPETITIONS% %% 2
if %REPETITIONS% LEQ 0 (
  echo ERROR: repetition count must be positive.
  exit /b 2
)
if %REPETITION_REMAINDER% EQU 0 (
  echo ERROR: repetition count must be odd.
  exit /b 2
)

set "REUSE=%~2"
set "BUILD_DIR=%REPO_ROOT%\build\v15_adaptive_routes_release"
set "TARGET=smartparallel_v150_adaptive_routes_benchmarks"
set "EXE=%BUILD_DIR%\benchmarks\v1.5.0\%TARGET%.exe"
for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "RUN_STAMP=%%I"
set "OUTPUT_DIR=%REPO_ROOT%\validation\output\v1.5.0_adaptive_routes\publication_%RUN_STAMP%"
set "RAW=%OUTPUT_DIR%\v1.5.0_adaptive_routes_raw.csv"
set "ARCHIVE=%OUTPUT_DIR%.zip"
set "BUILD_LOG=%OUTPUT_DIR%\v1.5.0_build_vectorization.log"

if /I "%REUSE%"=="reuse" if exist "%EXE%" goto :test
rmdir /s /q "%BUILD_DIR%" 2>nul

echo(
echo ==== [1/6] Configure SmartParallel v1.5 Adaptive Execution Routes ====
cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DVCPKG_MANIFEST_FEATURES=vision-opencv ^
  -DSMARTPARALLEL_BUILD_VISION=ON ^
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON ^
  -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V150_VISION_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V150_VISION_BENCHMARKS=ON ^
  -DSMARTPARALLEL_BUILD_BENCHMARKS=OFF ^
  -DSMARTPARALLEL_ENABLE_TBB=ON ^
  -DSMARTPARALLEL_REQUIRE_TBB=ON ^
  -DSMARTPARALLEL_INSTALL=ON
if errorlevel 1 goto :fail

echo(
echo ==== [2/6] Build library, tests, package targets, and benchmark ====
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%" >nul 2>nul
cmake --build "%BUILD_DIR%" > "%BUILD_LOG%" 2>&1
set "BUILD_STATUS=!ERRORLEVEL!"
type "%BUILD_LOG%"
if not "!BUILD_STATUS!"=="0" goto :fail

:test
echo(
echo ==== [3/6] Run complete deterministic CTest suite ====
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure --parallel 2
if errorlevel 1 goto :fail

echo(
echo ==== [4/6] Run v1.5 publication benchmark matrix ====
if not exist "%EXE%" (
  echo ERROR: benchmark executable was not found:
  echo   %EXE%
  goto :fail
)
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%" >nul 2>nul
"%EXE%" "%RAW%" "%REPETITIONS%"
if errorlevel 1 goto :fail
if not exist "%RAW%" (
  echo ERROR: raw benchmark CSV was not generated.
  goto :fail
)

echo(
echo ==== [5/6] Validate raw data and generate report/graphs ====
%PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v15_adaptive_routes.py" "%RAW%" "%OUTPUT_DIR%"
if errorlevel 1 goto :fail
%PYTHON_COMMAND% "%REPO_ROOT%\tools\generate_v15_benchmark_plots.py" ^
  "%OUTPUT_DIR%\v1.5.0_adaptive_routes.csv" ^
  "%RAW%" ^
  "%OUTPUT_DIR%\v1.5.0_adaptive_routes_learning.csv" ^
  "%OUTPUT_DIR%"
if errorlevel 1 goto :fail
if not exist "%OUTPUT_DIR%\v1.5.0_adaptive_routes.csv" goto :missing_analysis
if not exist "%OUTPUT_DIR%\v1.5.0_adaptive_routes_report.md" goto :missing_analysis
if not exist "%OUTPUT_DIR%\v1.5.0_automatic_speedup.svg" goto :missing_analysis
if not exist "%OUTPUT_DIR%\v1.5.0_route_selection_regret.svg" goto :missing_analysis
if not exist "%OUTPUT_DIR%\v1.5.0_native_kernel_vs_oracle.svg" goto :missing_analysis
if not exist "%OUTPUT_DIR%\v1.5.0_dispatch_overhead.svg" goto :missing_analysis
if not exist "%OUTPUT_DIR%\v1.5.0_adaptive_route_map.svg" goto :missing_analysis

echo(
echo ==== [6/6] Package publication artifacts ====
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Compress-Archive -Path '%OUTPUT_DIR%\*' -DestinationPath '%ARCHIVE%' -Force"
if errorlevel 1 goto :fail

echo(
echo ============================================================
echo SMARTPARALLEL V1.5 ADAPTIVE ROUTES VALIDATION PASSED
echo Raw samples: %RAW%
echo Summary: %OUTPUT_DIR%\v1.5.0_adaptive_routes.csv
echo Report: %OUTPUT_DIR%\v1.5.0_adaptive_routes_report.md
echo Shareable archive: %ARCHIVE%
echo ============================================================
exit /b 0

:missing_analysis
echo ERROR: analysis outputs were not generated.
goto :fail

:initialize_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 2
set "VS_INSTALLATION="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALLATION=%%I"
if not defined VS_INSTALLATION exit /b 2
call "%VS_INSTALLATION%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
where cl >nul 2>nul || exit /b 2
exit /b 0

:fail
echo(
echo SmartParallel v1.5 Adaptive Execution Routes validation failed.
echo Review the first error above.
exit /b 1
