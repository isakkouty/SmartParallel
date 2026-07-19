# Validation and recorded outputs

This directory contains standalone measurement programs, shared measurement helpers, Phase 1 artifacts, and the latest CSV benchmark outputs.

- `parallel_for_overhead.cpp` measures cold and cached framework phases.
- `prediction_validation.cpp` generates calibration data.
- `holdout_validation.cpp` generates independent holdout data.
- `machine_calibration_report.cpp` reports machine-specific priors.
- `output/` contains the current benchmark snapshot used by the v1 documentation.

See [Validation guide](../docs/v1/validation.md) and [Benchmark results](../docs/v1/benchmark-results.md).
