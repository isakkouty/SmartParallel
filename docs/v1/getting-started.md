# Getting started

## Requirements

SmartParallel requires C++17, CMake 3.20 or newer, and oneTBB. OpenCV is optional and required only for the computer-vision benchmarks.

## Build the library and example

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release ^
  -DTBB_DIR="<path-to-tbb-config>" ^
  -DSMARTPARALLEL_BUILD_EXAMPLES=ON
cmake --build build --config Release
```

## Use `parallel_for`

```cpp
#include <smart/execution/parallel.hpp>

smart::parallel_for(
    std::size_t{0},
    count,
    [&](std::size_t index)
    {
        output[index] = transform(input[index]);
    });
```

The range is half-open: `[begin, end)`. An empty range is valid. `end < begin` throws `std::invalid_argument`.

## Callback contract

The callback receives one `std::size_t` index. Each index is executed exactly once, including indices sampled during automatic profiling. The callback must therefore be safe to invoke concurrently whenever the selected plan is parallel. Writes should normally target independent elements or use explicit synchronization.

## Inspect the latest decision

```cpp
const smart::DecisionReport& report = smart::global_last_decision_report();
```

The report exposes the selected engine, strategy, worker count, chunk size, profile information, and candidate predictions. Timing details require `smart::global_config().enable_timing_diagnostics = true`.

## Run the complete Windows benchmark workflow

From a Visual Studio Developer Command Prompt:

```bat
scriptsenchmarksun_all_benchmarks.bat
```

Results are written to `validation/output/`.
