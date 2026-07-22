# SmartParallel

SmartParallel is a C++17 library that chooses and coordinates CPU parallel-loop execution at runtime, including nested loops that would otherwise oversubscribe the machine.

## What it solves

The fastest way to execute a loop depends on callback cost, iteration count, irregularity, backend overhead, and whether the loop is already inside parallel work. Hard-coding one strategy everywhere can make small loops slower and nested loops unsafe or wasteful.

SmartParallel provides one public loop API and applies two generations of runtime policy:

- **SmartParallel v1.0 — Automatic Loop Optimization:** selects a sequential or parallel execution strategy and improves repeated decisions using runtime observations.
- **SmartParallel v1.1 — Nested Parallelism Coordination:** coordinates nested loops through a shared root session, bounded worker leases, automatic frontier selection, and stable-plan reuse.

## Minimal example

```cpp
#include <smart/execution/parallel.hpp>

#include <cstddef>
#include <vector>

int main()
{
    std::vector<double> values(1'000'000);

    smart::parallel_for(
        std::size_t{0},
        values.size(),
        [&](std::size_t i)
        {
            values[i] = static_cast<double>(i) * 2.0;
        });
}
```

Nested calls use the same API. SmartParallel coordinates them under the active root budget instead of creating independent teams at every depth.

## Capabilities

- Runtime selection between sequential, ThreadPool, StaticThread, and oneTBB execution.
- Automatic profiling, bounded in-process experience, stable-plan reuse, and periodic production revalidation.
- Root-scoped nested concurrency budgets and lease accounting.
- Automatic nested-frontier selection with descendant sequential fast paths.
- Cooperative ThreadPool helping and constrained oneTBB arenas.
- Exception propagation, cooperative cancellation, exactly-once validation, and structured trace export.
- CMake package installation as `SmartParallel::smart_parallel`.

## Real-world v1.1 results

The final recorded suite covers OpenCV image pipelines, LZ4 batch compression, custom BVH construction, and a custom particle simulation. On the recorded four-worker Windows/MSVC machine:

- all **20 meaningful presets** (automatic median runtime at least 1 ms) beat sequential execution;
- automatic execution achieved a **2.33× geometric-mean speedup** across those presets;
- **19 of 20** were within 20% of the fastest valid tested strategy;
- all reported rows passed correctness checks, backend traces authenticated execution, and root concurrency stayed within four participants.

The results are machine-specific measurements, not universal guarantees. See the [v1.1 benchmark report](docs/v1.1/benchmarks.md) and [methodology](docs/v1.1/benchmark-methodology.md).

## Build

Requirements: CMake 3.20+, a C++17 compiler, and oneTBB for the oneTBB backend. The repository includes a vcpkg manifest for oneTBB, OpenCV, and LZ4 benchmark dependencies.

```text
cmake --preset release
cmake --build --preset release
```

Install and consume the exported package:

```text
cmake --install build/release --prefix path/to/install
```

```cmake
find_package(SmartParallel CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE SmartParallel::smart_parallel)
```

Windows users can run the complete real-world validation suite with:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

See [installation](docs/v1.1/installation.md) and [benchmark reproduction](docs/v1.1/benchmark-reproduction.md) for the complete workflows.

## Documentation

- [v1.1 documentation](docs/v1.1/README.md) — current release
- [Getting started](docs/v1.1/getting-started.md)
- [API reference](docs/v1.1/api.md)
- [Architecture](docs/v1.1/architecture.md)
- [Nested parallelism](docs/v1.1/nested-parallelism.md)
- [Runtime learning](docs/v1.1/runtime-learning.md)
- [Diagnostics](docs/v1.1/diagnostics.md)
- [Known limitations](docs/v1.1/known-limitations.md)
- [v1.0 archive](docs/v1.0/README.md)
- [Historical engineering archive](docs/archive/README.md)

## Project status

**Current release: v1.1.0.** SmartParallel is production-ready within the documented CPU parallel-loop scope and validated configurations. The nested execution model, worker accounting, supported CPU backends, and final real-world benchmark suite are stabilized for the public v1.1 release.

Important boundaries remain: admission is per root rather than process-wide, global configuration must not be mutated concurrently, traces add overhead, experience is in-memory by default, and automatic scheduling is not guaranteed to beat the best manually selected strategy on every workload.

## License

SmartParallel is distributed under the terms in [LICENSE](LICENSE).
