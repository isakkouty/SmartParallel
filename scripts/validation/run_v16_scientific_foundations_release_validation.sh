#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
REPETITIONS=${1:-11}
MODE=${2:-full}
RUN_STAMP=$(date +%Y%m%d_%H%M%S)
OUTPUT_DIR="$REPO_ROOT/validation/output/v1.6.0_scientific_foundations/publication_$RUN_STAMP"
BUILD_DIR="$REPO_ROOT/build/v16_scientific_release"
NO_TBB_DIR="$REPO_ROOT/build/v16_scientific_no_tbb"
SANITIZER_DIR="$REPO_ROOT/build/v16_scientific_sanitizers"
CLANG_DIR="$REPO_ROOT/build/v16_scientific_clang"
INSTALL_DIR="$OUTPUT_DIR/install"
RAW="$OUTPUT_DIR/v1.6.0_scientific_raw.csv"
EXE="$BUILD_DIR/benchmarks/v1.6.0/smartparallel_v160_scientific_benchmarks"
PILOT="$BUILD_DIR/examples/smartparallel_v160_heat_diffusion"

case "$REPETITIONS" in
  ''|*[!0-9]*) echo "ERROR: repetitions must be a positive integer" >&2; exit 2 ;;
esac
[ "$REPETITIONS" -gt 0 ] || { echo "ERROR: repetitions must be positive" >&2; exit 2; }
command -v cmake >/dev/null 2>&1 || { echo "ERROR: cmake not found" >&2; exit 2; }
command -v ctest >/dev/null 2>&1 || { echo "ERROR: ctest not found" >&2; exit 2; }
command -v python3 >/dev/null 2>&1 || { echo "ERROR: python3 not found" >&2; exit 2; }
mkdir -p "$OUTPUT_DIR"

SMARTPARALLEL_CPU_DESCRIPTION=$(LC_ALL=C lscpu 2>/dev/null | awk -F: '/Model name/ {sub(/^[[:space:]]+/, "", $2); print $2; exit}')
SMARTPARALLEL_CPU_DESCRIPTION=${SMARTPARALLEL_CPU_DESCRIPTION:-unreported}
export SMARTPARALLEL_CPU_DESCRIPTION

printf '\n==== [1/9] Configure Release publication build ====\n'
rm -rf "$BUILD_DIR"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_BUILD_VALIDATION=ON \
  -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_BENCHMARKS=ON \
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON \
  -DSMARTPARALLEL_BUILD_VISION=ON \
  -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
  -DSMARTPARALLEL_ENABLE_TBB=ON \
  -DSMARTPARALLEL_INSTALL=ON
cmake --build "$BUILD_DIR" --parallel

printf '\n==== [2/9] Run complete deterministic regression ====\n'
ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel 2 \
  --output-log "$OUTPUT_DIR/ctest-main.log"

printf '\n==== [3/9] Run heat-diffusion pilot ====\n'
"$PILOT" 128 128 50 reproducible | tee "$OUTPUT_DIR/v1.6.0_heat_diffusion_pilot.txt"

printf '\n==== [4/9] Run scientific publication benchmark ====\n'
"$EXE" "$RAW" "$REPETITIONS"
python3 "$REPO_ROOT/tools/analyze_v16_scientific_foundations.py" "$RAW" "$OUTPUT_DIR"

printf '\n==== [5/9] Validate documentation ====\n'
if python3 "$REPO_ROOT/tools/check_documentation.py" > "$OUTPUT_DIR/documentation-validation.log" 2>&1; then
  cat "$OUTPUT_DIR/documentation-validation.log"
else
  cat "$OUTPUT_DIR/documentation-validation.log"
  exit 1
fi

printf '\n==== [6/9] Install and validate downstream consumers ====\n'
cmake --install "$BUILD_DIR" --prefix "$INSTALL_DIR"
for consumer in package-consumer package-consumer-vision; do
  consumer_build="$OUTPUT_DIR/${consumer}-build"
  rm -rf "$consumer_build"
  cmake -S "$REPO_ROOT/tests/$consumer" -B "$consumer_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$INSTALL_DIR"
  cmake --build "$consumer_build" --parallel
  if [ "$consumer" = "package-consumer" ]; then
    consumer_log="$OUTPUT_DIR/core-package-consumer-test.log"
  else
    consumer_log="$OUTPUT_DIR/vision-package-consumer-test.log"
  fi
  ctest --test-dir "$consumer_build" --output-on-failure \
    --output-log "$consumer_log"
done

