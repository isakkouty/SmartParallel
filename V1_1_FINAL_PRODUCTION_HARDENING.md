# SmartParallel v1.1 final nested-production hardening

This pass preserves the existing root session, worker permits, lineage, frozen-plan, conservative-frontier, telemetry, and backend architecture.

## Production fixes

### Stable plans and profile invalidation

- Profile updates publish monotonic generations.
- Stable plans require the exact nonzero profile generation used to compute them.
- Cache clear is an epoch barrier: pre-clear observations cannot repopulate current state.
- In-flight build and revalidation ownership survives clear until the original RAII guard releases, preventing ABA-style marker loss.
- Revalidation is single-flight and triggered by both use count and wall-clock profile age.
- Contradictory observations invalidate optimization state immediately.
- Scheduler policy values and an explicit application policy generation participate in cache identity.

### Long-running cache and telemetry safety

- Profile retention is bounded by inactive-entry LRU eviction.
- Active build/revalidation entries are protected from eviction.
- Per-root plan snapshots and global completed trace records are bounded.
- Exception paths remove pending traces and publish exceptional records.
- Counters saturate and nested-shape evidence decays.
- Explicit callsite identity is available through `smart::with_parallel_callsite()`.

### Backend and trace consistency

- Actual backend identity is written from inside ThreadPool, StaticThread, and oneTBB execution paths.
- Trace records separate requested and confirmed backends.
- Runtime concurrency, oneTBB native delegation, and arena reuse are recorded independently.
- Validation runners fail when the requested backend is not present in summaries or detailed traces.
- Static chunks participate in session permits and exception cleanup.
- oneTBB arenas cannot exceed acquired participant width.

### Cancellation, reentrancy, and shutdown

- ThreadPool callbacks stop between iterations after scheduler-visible cancellation.
- ThreadPool queued-job exceptions are captured and rethrown by `wait()`.
- Reentrant waits track the complete per-thread queued-job stack depth.
- Cooperative jobs executed by callers establish the same pool ownership context as worker-executed jobs.
- During shutdown, existing worker jobs may publish dependency helpers required to finish, while new external submissions are rejected.
- Pool destruction drains queued and recursively published helper work before joining completes.

## Production stress

The release suite now contains 13 CTest targets. The nested production and shutdown stress gates cover:

- generation-safe plan publication;
- cache-clear invalidation and ownership ABA;
- age-based plan revalidation;
- 5,000-entry cache churn with bounded retention;
- randomized irregular trees and concurrent roots;
- short-root progress under a blocked long root;
- repeated deep nested exceptions and recovery;
- ThreadPool, StaticThread, and conditional oneTBB contracts;
- nested shutdown and reentrant waits;
- near-`size_t` scheduler arithmetic.

## Remaining v1.2 items

- strict process-wide fairness between sustained root sessions;
- an optional process-global admission controller;
- public cancellation tokens;
- adaptive descendant borrowing for skewed trees.

These are not required for the v1.1 exactly-once, exception, permit, shutdown, and backend-consistency contract.

## Release commands

Run all three backends and their traces, then compare them automatically:

```bat
scripts\validation\run_nested_cross_backend_validation.bat 31
```

Individual commands remain available through `run_nested_release_validation.bat`.
