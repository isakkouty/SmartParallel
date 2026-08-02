# SmartParallel v1.8 — Release validation status

## Current state

- Linux/GCC publication evidence: **accepted**.
- Linux exact-source-archive build and test: **required and recorded by the release workflow**.
- Clang warnings-as-errors and Linux sanitizer matrices: **required by the full Linux workflow**.
- Windows/MSVC publication evidence: **pending a separate run on the intended Windows release host**.
- Final cross-platform comparison: **pending Windows raw evidence**.

The source tree does not claim Windows acceptance without Windows-produced logs and raw measurements.

## Retained Linux evidence

The accepted Linux benchmark directory contains 31 paired repetitions, 2,232 raw records, 10,000-sample paired bootstrap intervals at 95% confidence, fourteen publication figures, and a plot manifest tied to the raw-data SHA-256. All nine mandatory governance gates passed. Performance ratios that favored the ungoverned control remain reported as failures rather than being hidden.

## Windows command still required

Run from a Visual Studio 2022 x64 Developer Command Prompt:

```bat
scripts\validation\run_v18_governed_execution_release_validation.bat 31 full
```

The Windows run must retain:

- complete MSVC test results;
- no-oneTBB/no-OpenCV results;
- oneTBB/OpenCV focused results where available;
- installed consumer results;
- v1.6, v1.7, and v1.8 benchmark evidence;
- Windows SVG figures and plot manifest;
- deterministic source-ZIP checksum;
- exact-ZIP build, tests, documentation validation, and benchmark smoke.

Final release acceptance remains blocked until that Windows evidence is reviewed and copied into a Windows-specific directory under `docs/v1.8/assets/benchmarks/`.
