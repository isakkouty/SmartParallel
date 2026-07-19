# Diagnostics

## Decision report

`global_last_decision_report()` provides the most recent selected plan and the evidence used to choose it. Typical fields include engine, strategy, workers, chunk size, function profile, candidate predictions, confidence, and decision source.

## Timing diagnostics

Enable phase timings before the call:

```cpp
smart::global_config().enable_timing_diagnostics = true;
```

The profiler diagnostics separate cache lookup, workload analysis, profiling, decision, execution, and total call time. This distinction is essential when evaluating tiny workloads because execution may be fast while decision overhead dominates.

## Interpreting plans

Examples:

```text
Sequential
oneTBB/DynamicChunks/w16/c0
ThreadPool/DynamicChunks/w8/c256
StaticThread/StaticChunks/w8/c0
```

A chunk value of zero means backend default. Worker count is a maximum concurrency request, not a guarantee that every worker is active throughout execution.

## Production telemetry warning

The “last” report and diagnostics are global snapshots. They are convenient for tests and single-caller benchmarking, but applications requiring per-request concurrent telemetry should add an explicit reporting surface rather than reading a process-wide last value.
