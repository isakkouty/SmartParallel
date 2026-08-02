# Changelog

## 1.8.0 — Governed Scientific Execution

### Added

- Explicit process-level `ResourceGovernor` with effective CPU-capacity diagnostics.
- Move-only, exception-safe root and inherited execution leases.
- Operation-specific minimum, preferred, maximum, requested, granted, capped, and observed worker fields.
- Flexible Adaptive partial grants and exact fail-closed Deterministic grants.
- Immediate, waiting, deadline, cancellation, impossible-request, and shutdown outcomes.
- Direct cancellation wakeups without periodic polling.
- FIFO admission with bounded bypass, aging, and oldest-request reservation.
- Shared multi-Runtime budgets with isolated profiles, adaptive state, and numerical configuration.
- Nested lease inheritance without independent blocking root acquisition.
- oneTBB task-arena upper-bound reporting and serialized OpenCV single-thread containment.
- Stable resource fingerprints, decision reports, and deployment-manifest fields.
- Publication benchmark schema with alternating pair order, real participation measurement, correct throughput definitions, raw samples, 95% bootstrap intervals, fourteen Linux publication SVG figures, and a plot manifest.
- Expanded governor, Runtime, cancellation, fairness, nested, deterministic, backend, effective-capacity, sanitizer, package-consumer, and exact-archive validation.

### Changed

- Flexible operations no longer reserve the Runtime ceiling blindly; useful concurrency is estimated from operation size, scheduler requirements, nesting state, and the Runtime ceiling.
- A governed backend running with one participant retains its authenticated scheduler identity instead of being rewritten as Sequential.
- Public concurrent sibling partitioning is not exposed until strict delegated-capacity accounting is proven across all scheduler paths.
- Benchmark statuses are `PASS`, `FAIL`, `INCONCLUSIVE — NO MATERIAL REGRESSION DETECTED`, or `INCONCLUSIVE — MORE EVIDENCE REQUIRED`.
- Source packaging excludes all validation output, build/install trees, binaries, caches, nested archives, Python bytecode, and dependency trees.

### Scope

- v1.8 is exclusively process-level CPU governance.
- Rodinia HotSpot is removed from the v1.8 artifact and preserved separately for `SmartParallel v1.9.0 — Rodinia HotSpot Integration`.
- OpenMP expansion, BLAS, FFT, NUMA scheduling, affinity, GPU, MPI, and cross-process governance remain deferred.

### Compatibility

- All accepted v1.0–v1.7 APIs and tests remain supported.
- `worker_budget` remains accepted; `maximum_workers` is the explicit v1.8 Runtime ceiling.
- v1.7 profiles are adapted only when their resource meaning is unambiguous; incompatible deterministic contracts fail closed.

All notable public changes to SmartParallel are documented here. Detailed internal milestone history is retained in [`docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md`](docs/archive/v1.1-development/CHANGELOG_DEVELOPMENT_HISTORY.md).

## [1.7.0] — Reproducible Runtime

### Added

- Owned `RuntimeOptions`, `Runtime`, and lightweight copyable `ExecutionContext` with construction-time configuration isolation.
- Context-aware parallel, numerical, scientific, and Vision overloads while preserving existing free functions through the process-default Runtime path.
- Adaptive and Deterministic execution modes with zero-maintenance exact-plan replay telemetry.
- Disabled, ReadOnly, and ReadWrite profile policies; exact semantic-operation profiles for threshold, AXPY, dot, norm, and stencil 2D.
- Candidate and Approved trust states with explicit approval tooling.
- Bounded strict canonical JSON parsing, duplicate-key rejection, SHA-256 entry/database integrity, and atomic explicit persistence.
- Runtime, workload, operation, profile, and experiment fingerprints excluding volatile process details.
- `smartparallel_calibrate`, `smartparallel_profile`, and `smartparallel_replay` installed tools.
- Separate-process heat-diffusion calibration, approval, replay, and byte-identical manifest comparison.
- Runtime/context overhead, cold/warm/deterministic, construction, and profile-scale benchmarks with generated SVG analysis.
- Fresh repetition-matched cold/warm/deterministic benchmark sampling with rotating order, output equality checks, and deterministic bootstrap acceptance intervals.
- v1.7 Runtime, Vision, parser, persistence, exact-failure, fingerprint, and cross-process validation.

