# Changelog

## v1.1.0 pre-integration release gates

- Made the default experience database synchronized and bounded by record and per-record plan limits.
- Prevented disabled online exploration from accumulating fingerprint state and bounded enabled exploration state.
- Made the function-profile cache capacity a strict bound under concurrent pinned entries.
- Centralized production limits for profiles, experience, exploration, trace retention, and per-root plan snapshots.
- Added repeated cross-backend deep cancellation/recovery, cache-concurrency, and cache-eviction validation.
- Strengthened empty shutdown, repeated construction/destruction, worker-exception propagation, and independent-runtime recovery tests.

## v1.1.0 final safety and cross-backend release gate

- Made stable-plan publication generation-safe and cache clear an epoch invalidation barrier.
- Preserved in-flight cache ownership across clear to prevent ABA-style single-flight loss.
- Added wall-clock stable-plan revalidation for low-frequency long-running callsites.
- Added confirmed actual-backend tracing for ThreadPool, StaticThread, and oneTBB.
- Added exceptional trace cleanup and deep nested cancellation/recovery stress.
- Fixed multi-level reentrant ThreadPool wait depth and nested shutdown helper publication.
- Added long-running cache churn, concurrent-root progress, and nested shutdown stress.
- Added required cross-backend execution and automated CSV/trace comparison scripts.

## v1.1.0 final production hardening

- Made stable-plan reuse policy-sensitive and revalidation single-flight.
- Bounded profile-cache, per-root snapshot, and nested-trace retention.
- Added decaying nested evidence, root-grouped observations, and explicit reusable-callsite identity.
- Routed StaticChunks through root-session lease accounting.
- Constrained oneTBB arena reuse to acquired participant width.
- Made StaticThread partial thread creation exception-safe.
- Hardened scheduler chunk arithmetic against `size_t` wraparound.
- Added randomized long-running production stress and required-oneTBB validation mode.
- Aligned CMake package version with v1.1.0.

## v1.1.0 nested execution release hardening

- Fixed a lost-notification window in cooperative helper completion and removed sub-millisecond timed polling.
- Made ThreadPool helper permits explicit, per participant, and released before completion is published.
- Made partial dependency-helper submission exception-safe.
- Replaced pool-worker-based lease inference with per-session participant ownership.
- Clamped participant reservations to actual iteration/backend capacity and added checked lease invariants.
- Replaced hash-only root plan snapshots with collision-safe contextual keys.
- Added conservative exactly-once root telemetry and periodic stable-plan revalidation.
- Added irregular-tree, lease-exhaustion, backend-switching, cancellation, reentrant-root, and completion-race tests.
- Added separate helper work-drain and completion-wake trace fields.
- Replaced a Clang-incompatible nested default argument in `FunctionProfiler` with an equivalent forwarding overload.

## Nested automatic execution stabilization (2026-07-21)

- Added a root-scoped nested execution session with enforceable worker leases.
- Added exactly-once nested telemetry, context-aware profile keys, nonblocking single-flight profile construction, stable cached plans, and per-root plan snapshots.
- Added the automatic parallel-frontier policy and a direct descendant fast path.
- Added time-based nested profitability and chunk-duration targeting.
- Made ThreadPool helper recruitment idle-aware and cancellable after useful work completes.
- Added structured decision/scheduler trace CSV output.
- Added `parallel_for_nd` for flattened rectangular nests.
- Split forced nested scheduler stress from the real public automatic benchmark.
- Added nested frontier/budget regression validation.
- Made oneTBB optional for ThreadPool-only builds.


### v1.1.0 nested benchmark stabilization

- Added live per-case and per-repetition progress output.
- Bounded the benchmark runtime domain to four workers for reproducible nested scheduling.
- Preserved the forced coordinator/executor path as an explicit scheduler stress case.
- Added a separate real public automatic `parallel_for` case at every nesting level.
- Added raw repetition output, stable-plan warm-ups, policy tracing, and the flattened N-dimensional comparison.


## Step 26 — Performance tuning and scheduler validation

- Added deterministic nested dynamic-chunk refinement using a configurable target chunk count per effective worker.
- Added diagnostics for original and effective chunk sizes.
- Added a focused scheduler validation and performance sanity gate.

