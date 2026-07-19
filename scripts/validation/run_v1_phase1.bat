@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem SmartParallel V1 Phase 1 — one-command build, measurement, data audit, and utility-learning readiness gate.
rem Run from a Visual Studio Developer Command Prompt.

for %%I in ("%~dp0..\..") do set "REPO_ROOT=%%~fI"
cd /d "%REPO_ROOT%"

where cmake >nul 2>nul || (echo ERROR: cmake was not found in PATH.& exit /b 2)
where py >nul 2>nul
if errorlevel 1 (
  where python >nul 2>nul || (echo ERROR: Python 3 was not found in PATH.& exit /b 2)
  set "PYTHON=python"
) else (
  set "PYTHON=py -3"
)
where cl >nul 2>nul || (echo ERROR: MSVC compiler "cl" was not found. Use a VS Developer Command Prompt.& exit /b 2)

set "TBB_CONFIG=%CD%\vcpkg_installed\x64-windows\share\tbb"
if not exist "%TBB_CONFIG%\TBBConfig.cmake" (
  echo ERROR: oneTBB was not found at:
  echo   %TBB_CONFIG%
  echo Restore vcpkg dependencies before running Phase 1.
  exit /b 2
)

echo(
echo ==== [1/6] Clean Phase 1 outputs ====
rmdir /s /q build_v1_phase1 2>nul
rmdir /s /q validation\phase1 2>nul
mkdir validation\phase1 >nul 2>nul
del /q validation\output\prediction_candidates.csv 2>nul
del /q validation\output\prediction_summary.csv 2>nul
del /q validation\output\prediction_metrics.csv 2>nul
del /q validation\output\holdout_candidates.csv 2>nul
del /q validation\output\holdout_summary.csv 2>nul
del /q validation\output\holdout_metrics.csv 2>nul

echo(
echo ==== [2/6] Configure Release build ====
cmake -S . -B build_v1_phase1 ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DTBB_DIR="%TBB_CONFIG%" ^
  -DSMARTPARALLEL_BUILD_V1_PHASE1=ON
if errorlevel 1 goto :fail

echo(
echo ==== [3/6] Build and unit-test decision primitives ====
cmake --build build_v1_phase1
if errorlevel 1 goto :fail
pushd build_v1_phase1
ctest -R smartparallel_v1_phase1 --output-on-failure
if errorlevel 1 (popd & goto :fail)
popd

echo(
echo ==== [4/6] Regenerate full-information calibration and holdout data ====
del /q smartparallel_experience.db* 2>nul
build_v1_phase1\smartparallel_calibration_dataset.exe
set "CALIBRATION_EXIT=!ERRORLEVEL!"
if not "!CALIBRATION_EXIT!"=="0" (
  echo ERROR: calibration executable exited with code !CALIBRATION_EXIT!.
  goto :fail
)

if not exist validation\output\prediction_summary.csv (
  echo ERROR: calibration summary CSV was not generated.
  goto :fail
)
set "CALIBRATION_LINES="
for /f %%A in ('find /c /v "" ^< validation\output\prediction_summary.csv') do set "CALIBRATION_LINES=%%A"
if not "!CALIBRATION_LINES!"=="101" (
  set /a CALIBRATION_ROWS=!CALIBRATION_LINES!-1
  echo ERROR: calibration dataset is incomplete. Expected 100 data rows, found !CALIBRATION_ROWS!.
  goto :fail
)

build_v1_phase1\smartparallel_holdout_dataset.exe
set "HOLDOUT_EXIT=!ERRORLEVEL!"
if not "!HOLDOUT_EXIT!"=="0" (
  echo ERROR: holdout executable exited with code !HOLDOUT_EXIT!.
  goto :fail
)

if not exist validation\output\prediction_candidates.csv (
  echo ERROR: calibration candidate CSV was not generated.
  goto :fail
)
if not exist validation\output\holdout_candidates.csv (
  echo ERROR: holdout candidate CSV was not generated.
  goto :fail
)

echo(
echo ==== [5/6] Audit production-safe decision features ====
%PYTHON% tools\phase1_dataset_audit.py ^
  --train validation\output\prediction_candidates.csv ^
  --holdout validation\output\holdout_candidates.csv ^
  --output validation\phase1\dataset_audit.json ^
  --require-ready
if errorlevel 1 goto :fail

echo(
echo ==== [6/6] Assess utility-learning readiness ====
%PYTHON% tools\phase1_regret_ranker.py ^
  --train validation\output\prediction_candidates.csv ^
  --holdout validation\output\holdout_candidates.csv ^
  --output validation\phase1 ^
  --min-training-groups 100
if errorlevel 1 goto :fail

echo(
echo ============================================================
echo Phase 1 validation completed.
echo Read: validation\phase1\PHASE1_RESULT.md
echo Metrics: validation\phase1\phase1_comparison.csv
echo Model and promotion status: validation\phase1\phase1_metrics.json
echo ============================================================
exit /b 0

:fail
echo(
echo Phase 1 failed. Review the first error above.
exit /b 1
