# Getting Started

## Requirements

SmartParallel Beta 1.0 currently targets C++17 and has been developed primarily with MSVC on Windows. Intel oneTBB is required by the current execution backend.

- CMake 3.20+
- C++17 compiler
- vcpkg or another oneTBB installation
- Python 3 with `pandas`, `matplotlib`, and `numpy` only when regenerating benchmark figures

## Build

With vcpkg configured through `VCPKG_ROOT`:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

The example executable is named `smartparallel_example`.

## First program

```cpp
#include <smart/execution/parallel.hpp>

#include <vector>

int main()
{
    std::vector<int> values(100'000, 1);

    smart::for_each(values, [](int& value)
    {
        value = value * 3 + 7;
    });
}
```

The callback may modify each element. It must be safe for concurrent invocation when SmartParallel chooses a parallel plan.

## Pair iteration

```cpp
smart::for_each_pair(points, segments,
    [](Point& point, Segment& segment)
    {
        test_intersection(point, segment);
    });
```

This executes the Cartesian product. The total iteration count is `points.size() * segments.size()`, so memory and runtime can grow quickly.

## Index ranges

```cpp
smart::parallel_for(0, count, [](std::size_t i)
{
    process(i);
});
```

## Diagnostics

```cpp
smart::global_config().enable_timing_diagnostics = true;
smart::global_config().enable_experience = false;

smart::for_each(values, work);

const auto& decision = smart::global_last_decision_report();
const auto& timings = smart::last_timing_report();
```

Diagnostics are useful for development and benchmarks. They add measurement work and should be disabled when the application does not need them.

## Callback safety

A callback must not introduce data races. Safe patterns include modifying only the element passed to the callback or using properly synchronized shared state. Avoid unsynchronized writes to a global accumulator from parallel callbacks.

## Benchmark plots

```powershell
py -m pip install pandas matplotlib numpy
py benchmarks\plot_all.py
```

Figures are written to each benchmark's `images/beta_1_0/` directory.