## Step 20 - Granularity & Concurrency-Budget Enforcement

- Added `NestedExecutionConstraints` and coordinator-side post-negotiation refinement.
- Clamped nested concurrency by iteration count, dynamic chunk count, and inherited budget.
- Converted nested plans with fewer than two useful workers into explicit sequential fallback.
- Added public `parallel_for` diagnostics for budget and granularity limiting.
- Added `granularity_concurrency_budget_enforcement` validation example and Windows runner.


## v1.1.0 Step 6 - Budget-Aware Nested Parallelism

## Step 18 - StaticThread & Fallback Strategies

- Added explicit caller-only sequential fallback requests to the backend contract.
- Integrated StaticThread root execution through the backend-neutral request/result path.
- Nested StaticThread and unsupported cross-backend transitions now remain bounded to one caller thread.
- Added diagnostics for runtime concurrency, spawned workers, and sequential fallback execution.
- Added focused validation for exact-once execution, bounded root teams, and conservative fallback behavior.


- Added an explicit `BudgetLimitedDelegation` nested policy.
- Compatible native runtimes delegate normally when the requested budget fits the parent budget.
- Oversized compatible requests remain parallel with the inherited parent budget.
- Nested execution falls back to sequential when the parent budget is exhausted or runtimes are incompatible.
- Added a focused Windows build-and-run example for all three budget thresholds.
# Changelog

## Step 5 - Nested Concurrency Budget

- Added `ExecutionContext::concurrency_budget`.
- Added parent, requested, and effective budget reporting to `NestedExecutionDecision`.
- Native nested runtimes inherit a budget capped by the parent context.
- Sequential and fallback execution use a budget of one.
- Added a focused Windows example script for budget inheritance and clamping.


## Unreleased - v1.1.0

- Added deterministic nested runtime policy selection.
- Same-runtime oneTBB nesting delegates to oneTBB natively.
- Unsupported and cross-runtime nesting currently uses a safe sequential inner fallback.
- Execution contexts now expose the active engine, parallel state, and nested policy.



## 1.0.0 packaging hotfix

- Restored automatic vcpkg toolchain discovery from `VCPKG_ROOT` before `project()`.
- Restored the original Windows default triplet, `x64-windows`, when no triplet is supplied.
- Preset builds no longer require repeating `CMAKE_TOOLCHAIN_FILE` or `VCPKG_TARGET_TRIPLET`.

## Release status: Stable v1.0.0.

- Stabilized adaptive index-range `parallel_for`.
- Added automatic callback profiling, profile caching, and a confirmed sequential fast path.
- Added ThreadPool, StaticThread, and oneTBB execution engines.
- Added hardware-, memory-, family-, confidence-, experience-, and residual-aware decision modeling.
- Added deterministic validation, hardening, overhead, OpenCV, scientific, stress, and decision-quality suites.
- Archived beta documentation and replaced it with an authoritative v1 documentation set.
- Organized Windows entry-point scripts under `scripts/`.
- Added benchmark plotting and committed a documented benchmark-results snapshot.

Performance results are machine-specific; see `docs/v1/benchmark-results.md`.

## Build and packaging infrastructure

- Added modular CMake files for the library, examples, tests, validation, and benchmark suites.
- Added `CMakePresets.json` with `debug`, `release`, `examples`, `validation`, `benchmarks`, and `all` presets.
- Added umbrella build options while retaining all previous fine-grained options.
- Added standard install rules and the exported `SmartParallel::smart_parallel` target.
- Added `SmartParallelConfig.cmake` and compatible package-version generation.
- Added automatic oneTBB dependency discovery for installed consumers.
- Added a generated public version header.
- Centralized compiler warnings and Windows oneTBB runtime copying in reusable CMake helpers.
- Expanded build, installation, and downstream-consumer documentation.

### Step 7 - Nested Budget Partitioning

- Added `NestedBudgetPartition` and `NestedBudgetPartitioner`.
- Added deterministic quotient-plus-remainder budget distribution across sibling nested children.
- Extended `NestedExecutionCoordinator` with partition-aware coordination and allocation diagnostics.
- Added sequential fallback for children whose partition is exhausted.
- Added a focused example script covering fair splits, remainder handling, and exhaustion.

