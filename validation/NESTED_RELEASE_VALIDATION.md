# SmartParallel v1.1 pre-integration release validation

This pass is limited to cancellation, shutdown/reentrancy, bounded runtime retention, and preservation of existing backend validation.

## Local correctness and sanitizer gates

Validation date: 2026-07-21.

- GCC 14.2 Release build: **passed**.
- Release CTest: **14/14 passed**.
- ASan + UBSan focused release-gate test: **passed**.
- ASan + UBSan shutdown/reentrancy test: **passed**.
- ThreadSanitizer focused release-gate test: **passed**.
- ThreadSanitizer shutdown/reentrancy test: **passed**.
- ThreadPool traced benchmark smoke: **passed**, correct checksums and confirmed actual backend.
- StaticThread traced benchmark smoke: **passed**, correct checksums and confirmed actual backend.

oneTBB was not installed in the local container. The Windows required-oneTBB command remains the real oneTBB release gate.

## Cache bounds validated

The focused test verifies the limits defined in `smart::runtime_limits`:

- profile-cache capacity remains strict under concurrent insertions;
- full pinned capacity rejects new publication rather than overflowing;
- experience records and plans remain bounded under concurrent insertion;
- new experience can be learned after eviction;
- disabled exploration creates no state;
- enabled exploration remains bounded;
- trace and per-root frozen-plan storage remain bounded and observable.

## Cancellation and lifecycle validation

The focused tests verify repeated deep exception-triggered cancellation, exact originating exception propagation, exact-once recovery, permit/helper/session cleanup, repeated empty construction/destruction, nested destruction draining, worker-exception propagation through `wait()`, and later independent runtime recovery.

## Complete Windows backend matrix

Run:

```bat
scripts\validation\run_nested_cross_backend_validation.bat 31
```

The command stops on the first failure and executes:

- ThreadPool performance and trace;
- StaticThread performance and trace;
- required real-oneTBB performance and trace;
- cross-backend checksum, backend-authenticity, lease, helper, and timing comparison.

## Individual Windows commands

```bat
scripts\validation\run_nested_release_validation.bat 31
scripts\validation\run_nested_release_validation.bat 3 trace
scripts\validation\run_nested_release_validation.bat 31 static
scripts\validation\run_nested_release_validation.bat 3 trace static
scripts\validation\run_nested_release_validation.bat 31 tbb
scripts\validation\run_nested_release_validation.bat 3 trace tbb
```
