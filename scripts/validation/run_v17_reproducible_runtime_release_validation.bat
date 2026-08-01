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

if not defined VCPKG_ROOT (
  echo ERROR: VCPKG_ROOT is required for the full oneTBB Windows matrix.
  exit /b 2
)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
  echo ERROR: invalid VCPKG_ROOT: %VCPKG_ROOT%
  exit /b 2
)
set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
set "VCPKG_INSTALL_ROOT=%REPO_ROOT%\vcpkg_installed"
set "VCPKG_TRIPLET=x64-windows"

set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=31"
for /f "delims=0123456789" %%A in ("%REPETITIONS%") do (echo ERROR: repetitions must be positive.& exit /b 2)
if %REPETITIONS% LEQ 0 (echo ERROR: repetitions must be positive.& exit /b 2)
set "MODE=%~2"
if not defined MODE set "MODE=full"
if /I not "%MODE%"=="full" if /I not "%MODE%"=="smoke" (echo ERROR: mode must be full or smoke.& exit /b 2)

for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "RUN_STAMP=%%I"
set "OUTPUT_DIR=%REPO_ROOT%\validation\output\v1.7.0_reproducible_runtime\publication_%RUN_STAMP%"
set "BUILD_DIR=%REPO_ROOT%\build\v17_reproducible_release"
set "NO_TBB_DIR=%REPO_ROOT%\build\v17_reproducible_no_tbb"
set "OPENCV_DIR=%REPO_ROOT%\build\v17_reproducible_opencv"
set "INSTALL_DIR=%OUTPUT_DIR%\install"
set "OPENCV_INSTALL_DIR=%OUTPUT_DIR%\opencv-install"
set "BENCHMARK_DIR=%OUTPUT_DIR%\v1.7.0_benchmarks"
set "V16_DIR=%OUTPUT_DIR%\v1.6.0_regression"
set "CLI_DIR=%OUTPUT_DIR%\cli-pilot"
set "SOURCE_ZIP=%OUTPUT_DIR%\SmartParallel-1.7.0-Reproducible-Runtime.zip"
rem Keep exact-ZIP source and build paths short. The archive contains retained
rem historical evidence paths; extracting below OUTPUT_DIR exceeds legacy
rem Windows path limits on otherwise valid archives.
set "EXACT_ROOT=%REPO_ROOT%\build\v17_exact_%RUN_STAMP%"
set "EXTRACT_DIR=%EXACT_ROOT%\src"
set "EXACT_BUILD=%EXACT_ROOT%\build"
set "EXACT_SOURCE=%EXTRACT_DIR%\SmartParallel-1.7.0"
set "CURRENT_STAGE=initialization"
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"
if not exist "%BENCHMARK_DIR%" mkdir "%BENCHMARK_DIR%"
if not exist "%V16_DIR%" mkdir "%V16_DIR%"

set "CURRENT_STAGE=[1/9] Configure MSVC Release publication build"
echo ==== !CURRENT_STAGE! ====
rmdir /s /q "%BUILD_DIR%" 2>nul
cmake -S "%REPO_ROOT%" -B "%BUILD_DIR%" -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
  -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
  -DVCPKG_MANIFEST_DIR="%REPO_ROOT%" ^
  -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALL_ROOT%" ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_BENCHMARKS=ON ^
  -DSMARTPARALLEL_BUILD_V170_BENCHMARKS=ON ^
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON ^
  -DSMARTPARALLEL_BUILD_VISION=ON ^
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF ^
  -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF ^
  -DSMARTPARALLEL_ENABLE_TBB=ON ^
  -DSMARTPARALLEL_REQUIRE_TBB=ON ^
  -DSMARTPARALLEL_INSTALL=ON
if not "!ERRORLEVEL!"=="0" goto :fail
cmake --build "%BUILD_DIR%"
if not "!ERRORLEVEL!"=="0" goto :fail
findstr /C:"SMARTPARALLEL_HAS_TBB=1" "%BUILD_DIR%\src\CMakeFiles\smart_parallel.dir\flags.make" >nul
if not "!ERRORLEVEL!"=="0" (echo ERROR: primary publication build was not compiled with oneTBB.& goto :fail)

