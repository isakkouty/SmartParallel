# v1.4 known limitations

SmartParallel v1.4 is designed to choose a strong stable route, not to guarantee the fastest possible backend for every invocation.

## Performance evidence is machine-specific

The checked-in benchmark uses one Windows/MSVC Release environment and seven repetitions per row. CPU model, memory configuration, power state, operating-system activity, compiler revision, and oneTBB revision can all change the result. Reproduce the suite on target hardware before making workload-specific claims.

## Automatic mode can differ from the fastest forced backend

The hot-dispatch policy uses hysteresis and favors direct sequential execution when results are close or noisy. A forced oneTBB run can therefore be faster in an individual benchmark case even when automatic mode selects sequential. This is visible in the accepted `parallel_any_of`, `parallel_find_if`, and `parallel_copy` rows.

## Learning is in-process

Algorithm dispatch evidence is retained in memory. A new process starts a new learning phase. Periodic revalidation intentionally adds occasional probe calls, and the first calls at a new workload bucket may not represent steady-state performance.

## Nested calls keep the existing scheduler path

The root automatic hot cache does not override nested execution. Nested algorithms continue through the established session, budget, and frontier machinery. This avoids cross-context decisions but means a cheap nested call can retain more scheduler overhead than a learned root call.

## Random-access iterators are required

All v1.4 algorithms require random-access iterators because the implementation partitions contiguous indexable ranges. Forward-only and bidirectional iterators are not accepted.

## Short-circuit work is best effort

Predicate and search operations can have work already in flight after the logical result is known. Invocation order and total predicate call count are not specified. Search still returns the earliest matching iterator.

## Reductions require associativity

Chunk and input ordering are preserved, but parallel parenthesization can differ from a strict left fold. Floating-point results can differ in their least significant bits. Non-associative operations are not supported as parallel reductions.

## Configuration remains process-global

Runtime configuration must be established before concurrent execution and must not be mutated concurrently with active calls. Cache generations handle material configuration changes, but they do not make process-global configuration mutation thread-safe.

## oneTBB remains optional

The oneTBB backend and its forced benchmark rows are available only when SmartParallel is built with oneTBB enabled. ThreadPool, StaticThread, sequential, and automatic execution remain available without it.
