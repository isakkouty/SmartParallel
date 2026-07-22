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
  echo Set it to your vcpkg directory, for example:
  echo   set VCPKG_ROOT=C:\vcpkg
  exit /b 2
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: invalid VCPKG_ROOT: %VCPKG_ROOT%
  exit /b 2
)

set "BUILD_DIR=%REPO_ROOT%\build\real_world_release"
set "REUSE=%~1"
if /I not "%REUSE%"=="reuse" rmdir /s /q "%BUILD_DIR%" 2>nul

if /I "%REUSE%"=="reuse" if exist "%BUILD_DIR%\benchmarks\v1.1.0\real_world\smartparallel_real_world_opencv.exe" goto :build

echo(
echo ==== Configure SmartParallel real-world integrations ====
cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -DVCPKG_MANIFEST_FEATURES=real-world-benchmarks ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_REAL_WORLD_BENCHMARKS=ON ^
  -DSMARTPARALLEL_REQUIRE_REAL_WORLD_DEPENDENCIES=ON ^
  -DSMARTPARALLEL_ENABLE_TBB=ON ^
  -DSMARTPARALLEL_REQUIRE_TBB=ON ^
  -DSMARTPARALLEL_INSTALL=OFF
if errorlevel 1 goto :fail

:build
echo(
echo ==== Build SmartParallel real-world integrations ====
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

echo(
echo ==== Run existing CTest suite ====
ctest --test-dir "%BUILD_DIR%" --output-on-failure
if errorlevel 1 goto :fail

echo(
echo Real-world benchmark build completed successfully.
echo Build directory: %BUILD_DIR%
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
echo Real-world benchmark build failed. Review the first error above.
exit /b 1