### Accepted validation

- Final Windows/MSVC `31 full` publication: **24/24** main tests, **24/24** isolated no-oneTBB/no-OpenCV tests, **3/3** oneTBB + OpenCV focused tests, and **6/6** exact returned source-ZIP tests passed.
- Installed core, profile, Vision, and OpenCV consumers passed; installed calibration, explicit approval, two fresh Deterministic replay processes, manifest comparison, and unchanged Approved profile checks passed.
- Every v1.7 benchmark objective was accepted. Adaptive warm start measured **2.600×** faster than fresh cold Adaptive execution with a 95% interval of **2.500–2.764×**; Deterministic replay measured **1.014×** warm latency with a **0.959–1.084×** interval.
- The 1,000-entry exact profile database loaded in **713.304 ms**, with a 95% interval of **708.684–716.005 ms**. Explicit Runtime and copied-context overhead intervals crossed the 20 µs objective and were reported honestly as `NOT-ESTABLISHED`.
- Two fresh replay manifests were byte-identical with SHA-256 `caa94172f51f4a161658ed39fff102340186ea6f3bba4f327a5a3fa2694e898c`, identical output digest, nine deterministic replays, and zero adaptive maintenance counters.
- The retained v1.6 Windows suite produced **3,936 samples** and passed every execution-validity, reference-accuracy, reproducibility, route-authentication, numerical-capability, cross-scheduler, pointwise-plan, Fast-compatibility, and scientific-kernel performance-sanity gate.
- An independent Linux/GCC publication also accepted every v1.7 objective and retained v1.6 gate.

### Documentation

- Reorganized the v1.7 documentation into a release-quality Runtime, profiles/trust, evidence/tooling, validation, migration, and limitations hierarchy.
- Added a complete public Runtime API guide and release-validation matrix.
- Published separate Windows/MSVC and Linux/GCC benchmark evidence records.
- Embedded all nine generated v1.7 benchmark figures in the release documentation and added the accepted Windows cross-process evidence.

### Compatibility and limits

- All v1.0–v1.6 APIs and validated kernels remain available.
- Explicit Runtime instances are isolated from later legacy global-configuration changes.
- Multiple Runtimes are not governed by one process-wide CPU budget.
- SHA-256 detects modification but does not authenticate authorship.
- Public named execution scopes remain optional deferred work.

## [1.6.0] — Scientific Foundations

### Added

- Public per-operation `NumericalPolicy::{Fast, Reproducible, Accurate}` presets and `NumericalOptions` without mutable global numerical configuration.
- Versioned deterministic reduction plans with fixed 1024-element leaves, indexed partial storage, and fixed merge trees independent of worker count and scheduler timing.
- Separate deterministic pointwise plans for one-dimensional AXPY and two-dimensional stencil work, allowing scheduler-independent result bits without serializing large grids.
- Deterministic Neumaier-style compensated accumulation for supported Accurate floating sums and dot products, including explicit NaN/infinity classification.
- Deterministic scaled sum-of-squares Accurate norm that avoids avoidable overflow and underflow from naïve squaring.
- Experimental host-only `smart::data::View<T, Rank>`, `VectorView`, and `MatrixView` with element strides, overflow validation, const conversion, contiguous helpers, declared alignment, and conservative overlap reporting.
- Experimental `smart::linalg::axpy`, `smart::linalg::dot`, `smart::linalg::norm`, and `smart::scientific::stencil_2d` APIs for float and double contiguous/strided views.
- Backward-compatible `ImageView`/matrix-view adapters without changing the validated v1.5 threshold implementation.
- A complete 2D heat-diffusion pilot with ping-pong views, fixed boundaries, independent reference validation, policy/plan reporting, checksum, and application timing.
- Schema-v2 benchmark evidence with separate execution-validity and reference-accuracy fields, complete-output digests, cross-scheduler reduction/pointwise matrices, generated reports, and nine SVG plots.
- Reproducible ZIP creation with normalized timestamps, deterministic ordering, and preserved executable permissions.

### Corrected before release

