
## v1.1.0 Step 6 - Budget-Aware Nested Parallelism

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