set "CURRENT_STAGE=[2/9] Run complete v1.0-v1.7 regression"
echo ==== !CURRENT_STAGE! ====
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure --parallel 2 ^
  --output-log "%OUTPUT_DIR%\ctest-main.log"
if not "!ERRORLEVEL!"=="0" goto :fail

set "CURRENT_STAGE=[3/9] Run v1.7 and v1.6 publication benchmarks"
echo ==== !CURRENT_STAGE! ====
"%BUILD_DIR%\benchmarks\v1.7.0\smartparallel_v170_reproducible_runtime_benchmarks.exe" "%BENCHMARK_DIR%" "%REPETITIONS%"
if not "!ERRORLEVEL!"=="0" goto :fail
if /I "%MODE%"=="full" (
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v17_reproducible_runtime.py" "%BENCHMARK_DIR%\raw.csv" "%BENCHMARK_DIR%"
) else (
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\validate_benchmark_smoke.py" v1.7 "%BENCHMARK_DIR%\raw.csv" --minimum-repetitions "%REPETITIONS%"
)
if not "!ERRORLEVEL!"=="0" goto :fail
"%BUILD_DIR%\benchmarks\v1.6.0\smartparallel_v160_scientific_benchmarks.exe" "%V16_DIR%\raw.csv" "%REPETITIONS%"
if not "!ERRORLEVEL!"=="0" goto :fail
if /I "%MODE%"=="full" (
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v16_scientific_foundations.py" "%V16_DIR%\raw.csv" "%V16_DIR%"
) else (
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\validate_benchmark_smoke.py" v1.6 "%V16_DIR%\raw.csv" --minimum-repetitions "%REPETITIONS%"
)
if not "!ERRORLEVEL!"=="0" goto :fail

set "CURRENT_STAGE=[4/9] Validate documentation, install, and consumers"
echo ==== !CURRENT_STAGE! ====
%PYTHON_COMMAND% "%REPO_ROOT%\tools\check_documentation.py" > "%OUTPUT_DIR%\documentation-validation.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\documentation-validation.log"& goto :fail)
type "%OUTPUT_DIR%\documentation-validation.log"
cmake --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%"
if not "!ERRORLEVEL!"=="0" goto :fail
if not exist "%INSTALL_DIR%\bin\tbb*.dll" (
  echo ERROR: installed oneTBB runtime DLL is missing from %INSTALL_DIR%\bin.
  goto :fail
)
for %%C in (package-consumer package-consumer-profile package-consumer-vision) do (
  set "CONSUMER_BUILD=%OUTPUT_DIR%\%%C-build"
  rmdir /s /q "!CONSUMER_BUILD!" 2>nul
  cmake -S "%REPO_ROOT%\tests\%%C" -B "!CONSUMER_BUILD!" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALL_ROOT%" ^
    -DCMAKE_PREFIX_PATH="%INSTALL_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --build "!CONSUMER_BUILD!"
  if not "!ERRORLEVEL!"=="0" goto :fail
  ctest --test-dir "!CONSUMER_BUILD!" -C Release --output-on-failure ^
    --output-log "%OUTPUT_DIR%\%%C.log"
  if not "!ERRORLEVEL!"=="0" goto :fail
)

