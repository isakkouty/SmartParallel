# SmartParallel

SmartParallel is a C++17 decision engine for selecting a safe parallel execution plan from measured workload context.

## Run the current validation

Open a Visual Studio Developer Command Prompt and run:

```bat
run_v1_phase1.bat
```

This is the repository's only launcher. It builds the library, runs the decision primitives, regenerates calibration and holdout candidate data, audits production-safe features, and evaluates the utility learner in shadow mode.

## Current policy

Runtime prediction is not part of the production decision path. Historical runtime-prediction fields remain only in offline validation files as a frozen comparison baseline. A utility model is promoted only after it has enough independent workload groups and beats the baseline on regret without increasing catastrophic decisions.

## Utility-learning gate

`run_v1_phase1.bat` is the single supported validation entry point. It builds and tests the library, regenerates calibration and holdout datasets, audits production-safe DecisionContext fields, and checks whether enough independent workloads exist to train a utility model.

The learner is not fitted below 100 calibration workloads. Legacy runtime estimates appear only as an offline benchmark and never control production decisions.

## Calibration suite

The Phase 1 launcher generates exactly 100 calibration workload groups: ten workload families across ten scales. The 17 holdout workloads remain unchanged and are never used for fitting. Utility-model training starts only when all 100 calibration groups are present; incomplete runs stay untrained.

### Opt-in V1 hybrid dispatch

```cpp
smart::global_config().enable_utility_model_runtime = true;
smart::global_config().utility_model_file_path = "smartparallel_utility_model.spm";
smart::parallel_for(0, count, work);
```

Only a validated `PROMOTED` model may override the analytical policy. All other
states—including the current Phase 1 `SHADOW_ONLY` model—fall back safely.

## OpenCV integration Test 1

The first real-project benchmark is available under
`integrations/opencv/test1_threshold.cpp`. It compares the same binary-threshold
kernel under a sequential loop, OpenCV `cv::parallel_for_`, and
SmartParallel `smart::parallel_for`, with `cv::threshold` as a specialized
correctness/performance reference. Run `run_v1_opencv_test1.bat`; results are
written to `validation/output/opencv_test1_threshold.csv`.
