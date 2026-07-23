# SmartParallel v1.4 — Parallel Algorithm Expansion

> **Current release:** SmartParallel v1.4.0

SmartParallel v1.4 extends the adaptive indexed runtime with parallel algorithms for elementwise work, reductions, counting, predicates, and search. It keeps the stabilized scheduler and nested-execution model while adding a root automatic hot-dispatch path for operations whose complete sequential body is cheaper than scheduler construction.

## Release result

The accepted benchmark snapshot validates all sixteen measured API cases across sequential, automatic, ThreadPool, StaticThread, and oneTBB modes:

- **80/80 summary rows** and **560/560 raw samples** passed correctness and backend authentication;
- the eight parallel-selected cases achieved a **3.30× geometric-mean speedup** over sequential;
- their automatic speedups ranged from **2.67× to 3.53×**;
- the eight corrected cheap-dispatch families were all within **3.5%** of direct sequential latency or faster.

![SmartParallel v1.4 automatic speedup](assets/benchmarks/automatic-speedup-by-algorithm.png)

See the [complete benchmark report](benchmarks.md) for all values, forced-backend comparisons, qualifications, and source data.

## Public APIs

```text
parallel_for_each
parallel_transform
parallel_copy
parallel_fill
parallel_generate
parallel_reduce
parallel_transform_reduce
parallel_count
parallel_count_if
parallel_any_of
parallel_all_of
parallel_none_of
parallel_find
parallel_find_if
```

All v1.4 algorithms are declared by:

```cpp
#include <smart/execution/algorithms.hpp>
```

## Runtime integration

Parallel plans reuse automatic backend selection, runtime profiling, nested execution sessions, root concurrency budgets, ThreadPool helping, StaticThread execution, oneTBB arenas, exception propagation, and diagnostics.

For `reduce`, `count`, `find`, predicate families, and `copy`, a bounded root automatic learning phase compares real end-to-end scheduled and sequential invocations. Stable sequential decisions are then checked before chunk creation so later calls can execute the direct one-pass implementation without constructing the scheduler. Forced backends and nested calls retain the original runtime path.

## Documentation

- [API and correctness contracts](api.md)
- [Architecture and hot dispatch](architecture.md)
- [Benchmark results](benchmarks.md)
- [Benchmark methodology](benchmark-methodology.md)
- [Benchmark reproduction](benchmark-reproduction.md)
- [Validation](validation.md)
- [Known limitations](known-limitations.md)
- [Release notes](release-notes.md)

Detailed scheduler and nested-runtime behavior remains documented under [`docs/v1.1/`](../v1.1/README.md). Cross-platform build and CI instructions remain under [`docs/v1.3/`](../v1.3/README.md).
