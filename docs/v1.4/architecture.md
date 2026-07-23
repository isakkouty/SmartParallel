# v1.4 algorithm architecture

SmartParallel v1.4 adds a parallel-algorithm layer without introducing a second scheduler. Parallel plans still execute bounded contiguous chunks through the existing `parallel_for` runtime, nested execution session, participant budget, ThreadPool helper model, StaticThread engine, and oneTBB arena integration.

## Public call path

For algorithms that remain profitable in parallel, the path is:

```text
public algorithm
  -> range validation and size
  -> logical chunk construction
  -> algorithm callback adapter
  -> existing parallel_for scheduler
  -> selected backend
  -> result assembly
```

Each algorithm supplies a direct whole-range sequential implementation as well as a chunk implementation. The scheduler and forced backend modes continue to use the established execution contracts.

## Algorithm-level hot dispatch

Microsecond-scale algorithms cannot afford to enter the complete scheduler merely to discover that direct sequential execution is faster. The v1.4 correction therefore adds a narrow dispatch layer before chunk construction for these root automatic families:

```text
parallel_reduce
parallel_count
parallel_find / parallel_find_if
parallel_any_of / parallel_all_of / parallel_none_of
parallel_copy
```

The hot path is:

```text
public algorithm
  -> inexpensive dispatch-key lookup
  -> stable sequential decision: execute the whole-range direct body
  -> otherwise: continue into the existing scheduler path
```

The already-fast transform, generate, for-each, fill, count-if, and transform-reduce families do not use this new early cache.

## Learning without duplicated user work

A single public invocation never executes both sequential and scheduled work. Learning uses separate real calls:

1. one call executes the complete scheduled route and records end-to-end time;
2. another call executes the complete direct sequential route and records end-to-end time;
3. the cache selects a stable route only after both observations exist;
4. close results favor sequential execution through hysteresis;
5. periodic revalidation samples the alternative route on a later invocation.

This preserves exactly-once callable semantics, output correctness, exception behavior, and side-effect expectations.

## Dispatch identity

Entries distinguish material execution conditions, including:

- algorithm family;
- callsite/callable and value-type identity where relevant;
- actual element-count bucket;
- byte-count and element-size buckets for copy;
- root execution and effective concurrency budget;
- configuration and profiling generations.

The cache is bounded and sharded. A small local hot layer avoids a contended global lookup on stable decisions, while process-wide entries retain learning samples and revalidation state.

## Invalidation and revalidation

A decision is not permanent. Workload buckets prevent small and large ranges from sharing one plan, and policy/profile epochs invalidate decisions when configuration or execution conditions materially change. Stable routes are periodically revalidated so a workload can move from sequential to scheduled execution or back again.

## Forced and nested execution

Forced ThreadPool, StaticThread, and oneTBB modes bypass the algorithm hot cache. They continue directly into the established scheduler/backend path so benchmark authentication and user intent remain unchanged.

Nested algorithm calls also bypass the root hot cache. They retain the active nested execution session, participant leases, frontier decisions, and descendant suppression rules from the v1.1 runtime.

## Algorithm-specific behavior

### Reduction and count

Direct routes use the corresponding one-pass standard-library operation and avoid partial-result allocation. Parallel routes retain ordered chunk reduction and deterministic partial combination.

### Search and predicates

Direct routes avoid chunk state and atomics. Parallel routes retain earliest-index search and best-effort predicate short-circuiting. Coordination atomics use relaxed ordering because scheduler completion provides the final synchronization boundary.

### Copy

Copy dispatch uses real sequential and scheduled timings plus byte-aware workload buckets. It does not infer sequential cost by multiplying a parallel measurement by worker count, which is unreliable for memory-bandwidth-limited operations. Scheduled chunks invoke `std::copy` so contiguous iterator optimizations remain available.

## Hardware discovery

Windows hardware topology discovery is cached once per process, matching the existing Linux and macOS approach. This removes repeated operating-system topology queries from cheap root calls and scheduler setup.