- Replaced the accidental use of the reduction leaf size for Reproducible/Accurate AXPY and stencil with dedicated fixed pointwise tiles.
- Authenticated real parallel pointwise execution across eligible scheduler engines and worker budgets.
- Replaced single-element AXPY/stencil/heat publication checks with complete logical-output validation outside timed regions.
- Separated IEEE execution validity from independent-reference accuracy so cancellation-sensitive Fast results are not mislabeled.
- Replaced order-biased Fast regression timing with adjacent alternating policy-aware and retained-overload samples.
- Reclassified the earlier Windows schema-v1 publication as historical pre-correction evidence and retained the current Windows/MSVC schema-v2 benchmark, main-test, documentation, and package-consumer evidence separately.
- Normalized source-archive timestamps to prevent CMake/Ninja regeneration loops after cross-time-zone extraction.
- Hardened the Windows v1.6 release workflow by removing fragile batch-label dispatch, enforcing CRLF command-file line endings, and isolating the no-oneTBB/no-OpenCV matrix from vcpkg auto-integration.
- Removed the MSVC `getenv` deprecation warning from the scientific benchmark and retained real-world environment capture.
- Added a dedicated clean source-release packager that excludes build, dependency, install, and generated validation-output trees.
- Removed repeated checked `View` indexing from scientific hot loops: AXPY, dot, norm, and stencil now validate extents, strides, overlap, and address spans once at operation entry, then execute through validated pointer/stride kernels.
- Added non-unit-column-stride stencil coverage and a broad largest-workload performance-sanity gate that fails when any Fast scientific kernel falls below 0.5× its compact direct-sequential reference.

### Compatibility

- Existing overloads remain source-compatible and default to retained Fast behavior.
- Existing v1.4 adaptive algorithms, hot dispatch, nested scheduling, and direct sequential routes remain intact.
- The v1.5 Vision/OpenCV provider architecture remains optional and isolated from the core package.
- Package names and exported targets are unchanged.

### Numerical contract

- Reproducible and Accurate are bitwise repeatable only under the documented same-binary, same-architecture, same-floating-environment scope.
- Accurate is offered only for recognized meaningful operations; unsupported custom Accurate reductions fail clearly and never degrade silently.
- Accurate AXPY and stencil intentionally share the Reproducible fixed pointwise expression.

### Accepted validation

- Corrected Linux/GCC 14.2 schema-v2 publication: **2,442 raw samples**.
- Complete deterministic suite: **20/20 passed**.
- Every execution-validity, required-reference, reproducibility, route-authentication, numerical-capability, adversarial-accuracy, cross-scheduler, and pointwise-plan gate passed.
- Accurate reduced the predefined adversarial sum and dot absolute errors from **3000 to 0**.
- Policy-aware Fast / retained Fast produced a paired median of **1.0634×** with a 90% robust interval of **0.9739–1.1611×**; the gate is an honest **not-established**, not evidence of a regression above 5%.
- The largest Fast AXPY, dot, norm, stencil, and heat workloads passed the broad performance-sanity gate with accepted machine-specific speedups of **1.19×**, **2.35×**, **2.96×**, **3.78×**, and **2.14×** versus compact direct-sequential references.
- Full AXPY vectors, stencil fields, and heat-diffusion fields were validated and recorded with complete-output digests.
- The corrected heat pilot was correct, reproducible, authenticated, and parallel; on the accepted Linux machine, Fast completed the largest 20-iteration workload about **2.14×** faster than the compact direct-sequential oracle. The result remains machine-specific and is not a universal speedup claim.

### Limitations

- No cross-compiler or cross-architecture bitwise guarantee.
- Canonical reductions use bounded temporary allocation; caller workspaces remain future work.
- Scientific APIs remain experimental and do not promise ABI stability in v1.6.
- No production-readiness, safety-critical, hard-real-time, GPU, MPI, OpenMP, BLAS, FFT, C, or Python claim is made.

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

[1.6.0]: docs/v1.6/release-notes.md
[1.5.0]: docs/v1.5/release-notes.md
[1.4.0]: docs/v1.4/release-notes.md
[1.3.0]: docs/v1.3/release-notes.md
[1.1.0]: docs/v1.1/release-notes.md
[1.0.0]: docs/v1.0/README.md
