# Changelog

All notable public changes to SmartParallel are documented here. Detailed internal milestone history is retained in [`docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md`](docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md).

## [1.5.0] — Adaptive execution routes

### Added

- Optional semantic vision module exported separately as `SmartParallel::vision` through the `SmartParallelVision` CMake package.
- `smart::vision::threshold` with exact one-channel `uint8_t` Binary and BinaryInverse semantics over contiguous, strided, disjoint, or exact in-place image views.
- Automatic complete-route selection across Native Sequential, Native ThreadPool, Native oneTBB, and optional OpenCV `cv::threshold`; Native StaticThread remains forceable for diagnostics.
- Bounded sharded route learning with two priming calls, balanced successive elimination, adaptive 3–11 sample windows, median/MAD bounds, independent winner holdout, conservative equivalent-route preference, single-flight probing, and bounded profile retention.
- Sparse performance-drift sentinels and four-invocation current-context ABBA revalidation, allowing a stable profile to replace an earlier winner when current runtime conditions change.
- Runtime-selected AVX2, SSE2, and portable branchless Native threshold kernels shared by every Native scheduler route.
- Zero-copy OpenCV `cv::Mat` views, provider build/runtime fingerprinting, cached provider state, and explicit `refresh_provider_state()` invalidation.
- Thread-local stable-route hot dispatch and operation diagnostics covering route, cache, learning, holdout, drift, revalidation, participants, chunks, timing state, and authentication.
- Route-training telemetry with training/current baselines, per-route statistics, holdout evidence, latest current-context comparison, and route-switch count.
- Deterministic validation for exact semantics, SIMD paths, route learning, noisy/outlier inputs, holdout reversal, distribution shift, concurrency, nested fallback, package consumption, and publication probe isolation.
- Proof-driven publication workflow with identical memory for every route, an independent sequential oracle, balanced deployment and steady-state orders, batched adjacent ABBA/BAAB dispatch measurement, raw schema v6, learning schema v2, Markdown/CSV evidence, and dependency-free SVG figures.
- Release documentation with accepted results, methodology, environment metadata, source hashes, generated benchmark assets, and a focused `v15_adaptive_threshold` example.

### Changed

- Extended SmartParallel from scheduler selection alone to complete-implementation selection for recognized semantic operations, without changing `IExecutionBackend` or the v1.4 generic algorithms.
- Stable Auto calls now use a compact exact thread-local key before full provider/profile construction and remain timer-free except for sparse drift sentinels.
- Equivalent route choices prefer Native Sequential, then Native parallel routes, then external providers when measured differences remain inside the configured equivalence band.
- The publication workflow learns under repeated calls, adapts under a balanced interleaved deployment regime, waits for a clean settled streak, and pauses maintenance only for the final timed matrix.

### Compatibility

- Existing `parallel_for` and v1.4 algorithm APIs are unchanged.
- The core `SmartParallel::smart_parallel` package remains independent of OpenCV and the vision module.
- OpenCV and `SmartParallel::vision` remain opt-in.
- Native-only builds provide the same public threshold API with OpenCV excluded from the candidate set.

### Accepted validation

- Complete deterministic suite: **18/18 passed**.
- Accepted Windows/MSVC publication: **2,238 correct and authenticated samples**.
- Route selection: **6/6 passed**.
- Native kernel versus independent oracle: **6/6 passed**.
- Stable Auto dispatch: **6/6 passed**.
- Combined release gate: **6/6 passed**.
- Recorded machine: 16 logical threads, SmartParallel worker budget 16, OpenCV 4.12.0, OpenCL disabled, authenticated AVX2 Native kernel.
- Auto achieved a **1.16× geometric-mean speedup over the independent sequential loop** and **1.48× over direct OpenCV** on the recorded machine.
- The two 1080p profiles initially selected OpenCV, detected a changed deployment regime, and switched once to Native Sequential using current-context evidence.
- Clang warnings-as-errors, ASan/UBSan, Native-only and OpenCV-enabled package consumers, core dependency isolation, and the retained v1.4 smoke matrix passed.

### Limitations

