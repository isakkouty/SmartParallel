# Reproducing the v1.4 algorithm benchmark

The v1.4 benchmark is a manual Release validation workflow for Windows/MSVC with oneTBB enabled. It configures the project, builds the library and benchmark target, runs the complete deterministic CTest suite, executes the five-mode algorithm matrix, and validates that all expected APIs and oneTBB rows are present.

## Requirements

- Windows 10 or later;
- Visual Studio 2022 with the Desktop development with C++ workload;
- CMake 3.20 or newer in `PATH`;
- a vcpkg checkout containing oneTBB;
- Python 3 with `pandas`, `numpy`, and `matplotlib` only when regenerating documentation figures.

## Release-quality benchmark

From a normal Command Prompt at the repository root:

```bat
set "VCPKG_ROOT=D:\Tools\vcpkg" && scripts\validation\run_v14_algorithm_release_validation.bat 31
```

The first argument is the repetition count. Use an odd value so the median is an observed sample. Seven repetitions are suitable for development; 31 or more are recommended for a publication run on an otherwise idle physical machine.

To rerun tests and benchmarks without reconfiguring or rebuilding an existing successful build directory:

```bat
scripts\validation\run_v14_algorithm_release_validation.bat 31 reuse
```

## Generated CSVs

```text
validation/output/v1.4.0_parallel_algorithms.csv
validation/output/v1.4.0_parallel_algorithms_raw.csv
```

The summary CSV contains one row per algorithm/mode pair. The raw CSV contains every timed repetition. Both include correctness and backend-authentication fields.

## Regenerating the report assets

After producing or replacing the CSVs:

```bat
python tools\plot_v14_algorithm_results.py
```

The script validates the source data before updating `docs/v1.4/assets/benchmarks/` with PNG/SVG figures, generated Markdown, aggregate metrics, and SHA-256 hashes of the input CSVs.

Optional explicit paths are supported:

```bat
python tools\plot_v14_algorithm_results.py ^
  --summary validation\output\v1.4.0_parallel_algorithms.csv ^
  --raw validation\output\v1.4.0_parallel_algorithms_raw.csv ^
  --output docs\v1.4\assets\benchmarks
```

## Measurement discipline

For a release run:

- use a physical machine rather than a shared CI runner or virtual machine;
- close background compilation, indexing, browser, and synchronization workloads;
- keep the power plan and CPU thermal state stable;
- avoid comparing results produced with different repetition counts or build types without saying so;
- preserve both CSV files, not only screenshots or summary values;
- record compiler, CPU, memory, operating-system, and oneTBB/vcpkg revisions in the release or pull-request notes.

The checked-in v1.4 snapshot uses seven repetitions and is retained as acceptance evidence for the hot-dispatch correction. It should not be presented as a universal hardware ranking.
