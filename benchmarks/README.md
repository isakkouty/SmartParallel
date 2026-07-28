# SmartParallel benchmark suites

## Current release suite: v1.5.0 adaptive execution routes

[`benchmarks/v1.5.0/`](v1.5.0/) evaluates exact `uint8_t` thresholding across Native SmartParallel routes and the optional OpenCV provider. The publication workflow validates correctness, route authentication, identical memory conditions, initial learning, runtime adaptation, Native-kernel quality, and stable Auto dispatch overhead.

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v15_adaptive_routes_release_validation.bat 31
```

Results are written to a timestamped directory and ZIP under:

```text
validation/output/v1.5.0_adaptive_routes/
```

The authoritative public report is [`docs/v1.5/benchmarks.md`](../docs/v1.5/benchmarks.md), with methodology in [`docs/v1.5/benchmark-methodology.md`](../docs/v1.5/benchmark-methodology.md).

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
