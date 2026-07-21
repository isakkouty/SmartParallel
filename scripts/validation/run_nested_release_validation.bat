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
  echo Install the Visual Studio C++ workload or run from a VS Developer Command Prompt.
  exit /b 2
)

set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=11"
set "TRACE_MODE=%~2"
set "BACKEND_MODE=%~3"
if /I "%TRACE_MODE%"=="tbb" set "BACKEND_MODE=tbb"
set "ENABLE_TBB=OFF"
set "REQUIRE_TBB=OFF"
if /I "%BACKEND_MODE%"=="tbb" (
  set "ENABLE_TBB=ON"
  set "REQUIRE_TBB=ON"
)
set "BUILD_DIR=%REPO_ROOT%\build\nested_release_validation"
set "OUTPUT=%REPO_ROOT%\validation\output\v1.1.0_nested_execution_optimized.csv"
if /I "%BACKEND_MODE%"=="tbb" set "OUTPUT=%REPO_ROOT%\validation\output\v1.1.0_nested_execution_optimized_tbb_run.csv"
if /I "%TRACE_MODE%"=="trace" set "OUTPUT=%REPO_ROOT%\validation\output\v1.1.0_nested_execution_optimized_trace_run.csv"
if /I "%TRACE_MODE%"=="trace" if /I "%BACKEND_MODE%"=="tbb" set "OUTPUT=%REPO_ROOT%\validation\output\v1.1.0_nested_execution_optimized_tbb_trace_run.csv"
set "TARGET=smartparallel_v110_nested_benchmarks"

if not exist "%REPO_ROOT%\validation\output" mkdir "%REPO_ROOT%\validation\output" >nul 2>nul

echo(
echo ==== [1/4] Clean nested release build ====
rmdir /s /q "%BUILD_DIR%" 2>nul

echo(
echo ==== [2/4] Configure nested release validation ====
cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V110_NESTED_BENCHMARKS=ON ^
  -DSMARTPARALLEL_INSTALL=OFF ^
  -DSMARTPARALLEL_ENABLE_TBB=%ENABLE_TBB% ^
  -DSMARTPARALLEL_REQUIRE_TBB=%REQUIRE_TBB%
if errorlevel 1 goto :fail

echo(
echo ==== [3/4] Build and test nested release validation ====
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

ctest --test-dir "%BUILD_DIR%" --output-on-failure
if errorlevel 1 goto :fail

echo(
echo ==== [4/4] Run nested release benchmark ====
set "EXE=%BUILD_DIR%\benchmarks\v1.1.0\%TARGET%.exe"
if not exist "%EXE%" (
  echo ERROR: benchmark executable not found:
  echo   %EXE%
  goto :fail
)

set "BENCHMARK_BACKEND=thread_pool"
if /I "%BACKEND_MODE%"=="tbb" set "BENCHMARK_BACKEND=tbb"

if /I "%TRACE_MODE%"=="trace" (
  "%EXE%" "%OUTPUT%" "%REPETITIONS%" trace "%BENCHMARK_BACKEND%"
) else (
  "%EXE%" "%OUTPUT%" "%REPETITIONS%" "%BENCHMARK_BACKEND%"
)
if errorlevel 1 goto :fail

echo(
echo ============================================================
echo Nested release validation completed successfully.
echo Summary: %OUTPUT%
echo Raw samples: %OUTPUT:.csv=_raw.csv%
echo Trace: %OUTPUT:.csv=_trace.csv%
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
echo Nested release validation failed. Review the first error above.
exit /b 1
