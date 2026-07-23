#!/usr/bin/env sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)
REPETITIONS=${1:-7}
REUSE=${2:-}
BUILD_DIR="$REPO_ROOT/build/v14_algorithm_release_validation"
OUTPUT="$REPO_ROOT/validation/output/v1.4.0_parallel_algorithms.csv"
EXE="$BUILD_DIR/benchmarks/v1.4.0/smartparallel_v140_algorithm_benchmarks"

if [ "$REUSE" != "reuse" ]; then
  rm -rf "$BUILD_DIR"
  cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DSMARTPARALLEL_BUILD_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V140_ALGORITHM_BENCHMARKS=ON \
    -DSMARTPARALLEL_BUILD_BENCHMARKS=OFF \
    -DSMARTPARALLEL_ENABLE_TBB=ON \
    -DSMARTPARALLEL_REQUIRE_TBB=ON \
    -DSMARTPARALLEL_INSTALL=ON
  cmake --build "$BUILD_DIR" --parallel
fi

ctest --test-dir "$BUILD_DIR" --output-on-failure --parallel 2
mkdir -p "$(dirname "$OUTPUT")"
"$EXE" "$OUTPUT" "$REPETITIONS"

echo "SmartParallel v1.4 algorithm release validation passed."
echo "Summary: $OUTPUT"
echo "Raw samples: ${OUTPUT%.csv}_raw.csv"
