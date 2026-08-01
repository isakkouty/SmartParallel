# Repository scripts

Root-level batch files are intentionally avoided. Scripts resolve the repository root from their own path and can be launched from any working directory unless their help text says otherwise.

## Complete v1.5 adaptive-route publication

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v15_adaptive_routes_release_validation.bat 31
```

This opt-in workflow installs the `vision-opencv` vcpkg feature, builds the complete deterministic suite plus the v1.5 vision validation and benchmark, runs CTest, records cold/learning/deployment/steady-state/dispatch phases, validates exact output and route authentication, generates summary CSV, learning telemetry, Markdown, source evidence, and six SVG figures, then creates a timestamped ZIP under `validation/output/v1.5.0_adaptive_routes/`.

Use `scripts\validation\run_v15_adaptive_routes_development.bat` for the seven-repetition development matrix.

After a successful publication run, publish its accepted assets into the documentation tree with:

```bat
py -3 tools\publish_v15_benchmark_docs.py validation\output\v1.5.0_adaptive_routes\publication_<timestamp>
```

## Complete v1.4 algorithm validation

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v14_algorithm_release_validation.bat 7
```

This configures an MSVC Release build with required oneTBB, builds the complete deterministic test suite and v1.4 benchmark target, runs CTest, then benchmarks all fourteen APIs in sequential, automatic, ThreadPool, StaticThread, and oneTBB modes.

## Retained v1.1 real-world validation

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

This builds and validates the retained OpenCV, LZ4, BVH, and particle integration suite.

## Focused benchmark scripts

| Script | Purpose |
|---|---|
| `scripts/benchmarks/build_real_world_benchmarks.bat` | Build the retained real-world benchmark targets. |
| `scripts/benchmarks/run_real_world_development.bat` | Faster retained real-world matrix. |
| `scripts/benchmarks/run_real_world_integration.bat` | Run one integration/preset/mode/backend selection. |
| `scripts/benchmarks/run_real_world_trace.bat` | Run diagnostic trace cases. |
| `scripts/benchmarks/run_real_world_backend_comparison.bat` | Compare supported scheduler backends. |
| `scripts/benchmarks/run_all_benchmarks.bat` | Run the historical v1.0 benchmark workflow. |

See [v1.5 benchmark reproduction](../docs/v1.5/benchmark-reproduction.md) for the current release workflow.

## Complete v1.6 scientific-foundations publication

Windows with required oneTBB and OpenCV:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v16_scientific_foundations_release_validation.bat 31
```

Linux/macOS:

```sh
scripts/validation/run_v16_scientific_foundations_release_validation.sh 21 full
```

The workflow runs regression, scientific benchmarks, documentation validation, installed consumers, an explicit no-oneTBB matrix, and supported sanitizer/compiler checks. Before archiving it records sanitized environment metadata, preserves CTest logs, removes install and consumer build trees, rejects dependency binaries, and emits a deterministic source-hash manifest.
