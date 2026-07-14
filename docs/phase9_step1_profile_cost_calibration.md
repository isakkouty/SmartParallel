# Phase 9 Step 1: robust profile-cost calibration

Validation showed a repeated pattern: SmartParallel often chose a reasonable plan while predicting a runtime far above the measured value. The first Phase 9 refinement therefore targets the full-workload extrapolation that feeds every candidate.

The profiler already exposes robust steady-state, trimmed, and median costs, plus an arithmetic mean. Previously, highly variable samples could give the mean too much authority, allowing a few slow observations to be multiplied across the complete workload. This step keeps the robust estimate authoritative, caps the mean-to-baseline ratio, and reduces mean influence when profiling is sparse, budget-limited, or unreliable.

This is deliberately a narrow correction. It does not change workload classification, candidate generation, historical ranking, persistence, or execution control. The validation suite should now show whether the large vertical over-prediction outliers shrink before we add further family-specific scaling.
