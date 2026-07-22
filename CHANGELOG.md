# Changelog

All notable public changes to SmartParallel are documented here. Detailed internal milestone history is retained in [`docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md`](docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md).

## [1.3.0] — Cross-platform CI and portability

### Added

- GitHub Actions build and correctness-test coverage for Windows/MSVC, Linux/GCC, Linux/Clang, and macOS/Apple Clang.
- Required oneTBB validation on Windows, Linux Release, Clang Release, and macOS, plus a Linux Debug configuration with oneTBB disabled.
- AddressSanitizer and UndefinedBehaviorSanitizer validation with Linux Clang.
- Installed CMake package validation through a separate external consumer project.
- Persistent vcpkg binary caching and a cached fallback vcpkg checkout for hosted runners.
- Cross-platform CI, installation, and portability documentation.
- CI-focused CMake presets for TBB-disabled, TBB-required, and sanitizer builds.

### Fixed

- Exported the standard `Threads::Threads` dependency so installed-package consumers link portably on Unix-like systems.
- Added native hardware discovery on Linux using process affinity, sysfs CPU topology, cache descriptors, NUMA nodes, and `sysconf` page size.
- Added native hardware discovery on macOS using `sysctl` logical/physical CPU, cache, cache-line, and page-size values, with a conservative one-node NUMA fallback.
- Improved Linux and macOS processor-model metadata for manually run real-world benchmarks.
- Made the nested-frontier mechanics test independent of callback timing and exact frontier depth. It now verifies the invariant contract—defer underfilled outer levels, establish a bounded frontier at level 3 or 4, suppress descendants when level 3 is selected, execute exactly once, and stay within the root lease budget—without changing production scheduler behavior.

### Validation

- The final v1.3 pull-request workflow passed all six jobs: Windows/MSVC Release with oneTBB, Linux/GCC Debug without oneTBB, Linux/GCC Release with oneTBB, Linux/Clang Release with oneTBB, macOS/Apple Clang Release with oneTBB, and Linux/Clang ASan+UBSan.
- Each normal platform job passed the 16 deterministic CTest tests, installation, and the external installed-package consumer.
- Real-world performance benchmarks were not executed in CI.

### Compatibility

- Scheduler behavior, nested execution semantics, public APIs, and benchmark algorithms are unchanged. One validation assertion was made compiler- and platform-independent without changing its correctness intent.
- The installable target remains `SmartParallel::smart_parallel`.
- Real-world performance benchmarks remain manual and are not CI merge gates.

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

[1.3.0]: docs/v1.3/release-notes.md
[1.1.0]: docs/v1.1/release-notes.md
[1.0.0]: docs/v1.0/README.md
