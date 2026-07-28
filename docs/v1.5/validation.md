# v1.5 validation

The deterministic target is:

```text
smartparallel_v150_vision_adaptive_routes_validation
```

It validates:

- two untimed priming observations per route;
- balanced successive-elimination learning with adaptive 3–11 sample windows;
- median/MAD bounds, outlier resistance, conservative equivalent-route preference, and independent holdout verification;
- failed holdout reopening, order independence, and provisional-winner reversal;
- runtime-selected AVX2/SSE2/branchless-scalar native kernels across contiguous/strided and disjoint/in-place Binary and BinaryInverse semantics;
- an MSVC `/Qvec-report:2` probe translation unit, plus an x86-64 deterministic gate that requires explicit SIMD selection;
- operation-specific profile separation;
- sparse drift detection, current-context stable/challenger ABBA revalidation, bounded distribution-shift recovery, and route promotion;
- single-flight exploration, concurrent-fallback measurement isolation, cancelled-probe recovery, and bounded profile retention;
- exact threshold output for Native Sequential, ThreadPool, and StaticThread;
- Native oneTBB and OpenCV when compiled in;
- automatic learning and authenticated selected routes;
- empty, one-pixel, odd-sized, and non-divisible workloads;
- exact in-place thresholding and non-contiguous rows without modifying padding;
- invalid dimensions, row strides, modes, routes, and partial buffer overlap;
- clear failure for an unavailable forced OpenCV route;
- safe Native Sequential fallback for nested automatic calls;
- installed-package consumption through `SmartParallel::vision`;
- a 31-call probe-free publication window after maintenance is explicitly paused.

Timing thresholds are not used by deterministic tests. The selector uses injected observations, including a regime where OpenCV wins initially, later slows sharply, triggers sparse drift sentinels, completes current-context ABBA revalidation, and switches to Native within bounded calls.

## Publication evidence validation

Real performance is evaluated only by the publication benchmark. Raw schema v6 carries:

- output checksums and exact mismatch counts;
- exploration, holdout, drift, and revalidation flags;
- exact source/destination addresses and alignment classes;
- initial-learning and deployment-settling call counts;
- route-switch count and Native-kernel identity;
- balanced steady-state order evidence;
- batched ABBA/BAAB block metadata.

Learning schema v2 records each candidate's training and current medians, MAD, range, measured/priming/holdout/current counts, active state, verification failures, drift state, and latest current-context comparison.

The analyzer rejects correctness failures, authentication failures, probe leakage, unfair memory, unbalanced route positions, inconsistent kernel identity, or malformed dispatch batches before evaluating performance.

## Accepted release matrix

The accepted Windows/MSVC publication run passed:

- **18/18 deterministic CTest targets**;
- **2,238/2,238 correct and authenticated benchmark samples**;
- **6/6 route-selection gates**;
- **6/6 native-kernel gates**;
- **6/6 stable-dispatch gates**;
- **6/6 combined release gates**.

Clang warnings-as-errors, Clang ASan/UBSan, native-only vision packaging, OpenCV-enabled packaging, installed-package consumers, core-only dependency isolation, and the retained v1.4 benchmark smoke matrix also passed during release preparation.

See [benchmark results](benchmarks.md), [methodology](benchmark-methodology.md), and [reproduction](benchmark-reproduction.md).
