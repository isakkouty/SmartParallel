@echo off
setlocal EnableExtensions EnableDelayedExpansion
for %%I in ("%~dp0\..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

where cmake >nul 2>nul || (echo ERROR: cmake not found.& exit /b 2)
where ctest >nul 2>nul || (echo ERROR: ctest not found.& exit /b 2)
where powershell >nul 2>nul || (echo ERROR: PowerShell not found.& exit /b 2)
set "PYTHON_COMMAND="
where py >nul 2>nul && set "PYTHON_COMMAND=py -3"
if not defined PYTHON_COMMAND where python >nul 2>nul && set "PYTHON_COMMAND=python"
if not defined PYTHON_COMMAND (echo ERROR: Python not found.& exit /b 2)
where cl >nul 2>nul
if errorlevel 1 call :initialize_msvc
if errorlevel 1 (echo ERROR: MSVC cl.exe not found.& exit /b 2)

set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=31"
for /f "delims=0123456789" %%A in ("%REPETITIONS%") do (echo ERROR: repetitions must be positive.& exit /b 2)
if %REPETITIONS% LEQ 0 (echo ERROR: repetitions must be positive.& exit /b 2)

if not defined VCPKG_ROOT (
  echo ERROR: VCPKG_ROOT is required for the full oneTBB/OpenCV Windows matrix.
  exit /b 2
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: invalid VCPKG_ROOT: %VCPKG_ROOT%
  exit /b 2
)

set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
set "VCPKG_INSTALL_ROOT=%REPO_ROOT%\vcpkg_installed"
set "VCPKG_TRIPLET=x64-windows"

for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "RUN_STAMP=%%I"
set "OUTPUT_DIR=%REPO_ROOT%\validation\output\v1.6.0_scientific_foundations\publication_%RUN_STAMP%"
set "BUILD_DIR=%REPO_ROOT%\build\v16_scientific_release"
set "NO_TBB_DIR=%REPO_ROOT%\build\v16_scientific_no_tbb"
set "INSTALL_DIR=%OUTPUT_DIR%\install"
set "RAW=%OUTPUT_DIR%\v1.6.0_scientific_raw.csv"
set "EXE=%BUILD_DIR%\benchmarks\v1.6.0\smartparallel_v160_scientific_benchmarks.exe"
set "PILOT=%BUILD_DIR%\examples\smartparallel_v160_heat_diffusion.exe"
set "ENVIRONMENT_FILE=%OUTPUT_DIR%\v1.6.0_environment.txt"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

powershell -NoProfile -Command "$cpu=@(Get-CimInstance Win32_Processor)[0]; if($null -ne $cpu){$cpu.Name.Trim()}" > "%OUTPUT_DIR%\cpu-description.tmp"
set /p "SMARTPARALLEL_CPU_DESCRIPTION="<"%OUTPUT_DIR%\cpu-description.tmp"
del /q "%OUTPUT_DIR%\cpu-description.tmp" 2>nul
if not defined SMARTPARALLEL_CPU_DESCRIPTION set "SMARTPARALLEL_CPU_DESCRIPTION=unreported"

 echo ==== [1/8] Configure MSVC Release with oneTBB and OpenCV ====
rmdir /s /q "%BUILD_DIR%" 2>nul
cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
  -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
  -DVCPKG_MANIFEST_DIR="%REPO_ROOT%" ^
  -DVCPKG_MANIFEST_FEATURES=vision-opencv ^
  -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALL_ROOT%" ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_BENCHMARKS=ON ^
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON ^
  -DSMARTPARALLEL_BUILD_VISION=ON ^
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON ^
  -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON ^
  -DSMARTPARALLEL_ENABLE_TBB=ON -DSMARTPARALLEL_REQUIRE_TBB=ON ^
  -DSMARTPARALLEL_INSTALL=ON
if errorlevel 1 goto :fail
cmake --build "%BUILD_DIR%"
if errorlevel 1 goto :fail

 echo ==== [2/8] Run complete CTest regression ====
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure --parallel 2 ^
  --output-log "%OUTPUT_DIR%\ctest-main.log"
if errorlevel 1 goto :fail

 echo ==== [3/8] Run heat pilot and benchmark ====
"%PILOT%" 128 128 50 reproducible > "%OUTPUT_DIR%\v1.6.0_heat_diffusion_pilot.txt"
if errorlevel 1 goto :fail
"%EXE%" "%RAW%" "%REPETITIONS%"
if errorlevel 1 goto :fail
%PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v16_scientific_foundations.py" "%RAW%" "%OUTPUT_DIR%"
if errorlevel 1 goto :fail

 echo ==== [4/8] Validate documentation ====
%PYTHON_COMMAND% "%REPO_ROOT%\tools\check_documentation.py" > "%OUTPUT_DIR%\documentation-validation.log" 2>&1
set "DOCUMENTATION_RC=%ERRORLEVEL%"
type "%OUTPUT_DIR%\documentation-validation.log"
if not "%DOCUMENTATION_RC%"=="0" goto :fail

 echo ==== [5/8] Install and validate package consumers ====
cmake --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%"
if errorlevel 1 goto :fail
for %%C in (package-consumer package-consumer-vision) do (
  set "CONSUMER_BUILD=%OUTPUT_DIR%\%%C-build"
  if "%%C"=="package-consumer" set "CONSUMER_LOG=%OUTPUT_DIR%\core-package-consumer-test.log"
  if "%%C"=="package-consumer-vision" set "CONSUMER_LOG=%OUTPUT_DIR%\vision-package-consumer-test.log"

  rmdir /s /q "!CONSUMER_BUILD!" 2>nul

  cmake -S "%REPO_ROOT%\tests\%%C" -B "!CONSUMER_BUILD!" ^
    -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
      -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALL_ROOT%" ^
    -DCMAKE_PREFIX_PATH="%INSTALL_DIR%"
  if errorlevel 1 goto :fail

  cmake --build "!CONSUMER_BUILD!"
  if errorlevel 1 goto :fail

  ctest --test-dir "!CONSUMER_BUILD!" -C Release --output-on-failure ^
    --output-log "!CONSUMER_LOG!"
  if errorlevel 1 goto :fail
)

 echo ==== [6/8] Validate isolated no-oneTBB/no-OpenCV build ====
rmdir /s /q "%NO_TBB_DIR%" 2>nul
cmake -E env --unset=VCPKG_ROOT --unset=VCPKG_INSTALLATION_ROOT ^
  --unset=VCPKG_FEATURE_FLAGS --unset=CMAKE_TOOLCHAIN_FILE ^
  cmake -S "%REPO_ROOT%" -B "%NO_TBB_DIR%" -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_TOOLCHAIN_FILE:FILEPATH= ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_VISION=ON ^
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF ^
  -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF ^
  -DSMARTPARALLEL_ENABLE_TBB=OFF ^
  -DSMARTPARALLEL_REQUIRE_TBB=OFF ^
  -DSMARTPARALLEL_INSTALL=ON
if errorlevel 1 goto :fail
findstr /C:"SMARTPARALLEL_ENABLE_TBB:BOOL=OFF" "%NO_TBB_DIR%\CMakeCache.txt" >nul
if errorlevel 1 (echo ERROR: no-oneTBB configure did not disable SMARTPARALLEL_ENABLE_TBB.& goto :fail)
findstr /C:"SMARTPARALLEL_ENABLE_OPENCV_PROVIDER:BOOL=OFF" "%NO_TBB_DIR%\CMakeCache.txt" >nul
if errorlevel 1 (echo ERROR: isolated configure did not disable OpenCV provider.& goto :fail)
cmake --build "%NO_TBB_DIR%"
if errorlevel 1 goto :fail
findstr /C:"SMARTPARALLEL_HAS_TBB=0" "%NO_TBB_DIR%\src\CMakeFiles\smart_parallel.dir\flags.make" >nul
if errorlevel 1 (echo ERROR: no-oneTBB build was not compiled with SMARTPARALLEL_HAS_TBB=0.& goto :fail)
findstr /C:"SMARTPARALLEL_VISION_HAS_OPENCV=0" "%NO_TBB_DIR%\vision\CMakeFiles\smart_parallel_vision.dir\flags.make" >nul
if errorlevel 1 (echo ERROR: isolated Vision build was not compiled with OpenCV disabled.& goto :fail)
ctest --test-dir "%NO_TBB_DIR%" -C Release --output-on-failure --parallel 2 ^
  --output-log "%OUTPUT_DIR%\ctest-no-tbb.log"
if errorlevel 1 goto :fail

 echo ==== [7/8] Record sanitized environment and source hashes ====
> "%ENVIRONMENT_FILE%" echo SmartParallel version: 1.6.0
>> "%ENVIRONMENT_FILE%" echo Run stamp: %RUN_STAMP%
>> "%ENVIRONMENT_FILE%" echo Repetitions: %REPETITIONS%
>> "%ENVIRONMENT_FILE%" echo CPU description used by benchmark: %SMARTPARALLEL_CPU_DESCRIPTION%
cl /Bv > "%OUTPUT_DIR%\cl-version.tmp" 2>&1
findstr /I /C:"Compiler Version" "%OUTPUT_DIR%\cl-version.tmp" >> "%ENVIRONMENT_FILE%"
if errorlevel 1 >> "%ENVIRONMENT_FILE%" echo Compiler: MSVC cl.exe (version line unavailable)
del /q "%OUTPUT_DIR%\cl-version.tmp" 2>nul
for /f "delims=" %%V in ('cmake --version ^| findstr /B /C:"cmake version"') do >> "%ENVIRONMENT_FILE%" echo CMake: %%V
powershell -NoProfile -Command "$os=Get-CimInstance Win32_OperatingSystem; 'OS: {0} {1} build {2}' -f $os.Caption,$os.Version,$os.BuildNumber" >> "%ENVIRONMENT_FILE%"
powershell -NoProfile -Command "$cpu=@(Get-CimInstance Win32_Processor)[0]; 'CPU: {0}' -f $cpu.Name.Trim()" >> "%ENVIRONMENT_FILE%"
powershell -NoProfile -Command "$cs=Get-CimInstance Win32_ComputerSystem; 'Logical processors: {0}' -f $cs.NumberOfLogicalProcessors; 'Physical memory bytes: {0}' -f $cs.TotalPhysicalMemory" >> "%ENVIRONMENT_FILE%"
>> "%ENVIRONMENT_FILE%" echo Architecture: x86_64
>> "%ENVIRONMENT_FILE%" echo Primary publication: oneTBB enabled, OpenCV enabled
>> "%ENVIRONMENT_FILE%" echo Secondary matrix: oneTBB disabled, OpenCV disabled
>> "%ENVIRONMENT_FILE%" echo Unsafe fast-math validation: disabled
%PYTHON_COMMAND% "%REPO_ROOT%\tools\generate_source_manifest.py" "%OUTPUT_DIR%\source-hashes.txt"
if errorlevel 1 goto :fail

 echo ==== [8/8] Clean and archive publication output ====
%PYTHON_COMMAND% "%REPO_ROOT%\tools\prepare_v16_publication_archive.py" "%OUTPUT_DIR%"
if errorlevel 1 goto :fail
%PYTHON_COMMAND% "%REPO_ROOT%\tools\create_reproducible_zip.py" ^
  "%OUTPUT_DIR%" "%OUTPUT_DIR%.zip" --root-name "publication_%RUN_STAMP%"
if errorlevel 1 goto :fail

 echo ============================================================
 echo SMARTPARALLEL V1.6 SCIENTIFIC FOUNDATIONS VALIDATION PASSED
 echo Raw samples: %RAW%
 echo Report: %OUTPUT_DIR%\v1.6.0_scientific_foundations_report.md
 echo Archive: %OUTPUT_DIR%.zip
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
echo SmartParallel v1.6 validation failed. Review the first error above.
exit /b 1
