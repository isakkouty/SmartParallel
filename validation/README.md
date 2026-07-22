# Validation and recorded outputs

The validation directory contains deterministic correctness, hardening, measurement, and recorded-output programs.

## Current release validation

The complete v1.1 real-world run is documented in:

- [Benchmark results](../docs/v1.1/benchmarks.md)
- [Methodology](../docs/v1.1/benchmark-methodology.md)
- [Reproduction](../docs/v1.1/benchmark-reproduction.md)

Recorded final outputs are under `validation/output/real_world/`.

## CMake validation preset

```text
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

## Historical outputs

Other CSV files under `validation/output/` support the v1.0 decision-quality, OpenCV, scientific, and overhead documentation. The corresponding archived benchmark suite is under `benchmarks/v1.0.0/`.