if [ "$MODE" = "full" ]; then
  printf '\n==== [7/9] Validate no-oneTBB configuration ====\n'
  rm -rf "$NO_TBB_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u VCPKG_FEATURE_FLAGS \
    -u CMAKE_TOOLCHAIN_FILE cmake -S "$REPO_ROOT" -B "$NO_TBB_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH= \
    -DSMARTPARALLEL_BUILD_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_REQUIRE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_REQUIRE_TBB=OFF \
    -DSMARTPARALLEL_INSTALL=ON
  grep -q '^SMARTPARALLEL_ENABLE_TBB:BOOL=OFF$' "$NO_TBB_DIR/CMakeCache.txt"
  grep -q '^SMARTPARALLEL_ENABLE_OPENCV_PROVIDER:BOOL=OFF$' "$NO_TBB_DIR/CMakeCache.txt"
  cmake --build "$NO_TBB_DIR" --parallel
  grep -q 'SMARTPARALLEL_HAS_TBB=0' "$NO_TBB_DIR/build.ninja"
  grep -q 'SMARTPARALLEL_VISION_HAS_OPENCV=0' "$NO_TBB_DIR/build.ninja"
  ctest --test-dir "$NO_TBB_DIR" --output-on-failure --parallel 2 \
    --output-log "$OUTPUT_DIR/ctest-no-tbb.log"

  printf '\n==== [8/9] Validate ASan/UBSan and Clang where available ====\n'
  rm -rf "$SANITIZER_DIR"
  env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u CMAKE_TOOLCHAIN_FILE cmake -S "$REPO_ROOT" -B "$SANITIZER_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_TOOLCHAIN_FILE= \
    -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_VISION=ON \
    -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=OFF \
    -DSMARTPARALLEL_ENABLE_SANITIZERS=ON \
    -DSMARTPARALLEL_INSTALL=OFF
  cmake --build "$SANITIZER_DIR" --parallel
  ctest --test-dir "$SANITIZER_DIR" --output-on-failure \
    --output-log "$OUTPUT_DIR/ctest-sanitizers.log"

  if command -v clang++ >/dev/null 2>&1; then
    rm -rf "$CLANG_DIR"
    env -u VCPKG_ROOT -u VCPKG_INSTALLATION_ROOT -u CMAKE_TOOLCHAIN_FILE cmake -S "$REPO_ROOT" -B "$CLANG_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE= \
      -DCMAKE_CXX_COMPILER=clang++ \
      -DCMAKE_CXX_FLAGS=-Werror \
      -DSMARTPARALLEL_BUILD_V160_SCIENTIFIC_VALIDATION=ON \
      -DSMARTPARALLEL_BUILD_VISION=ON \
      -DSMARTPARALLEL_ENABLE_OPENCV_PROVIDER=OFF \
      -DSMARTPARALLEL_ENABLE_TBB=OFF \
      -DSMARTPARALLEL_INSTALL=OFF
    cmake --build "$CLANG_DIR" --parallel
    ctest --test-dir "$CLANG_DIR" --output-on-failure \
      --output-log "$OUTPUT_DIR/ctest-clang.log"
  else
    echo "Clang not found; Clang validation not run." | tee "$OUTPUT_DIR/clang-status.txt"
  fi
else
  printf '\n==== [7/9] Extended matrices skipped (mode=%s) ====\n' "$MODE"
fi

printf '\n==== [9/9] Record sanitized environment, hashes, and archive ====\n'
{
  echo "SmartParallel version: 1.6.0"
  echo "Run stamp: $RUN_STAMP"
  echo "Mode: $MODE"
  echo "Repetitions: $REPETITIONS"
  echo "Operating system: $(uname -s 2>/dev/null || echo unknown)"
  echo "Kernel release: $(uname -r 2>/dev/null || echo unknown)"
  echo "Architecture: $(uname -m 2>/dev/null || echo unknown)"
  echo "CPU: $SMARTPARALLEL_CPU_DESCRIPTION"
  echo "CMake: $(cmake --version | head -1)"
  echo "C++ compiler: $(c++ --version 2>/dev/null | head -1 || echo unavailable)"
  echo "Clang: $(clang++ --version 2>/dev/null | head -1 || echo unavailable)"
  echo "Primary publication: oneTBB requested, OpenCV disabled"
  echo "Secondary matrix: oneTBB disabled, OpenCV disabled"
  echo "Unsafe fast-math validation: disabled"
} > "$OUTPUT_DIR/v1.6.0_environment.txt"
python3 "$REPO_ROOT/tools/generate_source_manifest.py" "$OUTPUT_DIR/source-hashes.txt"
python3 "$REPO_ROOT/tools/prepare_v16_publication_archive.py" "$OUTPUT_DIR"

OUTPUT_PARENT=$(dirname "$OUTPUT_DIR")
OUTPUT_NAME=$(basename "$OUTPUT_DIR")
python3 "$REPO_ROOT/tools/create_reproducible_zip.py" \
  "$OUTPUT_DIR" "$OUTPUT_DIR.zip" --root-name "$OUTPUT_NAME"

printf '\n============================================================\n'
printf 'SMARTPARALLEL V1.6 SCIENTIFIC FOUNDATIONS VALIDATION PASSED\n'
printf 'Raw samples: %s\n' "$RAW"
printf 'Report: %s\n' "$OUTPUT_DIR/v1.6.0_scientific_foundations_report.md"
printf 'Plots: %s\n' "$OUTPUT_DIR"
printf 'Archive: %s\n' "$OUTPUT_DIR.zip"
printf '============================================================\n'
