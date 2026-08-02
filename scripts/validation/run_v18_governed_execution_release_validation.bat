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
if not "!ERRORLEVEL!"=="0" call :initialize_msvc
if not "!ERRORLEVEL!"=="0" (echo ERROR: MSVC cl.exe not found.& exit /b 2)

if not defined VCPKG_ROOT (echo ERROR: VCPKG_ROOT is required.& exit /b 2)
if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (echo ERROR: invalid VCPKG_ROOT.& exit /b 2)
set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
set "VCPKG_INSTALL_ROOT=%REPO_ROOT%\vcpkg_installed"
set "VCPKG_OPENCV_INSTALL_ROOT=%REPO_ROOT%\build\v18_vcpkg_opencv"
set "VCPKG_TRIPLET=x64-windows"

set "REPETITIONS=%~1"
if not defined REPETITIONS set "REPETITIONS=31"
for /f "delims=0123456789" %%A in ("%REPETITIONS%") do (echo ERROR: repetitions must be positive.& exit /b 2)
if %REPETITIONS% LEQ 0 (echo ERROR: repetitions must be positive.& exit /b 2)
set "MODE=%~2"
if not defined MODE set "MODE=full"
if /I not "%MODE%"=="full" if /I not "%MODE%"=="smoke" (echo ERROR: mode must be full or smoke.& exit /b 2)
if /I "%MODE%"=="full" (
  set /a "REPETITION_PARITY=REPETITIONS%%2"
  if not "!REPETITION_PARITY!"=="1" (echo ERROR: full publication repetitions must be odd for the retained v1.5 paired-order matrix.& exit /b 2)
)

for /f %%I in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "RUN_STAMP=%%I"
set "OUTPUT_DIR=%REPO_ROOT%\validation\output\v1.8.0_governed_execution\publication_%RUN_STAMP%"
set "BUILD_DIR=%REPO_ROOT%\build\v18_governed_release"
set "NO_DEP_DIR=%REPO_ROOT%\build\v18_governed_no_dependencies"
set "OPENCV_DIR=%REPO_ROOT%\build\v18_governed_opencv"
set "INSTALL_DIR=%OUTPUT_DIR%\install"
set "OPENCV_INSTALL_DIR=%OUTPUT_DIR%\opencv-install"
set "V18_DIR=%OUTPUT_DIR%\v1.8.0_governed_execution"
set "V17_DIR=%OUTPUT_DIR%\v1.7.0_regression"
set "V16_DIR=%OUTPUT_DIR%\v1.6.0_regression"
set "V15_DIR=%OUTPUT_DIR%\v1.5.0_regression"
set "CLI_DIR=%OUTPUT_DIR%\cli-pilot"
set "SOURCE_ZIP=%OUTPUT_DIR%\SmartParallel-1.8.0-Governed-Scientific-Execution.zip"
set "SOURCE_ZIP_2=%OUTPUT_DIR%\SmartParallel-1.8.0-reproducibility-check.zip"
set "EXACT_ROOT=%REPO_ROOT%\build\v18_exact_%RUN_STAMP%"
set "EXTRACT_DIR=%EXACT_ROOT%\src"
set "EXACT_BUILD=%EXACT_ROOT%\build"
set "EXACT_OPENCV_BUILD=%EXACT_ROOT%\build-opencv"
set "EXACT_SOURCE=%EXTRACT_DIR%\SmartParallel-1.8.0"
set "CURRENT_STAGE=initialization"
for %%D in ("%OUTPUT_DIR%" "%V18_DIR%" "%V17_DIR%" "%V16_DIR%" "%V15_DIR%") do if not exist "%%~D" mkdir "%%~D"

set "CURRENT_STAGE=[1/10] Configure and build MSVC Release publication tree"
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
  -DSMARTPARALLEL_BUILD_V180_BENCHMARKS=ON ^
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
if not "!ERRORLEVEL!"=="0" (echo ERROR: primary build did not enable oneTBB.& goto :fail)

set "CURRENT_STAGE=[2/10] Run complete v1.0-v1.8 regression"
echo ==== !CURRENT_STAGE! ====
ctest --test-dir "%BUILD_DIR%" -C Release --output-on-failure --parallel 2 --output-log "%OUTPUT_DIR%\ctest-main.log"
if not "!ERRORLEVEL!"=="0" goto :fail

