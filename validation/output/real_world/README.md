# SmartParallel v1.1 final real-world results

This directory contains the authoritative checked-in v1.1.0 real-world benchmark run used by [`docs/v1.1/benchmarks.md`](../../../docs/v1.1/benchmarks.md).

- Benchmark commit: `f834709fc856`
- Platform: Windows, MSVC 19.44
- Selected worker limit: 4
- Warm-ups / timed repetitions: 3 / 31
- Integrations: OpenCV, LZ4, custom BVH, custom particles

For each integration, the directory contains raw, summary, trace, and environment CSVs. The combined files are:

- `v1.1.0_real_world_comparison.csv`
- `v1.1.0_real_world_auto_analysis.csv`
- `v1.1.0_real_world_analysis.md`

Regenerate the public figures with:

```text
python tools/plot_real_world_results.py
```

Results are machine-specific and should be interpreted with the [methodology](../../../docs/v1.1/benchmark-methodology.md).
