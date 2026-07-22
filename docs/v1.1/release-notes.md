# SmartParallel v1.1.0 release notes

> **Runtime documentation:** SmartParallel v1.1 behavior, retained by the current [v1.3 portability release](../v1.3/README.md).

SmartParallel v1.1.0 completes and stabilizes coordinated nested parallelism while retaining the v1.0 automatic loop-optimization API.

## Highlights

- Shared root sessions coordinate nested loops under a bounded concurrency budget.
- Automatic frontier selection avoids recursive oversubscription.
- ThreadPool, StaticThread, oneTBB, and sequential execution share one backend-neutral contract.
- Cooperative helping, participant leases, cancellation, and exception propagation are release-hardened.
- Runtime profiles, bounded experience, stable plans, and optional warm-up backend calibration reduce repeated decision overhead.
- Structured traces authenticate backend execution and expose budgets, leases, chunks, helpers, and completion timing.
- The final real-world suite validates OpenCV, LZ4, custom BVH, and custom particle workloads.

## Final measured outcome

On the recorded four-worker Windows/MSVC machine, all 20 presets with automatic median runtime of at least 1 ms beat sequential execution. Their geometric-mean automatic speedup was 2.33×, and 19 of 20 were within 20% of the fastest valid tested mode.

See [benchmarks](benchmarks.md) for the complete non-cherry-picked result set and qualifications.

## Upgrade compatibility

Existing `smart::parallel_for` callsites remain compatible. Nested calls may now execute under a shared frontier and should not be assumed to create independent teams.

## Release boundaries

v1.1 uses per-root rather than process-wide admission, keeps configuration process-global, stores experience in memory by default, and does not guarantee that automatic scheduling always beats a manually selected plan.