set "CURRENT_STAGE=[5/9] Exercise installed calibration, approval, and cross-process replay"
echo ==== !CURRENT_STAGE! ====
if not exist "%CLI_DIR%\calibration" mkdir "%CLI_DIR%\calibration"
> "%CLI_DIR%\calibration.json" echo {"schema_version":1,"operation":"heat_diffusion","rows":64,"columns":64,"iterations":8,"repetitions":3,"worker_budget":2,"seed":170,"numerical_policy":"Reproducible","output_directory":"%CLI_DIR:\=/%/calibration"}
"%INSTALL_DIR%\bin\smartparallel_calibrate.exe" "%CLI_DIR%\calibration.json" > "%CLI_DIR%\calibrate.log" 2>&1
if not "!ERRORLEVEL!"=="0" (echo ERROR: installed calibration failed.& type "%CLI_DIR%\calibrate.log"& goto :fail)
if not exist "%CLI_DIR%\calibration\candidate_profile.json" (
  echo ERROR: calibration completed without producing candidate_profile.json.
  type "%CLI_DIR%\calibrate.log"
  goto :fail
)
"%INSTALL_DIR%\bin\smartparallel_profile.exe" approve "%CLI_DIR%\calibration\candidate_profile.json" "%CLI_DIR%\approved_profile.json" > "%CLI_DIR%\approve.log" 2>&1
if not "!ERRORLEVEL!"=="0" (echo ERROR: installed profile approval failed.& type "%CLI_DIR%\approve.log"& goto :fail)
for /f %%H in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%CLI_DIR%\approved_profile.json').Hash.ToLowerInvariant()"') do set "PROFILE_SHA_BEFORE=%%H"
if not defined PROFILE_SHA_BEFORE (echo ERROR: could not hash Approved profile before replay.& goto :fail)
"%INSTALL_DIR%\bin\smartparallel_replay.exe" run "%CLI_DIR%\approved_profile.json" "%CLI_DIR%\replay-a.json" 64 64 8 2 170 > "%CLI_DIR%\replay-a.log" 2>&1
if not "!ERRORLEVEL!"=="0" (echo ERROR: deterministic replay A failed.& type "%CLI_DIR%\replay-a.log"& goto :fail)
"%INSTALL_DIR%\bin\smartparallel_replay.exe" run "%CLI_DIR%\approved_profile.json" "%CLI_DIR%\replay-b.json" 64 64 8 2 170 > "%CLI_DIR%\replay-b.log" 2>&1
if not "!ERRORLEVEL!"=="0" (echo ERROR: deterministic replay B failed.& type "%CLI_DIR%\replay-b.log"& goto :fail)
"%INSTALL_DIR%\bin\smartparallel_replay.exe" compare "%CLI_DIR%\replay-a.json" "%CLI_DIR%\replay-b.json" > "%CLI_DIR%\compare.log" 2>&1
if not "!ERRORLEVEL!"=="0" (echo ERROR: replay comparison failed.& type "%CLI_DIR%\compare.log"& goto :fail)
for /f %%H in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%CLI_DIR%\approved_profile.json').Hash.ToLowerInvariant()"') do set "PROFILE_SHA_AFTER=%%H"
if not defined PROFILE_SHA_AFTER (echo ERROR: could not hash Approved profile after replay.& goto :fail)
if not "!PROFILE_SHA_BEFORE!"=="!PROFILE_SHA_AFTER!" (echo ERROR: deterministic replay modified profile.& goto :fail)

