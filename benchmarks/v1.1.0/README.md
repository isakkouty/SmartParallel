# SmartParallel v1.1.0 benchmarks

This is the current benchmark suite for the v1.1 release.

## Real-world integrations

The primary release suite is under [`real_world/`](real_world/README.md):

- OpenCV image-processing pipelines
- LZ4 batch compression
- custom median-split BVH construction
- custom uniform-grid particle simulation

Run all integrations, presets, modes, traces, correctness checks, comparisons, and CTests:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

Output is written to `validation/output/real_world/`.

## Nested execution microbenchmarks

The source file `src/nested_execution_benchmarks.cpp` and launcher `scripts/run_nested_execution_benchmarks.bat` retain focused nested-depth and configuration measurements. These support engineering validation but are not the authoritative public performance report.

## Reporting

- [Final benchmark results](../../docs/v1.1/benchmarks.md)
- [Methodology](../../docs/v1.1/benchmark-methodology.md)
- [Reproduction](../../docs/v1.1/benchmark-reproduction.md)
- [CSV schema](real_world/CSV_SCHEMA.md)
