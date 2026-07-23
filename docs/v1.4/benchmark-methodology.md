# v1.4 algorithm benchmark methodology

The v1.4 benchmark executable covers all fourteen public API families and separately measures unary/binary transform and unary/binary transform-reduce, producing sixteen cases. Each case is measured in five modes:

- direct sequential reference;
- SmartParallel automatic selection;
- forced ThreadPool;
- forced StaticThread;
- forced oneTBB when oneTBB is available.

## Workloads

The benchmark uses deterministic random-access ranges. Compute-heavy transform, generate, for-each, count-if, and transform-reduce cases contain enough per-element work to make parallel execution profitable. Reduce, count, search, predicate, and copy cases intentionally exercise operations for which scheduler overhead or memory bandwidth can dominate.

Iteration counts are recorded in the CSV. The accepted snapshot uses 262,144 elements for most cases and 1,048,576 elements for copy and fill.

## Timing and correctness

Timing includes the complete public algorithm route and stops before checksum calculation. Every measured repetition then validates a deterministic checksum or result against the sequential reference. Forced backend rows authenticate the runtime that actually executed. Any incorrect or unauthenticated row causes the executable and release script to fail.

The benchmark writes:

```text
validation/output/v1.4.0_parallel_algorithms.csv
validation/output/v1.4.0_parallel_algorithms_raw.csv
```

The summary records median/minimum/maximum operation time, sequential median, speedup, checksum correctness, backend authentication, observed backend, and whether parallel execution was observed. The raw file retains every repetition.

## Automatic learning phase

Automatic rows include normal runtime learning. For the cheap-dispatch families, the unmeasured warm-up normally supplies a complete scheduled observation and the first measured call supplies a complete direct-sequential observation. Later calls use the learned route before chunk creation.

Each public invocation executes exactly one route. The benchmark never runs both alternatives inside one call, so user work, side effects, output writes, and exceptions are not duplicated for measurement.

Forced backend rows and nested calls bypass the algorithm-level hot cache and continue through the existing scheduler/backend implementations.

## Release interpretation

The v1.4 performance gate for the corrected cheap families is:

```text
automatic median <= direct sequential median * 1.05
```

This gate verifies that automatic dispatch no longer pays material scheduler overhead after learning. It does not require automatic mode to match the fastest forced backend on every machine.

For the protected parallel families, the release expectation is that automatic execution remains decisively faster than sequential and preserves the established scheduling path.

## Repetitions

The checked-in acceptance snapshot contains seven repetitions per algorithm/mode pair. Seven is suitable for development and regression confirmation. A new publication-quality machine result should use a larger odd count such as 31 on an otherwise idle physical machine.

See [benchmark reproduction](benchmark-reproduction.md) for the exact commands and [benchmark results](benchmarks.md) for the accepted data.
