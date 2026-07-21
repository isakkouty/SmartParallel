
## Step 26 — Performance tuning and scheduler validation

- Nested dynamic chunk sizes are deterministically refined to provide configurable scheduler headroom per effective worker.
- Timing checks remain diagnostic; release gates enforce correctness, budget bounds, exact-once execution, and non-pathological overhead.
- Run `scripts\examples\run_performance_tuning_scheduler_validation.bat`.

# SmartParallel

SmartParallel is a C++17 adaptive parallel-loop library. Its public `smart::parallel_for` API profiles an unknown callback, analyzes the workload, predicts candidate execution costs, and selects a sequential or parallel execution plan. The current CPU backends are a persistent thread pool, static threads, and oneTBB.

> **Release status:** v1 release. The core implementation and validation suites are stable; packaging and cross-platform CI remain release-hardening work.

## Why SmartParallel exists

A parallel loop is not automatically faster. Small callbacks may cost less than scheduler dispatch, uniform work may favor static partitioning, and irregular work benefits from dynamic load balancing. SmartParallel makes that policy decision at runtime while preserving exactly-once callback execution.

## Quick example

```cpp
#include <smart/execution/parallel.hpp>

#include <cstddef>
#include <vector>

int main()
{
    std::vector<double> values(1'000'000);

    smart::parallel_for(
        std::size_t{0},
        values.size(),
        [&](std::size_t index)
        {
            values[index] = static_cast<double>(index) * 2.0;
        });
}
```

## Current execution pipeline

```text
parallel_for
    -> profile/cache lookup
    -> workload analysis
    -> candidate generation
    -> analytical + historical prediction
    -> confidence/risk-aware ranking
    -> Sequential | ThreadPool | StaticThread | oneTBB
    -> diagnostics and optional experience recording
```

## Latest validation snapshot

The repository includes the latest measured CSV outputs under `validation/output/`.

- All OpenCV, scientific, stress, and decision-audit numerical checks passed.
- The decision-quality audit selected the fastest measured backend in **18 of 24 cases (75%)**.
- All six backend-selection misses occurred in tiny or small workloads, where profiling and dispatch overhead are proportionally dominant.
- Medium and large irregular-particle cases achieved roughly **10x** speedup over the sequential baseline on the recorded machine.

These values are machine-specific observations, not universal performance guarantees. See [Benchmark results](docs/v1/benchmark-results.md) and [Methodology](docs/v1/benchmark-methodology.md).

## Build

Requirements:

