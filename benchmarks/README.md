# SmartParallel benchmark suites

## v1.4.0 parallel algorithm suite

[`benchmarks/v1.4.0/`](v1.4.0/README.md) benchmarks every v1.4 public algorithm against a sequential reference and the available SmartParallel backends. Each repetition is checksum validated.

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v14_algorithm_release_validation.bat 7
```

Results are written to `validation/output/v1.4.0_parallel_algorithms.csv` and the matching `_raw.csv` file. The suite is manual and is not a CI merge gate.

## Current performance suite: v1.1.0 (retained for v1.3)

The current recorded performance suite is [`benchmarks/v1.1.0/`](v1.1.0/README.md). Its real-world integrations cover OpenCV image pipelines, LZ4 compression, custom BVH construction, and a custom particle simulation.

The authoritative public report is [`docs/v1.1/benchmarks.md`](../docs/v1.1/benchmarks.md). Methodology and reproduction instructions live in the same documentation tree rather than being duplicated here.

Run the complete Windows suite from the repository root:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

Results are written to:

```text
validation/output/real_world/
```

Regenerate the release figures with:

```text
python tools/plot_real_world_results.py
```

## Historical suite: v1.0.0

[`benchmarks/v1.0.0/`](v1.0.0/README.md) preserves the original OpenCV, scientific, stress, overhead, decision-quality, CSV, and figure suite. It remains available for historical comparison but is not the current release benchmark.
