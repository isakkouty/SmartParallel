# API reference

> **Current documentation:** SmartParallel v1.1.0.

## Primary loop API

Header:

```cpp
#include <smart/execution/parallel.hpp>
```

```cpp
template <typename Function>
void smart::parallel_for(std::size_t begin,
                         std::size_t end,
                         Function function);
```

The callback is invoked exactly once for each index in `[begin, end)` when execution completes successfully. Exceptions propagate to the caller after cooperative cancellation and backend cleanup.

## Multidimensional iteration

```cpp
template <std::size_t Dimensions, typename Function>
void smart::parallel_for_nd(
    const std::array<std::size_t, Dimensions>& extents,
    Function function);
```

`parallel_for_nd` flattens the iteration space safely and reconstructs the multidimensional coordinates for the callback. Overflow in the flattened size raises `std::overflow_error`.

## Explicit callsite identity

Reusable functor types or `std::function` objects used for semantically different work can be separated with:

```cpp
auto tagged = smart::with_parallel_callsite(0xA17E, callback);
smart::parallel_for(0, count, tagged);
```

## Global configuration

```cpp
#include <smart/core/config.hpp>

smart::Config& config = smart::global_config();
```

Configuration is process-global. Set it before starting concurrent SmartParallel calls; unsynchronized mutation during execution is unsupported.

## Execution backends

```cpp
#include <smart/execution/backend.hpp>

smart::IExecutionBackend& backend =
    smart::execution_backend(smart::ExecutionEngineType::ThreadPool);
```

`ExecutionEngineType` values are `Auto`, `ThreadPool`, `StaticThread`, `OneTbb`, and `Sequential`.

## Execution context

```cpp
#include <smart/execution/execution_context.hpp>

auto context = smart::current_execution_context();
bool nested = smart::inside_nested_parallel_loop();
```

The context exposes loop lineage, depth, selected engine, parallel state, and inherited concurrency budget.

## Diagnostics

The public headers expose the latest profile/nested diagnostic structures and structured trace helpers:

```cpp
auto profile = smart::global_last_parallel_for_profile_diagnostics();
auto nested = smart::global_last_parallel_for_nested_diagnostics();

auto records = smart::nested_execution_trace_snapshot();
smart::write_nested_execution_trace_csv("nested_trace.csv", records);
```

Enable trace collection through `smart::global_config().enable_nested_execution_trace`. Trace collection adds overhead.

## Experience persistence APIs

```cpp
bool saved = smart::save_experience("smartparallel_experience.db");
bool loaded = smart::load_experience("smartparallel_experience.db");
```

The default v1.1 workflow keeps experience in process memory. Automatic loading/saving is disabled unless explicitly configured or invoked.
