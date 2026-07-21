# SmartParallel v1.1 final production-hardening manifest

This source tree is based on `SmartParallel(160).zip`. It preserves the existing root-session, lease, lineage, frozen-plan, conservative-frontier, telemetry, and tracing architecture.

## Stable plans and cache

- `include/smart/profiling/function_profile_cache.hpp`
  - bounded least-recently-used profile retention;
  - protected build/revalidation entries;
  - single-flight stable-plan revalidation;
  - immediate invalidation on contradictory classification;
  - decaying nested-call evidence;
  - one observation count per root execution group;
  - saturating counters and access epochs.
- `include/smart/execution/parallel.hpp`
  - scheduler-policy signatures in profile and frozen-plan keys;
  - stronger callable identity for function pointers and `std::function`;
  - public `smart::with_parallel_callsite(key, callback)` wrapper for reusable functors;
  - policy-sensitive stable-plan reuse and conservative revalidation.
- `include/smart/execution/nested_execution_session.hpp`
  - full collision-safe snapshot keys include the policy signature;
  - bounded per-root frozen-plan storage;
  - bounded global trace retention.
- `include/smart/core/config.hpp`
  - cache, snapshot, trace, and nested-evidence retention controls.

## Backend consistency and lifetime safety

- `include/smart/execution/executor.hpp`
  - `StaticChunks` now uses the StaticThread backend/session path rather than bypassing permits.
- `include/smart/execution/backend.hpp`
  - oneTBB arena reuse is constrained by the acquired root-session width;
  - StaticThread partial-spawn failures join all created threads before rethrowing;
  - overflow-safe ThreadPool grain calculation.
- `include/smart/execution/work_chunk.hpp`
  - bounded compare-and-exchange chunk acquisition;
  - overflow-safe chunk count and range-end calculations.

## Build and validation

- `CMakeLists.txt`
  - project version aligned to `1.1.0`;
  - `SMARTPARALLEL_REQUIRE_TBB` prevents false backend validation through fallback.
- `tests/v1/nested_production_stress.cpp`
  - long-running cache/trace retention;
  - revalidation concurrency;
  - policy drift and explicit callsite identity;
  - near-limit chunk arithmetic;
  - StaticThread lease and exception paths;
  - randomized irregular concurrent roots;
  - conditional oneTBB arena-budget validation.
- `tests/CMakeLists.txt`
  - registers the production stress test; full release suite contains 12 tests.
- `scripts/validation/run_nested_release_validation.{bat,sh}`
  - normal, trace, and required-oneTBB modes;
  - separate output names so TBB runs cannot overwrite ThreadPool results.

## Documentation

- `V1_1_FINAL_PRODUCTION_HARDENING.md`
- `V1_1_NESTED_RELEASE_NOTES.md`
- `validation/NESTED_RELEASE_VALIDATION.md`
- relevant v1 configuration, profiling, backend, validation, and limitation pages.

## oneTBB backend selection and validation fix

- Added compile-time backend availability reporting through `execution_backend_available()`.
- Prevented the analytical engine selector and runtime utility policy from selecting oneTBB when it is not compiled.
- Resolved unavailable oneTBB backend requests to ThreadPool instead of silently executing sequentially.
- Added a oneTBB execution counter used by validation to prove that oneTBB actually handled benchmark work.
- Made the v1.1 nested benchmark backend configurable (`thread_pool` or `tbb`).
- Removed hard-coded ThreadPool selection from forced nested loops, root contexts, global configuration, and CSV output.
- Added hard failure when a TBB benchmark is requested from a build without oneTBB.
- Updated the Windows validation batch file to pass the requested backend into the benchmark executable.
- Made the installed CMake package depend on TBB only when SmartParallel was built with TBB support.
