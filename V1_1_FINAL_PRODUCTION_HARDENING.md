# SmartParallel v1.1 final nested-production hardening

This pass preserves the existing nested scheduler architecture. It addresses concrete long-running correctness and backend-consistency risks found during the final production review.

## Issues fixed before v1.1

### Stable-plan lifecycle

- Stable plans now carry a scheduler-policy signature. A plan learned under one relevant configuration is not reused after those policy values change.
- Stable-plan revalidation is single-flight per cache key. Concurrent callers cannot create a revalidation stampede or repeatedly reset one another's counters.
- Contradictory observations invalidate the existing stable plan immediately.
- Nested-shape evidence decays instead of becoming permanently true after one historical nested invocation.
- Observations are counted once per root execution group, preventing many sibling invocations inside one root from manufacturing false confidence.

### Profile cache lifetime and identity

- The profile cache is bounded and uses least-recently-used eviction for inactive entries.
- Entries currently being built or revalidated are protected from eviction.
- Access and observation counters use saturating arithmetic.
- Cache keys include the resolved scheduler-policy signature.
- Function pointers include their address in callable identity.
- Reusable functors and `std::function` callsites can be explicitly separated with `smart::with_parallel_callsite(key, callback)`.

### Session lifetime and retained diagnostics

- Per-root frozen-plan snapshots are bounded.
- Global nested trace retention is bounded and drops the oldest records first.
- These changes prevent unbounded memory growth in long-running processes with changing callsites or tracing enabled.

### Backend consistency

- `StaticChunks` now executes through the StaticThread backend/session contract instead of bypassing root permits and exception handling.
- StaticThread partial thread-creation failure joins already-created threads before propagating the exception.
- oneTBB reuses an existing arena only when its concurrency does not exceed the leased width; otherwise it enters a constrained arena.
- TBB-required validation can no longer silently fall back when oneTBB is unavailable.

### Scheduler arithmetic

- Dynamic work acquisition no longer uses a wrapping `fetch_add` cursor.
- Chunk acquisition and end calculations remain correct near `std::size_t` limits.

## Production stress added

The `smartparallel_nested_production_stress` target covers:

- bounded cache and trace retention;
- active build/revalidation entries during eviction pressure;
- single-flight revalidation;
- decaying nested-shape evidence;
- policy-signature invalidation;
- explicit reusable-callsite identity;
- near-`size_t` chunk arithmetic;
- StaticThread permit and exception cleanup;
- randomized irregular trees across concurrent roots;
- repeated nested exceptions and recovery;
- conditional real-oneTBB arena-width validation.

## Remaining non-blockers

### SHOULD improve in v1.2

- Strict fairness is not guaranteed between sustained competing root sessions sharing the global ThreadPool.
- The root budget remains per session rather than a process-wide admission limit.
- Applications that reuse one functor type for semantically different callsites should use `with_parallel_callsite`; automatic source-location identity is not available in C++17.
- External configuration mutation while calls are executing remains unsupported. Configure before concurrent execution.
- The public API has exception-driven internal cancellation but no general external cancellation-token API.

### Research only

- Adaptive descendant borrowing and frontier migration.
- Continuation-based arbitrary nested flattening.
- Process-wide fairness policies and NUMA-aware placement.

## Release gate

The package is ready for Windows validation after:

```bat
scripts\validation\run_nested_release_validation.bat 31
scripts\validation\run_nested_release_validation.bat 3 trace
scripts\validation\run_nested_release_validation.bat 11 tbb
```

The third command requires a discoverable oneTBB installation and fails configuration if TBB is not actually enabled.
