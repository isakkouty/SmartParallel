# Phase 6C: Uncertainty-aware residual correction

The analytical model remains SmartParallel's cold-start predictor. Phase 6C
learns only the residual error left by that model.

Exact plan history is weighted by sample count, runtime stability, and observed
prediction quality. Similar workloads may contribute a much smaller, bounded
signal when exact evidence is weak. High uncertainty converges to a neutral
factor of 1.0, preventing sparse or noisy history from replacing the analytical
model.

Candidate diagnostics expose the correction factor, confidence, uncertainty,
sample count, history weight, and whether exact or similarity evidence was used.