- CMake 3.20+
- C++17 compiler
- oneTBB
- OpenCV only for the OpenCV benchmark targets
- Python 3 with pandas and matplotlib only for plotting tools

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTBB_DIR="<path-to-tbb-config>"
cmake --build build --config Release
```

The Windows convenience launchers are organized under [`scripts/`](scripts/README.md):

```bat
scriptsenchmarks
un_all_benchmarks.bat
scriptsenchmarks
un_opencv_benchmarks.bat
scriptsalidation
un_v1_phase1.bat
```

## Repository map

| Path | Purpose |
|---|---|
| `include/smart/` | Public and reusable library headers |
| `src/` | Compiled implementation units |
| `examples/` | Minimal integration example |
| `tests/v1/` | Deterministic correctness and hardening tests |
| `benchmarks/v1.0.0/` | Original OpenCV, scientific, stress, decision-quality, CSV, and figure suite |
| `benchmarks/v1.1.0/` | Nested-depth and four-level configuration benchmarks for the v1.1.0 execution engine |
| `validation/` | Measurement programs and recorded results |
| `tools/` | Dataset analysis and benchmark plotting utilities |
| `scripts/` | Windows entry points for validation and benchmark runs |
| `docs/v1/` | Authoritative v1 documentation |
| `docs/beta/` | Archived pre-v1 documentation |

## Documentation

Start with the [v1 documentation index](docs/v1/README.md). Key references:

- [Getting started](docs/v1/getting-started.md)
- [Architecture](docs/v1/architecture.md)
- [Scheduler and decision model](docs/v1/scheduler.md)
- [Execution backends](docs/v1/execution-backends.md)
- [API reference](docs/v1/api.md)
- [Configuration reference](docs/v1/configuration.md)
- [Benchmark results](docs/v1/benchmark-results.md)
- [Validation guide](docs/v1/validation.md)
- [Known limitations](docs/v1/known-limitations.md)

## Scope of v1

v1 is centered on adaptive index-range `parallel_for`. Additional algorithms, OpenMP, GPU execution, NUMA policy, and more advanced oneTBB partitioner selection are intentionally future work.

## License

SmartParallel is distributed under the terms in [LICENSE](LICENSE).

## CMake presets and installation

Routine builds use named presets instead of long option lists:

```text
cmake --preset release
cmake --build --preset release
```

Other presets are `debug`, `examples`, `validation`, `benchmarks`, and `all`.
Install the release package with:

```text
cmake --install build/release --prefix path/to/install
```

Downstream CMake projects consume the exported target as
`SmartParallel::smart_parallel`. See
[`docs/v1/build-and-validation.md`](docs/v1/build-and-validation.md) for the full
preset, validation, installation, and `find_package` workflow.


## Platform support

SmartParallel is designed as a portable C++17 library.

Currently validated:

| Platform | Compiler | Status |
|----------|----------|--------|
| Windows | MSVC 2022 | ✅ Fully tested |

Planned validation:

- GCC
- Clang
- AppleClang

Cross-platform continuous integration is planned for a future release.

### Nested concurrency budgets

Nested `parallel_for` calls expose an effective concurrency budget through `ExecutionContext::concurrency_budget`. Native runtime delegation is capped by the parent budget, while sequential fallback uses a budget of one. Run `scripts\examples\run_nested_concurrency_budget.bat` to validate the Step 5 behavior on Windows.

### Nested budget partitioning

SmartParallel can deterministically divide a parent concurrency budget among a known group of sibling nested operations using `NestedBudgetPartitioner`. The partition uses quotient-plus-remainder allocation in child-index order, so a budget of 8 across 3 children becomes `3, 3, 2`. Children receiving zero allocation are forced to sequential fallback by `NestedExecutionCoordinator`.

### Revised Step 8: scheduler-visible work chunks

SmartParallel now includes a minimal scheduler-facing work representation in
`<smart/execution/work_chunk.hpp>`. `SchedulerVisibleWork` divides an index range
into deterministic chunks, allows concurrent acquisition, tracks completion, and
records the execution context that owns the work. This is the mechanism layer
needed before workers can safely help nested regions at chunk boundaries.

The existing `parallel_for` fast path is unchanged in this step. Dynamic worker
sharing, concurrency leases, and nested helping will be added only after this
chunk mechanism is validated.

## Revised nested scheduling prototype status

Steps 1-8 established nested execution policy, budgets, partitioning, and scheduler-visible chunks. Revised Step 9 added bounded shared consumers through `ThreadPool::execute_visible_work`. Revised Step 10 adds `ThreadPool::execute_visible_work_helping`: a worker already executing an outer pool job can consume inner chunks directly, recruit queued helpers, and execute queued helper jobs while waiting. This keeps saturated nested waits deadlock-free without interrupting iterations. Live concurrency leases remain a future step.

### Scheduling benchmark/regression gate
Run `scripts\examples\run_scheduling_regression_gate.bat` to measure the flat scheduling ratio and nested-helping speedup. Send the full output before enabling dynamic concurrency leases.

### Revised Step 12: backend-neutral execution contract

All current runtimes now implement `IExecutionBackend`. `BackendExecutionRequest` carries the range, inherited concurrency budget, chunk size, and callback; `BackendExecutionResult` reports the selected backend and effective budget. The existing `IExecutionEngine` and `execution_engine(...)` names remain available as compatibility aliases. Capability declarations now distinguish native nesting, cooperative helping, cancellation, and scheduler-visible work so the nested coordinator can become backend-neutral in the next step.

Run `scripts\examples\run_backend_execution_contract.bat` on Windows to validate the contract for ThreadPool, oneTBB, and StaticThread.

### Step 13: backend-neutral nested execution coordinator

`NestedExecutionCoordinator` now accepts any `IExecutionBackend` pair and records a `NestedBackendRelation` in each decision. Policy selection is based on declared runtime capabilities rather than concrete backend classes, while the existing registry-based overloads remain unchanged. Run `scripts\examples\run_backend_neutral_nested_coordinator.bat` for the focused Windows validation.

### v1.1.0 Step 14: Execution Lineage & Runtime Inheritance
Execution contexts now retain stable root, nearest parallel ancestor, runtime owner, and inherited-budget metadata across sequential and mixed-backend descendants. Validate with `scripts\\examples\\run_execution_lineage_runtime_inheritance.bat`.

### v1.1.0 Step 15: Backend negotiation and capability resolution

`NestedExecutionCoordinator` now resolves a backend-neutral execution mechanism before applying the currently active execution policy. The negotiation distinguishes direct execution, native runtime delegation, cooperative helping, and sequential fallback, and records the requested, available, and negotiated budgets. ThreadPool cooperative helping is recognized through its complete capability bundle but remains inactive until the dedicated ThreadPool integration milestone. Run `scripts\examples\run_backend_negotiation_capability_resolution.bat` for the focused Windows validation.

### v1.1.0 Step 17: oneTBB backend integration

Nested oneTBB work selected for native delegation now executes directly inside the active oneTBB task arena. This preserves the runtime domain established by the ancestor loop and avoids creating an unrelated nested arena. Run `scripts\examples\run_one_tbb_backend_integration.bat` for the focused Windows validation.

### StaticThread and fallback strategies (v1.1.0 Step 18)

StaticThread now participates in the backend-neutral execution contract. Root StaticThread work uses a bounded static team, while nested StaticThread requests and unsupported backend transitions use an explicit caller-only sequential fallback to avoid oversubscription. Validation is available through `scripts\examples\run_static_thread_fallback_strategies.bat`.

### Automatic nested `parallel_for` integration

The public `smart::parallel_for` entry point now performs the full nested execution flow automatically: it preserves the parent execution lineage, asks the nested coordinator to negotiate the backend mechanism, and routes work through native delegation, cooperative helping, or sequential fallback as appropriate. An explicitly configured backend constrains backend selection while the normal decision engine still determines whether the workload is worth parallelizing.

Focused Windows validation:

```bat
scripts\examples\run_automatic_parallel_for_nested_integration.bat
```

### Granularity and concurrency-budget enforcement

Nested backend negotiation is refined by the amount of useful schedulable work. The effective worker budget is capped by the inherited concurrency envelope, iterations per worker, and available chunks. If a nested range cannot keep at least two workers useful, SmartParallel executes it as an explicit sequential fallback.

Run the focused validation on Windows:

```bat
scripts\examples\run_granularity_concurrency_budget_enforcement.bat
```

### Step 21 validation: dependency-local helping and continuation priority

```bat
scripts\examples\run_dependency_local_helping_continuation_priority.bat
```

This validation saturates a two-worker pool, queues unrelated work ahead of an awaited helper, and verifies that the waiting worker selects only its dependency, resumes its continuation first, and leaves unrelated work available for normal execution afterward.

### Recursive multi-level nested execution validation

Run `scripts\examples\run_recursive_multi_level_nested_execution.bat` to validate four-level recursion, sequential lineage bridges, inherited budgets, and conservative mixed-backend transitions.

### Step 23 validation: exception propagation and cooperative cancellation

Run on Windows from the repository root:

```bat
scripts\examples\run_exception_propagation_cooperative_cancellation.bat
```

This verifies first-failure propagation, cooperative cancellation of unstarted chunks, no repeated work, and nested ThreadPool backend exception handling.


## Step 24
Implemented lifetime safety and shutdown guarantees.

### Step 25 validation: deep nesting and mixed backends

Run:

```bat
scripts\examples\run_deep_nesting_mixed_backend_validation.bat
```

This validates six-level ThreadPool recursion, five-level oneTBB recursion, conservative mixed-backend transitions, lineage preservation through sequential bridges, and repeated deep-nesting completion.

### Automatic depth-four public-path regression

Automatic callback profiling may invoke a nested `parallel_for` while the caller is already a ThreadPool worker. SmartParallel now detects that runtime re-entry at the backend boundary and uses dependency-local cooperative helping instead of the root-style global queue wait. This prevents the depth-four deadlock discovered by the v1.1.0 benchmark suite while preserving exact-once profiling and execution behavior.

Run the focused Windows validation with:

```bat
scripts\examples\run_public_parallel_for_depth_four_regression.bat
```
