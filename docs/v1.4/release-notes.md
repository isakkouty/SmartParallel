# SmartParallel v1.4.0 release notes

SmartParallel v1.4 adds adaptive standard-algorithm-style APIs on the stabilized v1.1 scheduler and the v1.3 portability/CI foundation.

## Added

- Fourteen adaptive parallel algorithm families for elementwise work, reductions, counting, predicates, and search.
- Unary and binary `parallel_transform` overloads.
- Unary and binary `parallel_transform_reduce` overloads.
- Indexed deterministic `parallel_generate`.
- Ordered chunk reduction and earliest-index parallel search.
- Dedicated correctness, nested-execution, exception, installed-package, sanitizer, and benchmark coverage.
- A five-mode benchmark matrix producing summary and raw CSV files.
- Reproducible v1.4 benchmark reporting with PNG/SVG figures, generated Markdown, machine-readable metrics, and source hashes.

## Performance correction

- Added an Auto-only, root-only algorithm hot-dispatch cache for `parallel_reduce`, `parallel_count`, `parallel_find`, `parallel_find_if`, `parallel_any_of`, `parallel_all_of`, `parallel_none_of`, and `parallel_copy`.
- Moved the stable sequential decision before chunk construction and scheduler entry.
- Learned from separate real complete scheduled and sequential invocations without duplicating user work.
- Added bounded sharded retention, workload/byte buckets, policy and profile-cache epoch invalidation, single-flight probes, hysteresis, and periodic revalidation.
- Corrected `parallel_copy` learning by measuring actual sequential copy time instead of inferring it from bandwidth-limited parallel execution.
- Updated scheduled copy chunks to call `std::copy` so contiguous iterators retain standard-library optimizations.
- Reduced search and predicate coordination overhead with relaxed atomics while retaining scheduler-completion synchronization.
- Cached Windows hardware topology discovery for the process lifetime.

## Preserved behavior

- Forced ThreadPool, StaticThread, and oneTBB modes bypass the algorithm hot cache.
- Nested calls retain the established nested execution session and participant budget.
- `parallel_for`, scheduler policy, backend engines, and v1.1 benchmark algorithms are unchanged.
- The already-fast transform, generate, for-each, fill, count-if, and transform-reduce families keep their original automatic scheduling path.

## Accepted benchmark result

The checked-in Windows/MSVC Release snapshot contains seven repetitions per algorithm/mode pair:

- **80/80 summary rows** and **560/560 raw samples** passed correctness and backend authentication;
- automatic selected ThreadPool for eight compute-heavy cases and direct sequential execution for eight cheap/bandwidth-sensitive cases;
- the parallel-selected group achieved a **3.30× geometric-mean speedup**, ranging from **2.67× to 3.53×**;
- every corrected cheap-dispatch family remained within **3.5%** of direct sequential latency or faster.

See [benchmark results](benchmarks.md) for the complete table and qualifications.

## Compatibility

- C++17 and CMake 3.20 remain the minimum requirements.
- The installed target remains `SmartParallel::smart_parallel`.
- v1.4 algorithms require random-access iterators.
- Reduction operations must be associative.
