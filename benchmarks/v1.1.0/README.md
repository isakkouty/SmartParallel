# SmartParallel v1.1.0 nested-execution benchmarks

This suite measures the capability introduced during the v1.1.0 development cycle: nested `parallel_for` calls that preserve execution lineage, respect concurrency budgets, and coordinate work without recursive thread-pool creation.

## Evidence produced

The suite contains two groups:

1. **Depth scaling**: two-, three-, and four-level nested calculations with different loop shapes and computation kernels. Each compares a direct sequential implementation with SmartParallel at every nesting level.
2. **Four-level configuration matrix**: the same four-level calculation is run fully sequentially, with only one selected level parallel, and with SmartParallel enabled at all levels. This shows whether the scheduler's coordinated nested policy behaves differently from manually choosing one parallel layer.

Every timed row validates a deterministic checksum. Timing samples are summarized by the median, minimum, and maximum. The CSV includes speedup relative to the matching fully sequential baseline.

## Run

From the repository root:

```bat
benchmarks\v1.1.0\scripts\run_nested_execution_benchmarks.bat
```

Optional arguments are the vcpkg toolchain path and repetition count:

```bat
benchmarks\v1.1.0\scripts\run_nested_execution_benchmarks.bat "C:\path\to\vcpkg.cmake" 11
```

Output:

```text
validation\output\v1.1.0_nested_execution_benchmarks.csv
```

Performance is machine-dependent. Correctness is a release gate; speedup is evidence to interpret rather than a guaranteed result for every shape.


## Benchmark runtime safety

The suite runs inside a bounded four-worker ThreadPool runtime domain and prints
progress before every warm-up and repetition. The four-level matrix benchmarks
the validated v1.1.0 coordinator/executor path directly.

During development, the original benchmark exposed a separate depth-four stall
in the automatic public `parallel_for` profiling path. That issue is deliberately
not hidden in release notes; the benchmark was moved to the validated coordinator
path so performance data can be collected safely while the public-path defect is
tracked independently.