set "CURRENT_STAGE=[6/9] Validate dependency matrices"
if /I "%MODE%"=="full" (
  echo ==== !CURRENT_STAGE!: no-oneTBB/no-OpenCV ====
  rmdir /s /q "%NO_TBB_DIR%" 2>nul
  cmake -E env --unset=VCPKG_ROOT --unset=VCPKG_INSTALLATION_ROOT --unset=VCPKG_FEATURE_FLAGS --unset=CMAKE_TOOLCHAIN_FILE ^
    cmake -S "%REPO_ROOT%" -B "%NO_TBB_DIR%" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_TOOLCHAIN_FILE:FILEPATH= ^
    -DSMARTPARALLEL_BUILD_VALIDATION=ON -DSMARTPARALLEL_BUILD_VISION=ON ^
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF ^
    -DSMARTPARALLEL_ENABLE_TBB=OFF -DSMARTPARALLEL_REQUIRE_TBB=OFF ^
    -DSMARTPARALLEL_INSTALL=ON
  if not "!ERRORLEVEL!"=="0" goto :fail
  findstr /C:"SMARTPARALLEL_ENABLE_TBB:BOOL=OFF" "%NO_TBB_DIR%\CMakeCache.txt" >nul
  if not "!ERRORLEVEL!"=="0" (echo ERROR: isolated configure did not disable oneTBB.& goto :fail)
  findstr /C:"SMARTPARALLEL_ENABLE_OPENCV_PROVIDER:BOOL=OFF" "%NO_TBB_DIR%\CMakeCache.txt" >nul
  if not "!ERRORLEVEL!"=="0" (echo ERROR: isolated configure did not disable OpenCV.& goto :fail)
  cmake --build "%NO_TBB_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  findstr /C:"SMARTPARALLEL_HAS_TBB=0" "%NO_TBB_DIR%\src\CMakeFiles\smart_parallel.dir\flags.make" >nul
  if not "!ERRORLEVEL!"=="0" (echo ERROR: isolated build was not compiled with SMARTPARALLEL_HAS_TBB=0.& goto :fail)
  findstr /C:"SMARTPARALLEL_VISION_HAS_OPENCV=0" "%NO_TBB_DIR%\vision\CMakeFiles\smart_parallel_vision.dir\flags.make" >nul
  if not "!ERRORLEVEL!"=="0" (echo ERROR: isolated Vision build was not compiled with OpenCV disabled.& goto :fail)
  ctest --test-dir "%NO_TBB_DIR%" -C Release --output-on-failure --parallel 2 ^
    --output-log "%OUTPUT_DIR%\ctest-no-tbb.log"
  if not "!ERRORLEVEL!"=="0" goto :fail

  echo ==== !CURRENT_STAGE!: oneTBB/OpenCV provider ====
  rmdir /s /q "%OPENCV_DIR%" 2>nul
  rmdir /s /q "%OPENCV_INSTALL_DIR%" 2>nul
  cmake -S "%REPO_ROOT%" -B "%OPENCV_DIR%" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
    -DVCPKG_MANIFEST_DIR="%REPO_ROOT%" ^
    -DVCPKG_MANIFEST_FEATURES=vision-opencv ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALL_ROOT%" ^
    -DSMARTPARALLEL_BUILD_V150_VISION_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_V170_TOOLS=OFF ^
    -DSMARTPARALLEL_BUILD_VISION=ON ^
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON ^
    -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON ^
    -DSMARTPARALLEL_ENABLE_TBB=ON ^
    -DSMARTPARALLEL_REQUIRE_TBB=ON ^
    -DSMARTPARALLEL_INSTALL=ON
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --build "%OPENCV_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  findstr /C:"SMARTPARALLEL_VISION_HAS_OPENCV=1" "%OPENCV_DIR%\vision\CMakeFiles\smart_parallel_vision.dir\flags.make" >nul
  if not "!ERRORLEVEL!"=="0" (echo ERROR: provider matrix was not compiled with OpenCV enabled.& goto :fail)
  ctest --test-dir "%OPENCV_DIR%" -C Release --output-on-failure ^
    -R "smartparallel_v150_vision|smartparallel_v160_vision|smartparallel_v170_vision" ^
    --output-log "%OUTPUT_DIR%\ctest-opencv.log"
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --install "%OPENCV_DIR%" --prefix "%OPENCV_INSTALL_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  set "OPENCV_CONSUMER_BUILD=%OUTPUT_DIR%\package-consumer-vision-opencv-build"
  rmdir /s /q "!OPENCV_CONSUMER_BUILD!" 2>nul
  cmake -S "%REPO_ROOT%\tests\package-consumer-vision" -B "!OPENCV_CONSUMER_BUILD!" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
    -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_INSTALL_ROOT%" ^
    -DCMAKE_PREFIX_PATH="%OPENCV_INSTALL_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --build "!OPENCV_CONSUMER_BUILD!"
  if not "!ERRORLEVEL!"=="0" goto :fail
  ctest --test-dir "!OPENCV_CONSUMER_BUILD!" -C Release --output-on-failure ^
    --output-log "%OUTPUT_DIR%\package-consumer-vision-opencv.log"
  if not "!ERRORLEVEL!"=="0" goto :fail
) else (
  echo ==== !CURRENT_STAGE! skipped in smoke mode ====
)

set "CURRENT_STAGE=[7/9] Create deterministic source ZIP"
echo ==== !CURRENT_STAGE! ====
%PYTHON_COMMAND% "%REPO_ROOT%\tools\create_source_release_zip.py" "%SOURCE_ZIP%" --root-name SmartParallel-1.7.0
if not "!ERRORLEVEL!"=="0" goto :fail
powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%SOURCE_ZIP%').Hash.ToLowerInvariant() + '  SmartParallel-1.7.0-Reproducible-Runtime.zip'" > "%OUTPUT_DIR%\source-zip.sha256"
if not "!ERRORLEVEL!"=="0" goto :fail

