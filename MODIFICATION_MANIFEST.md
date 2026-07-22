# Modification Manifest

This source archive is based on the uploaded `SmartParallel(172).zip` and keeps
its existing build generators, CMake options, vcpkg manifest, benchmark target
names, and Windows batch entry points.

## Core runtime

- `include/smart/core/config.hpp`
  - documents the narrowed one-item root tiny-work bypass contract
- `include/smart/decision/backend_calibration.hpp`
  - added thread-safe freeze/unfreeze state
  - frozen selection reuses only a resolved winner
  - frozen execution cannot record or probe
- `include/smart/execution/parallel.hpp`
  - limits root absolute-cost bypass to one-item nested roots with sequential profitability evidence
  - preserves sealed descendant direct execution and forced-backend semantics
- `include/smart/profiling/function_profile_cache.hpp`
  - adds thread-safe profile-revalidation freeze/unfreeze state
  - cache clear restores production revalidation behavior

## Real-world benchmark infrastructure

- `benchmarks/v1.1.0/real_world/include/real_world_benchmark.hpp`
  - freezes calibration and profile revalidation after warm-up
  - gates batched process CPU metrics by observed timer resolution
  - marks short or physically inconsistent CPU measurements unavailable
  - exports schema-4 environment metadata
- `scripts/benchmarks/compare_real_world_results.ps1`
  - forces invariant culture for combined numeric CSV output
  - writes Markdown through the .NET UTF-8 file API
  - verifies the Markdown analysis file exists and is non-empty
- `scripts/benchmarks/run_real_world_complete.bat`
  - independently verifies the final Markdown report exists and is non-empty
- `benchmarks/v1.1.0/real_world/CSV_SCHEMA.md`
  - documents schema version 4

## Tests

- `tests/v1/real_world_optimization_hardening.cpp`
  - exactly-once pilot cold-root coverage
  - one-item tiny nested-root bypass coverage
  - profitable multi-item root bypass exclusion coverage
  - profile revalidation freeze/unfreeze coverage
  - backend calibration freeze/unfreeze coverage

## Documentation

- `FINAL_HIGH_CONFIDENCE_OPTIMIZATION_UPDATE.md`
- `FINAL_STABILIZATION_UPDATE.md`
- `REAL_WORLD_LOCAL_VALIDATION_FINAL_172.md`
- `CHANGELOG.md`

## Unchanged build entry point

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```
