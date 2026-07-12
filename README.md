<div align="center">

# SmartParallel

### Adaptive C++ parallel execution framework

Write a familiar loop. Let SmartParallel decide whether and how to parallelize it.

**Beta 1.0** · **C++17** · **oneTBB** · **Windows-first prototype**

</div>

---

> [!IMPORTANT]
> SmartParallel is currently a **Beta 1.0 research and engineering prototype**. Its public API and decision heuristics may still evolve before the stable 1.0 release.

## Project Status

SmartParallel is currently in **Beta 1.0**.

This release focuses on validating the adaptive execution engine, workload analysis, function profiling, and backend selection through controlled synthetic and engineering-oriented benchmarks.

The core framework is functional, but the public API and decision model may still evolve before the first stable release.

Application case studies and third-party integrations are intentionally deferred until the API and runtime behavior receive further validation.

## The problem

Parallelizing a loop is not just a matter of adding threads. A developer must decide whether the workload is large enough, how expensive each iteration is, which backend to use, whether scheduling should be static or dynamic, and how many jobs should be created.

The best answer changes with the function, the input size, and the machine.

```cpp
for (auto& element : elements)
{
    compute(element);
}
```

A conventional parallel implementation requires the application to choose and maintain an execution strategy. SmartParallel keeps the application-facing code small:

```cpp
#include <smart/execution/parallel.hpp>

smart::for_each(elements, compute);
```

SmartParallel builds a workload description, analyzes it, samples the function, asks the decision engine for an execution plan, and delegates the work to the selected backend.

## Runtime pipeline

```text
User call
   │
   ▼
Workload Builder ──► Workload Analyzer ──► Function Profiler
                                              │
                                              ▼
                                      Decision Engine
                                              │
                         ┌────────────────────┼────────────────────┐
                         ▼                    ▼                    ▼
                    Sequential          Static Threads          oneTBB
                                              │
                                              ▼
                                      Execution statistics
```

The current public algorithms are:

```cpp
smart::for_each(container, function);
smart::parallel_for(begin, end, function);
smart::for_each_pair(a, b, function);
```

`for_each` and `for_each_pair` currently use runtime function profiling. `parallel_for` uses workload analysis and the decision engine without the same function-profiling path.

## What Beta 1.0 contains ( Beta 1.0 is published under the semantic version tag `v0.9.0-beta.1`. )

- Workload construction for index ranges, containers, and pair-container iteration spaces
- Workload analysis and hardware characteristic collection
- Runtime sampling through `FunctionProfiler`
- A composable decision-provider architecture
- Sequential, static-thread, thread-pool, and oneTBB execution paths
- Optional timing diagnostics
- An in-memory experience database with file save/load support
- A ten-benchmark validation suite with CSV output and generated figures

## Quick start

### Requirements

- A C++17 compiler
- CMake 3.20 or newer
- Intel oneTBB
- vcpkg is supported by the included `vcpkg.json`

### Build with CMake and vcpkg

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release
```

Run the example:

```powershell
.\build\Release\smartparallel_example.exe
```

### Minimal example

```cpp
#include <smart/execution/parallel.hpp>

#include <cmath>
#include <vector>

int main()
{
    std::vector<double> values(100'000, 1.0);

    smart::for_each(values, [](double& value)
    {
        for (int i = 0; i < 200; ++i)
        {
            value = std::sqrt(value + 1.0);
        }
    });
}
```

## Configuration and diagnostics

```cpp
smart::global_config().enable_experience = false;
smart::global_config().enable_timing_diagnostics = true;
```

After a profiled `for_each` or `for_each_pair`, the latest selected plan is available through:

```cpp
const auto& report = smart::global_last_decision_report();
```

Timing phases are available through `smart::last_timing_report()` when diagnostics are enabled.

## Benchmark suite

The repository contains benchmarks for tiny loops, compute-heavy kernels, mixed and irregular work, nested and pair iteration spaces, memory-oriented transforms, function profiling, and an engineering mesh workload.

![Engineering mesh scaling](benchmarks/engineering_mesh/images/beta_1_0/mesh_scaling.png)

The large engineering cases demonstrate that the framework can remain close to the fastest measured backend while preserving an automatic API. The memory-bandwidth benchmark also documents an important Beta 1.0 limitation: very cheap streaming transforms can be parallelized too early and may use a less efficient execution path than direct oneTBB.

See the [benchmark overview](benchmarks/README.md) for methodology, figures, build commands, and detailed interpretations.

## Project layout

```text
include/smart/        Public headers grouped by subsystem
src/                  Non-template implementation files
examples/             Small usage examples
benchmarks/           Benchmark programs, CSV results, plots, and READMEs
docs/                 Architecture, API, and getting-started documentation
```

## Documentation

- [Getting started](docs/getting_started.md)
- [Architecture](docs/architecture.md)
- [Public API guide](docs/api.md)
- [Decision engine](docs/decision_engine.md)
- [Benchmarks](benchmarks/README.md)
- [Roadmap](ROADMAP.md)
- [Contributing](CONTRIBUTING.md)

## Current limitations

Beta 1.0 should be understood as a validated prototype, not a universal optimizer.

- The decision model is heuristic and currently tuned around CPU execution.
- Function profiling adds a fixed runtime cost that is visible on tiny workloads.
- Memory-bound classification is not explicit yet.
- Thread count and chunk size are not fully adaptive.
- Hardware support is Windows-first; portability requires more validation.
- The experience database is useful infrastructure, but it is not an autonomous learning system.

## Version direction

The stable 1.0 release focuses on API stability, reproducible builds, tests, documentation, and cross-machine validation. Version 2 is intended to improve runtime prediction, memory-bound detection, adaptive thread counts, chunk sizing, and hardware awareness.

### Application validation

- Add representative application case studies
- Compare baseline, manually parallelized, and SmartParallel variants
- Measure integration complexity and performance
- Validate execution decisions across multi-phase workloads

### Third-party integrations

- Integrate SmartParallel into selected open-source workloads
- Evaluate real-world adoption cost
- Compare against existing parallel execution policies
- Document known limitations and migration patterns

## License

SmartParallel is released under the MIT License. See the `LICENSE` file for details.
