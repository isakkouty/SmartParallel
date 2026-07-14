# Phase 7 Step 4 — Explicit Confidence Model

Step 4 gives every candidate an explicit confidence estimate instead of treating all predictions as equally trustworthy.

The model combines profiling quality, workload-family certainty, residual-correction evidence, historical evidence quality, similarity quality, and agreement between analytical and learned ranking. A weighted geometric mean prevents a single weak signal from being hidden by several strong ones.

Low-confidence candidates receive a small bounded ranking penalty. The penalty does not replace the runtime model; it only breaks close decisions in favor of candidates supported by better evidence. The final confidence also includes the score margin between the best and second-best candidates, so close races are reported as less certain.

Diagnostics are exposed in `PlanCostEstimate` through the `model_*_confidence`, `model_uncertainty_penalty`, and `decision_margin_confidence` fields.
