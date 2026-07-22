# SmartParallel v1.1 final nested safety review

This pass keeps the existing nested execution architecture. It fixes only concrete production risks found in stable-plan lifetime, cache invalidation, backend observability, cancellation, reentrancy, and shutdown.

## Issues fixed

### 1. Stable-plan publication race — MUST fix

**Risk:** a plan computed from an older profile could be installed after another thread published a newer profile.

**Fix:** every accepted profile update receives a monotonic generation. Stable plans can only be installed against that exact nonzero generation. A rejected or invalidated profile store can no longer publish an unversioned plan.

### 2. Cache-clear invalidation race — MUST fix

**Risk:** a profiling operation started before `clear()` could repopulate the cache afterward.

**Fix:** cache invalidation advances an epoch. In-flight stores carrying an older epoch are rejected.

### 3. In-flight guard ABA after cache clear — MUST fix

**Risk:** clearing build/revalidation ownership markers while old RAII guards still existed could allow a new owner for the same key. Destruction of the old guard could then erase the new marker and break single-flight protection.

**Fix:** `clear()` no longer erases in-flight ownership sets. Existing guards keep ownership until normal release, while their old observations remain blocked by the cache epoch.

### 4. Permanently stale low-frequency plans — MUST fix

**Risk:** use-count revalidation alone may never refresh a callsite invoked rarely in a long-running service.

**Fix:** stable profiles now also have a wall-clock maximum age, controlled by `parallel_for_profile_revalidate_after_ms`. Revalidation remains single-flight.

### 5. Incomplete backend trace proof — MUST fix for release diagnostics

**Risk:** a benchmark could request one backend while a different backend actually executed.

**Fix:** trace records now separate `requested_backend` from confirmed `backend`, and record runtime concurrency, native delegation, runtime-domain reuse, and exceptional completion. Each backend writes its identity from inside its execution path. Benchmark and runner checks fail when actual execution does not match the requested backend.

### 6. Exception traces retained as pending state — MUST fix

**Risk:** repeated exceptions with tracing enabled could leave unfinished per-session trace entries.

**Fix:** every public execution exception path aborts and publishes an exceptional trace record before rethrowing.

### 7. Nested cancellation latency and cleanup — MUST fix

**Risk:** after one callback failed, a ThreadPool helper could continue executing the rest of its already-acquired chunk.

**Fix:** helper callbacks now check the scheduler-visible cancellation state between iterations. Deep cancellation/recovery is stress-tested under ThreadPool, StaticThread, and conditionally real oneTBB.

### 8. Reentrant `ThreadPool::wait()` depth — MUST fix

**Risk:** a queued job executed cooperatively inside another worker job could call `wait()` again. The old implementation assumed one active worker frame and could deadlock with multiple active reentrant frames.

**Fix:** ThreadPool tracks the active queued-job depth per thread and per current pool. Reentrant waits preserve the complete active stack depth. Cooperative jobs executed by external callers also establish the correct pool execution scope.

### 9. Shutdown during nested helper publication — MUST fix

**Risk:** a worker draining the queue during pool destruction could need to publish dependency helpers after shutdown began.

**Fix:** external submissions are rejected after shutdown begins, while a job already executing inside that same pool may still publish dependency work needed to complete itself. Destruction joins workers only after queued and recursively published work drains.

### 10. Worker exceptions outside managed regions — MUST fix

**Risk:** a generic queued job exception could terminate the process or disappear.

**Fix:** ThreadPool stores the first unhandled queued-job exception and rethrows it from `wait()`. Destructors remain nonthrowing.

### 11. Long-running cache and trace growth — MUST fix

**Risk:** changing callsites or always-on tracing could grow process memory indefinitely.

**Fix:** profile entries use bounded inactive-entry LRU eviction; frozen root snapshots and global trace records are bounded; counters saturate; nested-shape evidence decays; long-running churn and periodic invalidation are stress-tested.

### 12. Backend contract drift — MUST fix

**Risk:** StaticThread or oneTBB could bypass guarantees established for ThreadPool.

**Fix:**

- Static chunks execute through the session-aware backend contract.
- StaticThread joins partially created threads before propagating construction failure.
- oneTBB runtime concurrency is constrained to acquired session permits.
- oneTBB traces distinguish native delegation from actual arena reuse.
- direct success and exception contracts are tested for each compiled backend.

## Issues deliberately not changed

### Strict process-wide fairness — SHOULD improve in v1.2

Every external root remains a caller participant and can make progress without waiting for a helper. A stress test verifies that short roots complete while an unrelated long root is blocked. However, the global ThreadPool still uses a shared FIFO queue and does not promise strict proportional fairness between an unlimited stream of sustained root sessions.

### Process-global admission limit — SHOULD improve in v1.2

The configured root budget is per session. Multiple independent roots may collectively use more participants than one root budget. This is documented behavior, not a lease-accounting failure.

### Public cancellation tokens — SHOULD improve in v1.2

v1.1 supports exception-driven cooperative cancellation. It does not expose an external cancellation-token API.

### Adaptive frontier migration — Research only

The conservative frontier remains unchanged. Dynamic descendant borrowing or migration is not required for the v1.1 correctness contract.

## Validation added

- randomized irregular nested trees;
- many concurrent roots with exception recovery;
- short-root progress beside a blocked long root;
- repeated deep nested cancellation and recovery on every compiled backend;
- one-, two-, and three-level reentrant ThreadPool waits;
- ThreadPool destruction while nested helpers are published;
- shutdown while selected nested roots throw;
- 5,000-entry cache churn with bounded retention and periodic invalidation;
- cache-clear/build/revalidation ownership ABA regression;
- stable-plan generation race regression;
- real-backend trace confirmation;
- automated cross-backend checksum, trace, lease, and timing comparison.
