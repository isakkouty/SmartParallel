# SmartParallel v1.7 benchmark reproduction

The supported release scripts build, test, benchmark, install, consume, replay, package, and rebuild the exact source archive.

## Full publication commands

Windows, from a Visual Studio Developer Command Prompt:

```bat
scripts\validation\run_v17_reproducible_runtime_release_validation.bat 31 full
```

Linux/macOS:

```sh
SMARTPARALLEL_BUILD_JOBS=2 sh scripts/validation/run_v17_reproducible_runtime_release_validation.sh 31 full
```

Use `smoke` for a shorter correctness-and-packaging workflow. Short smoke samples intentionally do not receive publication-grade statistical acceptance.

## Prerequisites

- CMake 3.20 or newer;
- a C++17 compiler;
- Python 3;
- oneTBB when the TBB-enabled matrix is requested;
- OpenCV only for the optional provider matrix;
- on Windows, a Developer Command Prompt and a valid vcpkg toolchain environment.

## Direct benchmark invocation

After configuring the v1.7 release build:

```sh
build/v17_reproducible_release/benchmarks/v1.7.0/smartparallel_v170_reproducible_runtime_benchmarks \
  validation/output/v1.7-local 31
python3 tools/analyze_v17_reproducible_runtime.py \
  validation/output/v1.7-local/raw.csv \
  validation/output/v1.7-local/analysis
```

Multi-config generators may place the executable under a `Release` directory. The release script resolves the platform-specific location automatically.

## Generated v1.7 evidence

The benchmark directory contains:

- `raw.csv` — repetition-level samples and authentication fields;
- `summary.csv` — generated medians and sample counts;
- `metrics.json` — objective gates, point estimates, and bootstrap intervals;
- `report.md` — human-readable analysis;
- nine SVG figures;
- Candidate and Approved profiles used by benchmark cases;
- profile databases containing 1, 10, 100, and 1,000 entries.

The release publication additionally contains CTest logs, install-tree consumers, CLI calibration/approval/replay evidence, dependency matrices, documentation logs, the generated source ZIP, its SHA-256, and exact-ZIP rebuild evidence.

## Reviewing a run

A publishable run must show:

- all correctness and authentication tests passing;
- every v1.7 objective marked `PASS` or `INCONCLUSIVE-PASS`;
- no statistically credible objective failure;
- byte-identical replay manifests and unchanged Approved profile bytes;
- all retained v1.6 gates passing;
- source-manifest and exact-ZIP checks passing;
- no missing runtime dependency in installed tools or consumers.

See [benchmark methodology](benchmark-methodology.md), [validation](validation.md), and the [full reproduction guide](reproduction-guide.md).
