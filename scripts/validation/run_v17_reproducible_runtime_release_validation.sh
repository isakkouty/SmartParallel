#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
REPETITIONS=${1:-11}
MODE=${2:-full}
RUN_STAMP=$(date +%Y%m%d_%H%M%S)
BUILD_JOBS=${SMARTPARALLEL_BUILD_JOBS:-2}
OUTPUT_DIR="$REPO_ROOT/validation/output/v1.7.0_reproducible_runtime/publication_$RUN_STAMP"
BUILD_DIR="$REPO_ROOT/build/v17_reproducible_release"
NO_TBB_DIR="$REPO_ROOT/build/v17_reproducible_no_tbb"
SANITIZER_DIR="$REPO_ROOT/build/v17_reproducible_sanitizers"
CLANG_DIR="$REPO_ROOT/build/v17_reproducible_clang"
SHARED_DIR="$REPO_ROOT/build/v17_reproducible_shared"
SHARED_INSTALL_DIR="$OUTPUT_DIR/shared-install"
INSTALL_DIR="$OUTPUT_DIR/install"
BENCHMARK_DIR="$OUTPUT_DIR/v1.7.0_benchmarks"
V16_DIR="$OUTPUT_DIR/v1.6.0_regression"
SOURCE_ZIP="$OUTPUT_DIR/SmartParallel-1.7.0-Reproducible-Runtime.zip"
EXTRACT_DIR="$OUTPUT_DIR/exact-zip-extract"
EXACT_BUILD="$OUTPUT_DIR/exact-zip-build"

case "$REPETITIONS" in
  ''|*[!0-9]*) echo "ERROR: repetitions must be a positive integer" >&2; exit 2 ;;
esac
[ "$REPETITIONS" -gt 0 ] || { echo "ERROR: repetitions must be positive" >&2; exit 2; }
case "$MODE" in full|smoke) ;; *) echo "ERROR: mode must be full or smoke" >&2; exit 2 ;; esac
for command in cmake ctest python3; do
  command -v "$command" >/dev/null 2>&1 || { echo "ERROR: $command not found" >&2; exit 2; }
done
mkdir -p "$OUTPUT_DIR" "$BENCHMARK_DIR" "$V16_DIR"

printf '\n==== [1/11] Configure Release publication build ====\n'
rm -rf "$BUILD_DIR"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_BUILD_VALIDATION=ON \
  -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_V170_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON \
  -DSMARTPARALLEL_BUILD_VISION=ON \
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
  -DSMARTPARALLEL_ENABLE_TBB=ON \
  -DSMARTPARALLEL_INSTALL=ON
cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

printf '\n==== [2/11] Run complete v1.0-v1.7 regression ====\n'
ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel 2 \
  --output-log "$OUTPUT_DIR/ctest-main.log"

printf '\n==== [3/11] Run v1.7 Runtime/profile benchmarks ====\n'
"$BUILD_DIR/benchmarks/v1.7.0/smartparallel_v170_reproducible_runtime_benchmarks" \
  "$BENCHMARK_DIR" "$REPETITIONS"
if [ "$MODE" = "full" ]; then
  python3 "$REPO_ROOT/tools/analyze_v17_reproducible_runtime.py" \
    "$BENCHMARK_DIR/raw.csv" "$BENCHMARK_DIR"
else
  python3 "$REPO_ROOT/tools/validate_benchmark_smoke.py" \
    v1.7 "$BENCHMARK_DIR/raw.csv" --minimum-repetitions "$REPETITIONS"
fi

printf '\n==== [4/11] Run v1.6 scientific regression benchmark ====\n'
"$BUILD_DIR/benchmarks/v1.6.0/smartparallel_v160_scientific_benchmarks" \
  "$V16_DIR/raw.csv" "$REPETITIONS"
if [ "$MODE" = "full" ]; then
  python3 "$REPO_ROOT/tools/analyze_v16_scientific_foundations.py" \
    "$V16_DIR/raw.csv" "$V16_DIR"
