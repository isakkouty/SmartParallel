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
