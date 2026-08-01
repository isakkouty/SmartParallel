# SmartParallel v1.6 validation

## Deterministic test coverage

The v1.6 focused tests cover:

- zero, one, odd, boundary-adjacent, and large reduction lengths;
- fixed results across worker budgets 1, 2, 3, 4, and 8 where applicable;
- ThreadPool, StaticThread, and oneTBB when available;
- generic Reproducible reductions and unsupported Accurate rejection;
- compensated cancellation improvements;
- NaN, infinity, signed-zero, overflow, and underflow behavior;
- robust norm cases where naïve squaring overflows or underflows;
- rank-1 and rank-2 contiguous and strided views;
- null/zero views, overflow rejection, const conversion, indexing, unique mapping, and overlap classification;
- float/double AXPY, exact same mapping, striding, alpha edge cases, and overlap rejection;
- strided dot and norm;
- 0×0, 1×1, 1×N, N×1, 2×2, odd, contiguous, and padded stencil layouts;
- full bitwise AXPY and stencil comparison across eligible schedulers and worker budgets;
- authentication of `canonical-pointwise-v1-target4096` and `canonical-pointwise-2d-v1-target4096`;
- multi-step heat-diffusion integration;
- optional `ImageView` adapter round trips.

Tests do not depend on timing thresholds.

## Corrected release-candidate validation

The exact corrected working tree completed:

- complete deterministic CTest suite: **20/20 passed**;
- focused scientific foundation tests: **Pass**;
- v1.5 Vision regression: **Pass**;
- v1.4 algorithm regression: **Pass**;
- Reproducible heat pilot: **Pass**;
- Accurate heat pilot: **Pass**;
- schema-v2 benchmark/analyzer workflow: **Pass**;
- documentation/evidence validation: **Pass** after final asset publication;
- clean source archive extraction and rebuild: required final packaging gate;
- Windows command-file CRLF validation: required;
- isolated no-oneTBB/no-OpenCV configuration authentication: required;
- generated build, dependency, install, and `validation/output` trees excluded from source releases.

## Accepted performance evidence

The accepted corrected Linux/GCC/x86-64 publication contains **2,442/2,442 samples** and passed:

- execution validity;
- required reference accuracy;
- reproducibility;
- route authentication;
- numerical capability;
- adversarial Accurate improvement;
- sum cross-scheduler matrix;
- AXPY pointwise matrix;
- stencil pointwise matrix;
- pointwise-plan authentication;
- Fast compatibility investigation gate.

The historical Windows schema-v1 evidence remains pre-correction traceability only. A later Windows/MSVC schema-v2 run is retained as workflow evidence: the main and isolated no-oneTBB/no-OpenCV suites each passed 20/20 tests, together with 3,936 benchmark samples, documentation validation, and both installed consumers. Because that run predates the final validated pointer/stride kernels and performance-sanity gate, rerun the final source before publishing current Windows performance evidence.

## Claim boundary

A successful run on one compiler does not substitute for another compiler or hardware platform. Performance results are machine-specific. Bitwise reproducibility remains limited to the documented same-binary, same-architecture, same-floating-environment scope.

## Scientific-kernel performance sanity

For the largest Fast AXPY, dot, norm, stencil, and heat workloads, the publication analyzer requires SmartParallel to remain at least 0.5× the compact direct-sequential reference. This gate is intentionally tolerant of machine variation but rejects catastrophic hot-loop regressions.
