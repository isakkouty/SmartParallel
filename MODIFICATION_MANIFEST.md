# SmartParallel v1.1 pre-integration hardening manifest

This source tree is based on `SmartParallel(165).zip`. Changes are limited to cancellation, shutdown/reentrancy, bounded runtime caches, and focused regression validation.

## Runtime limits and caches

- `include/smart/core/config.hpp`
  - central explicit production limits for profile, experience, exploration, trace, and plan-snapshot retention;
  - zero-valued retention settings select a bounded default rather than unlimited storage.
- `include/smart/profiling/function_profile_cache.hpp`
  - strict maximum capacity even when all existing entries are actively building or revalidating;
  - rejects an incoming publication when no inactive entry can be evicted.
- `include/smart/decision/exploration_policy.hpp`
  - no state allocation while online exploration is disabled;
  - bounded least-recently-used state while enabled;
  - observable size for validation.
- `include/smart/experience/experience_database.hpp`
  - synchronized query/update/load/save/clear operations;
  - bounded least-recently-used records and plans;
  - safe copy-based query APIs and test diagnostics.
- `include/smart/experience/experience_record.hpp`
- `include/smart/experience/experience_entry.hpp`
  - access-generation metadata used by bounded eviction.
- `include/smart/execution/nested_execution_session.hpp`
  - bounded trace and frozen-plan retention uses central defaults;
  - observable pending-trace and plan-snapshot counts.

## Runtime diagnostics

- `include/smart/execution/thread_pool.hpp`
- `src/thread_pool.cpp`
  - read-only active-job, queued-job, busy-worker, and shutdown-state diagnostics used by lifecycle tests.

## Focused validation

- `tests/v1/preintegration_release_gates.cpp` (new)
  - repeated cross-backend deep cancellation and recovery;
  - exception identity, exact-once, permit/helper/session baseline checks;
  - concurrent strict profile-cache bound;
  - experience and exploration bounds;
  - trace and plan-snapshot limits.
- `tests/v1/nested_shutdown_stress.cpp`
  - empty and repeated construction/destruction;
  - worker exception propagation and recovery;
  - existing nested shutdown and exception-drain coverage retained.
- `tests/CMakeLists.txt`
  - registers the focused release-gate test; complete suite contains 14 tests.

## Documentation

- `V1_1_PREINTEGRATION_RELEASE_GATES.md` (new)
- `CHANGELOG.md`
- `validation/NESTED_RELEASE_VALIDATION.md`

# SmartParallel v1.1 real-world integration additions

## Build and dependency integration

- `CMakeLists.txt`
- `cmake/SmartParallelOptions.cmake`
- `benchmarks/v1.1.0/CMakeLists.txt`
- `benchmarks/v1.1.0/real_world/CMakeLists.txt`
- `vcpkg.json`

Adds optional OpenCV, LZ4, BVH, and particle targets. OpenCV/LZ4 remain
benchmark-only dependencies and are enabled through the vcpkg
`real-world-benchmarks` feature.

## Shared benchmark infrastructure

- `benchmarks/v1.1.0/real_world/include/real_world_benchmark.hpp`

Provides the common CLI, execution modes, cold/warm phases, correctness gate,
backend confirmation, concurrency measurement, statistics, regret calculation,
bounded trace export, and four compatible CSV files per integration.

## Integrations

- `benchmarks/v1.1.0/real_world/src/opencv_image_pipeline.cpp`
- `benchmarks/v1.1.0/real_world/src/lz4_batch_compression.cpp`
- `benchmarks/v1.1.0/real_world/src/bvh_construction.cpp`
- `benchmarks/v1.1.0/real_world/src/particle_simulation.cpp`

## Windows automation and analysis

- `scripts/benchmarks/build_real_world_benchmarks.bat`
- `scripts/benchmarks/run_real_world_integration.bat`
- `scripts/benchmarks/run_real_world_development.bat`
- `scripts/benchmarks/run_real_world_complete.bat`
- `scripts/benchmarks/run_real_world_trace.bat`
- `scripts/benchmarks/run_real_world_backend_comparison.bat`
- `scripts/benchmarks/compare_real_world_results.ps1`

## Documentation

- `REAL_WORLD_INTEGRATION_SUITE.md`
- `benchmarks/v1.1.0/real_world/README.md`
- `benchmarks/v1.1.0/real_world/CSV_SCHEMA.md`
- `README.md`
- `INSTALL_NOTES.md`
- `CHANGELOG.md`

No scheduler architecture was changed by this integration pass.

## Validation evidence

- `REAL_WORLD_LOCAL_VALIDATION.md`
  - records core-only build proof, 17/17 available CTests, representative
    non-cherry-picked results, fixed benchmark diagnostics, and the remaining
    Windows/OpenCV/oneTBB gates.

## Real-world optimization pass (SmartParallel 170)

### Core/configuration

- `include/smart/core/config.hpp`
- `include/smart/decision/backend_calibration.hpp` (new)
- `include/smart/execution/execution_context.hpp`
- `include/smart/execution/nested_execution_session.hpp`
- `include/smart/execution/parallel.hpp`
- `include/smart/execution/thread_pool.hpp`
- `src/thread_pool.cpp`

### Real-world benchmark infrastructure

- `benchmarks/v1.1.0/real_world/include/real_world_benchmark.hpp`
- `benchmarks/v1.1.0/real_world/src/opencv_image_pipeline.cpp`
- `benchmarks/v1.1.0/real_world/README.md`
- `benchmarks/v1.1.0/real_world/CSV_SCHEMA.md`

### Validation

- `tests/v1/real_world_optimization_hardening.cpp` (new)
- `tests/CMakeLists.txt`

### Documentation

- `REAL_WORLD_OPTIMIZATION_UPDATE.md` (new)
- `REAL_WORLD_INTEGRATION_SUITE.md`
- `REAL_WORLD_LOCAL_VALIDATION.md`
- `CHANGELOG.md`
- `MODIFICATION_MANIFEST.md`
