# API reference

## Primary header

```cpp
#include <smart/execution/parallel.hpp>
```

## `smart::parallel_for`

```cpp
template <typename Function>
void parallel_for(std::size_t begin, std::size_t end, Function function);
```

Executes `function(index)` for every index in `[begin, end)`. The function returns after all iterations complete. Empty ranges are accepted; reversed ranges throw `std::invalid_argument`.

## Configuration

```cpp
smart::Config& config = smart::global_config();
```

Configuration is process-wide. Set options before starting concurrent work. See [Configuration reference](configuration.md).

## Decision report

```cpp
const smart::DecisionReport& report = smart::global_last_decision_report();
```

The report includes the selected `ExecutionPlan`, optional function profile, candidate predictions, and decision metadata.

## Profile diagnostics

```cpp
const smart::ParallelForProfileDiagnostics& diagnostics =
    smart::global_last_parallel_for_profile_diagnostics();
```

Diagnostics include cache status, sampled-iteration count, fast-path status, and phase timings.

## Execution hints

`ExecutionHints` can describe arithmetic intensity, branchiness, memory randomness, vectorization potential, dependency depth, external working-set size, bytes touched per iteration, and estimated memory-level parallelism. Convenience constructors include `compute_heavy()`, `memory_random()`, and `pointer_chasing()`.

## Direct execution plans

Lower-level code may construct an `ExecutionPlan` and call execution facilities directly, but this is an advanced integration surface. The v1 stability promise centers on `parallel_for`, configuration, hints, and diagnostics.
