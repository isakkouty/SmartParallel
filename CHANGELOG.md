# Changelog

All notable public changes to SmartParallel are documented here. Detailed internal milestone history is retained in [`docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md`](docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md).

## [1.1.0] — Nested parallelism coordination

### Added

- Automatic coordination of nested `smart::parallel_for` calls under a shared root execution session.
- Root-scoped concurrency budgets, participant leases, bounded helper recruitment, and backend-authenticated traces.
- Automatic nested-frontier selection with safe descendant sequential fast paths.
- Backend-neutral execution through ThreadPool, StaticThread, oneTBB, and sequential fallback.
- Cooperative ThreadPool helping, constrained oneTBB arenas, and deterministic StaticThread fallback behavior.
- Session-local plan reuse, bounded profile retention, stable-plan revalidation, and optional backend calibration.
- `parallel_for_nd` for flattened multidimensional iteration spaces.
- Structured nested diagnostics covering lineage, budgets, leases, chunks, helpers, completion timing, exceptions, and cancellation.
- Real-world v1.1 benchmarks for OpenCV image processing, LZ4 compression, custom BVH construction, and custom particle simulation.

### Improved

- Exactly-once cold learning and analytical first-run planning for nested and coarse recursive workloads.
- Weighted OpenCV work decomposition and reduced descendant dispatch overhead.
- Stable timed-phase benchmark behavior by freezing calibration and revalidation after warm-up.
- CSV schemas, invariant numeric formatting, backend authenticity checks, CPU-time reporting, and reproducible Python figures.
- Release hardening for exception propagation, cancellation recovery, deep nesting, mixed backends, cache bounds, and shutdown safety.

### Compatibility

- The primary API remains `smart::parallel_for(begin, end, callback)`.
- C++17 and CMake 3.20+ remain the minimum language and build requirements.
- The installable CMake target remains `SmartParallel::smart_parallel`.

### Known limitations

- Concurrency admission is enforced per root session, not globally across unrelated external roots.
- Strict frontier sealing can leave some opportunity unused on highly skewed trees.
- Runtime configuration is process-global and must be established before concurrent execution.
- The experience database is in-memory by default; optional persistence APIs exist but are not the default release workflow.
- Automatic execution is not guaranteed to match the fastest manually selected strategy for every workload.

## [1.0.0] — Automatic loop optimization

- Introduced adaptive index-range `parallel_for`.
- Added callback profiling, workload analysis, candidate-plan prediction, and runtime strategy selection.
- Added ThreadPool, StaticThread, oneTBB, and sequential execution paths.
- Added bounded runtime experience, diagnostics, validation programs, and the original OpenCV/scientific benchmark suite.

[1.1.0]: docs/v1.1/release-notes.md
[1.0.0]: docs/v1.0/README.md