- v1.5 begins with one semantic operation; arbitrary `parallel_transform` lambdas remain Native-only because equivalence to a specialized provider cannot be proven safely.
- Route learning is in-process and root-only. Persistent operation profiles and coordinated external-runtime participant leasing remain future work.
- Performance choices and speedups are machine-specific.

## [1.4.0] — Parallel algorithm expansion

### Added

- `parallel_for_each`, unary/binary `parallel_transform`, `parallel_copy`, `parallel_fill`, and indexed `parallel_generate`.
- `parallel_reduce` and unary/binary `parallel_transform_reduce` with ordered chunk combination.
- `parallel_count`, `parallel_count_if`, `parallel_any_of`, `parallel_all_of`, and `parallel_none_of`.
- `parallel_find` and `parallel_find_if` with earliest-index result semantics.
- Dedicated cross-backend correctness, nested execution, exception, sanitizer, installed-package, and benchmark validation.
- A versioned v1.4 benchmark matrix with sequential, automatic, ThreadPool, StaticThread, and oneTBB modes.

### Fixed

- Added a scheduler-approved direct sequential range path for v1.4 algorithms. When automatic scheduling selects sequential execution, the algorithm now uses one-pass standard-library or iterator traversal instead of executing logical chunks through per-chunk dispatch, partial-result storage, or search/predicate atomics.
- Deferred per-chunk callable copies and reduction partial storage until a chunk is actually executed, keeping the cached sequential path close to the direct sequential baseline without changing parallel plans or backend behavior.
- Added deterministic validation that cached automatic sequential plans preserve correctness for reduce, count, and search.
- Added an Auto-only, root-only algorithm hot-dispatch cache for `parallel_reduce`, `parallel_count`, `parallel_find`, `parallel_find_if`, `parallel_any_of`, `parallel_all_of`, `parallel_none_of`, and `parallel_copy`. It learns from separate real complete scheduled and sequential invocations, then bypasses chunk construction and scheduler entry when direct sequential execution wins.
- Added bounded, sharded dispatch retention, workload/byte buckets, policy and profile-cache epoch invalidation, single-flight probes, hysteresis, and periodic revalidation.
- Corrected `parallel_copy` learning by measuring actual sequential copy time instead of inferring it from a bandwidth-limited parallel execution; scheduled chunks now use `std::copy` so contiguous iterators retain standard-library optimizations.
- Reduced search and predicate coordination overhead with relaxed atomics while retaining scheduler-completion synchronization.
- Cached Windows hardware topology discovery for the process lifetime, matching the existing Linux and macOS behavior.

### Architecture

- All algorithms reuse the existing adaptive scheduler, backend abstraction, runtime learning, nested coordinator, concurrency budgets, ThreadPool helping, StaticThread engine, and oneTBB arenas.
- Algorithms submit bounded contiguous logical chunks through one internal scheduler adapter; no second execution model was introduced.
- Existing `parallel_for`, scheduler policy, backend logic, and v1.1 benchmark algorithms remain unchanged.

### Semantics

- v1.4 algorithms require random-access iterators.
- Reduction operations must be associative; chunk and input ordering are preserved but parenthesization may differ from a sequential fold.
- Predicate and search algorithms use best-effort short-circuiting, and search returns the earliest match.
- User callables may execute concurrently and must synchronize shared mutable state.

### Validation

- GCC Release passed the complete 17-test deterministic suite.
- Clang Release and Clang ASan+UBSan passed the v1.4 algorithm validation.
- The installed CMake package consumer compiled and executed v1.4 transform and reduction APIs.
- Local GCC and Clang benchmark matrices passed checksum validation for every API.
- The accepted Windows/MSVC Release snapshot passed all 80 summary rows and 560 raw samples with checksum correctness and backend authentication.
- The eight parallel-selected automatic cases achieved a 3.30× geometric-mean speedup; all eight corrected cheap-dispatch families were within 3.5% of direct sequential latency or faster.
- Added a versioned v1.4 benchmark report, reproduction guide, PNG/SVG figures, generated metrics, and source hashes.

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

[1.5.0]: docs/v1.5/release-notes.md
[1.4.0]: docs/v1.4/release-notes.md
[1.3.0]: docs/v1.3/release-notes.md
[1.1.0]: docs/v1.1/release-notes.md
[1.0.0]: docs/v1.0/README.md