set "CURRENT_STAGE=[3/10] Run v1.6-v1.8 publication benchmarks"
echo ==== !CURRENT_STAGE! ====
rem Recreate publication writers' directories after the historical regression.
rem No test is permitted to make a later successful stage fail through stale cleanup.
for %%D in ("%OUTPUT_DIR%" "%V18_DIR%" "%V17_DIR%" "%V16_DIR%" "%V15_DIR%" "%CLI_DIR%") do if not exist "%%~D" mkdir "%%~D"
"%BUILD_DIR%\benchmarks\v1.6.0\smartparallel_v160_scientific_benchmarks.exe" "%V16_DIR%\raw.csv" "%REPETITIONS%"
if not "!ERRORLEVEL!"=="0" goto :fail
"%BUILD_DIR%\benchmarks\v1.7.0\smartparallel_v170_reproducible_runtime_benchmarks.exe" "%V17_DIR%" "%REPETITIONS%"
if not "!ERRORLEVEL!"=="0" goto :fail
"%BUILD_DIR%\benchmarks\v1.8.0\smartparallel_v180_governed_execution_benchmarks.exe" "%V18_DIR%" "%REPETITIONS%"
if not "!ERRORLEVEL!"=="0" goto :fail
if /I "%MODE%"=="full" (
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v16_scientific_foundations.py" "%V16_DIR%\raw.csv" "%V16_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v17_reproducible_runtime.py" "%V17_DIR%\raw.csv" "%V17_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
) else (
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\validate_benchmark_smoke.py" v1.6 "%V16_DIR%\raw.csv" --minimum-repetitions "%REPETITIONS%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\validate_benchmark_smoke.py" v1.7 "%V17_DIR%\raw.csv" --minimum-repetitions "%REPETITIONS%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\validate_benchmark_smoke.py" v1.8 "%V18_DIR%\raw.csv" --minimum-repetitions "%REPETITIONS%"
  if not "!ERRORLEVEL!"=="0" goto :fail
)

set "CURRENT_STAGE=[4/10] Install and validate consumers and replay"
echo ==== !CURRENT_STAGE! ====
cmake --install "%BUILD_DIR%" --prefix "%INSTALL_DIR%"
if not "!ERRORLEVEL!"=="0" goto :fail
if not exist "%INSTALL_DIR%\bin\tbb*.dll" (echo ERROR: installed oneTBB runtime DLL missing.& goto :fail)
for %%C in (package-consumer package-consumer-profile package-consumer-vision package-consumer-governor package-consumer-deterministic package-consumer-nested) do (
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
  ctest --test-dir "!CONSUMER_BUILD!" -C Release --output-on-failure --output-log "%OUTPUT_DIR%\%%C.log"
  if not "!ERRORLEVEL!"=="0" goto :fail
)
if not exist "%CLI_DIR%\calibration" mkdir "%CLI_DIR%\calibration"
> "%CLI_DIR%\calibration.json" echo {"schema_version":1,"operation":"heat_diffusion","rows":64,"columns":64,"iterations":8,"repetitions":3,"worker_budget":2,"seed":180,"numerical_policy":"Reproducible","output_directory":"%CLI_DIR:\=/%/calibration"}
"%INSTALL_DIR%\bin\smartparallel_calibrate.exe" "%CLI_DIR%\calibration.json" > "%CLI_DIR%\calibrate.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%CLI_DIR%\calibrate.log"& goto :fail)
"%INSTALL_DIR%\bin\smartparallel_profile.exe" approve "%CLI_DIR%\calibration\candidate_profile.json" "%CLI_DIR%\approved_profile.json" > "%CLI_DIR%\approve.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%CLI_DIR%\approve.log"& goto :fail)
for /f %%H in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%CLI_DIR%\approved_profile.json').Hash.ToLowerInvariant()"') do set "PROFILE_SHA_BEFORE=%%H"
"%INSTALL_DIR%\bin\smartparallel_replay.exe" run "%CLI_DIR%\approved_profile.json" "%CLI_DIR%\replay-a.json" 64 64 8 2 180 > "%CLI_DIR%\replay-a.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%CLI_DIR%\replay-a.log"& goto :fail)
"%INSTALL_DIR%\bin\smartparallel_replay.exe" run "%CLI_DIR%\approved_profile.json" "%CLI_DIR%\replay-b.json" 64 64 8 2 180 > "%CLI_DIR%\replay-b.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%CLI_DIR%\replay-b.log"& goto :fail)
"%INSTALL_DIR%\bin\smartparallel_replay.exe" compare "%CLI_DIR%\replay-a.json" "%CLI_DIR%\replay-b.json" > "%CLI_DIR%\compare.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%CLI_DIR%\compare.log"& goto :fail)
for /f %%H in ('powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%CLI_DIR%\approved_profile.json').Hash.ToLowerInvariant()"') do set "PROFILE_SHA_AFTER=%%H"
if not "!PROFILE_SHA_BEFORE!"=="!PROFILE_SHA_AFTER!" (echo ERROR: deterministic replay modified Approved profile.& goto :fail)

