@echo off
rem Shared helper. Call with: build_dir target cmake_option csv_path display_name [toolchain]
setlocal EnableExtensions
cd /d "%~dp0\..\..\.."

set "BUILD_DIR=%~1"
set "TARGET=%~2"
set "OPTION=%~3"
set "CSV_PATH=%~4"
set "DISPLAY_NAME=%~5"
set "TOOLCHAIN=%~6"

if not defined TOOLCHAIN if defined VCPKG_ROOT set "TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined TOOLCHAIN set "TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
if not exist "%TOOLCHAIN%" (
    echo ERROR: vcpkg toolchain not found: "%TOOLCHAIN%"
    echo Set VCPKG_ROOT or pass the toolchain path.
    exit /b 1
)

echo ==== [0/3] Normalize project timestamps ====
powershell -NoProfile -ExecutionPolicy Bypass -Command "$cutoff=(Get-Date).AddMinutes(-5); Get-ChildItem -LiteralPath . -Recurse -File | Where-Object { $_.FullName -notmatch '\\build[^\\]*\\' -and $_.FullName -notmatch '\\.git\\' } | ForEach-Object { $_.LastWriteTime=$cutoff }"
if errorlevel 1 exit /b 1

echo(
echo ==== [1/3] Configure %DISPLAY_NAME% ====
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
cmake -S . -B "%BUILD_DIR%" ^
  -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_TOOLCHAIN_FILE="%TOOLCHAIN%" ^
  -DVCPKG_TARGET_TRIPLET=x64-windows ^
  -D%OPTION%=ON
if errorlevel 1 exit /b 1

echo(
echo ==== [2/3] Build %DISPLAY_NAME% ====
cmake --build "%BUILD_DIR%" --target "%TARGET%"
if errorlevel 1 exit /b 1

echo(
echo ==== [3/3] Run %DISPLAY_NAME% ====
if not exist validation\output mkdir validation\output
set "EXE_PATH="
for /f "delims=" %%F in ('dir /s /b "%BUILD_DIR%\%TARGET%.exe" 2^>nul') do if not defined EXE_PATH set "EXE_PATH=%%~fF"
if not defined EXE_PATH (
    echo ERROR: benchmark executable not found: %TARGET%.exe
    exit /b 1
)
"%EXE_PATH%" "%CSV_PATH%"
if errorlevel 1 exit /b 1

echo(
echo ============================================================
echo %DISPLAY_NAME% completed.
echo Results: %CSV_PATH%
echo ============================================================
