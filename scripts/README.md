# Windows scripts

Root-level batch files are intentionally avoided. These launchers determine the repository root from their own path and may be run from any working directory.

| Script | Purpose |
|---|---|
| `scripts/examples/run_nested_context.bat` | Build and run the lightweight nested execution-context example |
| `scripts/benchmarks/run_all_benchmarks.bat` | Complete OpenCV, scientific, and decision-quality workflow |
| `scripts/benchmarks/run_opencv_benchmarks.bat` | OpenCV benchmark suite |
| `scripts/validation/run_v1_phase1.bat` | Phase 1 build, data generation, audit, and ranker workflow |

The lower-level benchmark-specific scripts remain beside their suites because they are implementation details of those suites.

## Development examples

Use the focused example scripts while developing a feature before adding formal tests or benchmarks. Each script configures a clean release build under `build\<example-name>`, builds only the required example target, and runs it. On Windows, the script initializes the Visual Studio C++ environment automatically when needed.

```bat
scripts\examples\run_nested_context.bat
```

- `examples\run_budget_aware_nested_parallelism.bat` configures, builds, and runs the Step 6 budget-aware nested parallelism example.

- `scripts\examples\run_scheduler_visible_work_chunks.bat` configures, builds,
  and runs the revised Step 8 scheduler-visible work chunk validation.
