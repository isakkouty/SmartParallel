# SmartParallel benchmark suites

Benchmarks are versioned so historical evidence remains reproducible while new releases can prove new capabilities.

## Suites

- [`v1.0.0/`](v1.0.0/): the original application, decision-quality, OpenCV, scientific, overhead, CSV, and figure suite.
- [`v1.1.0/`](v1.1.0/): nested-execution benchmarks covering multiple depths and a four-level configuration matrix.

## Run v1.1.0 nested benchmarks

From the repository root:

```bat
benchmarks\v1.1.0\scripts\run_nested_execution_benchmarks.bat
```

CSV output:

```text
validation\output\v1.1.0_nested_execution_benchmarks.csv
```

## Run all suites

```bat
cmake --preset benchmarks
cmake --build --preset benchmarks
scripts\benchmarks\run_all_benchmarks.bat
```

Timings are environment-specific. Correctness checks and checksums are mandatory; speedups should be interpreted per workload and machine rather than treated as universal guarantees.
