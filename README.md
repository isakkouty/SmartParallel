# SmartParallel

SmartParallel is a C++17 adaptive parallel-loop library. Its public `smart::parallel_for` API profiles an unknown callback, analyzes the workload, predicts candidate execution costs, and selects a sequential or parallel execution plan. The current CPU backends are a persistent thread pool, static threads, and oneTBB.

> **Release status:** v1 release. The core implementation and validation suites are stable; packaging and cross-platform CI remain release-hardening work.

## Why SmartParallel exists

A parallel loop is not automatically faster. Small callbacks may cost less than scheduler dispatch, uniform work may favor static partitioning, and irregular work benefits from dynamic load balancing. SmartParallel makes that policy decision at runtime while preserving exactly-once callback execution.

## Quick example

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
        [&](std::size_t index)
        {
            values[index] = static_cast<double>(index) * 2.0;
        });
}
```

## Current execution pipeline

```text
parallel_for
    -> profile/cache lookup
    -> workload analysis
    -> candidate generation
    -> analytical + historical prediction
    -> confidence/risk-aware ranking
    -> Sequential | ThreadPool | StaticThread | oneTBB
    -> diagnostics and optional experience recording
```

## Latest validation snapshot

The repository includes the latest measured CSV outputs under `validation/output/`.

- All OpenCV, scientific, stress, and decision-audit numerical checks passed.
- The decision-quality audit selected the fastest measured backend in **18 of 24 cases (75%)**.
- All six backend-selection misses occurred in tiny or small workloads, where profiling and dispatch overhead are proportionally dominant.
- Medium and large irregular-particle cases achieved roughly **10x** speedup over the sequential baseline on the recorded machine.

These values are machine-specific observations, not universal performance guarantees. See [Benchmark results](docs/v1/benchmark-results.md) and [Methodology](docs/v1/benchmark-methodology.md).

## Build

Requirements:

- CMake 3.20+
- C++17 compiler
- oneTBB
- OpenCV only for the OpenCV benchmark targets
- Python 3 with pandas and matplotlib only for plotting tools

```bat
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTBB_DIR="<path-to-tbb-config>"
cmake --build build --config Release
```

The Windows convenience launchers are organized under [`scripts/`](scripts/README.md):

```bat
scriptsenchmarks
un_all_benchmarks.bat
scriptsenchmarks
un_opencv_benchmarks.bat
scriptsalidation
un_v1_phase1.bat
```

## Repository map

| Path | Purpose |
|---|---|
| `include/smart/` | Public and reusable library headers |
| `src/` | Compiled implementation units |
| `examples/` | Minimal integration example |
| `tests/v1/` | Deterministic correctness and hardening tests |
| `benchmarks/` | OpenCV, scientific, stress, and decision-quality workloads |
| `validation/` | Measurement programs and recorded results |
| `tools/` | Dataset analysis and benchmark plotting utilities |
| `scripts/` | Windows entry points for validation and benchmark runs |
| `docs/v1/` | Authoritative v1 documentation |
| `docs/beta/` | Archived pre-v1 documentation |

## Documentation

Start with the [v1 documentation index](docs/v1/README.md). Key references:

- [Getting started](docs/v1/getting-started.md)
- [Architecture](docs/v1/architecture.md)
- [Scheduler and decision model](docs/v1/scheduler.md)
- [Execution backends](docs/v1/execution-backends.md)
- [API reference](docs/v1/api.md)
- [Configuration reference](docs/v1/configuration.md)
- [Benchmark results](docs/v1/benchmark-results.md)
- [Validation guide](docs/v1/validation.md)
- [Known limitations](docs/v1/known-limitations.md)

## Scope of v1

v1 is centered on adaptive index-range `parallel_for`. Additional algorithms, OpenMP, GPU execution, NUMA policy, and more advanced oneTBB partitioner selection are intentionally future work.

## License

SmartParallel is distributed under the terms in [LICENSE](LICENSE).

## CMake presets and installation

Routine builds use named presets instead of long option lists:

```text
cmake --preset release
cmake --build --preset release
```

Other presets are `debug`, `examples`, `validation`, `benchmarks`, and `all`.
Install the release package with:

```text
cmake --install build/release --prefix path/to/install
```

Downstream CMake projects consume the exported target as
`SmartParallel::smart_parallel`. See
[`docs/v1/build-and-validation.md`](docs/v1/build-and-validation.md) for the full
preset, validation, installation, and `find_package` workflow.


## Platform support

SmartParallel is designed as a portable C++17 library.

Currently validated:

| Platform | Compiler | Status |
|----------|----------|--------|
| Windows | MSVC 2022 | ✅ Fully tested |

Planned validation:

- GCC
- Clang
- AppleClang

Cross-platform continuous integration is planned for a future release.
