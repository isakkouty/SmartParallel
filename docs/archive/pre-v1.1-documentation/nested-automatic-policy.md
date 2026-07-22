# Automatic nested-loop policy

SmartParallel treats nested calls as one root execution domain when every nested level uses `smart::parallel_for`.

A root-scoped session owns the worker budget, immutable plan snapshots for the current run, and optional trace records. Nested backends acquire worker leases from that session, so sibling regions cannot independently exceed the configured root budget.

Cold nested callsites execute once, sequentially, while measuring the complete subtree. This avoids recursive sampling and preserves side-effect correctness. Cache identity includes the loop depth, parent callsite, iteration bucket, root budget, and requested engine.

Profile construction is nonblocking single-flight per context-aware key. One caller owns cold profile publication; concurrent siblings keep executing conservatively instead of blocking or racing to overwrite the same profile.

After telemetry is available, the automatic policy selects a parallel frontier. Underfilled outer levels are deferred. The first level with enough coarse work can be promoted, and descendants of that frontier use a low-overhead direct sequential path. A hysteresis margin prevents borderline work estimates from repeatedly changing plan.

For regular rectangular nests, `parallel_for_nd` is the preferred optimized form because it creates a single flattened work region.

## Trace

Set:

```cpp
smart::global_config().enable_nested_execution_trace = true;
```

Then retrieve or write records with:

```cpp
auto records = smart::nested_execution_trace_snapshot();
smart::write_nested_execution_trace_csv("nested_trace.csv", records);
```

The trace includes callsite/depth, cache and plan reuse, selected backend/policy, worker budget and leases, chunk size, helper jobs, cancellations, and the time between useful-work completion and helper retirement.
