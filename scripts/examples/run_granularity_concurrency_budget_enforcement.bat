@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

where cmake >nul 2>nul || (
  echo ERROR: cmake was not found in PATH.
  exit /b 2
)

where cl >nul 2>nul
if errorlevel 1 call :initialize_msvc
if errorlevel 1 exit /b %errorlevel%

set "TBB_CONFIG=%REPO_ROOT%\vcpkg_installed\x64-windows\share\tbb"
if not exist "%TBB_CONFIG%\TBBConfig.cmake" (
  echo ERROR: oneTBB was not found at:
  echo   %TBB_CONFIG%
  exit /b 2
)

set "BUILD_DIR=%REPO_ROOT%\build\granularity_concurrency_budget_enforcement"

echo(
echo ==== [1/3] Configure granularity and concurrency-budget enforcement ====
rmdir /s /q "%BUILD_DIR%" 2>nul
cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DTBB_DIR="%TBB_CONFIG%" ^
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON ^
  -DSMARTPARALLEL_BUILD_VALIDATION=OFF ^
  -DSMARTPARALLEL_BUILD_BENCHMARKS=OFF ^
  -DSMARTPARALLEL_INSTALL=OFF
if errorlevel 1 goto :fail

echo(
echo ==== [2/3] Build granularity and concurrency-budget enforcement ====
cmake --build "%BUILD_DIR%" --target smartparallel_granularity_concurrency_budget_enforcement
if errorlevel 1 goto :fail

echo(
echo ==== [3/3] Run granularity and concurrency-budget enforcement ====
"%BUILD_DIR%\examples\smartparallel_granularity_concurrency_budget_enforcement.exe"
if errorlevel 1 goto :fail

echo(
echo ============================================================
echo granularity and concurrency-budget enforcement completed successfully.
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
echo granularity and concurrency-budget enforcement failed. Review the first error above.
exit /b 1