## v1.1.x revised roadmap — Step 8: Scheduler-visible work chunks

- Added `WorkChunk`, `WorkChunkProgress`, and `SchedulerVisibleWork`.
- Remaining loop work can now be acquired as deterministic, scheduler-visible chunks.
- Added lock-free chunk acquisition counters and explicit completion tracking.
- Captured the owning execution-context identity on each work source.
- Preserved the existing `parallel_for` backend fast path; shared scheduling is not enabled yet.
- Added `run_scheduler_visible_work_chunks.bat` for focused Windows validation.

### Revised Step 9 - Shared ThreadPool work queues

- Added `ThreadPool::execute_visible_work` as a bounded shared-consumer prototype.
- Multiple ThreadPool workers now acquire scheduler-visible chunks from one region.
- Workers request more work only at chunk boundaries, preserving completed results and unstarted work.
- Added a Windows validation script for exactly-once chunk execution and shared-worker participation.

### Revised Step 10 - Nested ThreadPool helping prototype

- Added `ThreadPool::execute_visible_work_helping` for calls made from ordinary threads or from workers already executing an outer ThreadPool job.
- The calling thread directly consumes inner chunks and submitted helper jobs share the same scheduler-visible source.
- Waiting nested callers may execute another queued pool job, preventing all-workers-busy nested waits from deadlocking.
- Rebalancing remains cooperative at chunk boundaries; no iteration is interrupted or restarted.
- Added focused validation for multi-worker inner participation, exactly-once iteration execution, and saturated four-worker nested progress.

### Revised Step 11 - Scheduling benchmark and regression gate
- Added a Windows benchmark gate comparing the existing flat `parallel_for` path with scheduler-visible ThreadPool execution.
- Added a nested serial-vs-helping CPU workload with exact-result validation.
- Timing results are diagnostic rather than hard-coded pass thresholds, avoiding machine-dependent false failures.

### Step 12 - Backend-neutral execution contract
- Added `IExecutionBackend` as the common execution contract while retaining `IExecutionEngine` as a source-compatible alias.
- Added `BackendExecutionRequest` and `BackendExecutionResult` so coordinators can submit bounded work without backend-specific calls.
- Extended runtime capabilities with cooperative-helping, cancellation, and scheduler-visible-work declarations.
- Declared ThreadPool helping/visible-work support, oneTBB native-nesting support, and conservative StaticThread capabilities.
- Added a focused Windows validation script covering all current backends and the compatibility alias.

### Step 13 - Backend-Neutral Nested Execution Coordinator

- Refactored `NestedExecutionCoordinator` so backend relationships are described exclusively through `IExecutionBackend` and `RuntimeCapabilities`.
- Added `NestedBackendRelation` diagnostics for parent backend, child backend, capability snapshots, backend equality, and native-nesting compatibility.
- Added explicit coordinator overloads accepting arbitrary backend implementations, enabling future backends and deterministic test doubles without global-registry changes.
- Preserved all existing nested policy behavior while removing backend-class assumptions from the coordinator decision boundary.
- Added focused Windows validation for registry backends, cross-backend fallback, future-backend capability delegation, and capability-over-name precedence.

### Step 14 - Execution Lineage & Runtime Inheritance
- Added stable root-loop, nearest-parallel-ancestor, and runtime-owner lineage metadata.
- Sequential regions now preserve the runtime domain and concurrency envelope inherited from parallel ancestors.
- Added depth-four validation covering oneTBB -> sequential -> oneTBB -> ThreadPool lineage.

### v1.1.0 Step 15 - Backend Negotiation & Capability Resolution

- Added `NestedExecutionMechanism` to distinguish direct execution, native delegation, cooperative helping, and sequential fallback.
- Added `BackendNegotiationResult` diagnostics with requested, available, and negotiated budgets.
- Resolve oneTBB native nesting and ThreadPool cooperative helping from backend capability bundles rather than backend names.
- Keep cross-backend transitions conservative until their dedicated integration policies are implemented.
- Preserve the existing active execution policy: cooperative helping is recognized but is not automatically activated by this structural step.
- Added focused Windows validation for mechanism precedence, incomplete capability bundles, and cross-backend fallback.

### v1.1.0 Step 17 - oneTBB Backend Integration

