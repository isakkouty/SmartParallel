# SmartParallel

SmartParallel is a C++17 adaptive execution framework. It chooses and coordinates CPU parallel-loop scheduling at runtime and, for recognized semantic operations, can also select between complete Native and specialized implementations.

## What it solves

The fastest way to execute a loop depends on callback cost, iteration count, irregularity, backend overhead, and whether the loop is already inside parallel work. Hard-coding one strategy everywhere can make small loops slower and nested loops unsafe or wasteful.

SmartParallel provides an adaptive loop runtime, coordinated nested execution, cross-platform validation, a v1.4 parallel-algorithm layer, and an optional v1.5 semantic-operation layer that can choose between Native SmartParallel and specialized implementations. The stabilized runtime continues to run across Windows, Linux, and macOS:

- **SmartParallel v1.0 — Automatic Loop Optimization:** selects a sequential or parallel execution strategy and improves repeated decisions using runtime observations.
- **SmartParallel v1.1 — Nested Parallelism Coordination:** coordinates nested loops through a shared root session, bounded worker leases, automatic frontier selection, and stable-plan reuse.
- **SmartParallel v1.3 — Cross-Platform CI and Portability:** validates the same scheduler and API on Windows/MSVC, Linux/GCC and Clang, and macOS/Apple Clang, including installed-package consumers and oneTBB on/off builds.
- **SmartParallel v1.4 — Parallel Algorithm Expansion:** adds adaptive elementwise, reduction, counting, predicate, and search algorithms, including pre-scheduler hot dispatch for cheap operations.
- **SmartParallel v1.5 — Adaptive Execution Routes:** adds optional semantic operations whose automatic mode can learn between complete Native routes and specialized providers, beginning with exact 8-bit thresholding and OpenCV.

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

The v1.4 algorithms are available from:

```cpp
#include <smart/execution/algorithms.hpp>

#include <functional>

auto total = smart::parallel_transform_reduce(
    values.begin(), values.end(), 0.0, std::plus<>{},
    [](double value) { return value * value; });
```

The optional v1.5 vision module expresses an operation while leaving the complete route automatic:

```cpp
#include <smart/vision/vision.hpp>

smart::vision::threshold(source, destination);
```

Depending on measured behavior and availability, the same call can use Native Sequential, Native ThreadPool, Native oneTBB, or OpenCV `cv::threshold`.

## Capabilities

- Runtime selection between sequential, ThreadPool, StaticThread, and oneTBB execution.
- Automatic profiling, bounded in-process experience, stable-plan reuse, and periodic production revalidation.
- Root-scoped nested concurrency budgets and lease accounting.
- Automatic nested-frontier selection with descendant sequential fast paths.
- Cooperative ThreadPool helping and constrained oneTBB arenas.
- Exception propagation, cooperative cancellation, exactly-once validation, and structured trace export.
- Native Windows, Linux, and macOS CPU-topology/cache discovery with conservative fallbacks.
- Fourteen adaptive v1.4 algorithms covering transforms, reductions, counting, predicates, and search, with a direct one-pass path when automatic scheduling selects sequential execution.
- Optional v1.5 semantic threshold operation with balanced successive-elimination learning, independent holdout verification, sparse drift sentinels, current-context ABBA revalidation, hot-cache reuse, runtime-selected AVX2/SSE2 native kernels, exact strided/in-place support, and optional zero-copy OpenCV execution.
- Core CMake package installation as `SmartParallel::smart_parallel`, plus the optional separate `SmartParallel::vision` target.

## v1.5 adaptive-route results

The accepted Windows/MSVC Release publication run evaluated exact `uint8_t` thresholding across six image profiles from 320×240 through 8K, including a strided 1080p ROI:

- all **2,238 measured samples** passed exact-output correctness and route authentication;
- all **6/6 combined release gates** passed;
- Auto achieved a **1.16× geometric-mean speedup over the independent sequential loop** and **1.48× over direct OpenCV** on the recorded machine;
- Auto settled on Native Sequential for the small and 1080p profiles and Native ThreadPool for 4K and 8K;
- the two 1080p profiles initially learned OpenCV, detected a changed deployment regime, and switched once to Native Sequential;
- the authenticated Native AVX2 kernel passed the independent compiler-oracle gate on all six presets;
- stable Auto dispatch was approximately **0.012–0.051 µs** for the small and medium profiles, while large-profile intervals were correctly reported as statistically inconclusive passes.

![SmartParallel v1.5 automatic speedup](docs/v1.5/assets/benchmarks/v1.5.0_automatic_speedup.svg)

These measurements are machine-specific. The release claim is that Auto selected a route within the declared equivalence gate and adapted when its original decision became stale—not that one provider is universally fastest. See the [complete v1.5 benchmark report](docs/v1.5/benchmarks.md), [methodology](docs/v1.5/benchmark-methodology.md), and [reproduction guide](docs/v1.5/benchmark-reproduction.md).

## v1.4 algorithm results

