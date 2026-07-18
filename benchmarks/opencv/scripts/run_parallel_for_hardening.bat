@echo off
setlocal EnableExtensions
set "ROOT=%~dp0..\..\.."
for %%I in ("%ROOT%") do set "ROOT=%%~fI"
set "BUILD=%ROOT%\build_parallel_for_hardening"
set "TOOLCHAIN=%~1"
if "%TOOLCHAIN%"=="" set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"

if exist "%BUILD%" rmdir /s /q "%BUILD%"
cmake -S "%ROOT%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
  -DSMARTPARALLEL_BUILD_PARALLEL_FOR_HARDENING=ON
if errorlevel 1 exit /b 1
cmake --build "%BUILD%" --config Release
if errorlevel 1 exit /b 1
ctest --test-dir "%BUILD%" --output-on-failure -C Release
exit /b %errorlevel%
