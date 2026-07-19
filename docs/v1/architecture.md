# Architecture

## Design objective

SmartParallel separates **policy** from **execution**. The decision layer determines whether parallelism is worthwhile and which plan is most appropriate; backend runtimes perform the low-level scheduling.

## End-to-end flow

```text
User callback and index range
          |
          v
Function-profile cache lookup
          |
          +-- reliable sequential profile --> cached sequential fast path
          |
          v
Regional callback sampling
          |
          v
WorkloadAnalyzer + hardware characteristics
          |
          v
PerformanceModelBuilder and workload-family classification
          |
          v
Candidate generation
  Sequential / ThreadPool / StaticThread / oneTBB
          |
          v
Analytical prediction
  + machine calibration
  + experience ranking
  + residual correction
  + confidence and risk controls
          |
          v
ExecutionPlan
          |
          v
Backend execution
          |
          v
Decision report, timing diagnostics, optional experience update
```

## Major modules

| Module | Responsibility |
|---|---|
| `workload/` | Represents and structurally analyzes work |
| `profiling/` | Samples callback cost and caches reusable profiles |
| `hardware/` | Captures CPU topology and cache characteristics |
| `model/` | Builds performance and memory feature models |
| `decision/` | Generates, predicts, calibrates, and ranks candidate plans |
| `ranking/` | Utility and regret-aware learned ranking components |
| `experience/` | Stores observations and historical plan outcomes |
| `execution/` | Implements ThreadPool, StaticThread, and oneTBB execution |
| `validation/` | Measures correctness, prediction quality, and overhead |

## Safety boundaries

The analytical model remains the fallback. Learned evidence is bounded by minimum sample counts, confidence thresholds, uncertainty penalties, and override guards. Online exploration is disabled by default.

## State and thread considerations

`global_config()`, the function-profile cache, experience database, last decision report, and diagnostics are process-wide facilities. Applications should configure the library before concurrent use and should not treat the single “last report” as per-request telemetry in a multi-caller environment.
