# SmartParallel v1.5.0 release notes

SmartParallel v1.5 — Adaptive Execution Routes extends automatic scheduling into automatic complete-implementation selection for recognized semantic operations.

## Added

- Optional `SmartParallel::vision` module and separate installed `SmartParallelVision` package.
- `smart::vision::threshold` for exact `uint8_t` binary and inverse-binary thresholding.
- Non-owning contiguous and strided image views.
- Automatic comparison of Native Sequential, Native ThreadPool, Native oneTBB, and optional OpenCV routes.
- Forced StaticThread and all other routes for diagnostics.
- Bounded sharded route profiles with two untimed priming observations, balanced successive-elimination rounds, median/MAD confidence bounds, independent winner holdout verification, conservative equivalent-route preference, single-flight probes, sparse drift sentinels, current-context ABBA revalidation, configuration invalidation, and a thread-local stable-route hot cache.
- OpenCV build/runtime fingerprinting and zero-copy `cv::Mat` views.
- Runtime-selected native threshold kernels: AVX2, SSE2, or a portable branchless scalar fallback. Unsigned-byte comparisons use an exact sign-bit transform in explicit SIMD, and contiguous/strided plus disjoint/in-place paths preserve the same semantics across every Native scheduler route.
- Stable hot-route calls use a compact exact thread-local key before full provider/profile construction and bypass internal route timing; external publication timing remains authoritative.
- Cached OpenCV provider state with an explicit `refresh_provider_state()` invalidation API.
- Thread-local operation decision diagnostics, route-training telemetry, and explicit route authentication.
- Deterministic route-selector, distribution-shift, correctness, SIMD, strided-image, forced-route, nested-fallback, and package-consumer tests.
- Proof-driven publication benchmark with an independent sequential oracle, identical memory for every route, balanced initial learning, deployment-regime adaptation, balanced Williams-style steady-state orders, batched adjacent ABBA/BAAB dispatch measurement, learning schema-v2 telemetry, raw schema-v6 evidence, robust confidence bounds, recomputed summaries, Markdown reporting, and dependency-free SVG generation.
- Release documentation with accepted summary data, environment metadata, source hashes, generated figures, benchmark methodology, and reproducible documentation publishing tools.

## Preserved

- Existing `parallel_for` and v1.4 algorithm APIs are unchanged.
- Existing `IExecutionBackend` and scheduler selection remain unchanged.
- The core installed target remains `SmartParallel::smart_parallel` and does not require OpenCV.
- OpenCV and the vision module are opt-in.

## Accepted validation

The accepted Windows/MSVC Release publication run used OpenCV 4.12.0, 16 logical threads, a SmartParallel worker budget of 16, OpenCL disabled, and the authenticated AVX2 Native kernel.

- **2,238 measured samples** passed exact correctness and route authentication.
- **6/6 route-selection gates** passed.
- **6/6 native-kernel gates** passed.
- **6/6 stable-dispatch gates** passed.
- **6/6 combined release gates** passed.
- Auto achieved a **1.16× geometric-mean speedup over the independent sequential loop** and **1.48× over direct OpenCV** on the recorded machine.
- The two 1080p profiles initially learned OpenCV, detected a changed deployment regime, and switched once to Native Sequential using current-context evidence.
- The complete deterministic CTest suite passed **18/18**.
- Clang warnings-as-errors, ASan/UBSan, native-only packaging, OpenCV-enabled packaging, installed-package consumers, and retained v1.4 benchmark smoke validation passed.

See [benchmark results](benchmarks.md), [methodology](benchmark-methodology.md), and [validation](validation.md).

## Interpretation

The v1.5 success criterion is not that Native SmartParallel beats OpenCV. It is that one application call approaches the fastest eligible correct route on the current machine without requiring the application to choose OpenCV or a scheduler manually, and that the framework can revisit that decision when current performance no longer matches its original observations.

The accepted route map is machine-specific. Applications should use Auto rather than hard-code the recorded winners.