set "CURRENT_STAGE=[5/10] Validate no-dependency and OpenCV matrices"
if /I "%MODE%"=="full" (
  echo ==== !CURRENT_STAGE!: no-oneTBB/no-OpenCV ====
  rmdir /s /q "%NO_DEP_DIR%" 2>nul
  cmake -E env --unset=VCPKG_ROOT --unset=VCPKG_INSTALLATION_ROOT --unset=VCPKG_FEATURE_FLAGS --unset=CMAKE_TOOLCHAIN_FILE ^
    cmake -S "%REPO_ROOT%" -B "%NO_DEP_DIR%" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_TOOLCHAIN_FILE:FILEPATH= ^
    -DSMARTPARALLEL_BUILD_VALIDATION=ON -DSMARTPARALLEL_BUILD_VISION=ON ^
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF ^
    -DSMARTPARALLEL_ENABLE_TBB=OFF -DSMARTPARALLEL_REQUIRE_TBB=OFF -DSMARTPARALLEL_INSTALL=ON
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --build "%NO_DEP_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  ctest --test-dir "%NO_DEP_DIR%" -C Release --output-on-failure --parallel 2 --output-log "%OUTPUT_DIR%\ctest-no-dependencies.log"
  if not "!ERRORLEVEL!"=="0" goto :fail

  echo ==== !CURRENT_STAGE!: oneTBB/OpenCV/v1.5 measured regression ====
  rmdir /s /q "%OPENCV_DIR%" 2>nul
  rmdir /s /q "%OPENCV_INSTALL_DIR%" 2>nul
  cmake -S "%REPO_ROOT%" -B "%OPENCV_DIR%" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
    -DVCPKG_MANIFEST_DIR="%REPO_ROOT%" -DVCPKG_MANIFEST_FEATURES=vision-opencv ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_OPENCV_INSTALL_ROOT%" ^
    -DSMARTPARALLEL_BUILD_V150_VISION_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_V150_VISION_BENCHMARKS=ON ^
    -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_V180_GOVERNED_EXECUTION_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_VISION=ON ^
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON ^
    -DSMARTPARALLEL_ENABLE_TBB=ON -DSMARTPARALLEL_REQUIRE_TBB=ON -DSMARTPARALLEL_INSTALL=ON
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --build "%OPENCV_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  ctest --test-dir "%OPENCV_DIR%" -C Release --output-on-failure ^
    -R "smartparallel_v150_vision|smartparallel_v160_vision|smartparallel_v170_vision|smartparallel_v180_opencv" ^
    --output-log "%OUTPUT_DIR%\ctest-opencv.log"
  if not "!ERRORLEVEL!"=="0" goto :fail
  "%OPENCV_DIR%\benchmarks\v1.5.0\smartparallel_v150_adaptive_routes_benchmarks.exe" "%V15_DIR%\raw.csv" "%REPETITIONS%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v15_adaptive_routes.py" "%V15_DIR%\raw.csv" "%V15_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  %PYTHON_COMMAND% "%REPO_ROOT%\tools\summarize_v15_regression.py" "%V15_DIR%\raw.csv" "%V15_DIR%\v1.5.0_adaptive_routes.csv" "%V15_DIR%\metrics.json"
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --install "%OPENCV_DIR%" --prefix "%OPENCV_INSTALL_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  set "OPENCV_CONSUMER_BUILD=%OUTPUT_DIR%\package-consumer-vision-opencv-build"
  rmdir /s /q "!OPENCV_CONSUMER_BUILD!" 2>nul
  cmake -S "%REPO_ROOT%\tests\package-consumer-vision" -B "!OPENCV_CONSUMER_BUILD!" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_OPENCV_INSTALL_ROOT%" -DCMAKE_PREFIX_PATH="%OPENCV_INSTALL_DIR%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --build "!OPENCV_CONSUMER_BUILD!"
  if not "!ERRORLEVEL!"=="0" goto :fail
  ctest --test-dir "!OPENCV_CONSUMER_BUILD!" -C Release --output-on-failure --output-log "%OUTPUT_DIR%\package-consumer-vision-opencv.log"
  if not "!ERRORLEVEL!"=="0" goto :fail
) else (
  echo ==== !CURRENT_STAGE! skipped in smoke mode ====
)

