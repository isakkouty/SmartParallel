# Architecture

SmartParallel separates **the user's algorithm** from **the policy used to execute it**. The public API expresses the iteration space; internal modules turn that call into an `ExecutionPlan` and then run it through a backend.

## High-level flow

```text
Application
   │
   ▼
Public API (`for_each`, `parallel_for`, `for_each_pair`)
   │
   ├── Workload Builder
   ├── Workload Analyzer
   ├── Function Profiler (for profiled APIs)
   └── Decision Engine
          │
          ▼
     ExecutionPlan
          │
          ├── Sequential
          ├── StaticThread
          ├── ThreadPool
          └── oneTBB
```

## Module map

| Module | Responsibility |
|---|---|
| `core/` | Global configuration, statistics, timers, and timing reports |
| `workload/` | Workload descriptions, builders, analysis, and fingerprints |
| `profiling/` | Runtime function sampling and profile caching |
| `decision/` | Hints, providers, reports, rules, and plan selection |
| `model/` | Execution characteristics and performance-model data structures |
| `execution/` | Executors and concrete CPU backends |
| `hardware/` | Logical-thread and Windows topology/cache discovery |
| `experience/` | Historical execution records and a history-based provider |

## Public API path

### `smart::for_each`

1. Clears the timing report when diagnostics are enabled.
2. Builds a container workload.
3. Analyzes that workload.
4. Samples the supplied function using `FunctionProfiler`.
5. Calls `DecisionEngine::decide` with the profile.
6. Executes either the dedicated static-container path or the generic executor.
7. Optionally records elapsed time in the experience database.

### `smart::for_each_pair`

The pair API flattens the Cartesian product into a one-dimensional iteration space. Index `k` is mapped back to `(i, j)` using division and modulo. Profiling and execution use the same flattened mapping.

### `smart::parallel_for`

The index-range API builds and analyzes a range workload and asks the decision engine for a plan. In the current Beta 1.0 implementation it does not run the same function-profiling stage used by `for_each` and `for_each_pair`.

## Workloads

A `Workload` is a backend-independent description of an iteration space. Builders create workloads for index ranges, containers, and pair containers. The analyzer derives additional characteristics consumed by the decision system.

This design prevents execution backends from needing to understand user containers directly. They receive a plan and a callable over flattened indices.

## Function profiling

`FunctionProfiler` executes a bounded number of sample batches and reports values including:

- average, p95, and maximum time per iteration
- estimated total work
- estimated parallel overhead
- a parallel-worthiness score
- instability ratio and stability flag

The profile is an input to the decision engine. Profiling is deliberately small, but its cost can dominate extremely tiny workloads; the benchmark suite reports this honestly.

## Decision providers

`DecisionEngine` builds a `DecisionContext` and delegates to `CompositeDecisionProvider`. The provider architecture allows several sources of recommendations to participate while returning a unified `DecisionReport`.

If no report is produced, the engine falls back to a one-job sequential plan.

An `ExecutionPlan` records at least:

- execution engine
- scheduling strategy
- job count
- whether the plan is parallel

The latest report from profiled public APIs is copied to `global_last_decision_report()` for diagnostics and benchmarks.

## Execution

The execution layer supports these conceptual paths:

- **Sequential** — one caller thread processes every iteration.
- **StaticThread** — the iteration space is split into fixed contiguous chunks.
- **ThreadPool** — jobs are submitted to a persistent process-wide pool.
- **oneTBB** — work is delegated to Intel oneTBB.

Static and dynamic scheduling are plan strategies; they are not synonymous with a single backend. For example, the benchmark logs commonly show `oneTBB / DynamicChunks`.

## Timing diagnostics

`TimingScope` records named phases into the current timing report. Typical phases are:

```text
workload_build
workload_analysis
function_profile
decision
execution or execution_static_chunks
experience_record
total
```

Some execution scopes are nested. Consumers must avoid adding both a generic execution phase and its backend-specific child, which would double-count execution time.

## Experience database

The experience database groups records by workload fingerprint and execution-plan key. It tracks best, average, last, variance, standard deviation, sample count, and confidence. Records can be saved to and loaded from a text file.

This is a historical measurement facility. Beta 1.0 does not claim general machine learning or guaranteed convergence to an optimal plan.

## Hardware discovery

`hardware_threads()` uses `std::thread::hardware_concurrency`. On Windows, `hardware_characteristics()` additionally queries logical processor information for physical cores, NUMA nodes, page size, cache line size, and aggregate cache sizes when available.

The existence of topology data does not mean every field is already used by the decision model. Deeper topology-aware scheduling belongs to later versions.

## Extension boundaries

The current design makes these improvements possible without changing the basic public API:

- new decision providers
- richer performance models
- additional execution backends
- adaptive thread and chunk selection
- persistent historical policies

The public API should remain small while internal policy becomes more sophisticated.

## Utility-model persistence

Phase 1 now emits `validation/phase1/smartparallel_utility_model.spm`, a versioned text artifact containing the feature-schema identifier, promotion status, scaler parameters, and learned linear weights. C++ applications can load it with `smart::ranking::load_utility_model_artifact`; production code must still check `artifact.promoted()` and fall back to the analytical policy when the model is shadow-only, incompatible, or missing.
