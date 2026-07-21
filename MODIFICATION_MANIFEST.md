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
