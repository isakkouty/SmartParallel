#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
REPETITIONS=${1:-31}
MODE=${2:-full}
BUILD_JOBS=${SMARTPARALLEL_BUILD_JOBS:-2}
RUN_STAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$REPO_ROOT/validation/output/v1.8.0_governed_execution/publication_$RUN_STAMP"
BUILD_DIR="$REPO_ROOT/build/v18_governed_release"
NO_DEP_DIR="$REPO_ROOT/build/v18_governed_no_dependencies"
ASAN_DIR="$REPO_ROOT/build/v18_governed_asan_ubsan"
TSAN_DIR="$REPO_ROOT/build/v18_governed_tsan"
CLANG_DIR="$REPO_ROOT/build/v18_governed_clang"
SHARED_DIR="$REPO_ROOT/build/v18_governed_shared"
OPENCV_DIR="$REPO_ROOT/build/v18_governed_opencv"
INSTALL_DIR="$OUTPUT_DIR/install"
SHARED_INSTALL_DIR="$OUTPUT_DIR/shared-install"
OPENCV_INSTALL_DIR="$OUTPUT_DIR/opencv-install"
V18_DIR="$OUTPUT_DIR/v1.8.0_governed_execution"
V17_DIR="$OUTPUT_DIR/v1.7.0_regression"
V16_DIR="$OUTPUT_DIR/v1.6.0_regression"
CLI_DIR="$OUTPUT_DIR/cli-pilot"
SOURCE_ZIP="$OUTPUT_DIR/SmartParallel-1.8.0-Governed-Scientific-Execution.zip"
SOURCE_ZIP_2="$OUTPUT_DIR/SmartParallel-1.8.0-reproducibility-check.zip"
EXACT_ROOT="$REPO_ROOT/build/v18_exact_$RUN_STAMP"
EXTRACT_DIR="$EXACT_ROOT/src"
EXACT_SOURCE="$EXTRACT_DIR/SmartParallel-1.8.0"
EXACT_BUILD="$EXACT_ROOT/build"
EXACT_TBB_BUILD="$EXACT_ROOT/build-tbb"
EXACT_OPENCV_BUILD="$EXACT_ROOT/build-opencv"
STATUS_FILE="$OUTPUT_DIR/matrix-status.txt"
OPENCV_AVAILABLE=0

case "$REPETITIONS" in ''|*[!0-9]*) echo "ERROR: repetitions must be a positive integer" >&2; exit 2;; esac
[ "$REPETITIONS" -gt 0 ] || { echo "ERROR: repetitions must be positive" >&2; exit 2; }
case "$MODE" in full|smoke) ;; *) echo "ERROR: mode must be full or smoke" >&2; exit 2;; esac
for command in cmake ctest python3 ninja; do
  command -v "$command" >/dev/null 2>&1 || { echo "ERROR: $command not found" >&2; exit 2; }
done
mkdir -p "$OUTPUT_DIR" "$V18_DIR" "$V17_DIR" "$V16_DIR"
: > "$STATUS_FILE"

run_consumer() {
  source_dir=$1
  name=$2
  prefix=$3
  build="$OUTPUT_DIR/${name}-build"
  rm -rf "$build"
  cmake -S "$source_dir" -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$prefix"
  cmake --build "$build" --parallel "$BUILD_JOBS"
  ctest --test-dir "$build" --output-on-failure \
    --output-log "$OUTPUT_DIR/${name}.log"
}

printf '\n==== [1/14] Configure and build GCC Release publication tree ====\n'
rm -rf "$BUILD_DIR"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_BUILD_VALIDATION=ON \
  -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_V170_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_V180_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON \
  -DSMARTPARALLEL_BUILD_VISION=ON \
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
  -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF \
  -DSMARTPARALLEL_ENABLE_TBB=ON \
  -DSMARTPARALLEL_REQUIRE_TBB=OFF \
  -DSMARTPARALLEL_INSTALL=ON
cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

printf '\n==== [2/14] Run complete v1.0-v1.8 regression ====\n'
ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel 2 \
  --output-log "$OUTPUT_DIR/ctest-main.log"

printf '\n==== [3/14] Publish retained v1.6/v1.7 and v1.8 benchmarks ====\n'
# Historical tests are not allowed to own publication paths. Recreate the
# evidence tree immediately before writers run so an unrelated cleanup cannot
# turn a successful regression stage into a misleading file-open failure.
mkdir -p "$OUTPUT_DIR" "$V18_DIR" "$V17_DIR" "$V16_DIR" "$CLI_DIR"
: > "$STATUS_FILE"
"$BUILD_DIR/benchmarks/v1.6.0/smartparallel_v160_scientific_benchmarks" \
  "$V16_DIR/raw.csv" "$REPETITIONS"