The accepted Windows/MSVC Release snapshot covers sixteen algorithm cases across sequential, automatic, ThreadPool, StaticThread, and oneTBB modes:

- all **80 summary rows** and **560 raw samples** passed correctness and backend authentication;
- automatic selected ThreadPool for eight compute-heavy cases and direct sequential execution for eight cheap or bandwidth-sensitive cases;
- the parallel-selected group achieved a **3.30× geometric-mean speedup**, ranging from **2.67× to 3.53×**;
- every corrected cheap-dispatch family stayed within **3.5%** of direct sequential latency or faster.

![SmartParallel v1.4 automatic speedup](docs/v1.4/assets/benchmarks/automatic-speedup-by-algorithm.png)

The results are machine-specific. See the [complete v1.4 benchmark report](docs/v1.4/benchmarks.md), [methodology](docs/v1.4/benchmark-methodology.md), and [reproduction guide](docs/v1.4/benchmark-reproduction.md).

## Real-world v1.1 results

The final recorded suite covers OpenCV image pipelines, LZ4 batch compression, custom BVH construction, and a custom particle simulation. On the recorded four-worker Windows/MSVC machine:

- all **20 meaningful presets** (automatic median runtime at least 1 ms) beat sequential execution;
- automatic execution achieved a **2.33× geometric-mean speedup** across those presets;
- **19 of 20** were within 20% of the fastest valid tested strategy;
- all reported rows passed correctness checks, backend traces authenticated execution, and root concurrency stayed within four participants.

The results are machine-specific measurements, not universal guarantees. See the [v1.1 benchmark report](docs/v1.1/benchmarks.md) and [methodology](docs/v1.1/benchmark-methodology.md).

## Build

Requirements: CMake 3.20+, a C++17 compiler, and oneTBB only when the oneTBB backend is enabled. OpenCV is required only when the optional v1.5 OpenCV route is enabled. SmartParallel is continuously validated with MSVC, GCC, Clang, and Apple Clang. The repository includes a vcpkg manifest for oneTBB and optional real-world benchmark dependencies.

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

When the optional vision module was installed:

```cmake
find_package(SmartParallelVision CONFIG REQUIRED)
target_link_libraries(my_application PRIVATE SmartParallel::vision)
```

Windows users can build, test, and benchmark every v1.4 algorithm with:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v14_algorithm_release_validation.bat 7
```

Run the v1.5 adaptive-route publication workflow with:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v15_adaptive_routes_release_validation.bat 31
```

The publication workflow first learns under repeated calls, then enters a balanced interleaved deployment regime with normal drift detection and current-context revalidation enabled. After the route settles, maintenance is paused only for the clean timed matrix. The workflow records training and current baselines, route switches, drift evidence, balanced route orders, and batched adjacent ABBA/BAAB dispatch overhead. Production drift detection and revalidation remain enabled outside that measurement window.

The historical v1.1 real-world suite remains available through `scripts\benchmarks\run_real_world_complete.bat`. See the [v1.4 API](docs/v1.4/api.md), [architecture](docs/v1.4/architecture.md), [benchmark results](docs/v1.4/benchmarks.md), [v1.4 reproduction guide](docs/v1.4/benchmark-reproduction.md), [v1.3 cross-platform installation guide](docs/v1.3/installation.md), [GitHub Actions setup](docs/v1.3/ci-and-github-setup.md), [native hardware discovery](docs/v1.3/native-hardware-discovery.md), and [release checklist](docs/v1.3/release-checklist.md).

## Documentation

- [v1.5 documentation](docs/v1.5/README.md) — adaptive execution routes and optional OpenCV provider
- [v1.5 benchmark results](docs/v1.5/benchmarks.md) — accepted 6/6 proof-gate publication and figures
- [v1.5 benchmark methodology](docs/v1.5/benchmark-methodology.md) — fairness, learning, adaptation, and release gates
- [v1.4 documentation](docs/v1.4/README.md) — parallel-algorithm release retained by v1.5
- [v1.4 benchmark results](docs/v1.4/benchmarks.md) — accepted automatic/forced backend matrix and graphs
- [v1.3 documentation](docs/v1.3/README.md) — portability and CI release
- [v1.1 runtime documentation](docs/v1.1/README.md) — scheduler and nested-parallelism behavior retained by v1.4
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

**Current release: v1.5.0.** v1.5 preserves the stabilized scheduler and v1.4 algorithms while adding an optional semantic-operation layer that learns between complete Native and OpenCV threshold routes, verifies its initial winner, detects performance drift, and switches using current-context evidence. The accepted threshold publication passed all 6/6 combined gates with 2,238 correct and authenticated samples. The repository also retains the accepted v1.4 algorithm measurements and v1.1 real-world integration results. Performance benchmarks remain manual and machine-specific.

Important boundaries remain: admission is per root rather than process-wide, global configuration must not be mutated concurrently, traces add overhead, experience is in-memory by default, and automatic scheduling is not guaranteed to beat the best manually selected strategy on every workload.

## License

SmartParallel is distributed under the terms in [LICENSE](LICENSE).
