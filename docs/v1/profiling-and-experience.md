# Profiling and experience

## Automatic callback profiling

Unknown non-nested root workloads can use bounded callback sampling while preserving exactly-once execution by excluding sampled indices from later execution. Nested cold execution uses a conservative exactly-once first run and learns from completed execution telemetry instead of recursively pre-running nested callbacks.

The profile records callback cost distribution, confidence, nested-call evidence, structural child observations, and the plan context used for the observation.

## Profile cache lifecycle

Profiles are keyed by callable identity, workload bucket, nesting context, root budget/backend context, and a scheduler-policy signature. Function pointers include their address. Reused functor types or `std::function` callsites that need independent profiles can be wrapped explicitly:

```cpp
smart::parallel_for(first, last,
    smart::with_parallel_callsite(0xA17E, reusable_callback));
```

The cache is bounded by `parallel_for_profile_cache_max_entries`. Inactive least-recently-used entries are evicted; entries currently being constructed or revalidated are protected. Counters saturate rather than wrapping.

Nested-call evidence is blended and can decay. Observations are counted once per root execution group so repeated sibling invocations do not manufacture confidence.

## Stable plans and revalidation

Stable plans are reused only when their complete contextual key and scheduler-policy signature match. Periodic revalidation is single-flight: one caller refreshes a key while concurrent callers continue using the last complete profile.

A contradictory observation invalidates the stable plan immediately. This prevents permanent lock-in after workload drift while avoiding a revalidation stampede.

Changing `global_config()` while calls are executing is unsupported. Configure the process before starting concurrent work.

## Experience database

When enabled, completed executions can update historical evidence for workload fingerprints and plans. Persistence is off by default. Historical ranking remains bounded and separate from the in-process profile-cache lifecycle.