"$BUILD_DIR/benchmarks/v1.7.0/smartparallel_v170_reproducible_runtime_benchmarks" \
  "$V17_DIR" "$REPETITIONS"
"$BUILD_DIR/benchmarks/v1.8.0/smartparallel_v180_governed_execution_benchmarks" \
  "$V18_DIR" "$REPETITIONS"
if [ "$MODE" = full ]; then
  python3 "$REPO_ROOT/tools/analyze_v16_scientific_foundations.py" \
    "$V16_DIR/raw.csv" "$V16_DIR"
  python3 "$REPO_ROOT/tools/analyze_v17_reproducible_runtime.py" \
    "$V17_DIR/raw.csv" "$V17_DIR"
  python3 "$REPO_ROOT/tools/analyze_v18_governed_execution.py" \
    "$V18_DIR/raw.csv" "$V18_DIR" \
    --v16-metrics "$V16_DIR/v1.6.0_scientific_metrics.json" \
    --v17-metrics "$V17_DIR/metrics.json"
  ACCEPTED="$REPO_ROOT/docs/v1.8/assets/benchmarks/linux-gcc-accepted"
  rm -rf "$ACCEPTED"
  mkdir -p "$ACCEPTED"
  cp "$V18_DIR/raw.csv" "$V18_DIR/summary.csv" "$V18_DIR/metrics.json" \
     "$V18_DIR/report.md" "$V18_DIR/environment.txt" \
     "$V18_DIR/plot-manifest.json" "$ACCEPTED/"
  cp "$V18_DIR"/*.svg "$ACCEPTED/"
else
  python3 "$REPO_ROOT/tools/validate_benchmark_smoke.py" \
    v1.6 "$V16_DIR/raw.csv" --minimum-repetitions "$REPETITIONS"
  python3 "$REPO_ROOT/tools/validate_benchmark_smoke.py" \
    v1.7 "$V17_DIR/raw.csv" --minimum-repetitions "$REPETITIONS"
  python3 "$REPO_ROOT/tools/validate_benchmark_smoke.py" \
    v1.8 "$V18_DIR/raw.csv" --minimum-repetitions "$REPETITIONS"
fi

printf '\n==== [4/14] Validate documentation and accepted evidence ====\n'
python3 "$REPO_ROOT/tools/check_documentation.py" \
  > "$OUTPUT_DIR/documentation-validation.log" 2>&1
cat "$OUTPUT_DIR/documentation-validation.log"

printf '\n==== [5/14] Install static package and validate consumers ====\n'
rm -rf "$INSTALL_DIR"
cmake --install "$BUILD_DIR" --prefix "$INSTALL_DIR"
for consumer in package-consumer package-consumer-profile package-consumer-vision \
  package-consumer-governor package-consumer-deterministic package-consumer-nested
do
  run_consumer "$REPO_ROOT/tests/$consumer" "$consumer" "$INSTALL_DIR"
done

printf '\n==== [6/14] Validate installed calibration, approval, and replay ====\n'
mkdir -p "$CLI_DIR/calibration"
cat > "$CLI_DIR/calibration.json" <<EOF
{"schema_version":1,"operation":"heat_diffusion","rows":64,"columns":64,"iterations":8,"repetitions":3,"worker_budget":2,"seed":180,"numerical_policy":"Reproducible","output_directory":"$CLI_DIR/calibration"}
EOF
"$INSTALL_DIR/bin/smartparallel_calibrate" "$CLI_DIR/calibration.json" > "$CLI_DIR/calibrate.log" 2>&1
[ -s "$CLI_DIR/calibration/candidate_profile.json" ] || { cat "$CLI_DIR/calibrate.log" >&2; exit 1; }
"$INSTALL_DIR/bin/smartparallel_profile" approve \
  "$CLI_DIR/calibration/candidate_profile.json" "$CLI_DIR/approved_profile.json" \
  > "$CLI_DIR/approve.log" 2>&1
PROFILE_SHA_BEFORE=$(python3 - "$CLI_DIR/approved_profile.json" <<'PY'
import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())
PY
)
"$INSTALL_DIR/bin/smartparallel_replay" run "$CLI_DIR/approved_profile.json" \
  "$CLI_DIR/replay-a.json" 64 64 8 2 180 > "$CLI_DIR/replay-a.log" 2>&1
"$INSTALL_DIR/bin/smartparallel_replay" run "$CLI_DIR/approved_profile.json" \
  "$CLI_DIR/replay-b.json" 64 64 8 2 180 > "$CLI_DIR/replay-b.log" 2>&1
"$INSTALL_DIR/bin/smartparallel_replay" compare \
  "$CLI_DIR/replay-a.json" "$CLI_DIR/replay-b.json" > "$CLI_DIR/compare.log" 2>&1
PROFILE_SHA_AFTER=$(python3 - "$CLI_DIR/approved_profile.json" <<'PY'
import hashlib,sys
print(hashlib.sha256(open(sys.argv[1],'rb').read()).hexdigest())
PY
)
[ "$PROFILE_SHA_BEFORE" = "$PROFILE_SHA_AFTER" ] || { echo "ERROR: ReadOnly replay modified Approved profile" >&2; exit 1; }

if [ "$MODE" = full ]; then
  printf '\n==== [7/14] Validate no-oneTBB/no-OpenCV matrix ====\n'
  rm -rf "$NO_DEP_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u VCPKG_FEATURE_FLAGS -u CMAKE_TOOLCHAIN_FILE \
    cmake -S "$REPO_ROOT" -B "$NO_DEP_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
    -DSMARTPARALLEL_BUILD_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_REQUIRE_TBB=OFF \
    -DSMARTPARALLEL_INSTALL=ON
  cmake --build "$NO_DEP_DIR" --parallel "$BUILD_JOBS"
  ctest --test-dir "$NO_DEP_DIR" --output-on-failure --parallel 2 \
    --output-log "$OUTPUT_DIR/ctest-no-dependencies.log"

  printf '\n==== [8/14] Validate ASan/UBSan, Clang -Werror, and TSan ====\n'
  rm -rf "$ASAN_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u CMAKE_TOOLCHAIN_FILE \
    cmake -S "$REPO_ROOT" -B "$ASAN_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
    -DSMARTPARALLEL_BUILD_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_ENABLE_SANITIZERS=ON \
    -DSMARTPARALLEL_INSTALL=OFF
  cmake --build "$ASAN_DIR" --parallel "$BUILD_JOBS"
  ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 \
    ctest --test-dir "$ASAN_DIR" --output-on-failure --parallel 1 \
    --output-log "$OUTPUT_DIR/ctest-asan-ubsan.log"

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
    echo "Clang warnings-as-errors: PASS" >> "$STATUS_FILE"
  else
    echo "Clang warnings-as-errors: NOT RUN (clang++ unavailable)" >> "$STATUS_FILE"
  fi

  rm -rf "$TSAN_DIR"
  if env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u CMAKE_TOOLCHAIN_FILE \
      cmake -S "$REPO_ROOT" -B "$TSAN_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
      -DCMAKE_CXX_FLAGS='-fsanitize=thread -fno-omit-frame-pointer -O1' \
      -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=thread' \
      -DSMARTPARALLEL_BUILD_V180_GOVERNED_EXECUTION_VALIDATION=ON \
      -DSMARTPARALLEL_BUILD_VISION=OFF \
      -DSMARTPARALLEL_ENABLE_TBB=OFF \
      -DSMARTPARALLEL_INSTALL=OFF \
      > "$OUTPUT_DIR/tsan-configure.log" 2>&1 \
    && cmake --build "$TSAN_DIR" --parallel "$BUILD_JOBS" > "$OUTPUT_DIR/tsan-build.log" 2>&1
  then
    set +e
    TSAN_OPTIONS=halt_on_error=1:history_size=7 ctest --test-dir "$TSAN_DIR" \
      -R 'smartparallel_v180_(resource_governor|runtime_governance)' \
      --output-on-failure --parallel 1 --output-log "$OUTPUT_DIR/ctest-tsan.log"
    tsan_status=$?
    set -e
    if [ "$tsan_status" -eq 0 ]; then
      echo "ThreadSanitizer: PASS" >> "$STATUS_FILE"
    elif grep -Eq 'unexpected memory mapping|ThreadSanitizer.*unsupported|FATAL: ThreadSanitizer' "$OUTPUT_DIR/ctest-tsan.log"; then
      echo "ThreadSanitizer: PLATFORM LIMITATION (runtime unavailable; stress matrix retained)" >> "$STATUS_FILE"
    else
      cat "$OUTPUT_DIR/ctest-tsan.log" >&2
      echo "ERROR: ThreadSanitizer detected a defect or unexplained failure" >&2
      exit 1
    fi
  else
    echo "ThreadSanitizer: PLATFORM LIMITATION (configure/build unavailable)" >> "$STATUS_FILE"
  fi

  printf '\n==== [9/14] Validate optional oneTBB/OpenCV matrices where available ====\n'
  if grep -q 'SMARTPARALLEL_HAS_TBB=1' "$BUILD_DIR/src/CMakeFiles/smart_parallel.dir/flags.make" 2>/dev/null; then
    ctest --test-dir "$BUILD_DIR" -R 'smartparallel_v180_runtime_governance' \
      --output-on-failure --output-log "$OUTPUT_DIR/ctest-onetbb.log"
    echo "oneTBB matrix: PASS" >> "$STATUS_FILE"
  else
    echo "oneTBB matrix: NOT AVAILABLE on this host" >> "$STATUS_FILE"
  fi
  rm -rf "$OPENCV_DIR" "$OPENCV_INSTALL_DIR"
  set +e
  cmake -S "$REPO_ROOT" -B "$OPENCV_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSMARTPARALLEL_BUILD_V150_VISION_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V170_REPRODUCIBLE_RUNTIME_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V180_GOVERNED_EXECUTION_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON \
    -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON \
    -DSMARTPARALLEL_ENABLE_TBB=ON \
    -DSMARTPARALLEL_REQUIRE_TBB=OFF \
    -DSMARTPARALLEL_INSTALL=ON > "$OUTPUT_DIR/opencv-configure.log" 2>&1
  opencv_config=$?
  set -e
  if [ "$opencv_config" -eq 0 ]; then
    OPENCV_AVAILABLE=1
    cmake --build "$OPENCV_DIR" --parallel "$BUILD_JOBS"
    ctest --test-dir "$OPENCV_DIR" -R 'smartparallel_v(150_vision|160_vision|170_vision|180_opencv)' \
      --output-on-failure --output-log "$OUTPUT_DIR/ctest-opencv.log"
    cmake --install "$OPENCV_DIR" --prefix "$OPENCV_INSTALL_DIR"
    run_consumer "$REPO_ROOT/tests/package-consumer-vision" \
      package-consumer-vision-opencv "$OPENCV_INSTALL_DIR"
    echo "OpenCV containment matrix: PASS" >> "$STATUS_FILE"
  else
    echo "OpenCV containment matrix: NOT AVAILABLE on this host" >> "$STATUS_FILE"
  fi

  printf '\n==== [10/14] Validate shared-library package ====\n'
  rm -rf "$SHARED_DIR" "$SHARED_INSTALL_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u CMAKE_TOOLCHAIN_FILE \
    cmake -S "$REPO_ROOT" -B "$SHARED_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
    -DSMARTPARALLEL_BUILD_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_INSTALL=ON
  cmake --build "$SHARED_DIR" --parallel "$BUILD_JOBS"
  ctest --test-dir "$SHARED_DIR" --output-on-failure --parallel 2 \
    --output-log "$OUTPUT_DIR/ctest-shared.log"
  cmake --install "$SHARED_DIR" --prefix "$SHARED_INSTALL_DIR"
  for consumer in package-consumer package-consumer-profile package-consumer-vision \
    package-consumer-governor package-consumer-deterministic package-consumer-nested
  do
    run_consumer "$REPO_ROOT/tests/$consumer" "shared-$consumer" "$SHARED_INSTALL_DIR"
  done
else
  printf '\n==== [7/14]-[10/14] Full matrices skipped in smoke mode ====\n'
fi

printf '\n==== [11/14] Create two deterministic source ZIPs ====\n'
python3 "$REPO_ROOT/tools/create_source_release_zip.py" "$SOURCE_ZIP" \
  --root-name SmartParallel-1.8.0
python3 "$REPO_ROOT/tools/create_source_release_zip.py" "$SOURCE_ZIP_2" \
  --root-name SmartParallel-1.8.0
cmp "$SOURCE_ZIP" "$SOURCE_ZIP_2"
python3 "$REPO_ROOT/tools/verify_source_manifest.py" > "$OUTPUT_DIR/source-manifest-verification.log"
sha256sum "$SOURCE_ZIP" > "$OUTPUT_DIR/source-zip.sha256"
rm -f "$SOURCE_ZIP_2"

printf '\n==== [12/14] Independently rebuild exact returned ZIP ====\n'
rm -rf "$EXACT_ROOT"
mkdir -p "$EXTRACT_DIR"
python3 - "$SOURCE_ZIP" "$EXTRACT_DIR" <<'PY'
import sys,zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    if len(z.namelist()) != len(set(z.namelist())):
        raise SystemExit('duplicate ZIP entries')
    for name in z.namelist():
        parts=name.replace('\\','/').split('/')
        if name.startswith(('/', '\\')) or '..' in parts:
            raise SystemExit(f'unsafe ZIP entry: {name}')
    bad=z.testzip()
    if bad: raise SystemExit(f'ZIP integrity failure: {bad}')
    z.extractall(sys.argv[2])
PY
python3 "$EXACT_SOURCE/tools/verify_source_manifest.py" > "$OUTPUT_DIR/exact-zip-manifest.log"
env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u VCPKG_FEATURE_FLAGS -u CMAKE_TOOLCHAIN_FILE \
  cmake -S "$EXACT_SOURCE" -B "$EXACT_BUILD" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
  -DSMARTPARALLEL_BUILD_VALIDATION=ON \
  -DSMARTPARALLEL_BUILD_V180_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_VISION=ON \
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
  -DSMARTPARALLEL_ENABLE_TBB=OFF \
  -DSMARTPARALLEL_INSTALL=ON
cmake --build "$EXACT_BUILD" --parallel "$BUILD_JOBS"
ctest --test-dir "$EXACT_BUILD" --output-on-failure --parallel 2 \
  --output-log "$OUTPUT_DIR/ctest-exact-zip.log"
"$EXACT_BUILD/benchmarks/v1.8.0/smartparallel_v180_governed_execution_benchmarks" \
  "$OUTPUT_DIR/exact-zip-benchmark" 3
python3 "$EXACT_SOURCE/tools/validate_benchmark_smoke.py" \
  v1.8 "$OUTPUT_DIR/exact-zip-benchmark/raw.csv" --minimum-repetitions 3
python3 "$EXACT_SOURCE/tools/check_documentation.py" \
  > "$OUTPUT_DIR/exact-zip-documentation.log" 2>&1

if [ "$MODE" = full ]; then
  printf '\n==== [13/14] Validate exact ZIP optional dependency paths ====\n'
  rm -rf "$EXACT_TBB_BUILD"
  cmake -S "$EXACT_SOURCE" -B "$EXACT_TBB_BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSMARTPARALLEL_BUILD_V180_GOVERNED_EXECUTION_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=ON \
    -DSMARTPARALLEL_REQUIRE_TBB=OFF \
    -DSMARTPARALLEL_INSTALL=OFF
  cmake --build "$EXACT_TBB_BUILD" --parallel "$BUILD_JOBS"
  ctest --test-dir "$EXACT_TBB_BUILD" -R 'smartparallel_v180_' \
    --output-on-failure --output-log "$OUTPUT_DIR/ctest-exact-onetbb.log"
  if [ "$OPENCV_AVAILABLE" -eq 1 ]; then
    rm -rf "$EXACT_OPENCV_BUILD"
    cmake -S "$EXACT_SOURCE" -B "$EXACT_OPENCV_BUILD" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DSMARTPARALLEL_BUILD_V180_GOVERNED_EXECUTION_VALIDATION=ON \
      -DSMARTPARALLEL_BUILD_VISION=ON \
      -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=ON \
      -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=ON \
      -DSMARTPARALLEL_ENABLE_TBB=ON \
      -DSMARTPARALLEL_REQUIRE_TBB=OFF \
      -DSMARTPARALLEL_INSTALL=OFF
    cmake --build "$EXACT_OPENCV_BUILD" --parallel "$BUILD_JOBS"
    ctest --test-dir "$EXACT_OPENCV_BUILD" -R 'smartparallel_v180_' \
      --output-on-failure --output-log "$OUTPUT_DIR/ctest-exact-opencv.log"
  fi
else
  printf '\n==== [13/14] Exact optional matrices skipped in smoke mode ====\n'
fi

printf '\n==== [14/14] Validation complete ====\n'
printf '%s\n' 'SMARTPARALLEL V1.8 GOVERNED SCIENTIFIC EXECUTION VALIDATION PASSED'
printf 'Evidence directory: %s\n' "$OUTPUT_DIR"
printf 'Benchmark report: %s\n' "$V18_DIR/report.md"
printf 'Source ZIP: %s\n' "$SOURCE_ZIP"
printf 'SHA-256: '
sha256sum "$SOURCE_ZIP"
