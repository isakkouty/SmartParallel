# SmartParallel V1 — Prediction Feedback and Persistence

This increment connects Phase 2 predictions to measured execution outcomes.

## Runtime flow

1. SmartParallel produces candidate cost estimates.
2. The selected plan executes.
3. The runtime finds the prediction for that exact plan.
4. Actual execution time and predicted execution time are recorded together.
5. Repeated observations produce a bounded runtime-correction factor.
6. Future predictions for the same workload/function/hardware fingerprint may be calibrated.

## Persistence

Persistence is opt-in. Configure:

```cpp
smart::global_config().enable_experience = true;
smart::global_config().enable_experience_persistence = true;
smart::global_config().experience_file_path = "my_application.smartparallel.db";
smart::global_config().experience_autosave_interval = 16;
```

The database is loaded lazily on first use. It is autosaved after the configured number of new records. Applications can force a save with `smart::flush_experience()` or use `smart::save_experience(path)` and `smart::load_experience(path)` explicitly.

## Stored data

The file stores compact fingerprints and per-plan aggregate measurements. User containers, callback values, and source code are never stored.

Each entry includes actual runtime statistics, prediction error, average absolute prediction error, and an average actual/predicted runtime correction.

## Safety

Prediction calibration changes cost estimates only. Predictive execution remains controlled by `enable_predictive_decisions`, which is still disabled by default.