else
  python3 "$REPO_ROOT/tools/validate_benchmark_smoke.py" \
    v1.6 "$V16_DIR/raw.csv" --minimum-repetitions "$REPETITIONS"
fi

printf '\n==== [5/11] Validate documentation and release metadata ====\n'
python3 "$REPO_ROOT/tools/check_documentation.py" \
  > "$OUTPUT_DIR/documentation-validation.log" 2>&1
cat "$OUTPUT_DIR/documentation-validation.log"

printf '\n==== [6/11] Install and validate downstream consumers ====\n'
cmake --install "$BUILD_DIR" --prefix "$INSTALL_DIR"
for consumer in package-consumer package-consumer-profile package-consumer-vision; do
  consumer_build="$OUTPUT_DIR/${consumer}-build"
  rm -rf "$consumer_build"
  cmake -S "$REPO_ROOT/tests/$consumer" -B "$consumer_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$INSTALL_DIR"
  cmake --build "$consumer_build" --parallel "$BUILD_JOBS"
  ctest --test-dir "$consumer_build" --output-on-failure \
    --output-log "$OUTPUT_DIR/${consumer}.log"
done

printf '\n==== [7/11] Exercise installed CLI calibration/approval/replay ====\n'
CLI_DIR="$OUTPUT_DIR/cli-pilot"
mkdir -p "$CLI_DIR/calibration"
cat > "$CLI_DIR/calibration.json" <<MANIFEST
{"schema_version":1,"operation":"heat_diffusion","rows":64,"columns":64,"iterations":8,"repetitions":3,"worker_budget":2,"seed":170,"numerical_policy":"Reproducible","output_directory":"$CLI_DIR/calibration"}
MANIFEST
"$INSTALL_DIR/bin/smartparallel_calibrate" "$CLI_DIR/calibration.json" \
  > "$CLI_DIR/calibrate.log"
"$INSTALL_DIR/bin/smartparallel_profile" approve \
  "$CLI_DIR/calibration/candidate_profile.json" "$CLI_DIR/approved_profile.json" \
  > "$CLI_DIR/approve.log"
PROFILE_SHA_BEFORE=$(python3 - "$CLI_DIR/approved_profile.json" <<'PY'
import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())
PY
)
"$INSTALL_DIR/bin/smartparallel_replay" run "$CLI_DIR/approved_profile.json" \
  "$CLI_DIR/replay-a.json" 64 64 8 2 170 > "$CLI_DIR/replay-a.log"
"$INSTALL_DIR/bin/smartparallel_replay" run "$CLI_DIR/approved_profile.json" \
  "$CLI_DIR/replay-b.json" 64 64 8 2 170 > "$CLI_DIR/replay-b.log"
"$INSTALL_DIR/bin/smartparallel_replay" compare \
  "$CLI_DIR/replay-a.json" "$CLI_DIR/replay-b.json" > "$CLI_DIR/compare.log"
PROFILE_SHA_AFTER=$(python3 - "$CLI_DIR/approved_profile.json" <<'PY'
import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())
PY
)
[ "$PROFILE_SHA_BEFORE" = "$PROFILE_SHA_AFTER" ] || {
  echo "ERROR: ReadOnly deterministic replay modified the Approved profile" >&2; exit 1;
}

