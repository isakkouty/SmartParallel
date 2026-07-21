#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

repetitions="${1:-11}"
trace_mode="${2:-}"
backend_mode="${3:-}"
if [[ "$trace_mode" == "tbb" ]]; then
  backend_mode="tbb"
fi
enable_tbb="OFF"
require_tbb="OFF"
if [[ "$backend_mode" == "tbb" ]]; then
  enable_tbb="ON"
  require_tbb="ON"
fi
build_dir="build_nested_release"
output="validation/output/v1.1.0_nested_execution_optimized.csv"
if [[ "$backend_mode" == "tbb" ]]; then
  output="validation/output/v1.1.0_nested_execution_optimized_tbb_run.csv"
fi
if [[ "$trace_mode" == "trace" ]]; then
  output="validation/output/v1.1.0_nested_execution_optimized_trace_run.csv"
fi
if [[ "$trace_mode" == "trace" && "$backend_mode" == "tbb" ]]; then
  output="validation/output/v1.1.0_nested_execution_optimized_tbb_trace_run.csv"
fi

cmake -S . -B "$build_dir" \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMARTPARALLEL_BUILD_VALIDATION=ON \
  -DSMARTPARALLEL_BUILD_V110_NESTED_BENCHMARKS=ON \
  -DSMARTPARALLEL_INSTALL=OFF \
  -DSMARTPARALLEL_ENABLE_TBB="$enable_tbb" \
  -DSMARTPARALLEL_REQUIRE_TBB="$require_tbb"

cmake --build "$build_dir" --config Release -j
ctest --test-dir "$build_dir" -C Release --output-on-failure

exe="$build_dir/benchmarks/v1.1.0/smartparallel_v110_nested_benchmarks"
if [[ ! -x "$exe" ]]; then
  exe="$build_dir/benchmarks/v1.1.0/Release/smartparallel_v110_nested_benchmarks"
fi

args=("$output" "$repetitions")
if [[ "$trace_mode" == "trace" ]]; then
  args+=(trace)
fi
"$exe" "${args[@]}"

echo
printf 'Summary: %s\n' "$output"
printf 'Raw samples: %s\n' "${output%.csv}_raw.csv"
printf 'Trace: %s\n' "${output%.csv}_trace.csv"
