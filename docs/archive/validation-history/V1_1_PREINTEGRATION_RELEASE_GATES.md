# SmartParallel v1.1 pre-integration release gates

This pass is intentionally limited to cancellation, shutdown/reentrancy, bounded runtime retention, and backend regression coverage. It does not change scheduler policy or add optimization work.

## Concrete defects fixed

### Process-wide experience database was unsafe and unbounded

`ExperienceDatabase` is enabled by default and can be accessed by concurrent roots. It previously stored records and plans without synchronization and without a retention limit. Concurrent access could race, and long-running applications could grow the database indefinitely.

The database now:

- synchronizes all load, query, update, clear, and persistence operations;
- retains at most `runtime_limits::experience_records` records;
- retains at most `runtime_limits::experience_plans_per_record` plans per record;
- evicts least-recently-used inactive data;
- exposes record and plan counts for validation.

Compatibility pointer queries return thread-local copies so callers cannot retain pointers into storage that may be evicted by another thread.

### Disabled online exploration accumulated permanent state

The exploration policy created a fingerprint entry before checking whether online exploration was enabled. Default configurations therefore accumulated process-lifetime state even though exploration was disabled.

Disabled exploration now creates no state. Enabled exploration uses a bounded least-recently-used table and exposes its current size.

### Profile-cache capacity was not a hard bound

When every cached entry was actively building or revalidating, the cache could insert another entry beyond its configured maximum. Concurrent cold callsites could therefore cause temporary unbounded growth.

The cache now rejects a new publication when capacity is full and no inactive entry can be evicted. A later call may learn the profile after an entry becomes evictable.

### Zero-valued retention settings could disable bounds

Zero previously meant unlimited retention for several long-lived structures. Zero now selects the central production default. Unbounded runtime retention is not supported by the v1.1 configuration surface.

## Central runtime limits

All process-lifetime or long-lived limits are defined in `include/smart/core/config.hpp` under `smart::runtime_limits`:

| Structure | Default limit |
|---|---:|
| Function profile cache | 4,096 entries |
| Experience records | 4,096 records |
| Plans per experience record | 64 plans |
| Online exploration state | 4,096 fingerprints |
| Global nested trace buffer | 65,536 records |
| Per-root frozen plan snapshots | 4,096 snapshots |

Runtime configuration may choose a smaller or larger positive limit. A configured value of zero selects the default shown above.

Session-owned transient state is destroyed with its `NestedExecutionSession`. ThreadPool queues and helper registrations are drained by pool destruction and are observable through test diagnostics.

## Focused validation added

`smartparallel_v11_preintegration_release_gates` validates:

- repeated exception-triggered cancellation during deep nested execution;
- concurrent callback/helper participation;
- exact originating exception propagation;
- no duplicate iteration execution;
- no work continuing after the call returns;
- permits, active callbacks, helper jobs, and session invariants returning to baseline;
- successful exact-once execution after every cancellation cycle;
- ThreadPool, StaticThread, and oneTBB when compiled;
- strict profile-cache capacity under concurrent insertion;
- full-capacity behavior while all entries are pinned;
- bounded and concurrent experience-database insertion;
- continued learning after eviction;
- zero exploration-state growth while disabled;
- bounded exploration state while enabled;
- visible trace and frozen-plan snapshot bounds.

`smartparallel_nested_shutdown_stress` additionally validates:

- shutdown/destruction with no active work;
- repeated pool construction and destruction;
- draining root and nested work during destruction;
- exception propagation through `wait()`;
- recovery after an observed worker exception;
- functionality of later independent runtime instances.

SmartParallel exposes destruction as the ThreadPool shutdown boundary. There is no public concurrent `shutdown()` operation. Owners must stop external submissions before destruction and must not destroy a pool from one of its own worker callbacks. Submissions after object destruction are invalid C++ object use, not a supported rejection path.

## Local results

- GCC 14.2 Release build: passed.
- CTest: 14/14 passed.
- Focused ASan + UBSan release-gate and shutdown tests: passed.
- Focused ThreadSanitizer release-gate and shutdown tests: passed.
- ThreadPool traced benchmark smoke: passed with correct checksums and confirmed backend.
- StaticThread traced benchmark smoke: passed with correct checksums and confirmed backend.

oneTBB was unavailable in the local container. The Windows required-oneTBB command remains the real oneTBB release gate.

## Complete Windows release command

```bat
scripts\validation\run_nested_cross_backend_validation.bat 31
```

The batch script runs ThreadPool, StaticThread, and required-real-oneTBB performance and trace validations sequentially, compares the generated results, and exits immediately on the first failure.