if [ "$MODE" = "full" ]; then
  printf '\n==== [8/11] Validate dependency-disabled matrix ====\n'
  rm -rf "$NO_TBB_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u VCPKG_FEATURE_FLAGS \
    -u CMAKE_TOOLCHAIN_FILE cmake -S "$REPO_ROOT" -B "$NO_TBB_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
    -DSMARTPARALLEL_BUILD_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_REQUIRE_TBB=OFF \
    -DSMARTPARALLEL_INSTALL=ON
  cmake --build "$NO_TBB_DIR" --parallel "$BUILD_JOBS"
  ctest --test-dir "$NO_TBB_DIR" --output-on-failure --parallel 2 \
    --output-log "$OUTPUT_DIR/ctest-no-tbb.log"

  printf '\n==== [9/11] Validate sanitizers and Clang warnings-as-errors ====\n'
  rm -rf "$SANITIZER_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u CMAKE_TOOLCHAIN_FILE \
    cmake -S "$REPO_ROOT" -B "$SANITIZER_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
    -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V170_TOOLS=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_ENABLE_SANITIZERS=ON \
    -DSMARTPARALLEL_INSTALL=OFF
  cmake --build "$SANITIZER_DIR" --parallel "$BUILD_JOBS"
  ctest --test-dir "$SANITIZER_DIR" --output-on-failure \
    --output-log "$OUTPUT_DIR/ctest-sanitizers.log"

  if command -v clang++ >/dev/null 2>&1; then
    rm -rf "$CLANG_DIR"
    env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u CMAKE_TOOLCHAIN_FILE \
      cmake -S "$REPO_ROOT" -B "$CLANG_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
      -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_CXX_FLAGS=-Werror \
      -DSMARTPARALLEL_BUILD_VALIDATION=ON \
      -DSMARTPARALLEL_BUILD_VISION=ON \
      -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
      -DSMARTPARALLEL_ENABLE_TBB=OFF \
      -DSMARTPARALLEL_INSTALL=OFF
    cmake --build "$CLANG_DIR" --parallel "$BUILD_JOBS"
    ctest --test-dir "$CLANG_DIR" --output-on-failure --parallel 2 \
      --output-log "$OUTPUT_DIR/ctest-clang.log"
  else
    echo "Clang not found; Clang validation not run." > "$OUTPUT_DIR/clang-status.txt"
  fi

  printf '\n==== [9/11] Validate shared-library install and CLI loader paths ====\n'
  rm -rf "$SHARED_DIR" "$SHARED_INSTALL_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u VCPKG_FEATURE_FLAGS \
    -u CMAKE_TOOLCHAIN_FILE cmake -S "$REPO_ROOT" -B "$SHARED_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
    -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V170_TOOLS=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_REQUIRE_TBB=OFF \
    -DSMARTPARALLEL_INSTALL=ON
  cmake --build "$SHARED_DIR" --parallel "$BUILD_JOBS"
  ctest --test-dir "$SHARED_DIR" --output-on-failure \
    --output-log "$OUTPUT_DIR/ctest-shared.log"
  cmake --install "$SHARED_DIR" --prefix "$SHARED_INSTALL_DIR"
  SHARED_CLI_DIR="$OUTPUT_DIR/shared-cli-smoke"
  mkdir -p "$SHARED_CLI_DIR/calibration"
  cat > "$SHARED_CLI_DIR/calibration.json" <<SHAREDMANIFEST
{"schema_version":1,"operation":"heat_diffusion","rows":16,"columns":16,"iterations":2,"repetitions":3,"worker_budget":2,"seed":170,"numerical_policy":"Reproducible","output_directory":"$SHARED_CLI_DIR/calibration"}
SHAREDMANIFEST
  "$SHARED_INSTALL_DIR/bin/smartparallel_calibrate" \
    "$SHARED_CLI_DIR/calibration.json" > "$SHARED_CLI_DIR/calibrate.log" 2>&1
  [ -s "$SHARED_CLI_DIR/calibration/candidate_profile.json" ] || {
    cat "$SHARED_CLI_DIR/calibrate.log" >&2
    echo "ERROR: installed shared CLI did not produce a Candidate profile" >&2
    exit 1
  }
else
  printf '\n==== [8-9/11] Extended compiler matrices skipped (mode=smoke) ====\n'
fi

printf '\n==== [10/11] Create deterministic source ZIP and manifest ====\n'
python3 "$REPO_ROOT/tools/create_source_release_zip.py" "$SOURCE_ZIP" \
  --root-name SmartParallel-1.7.0
