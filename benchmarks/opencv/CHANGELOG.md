# Changelog

## Automatic parallel_for hardening
- Added stratified regional sampling with exact-once execution.
- Added cost-based predicted speedup using measured callback cost, machine thread count, overhead, and imbalance penalty.
- Added thread-safe callback profile reuse keyed by callable type and iteration bucket.
- Added edge-case, exception, misleading-cost-distribution, and cache-reuse validation.
- Added cheap-loop profiling overhead benchmark and full regression script.


## Automatic `parallel_for` profiling

- `smart::parallel_for` now executes and times a small prefix exactly once.
- The measured callback cost is supplied automatically to the decision engine.
- No execution hints are required for compute-heavy callbacks.
- Sampled prefix iterations are excluded from the remaining scheduled range, so callbacks are never duplicated.
- Workload thresholds are configurable through `smart::global_config()`.
# OpenCV benchmark update

- Moved OpenCV benchmark sources to `benchmarks/opencv/src`.
- Moved and consolidated Windows runners in `benchmarks/opencv/scripts`.
- Added a shared batch runner to eliminate duplicated configure/build logic.
- Added `run_opencv_benchmarks.bat` as the repository-root entry point.
- Added six stress workloads in `stress_suite.cpp`.
- Removed accidental command-named files, Python bytecode caches, empty artifacts, and generated OpenCV CSV output.