set "CURRENT_STAGE=[6/10] Analyze v1.8 evidence and validate documentation"
echo ==== !CURRENT_STAGE! ====
if /I "%MODE%"=="full" (
  set "LINUX_RAW=%REPO_ROOT%\docs\v1.8\assets\benchmarks\linux-gcc-accepted\raw.csv"
  if exist "!LINUX_RAW!" (
    %PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v18_governed_execution.py" "%V18_DIR%\raw.csv" "%V18_DIR%" ^
      --comparison-raw "!LINUX_RAW!" --v15-metrics "%V15_DIR%\metrics.json" ^
      --v16-metrics "%V16_DIR%\v1.6.0_scientific_metrics.json" --v17-metrics "%V17_DIR%\metrics.json"
  ) else (
    %PYTHON_COMMAND% "%REPO_ROOT%\tools\analyze_v18_governed_execution.py" "%V18_DIR%\raw.csv" "%V18_DIR%" ^
      --v15-metrics "%V15_DIR%\metrics.json" --v16-metrics "%V16_DIR%\v1.6.0_scientific_metrics.json" ^
      --v17-metrics "%V17_DIR%\metrics.json"
  )
  if not "!ERRORLEVEL!"=="0" goto :fail
)
%PYTHON_COMMAND% "%REPO_ROOT%\tools\check_documentation.py" > "%OUTPUT_DIR%\documentation-validation.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\documentation-validation.log"& goto :fail)
type "%OUTPUT_DIR%\documentation-validation.log"

set "CURRENT_STAGE=[7/10] Create reproducible deterministic source ZIP"
echo ==== !CURRENT_STAGE! ====
%PYTHON_COMMAND% "%REPO_ROOT%\tools\create_source_release_zip.py" "%SOURCE_ZIP%" --root-name SmartParallel-1.8.0
if not "!ERRORLEVEL!"=="0" goto :fail
%PYTHON_COMMAND% "%REPO_ROOT%\tools\create_source_release_zip.py" "%SOURCE_ZIP_2%" --root-name SmartParallel-1.8.0
if not "!ERRORLEVEL!"=="0" goto :fail
fc /b "%SOURCE_ZIP%" "%SOURCE_ZIP_2%" >nul
if not "!ERRORLEVEL!"=="0" (echo ERROR: independent source ZIP generations differ.& goto :fail)
del /q "%SOURCE_ZIP_2%"
%PYTHON_COMMAND% "%REPO_ROOT%\tools\verify_source_manifest.py" > "%OUTPUT_DIR%\source-manifest-verification.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\source-manifest-verification.log"& goto :fail)
powershell -NoProfile -Command "(Get-FileHash -Algorithm SHA256 '%SOURCE_ZIP%').Hash.ToLowerInvariant() + '  SmartParallel-1.8.0-Governed-Scientific-Execution.zip'" > "%OUTPUT_DIR%\source-zip.sha256"
if not "!ERRORLEVEL!"=="0" goto :fail

set "CURRENT_STAGE=[8/10] Rebuild and test exact returned ZIP"
echo ==== !CURRENT_STAGE! ====
rmdir /s /q "%EXACT_ROOT%" 2>nul
cmake -E make_directory "%EXTRACT_DIR%"
if not "!ERRORLEVEL!"=="0" goto :fail
cmake -E chdir "%EXTRACT_DIR%" cmake -E tar xvf "%SOURCE_ZIP%" > "%OUTPUT_DIR%\exact-zip-extract.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\exact-zip-extract.log"& goto :fail)
%PYTHON_COMMAND% "%EXACT_SOURCE%\tools\verify_source_manifest.py" > "%OUTPUT_DIR%\exact-zip-manifest.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\exact-zip-manifest.log"& goto :fail)
cmake -E env --unset=VCPKG_ROOT --unset=VCPKG_INSTALLATION_ROOT --unset=VCPKG_FEATURE_FLAGS --unset=CMAKE_TOOLCHAIN_FILE ^
  cmake -S "%EXACT_SOURCE%" -B "%EXACT_BUILD%" -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl -DCMAKE_TOOLCHAIN_FILE:FILEPATH= ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON -DSMARTPARALLEL_BUILD_V180_BENCHMARKS=ON ^
  -DSMARTPARALLEL_BUILD_VISION=ON -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF ^
  -DSMARTPARALLEL_ENABLE_TBB=OFF -DSMARTPARALLEL_INSTALL=ON
