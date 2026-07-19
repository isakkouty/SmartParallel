# Windows scripts

Root-level batch files are intentionally avoided. These launchers determine the repository root from their own path and may be run from any working directory.

| Script | Purpose |
|---|---|
| `benchmarks/run_all_benchmarks.bat` | Complete OpenCV, scientific, and decision-quality workflow |
| `benchmarks/run_opencv_benchmarks.bat` | OpenCV benchmark suite |
| `validation/run_v1_phase1.bat` | Phase 1 build, data generation, audit, and ranker workflow |

The lower-level benchmark-specific scripts remain beside their suites because they are implementation details of those suites.