python3 "$REPO_ROOT/tools/generate_source_manifest.py" "$OUTPUT_DIR/source-hashes.txt"
python3 - "$SOURCE_ZIP" > "$OUTPUT_DIR/source-zip.sha256" <<'PY'
import hashlib,sys
path=sys.argv[1]
print(hashlib.sha256(open(path,'rb').read()).hexdigest()+"  "+path.rsplit('/',1)[-1])
PY

printf '\n==== [11/11] Rebuild and retest the exact returned ZIP ====\n'
rm -rf "$EXTRACT_DIR" "$EXACT_BUILD"
mkdir -p "$EXTRACT_DIR"
python3 - "$SOURCE_ZIP" "$EXTRACT_DIR" <<'PY'
import sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    archive.extractall(sys.argv[2])
PY
EXACT_SOURCE="$EXTRACT_DIR/SmartParallel-1.7.0"
python3 "$EXACT_SOURCE/tools/verify_source_manifest.py" \
  > "$OUTPUT_DIR/exact-zip-manifest.log" 2>&1
cmake -S "$EXACT_SOURCE" -B "$EXACT_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON \
  -DSMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION=ON \
  -DSMARTPARALLEL_BUILD_V170_TOOLS=ON \
  -DSMARTPARALLEL_BUILD_V170_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_VISION=ON \
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
  -DSMARTPARALLEL_ENABLE_TBB=OFF \
  -DSMARTPARALLEL_INSTALL=ON
cmake --build "$EXACT_BUILD" --parallel "$BUILD_JOBS"
ctest --test-dir "$EXACT_BUILD" --output-on-failure \
  --output-log "$OUTPUT_DIR/ctest-exact-zip.log"
"$EXACT_BUILD/benchmarks/v1.7.0/smartparallel_v170_reproducible_runtime_benchmarks" \
  "$OUTPUT_DIR/exact-zip-benchmark" 3
python3 "$EXACT_SOURCE/tools/validate_benchmark_smoke.py" \
  v1.7 "$OUTPUT_DIR/exact-zip-benchmark/raw.csv" --minimum-repetitions 3
python3 "$EXACT_SOURCE/tools/check_documentation.py" \
  > "$OUTPUT_DIR/exact-zip-documentation.log" 2>&1

{
  echo "SmartParallel version: 1.7.0"
  echo "Release title: SmartParallel v1.7.0 — Reproducible Runtime"
  echo "Trust statement: v1.7 — Trust the experiment."
  echo "Run stamp: $RUN_STAMP"
  echo "Mode: $MODE"
  echo "Repetitions: $REPETITIONS"
  echo "OS: $(uname -s 2>/dev/null || echo unknown)"
  echo "Kernel: $(uname -r 2>/dev/null || echo unknown)"
  echo "Architecture: $(uname -m 2>/dev/null || echo unknown)"
  echo "CMake: $(cmake --version | head -1)"
  echo "Compiler: $(c++ --version 2>/dev/null | head -1 || echo unavailable)"
  echo "Clang: $(clang++ --version 2>/dev/null | head -1 || echo unavailable)"
  echo "OpenCV publication route: disabled"
  echo "oneTBB requested in primary build; availability recorded by CMake"
  echo "Exact source ZIP rebuild: passed"
} > "$OUTPUT_DIR/v1.7.0_environment.txt"

printf '\n============================================================\n'
printf 'SMARTPARALLEL V1.7 REPRODUCIBLE RUNTIME VALIDATION PASSED\n'
printf 'Benchmark report: %s\n' "$BENCHMARK_DIR/report.md"
printf 'Cross-process pilot: %s\n' "$CLI_DIR"
printf 'Source ZIP: %s\n' "$SOURCE_ZIP"
printf 'SHA-256 file: %s\n' "$OUTPUT_DIR/source-zip.sha256"
printf '============================================================\n'