set "CURRENT_STAGE=[8/9] Rebuild and retest exact source ZIP"
echo ==== !CURRENT_STAGE! ====
rmdir /s /q "%EXACT_ROOT%" 2>nul
cmake -E make_directory "%EXTRACT_DIR%"
if not "!ERRORLEVEL!"=="0" goto :fail
cmake -E chdir "%EXTRACT_DIR%" cmake -E tar xvf "%SOURCE_ZIP%" > "%OUTPUT_DIR%\exact-zip-extract.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\exact-zip-extract.log"& goto :fail)
if not exist "%EXACT_SOURCE%\CMakeLists.txt" (echo ERROR: exact source ZIP did not extract the expected root.& goto :fail)
%PYTHON_COMMAND% "%EXACT_SOURCE%\tools\verify_source_manifest.py" > "%OUTPUT_DIR%\exact-zip-manifest.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\exact-zip-manifest.log"& goto :fail)
cmake -E env --unset=VCPKG_ROOT --unset=VCPKG_INSTALLATION_ROOT --unset=VCPKG_FEATURE_FLAGS --unset=CMAKE_TOOLCHAIN_FILE ^
  cmake -S "%EXACT_SOURCE%" -B "%EXACT_BUILD%" -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_TOOLCHAIN_FILE:FILEPATH= ^
  -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION=ON ^
  -DSMARTPARALLEL_BUILD_V170_TOOLS=ON ^
  -DSMARTPARALLEL_BUILD_V170_BENCHMARKS=ON ^
  -DSMARTPARALLEL_BUILD_VISION=ON ^
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF ^
  -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF ^
  -DSMARTPARALLEL_ENABLE_TBB=OFF ^
  -DSMARTPARALLEL_REQUIRE_TBB=OFF ^
  -DSMARTPARALLEL_INSTALL=ON
if not "!ERRORLEVEL!"=="0" goto :fail
cmake --build "%EXACT_BUILD%"
if not "!ERRORLEVEL!"=="0" goto :fail
findstr /C:"SMARTPARALLEL_HAS_TBB=0" "%EXACT_BUILD%\src\CMakeFiles\smart_parallel.dir\flags.make" >nul
if not "!ERRORLEVEL!"=="0" (echo ERROR: exact-ZIP build unexpectedly enabled oneTBB.& goto :fail)
findstr /C:"SMARTPARALLEL_VISION_HAS_OPENCV=0" "%EXACT_BUILD%\vision\CMakeFiles\smart_parallel_vision.dir\flags.make" >nul
if not "!ERRORLEVEL!"=="0" (echo ERROR: exact-ZIP build unexpectedly enabled OpenCV.& goto :fail)
ctest --test-dir "%EXACT_BUILD%" -C Release --output-on-failure ^
  --output-log "%OUTPUT_DIR%\ctest-exact-zip.log"
if not "!ERRORLEVEL!"=="0" goto :fail
"%EXACT_BUILD%\benchmarks\v1.7.0\smartparallel_v170_reproducible_runtime_benchmarks.exe" "%OUTPUT_DIR%\exact-zip-benchmark" 3
if not "!ERRORLEVEL!"=="0" goto :fail
%PYTHON_COMMAND% "%EXACT_SOURCE%\tools\validate_benchmark_smoke.py" v1.7 "%OUTPUT_DIR%\exact-zip-benchmark\raw.csv" --minimum-repetitions 3
if not "!ERRORLEVEL!"=="0" goto :fail
%PYTHON_COMMAND% "%EXACT_SOURCE%\tools\check_documentation.py" > "%OUTPUT_DIR%\exact-zip-documentation.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\exact-zip-documentation.log"& goto :fail)
rmdir /s /q "%EXACT_ROOT%" 2>nul

set "CURRENT_STAGE=[9/9] Validation complete"
echo ==== !CURRENT_STAGE! ====
echo ============================================================
echo SMARTPARALLEL V1.7 REPRODUCIBLE RUNTIME VALIDATION PASSED
echo Benchmark report: %BENCHMARK_DIR%\report.md
echo Cross-process pilot: %CLI_DIR%
echo Source ZIP: %SOURCE_ZIP%
echo SHA-256 file: %OUTPUT_DIR%\source-zip.sha256
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
echo.
echo SmartParallel v1.7 validation failed during !CURRENT_STAGE!.
echo Evidence directory: %OUTPUT_DIR%
echo Review the first error above and the stage-specific logs.
exit /b 1
