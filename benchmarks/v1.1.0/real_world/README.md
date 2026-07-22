# SmartParallel v1.1 Real-World Integration Suite

This suite compares SmartParallel automatic scheduling with explicit sequential,
backend-constrained, manually parallel, nested-frontier, and flattened strategies
on four deterministic CPU workloads. The optimized build additionally exercises
frontier-sealed descendant direct execution, bounded root-plan memoization,
analytical exactly-once cold learning, bounded backend calibration, and weighted
OpenCV work decomposition.

The integrations are benchmark-only targets. OpenCV and LZ4 are not linked into
or installed with the SmartParallel core library.

## Integrations

| Integration | Scheduling challenge | Presets |
|---|---|---|
| OpenCV image pipeline | Images versus image tiles; controlled third-party threading | `tiny`, `one_large`, `few_large`, `many_medium`, `thousands_small`, `mixed_sizes` |
| LZ4 batch round trip | Per-block overhead, size skew, compressibility, bandwidth | `tiny_compressible`, `tiny_incompressible`, `medium_compressible`, `medium_incompressible`, `large_blocks`, `mixed_sizes` |
| BVH construction | Recursive irregular work and branch imbalance | `small_uniform`, `uniform`, `clustered`, `highly_unbalanced`, `mixed_distribution`, `large_uniform` |
| Particle simulation | Repeated callsites, changing density/count, plan reuse | `tiny`, `uniform`, `clustered`, `sparse`, `sudden_count_change`, `gradual_count_increase`, `moving_clusters` |

All input data is generated before timed execution from the recorded `--seed`.
No download or external dataset is required.

## Dependencies on Windows

Required tools:

- Visual Studio 2022 with **Desktop development with C++**
- CMake 3.20 or newer
- vcpkg
- PowerShell, included with supported Windows versions

Set `VCPKG_ROOT` to the vcpkg installation:

```bat
set VCPKG_ROOT=D:\Tools\vcpkg
```

The provided build script uses vcpkg manifest mode and the
`real-world-benchmarks` manifest feature. During CMake configuration, vcpkg
installs these packages for `x64-windows` when they are missing:

```text
tbb
opencv4
lz4
```

No manual package command is normally necessary. A manual fallback is:

```bat
%VCPKG_ROOT%\vcpkg.exe install tbb:x64-windows opencv4:x64-windows lz4:x64-windows
```

## Build and run

Build all integrations and run CTest:

```bat
scripts\benchmarks\build_real_world_benchmarks.bat
```

Run a 15-repetition development set:

```bat
scripts\benchmarks\run_real_world_development.bat 15
```

Run the complete 31-repetition suite, all available modes and backends, with
trace export and comparison. The command stops on the first failure:

```bat
scripts\benchmarks\run_real_world_complete.bat 31
```

Equivalent named comparison command:

```bat
scripts\benchmarks\run_real_world_backend_comparison.bat 31
```

Run one integration:

```bat
scripts\benchmarks\run_real_world_integration.bat opencv 15 mixed_sizes core all trace
scripts\benchmarks\run_real_world_integration.bat lz4 15 mixed_sizes all all trace
scripts\benchmarks\run_real_world_integration.bat bvh 15 highly_unbalanced all all trace
scripts\benchmarks\run_real_world_integration.bat particles 15 sudden_count_change all all trace
```

The positional arguments are:

```text
integration repetitions preset mode backend [trace]
```

## Direct executable CLI

Each executable supports:

```text
--preset all|name[,name...]
--mode all|core|sequential|manual|smart_auto|smart_forced_sequential|
       smart_forced|outer_only|inner_only|all_levels|flattened[,..]
--backend all|thread_pool|static_thread|one_tbb[,..]
--repetitions N
--warmups N
--workers N
--seed N
--output-dir PATH
--trace
--list-presets
```

Unavailable optional backends are skipped only when `--backend all` is used.
Explicitly requesting an unavailable backend returns an error. The complete
Windows build requires oneTBB, so it cannot silently produce a non-TBB result.

