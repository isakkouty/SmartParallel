@echo off
setlocal EnableExtensions

rem SmartParallel v1.1 Step 5 - configure, build, and run the nested concurrency budget example.
rem This script can be launched from a normal Command Prompt or PowerShell.

for %%I in ("%~dp0..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

where cmake >nul 2>nul || (
  echo ERROR: cmake was not found in PATH.
  exit /b 2
)

rem Match the established SmartParallel Windows build format: MSVC + NMake.
where cl >nul 2>nul
if errorlevel 1 call :initialize_msvc
if errorlevel 1 exit /b %errorlevel%

set "TBB_CONFIG=%REPO_ROOT%\vcpkg_installed\x64-windows\share\tbb"
if not exist "%TBB_CONFIG%\TBBConfig.cmake" (
  echo ERROR: oneTBB was not found at:
  echo   %TBB_CONFIG%
  echo Restore the vcpkg dependencies before running this example.
  exit /b 2
)

set "BUILD_DIR=%REPO_ROOT%\build\nested_concurrency_budget"

echo(
echo ==== [1/3] Configure nested concurrency budget example ====
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
echo ==== [2/3] Build nested concurrency budget example ====
cmake --build "%BUILD_DIR%" --target smartparallel_example
if errorlevel 1 goto :fail

echo(
echo ==== [3/3] Run nested concurrency budget example ====
"%BUILD_DIR%\examples\smartparallel_example.exe"
if errorlevel 1 goto :fail

echo(
echo ============================================================
echo Nested concurrency budget example completed successfully.
echo ============================================================
exit /b 0

:initialize_msvc
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo ERROR: MSVC compiler "cl" was not found and vswhere.exe is unavailable.
  echo Install Visual Studio C++ build tools or use a VS Developer Command Prompt.
  exit /b 2
)

set "VS_INSTALLATION="
for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_INSTALLATION=%%I"

if not defined VS_INSTALLATION (
  echo ERROR: No Visual Studio installation with C++ build tools was found.
  exit /b 2
)

if not exist "%VS_INSTALLATION%\Common7\Tools\VsDevCmd.bat" (
  echo ERROR: Visual Studio developer environment script was not found.
  exit /b 2
)

call "%VS_INSTALLATION%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
where cl >nul 2>nul || (
  echo ERROR: Failed to initialize the MSVC compiler environment.
  exit /b 2
)
exit /b 0

:fail
echo(
echo Nested concurrency budget example failed. Review the first error above.
exit /b 1
