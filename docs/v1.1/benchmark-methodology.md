# Benchmark methodology

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

## Source of truth

The public v1.1 report uses the checked-in final run under `validation/output/real_world/`. The authoritative automatic summary is `v1.1.0_real_world_auto_analysis.csv`; per-integration raw, summary, trace, and environment CSVs provide the underlying observations.

## Environment

The final run used benchmark commit `f834709fc856` and was recorded from `2026-07-22T14:01:40Z` through `2026-07-22T14:08:53Z` on Windows with MSVC 19.44, an AMD64 Family 23 Model 8 processor with 16 logical processors, a selected SmartParallel worker limit of four, oneTBB 2023.0.0, OpenCV 4.12.0, 31 timed repetitions, three warm-ups, and deterministic seed `1511505647`. OpenCV internal threading was set to one so SmartParallel owned the measured parallelism.

## Timing and ranking

- Runtime values are medians unless stated otherwise.
- Speedup is sequential median divided by automatic median.
- Regret is the difference between automatic median and the fastest valid tested mode for the same integration/preset.
- The comparison retains every valid strategy; it does not discard cases where sequential or a manual strategy wins.
- Backend calibration and profile revalidation are permitted during warm-up and frozen during timed repetitions.

## Strategies compared

The matrix includes sequential execution, manual ThreadPool/StaticThread/oneTBB, SmartParallel automatic and forced-backend modes, and—where nested structure exists—outer-only, inner-only, all-level, flattened, and automatic-frontier variants.

## Correctness and authenticity

- OpenCV compares deterministic per-image output hashes and exact-once execution.
- LZ4 performs compress/decompress round trips, byte-for-byte restoration, exact-once checks, and checksums.
- BVH validates tree structure, primitive occurrence, bounds, traversal counts against brute force, and checksums.
- Particles compare the deterministic final state to a sequential reference with absolute tolerance `1e-11` and a quantized checksum.
- Trace rows authenticate the backend that actually executed.
- Summary and trace records enforce the four-participant root budget.

## Interpretation limits

Measurements are machine-specific. Generated inputs exclude disk I/O. The BVH and particle workloads are custom realistic benchmark implementations, not integrations into external engines. Relative regret on sub-millisecond cases can look large even when the absolute difference is a fraction of a millisecond.
