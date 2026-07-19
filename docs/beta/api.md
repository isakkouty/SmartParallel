# Public API Guide

The public surface of Beta 1.0 is intentionally small. Include `<smart/execution/parallel.hpp>` for the adaptive algorithms.

## `smart::for_each`

```cpp
template <typename Container, typename Function>
void for_each(Container& container, Function function);
```

Applies `function` to every element in a mutable container. The implementation builds a workload, analyzes it, samples the callback, selects a plan, executes it, and optionally records experience.

Requirements:

- `container.size()` and `container[index]` must be available.
- The callback must accept the element expression produced by `container[index]`.
- Concurrent invocation must be safe when a parallel plan is selected.

## `smart::parallel_for`

```cpp
template <typename Function>
void parallel_for(std::size_t begin, std::size_t end, Function function);
```

Invokes `function(i)` for every index in `[begin, end)`. Beta 1.0 does not apply the same runtime function-profiling stage used by `for_each`.

## `smart::for_each_pair`

```cpp
template <typename ContainerA, typename ContainerB, typename Function>
void for_each_pair(ContainerA& a, ContainerB& b, Function function);
```

Invokes the callback for every pair `(a[i], b[j])`. Internally the Cartesian product is flattened into a one-dimensional range.

## Configuration

`smart::global_config()` returns the process-wide framework configuration. The commonly used Beta 1.0 switches are:

```cpp
smart::global_config().enable_timing_diagnostics = true;
smart::global_config().enable_experience = true;
```

## Decision report

After a profiled API call:

```cpp
const smart::DecisionReport& report =
    smart::global_last_decision_report();
```

The selected plan is available as `report.plan`, including engine, strategy, job count, and parallel flag.

## Timing report

```cpp
const smart::TimingReport& report = smart::last_timing_report();
```

The report contains named timing phases. Some phases may be nested, so summing every entry can double-count time.

## Experience database

```cpp
auto& database = smart::global_experience_database();
database.save_to_file("smartparallel-experience.txt");
database.load_from_file("smartparallel-experience.txt");
```

Experience recording is optional and keyed by workload fingerprints and execution plans.

## Hybrid persisted utility model

The V1 runtime model is opt-in and safe by default:

```cpp
smart::global_config().enable_utility_model_runtime = true;
smart::global_config().utility_model_file_path = "smartparallel_utility_model.spm";
smart::global_config().minimum_utility_model_confidence = 0.60;

smart::parallel_for(0, count, [&](std::size_t i) {
    process(i);
});
```

SmartParallel first obtains its analytical decision, then evaluates Sequential,
ThreadPool, and oneTBB candidate plans with the persisted utility model. The
model may override the analytical result only when all of the following hold:

- the artifact can be loaded and validated;
- its feature schema is `phase1_utility_v1`;
- its promotion status is `PROMOTED`;
- the score margin reaches `minimum_utility_model_confidence`.

Missing, malformed, incompatible, shadow-only, and low-confidence models always
fall back to the analytical plan. Diagnostics are available through
`smart::global_last_decision_report()` in the `utility_model_*` fields.

## Automatic `parallel_for` decision profiling

`smart::parallel_for(begin, end, callback)` automatically executes and times a
small prefix of the real range. Those iterations are completed exactly once and
are excluded from the subsequently scheduled remainder. The measured cost is
passed to the decision engine, allowing expensive callbacks to select a
parallel backend even when their iteration count is below the normal cheap-loop
threshold.

The behavior is enabled by default and can be tuned through:

```cpp
smart::global_config().enable_parallel_for_auto_profiling = true;
smart::global_config().parallel_for_profile_min_samples = 8;
smart::global_config().parallel_for_profile_max_samples = 64;
smart::global_config().parallel_for_profile_min_signal_ms = 0.01;
smart::global_config().parallel_for_estimated_overhead_ms = 1.0;
```

The analytical iteration thresholds are also configurable while retaining the
previous defaults:

```cpp
smart::global_config().small_workload_iteration_threshold = 1'000;
smart::global_config().cheap_workload_sequential_threshold = 100'000;
smart::global_config().many_iterations_threshold = 1'000'000;
```
