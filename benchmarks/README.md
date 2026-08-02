# SmartParallel benchmark suites

## Current release suite: v1.8.0 governed scientific execution

[`benchmarks/v1.8.0/`](v1.8.0/) measures operation-specific admission, lease overhead, direct cancellation, Adaptive partial grants, nested participation, scheduler behavior, concurrent Runtime scaling, fairness diagnostics, deterministic exact grants, and true machine-pressure governed versus ungoverned execution.

The publication workflow performs untimed warmups, alternates paired measurement order, retains raw samples and the randomization seed, counts real callback participants, defines throughput as completed Runtime operations divided by elapsed seconds, and reports paired bootstrap 95% confidence intervals. Negative and inconclusive performance results remain visible.

Older benchmark suites remain available for regression validation.

## Retained v1.4.0 parallel algorithm suite

[`benchmarks/v1.4.0/`](v1.4.0/README.md) benchmarks every v1.4 public algorithm against a sequential reference and the available SmartParallel scheduler backends. Each repetition is checksum validated.

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v14_algorithm_release_validation.bat 7
```

Results are written to `validation/output/v1.4.0_parallel_algorithms.csv` and the matching `_raw.csv` file. The suite is manual and is not a CI merge gate.

## Retained v1.1.0 real-world suite

[`benchmarks/v1.1.0/`](v1.1.0/README.md) covers OpenCV image pipelines, LZ4 compression, custom BVH construction, and a custom particle simulation.

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

The authoritative report is [`docs/v1.1/benchmarks.md`](../docs/v1.1/benchmarks.md).

## Historical v1.0.0 suite

[`benchmarks/v1.0.0/`](v1.0.0/README.md) preserves the original OpenCV, scientific, stress, overhead, decision-quality, CSV, and figure suite for historical comparison.
