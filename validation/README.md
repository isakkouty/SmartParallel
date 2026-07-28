# Validation and recorded outputs

The validation directory contains deterministic correctness, hardening, measurement, and recorded-output programs.

## Current v1.5 release validation

The complete adaptive-route publication is documented in:

- [Benchmark results](../docs/v1.5/benchmarks.md)
- [Benchmark methodology](../docs/v1.5/benchmark-methodology.md)
- [Benchmark reproduction](../docs/v1.5/benchmark-reproduction.md)
- [Deterministic validation](../docs/v1.5/validation.md)

The accepted release evidence is under:

```text
validation/output/v1.5.0_adaptive_routes/publication_20260728_184110/
```

The accepted run contains 2,238 correct and authenticated samples and passes all 6/6 combined proof gates. Performance is machine-specific.

## CMake validation preset

```text
cmake --preset validation
cmake --build --preset validation
ctest --preset validation
```

## Retained outputs

- `validation/output/real_world/` supports the retained v1.1 integration report.
- v1.4 summary/raw CSVs support the parallel-algorithm report.
- Other top-level CSV files support historical v1.0 decision-quality, OpenCV, scientific, and overhead documentation.