if not "!ERRORLEVEL!"=="0" goto :fail
cmake --build "%EXACT_BUILD%"
if not "!ERRORLEVEL!"=="0" goto :fail
ctest --test-dir "%EXACT_BUILD%" -C Release --output-on-failure --parallel 2 --output-log "%OUTPUT_DIR%\ctest-exact-zip.log"
if not "!ERRORLEVEL!"=="0" goto :fail
"%EXACT_BUILD%\benchmarks\v1.8.0\smartparallel_v180_governed_execution_benchmarks.exe" "%OUTPUT_DIR%\exact-zip-benchmark" 3
if not "!ERRORLEVEL!"=="0" goto :fail
%PYTHON_COMMAND% "%EXACT_SOURCE%\tools\validate_benchmark_smoke.py" v1.8 "%OUTPUT_DIR%\exact-zip-benchmark\raw.csv" --minimum-repetitions 3
if not "!ERRORLEVEL!"=="0" goto :fail
%PYTHON_COMMAND% "%EXACT_SOURCE%\tools\check_documentation.py" > "%OUTPUT_DIR%\exact-zip-documentation.log" 2>&1
if not "!ERRORLEVEL!"=="0" (type "%OUTPUT_DIR%\exact-zip-documentation.log"& goto :fail)

set "CURRENT_STAGE=[9/10] Validate exact ZIP oneTBB/OpenCV paths"
if /I "%MODE%"=="full" (
  echo ==== !CURRENT_STAGE! ====
  rmdir /s /q "%EXACT_OPENCV_BUILD%" 2>nul
  cmake -S "%EXACT_SOURCE%" -B "%EXACT_OPENCV_BUILD%" -G "NMake Makefiles" ^
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
    -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" -DVCPKG_TARGET_TRIPLET=%VCPKG_TRIPLET% ^
    -DVCPKG_MANIFEST_DIR="%EXACT_SOURCE%" -DVCPKG_MANIFEST_FEATURES=vision-opencv ^
    -DVCPKG_INSTALLED_DIR="%VCPKG_OPENCV_INSTALL_ROOT%" ^
    -DSMARTPARALLEL_BUILD_V150_VISION_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_V180_GOVERNED_EXECUTION_VALIDATION=ON ^
    -DSMARTPARALLEL_BUILD_VISION=ON -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON ^
    -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON -DSMARTPARALLEL_ENABLE_TBB=ON ^
    -DSMARTPARALLEL_REQUIRE_TBB=ON -DSMARTPARALLEL_INSTALL=OFF
  if not "!ERRORLEVEL!"=="0" goto :fail
  cmake --build "%EXACT_OPENCV_BUILD%"
  if not "!ERRORLEVEL!"=="0" goto :fail
  ctest --test-dir "%EXACT_OPENCV_BUILD%" -C Release --output-on-failure ^
    -R "smartparallel_v150_vision|smartparallel_v180_" --output-log "%OUTPUT_DIR%\ctest-exact-opencv.log"
  if not "!ERRORLEVEL!"=="0" goto :fail
) else (
  echo ==== !CURRENT_STAGE! skipped in smoke mode ====
)

set "CURRENT_STAGE=[10/10] Validation complete"
echo ==== !CURRENT_STAGE! ====
echo ============================================================
echo SMARTPARALLEL V1.8 GOVERNED SCIENTIFIC EXECUTION VALIDATION PASSED
echo Evidence directory: %OUTPUT_DIR%
echo Benchmark report: %V18_DIR%\report.md
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
echo SmartParallel v1.8 validation failed during !CURRENT_STAGE!.
echo Evidence directory: %OUTPUT_DIR%
echo Review the first error above and the stage-specific logs.
exit /b 1
