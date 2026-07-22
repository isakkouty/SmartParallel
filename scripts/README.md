# Repository scripts

Root-level batch files are intentionally avoided. Scripts resolve the repository root from their own path and can be launched from any working directory unless their help text says otherwise.

## Complete v1.1 real-world validation

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\benchmarks\run_real_world_complete.bat 31
```

This is the authoritative one-line release workflow. It builds the MSVC/NMake Release configuration, resolves vcpkg dependencies, runs CTest and all real-world benchmark modes, validates correctness and backend authenticity, and writes results to `validation/output/real_world/`.

## Focused benchmark scripts

| Script | Purpose |
|---|---|
| `scripts/benchmarks/build_real_world_benchmarks.bat` | Build the real-world benchmark targets. |
| `scripts/benchmarks/run_real_world_development.bat` | Faster development matrix. |
| `scripts/benchmarks/run_real_world_integration.bat` | Run one integration/preset/mode/backend selection. |
| `scripts/benchmarks/run_real_world_trace.bat` | Run diagnostic trace cases. |
| `scripts/benchmarks/run_real_world_backend_comparison.bat` | Compare supported backends. |
| `scripts/benchmarks/run_all_benchmarks.bat` | Run the historical v1.0 benchmark workflow. |

## Validation scripts

`validation/` launchers exercise nested release gates, cross-backend comparisons, and the original v1 decision-quality workflow.

## Example scripts

`scripts/examples/` contains focused build-and-run launchers for contexts, budgets, backend contracts, helping, exceptions, deep nesting, mixed backends, and performance regression checks. These are engineering examples rather than the public benchmark report.

See [benchmark reproduction](../docs/v1.1/benchmark-reproduction.md) for the documented release commands.
