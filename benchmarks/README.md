# SmartParallel benchmark suite

This directory contains reproducible application benchmarks for SmartParallel's adaptive `parallel_for` implementation. The recorded snapshot in [`results/data`](results/data) was produced on a machine reporting **16 hardware threads**. Every recorded correctness check passed.

## Recorded results at a glance

| Metric | Recorded result |
| --- | ---: |
| Application cases in decision-quality audit | 24 |
| Correct adaptive backend decisions | 18/24 (75.0%) |
| Output-correct audit cases | 24/24 |
| Median adaptive speedup vs sequential | 0.99× |
| Geometric-mean adaptive speedup vs sequential | 1.41× |
| Maximum adaptive speedup | 10.14× |
| Cached scheduler overhead | 5.8 µs mean |

![Adaptive speedup across audited cases](results/figures/decision_quality_speedup.png)

The results are intentionally mixed. SmartParallel performs best on expensive and irregular workloads, reaching roughly **9–13×** speedups. Very small or memory-sensitive regular loops can remain faster sequentially, and the audit documents those misses rather than hiding them.

## Suites

- [`opencv/`](opencv/README.md): threshold, convolution, Sobel, and six irregular image workloads.
- [`scientific/`](scientific/README.md): numerical integration, heat diffusion, and irregular particles.
- [`decision_quality/`](decision_quality/README.md): forced-backend comparison, prediction diagnostics, regret, and overhead.

## Running the complete suite

From the repository root:

```bat
cmake --preset benchmarks
cmake --build --preset benchmarks
scripts\benchmarks\run_all_benchmarks.bat
```

CSV output is normally written to `validation/output`. The snapshot used by this report is committed under `benchmarks/results/data`; generated figures are under `benchmarks/results/figures` in both PNG and SVG formats.

## Interpretation guidelines

- A speedup above **1×** means SmartParallel is faster than the direct sequential implementation.
- Specialized OpenCV primitives are included as optimized-library references, not as equivalent scheduler implementations.
- Timings are environment-specific. Re-run the suite before making claims about another CPU, compiler, power policy, or dependency version.
