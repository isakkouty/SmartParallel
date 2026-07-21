#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"

repetitions="${1:-11}"
trace_mode="${2:-}"
backend_mode="${3:-}"
reuse_build="${4:-}"
if [[ "$trace_mode" == "tbb" ]]; then
  backend_mode="tbb"
elif [[ "$trace_mode" == "static" ]]; then
  backend_mode="static"
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
elif [[ "$backend_mode" == "static" ]]; then
  output="validation/output/v1.1.0_nested_execution_optimized_static_run.csv"
fi
if [[ "$trace_mode" == "trace" ]]; then
  output="validation/output/v1.1.0_nested_execution_optimized_trace_run.csv"
fi
if [[ "$trace_mode" == "trace" && "$backend_mode" == "tbb" ]]; then
  output="validation/output/v1.1.0_nested_execution_optimized_tbb_trace_run.csv"
elif [[ "$trace_mode" == "trace" && "$backend_mode" == "static" ]]; then
  output="validation/output/v1.1.0_nested_execution_optimized_static_trace_run.csv"
fi

exe="$build_dir/benchmarks/v1.1.0/smartparallel_v110_nested_benchmarks"
release_exe="$build_dir/benchmarks/v1.1.0/Release/smartparallel_v110_nested_benchmarks"
if [[ "$reuse_build" != "reuse" || ( ! -x "$exe" && ! -x "$release_exe" ) ]]; then
  rm -rf "$build_dir"
  cmake -S . -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSMARTPARALLEL_BUILD_VALIDATION=ON \
    -DSMARTPARALLEL_BUILD_V110_NESTED_BENCHMARKS=ON \
    -DSMARTPARALLEL_INSTALL=OFF \
    -DSMARTPARALLEL_ENABLE_TBB="$enable_tbb" \
    -DSMARTPARALLEL_REQUIRE_TBB="$require_tbb"

  cmake --build "$build_dir" --config Release -j
  ctest --test-dir "$build_dir" -C Release --output-on-failure
fi

if [[ ! -x "$exe" ]]; then
  exe="$release_exe"
fi

args=("$output" "$repetitions")
if [[ "$trace_mode" == "trace" ]]; then
  args+=(trace)
fi
if [[ "$backend_mode" == "tbb" ]]; then
  args+=(tbb)
elif [[ "$backend_mode" == "static" ]]; then
  args+=(static_thread)
else
  args+=(thread_pool)
fi
"$exe" "${args[@]}"

expected_backend="thread_pool"
if [[ "$backend_mode" == "tbb" ]]; then
  expected_backend="one_tbb"
elif [[ "$backend_mode" == "static" ]]; then
  expected_backend="static_thread"
fi
if ! grep -q ",${expected_backend}," "$output"; then
  echo "ERROR: requested backend ${expected_backend} was not confirmed in ${output}." >&2
  exit 1
fi
if [[ "$trace_mode" == "trace" ]]; then
  trace_output="${output%.csv}_trace.csv"
  if ! grep -q ",${expected_backend},${expected_backend},1," "$trace_output"; then
    echo "ERROR: detailed trace did not confirm actual backend ${expected_backend}." >&2
    exit 1
  fi
fi

echo
printf 'Summary: %s\n' "$output"
printf 'Raw samples: %s\n' "${output%.csv}_raw.csv"
printf 'Trace: %s\n' "${output%.csv}_trace.csv"