- Added explicit native-delegation requests to the backend-neutral execution contract.
- oneTBB nested execution now reuses the active `task_arena` instead of creating a separate nested arena.
- Added backend execution diagnostics for runtime-domain reuse and active arena concurrency.
- Routed native and budget-limited nested policies through the oneTBB native-delegation path.
- Added focused validation for exact-once execution, multi-worker participation, inherited arena bounds, and executor routing.

### Step 19 - Automatic `parallel_for` Nested Integration

- Routed the ordinary public `parallel_for` path through backend selection, nested coordination, and backend execution mechanisms.
- Applied an explicitly configured execution backend to the decided parallel plan without bypassing the normal parallel-worthiness decision.
- Preserved complete execution lineage during profiling-time callback invocations by representing profiling as a sequential region until the final plan is known.
- Added public-path nested diagnostics for selected backend, policy, mechanism, runtime relationship, and effective budget.
- Added focused validation for automatic ThreadPool helping, oneTBB native delegation, and conservative cross-backend fallback.

### v1.1.0 Step 21 - Dependency-local helping and continuation priority

- Tagged cooperative ThreadPool helper jobs with the scheduler-visible dependency they advance.
- Replaced arbitrary global-queue execution during nested waits with dependency-local queue scanning.
- Waiting workers now help only the work source they are blocked on and leave unrelated jobs queued.
- Resume the waiting continuation immediately after its dependency completes.
- Added helping diagnostics and focused validation for exact-once execution, local helping, continuation priority, and preservation of unrelated queued work.

### Step 22 - Recursive Multi-Level Nested Execution
- Added four-level coordinated ThreadPool recursion validation.
- Verified inherited concurrency budgets and runtime lineage across arbitrary nesting depth.
- Verified sequential regions preserve runtime ownership for later backend re-entry.
- Verified deep mixed-backend transitions remain conservative and exact-once.

## v1.1.0 Step 23 - Exception Propagation & Cooperative Cancellation

- Added first-failure capture to scheduler-visible work regions.
- Added cooperative cancellation that prevents new chunk acquisition after failure.
- ThreadPool worker exceptions now propagate to the waiting caller instead of terminating the process.
- Nested cooperative-helping waits remain active until all dependency helpers retire before rethrowing.
- StaticThread execution now captures the first worker exception, cancels sibling ranges cooperatively, joins all threads, and rethrows on the caller.
- Added focused validation and Windows runner for exception propagation, cancellation visibility, and no-repeat behavior.

- Step24: Lifetime safety & shutdown guarantees.

## v1.1.0 Step 25 - Deep Nesting & Mixed-Backend Validation

- Added six-level ThreadPool recursion validation with exact leaf accounting and inherited-budget checks.
- Added five-level oneTBB recursion validation across native nested runtime reuse.
- Added a mixed ThreadPool -> StaticThread fallback -> ThreadPool re-entry -> oneTBB fallback chain.
- Verified root lineage and runtime ownership survive sequential bridges and repeated backend transitions.
- Added repeated deep-nesting stress rounds to detect stranded, leaked, or incomplete work.

## v1.1.0 Nested Benchmark Milestone

- Versioned the original benchmark suite under `benchmarks/v1.0.0` so its CSV and figures remain reproducible.
- Added `benchmarks/v1.1.0` for nested-execution performance evidence.
- Added two-, three-, and four-level nested workload comparisons with deterministic correctness checksums.
- Added a four-level configuration matrix comparing all-sequential, one selected parallel level, and SmartParallel at every level.
- Added CSV output with medians, ranges, speedup versus the matching sequential baseline, depth, parallel-level mask, and machine metadata.

## v1.1.0 Automatic depth-four public-path regression fix

- Fixed a deadlock when automatic `parallel_for` profiling recursively re-entered ThreadPool from one of its own workers at four or more levels.
- ThreadPool now tracks worker ownership and automatically upgrades worker-side re-entry to dependency-local cooperative helping, even when an intermediate profiling or sequential region was classified as direct execution.
- Root callers continue to use the normal bounded shared-queue path; only owned-worker re-entry is upgraded.
- Added a focused public-API regression covering four nested automatic `parallel_for` levels, exact-once execution, worker-side re-entry, timeout detection, and repeated stress rounds.
