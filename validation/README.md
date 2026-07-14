# Predictive Model Validation

The validation tools measure whether SmartParallel's predictive cost model matches real execution. They are separate from unit tests: every available candidate plan is executed, then predicted and measured runtimes are compared.

## Validation programs

### Calibration suite

`smartparallel_prediction_validation` retains the original workload families used while developing machine calibration. It is useful for regression tracking, but it must not be treated as proof of generalization.

### Holdout suite

`smartparallel_prediction_holdout_validation` uses workload families that were not part of the original calibration suite:

- memory-streaming transforms;
- cache-resident transforms;
- branch-heavy loops;
- tiny but expensive workloads;
- clustered expensive regions;
- pointer-chasing/sparse-like access;
- large-record traversal;
- Cartesian pair workloads.

The holdout suite reports both exact winner accuracy and whether the predicted plan finished within 3% of the measured best. The near-optimal metric prevents harmless timer noise from being counted as a major decision failure.

## Build

From the project root in an x64 MSVC developer terminal:

```bat
cmake -S . -B build ^
  -G "NMake Makefiles" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER=cl ^
  -DTBB_DIR="%CD%\vcpkg_installed\x64-windows\share\tbb" ^
  -DSMARTPARALLEL_BUILD_VALIDATION=ON

cmake --build build
```

On Windows, CMake copies the oneTBB runtime beside both validation executables.

## Run

```bat
build\smartparallel_prediction_validation.exe
build\smartparallel_prediction_holdout_validation.exe
py validation\plot_validation.py
```

## Outputs

Calibration suite:

- `validation/output/prediction_candidates.csv`
- `validation/output/prediction_summary.csv`
- `validation/output/prediction_metrics.csv`

Holdout suite:

- `validation/output/holdout_candidates.csv`
- `validation/output/holdout_summary.csv`
- `validation/output/holdout_metrics.csv`
- `validation/output/holdout_suite_metrics.csv`

Images are written to `validation/images/` with `calibration_` and `holdout_` prefixes.

## Metrics

- **Exact winner accuracy:** predicted plan family exactly matches the measured fastest family.
- **Within-3% accuracy:** predicted plan is no more than 3% slower than the measured fastest plan.
- **Prediction error:** absolute difference between predicted and measured runtime.
- **Decision regret:** runtime cost of using the predicted plan instead of the measured fastest plan.

Phase 2 acceptance is based primarily on holdout regret and near-optimal accuracy, not on calibration-suite accuracy alone.

## Phase 4 ranking-evolution validation

Build with `SMARTPARALLEL_BUILD_VALIDATION=ON`, then run:

```bat
build\smartparallel_ranking_evolution_validation.exe
```

This validator repeatedly measures every candidate for four representative
workloads. After each round it records those measurements into the in-process
experience database, predicts again, and reports whether historical ranking
reduces selected-plan regret. It also saves, clears, reloads, and reuses the
experience database to validate persistence across process-style restarts.

Outputs:

- `validation/output/ranking_evolution_candidates.csv`
- `validation/output/ranking_evolution_summary.csv`
- `validation/output/ranking_evolution_metrics.csv`
- `validation/output/ranking_evolution_experience.db`