## Execution-mode semantics

| Mode | Meaning |
|---|---|
| `sequential` | Direct sequential reference |
| `manual_<backend>` | One manually selected coarse parallel level |
| `smart_auto` / `smart_auto_frontier` | SmartParallel selects sequential/parallel, backend, and nested frontier |
| `smart_forced_sequential` | SmartParallel-compatible forced sequential reference |
| `smart_forced_<backend>` | Automatic strategy decision constrained to one backend |
| `outer_only_<backend>` | Parallelize only the outer natural level |
| `inner_only_<backend>` | Parallelize only the inner natural level |
| `all_levels_<backend>` | Request coordinated parallel execution at every natural level |
| `flattened_<backend>` | Flatten independent work into one explicit scheduling region where meaningful |

LZ4 is a flat independent-block workload, so nested-only modes are not emitted.

## Optimization behavior under test

- Descendants below a sealed parallel frontier take a direct sequential path in
  untraced stable execution.
- Repeated root-local plan resolutions may use the bounded session memo.
- Large cold roots may learn from one analytical exactly-once execution.
- Real-world automatic roots enable bounded ThreadPool/oneTBB calibration after
  plan stability; the core library default remains disabled.
- OpenCV outer work is deterministic, largest-first, and splits only oversized
  images at existing tile boundaries.

## Benchmark phases

For every preset and mode, the runner separates:

1. deterministic setup and reference generation,
2. one cold exactly-once execution,
3. untimed warm-up executions,
4. timed warm executions,
5. correctness validation outside the timed region,
6. one post-measurement diagnostic trace execution,
7. CSV export.

OpenCV's internal worker pool is forced to one thread. SmartParallel therefore
owns the parallelism in the compared SmartParallel paths.

## Correctness

- **OpenCV:** exact per-tile output hashes from resize, grayscale, blur, Canny,
  threshold, and morphology stages.
- **LZ4:** every block executes exactly once; compressed sizes are valid; every
  block decompresses byte-for-byte to the original input.
- **BVH:** every primitive appears once; leaf limits and bounds hold; traversal
  counts match brute force; nested exception/cancellation recovery is checked.
- **Particles:** particle count is preserved; all state is finite; final state
  matches the deterministic sequential reference within `1e-11`; quantized
  checksums match.

A correctness failure marks the mode invalid, preserves diagnostics, and returns
failure. The comparison script refuses to rank incorrect rows.

## Output

Files are written to `validation\output\real_world`:

```text
v1.1.0_real_world_<integration>_raw.csv
v1.1.0_real_world_<integration>_summary.csv
v1.1.0_real_world_<integration>_trace.csv
v1.1.0_real_world_<integration>_environment.csv
v1.1.0_real_world_comparison.csv
v1.1.0_real_world_auto_analysis.csv
v1.1.0_real_world_analysis.md
```

Trace export is representative and bounded to 1,024 records per
preset/mode diagnostic execution. Full in-process trace retention remains
subject to SmartParallel's own bounded trace policy.

See [CSV_SCHEMA.md](CSV_SCHEMA.md) for field definitions.

## Build isolation

The core build remains valid with every real-world option disabled:

```bat
cmake -S . -B build\core_release -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=cl ^
  -DSMARTPARALLEL_BUILD_REAL_WORLD_BENCHMARKS=OFF
cmake --build build\core_release
```

OpenCV and LZ4 are benchmark-only dependencies and are not exported by the
installed SmartParallel package.

## Limitations

- CPU utilization is based on process CPU time, not hardware counters. The CSV
  also reports process CPU equivalent cores for direct interpretation.
- Peak memory is process peak working set, not per-mode allocation accounting.
- Inputs are deterministic generated corpora; disk I/O is intentionally excluded.
- The BVH is a controlled median-split builder, not a replacement for Embree.
- The particle workload is a scheduling benchmark, not a full physics engine.
- The complete all-mode matrix is deliberately large and can take substantially
  longer than the development command.
