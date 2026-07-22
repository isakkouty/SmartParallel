# SmartParallel v1.1 real-world integration suite

This directory contains the current release benchmarks for automatic and nested scheduling.

## Integrations

| Integration | Workload |
|---|---|
| OpenCV | Deterministic image pipelines with image-level and tile-level work. |
| LZ4 | Independent compression/decompression blocks with varied size and compressibility. |
| BVH | Custom median-split recursive construction over deterministic primitive distributions. |
| Particles | Custom uniform-grid neighbor-force simulation across repeated dynamic frames. |

OpenCV and LZ4 use their external libraries. BVH and particles are custom realistic benchmark implementations and should not be described as integrations into external engines.

## Dependencies

The root `vcpkg.json` supplies oneTBB, OpenCV, and LZ4. On Windows:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg"
```

## Complete run

From the repository root:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

The script performs a clean MSVC/NMake Release configuration, vcpkg dependency resolution, build, CTest, all integration runs, trace/authenticity checks, comparison generation, and Markdown analysis.

## Focused runs

```bat
scripts\benchmarks\run_real_world_development.bat 15
scripts\benchmarks\run_real_world_integration.bat particles 31 all all all trace
scripts\benchmarks\run_real_world_trace.bat
scripts\benchmarks\run_real_world_backend_comparison.bat
```

The integration command accepts:

```text
run_real_world_integration.bat <opencv|lz4|bvh|particles> [repetitions] [preset] [mode] [backend] [trace]
```

## Comparison modes

The complete matrix includes sequential, manual backend, SmartParallel automatic/forced, outer-only, inner-only, all-level, and flattened execution where the workload structure supports them. Every correctness-valid mode remains in the comparison, including cases where automatic is not the fastest.

## Benchmark phases

- cold execution
- three warm-up executions
- 31 timed repetitions in the release command
- representative structured trace export

Backend calibration and profile revalidation may learn during warm-up and are frozen during timed repetitions. Production defaults remain adaptive.

## Correctness

- OpenCV: deterministic hashes and exact-once counters.
- LZ4: round-trip byte equality, exact-once counters, and checksum.
- BVH: structure, bounds, occurrence, traversal/reference, and checksum validation.
- Particles: final-state comparison to a sequential reference with tolerance `1e-11` and checksum.

The comparator also requires backend-authenticated traces and a root concurrency/lease maximum no greater than four for the recorded release configuration.

## Output

Files are written under `validation/output/real_world/`:

```text
v1.1.0_real_world_<integration>_raw.csv
v1.1.0_real_world_<integration>_summary.csv
v1.1.0_real_world_<integration>_trace.csv
v1.1.0_real_world_<integration>_environment.csv
v1.1.0_real_world_comparison.csv
v1.1.0_real_world_auto_analysis.csv
v1.1.0_real_world_analysis.md
```

See [`CSV_SCHEMA.md`](CSV_SCHEMA.md), the [public report](../../../docs/v1.1/benchmarks.md), and [reproduction guide](../../../docs/v1.1/benchmark-reproduction.md).

## Plot generation

```text
python tools/plot_real_world_results.py
```

Generated release figures are stored in `docs/v1.1/assets/benchmarks/`.
