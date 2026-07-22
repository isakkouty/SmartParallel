# SmartParallel benchmark suites

## Current suite: v1.1.0

The current release benchmark suite is [`benchmarks/v1.1.0/`](v1.1.0/README.md). Its real-world integrations cover OpenCV image pipelines, LZ4 compression, custom BVH construction, and a custom particle simulation.

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
