@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

where cmake >nul 2>nul || (
  echo ERROR: cmake was not found in PATH.
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
  echo Set it to your permanent vcpkg directory, for example:
  echo   set VCPKG_ROOT=D:\Tools\vcpkg
  exit /b 2
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: invalid VCPKG_ROOT: %VCPKG_ROOT%
  exit /b 2
)

set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=7"
set "REUSE=%~2"
set "BUILD_DIR=%REPO_ROOT%\build\v14_algorithm_release_validation"
set "OUTPUT=%REPO_ROOT%\validation\output\v1.4.0_parallel_algorithms.csv"
set "TARGET=smartparallel_v140_algorithm_benchmarks"
set "EXE=%BUILD_DIR%\benchmarks\v1.4.0\%TARGET%.exe"

if not exist "%REPO_ROOT%\validation\output" mkdir "%REPO_ROOT%\validation\output" >nul 2>nul
if /I "%REUSE%"=="reuse" if exist "%EXE%" goto :test

rmdir /s /q "%BUILD_DIR%" 2>nul

echo(
echo ==== [1/4] Configure SmartParallel v1.4 Release validation ====
cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V140_ALGORITHM_BENCHMARKS=ON ^
  -DSMARTPARALLEL_BUILD_BENCHMARKS=OFF ^
  -DSMARTPARALLEL_ENABLE_TBB=ON ^
  -DSMARTPARALLEL_REQUIRE_TBB=ON ^
  -DSMARTPARALLEL_INSTALL=ON
if errorlevel 1 goto :fail

echo(
echo ==== [2/4] Build SmartParallel, validation, and v1.4 benchmarks ====
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

:test
echo(
echo ==== [3/4] Run complete deterministic CTest suite ====
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure --parallel 2
if errorlevel 1 goto :fail

echo(
echo ==== [4/4] Run v1.4 algorithm benchmark matrix ====
if not exist "%EXE%" (
  echo ERROR: v1.4 benchmark executable was not found:
  echo   %EXE%
  goto :fail
)
"%EXE%" "%OUTPUT%" "%REPETITIONS%"
if errorlevel 1 goto :fail

for %%A in (
  parallel_for_each
  parallel_transform
  parallel_transform_binary
  parallel_copy
  parallel_fill
  parallel_generate
  parallel_reduce
  parallel_transform_reduce
  parallel_transform_reduce_binary
  parallel_count
  parallel_count_if
  parallel_any_of
  parallel_all_of
  parallel_none_of
  parallel_find
  parallel_find_if
) do (
  findstr /C:",%%A," "%OUTPUT%" >nul
  if errorlevel 1 (
    echo ERROR: benchmark output is missing %%A.
    goto :fail
  )
)
findstr /C:",one_tbb," "%OUTPUT%" >nul
if errorlevel 1 (
  echo ERROR: oneTBB benchmark rows were not generated.
  goto :fail
)
if not exist "%OUTPUT:.csv=_raw.csv%" (
  echo ERROR: raw benchmark samples were not generated.
  goto :fail
)

echo(
echo ============================================================
echo SMARTPARALLEL V1.4 ALGORITHM RELEASE VALIDATION PASSED
echo Summary: %OUTPUT%
echo Raw samples: %OUTPUT:.csv=_raw.csv%
echo ============================================================
exit /b 0

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
echo SmartParallel v1.4 algorithm release validation failed. Review the first error above.
exit /b 1
